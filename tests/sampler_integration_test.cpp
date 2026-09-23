#include "../src/perf/sample_collector.hpp"
#include "perflens/perf_sampler.hpp"
#include "perflens/process_runner.hpp"
#include "perflens/symbolizer.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <iostream>
#include <sched.h>
#include <set>
#include <sys/wait.h>
#include <system_error>
#include <thread>

using namespace std::chrono_literals;

namespace {

std::vector<int> testCpus() {
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof(allowed), &allowed) == -1) {
        throw std::system_error(errno, std::generic_category(), "test affinity");
    }
    std::vector<int> cpus;
    for (int cpu = 0; cpu < CPU_SETSIZE && cpus.size() < 2; ++cpu) {
        if (CPU_ISSET(cpu, &allowed)) {
            cpus.push_back(cpu);
        }
    }
    return cpus;
}

void requireSampling(const std::vector<int>& cpus) {
    try {
        perflens::PerfSampler probe{getpid(), cpus, 99};
        probe.stop();
    } catch (const std::system_error& error) {
        const int code = error.code().value();
        if (code == EACCES || code == EPERM || code == ENOENT || code == ENODEV || code == EOPNOTSUPP ||
            code == ENOSYS) {
            SKIP("CPU sampling unavailable: " << error.what());
        }
        throw;
    }
}

class Target {
  public:
    Target(const std::vector<int>& cpus, const bool threaded) {
        int descriptors[2];
        if (pipe(descriptors) == -1) {
            throw std::system_error(errno, std::generic_category(), "test pipe");
        }
        perflens::detail::UniqueFd readEnd{descriptors[0]};
        release_ = perflens::detail::UniqueFd{descriptors[1]};
        cpu_set_t mask;
        CPU_ZERO(&mask);
        for (const int cpu : cpus) {
            CPU_SET(cpu, &mask);
        }
        pid = fork();
        if (pid == -1) {
            throw std::system_error(errno, std::generic_category(), "test fork");
        }
        if (pid == 0) {
            close(descriptors[1]);
            char release;
            if (sched_setaffinity(0, sizeof(mask), &mask) == -1 || read(descriptors[0], &release, 1) != 1) {
                _exit(126);
            }
            close(descriptors[0]);
            if (threaded) {
                execl("./threaded_hotspot", "threaded_hotspot", "4", "200000000", nullptr);
            } else {
                execl("./cpu_hotspot", "cpu_hotspot", "candidate", nullptr);
            }
            _exit(127);
        }
    }
    ~Target() {
        if (pid > 0) {
            kill(pid, SIGKILL);
            while (waitpid(pid, nullptr, 0) == -1 && errno == EINTR) {
            }
        }
    }
    void release() {
        const char value = 1;
        REQUIRE(write(release_.get(), &value, 1) == 1);
    }
    pid_t pid{-1};

  private:
    perflens::detail::UniqueFd release_;
};

bool hasWorker(const perflens::CpuProfile& profile) {
    return std::any_of(profile.functions.begin(), profile.functions.end(), [](const auto& function) {
        return function.function.find("workerHotspot") != std::string::npos;
    });
}

}

TEST_CASE("inherited sampling records worker TIDs and symbols", "[hardware]") {
    auto cpus = testCpus();
    SECTION("one CPU") {
        cpus.resize(1);
    }
    SECTION("allowed CPUs") {}
    requireSampling(cpus);
    Target target{cpus, true};
    const auto targetPid = target.pid;
    perflens::PerfSampler sampler{targetPid, cpus, 99};
    sampler.start();
    target.release();
    std::vector<perflens::MemoryMapping> mappings;
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    int status{};
    while (true) {
        const auto current = perflens::readProcMaps(targetPid);
        if (!current.empty()) {
            mappings = current;
        }
        const auto waited = waitpid(targetPid, &status, WNOHANG);
        if (waited == targetPid) {
            target.pid = -1;
            break;
        }
        REQUIRE((waited == 0 || (waited == -1 && errno == EINTR)));
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(5ms);
    }
    sampler.stop();
    sampler.stop();
    REQUIRE(WIFEXITED(status));
    REQUIRE(WEXITSTATUS(status) == 0);
    std::set<std::uint32_t> workers;
    for (const auto& sample : sampler.samples()) {
        CHECK(sample.processId == static_cast<std::uint32_t>(targetPid));
        if (sample.threadId != static_cast<std::uint32_t>(targetPid)) {
            workers.insert(sample.threadId);
        }
    }
    CHECK(workers.size() >= 2);
    CHECK(std::is_sorted(sampler.samples().begin(),
                         sampler.samples().end(),
                         [](const auto& a, const auto& b) { return a.time < b.time; }));
    perflens::CpuProfile profile;
    profile.functions = perflens::symbolizeSamples(sampler.samples(), mappings);
    CHECK(hasWorker(profile));
    std::cout << "worker TIDs=" << workers.size() << " samples=" << sampler.samples().size()
              << " lost=" << sampler.lostSamples() << '\n';
    CHECK(sampler.lostSamples() == 0);
}

TEST_CASE("sampling survives target lifecycle outcomes", "[hardware]") {
    const auto cpus = testCpus();
    requireSampling({cpus.front()});
    perflens::RunOptions options;
    options.collectPerfCounters = false;
    options.requirePerf = true;
    options.samplingFrequency = 199;
    options.cpu = static_cast<unsigned int>(cpus.front());
    options.timeout = 5s;
    options.terminationGrace = 20ms;
    SECTION("single thread") {
        const auto result = perflens::ProcessRunner{}.run({"./cpu_hotspot", "candidate"}, options);
        REQUIRE(result.succeeded());
        REQUIRE(result.result.cpuProfile.available);
        CHECK_FALSE(result.result.cpuProfile.functions.empty());
    }
    SECTION("timeout") {
        options.timeout = 50ms;
        CHECK(perflens::ProcessRunner{}.run({"/bin/sleep", "2"}, options).reason ==
              perflens::TerminationReason::timedOut);
    }
    SECTION("target signal") {
        CHECK(perflens::ProcessRunner{}.run({"/bin/sh", "-c", "kill -TERM $$"}, options).reason ==
              perflens::TerminationReason::signaled);
    }
    SECTION("runner interrupt") {
        CHECK(perflens::ProcessRunner{}
                  .run({"/bin/sh", "-c", "kill -INT \"$PPID\"; exec sleep 2"}, options)
                  .reason == perflens::TerminationReason::interrupted);
    }
    SECTION("exec failure") {
        CHECK_THROWS(perflens::ProcessRunner{}.run({"/definitely/missing"}, options));
    }
    SECTION("workers created before collection starts") {
        perflens::RunOptions::ServiceLifecycle service;
        service.startupDelay = 200ms;
        service.workloadCommand = {"/bin/sleep", "1"};
        options.service = service;
        const auto result = perflens::ProcessRunner{}.run({"./threaded_hotspot", "4", "5000000000"}, options);
        REQUIRE(result.succeeded());
        CHECK(hasWorker(result.result.cpuProfile));
    }
    SECTION("workload launch failure") {
        perflens::RunOptions::ServiceLifecycle service;
        service.workloadCommand = {"/definitely/missing"};
        options.service = service;
        CHECK_THROWS(perflens::ProcessRunner{}.run({"/bin/sleep", "5"}, options));
    }
}

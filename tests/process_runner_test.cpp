#include "perflens/process_runner.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <csignal>
#include <fstream>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <vector>

#include <sched.h>

using namespace std::chrono_literals;

TEST_CASE("successful targets produce process metrics") {
    const perflens::ProcessOutcome outcome = perflens::ProcessRunner{}.run({"/bin/true"});

    CHECK(outcome.succeeded());
    CHECK(outcome.reason == perflens::TerminationReason::exited);
    CHECK(outcome.exitCode == 0);
    CHECK(outcome.result.process.wallTime > 0ns);
    CHECK(outcome.result.process.maxRssBytes > 0);
}

TEST_CASE("nonzero target exits remain explicit") {
    const perflens::ProcessOutcome outcome = perflens::ProcessRunner{}.run({"/bin/sh", "-c", "exit 7"});

    CHECK_FALSE(outcome.succeeded());
    CHECK(outcome.reason == perflens::TerminationReason::exited);
    CHECK(outcome.exitCode == 7);
}

TEST_CASE("target signals remain explicit") {
    const perflens::ProcessOutcome outcome =
        perflens::ProcessRunner{}.run({"/bin/sh", "-c", "kill -TERM $$"});

    CHECK_FALSE(outcome.succeeded());
    CHECK(outcome.reason == perflens::TerminationReason::signaled);
    CHECK(outcome.signal == SIGTERM);
}

TEST_CASE("exec failures include the operating system error") {
    try {
        static_cast<void>(perflens::ProcessRunner{}.run({"/definitely/not/a/program"}));
        FAIL("missing executable did not fail");
    } catch (const std::system_error& error) {
        CHECK(error.code() == std::errc::no_such_file_or_directory);
    }
}

TEST_CASE("timeouts terminate the complete target process group") {
    perflens::RunOptions options;
    options.timeout = 30ms;
    options.terminationGrace = 20ms;

    const perflens::ProcessOutcome outcome =
        perflens::ProcessRunner{}.run({"/bin/sh", "-c", "sleep 2"}, options);

    CHECK_FALSE(outcome.succeeded());
    CHECK(outcome.reason == perflens::TerminationReason::timedOut);
    CHECK(outcome.result.process.wallTime < 1s);
}

TEST_CASE("empty commands are rejected") {
    CHECK_THROWS_AS(perflens::ProcessRunner{}.run(std::vector<std::string>{}), std::invalid_argument);
}

TEST_CASE("targets can be pinned to an allowed CPU") {
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    REQUIRE(sched_getaffinity(0, sizeof(allowed), &allowed) == 0);
    unsigned int cpu = 0;
    while (cpu < CPU_SETSIZE && !CPU_ISSET(cpu, &allowed)) {
        ++cpu;
    }
    REQUIRE(cpu < CPU_SETSIZE);

    perflens::RunOptions options;
    options.cpu = cpu;
    CHECK(perflens::ProcessRunner{}.run({"/bin/true"}, options).succeeded());
}

TEST_CASE("invalid CPU affinity is a pre-exec failure") {
    perflens::RunOptions options;
    options.cpu = CPU_SETSIZE;

    try {
        static_cast<void>(perflens::ProcessRunner{}.run({"/bin/true"}, options));
        FAIL("invalid CPU affinity did not fail");
    } catch (const std::system_error& error) {
        CHECK(error.code() == std::errc::invalid_argument);
    }
}

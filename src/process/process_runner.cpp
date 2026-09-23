#include "perflens/process_runner.hpp"

#include "perflens/perf_counter.hpp"
#include "perflens/perf_sampler.hpp"
#include "perflens/symbolizer.hpp"
#include "perflens/workload_metrics.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <netdb.h>
#include <poll.h>
#include <sched.h>
#include <spawn.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace perflens {
namespace {

volatile std::sig_atomic_t receivedSignal = 0;

extern "C" void rememberSignal(const int signal) {
    receivedSignal = signal;
}

class SignalHandlerGuard {
  public:
    SignalHandlerGuard() {
        receivedSignal = 0;
        struct sigaction action{};
        action.sa_handler = rememberSignal;
        sigemptyset(&action.sa_mask);
        if (sigaction(SIGINT, &action, &oldInterrupt_) == -1) {
            throw std::system_error(errno, std::generic_category(), "sigaction");
        }
        if (sigaction(SIGTERM, &action, &oldTerminate_) == -1) {
            const int installError = errno;
            sigaction(SIGINT, &oldInterrupt_, nullptr);
            throw std::system_error(installError, std::generic_category(), "sigaction");
        }
        struct sigaction ignore{};
        ignore.sa_handler = SIG_IGN;
        sigemptyset(&ignore.sa_mask);
        if (sigaction(SIGPIPE, &ignore, &oldBrokenPipe_) == -1) {
            const int installError = errno;
            sigaction(SIGINT, &oldInterrupt_, nullptr);
            sigaction(SIGTERM, &oldTerminate_, nullptr);
            throw std::system_error(installError, std::generic_category(), "sigaction");
        }
    }

    ~SignalHandlerGuard() {
        sigaction(SIGINT, &oldInterrupt_, nullptr);
        sigaction(SIGTERM, &oldTerminate_, nullptr);
        sigaction(SIGPIPE, &oldBrokenPipe_, nullptr);
    }

    SignalHandlerGuard(const SignalHandlerGuard&) = delete;
    SignalHandlerGuard& operator=(const SignalHandlerGuard&) = delete;

  private:
    struct sigaction oldInterrupt_{};
    struct sigaction oldTerminate_{};
    struct sigaction oldBrokenPipe_{};
};

class FileDescriptor {
  public:
    explicit FileDescriptor(const int descriptor = -1) : descriptor_(descriptor) {}
    ~FileDescriptor() {
        if (descriptor_ != -1) {
            close(descriptor_);
        }
    }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    FileDescriptor(FileDescriptor&& other) noexcept : descriptor_(other.release()) {}
    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        if (this != &other) {
            if (descriptor_ != -1) {
                close(descriptor_);
            }
            descriptor_ = other.release();
        }
        return *this;
    }
    [[nodiscard]] int get() const noexcept {
        return descriptor_;
    }
    int release() noexcept {
        const int descriptor = descriptor_;
        descriptor_ = -1;
        return descriptor;
    }

  private:
    int descriptor_;
};

class ChildGuard {
  public:
    explicit ChildGuard(const pid_t child) : child_(child) {}
    ~ChildGuard() {
        if (child_ == -1) {
            return;
        }
        kill(-child_, SIGKILL);
        kill(child_, SIGKILL);
        while (waitpid(child_, nullptr, 0) == -1 && errno == EINTR) {
        }
    }
    ChildGuard(const ChildGuard&) = delete;
    ChildGuard& operator=(const ChildGuard&) = delete;
    void release() noexcept {
        // the leader can exit while descendants are still running
        kill(-child_, SIGKILL);
        child_ = -1;
    }

  private:
    pid_t child_;
};

class MetricsFile {
  public:
    explicit MetricsFile(const bool enabled) {
        if (!enabled) {
            return;
        }
        std::array<char, 32> pattern{};
        std::strcpy(pattern.data(), "/tmp/perflens-metrics-XXXXXX");
        const int descriptor = mkstemp(pattern.data());
        if (descriptor == -1) {
            throw std::system_error(errno, std::generic_category(), "mkstemp workload metrics");
        }
        close(descriptor);
        path_ = pattern.data();
        unlink(path_.c_str());
    }

    ~MetricsFile() {
        if (!path_.empty()) {
            unlink(path_.c_str());
        }
    }

    [[nodiscard]] const std::string& path() const noexcept {
        return path_;
    }

  private:
    std::string path_;
};

enum class ChildStage : int {
    affinity = 1,
    environment = 2,
    startBarrier = 3,
    exec = 4,
};

struct ChildError {
    ChildStage stage{};
    int error{};
};

std::array<FileDescriptor, 2> createPipe() {
    std::array<int, 2> descriptors{};
    if (pipe2(descriptors.data(), O_CLOEXEC) == -1) {
        throw std::system_error(errno, std::generic_category(), "pipe2");
    }
    return {FileDescriptor{descriptors[0]}, FileDescriptor{descriptors[1]}};
}

std::chrono::nanoseconds timevalToNanoseconds(const timeval& value) {
    return std::chrono::seconds{value.tv_sec} + std::chrono::microseconds{value.tv_usec};
}

std::uint64_t nonnegative(const long value) {
    return value < 0 ? 0U : static_cast<std::uint64_t>(value);
}

ProcessMetrics makeMetrics(const std::chrono::steady_clock::duration wallTime, const rusage& usage) {
    ProcessMetrics metrics;
    metrics.wallTime = std::chrono::duration_cast<std::chrono::nanoseconds>(wallTime);
    metrics.userTime = timevalToNanoseconds(usage.ru_utime);
    metrics.systemTime = timevalToNanoseconds(usage.ru_stime);
    // Linux reports ru_maxrss in KiB.
    metrics.maxRssBytes = nonnegative(usage.ru_maxrss) * 1024U;
    metrics.minorFaults = nonnegative(usage.ru_minflt);
    metrics.majorFaults = nonnegative(usage.ru_majflt);
    metrics.voluntaryContextSwitches = nonnegative(usage.ru_nvcsw);
    metrics.involuntaryContextSwitches = nonnegative(usage.ru_nivcsw);
    return metrics;
}

void restoreChildSignals() {
    struct sigaction action{};
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGPIPE, &action, nullptr);
}

void writeChildError(const int descriptor, const ChildStage stage, const int error) {
    const ChildError childError{stage, error};
    const auto* bytes = reinterpret_cast<const char*>(&childError);
    std::size_t written = 0;
    while (written < sizeof(childError)) {
        const ssize_t count = write(descriptor, bytes + written, sizeof(childError) - written);
        if (count > 0) {
            written += static_cast<std::size_t>(count);
        } else if (count == -1 && errno != EINTR) {
            break;
        }
    }
}

bool waitForRelease(const int descriptor) {
    char byte = 0;
    while (true) {
        const ssize_t count = read(descriptor, &byte, 1);
        if (count == 1) {
            return true;
        }
        if (count == 0) {
            return false;
        }
        if (errno != EINTR) {
            return false;
        }
    }
}

[[noreturn]] void executeChild(const std::vector<std::string>& command,
                               const RunOptions& options,
                               const std::string& metricsPath,
                               const int startRead,
                               const int startWrite,
                               const int errorRead,
                               const int errorWrite) {
    close(startWrite);
    close(errorRead);
    setpgid(0, 0);
    restoreChildSignals();

    if (options.cpu) {
        if (*options.cpu >= CPU_SETSIZE) {
            writeChildError(errorWrite, ChildStage::affinity, EINVAL);
            _exit(126);
        }
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(*options.cpu, &set);
        if (sched_setaffinity(0, sizeof(set), &set) == -1) {
            writeChildError(errorWrite, ChildStage::affinity, errno);
            _exit(126);
        }
    }

    for (const auto& [name, value] : options.environment) {
        if (setenv(name.c_str(), value.c_str(), 1) == -1) {
            writeChildError(errorWrite, ChildStage::environment, errno);
            _exit(126);
        }
    }
    if (!metricsPath.empty() && setenv("PERFLENS_METRICS_FILE", metricsPath.c_str(), 1) == -1) {
        writeChildError(errorWrite, ChildStage::environment, errno);
        _exit(126);
    }

    if (!waitForRelease(startRead)) {
        writeChildError(errorWrite, ChildStage::startBarrier, errno == 0 ? EPIPE : errno);
        _exit(126);
    }
    close(startRead);

    std::vector<char*> arguments;
    arguments.reserve(command.size() + 1);
    for (const std::string& value : command) {
        arguments.push_back(const_cast<char*>(value.c_str()));
    }
    arguments.push_back(nullptr);
    execvp(arguments.front(), arguments.data());
    writeChildError(errorWrite, ChildStage::exec, errno);
    _exit(127);
}

std::optional<ChildError> readChildError(const int descriptor) {
    ChildError childError{};
    auto* bytes = reinterpret_cast<char*>(&childError);
    std::size_t received = 0;
    while (received < sizeof(childError)) {
        const ssize_t count = read(descriptor, bytes + received, sizeof(childError) - received);
        if (count > 0) {
            received += static_cast<std::size_t>(count);
        } else if (count == 0) {
            break;
        } else if (errno != EINTR) {
            throw std::system_error(errno, std::generic_category(), "read child status");
        }
    }
    if (received == 0) {
        return std::nullopt;
    }
    if (received != sizeof(childError)) {
        throw std::runtime_error("incomplete child status");
    }
    return childError;
}

void throwChildError(const ChildError& error, const std::string& executable) {
    std::string operation;
    switch (error.stage) {
    case ChildStage::affinity:
        operation = "set target CPU affinity";
        break;
    case ChildStage::environment:
        operation = "set target environment";
        break;
    case ChildStage::startBarrier:
        operation = "release target start barrier";
        break;
    case ChildStage::exec:
        operation = "execvp " + executable;
        break;
    }
    throw std::system_error(error.error, std::generic_category(), operation);
}

void signalProcessGroup(const pid_t child, const int signal) {
    if (kill(-child, signal) == -1 && errno != ESRCH) {
        throw std::system_error(errno, std::generic_category(), "kill target process group");
    }
}

std::string currentExecutable(const pid_t process) {
    std::array<char, 4096> path{};
    const std::string link = "/proc/" + std::to_string(process) + "/exe";
    const ssize_t count = readlink(link.c_str(), path.data(), path.size() - 1);
    return count <= 0 ? std::string{} : std::string{path.data(), static_cast<std::size_t>(count)};
}

struct ProcSnapshot {
    std::uint64_t userTicks{};
    std::uint64_t systemTicks{};
    std::uint64_t minorFaults{};
    std::uint64_t majorFaults{};
    std::uint64_t voluntaryContextSwitches{};
    std::uint64_t involuntaryContextSwitches{};
};

std::optional<ProcSnapshot> readProcSnapshot(const pid_t process) {
    std::ifstream statInput{"/proc/" + std::to_string(process) + "/stat"};
    std::string statLine;
    if (!std::getline(statInput, statLine)) {
        return std::nullopt;
    }
    const std::size_t commandEnd = statLine.rfind(')');
    if (commandEnd == std::string::npos || commandEnd + 2 >= statLine.size()) {
        return std::nullopt;
    }
    std::istringstream fields{statLine.substr(commandEnd + 2)};
    std::vector<std::string> values;
    std::string value;
    while (fields >> value) {
        values.push_back(value);
    }
    if (values.size() <= 12) {
        return std::nullopt;
    }

    ProcSnapshot snapshot;
    snapshot.minorFaults = std::stoull(values[7]);
    snapshot.majorFaults = std::stoull(values[9]);
    snapshot.userTicks = std::stoull(values[11]);
    snapshot.systemTicks = std::stoull(values[12]);

    std::ifstream statusInput{"/proc/" + std::to_string(process) + "/status"};
    std::string line;
    while (std::getline(statusInput, line)) {
        if (line.starts_with("voluntary_ctxt_switches:")) {
            snapshot.voluntaryContextSwitches = std::stoull(line.substr(line.find(':') + 1));
        } else if (line.starts_with("nonvoluntary_ctxt_switches:")) {
            snapshot.involuntaryContextSwitches = std::stoull(line.substr(line.find(':') + 1));
        }
    }
    return snapshot;
}

std::chrono::nanoseconds ticksToNanoseconds(const std::uint64_t ticks) {
    const long ticksPerSecond = sysconf(_SC_CLK_TCK);
    if (ticksPerSecond <= 0) {
        return std::chrono::nanoseconds{};
    }
    const long double nanoseconds =
        static_cast<long double>(ticks) * 1'000'000'000.0L / static_cast<long double>(ticksPerSecond);
    return std::chrono::nanoseconds{static_cast<std::int64_t>(nanoseconds)};
}

std::uint64_t difference(const std::uint64_t end, const std::uint64_t start) {
    return end >= start ? end - start : 0;
}

pid_t startWorkload(const std::vector<std::string>& command,
                    const std::vector<std::pair<std::string, std::string>>& environment,
                    const std::string& metricsPath) {
    // the sampler may be running, so do not allocate in a forked child
    std::vector<std::string> values;
    for (char** entry = environ; *entry != nullptr; ++entry) {
        values.emplace_back(*entry);
    }
    const auto setValue = [&](const std::string& name, const std::string& value) {
        if (name.empty() || name.find('=') != std::string::npos) {
            throw std::invalid_argument("invalid workload environment name");
        }
        const std::string prefix = name + '=';
        std::erase_if(values, [&](const std::string& entry) { return entry.starts_with(prefix); });
        values.push_back(prefix + value);
    };
    for (const auto& [name, value] : environment) {
        setValue(name, value);
    }
    if (!metricsPath.empty()) {
        setValue("PERFLENS_METRICS_FILE", metricsPath);
    }
    std::vector<char*> env;
    for (auto& value : values) {
        env.push_back(value.data());
    }
    env.push_back(nullptr);
    std::vector<char*> arguments;
    for (const std::string& value : command) {
        arguments.push_back(const_cast<char*>(value.c_str()));
    }
    arguments.push_back(nullptr);
    const auto check = [](const int error) {
        if (error != 0) {
            throw std::system_error(error, std::generic_category(), "spawn workload");
        }
    };
    posix_spawnattr_t attributes{};
    check(posix_spawnattr_init(&attributes));
    pid_t child = -1;
    try {
        sigset_t defaults{};
        sigemptyset(&defaults);
        sigaddset(&defaults, SIGINT);
        sigaddset(&defaults, SIGTERM);
        sigaddset(&defaults, SIGPIPE);
        check(posix_spawnattr_setsigdefault(&attributes, &defaults));
        check(posix_spawnattr_setpgroup(&attributes, 0));
        check(posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGDEF));
        if (command.front().find('/') != std::string::npos) {
            check(posix_spawn(&child, arguments.front(), nullptr, &attributes, arguments.data(), env.data()));
        } else {
            // use the workload's PATH, including overrides in RunOptions
            std::string path = "/bin:/usr/bin";
            for (const auto& value : values) {
                if (value.starts_with("PATH=")) {
                    path = value.substr(5);
                }
            }
            int spawnError = ENOENT;
            bool denied = false;
            std::size_t begin = 0;
            do {
                const auto end = path.find(':', begin);
                const auto directory = path.substr(begin, end == std::string::npos ? end : end - begin);
                const auto executable =
                    directory.empty() ? command.front() : directory + '/' + command.front();
                spawnError = posix_spawn(
                    &child, executable.c_str(), nullptr, &attributes, arguments.data(), env.data());
                if (spawnError == 0) {
                    break;
                }
                denied = denied || spawnError == EACCES;
                if (spawnError != ENOENT && spawnError != ENOTDIR && spawnError != EACCES) {
                    break;
                }
                if (end == std::string::npos) {
                    spawnError = denied ? EACCES : ENOENT;
                    break;
                }
                begin = end + 1;
            } while (true);
            check(spawnError);
        }
    } catch (...) {
        posix_spawnattr_destroy(&attributes);
        throw;
    }
    posix_spawnattr_destroy(&attributes);
    return child;
}

std::vector<int> samplingCpus(const std::optional<unsigned int> pinned) {
    if (pinned) {
        if (*pinned >= CPU_SETSIZE) {
            throw std::invalid_argument("sampling CPU is outside the supported affinity mask");
        }
        return {static_cast<int>(*pinned)};
    }
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof(allowed), &allowed) == -1) {
        throw std::system_error(errno, std::generic_category(), "sampling CPU affinity");
    }
    std::vector<int> cpus;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (CPU_ISSET(cpu, &allowed)) {
            cpus.push_back(cpu);
        }
    }
    return cpus;
}

bool tcpReady(const RunOptions::TcpEndpoint& endpoint) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    const std::string port = std::to_string(endpoint.port);
    const int lookup = getaddrinfo(endpoint.host.c_str(), port.c_str(), &hints, &addresses);
    if (lookup != 0) {
        return false;
    }

    bool ready = false;
    for (addrinfo* address = addresses; address != nullptr && !ready; address = address->ai_next) {
        const int descriptor =
            socket(address->ai_family, address->ai_socktype | SOCK_CLOEXEC, address->ai_protocol);
        if (descriptor == -1) {
            continue;
        }
        const int flags = fcntl(descriptor, F_GETFL, 0);
        if (flags != -1) {
            fcntl(descriptor, F_SETFL, flags | O_NONBLOCK);
        }
        const int result = connect(descriptor, address->ai_addr, address->ai_addrlen);
        if (result == 0) {
            ready = true;
        } else if (errno == EINPROGRESS) {
            pollfd event{descriptor, POLLOUT, 0};
            if (poll(&event, 1, 5) > 0) {
                int socketError = 0;
                socklen_t length = sizeof(socketError);
                ready = getsockopt(descriptor, SOL_SOCKET, SO_ERROR, &socketError, &length) == 0 &&
                        socketError == 0;
            }
        }
        close(descriptor);
    }
    freeaddrinfo(addresses);
    return ready;
}

}

bool ProcessOutcome::succeeded() const noexcept {
    return reason == TerminationReason::exited && exitCode == 0;
}

ProcessOutcome ProcessRunner::run(const std::vector<std::string>& command, const RunOptions& options) const {
    if (command.empty() || command.front().empty()) {
        throw std::invalid_argument("target command must not be empty");
    }
    if (options.timeout && *options.timeout <= std::chrono::nanoseconds::zero()) {
        throw std::invalid_argument("timeout must be greater than zero");
    }
    if (options.terminationGrace < std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("termination grace period must not be negative");
    }
    if (options.service) {
        if (options.service->workloadCommand.empty()) {
            throw std::invalid_argument("service workload command must not be empty");
        }
        if (options.service->readyTimeout <= std::chrono::milliseconds::zero()) {
            throw std::invalid_argument("service readiness timeout must be greater than zero");
        }
        if (options.service->shutdownGrace < std::chrono::milliseconds::zero()) {
            throw std::invalid_argument("service shutdown grace period must not be negative");
        }
    }

    auto startPipe = createPipe();
    auto errorPipe = createPipe();
    MetricsFile metricsFile{options.collectApplicationMetrics};
    SignalHandlerGuard signalHandlers;

    const pid_t child = fork();
    if (child == -1) {
        throw std::system_error(errno, std::generic_category(), "fork");
    }
    if (child == 0) {
        executeChild(command,
                     options,
                     options.service ? std::string{} : metricsFile.path(),
                     startPipe[0].get(),
                     startPipe[1].get(),
                     errorPipe[0].get(),
                     errorPipe[1].get());
    }

    ChildGuard childGuard{child};
    FileDescriptor childExit{static_cast<int>(syscall(SYS_pidfd_open, child, 0))};
    startPipe[0] = FileDescriptor{};
    errorPipe[1] = FileDescriptor{};
    if (setpgid(child, child) == -1 && errno != EACCES && errno != ESRCH) {
        throw std::system_error(errno, std::generic_category(), "setpgid");
    }

    std::unique_ptr<PerfCounterCollector> counters;
    std::string counterFailure;
    if (options.collectPerfCounters) {
        try {
            counters = std::make_unique<PerfCounterCollector>(child);
        } catch (const std::exception& error) {
            counterFailure = error.what();
            if (options.requirePerf) {
                throw;
            }
        }
    }

    std::unique_ptr<PerfSampler> sampler;
    std::string samplerFailure;
    if (options.samplingFrequency != 0) {
        try {
            sampler =
                std::make_unique<PerfSampler>(child, samplingCpus(options.cpu), options.samplingFrequency);
        } catch (const std::exception& error) {
            samplerFailure = error.what();
            if (options.requirePerf) {
                throw;
            }
        }
    }

    bool collectorsStarted = false;
    bool collectorsStopped = false;
    const auto startCollectors = [&]() {
        if (counters) {
            counters->start();
        }
        if (sampler) {
            try {
                sampler->start();
            } catch (const std::exception& error) {
                samplerFailure = error.what();
                sampler.reset();
                if (options.requirePerf) {
                    throw;
                }
            }
        }
        collectorsStarted = true;
    };
    const auto stopCollectors = [&]() {
        if (!collectorsStarted || collectorsStopped) {
            return;
        }
        if (counters) {
            try {
                counters->stop();
            } catch (const std::exception& error) {
                counterFailure = error.what();
                counters.reset();
                if (options.requirePerf) {
                    throw;
                }
            }
        }
        if (sampler) {
            try {
                sampler->stop();
            } catch (const std::exception& error) {
                samplerFailure = error.what();
                sampler.reset();
                if (options.requirePerf) {
                    throw;
                }
            }
        }
        collectorsStopped = true;
    };
    if (!options.service) {
        startCollectors();
    }

    const auto startedAt = std::chrono::steady_clock::now();
    const char release = 1;
    if (write(startPipe[1].get(), &release, 1) != 1) {
        const int releaseError = errno;
        startPipe[1] = FileDescriptor{};
        while (waitpid(child, nullptr, 0) == -1 && errno == EINTR) {
        }
        childGuard.release();
        const std::optional<ChildError> childError = readChildError(errorPipe[0].get());
        if (childError) {
            throwChildError(*childError, command.front());
        }
        throw std::system_error(releaseError, std::generic_category(), "release target start barrier");
    }
    startPipe[1] = FileDescriptor{};

    int status = 0;
    rusage usage{};
    bool stopping = false;
    bool timedOut = false;
    bool interrupted = false;
    bool sentKill = false;
    auto graceDeadline = std::chrono::steady_clock::time_point::max();
    auto nextMapCapture = startedAt;
    std::vector<MemoryMapping> mappings;
    const std::string ownExecutable = currentExecutable(getpid());
    std::unique_ptr<ChildGuard> workloadGuard;
    pid_t workload = -1;
    bool workloadCompleted = false;
    bool managedStop = false;
    std::string lifecycleFailure;
    auto measurementStartedAt = startedAt;
    auto measurementFinishedAt = std::chrono::steady_clock::time_point{};
    std::optional<ProcSnapshot> measurementStartSnapshot;
    std::optional<ProcSnapshot> measurementEndSnapshot;

    while (true) {
        const pid_t waitResult = wait4(child, &status, WNOHANG, &usage);
        if (waitResult == child) {
            break;
        }
        if (waitResult == -1 && errno != EINTR) {
            throw std::system_error(errno, std::generic_category(), "wait4");
        }

        const auto now = std::chrono::steady_clock::now();
        if (options.service && workload == -1 && !workloadCompleted && lifecycleFailure.empty() &&
            !stopping) {
            const bool delayElapsed = now - startedAt >= options.service->startupDelay;
            const bool ready =
                delayElapsed && (!options.service->readyTcp || tcpReady(*options.service->readyTcp));
            if (ready) {
                startCollectors();
                measurementStartedAt = now;
                measurementStartSnapshot = readProcSnapshot(child);
                workload =
                    startWorkload(options.service->workloadCommand, options.environment, metricsFile.path());
                workloadGuard = std::make_unique<ChildGuard>(workload);
            } else if (now - startedAt >= options.service->readyTimeout) {
                lifecycleFailure = "service readiness timed out";
                stopping = true;
                signalProcessGroup(child, SIGTERM);
                graceDeadline = now + options.service->shutdownGrace;
            }
        }

        if (workload != -1) {
            int workloadStatus = 0;
            const pid_t workloadWait = waitpid(workload, &workloadStatus, WNOHANG);
            if (workloadWait == workload) {
                workloadGuard->release();
                workloadGuard.reset();
                workload = -1;
                measurementFinishedAt = now;
                measurementEndSnapshot = readProcSnapshot(child);
                stopCollectors();
                workloadCompleted = WIFEXITED(workloadStatus) && WEXITSTATUS(workloadStatus) == 0;
                if (!workloadCompleted) {
                    if (WIFEXITED(workloadStatus)) {
                        lifecycleFailure =
                            "workload exited with status " + std::to_string(WEXITSTATUS(workloadStatus));
                    } else if (WIFSIGNALED(workloadStatus)) {
                        lifecycleFailure =
                            "workload terminated by signal " + std::to_string(WTERMSIG(workloadStatus));
                    } else {
                        lifecycleFailure = "workload ended unexpectedly";
                    }
                } else {
                    managedStop = true;
                }
                stopping = true;
                signalProcessGroup(child, SIGTERM);
                graceDeadline = now + options.service->shutdownGrace;
            } else if (workloadWait == -1 && errno != EINTR) {
                throw std::system_error(errno, std::generic_category(), "waitpid workload");
            }
        }

        if (sampler && collectorsStarted) {
            if (now >= nextMapCapture && currentExecutable(child) != ownExecutable) {
                const std::vector<MemoryMapping> currentMappings = readProcMaps(child);
                if (!currentMappings.empty()) {
                    mappings = currentMappings;
                }
                nextMapCapture = now + std::chrono::milliseconds{20};
            }
        }
        if (!stopping && receivedSignal != 0) {
            interrupted = true;
            stopping = true;
            signalProcessGroup(child, static_cast<int>(receivedSignal));
            if (workload != -1) {
                signalProcessGroup(workload, static_cast<int>(receivedSignal));
            }
            graceDeadline = now + options.terminationGrace;
        } else if (!stopping && options.timeout && now - startedAt >= *options.timeout) {
            timedOut = true;
            stopping = true;
            signalProcessGroup(child, SIGTERM);
            if (workload != -1) {
                signalProcessGroup(workload, SIGTERM);
            }
            graceDeadline = now + options.terminationGrace;
        } else if (stopping && !sentKill && now >= graceDeadline) {
            signalProcessGroup(child, SIGKILL);
            if (workload != -1) {
                signalProcessGroup(workload, SIGKILL);
            }
            sentKill = true;
        }
        if (childExit.get() != -1) {
            pollfd exitEvent{childExit.get(), POLLIN, 0};
            poll(&exitEvent, 1, 2);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
    }

    const auto finishedAt = std::chrono::steady_clock::now();
    if (workloadGuard) {
        if (lifecycleFailure.empty() && !interrupted && !timedOut) {
            lifecycleFailure = "service exited before the workload completed";
        }
        workloadGuard.reset();
        workload = -1;
    }
    if (options.service && !workloadCompleted && lifecycleFailure.empty() && !interrupted && !timedOut) {
        lifecycleFailure = "service exited before the workload started";
    }
    stopCollectors();
    childGuard.release();
    const std::optional<ChildError> childError = readChildError(errorPipe[0].get());
    if (childError) {
        throwChildError(*childError, command.front());
    }
    if (!lifecycleFailure.empty()) {
        throw std::runtime_error(lifecycleFailure);
    }

    ProcessOutcome outcome;
    if (measurementFinishedAt == std::chrono::steady_clock::time_point{}) {
        measurementFinishedAt = finishedAt;
    }
    outcome.result.process = makeMetrics(measurementFinishedAt - measurementStartedAt, usage);
    if (measurementStartSnapshot && measurementEndSnapshot) {
        outcome.result.process.userTime = ticksToNanoseconds(
            difference(measurementEndSnapshot->userTicks, measurementStartSnapshot->userTicks));
        outcome.result.process.systemTime = ticksToNanoseconds(
            difference(measurementEndSnapshot->systemTicks, measurementStartSnapshot->systemTicks));
        outcome.result.process.minorFaults =
            difference(measurementEndSnapshot->minorFaults, measurementStartSnapshot->minorFaults);
        outcome.result.process.majorFaults =
            difference(measurementEndSnapshot->majorFaults, measurementStartSnapshot->majorFaults);
        outcome.result.process.voluntaryContextSwitches =
            difference(measurementEndSnapshot->voluntaryContextSwitches,
                       measurementStartSnapshot->voluntaryContextSwitches);
        outcome.result.process.involuntaryContextSwitches =
            difference(measurementEndSnapshot->involuntaryContextSwitches,
                       measurementStartSnapshot->involuntaryContextSwitches);
    }
    outcome.result.counters.unavailableReason =
        options.collectPerfCounters ? counterFailure : "hardware counters disabled";
    outcome.result.cpuProfile.unavailableReason =
        options.samplingFrequency == 0 ? "CPU sampling disabled" : samplerFailure;

    if (counters) {
        try {
            outcome.result.counters = counters->read();
        } catch (const std::exception& error) {
            if (options.requirePerf) {
                throw;
            }
            outcome.result.counters = PerfCounters{};
            outcome.result.counters.unavailableReason = error.what();
        }
    }
    if (sampler) {
        try {
            outcome.result.cpuProfile.available = true;
            outcome.result.cpuProfile.lostSamples = sampler->lostSamples();
            outcome.result.cpuProfile.functions = symbolizeSamples(sampler->samples(), mappings);
            outcome.result.cpuProfile.unavailableReason.clear();
        } catch (const std::exception& error) {
            if (options.requirePerf) {
                throw;
            }
            outcome.result.cpuProfile = CpuProfile{};
            outcome.result.cpuProfile.unavailableReason = error.what();
        }
    }
    if (options.collectApplicationMetrics) {
        outcome.result.application = loadApplicationMetrics(metricsFile.path());
    }

    if (interrupted) {
        outcome.reason = TerminationReason::interrupted;
        outcome.signal = static_cast<int>(receivedSignal);
    } else if (timedOut) {
        outcome.reason = TerminationReason::timedOut;
        outcome.signal = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
    } else if (managedStop) {
        outcome.reason = TerminationReason::exited;
        outcome.exitCode = 0;
    } else if (WIFEXITED(status)) {
        outcome.reason = TerminationReason::exited;
        outcome.exitCode = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        outcome.reason = TerminationReason::signaled;
        outcome.signal = WTERMSIG(status);
    } else {
        throw std::runtime_error("target ended with an unsupported wait status");
    }
    return outcome;
}

}

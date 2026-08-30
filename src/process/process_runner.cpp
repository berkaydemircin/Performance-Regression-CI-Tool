#include "perflens/process_runner.hpp"

#include "perflens/perf_counter.hpp"
#include "perflens/workload_metrics.hpp"

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
                     metricsFile.path(),
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

    bool collectorsStarted = false;
    bool collectorsStopped = false;
    const auto startCollectors = [&]() {
        if (counters) {
            counters->start();
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

        collectorsStopped = true;
    };
    startCollectors();

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
    while (true) {
        const pid_t waitResult = wait4(child, &status, WNOHANG, &usage);
        if (waitResult == child) {
            break;
        }
        if (waitResult == -1 && errno != EINTR) {
            throw std::system_error(errno, std::generic_category(), "wait4");
        }

        const auto now = std::chrono::steady_clock::now();

        if (!stopping && receivedSignal != 0) {
            interrupted = true;
            stopping = true;
            signalProcessGroup(child, static_cast<int>(receivedSignal));

            graceDeadline = now + options.terminationGrace;
        } else if (!stopping && options.timeout && now - startedAt >= *options.timeout) {
            timedOut = true;
            stopping = true;
            signalProcessGroup(child, SIGTERM);

            graceDeadline = now + options.terminationGrace;
        } else if (stopping && !sentKill && now >= graceDeadline) {
            signalProcessGroup(child, SIGKILL);

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

    stopCollectors();
    childGuard.release();
    const std::optional<ChildError> childError = readChildError(errorPipe[0].get());
    if (childError) {
        throwChildError(*childError, command.front());
    }

    ProcessOutcome outcome;
    outcome.result.process = makeMetrics(finishedAt - startedAt, usage);
    outcome.result.counters.unavailableReason =
        options.collectPerfCounters ? counterFailure : "hardware counters disabled";
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

    if (options.collectApplicationMetrics) {
        outcome.result.application = loadApplicationMetrics(metricsFile.path());
    }

    if (interrupted) {
        outcome.reason = TerminationReason::interrupted;
        outcome.signal = static_cast<int>(receivedSignal);
    } else if (timedOut) {
        outcome.reason = TerminationReason::timedOut;
        outcome.signal = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
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

#pragma once

#include "perflens/model.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace perflens {

enum class TerminationReason {
    exited,
    signaled,
    timedOut,
    interrupted,
};

struct RunOptions {
    std::optional<std::chrono::nanoseconds> timeout;
    std::chrono::milliseconds terminationGrace{500};
    std::optional<unsigned int> cpu;
    bool collectPerfCounters{true};
    bool requirePerf{};
    std::uint64_t samplingFrequency{};
    bool collectApplicationMetrics{true};
    std::vector<std::pair<std::string, std::string>> environment;
    struct TcpEndpoint {
        std::string host;
        std::uint16_t port{};
    };
    struct ServiceLifecycle {
        std::vector<std::string> workloadCommand;
        std::chrono::milliseconds startupDelay{};
        std::optional<TcpEndpoint> readyTcp;
        std::chrono::milliseconds readyTimeout{5000};
        std::chrono::milliseconds shutdownGrace{500};
    };
    std::optional<ServiceLifecycle> service;
};

struct ProcessOutcome {
    RunResult result;
    TerminationReason reason{TerminationReason::exited};
    int exitCode{};
    int signal{};

    [[nodiscard]] bool succeeded() const noexcept;
};

class ProcessRunner {
  public:
    [[nodiscard]] ProcessOutcome run(const std::vector<std::string>& command,
                                     const RunOptions& options = {}) const;
};

}

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
    std::vector<std::pair<std::string, std::string>> environment;
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

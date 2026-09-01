#include "perflens/benchmark_runner.hpp"

#include <algorithm>
#include <csignal>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace perflens {
namespace {

enum class Variant {
    baseline,
    candidate,
};

std::uint64_t chooseSeed(const std::optional<std::uint64_t> requested) {
    if (requested) {
        return *requested;
    }
    std::random_device source;
    return (static_cast<std::uint64_t>(source()) << 32U) ^ static_cast<std::uint64_t>(source());
}

std::vector<Variant> balancedOrder(const std::size_t count, std::mt19937_64& generator) {
    std::vector<Variant> order;
    order.reserve(count * 2);
    for (std::size_t index = 0; index < count; ++index) {
        order.push_back(Variant::baseline);
        order.push_back(Variant::candidate);
    }
    std::shuffle(order.begin(), order.end(), generator);
    return order;
}

RunResult runSuccessful(const std::vector<std::string>& command, const RunOptions& options) {
    const ProcessOutcome outcome = ProcessRunner{}.run(command, options);
    if (outcome.succeeded()) {
        return outcome.result;
    }

    std::string message = "target benchmark failed: ";
    switch (outcome.reason) {
    case TerminationReason::exited:
        message += "exit status " + std::to_string(outcome.exitCode);
        break;
    case TerminationReason::signaled:
        message += "signal " + std::to_string(outcome.signal);
        break;
    case TerminationReason::timedOut:
        message += "timeout";
        break;
    case TerminationReason::interrupted:
        message += "interrupted";
        break;
    }
    const int exitCode = outcome.reason == TerminationReason::interrupted
                             ? 128 + (outcome.signal == 0 ? SIGINT : outcome.signal)
                             : 2;
    throw BenchmarkError(message, exitCode);
}

}

BenchmarkError::BenchmarkError(std::string message, const int exitCode)
    : std::runtime_error(std::move(message)), exitCode_(exitCode) {}

int BenchmarkError::exitCode() const noexcept {
    return exitCode_;
}

BenchmarkResult runBenchmark(const std::vector<std::string>& command,
                             const BenchmarkOptions& benchmarkOptions,
                             const RunOptions& runOptions) {
    if (benchmarkOptions.repeat == 0) {
        throw std::invalid_argument("repeat must be greater than zero");
    }
    for (std::size_t index = 0; index < benchmarkOptions.warmup; ++index) {
        static_cast<void>(runSuccessful(command, runOptions));
    }

    BenchmarkResult result;
    result.runs.reserve(benchmarkOptions.repeat);
    for (std::size_t index = 0; index < benchmarkOptions.repeat; ++index) {
        result.runs.push_back(runSuccessful(command, runOptions));
    }
    result.summary = summarizeRuns(result.runs);
    return result;
}

ComparisonResult runComparison(const std::vector<std::string>& baseline,
                               const std::vector<std::string>& candidate,
                               const BenchmarkOptions& benchmarkOptions,
                               const RunOptions& runOptions,
                               const Thresholds& thresholds) {
    if (benchmarkOptions.repeat == 0) {
        throw std::invalid_argument("repeat must be greater than zero");
    }

    ComparisonResult result;
    result.seed = chooseSeed(benchmarkOptions.seed);
    std::mt19937_64 generator{result.seed};

    for (const Variant variant : balancedOrder(benchmarkOptions.warmup, generator)) {
        static_cast<void>(runSuccessful(variant == Variant::baseline ? baseline : candidate, runOptions));
    }

    result.baseline.runs.reserve(benchmarkOptions.repeat);
    result.candidate.runs.reserve(benchmarkOptions.repeat);
    for (const Variant variant : balancedOrder(benchmarkOptions.repeat, generator)) {
        if (variant == Variant::baseline) {
            result.baseline.runs.push_back(runSuccessful(baseline, runOptions));
        } else {
            result.candidate.runs.push_back(runSuccessful(candidate, runOptions));
        }
    }

    result.baseline.summary = summarizeRuns(result.baseline.runs);
    result.candidate.summary = summarizeRuns(result.candidate.runs);
    result.metrics = compareMetrics(result.baseline.summary, result.candidate.summary);
    result.profileChanges = compareProfiles(result.baseline.runs, result.candidate.runs);
    result.violations = evaluateThresholds(result.metrics, thresholds);
    return result;
}

}

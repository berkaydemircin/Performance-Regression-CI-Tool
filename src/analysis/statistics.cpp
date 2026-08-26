#include "perflens/analysis.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <vector>

namespace perflens {
namespace {

template <typename Extractor>
std::vector<double> collect(const std::vector<RunResult>& runs, Extractor extractor) {
    std::vector<double> values;
    values.reserve(runs.size());
    for (const RunResult& run : runs) {
        values.push_back(extractor(run));
    }
    return values;
}

}

Distribution summarize(const std::vector<double>& values) {
    if (values.empty()) {
        throw std::invalid_argument("cannot summarize an empty sample");
    }

    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());

    Distribution result;
    result.count = sorted.size();
    result.minimum = sorted.front();
    result.maximum = sorted.back();
    result.mean = std::accumulate(sorted.begin(), sorted.end(), 0.0) / static_cast<double>(sorted.size());

    const std::size_t middle = sorted.size() / 2;
    result.median = sorted.size() % 2 == 0 ? (sorted[middle - 1] + sorted[middle]) / 2.0 : sorted[middle];

    double squaredDifference = 0.0;
    for (const double value : sorted) {
        const double difference = value - result.mean;
        squaredDifference += difference * difference;
    }
    result.standardDeviation = std::sqrt(squaredDifference / static_cast<double>(sorted.size()));
    result.coefficientOfVariation =
        result.mean == 0.0 ? 0.0 : result.standardDeviation / std::abs(result.mean) * 100.0;
    return result;
}

BenchmarkSummary summarizeRuns(const std::vector<RunResult>& runs) {
    if (runs.empty()) {
        throw std::invalid_argument("cannot summarize a benchmark without measured runs");
    }

    BenchmarkSummary summary;
    summary.wallTimeNs = summarize(collect(
        runs, [](const RunResult& run) { return static_cast<double>(run.process.wallTime.count()); }));
    summary.userTimeNs = summarize(collect(
        runs, [](const RunResult& run) { return static_cast<double>(run.process.userTime.count()); }));
    summary.systemTimeNs = summarize(collect(
        runs, [](const RunResult& run) { return static_cast<double>(run.process.systemTime.count()); }));
    summary.maxRssBytes = summarize(
        collect(runs, [](const RunResult& run) { return static_cast<double>(run.process.maxRssBytes); }));
    summary.voluntaryContextSwitches = summarize(collect(runs, [](const RunResult& run) {
        return static_cast<double>(run.process.voluntaryContextSwitches);
    }));
    summary.involuntaryContextSwitches = summarize(collect(runs, [](const RunResult& run) {
        return static_cast<double>(run.process.involuntaryContextSwitches);
    }));

    return summary;
}

}

#pragma once

#include "perflens/model.hpp"

#include <optional>
#include <string>
#include <vector>

namespace perflens {

struct Thresholds {
    std::optional<double> maxRuntimeRegressionPercent;
    std::optional<double> maxRssRegressionPercent;
};

[[nodiscard]] Distribution summarize(const std::vector<double>& values);
[[nodiscard]] BenchmarkSummary summarizeRuns(const std::vector<RunResult>& runs);
[[nodiscard]] std::vector<MetricComparison> compareMetrics(const BenchmarkSummary& baseline,
                                                           const BenchmarkSummary& candidate);
[[nodiscard]] std::vector<ThresholdViolation> evaluateThresholds(const std::vector<MetricComparison>& metrics,
                                                                 const Thresholds& thresholds);
[[nodiscard]] Thresholds loadThresholds(const std::string& path);

}

#include "perflens/analysis.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace perflens {
namespace {

std::optional<double> percentChange(const double baseline, const double candidate) {
    if (baseline == 0.0) {
        return candidate == 0.0 ? std::optional<double>{0.0} : std::nullopt;
    }
    return (candidate - baseline) / baseline * 100.0;
}

void addMetric(std::vector<MetricComparison>& output,
               const std::string& key,
               const std::string& label,
               const std::string& unit,
               const Distribution& baseline,
               const Distribution& candidate,
               const bool higherIsBetter) {
    output.push_back({key,
                      label,
                      unit,
                      baseline.median,
                      candidate.median,
                      percentChange(baseline.median, candidate.median),
                      higherIsBetter});
}

void addOptionalMetric(std::vector<MetricComparison>& output,
                       const std::string& key,
                       const std::string& label,
                       const std::string& unit,
                       const std::optional<Distribution>& baseline,
                       const std::optional<Distribution>& candidate,
                       const bool higherIsBetter) {
    if (baseline && candidate) {
        addMetric(output, key, label, unit, *baseline, *candidate, higherIsBetter);
    }
}

std::optional<double> thresholdFor(const Thresholds& thresholds, const std::string& key) {
    if (key == "runtime") {
        return thresholds.maxRuntimeRegressionPercent;
    }
    if (key == "p99_latency") {
        return thresholds.maxP99LatencyRegressionPercent;
    }
    if (key == "throughput") {
        return thresholds.maxThroughputRegressionPercent;
    }
    if (key == "cpu_time_per_operation") {
        return thresholds.maxCpuRegressionPercent;
    }
    if (key == "max_rss") {
        return thresholds.maxRssRegressionPercent;
    }
    return std::nullopt;
}

void readThreshold(const nlohmann::json& json, const char* key, std::optional<double>& output) {
    const auto iterator = json.find(key);
    if (iterator != json.end()) {
        const double value = iterator->get<double>();
        if (!std::isfinite(value) || value < 0.0) {
            throw std::invalid_argument(std::string{"threshold must be a finite nonnegative number: "} + key);
        }
        output = value;
    }
}

}

std::vector<MetricComparison> compareMetrics(const BenchmarkSummary& baseline,
                                             const BenchmarkSummary& candidate) {
    std::vector<MetricComparison> metrics;
    addMetric(metrics, "runtime", "Runtime", "ns", baseline.wallTimeNs, candidate.wallTimeNs, false);
    addMetric(metrics, "user_cpu", "User CPU", "ns", baseline.userTimeNs, candidate.userTimeNs, false);
    addMetric(
        metrics, "system_cpu", "System CPU", "ns", baseline.systemTimeNs, candidate.systemTimeNs, false);
    addMetric(metrics, "max_rss", "Maximum RSS", "bytes", baseline.maxRssBytes, candidate.maxRssBytes, false);
    addMetric(metrics,
              "voluntary_context_switches",
              "Voluntary context switches",
              "count",
              baseline.voluntaryContextSwitches,
              candidate.voluntaryContextSwitches,
              false);
    addMetric(metrics,
              "involuntary_context_switches",
              "Involuntary context switches",
              "count",
              baseline.involuntaryContextSwitches,
              candidate.involuntaryContextSwitches,
              false);
    addOptionalMetric(
        metrics, "throughput", "Throughput", "ops/s", baseline.throughput, candidate.throughput, true);
    addOptionalMetric(
        metrics, "p50_latency", "p50 latency", "us", baseline.p50LatencyUs, candidate.p50LatencyUs, false);
    addOptionalMetric(
        metrics, "p95_latency", "p95 latency", "us", baseline.p95LatencyUs, candidate.p95LatencyUs, false);
    addOptionalMetric(
        metrics, "p99_latency", "p99 latency", "us", baseline.p99LatencyUs, candidate.p99LatencyUs, false);
    addOptionalMetric(metrics,
                      "cpu_time_per_operation",
                      "CPU time / operation",
                      "us",
                      baseline.cpuTimePerOperationUs,
                      candidate.cpuTimePerOperationUs,
                      false);
    return metrics;
}

std::vector<ThresholdViolation> evaluateThresholds(const std::vector<MetricComparison>& metrics,
                                                   const Thresholds& thresholds) {
    std::vector<ThresholdViolation> violations;

    for (const MetricComparison& metric : metrics) {
        const std::optional<double> limit = thresholdFor(thresholds, metric.key);
        if (!limit) {
            continue;
        }
        if (!metric.changePercent) {
            continue;
        }
        const double regression = metric.higherIsBetter ? -*metric.changePercent : *metric.changePercent;
        if (regression > *limit) {
            violations.push_back({metric.key, *metric.changePercent, *limit});
        }
    }
    return violations;
}

Thresholds loadThresholds(const std::string& path) {
    std::ifstream input{path};
    if (!input) {
        throw std::runtime_error("could not open threshold configuration: " + path);
    }
    const nlohmann::json document = nlohmann::json::parse(input);
    const nlohmann::json& json = document.contains("thresholds") ? document.at("thresholds") : document;

    Thresholds thresholds;
    readThreshold(json, "runtime_pct", thresholds.maxRuntimeRegressionPercent);
    readThreshold(json, "p99_latency_pct", thresholds.maxP99LatencyRegressionPercent);
    readThreshold(json, "throughput_pct", thresholds.maxThroughputRegressionPercent);
    readThreshold(json, "cpu_time_pct", thresholds.maxCpuRegressionPercent);
    readThreshold(json, "max_rss_pct", thresholds.maxRssRegressionPercent);
    return thresholds;
}

}

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace perflens {

struct ProcessMetrics {
    std::chrono::nanoseconds wallTime{};
    std::chrono::nanoseconds userTime{};
    std::chrono::nanoseconds systemTime{};
    std::uint64_t maxRssBytes{};
    std::uint64_t minorFaults{};
    std::uint64_t majorFaults{};
    std::uint64_t voluntaryContextSwitches{};
    std::uint64_t involuntaryContextSwitches{};
};

struct RunResult {
    ProcessMetrics process;
};

struct Distribution {
    std::size_t count{};
    double mean{};
    double median{};
    double minimum{};
    double maximum{};
    double standardDeviation{};
    double coefficientOfVariation{};
};

struct BenchmarkSummary {
    Distribution wallTimeNs;
    Distribution userTimeNs;
    Distribution systemTimeNs;
    Distribution maxRssBytes;
    Distribution voluntaryContextSwitches;
    Distribution involuntaryContextSwitches;
};

struct BenchmarkResult {
    std::vector<RunResult> runs;
    BenchmarkSummary summary;
};

struct MetricComparison {
    std::string key;
    std::string label;
    std::string unit;
    double baseline{};
    double candidate{};
    std::optional<double> changePercent;
    bool higherIsBetter{};
};

struct ThresholdViolation {
    std::string metric;
    double changePercent{};
    double limitPercent{};
};

struct ComparisonResult {
    BenchmarkResult baseline;
    BenchmarkResult candidate;
    std::uint64_t seed{};
    std::vector<MetricComparison> metrics;
    std::vector<ThresholdViolation> violations;
};

void to_json(nlohmann::json& json, const ProcessMetrics& value);
void from_json(const nlohmann::json& json, ProcessMetrics& value);
void to_json(nlohmann::json& json, const RunResult& value);
void from_json(const nlohmann::json& json, RunResult& value);
void to_json(nlohmann::json& json, const Distribution& value);
void from_json(const nlohmann::json& json, Distribution& value);
void to_json(nlohmann::json& json, const BenchmarkSummary& value);
void from_json(const nlohmann::json& json, BenchmarkSummary& value);
void to_json(nlohmann::json& json, const BenchmarkResult& value);
void from_json(const nlohmann::json& json, BenchmarkResult& value);
void to_json(nlohmann::json& json, const MetricComparison& value);
void from_json(const nlohmann::json& json, MetricComparison& value);
void to_json(nlohmann::json& json, const ThresholdViolation& value);
void from_json(const nlohmann::json& json, ThresholdViolation& value);
void to_json(nlohmann::json& json, const ComparisonResult& value);
void from_json(const nlohmann::json& json, ComparisonResult& value);

}

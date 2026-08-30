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

struct PerfCounters {
    bool available{};
    std::string unavailableReason;
    std::uint64_t timeEnabledNs{};
    std::uint64_t timeRunningNs{};
    std::uint64_t cycles{};
    std::uint64_t instructions{};
    std::uint64_t branches{};
    std::uint64_t branchMisses{};
    std::uint64_t cacheReferences{};
    std::uint64_t cacheMisses{};
};

struct LatencyMetrics {
    std::optional<double> p50Us;
    std::optional<double> p95Us;
    std::optional<double> p99Us;
};

struct ApplicationMetrics {
    std::optional<std::uint64_t> operations;
    std::optional<double> throughput;
    LatencyMetrics latency;
};

struct RunResult {
    ProcessMetrics process;
    PerfCounters counters;
    std::optional<ApplicationMetrics> application;
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
    std::optional<Distribution> cycles;
    std::optional<Distribution> instructions;
    std::optional<Distribution> branches;
    std::optional<Distribution> branchMisses;
    std::optional<Distribution> cacheReferences;
    std::optional<Distribution> cacheMisses;
    std::optional<Distribution> ipc;
    std::optional<Distribution> throughput;
    std::optional<Distribution> p50LatencyUs;
    std::optional<Distribution> p95LatencyUs;
    std::optional<Distribution> p99LatencyUs;
    std::optional<Distribution> cpuTimePerOperationUs;
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
void to_json(nlohmann::json& json, const PerfCounters& value);
void from_json(const nlohmann::json& json, PerfCounters& value);
void to_json(nlohmann::json& json, const LatencyMetrics& value);
void from_json(const nlohmann::json& json, LatencyMetrics& value);
void to_json(nlohmann::json& json, const ApplicationMetrics& value);
void from_json(const nlohmann::json& json, ApplicationMetrics& value);
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

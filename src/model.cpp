#include "perflens/model.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace perflens {
namespace {

std::int64_t nanoseconds(const std::chrono::nanoseconds duration) {
    return duration.count();
}

template <typename T>
void putOptional(nlohmann::json& json, const std::string_view key, const std::optional<T>& value) {
    if (value) {
        json[std::string{key}] = *value;
    }
}

template <typename T>
void getOptional(const nlohmann::json& json, const std::string_view key, std::optional<T>& value) {
    const auto iterator = json.find(key);
    if (iterator != json.end() && !iterator->is_null()) {
        value = iterator->template get<T>();
    } else {
        value.reset();
    }
}

}

void to_json(nlohmann::json& json, const ProcessMetrics& value) {
    json = nlohmann::json{{"wall_time_ns", nanoseconds(value.wallTime)},
                          {"user_time_ns", nanoseconds(value.userTime)},
                          {"system_time_ns", nanoseconds(value.systemTime)},
                          {"max_rss_bytes", value.maxRssBytes},
                          {"minor_faults", value.minorFaults},
                          {"major_faults", value.majorFaults},
                          {"voluntary_context_switches", value.voluntaryContextSwitches},
                          {"involuntary_context_switches", value.involuntaryContextSwitches}};
}

void from_json(const nlohmann::json& json, ProcessMetrics& value) {
    value.wallTime = std::chrono::nanoseconds{json.at("wall_time_ns").get<std::int64_t>()};
    value.userTime = std::chrono::nanoseconds{json.at("user_time_ns").get<std::int64_t>()};
    value.systemTime = std::chrono::nanoseconds{json.at("system_time_ns").get<std::int64_t>()};
    json.at("max_rss_bytes").get_to(value.maxRssBytes);
    json.at("minor_faults").get_to(value.minorFaults);
    json.at("major_faults").get_to(value.majorFaults);
    json.at("voluntary_context_switches").get_to(value.voluntaryContextSwitches);
    json.at("involuntary_context_switches").get_to(value.involuntaryContextSwitches);
}

void to_json(nlohmann::json& json, const PerfCounters& value) {
    json = nlohmann::json{{"available", value.available},
                          {"unavailable_reason", value.unavailableReason},
                          {"time_enabled_ns", value.timeEnabledNs},
                          {"time_running_ns", value.timeRunningNs},
                          {"cycles", value.cycles},
                          {"instructions", value.instructions},
                          {"branches", value.branches},
                          {"branch_misses", value.branchMisses},
                          {"cache_references", value.cacheReferences},
                          {"cache_misses", value.cacheMisses}};
}

void from_json(const nlohmann::json& json, PerfCounters& value) {
    json.at("available").get_to(value.available);
    json.at("unavailable_reason").get_to(value.unavailableReason);
    json.at("time_enabled_ns").get_to(value.timeEnabledNs);
    json.at("time_running_ns").get_to(value.timeRunningNs);
    json.at("cycles").get_to(value.cycles);
    json.at("instructions").get_to(value.instructions);
    json.at("branches").get_to(value.branches);
    json.at("branch_misses").get_to(value.branchMisses);
    json.at("cache_references").get_to(value.cacheReferences);
    json.at("cache_misses").get_to(value.cacheMisses);
}

void to_json(nlohmann::json& json, const LatencyMetrics& value) {
    json = nlohmann::json::object();
    putOptional(json, "p50_us", value.p50Us);
    putOptional(json, "p95_us", value.p95Us);
    putOptional(json, "p99_us", value.p99Us);
}

void from_json(const nlohmann::json& json, LatencyMetrics& value) {
    getOptional(json, "p50_us", value.p50Us);
    getOptional(json, "p95_us", value.p95Us);
    getOptional(json, "p99_us", value.p99Us);
}

void to_json(nlohmann::json& json, const ApplicationMetrics& value) {
    json = nlohmann::json{{"latency", value.latency}};
    putOptional(json, "operations", value.operations);
    putOptional(json, "throughput", value.throughput);
}

void from_json(const nlohmann::json& json, ApplicationMetrics& value) {
    getOptional(json, "operations", value.operations);
    getOptional(json, "throughput", value.throughput);
    const auto latency = json.find("latency");
    value.latency = latency == json.end() ? LatencyMetrics{} : latency->get<LatencyMetrics>();
}

void to_json(nlohmann::json& json, const FunctionSample& value) {
    json = nlohmann::json{{"module", value.module}, {"function", value.function}, {"samples", value.samples}};
}

void from_json(const nlohmann::json& json, FunctionSample& value) {
    json.at("module").get_to(value.module);
    json.at("function").get_to(value.function);
    json.at("samples").get_to(value.samples);
}

void to_json(nlohmann::json& json, const CpuProfile& value) {
    json = nlohmann::json{{"available", value.available},
                          {"unavailable_reason", value.unavailableReason},
                          {"lost_samples", value.lostSamples},
                          {"functions", value.functions}};
}

void from_json(const nlohmann::json& json, CpuProfile& value) {
    json.at("available").get_to(value.available);
    json.at("unavailable_reason").get_to(value.unavailableReason);
    json.at("lost_samples").get_to(value.lostSamples);
    json.at("functions").get_to(value.functions);
}

void to_json(nlohmann::json& json, const RunResult& value) {
    json = nlohmann::json{
        {"process", value.process}, {"counters", value.counters}, {"cpu_profile", value.cpuProfile}};
    putOptional(json, "application", value.application);
}

void from_json(const nlohmann::json& json, RunResult& value) {
    json.at("process").get_to(value.process);
    json.at("counters").get_to(value.counters);
    json.at("cpu_profile").get_to(value.cpuProfile);
    getOptional(json, "application", value.application);
}

void to_json(nlohmann::json& json, const Distribution& value) {
    json = nlohmann::json{{"count", value.count},
                          {"mean", value.mean},
                          {"median", value.median},
                          {"minimum", value.minimum},
                          {"maximum", value.maximum},
                          {"standard_deviation", value.standardDeviation},
                          {"coefficient_of_variation", value.coefficientOfVariation}};
}

void from_json(const nlohmann::json& json, Distribution& value) {
    json.at("count").get_to(value.count);
    json.at("mean").get_to(value.mean);
    json.at("median").get_to(value.median);
    json.at("minimum").get_to(value.minimum);
    json.at("maximum").get_to(value.maximum);
    json.at("standard_deviation").get_to(value.standardDeviation);
    json.at("coefficient_of_variation").get_to(value.coefficientOfVariation);
}

void to_json(nlohmann::json& json, const BenchmarkSummary& value) {
    json = nlohmann::json{{"wall_time_ns", value.wallTimeNs},
                          {"user_time_ns", value.userTimeNs},
                          {"system_time_ns", value.systemTimeNs},
                          {"max_rss_bytes", value.maxRssBytes},
                          {"voluntary_context_switches", value.voluntaryContextSwitches},
                          {"involuntary_context_switches", value.involuntaryContextSwitches}};
    putOptional(json, "cycles", value.cycles);
    putOptional(json, "instructions", value.instructions);
    putOptional(json, "branches", value.branches);
    putOptional(json, "branch_misses", value.branchMisses);
    putOptional(json, "cache_references", value.cacheReferences);
    putOptional(json, "cache_misses", value.cacheMisses);
    putOptional(json, "ipc", value.ipc);
    putOptional(json, "throughput", value.throughput);
    putOptional(json, "p50_latency_us", value.p50LatencyUs);
    putOptional(json, "p95_latency_us", value.p95LatencyUs);
    putOptional(json, "p99_latency_us", value.p99LatencyUs);
    putOptional(json, "cpu_time_per_operation_us", value.cpuTimePerOperationUs);
}

void from_json(const nlohmann::json& json, BenchmarkSummary& value) {
    json.at("wall_time_ns").get_to(value.wallTimeNs);
    json.at("user_time_ns").get_to(value.userTimeNs);
    json.at("system_time_ns").get_to(value.systemTimeNs);
    json.at("max_rss_bytes").get_to(value.maxRssBytes);
    json.at("voluntary_context_switches").get_to(value.voluntaryContextSwitches);
    json.at("involuntary_context_switches").get_to(value.involuntaryContextSwitches);
    getOptional(json, "cycles", value.cycles);
    getOptional(json, "instructions", value.instructions);
    getOptional(json, "branches", value.branches);
    getOptional(json, "branch_misses", value.branchMisses);
    getOptional(json, "cache_references", value.cacheReferences);
    getOptional(json, "cache_misses", value.cacheMisses);
    getOptional(json, "ipc", value.ipc);
    getOptional(json, "throughput", value.throughput);
    getOptional(json, "p50_latency_us", value.p50LatencyUs);
    getOptional(json, "p95_latency_us", value.p95LatencyUs);
    getOptional(json, "p99_latency_us", value.p99LatencyUs);
    getOptional(json, "cpu_time_per_operation_us", value.cpuTimePerOperationUs);
}

void to_json(nlohmann::json& json, const BenchmarkResult& value) {
    json = nlohmann::json{{"runs", value.runs}, {"summary", value.summary}};
}

void from_json(const nlohmann::json& json, BenchmarkResult& value) {
    json.at("runs").get_to(value.runs);
    json.at("summary").get_to(value.summary);
}

void to_json(nlohmann::json& json, const MetricComparison& value) {
    json = nlohmann::json{{"key", value.key},
                          {"label", value.label},
                          {"unit", value.unit},
                          {"baseline", value.baseline},
                          {"candidate", value.candidate},
                          {"higher_is_better", value.higherIsBetter}};
    putOptional(json, "change_percent", value.changePercent);
}

void from_json(const nlohmann::json& json, MetricComparison& value) {
    json.at("key").get_to(value.key);
    json.at("label").get_to(value.label);
    json.at("unit").get_to(value.unit);
    json.at("baseline").get_to(value.baseline);
    json.at("candidate").get_to(value.candidate);
    getOptional(json, "change_percent", value.changePercent);
    json.at("higher_is_better").get_to(value.higherIsBetter);
}

void to_json(nlohmann::json& json, const ProfileChange& value) {
    json = nlohmann::json{{"module", value.module},
                          {"function", value.function},
                          {"baseline_percent", value.baselinePercent},
                          {"candidate_percent", value.candidatePercent},
                          {"change_percentage_points", value.changePercentagePoints}};
}

void from_json(const nlohmann::json& json, ProfileChange& value) {
    json.at("module").get_to(value.module);
    json.at("function").get_to(value.function);
    json.at("baseline_percent").get_to(value.baselinePercent);
    json.at("candidate_percent").get_to(value.candidatePercent);
    json.at("change_percentage_points").get_to(value.changePercentagePoints);
}

void to_json(nlohmann::json& json, const ThresholdViolation& value) {
    json = nlohmann::json{{"metric", value.metric},
                          {"change_percent", value.changePercent},
                          {"limit_percent", value.limitPercent}};
}

void from_json(const nlohmann::json& json, ThresholdViolation& value) {
    json.at("metric").get_to(value.metric);
    json.at("change_percent").get_to(value.changePercent);
    json.at("limit_percent").get_to(value.limitPercent);
}

void to_json(nlohmann::json& json, const ComparisonResult& value) {
    json = nlohmann::json{{"baseline", value.baseline},
                          {"candidate", value.candidate},
                          {"seed", value.seed},
                          {"metrics", value.metrics},
                          {"profile_changes", value.profileChanges},
                          {"violations", value.violations}};
}

void from_json(const nlohmann::json& json, ComparisonResult& value) {
    json.at("baseline").get_to(value.baseline);
    json.at("candidate").get_to(value.candidate);
    json.at("seed").get_to(value.seed);
    json.at("metrics").get_to(value.metrics);
    json.at("profile_changes").get_to(value.profileChanges);
    json.at("violations").get_to(value.violations);
}

}

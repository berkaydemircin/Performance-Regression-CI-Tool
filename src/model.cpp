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

void to_json(nlohmann::json& json, const RunResult& value) {
    json = nlohmann::json{{"process", value.process}};
    putOptional(json, "application", value.application);
}

void from_json(const nlohmann::json& json, RunResult& value) {
    json.at("process").get_to(value.process);
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
                          {"violations", value.violations}};
}

void from_json(const nlohmann::json& json, ComparisonResult& value) {
    json.at("baseline").get_to(value.baseline);
    json.at("candidate").get_to(value.candidate);
    json.at("seed").get_to(value.seed);
    json.at("metrics").get_to(value.metrics);
    json.at("violations").get_to(value.violations);
}

}

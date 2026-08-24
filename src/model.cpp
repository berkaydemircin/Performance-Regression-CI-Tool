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

void to_json(nlohmann::json& json, const RunResult& value) {
    json = nlohmann::json{{"process", value.process}};
}

void from_json(const nlohmann::json& json, RunResult& value) {
    json.at("process").get_to(value.process);
}

}

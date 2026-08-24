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

void to_json(nlohmann::json& json, const ProcessMetrics& value);
void from_json(const nlohmann::json& json, ProcessMetrics& value);
void to_json(nlohmann::json& json, const RunResult& value);
void from_json(const nlohmann::json& json, RunResult& value);

}

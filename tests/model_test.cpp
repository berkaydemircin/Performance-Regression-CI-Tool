#include "perflens/model.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>

TEST_CASE("run results round trip through JSON") {
    perflens::RunResult original;
    original.process.wallTime = std::chrono::nanoseconds{123};
    original.process.userTime = std::chrono::nanoseconds{45};
    original.process.systemTime = std::chrono::nanoseconds{6};
    original.process.maxRssBytes = 4096;
    original.process.minorFaults = 7;
    original.process.majorFaults = 8;
    original.process.voluntaryContextSwitches = 9;
    original.process.involuntaryContextSwitches = 10;

    const nlohmann::json json = original;
    const perflens::RunResult decoded = json.get<perflens::RunResult>();

    CHECK(decoded.process.wallTime == original.process.wallTime);
    CHECK(decoded.process.userTime == original.process.userTime);
    CHECK(decoded.process.systemTime == original.process.systemTime);
    CHECK(decoded.process.maxRssBytes == original.process.maxRssBytes);
    CHECK(decoded.process.minorFaults == original.process.minorFaults);
    CHECK(decoded.process.majorFaults == original.process.majorFaults);
    CHECK(decoded.process.voluntaryContextSwitches == original.process.voluntaryContextSwitches);
    CHECK(decoded.process.involuntaryContextSwitches == original.process.involuntaryContextSwitches);
}

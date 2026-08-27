#include "perflens/analysis.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <vector>

TEST_CASE("statistics describe repeated measurements") {
    const perflens::Distribution result = perflens::summarize({1.0, 2.0, 3.0, 4.0});

    CHECK(result.count == 4);
    CHECK(result.mean == 2.5);
    CHECK(result.median == 2.5);
    CHECK(result.minimum == 1.0);
    CHECK(result.maximum == 4.0);
    CHECK(result.standardDeviation == Catch::Approx(1.1180339887));
    CHECK(result.coefficientOfVariation == Catch::Approx(44.72135955));
}

TEST_CASE("run summaries include derived application metrics") {
    perflens::RunResult run;
    run.process.wallTime = std::chrono::milliseconds{10};
    run.process.userTime = std::chrono::milliseconds{4};
    run.process.systemTime = std::chrono::milliseconds{1};
    run.application = perflens::ApplicationMetrics{};
    run.application->operations = 1000;
    run.application->throughput = 100'000.0;
    run.application->latency.p99Us = 20.0;

    const perflens::BenchmarkSummary summary = perflens::summarizeRuns({run});

    REQUIRE(summary.cpuTimePerOperationUs);
    CHECK(summary.cpuTimePerOperationUs->median == 5.0);
    REQUIRE(summary.p99LatencyUs);
    CHECK(summary.p99LatencyUs->median == 20.0);
}

TEST_CASE("thresholds use metric direction") {
    const std::vector<perflens::MetricComparison> metrics{
        {"runtime", "Runtime", "ns", 100.0, 112.0, 12.0, false},
        {"throughput", "Throughput", "ops/s", 100.0, 92.0, -8.0, true},
    };
    perflens::Thresholds thresholds;
    thresholds.maxRuntimeRegressionPercent = 10.0;
    thresholds.maxThroughputRegressionPercent = 5.0;

    const std::vector<perflens::ThresholdViolation> violations =
        perflens::evaluateThresholds(metrics, thresholds);

    REQUIRE(violations.size() == 2);
    CHECK(violations[0].metric == "runtime");
    CHECK(violations[1].metric == "throughput");
}

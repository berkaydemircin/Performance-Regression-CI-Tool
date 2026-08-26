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

TEST_CASE("thresholds use metric direction") {
    const std::vector<perflens::MetricComparison> metrics{
        {"runtime", "Runtime", "ns", 100.0, 112.0, 12.0, false},
    };
    perflens::Thresholds thresholds;
    thresholds.maxRuntimeRegressionPercent = 10.0;

    const std::vector<perflens::ThresholdViolation> violations =
        perflens::evaluateThresholds(metrics, thresholds);

    REQUIRE(violations.size() == 1);
    CHECK(violations[0].metric == "runtime");
}

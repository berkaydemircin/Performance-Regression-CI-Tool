#include "perflens/perf_counter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include <linux/perf_event.h>

TEST_CASE("multiplexed counters scale by scheduled time") {
    CHECK(perflens::scaleCounter(100, 1000, 500) == 200);
    CHECK(perflens::scaleCounter(100, 500, 500) == 100);
    CHECK(perflens::scaleCounter(100, 400, 500) == 100);
    CHECK_THROWS_AS(perflens::scaleCounter(100, 500, 0), std::invalid_argument);
}

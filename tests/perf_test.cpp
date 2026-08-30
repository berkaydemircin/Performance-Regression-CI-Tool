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

TEST_CASE("counter scaling preserves the unsigned range") {
    const std::uint64_t high = std::uint64_t{1} << 62;
    CHECK(perflens::scaleCounter(high, 3, 1) == high * 3);
    CHECK(perflens::scaleCounter(high, 8, 1) == std::numeric_limits<std::uint64_t>::max());
}

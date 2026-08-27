#include "perflens/workload_metrics.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdio>
#include <fstream>
#include <optional>
#include <string>

#include <unistd.h>

namespace {

class TemporaryFile {
  public:
    TemporaryFile() {
        std::array<char, 32> pattern{};
        std::snprintf(pattern.data(), pattern.size(), "/tmp/perflens-test-XXXXXX");
        const int descriptor = mkstemp(pattern.data());
        REQUIRE(descriptor != -1);
        close(descriptor);
        path = pattern.data();
    }
    ~TemporaryFile() {
        unlink(path.c_str());
    }
    std::string path;
};

}

TEST_CASE("workload metrics protocol imports valid measurements") {
    TemporaryFile file;
    {
        std::ofstream output{file.path};
        output << R"({"operations":1000,"throughput":50000,"latency":{"p50_us":10,"p95_us":20,"p99_us":30}})";
    }

    const std::optional<perflens::ApplicationMetrics> metrics = perflens::loadApplicationMetrics(file.path);

    REQUIRE(metrics);
    REQUIRE(metrics->operations);
    CHECK(*metrics->operations == 1000);
    REQUIRE(metrics->latency.p99Us);
    CHECK(*metrics->latency.p99Us == 30.0);
}

TEST_CASE("workload latency percentiles must be ordered") {
    TemporaryFile file;
    {
        std::ofstream output{file.path};
        output << R"({"latency":{"p50_us":30,"p95_us":20,"p99_us":10}})";
    }

    CHECK_THROWS_AS(perflens::loadApplicationMetrics(file.path), std::invalid_argument);
}

TEST_CASE("operation counts reject negative and fractional numbers") {
    TemporaryFile file;
    for (const std::string value : {"-1", "1.5", "0"}) {
        std::ofstream{file.path} << "{\"operations\":" << value << "}";
        CHECK_THROWS_AS(perflens::loadApplicationMetrics(file.path), std::invalid_argument);
    }
}

TEST_CASE("p50 cannot exceed p99 when p95 is omitted") {
    TemporaryFile file;
    std::ofstream{file.path} << R"({"latency":{"p50_us":20,"p99_us":10}})";
    CHECK_THROWS_AS(perflens::loadApplicationMetrics(file.path), std::invalid_argument);
}

#include "perflens/process_runner.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <stdexcept>
#include <string>

using namespace std::chrono_literals;

TEST_CASE("service lifecycle waits runs workload and stops the server") {
    perflens::RunOptions options;
    options.collectPerfCounters = false;
    options.timeout = 5s;
    perflens::RunOptions::ServiceLifecycle service;
    service.workloadCommand = {"./example_workload", "--port", "19191", "--operations", "100"};
    service.readyTcp = perflens::RunOptions::TcpEndpoint{"127.0.0.1", 19191};
    service.readyTimeout = 2s;
    options.service = service;

    const perflens::ProcessOutcome outcome =
        perflens::ProcessRunner{}.run({"./example_server", "--port", "19191", "--mode", "baseline"}, options);

    CHECK(outcome.succeeded());
    REQUIRE(outcome.result.application);
    REQUIRE(outcome.result.application->operations);
    CHECK(*outcome.result.application->operations == 100);
}

TEST_CASE("service readiness timeout is a runtime failure") {
    perflens::RunOptions options;
    options.collectPerfCounters = false;
    perflens::RunOptions::ServiceLifecycle service;
    service.workloadCommand = {"/bin/true"};
    service.readyTcp = perflens::RunOptions::TcpEndpoint{"127.0.0.1", 1};
    service.readyTimeout = 30ms;
    service.shutdownGrace = 10ms;
    options.service = service;

    try {
        static_cast<void>(perflens::ProcessRunner{}.run({"/bin/sh", "-c", "sleep 1"}, options));
        FAIL("service readiness did not time out");
    } catch (const std::runtime_error& error) {
        CHECK(std::string{error.what()} == "service readiness timed out");
    }
}

TEST_CASE("a service that exits before readiness fails") {
    perflens::RunOptions options;
    options.collectPerfCounters = false;
    perflens::RunOptions::ServiceLifecycle service;
    service.workloadCommand = {"/bin/true"};
    service.startupDelay = 100ms;
    options.service = service;

    CHECK_THROWS_AS(perflens::ProcessRunner{}.run({"/bin/true"}, options), std::runtime_error);
}

#include "perflens/analysis.hpp"
#include "perflens/benchmark_runner.hpp"
#include "perflens/process_runner.hpp"
#include "perflens/report.hpp"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::chrono::nanoseconds parseDuration(const std::string_view text, const bool allowZero = false) {
    if (text.empty()) {
        throw std::invalid_argument("duration must not be empty");
    }
    std::size_t parsed = 0;
    const double value = std::stod(std::string{text}, &parsed);
    if (!std::isfinite(value) || value < 0.0 || (!allowZero && value == 0.0)) {
        throw std::invalid_argument(allowZero ? "duration must be finite and nonnegative"
                                              : "duration must be positive and finite");
    }

    const std::string_view unit = text.substr(parsed);
    double nanoseconds = 0.0;
    if (unit == "ns") {
        nanoseconds = value;
    } else if (unit == "us") {
        nanoseconds = value * 1'000.0;
    } else if (unit == "ms") {
        nanoseconds = value * 1'000'000.0;
    } else if (unit == "s") {
        nanoseconds = value * 1'000'000'000.0;
    } else if (unit == "m") {
        nanoseconds = value * 60'000'000'000.0;
    } else {
        throw std::invalid_argument("duration unit must be ns, us, ms, s, or m");
    }
    if (nanoseconds >= static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
        throw std::out_of_range("duration is too large");
    }
    return std::chrono::nanoseconds{static_cast<std::int64_t>(nanoseconds)};
}

std::vector<std::string> shellCommand(const std::string& command) {
    if (command.empty()) {
        throw std::invalid_argument("command must not be empty");
    }
    // keep the measured PID on the executable
    return {"/bin/sh", "-c", "exec " + command};
}

perflens::RunOptions::TcpEndpoint parseEndpoint(const std::string& value) {
    const std::size_t separator = value.rfind(':');
    if (separator == std::string::npos || separator == 0 || separator + 1 == value.size()) {
        throw std::invalid_argument("TCP readiness must use HOST:PORT");
    }
    std::string host = value.substr(0, separator);
    if (host.size() >= 2 && host.front() == '[' && host.back() == ']') {
        host = host.substr(1, host.size() - 2);
    }
    std::size_t parsed = 0;
    const unsigned long port = std::stoul(value.substr(separator + 1), &parsed);
    if (parsed != value.size() - separator - 1 || port == 0 || port > 65535) {
        throw std::invalid_argument("TCP readiness port must be between 1 and 65535");
    }
    return {std::move(host), static_cast<std::uint16_t>(port)};
}

template <typename T> void writeJson(const std::string& path, const T& result) {
    std::ofstream output{path};
    if (!output) {
        throw std::runtime_error("could not open JSON output: " + path);
    }
    output << nlohmann::json(result).dump(2) << '\n';
    if (!output) {
        throw std::runtime_error("could not write JSON output: " + path);
    }
}

perflens::RunOptions makeRunOptions(const std::optional<std::string>& timeout,
                                    const std::string& gracePeriod,
                                    const std::optional<unsigned int> cpu,
                                    const bool noCounters,
                                    const bool requirePerf,
                                    const std::uint64_t profileFrequency) {
    perflens::RunOptions options;
    if (timeout) {
        options.timeout = parseDuration(*timeout);
    }
    options.terminationGrace =
        std::chrono::duration_cast<std::chrono::milliseconds>(parseDuration(gracePeriod, true));
    options.cpu = cpu;
    options.collectPerfCounters = !noCounters;
    options.requirePerf = requirePerf;
    options.samplingFrequency = profileFrequency;
    return options;
}

void overrideThreshold(std::optional<double>& target, const std::optional<double>& value, const char* name) {
    if (!value) {
        return;
    }
    if (!std::isfinite(*value) || *value < 0.0) {
        throw std::invalid_argument(std::string{name} + " must be finite and nonnegative");
    }
    target = value;
}

}

int main(const int argc, char** argv) {
    CLI::App app{"Controlled Linux performance regression measurements", "perflens"};
    app.require_subcommand(1);
    app.set_version_flag("--version", "PerfLens 0.1.0");

    std::vector<std::string> runCommand;
    std::size_t runRepeat = 1;
    std::size_t runWarmup = 0;
    std::optional<unsigned int> runCpu;
    std::optional<std::string> runTimeout;
    std::string runGrace = "500ms";
    bool runNoCounters = false;
    bool runRequirePerf = false;
    std::uint64_t runProfileFrequency = 0;
    std::optional<std::string> runJson;

    CLI::App* run = app.add_subcommand("run", "Execute a terminating target and report resource usage");
    run->positionals_at_end();
    run->add_option("command", runCommand, "Target command and arguments")->required()->expected(-1);
    run->add_option("--repeat", runRepeat, "Measured executions")->check(CLI::PositiveNumber);
    run->add_option("--warmup", runWarmup, "Discarded warmup executions");
    run->add_option("--cpu", runCpu, "CPU to pin the target to");
    run->add_option("--timeout", runTimeout, "Stop each target after this duration");
    run->add_option("--grace-period", runGrace, "Wait before sending SIGKILL");
    run->add_flag("--no-counters", runNoCounters, "Disable hardware-counter collection");
    run->add_flag("--require-perf", runRequirePerf, "Fail when perf events are unavailable");
    run->add_option("--profile-frequency", runProfileFrequency, "Collect CPU samples at this frequency");
    run->add_option("--json", runJson, "Write the benchmark result as JSON");

    std::string baselineCommand;
    std::string candidateCommand;
    std::optional<std::string> workloadCommand;
    std::size_t compareRepeat = 8;
    std::size_t compareWarmup = 2;
    std::optional<std::uint64_t> compareSeed;
    std::optional<unsigned int> compareCpu;
    std::optional<std::string> compareTimeout;
    std::string compareGrace = "500ms";
    bool compareNoCounters = false;
    bool compareRequirePerf = false;
    std::uint64_t compareProfileFrequency = 0;
    std::optional<std::string> compareJson;
    std::string startupDelay = "0ms";
    std::optional<std::string> readyTcp;
    std::string readyTimeout = "5s";
    std::string shutdownGrace = "500ms";
    std::optional<std::string> thresholdConfig;
    std::optional<double> maxRuntimeRegression;
    std::optional<double> maxP99Regression;
    std::optional<double> maxThroughputRegression;
    std::optional<double> maxCpuRegression;
    std::optional<double> maxRssRegression;

    CLI::App* compare = app.add_subcommand("compare", "Run a balanced baseline and candidate comparison");
    compare->add_option("--baseline", baselineCommand, "Baseline command line")->required();
    compare->add_option("--candidate", candidateCommand, "Candidate command line")->required();
    compare->add_option("--workload", workloadCommand, "Workload command for long-running services");
    compare->add_option("--repeat", compareRepeat, "Measured executions per variant")
        ->check(CLI::PositiveNumber);
    compare->add_option("--warmup", compareWarmup, "Discarded executions per variant");
    compare->add_option("--seed", compareSeed, "Reproducible balanced-order seed");
    compare->add_option("--cpu", compareCpu, "CPU to pin each target to");
    compare->add_option("--timeout", compareTimeout, "Stop each run after this duration");
    compare->add_option("--grace-period", compareGrace, "Wait before sending SIGKILL");
    compare->add_flag("--no-counters", compareNoCounters, "Disable hardware-counter collection");
    compare->add_flag("--require-perf", compareRequirePerf, "Fail when perf events are unavailable");
    compare->add_option(
        "--profile-frequency", compareProfileFrequency, "Collect CPU samples at this frequency");
    compare->add_option("--json", compareJson, "Write the comparison result as JSON");
    compare->add_option("--startup-delay", startupDelay, "Fixed delay before a service workload");
    compare->add_option("--ready-tcp", readyTcp, "Wait for HOST:PORT before a service workload");
    compare->add_option("--ready-timeout", readyTimeout, "Maximum service readiness wait");
    compare->add_option(
        "--shutdown-grace", shutdownGrace, "Wait before killing a service after its workload");
    compare->add_option("--config", thresholdConfig, "JSON threshold configuration");
    compare->add_option(
        "--max-runtime-regression", maxRuntimeRegression, "Maximum runtime increase in percent");
    compare->add_option("--max-p99-regression", maxP99Regression, "Maximum p99 latency increase in percent");
    compare->add_option(
        "--max-throughput-regression", maxThroughputRegression, "Maximum throughput decrease in percent");
    compare->add_option(
        "--max-cpu-regression", maxCpuRegression, "Maximum CPU-per-operation increase in percent");
    compare->add_option("--max-rss-regression", maxRssRegression, "Maximum RSS increase in percent");

    CLI11_PARSE(app, argc, argv);

    try {
        if (*run) {
            const perflens::RunOptions options = makeRunOptions(
                runTimeout, runGrace, runCpu, runNoCounters, runRequirePerf, runProfileFrequency);
            const perflens::BenchmarkResult result =
                perflens::runBenchmark(runCommand, {runRepeat, runWarmup, std::nullopt}, options);
            perflens::printBenchmarkReport(std::cout, result);
            if (runJson) {
                writeJson(*runJson, result);
            }
            return 0;
        }

        perflens::RunOptions options = makeRunOptions(compareTimeout,
                                                      compareGrace,
                                                      compareCpu,
                                                      compareNoCounters,
                                                      compareRequirePerf,
                                                      compareProfileFrequency);
        if (workloadCommand) {
            perflens::RunOptions::ServiceLifecycle service;
            service.workloadCommand = shellCommand(*workloadCommand);
            service.startupDelay =
                std::chrono::duration_cast<std::chrono::milliseconds>(parseDuration(startupDelay, true));
            if (readyTcp) {
                service.readyTcp = parseEndpoint(*readyTcp);
            }
            service.readyTimeout =
                std::chrono::duration_cast<std::chrono::milliseconds>(parseDuration(readyTimeout));
            service.shutdownGrace =
                std::chrono::duration_cast<std::chrono::milliseconds>(parseDuration(shutdownGrace, true));
            options.service = std::move(service);
        } else if (readyTcp || startupDelay != "0ms") {
            throw std::invalid_argument("service readiness options require --workload");
        }

        perflens::Thresholds thresholds =
            thresholdConfig ? perflens::loadThresholds(*thresholdConfig) : perflens::Thresholds{};
        overrideThreshold(thresholds.maxRuntimeRegressionPercent, maxRuntimeRegression, "runtime threshold");
        overrideThreshold(thresholds.maxP99LatencyRegressionPercent, maxP99Regression, "p99 threshold");
        overrideThreshold(
            thresholds.maxThroughputRegressionPercent, maxThroughputRegression, "throughput threshold");
        overrideThreshold(thresholds.maxCpuRegressionPercent, maxCpuRegression, "CPU threshold");
        overrideThreshold(thresholds.maxRssRegressionPercent, maxRssRegression, "RSS threshold");

        const perflens::ComparisonResult result =
            perflens::runComparison(shellCommand(baselineCommand),
                                    shellCommand(candidateCommand),
                                    {compareRepeat, compareWarmup, compareSeed},
                                    options,
                                    thresholds);
        perflens::printComparisonReport(std::cout, result);
        if (compareJson) {
            writeJson(*compareJson, result);
        }
        return result.violations.empty() ? 0 : 1;
    } catch (const perflens::BenchmarkError& error) {
        std::cerr << "perflens: " << error.what() << '\n';
        return error.exitCode();
    } catch (const std::exception& error) {
        std::cerr << "perflens: " << error.what() << '\n';
        return 2;
    }
}

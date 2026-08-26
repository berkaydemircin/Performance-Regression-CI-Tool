#include "perflens/report.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <numeric>
#include <ostream>
#include <sstream>
#include <string>

namespace perflens {
namespace {

std::string formatDuration(const double nanoseconds) {
    std::ostringstream output;
    output << std::fixed;
    if (nanoseconds >= 1'000'000'000.0) {
        output << std::setprecision(3) << nanoseconds / 1'000'000'000.0 << " s";
    } else if (nanoseconds >= 1'000'000.0) {
        output << std::setprecision(3) << nanoseconds / 1'000'000.0 << " ms";
    } else if (nanoseconds >= 1'000.0) {
        output << std::setprecision(3) << nanoseconds / 1'000.0 << " us";
    } else {
        output << std::setprecision(0) << nanoseconds << " ns";
    }
    return output.str();
}

std::string formatBytes(const double bytes) {
    static constexpr std::array<const char*, 4> units{"B", "KiB", "MiB", "GiB"};
    double value = bytes;
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < units.size()) {
        value /= 1024.0;
        ++unit;
    }
    std::ostringstream output;
    output << std::fixed << std::setprecision(unit == 0 ? 0 : 2) << value << ' ' << units[unit];
    return output.str();
}

std::string formatCount(const double value) {
    std::ostringstream output;
    if (std::abs(value) >= 1'000'000'000.0) {
        output << std::fixed << std::setprecision(3) << value / 1'000'000'000.0 << " B";
    } else if (std::abs(value) >= 1'000'000.0) {
        output << std::fixed << std::setprecision(3) << value / 1'000'000.0 << " M";
    } else if (std::abs(value) >= 1'000.0) {
        output << std::fixed << std::setprecision(3) << value / 1'000.0 << " K";
    } else {
        output << std::fixed << std::setprecision(value == std::floor(value) ? 0 : 3) << value;
    }
    return output.str();
}

std::string formatMetric(const std::string& unit, const double value) {
    if (unit == "ns") {
        return formatDuration(value);
    }
    if (unit == "bytes") {
        return formatBytes(value);
    }
    if (unit == "us") {
        std::ostringstream output;
        output << std::fixed << std::setprecision(3) << value << " us";
        return output.str();
    }
    if (unit == "ops/s") {
        return formatCount(value) + "/s";
    }
    if (unit == "ratio") {
        std::ostringstream output;
        output << std::fixed << std::setprecision(3) << value;
        return output.str();
    }
    return formatCount(value);
}

std::string formatChange(const double value) {
    std::ostringstream output;
    if (std::isinf(value)) {
        return value > 0 ? "+inf" : "-inf";
    }
    output << std::showpos << std::fixed << std::setprecision(1) << value << '%';
    return output.str();
}

void printRow(std::ostream& output, const std::string& label, const std::string& value) {
    output << std::left << std::setw(34) << label << std::right << std::setw(16) << value << '\n';
}

void printDistribution(std::ostream& output,
                       const std::string& label,
                       const Distribution& distribution,
                       const std::string& unit) {
    output << '\n' << label << '\n';
    printRow(output, "runs", std::to_string(distribution.count));
    printRow(output, "median", formatMetric(unit, distribution.median));
    printRow(output, "mean", formatMetric(unit, distribution.mean));
    printRow(output, "stddev", formatMetric(unit, distribution.standardDeviation));
    std::ostringstream variation;
    variation << std::fixed << std::setprecision(2) << distribution.coefficientOfVariation << '%';
    printRow(output, "CV", variation.str());
    printRow(output, "min", formatMetric(unit, distribution.minimum));
    printRow(output, "max", formatMetric(unit, distribution.maximum));
}

}

void printProcessReport(std::ostream& output, const ProcessMetrics& metrics) {
    printRow(output, "Runtime", formatDuration(static_cast<double>(metrics.wallTime.count())));
    printRow(output, "User CPU", formatDuration(static_cast<double>(metrics.userTime.count())));
    printRow(output, "System CPU", formatDuration(static_cast<double>(metrics.systemTime.count())));
    printRow(output, "Maximum RSS", formatBytes(static_cast<double>(metrics.maxRssBytes)));
    printRow(output, "Minor page faults", formatCount(static_cast<double>(metrics.minorFaults)));
    printRow(output, "Major page faults", formatCount(static_cast<double>(metrics.majorFaults)));
    printRow(output,
             "Voluntary context switches",
             formatCount(static_cast<double>(metrics.voluntaryContextSwitches)));
    printRow(output,
             "Involuntary context switches",
             formatCount(static_cast<double>(metrics.involuntaryContextSwitches)));
}

void printRunReport(std::ostream& output, const RunResult& result) {
    printProcessReport(output, result.process);
    output << '\n';
}

void printBenchmarkReport(std::ostream& output, const BenchmarkResult& result) {
    printDistribution(output, "Runtime", result.summary.wallTimeNs, "ns");
    printDistribution(output, "User CPU", result.summary.userTimeNs, "ns");
    printDistribution(output, "Maximum RSS", result.summary.maxRssBytes, "bytes");

    if (result.runs.size() == 1) {
        output << '\n';
        printRunReport(output, result.runs.front());
    }
}

void printComparisonReport(std::ostream& output, const ComparisonResult& result) {
    output << "PerfLens comparison\n";
    output << "seed: " << result.seed << "\n\n";
    output << std::left << std::setw(34) << "metric" << std::right << std::setw(17) << "baseline"
           << std::setw(17) << "candidate" << std::setw(13) << "change" << '\n';

    for (const MetricComparison& metric : result.metrics) {
        output << std::left << std::setw(34) << metric.label << std::right << std::setw(17)
               << formatMetric(metric.unit, metric.baseline) << std::setw(17)
               << formatMetric(metric.unit, metric.candidate) << std::setw(13)
               << (metric.changePercent ? formatChange(*metric.changePercent) : "n/a") << '\n';
    }

    if (!result.violations.empty()) {
        output << "\nPerformance threshold violations\n";
        for (const ThresholdViolation& violation : result.violations) {
            output << "  " << violation.metric << ": " << formatChange(violation.changePercent) << " (limit "
                   << std::fixed << std::setprecision(1) << violation.limitPercent << "%)\n";
        }
    }
}

}

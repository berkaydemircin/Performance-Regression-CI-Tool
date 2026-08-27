#include "perflens/workload_metrics.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>

namespace perflens {
namespace {

void validateNumber(const std::optional<double>& value, const char* name) {
    if (value && (!std::isfinite(*value) || *value < 0.0)) {
        throw std::invalid_argument(std::string{"invalid workload metric: "} + name);
    }
}

}

std::optional<ApplicationMetrics> loadApplicationMetrics(const std::string& path) {
    std::error_code fileError;
    if (!std::filesystem::exists(path, fileError)) {
        return std::nullopt;
    }
    if (fileError) {
        throw std::runtime_error("could not inspect workload metrics file: " + fileError.message());
    }

    std::ifstream input{path};
    if (!input) {
        throw std::runtime_error("could not open workload metrics file");
    }
    const nlohmann::json json = nlohmann::json::parse(input);
    ApplicationMetrics metrics = json.get<ApplicationMetrics>();

    if (metrics.operations && *metrics.operations == 0) {
        throw std::invalid_argument("workload operations must be greater than zero");
    }
    validateNumber(metrics.throughput, "throughput");
    validateNumber(metrics.latency.p50Us, "latency.p50_us");
    validateNumber(metrics.latency.p95Us, "latency.p95_us");
    validateNumber(metrics.latency.p99Us, "latency.p99_us");
    if (!metrics.operations && !metrics.throughput && !metrics.latency.p50Us && !metrics.latency.p95Us &&
        !metrics.latency.p99Us) {
        throw std::invalid_argument("workload metrics file did not contain any measurements");
    }
    if (metrics.latency.p50Us && metrics.latency.p95Us && *metrics.latency.p50Us > *metrics.latency.p95Us) {
        throw std::invalid_argument("workload p50 latency exceeds p95 latency");
    }
    if (metrics.latency.p95Us && metrics.latency.p99Us && *metrics.latency.p95Us > *metrics.latency.p99Us) {
        throw std::invalid_argument("workload p95 latency exceeds p99 latency");
    }
    return metrics;
}

}

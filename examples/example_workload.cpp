#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

std::string option(const int argc, char** argv, const std::string& name, const std::string& fallback) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::string{argv[index]} == name) {
            return argv[index + 1];
        }
    }
    return fallback;
}

double percentile(const std::vector<double>& sorted, const double fraction) {
    const std::size_t index = static_cast<std::size_t>(fraction * static_cast<double>(sorted.size() - 1));
    return sorted[index];
}

void transfer(const int descriptor, std::byte* data, const std::size_t size, const bool sending) {
    std::size_t completed = 0;
    while (completed < size) {
        const ssize_t count = sending ? send(descriptor, data + completed, size - completed, MSG_NOSIGNAL)
                                      : recv(descriptor, data + completed, size - completed, 0);
        if (count <= 0) {
            throw std::runtime_error("workload connection closed");
        }
        completed += static_cast<std::size_t>(count);
    }
}

}

int main(const int argc, char** argv) {
    const std::string host = option(argc, argv, "--host", "127.0.0.1");
    const std::uint16_t port = static_cast<std::uint16_t>(std::stoul(option(argc, argv, "--port", "19090")));
    const std::size_t operations = std::stoull(option(argc, argv, "--operations", "2000"));

    const int descriptor = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1 ||
        connect(descriptor, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == -1) {
        throw std::runtime_error("could not connect to example server");
    }

    std::vector<double> latencies;
    latencies.reserve(operations);
    std::array<std::byte, 8> message{};
    const auto startedAt = std::chrono::steady_clock::now();
    for (std::size_t index = 0; index < operations; ++index) {
        const auto operationStartedAt = std::chrono::steady_clock::now();
        transfer(descriptor, message.data(), message.size(), true);
        transfer(descriptor, message.data(), message.size(), false);
        latencies.push_back(
            std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - operationStartedAt)
                .count());
    }
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt).count();
    close(descriptor);
    std::sort(latencies.begin(), latencies.end());

    const char* metricsPath = std::getenv("PERFLENS_METRICS_FILE");
    if (metricsPath == nullptr) {
        throw std::runtime_error("PERFLENS_METRICS_FILE is not set");
    }
    std::ofstream output{metricsPath};
    output << "{\n"
           << "  \"operations\": " << operations << ",\n"
           << "  \"throughput\": " << static_cast<double>(operations) / seconds << ",\n"
           << "  \"latency\": {\n"
           << "    \"p50_us\": " << percentile(latencies, 0.50) << ",\n"
           << "    \"p95_us\": " << percentile(latencies, 0.95) << ",\n"
           << "    \"p99_us\": " << percentile(latencies, 0.99) << "\n"
           << "  }\n"
           << "}\n";
    if (!output) {
        throw std::runtime_error("could not write workload metrics");
    }
}

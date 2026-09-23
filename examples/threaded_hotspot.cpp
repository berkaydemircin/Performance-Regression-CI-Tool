#include <barrier>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

[[gnu::noinline]] std::uint64_t workerHotspot(std::uint64_t value, const std::uint64_t iterations) {
    for (std::uint64_t index = 0; index < iterations; ++index) {
        value ^= value << 13U;
        value ^= value >> 7U;
        value ^= value << 17U;
    }
    return value;
}

int main(const int argc, char** argv) {
    const int count = argc > 1 ? std::stoi(argv[1]) : 4;
    const std::uint64_t iterations = argc > 2 ? std::stoull(argv[2]) : 200'000'000;
    if (count < 1 || count > 256 || iterations == 0) {
        std::cerr << "use 1 to 256 threads and a positive iteration count\n";
        return 2;
    }
    std::barrier ready{count};
    std::vector<std::uint64_t> results(static_cast<std::size_t>(count));
    std::vector<std::jthread> workers;
    for (int index = 0; index < count; ++index) {
        workers.emplace_back([&, index] {
            ready.arrive_and_wait();
            results[static_cast<std::size_t>(index)] =
                workerHotspot(static_cast<std::uint64_t>(index + 1), iterations);
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    std::uint64_t total = 0;
    for (const auto value : results) {
        total ^= value;
    }
    std::cout << total << '\n';
}

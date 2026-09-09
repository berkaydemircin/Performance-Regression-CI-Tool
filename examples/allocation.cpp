#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

int main(const int argc, char** argv) {
    const bool allocate = argc > 1 && std::string{argv[1]} == "allocate";
    constexpr std::size_t iterations = 2'000'000;
    std::uint64_t total = 0;
    if (allocate) {
        for (std::size_t index = 0; index < iterations; ++index) {
            auto value = std::make_unique<std::uint64_t>(index);
            total += *value;
        }
    } else {
        std::uint64_t value = 0;
        for (std::size_t index = 0; index < iterations; ++index) {
            value = index;
            total += value;
        }
    }
    std::cout << total << '\n';
}

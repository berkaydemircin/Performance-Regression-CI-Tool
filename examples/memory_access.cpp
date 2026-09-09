#include <algorithm>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

int main(const int argc, char** argv) {
    const bool randomAccess = argc > 1 && std::string{argv[1]} == "random";
    constexpr std::size_t count = 4'000'000;
    std::vector<std::uint64_t> values(count);
    std::iota(values.begin(), values.end(), 1U);
    std::vector<std::uint32_t> order(count);
    std::iota(order.begin(), order.end(), 0U);
    if (randomAccess) {
        std::mt19937 generator{7};
        std::shuffle(order.begin(), order.end(), generator);
    }

    std::uint64_t sum = 0;
    for (const std::uint32_t index : order) {
        sum += values[index];
    }
    std::cout << sum << '\n';
}

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <vector>

int main(const int argc, char** argv) {
    const bool unpredictable = argc > 1 && std::string{argv[1]} == "unpredictable";
    std::vector<std::uint8_t> decisions(30'000'000);
    if (unpredictable) {
        std::mt19937 generator{11};
        std::uniform_int_distribution<int> bit{0, 1};
        std::generate(
            decisions.begin(), decisions.end(), [&] { return static_cast<std::uint8_t>(bit(generator)); });
    }

    std::uint64_t total = 0;
    for (const std::uint8_t decision : decisions) {
        if (decision != 0) {
            total += 3;
        } else {
            total += 1;
        }
    }
    std::cout << total << '\n';
}

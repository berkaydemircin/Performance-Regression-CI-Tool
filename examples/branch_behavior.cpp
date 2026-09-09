#include <algorithm>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <vector>

[[gnu::noinline]] std::uint64_t taken() {
    asm volatile("" ::: "memory");
    return 3;
}

[[gnu::noinline]] std::uint64_t notTaken() {
    asm volatile("" ::: "memory");
    return 1;
}

int main(const int argc, char** argv) {
    const bool unpredictable = argc > 1 && std::string{argv[1]} == "unpredictable";
    std::vector<std::uint8_t> decisions(30'000'000);
    std::mt19937 generator{11};
    std::uniform_int_distribution<int> bit{0, 1};
    for (std::size_t index = 0; index < decisions.size(); ++index) {
        const auto value = static_cast<std::uint8_t>(bit(generator));
        decisions[index] = unpredictable ? value : static_cast<std::uint8_t>(index >= decisions.size() / 2);
    }

    std::uint64_t total = 0;
    for (const std::uint8_t decision : decisions) {
        if (decision != 0) {
            total += taken();
        } else {
            total += notTaken();
        }
    }
    std::cout << total << '\n';
}

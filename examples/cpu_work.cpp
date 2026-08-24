#include <cstdint>
#include <iostream>

int main() {
    std::uint64_t value = 0x9e3779b97f4a7c15ULL;
    for (std::uint64_t index = 0; index < 40'000'000; ++index) {
        value ^= value << 7U;
        value ^= value >> 9U;
        value += index;
    }
    std::cout << value << '\n';
}

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

[[gnu::noinline]] std::uint64_t consume(std::uint64_t* value) {
    asm volatile("" : : "r"(value) : "memory");
    return *value;
}

int main(const int argc, char** argv) {
    const bool allocate = argc > 1 && std::string{argv[1]} == "allocate";
    constexpr std::size_t iterations = 2'000'000;
    std::uint64_t total = 0;
    if (allocate) {
        for (std::size_t index = 0; index < iterations; ++index) {
            auto value = std::make_unique<std::uint64_t>(index);
            total += consume(value.get());
        }
    } else {
        std::uint64_t value = 0;
        for (std::size_t index = 0; index < iterations; ++index) {
            value = index;
            total += consume(&value);
        }
    }
    std::cout << total << '\n';
}

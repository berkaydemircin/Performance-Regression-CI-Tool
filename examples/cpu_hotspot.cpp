#include <cstdint>
#include <iostream>
#include <string>

namespace {

[[gnu::noinline]] std::uint64_t hashLookup(std::uint64_t value, const std::uint64_t rounds) {
    for (std::uint64_t index = 0; index < rounds; ++index) {
        value ^= value << 13U;
        value ^= value >> 7U;
        value ^= value << 17U;
    }
    return value;
}

[[gnu::noinline]] std::uint64_t parseRecord(std::uint64_t value) {
    for (std::uint64_t index = 0; index < 3'000'000; ++index) {
        value = value * 2862933555777941757ULL + 3037000493ULL;
    }
    return value;
}

}

int main(const int argc, char** argv) {
    const bool candidate = argc > 1 && std::string{argv[1]} == "candidate";
    std::uint64_t value = parseRecord(7);
    value ^= hashLookup(value, candidate ? 20'000'000 : 4'000'000);
    std::cout << value << '\n';
}

#include "perflens/symbolizer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include <algorithm>
#include <cstdint>
#include <unistd.h>

namespace {

[[gnu::noinline]] void symbolizationAnchor() {
    asm volatile("" ::: "memory");
}

}

TEST_CASE("proc maps parser keeps addresses offsets and paths") {
    const std::string maps = "55d837620000-55d837630000 r-xp 00001000 08:01 42 /tmp/example app\n"
                             "7ffd00000000-7ffd00021000 rw-p 00000000 00:00 0 [stack]\n";

    const std::vector<perflens::MemoryMapping> parsed = perflens::parseProcMaps(maps);

    REQUIRE(parsed.size() == 2);
    CHECK(parsed[0].start == 0x55d837620000ULL);
    CHECK(parsed[0].end == 0x55d837630000ULL);
    CHECK(parsed[0].fileOffset == 0x1000ULL);
    CHECK(parsed[0].executable);
    CHECK(parsed[0].path == "/tmp/example app");
    CHECK_FALSE(parsed[1].executable);
}

TEST_CASE("unmapped samples aggregate as unknown") {
    const std::vector<perflens::CpuSample> samples{{1, 1, 1, 1}, {2, 1, 1, 2}};

    const std::vector<perflens::FunctionSample> functions = perflens::symbolizeSamples(samples, {});

    REQUIRE(functions.size() == 1);
    CHECK(functions.front().module == "[unknown]");
    CHECK(functions.front().function == "[unknown]");
    CHECK(functions.front().samples == 2);
}

TEST_CASE("PIE addresses resolve through proc mappings") {
    symbolizationAnchor();
    const auto address = reinterpret_cast<std::uintptr_t>(&symbolizationAnchor);
    const std::vector<perflens::MemoryMapping> mappings = perflens::readProcMaps(getpid());
    INFO("address=" << std::hex << address << " mappings=" << std::dec << mappings.size());
    REQUIRE_FALSE(mappings.empty());
    INFO("first=" << std::hex << mappings.front().start << "-" << mappings.front().end << " "
                  << mappings.front().path);
    const auto mapping =
        std::find_if(mappings.begin(), mappings.end(), [address](const perflens::MemoryMapping& value) {
            return address >= value.start && address < value.end;
        });
    REQUIRE(mapping != mappings.end());
    INFO(mapping->path << " executable=" << mapping->executable);
    const std::vector<perflens::FunctionSample> functions =
        perflens::symbolizeSamples({{static_cast<std::uint64_t>(address),
                                     static_cast<std::uint32_t>(getpid()),
                                     static_cast<std::uint32_t>(getpid()),
                                     1}},
                                   mappings);

    REQUIRE_FALSE(functions.empty());
    INFO(functions.front().module << " " << functions.front().function);
    CHECK(functions.front().function.find("symbolizationAnchor") != std::string::npos);
}

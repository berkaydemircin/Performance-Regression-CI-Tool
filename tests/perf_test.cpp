#include "perflens/perf_counter.hpp"
#include "perflens/perf_sampler.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include <linux/perf_event.h>

TEST_CASE("multiplexed counters scale by scheduled time") {
    CHECK(perflens::scaleCounter(100, 1000, 500) == 200);
    CHECK(perflens::scaleCounter(100, 500, 500) == 100);
    CHECK(perflens::scaleCounter(100, 400, 500) == 100);
    CHECK_THROWS_AS(perflens::scaleCounter(100, 500, 0), std::invalid_argument);
}

TEST_CASE("ring copies handle wraparound") {
    const std::array<std::byte, 8> ring{
        std::byte{0},
        std::byte{1},
        std::byte{2},
        std::byte{3},
        std::byte{4},
        std::byte{5},
        std::byte{6},
        std::byte{7},
    };
    std::array<std::byte, 5> output{};

    perflens::copyRingBytes(ring, 6, output);

    CHECK(std::to_integer<int>(output[0]) == 6);
    CHECK(std::to_integer<int>(output[1]) == 7);
    CHECK(std::to_integer<int>(output[2]) == 0);
    CHECK(std::to_integer<int>(output[3]) == 1);
    CHECK(std::to_integer<int>(output[4]) == 2);
}

TEST_CASE("sample and lost perf records parse their payloads") {
    struct SampleRecord {
        perf_event_header header{PERF_RECORD_SAMPLE, 0, sizeof(SampleRecord)};
        std::uint64_t instructionPointer{0x1234};
        std::uint32_t processId{7};
        std::uint32_t threadId{8};
        std::uint64_t time{9};
    } sample;
    const auto sampleBytes = std::as_bytes(std::span{&sample, 1});
    const perflens::ParsedPerfRecord parsedSample = perflens::parsePerfRecord(sampleBytes);

    REQUIRE(parsedSample.sample);
    CHECK(parsedSample.sample->instructionPointer == 0x1234);
    CHECK(parsedSample.sample->processId == 7);
    CHECK(parsedSample.sample->threadId == 8);
    CHECK(parsedSample.sample->time == 9);

    struct LostRecord {
        perf_event_header header{PERF_RECORD_LOST, 0, sizeof(LostRecord)};
        std::uint64_t id{3};
        std::uint64_t lost{11};
    } lost;
    const perflens::ParsedPerfRecord parsedLost =
        perflens::parsePerfRecord(std::as_bytes(std::span{&lost, 1}));
    CHECK(parsedLost.lostSamples == 11);
}

TEST_CASE("counter scaling preserves the unsigned range") {
    const std::uint64_t high = std::uint64_t{1} << 62;
    CHECK(perflens::scaleCounter(high, 3, 1) == high * 3);
    CHECK(perflens::scaleCounter(high, 8, 1) == std::numeric_limits<std::uint64_t>::max());
}

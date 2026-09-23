#include "../src/perf/sample_collector.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstring>
#include <sys/eventfd.h>

namespace {

struct SampleRecord {
    perf_event_header header{PERF_RECORD_SAMPLE, 0, sizeof(SampleRecord)};
    std::uint64_t ip{0x1234};
    std::uint32_t pid{7};
    std::uint32_t tid{8};
    std::uint64_t time{9};
};

struct LostRecord {
    perf_event_header header{PERF_RECORD_LOST, 0, sizeof(LostRecord)};
    std::uint64_t id{0};
    std::uint64_t lost{11};
};

struct Ring {
    perflens::detail::UniqueFd fd{eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)};
    perf_event_mmap_page metadata{};
    std::array<std::byte, 256> bytes{};

    perflens::detail::RingView view() {
        return {fd.get(), &metadata, bytes};
    }

    template <typename T> void append(const T& record) {
        const auto input = std::as_bytes(std::span{&record, 1});
        const auto head = __atomic_load_n(&metadata.data_head, __ATOMIC_RELAXED);
        for (std::size_t index = 0; index < input.size(); ++index) {
            bytes[(head + index) % bytes.size()] = input[index];
        }
        __atomic_store_n(&metadata.data_head, head + input.size(), __ATOMIC_RELEASE);
    }
};

}

TEST_CASE("collector merges rings and keeps only target threads", "[collector]") {
    Ring first, second;
    SampleRecord sample;
    sample.time = 30;
    first.append(sample);
    first.append(LostRecord{});
    sample.tid = 9;
    sample.time = 10;
    second.append(sample);
    sample.pid = 100;
    second.append(sample);
    second.append(LostRecord{});
    perflens::detail::SampleCollector collector{{first.view(), second.view()}, 7};
    CHECK_THROWS_AS(collector.samples(), std::logic_error);
    collector.start();
    CHECK_THROWS_AS(collector.start(), std::logic_error);
    CHECK_THROWS_AS(collector.lostSamples(), std::logic_error);
    collector.stop();
    collector.stop();
    REQUIRE(collector.samples().size() == 2);
    CHECK(collector.samples()[0].time == 10);
    CHECK(collector.samples()[0].threadId == 9);
    CHECK(collector.samples()[1].time == 30);
    CHECK(collector.lostSamples() == 22);
    CHECK(first.metadata.data_head == first.metadata.data_tail);
    CHECK(second.metadata.data_head == second.metadata.data_tail);
}

TEST_CASE("collector parses a record across the ring boundary", "[collector]") {
    Ring ring;
    ring.metadata.data_head = ring.metadata.data_tail = 252;
    ring.append(SampleRecord{});
    perflens::detail::SampleCollector collector{{ring.view()}, 7};
    collector.start();
    collector.stop();
    REQUIRE(collector.samples().size() == 1);
    CHECK(collector.samples()[0].instructionPointer == 0x1234);
}

TEST_CASE("collector reports malformed records after joining", "[collector]") {
    Ring ring;
    SECTION("partial header") {
        ring.metadata.data_head = 3;
    }
    SECTION("partial payload") {
        ring.append(perf_event_header{PERF_RECORD_SAMPLE, 0, sizeof(SampleRecord)});
    }
    SECTION("zero length") {
        ring.append(perf_event_header{PERF_RECORD_SAMPLE, 0, 0});
    }
    SECTION("overrun") {
        ring.metadata.data_head = ring.bytes.size() + 1;
    }
    perflens::detail::SampleCollector collector{{ring.view()}, 7};
    collector.start();
    CHECK_THROWS_AS(collector.stop(), std::runtime_error);
    CHECK_THROWS_AS(collector.stop(), std::runtime_error);
    CHECK_THROWS_AS(collector.samples(), std::runtime_error);
}

TEST_CASE("collector wakes on data and drains before shutdown", "[collector]") {
    using namespace std::chrono_literals;
    Ring ring;
    perflens::detail::SampleCollector collector{{ring.view()}, 7};
    collector.start();
    ring.append(SampleRecord{});
    const std::uint64_t wake = 1;
    REQUIRE(write(ring.fd.get(), &wake, sizeof(wake)) == sizeof(wake));
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    bool consumed = false;
    while (
        !(consumed = __atomic_load_n(&ring.metadata.data_tail, __ATOMIC_ACQUIRE) == sizeof(SampleRecord)) &&
        std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    collector.stop();
    CHECK(consumed);
    CHECK(collector.samples().size() == 1);
}

TEST_CASE("idle collector destruction wakes poll", "[collector]") {
    Ring ring;
    for (int attempt = 0; attempt < 20; ++attempt) {
        perflens::detail::SampleCollector collector{{ring.view()}, 7};
        collector.start();
    }
}

TEST_CASE("collector drains a hung up descriptor and still shuts down", "[collector]") {
    int descriptors[2];
    REQUIRE(pipe(descriptors) == 0);
    perflens::detail::UniqueFd reader{descriptors[0]}, writer{descriptors[1]};
    Ring ring;
    ring.append(SampleRecord{});
    auto view = ring.view();
    view.descriptor = reader.get();
    perflens::detail::SampleCollector collector{{view}, 7};
    collector.start();
    writer = perflens::detail::UniqueFd{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    bool consumed = false;
    while (
        !(consumed = __atomic_load_n(&ring.metadata.data_tail, __ATOMIC_ACQUIRE) == sizeof(SampleRecord)) &&
        std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    collector.stop();
    CHECK(consumed);
    CHECK(collector.samples().size() == 1);
}

TEST_CASE("collector can stop before starting", "[collector]") {
    perflens::detail::SampleCollector collector{{}, 7};
    collector.stop();
    collector.stop();
    CHECK(collector.samples().empty());
    CHECK_THROWS_AS(collector.start(), std::logic_error);
}

TEST_CASE("record parser rejects truncated payloads", "[collector]") {
    for (const auto type : {PERF_RECORD_SAMPLE, PERF_RECORD_LOST}) {
        perf_event_header header{static_cast<std::uint32_t>(type), 0, sizeof(perf_event_header)};
        CHECK_THROWS_AS(perflens::parsePerfRecord(std::as_bytes(std::span{&header, 1})), std::runtime_error);
    }
}

TEST_CASE("sampler rejects invalid CPU lists before opening events", "[collector]") {
    CHECK_THROWS_AS(perflens::PerfSampler(getpid(), std::vector<int>{}, 99), std::invalid_argument);
    CHECK_THROWS_AS(perflens::PerfSampler(getpid(), std::vector<int>{-1}, 99), std::invalid_argument);
    CHECK_THROWS_AS(perflens::PerfSampler(getpid(), std::vector<int>{0, 0}, 99), std::invalid_argument);
    CHECK_THROWS_AS(perflens::PerfSampler(getpid(), std::vector<int>{0}, 0), std::invalid_argument);
    CHECK_THROWS_AS(perflens::PerfSampler(getpid(), std::vector<int>{0}, 99, 3), std::invalid_argument);
}

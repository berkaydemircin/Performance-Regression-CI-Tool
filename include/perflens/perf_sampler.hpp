#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <sys/types.h>

namespace perflens {

struct CpuSample {
    std::uint64_t instructionPointer{};
    std::uint32_t processId{};
    std::uint32_t threadId{};
    std::uint64_t time{};
};

struct ParsedPerfRecord {
    std::optional<CpuSample> sample;
    std::uint64_t lostSamples{};
};

void copyRingBytes(std::span<const std::byte> ring, std::uint64_t offset, std::span<std::byte> output);

[[nodiscard]] ParsedPerfRecord parsePerfRecord(std::span<const std::byte> record);

class PerfSampler {
  public:
    PerfSampler(pid_t target, std::span<const int> cpus, std::uint64_t frequency, std::size_t dataPages = 16);
    ~PerfSampler();

    PerfSampler(const PerfSampler&) = delete;
    PerfSampler& operator=(const PerfSampler&) = delete;
    PerfSampler(PerfSampler&&) noexcept;
    PerfSampler& operator=(PerfSampler&&) noexcept;

    void start();
    void stop();
    // results are only available after stop has joined the collector
    [[nodiscard]] const std::vector<CpuSample>& samples() const;
    [[nodiscard]] std::uint64_t lostSamples() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}

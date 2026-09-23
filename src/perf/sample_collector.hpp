#pragma once

#include "perflens/perf_sampler.hpp"

#include <exception>
#include <linux/perf_event.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace perflens::detail {

class UniqueFd {
  public:
    explicit UniqueFd(int fd = -1) : fd_(fd) {}
    ~UniqueFd() {
        if (fd_ != -1) {
            close(fd_);
        }
    }
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;
    UniqueFd(UniqueFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            UniqueFd old{std::exchange(fd_, std::exchange(other.fd_, -1))};
        }
        return *this;
    }
    int get() const noexcept {
        return fd_;
    }

  private:
    int fd_;
};

// the sampler owns the mappings and keeps them alive until the collector joins
struct RingView {
    int descriptor;
    perf_event_mmap_page* metadata;
    std::span<const std::byte> data;
};

class SampleCollector {
  public:
    SampleCollector(std::vector<RingView> rings, std::uint32_t target);
    ~SampleCollector();
    SampleCollector(const SampleCollector&) = delete;
    SampleCollector& operator=(const SampleCollector&) = delete;

    void start();
    void stop();
    const std::vector<CpuSample>& samples() const;
    std::uint64_t lostSamples() const;

  private:
    void collect(std::stop_token token);
    void drain(const RingView& ring);
    void checkStopped() const;

    std::vector<RingView> rings_;
    std::uint32_t target_;
    UniqueFd wake_;
    std::vector<CpuSample> samples_;
    std::uint64_t lost_{};
    std::exception_ptr failure_;
    bool started_{};
    bool stopped_{};
    std::jthread worker_;
};

}

#pragma once

#include "perflens/model.hpp"

#include <cstdint>
#include <memory>

#include <sys/types.h>

namespace perflens {

[[nodiscard]] std::uint64_t
scaleCounter(std::uint64_t rawValue, std::uint64_t timeEnabled, std::uint64_t timeRunning);

class PerfCounterCollector {
  public:
    explicit PerfCounterCollector(pid_t target);
    ~PerfCounterCollector();

    PerfCounterCollector(const PerfCounterCollector&) = delete;
    PerfCounterCollector& operator=(const PerfCounterCollector&) = delete;
    PerfCounterCollector(PerfCounterCollector&&) noexcept;
    PerfCounterCollector& operator=(PerfCounterCollector&&) noexcept;

    void start();
    void stop();
    [[nodiscard]] PerfCounters read() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}

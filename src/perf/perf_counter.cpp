#include "perflens/perf_counter.hpp"

#include <array>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace perflens {
namespace {

constexpr std::array<std::uint64_t, 6> eventConfigs{
    PERF_COUNT_HW_CPU_CYCLES,
    PERF_COUNT_HW_INSTRUCTIONS,
    PERF_COUNT_HW_BRANCH_INSTRUCTIONS,
    PERF_COUNT_HW_BRANCH_MISSES,
    PERF_COUNT_HW_CACHE_REFERENCES,
    PERF_COUNT_HW_CACHE_MISSES,
};

int openEvent(const pid_t target, const std::uint64_t config, const int group, const bool leader) {
    perf_event_attr attributes{};
    attributes.type = PERF_TYPE_HARDWARE;
    attributes.size = sizeof(attributes);
    attributes.config = config;
    attributes.disabled = leader ? 1U : 0U;
    attributes.inherit = 1U;
    attributes.exclude_hv = 1U;
    attributes.read_format =
        PERF_FORMAT_GROUP | PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
    return static_cast<int>(
        syscall(__NR_perf_event_open, &attributes, target, -1, group, PERF_FLAG_FD_CLOEXEC));
}

void closeAll(std::vector<int>& descriptors) {
    for (const int descriptor : descriptors) {
        if (descriptor != -1) {
            close(descriptor);
        }
    }
    descriptors.clear();
}

}

struct PerfCounterCollector::Impl {
    std::vector<int> descriptors;
    ~Impl() {
        closeAll(descriptors);
    }
};

std::uint64_t
scaleCounter(const std::uint64_t rawValue, const std::uint64_t timeEnabled, const std::uint64_t timeRunning) {
    if (timeRunning == 0) {
        throw std::invalid_argument("counter did not run");
    }
    if (timeRunning >= timeEnabled) {
        return rawValue;
    }

    const long double scaled = static_cast<long double>(rawValue) * static_cast<long double>(timeEnabled) /
                               static_cast<long double>(timeRunning);
    if (scaled >= static_cast<long double>(std::numeric_limits<std::uint64_t>::max())) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return static_cast<std::uint64_t>(scaled + 0.5L);
}

PerfCounterCollector::PerfCounterCollector(const pid_t target) : impl_(std::make_unique<Impl>()) {
    impl_->descriptors.reserve(eventConfigs.size());
    int leader = -1;
    for (std::size_t index = 0; index < eventConfigs.size(); ++index) {
        const int descriptor = openEvent(target, eventConfigs[index], leader, index == 0);
        if (descriptor == -1) {
            const int openError = errno;
            closeAll(impl_->descriptors);
            throw std::system_error(openError, std::generic_category(), "perf_event_open hardware counters");
        }
        impl_->descriptors.push_back(descriptor);
        if (index == 0) {
            leader = descriptor;
        }
    }
}

PerfCounterCollector::~PerfCounterCollector() = default;

PerfCounterCollector::PerfCounterCollector(PerfCounterCollector&&) noexcept = default;
PerfCounterCollector& PerfCounterCollector::operator=(PerfCounterCollector&&) noexcept = default;

void PerfCounterCollector::start() {
    const int leader = impl_->descriptors.front();
    if (ioctl(leader, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) == -1 ||
        ioctl(leader, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) == -1) {
        throw std::system_error(errno, std::generic_category(), "enable perf counters");
    }
}

void PerfCounterCollector::stop() {
    if (ioctl(impl_->descriptors.front(), PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) == -1 &&
        errno != ESRCH) {
        throw std::system_error(errno, std::generic_category(), "disable perf counters");
    }
}

PerfCounters PerfCounterCollector::read() const {
    struct GroupRead {
        std::uint64_t count;
        std::uint64_t timeEnabled;
        std::uint64_t timeRunning;
        std::array<std::uint64_t, eventConfigs.size()> values;
    } data{};

    const ssize_t bytes = ::read(impl_->descriptors.front(), &data, sizeof(data));
    if (bytes == -1) {
        throw std::system_error(errno, std::generic_category(), "read perf counters");
    }
    if (static_cast<std::size_t>(bytes) != sizeof(data) || data.count != eventConfigs.size()) {
        throw std::runtime_error("unexpected perf counter group read size");
    }
    if (data.timeRunning == 0) {
        throw std::runtime_error("hardware counters were never scheduled");
    }

    PerfCounters counters;
    counters.available = true;
    counters.timeEnabledNs = data.timeEnabled;
    counters.timeRunningNs = data.timeRunning;
    counters.cycles = scaleCounter(data.values[0], data.timeEnabled, data.timeRunning);
    counters.instructions = scaleCounter(data.values[1], data.timeEnabled, data.timeRunning);
    counters.branches = scaleCounter(data.values[2], data.timeEnabled, data.timeRunning);
    counters.branchMisses = scaleCounter(data.values[3], data.timeEnabled, data.timeRunning);
    counters.cacheReferences = scaleCounter(data.values[4], data.timeEnabled, data.timeRunning);
    counters.cacheMisses = scaleCounter(data.values[5], data.timeEnabled, data.timeRunning);
    return counters;
}

}

#include "perflens/perf_sampler.hpp"
#include "sample_collector.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace perflens {
namespace {

template <typename T> T readValue(const std::vector<std::byte>& record, std::size_t& offset) {
    if (offset + sizeof(T) > record.size()) {
        throw std::runtime_error("truncated perf record");
    }
    T value{};
    std::memcpy(&value, record.data() + offset, sizeof(T));
    offset += sizeof(T);
    return value;
}

}

struct PerfRing {
    detail::UniqueFd descriptor;
    void* mapping{MAP_FAILED};
    std::size_t mappingSize{};
    std::size_t dataSize{};
    ~PerfRing() {
        if (mapping != MAP_FAILED) {
            munmap(mapping, mappingSize);
        }
    }
};

struct PerfSampler::Impl {
    std::vector<std::unique_ptr<PerfRing>> rings;
    std::unique_ptr<detail::SampleCollector> collector;
    bool started{};
    bool stopped{};
    std::exception_ptr failure;

    void stop() {
        if (!stopped) {
            for (const auto& ring : rings) {
                if (ioctl(ring->descriptor.get(), PERF_EVENT_IOC_DISABLE, 0) == -1 && errno != ESRCH) {
                    if (!failure) {
                        failure = std::make_exception_ptr(
                            std::system_error(errno, std::generic_category(), "disable CPU sampler"));
                    }
                }
            }
            if (collector) {
                try {
                    collector->stop();
                } catch (...) {
                    if (!failure) {
                        failure = std::current_exception();
                    }
                }
            }
            stopped = true;
        }
        if (failure) {
            std::rethrow_exception(failure);
        }
    }
    ~Impl() {
        try {
            stop();
        } catch (...) {
            // destruction still joins the collector and releases every ring
        }
    }
};

void copyRingBytes(const std::span<const std::byte> ring,
                   const std::uint64_t offset,
                   const std::span<std::byte> output) {
    if (ring.empty() && !output.empty()) {
        throw std::invalid_argument("cannot copy from an empty ring");
    }
    std::size_t copied = 0;
    while (copied < output.size()) {
        const std::size_t source = static_cast<std::size_t>((offset + copied) % ring.size());
        const std::size_t count = std::min(output.size() - copied, ring.size() - source);
        std::memcpy(output.data() + copied, ring.data() + source, count);
        copied += count;
    }
}

ParsedPerfRecord parsePerfRecord(const std::span<const std::byte> bytes) {
    if (bytes.size() < sizeof(perf_event_header)) {
        throw std::runtime_error("truncated perf record header");
    }
    perf_event_header header{};
    std::memcpy(&header, bytes.data(), sizeof(header));
    if (header.size != bytes.size()) {
        throw std::runtime_error("perf record size does not match its header");
    }

    const std::vector<std::byte> record{bytes.begin(), bytes.end()};
    std::size_t offset = sizeof(perf_event_header);
    ParsedPerfRecord parsed;
    if (header.type == PERF_RECORD_SAMPLE) {
        CpuSample sample;
        sample.instructionPointer = readValue<std::uint64_t>(record, offset);
        sample.processId = readValue<std::uint32_t>(record, offset);
        sample.threadId = readValue<std::uint32_t>(record, offset);
        sample.time = readValue<std::uint64_t>(record, offset);
        parsed.sample = sample;
    } else if (header.type == PERF_RECORD_LOST) {
        static_cast<void>(readValue<std::uint64_t>(record, offset));
        parsed.lostSamples = readValue<std::uint64_t>(record, offset);
    }
    return parsed;
}

PerfSampler::PerfSampler(const pid_t target,
                         const std::span<const int> cpus,
                         const std::uint64_t frequency,
                         const std::size_t dataPages)
    : impl_(std::make_unique<Impl>()) {
    if (target <= 0 || cpus.empty()) {
        throw std::invalid_argument("CPU sampler needs a target PID and at least one CPU");
    }
    std::set<int> uniqueCpus;
    for (const int cpu : cpus) {
        if (cpu < 0 || !uniqueCpus.insert(cpu).second) {
            throw std::invalid_argument("sampling CPUs must be distinct and nonnegative");
        }
    }
    if (frequency == 0 || dataPages == 0 || (dataPages & (dataPages - 1)) != 0) {
        throw std::invalid_argument("sampling frequency and power-of-two data pages must be nonzero");
    }
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) {
        throw std::runtime_error("could not determine system page size");
    }
    const auto pageBytes = static_cast<std::size_t>(pageSize);
    if (dataPages > std::numeric_limits<std::size_t>::max() / pageBytes - 1) {
        throw std::invalid_argument("CPU sample buffer is too large");
    }

    perf_event_attr attributes{};
    attributes.type = PERF_TYPE_HARDWARE;
    attributes.size = sizeof(attributes);
    attributes.config = PERF_COUNT_HW_CPU_CYCLES;
    attributes.disabled = 1U;
    // each CPU buffer also receives samples from new target threads
    attributes.inherit = 1U;
    attributes.exclude_kernel = 1U;
    attributes.exclude_hv = 1U;
    attributes.freq = 1U;
    attributes.sample_freq = frequency;
    attributes.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_TIME;
    attributes.wakeup_events = 1U;
    attributes.use_clockid = 1U;
    attributes.clockid = CLOCK_MONOTONIC;

    std::vector<detail::RingView> views;
    for (const int cpu : cpus) {
        auto ring = std::make_unique<PerfRing>();
        ring->descriptor = detail::UniqueFd{static_cast<int>(
            syscall(__NR_perf_event_open, &attributes, target, cpu, -1, PERF_FLAG_FD_CLOEXEC))};
        if (ring->descriptor.get() == -1) {
            throw std::system_error(
                errno, std::generic_category(), "perf_event_open CPU sampler on CPU " + std::to_string(cpu));
        }
        ring->dataSize = pageBytes * dataPages;
        ring->mappingSize = pageBytes + ring->dataSize;
        ring->mapping =
            mmap(nullptr, ring->mappingSize, PROT_READ | PROT_WRITE, MAP_SHARED, ring->descriptor.get(), 0);
        if (ring->mapping == MAP_FAILED) {
            throw std::system_error(
                errno, std::generic_category(), "mmap CPU sample buffer on CPU " + std::to_string(cpu));
        }
        auto* metadata = static_cast<perf_event_mmap_page*>(ring->mapping);
        if (metadata->data_offset != pageBytes || metadata->data_size != ring->dataSize) {
            throw std::runtime_error("unexpected CPU sample buffer layout");
        }
        views.push_back({ring->descriptor.get(),
                         metadata,
                         {reinterpret_cast<const std::byte*>(ring->mapping) + pageBytes, ring->dataSize}});
        impl_->rings.push_back(std::move(ring));
    }
    impl_->collector =
        std::make_unique<detail::SampleCollector>(std::move(views), static_cast<std::uint32_t>(target));
}

PerfSampler::~PerfSampler() = default;

PerfSampler::PerfSampler(PerfSampler&&) noexcept = default;
PerfSampler& PerfSampler::operator=(PerfSampler&&) noexcept = default;

void PerfSampler::start() {
    if (impl_->started || impl_->stopped) {
        throw std::logic_error("CPU sampler can only start once");
    }
    try {
        impl_->collector->start();
        for (const auto& ring : impl_->rings) {
            if (ioctl(ring->descriptor.get(), PERF_EVENT_IOC_RESET, 0) == -1 ||
                ioctl(ring->descriptor.get(), PERF_EVENT_IOC_ENABLE, 0) == -1) {
                throw std::system_error(errno, std::generic_category(), "enable CPU sampler");
            }
        }
        impl_->started = true;
    } catch (...) {
        impl_->failure = std::current_exception();
        impl_->stop();
    }
}

void PerfSampler::stop() {
    impl_->stop();
}

const std::vector<CpuSample>& PerfSampler::samples() const {
    if (impl_->failure) {
        std::rethrow_exception(impl_->failure);
    }
    return impl_->collector->samples();
}

std::uint64_t PerfSampler::lostSamples() const {
    if (impl_->failure) {
        std::rethrow_exception(impl_->failure);
    }
    return impl_->collector->lostSamples();
}

}

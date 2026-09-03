#include "perflens/perf_sampler.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
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

struct PerfSampler::Impl {
    int descriptor{-1};
    void* mapping{MAP_FAILED};
    std::size_t mappingSize{};
    std::size_t dataSize{};
    std::vector<CpuSample> samples;
    std::uint64_t lost{};
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

PerfSampler::PerfSampler(const pid_t target, const std::uint64_t frequency, const std::size_t dataPages)
    : impl_(std::make_unique<Impl>()) {
    if (frequency == 0 || dataPages == 0 || (dataPages & (dataPages - 1)) != 0) {
        throw std::invalid_argument("sampling frequency and power-of-two data pages must be nonzero");
    }

    perf_event_attr attributes{};
    attributes.type = PERF_TYPE_HARDWARE;
    attributes.size = sizeof(attributes);
    attributes.config = PERF_COUNT_HW_CPU_CYCLES;
    attributes.disabled = 1U;
    // this sampler covers the targets main thread only
    attributes.inherit = 0U;
    attributes.exclude_kernel = 1U;
    attributes.exclude_hv = 1U;
    attributes.freq = 1U;
    attributes.sample_freq = frequency;
    attributes.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_TIME;
    attributes.wakeup_events = 1U;

    impl_->descriptor =
        static_cast<int>(syscall(__NR_perf_event_open, &attributes, target, -1, -1, PERF_FLAG_FD_CLOEXEC));
    if (impl_->descriptor == -1) {
        throw std::system_error(errno, std::generic_category(), "perf_event_open CPU sampler");
    }

    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) {
        throw std::runtime_error("could not determine system page size");
    }
    impl_->dataSize = static_cast<std::size_t>(pageSize) * dataPages;
    impl_->mappingSize = static_cast<std::size_t>(pageSize) + impl_->dataSize;
    impl_->mapping =
        mmap(nullptr, impl_->mappingSize, PROT_READ | PROT_WRITE, MAP_SHARED, impl_->descriptor, 0);
    if (impl_->mapping == MAP_FAILED) {
        const int mapError = errno;
        close(impl_->descriptor);
        impl_->descriptor = -1;
        throw std::system_error(mapError, std::generic_category(), "mmap perf sample buffer");
    }
}

PerfSampler::~PerfSampler() {
    if (!impl_) {
        return;
    }
    if (impl_->mapping != MAP_FAILED) {
        munmap(impl_->mapping, impl_->mappingSize);
    }
    if (impl_->descriptor != -1) {
        close(impl_->descriptor);
    }
}

PerfSampler::PerfSampler(PerfSampler&&) noexcept = default;
PerfSampler& PerfSampler::operator=(PerfSampler&&) noexcept = default;

void PerfSampler::start() {
    if (ioctl(impl_->descriptor, PERF_EVENT_IOC_RESET, 0) == -1 ||
        ioctl(impl_->descriptor, PERF_EVENT_IOC_ENABLE, 0) == -1) {
        throw std::system_error(errno, std::generic_category(), "enable CPU sampler");
    }
}

void PerfSampler::stop() {
    if (ioctl(impl_->descriptor, PERF_EVENT_IOC_DISABLE, 0) == -1 && errno != ESRCH) {
        throw std::system_error(errno, std::generic_category(), "disable CPU sampler");
    }
    drain();
}

void PerfSampler::drain() {
    auto* metadata = static_cast<perf_event_mmap_page*>(impl_->mapping);
    const std::uint64_t head = __atomic_load_n(&metadata->data_head, __ATOMIC_ACQUIRE);
    std::uint64_t tail = metadata->data_tail;
    const auto* data = reinterpret_cast<const std::byte*>(metadata) + sysconf(_SC_PAGESIZE);
    const std::span<const std::byte> ring{data, impl_->dataSize};

    while (tail < head) {
        perf_event_header header{};
        copyRingBytes(ring, tail, std::as_writable_bytes(std::span{&header, 1}));
        if (header.size < sizeof(header) || header.size > impl_->dataSize) {
            throw std::runtime_error("invalid perf record size");
        }

        std::vector<std::byte> record(header.size);
        copyRingBytes(ring, tail, record);
        const ParsedPerfRecord parsed = parsePerfRecord(record);
        if (parsed.sample) {
            impl_->samples.push_back(*parsed.sample);
        }
        impl_->lost += parsed.lostSamples;
        tail += header.size;
    }

    __atomic_store_n(&metadata->data_tail, tail, __ATOMIC_RELEASE);
}

const std::vector<CpuSample>& PerfSampler::samples() const noexcept {
    return impl_->samples;
}

std::uint64_t PerfSampler::lostSamples() const noexcept {
    return impl_->lost;
}

}

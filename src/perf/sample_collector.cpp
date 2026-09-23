#include "sample_collector.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <poll.h>
#include <pthread.h>
#include <stdexcept>
#include <sys/eventfd.h>
#include <system_error>

namespace perflens::detail {

SampleCollector::SampleCollector(std::vector<RingView> rings, const std::uint32_t target)
    : rings_(std::move(rings)), target_(target), wake_(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)) {
    if (wake_.get() == -1) {
        throw std::system_error(errno, std::generic_category(), "eventfd CPU sampler");
    }
}

SampleCollector::~SampleCollector() {
    try {
        stop();
    } catch (...) {
        // stop has already joined before reporting collector errors
    }
}

void SampleCollector::start() {
    if (started_ || stopped_) {
        throw std::logic_error("CPU sampler can only start once");
    }
    // keep the runner's signal handlers on the main thread
    sigset_t blocked{}, previous{};
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGINT);
    sigaddset(&blocked, SIGTERM);
    const int maskError = pthread_sigmask(SIG_BLOCK, &blocked, &previous);
    if (maskError != 0) {
        throw std::system_error(maskError, std::generic_category(), "block collector signals");
    }
    try {
        worker_ = std::jthread([this](std::stop_token token) {
            try {
                collect(token);
            } catch (...) {
                failure_ = std::current_exception();
            }
        });
        started_ = true;
    } catch (...) {
        pthread_sigmask(SIG_SETMASK, &previous, nullptr);
        throw;
    }
    pthread_sigmask(SIG_SETMASK, &previous, nullptr);
}

void SampleCollector::stop() {
    int wakeError = 0;
    if (!stopped_) {
        if (worker_.joinable()) {
            worker_.request_stop();
            const std::uint64_t wake = 1;
            ssize_t written;
            do {
                written = write(wake_.get(), &wake, sizeof(wake));
            } while (written == -1 && errno == EINTR);
            // EAGAIN means the eventfd is already readable
            if (written == -1 && errno != EAGAIN) {
                wakeError = errno;
            }
            worker_.join();
        }
        stopped_ = true;
        std::sort(samples_.begin(), samples_.end(), [](const CpuSample& a, const CpuSample& b) {
            return a.time < b.time;
        });
    }
    if (wakeError != 0 && !failure_) {
        failure_ = std::make_exception_ptr(
            std::system_error(wakeError, std::generic_category(), "wake CPU sampler"));
    }
    if (failure_) {
        std::rethrow_exception(failure_);
    }
}

void SampleCollector::collect(const std::stop_token token) {
    std::vector<pollfd> events;
    for (const auto& ring : rings_) {
        events.push_back({ring.descriptor, POLLIN, 0});
    }
    events.push_back({wake_.get(), POLLIN, 0});
    while (!token.stop_requested()) {
        // the timeout is a fallback if the shutdown eventfd write fails
        if (poll(events.data(), static_cast<nfds_t>(events.size()), 1000) == -1) {
            if (errno == EINTR) {
                continue;
            }
            throw std::system_error(errno, std::generic_category(), "poll CPU sampler");
        }
        if (events.back().revents != 0) {
            break;
        }
        for (std::size_t index = 0; index < rings_.size(); ++index) {
            const short ready = events[index].revents;
            if ((ready & (POLLERR | POLLNVAL)) != 0) {
                throw std::runtime_error("CPU sampler descriptor failed");
            }
            if ((ready & (POLLIN | POLLHUP)) != 0) {
                drain(rings_[index]);
            }
            if ((ready & POLLHUP) != 0) {
                // an exited target must not leave poll spinning on HUP
                events[index].fd = -1;
            }
        }
    }
    for (const auto& ring : rings_) {
        drain(ring);
    }
}

void SampleCollector::drain(const RingView& ring) {
    const std::uint64_t head = __atomic_load_n(&ring.metadata->data_head, __ATOMIC_ACQUIRE);
    std::uint64_t tail = ring.metadata->data_tail;
    if (head - tail > ring.data.size()) {
        throw std::runtime_error("CPU sample buffer overrun");
    }
    while (tail != head) {
        if (head - tail < sizeof(perf_event_header)) {
            throw std::runtime_error("truncated perf record header in ring");
        }
        perf_event_header header{};
        copyRingBytes(ring.data, tail, std::as_writable_bytes(std::span{&header, 1}));
        if (header.size < sizeof(header) || header.size > ring.data.size() || header.size > head - tail) {
            throw std::runtime_error("invalid perf record size in ring");
        }
        std::vector<std::byte> bytes(header.size);
        copyRingBytes(ring.data, tail, bytes);
        const auto record = parsePerfRecord(bytes);
        // inherited child processes have different mappings, so leave them out
        if (record.sample && record.sample->processId == target_) {
            samples_.push_back(*record.sample);
        }
        lost_ += record.lostSamples;
        tail += header.size;
    }
    // perf requires a full barrier before making consumed space available again
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    __atomic_store_n(&ring.metadata->data_tail, tail, __ATOMIC_RELEASE);
}

void SampleCollector::checkStopped() const {
    if (!stopped_) {
        throw std::logic_error("stop CPU sampler before reading samples");
    }
    if (failure_) {
        std::rethrow_exception(failure_);
    }
}

const std::vector<CpuSample>& SampleCollector::samples() const {
    checkStopped();
    return samples_;
}

std::uint64_t SampleCollector::lostSamples() const {
    checkStopped();
    return lost_;
}

}

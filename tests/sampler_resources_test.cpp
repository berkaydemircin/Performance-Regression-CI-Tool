#include "perflens/perf_sampler.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cerrno>
#include <cstdarg>
#include <fcntl.h>
#include <linux/perf_event.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <vector>

namespace {

struct FakePerf {
    bool active{};
    int openCalls{};
    int mapCalls{};
    int unmapCalls{};
    int enableCalls{};
    int failOpen{};
    int failMap{};
    int failEnable{};
    bool failDisable{};
    std::vector<int> fds;
    std::vector<int> cpus;
    std::vector<perf_event_attr> attributes;
} fake;

struct Fixture {
    Fixture() {
        fake = {};
        fake.active = true;
    }
    ~Fixture() {
        fake.active = false;
    }
    void checkClosed() {
        for (const int fd : fake.fds) {
            CHECK(fcntl(fd, F_GETFD) == -1);
            CHECK(errno == EBADF);
        }
    }
};

}

// only this test executable replaces the perf syscalls
extern "C" long __real_syscall(long number, ...);
extern "C" void* __real_mmap(void*, size_t, int, int, int, off_t);
extern "C" int __real_munmap(void*, size_t);
extern "C" int __real_ioctl(int, unsigned long, ...);

extern "C" long __wrap_syscall(long number, ...) {
    va_list args;
    va_start(args, number);
    auto* attributes = va_arg(args, perf_event_attr*);
    const auto target = va_arg(args, int);
    const auto cpu = va_arg(args, int);
    const auto group = va_arg(args, int);
    const auto flags = va_arg(args, unsigned long);
    va_end(args);
    if (!fake.active) {
        return __real_syscall(number, attributes, target, cpu, group, flags);
    }
    ++fake.openCalls;
    if (fake.openCalls == fake.failOpen) {
        errno = EMFILE;
        return -1;
    }
    const int fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    fake.fds.push_back(fd);
    fake.cpus.push_back(cpu);
    fake.attributes.push_back(*attributes);
    return fd;
}

extern "C" void* __wrap_mmap(void* address, size_t length, int prot, int flags, int fd, off_t offset) {
    if (!fake.active) {
        return __real_mmap(address, length, prot, flags, fd, offset);
    }
    ++fake.mapCalls;
    if (fake.mapCalls == fake.failMap) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    void* mapping = __real_mmap(nullptr, length, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping != MAP_FAILED) {
        auto* metadata = static_cast<perf_event_mmap_page*>(mapping);
        metadata->data_offset = static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE));
        metadata->data_size = length - metadata->data_offset;
    }
    return mapping;
}

extern "C" int __wrap_munmap(void* address, size_t length) {
    if (fake.active) {
        ++fake.unmapCalls;
    }
    return __real_munmap(address, length);
}

extern "C" int __wrap_ioctl(int fd, unsigned long request, ...) {
    if (!fake.active) {
        return __real_ioctl(fd, request, 0);
    }
    if (request == PERF_EVENT_IOC_ENABLE && ++fake.enableCalls == fake.failEnable) {
        errno = EIO;
        return -1;
    }
    if (request == PERF_EVENT_IOC_DISABLE && fake.failDisable) {
        errno = EIO;
        return -1;
    }
    return 0;
}

TEST_CASE("sampler releases resources after partial construction", "[collector]") {
    Fixture fixture;
    SECTION("open failure") {
        fake.failOpen = 2;
        CHECK_THROWS(perflens::PerfSampler(getpid(), std::vector<int>{0, 1}, 99));
        CHECK(fake.unmapCalls == 1);
    }
    SECTION("mmap failure") {
        fake.failMap = 2;
        CHECK_THROWS(perflens::PerfSampler(getpid(), std::vector<int>{0, 1}, 99));
        CHECK(fake.unmapCalls == 1);
    }
    fixture.checkClosed();
}

TEST_CASE("sampler failures still stop and join the collector", "[collector]") {
    Fixture fixture;
    {
        perflens::PerfSampler sampler{getpid(), std::vector<int>{0, 1}, 99};
        SECTION("enable failure after one CPU started") {
            fake.failEnable = 2;
            CHECK_THROWS(sampler.start());
            CHECK_THROWS(sampler.stop());
        }
        SECTION("disable failure") {
            sampler.start();
            fake.failDisable = true;
            CHECK_THROWS(sampler.stop());
            CHECK_THROWS(sampler.stop());
        }
        SECTION("normal shutdown") {
            sampler.start();
            CHECK_THROWS(sampler.samples());
            sampler.stop();
            sampler.stop();
            CHECK(sampler.samples().empty());
            CHECK(sampler.lostSamples() == 0);
        }
        SECTION("destruction while blocked") {
            sampler.start();
        }
        SECTION("move assignment stops the old collector") {
            sampler.start();
            perflens::PerfSampler other{getpid(), std::vector<int>{0}, 99};
            other.start();
            other = std::move(sampler);
            other.stop();
        }
    }
    CHECK(fake.unmapCalls == fake.mapCalls);
    fixture.checkClosed();
    CHECK(fake.cpus.front() == 0);
    CHECK(fake.cpus[1] == 1);
    for (const auto& attributes : fake.attributes) {
        CHECK(attributes.inherit == 1);
        CHECK(attributes.exclude_kernel == 1);
        CHECK(attributes.use_clockid == 1);
    }
}

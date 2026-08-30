#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

int main(const int argc, char** argv) {
    const bool contended = argc > 1 && std::string{argv[1]} == "contended";
    constexpr std::size_t threadCount = 4;
    constexpr std::size_t iterations = 500'000;
    std::mutex sharedMutex;
    std::uint64_t shared = 0;
    std::vector<std::thread> threads;
    threads.reserve(threadCount);

    for (std::size_t thread = 0; thread < threadCount; ++thread) {
        threads.emplace_back([&, thread] {
            std::mutex localMutex;
            std::uint64_t local = 0;
            for (std::size_t index = 0; index < iterations; ++index) {
                std::lock_guard lock{contended ? sharedMutex : localMutex};
                if (contended) {
                    ++shared;
                } else {
                    local += thread + index;
                }
            }
            if (!contended) {
                std::lock_guard lock{sharedMutex};
                shared += local;
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    std::cout << shared << '\n';
}

#pragma once

#include "perflens/model.hpp"
#include "perflens/perf_sampler.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include <sys/types.h>

namespace perflens {

struct MemoryMapping {
    std::uint64_t start{};
    std::uint64_t end{};
    std::uint64_t fileOffset{};
    bool executable{};
    std::string path;
};

[[nodiscard]] std::vector<MemoryMapping> parseProcMaps(const std::string& contents);
[[nodiscard]] std::vector<MemoryMapping> readProcMaps(pid_t target);
[[nodiscard]] std::vector<FunctionSample> symbolizeSamples(const std::vector<CpuSample>& samples,
                                                           const std::vector<MemoryMapping>& mappings);

}

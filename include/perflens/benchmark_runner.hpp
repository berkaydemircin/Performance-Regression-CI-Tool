#pragma once

#include "perflens/analysis.hpp"
#include "perflens/model.hpp"
#include "perflens/process_runner.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace perflens {

struct BenchmarkOptions {
    std::size_t repeat{1};
    std::size_t warmup{};
    std::optional<std::uint64_t> seed;
};

class BenchmarkError : public std::runtime_error {
  public:
    BenchmarkError(std::string message, int exitCode);
    [[nodiscard]] int exitCode() const noexcept;

  private:
    int exitCode_;
};

[[nodiscard]] BenchmarkResult runBenchmark(const std::vector<std::string>& command,
                                           const BenchmarkOptions& benchmarkOptions,
                                           const RunOptions& runOptions);

[[nodiscard]] ComparisonResult runComparison(const std::vector<std::string>& baseline,
                                             const std::vector<std::string>& candidate,
                                             const BenchmarkOptions& benchmarkOptions,
                                             const RunOptions& runOptions,
                                             const Thresholds& thresholds = {});

}

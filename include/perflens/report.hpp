#pragma once

#include "perflens/model.hpp"

#include <iosfwd>

namespace perflens {

void printProcessReport(std::ostream& output, const ProcessMetrics& metrics);
void printRunReport(std::ostream& output, const RunResult& result);
void printBenchmarkReport(std::ostream& output, const BenchmarkResult& result);
void printComparisonReport(std::ostream& output, const ComparisonResult& result);

}

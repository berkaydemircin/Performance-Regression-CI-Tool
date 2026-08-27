#pragma once

#include "perflens/model.hpp"

#include <optional>
#include <string>

namespace perflens {

[[nodiscard]] std::optional<ApplicationMetrics> loadApplicationMetrics(const std::string& path);

}

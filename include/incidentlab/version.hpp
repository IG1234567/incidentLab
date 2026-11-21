#pragma once

#include <string>

namespace incidentlab {

/// Semantic version baked into the build by CMake.
///
/// A plain value type is enough: three numbers have no invariant to protect.
/// Types that must protect their state (a circuit breaker, a queue) come later.
struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
};

/// Version of this build. The numbers come from project(VERSION) in CMakeLists.txt.
Version current_version() noexcept;

/// "major.minor.patch", for example "1.0.0".
/// Digits are formatted without locale-specific separators.
std::string version_string();

}  // namespace incidentlab

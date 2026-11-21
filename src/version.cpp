#include "incidentlab/version.hpp"

#include <string>

#if !defined(INCIDENTLAB_VERSION_MAJOR) || !defined(INCIDENTLAB_VERSION_MINOR) || \
    !defined(INCIDENTLAB_VERSION_PATCH)
#error "Build IncidentLab with CMake so INCIDENTLAB_VERSION_* macros are defined."
#endif

namespace incidentlab {

Version current_version() noexcept {
    return Version{INCIDENTLAB_VERSION_MAJOR, INCIDENTLAB_VERSION_MINOR, INCIDENTLAB_VERSION_PATCH};
}

std::string version_string() {
    const Version version = current_version();
    return std::to_string(version.major) + "." + std::to_string(version.minor) + "." +
           std::to_string(version.patch);
}

}  // namespace incidentlab

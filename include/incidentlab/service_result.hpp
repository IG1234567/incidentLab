#pragma once

#include <string>

namespace incidentlab {

/// Outcome of a service operation.
///
/// Success and Rejected are normal domain results. Unsupported operations use Rejected.
/// Invalid request structure throws std::invalid_argument instead.
enum class ServiceStatus { Success, Rejected };

struct ServiceResult {
    ServiceStatus status = ServiceStatus::Rejected;
    std::string message;
};

inline const char* to_string(ServiceStatus status) noexcept {
    switch (status) {
    case ServiceStatus::Success:
        return "SUCCESS";
    case ServiceStatus::Rejected:
        return "REJECTED";
    }
    return "REJECTED";
}

}  // namespace incidentlab

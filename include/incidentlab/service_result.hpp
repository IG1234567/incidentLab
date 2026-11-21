#pragma once

#include <string>

namespace incidentlab {

/// Outcome of one request.
///
/// Success and Rejected are normal domain results. Unsupported operations use Rejected.
/// Failed, TimedOut, and Dropped are injected simulation outcomes, not C++ exceptions.
/// Invalid request structure throws std::invalid_argument instead.
enum class ServiceStatus { Success, Rejected, Failed, TimedOut, Dropped, CircuitRejected };

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
    case ServiceStatus::Failed:
        return "FAILED";
    case ServiceStatus::TimedOut:
        return "TIMED OUT";
    case ServiceStatus::Dropped:
        return "DROPPED";
    case ServiceStatus::CircuitRejected:
        return "CIRCUIT REJECTED";
    }
    return "REJECTED";
}

}  // namespace incidentlab

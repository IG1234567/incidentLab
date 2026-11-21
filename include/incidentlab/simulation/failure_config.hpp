#pragma once

#include <cstdint>
#include <stdexcept>

namespace incidentlab {

/// How one configured service should misbehave. None means the service runs normally.
enum class FailureType { None, ServiceFailure, SlowResponse, Timeout, DroppedRequest };

/// Failure settings for one service name.
///
/// every_nth_request selects the requests that use `type`. The others run normally.
/// A value of 1 applies the failure to every request. Reconfiguration resets the counter.
///
/// For Timeout, delay_ms is the processing time that would have been used, and timeout_ms
/// is the deadline. The deadline must be shorter than the processing time.
/// DroppedRequest ignores delay_ms and timeout_ms. The drop happens at the start time.
struct FailureConfig {
    FailureType type = FailureType::None;
    std::uint64_t delay_ms = 0;
    std::uint64_t timeout_ms = 0;
    std::uint64_t every_nth_request = 1;
};

inline void validate_failure_config(const FailureConfig& config) {
    if (config.every_nth_request == 0) {
        throw std::invalid_argument("every_nth_request must be at least 1");
    }
    if (config.type != FailureType::Timeout) {
        return;
    }
    if (config.timeout_ms == 0) {
        throw std::invalid_argument("timeout threshold must be greater than 0");
    }
    if (config.delay_ms <= config.timeout_ms) {
        throw std::invalid_argument(
            "timeout requires the processing delay to be greater than the timeout threshold");
    }
}

inline const char* to_string(FailureType type) noexcept {
    switch (type) {
    case FailureType::None:
        return "None";
    case FailureType::ServiceFailure:
        return "ServiceFailure";
    case FailureType::SlowResponse:
        return "SlowResponse";
    case FailureType::Timeout:
        return "Timeout";
    case FailureType::DroppedRequest:
        return "DroppedRequest";
    }
    return "None";
}

}  // namespace incidentlab

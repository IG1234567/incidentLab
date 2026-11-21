#pragma once

#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace incidentlab {

/// Retry settings for one service. max_attempts includes the first try.
///
/// A value of 3 is the original attempt plus two retries. Retries are not used when this
/// configuration is absent, or when max_attempts is 1.
/// The first retry waits initial_delay_ms. The next waits that delay times the multiplier.
struct RetryConfig {
    std::size_t max_attempts = 3;
    std::uint64_t initial_delay_ms = 100;
    double backoff_multiplier = 2.0;
};

inline void validate_retry_config(const RetryConfig& config) {
    if (config.max_attempts == 0) {
        throw std::invalid_argument("max_attempts must be at least 1");
    }
    if (!std::isfinite(config.backoff_multiplier) || config.backoff_multiplier < 1.0) {
        throw std::invalid_argument("backoff_multiplier must be a finite number at least 1");
    }
}

/// Delay before retry number `retry_number`. The first retry is number 1.
inline std::uint64_t retry_backoff_ms(const RetryConfig& config, std::size_t retry_number) {
    if (retry_number == 0) {
        throw std::invalid_argument("retry number must be at least 1");
    }
    // 2^64 is the first value that does not fit in std::uint64_t. It is exact as a double.
    constexpr double kUint64Limit = 18446744073709551616.0;
    double factor = 1.0;
    for (std::size_t step = 1; step < retry_number; ++step) {
        if (!(factor < kUint64Limit / config.backoff_multiplier)) {
            throw std::overflow_error("retry backoff overflow");
        }
        factor *= config.backoff_multiplier;
    }
    const double delay = static_cast<double>(config.initial_delay_ms) * factor;
    if (!(delay >= 0.0) || !(delay < kUint64Limit)) {
        throw std::overflow_error("retry backoff overflow");
    }
    return static_cast<std::uint64_t>(delay);
}

}  // namespace incidentlab

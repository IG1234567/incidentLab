#pragma once

#include "incidentlab/simulation/simulation_clock.hpp"

#include <cstddef>
#include <stdexcept>

namespace incidentlab {

enum class CircuitState { Closed, Open, HalfOpen };

inline const char* to_string(CircuitState state) noexcept {
    switch (state) {
    case CircuitState::Closed:
        return "CLOSED";
    case CircuitState::Open:
        return "OPEN";
    case CircuitState::HalfOpen:
        return "HALF_OPEN";
    }
    return "CLOSED";
}

enum class CircuitAdmission { Allow, Probe, Block };

/// How many consecutive failures open the circuit, and how long it stays open.
///
/// failure_threshold counts outcomes that are known, not starts. reset_timeout_ms is virtual
/// time measured from the moment the circuit opened. Zero means the next later action may probe.
struct CircuitBreakerConfig {
    std::size_t failure_threshold = 3;
    std::uint64_t reset_timeout_ms = 500;
};

inline void validate_circuit_breaker_config(const CircuitBreakerConfig& config) {
    if (config.failure_threshold == 0) {
        throw std::invalid_argument("failure_threshold must be at least 1");
    }
}

/// Breaker state for one service. The engine owns one of these per configured service.
///
/// An attempt that started while the circuit was closed, and finishes after another attempt
/// has already opened it, does not change the state. Only the half-open probe, or a failure
/// or success that finishes while the circuit is still closed, updates it.
class CircuitBreaker {
public:
    explicit CircuitBreaker(CircuitBreakerConfig config);

    CircuitState state() const noexcept;
    bool probe_in_flight() const noexcept;

    /// What a new attempt may do at `now`. Does not change the state.
    CircuitAdmission admission(SimulationClock::Time now) const;

    /// True when a failure of this attempt would open the circuit.
    bool failure_would_open(bool is_probe) const noexcept;

    void begin_probe();
    bool note_failure(SimulationClock::Time now, bool is_probe);
    bool note_success(bool is_probe);

private:
    bool deadline_reached(SimulationClock::Time now) const noexcept;

    CircuitBreakerConfig config_;
    CircuitState state_ = CircuitState::Closed;
    std::size_t consecutive_failures_ = 0;
    SimulationClock::Time opened_at_ = 0;
    bool probe_in_flight_ = false;
};

}  // namespace incidentlab

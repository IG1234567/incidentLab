#include "incidentlab/simulation/circuit_breaker.hpp"

#include <limits>

namespace incidentlab {

CircuitBreaker::CircuitBreaker(CircuitBreakerConfig config) : config_(config) {
    validate_circuit_breaker_config(config_);
}

CircuitState CircuitBreaker::state() const noexcept { return state_; }

bool CircuitBreaker::probe_in_flight() const noexcept { return probe_in_flight_; }

CircuitAdmission CircuitBreaker::admission(SimulationClock::Time now) const {
    if (state_ == CircuitState::Closed) {
        return CircuitAdmission::Allow;
    }
    if (probe_in_flight_) {
        return CircuitAdmission::Block;
    }
    if (state_ == CircuitState::Open && !deadline_reached(now)) {
        return CircuitAdmission::Block;
    }
    return CircuitAdmission::Probe;
}

bool CircuitBreaker::failure_would_open(bool is_probe) const noexcept {
    if (is_probe) {
        return true;
    }
    if (state_ != CircuitState::Closed) {
        return false;
    }
    return consecutive_failures_ + 1 >= config_.failure_threshold;
}

void CircuitBreaker::begin_probe() {
    state_ = CircuitState::HalfOpen;
    probe_in_flight_ = true;
}

bool CircuitBreaker::note_failure(SimulationClock::Time now, bool is_probe) {
    if (is_probe) {
        probe_in_flight_ = false;
        state_ = CircuitState::Open;
        opened_at_ = now;
        consecutive_failures_ = 0;
        return true;
    }
    if (state_ != CircuitState::Closed) {
        return false;
    }
    ++consecutive_failures_;
    if (consecutive_failures_ >= config_.failure_threshold) {
        state_ = CircuitState::Open;
        opened_at_ = now;
        consecutive_failures_ = 0;
        return true;
    }
    return false;
}

bool CircuitBreaker::note_success(bool is_probe) {
    if (is_probe) {
        probe_in_flight_ = false;
        state_ = CircuitState::Closed;
        consecutive_failures_ = 0;
        return true;
    }
    if (state_ != CircuitState::Closed) {
        return false;
    }
    consecutive_failures_ = 0;
    return false;
}

bool CircuitBreaker::deadline_reached(SimulationClock::Time now) const noexcept {
    if (now < opened_at_) {
        return false;
    }
    if (config_.reset_timeout_ms > std::numeric_limits<SimulationClock::Time>::max() - opened_at_) {
        return false;
    }
    return now >= opened_at_ + config_.reset_timeout_ms;
}

}  // namespace incidentlab

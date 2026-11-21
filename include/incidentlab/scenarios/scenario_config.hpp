#pragma once

#include "incidentlab/service_result.hpp"
#include "incidentlab/simulation/simulation_clock.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace incidentlab {

/// One logical call in the shared comparison workload.
///
/// Every scenario schedules these calls at these times. Resilience settings do not change
/// the id, the service, the operation, or the arrival time.
struct WorkloadCall {
    std::string request_id;
    std::string service_name;
    std::string operation;
    SimulationClock::Time arrival_ms = 0;
};

/// Outcomes for one logical request, by that request's own attempt number.
///
/// Attempt 1 uses index 0. A later attempt uses the last entry when the script is shorter.
/// The rule is attached to the request, not to a service-wide attempt counter, so a retry
/// in one scenario does not change which other request is supposed to fail.
struct FaultRule {
    std::string request_id;
    std::vector<ServiceStatus> attempts;
};

/// Resilience settings for one comparison run. The value is not modified while it runs.
///
/// Retry and the circuit breaker are optional. The engine has no separate dead-letter
/// switch: a terminal failure is dead-lettered only when that service has a retry policy
/// with at least one retry, or a circuit breaker. Scenario B and Scenario C therefore
/// still produce dead-letter entries. The comparison reports that engine behavior.
///
/// max_attempts includes the first try and is read only when retry_enabled is true.
/// failure_threshold and reset_timeout_ms are read only when circuit_breaker_enabled is true.
struct ScenarioConfig {
    std::string id;
    std::string name;
    bool retry_enabled = false;
    bool circuit_breaker_enabled = false;
    std::size_t max_attempts = 2;
    std::uint64_t retry_initial_delay_ms = 100;
    double retry_backoff_multiplier = 2.0;
    std::size_t circuit_failure_threshold = 2;
    std::uint64_t circuit_reset_timeout_ms = 400;
};

/// Processing time added to every delivery attempt in the comparison workload.
/// It is the same in every scenario. It is not a resilience setting.
constexpr std::uint64_t kComparisonProcessingDelayMs = 20;

/// Shared trace id for every comparison request.
constexpr const char* kComparisonTraceId = "TRACE-CMP";

/// Throws std::invalid_argument when the id, the name, or an enabled policy is invalid.
void validate_scenario_config(const ScenarioConfig& config);

/// Throws std::invalid_argument when two scenarios share an id. An empty list is valid.
void validate_scenario_set(const std::vector<ScenarioConfig>& scenarios);

/// The ten calls used by every predefined scenario, in schedule order.
const std::vector<WorkloadCall>& comparison_workload();

/// The fault script used by every predefined scenario.
const std::vector<FaultRule>& comparison_faults();

/// Baseline, Retry Only, Circuit Breaker, and Full Resilience, in that order.
std::vector<ScenarioConfig> predefined_scenarios();

}  // namespace incidentlab

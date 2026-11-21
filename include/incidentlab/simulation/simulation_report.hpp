#pragma once

#include "incidentlab/event_history.hpp"
#include "incidentlab/service_result.hpp"
#include "incidentlab/simulation/simulation_clock.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace incidentlab {

/// One request the engine has already executed, in execution order.
struct ExecutionRecord {
    SimulationClock::Time simulation_time = 0;
    std::string request_id;
    std::string trace_id;
    std::string service_name;
    std::string operation;
    ServiceResult result;
};

/// A consistent snapshot of one engine.
///
/// EventHistory inside the engine is the only log that grows. This copy and event_times
/// are taken together, so the timestamp at index i belongs to history.events()[i].
struct SimulationReport {
    SimulationClock::Time current_time = 0;
    std::size_t processed_requests = 0;
    std::size_t pending_requests = 0;
    std::size_t successful_requests = 0;
    std::size_t rejected_requests = 0;
    EventHistory history;
    std::vector<SimulationClock::Time> event_times;
    std::vector<ExecutionRecord> executions;
};

}  // namespace incidentlab

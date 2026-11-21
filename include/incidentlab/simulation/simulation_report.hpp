#pragma once

#include "incidentlab/event_history.hpp"
#include "incidentlab/service_result.hpp"
#include "incidentlab/simulation/dead_letter_queue.hpp"
#include "incidentlab/simulation/simulation_clock.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace incidentlab {

/// One request the engine has finished, in the order the terminal outcomes were recorded.
///
/// simulation_time is the virtual time of the final outcome. start_time is when the logical
/// request first started. They are equal when the outcome is recorded at that start.
struct ExecutionRecord {
    SimulationClock::Time simulation_time = 0;
    std::string request_id;
    std::string trace_id;
    std::string service_name;
    std::string operation;
    ServiceResult result;
    SimulationClock::Time start_time = 0;
};

/// A consistent snapshot of one engine.
///
/// EventHistory inside the engine is the only log that grows. This copy and event_times
/// are taken together, so the timestamp at index i belongs to history.events()[i].
struct SimulationReport {
    SimulationClock::Time current_time = 0;
    std::size_t processed_requests = 0;
    std::size_t pending_requests = 0;
    std::size_t in_progress_requests = 0;
    std::size_t successful_requests = 0;
    std::size_t rejected_requests = 0;
    std::size_t failed_requests = 0;
    std::size_t timed_out_requests = 0;
    std::size_t dropped_requests = 0;
    std::size_t circuit_rejected_requests = 0;
    std::size_t total_attempts = 0;
    std::size_t retry_attempts = 0;
    std::size_t dead_lettered_requests = 0;
    EventHistory history;
    std::vector<SimulationClock::Time> event_times;
    std::vector<ExecutionRecord> executions;
    std::vector<DeadLetterEntry> dead_letters;
};

}  // namespace incidentlab

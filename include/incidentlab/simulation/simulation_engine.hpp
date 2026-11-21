#pragma once

#include "incidentlab/event_history.hpp"
#include "incidentlab/service.hpp"
#include "incidentlab/simulation/event_scheduler.hpp"
#include "incidentlab/simulation/simulation_clock.hpp"
#include "incidentlab/simulation/simulation_report.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace incidentlab {

/// Runs scheduled requests in virtual time and records what happened.
///
/// register_service() stores a non-owning pointer. The Service must stay alive for as long
/// as this engine can still call it. The engine does not delete services.
///
/// Service lookup uses std::unordered_map and is O(1) on average. Processing N already
/// scheduled requests is O(N log N) because each removal from the scheduler is O(log N).
class SimulationEngine {
public:
    void register_service(Service& service);

    /// Queues the request. It does not call the service.
    /// Throws std::invalid_argument when scheduled_time is earlier than the clock.
    void schedule_request(Request request, SimulationClock::Time scheduled_time);

    /// Executes up to max_requests pending requests. Returns how many ran.
    /// A limit of 0 runs nothing. Time, history, and the remaining queue are kept.
    std::size_t run_next(std::size_t max_requests);

    /// Executes every pending request.
    void run();

    const SimulationClock& clock() const noexcept;
    const EventHistory& history() const noexcept;
    SimulationReport report() const;

private:
    void execute(const ScheduledEvent& scheduled);
    void record(Event event);

    SimulationClock clock_;
    EventScheduler scheduler_;
    EventHistory history_;
    std::vector<SimulationClock::Time> event_times_;
    std::vector<ExecutionRecord> executions_;
    std::unordered_map<std::string, Service*> services_;
    std::size_t successful_requests_ = 0;
    std::size_t rejected_requests_ = 0;
};

}  // namespace incidentlab

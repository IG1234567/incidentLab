#pragma once

#include "incidentlab/event_history.hpp"
#include "incidentlab/service.hpp"
#include "incidentlab/simulation/event_scheduler.hpp"
#include "incidentlab/simulation/failure_config.hpp"
#include "incidentlab/simulation/simulation_clock.hpp"
#include "incidentlab/simulation/simulation_report.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace incidentlab {

/// Runs scheduled requests in virtual time and records what happened.
///
/// register_service() stores a non-owning pointer. The Service must stay alive for as long
/// as this engine can still call it. The engine does not delete services.
/// Service::handle must not call back into the same engine.
///
/// Service lookup uses std::unordered_map and is O(1) on average. Processing N already
/// scheduled actions is O(N log N) because each removal from the scheduler is O(log N).
///
/// A request with no injected delay starts and finishes in one step. A slow response, a
/// delayed service failure, or a timeout schedules a Finish action on the same queue.
/// The clock moves only forward, to the action being committed.
///
/// Failure injection is optional. With no configuration, behavior matches Stage 3.
/// configure_failure replaces the policy for that service name and resets its counter.
/// FailureType::None removes the policy. A request already started keeps the outcome chosen
/// at its start.
///
/// The per-service counter increments when a start action is committed, in execution order.
/// That includes unsupported operations that reach the service. A dropped request does not
/// call handle and does not record RequestReceived, but it still consumes its turn in the
/// counter so every Nth drop stays aligned. The failure applies when the count is divisible
/// by every_nth_request. Other requests for that service run normally.
///
/// Exception safety: handle() runs before the engine commits a start. If it throws, the
/// start stays queued, nothing is recorded, the failure counter stays unchanged, and the
/// exception propagates. It is not stored as a simulated failure. A simulated ServiceFailure,
/// Timeout, or DroppedRequest does not throw. A Finish action does not call handle. If
/// committing a finish throws, the recorded partial outcome is rolled back, the finish stays
/// queued, and the exception propagates. A later run_next retries that finish.
///
/// pending_requests counts starts that have not run. in_progress_requests counts starts whose
/// finish is still queued. processed_requests counts terminal outcomes only:
/// successful + rejected + failed + timed out + dropped.
class SimulationEngine {
public:
    void register_service(Service& service);

    /// Replaces the failure policy for service_name and resets that service's counter.
    /// FailureType::None removes the policy. Throws std::invalid_argument when the name is
    /// empty or the configuration is invalid.
    void configure_failure(const std::string& service_name, FailureConfig config);

    /// Queues the request. It does not call the service.
    /// Throws std::invalid_argument when scheduled_time is earlier than the clock or when
    /// request_id is already scheduled or in progress.
    void schedule_request(Request request, SimulationClock::Time scheduled_time);

    /// Finishes up to max_requests requests. Returns how many reached a terminal outcome.
    /// A limit of 0 runs nothing. Actions that only start a delayed request do not count.
    ///
    /// If a service throws, earlier terminal outcomes from this call stay committed, the
    /// throwing start stays queued, and the exception leaves this function.
    std::size_t run_next(std::size_t max_requests);

    /// Executes until no start or finish actions remain.
    void run();

    const SimulationClock& clock() const noexcept;
    const EventHistory& history() const noexcept;
    SimulationReport report() const;

private:
    struct FailurePolicy {
        FailureConfig config;
        std::uint64_t requests_seen = 0;
    };

    struct InFlight {
        ServiceResult result;
        std::string service_name;
        SimulationClock::Time start_time = 0;
    };

    void process(ScheduledEvent scheduled);
    void process_start(const ScheduledEvent& scheduled);
    void process_finish(const ScheduledEvent& scheduled);
    void commit(std::vector<Event> events, std::vector<SimulationClock::Time> times,
                bool record_execution, ExecutionRecord execution, bool count_status,
                ServiceStatus status, SimulationClock::Time advance_to);
    void rollback(std::size_t events_before, std::size_t times_before,
                  std::size_t executions_before, bool counted, ServiceStatus status);
    void add_status(ServiceStatus status);
    void remove_status(ServiceStatus status);
    void record(Event event, SimulationClock::Time time);
    static EventType event_type_for(ServiceStatus status);
    static SimulationClock::Time add_time(SimulationClock::Time start, SimulationClock::Time delta);

    SimulationClock clock_;
    EventScheduler scheduler_;
    EventHistory history_;
    std::vector<SimulationClock::Time> event_times_;
    std::vector<ExecutionRecord> executions_;
    std::unordered_map<std::string, Service*> services_;
    std::unordered_map<std::string, FailurePolicy> failures_;
    std::unordered_map<std::string, InFlight> in_flight_;
    std::unordered_set<std::string> known_ids_;
    std::size_t not_started_ = 0;
    std::size_t successful_requests_ = 0;
    std::size_t rejected_requests_ = 0;
    std::size_t failed_requests_ = 0;
    std::size_t timed_out_requests_ = 0;
    std::size_t dropped_requests_ = 0;
};

}  // namespace incidentlab

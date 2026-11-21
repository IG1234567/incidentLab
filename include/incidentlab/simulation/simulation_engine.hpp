#pragma once

#include "incidentlab/event_history.hpp"
#include "incidentlab/service.hpp"
#include "incidentlab/simulation/circuit_breaker.hpp"
#include "incidentlab/simulation/dead_letter_queue.hpp"
#include "incidentlab/simulation/event_scheduler.hpp"
#include "incidentlab/simulation/failure_config.hpp"
#include "incidentlab/simulation/retry_config.hpp"
#include "incidentlab/simulation/simulation_clock.hpp"
#include "incidentlab/simulation/simulation_report.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
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
/// Failure injection, retry, and the circuit breaker are optional and scoped to a service
/// name. With none of them configured, behavior matches Stage 4. configure_retry and
/// configure_circuit_breaker replace that service's settings. A circuit reconfiguration
/// also resets that breaker to closed. A request already started keeps the attempt it began.
///
/// Retries are further Start actions on the same scheduler. The clock never moves backward.
/// An unexpected exception is not retried. Failed, TimedOut, and Dropped are retried until
/// max_attempts or until the circuit is open. Rejected is not retried.
///
/// The dead-letter queue receives a logical request only when a retry policy or a circuit
/// breaker is configured for its service and the failure is terminal. Stage 4 runs do not
/// write it. Successful and rejected requests are not written there.
///
/// processed_requests counts final logical outcomes:
/// successful + rejected + failed + timed out + dropped + circuit rejected.
/// total_attempts counts starts that were allowed to reach the service. A blocked attempt
/// is not one of them. pending_requests are starts that have not run. in_progress_requests
/// have started and are waiting for a delayed outcome.
///
/// Exception safety: handle() runs before a start is committed. If it throws, the start
/// stays queued, nothing new is recorded, and the breaker is unchanged. A finish that fails
/// to commit is rolled back and left queued.
class SimulationEngine {
public:
    void register_service(Service& service);

    /// Replaces the failure policy for service_name and resets that service's failure counter.
    /// FailureType::None removes the policy.
    void configure_failure(const std::string& service_name, FailureConfig config);

    /// Replaces the retry policy for service_name. Does not cancel a retry already queued.
    void configure_retry(const std::string& service_name, RetryConfig config);

    /// Replaces the circuit breaker for service_name and resets it to closed.
    void configure_circuit_breaker(const std::string& service_name, CircuitBreakerConfig config);

    /// Empty when that service has no breaker.
    std::optional<CircuitState> circuit_state(const std::string& service_name) const;

    const DeadLetterQueue& dead_letters() const noexcept;

    /// Queues the request. It does not call the service.
    /// Throws std::invalid_argument when scheduled_time is earlier than the clock or when
    /// request_id is already scheduled or in progress.
    void schedule_request(Request request, SimulationClock::Time scheduled_time);

    /// Finishes up to max_requests logical requests. Returns how many reached a final outcome.
    /// A retry or a delayed finish does not by itself count. A limit of 0 runs nothing.
    /// This is not one scheduler action: a slow request's start and finish are separate actions,
    /// and run_next keeps going until a logical request reaches a final outcome.
    std::size_t run_next(std::size_t max_requests);

    /// Runs one scheduler action, a Start or a Finish. Returns false when nothing is queued.
    ///
    /// The visualization Step control uses this. It does not change run_next(). Virtual time
    /// moves only as far as that one action requires.
    bool step_once();

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

    struct LogicalRequest {
        std::size_t attempts = 0;
        SimulationClock::Time started_at = 0;
    };

    struct InFlight {
        ServiceResult result;
        std::string service_name;
        SimulationClock::Time start_time = 0;
        std::size_t attempt_number = 0;
        bool probe = false;
    };

    void process(ScheduledEvent scheduled);
    void process_start(const ScheduledEvent& scheduled);
    void process_finish(const ScheduledEvent& scheduled);
    void reject_blocked(const Request& request, const std::string& service_name,
                        SimulationClock::Time time);
    /// Returns true when a later attempt was scheduled.
    bool finish_attempt(const Request& request, const std::string& service_name,
                        const ServiceResult& result, SimulationClock::Time start_time,
                        SimulationClock::Time outcome_time, std::size_t attempt_number, bool probe,
                        bool include_received);
    void ensure_retry_fits(const std::string& service_name, ServiceStatus status,
                           std::size_t attempt_number, SimulationClock::Time outcome_time,
                           bool failure_opens_circuit) const;
    void commit(std::vector<Event> events, std::vector<SimulationClock::Time> times,
                bool record_execution, ExecutionRecord execution, bool count_status,
                ServiceStatus status, SimulationClock::Time advance_to);
    void rollback(std::size_t events_before, std::size_t times_before,
                  std::size_t executions_before, bool counted, ServiceStatus status);
    void add_status(ServiceStatus status);
    void remove_status(ServiceStatus status);
    void record(Event event, SimulationClock::Time time);
    static EventType event_type_for(ServiceStatus status);
    static bool is_service_failure(ServiceStatus status) noexcept;
    static SimulationClock::Time add_time(SimulationClock::Time start, SimulationClock::Time delta);

    SimulationClock clock_;
    EventScheduler scheduler_;
    EventHistory history_;
    std::vector<SimulationClock::Time> event_times_;
    std::vector<ExecutionRecord> executions_;
    std::unordered_map<std::string, Service*> services_;
    std::unordered_map<std::string, FailurePolicy> failures_;
    std::unordered_map<std::string, RetryConfig> retries_;
    std::unordered_map<std::string, CircuitBreaker> breakers_;
    std::unordered_map<std::string, LogicalRequest> logical_;
    std::unordered_map<std::string, InFlight> in_flight_;
    std::unordered_set<std::string> known_ids_;
    DeadLetterQueue dead_letters_;
    std::size_t not_started_ = 0;
    std::size_t total_attempts_ = 0;
    std::size_t retry_attempts_ = 0;
    std::size_t successful_requests_ = 0;
    std::size_t rejected_requests_ = 0;
    std::size_t failed_requests_ = 0;
    std::size_t timed_out_requests_ = 0;
    std::size_t dropped_requests_ = 0;
    std::size_t circuit_rejected_requests_ = 0;
};

}  // namespace incidentlab

#include "incidentlab/simulation/simulation_engine.hpp"

#include "incidentlab/event.hpp"

#include <cstdint>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace incidentlab {
namespace {

std::string format_event_id(std::uint64_t sequence) {
    std::ostringstream out;
    out << "EVT-" << std::setw(3) << std::setfill('0') << sequence;
    return out.str();
}

}  // namespace

void SimulationEngine::register_service(Service& service) {
    const std::string name = service.name();
    if (services_.find(name) != services_.end()) {
        throw std::invalid_argument("service already registered: " + name);
    }
    services_.emplace(name, &service);
}

void SimulationEngine::configure_failure(const std::string& service_name, FailureConfig config) {
    if (service_name.empty()) {
        throw std::invalid_argument("service name must not be empty");
    }
    validate_failure_config(config);
    if (config.type == FailureType::None) {
        failures_.erase(service_name);
        return;
    }
    failures_[service_name] = FailurePolicy{config, 0};
}

void SimulationEngine::configure_retry(const std::string& service_name, RetryConfig config) {
    if (service_name.empty()) {
        throw std::invalid_argument("service name must not be empty");
    }
    validate_retry_config(config);
    retries_.insert_or_assign(service_name, config);
}

void SimulationEngine::configure_circuit_breaker(const std::string& service_name,
                                                 CircuitBreakerConfig config) {
    if (service_name.empty()) {
        throw std::invalid_argument("service name must not be empty");
    }
    breakers_.insert_or_assign(service_name, CircuitBreaker{config});
}

std::optional<CircuitState> SimulationEngine::circuit_state(const std::string& service_name) const {
    const auto found = breakers_.find(service_name);
    if (found == breakers_.end()) {
        return std::nullopt;
    }
    return found->second.state();
}

const DeadLetterQueue& SimulationEngine::dead_letters() const noexcept { return dead_letters_; }

void SimulationEngine::schedule_request(Request request, SimulationClock::Time scheduled_time) {
    const std::string request_id = request.request_id;
    if (known_ids_.find(request_id) != known_ids_.end()) {
        throw std::invalid_argument("duplicate request id: " + request_id);
    }
    known_ids_.insert(request_id);
    try {
        scheduler_.schedule(std::move(request), scheduled_time, clock_.now());
    } catch (...) {
        known_ids_.erase(request_id);
        throw;
    }
    ++not_started_;
}

std::size_t SimulationEngine::run_next(std::size_t max_requests) {
    std::size_t finished = 0;
    while (finished < max_requests && scheduler_.has_pending()) {
        const std::size_t processed_before = executions_.size();
        process(scheduler_.next());
        scheduler_.discard_next();
        finished += executions_.size() - processed_before;
    }
    return finished;
}

void SimulationEngine::run() {
    while (scheduler_.has_pending()) {
        run_next(1);
    }
}

const SimulationClock& SimulationEngine::clock() const noexcept { return clock_; }

const EventHistory& SimulationEngine::history() const noexcept { return history_; }

SimulationReport SimulationEngine::report() const {
    SimulationReport report;
    report.current_time = clock_.now();
    report.processed_requests = executions_.size();
    report.pending_requests = not_started_;
    report.in_progress_requests = in_flight_.size();
    report.successful_requests = successful_requests_;
    report.rejected_requests = rejected_requests_;
    report.failed_requests = failed_requests_;
    report.timed_out_requests = timed_out_requests_;
    report.dropped_requests = dropped_requests_;
    report.circuit_rejected_requests = circuit_rejected_requests_;
    report.total_attempts = total_attempts_;
    report.retry_attempts = retry_attempts_;
    report.dead_lettered_requests = dead_letters_.size();
    report.history = history_;
    report.event_times = event_times_;
    report.executions = executions_;
    report.dead_letters = dead_letters_.entries();
    return report;
}

void SimulationEngine::process(ScheduledEvent scheduled) {
    if (scheduled.scheduled_time < clock_.now()) {
        throw std::invalid_argument(
            "cannot execute a request at " + std::to_string(scheduled.scheduled_time) +
            " ms before simulation time " + std::to_string(clock_.now()) + " ms");
    }
    if (scheduled.action == ScheduledAction::Finish) {
        process_finish(scheduled);
    } else {
        process_start(scheduled);
    }
}

void SimulationEngine::process_start(const ScheduledEvent& scheduled) {
    const SimulationClock::Time start = scheduled.scheduled_time;
    const Request request = scheduled.request;
    const std::string request_id = request.request_id;

    const auto found = services_.find(request.target_service);
    Service* const service = found == services_.end() ? nullptr : found->second;
    std::string service_name = request.target_service;
    if (service != nullptr) {
        service_name = service->name();
    }

    CircuitBreaker* breaker = nullptr;
    const auto breaker_found = breakers_.find(request.target_service);
    if (breaker_found != breakers_.end()) {
        breaker = &breaker_found->second;
    }
    const CircuitAdmission admission =
        breaker == nullptr ? CircuitAdmission::Allow : breaker->admission(start);
    if (admission == CircuitAdmission::Block) {
        reject_blocked(request, service_name, start);
        --not_started_;
        return;
    }
    const bool probe = admission == CircuitAdmission::Probe;

    FailurePolicy* policy = nullptr;
    const auto policy_found = failures_.find(request.target_service);
    if (policy_found != failures_.end()) {
        policy = &policy_found->second;
    }

    std::uint64_t next_count = 0;
    bool applies = false;
    if (policy != nullptr) {
        if (policy->requests_seen == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("failure counter overflow");
        }
        next_count = policy->requests_seen + 1;
        applies = next_count % policy->config.every_nth_request == 0;
    }

    const FailureType applied =
        applies && policy != nullptr ? policy->config.type : FailureType::None;
    const bool dropped = applied == FailureType::DroppedRequest;
    const bool call_handle = !dropped && applied != FailureType::ServiceFailure &&
                             applied != FailureType::Timeout && service != nullptr;

    ServiceResult result{ServiceStatus::Rejected, "Unknown service: " + request.target_service};
    SimulationClock::Time completion = start;
    if (dropped) {
        result = ServiceResult{ServiceStatus::Dropped, "Request dropped before delivery"};
    } else if (applied == FailureType::ServiceFailure) {
        result = ServiceResult{ServiceStatus::Failed, "Injected service failure"};
        completion = add_time(start, policy->config.delay_ms);
    } else if (applied == FailureType::Timeout) {
        result = ServiceResult{ServiceStatus::TimedOut, "Request timed out"};
        completion = add_time(start, policy->config.timeout_ms);
    } else if (applied == FailureType::SlowResponse) {
        completion = add_time(start, policy->config.delay_ms);
    }

    if (call_handle) {
        result = service->handle(request);
    }

    const auto logical_found = logical_.find(request_id);
    const bool had_logical = logical_found != logical_.end();
    const LogicalRequest old_logical = had_logical ? logical_found->second : LogicalRequest{};
    LogicalRequest updated = old_logical;
    if (updated.attempts == 0) {
        updated.started_at = start;
    }
    const std::size_t attempt_number = updated.attempts + 1;
    updated.attempts = attempt_number;

    const bool opens = breaker != nullptr && is_service_failure(result.status) &&
                       breaker->failure_would_open(probe);
    ensure_retry_fits(request.target_service, result.status, attempt_number, completion, opens);

    const bool immediate = dropped || completion == start;
    bool retried = false;
    std::optional<CircuitBreaker> saved_breaker;
    if (breaker != nullptr) {
        saved_breaker = *breaker;
    }
    const std::size_t events_before = history_.size();
    const std::size_t times_before = event_times_.size();
    const std::size_t executions_before = executions_.size();
    try {
        logical_[request_id] = updated;
        if (probe && breaker != nullptr) {
            breaker->begin_probe();
        }
        if (immediate) {
            retried = finish_attempt(request, service_name, result, start, start, attempt_number,
                                     probe, true);
        } else {
            std::vector<Event> starting;
            std::vector<SimulationClock::Time> times;
            if (probe) {
                starting.emplace_back(format_event_id(history_.size() + starting.size() + 1),
                                      request.request_id, request.trace_id, service_name,
                                      EventType::CircuitHalfOpened, "Circuit half-open");
                times.push_back(start);
            }
            starting.emplace_back(format_event_id(history_.size() + starting.size() + 1),
                                  request.request_id, request.trace_id, service_name,
                                  EventType::RequestReceived, "Request received");
            times.push_back(start);
            in_flight_.insert_or_assign(
                request_id, InFlight{result, service_name, start, attempt_number, probe});
            try {
                for (std::size_t index = 0; index < starting.size(); ++index) {
                    record(std::move(starting[index]), times[index]);
                }
                scheduler_.schedule(request, completion, clock_.now(), ScheduledAction::Finish);
                clock_.advance_to(start);
            } catch (...) {
                rollback(events_before, times_before, executions_before, false, result.status);
                in_flight_.erase(request_id);
                throw;
            }
        }
    } catch (...) {
        if (saved_breaker.has_value() && breaker != nullptr) {
            *breaker = *saved_breaker;
        }
        if (had_logical) {
            logical_[request_id] = old_logical;
        } else {
            logical_.erase(request_id);
        }
        throw;
    }

    if (policy != nullptr) {
        policy->requests_seen = next_count;
    }
    ++total_attempts_;
    --not_started_;
    if (immediate && retried) {
        ++retry_attempts_;
        ++not_started_;
    } else if (immediate) {
        logical_.erase(request_id);
        known_ids_.erase(request_id);
    }
}

void SimulationEngine::process_finish(const ScheduledEvent& scheduled) {
    const Request& request = scheduled.request;
    const auto found = in_flight_.find(request.request_id);
    if (found == in_flight_.end()) {
        throw std::logic_error("finish action has no matching start: " + request.request_id);
    }
    const InFlight flight = found->second;

    CircuitBreaker* breaker = nullptr;
    const auto breaker_found = breakers_.find(request.target_service);
    if (breaker_found != breakers_.end()) {
        breaker = &breaker_found->second;
    }
    std::optional<CircuitBreaker> saved_breaker;
    if (breaker != nullptr) {
        saved_breaker = *breaker;
    }
    const auto logical_found = logical_.find(request.request_id);
    const bool had_logical = logical_found != logical_.end();
    const LogicalRequest old_logical = had_logical ? logical_found->second : LogicalRequest{};

    const bool opens = breaker != nullptr && is_service_failure(flight.result.status) &&
                       breaker->failure_would_open(flight.probe);
    ensure_retry_fits(request.target_service, flight.result.status, flight.attempt_number,
                      scheduled.scheduled_time, opens);

    bool retried = false;
    try {
        retried = finish_attempt(request, flight.service_name, flight.result, flight.start_time,
                                 scheduled.scheduled_time, flight.attempt_number, flight.probe,
                                 false);
    } catch (...) {
        if (saved_breaker.has_value() && breaker != nullptr) {
            *breaker = *saved_breaker;
        }
        if (had_logical) {
            logical_[request.request_id] = old_logical;
        } else {
            logical_.erase(request.request_id);
        }
        throw;
    }

    in_flight_.erase(request.request_id);
    if (retried) {
        ++retry_attempts_;
        ++not_started_;
    } else {
        logical_.erase(request.request_id);
        known_ids_.erase(request.request_id);
    }
}

void SimulationEngine::reject_blocked(const Request& request, const std::string& service_name,
                                      SimulationClock::Time time) {
    const auto logical_found = logical_.find(request.request_id);
    const std::size_t attempts =
        logical_found == logical_.end() ? 0 : logical_found->second.attempts;
    const SimulationClock::Time started =
        logical_found == logical_.end() ? time : logical_found->second.started_at;
    const ServiceResult result{ServiceStatus::CircuitRejected, "Circuit breaker open"};
    Event rejected{format_event_id(history_.size() + 1), request.request_id, request.trace_id,
                   service_name, EventType::CircuitRejected, result.message};
    Event dead{format_event_id(history_.size() + 2), request.request_id, request.trace_id,
               service_name, EventType::DeadLettered, "Moved to dead-letter queue"};
    ExecutionRecord execution{time, request.request_id, request.trace_id, service_name,
                              request.operation, result, started};
    dead_letters_.push(DeadLetterEntry{request, attempts, result.status, time, result.message});
    try {
        commit({std::move(rejected), std::move(dead)}, {time, time}, true, std::move(execution),
               true, result.status, time);
    } catch (...) {
        dead_letters_.pop_last();
        throw;
    }
    logical_.erase(request.request_id);
    known_ids_.erase(request.request_id);
}

bool SimulationEngine::finish_attempt(const Request& request, const std::string& service_name,
                                      const ServiceResult& result, SimulationClock::Time start_time,
                                      SimulationClock::Time outcome_time,
                                      std::size_t attempt_number, bool probe,
                                      bool include_received) {
    CircuitBreaker* breaker = nullptr;
    const auto breaker_found = breakers_.find(request.target_service);
    if (breaker_found != breakers_.end()) {
        breaker = &breaker_found->second;
    }

    bool opened = false;
    bool closed = false;
    if (breaker != nullptr && (probe || breaker->state() == CircuitState::Closed)) {
        if (result.status == ServiceStatus::Success) {
            closed = breaker->note_success(probe);
        } else if (is_service_failure(result.status)) {
            opened = breaker->note_failure(outcome_time, probe);
        }
    }

    const auto retry_found = retries_.find(request.target_service);
    const bool retry_configured =
        retry_found != retries_.end() && retry_found->second.max_attempts >= 2;
    bool retry = retry_configured && is_service_failure(result.status) &&
                 attempt_number < retry_found->second.max_attempts;
    if (retry && breaker != nullptr && breaker->state() != CircuitState::Closed) {
        retry = false;
    }

    std::uint64_t delay = 0;
    SimulationClock::Time next_time = outcome_time;
    if (retry) {
        delay = retry_backoff_ms(retry_found->second, attempt_number);
        next_time = add_time(outcome_time, delay);
    }

    const bool track_dlq = breaker != nullptr || retry_configured;
    const bool send_dlq = !retry && track_dlq && is_service_failure(result.status);

    const auto logical_found = logical_.find(request.request_id);
    const SimulationClock::Time started =
        logical_found == logical_.end() ? start_time : logical_found->second.started_at;

    std::vector<Event> events;
    std::vector<SimulationClock::Time> times;
    auto add_event = [&](EventType type, const std::string& message) {
        events.emplace_back(format_event_id(history_.size() + events.size() + 1),
                            request.request_id, request.trace_id, service_name, type, message);
        times.push_back(outcome_time);
    };
    if (probe && include_received) {
        add_event(EventType::CircuitHalfOpened, "Circuit half-open");
    }
    if (include_received && result.status != ServiceStatus::Dropped) {
        add_event(EventType::RequestReceived, "Request received");
    }
    add_event(event_type_for(result.status), result.message);
    if (opened) {
        add_event(EventType::CircuitOpened, "Circuit opened");
    }
    if (closed) {
        add_event(EventType::CircuitClosed, "Circuit closed");
    }
    if (retry) {
        add_event(EventType::RetryScheduled, "Retry scheduled in " + std::to_string(delay) + " ms");
    }
    if (send_dlq) {
        add_event(EventType::DeadLettered, "Moved to dead-letter queue");
    }

    ExecutionRecord execution{outcome_time, request.request_id, request.trace_id, service_name,
                              request.operation, result, started};
    bool dlq_pushed = false;
    const std::size_t events_before = history_.size();
    const std::size_t times_before = event_times_.size();
    const std::size_t executions_before = executions_.size();
    bool counted = false;
    try {
        if (send_dlq) {
            dead_letters_.push(DeadLetterEntry{request, attempt_number, result.status, outcome_time,
                                               result.message});
            dlq_pushed = true;
        }
        for (std::size_t index = 0; index < events.size(); ++index) {
            record(std::move(events[index]), times[index]);
        }
        if (!retry) {
            executions_.push_back(std::move(execution));
            add_status(result.status);
            counted = true;
        }
        if (retry) {
            scheduler_.schedule(request, next_time, clock_.now(), ScheduledAction::Start);
        }
        clock_.advance_to(outcome_time);
    } catch (...) {
        if (counted) {
            remove_status(result.status);
        }
        rollback(events_before, times_before, executions_before, false, result.status);
        if (dlq_pushed) {
            dead_letters_.pop_last();
        }
        throw;
    }
    return retry;
}

void SimulationEngine::ensure_retry_fits(const std::string& service_name, ServiceStatus status,
                                         std::size_t attempt_number,
                                         SimulationClock::Time outcome_time,
                                         bool failure_opens_circuit) const {
    if (!is_service_failure(status) || failure_opens_circuit) {
        return;
    }
    const auto retry_found = retries_.find(service_name);
    if (retry_found == retries_.end() || retry_found->second.max_attempts < 2 ||
        attempt_number >= retry_found->second.max_attempts) {
        return;
    }
    const std::uint64_t delay = retry_backoff_ms(retry_found->second, attempt_number);
    add_time(outcome_time, delay);
}

void SimulationEngine::commit(std::vector<Event> events, std::vector<SimulationClock::Time> times,
                              bool record_execution, ExecutionRecord execution, bool count_status,
                              ServiceStatus status, SimulationClock::Time advance_to) {
    event_times_.reserve(event_times_.size() + times.size());
    history_.reserve(history_.size() + events.size());
    if (record_execution) {
        executions_.reserve(executions_.size() + 1);
    }

    const std::size_t events_before = history_.size();
    const std::size_t times_before = event_times_.size();
    const std::size_t executions_before = executions_.size();
    bool counted = false;
    try {
        for (std::size_t index = 0; index < events.size(); ++index) {
            record(std::move(events[index]), times[index]);
        }
        if (record_execution) {
            executions_.push_back(std::move(execution));
        }
        if (count_status) {
            add_status(status);
            counted = true;
        }
        clock_.advance_to(advance_to);
    } catch (...) {
        rollback(events_before, times_before, executions_before, counted, status);
        throw;
    }
}

void SimulationEngine::rollback(std::size_t events_before, std::size_t times_before,
                                std::size_t executions_before, bool counted, ServiceStatus status) {
    if (counted) {
        remove_status(status);
    }
    while (executions_.size() > executions_before) {
        executions_.pop_back();
    }
    while (event_times_.size() > times_before) {
        event_times_.pop_back();
    }
    while (history_.size() > events_before) {
        history_.drop_last();
    }
}

void SimulationEngine::add_status(ServiceStatus status) {
    switch (status) {
    case ServiceStatus::Success:
        ++successful_requests_;
        break;
    case ServiceStatus::Rejected:
        ++rejected_requests_;
        break;
    case ServiceStatus::Failed:
        ++failed_requests_;
        break;
    case ServiceStatus::TimedOut:
        ++timed_out_requests_;
        break;
    case ServiceStatus::Dropped:
        ++dropped_requests_;
        break;
    case ServiceStatus::CircuitRejected:
        ++circuit_rejected_requests_;
        break;
    }
}

void SimulationEngine::remove_status(ServiceStatus status) {
    switch (status) {
    case ServiceStatus::Success:
        --successful_requests_;
        break;
    case ServiceStatus::Rejected:
        --rejected_requests_;
        break;
    case ServiceStatus::Failed:
        --failed_requests_;
        break;
    case ServiceStatus::TimedOut:
        --timed_out_requests_;
        break;
    case ServiceStatus::Dropped:
        --dropped_requests_;
        break;
    case ServiceStatus::CircuitRejected:
        --circuit_rejected_requests_;
        break;
    }
}

void SimulationEngine::record(Event event, SimulationClock::Time time) {
    event_times_.push_back(time);
    try {
        history_.record(std::move(event));
    } catch (...) {
        event_times_.pop_back();
        throw;
    }
}

EventType SimulationEngine::event_type_for(ServiceStatus status) {
    switch (status) {
    case ServiceStatus::Success:
        return EventType::RequestCompleted;
    case ServiceStatus::Rejected:
        return EventType::RequestRejected;
    case ServiceStatus::Failed:
        return EventType::RequestFailed;
    case ServiceStatus::TimedOut:
        return EventType::RequestTimedOut;
    case ServiceStatus::Dropped:
        return EventType::RequestDropped;
    case ServiceStatus::CircuitRejected:
        return EventType::CircuitRejected;
    }
    return EventType::RequestRejected;
}

bool SimulationEngine::is_service_failure(ServiceStatus status) noexcept {
    return status == ServiceStatus::Failed || status == ServiceStatus::TimedOut ||
           status == ServiceStatus::Dropped;
}

SimulationClock::Time SimulationEngine::add_time(SimulationClock::Time start,
                                                SimulationClock::Time delta) {
    if (delta > std::numeric_limits<SimulationClock::Time>::max() - start) {
        throw std::overflow_error("simulation time overflow");
    }
    return start + delta;
}

}  // namespace incidentlab

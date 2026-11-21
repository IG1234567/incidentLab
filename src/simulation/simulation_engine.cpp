#include "incidentlab/simulation/simulation_engine.hpp"

#include "incidentlab/event.hpp"

#include <cstdint>
#include <iomanip>
#include <limits>
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
        // Copy happens inside process. A throw leaves this action at the front of the queue.
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
    report.history = history_;
    report.event_times = event_times_;
    report.executions = executions_;
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

    const auto found = services_.find(request.target_service);
    Service* const service = found == services_.end() ? nullptr : found->second;
    std::string service_name = request.target_service;
    if (service != nullptr) {
        service_name = service->name();
    }

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

    // handle() runs before any commit. A throw leaves the start queued and the counter unchanged.
    if (call_handle) {
        result = service->handle(request);
    }

    const std::string request_id = request.request_id;
    if (dropped || completion == start) {
        std::vector<Event> events;
        std::vector<SimulationClock::Time> times;
        if (!dropped) {
            events.push_back(Event{format_event_id(history_.size() + 1), request.request_id,
                                    request.trace_id, service_name, EventType::RequestReceived,
                                    "Request received"});
            times.push_back(start);
        }
        events.push_back(Event{format_event_id(history_.size() + events.size() + 1),
                                request.request_id, request.trace_id, service_name,
                                event_type_for(result.status), result.message});
        times.push_back(start);
        ExecutionRecord execution{start, request.request_id, request.trace_id, service_name,
                                  request.operation, result, start};
        commit(std::move(events), std::move(times), true, std::move(execution), true, result.status,
               start);
        known_ids_.erase(request_id);
    } else {
        Event received{format_event_id(history_.size() + 1), request.request_id, request.trace_id,
                       service_name, EventType::RequestReceived, "Request received"};
        in_flight_.insert_or_assign(
            request_id, InFlight{result, service_name, start});
        const std::size_t events_before = history_.size();
        const std::size_t times_before = event_times_.size();
        const std::size_t executions_before = executions_.size();
        try {
            record(std::move(received), start);
            scheduler_.schedule(request, completion, clock_.now(), ScheduledAction::Finish);
            clock_.advance_to(start);
        } catch (...) {
            rollback(events_before, times_before, executions_before, false, result.status);
            in_flight_.erase(request_id);
            throw;
        }
    }

    if (policy != nullptr) {
        policy->requests_seen = next_count;
    }
    --not_started_;
}

void SimulationEngine::process_finish(const ScheduledEvent& scheduled) {
    const Request& request = scheduled.request;
    const auto found = in_flight_.find(request.request_id);
    if (found == in_flight_.end()) {
        throw std::logic_error("finish action has no matching start: " + request.request_id);
    }
    const InFlight flight = found->second;
    const ServiceStatus status = flight.result.status;
    Event finished{format_event_id(history_.size() + 1), request.request_id, request.trace_id,
                   flight.service_name, event_type_for(status), flight.result.message};
    ExecutionRecord execution{scheduled.scheduled_time, request.request_id, request.trace_id,
                              flight.service_name, request.operation, flight.result,
                              flight.start_time};
    commit({std::move(finished)}, {scheduled.scheduled_time}, true, std::move(execution), true,
           status, scheduled.scheduled_time);
    in_flight_.erase(request.request_id);
    known_ids_.erase(request.request_id);
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
    }
    return EventType::RequestRejected;
}

SimulationClock::Time SimulationEngine::add_time(SimulationClock::Time start,
                                                SimulationClock::Time delta) {
    if (delta > std::numeric_limits<SimulationClock::Time>::max() - start) {
        throw std::overflow_error("simulation time overflow");
    }
    return start + delta;
}

}  // namespace incidentlab

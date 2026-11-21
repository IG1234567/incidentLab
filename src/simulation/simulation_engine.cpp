#include "incidentlab/simulation/simulation_engine.hpp"

#include "incidentlab/event.hpp"

#include <cstdint>
#include <iomanip>
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

void SimulationEngine::schedule_request(Request request, SimulationClock::Time scheduled_time) {
    scheduler_.schedule(std::move(request), scheduled_time, clock_.now());
}

std::size_t SimulationEngine::run_next(std::size_t max_requests) {
    std::size_t executed = 0;
    while (executed < max_requests && scheduler_.has_pending()) {
        // Leave the request queued until execute() has committed it. A throw from the
        // service then keeps the request, the clock, and the history as they were.
        execute(scheduler_.next());
        scheduler_.discard_next();
        ++executed;
    }
    return executed;
}

void SimulationEngine::run() { run_next(scheduler_.pending_count()); }

const SimulationClock& SimulationEngine::clock() const noexcept { return clock_; }

const EventHistory& SimulationEngine::history() const noexcept { return history_; }

SimulationReport SimulationEngine::report() const {
    SimulationReport report;
    report.current_time = clock_.now();
    report.processed_requests = executions_.size();
    report.pending_requests = scheduler_.pending_count();
    report.successful_requests = successful_requests_;
    report.rejected_requests = rejected_requests_;
    report.history = history_;
    report.event_times = event_times_;
    report.executions = executions_;
    return report;
}

void SimulationEngine::execute(const ScheduledEvent& scheduled) {
    if (scheduled.scheduled_time < clock_.now()) {
        throw std::invalid_argument(
            "cannot execute a request at " + std::to_string(scheduled.scheduled_time) +
            " ms before simulation time " + std::to_string(clock_.now()) + " ms");
    }

    // Copy before handle(). The service must not need the queued object to stay put,
    // and nothing below may run if copying or the service call throws.
    const SimulationClock::Time time = scheduled.scheduled_time;
    const Request request = scheduled.request;

    const auto found = services_.find(request.target_service);
    Service* const service = found == services_.end() ? nullptr : found->second;
    std::string service_name = request.target_service;
    if (service != nullptr) {
        service_name = service->name();
    }

    ServiceResult result{ServiceStatus::Rejected, "Unknown service: " + request.target_service};
    if (service != nullptr) {
        result = service->handle(request);
    }

    const bool succeeded = result.status == ServiceStatus::Success;
    const EventType outcome =
        succeeded ? EventType::RequestCompleted : EventType::RequestRejected;
    Event received{format_event_id(history_.size() + 1), request.request_id, request.trace_id,
                   service_name, EventType::RequestReceived, "Request received"};
    Event finished{format_event_id(history_.size() + 2), request.request_id, request.trace_id,
                   service_name, outcome, result.message};
    ExecutionRecord execution{time, request.request_id, request.trace_id, service_name,
                              request.operation, std::move(result)};

    // Room for the commit, so the appends below do not reallocate.
    event_times_.reserve(event_times_.size() + 2);
    executions_.reserve(executions_.size() + 1);
    history_.reserve(history_.size() + 2);

    const std::size_t events_before = history_.size();
    const std::size_t times_before = event_times_.size();
    const std::size_t executions_before = executions_.size();
    bool counted = false;
    try {
        record(std::move(received), time);
        record(std::move(finished), time);
        executions_.push_back(std::move(execution));
        if (succeeded) {
            ++successful_requests_;
        } else {
            ++rejected_requests_;
        }
        counted = true;
        // Checked above: this time is not earlier than now, so advance_to does not throw.
        clock_.advance_to(time);
    } catch (...) {
        if (counted) {
            if (succeeded) {
                --successful_requests_;
            } else {
                --rejected_requests_;
            }
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
        throw;
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

}  // namespace incidentlab

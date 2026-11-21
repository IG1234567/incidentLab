#include "incidentlab/simulation/simulation_engine.hpp"

#include "incidentlab/event.hpp"

#include <cstdint>
#include <iomanip>
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

void SimulationEngine::schedule_request(Request request, SimulationClock::Time scheduled_time) {
    scheduler_.schedule(std::move(request), scheduled_time, clock_.now());
}

std::size_t SimulationEngine::run_next(std::size_t max_requests) {
    std::size_t executed = 0;
    while (executed < max_requests) {
        std::optional<ScheduledEvent> next = scheduler_.pop_next();
        if (!next.has_value()) {
            break;
        }
        execute(*next);
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
    clock_.advance_to(scheduled.scheduled_time);

    const Request& request = scheduled.request;
    const auto found = services_.find(request.target_service);
    Service* const service = found == services_.end() ? nullptr : found->second;
    const std::string service_name = service == nullptr ? request.target_service : service->name();

    record(Event{format_event_id(history_.size() + 1), request.request_id, request.trace_id,
                 service_name, EventType::RequestReceived, "Request received"});

    ServiceResult result{ServiceStatus::Rejected, "Unknown service: " + request.target_service};
    if (service != nullptr) {
        result = service->handle(request);
    }

    const EventType outcome = result.status == ServiceStatus::Success ? EventType::RequestCompleted
                                                                      : EventType::RequestRejected;
    record(Event{format_event_id(history_.size() + 1), request.request_id, request.trace_id,
                 service_name, outcome, result.message});

    if (result.status == ServiceStatus::Success) {
        ++successful_requests_;
    } else {
        ++rejected_requests_;
    }
    executions_.push_back(ExecutionRecord{clock_.now(), request.request_id, request.trace_id,
                                          service_name, request.operation, result});
}

void SimulationEngine::record(Event event) {
    event_times_.push_back(clock_.now());
    history_.record(std::move(event));
}

}  // namespace incidentlab

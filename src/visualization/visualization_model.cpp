#include "incidentlab/visualization/visualization_model.hpp"

#include "incidentlab/observability/metrics_calculator.hpp"

#include <cmath>
#include <map>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace incidentlab {
namespace {

bool is_terminal(EventType type) noexcept {
    return type == EventType::RequestCompleted || type == EventType::RequestRejected ||
           type == EventType::RequestFailed || type == EventType::RequestTimedOut ||
           type == EventType::RequestDropped || type == EventType::CircuitRejected;
}

VisualServiceStatus status_from_terminal(EventType type) noexcept {
    switch (type) {
    case EventType::RequestCompleted:
        return VisualServiceStatus::Healthy;
    case EventType::RequestTimedOut:
        return VisualServiceStatus::TimedOut;
    case EventType::RequestDropped:
        return VisualServiceStatus::Dropped;
    case EventType::RequestFailed:
    case EventType::RequestRejected:
        return VisualServiceStatus::Failed;
    default:
        return VisualServiceStatus::Idle;
    }
}

const ServiceMetrics* find_metrics(const ObservabilityReport& report, const std::string& name) {
    for (const ServiceMetrics& service : report.services) {
        if (service.service_name == name) {
            return &service;
        }
    }
    return nullptr;
}

const CircuitMetrics* find_circuit(const ObservabilityReport& report, const std::string& name) {
    for (const CircuitMetrics& circuit : report.circuits) {
        if (circuit.service_name == name) {
            return &circuit;
        }
    }
    return nullptr;
}

}  // namespace

const char* to_string(PlaybackSpeed speed) noexcept {
    switch (speed) {
    case PlaybackSpeed::Half:
        return "0.5x";
    case PlaybackSpeed::Normal:
        return "1x";
    case PlaybackSpeed::Double:
        return "2x";
    case PlaybackSpeed::Quadruple:
        return "4x";
    }
    return "1x";
}

const char* to_string(PlaybackState state) noexcept {
    switch (state) {
    case PlaybackState::Ready:
        return "Ready";
    case PlaybackState::Running:
        return "Running";
    case PlaybackState::Paused:
        return "Paused";
    case PlaybackState::Completed:
        return "Completed";
    }
    return "Ready";
}

const char* to_string(VisualServiceStatus status) noexcept {
    switch (status) {
    case VisualServiceStatus::Idle:
        return "IDLE";
    case VisualServiceStatus::Healthy:
        return "HEALTHY";
    case VisualServiceStatus::Processing:
        return "PROCESSING";
    case VisualServiceStatus::Failed:
        return "FAILED";
    case VisualServiceStatus::TimedOut:
        return "TIMED OUT";
    case VisualServiceStatus::Dropped:
        return "DROPPED";
    case VisualServiceStatus::CircuitOpen:
        return "CIRCUIT OPEN";
    }
    return "IDLE";
}

std::string circuit_label(const std::optional<CircuitState>& state) {
    if (!state.has_value()) {
        return "NOT CONFIGURED";
    }
    if (*state == CircuitState::HalfOpen) {
        return "HALF-OPEN";
    }
    return to_string(*state);
}

double playback_interval_seconds(PlaybackSpeed speed) noexcept {
    switch (speed) {
    case PlaybackSpeed::Half:
        return 1.0;
    case PlaybackSpeed::Normal:
        return 0.5;
    case PlaybackSpeed::Double:
        return 0.25;
    case PlaybackSpeed::Quadruple:
        return 0.125;
    }
    return 0.5;
}

std::string format_optional_metric(const std::optional<double>& value) {
    if (!value.has_value() || !std::isfinite(*value)) {
        return "N/A";
    }
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(2);
    out << *value;
    return out.str();
}

VisualizationSnapshot build_visualization_snapshot(
    const SimulationReport& report, const std::vector<std::string>& service_names,
    const std::vector<ScheduledFlow>& flows, PlaybackState playback, PlaybackSpeed speed,
    const std::optional<std::string>& selected_service,
    const std::optional<std::string>& selected_event_id) {
    VisualizationSnapshot snapshot;
    snapshot.playback = playback;
    snapshot.speed = speed;
    snapshot.simulation_time_ms = report.current_time;
    snapshot.metrics = MetricsCalculator::calculate(report);
    snapshot.selected_service = selected_service;
    snapshot.selected_event_id = selected_event_id;

    std::map<std::string, ServiceNodeView> nodes;
    for (const std::string& name : service_names) {
        nodes[name].name = name;
    }
    for (const ServiceMetrics& service : snapshot.metrics.services) {
        nodes[service.service_name].name = service.service_name;
    }
    for (const CircuitStateSnapshot& circuit : report.circuit_states) {
        nodes[circuit.service_name].name = circuit.service_name;
        nodes[circuit.service_name].circuit = circuit.state;
    }

    std::unordered_map<std::string, std::unordered_map<std::string, bool>> in_flight;
    std::unordered_map<std::string, EventType> latest_terminal;
    const std::vector<Event>& events = report.history.events();
    for (const Event& event : events) {
        ServiceNodeView& node = nodes[event.service_name];
        node.name = event.service_name;
        auto& requests = in_flight[event.service_name];
        if (event.type == EventType::RequestReceived) {
            requests[event.request_id] = true;
        } else if (is_terminal(event.type)) {
            requests[event.request_id] = false;
            if (event.type != EventType::CircuitRejected) {
                latest_terminal[event.service_name] = event.type;
            }
        }
    }

    snapshot.services.reserve(nodes.size());
    for (auto& entry : nodes) {
        ServiceNodeView& node = entry.second;
        if (const ServiceMetrics* metrics = find_metrics(snapshot.metrics, node.name)) {
            node.completed_requests = metrics->outcomes.completed;
            node.successful_requests = metrics->outcomes.successful;
            node.failed_requests = metrics->outcomes.failed;
            node.timed_out_requests = metrics->outcomes.timed_out;
            node.dropped_requests = metrics->outcomes.dropped;
            node.circuit_rejected_requests = metrics->outcomes.circuit_rejected;
            node.success_rate_percent = metrics->success_rate_percent;
        }
        if (!node.circuit.has_value()) {
            if (const CircuitMetrics* circuit = find_circuit(snapshot.metrics, node.name)) {
                node.circuit = circuit->final_state;
            }
        }

        bool processing = false;
        const auto flying = in_flight.find(node.name);
        if (flying != in_flight.end()) {
            for (const auto& request : flying->second) {
                processing = processing || request.second;
            }
        }

        if (node.circuit.has_value() && *node.circuit == CircuitState::Open) {
            node.status = VisualServiceStatus::CircuitOpen;
        } else if (processing ||
                   (node.circuit.has_value() && *node.circuit == CircuitState::HalfOpen)) {
            node.status = VisualServiceStatus::Processing;
        } else {
            const auto terminal = latest_terminal.find(node.name);
            if (terminal == latest_terminal.end()) {
                node.status = VisualServiceStatus::Idle;
            } else {
                node.status = status_from_terminal(terminal->second);
            }
        }
        node.selected = selected_service.has_value() && *selected_service == node.name;
        snapshot.services.push_back(std::move(node));
    }

    struct EdgeKey {
        std::string source;
        std::string target;
        bool operator<(const EdgeKey& other) const {
            if (source != other.source) {
                return source < other.source;
            }
            return target < other.target;
        }
    };
    struct EdgeAccum {
        bool observed = false;
        std::string request_id;
        std::string trace_id;
        std::optional<EventType> type;
        std::size_t history_index = 0;
    };
    std::unordered_map<std::string, std::size_t> last_index;
    std::unordered_map<std::string, EventType> last_type;
    for (std::size_t index = 0; index < events.size(); ++index) {
        last_index[events[index].request_id] = index;
        last_type[events[index].request_id] = events[index].type;
    }
    std::map<EdgeKey, EdgeAccum> edges;
    for (const ScheduledFlow& flow : flows) {
        EdgeAccum& edge = edges[EdgeKey{flow.source_service, flow.target_service}];
        const auto found = last_index.find(flow.request_id);
        if (found == last_index.end()) {
            continue;
        }
        if (edge.observed && found->second < edge.history_index) {
            continue;
        }
        edge.observed = true;
        edge.request_id = flow.request_id;
        edge.trace_id = flow.trace_id;
        edge.type = last_type[flow.request_id];
        edge.history_index = found->second;
    }
    snapshot.flows.reserve(edges.size());
    for (const auto& edge : edges) {
        snapshot.flows.push_back(FlowEdge{edge.first.source, edge.first.target, edge.second.observed,
                                           edge.second.request_id, edge.second.trace_id,
                                           edge.second.type});
    }
    return snapshot;
}

std::vector<TimelineEntry> timeline_for_selected_service(const VisualizationSnapshot& snapshot) {
    if (!snapshot.selected_service.has_value()) {
        return snapshot.metrics.timeline;
    }
    return timeline_for_service(snapshot.metrics, *snapshot.selected_service);
}

std::optional<TimelineEntry> selected_timeline_entry(const VisualizationSnapshot& snapshot) {
    if (!snapshot.selected_event_id.has_value()) {
        return std::nullopt;
    }
    for (const TimelineEntry& entry : snapshot.metrics.timeline) {
        if (entry.event_id == *snapshot.selected_event_id) {
            return entry;
        }
    }
    return std::nullopt;
}

}  // namespace incidentlab

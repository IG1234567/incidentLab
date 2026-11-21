#pragma once

#include "incidentlab/observability/metrics.hpp"
#include "incidentlab/simulation/circuit_breaker.hpp"
#include "incidentlab/simulation/simulation_report.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace incidentlab {

/// How fast the window asks for the next scheduler action.
///
/// 0.5x waits 1000 ms, 1x waits 500 ms, 2x waits 250 ms, and 4x waits 125 ms of frame time.
/// The speed never changes virtual timestamps or outcomes.
enum class PlaybackSpeed { Half, Normal, Double, Quadruple };

enum class PlaybackState { Ready, Running, Paused, Completed };

/// What the service node should emphasize. TimedOut and Dropped stay distinct from Failed.
/// CircuitOpen is used only when a breaker snapshot says Open, never as a default.
enum class VisualServiceStatus { Idle, Healthy, Processing, Failed, TimedOut, Dropped, CircuitOpen };

/// One scheduled call. An edge drawn from it is an observed request, not a permanent dependency.
struct ScheduledFlow {
    std::string request_id;
    std::string trace_id;
    std::string source_service;
    std::string target_service;
};

struct FlowEdge {
    std::string source_service;
    std::string target_service;
    /// True after that request appears in the history. A scheduled edge is not a dependency.
    bool observed = false;
    std::string latest_request_id;
    std::string latest_trace_id;
    std::optional<EventType> latest_type;
};

struct ServiceNodeView {
    std::string name;
    VisualServiceStatus status = VisualServiceStatus::Idle;
    std::size_t completed_requests = 0;
    std::size_t successful_requests = 0;
    std::size_t failed_requests = 0;
    std::size_t timed_out_requests = 0;
    std::size_t dropped_requests = 0;
    std::size_t circuit_rejected_requests = 0;
    double success_rate_percent = 0.0;
    /// Empty when that service has no circuit breaker. That is not the same as Closed.
    std::optional<CircuitState> circuit;
    bool selected = false;
};

/// Display data owned by value. Safe after the engine that produced the report is gone.
struct VisualizationSnapshot {
    PlaybackState playback = PlaybackState::Ready;
    PlaybackSpeed speed = PlaybackSpeed::Normal;
    std::uint64_t simulation_time_ms = 0;
    std::vector<ServiceNodeView> services;
    std::vector<FlowEdge> flows;
    ObservabilityReport metrics;
    std::optional<std::string> selected_service;
    std::optional<std::string> selected_event_id;
};

const char* to_string(PlaybackSpeed speed) noexcept;
const char* to_string(PlaybackState state) noexcept;
const char* to_string(VisualServiceStatus status) noexcept;

/// "NOT CONFIGURED" when the service has no breaker. Never reports CLOSED for that case.
std::string circuit_label(const std::optional<CircuitState>& state);

/// Frame-time gap between automatic steps. Not a virtual-time scale.
double playback_interval_seconds(PlaybackSpeed speed) noexcept;

/// "N/A" when the metric has no value. Never "nan" or "inf".
std::string format_optional_metric(const std::optional<double>& value);

VisualizationSnapshot build_visualization_snapshot(
    const SimulationReport& report, const std::vector<std::string>& service_names,
    const std::vector<ScheduledFlow>& flows, PlaybackState playback, PlaybackSpeed speed,
    const std::optional<std::string>& selected_service,
    const std::optional<std::string>& selected_event_id);

/// Entries for the selected service, or every entry when nothing is selected.
/// The full timeline on the snapshot is left unchanged.
std::vector<TimelineEntry> timeline_for_selected_service(const VisualizationSnapshot& snapshot);

/// The selected event, copied out of the timeline. Empty when the id is unknown.
std::optional<TimelineEntry> selected_timeline_entry(const VisualizationSnapshot& snapshot);

}  // namespace incidentlab

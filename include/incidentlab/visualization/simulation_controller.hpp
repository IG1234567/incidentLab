#pragma once

#include "incidentlab/visualization/visualization_model.hpp"

#include <memory>
#include <string>

namespace incidentlab {

/// Owns one visualization scenario and the display snapshot.
///
/// Step runs one SimulationEngine::step_once() action. Start, pause, resume, and playback
/// speed do not schedule extra requests and do not change virtual time by themselves.
/// Reset rebuilds the same scenario. It does not clear private engine fields in place.
class SimulationController {
public:
    SimulationController();
    ~SimulationController();

    SimulationController(const SimulationController&) = delete;
    SimulationController& operator=(const SimulationController&) = delete;

    PlaybackState state() const noexcept;
    PlaybackSpeed speed() const noexcept;
    double playback_interval_seconds() const noexcept;

    void set_speed(PlaybackSpeed speed);

    /// Begins automatic playback. Does not schedule the scenario again.
    void start();
    void pause();
    void resume();

    /// One scheduler action. False when the scenario has no queued action.
    bool step();

    /// Rebuilds the scenario, the snapshot, and the selection.
    void reset();

    /// If playback is running, runs at most one step once enough frame time has accumulated.
    /// Frame time is not virtual time. Pause and Ready ignore it.
    bool tick(double frame_seconds);

    /// False when the name is not in the snapshot. The previous selection stays.
    bool select_service(const std::string& service_name);
    void clear_service_selection();

    /// False when the event id is not in the timeline.
    bool select_event(const std::string& event_id);
    void clear_event_selection();

    /// Valid until the next mutating call.
    const VisualizationSnapshot& snapshot() const noexcept;

private:
    struct Session;

    void rebuild_snapshot();
    void finish_if_idle();

    std::unique_ptr<Session> session_;
    VisualizationSnapshot snapshot_;
    PlaybackState state_ = PlaybackState::Ready;
    PlaybackSpeed speed_ = PlaybackSpeed::Normal;
    double accumulator_seconds_ = 0.0;
    std::optional<std::string> selected_service_;
    std::optional<std::string> selected_event_id_;
};

}  // namespace incidentlab

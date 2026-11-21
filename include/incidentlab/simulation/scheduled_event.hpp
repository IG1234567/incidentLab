#pragma once

#include "incidentlab/request.hpp"
#include "incidentlab/simulation/simulation_clock.hpp"

#include <cstdint>
#include <utility>

namespace incidentlab {

/// What a queued action will do when the engine reaches it.
///
/// Start delivers a request. Finish records an outcome whose virtual time is later than the start.
enum class ScheduledAction { Start, Finish };

/// Work that has not happened yet.
///
/// This is not a domain Event. An Event is recorded after the engine runs the action.
/// A Start and a later Finish for the same request share one queue, ordered by time and then
/// by sequence number.
struct ScheduledEvent {
    ScheduledEvent(SimulationClock::Time scheduled_time_in, std::uint64_t sequence_number_in,
                   Request request_in, ScheduledAction action_in = ScheduledAction::Start)
        : scheduled_time(scheduled_time_in),
          sequence_number(sequence_number_in),
          request(std::move(request_in)),
          action(action_in) {}

    SimulationClock::Time scheduled_time;
    std::uint64_t sequence_number;
    Request request;
    ScheduledAction action = ScheduledAction::Start;
};

}  // namespace incidentlab

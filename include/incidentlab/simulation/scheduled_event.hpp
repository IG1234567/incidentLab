#pragma once

#include "incidentlab/request.hpp"
#include "incidentlab/simulation/simulation_clock.hpp"

#include <cstdint>
#include <utility>

namespace incidentlab {

/// Work that has not happened yet.
///
/// This is not a domain Event. An Event is recorded after the engine runs the request.
struct ScheduledEvent {
    ScheduledEvent(SimulationClock::Time scheduled_time_in, std::uint64_t sequence_number_in,
                   Request request_in)
        : scheduled_time(scheduled_time_in),
          sequence_number(sequence_number_in),
          request(std::move(request_in)) {}

    SimulationClock::Time scheduled_time;
    std::uint64_t sequence_number;
    Request request;
};

}  // namespace incidentlab

#include "incidentlab/simulation/simulation_clock.hpp"

#include <stdexcept>
#include <string>

namespace incidentlab {

SimulationClock::Time SimulationClock::now() const noexcept { return now_; }

void SimulationClock::advance_to(Time new_time) {
    if (new_time < now_) {
        throw std::invalid_argument("simulation time cannot move backward from " +
                                    std::to_string(now_) + " ms to " + std::to_string(new_time) +
                                    " ms");
    }
    now_ = new_time;
}

}  // namespace incidentlab

#pragma once

#include <cstdint>
#include <stdexcept>

namespace incidentlab {

/// Virtual milliseconds for one simulation. This is not the system clock.
///
/// Time starts at 0. advance_to() moves it forward or leaves it unchanged.
/// A smaller value throws std::invalid_argument.
class SimulationClock {
public:
    using Time = std::uint64_t;

    Time now() const noexcept;
    void advance_to(Time new_time);

private:
    Time now_ = 0;
};

}  // namespace incidentlab

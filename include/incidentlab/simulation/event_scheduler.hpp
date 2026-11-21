#pragma once

#include "incidentlab/simulation/scheduled_event.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <queue>
#include <vector>

namespace incidentlab {

/// Pending requests ordered by virtual time, then by insertion sequence.
///
/// The scheduler does not own the clock. The caller passes the current time so a request
/// cannot be scheduled in the past. Scheduling is O(log N). Removing the next request is
/// O(log N). N is the number of pending requests.
class EventScheduler {
public:
    void schedule(Request request, SimulationClock::Time scheduled_time, SimulationClock::Time now);

    /// The earliest pending request, or nullopt when nothing is waiting.
    std::optional<ScheduledEvent> pop_next();

    /// The earliest pending request. It stays queued.
    /// Throws std::logic_error when the scheduler is empty.
    const ScheduledEvent& next() const;

    /// Removes the earliest pending request. Does nothing when the scheduler is empty.
    ///
    /// The engine calls this only after that request has been committed.
    void discard_next() noexcept;

    bool has_pending() const noexcept;
    std::size_t pending_count() const noexcept;

private:
    // priority_queue puts the greatest element on top. A later time, then a higher
    // sequence number, compares smaller so the earliest insertion comes out first.
    struct EarlierFirst {
        bool operator()(const ScheduledEvent& left, const ScheduledEvent& right) const {
            if (left.scheduled_time != right.scheduled_time) {
                return left.scheduled_time > right.scheduled_time;
            }
            return left.sequence_number > right.sequence_number;
        }
    };

    std::priority_queue<ScheduledEvent, std::vector<ScheduledEvent>, EarlierFirst> queue_;
    std::uint64_t next_sequence_ = 0;
};

}  // namespace incidentlab

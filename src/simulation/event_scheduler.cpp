#include "incidentlab/simulation/event_scheduler.hpp"

#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace incidentlab {

void EventScheduler::schedule(Request request, SimulationClock::Time scheduled_time,
                              SimulationClock::Time now, ScheduledAction action) {
    if (scheduled_time < now) {
        throw std::invalid_argument(
            "cannot schedule a request at " + std::to_string(scheduled_time) +
            " ms before simulation time " + std::to_string(now) + " ms");
    }
    if (next_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("EventScheduler sequence number overflow");
    }

    // Increment only after the push. A throw from the queue then leaves the sequence unused
    // and the scheduler unchanged.
    queue_.push(ScheduledEvent{scheduled_time, next_sequence_, std::move(request), action});
    ++next_sequence_;
}

std::optional<ScheduledEvent> EventScheduler::pop_next() {
    if (queue_.empty()) {
        return std::nullopt;
    }
    ScheduledEvent next = queue_.top();
    queue_.pop();
    return next;
}

const ScheduledEvent& EventScheduler::next() const {
    if (queue_.empty()) {
        throw std::logic_error("EventScheduler::next called on an empty scheduler");
    }
    return queue_.top();
}

void EventScheduler::discard_next() noexcept {
    if (!queue_.empty()) {
        queue_.pop();
    }
}

bool EventScheduler::has_pending() const noexcept { return !queue_.empty(); }

std::size_t EventScheduler::pending_count() const noexcept { return queue_.size(); }

}  // namespace incidentlab

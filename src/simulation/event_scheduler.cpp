#include "incidentlab/simulation/event_scheduler.hpp"

#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace incidentlab {

void EventScheduler::schedule(Request request, SimulationClock::Time scheduled_time,
                              SimulationClock::Time now) {
    if (scheduled_time < now) {
        throw std::invalid_argument(
            "cannot schedule a request at " + std::to_string(scheduled_time) +
            " ms before simulation time " + std::to_string(now) + " ms");
    }
    if (next_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("EventScheduler sequence number overflow");
    }

    const std::uint64_t sequence = next_sequence_;
    ++next_sequence_;
    queue_.push(ScheduledEvent{scheduled_time, sequence, std::move(request)});
}

std::optional<ScheduledEvent> EventScheduler::pop_next() {
    if (queue_.empty()) {
        return std::nullopt;
    }
    ScheduledEvent next = queue_.top();
    queue_.pop();
    return next;
}

bool EventScheduler::has_pending() const noexcept { return !queue_.empty(); }

std::size_t EventScheduler::pending_count() const noexcept { return queue_.size(); }

}  // namespace incidentlab

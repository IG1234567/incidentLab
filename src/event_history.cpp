#include "incidentlab/event_history.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace incidentlab {

void EventHistory::record(Event event) { events_.push_back(std::move(event)); }

void EventHistory::reserve(std::size_t capacity) { events_.reserve(capacity); }

void EventHistory::drop_last() noexcept {
    if (!events_.empty()) {
        events_.pop_back();
    }
}

const std::vector<Event>& EventHistory::events() const noexcept { return events_; }

std::size_t EventHistory::size() const noexcept { return events_.size(); }

std::vector<Event> EventHistory::by_trace_id(const std::string& trace_id) const {
    std::vector<Event> matches;
    std::copy_if(events_.begin(), events_.end(), std::back_inserter(matches),
                 [&](const Event& event) { return event.trace_id == trace_id; });
    return matches;
}

std::vector<Event> EventHistory::by_request_id(const std::string& request_id) const {
    std::vector<Event> matches;
    std::copy_if(events_.begin(), events_.end(), std::back_inserter(matches),
                 [&](const Event& event) { return event.request_id == request_id; });
    return matches;
}

}  // namespace incidentlab

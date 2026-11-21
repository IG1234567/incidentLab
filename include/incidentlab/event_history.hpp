#pragma once

#include "incidentlab/event.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace incidentlab {

/// Ordered record of events for one run.
///
/// The vector is private. Callers append through record() and read through the const views.
/// There is one history per workflow, not a process-wide log.
class EventHistory {
public:
    void record(Event event);

    /// Allocates room for at least `capacity` events. The history size does not change.
    void reserve(std::size_t capacity);

    /// Removes the newest event. Does nothing when the history is empty.
    ///
    /// The simulation engine uses this to undo a commit that could not finish.
    void drop_last() noexcept;

    const std::vector<Event>& events() const noexcept;
    std::size_t size() const noexcept;

    std::vector<Event> by_trace_id(const std::string& trace_id) const;
    std::vector<Event> by_request_id(const std::string& request_id) const;

private:
    std::vector<Event> events_;
};

}  // namespace incidentlab

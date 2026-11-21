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

    const std::vector<Event>& events() const noexcept;
    std::size_t size() const noexcept;

    std::vector<Event> by_trace_id(const std::string& trace_id) const;
    std::vector<Event> by_request_id(const std::string& request_id) const;

private:
    std::vector<Event> events_;
};

}  // namespace incidentlab

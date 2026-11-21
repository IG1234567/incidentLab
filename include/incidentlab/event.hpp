#pragma once

#include <stdexcept>
#include <string>

namespace incidentlab {

enum class EventType { RequestReceived, RequestCompleted, RequestRejected };

/// One recorded fact about a request.
///
/// Virtual time is not stored here. SimulationEngine keeps a timestamp beside each
/// recorded event so this type stays the same for Stage 2 callers.
/// Construction rejects an empty event id, request id, trace id, or service name.
struct Event {
    Event(std::string event_id, std::string request_id, std::string trace_id,
          std::string service_name, EventType type, std::string message);

    std::string event_id;
    std::string request_id;
    std::string trace_id;
    std::string service_name;
    EventType type = EventType::RequestReceived;
    std::string message;
};

inline const char* to_string(EventType type) noexcept {
    switch (type) {
    case EventType::RequestReceived:
        return "RequestReceived";
    case EventType::RequestCompleted:
        return "RequestCompleted";
    case EventType::RequestRejected:
        return "RequestRejected";
    }
    return "RequestRejected";
}

}  // namespace incidentlab

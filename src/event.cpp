#include "incidentlab/event.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace incidentlab {
namespace {

void require_non_empty(const std::string& value, const char* description) {
    if (value.empty()) {
        throw std::invalid_argument(std::string(description) + " must not be empty");
    }
}

}  // namespace

Event::Event(std::string event_id_in, std::string request_id_in, std::string trace_id_in,
             std::string service_name_in, EventType type_in, std::string message_in) {
    require_non_empty(event_id_in, "Event event_id");
    require_non_empty(request_id_in, "Event request_id");
    require_non_empty(trace_id_in, "Event trace_id");
    require_non_empty(service_name_in, "Event service_name");

    event_id = std::move(event_id_in);
    request_id = std::move(request_id_in);
    trace_id = std::move(trace_id_in);
    service_name = std::move(service_name_in);
    type = type_in;
    message = std::move(message_in);
}

}  // namespace incidentlab

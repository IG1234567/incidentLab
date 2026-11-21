#include "incidentlab/request.hpp"

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

Request::Request(std::string request_id_in, std::string trace_id_in, std::string source_service_in,
                 std::string target_service_in, std::string operation_in) {
    require_non_empty(request_id_in, "Request request_id");
    require_non_empty(trace_id_in, "Request trace_id");
    require_non_empty(target_service_in, "Request target_service");
    require_non_empty(operation_in, "Request operation");

    request_id = std::move(request_id_in);
    trace_id = std::move(trace_id_in);
    source_service = std::move(source_service_in);
    target_service = std::move(target_service_in);
    operation = std::move(operation_in);
}

}  // namespace incidentlab

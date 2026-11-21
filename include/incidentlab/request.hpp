#pragma once

#include <stdexcept>
#include <string>

namespace incidentlab {

/// One simulated call from a caller to a service.
///
/// Construction rejects an empty request id, trace id, target service, or operation.
/// An empty source service is allowed: the caller may be outside the model.
/// Changing the fields after construction is possible; callers treat a Request as a value
/// and do not rewrite its identifiers.
struct Request {
    Request(std::string request_id, std::string trace_id, std::string source_service,
            std::string target_service, std::string operation);

    std::string request_id;
    std::string trace_id;
    std::string source_service;
    std::string target_service;
    std::string operation;
};

}  // namespace incidentlab

#pragma once

#include "incidentlab/request.hpp"
#include "incidentlab/service_result.hpp"

#include <string>

namespace incidentlab {

/// Contract for a simulated service. Concrete services override both operations.
///
/// The interface stays small so a later simulation engine can call handle() without
/// knowing which service it holds. Services do not print and do not open sockets.
class Service {
public:
    virtual ~Service() = default;

    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;
    Service(Service&&) = delete;
    Service& operator=(Service&&) = delete;

    virtual std::string name() const = 0;
    virtual ServiceResult handle(const Request& request) = 0;

protected:
    Service() = default;
};

}  // namespace incidentlab

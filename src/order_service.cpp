#include "incidentlab/order_service.hpp"

namespace incidentlab {

std::string OrderService::name() const { return "OrderService"; }

ServiceResult OrderService::handle(const Request& request) {
    if (request.operation != "CreateOrder") {
        return ServiceResult{ServiceStatus::Rejected,
                             "Unsupported operation: " + request.operation};
    }
    return ServiceResult{ServiceStatus::Success, "Order created successfully"};
}

}  // namespace incidentlab

#include "incidentlab/payment_service.hpp"

namespace incidentlab {

std::string PaymentService::name() const { return "PaymentService"; }

ServiceResult PaymentService::handle(const Request& request) {
    if (request.operation != "ProcessPayment") {
        return ServiceResult{ServiceStatus::Rejected,
                             "Unsupported operation: " + request.operation};
    }
    return ServiceResult{ServiceStatus::Success, "Payment processed successfully"};
}

}  // namespace incidentlab

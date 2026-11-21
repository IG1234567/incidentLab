#include "incidentlab/inventory_service.hpp"

namespace incidentlab {

std::string InventoryService::name() const { return "InventoryService"; }

ServiceResult InventoryService::handle(const Request& request) {
    if (request.operation != "ReserveInventory") {
        return ServiceResult{ServiceStatus::Rejected,
                             "Unsupported operation: " + request.operation};
    }
    return ServiceResult{ServiceStatus::Success, "Inventory reserved successfully"};
}

}  // namespace incidentlab

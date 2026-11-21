#pragma once

#include "incidentlab/service.hpp"

namespace incidentlab {

/// Accepts the ReserveInventory operation.
class InventoryService final : public Service {
public:
    std::string name() const override;
    ServiceResult handle(const Request& request) override;
};

}  // namespace incidentlab

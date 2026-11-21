#include "incidentlab/simulation/stage3_demo.hpp"

#include "incidentlab/inventory_service.hpp"
#include "incidentlab/order_service.hpp"
#include "incidentlab/payment_service.hpp"
#include "incidentlab/simulation/simulation_engine.hpp"

namespace incidentlab {

SimulationReport run_stage3_demo() {
    OrderService orders;
    PaymentService payments;
    InventoryService inventory;

    SimulationEngine engine;
    engine.register_service(orders);
    engine.register_service(payments);
    engine.register_service(inventory);

    // Inserted out of time order. The scheduler, not this list, decides who runs first.
    engine.schedule_request(
        Request{"REQ-003", "TRACE-001", "client", "InventoryService", "ReserveInventory"}, 50);
    engine.schedule_request(
        Request{"REQ-001", "TRACE-001", "client", "OrderService", "CreateOrder"}, 10);
    engine.schedule_request(
        Request{"REQ-002", "TRACE-001", "client", "PaymentService", "ProcessPayment"}, 30);

    engine.run();
    return engine.report();
}

}  // namespace incidentlab

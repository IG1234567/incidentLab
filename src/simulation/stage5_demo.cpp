#include "incidentlab/simulation/stage5_demo.hpp"

#include "incidentlab/inventory_service.hpp"
#include "incidentlab/simulation/simulation_engine.hpp"

#include <cstddef>
#include <string>

namespace incidentlab {
namespace {

class FailFirstService : public Service {
public:
    FailFirstService(std::string name, std::size_t failures)
        : name_(std::move(name)), failures_(failures) {}

    std::string name() const override { return name_; }

    ServiceResult handle(const Request&) override {
        if (calls_ < failures_) {
            ++calls_;
            return ServiceResult{ServiceStatus::Failed, "Temporary failure"};
        }
        ++calls_;
        return ServiceResult{ServiceStatus::Success, "recovered"};
    }

private:
    std::string name_;
    std::size_t failures_ = 0;
    std::size_t calls_ = 0;
};

}  // namespace

Stage5Demo run_stage5_demo() {
    Stage5Demo demo;

    {
        FailFirstService payments{"PaymentService", 2};
        SimulationEngine engine;
        engine.register_service(payments);
        engine.configure_retry("PaymentService", RetryConfig{3, 100, 2.0});
        engine.schedule_request(
            Request{"REQ-PAY", "TRACE-RETRY", "client", "PaymentService", "ProcessPayment"}, 10);
        engine.run();
        demo.retry = engine.report();
    }

    {
        FailFirstService payments{"PaymentService", 3};
        SimulationEngine engine;
        engine.register_service(payments);
        engine.configure_circuit_breaker("PaymentService", CircuitBreakerConfig{3, 500});
        engine.schedule_request(
            Request{"REQ-C1", "TRACE-CIRCUIT", "client", "PaymentService", "ProcessPayment"}, 10);
        engine.schedule_request(
            Request{"REQ-C2", "TRACE-CIRCUIT", "client", "PaymentService", "ProcessPayment"}, 20);
        engine.schedule_request(
            Request{"REQ-C3", "TRACE-CIRCUIT", "client", "PaymentService", "ProcessPayment"}, 30);
        engine.schedule_request(
            Request{"REQ-C4", "TRACE-CIRCUIT", "client", "PaymentService", "ProcessPayment"}, 40);
        engine.schedule_request(
            Request{"REQ-C5", "TRACE-CIRCUIT", "client", "PaymentService", "ProcessPayment"}, 530);
        engine.run();
        demo.circuit_breaker = engine.report();
    }

    {
        InventoryService inventory;
        SimulationEngine engine;
        engine.register_service(inventory);
        engine.configure_failure(
            "InventoryService",
            FailureConfig{FailureType::ServiceFailure, 0, 0, 1});
        engine.configure_retry("InventoryService", RetryConfig{3, 100, 2.0});
        engine.schedule_request(
            Request{"REQ-INV", "TRACE-DLQ", "client", "InventoryService", "ReserveInventory"}, 10);
        engine.run();
        demo.dead_letter = engine.report();
    }

    return demo;
}

}  // namespace incidentlab

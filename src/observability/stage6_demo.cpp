#include "incidentlab/observability/stage6_demo.hpp"

#include "incidentlab/observability/metrics_calculator.hpp"
#include "incidentlab/order_service.hpp"
#include "incidentlab/simulation/simulation_engine.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace incidentlab {
namespace {

class StatusScript : public Service {
public:
    StatusScript(std::string name, std::vector<ServiceStatus> results)
        : name_(std::move(name)), results_(std::move(results)) {}

    std::string name() const override { return name_; }

    ServiceResult handle(const Request&) override {
        const ServiceStatus status = results_.at(calls_);
        ++calls_;
        if (status == ServiceStatus::Success) {
            return ServiceResult{status, "recovered"};
        }
        return ServiceResult{status, "Service failed"};
    }

private:
    std::string name_;
    std::vector<ServiceStatus> results_;
    std::size_t calls_ = 0;
};

Request make_request(const char* request_id, const char* service, const char* operation) {
    return Request{request_id, "TRACE-STAGE6", "client", service, operation};
}

}  // namespace

Stage6Demo run_stage6_demo() {
    OrderService orders;
    StatusScript payments{"PaymentService",
                          {ServiceStatus::Failed, ServiceStatus::Failed, ServiceStatus::Success}};
    StatusScript inventory{"InventoryService",
                           {ServiceStatus::Failed, ServiceStatus::Success, ServiceStatus::Failed,
                            ServiceStatus::Failed}};

    SimulationEngine engine;
    engine.register_service(orders);
    engine.register_service(payments);
    engine.register_service(inventory);
    engine.configure_circuit_breaker("PaymentService", CircuitBreakerConfig{2, 980});
    engine.configure_retry("InventoryService", RetryConfig{2, 100, 2.0});

    engine.schedule_request(make_request("REQ-O1", "OrderService", "CreateOrder"), 0);
    engine.schedule_request(make_request("REQ-O2", "OrderService", "CreateOrder"), 1);
    engine.schedule_request(make_request("REQ-P1", "PaymentService", "ProcessPayment"), 10);
    engine.schedule_request(make_request("REQ-P2", "PaymentService", "ProcessPayment"), 20);
    engine.schedule_request(make_request("REQ-P3", "PaymentService", "ProcessPayment"), 30);
    engine.schedule_request(make_request("REQ-I1", "InventoryService", "ReserveInventory"), 40);
    engine.schedule_request(make_request("REQ-I2", "InventoryService", "ReserveInventory"), 200);
    engine.schedule_request(make_request("REQ-P4", "PaymentService", "ProcessPayment"), 1000);
    engine.run();

    Stage6Demo demo;
    demo.simulation = engine.report();
    demo.observability = MetricsCalculator::calculate(demo.simulation);
    return demo;
}

}  // namespace incidentlab

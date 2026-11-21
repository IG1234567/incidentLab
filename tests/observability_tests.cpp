#define INCIDENTLAB_TEST_SUITE "observability"
#include "test_harness.hpp"

#include "incidentlab/inventory_service.hpp"
#include "incidentlab/observability/metrics_calculator.hpp"
#include "incidentlab/observability/stage6_demo.hpp"
#include "incidentlab/order_service.hpp"
#include "incidentlab/payment_service.hpp"
#include "incidentlab/simulation/simulation_engine.hpp"
#include "incidentlab/simulation/stage3_demo.hpp"
#include "incidentlab/simulation/stage4_demo.hpp"
#include "incidentlab/simulation/stage5_demo.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using incidentlab::FailureConfig;
using incidentlab::FailureType;
using incidentlab::MetricsCalculator;
using incidentlab::ObservabilityReport;
using incidentlab::ServiceStatus;

incidentlab::Request make_request(const std::string& request_id, const std::string& target,
                                  const std::string& operation) {
    return incidentlab::Request{request_id, "TRACE-001", "client", target, operation};
}

incidentlab::Request make_traced(const std::string& request_id, const std::string& trace,
                                 const std::string& target, const std::string& operation) {
    return incidentlab::Request{request_id, trace, "client", target, operation};
}

template <typename Exception, typename Function>
void expect_throws(Function&& function) {
    try {
        function();
    } catch (const Exception&) {
        return;
    } catch (const std::exception& ex) {
        throw incidentlab_test::Failure(std::string("unexpected exception: ") + ex.what());
    }
    throw incidentlab_test::Failure("expected exception was not thrown");
}

class NamedService : public incidentlab::Service {
public:
    explicit NamedService(std::string name) : name_(std::move(name)) {}

    std::string name() const override { return name_; }

    incidentlab::ServiceResult handle(const incidentlab::Request&) override {
        return incidentlab::ServiceResult{ServiceStatus::Success, "ok"};
    }

private:
    std::string name_;
};

class ScriptedService : public incidentlab::Service {
public:
    ScriptedService(std::string name, std::vector<ServiceStatus> results)
        : name_(std::move(name)), results_(std::move(results)) {}

    std::string name() const override { return name_; }

    incidentlab::ServiceResult handle(const incidentlab::Request&) override {
        const ServiceStatus status = results_.at(calls_);
        ++calls_;
        if (status == ServiceStatus::Success) {
            return incidentlab::ServiceResult{status, "recovered"};
        }
        return incidentlab::ServiceResult{status, "Service failed"};
    }

private:
    std::string name_;
    std::vector<ServiceStatus> results_;
    std::size_t calls_ = 0;
};

ObservabilityReport delays(const std::vector<std::uint64_t>& values) {
    std::vector<std::unique_ptr<NamedService>> services;
    incidentlab::SimulationEngine engine;
    for (std::size_t index = 0; index < values.size(); ++index) {
        const std::string name = "Latency-" + std::to_string(index);
        services.push_back(std::make_unique<NamedService>(name));
        engine.register_service(*services.back());
        engine.configure_failure(
            name, FailureConfig{FailureType::SlowResponse, values[index], 0, 1});
        engine.schedule_request(make_request("REQ-" + std::to_string(index), name, "Work"),
                                static_cast<incidentlab::SimulationClock::Time>(index * 1000));
    }
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    return MetricsCalculator::calculate(report);
}

const incidentlab::ServiceMetrics& find_service(const ObservabilityReport& report,
                                                const std::string& name) {
    for (const incidentlab::ServiceMetrics& service : report.services) {
        if (service.service_name == name) {
            return service;
        }
    }
    throw incidentlab_test::Failure("missing service " + name);
}

}  // namespace

INCIDENTLAB_TEST(empty_simulation_has_zero_rates) {
    incidentlab::SimulationEngine engine;
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(metrics.global.outcomes.completed == 0);
    EXPECT(metrics.global.success_rate_percent == 0.0);
    EXPECT(metrics.global.failure_rate_percent == 0.0);
    EXPECT(metrics.global.latency.average_ms == 0.0);
    EXPECT(metrics.services.empty());
    EXPECT(metrics.timeline.empty());
}

INCIDENTLAB_TEST(all_requests_successful) {
    incidentlab::OrderService orders;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    for (int index = 0; index < 4; ++index) {
        engine.schedule_request(make_request("REQ-" + std::to_string(index), "OrderService",
                                             "CreateOrder"),
                                static_cast<incidentlab::SimulationClock::Time>(index * 10));
    }
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(metrics.global.outcomes.completed == 4);
    EXPECT(metrics.global.outcomes.successful == 4);
    EXPECT(metrics.global.success_rate_percent == 100.0);
    EXPECT(metrics.global.failure_rate_percent == 0.0);
}

INCIDENTLAB_TEST(all_requests_failed) {
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_failure("PaymentService",
                             FailureConfig{FailureType::ServiceFailure, 0, 0, 1});
    for (int index = 0; index < 3; ++index) {
        engine.schedule_request(make_request("REQ-" + std::to_string(index), "PaymentService",
                                             "ProcessPayment"),
                                static_cast<incidentlab::SimulationClock::Time>(index * 10));
    }
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(metrics.global.outcomes.completed == 3);
    EXPECT(metrics.global.outcomes.failed == 3);
    EXPECT(metrics.global.outcomes.successful == 0);
    EXPECT(metrics.global.success_rate_percent == 0.0);
    EXPECT(metrics.global.failure_rate_percent == 100.0);
}

INCIDENTLAB_TEST(mixed_success_and_failure_rates) {
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_failure("PaymentService",
                             FailureConfig{FailureType::ServiceFailure, 0, 0, 2});
    for (int index = 1; index <= 4; ++index) {
        engine.schedule_request(make_request("REQ-" + std::to_string(index), "PaymentService",
                                             "ProcessPayment"),
                                static_cast<incidentlab::SimulationClock::Time>(index * 10));
    }
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(metrics.global.outcomes.completed == 4);
    EXPECT(metrics.global.outcomes.successful == 2);
    EXPECT(metrics.global.outcomes.failed == 2);
    EXPECT(metrics.global.success_rate_percent == 50.0);
    EXPECT(metrics.global.failure_rate_percent == 50.0);
}

INCIDENTLAB_TEST(retry_recovery_counts_as_one_success) {
    ScriptedService payments{"PaymentService", {ServiceStatus::Failed, ServiceStatus::Success}};
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_retry("PaymentService", incidentlab::RetryConfig{2, 100, 2.0});
    engine.schedule_request(make_request("REQ-1", "PaymentService", "ProcessPayment"), 10);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(metrics.global.outcomes.completed == 1);
    EXPECT(metrics.global.outcomes.successful == 1);
    EXPECT(metrics.global.outcomes.failed == 0);
    EXPECT(metrics.global.retries.total_attempts == 2);
    EXPECT(metrics.global.retries.retry_attempts == 1);
    EXPECT(metrics.global.success_rate_percent == 100.0);
}

INCIDENTLAB_TEST(pending_requests_are_excluded) {
    incidentlab::OrderService orders;
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.register_service(payments);
    engine.schedule_request(make_request("REQ-1", "OrderService", "CreateOrder"), 10);
    engine.schedule_request(make_request("REQ-2", "PaymentService", "ProcessPayment"), 50);
    EXPECT(engine.run_next(1) == 1);
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(report.pending_requests == 1);
    EXPECT(metrics.global.outcomes.completed == 1);
    EXPECT(metrics.global.outcomes.successful == 1);
}

INCIDENTLAB_TEST(in_progress_requests_are_excluded) {
    incidentlab::OrderService orders;
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.register_service(payments);
    engine.configure_failure("OrderService",
                             FailureConfig{FailureType::SlowResponse, 100, 0, 1});
    engine.schedule_request(make_request("REQ-SLOW", "OrderService", "CreateOrder"), 10);
    engine.schedule_request(make_request("REQ-FAST", "PaymentService", "ProcessPayment"), 30);
    EXPECT(engine.run_next(1) == 1);
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(report.in_progress_requests == 1);
    EXPECT(metrics.global.outcomes.completed == 1);
    EXPECT(metrics.global.outcomes.successful == 1);
    EXPECT(find_service(metrics, "PaymentService").outcomes.completed == 1);
    EXPECT(find_service(metrics, "OrderService").outcomes.completed == 0);
}

INCIDENTLAB_TEST(circuit_rejected_requests_are_classified) {
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_failure("PaymentService",
                             FailureConfig{FailureType::ServiceFailure, 0, 0, 1});
    engine.configure_circuit_breaker("PaymentService", incidentlab::CircuitBreakerConfig{1, 500});
    engine.schedule_request(make_request("REQ-FAIL", "PaymentService", "ProcessPayment"), 0);
    engine.schedule_request(make_request("REQ-BLOCK", "PaymentService", "ProcessPayment"), 10);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(metrics.global.outcomes.completed == 2);
    EXPECT(metrics.global.outcomes.failed == 1);
    EXPECT(metrics.global.outcomes.circuit_rejected == 1);
    EXPECT(metrics.global.outcomes.successful == 0);
    EXPECT(metrics.global.failure_rate_percent == 100.0);
}

INCIDENTLAB_TEST(zero_latency_is_zero) {
    incidentlab::OrderService orders;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.schedule_request(make_request("REQ-1", "OrderService", "CreateOrder"), 15);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(metrics.global.latency.min_ms == 0.0);
    EXPECT(metrics.global.latency.max_ms == 0.0);
    EXPECT(metrics.global.latency.average_ms == 0.0);
    EXPECT(metrics.global.latency.p50_ms == 0.0);
    EXPECT(metrics.global.latency.p99_ms == 0.0);
}

INCIDENTLAB_TEST(single_request_latency) {
    const ObservabilityReport metrics = delays({40});
    EXPECT(metrics.global.outcomes.completed == 1);
    EXPECT(metrics.global.latency.average_ms == 40.0);
    EXPECT(metrics.global.latency.min_ms == 40.0);
    EXPECT(metrics.global.latency.max_ms == 40.0);
    EXPECT(metrics.global.latency.p50_ms == 40.0);
    EXPECT(metrics.global.latency.p95_ms == 40.0);
    EXPECT(metrics.global.latency.p99_ms == 40.0);
}

INCIDENTLAB_TEST(average_latency) {
    const ObservabilityReport metrics = delays({10, 20, 30});
    EXPECT(metrics.global.latency.average_ms == 20.0);
}

INCIDENTLAB_TEST(minimum_latency) {
    const ObservabilityReport metrics = delays({10, 20, 30});
    EXPECT(metrics.global.latency.min_ms == 10.0);
}

INCIDENTLAB_TEST(maximum_latency) {
    const ObservabilityReport metrics = delays({10, 20, 30});
    EXPECT(metrics.global.latency.max_ms == 30.0);
}

INCIDENTLAB_TEST(p50_uses_nearest_rank) {
    const ObservabilityReport metrics = delays({10, 20, 30, 40, 50});
    EXPECT(metrics.global.latency.p50_ms == 30.0);
}

INCIDENTLAB_TEST(p95_uses_nearest_rank) {
    const ObservabilityReport metrics = delays({10, 20, 30, 40, 50});
    EXPECT(metrics.global.latency.p95_ms == 50.0);
}

INCIDENTLAB_TEST(identical_latencies_share_every_percentile) {
    const ObservabilityReport metrics = delays({15, 15, 15});
    EXPECT(metrics.global.latency.min_ms == 15.0);
    EXPECT(metrics.global.latency.max_ms == 15.0);
    EXPECT(metrics.global.latency.average_ms == 15.0);
    EXPECT(metrics.global.latency.p50_ms == 15.0);
    EXPECT(metrics.global.latency.p95_ms == 15.0);
    EXPECT(metrics.global.latency.p99_ms == 15.0);
}

INCIDENTLAB_TEST(p99_uses_nearest_rank) {
    const ObservabilityReport metrics = delays({10, 20, 30, 40, 50});
    EXPECT(metrics.global.latency.p99_ms == 50.0);
}

INCIDENTLAB_TEST(retry_backoff_is_included_in_latency) {
    ScriptedService payments{"PaymentService", {ServiceStatus::Failed, ServiceStatus::Success}};
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_retry("PaymentService", incidentlab::RetryConfig{2, 100, 2.0});
    engine.schedule_request(make_request("REQ-1", "PaymentService", "ProcessPayment"), 10);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(report.executions[0].start_time == 10);
    EXPECT(report.executions[0].simulation_time == 110);
    EXPECT(metrics.global.latency.average_ms == 100.0);
}

INCIDENTLAB_TEST(timeout_latency_uses_the_deadline) {
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_failure("PaymentService", FailureConfig{FailureType::Timeout, 80, 50, 1});
    engine.schedule_request(make_request("REQ-1", "PaymentService", "ProcessPayment"), 10);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(metrics.global.outcomes.timed_out == 1);
    EXPECT(metrics.global.latency.average_ms == 50.0);
    EXPECT(metrics.global.observation_end_ms == 60);
}

INCIDENTLAB_TEST(dropped_request_latency_is_zero) {
    incidentlab::InventoryService inventory;
    incidentlab::SimulationEngine engine;
    engine.register_service(inventory);
    engine.configure_failure("InventoryService",
                             FailureConfig{FailureType::DroppedRequest, 0, 0, 1});
    engine.schedule_request(make_request("REQ-1", "InventoryService", "ReserveInventory"), 25);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(metrics.global.outcomes.dropped == 1);
    EXPECT(metrics.global.latency.average_ms == 0.0);
    EXPECT(metrics.global.retries.total_attempts == 1);
}

INCIDENTLAB_TEST(overlapping_requests_keep_their_own_latency) {
    NamedService slow{"SlowService"};
    NamedService fast{"FastService"};
    incidentlab::SimulationEngine engine;
    engine.register_service(slow);
    engine.register_service(fast);
    engine.configure_failure("SlowService", FailureConfig{FailureType::SlowResponse, 100, 0, 1});
    engine.configure_failure("FastService", FailureConfig{FailureType::SlowResponse, 20, 0, 1});
    engine.schedule_request(make_request("REQ-SLOW", "SlowService", "Work"), 0);
    engine.schedule_request(make_request("REQ-FAST", "FastService", "Work"), 10);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(metrics.global.latency.min_ms == 20.0);
    EXPECT(metrics.global.latency.max_ms == 100.0);
    EXPECT(metrics.global.latency.average_ms == 60.0);
    EXPECT(find_service(metrics, "SlowService").latency.average_ms == 100.0);
    EXPECT(find_service(metrics, "FastService").latency.average_ms == 20.0);
}

INCIDENTLAB_TEST(large_virtual_timestamps) {
    const auto start = std::uint64_t{1} << 62;
    NamedService service{"ClockService"};
    incidentlab::SimulationEngine engine;
    engine.register_service(service);
    engine.configure_failure("ClockService", FailureConfig{FailureType::SlowResponse, 1000, 0, 1});
    engine.schedule_request(make_request("REQ-1", "ClockService", "Work"), start);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(metrics.global.observation_start_ms == start);
    EXPECT(metrics.global.latency.average_ms == 1000.0);
    EXPECT(metrics.global.latency.max_ms == 1000.0);
}

INCIDENTLAB_TEST(throughput_uses_the_observation_window) {
    incidentlab::OrderService orders;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    for (int index = 0; index < 9; ++index) {
        engine.schedule_request(make_request("REQ-" + std::to_string(index), "OrderService",
                                             "CreateOrder"),
                                100);
    }
    engine.schedule_request(make_request("REQ-LAST", "OrderService", "CreateOrder"), 2100);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(metrics.global.outcomes.completed == 10);
    EXPECT(metrics.global.observation_start_ms == 100);
    EXPECT(metrics.global.observation_end_ms == 2100);
    EXPECT(metrics.global.throughput_requests_per_second.has_value());
    EXPECT(*metrics.global.throughput_requests_per_second == 5.0);
}

INCIDENTLAB_TEST(zero_duration_throughput_is_unavailable) {
    incidentlab::OrderService orders;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.schedule_request(make_request("REQ-A", "OrderService", "CreateOrder"), 80);
    engine.schedule_request(make_request("REQ-B", "OrderService", "CreateOrder"), 80);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(metrics.global.outcomes.completed == 2);
    EXPECT(metrics.global.observation_start_ms == 80);
    EXPECT(metrics.global.observation_end_ms == 80);
    EXPECT(!metrics.global.throughput_requests_per_second.has_value());
}

INCIDENTLAB_TEST(empty_simulation_throughput_is_unavailable) {
    incidentlab::SimulationEngine engine;
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(metrics.global.outcomes.completed == 0);
    EXPECT(!metrics.global.observation_start_ms.has_value());
    EXPECT(!metrics.global.throughput_requests_per_second.has_value());
}

INCIDENTLAB_TEST(large_initial_timestamp_does_not_stretch_throughput) {
    const auto start = std::uint64_t{1} << 40;
    NamedService service{"ClockService"};
    incidentlab::SimulationEngine engine;
    engine.register_service(service);
    engine.configure_failure("ClockService", FailureConfig{FailureType::SlowResponse, 100, 0, 1});
    engine.schedule_request(make_request("REQ-1", "ClockService", "Work"), start);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(metrics.global.observation_start_ms == start);
    EXPECT(metrics.global.observation_end_ms == start + 100);
    EXPECT(*metrics.global.throughput_requests_per_second == 10.0);
}

INCIDENTLAB_TEST(pending_requests_are_excluded_from_throughput) {
    incidentlab::OrderService orders;
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.register_service(payments);
    engine.configure_failure("OrderService",
                             FailureConfig{FailureType::SlowResponse, 1000, 0, 1});
    engine.schedule_request(make_request("REQ-DONE", "OrderService", "CreateOrder"), 0);
    engine.schedule_request(make_request("REQ-LATER", "PaymentService", "ProcessPayment"), 10000);
    EXPECT(engine.run_next(1) == 1);
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(report.pending_requests == 1);
    EXPECT(metrics.global.outcomes.completed == 1);
    EXPECT(metrics.global.observation_end_ms == 1000);
    EXPECT(*metrics.global.throughput_requests_per_second == 1.0);
}

INCIDENTLAB_TEST(multiple_services_are_reported) {
    incidentlab::OrderService orders;
    incidentlab::PaymentService payments;
    incidentlab::InventoryService inventory;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.register_service(payments);
    engine.register_service(inventory);
    engine.schedule_request(make_request("REQ-O", "OrderService", "CreateOrder"), 10);
    engine.schedule_request(make_request("REQ-P", "PaymentService", "ProcessPayment"), 20);
    engine.schedule_request(make_request("REQ-I", "InventoryService", "ReserveInventory"), 30);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(metrics.services.size() == 3);
    EXPECT(metrics.global.outcomes.completed == 3);
}

INCIDENTLAB_TEST(per_service_statistics_are_independent) {
    incidentlab::OrderService orders;
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.register_service(payments);
    engine.configure_failure("PaymentService",
                             FailureConfig{FailureType::ServiceFailure, 0, 0, 1});
    engine.schedule_request(make_request("REQ-O1", "OrderService", "CreateOrder"), 10);
    engine.schedule_request(make_request("REQ-O2", "OrderService", "CreateOrder"), 20);
    engine.schedule_request(make_request("REQ-P1", "PaymentService", "ProcessPayment"), 30);
    engine.schedule_request(make_request("REQ-P2", "PaymentService", "ProcessPayment"), 40);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    const auto order = find_service(metrics, "OrderService");
    const auto payment = find_service(metrics, "PaymentService");
    EXPECT(order.outcomes.successful == 2);
    EXPECT(order.success_rate_percent == 100.0);
    EXPECT(order.failure_rate_percent == 0.0);
    EXPECT(payment.outcomes.failed == 2);
    EXPECT(payment.success_rate_percent == 0.0);
    EXPECT(payment.failure_rate_percent == 100.0);
    EXPECT(metrics.global.success_rate_percent == 50.0);
}

INCIDENTLAB_TEST(service_order_is_alphabetical) {
    incidentlab::OrderService orders;
    incidentlab::PaymentService payments;
    incidentlab::InventoryService inventory;
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.register_service(inventory);
    engine.register_service(orders);
    engine.schedule_request(make_request("REQ-P", "PaymentService", "ProcessPayment"), 30);
    engine.schedule_request(make_request("REQ-I", "InventoryService", "ReserveInventory"), 10);
    engine.schedule_request(make_request("REQ-O", "OrderService", "CreateOrder"), 20);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(metrics.services.size() == 3);
    EXPECT(metrics.services[0].service_name == "InventoryService");
    EXPECT(metrics.services[1].service_name == "OrderService");
    EXPECT(metrics.services[2].service_name == "PaymentService");
}

INCIDENTLAB_TEST(unknown_service_is_classified_by_name) {
    incidentlab::SimulationEngine engine;
    engine.schedule_request(make_request("REQ-1", "ShippingService", "ShipOrder"), 10);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(metrics.services.size() == 1);
    EXPECT(metrics.services[0].service_name == "ShippingService");
    EXPECT(metrics.services[0].outcomes.rejected == 1);
    EXPECT(metrics.services[0].outcomes.failed == 0);
    EXPECT(metrics.services[0].success_rate_percent == 0.0);
    EXPECT(metrics.services[0].failure_rate_percent == 100.0);
}

INCIDENTLAB_TEST(global_totals_equal_per_service_totals) {
    incidentlab::OrderService orders;
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.register_service(payments);
    engine.configure_failure("PaymentService",
                             FailureConfig{FailureType::ServiceFailure, 0, 0, 1});
    engine.schedule_request(make_request("REQ-O", "OrderService", "CreateOrder"), 10);
    engine.schedule_request(make_request("REQ-P", "PaymentService", "ProcessPayment"), 20);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    std::size_t completed = 0;
    std::size_t attempts = 0;
    std::size_t successful = 0;
    for (const incidentlab::ServiceMetrics& service : metrics.services) {
        completed += service.outcomes.completed;
        attempts += service.retries.total_attempts;
        successful += service.outcomes.successful;
    }
    EXPECT(completed == metrics.global.outcomes.completed);
    EXPECT(attempts == metrics.global.retries.total_attempts);
    EXPECT(successful == metrics.global.outcomes.successful);
    EXPECT(completed == 2);
    EXPECT(attempts == 2);
}

INCIDENTLAB_TEST(retry_attempt_counts) {
    ScriptedService payments{
        "PaymentService",
        {ServiceStatus::Failed, ServiceStatus::Failed, ServiceStatus::Success}};
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_retry("PaymentService", incidentlab::RetryConfig{3, 100, 2.0});
    engine.schedule_request(make_request("REQ-1", "PaymentService", "ProcessPayment"), 10);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(metrics.global.outcomes.completed == 1);
    EXPECT(metrics.global.retries.total_attempts == 3);
    EXPECT(metrics.global.retries.retry_attempts == 2);
    EXPECT(metrics.global.retries.requests_requiring_retry == 1);
    EXPECT(metrics.global.retries.recovered_after_retry == 1);
    EXPECT(metrics.global.retries.exhausted_retries == 0);
}

INCIDENTLAB_TEST(recovery_after_retry) {
    ScriptedService payments{"PaymentService", {ServiceStatus::Failed, ServiceStatus::Success}};
    incidentlab::OrderService orders;
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.register_service(orders);
    engine.configure_retry("PaymentService", incidentlab::RetryConfig{2, 50, 2.0});
    engine.schedule_request(make_request("REQ-PAY", "PaymentService", "ProcessPayment"), 10);
    engine.schedule_request(make_request("REQ-ORD", "OrderService", "CreateOrder"), 20);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(metrics.global.retries.recovered_after_retry == 1);
    EXPECT(metrics.global.retries.requests_requiring_retry == 1);
    EXPECT(find_service(metrics, "PaymentService").retries.recovered_after_retry == 1);
    EXPECT(find_service(metrics, "OrderService").retries.recovered_after_retry == 0);
    EXPECT(metrics.global.outcomes.successful == 2);
}

INCIDENTLAB_TEST(exhausted_retry_count) {
    incidentlab::InventoryService inventory;
    incidentlab::SimulationEngine engine;
    engine.register_service(inventory);
    engine.configure_failure("InventoryService",
                             FailureConfig{FailureType::ServiceFailure, 0, 0, 1});
    engine.configure_retry("InventoryService", incidentlab::RetryConfig{3, 10, 2.0});
    engine.schedule_request(make_request("REQ-1", "InventoryService", "ReserveInventory"), 0);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(metrics.global.retries.exhausted_retries == 1);
    EXPECT(metrics.global.retries.recovered_after_retry == 0);
    EXPECT(metrics.global.retries.total_attempts == 3);
    EXPECT(metrics.global.retries.retry_attempts == 2);
    EXPECT(metrics.global.dead_lettered_requests == 1);
    EXPECT(metrics.global.latency.average_ms == 30.0);
    EXPECT(metrics.global.outcomes.failed == 1);
}

INCIDENTLAB_TEST(circuit_opening_count) {
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_failure("PaymentService",
                             FailureConfig{FailureType::ServiceFailure, 0, 0, 1});
    engine.configure_circuit_breaker("PaymentService", incidentlab::CircuitBreakerConfig{1, 1000});
    engine.schedule_request(make_request("REQ-1", "PaymentService", "ProcessPayment"), 0);
    engine.schedule_request(make_request("REQ-2", "PaymentService", "ProcessPayment"), 10);
    engine.schedule_request(make_request("REQ-3", "PaymentService", "ProcessPayment"), 20);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(metrics.global.circuit_openings == 1);
    EXPECT(metrics.global.outcomes.circuit_rejected == 2);
    EXPECT(metrics.circuits.size() == 1);
    EXPECT(metrics.circuits[0].opened == 1);
    EXPECT(metrics.circuits[0].blocked_requests == 2);
    EXPECT(metrics.circuits[0].final_state == incidentlab::CircuitState::Open);
}

INCIDENTLAB_TEST(circuit_rejection_count) {
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_failure("PaymentService",
                             FailureConfig{FailureType::ServiceFailure, 0, 0, 1});
    engine.configure_circuit_breaker("PaymentService", incidentlab::CircuitBreakerConfig{1, 500});
    engine.schedule_request(make_request("REQ-FAIL", "PaymentService", "ProcessPayment"), 0);
    engine.schedule_request(make_request("REQ-BLOCK", "PaymentService", "ProcessPayment"), 40);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(metrics.global.outcomes.failed == 1);
    EXPECT(metrics.global.outcomes.circuit_rejected == 1);
    EXPECT(metrics.global.retries.exhausted_retries == 0);
    EXPECT(metrics.global.dead_lettered_requests == 2);
    EXPECT(metrics.circuits[0].blocked_requests == 1);
}

INCIDENTLAB_TEST(dead_letters_are_counted_per_service) {
    incidentlab::PaymentService payments;
    incidentlab::InventoryService inventory;
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.register_service(inventory);
    engine.configure_failure("PaymentService",
                             FailureConfig{FailureType::ServiceFailure, 0, 0, 1});
    engine.configure_failure("InventoryService",
                             FailureConfig{FailureType::ServiceFailure, 0, 0, 1});
    engine.configure_circuit_breaker("PaymentService", incidentlab::CircuitBreakerConfig{1, 500});
    engine.configure_retry("InventoryService", incidentlab::RetryConfig{2, 10, 2.0});
    engine.schedule_request(make_request("REQ-P", "PaymentService", "ProcessPayment"), 10);
    engine.schedule_request(make_request("REQ-I", "InventoryService", "ReserveInventory"), 20);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(metrics.global.dead_lettered_requests == 2);
    EXPECT(metrics.dead_letters.size() == 2);
    EXPECT(find_service(metrics, "PaymentService").dead_lettered_requests == 1);
    EXPECT(find_service(metrics, "InventoryService").dead_lettered_requests == 1);
    EXPECT(metrics.global.retries.exhausted_retries == 1);
    EXPECT(metrics.dead_letters[0].request_id == "REQ-P");
    EXPECT(metrics.dead_letters[0].trace_id == "TRACE-001");
    EXPECT(metrics.dead_letters[1].request_id == "REQ-I");
    EXPECT(metrics.dead_letters[1].attempts == 2);
}

INCIDENTLAB_TEST(timeline_preserves_chronological_order) {
    const incidentlab::SimulationReport report = incidentlab::run_stage3_demo();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(metrics.timeline.size() == report.history.size());
    EXPECT(metrics.timeline[0].time_ms == 10);
    EXPECT(metrics.timeline[0].request_id == "REQ-001");
    for (std::size_t index = 1; index < metrics.timeline.size(); ++index) {
        EXPECT(metrics.timeline[index].time_ms >= metrics.timeline[index - 1].time_ms);
        EXPECT(metrics.timeline[index].event_id == report.history.events()[index].event_id);
    }
}

INCIDENTLAB_TEST(timeline_preserves_equal_timestamp_order) {
    incidentlab::OrderService orders;
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.register_service(payments);
    engine.schedule_request(make_request("REQ-A", "OrderService", "CreateOrder"), 20);
    engine.schedule_request(make_request("REQ-B", "PaymentService", "ProcessPayment"), 20);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(metrics.timeline[0].time_ms == 20);
    EXPECT(metrics.timeline[1].time_ms == 20);
    EXPECT(metrics.timeline[0].request_id == "REQ-A");
    EXPECT(metrics.timeline[2].request_id == "REQ-B");
    EXPECT(metrics.timeline[0].event_id == report.history.events()[0].event_id);
}

INCIDENTLAB_TEST(timeline_filters_by_request_id) {
    const incidentlab::SimulationReport report = incidentlab::run_stage3_demo();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    const std::vector<incidentlab::TimelineEntry> selected =
        incidentlab::timeline_for_request(metrics, "REQ-002");
    EXPECT(selected.size() == 2);
    EXPECT(selected[0].request_id == "REQ-002");
    EXPECT(selected[1].request_id == "REQ-002");
    EXPECT(selected[0].type == incidentlab::EventType::RequestReceived);
    EXPECT(selected[1].type == incidentlab::EventType::RequestCompleted);
}

INCIDENTLAB_TEST(timeline_filters_by_trace_id) {
    incidentlab::OrderService orders;
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.register_service(payments);
    engine.schedule_request(make_traced("REQ-A", "TRACE-A", "OrderService", "CreateOrder"), 10);
    engine.schedule_request(
        make_traced("REQ-B", "TRACE-B", "PaymentService", "ProcessPayment"), 20);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    const std::vector<incidentlab::TimelineEntry> trace_a =
        incidentlab::timeline_for_trace(metrics, "TRACE-A");
    const std::vector<incidentlab::TimelineEntry> trace_b =
        incidentlab::timeline_for_trace(metrics, "TRACE-B");
    EXPECT(trace_a.size() == 2);
    EXPECT(trace_b.size() == 2);
    EXPECT(trace_a[0].request_id == "REQ-A");
    EXPECT(trace_b[0].request_id == "REQ-B");
    EXPECT(incidentlab::timeline_for_trace(metrics, "TRACE-MISSING").empty());
}

INCIDENTLAB_TEST(timeline_filters_by_service) {
    const incidentlab::SimulationReport report = incidentlab::run_stage3_demo();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    const std::vector<incidentlab::TimelineEntry> orders =
        incidentlab::timeline_for_service(metrics, "OrderService");
    EXPECT(orders.size() == 2);
    EXPECT(orders[0].service_name == "OrderService");
    EXPECT(orders[1].service_name == "OrderService");
    EXPECT(incidentlab::timeline_for_service(metrics, "Missing").empty());
}

INCIDENTLAB_TEST(misaligned_history_is_rejected) {
    const incidentlab::SimulationReport source = incidentlab::run_stage3_demo();
    incidentlab::SimulationReport broken = source;
    broken.event_times.pop_back();
    expect_throws<std::logic_error>([&broken] { MetricsCalculator::calculate(broken); });
}

INCIDENTLAB_TEST(completion_before_start_is_rejected) {
    incidentlab::SimulationReport report;
    report.processed_requests = 1;
    report.successful_requests = 1;
    incidentlab::ExecutionRecord execution;
    execution.simulation_time = 5;
    execution.start_time = 10;
    execution.request_id = "REQ-1";
    execution.service_name = "OrderService";
    execution.result = incidentlab::ServiceResult{ServiceStatus::Success, "ok"};
    report.executions.push_back(execution);
    expect_throws<std::logic_error>([&report] { MetricsCalculator::calculate(report); });
}

INCIDENTLAB_TEST(stage3_metrics_match_the_demo) {
    const incidentlab::SimulationReport report = incidentlab::run_stage3_demo();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(metrics.global.outcomes.completed == 3);
    EXPECT(metrics.global.outcomes.successful == 3);
    EXPECT(metrics.global.success_rate_percent == 100.0);
    EXPECT(metrics.global.failure_rate_percent == 0.0);
    EXPECT(metrics.global.retries.total_attempts == 3);
    EXPECT(metrics.global.retries.retry_attempts == 0);
    EXPECT(metrics.global.latency.average_ms == 0.0);
    EXPECT(metrics.global.observation_start_ms == 10);
    EXPECT(metrics.global.observation_end_ms == 50);
    EXPECT(*metrics.global.throughput_requests_per_second == 75.0);
}

INCIDENTLAB_TEST(stage4_metrics_match_the_demo) {
    const incidentlab::SimulationReport report = incidentlab::run_stage4_demo();
    const ObservabilityReport metrics = MetricsCalculator::calculate(report);
    EXPECT(metrics.global.outcomes.completed == 3);
    EXPECT(metrics.global.outcomes.successful == 2);
    EXPECT(metrics.global.outcomes.timed_out == 1);
    EXPECT(metrics.global.success_rate_percent == 200.0 / 3.0);
    EXPECT(metrics.global.failure_rate_percent == 100.0 / 3.0);
    EXPECT(metrics.global.latency.average_ms == 100.0 / 3.0);
    EXPECT(metrics.global.latency.p50_ms == 0.0);
    EXPECT(metrics.global.latency.p95_ms == 100.0);
    EXPECT(metrics.global.latency.p99_ms == 100.0);
    EXPECT(metrics.global.observation_start_ms == 10);
    EXPECT(metrics.global.observation_end_ms == 130);
    EXPECT(*metrics.global.throughput_requests_per_second == 25.0);
    EXPECT(metrics.global.dead_lettered_requests == 0);
    EXPECT(metrics.global.circuit_openings == 0);
}

INCIDENTLAB_TEST(stage5_metrics_match_the_demo) {
    const incidentlab::Stage5Demo demo = incidentlab::run_stage5_demo();
    const ObservabilityReport retry = MetricsCalculator::calculate(demo.retry);
    EXPECT(retry.global.outcomes.successful == 1);
    EXPECT(retry.global.latency.average_ms == 300.0);
    EXPECT(retry.global.retries.total_attempts == 3);
    EXPECT(retry.global.retries.retry_attempts == 2);
    EXPECT(retry.global.retries.requests_requiring_retry == 1);
    EXPECT(retry.global.retries.recovered_after_retry == 1);
    EXPECT(retry.global.retries.exhausted_retries == 0);
    EXPECT(*retry.global.throughput_requests_per_second == 1000.0 / 300.0);

    const ObservabilityReport circuit = MetricsCalculator::calculate(demo.circuit_breaker);
    EXPECT(circuit.global.outcomes.completed == 5);
    EXPECT(circuit.global.outcomes.successful == 1);
    EXPECT(circuit.global.outcomes.failed == 3);
    EXPECT(circuit.global.outcomes.circuit_rejected == 1);
    EXPECT(circuit.global.success_rate_percent == 20.0);
    EXPECT(circuit.global.failure_rate_percent == 80.0);
    EXPECT(circuit.global.circuit_openings == 1);
    EXPECT(circuit.global.circuit_half_openings == 1);
    EXPECT(circuit.global.circuit_closings == 1);
    EXPECT(circuit.global.dead_lettered_requests == 4);
    EXPECT(circuit.global.retries.exhausted_retries == 0);
    EXPECT(circuit.circuits[0].final_state == incidentlab::CircuitState::Closed);

    const ObservabilityReport dead = MetricsCalculator::calculate(demo.dead_letter);
    EXPECT(dead.global.outcomes.failed == 1);
    EXPECT(dead.global.retries.exhausted_retries == 1);
    EXPECT(dead.global.retries.total_attempts == 3);
    EXPECT(dead.global.retries.retry_attempts == 2);
    EXPECT(dead.global.dead_lettered_requests == 1);
    EXPECT(dead.global.latency.average_ms == 300.0);
    EXPECT(dead.dead_letters[0].request_id == "REQ-INV");
    EXPECT(dead.dead_letters[0].trace_id == "TRACE-DLQ");
}

INCIDENTLAB_TEST(repeated_calculation_is_deterministic) {
    const incidentlab::SimulationReport report = incidentlab::run_stage4_demo();
    const ObservabilityReport first = MetricsCalculator::calculate(report);
    const ObservabilityReport second = MetricsCalculator::calculate(report);
    EXPECT(first.global.success_rate_percent == second.global.success_rate_percent);
    EXPECT(first.global.latency.p95_ms == second.global.latency.p95_ms);
    EXPECT(first.timeline.size() == second.timeline.size());
    EXPECT(first.timeline[0].event_id == second.timeline[0].event_id);
    EXPECT(first.services.size() == second.services.size());
    EXPECT(first.services[0].service_name == second.services[0].service_name);
    EXPECT(*first.global.throughput_requests_per_second ==
           *second.global.throughput_requests_per_second);
}

INCIDENTLAB_TEST(metrics_remain_valid_after_the_engine_is_destroyed) {
    ObservabilityReport metrics;
    {
        incidentlab::OrderService orders;
        incidentlab::SimulationEngine engine;
        engine.register_service(orders);
        engine.schedule_request(make_request("REQ-1", "OrderService", "CreateOrder"), 10);
        engine.run();
        const incidentlab::SimulationReport report = engine.report();
        metrics = MetricsCalculator::calculate(report);
    }
    EXPECT(metrics.global.outcomes.completed == 1);
    EXPECT(metrics.timeline.size() == 2);
    EXPECT(metrics.timeline[0].request_id == "REQ-1");
    EXPECT(metrics.services[0].service_name == "OrderService");
}

INCIDENTLAB_TEST(calculation_does_not_change_the_report) {
    incidentlab::OrderService orders;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.schedule_request(make_request("REQ-1", "OrderService", "CreateOrder"), 10);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    incidentlab::SimulationReport copy = report;
    const std::string event_id = copy.history.events()[0].event_id;
    const std::size_t history_size = copy.history.size();
    const std::size_t successful = copy.successful_requests;
    const auto first_time = copy.event_times[0];
    const ObservabilityReport metrics = MetricsCalculator::calculate(copy);
    EXPECT(copy.history.events()[0].event_id == event_id);
    EXPECT(copy.history.size() == history_size);
    EXPECT(copy.successful_requests == successful);
    EXPECT(copy.event_times[0] == first_time);
    EXPECT(copy.executions[0].request_id == "REQ-1");
    EXPECT(metrics.global.outcomes.completed == 1);
}

INCIDENTLAB_TEST(stage6_demo_metrics_are_calculated) {
    const incidentlab::Stage6Demo demo = incidentlab::run_stage6_demo();
    const ObservabilityReport& metrics = demo.observability;
    EXPECT(metrics.global.outcomes.completed == 8);
    EXPECT(metrics.global.outcomes.successful == 4);
    EXPECT(metrics.global.outcomes.failed == 3);
    EXPECT(metrics.global.outcomes.circuit_rejected == 1);
    EXPECT(metrics.global.success_rate_percent == 50.0);
    EXPECT(metrics.global.failure_rate_percent == 50.0);
    EXPECT(metrics.global.retries.total_attempts == 9);
    EXPECT(metrics.global.retries.retry_attempts == 2);
    EXPECT(metrics.global.retries.requests_requiring_retry == 2);
    EXPECT(metrics.global.retries.recovered_after_retry == 1);
    EXPECT(metrics.global.retries.exhausted_retries == 1);
    EXPECT(metrics.global.latency.average_ms == 25.0);
    EXPECT(metrics.global.latency.p50_ms == 0.0);
    EXPECT(metrics.global.latency.p95_ms == 100.0);
    EXPECT(metrics.global.latency.p99_ms == 100.0);
    EXPECT(metrics.global.observation_start_ms == 0);
    EXPECT(metrics.global.observation_end_ms == 1000);
    EXPECT(*metrics.global.throughput_requests_per_second == 8.0);
    EXPECT(metrics.global.circuit_openings == 1);
    EXPECT(metrics.global.circuit_half_openings == 1);
    EXPECT(metrics.global.circuit_closings == 1);
    EXPECT(metrics.global.dead_lettered_requests == 4);
    EXPECT(metrics.services[0].service_name == "InventoryService");
    EXPECT(metrics.services[1].service_name == "OrderService");
    EXPECT(metrics.services[2].service_name == "PaymentService");
    EXPECT(find_service(metrics, "InventoryService").latency.average_ms == 100.0);
    EXPECT(find_service(metrics, "OrderService").success_rate_percent == 100.0);
    EXPECT(find_service(metrics, "PaymentService").success_rate_percent == 25.0);
    EXPECT(find_service(metrics, "PaymentService").failure_rate_percent == 75.0);
    EXPECT(metrics.circuits[0].final_state == incidentlab::CircuitState::Closed);
}

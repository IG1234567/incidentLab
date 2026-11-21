#include "test_harness.hpp"

#include "incidentlab/inventory_service.hpp"
#include "incidentlab/order_service.hpp"
#include "incidentlab/payment_service.hpp"
#include "incidentlab/simulation/event_scheduler.hpp"
#include "incidentlab/simulation/simulation_clock.hpp"
#include "incidentlab/simulation/simulation_engine.hpp"
#include "incidentlab/simulation/stage3_demo.hpp"

#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

template <typename Function>
void expect_invalid_argument(Function&& function) {
    try {
        function();
    } catch (const std::invalid_argument&) {
        return;
    } catch (const std::exception& ex) {
        throw incidentlab_test::Failure(std::string("unexpected exception: ") + ex.what());
    }
    throw incidentlab_test::Failure("expected std::invalid_argument");
}

incidentlab::Request make_request(const std::string& request_id, const std::string& target,
                                  const std::string& operation) {
    return incidentlab::Request{request_id, "TRACE-001", "client", target, operation};
}

void register_all(incidentlab::SimulationEngine& engine, incidentlab::OrderService& orders,
                  incidentlab::PaymentService& payments, incidentlab::InventoryService& inventory) {
    engine.register_service(orders);
    engine.register_service(payments);
    engine.register_service(inventory);
}

}  // namespace

INCIDENTLAB_TEST(simulation_clock_starts_at_zero) {
    const incidentlab::SimulationClock clock;
    EXPECT(clock.now() == 0);
}

INCIDENTLAB_TEST(simulation_clock_advances) {
    incidentlab::SimulationClock clock;
    clock.advance_to(10);
    EXPECT(clock.now() == 10);
    clock.advance_to(50);
    EXPECT(clock.now() == 50);
}

INCIDENTLAB_TEST(simulation_clock_rejects_backward_time) {
    incidentlab::SimulationClock clock;
    clock.advance_to(30);
    expect_invalid_argument([&clock] { clock.advance_to(29); });
    EXPECT(clock.now() == 30);
}

INCIDENTLAB_TEST(simulation_clock_allows_advancing_to_the_same_time) {
    incidentlab::SimulationClock clock;
    clock.advance_to(15);
    clock.advance_to(15);
    EXPECT(clock.now() == 15);
}

INCIDENTLAB_TEST(scheduler_executes_events_chronologically) {
    incidentlab::EventScheduler scheduler;
    scheduler.schedule(make_request("REQ-A", "OrderService", "CreateOrder"), 50, 0);
    scheduler.schedule(make_request("REQ-B", "PaymentService", "ProcessPayment"), 10, 0);
    scheduler.schedule(make_request("REQ-C", "InventoryService", "ReserveInventory"), 30, 0);

    const auto first = scheduler.pop_next();
    const auto second = scheduler.pop_next();
    const auto third = scheduler.pop_next();
    EXPECT(first.has_value() && first->request.request_id == "REQ-B");
    EXPECT(second.has_value() && second->request.request_id == "REQ-C");
    EXPECT(third.has_value() && third->request.request_id == "REQ-A");
    EXPECT(first->scheduled_time == 10);
    EXPECT(second->scheduled_time == 30);
    EXPECT(third->scheduled_time == 50);
}

INCIDENTLAB_TEST(scheduler_preserves_insertion_order_for_equal_times) {
    incidentlab::EventScheduler scheduler;
    scheduler.schedule(make_request("REQ-A", "OrderService", "CreateOrder"), 20, 0);
    scheduler.schedule(make_request("REQ-B", "PaymentService", "ProcessPayment"), 20, 0);
    scheduler.schedule(make_request("REQ-C", "InventoryService", "ReserveInventory"), 20, 0);

    EXPECT(scheduler.pop_next()->request.request_id == "REQ-A");
    EXPECT(scheduler.pop_next()->request.request_id == "REQ-B");
    EXPECT(scheduler.pop_next()->request.request_id == "REQ-C");
}

INCIDENTLAB_TEST(scheduler_empty_pop_returns_nullopt) {
    incidentlab::EventScheduler scheduler;
    EXPECT(!scheduler.has_pending());
    EXPECT(scheduler.pending_count() == 0);
    EXPECT(!scheduler.pop_next().has_value());
}

INCIDENTLAB_TEST(scheduler_rejects_scheduling_in_the_past) {
    incidentlab::EventScheduler scheduler;
    expect_invalid_argument([&scheduler] {
        scheduler.schedule(make_request("REQ-001", "OrderService", "CreateOrder"), 5, 10);
    });
    EXPECT(scheduler.pending_count() == 0);
}

INCIDENTLAB_TEST(scheduler_pending_count_matches_scheduled_events) {
    incidentlab::EventScheduler scheduler;
    EXPECT(scheduler.pending_count() == 0);
    scheduler.schedule(make_request("REQ-001", "OrderService", "CreateOrder"), 10, 0);
    scheduler.schedule(make_request("REQ-002", "PaymentService", "ProcessPayment"), 0, 0);
    EXPECT(scheduler.pending_count() == 2);
    EXPECT(scheduler.has_pending());
    scheduler.pop_next();
    EXPECT(scheduler.pending_count() == 1);
}

INCIDENTLAB_TEST(scheduler_allows_scheduling_at_current_time) {
    incidentlab::EventScheduler scheduler;
    scheduler.schedule(make_request("REQ-001", "OrderService", "CreateOrder"), 10, 10);
    EXPECT(scheduler.pending_count() == 1);
    EXPECT(scheduler.pop_next()->scheduled_time == 10);
}

INCIDENTLAB_TEST(engine_registers_services) {
    incidentlab::OrderService orders;
    incidentlab::PaymentService payments;
    incidentlab::InventoryService inventory;
    incidentlab::SimulationEngine engine;
    register_all(engine, orders, payments, inventory);

    engine.schedule_request(make_request("REQ-001", "PaymentService", "ProcessPayment"), 5);
    EXPECT(engine.run_next(1) == 1);
    EXPECT(engine.report().executions[0].service_name == "PaymentService");
    EXPECT(engine.report().executions[0].result.message == "Payment processed successfully");
}

INCIDENTLAB_TEST(engine_rejects_duplicate_service_registration) {
    incidentlab::OrderService first;
    incidentlab::OrderService second;
    incidentlab::SimulationEngine engine;
    engine.register_service(first);
    expect_invalid_argument([&engine, &second] { engine.register_service(second); });

    engine.schedule_request(make_request("REQ-001", "OrderService", "CreateOrder"), 1);
    engine.run();
    EXPECT(engine.report().successful_requests == 1);
}

INCIDENTLAB_TEST(engine_routes_request_to_target_service) {
    incidentlab::OrderService orders;
    incidentlab::PaymentService payments;
    incidentlab::InventoryService inventory;
    incidentlab::SimulationEngine engine;
    register_all(engine, orders, payments, inventory);

    engine.schedule_request(make_request("REQ-003", "InventoryService", "ReserveInventory"), 50);
    engine.schedule_request(make_request("REQ-001", "OrderService", "CreateOrder"), 10);
    engine.schedule_request(make_request("REQ-002", "PaymentService", "ProcessPayment"), 30);
    engine.run();

    const incidentlab::SimulationReport report = engine.report();
    EXPECT(report.executions.size() == 3);
    EXPECT(report.executions[0].request_id == "REQ-001");
    EXPECT(report.executions[0].result.message == "Order created successfully");
    EXPECT(report.executions[1].request_id == "REQ-002");
    EXPECT(report.executions[1].result.message == "Payment processed successfully");
    EXPECT(report.executions[2].request_id == "REQ-003");
    EXPECT(report.executions[2].result.message == "Inventory reserved successfully");
}

INCIDENTLAB_TEST(engine_rejects_unknown_service) {
    incidentlab::OrderService orders;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.schedule_request(make_request("REQ-009", "ShippingService", "ShipOrder"), 7);
    engine.run();

    const incidentlab::SimulationReport report = engine.report();
    EXPECT(report.rejected_requests == 1);
    EXPECT(report.successful_requests == 0);
    EXPECT(report.executions[0].result.status == incidentlab::ServiceStatus::Rejected);
    EXPECT(report.executions[0].result.message == "Unknown service: ShippingService");
    EXPECT(report.history.size() == 2);
    EXPECT(report.history.events()[1].type == incidentlab::EventType::RequestRejected);
    EXPECT(report.history.events()[1].service_name == "ShippingService");
}

INCIDENTLAB_TEST(engine_records_two_events_for_a_successful_request) {
    incidentlab::OrderService orders;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.schedule_request(make_request("REQ-001", "OrderService", "CreateOrder"), 10);
    engine.run();

    const auto& events = engine.history().events();
    EXPECT(events.size() == 2);
    EXPECT(events[0].type == incidentlab::EventType::RequestReceived);
    EXPECT(events[0].message == "Request received");
    EXPECT(events[1].type == incidentlab::EventType::RequestCompleted);
    EXPECT(events[1].message == "Order created successfully");
}

INCIDENTLAB_TEST(engine_records_rejection_for_unsupported_operation) {
    incidentlab::OrderService orders;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.schedule_request(make_request("REQ-004", "OrderService", "ShipOrder"), 4);
    engine.run();

    const auto& events = engine.history().events();
    EXPECT(events.size() == 2);
    EXPECT(events[0].type == incidentlab::EventType::RequestReceived);
    EXPECT(events[1].type == incidentlab::EventType::RequestRejected);
    EXPECT(events[1].message == "Unsupported operation: ShipOrder");
    EXPECT(engine.report().rejected_requests == 1);
}

INCIDENTLAB_TEST(engine_preserves_request_id) {
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.schedule_request(make_request("REQ-002", "PaymentService", "ProcessPayment"), 3);
    engine.run();

    EXPECT(engine.history().by_request_id("REQ-002").size() == 2);
    EXPECT(engine.report().executions[0].request_id == "REQ-002");
}

INCIDENTLAB_TEST(engine_preserves_trace_id) {
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    const incidentlab::Request request{"REQ-002", "TRACE-777", "client", "PaymentService",
                                       "ProcessPayment"};
    engine.schedule_request(request, 3);
    engine.run();

    EXPECT(engine.history().by_trace_id("TRACE-777").size() == 2);
    for (const incidentlab::Event& event : engine.history().events()) {
        EXPECT(event.trace_id == "TRACE-777");
    }
}

INCIDENTLAB_TEST(engine_records_virtual_timestamps) {
    incidentlab::OrderService orders;
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.register_service(payments);
    engine.schedule_request(make_request("REQ-002", "PaymentService", "ProcessPayment"), 30);
    engine.schedule_request(make_request("REQ-001", "OrderService", "CreateOrder"), 10);
    engine.run();

    const incidentlab::SimulationReport report = engine.report();
    EXPECT(report.event_times.size() == report.history.size());
    EXPECT(report.event_times.size() == 4);
    EXPECT(report.event_times[0] == 10);
    EXPECT(report.event_times[1] == 10);
    EXPECT(report.event_times[2] == 30);
    EXPECT(report.event_times[3] == 30);
    EXPECT(report.executions[0].simulation_time == 10);
    EXPECT(report.executions[1].simulation_time == 30);
    EXPECT(report.current_time == 30);
}

INCIDENTLAB_TEST(engine_event_ids_are_unique_and_deterministic) {
    auto run_once = [] {
        incidentlab::OrderService orders;
        incidentlab::SimulationEngine engine;
        engine.register_service(orders);
        engine.schedule_request(make_request("REQ-001", "OrderService", "CreateOrder"), 10);
        engine.schedule_request(make_request("REQ-002", "OrderService", "CreateOrder"), 20);
        engine.run();
        return engine.report();
    };

    const incidentlab::SimulationReport first = run_once();
    const incidentlab::SimulationReport second = run_once();
    std::set<std::string> ids;
    EXPECT(first.history.size() == second.history.size());
    for (std::size_t index = 0; index < first.history.size(); ++index) {
        EXPECT(first.history.events()[index].event_id == second.history.events()[index].event_id);
        ids.insert(first.history.events()[index].event_id);
    }
    EXPECT(ids.size() == first.history.size());
    EXPECT(first.history.events()[0].event_id == "EVT-001");
    EXPECT(first.history.events()[1].event_id == "EVT-002");
    EXPECT(first.history.events()[2].event_id == "EVT-003");
    EXPECT(first.history.events()[3].event_id == "EVT-004");
}

INCIDENTLAB_TEST(engine_scheduling_does_not_execute_immediately) {
    incidentlab::OrderService orders;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.schedule_request(make_request("REQ-001", "OrderService", "CreateOrder"), 10);

    const incidentlab::SimulationReport before = engine.report();
    EXPECT(before.processed_requests == 0);
    EXPECT(before.pending_requests == 1);
    EXPECT(before.history.size() == 0);
    EXPECT(before.current_time == 0);
}

INCIDENTLAB_TEST(run_next_zero_processes_nothing) {
    incidentlab::OrderService orders;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.schedule_request(make_request("REQ-001", "OrderService", "CreateOrder"), 10);
    EXPECT(engine.run_next(0) == 0);
    EXPECT(engine.report().pending_requests == 1);
    EXPECT(engine.report().processed_requests == 0);
    EXPECT(engine.clock().now() == 0);
    EXPECT(engine.history().size() == 0);
}

INCIDENTLAB_TEST(run_next_two_processes_exactly_two_requests) {
    incidentlab::OrderService orders;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    for (int index = 1; index <= 5; ++index) {
        engine.schedule_request(make_request("REQ-00" + std::to_string(index), "OrderService",
                                             "CreateOrder"),
                                static_cast<incidentlab::SimulationClock::Time>(index * 10));
    }
    EXPECT(engine.run_next(2) == 2);
    EXPECT(engine.report().processed_requests == 2);
    EXPECT(engine.report().pending_requests == 3);
}

INCIDENTLAB_TEST(remaining_requests_stay_pending) {
    incidentlab::OrderService orders;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.schedule_request(make_request("REQ-001", "OrderService", "CreateOrder"), 10);
    engine.schedule_request(make_request("REQ-002", "OrderService", "CreateOrder"), 20);
    engine.schedule_request(make_request("REQ-003", "OrderService", "CreateOrder"), 30);
    engine.run_next(1);
    EXPECT(engine.report().pending_requests == 2);
    EXPECT(engine.report().executions[0].request_id == "REQ-001");
}

INCIDENTLAB_TEST(execution_can_resume) {
    incidentlab::OrderService orders;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.schedule_request(make_request("REQ-001", "OrderService", "CreateOrder"), 10);
    engine.schedule_request(make_request("REQ-002", "OrderService", "CreateOrder"), 20);
    engine.schedule_request(make_request("REQ-003", "OrderService", "CreateOrder"), 30);

    EXPECT(engine.run_next(2) == 2);
    EXPECT(engine.run_next(2) == 1);
    EXPECT(engine.report().pending_requests == 0);
    EXPECT(engine.report().executions[2].request_id == "REQ-003");
}

INCIDENTLAB_TEST(virtual_time_does_not_reset_between_batches) {
    incidentlab::OrderService orders;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.schedule_request(make_request("REQ-001", "OrderService", "CreateOrder"), 10);
    engine.schedule_request(make_request("REQ-002", "OrderService", "CreateOrder"), 40);
    engine.run_next(1);
    EXPECT(engine.clock().now() == 10);
    engine.run_next(1);
    EXPECT(engine.clock().now() == 40);
}

INCIDENTLAB_TEST(events_are_not_duplicated_when_execution_resumes) {
    incidentlab::OrderService orders;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.schedule_request(make_request("REQ-001", "OrderService", "CreateOrder"), 10);
    engine.schedule_request(make_request("REQ-002", "OrderService", "CreateOrder"), 20);
    engine.run_next(1);
    EXPECT(engine.history().size() == 2);
    engine.run_next(1);
    EXPECT(engine.history().size() == 4);
    std::set<std::string> ids;
    for (const incidentlab::Event& event : engine.history().events()) {
        ids.insert(event.event_id);
    }
    EXPECT(ids.size() == 4);
}

INCIDENTLAB_TEST(run_next_beyond_pending_processes_only_available_requests) {
    incidentlab::OrderService orders;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.schedule_request(make_request("REQ-001", "OrderService", "CreateOrder"), 10);
    EXPECT(engine.run_next(5) == 1);
    EXPECT(engine.run_next(5) == 0);
    EXPECT(engine.report().processed_requests == 1);
    EXPECT(engine.report().pending_requests == 0);
    EXPECT(engine.history().size() == 2);
}

INCIDENTLAB_TEST(stage3_demo_completes_three_services) {
    const incidentlab::SimulationReport report = incidentlab::run_stage3_demo();
    EXPECT(report.processed_requests == 3);
    EXPECT(report.successful_requests == 3);
    EXPECT(report.rejected_requests == 0);
    EXPECT(report.pending_requests == 0);
    EXPECT(report.executions[0].request_id == "REQ-001");
    EXPECT(report.executions[0].service_name == "OrderService");
    EXPECT(report.executions[0].simulation_time == 10);
    EXPECT(report.executions[1].request_id == "REQ-002");
    EXPECT(report.executions[1].service_name == "PaymentService");
    EXPECT(report.executions[1].simulation_time == 30);
    EXPECT(report.executions[2].request_id == "REQ-003");
    EXPECT(report.executions[2].service_name == "InventoryService");
    EXPECT(report.executions[2].simulation_time == 50);
    for (const incidentlab::ExecutionRecord& step : report.executions) {
        EXPECT(step.trace_id == "TRACE-001");
    }
}

INCIDENTLAB_TEST(stage3_demo_final_time_is_50) {
    const incidentlab::SimulationReport report = incidentlab::run_stage3_demo();
    EXPECT(report.current_time == 50);
}

class ThrowingService : public incidentlab::Service {
public:
    explicit ThrowingService(std::string name) : name_(std::move(name)) {}

    std::string name() const override { return name_; }

    incidentlab::ServiceResult handle(const incidentlab::Request& request) override {
        throw std::runtime_error("service failed: " + request.request_id);
    }

private:
    std::string name_;
};

class FailOnceService : public incidentlab::Service {
public:
    std::string name() const override { return "FailOnceService"; }

    incidentlab::ServiceResult handle(const incidentlab::Request&) override {
        ++calls_;
        if (calls_ == 1) {
            throw std::runtime_error("first attempt failed");
        }
        return incidentlab::ServiceResult{incidentlab::ServiceStatus::Success, "recovered"};
    }

    int calls() const { return calls_; }

private:
    int calls_ = 0;
};

void expect_consistent(const incidentlab::SimulationEngine& engine) {
    const incidentlab::SimulationReport report = engine.report();
    EXPECT(report.history.size() == report.event_times.size());
    EXPECT(report.history.size() == engine.history().size());
    EXPECT(report.processed_requests == report.executions.size());
    EXPECT(report.successful_requests + report.rejected_requests == report.processed_requests);
    EXPECT(report.current_time == engine.clock().now());
}

INCIDENTLAB_TEST(throwing_service_keeps_the_request_pending) {
    ThrowingService billing{"BillingService"};
    incidentlab::SimulationEngine engine;
    engine.register_service(billing);
    engine.schedule_request(make_request("REQ-009", "BillingService", "Charge"), 25);

    try {
        engine.run_next(1);
        throw incidentlab_test::Failure("expected the service exception");
    } catch (const std::runtime_error& ex) {
        EXPECT(std::string(ex.what()) == "service failed: REQ-009");
    }

    expect_consistent(engine);
    EXPECT(engine.report().processed_requests == 0);
    EXPECT(engine.report().pending_requests == 1);
    EXPECT(engine.report().successful_requests == 0);
    EXPECT(engine.report().rejected_requests == 0);
    EXPECT(engine.history().size() == 0);
    EXPECT(engine.clock().now() == 0);

    try {
        engine.run();
        throw incidentlab_test::Failure("expected the service exception again");
    } catch (const std::runtime_error& ex) {
        EXPECT(std::string(ex.what()) == "service failed: REQ-009");
    }
    EXPECT(engine.report().pending_requests == 1);
    EXPECT(engine.history().size() == 0);
}

INCIDENTLAB_TEST(throwing_service_does_not_drop_later_requests) {
    incidentlab::OrderService orders;
    ThrowingService billing{"BillingService"};
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.register_service(billing);
    engine.schedule_request(make_request("REQ-001", "OrderService", "CreateOrder"), 10);
    engine.schedule_request(make_request("REQ-002", "BillingService", "Charge"), 20);
    engine.schedule_request(make_request("REQ-003", "OrderService", "CreateOrder"), 30);

    try {
        engine.run_next(3);
        throw incidentlab_test::Failure("expected the service exception");
    } catch (const std::runtime_error&) {
    }

    expect_consistent(engine);
    const incidentlab::SimulationReport report = engine.report();
    EXPECT(report.processed_requests == 1);
    EXPECT(report.pending_requests == 2);
    EXPECT(report.successful_requests == 1);
    EXPECT(report.rejected_requests == 0);
    EXPECT(report.current_time == 10);
    EXPECT(report.executions.size() == 1);
    EXPECT(report.executions[0].request_id == "REQ-001");
    EXPECT(report.history.size() == 2);
    EXPECT(report.event_times[0] == 10);
    EXPECT(report.event_times[1] == 10);
    EXPECT(report.history.events()[0].type == incidentlab::EventType::RequestReceived);
    EXPECT(report.history.events()[1].type == incidentlab::EventType::RequestCompleted);
    EXPECT(report.history.by_request_id("REQ-002").empty());
    EXPECT(report.history.by_request_id("REQ-003").empty());
}

INCIDENTLAB_TEST(throwing_service_preserves_equal_time_order) {
    ThrowingService first{"ServiceA"};
    incidentlab::OrderService orders;
    incidentlab::SimulationEngine engine;
    engine.register_service(first);
    engine.register_service(orders);
    engine.schedule_request(make_request("REQ-A", "ServiceA", "Charge"), 20);
    engine.schedule_request(make_request("REQ-B", "OrderService", "CreateOrder"), 20);

    try {
        engine.run_next(2);
        throw incidentlab_test::Failure("expected the service exception");
    } catch (const std::runtime_error& ex) {
        EXPECT(std::string(ex.what()) == "service failed: REQ-A");
    }

    try {
        engine.run_next(1);
        throw incidentlab_test::Failure("expected the same request to remain first");
    } catch (const std::runtime_error& ex) {
        EXPECT(std::string(ex.what()) == "service failed: REQ-A");
    }
    EXPECT(engine.report().pending_requests == 2);
    EXPECT(engine.history().size() == 0);
    EXPECT(engine.clock().now() == 0);
}

INCIDENTLAB_TEST(failed_attempt_can_be_executed_later) {
    FailOnceService flaky;
    incidentlab::SimulationEngine engine;
    engine.register_service(flaky);
    engine.schedule_request(make_request("REQ-004", "FailOnceService", "Charge"), 15);

    try {
        engine.run_next(1);
        throw incidentlab_test::Failure("expected the first attempt to throw");
    } catch (const std::runtime_error&) {
    }
    EXPECT(flaky.calls() == 1);
    EXPECT(engine.history().size() == 0);
    EXPECT(engine.clock().now() == 0);

    EXPECT(engine.run_next(1) == 1);
    expect_consistent(engine);
    const incidentlab::SimulationReport report = engine.report();
    EXPECT(report.processed_requests == 1);
    EXPECT(report.pending_requests == 0);
    EXPECT(report.successful_requests == 1);
    EXPECT(report.current_time == 15);
    EXPECT(report.history.size() == 2);
    EXPECT(report.history.events()[0].event_id == "EVT-001");
    EXPECT(report.history.events()[1].event_id == "EVT-002");
    EXPECT(report.history.events()[1].message == "recovered");
    EXPECT(report.event_times[0] == 15);
    EXPECT(report.event_times[1] == 15);
    EXPECT(flaky.calls() == 2);
}

INCIDENTLAB_TEST(run_next_zero_does_not_call_a_throwing_service) {
    ThrowingService billing{"BillingService"};
    incidentlab::SimulationEngine engine;
    engine.register_service(billing);
    engine.schedule_request(make_request("REQ-001", "BillingService", "Charge"), 10);
    EXPECT(engine.run_next(0) == 0);
    EXPECT(engine.report().pending_requests == 1);
    EXPECT(engine.history().size() == 0);
}

INCIDENTLAB_TEST(scheduler_next_does_not_remove_the_event) {
    incidentlab::EventScheduler scheduler;
    scheduler.schedule(make_request("REQ-001", "OrderService", "CreateOrder"), 10, 0);
    EXPECT(scheduler.next().request.request_id == "REQ-001");
    EXPECT(scheduler.pending_count() == 1);
    scheduler.discard_next();
    EXPECT(scheduler.pending_count() == 0);
    try {
        scheduler.next();
        throw incidentlab_test::Failure("expected std::logic_error");
    } catch (const std::logic_error&) {
    }
    scheduler.discard_next();
    EXPECT(scheduler.pending_count() == 0);
}

INCIDENTLAB_TEST(stage3_demo_records_six_events) {
    const incidentlab::SimulationReport report = incidentlab::run_stage3_demo();
    EXPECT(report.history.size() == 6);
    EXPECT(report.event_times.size() == 6);
    EXPECT(report.history.by_trace_id("TRACE-001").size() == 6);
}

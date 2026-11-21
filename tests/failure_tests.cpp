#include "test_harness.hpp"

#include "incidentlab/order_service.hpp"
#include "incidentlab/payment_service.hpp"
#include "incidentlab/simulation/event_scheduler.hpp"
#include "incidentlab/simulation/failure_config.hpp"
#include "incidentlab/simulation/simulation_engine.hpp"
#include "incidentlab/simulation/stage3_demo.hpp"
#include "incidentlab/simulation/stage4_demo.hpp"

#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

incidentlab::Request make_request(const std::string& request_id, const std::string& target,
                                  const std::string& operation) {
    return incidentlab::Request{request_id, "TRACE-001", "client", target, operation};
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

void expect_consistent(const incidentlab::SimulationEngine& engine) {
    const incidentlab::SimulationReport report = engine.report();
    EXPECT(report.history.size() == report.event_times.size());
    EXPECT(report.processed_requests == report.executions.size());
    EXPECT(report.successful_requests + report.rejected_requests + report.failed_requests +
               report.timed_out_requests + report.dropped_requests +
               report.circuit_rejected_requests ==
           report.processed_requests);
}

class CountingService : public incidentlab::Service {
public:
    explicit CountingService(std::string name) : name_(std::move(name)) {}

    std::string name() const override { return name_; }

    incidentlab::ServiceResult handle(const incidentlab::Request&) override {
        ++calls_;
        return incidentlab::ServiceResult{incidentlab::ServiceStatus::Success, "counted"};
    }

    int calls() const { return calls_; }

private:
    std::string name_;
    int calls_ = 0;
};

class ThrowingService : public incidentlab::Service {
public:
    std::string name() const override { return "ThrowingService"; }

    incidentlab::ServiceResult handle(const incidentlab::Request& request) override {
        throw std::runtime_error("service failed: " + request.request_id);
    }
};

class FailOnceService : public incidentlab::Service {
public:
    explicit FailOnceService(std::string name) : name_(std::move(name)) {}

    std::string name() const override { return name_; }

    incidentlab::ServiceResult handle(const incidentlab::Request&) override {
        if (!thrown_) {
            thrown_ = true;
            throw std::runtime_error("service failed once");
        }
        return incidentlab::ServiceResult{incidentlab::ServiceStatus::Success, "recovered"};
    }

private:
    std::string name_;
    bool thrown_ = false;
};

}  // namespace

INCIDENTLAB_TEST(failure_injection_is_disabled_by_default) {
    const incidentlab::SimulationReport report = incidentlab::run_stage3_demo();
    EXPECT(report.successful_requests == 3);
    EXPECT(report.failed_requests == 0);
    EXPECT(report.timed_out_requests == 0);
    EXPECT(report.dropped_requests == 0);
    EXPECT(report.current_time == 50);
}

INCIDENTLAB_TEST(valid_failure_configuration_is_accepted) {
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_failure("PaymentService",
                             incidentlab::FailureConfig{incidentlab::FailureType::Timeout, 200, 100,
                                                        1});
    engine.schedule_request(make_request("REQ-001", "PaymentService", "ProcessPayment"), 30);
    engine.run();
    EXPECT(engine.report().timed_out_requests == 1);
}

INCIDENTLAB_TEST(empty_service_name_is_rejected) {
    incidentlab::SimulationEngine engine;
    expect_throws<std::invalid_argument>([&engine] {
        engine.configure_failure("", incidentlab::FailureConfig{});
    });
}

INCIDENTLAB_TEST(invalid_periodic_interval_is_rejected) {
    incidentlab::SimulationEngine engine;
    expect_throws<std::invalid_argument>([&engine] {
        engine.configure_failure(
            "PaymentService",
            incidentlab::FailureConfig{incidentlab::FailureType::ServiceFailure, 0, 0, 0});
    });
}

INCIDENTLAB_TEST(invalid_timeout_configuration_is_rejected) {
    incidentlab::SimulationEngine engine;
    expect_throws<std::invalid_argument>([&engine] {
        engine.configure_failure(
            "PaymentService",
            incidentlab::FailureConfig{incidentlab::FailureType::Timeout, 200, 0, 1});
    });
    expect_throws<std::invalid_argument>([&engine] {
        engine.configure_failure(
            "PaymentService",
            incidentlab::FailureConfig{incidentlab::FailureType::Timeout, 100, 100, 1});
    });
}

INCIDENTLAB_TEST(per_service_configuration_is_independent) {
    incidentlab::OrderService orders;
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.register_service(payments);
    engine.configure_failure(
        "PaymentService",
        incidentlab::FailureConfig{incidentlab::FailureType::ServiceFailure, 0, 0, 1});
    engine.schedule_request(make_request("REQ-001", "OrderService", "CreateOrder"), 10);
    engine.schedule_request(make_request("REQ-002", "PaymentService", "ProcessPayment"), 20);
    engine.run();
    EXPECT(engine.report().executions[0].result.status == incidentlab::ServiceStatus::Success);
    EXPECT(engine.report().executions[1].result.status == incidentlab::ServiceStatus::Failed);
}

INCIDENTLAB_TEST(service_failure_produces_failed_without_calling_handle) {
    CountingService payments{"PaymentService"};
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_failure(
        "PaymentService",
        incidentlab::FailureConfig{incidentlab::FailureType::ServiceFailure, 0, 0, 1});
    engine.schedule_request(make_request("REQ-002", "PaymentService", "ProcessPayment"), 50);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    EXPECT(payments.calls() == 0);
    EXPECT(report.failed_requests == 1);
    EXPECT(report.executions[0].result.status == incidentlab::ServiceStatus::Failed);
    EXPECT(report.history.events()[0].type == incidentlab::EventType::RequestReceived);
    EXPECT(report.history.events()[1].type == incidentlab::EventType::RequestFailed);
    EXPECT(report.event_times[0] == 50);
    EXPECT(report.event_times[1] == 50);
    EXPECT(report.current_time == 50);
}

INCIDENTLAB_TEST(slow_response_succeeds_after_the_configured_delay) {
    CountingService orders{"OrderService"};
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.configure_failure(
        "OrderService",
        incidentlab::FailureConfig{incidentlab::FailureType::SlowResponse, 150, 0, 1});
    engine.schedule_request(make_request("REQ-001", "OrderService", "CreateOrder"), 10);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    EXPECT(orders.calls() == 1);
    EXPECT(report.successful_requests == 1);
    EXPECT(report.history.events()[0].type == incidentlab::EventType::RequestReceived);
    EXPECT(report.event_times[0] == 10);
    EXPECT(report.history.events()[1].type == incidentlab::EventType::RequestCompleted);
    EXPECT(report.event_times[1] == 160);
    EXPECT(report.executions[0].start_time == 10);
    EXPECT(report.executions[0].simulation_time == 160);
    EXPECT(report.current_time == 160);
}

INCIDENTLAB_TEST(timeout_occurs_at_the_deadline_not_the_full_delay) {
    CountingService payments{"PaymentService"};
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_failure("PaymentService",
                             incidentlab::FailureConfig{incidentlab::FailureType::Timeout, 200, 100,
                                                        1});
    engine.schedule_request(make_request("REQ-002", "PaymentService", "ProcessPayment"), 30);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    EXPECT(payments.calls() == 0);
    EXPECT(report.timed_out_requests == 1);
    EXPECT(report.event_times[0] == 30);
    EXPECT(report.history.events()[0].type == incidentlab::EventType::RequestReceived);
    EXPECT(report.event_times[1] == 130);
    EXPECT(report.history.events()[1].type == incidentlab::EventType::RequestTimedOut);
    EXPECT(report.current_time == 130);
    EXPECT(report.executions[0].result.message == "Request timed out");
}

INCIDENTLAB_TEST(dropped_request_does_not_call_handle) {
    CountingService inventory{"InventoryService"};
    incidentlab::SimulationEngine engine;
    engine.register_service(inventory);
    engine.configure_failure(
        "InventoryService",
        incidentlab::FailureConfig{incidentlab::FailureType::DroppedRequest, 0, 0, 1});
    engine.schedule_request(make_request("REQ-003", "InventoryService", "ReserveInventory"), 70);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    EXPECT(inventory.calls() == 0);
    EXPECT(report.dropped_requests == 1);
    EXPECT(report.history.size() == 1);
    EXPECT(report.history.events()[0].type == incidentlab::EventType::RequestDropped);
    EXPECT(report.event_times[0] == 70);
    EXPECT(report.history.by_request_id("REQ-003").size() == 1);
}

INCIDENTLAB_TEST(normal_request_still_calls_handle) {
    CountingService orders{"OrderService"};
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.schedule_request(make_request("REQ-001", "OrderService", "CreateOrder"), 10);
    engine.run();
    EXPECT(orders.calls() == 1);
    EXPECT(engine.report().successful_requests == 1);
}

INCIDENTLAB_TEST(unsupported_operation_is_rejected_when_no_failure_applies) {
    incidentlab::OrderService orders;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.configure_failure(
        "OrderService",
        incidentlab::FailureConfig{incidentlab::FailureType::ServiceFailure, 0, 0, 2});
    engine.schedule_request(make_request("REQ-001", "OrderService", "ShipOrder"), 10);
    engine.run();
    EXPECT(engine.report().rejected_requests == 1);
    EXPECT(engine.report().history.events()[1].type == incidentlab::EventType::RequestRejected);
    EXPECT(engine.report().history.events()[1].message == "Unsupported operation: ShipOrder");
}

INCIDENTLAB_TEST(unknown_service_is_still_rejected) {
    incidentlab::SimulationEngine engine;
    engine.schedule_request(make_request("REQ-009", "ShippingService", "ShipOrder"), 10);
    engine.run();
    EXPECT(engine.report().rejected_requests == 1);
    EXPECT(engine.report().executions[0].result.message == "Unknown service: ShippingService");
}

INCIDENTLAB_TEST(every_third_matching_request_fails) {
    CountingService payments{"PaymentService"};
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_failure(
        "PaymentService",
        incidentlab::FailureConfig{incidentlab::FailureType::ServiceFailure, 0, 0, 3});
    for (int index = 1; index <= 6; ++index) {
        engine.schedule_request(make_request("REQ-00" + std::to_string(index), "PaymentService",
                                             "ProcessPayment"),
                                static_cast<incidentlab::SimulationClock::Time>(index * 10));
    }
    engine.run();
    const auto& executions = engine.report().executions;
    EXPECT(executions.size() == 6);
    EXPECT(executions[0].result.status == incidentlab::ServiceStatus::Success);
    EXPECT(executions[1].result.status == incidentlab::ServiceStatus::Success);
    EXPECT(executions[2].result.status == incidentlab::ServiceStatus::Failed);
    EXPECT(executions[3].result.status == incidentlab::ServiceStatus::Success);
    EXPECT(executions[4].result.status == incidentlab::ServiceStatus::Success);
    EXPECT(executions[5].result.status == incidentlab::ServiceStatus::Failed);
    EXPECT(payments.calls() == 4);
}

INCIDENTLAB_TEST(service_counters_are_independent) {
    incidentlab::OrderService orders;
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.register_service(payments);
    const incidentlab::FailureConfig every_second{incidentlab::FailureType::ServiceFailure, 0, 0,
                                                  2};
    engine.configure_failure("OrderService", every_second);
    engine.configure_failure("PaymentService", every_second);
    engine.schedule_request(make_request("REQ-001", "OrderService", "CreateOrder"), 10);
    engine.schedule_request(make_request("REQ-002", "PaymentService", "ProcessPayment"), 20);
    engine.schedule_request(make_request("REQ-003", "OrderService", "CreateOrder"), 30);
    engine.schedule_request(make_request("REQ-004", "PaymentService", "ProcessPayment"), 40);
    engine.run();
    const auto& executions = engine.report().executions;
    EXPECT(executions[0].result.status == incidentlab::ServiceStatus::Success);
    EXPECT(executions[1].result.status == incidentlab::ServiceStatus::Success);
    EXPECT(executions[2].result.status == incidentlab::ServiceStatus::Failed);
    EXPECT(executions[3].result.status == incidentlab::ServiceStatus::Failed);
}

INCIDENTLAB_TEST(equal_time_requests_keep_schedule_order_without_delay) {
    incidentlab::OrderService orders;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.schedule_request(make_request("REQ-A", "OrderService", "CreateOrder"), 20);
    engine.schedule_request(make_request("REQ-B", "OrderService", "CreateOrder"), 20);
    engine.run();
    EXPECT(engine.report().executions[0].request_id == "REQ-A");
    EXPECT(engine.report().executions[1].request_id == "REQ-B");
    EXPECT(engine.report().history.events()[0].request_id == "REQ-A");
    EXPECT(engine.report().history.events()[1].request_id == "REQ-A");
    EXPECT(engine.report().history.events()[2].request_id == "REQ-B");
}

INCIDENTLAB_TEST(repeated_runs_are_identical) {
    auto run_once = [] {
        incidentlab::PaymentService payments;
        incidentlab::SimulationEngine engine;
        engine.register_service(payments);
        engine.configure_failure(
            "PaymentService",
            incidentlab::FailureConfig{incidentlab::FailureType::ServiceFailure, 0, 0, 2});
        engine.schedule_request(make_request("REQ-001", "PaymentService", "ProcessPayment"), 10);
        engine.schedule_request(make_request("REQ-002", "PaymentService", "ProcessPayment"), 20);
        engine.run();
        return engine.report();
    };
    const incidentlab::SimulationReport first = run_once();
    const incidentlab::SimulationReport second = run_once();
    EXPECT(first.history.size() == second.history.size());
    for (std::size_t index = 0; index < first.history.size(); ++index) {
        EXPECT(first.history.events()[index].event_id == second.history.events()[index].event_id);
        EXPECT(first.history.events()[index].type == second.history.events()[index].type);
        EXPECT(first.event_times[index] == second.event_times[index]);
    }
}

INCIDENTLAB_TEST(timeout_and_same_timestamp_follow_sequence_order) {
    incidentlab::OrderService orders;
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.register_service(payments);
    engine.configure_failure("PaymentService",
                             incidentlab::FailureConfig{incidentlab::FailureType::Timeout, 100, 40,
                                                        1});
    engine.schedule_request(make_request("REQ-A", "PaymentService", "ProcessPayment"), 10);
    engine.schedule_request(make_request("REQ-B", "OrderService", "CreateOrder"), 50);
    engine.run();
    const auto report = engine.report();
    const auto& events = report.history.events();
    EXPECT(events[0].request_id == "REQ-A");
    EXPECT(events[0].type == incidentlab::EventType::RequestReceived);
    EXPECT(report.event_times[0] == 10);
    EXPECT(events[1].request_id == "REQ-B");
    EXPECT(events[1].type == incidentlab::EventType::RequestReceived);
    EXPECT(events[2].request_id == "REQ-B");
    EXPECT(events[2].type == incidentlab::EventType::RequestCompleted);
    EXPECT(report.event_times[2] == 50);
    EXPECT(events[3].request_id == "REQ-A");
    EXPECT(events[3].type == incidentlab::EventType::RequestTimedOut);
    EXPECT(report.event_times[3] == 50);
}

INCIDENTLAB_TEST(overlapping_requests_stay_in_chronological_order) {
    const incidentlab::SimulationReport report = incidentlab::run_stage4_demo();
    EXPECT(report.processed_requests == 3);
    EXPECT(report.successful_requests == 2);
    EXPECT(report.timed_out_requests == 1);
    EXPECT(report.failed_requests == 0);
    EXPECT(report.dropped_requests == 0);
    EXPECT(report.rejected_requests == 0);
    EXPECT(report.pending_requests == 0);
    EXPECT(report.in_progress_requests == 0);
    EXPECT(report.current_time == 130);
    EXPECT(report.history.size() == 6);
    const auto& events = report.history.events();
    EXPECT(events[0].request_id == "REQ-001");
    EXPECT(events[0].type == incidentlab::EventType::RequestReceived);
    EXPECT(report.event_times[0] == 10);
    EXPECT(events[1].request_id == "REQ-001");
    EXPECT(events[1].type == incidentlab::EventType::RequestCompleted);
    EXPECT(events[2].request_id == "REQ-002");
    EXPECT(events[2].type == incidentlab::EventType::RequestReceived);
    EXPECT(report.event_times[2] == 30);
    EXPECT(events[3].request_id == "REQ-003");
    EXPECT(events[3].type == incidentlab::EventType::RequestReceived);
    EXPECT(report.event_times[3] == 50);
    EXPECT(events[4].request_id == "REQ-003");
    EXPECT(events[4].type == incidentlab::EventType::RequestCompleted);
    EXPECT(events[5].request_id == "REQ-002");
    EXPECT(events[5].type == incidentlab::EventType::RequestTimedOut);
    EXPECT(report.event_times[5] == 130);
    for (std::size_t index = 1; index < report.event_times.size(); ++index) {
        EXPECT(report.event_times[index] >= report.event_times[index - 1]);
    }
    for (const incidentlab::Event& event : events) {
        EXPECT(event.trace_id == "TRACE-001");
    }
}

INCIDENTLAB_TEST(zero_delay_slow_response_completes_at_the_start) {
    CountingService orders{"OrderService"};
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.configure_failure("OrderService", incidentlab::FailureConfig{
                                                 incidentlab::FailureType::SlowResponse, 0, 0, 1});
    engine.schedule_request(make_request("REQ-001", "OrderService", "CreateOrder"), 15);
    engine.run();
    EXPECT(orders.calls() == 1);
    EXPECT(engine.report().event_times[0] == 15);
    EXPECT(engine.report().event_times[1] == 15);
    EXPECT(engine.report().current_time == 15);
    EXPECT(engine.report().successful_requests == 1);
}

INCIDENTLAB_TEST(in_progress_request_is_not_counted_as_completed) {
    CountingService orders{"OrderService"};
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.register_service(payments);
    engine.configure_failure(
        "OrderService",
        incidentlab::FailureConfig{incidentlab::FailureType::SlowResponse, 100, 0, 1});
    engine.schedule_request(make_request("REQ-001", "OrderService", "CreateOrder"), 10);
    engine.schedule_request(make_request("REQ-002", "PaymentService", "ProcessPayment"), 30);
    EXPECT(engine.run_next(1) == 1);
    const incidentlab::SimulationReport midway = engine.report();
    EXPECT(midway.processed_requests == 1);
    EXPECT(midway.successful_requests == 1);
    EXPECT(midway.in_progress_requests == 1);
    EXPECT(midway.pending_requests == 0);
    EXPECT(midway.current_time == 30);
    EXPECT(midway.executions[0].request_id == "REQ-002");
    EXPECT(midway.history.by_request_id("REQ-001").size() == 1);
    EXPECT(midway.history.events()[0].type == incidentlab::EventType::RequestReceived);

    EXPECT(engine.run_next(1) == 1);
    const incidentlab::SimulationReport done = engine.report();
    EXPECT(done.processed_requests == 2);
    EXPECT(done.in_progress_requests == 0);
    EXPECT(done.current_time == 110);
    EXPECT(done.executions[1].request_id == "REQ-001");
    EXPECT(done.executions[1].simulation_time == 110);
    EXPECT(orders.calls() == 1);
}

INCIDENTLAB_TEST(failure_counts_match_terminal_outcomes) {
    incidentlab::OrderService orders;
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.register_service(payments);
    engine.configure_failure(
        "PaymentService",
        incidentlab::FailureConfig{incidentlab::FailureType::ServiceFailure, 0, 0, 1});
    engine.configure_failure("ShippingService",
                             incidentlab::FailureConfig{incidentlab::FailureType::DroppedRequest, 0,
                                                        0, 1});
    engine.configure_failure("InventoryService",
                             incidentlab::FailureConfig{incidentlab::FailureType::Timeout, 80, 20,
                                                        1});
    engine.schedule_request(make_request("REQ-001", "OrderService", "CreateOrder"), 10);
    engine.schedule_request(make_request("REQ-002", "OrderService", "ShipOrder"), 20);
    engine.schedule_request(make_request("REQ-003", "PaymentService", "ProcessPayment"), 30);
    engine.schedule_request(make_request("REQ-004", "InventoryService", "ReserveInventory"), 40);
    engine.schedule_request(make_request("REQ-005", "ShippingService", "ShipOrder"), 100);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    EXPECT(report.successful_requests == 1);
    EXPECT(report.rejected_requests == 1);
    EXPECT(report.failed_requests == 1);
    EXPECT(report.timed_out_requests == 1);
    EXPECT(report.dropped_requests == 1);
    EXPECT(report.processed_requests == 5);
    EXPECT(report.pending_requests == 0);
    std::set<std::string> ids;
    for (const incidentlab::Event& event : report.history.events()) {
        ids.insert(event.event_id);
        EXPECT(event.trace_id == "TRACE-001");
        EXPECT(!event.request_id.empty());
    }
    EXPECT(ids.size() == report.history.size());
    expect_consistent(engine);
}

INCIDENTLAB_TEST(reconfiguration_resets_the_failure_counter) {
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_failure(
        "PaymentService",
        incidentlab::FailureConfig{incidentlab::FailureType::ServiceFailure, 0, 0, 2});
    engine.schedule_request(make_request("REQ-001", "PaymentService", "ProcessPayment"), 10);
    engine.run();
    engine.configure_failure(
        "PaymentService",
        incidentlab::FailureConfig{incidentlab::FailureType::ServiceFailure, 0, 0, 2});
    engine.schedule_request(make_request("REQ-002", "PaymentService", "ProcessPayment"), 20);
    engine.run();
    EXPECT(engine.report().executions[0].result.status == incidentlab::ServiceStatus::Success);
    EXPECT(engine.report().executions[1].result.status == incidentlab::ServiceStatus::Success);
}

INCIDENTLAB_TEST(unexpected_exception_keeps_completed_work_and_the_request) {
    incidentlab::OrderService orders;
    ThrowingService throwing;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.register_service(throwing);
    engine.schedule_request(make_request("REQ-001", "OrderService", "CreateOrder"), 10);
    engine.schedule_request(make_request("REQ-002", "ThrowingService", "Charge"), 20);
    expect_throws<std::runtime_error>([&engine] { engine.run(); });
    const incidentlab::SimulationReport report = engine.report();
    EXPECT(report.successful_requests == 1);
    EXPECT(report.processed_requests == 1);
    EXPECT(report.pending_requests == 1);
    EXPECT(report.in_progress_requests == 0);
    EXPECT(report.history.size() == 2);
    EXPECT(report.history.size() == report.event_times.size());
    EXPECT(report.history.by_request_id("REQ-002").empty());
    EXPECT(report.current_time == 10);
    expect_throws<std::runtime_error>([&engine] { engine.run_next(1); });
    EXPECT(engine.report().pending_requests == 1);
    EXPECT(engine.report().history.size() == 2);
}

INCIDENTLAB_TEST(simulation_time_overflow_is_rejected_before_commit) {
    CountingService orders{"OrderService"};
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.configure_failure(
        "OrderService",
        incidentlab::FailureConfig{incidentlab::FailureType::SlowResponse, 100, 0, 1});
    const auto start = std::numeric_limits<std::uint64_t>::max() - 10;
    engine.schedule_request(make_request("REQ-001", "OrderService", "CreateOrder"), start);
    expect_throws<std::overflow_error>([&engine] { engine.run(); });
    EXPECT(orders.calls() == 0);
    EXPECT(engine.report().pending_requests == 1);
    EXPECT(engine.history().size() == 0);
    EXPECT(engine.clock().now() == 0);
}

INCIDENTLAB_TEST(none_removes_a_failure_policy) {
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_failure(
        "PaymentService",
        incidentlab::FailureConfig{incidentlab::FailureType::ServiceFailure, 0, 0, 1});
    engine.configure_failure("PaymentService", incidentlab::FailureConfig{});
    engine.schedule_request(make_request("REQ-001", "PaymentService", "ProcessPayment"), 10);
    engine.run();
    EXPECT(engine.report().successful_requests == 1);
}

INCIDENTLAB_TEST(slow_response_throw_does_not_schedule_a_finish) {
    FailOnceService orders{"OrderService"};
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.configure_failure(
        "OrderService",
        incidentlab::FailureConfig{incidentlab::FailureType::SlowResponse, 40, 0, 1});
    engine.schedule_request(make_request("REQ-001", "OrderService", "CreateOrder"), 10);
    expect_throws<std::runtime_error>([&engine] { engine.run(); });

    const incidentlab::SimulationReport failed = engine.report();
    EXPECT(failed.pending_requests == 1);
    EXPECT(failed.in_progress_requests == 0);
    EXPECT(failed.processed_requests == 0);
    EXPECT(failed.history.size() == 0);
    EXPECT(failed.event_times.size() == 0);
    EXPECT(failed.current_time == 0);

    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    EXPECT(report.successful_requests == 1);
    EXPECT(report.processed_requests == 1);
    EXPECT(report.in_progress_requests == 0);
    EXPECT(report.pending_requests == 0);
    EXPECT(report.history.size() == 2);
    EXPECT(report.event_times.size() == 2);
    EXPECT(report.event_times[0] == 10);
    EXPECT(report.event_times[1] == 50);
    EXPECT(report.history.events()[0].type == incidentlab::EventType::RequestReceived);
    EXPECT(report.history.events()[1].type == incidentlab::EventType::RequestCompleted);
    EXPECT(report.current_time == 50);
}

INCIDENTLAB_TEST(in_flight_request_survives_a_later_exception) {
    incidentlab::OrderService orders;
    FailOnceService payments{"PaymentService"};
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.register_service(payments);
    engine.configure_failure(
        "OrderService",
        incidentlab::FailureConfig{incidentlab::FailureType::SlowResponse, 100, 0, 1});
    engine.schedule_request(make_request("REQ-001", "OrderService", "CreateOrder"), 10);
    engine.schedule_request(make_request("REQ-002", "PaymentService", "ProcessPayment"), 40);
    expect_throws<std::runtime_error>([&engine] { engine.run(); });

    const incidentlab::SimulationReport midway = engine.report();
    EXPECT(midway.in_progress_requests == 1);
    EXPECT(midway.pending_requests == 1);
    EXPECT(midway.processed_requests == 0);
    EXPECT(midway.successful_requests == 0);
    EXPECT(midway.history.size() == 1);
    EXPECT(midway.event_times.size() == 1);
    EXPECT(midway.event_times[0] == 10);
    EXPECT(midway.history.events()[0].request_id == "REQ-001");
    EXPECT(midway.current_time == 10);

    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    EXPECT(report.processed_requests == 2);
    EXPECT(report.successful_requests == 2);
    EXPECT(report.in_progress_requests == 0);
    EXPECT(report.pending_requests == 0);
    EXPECT(report.history.size() == 4);
    EXPECT(report.history.size() == report.event_times.size());
    EXPECT(report.history.by_request_id("REQ-001").size() == 2);
    EXPECT(report.event_times.back() == 110);
    EXPECT(report.current_time == 110);
    for (std::size_t index = 1; index < report.event_times.size(); ++index) {
        EXPECT(report.event_times[index] >= report.event_times[index - 1]);
    }
}

INCIDENTLAB_TEST(run_next_zero_does_not_start_a_configured_timeout) {
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_failure("PaymentService",
                             incidentlab::FailureConfig{incidentlab::FailureType::Timeout, 200, 100,
                                                        1});
    engine.schedule_request(make_request("REQ-002", "PaymentService", "ProcessPayment"), 30);
    EXPECT(engine.run_next(0) == 0);
    const incidentlab::SimulationReport idle = engine.report();
    EXPECT(idle.pending_requests == 1);
    EXPECT(idle.in_progress_requests == 0);
    EXPECT(idle.processed_requests == 0);
    EXPECT(idle.history.size() == 0);
    EXPECT(idle.current_time == 0);

    EXPECT(engine.run_next(1) == 1);
    const incidentlab::SimulationReport report = engine.report();
    EXPECT(report.timed_out_requests == 1);
    EXPECT(report.processed_requests == 1);
    EXPECT(report.in_progress_requests == 0);
    EXPECT(report.current_time == 130);
}

INCIDENTLAB_TEST(scheduler_keeps_start_before_a_later_same_time_finish) {
    incidentlab::EventScheduler scheduler;
    scheduler.schedule(make_request("REQ-B", "OrderService", "CreateOrder"), 50, 0,
                       incidentlab::ScheduledAction::Start);
    scheduler.schedule(make_request("REQ-A", "PaymentService", "ProcessPayment"), 50, 0,
                       incidentlab::ScheduledAction::Finish);
    EXPECT(scheduler.next().request.request_id == "REQ-B");
    EXPECT(scheduler.next().action == incidentlab::ScheduledAction::Start);
    scheduler.discard_next();
    EXPECT(scheduler.next().action == incidentlab::ScheduledAction::Finish);
    EXPECT(scheduler.next().request.request_id == "REQ-A");
}

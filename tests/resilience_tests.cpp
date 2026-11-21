#include "test_harness.hpp"

#include "incidentlab/order_service.hpp"
#include "incidentlab/payment_service.hpp"
#include "incidentlab/simulation/simulation_engine.hpp"
#include "incidentlab/simulation/stage4_demo.hpp"
#include "incidentlab/simulation/stage5_demo.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
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

bool has_type(const incidentlab::SimulationReport& report, incidentlab::EventType type) {
    for (const incidentlab::Event& event : report.history.events()) {
        if (event.type == type) {
            return true;
        }
    }
    return false;
}

std::size_t count_type(const incidentlab::SimulationReport& report, incidentlab::EventType type) {
    std::size_t count = 0;
    for (const incidentlab::Event& event : report.history.events()) {
        if (event.type == type) {
            ++count;
        }
    }
    return count;
}

class ScriptedService : public incidentlab::Service {
public:
    ScriptedService(std::string name, std::vector<incidentlab::ServiceStatus> results)
        : name_(std::move(name)), results_(std::move(results)) {}

    std::string name() const override { return name_; }

    incidentlab::ServiceResult handle(const incidentlab::Request&) override {
        ++calls_;
        const incidentlab::ServiceStatus status =
            calls_ <= results_.size() ? results_[calls_ - 1] : incidentlab::ServiceStatus::Failed;
        const char* message = status == incidentlab::ServiceStatus::Success ? "ok" : "fail";
        return incidentlab::ServiceResult{status, message};
    }

    int calls() const { return static_cast<int>(calls_); }

private:
    std::string name_;
    std::vector<incidentlab::ServiceStatus> results_;
    std::size_t calls_ = 0;
};

class ThrowingService : public incidentlab::Service {
public:
    std::string name() const override { return "ThrowingService"; }

    incidentlab::ServiceResult handle(const incidentlab::Request&) override {
        throw std::runtime_error("service failed");
    }
};

using Status = incidentlab::ServiceStatus;

}  // namespace

INCIDENTLAB_TEST(retry_is_disabled_by_default) {
    const incidentlab::SimulationReport report = incidentlab::run_stage4_demo();
    EXPECT(report.successful_requests == 2);
    EXPECT(report.timed_out_requests == 1);
    EXPECT(report.retry_attempts == 0);
    EXPECT(report.total_attempts == 3);
    EXPECT(report.dead_lettered_requests == 0);
    EXPECT(report.circuit_rejected_requests == 0);
    EXPECT(!has_type(report, incidentlab::EventType::RetryScheduled));
}

INCIDENTLAB_TEST(successful_request_is_not_retried) {
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_retry("PaymentService", incidentlab::RetryConfig{3, 100, 2.0});
    engine.schedule_request(make_request("REQ-001", "PaymentService", "ProcessPayment"), 10);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    EXPECT(report.successful_requests == 1);
    EXPECT(report.total_attempts == 1);
    EXPECT(report.retry_attempts == 0);
    EXPECT(report.dead_lettered_requests == 0);
    EXPECT(!has_type(report, incidentlab::EventType::RetryScheduled));
}

INCIDENTLAB_TEST(failed_request_is_retried_until_success) {
    ScriptedService payments{"PaymentService", {Status::Failed, Status::Failed, Status::Success}};
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_retry("PaymentService", incidentlab::RetryConfig{3, 100, 2.0});
    engine.schedule_request(make_request("REQ-PAY", "PaymentService", "ProcessPayment"), 10);
    EXPECT(engine.run_next(1) == 1);
    const incidentlab::SimulationReport report = engine.report();
    EXPECT(payments.calls() == 3);
    EXPECT(report.successful_requests == 1);
    EXPECT(report.failed_requests == 0);
    EXPECT(report.processed_requests == 1);
    EXPECT(report.total_attempts == 3);
    EXPECT(report.retry_attempts == 2);
    EXPECT(report.pending_requests == 0);
    EXPECT(report.event_times[0] == 10);
    EXPECT(count_type(report, incidentlab::EventType::RequestFailed) == 2);
    EXPECT(count_type(report, incidentlab::EventType::RequestCompleted) == 1);
    EXPECT(report.history.size() == report.event_times.size());
}

INCIDENTLAB_TEST(timeout_is_retried) {
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_failure("PaymentService",
                             incidentlab::FailureConfig{incidentlab::FailureType::Timeout, 80, 50,
                                                        1});
    engine.configure_retry("PaymentService", incidentlab::RetryConfig{2, 20, 2.0});
    engine.schedule_request(make_request("REQ-002", "PaymentService", "ProcessPayment"), 10);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    EXPECT(report.timed_out_requests == 1);
    EXPECT(report.total_attempts == 2);
    EXPECT(report.retry_attempts == 1);
    EXPECT(report.event_times[0] == 10);
    EXPECT(count_type(report, incidentlab::EventType::RequestTimedOut) == 2);
    EXPECT(report.dead_letters.size() == 1);
    EXPECT(report.dead_letters[0].failed_at_ms == 130);
}

INCIDENTLAB_TEST(dropped_request_is_retried) {
    ScriptedService inventory{"InventoryService", {Status::Success}};
    incidentlab::SimulationEngine engine;
    engine.register_service(inventory);
    engine.configure_failure(
        "InventoryService",
        incidentlab::FailureConfig{incidentlab::FailureType::DroppedRequest, 0, 0, 1});
    engine.configure_retry("InventoryService", incidentlab::RetryConfig{2, 15, 2.0});
    engine.schedule_request(make_request("REQ-003", "InventoryService", "ReserveInventory"), 5);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    EXPECT(inventory.calls() == 0);
    EXPECT(report.dropped_requests == 1);
    EXPECT(report.total_attempts == 2);
    EXPECT(count_type(report, incidentlab::EventType::RequestDropped) == 2);
    EXPECT(report.event_times[0] == 5);
    EXPECT(report.dead_letters[0].attempts == 2);
}

INCIDENTLAB_TEST(rejected_request_is_not_retried) {
    incidentlab::OrderService orders;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.configure_retry("OrderService", incidentlab::RetryConfig{5, 10, 2.0});
    engine.schedule_request(make_request("REQ-001", "OrderService", "ShipOrder"), 10);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    EXPECT(report.rejected_requests == 1);
    EXPECT(report.total_attempts == 1);
    EXPECT(report.retry_attempts == 0);
    EXPECT(report.dead_lettered_requests == 0);
    EXPECT(!has_type(report, incidentlab::EventType::RetryScheduled));
}

INCIDENTLAB_TEST(maximum_attempts_are_enforced) {
    ScriptedService payments{"PaymentService",
                             {Status::Failed, Status::Failed, Status::Failed, Status::Success}};
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_retry("PaymentService", incidentlab::RetryConfig{3, 10, 2.0});
    engine.schedule_request(make_request("REQ-001", "PaymentService", "ProcessPayment"), 0);
    engine.run();
    EXPECT(payments.calls() == 3);
    EXPECT(engine.report().failed_requests == 1);
    EXPECT(engine.report().total_attempts == 3);
    EXPECT(engine.report().retry_attempts == 2);
    EXPECT(engine.report().pending_requests == 0);
    EXPECT(engine.dead_letters().size() == 1);
}

INCIDENTLAB_TEST(exponential_backoff_timestamps_are_correct) {
    const incidentlab::Stage5Demo demo = incidentlab::run_stage5_demo();
    const incidentlab::SimulationReport& report = demo.retry;
    EXPECT(report.event_times[0] == 10);
    EXPECT(count_type(report, incidentlab::EventType::RequestFailed) == 2);
    bool saw_110 = false;
    bool saw_310 = false;
    for (const auto time : report.event_times) {
        if (time == 110) {
            saw_110 = true;
        }
        if (time == 310) {
            saw_310 = true;
        }
    }
    EXPECT(saw_110);
    EXPECT(saw_310);
    EXPECT(report.current_time == 310);
    EXPECT(report.successful_requests == 1);
    for (std::size_t index = 1; index < report.event_times.size(); ++index) {
        EXPECT(report.event_times[index] >= report.event_times[index - 1]);
    }
}

INCIDENTLAB_TEST(invalid_retry_configuration_is_rejected) {
    incidentlab::SimulationEngine engine;
    expect_throws<std::invalid_argument>([&engine] {
        engine.configure_retry("PaymentService", incidentlab::RetryConfig{0, 100, 2.0});
    });
    expect_throws<std::invalid_argument>([&engine] {
        engine.configure_retry("PaymentService", incidentlab::RetryConfig{3, 100, 0.5});
    });
    expect_throws<std::invalid_argument>([&engine] {
        const double invalid = std::numeric_limits<double>::quiet_NaN();
        engine.configure_retry("PaymentService", incidentlab::RetryConfig{3, 100, invalid});
    });
    expect_throws<std::invalid_argument>(
        [&engine] { engine.configure_retry("", incidentlab::RetryConfig{}); });
    expect_throws<std::invalid_argument>([&engine] {
        engine.configure_circuit_breaker("PaymentService",
                                        incidentlab::CircuitBreakerConfig{0, 10});
    });
    expect_throws<std::invalid_argument>([&engine] {
        engine.configure_circuit_breaker("", incidentlab::CircuitBreakerConfig{});
    });
}

INCIDENTLAB_TEST(retry_execution_is_deterministic) {
    auto run_once = [] {
        ScriptedService payments{"PaymentService", {Status::Failed, Status::Success}};
        incidentlab::SimulationEngine engine;
        engine.register_service(payments);
        engine.configure_retry("PaymentService", incidentlab::RetryConfig{3, 40, 2.0});
        engine.schedule_request(make_request("REQ-001", "PaymentService", "ProcessPayment"), 5);
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

INCIDENTLAB_TEST(circuit_starts_closed) {
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    EXPECT(!engine.circuit_state("PaymentService").has_value());
    engine.configure_circuit_breaker("PaymentService", incidentlab::CircuitBreakerConfig{3, 500});
    EXPECT(engine.circuit_state("PaymentService") == incidentlab::CircuitState::Closed);
}

INCIDENTLAB_TEST(consecutive_failures_open_the_circuit) {
    ScriptedService payments{"PaymentService", {Status::Failed, Status::Failed, Status::Failed}};
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_circuit_breaker("PaymentService", incidentlab::CircuitBreakerConfig{3, 500});
    engine.schedule_request(make_request("REQ-1", "PaymentService", "ProcessPayment"), 10);
    engine.schedule_request(make_request("REQ-2", "PaymentService", "ProcessPayment"), 20);
    engine.schedule_request(make_request("REQ-3", "PaymentService", "ProcessPayment"), 30);
    engine.run();
    EXPECT(engine.circuit_state("PaymentService") == incidentlab::CircuitState::Open);
    EXPECT(count_type(engine.report(), incidentlab::EventType::CircuitOpened) == 1);
    EXPECT(engine.report().current_time == 30);
}

INCIDENTLAB_TEST(success_resets_the_failure_counter) {
    ScriptedService payments{"PaymentService",
                             {Status::Failed, Status::Failed, Status::Success, Status::Failed,
                              Status::Failed, Status::Failed}};
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_circuit_breaker("PaymentService", incidentlab::CircuitBreakerConfig{3, 500});
    for (int index = 1; index <= 5; ++index) {
        engine.schedule_request(make_request("REQ-" + std::to_string(index), "PaymentService",
                                             "ProcessPayment"),
                                static_cast<incidentlab::SimulationClock::Time>(index * 10));
    }
    engine.run();
    EXPECT(engine.circuit_state("PaymentService") == incidentlab::CircuitState::Closed);
    engine.schedule_request(make_request("REQ-6", "PaymentService", "ProcessPayment"), 60);
    engine.run();
    EXPECT(engine.circuit_state("PaymentService") == incidentlab::CircuitState::Open);
}

INCIDENTLAB_TEST(open_circuit_blocks_without_calling_the_service) {
    ScriptedService payments{"PaymentService", {Status::Failed, Status::Success}};
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_circuit_breaker("PaymentService", incidentlab::CircuitBreakerConfig{1, 500});
    engine.schedule_request(make_request("REQ-1", "PaymentService", "ProcessPayment"), 10);
    engine.schedule_request(make_request("REQ-2", "PaymentService", "ProcessPayment"), 20);
    engine.run();
    EXPECT(payments.calls() == 1);
    EXPECT(engine.report().circuit_rejected_requests == 1);
    EXPECT(engine.report().failed_requests == 1);
    EXPECT(engine.report().total_attempts == 1);
    EXPECT(has_type(engine.report(), incidentlab::EventType::CircuitRejected));
}

INCIDENTLAB_TEST(reset_timeout_allows_one_half_open_probe) {
    ScriptedService payments{"PaymentService", {Status::Failed, Status::Success}};
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_circuit_breaker("PaymentService", incidentlab::CircuitBreakerConfig{1, 100});
    engine.schedule_request(make_request("REQ-1", "PaymentService", "ProcessPayment"), 10);
    engine.schedule_request(make_request("REQ-2", "PaymentService", "ProcessPayment"), 50);
    engine.schedule_request(make_request("REQ-3", "PaymentService", "ProcessPayment"), 110);
    engine.run();
    EXPECT(payments.calls() == 2);
    EXPECT(engine.circuit_state("PaymentService") == incidentlab::CircuitState::Closed);
    EXPECT(count_type(engine.report(), incidentlab::EventType::CircuitHalfOpened) == 1);
    EXPECT(count_type(engine.report(), incidentlab::EventType::CircuitClosed) == 1);
    EXPECT(engine.report().circuit_rejected_requests == 1);
    EXPECT(engine.report().successful_requests == 1);
}

INCIDENTLAB_TEST(failed_probe_reopens_the_circuit) {
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_failure(
        "PaymentService",
        incidentlab::FailureConfig{incidentlab::FailureType::ServiceFailure, 0, 0, 1});
    engine.configure_circuit_breaker("PaymentService", incidentlab::CircuitBreakerConfig{1, 100});
    engine.schedule_request(make_request("REQ-1", "PaymentService", "ProcessPayment"), 0);
    engine.schedule_request(make_request("REQ-2", "PaymentService", "ProcessPayment"), 100);
    engine.schedule_request(make_request("REQ-3", "PaymentService", "ProcessPayment"), 150);
    engine.run();
    EXPECT(engine.circuit_state("PaymentService") == incidentlab::CircuitState::Open);
    EXPECT(engine.report().circuit_rejected_requests == 1);
    EXPECT(count_type(engine.report(), incidentlab::EventType::CircuitHalfOpened) == 1);
    EXPECT(count_type(engine.report(), incidentlab::EventType::CircuitOpened) == 2);
}

INCIDENTLAB_TEST(only_one_half_open_probe_is_in_flight) {
    ScriptedService payments{"PaymentService", {Status::Failed}};
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_failure(
        "PaymentService",
        incidentlab::FailureConfig{incidentlab::FailureType::ServiceFailure, 0, 0, 1});
    engine.configure_circuit_breaker("PaymentService", incidentlab::CircuitBreakerConfig{1, 100});
    engine.schedule_request(make_request("REQ-1", "PaymentService", "ProcessPayment"), 0);
    EXPECT(engine.run_next(1) == 1);
    engine.configure_failure(
        "PaymentService",
        incidentlab::FailureConfig{incidentlab::FailureType::ServiceFailure, 100, 0, 1});
    engine.schedule_request(make_request("REQ-2", "PaymentService", "ProcessPayment"), 100);
    engine.schedule_request(make_request("REQ-3", "PaymentService", "ProcessPayment"), 100);
    EXPECT(engine.run_next(1) == 1);
    const incidentlab::SimulationReport midway = engine.report();
    EXPECT(midway.in_progress_requests == 1);
    EXPECT(midway.circuit_rejected_requests == 1);
    EXPECT(engine.circuit_state("PaymentService") == incidentlab::CircuitState::HalfOpen);
    engine.run();
    EXPECT(engine.circuit_state("PaymentService") == incidentlab::CircuitState::Open);
    EXPECT(engine.report().in_progress_requests == 0);
}

INCIDENTLAB_TEST(circuit_state_is_independent_per_service) {
    ScriptedService payments{"PaymentService", {Status::Failed}};
    ScriptedService orders{"OrderService", {Status::Failed}};
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.register_service(orders);
    engine.configure_circuit_breaker("PaymentService", incidentlab::CircuitBreakerConfig{1, 500});
    engine.configure_circuit_breaker("OrderService", incidentlab::CircuitBreakerConfig{3, 500});
    engine.schedule_request(make_request("REQ-P", "PaymentService", "ProcessPayment"), 10);
    engine.schedule_request(make_request("REQ-O", "OrderService", "CreateOrder"), 20);
    engine.run();
    EXPECT(engine.circuit_state("PaymentService") == incidentlab::CircuitState::Open);
    EXPECT(engine.circuit_state("OrderService") == incidentlab::CircuitState::Closed);
}

INCIDENTLAB_TEST(exhausted_retries_create_one_dlq_entry) {
    const incidentlab::Stage5Demo demo = incidentlab::run_stage5_demo();
    const incidentlab::SimulationReport& report = demo.dead_letter;
    EXPECT(report.dead_lettered_requests == 1);
    EXPECT(report.dead_letters.size() == 1);
    EXPECT(report.failed_requests == 1);
    EXPECT(report.total_attempts == 3);
    EXPECT(report.retry_attempts == 2);
    EXPECT(report.dead_letters[0].request.request_id == "REQ-INV");
    EXPECT(report.dead_letters[0].request.trace_id == "TRACE-DLQ");
    EXPECT(report.dead_letters[0].attempts == 3);
    EXPECT(report.dead_letters[0].final_status == incidentlab::ServiceStatus::Failed);
    EXPECT(report.dead_letters[0].failed_at_ms == 310);
    EXPECT(report.dead_letters[0].reason == "Injected service failure");
    EXPECT(report.history.size() == report.event_times.size());
}

INCIDENTLAB_TEST(successful_recovery_creates_no_dlq_entry) {
    const incidentlab::SimulationReport report = incidentlab::run_stage5_demo().retry;
    EXPECT(report.dead_lettered_requests == 0);
    EXPECT(report.dead_letters.empty());
    EXPECT(report.successful_requests == 1);
}

INCIDENTLAB_TEST(dlq_does_not_contain_duplicate_logical_requests) {
    ScriptedService payments{"PaymentService", {Status::Failed, Status::Failed}};
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_retry("PaymentService", incidentlab::RetryConfig{2, 5, 2.0});
    engine.schedule_request(make_request("REQ-1", "PaymentService", "ProcessPayment"), 0);
    engine.schedule_request(make_request("REQ-2", "PaymentService", "ProcessPayment"), 50);
    engine.run();
    EXPECT(engine.dead_letters().size() == 2);
    EXPECT(engine.dead_letters().entries()[0].request.request_id == "REQ-1");
    EXPECT(engine.dead_letters().entries()[1].request.request_id == "REQ-2");
}

INCIDENTLAB_TEST(retry_stops_when_the_failure_opens_the_circuit) {
    ScriptedService payments{"PaymentService", {Status::Failed, Status::Failed}};
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_retry("PaymentService", incidentlab::RetryConfig{3, 100, 2.0});
    engine.configure_circuit_breaker("PaymentService", incidentlab::CircuitBreakerConfig{1, 500});
    engine.schedule_request(make_request("REQ-1", "PaymentService", "ProcessPayment"), 10);
    engine.run();
    EXPECT(payments.calls() == 1);
    EXPECT(engine.report().retry_attempts == 0);
    EXPECT(engine.report().failed_requests == 1);
    EXPECT(engine.report().dead_lettered_requests == 1);
    EXPECT(engine.circuit_state("PaymentService") == incidentlab::CircuitState::Open);
    EXPECT(!has_type(engine.report(), incidentlab::EventType::RetryScheduled));
}

INCIDENTLAB_TEST(a_later_retry_is_blocked_by_the_open_circuit) {
    ScriptedService payments{"PaymentService", {Status::Failed, Status::Failed, Status::Failed}};
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_retry("PaymentService", incidentlab::RetryConfig{3, 100, 2.0});
    engine.configure_circuit_breaker("PaymentService", incidentlab::CircuitBreakerConfig{2, 500});
    engine.schedule_request(make_request("REQ-1", "PaymentService", "ProcessPayment"), 10);
    engine.schedule_request(make_request("REQ-2", "PaymentService", "ProcessPayment"), 50);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    EXPECT(report.retry_attempts == 1);
    EXPECT(report.circuit_rejected_requests == 1);
    EXPECT(report.failed_requests == 1);
    EXPECT(report.dead_lettered_requests == 2);
    EXPECT(report.dead_letters[0].request.request_id == "REQ-2");
    EXPECT(report.dead_letters[0].final_status == incidentlab::ServiceStatus::Failed);
    EXPECT(report.dead_letters[1].request.request_id == "REQ-1");
    EXPECT(report.dead_letters[1].attempts == 1);
    EXPECT(report.dead_letters[1].final_status == incidentlab::ServiceStatus::CircuitRejected);
}

INCIDENTLAB_TEST(retry_backoff_overflow_leaves_the_start_queued) {
    ScriptedService payments{"PaymentService", {Status::Failed}};
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    const auto half = std::uint64_t{1} << 63;
    engine.configure_retry("PaymentService", incidentlab::RetryConfig{2, half, 2.0});
    engine.schedule_request(make_request("REQ-1", "PaymentService", "ProcessPayment"), half);
    expect_throws<std::overflow_error>([&engine] { engine.run(); });
    EXPECT(engine.report().pending_requests == 1);
    EXPECT(engine.history().size() == 0);
    EXPECT(engine.clock().now() == 0);
    EXPECT(engine.dead_letters().size() == 0);
    EXPECT(payments.calls() == 1);
}

INCIDENTLAB_TEST(equal_timestamp_retry_keeps_sequence_order) {
    ScriptedService payments{"PaymentService", {Status::Failed, Status::Success}};
    incidentlab::OrderService orders;
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.register_service(orders);
    engine.configure_retry("PaymentService", incidentlab::RetryConfig{2, 0, 2.0});
    engine.schedule_request(make_request("REQ-A", "PaymentService", "ProcessPayment"), 20);
    engine.schedule_request(make_request("REQ-B", "OrderService", "CreateOrder"), 20);
    engine.run();
    std::vector<std::string> received;
    const incidentlab::SimulationReport report = engine.report();
    for (const incidentlab::Event& event : report.history.events()) {
        if (event.type == incidentlab::EventType::RequestReceived) {
            received.push_back(event.request_id);
        }
    }
    EXPECT(received.size() == 3);
    EXPECT(received[0] == "REQ-A");
    EXPECT(received[1] == "REQ-B");
    EXPECT(received[2] == "REQ-A");
}

INCIDENTLAB_TEST(unexpected_exception_is_not_retried) {
    ThrowingService throwing;
    incidentlab::SimulationEngine engine;
    engine.register_service(throwing);
    engine.configure_retry("ThrowingService", incidentlab::RetryConfig{3, 10, 2.0});
    engine.configure_circuit_breaker("ThrowingService", incidentlab::CircuitBreakerConfig{1, 50});
    engine.schedule_request(make_request("REQ-1", "ThrowingService", "Charge"), 10);
    expect_throws<std::runtime_error>([&engine] { engine.run(); });
    EXPECT(engine.report().pending_requests == 1);
    EXPECT(engine.history().size() == 0);
    EXPECT(engine.history().size() == engine.report().event_times.size());
    EXPECT(engine.dead_letters().size() == 0);
    EXPECT(engine.circuit_state("ThrowingService") == incidentlab::CircuitState::Closed);
    EXPECT(engine.clock().now() == 0);
}

INCIDENTLAB_TEST(run_next_zero_does_not_start_a_retry_policy) {
    ScriptedService payments{"PaymentService", {Status::Failed}};
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_retry("PaymentService", incidentlab::RetryConfig{3, 10, 2.0});
    engine.schedule_request(make_request("REQ-1", "PaymentService", "ProcessPayment"), 10);
    EXPECT(engine.run_next(0) == 0);
    EXPECT(engine.history().size() == 0);
    EXPECT(engine.report().pending_requests == 1);
    EXPECT(engine.clock().now() == 0);
}

INCIDENTLAB_TEST(zero_delay_retries_stay_at_the_same_time) {
    ScriptedService payments{"PaymentService", {Status::Failed, Status::Failed, Status::Success}};
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_retry("PaymentService", incidentlab::RetryConfig{3, 0, 2.0});
    engine.schedule_request(make_request("REQ-1", "PaymentService", "ProcessPayment"), 10);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    EXPECT(report.current_time == 10);
    EXPECT(report.total_attempts == 3);
    EXPECT(report.successful_requests == 1);
    for (const auto time : report.event_times) {
        EXPECT(time == 10);
    }
}

INCIDENTLAB_TEST(stage5_circuit_demo_reaches_half_open_then_closed) {
    const incidentlab::SimulationReport report = incidentlab::run_stage5_demo().circuit_breaker;
    EXPECT(count_type(report, incidentlab::EventType::CircuitOpened) == 1);
    EXPECT(count_type(report, incidentlab::EventType::CircuitHalfOpened) == 1);
    EXPECT(count_type(report, incidentlab::EventType::CircuitClosed) == 1);
    EXPECT(report.circuit_rejected_requests == 1);
    EXPECT(report.successful_requests == 1);
    EXPECT(report.current_time == 530);
    EXPECT(report.history.size() == report.event_times.size());
}

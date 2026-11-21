#define INCIDENTLAB_TEST_SUITE "quality"
#include "test_harness.hpp"

#include "incidentlab/inventory_service.hpp"
#include "incidentlab/observability/metrics_calculator.hpp"
#include "incidentlab/order_service.hpp"
#include "incidentlab/payment_service.hpp"
#include "incidentlab/scenarios/comparison_report.hpp"
#include "incidentlab/scenarios/scenario_runner.hpp"
#include "incidentlab/simulation/circuit_breaker.hpp"
#include "incidentlab/simulation/event_scheduler.hpp"
#include "incidentlab/simulation/retry_config.hpp"
#include "incidentlab/simulation/simulation_clock.hpp"
#include "incidentlab/simulation/simulation_engine.hpp"

#include <chrono>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

template <typename Function>
void expect_exception(Function&& function) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw incidentlab_test::Failure("expected an exception");
}

incidentlab::Request make_request(const std::string& request_id, const std::string& trace_id,
                                  const std::string& target, const std::string& operation) {
    return incidentlab::Request{request_id, trace_id, "client", target, operation};
}

std::size_t terminal_sum(const incidentlab::OutcomeCounts& counts) {
    return counts.successful + counts.rejected + counts.failed + counts.timed_out + counts.dropped +
           counts.circuit_rejected;
}

}  // namespace

INCIDENTLAB_TEST(clock_reaches_the_maximum_timestamp) {
    incidentlab::SimulationClock clock;
    const auto maximum = std::numeric_limits<incidentlab::SimulationClock::Time>::max();
    clock.advance_to(maximum);
    EXPECT(clock.now() == maximum);
    clock.advance_to(maximum);
    EXPECT(clock.now() == maximum);
    expect_exception([&clock] { clock.advance_to(maximum - 1); });
    EXPECT(clock.now() == maximum);
}

INCIDENTLAB_TEST(new_clock_starts_at_zero_without_resetting_another) {
    incidentlab::SimulationClock advanced;
    advanced.advance_to(40);
    const incidentlab::SimulationClock fresh;
    EXPECT(fresh.now() == 0);
    EXPECT(advanced.now() == 40);
}

INCIDENTLAB_TEST(scheduler_is_empty_after_the_last_event) {
    incidentlab::EventScheduler scheduler;
    scheduler.schedule(make_request("REQ-1", "TRACE-Q", "OrderService", "CreateOrder"), 5, 0);
    EXPECT(scheduler.pending_count() == 1);
    const auto only = scheduler.pop_next();
    EXPECT(only.has_value());
    EXPECT(only->request.request_id == "REQ-1");
    EXPECT(only->request.trace_id == "TRACE-Q");
    EXPECT(only->request.target_service == "OrderService");
    EXPECT(!scheduler.has_pending());
    EXPECT(!scheduler.pop_next().has_value());
    expect_exception([&scheduler] { (void)scheduler.next(); });
}

INCIDENTLAB_TEST(scheduler_orders_an_event_added_after_a_pop) {
    incidentlab::EventScheduler scheduler;
    scheduler.schedule(make_request("REQ-A", "TRACE-Q", "OrderService", "CreateOrder"), 10, 0);
    scheduler.schedule(make_request("REQ-B", "TRACE-Q", "PaymentService", "ProcessPayment"), 40, 0);
    EXPECT(scheduler.pop_next()->request.request_id == "REQ-A");
    scheduler.schedule(make_request("REQ-C", "TRACE-Q", "InventoryService", "ReserveInventory"), 20,
                       0);
    scheduler.schedule(make_request("REQ-D", "TRACE-Q", "OrderService", "CreateOrder"), 40, 0);
    EXPECT(scheduler.pop_next()->request.request_id == "REQ-C");
    EXPECT(scheduler.pop_next()->request.request_id == "REQ-B");
    EXPECT(scheduler.pop_next()->request.request_id == "REQ-D");
    EXPECT(scheduler.pending_count() == 0);
}

INCIDENTLAB_TEST(scheduler_discard_next_on_an_empty_queue_is_safe) {
    incidentlab::EventScheduler scheduler;
    scheduler.discard_next();
    EXPECT(scheduler.pending_count() == 0);
    scheduler.schedule(make_request("REQ-1", "TRACE-Q", "OrderService", "CreateOrder"), 1, 0);
    scheduler.schedule(make_request("REQ-2", "TRACE-Q", "OrderService", "CreateOrder"), 2, 0);
    scheduler.discard_next();
    EXPECT(scheduler.pending_count() == 1);
    EXPECT(scheduler.next().request.request_id == "REQ-2");
}

INCIDENTLAB_TEST(replacing_a_failure_policy_discards_the_previous_type) {
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_failure("PaymentService",
                             incidentlab::FailureConfig{incidentlab::FailureType::Timeout, 200, 50, 1});
    engine.configure_failure(
        "PaymentService",
        incidentlab::FailureConfig{incidentlab::FailureType::ServiceFailure, 0, 0, 1});
    engine.schedule_request(make_request("REQ-1", "TRACE-Q", "PaymentService", "ProcessPayment"), 10);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    EXPECT(report.failed_requests == 1);
    EXPECT(report.timed_out_requests == 0);
    EXPECT(report.history.events()[1].type == incidentlab::EventType::RequestFailed);
    EXPECT(report.event_times[0] == 10);
    EXPECT(report.event_times[1] == 10);
    EXPECT(report.current_time == 10);
}

INCIDENTLAB_TEST(terminal_categories_partition_completed_requests) {
    incidentlab::OrderService orders;
    incidentlab::PaymentService payments;
    incidentlab::InventoryService inventory;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.register_service(payments);
    engine.register_service(inventory);
    engine.configure_failure("PaymentService",
                             incidentlab::FailureConfig{incidentlab::FailureType::Timeout, 200, 40, 1});
    engine.configure_failure(
        "InventoryService",
        incidentlab::FailureConfig{incidentlab::FailureType::DroppedRequest, 0, 0, 1});
    engine.schedule_request(make_request("REQ-OK", "TRACE-PART", "OrderService", "CreateOrder"), 0);
    engine.schedule_request(make_request("REQ-BAD", "TRACE-PART", "OrderService", "ShipOrder"), 5);
    engine.schedule_request(make_request("REQ-TIME", "TRACE-PART", "PaymentService", "ProcessPayment"),
                            10);
    engine.schedule_request(
        make_request("REQ-DROP", "TRACE-PART", "InventoryService", "ReserveInventory"), 15);
    engine.run();

    const incidentlab::ObservabilityReport metrics =
        incidentlab::MetricsCalculator::calculate(engine.report());
    const incidentlab::OutcomeCounts& counts = metrics.global.outcomes;
    EXPECT(counts.successful == 1);
    EXPECT(counts.rejected == 1);
    EXPECT(counts.failed == 0);
    EXPECT(counts.timed_out == 1);
    EXPECT(counts.dropped == 1);
    EXPECT(counts.circuit_rejected == 0);
    EXPECT(counts.completed == 4);
    EXPECT(counts.completed == terminal_sum(counts));
    EXPECT(counts.completed != counts.successful + counts.failed + counts.circuit_rejected);
    EXPECT(metrics.global.success_rate_percent == 25.0);
    EXPECT(metrics.global.failure_rate_percent == 75.0);
    EXPECT(engine.report().executions[0].request_id == "REQ-OK");
    EXPECT(engine.report().executions[0].trace_id == "TRACE-PART");
    EXPECT(engine.report().executions[0].service_name == "OrderService");
}

INCIDENTLAB_TEST(failure_rate_counts_every_unsuccessful_outcome) {
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_failure(
        "PaymentService",
        incidentlab::FailureConfig{incidentlab::FailureType::ServiceFailure, 0, 0, 1});
    engine.configure_circuit_breaker("PaymentService", incidentlab::CircuitBreakerConfig{1, 500});
    engine.schedule_request(make_request("REQ-FAIL", "TRACE-Q", "PaymentService", "ProcessPayment"), 0);
    engine.schedule_request(make_request("REQ-BLOCK", "TRACE-Q", "PaymentService", "ProcessPayment"),
                            10);
    engine.run();
    const incidentlab::ObservabilityReport metrics =
        incidentlab::MetricsCalculator::calculate(engine.report());
    const incidentlab::OutcomeCounts& counts = metrics.global.outcomes;
    EXPECT(counts.completed == 2);
    EXPECT(counts.failed == 1);
    EXPECT(counts.circuit_rejected == 1);
    EXPECT(counts.successful == 0);
    EXPECT(counts.completed == terminal_sum(counts));
    EXPECT(metrics.global.success_rate_percent == 0.0);
    EXPECT(metrics.global.failure_rate_percent == 100.0);
    EXPECT(counts.failed != counts.completed);
}

INCIDENTLAB_TEST(service_failure_without_resilience_skips_the_dlq) {
    incidentlab::InventoryService inventory;
    incidentlab::SimulationEngine engine;
    engine.register_service(inventory);
    engine.configure_failure(
        "InventoryService",
        incidentlab::FailureConfig{incidentlab::FailureType::ServiceFailure, 0, 0, 1});
    engine.schedule_request(
        make_request("REQ-1", "TRACE-Q", "InventoryService", "ReserveInventory"), 20);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    EXPECT(report.failed_requests == 1);
    EXPECT(report.dead_lettered_requests == 0);
    EXPECT(report.dead_letters.empty());
    EXPECT(engine.dead_letters().size() == 0);
    EXPECT(report.history.size() == 2);
    EXPECT(report.history.events()[1].type == incidentlab::EventType::RequestFailed);
}

INCIDENTLAB_TEST(separate_runs_do_not_share_dead_letters) {
    incidentlab::PaymentService first_payments;
    incidentlab::SimulationEngine first;
    first.register_service(first_payments);
    first.configure_failure(
        "PaymentService",
        incidentlab::FailureConfig{incidentlab::FailureType::ServiceFailure, 0, 0, 1});
    first.configure_retry("PaymentService", incidentlab::RetryConfig{2, 10, 2.0});
    first.schedule_request(make_request("REQ-1", "TRACE-A", "PaymentService", "ProcessPayment"), 0);
    first.run();
    EXPECT(first.dead_letters().size() == 1);

    incidentlab::PaymentService second_payments;
    incidentlab::SimulationEngine second;
    second.register_service(second_payments);
    second.schedule_request(make_request("REQ-2", "TRACE-B", "PaymentService", "ProcessPayment"), 0);
    second.run();
    EXPECT(second.dead_letters().size() == 0);
    EXPECT(second.report().successful_requests == 1);
    EXPECT(first.dead_letters().size() == 1);
}

INCIDENTLAB_TEST(dead_letter_preserves_request_and_trace_ids) {
    incidentlab::PaymentService payments;
    incidentlab::SimulationEngine engine;
    engine.register_service(payments);
    engine.configure_failure(
        "PaymentService",
        incidentlab::FailureConfig{incidentlab::FailureType::ServiceFailure, 0, 0, 1});
    engine.configure_retry("PaymentService", incidentlab::RetryConfig{2, 25, 2.0});
    engine.schedule_request(make_request("REQ-DLQ", "TRACE-DLQ", "PaymentService", "ProcessPayment"),
                            5);
    engine.run();
    const incidentlab::SimulationReport report = engine.report();
    EXPECT(report.processed_requests == 1);
    EXPECT(report.retry_attempts == 1);
    EXPECT(report.dead_letters.size() == 1);
    EXPECT(report.dead_letters[0].request.request_id == "REQ-DLQ");
    EXPECT(report.dead_letters[0].request.trace_id == "TRACE-DLQ");
    EXPECT(report.dead_letters[0].attempts == 2);
    EXPECT(report.executions.size() == 1);
    std::size_t completed = 0;
    std::size_t scheduled = 0;
    for (const incidentlab::Event& event : report.history.events()) {
        EXPECT(event.request_id == "REQ-DLQ");
        EXPECT(event.trace_id == "TRACE-DLQ");
        EXPECT(event.service_name == "PaymentService");
        if (event.type == incidentlab::EventType::RequestCompleted) {
            ++completed;
        }
        if (event.type == incidentlab::EventType::RetryScheduled) {
            ++scheduled;
        }
    }
    EXPECT(completed == 0);
    EXPECT(scheduled == 1);
}

INCIDENTLAB_TEST(circuit_reset_boundary_is_exact) {
    incidentlab::CircuitBreaker breaker{incidentlab::CircuitBreakerConfig{1, 100}};
    EXPECT(breaker.state() == incidentlab::CircuitState::Closed);
    EXPECT(breaker.admission(0) == incidentlab::CircuitAdmission::Allow);
    EXPECT(breaker.note_failure(10, false));
    EXPECT(breaker.state() == incidentlab::CircuitState::Open);
    EXPECT(breaker.admission(109) == incidentlab::CircuitAdmission::Block);
    EXPECT(breaker.admission(110) == incidentlab::CircuitAdmission::Probe);
    breaker.begin_probe();
    EXPECT(breaker.state() == incidentlab::CircuitState::HalfOpen);
    EXPECT(breaker.probe_in_flight());
    EXPECT(breaker.admission(110) == incidentlab::CircuitAdmission::Block);
    EXPECT(breaker.note_success(true));
    EXPECT(breaker.state() == incidentlab::CircuitState::Closed);
    EXPECT(!breaker.probe_in_flight());

    incidentlab::CircuitBreaker reopened{incidentlab::CircuitBreakerConfig{1, 100}};
    EXPECT(reopened.note_failure(0, false));
    reopened.begin_probe();
    EXPECT(reopened.note_failure(100, true));
    EXPECT(reopened.state() == incidentlab::CircuitState::Open);
    EXPECT(reopened.admission(199) == incidentlab::CircuitAdmission::Block);
    EXPECT(reopened.admission(200) == incidentlab::CircuitAdmission::Probe);

    incidentlab::CircuitBreaker other{incidentlab::CircuitBreakerConfig{2, 100}};
    EXPECT(other.state() == incidentlab::CircuitState::Closed);
    EXPECT(other.admission(0) == incidentlab::CircuitAdmission::Allow);
}

INCIDENTLAB_TEST(retry_backoff_boundaries_match_the_formula) {
    const incidentlab::RetryConfig doubling{3, 100, 2.0};
    EXPECT(incidentlab::retry_backoff_ms(doubling, 1) == 100);
    EXPECT(incidentlab::retry_backoff_ms(doubling, 2) == 200);
    EXPECT(incidentlab::retry_backoff_ms(doubling, 3) == 400);
    const incidentlab::RetryConfig flat{4, 50, 1.0};
    EXPECT(incidentlab::retry_backoff_ms(flat, 1) == 50);
    EXPECT(incidentlab::retry_backoff_ms(flat, 4) == 50);
    expect_exception([&doubling] { (void)incidentlab::retry_backoff_ms(doubling, 0); });
    const incidentlab::RetryConfig overflowing{2, std::numeric_limits<std::uint64_t>::max(), 2.0};
    expect_exception([&overflowing] { (void)incidentlab::retry_backoff_ms(overflowing, 2); });
}

INCIDENTLAB_TEST(slow_response_does_not_sleep) {
    incidentlab::OrderService orders;
    incidentlab::SimulationEngine engine;
    engine.register_service(orders);
    engine.configure_failure(
        "OrderService", incidentlab::FailureConfig{incidentlab::FailureType::SlowResponse, 60000, 0, 1});
    engine.schedule_request(make_request("REQ-1", "TRACE-Q", "OrderService", "CreateOrder"), 0);
    const auto started = std::chrono::steady_clock::now();
    engine.run();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    EXPECT(elapsed < std::chrono::seconds(5));
    EXPECT(engine.clock().now() == 60000);
    EXPECT(engine.report().successful_requests == 1);
    EXPECT(engine.report().executions[0].simulation_time == 60000);
}

INCIDENTLAB_TEST(metrics_copy_outlives_the_engine_and_services) {
    incidentlab::ObservabilityReport metrics;
    {
        struct Session {
            incidentlab::OrderService orders;
            incidentlab::SimulationEngine engine;
            Session() { engine.register_service(orders); }
        };
        Session session;
        session.engine.schedule_request(
            make_request("REQ-1", "TRACE-LIFE", "OrderService", "CreateOrder"), 8);
        session.engine.run();
        metrics = incidentlab::MetricsCalculator::calculate(session.engine.report());
    }
    EXPECT(metrics.global.outcomes.completed == 1);
    EXPECT(metrics.global.outcomes.successful == 1);
    EXPECT(metrics.global.latency.average_ms == 0.0);
    EXPECT(metrics.timeline.size() == 2);
    EXPECT(metrics.timeline[0].request_id == "REQ-1");
    EXPECT(metrics.timeline[0].trace_id == "TRACE-LIFE");
    EXPECT(metrics.timeline[0].service_name == "OrderService");
    EXPECT(metrics.services.size() == 1);
    EXPECT(metrics.services[0].service_name == "OrderService");
}

INCIDENTLAB_TEST(copied_scenario_result_keeps_the_baseline) {
    incidentlab::ComparisonReport report = incidentlab::run_predefined_comparison();
    const incidentlab::ScenarioResult* found = incidentlab::find_scenario(report, "baseline");
    EXPECT(found != nullptr);
    const incidentlab::ScenarioResult baseline = *found;
    report = incidentlab::ComparisonReport{};
    EXPECT(report.scenarios.empty());
    EXPECT(baseline.config.id == "baseline");
    EXPECT(baseline.metrics.global.outcomes.completed == 10);
    EXPECT(baseline.metrics.global.outcomes.successful == 4);
    EXPECT(baseline.metrics.global.success_rate_percent == 40.0);
    EXPECT(baseline.metrics.global.failure_rate_percent == 60.0);
    EXPECT(baseline.simulation_end_time_ms == 1020);
}

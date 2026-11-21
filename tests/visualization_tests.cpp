#include "test_harness.hpp"

#include "incidentlab/service.hpp"
#include "incidentlab/simulation/simulation_engine.hpp"
#include "incidentlab/visualization/simulation_controller.hpp"
#include "incidentlab/visualization/visualization_model.hpp"

#include <cmath>
#include <string>

namespace {

const incidentlab::ServiceNodeView& node_named(const incidentlab::VisualizationSnapshot& snapshot,
                                               const char* name) {
    for (const incidentlab::ServiceNodeView& node : snapshot.services) {
        if (node.name == name) {
            return node;
        }
    }
    EXPECT(false);
    return snapshot.services.front();
}

const incidentlab::ServiceMetrics& metrics_named(const incidentlab::ObservabilityReport& report,
                                                 const char* name) {
    for (const incidentlab::ServiceMetrics& service : report.services) {
        if (service.service_name == name) {
            return service;
        }
    }
    EXPECT(false);
    return report.services.front();
}

const incidentlab::FlowEdge& flow_to(const incidentlab::VisualizationSnapshot& snapshot,
                                     const char* target) {
    for (const incidentlab::FlowEdge& edge : snapshot.flows) {
        if (edge.target_service == target) {
            return edge;
        }
    }
    EXPECT(false);
    return snapshot.flows.front();
}

bool timeline_has(const incidentlab::ObservabilityReport& report, const char* request,
                  incidentlab::EventType type) {
    for (const incidentlab::TimelineEntry& entry : report.timeline) {
        if (entry.request_id == request && entry.type == type) {
            return true;
        }
    }
    return false;
}

void step_times(incidentlab::SimulationController& controller, int count) {
    for (int index = 0; index < count; ++index) {
        EXPECT(controller.step());
    }
}

class SlowService final : public incidentlab::Service {
public:
    std::string name() const override { return "SlowService"; }
    incidentlab::ServiceResult handle(const incidentlab::Request&) override {
        return incidentlab::ServiceResult{incidentlab::ServiceStatus::Success, "ok"};
    }
};

}  // namespace

INCIDENTLAB_TEST(initial_state_is_ready) {
    const incidentlab::SimulationController controller;
    EXPECT(controller.state() == incidentlab::PlaybackState::Ready);
    EXPECT(controller.snapshot().playback == incidentlab::PlaybackState::Ready);
    EXPECT(controller.snapshot().simulation_time_ms == 0);
    EXPECT(controller.snapshot().metrics.global.outcomes.completed == 0);
    EXPECT(controller.snapshot().metrics.timeline.empty());
}

INCIDENTLAB_TEST(start_changes_ready_to_running_without_executing) {
    incidentlab::SimulationController controller;
    controller.start();
    EXPECT(controller.state() == incidentlab::PlaybackState::Running);
    EXPECT(controller.snapshot().simulation_time_ms == 0);
    EXPECT(controller.snapshot().metrics.global.outcomes.completed == 0);
    EXPECT(controller.snapshot().metrics.timeline.empty());
    controller.start();
    EXPECT(controller.state() == incidentlab::PlaybackState::Running);
    EXPECT(controller.snapshot().metrics.timeline.empty());
}

INCIDENTLAB_TEST(pause_changes_running_to_paused) {
    incidentlab::SimulationController controller;
    controller.start();
    controller.pause();
    EXPECT(controller.state() == incidentlab::PlaybackState::Paused);
    EXPECT(controller.snapshot().simulation_time_ms == 0);
}

INCIDENTLAB_TEST(pause_from_ready_does_not_change_state) {
    incidentlab::SimulationController controller;
    controller.pause();
    EXPECT(controller.state() == incidentlab::PlaybackState::Ready);
}

INCIDENTLAB_TEST(resume_continues_from_paused) {
    incidentlab::SimulationController controller;
    controller.start();
    EXPECT(controller.tick(0.5));
    EXPECT(controller.step());
    EXPECT(controller.snapshot().simulation_time_ms == 10);
    controller.pause();
    EXPECT(!controller.tick(10.0));
    EXPECT(controller.state() == incidentlab::PlaybackState::Paused);
    EXPECT(controller.snapshot().simulation_time_ms == 10);
    controller.resume();
    EXPECT(controller.state() == incidentlab::PlaybackState::Running);
    EXPECT(controller.tick(0.5));
    EXPECT(controller.snapshot().simulation_time_ms == 20);
    EXPECT(controller.state() == incidentlab::PlaybackState::Running);
}

INCIDENTLAB_TEST(resume_from_ready_does_not_start) {
    incidentlab::SimulationController controller;
    controller.resume();
    EXPECT(controller.state() == incidentlab::PlaybackState::Ready);
    EXPECT(!controller.tick(1.0));
}

INCIDENTLAB_TEST(step_runs_one_scheduler_action) {
    incidentlab::SimulationController controller;
    EXPECT(controller.step());
    const incidentlab::VisualizationSnapshot first = controller.snapshot();
    EXPECT(controller.state() == incidentlab::PlaybackState::Paused);
    EXPECT(first.simulation_time_ms == 0);
    EXPECT(first.metrics.global.outcomes.completed == 1);
    EXPECT(first.metrics.timeline.size() == 2);

    EXPECT(controller.step());
    const incidentlab::VisualizationSnapshot second = controller.snapshot();
    EXPECT(controller.state() == incidentlab::PlaybackState::Paused);
    EXPECT(second.simulation_time_ms == 10);
    EXPECT(second.metrics.global.outcomes.completed == 1);
    EXPECT(second.metrics.timeline.size() == 3);
    EXPECT(node_named(second, "OrderService").status == incidentlab::VisualServiceStatus::Processing);
}

INCIDENTLAB_TEST(step_while_running_stays_running) {
    incidentlab::SimulationController controller;
    controller.start();
    EXPECT(controller.step());
    EXPECT(controller.state() == incidentlab::PlaybackState::Running);
    EXPECT(controller.snapshot().metrics.global.outcomes.completed == 1);
}

INCIDENTLAB_TEST(step_does_not_duplicate_requests) {
    incidentlab::SimulationController controller;
    controller.start();
    controller.start();
    int steps = 0;
    while (controller.step()) {
        ++steps;
    }
    EXPECT(steps == 12);
    EXPECT(controller.snapshot().metrics.global.outcomes.completed == 8);
    EXPECT(controller.snapshot().metrics.global.retries.total_attempts == 9);
}

INCIDENTLAB_TEST(reset_restores_the_initial_scenario) {
    incidentlab::SimulationController controller;
    controller.set_speed(incidentlab::PlaybackSpeed::Quadruple);
    step_times(controller, 12);
    EXPECT(controller.select_service("PaymentService"));
    EXPECT(controller.select_event(controller.snapshot().metrics.timeline.front().event_id));
    controller.reset();
    const incidentlab::VisualizationSnapshot snapshot = controller.snapshot();
    EXPECT(controller.state() == incidentlab::PlaybackState::Ready);
    EXPECT(controller.speed() == incidentlab::PlaybackSpeed::Quadruple);
    EXPECT(snapshot.simulation_time_ms == 0);
    EXPECT(snapshot.metrics.global.outcomes.completed == 0);
    EXPECT(snapshot.metrics.global.outcomes.successful == 0);
    EXPECT(snapshot.metrics.global.retries.total_attempts == 0);
    EXPECT(snapshot.metrics.global.dead_lettered_requests == 0);
    EXPECT(snapshot.metrics.timeline.empty());
    EXPECT(!snapshot.selected_service.has_value());
    EXPECT(!snapshot.selected_event_id.has_value());
    EXPECT(snapshot.services.size() == 3);
    EXPECT(node_named(snapshot, "OrderService").status == incidentlab::VisualServiceStatus::Idle);
}

INCIDENTLAB_TEST(completed_state_is_detected_when_nothing_remains) {
    incidentlab::SimulationController controller;
    controller.start();
    step_times(controller, 11);
    EXPECT(controller.state() == incidentlab::PlaybackState::Running);
    EXPECT(controller.snapshot().simulation_time_ms == 530);
    EXPECT(controller.step());
    EXPECT(controller.state() == incidentlab::PlaybackState::Completed);
    EXPECT(!controller.step());
    EXPECT(!controller.tick(5.0));
    EXPECT(controller.state() == incidentlab::PlaybackState::Completed);
    controller.start();
    EXPECT(controller.state() == incidentlab::PlaybackState::Completed);
}

INCIDENTLAB_TEST(two_identical_runs_match) {
    incidentlab::SimulationController first;
    incidentlab::SimulationController second;
    step_times(first, 12);
    step_times(second, 12);
    const incidentlab::VisualizationSnapshot left = first.snapshot();
    const incidentlab::VisualizationSnapshot right = second.snapshot();
    EXPECT(left.simulation_time_ms == right.simulation_time_ms);
    EXPECT(left.metrics.timeline.size() == right.metrics.timeline.size());
    for (std::size_t index = 0; index < left.metrics.timeline.size(); ++index) {
        EXPECT(left.metrics.timeline[index].event_id == right.metrics.timeline[index].event_id);
        EXPECT(left.metrics.timeline[index].time_ms == right.metrics.timeline[index].time_ms);
        EXPECT(left.metrics.timeline[index].type == right.metrics.timeline[index].type);
        EXPECT(left.metrics.timeline[index].request_id == right.metrics.timeline[index].request_id);
    }
    EXPECT(left.metrics.global.outcomes.successful == right.metrics.global.outcomes.successful);
    EXPECT(left.metrics.global.latency.p95_ms == right.metrics.global.latency.p95_ms);
}

INCIDENTLAB_TEST(reset_and_replay_match_a_fresh_run) {
    incidentlab::SimulationController replayed;
    step_times(replayed, 6);
    replayed.reset();
    step_times(replayed, 12);
    incidentlab::SimulationController fresh;
    step_times(fresh, 12);
    const incidentlab::VisualizationSnapshot left = replayed.snapshot();
    const incidentlab::VisualizationSnapshot right = fresh.snapshot();
    EXPECT(left.metrics.timeline.size() == right.metrics.timeline.size());
    EXPECT(left.simulation_time_ms == right.simulation_time_ms);
    EXPECT(left.metrics.global.dead_lettered_requests == right.metrics.global.dead_lettered_requests);
    for (std::size_t index = 0; index < left.metrics.timeline.size(); ++index) {
        EXPECT(left.metrics.timeline[index].event_id == right.metrics.timeline[index].event_id);
        EXPECT(left.metrics.timeline[index].message == right.metrics.timeline[index].message);
    }
}

INCIDENTLAB_TEST(playback_speed_does_not_change_outcomes) {
    incidentlab::SimulationController fast;
    incidentlab::SimulationController slow;
    fast.set_speed(incidentlab::PlaybackSpeed::Quadruple);
    slow.set_speed(incidentlab::PlaybackSpeed::Half);
    EXPECT(fast.playback_interval_seconds() == 0.125);
    EXPECT(slow.playback_interval_seconds() == 1.0);
    step_times(fast, 12);
    step_times(slow, 12);
    EXPECT(fast.snapshot().simulation_time_ms == slow.snapshot().simulation_time_ms);
    EXPECT(fast.snapshot().metrics.global.outcomes.completed ==
           slow.snapshot().metrics.global.outcomes.completed);
    EXPECT(fast.snapshot().metrics.global.latency.average_ms ==
           slow.snapshot().metrics.global.latency.average_ms);
    EXPECT(fast.snapshot().speed == incidentlab::PlaybackSpeed::Quadruple);
    EXPECT(slow.snapshot().speed == incidentlab::PlaybackSpeed::Half);
}

INCIDENTLAB_TEST(playback_tick_uses_frame_time_and_steps_once) {
    incidentlab::SimulationController normal;
    normal.start();
    EXPECT(!normal.tick(0.4));
    EXPECT(normal.snapshot().metrics.global.outcomes.completed == 0);
    EXPECT(normal.tick(0.1));
    EXPECT(normal.snapshot().metrics.global.outcomes.completed == 1);
    EXPECT(!normal.tick(0.0));
    EXPECT(!normal.tick(-2.0));
    EXPECT(normal.snapshot().metrics.global.outcomes.completed == 1);

    incidentlab::SimulationController rapid;
    rapid.set_speed(incidentlab::PlaybackSpeed::Quadruple);
    rapid.start();
    EXPECT(!rapid.tick(0.1));
    EXPECT(rapid.tick(0.025));
    EXPECT(rapid.snapshot().metrics.global.outcomes.completed == 1);
    EXPECT(rapid.snapshot().simulation_time_ms == 0);
}

INCIDENTLAB_TEST(pause_does_not_advance_virtual_time) {
    incidentlab::SimulationController controller;
    controller.start();
    controller.tick(0.49);
    controller.pause();
    controller.resume();
    EXPECT(!controller.tick(0.1));
    EXPECT(controller.snapshot().simulation_time_ms == 0);
    EXPECT(controller.snapshot().metrics.timeline.empty());
}

INCIDENTLAB_TEST(step_order_is_deterministic) {
    incidentlab::SimulationController controller;
    step_times(controller, 12);
    const std::vector<incidentlab::TimelineEntry>& timeline = controller.snapshot().metrics.timeline;
    EXPECT(timeline.front().event_id == "EVT-001");
    EXPECT(timeline.front().request_id == "REQ-O1");
    EXPECT(timeline.front().type == incidentlab::EventType::RequestReceived);
    EXPECT(timeline[1].event_id == "EVT-002");
    EXPECT(timeline[1].type == incidentlab::EventType::RequestCompleted);
    for (std::size_t index = 1; index < timeline.size(); ++index) {
        EXPECT(timeline[index].time_ms >= timeline[index - 1].time_ms);
        EXPECT(timeline[index].event_id > timeline[index - 1].event_id);
    }
}

INCIDENTLAB_TEST(service_nodes_reflect_registered_services) {
    const incidentlab::SimulationController controller;
    const incidentlab::VisualizationSnapshot snapshot = controller.snapshot();
    EXPECT(snapshot.services.size() == 3);
    EXPECT(snapshot.services[0].name == "InventoryService");
    EXPECT(snapshot.services[1].name == "OrderService");
    EXPECT(snapshot.services[2].name == "PaymentService");
    EXPECT(node_named(snapshot, "InventoryService").completed_requests == 0);
    EXPECT(node_named(snapshot, "InventoryService").status == incidentlab::VisualServiceStatus::Idle);
    EXPECT(snapshot.flows.size() == 3);
    EXPECT(flow_to(snapshot, "OrderService").source_service == "client");
    EXPECT(!flow_to(snapshot, "OrderService").observed);
}

INCIDENTLAB_TEST(service_ordering_stays_alphabetical) {
    incidentlab::SimulationController controller;
    step_times(controller, 12);
    const incidentlab::VisualizationSnapshot snapshot = controller.snapshot();
    EXPECT(snapshot.services[0].name == "InventoryService");
    EXPECT(snapshot.services[1].name == "OrderService");
    EXPECT(snapshot.services[2].name == "PaymentService");
    EXPECT(snapshot.metrics.services[0].service_name == "InventoryService");
    EXPECT(snapshot.metrics.services[1].service_name == "OrderService");
    EXPECT(snapshot.metrics.services[2].service_name == "PaymentService");
}

INCIDENTLAB_TEST(selected_service_remains_valid_across_steps) {
    incidentlab::SimulationController controller;
    EXPECT(controller.select_service("OrderService"));
    EXPECT(controller.snapshot().selected_service == "OrderService");
    EXPECT(node_named(controller.snapshot(), "OrderService").selected);
    EXPECT(!node_named(controller.snapshot(), "PaymentService").selected);
    controller.step();
    EXPECT(controller.snapshot().selected_service == "OrderService");
    EXPECT(node_named(controller.snapshot(), "OrderService").selected);
}

INCIDENTLAB_TEST(invalid_service_selection_is_ignored) {
    incidentlab::SimulationController controller;
    EXPECT(controller.select_service("OrderService"));
    EXPECT(!controller.select_service("UnknownService"));
    EXPECT(!controller.select_service(""));
    EXPECT(controller.snapshot().selected_service == "OrderService");
    EXPECT(!controller.select_event("EVT-999"));
    EXPECT(!controller.snapshot().selected_event_id.has_value());
}

INCIDENTLAB_TEST(circuit_breaker_states_are_not_invented) {
    incidentlab::SimulationController controller;
    const incidentlab::VisualizationSnapshot initial = controller.snapshot();
    EXPECT(!node_named(initial, "OrderService").circuit.has_value());
    EXPECT(!node_named(initial, "InventoryService").circuit.has_value());
    EXPECT(incidentlab::circuit_label(node_named(initial, "OrderService").circuit) ==
           "NOT CONFIGURED");
    EXPECT(node_named(initial, "PaymentService").circuit == incidentlab::CircuitState::Closed);
    EXPECT(incidentlab::circuit_label(node_named(initial, "PaymentService").circuit) == "CLOSED");

    step_times(controller, 4);
    EXPECT(node_named(controller.snapshot(), "PaymentService").circuit ==
           incidentlab::CircuitState::Open);
    EXPECT(node_named(controller.snapshot(), "PaymentService").status ==
           incidentlab::VisualServiceStatus::CircuitOpen);

    step_times(controller, 7);
    EXPECT(controller.snapshot().simulation_time_ms == 530);
    EXPECT(node_named(controller.snapshot(), "PaymentService").circuit ==
           incidentlab::CircuitState::HalfOpen);
    EXPECT(node_named(controller.snapshot(), "PaymentService").status ==
           incidentlab::VisualServiceStatus::Processing);
    EXPECT(incidentlab::circuit_label(node_named(controller.snapshot(), "PaymentService").circuit) ==
           "HALF-OPEN");

    EXPECT(controller.step());
    EXPECT(node_named(controller.snapshot(), "PaymentService").circuit ==
           incidentlab::CircuitState::Closed);
    EXPECT(node_named(controller.snapshot(), "PaymentService").status ==
           incidentlab::VisualServiceStatus::Healthy);
    EXPECT(incidentlab::circuit_label(std::nullopt) == "NOT CONFIGURED");
}

INCIDENTLAB_TEST(missing_metrics_display_as_unavailable) {
    const incidentlab::SimulationController controller;
    EXPECT(incidentlab::format_optional_metric(std::nullopt) == "N/A");
    EXPECT(incidentlab::format_optional_metric(std::optional<double>{std::nan("")}) == "N/A");
    EXPECT(!controller.snapshot().metrics.global.throughput_requests_per_second.has_value());
    EXPECT(incidentlab::format_optional_metric(
               controller.snapshot().metrics.global.throughput_requests_per_second) == "N/A");
    EXPECT(controller.snapshot().metrics.global.success_rate_percent == 0.0);
    EXPECT(controller.snapshot().metrics.global.failure_rate_percent == 0.0);
}

INCIDENTLAB_TEST(empty_simulation_snapshot_is_valid) {
    const incidentlab::SimulationReport report;
    const incidentlab::VisualizationSnapshot snapshot = incidentlab::build_visualization_snapshot(
        report, {"InventoryService", "OrderService"}, {}, incidentlab::PlaybackState::Ready,
        incidentlab::PlaybackSpeed::Normal, std::nullopt, std::nullopt);
    EXPECT(snapshot.services.size() == 2);
    EXPECT(snapshot.services[0].name == "InventoryService");
    EXPECT(snapshot.services[0].status == incidentlab::VisualServiceStatus::Idle);
    EXPECT(snapshot.services[0].completed_requests == 0);
    EXPECT(snapshot.flows.empty());
    EXPECT(snapshot.metrics.timeline.empty());
    EXPECT(snapshot.metrics.global.outcomes.completed == 0);
    EXPECT(incidentlab::timeline_for_selected_service(snapshot).empty());
    EXPECT(!incidentlab::selected_timeline_entry(snapshot).has_value());
}

INCIDENTLAB_TEST(timeline_is_chronological_and_keeps_equal_timestamps) {
    incidentlab::SimulationController controller;
    step_times(controller, 12);
    const std::vector<incidentlab::TimelineEntry>& timeline = controller.snapshot().metrics.timeline;
    EXPECT(timeline.size() == 28);
    bool saw_equal = false;
    for (std::size_t index = 1; index < timeline.size(); ++index) {
        EXPECT(timeline[index].time_ms >= timeline[index - 1].time_ms);
        if (timeline[index].time_ms == timeline[index - 1].time_ms) {
            saw_equal = true;
            EXPECT(timeline[index].event_id > timeline[index - 1].event_id);
        }
    }
    EXPECT(saw_equal);
    const std::vector<incidentlab::TimelineEntry> payment =
        incidentlab::timeline_for_request(controller.snapshot().metrics, "REQ-P2");
    EXPECT(payment.size() == 4);
    EXPECT(payment[0].type == incidentlab::EventType::RequestReceived);
    EXPECT(payment[1].type == incidentlab::EventType::RequestFailed);
    EXPECT(payment[2].type == incidentlab::EventType::CircuitOpened);
    EXPECT(payment[3].type == incidentlab::EventType::DeadLettered);
    EXPECT(payment[0].time_ms == 30);
    EXPECT(payment[3].time_ms == 30);
}

INCIDENTLAB_TEST(service_filter_and_correlation_are_preserved) {
    incidentlab::SimulationController controller;
    step_times(controller, 12);
    EXPECT(controller.select_service("PaymentService"));
    const std::vector<incidentlab::TimelineEntry> filtered =
        incidentlab::timeline_for_selected_service(controller.snapshot());
    EXPECT(!filtered.empty());
    for (const incidentlab::TimelineEntry& entry : filtered) {
        EXPECT(entry.service_name == "PaymentService");
        EXPECT(entry.trace_id == "TRACE-VIZ");
    }
    const std::vector<incidentlab::TimelineEntry> inventory =
        incidentlab::timeline_for_request(controller.snapshot().metrics, "REQ-I1");
    EXPECT(inventory.size() == 5);
    EXPECT(inventory[0].request_id == "REQ-I1");
    EXPECT(inventory[0].trace_id == "TRACE-VIZ");
    EXPECT(inventory[2].type == incidentlab::EventType::RetryScheduled);
    EXPECT(inventory[4].type == incidentlab::EventType::RequestCompleted);
    const std::vector<incidentlab::TimelineEntry> trace =
        incidentlab::timeline_for_trace(controller.snapshot().metrics, "TRACE-VIZ");
    EXPECT(trace.size() == controller.snapshot().metrics.timeline.size());
}

INCIDENTLAB_TEST(timeline_selection_is_stable) {
    incidentlab::SimulationController controller;
    EXPECT(controller.step());
    const std::string event_id = controller.snapshot().metrics.timeline.front().event_id;
    EXPECT(controller.select_event(event_id));
    controller.step();
    EXPECT(controller.snapshot().selected_event_id == event_id);
    const std::optional<incidentlab::TimelineEntry> selected =
        incidentlab::selected_timeline_entry(controller.snapshot());
    EXPECT(selected.has_value());
    EXPECT(selected->event_id == "EVT-001");
    EXPECT(selected->request_id == "REQ-O1");
    EXPECT(selected->trace_id == "TRACE-VIZ");
    EXPECT(selected->service_name == "OrderService");
    EXPECT(selected->time_ms == 0);
    EXPECT(!controller.select_event("EVT-404"));
    EXPECT(controller.snapshot().selected_event_id == event_id);
    controller.clear_event_selection();
    EXPECT(!controller.snapshot().selected_event_id.has_value());
    controller.reset();
    EXPECT(!controller.snapshot().selected_event_id.has_value());
}

INCIDENTLAB_TEST(retry_recovery_circuit_and_dlq_appear_in_the_snapshot) {
    incidentlab::SimulationController controller;
    step_times(controller, 5);
    EXPECT(controller.snapshot().metrics.global.outcomes.circuit_rejected == 1);
    EXPECT(timeline_has(controller.snapshot().metrics, "REQ-P3",
                        incidentlab::EventType::CircuitRejected));

    step_times(controller, 3);
    EXPECT(controller.snapshot().simulation_time_ms == 170);
    EXPECT(controller.snapshot().metrics.global.retries.recovered_after_retry == 1);
    EXPECT(timeline_has(controller.snapshot().metrics, "REQ-I1",
                        incidentlab::EventType::RequestCompleted));

    step_times(controller, 2);
    EXPECT(controller.snapshot().metrics.global.retries.exhausted_retries == 1);
    EXPECT(controller.snapshot().metrics.global.dead_lettered_requests == 4);
    EXPECT(controller.snapshot().metrics.dead_letters.size() == 4);
    EXPECT(controller.snapshot().metrics.dead_letters[0].request_id == "REQ-P1");
    EXPECT(controller.snapshot().metrics.dead_letters[1].request_id == "REQ-P2");
    EXPECT(controller.snapshot().metrics.dead_letters[2].request_id == "REQ-P3");
    EXPECT(controller.snapshot().metrics.dead_letters[3].request_id == "REQ-I2");
    EXPECT(controller.snapshot().metrics.dead_letters[2].trace_id == "TRACE-VIZ");
    EXPECT(controller.snapshot().metrics.dead_letters[2].attempts == 0);

    step_times(controller, 2);
    EXPECT(controller.snapshot().metrics.global.circuit_openings == 1);
    EXPECT(controller.snapshot().metrics.global.circuit_half_openings == 1);
    EXPECT(controller.snapshot().metrics.global.circuit_closings == 1);
    EXPECT(node_named(controller.snapshot(), "OrderService").status ==
           incidentlab::VisualServiceStatus::TimedOut);
    EXPECT(node_named(controller.snapshot(), "InventoryService").status ==
           incidentlab::VisualServiceStatus::Failed);
    EXPECT(flow_to(controller.snapshot(), "InventoryService").observed);
    EXPECT(flow_to(controller.snapshot(), "InventoryService").latest_request_id == "REQ-I2");
    EXPECT(flow_to(controller.snapshot(), "InventoryService").latest_trace_id == "TRACE-VIZ");
}

INCIDENTLAB_TEST(metrics_match_stage6_calculations) {
    incidentlab::SimulationController controller;
    step_times(controller, 12);
    const incidentlab::GlobalMetrics global = controller.snapshot().metrics.global;
    EXPECT(global.outcomes.completed == 8);
    EXPECT(global.outcomes.successful == 3);
    EXPECT(global.outcomes.failed == 3);
    EXPECT(global.outcomes.timed_out == 1);
    EXPECT(global.outcomes.dropped == 0);
    EXPECT(global.outcomes.circuit_rejected == 1);
    EXPECT(global.success_rate_percent == 37.5);
    EXPECT(global.failure_rate_percent == 62.5);
    EXPECT(global.retries.total_attempts == 9);
    EXPECT(global.retries.retry_attempts == 2);
    EXPECT(global.retries.requests_requiring_retry == 2);
    EXPECT(global.retries.recovered_after_retry == 1);
    EXPECT(global.retries.exhausted_retries == 1);
    EXPECT(global.circuit_openings == 1);
    EXPECT(global.circuit_half_openings == 1);
    EXPECT(global.circuit_closings == 1);
    EXPECT(global.dead_lettered_requests == 4);
    EXPECT(global.latency.average_ms == 42.5);
    EXPECT(global.latency.p50_ms == 0.0);
    EXPECT(global.latency.p95_ms == 100.0);
    EXPECT(global.latency.p99_ms == 100.0);
    EXPECT(global.observation_start_ms == 0);
    EXPECT(global.observation_end_ms == 630);
    EXPECT(global.throughput_requests_per_second == 8000.0 / 630.0);

    const incidentlab::VisualizationSnapshot snapshot = controller.snapshot();
    const incidentlab::ServiceMetrics order = metrics_named(snapshot.metrics, "OrderService");
    const incidentlab::ServiceMetrics payment = metrics_named(snapshot.metrics, "PaymentService");
    const incidentlab::ServiceMetrics inventory =
        metrics_named(snapshot.metrics, "InventoryService");
    EXPECT(order.outcomes.completed == 2);
    EXPECT(order.outcomes.successful == 1);
    EXPECT(order.outcomes.timed_out == 1);
    EXPECT(order.success_rate_percent == 50.0);
    EXPECT(order.latency.average_ms == 20.0);
    EXPECT(order.dead_lettered_requests == 0);
    EXPECT(payment.outcomes.completed == 4);
    EXPECT(payment.outcomes.failed == 2);
    EXPECT(payment.outcomes.circuit_rejected == 1);
    EXPECT(payment.success_rate_percent == 25.0);
    EXPECT(payment.failure_rate_percent == 75.0);
    EXPECT(payment.latency.p95_ms == 100.0);
    EXPECT(payment.circuit_openings == 1);
    EXPECT(payment.dead_lettered_requests == 3);
    EXPECT(inventory.outcomes.successful == 1);
    EXPECT(inventory.outcomes.failed == 1);
    EXPECT(inventory.retries.retry_attempts == 2);
    EXPECT(inventory.retries.recovered_after_retry == 1);
    EXPECT(inventory.retries.exhausted_retries == 1);
    EXPECT(inventory.latency.average_ms == 100.0);
    EXPECT(inventory.dead_lettered_requests == 1);
    EXPECT(!snapshot.metrics.circuits.empty());
    EXPECT(snapshot.metrics.circuits.back().service_name == "PaymentService");
    EXPECT(snapshot.metrics.circuits.back().final_state == incidentlab::CircuitState::Closed);
    EXPECT(snapshot.metrics.circuits.back().blocked_requests == 1);
}

INCIDENTLAB_TEST(step_once_does_not_change_run_next) {
    SlowService slow;
    incidentlab::SimulationEngine stepped;
    stepped.register_service(slow);
    stepped.configure_failure("SlowService",
                              incidentlab::FailureConfig{incidentlab::FailureType::SlowResponse, 50,
                                                         0, 1});
    stepped.schedule_request(
        incidentlab::Request{"REQ-S", "TRACE-S", "client", "SlowService", "Work"}, 0);
    EXPECT(stepped.step_once());
    const incidentlab::SimulationReport after_start = stepped.report();
    EXPECT(after_start.processed_requests == 0);
    EXPECT(after_start.in_progress_requests == 1);
    EXPECT(after_start.current_time == 0);
    EXPECT(stepped.step_once());
    EXPECT(stepped.report().processed_requests == 1);
    EXPECT(stepped.report().current_time == 50);
    EXPECT(!stepped.step_once());

    SlowService again;
    incidentlab::SimulationEngine logical;
    logical.register_service(again);
    logical.configure_failure("SlowService",
                              incidentlab::FailureConfig{incidentlab::FailureType::SlowResponse, 50,
                                                         0, 1});
    logical.schedule_request(
        incidentlab::Request{"REQ-S", "TRACE-S", "client", "SlowService", "Work"}, 0);
    EXPECT(logical.run_next(1) == 1);
    EXPECT(logical.report().processed_requests == 1);
    EXPECT(logical.report().in_progress_requests == 0);
    EXPECT(logical.report().current_time == 50);
    EXPECT(logical.run_next(0) == 0);
}

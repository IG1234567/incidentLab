#include "test_harness.hpp"

#include "incidentlab/event.hpp"
#include "incidentlab/scenarios/scenario_runner.hpp"
#include "incidentlab/visualization/simulation_controller.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

const incidentlab::ScenarioResult& by_id(const incidentlab::ComparisonReport& report,
                                         const std::string& id) {
    const incidentlab::ScenarioResult* found = incidentlab::find_scenario(report, id);
    EXPECT(found != nullptr);
    return *found;
}

std::size_t count_type(const incidentlab::ObservabilityReport& report, incidentlab::EventType type) {
    std::size_t count = 0;
    for (const incidentlab::TimelineEntry& entry : report.timeline) {
        if (entry.type == type) {
            ++count;
        }
    }
    return count;
}

bool same_signature(const incidentlab::ObservabilityReport& left,
                    const incidentlab::ObservabilityReport& right) {
    if (left.timeline.size() != right.timeline.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.timeline.size(); ++index) {
        const incidentlab::TimelineEntry& a = left.timeline[index];
        const incidentlab::TimelineEntry& b = right.timeline[index];
        if (a.time_ms != b.time_ms || a.type != b.type || a.request_id != b.request_id ||
            a.service_name != b.service_name) {
            return false;
        }
    }
    return left.global.outcomes.successful == right.global.outcomes.successful &&
           left.global.dead_lettered_requests == right.global.dead_lettered_requests &&
           left.global.retries.total_attempts == right.global.retries.total_attempts;
}

const incidentlab::ComparisonReport& cached() {
    static const incidentlab::ComparisonReport report = incidentlab::run_predefined_comparison();
    return report;
}

}  // namespace

INCIDENTLAB_TEST(valid_configuration_is_accepted) {
    const std::vector<incidentlab::ScenarioConfig> scenarios = incidentlab::predefined_scenarios();
    EXPECT(scenarios.size() == 4);
    incidentlab::validate_scenario_set(scenarios);
    EXPECT(scenarios[0].id == "baseline");
    EXPECT(scenarios[0].retry_enabled == false);
    EXPECT(scenarios[0].circuit_breaker_enabled == false);
    EXPECT(scenarios[1].retry_enabled == true);
    EXPECT(scenarios[1].circuit_breaker_enabled == false);
    EXPECT(scenarios[2].retry_enabled == false);
    EXPECT(scenarios[2].circuit_breaker_enabled == true);
    EXPECT(scenarios[3].retry_enabled == true);
    EXPECT(scenarios[3].circuit_breaker_enabled == true);
}

INCIDENTLAB_TEST(invalid_retry_parameters_are_rejected) {
    incidentlab::ScenarioConfig config = incidentlab::predefined_scenarios()[1];
    config.max_attempts = 0;
    bool threw = false;
    try {
        incidentlab::validate_scenario_config(config);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    EXPECT(threw);
}

INCIDENTLAB_TEST(invalid_circuit_breaker_parameters_are_rejected) {
    incidentlab::ScenarioConfig config = incidentlab::predefined_scenarios()[2];
    config.circuit_failure_threshold = 0;
    bool threw = false;
    try {
        incidentlab::validate_scenario_config(config);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    EXPECT(threw);
}

INCIDENTLAB_TEST(duplicate_scenario_ids_are_rejected) {
    std::vector<incidentlab::ScenarioConfig> scenarios = incidentlab::predefined_scenarios();
    scenarios.push_back(scenarios.front());
    bool threw = false;
    try {
        incidentlab::run_comparison(scenarios);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    EXPECT(threw);
}

INCIDENTLAB_TEST(empty_comparison_input_is_safe) {
    const incidentlab::ComparisonReport report = incidentlab::run_comparison({});
    EXPECT(report.scenarios.empty());
    const incidentlab::ScenarioDelta delta = incidentlab::delta_from_baseline(report, "full");
    EXPECT(delta.baseline_found == false);
    EXPECT(delta.throughput_requests_per_second.comparable == false);
    EXPECT(std::isfinite(delta.success_rate.absolute_difference));
}

INCIDENTLAB_TEST(every_scenario_uses_fresh_state) {
    const incidentlab::ComparisonReport report = cached();
    const auto baseline = by_id(report, "baseline").metrics.global;
    const auto full = by_id(report, "full").metrics.global;
    EXPECT(baseline.dead_lettered_requests == 0);
    EXPECT(baseline.circuit_openings == 0);
    EXPECT(full.dead_lettered_requests == 6);
    EXPECT(full.circuit_openings == 2);
    EXPECT(by_id(report, "baseline").simulation_end_time_ms == 1020);
}

INCIDENTLAB_TEST(scenario_order_does_not_change_results) {
    std::vector<incidentlab::ScenarioConfig> reversed = incidentlab::predefined_scenarios();
    std::reverse(reversed.begin(), reversed.end());
    const incidentlab::ComparisonReport forward = cached();
    const incidentlab::ComparisonReport backward = incidentlab::run_comparison(reversed);
    for (const incidentlab::ScenarioResult& item : forward.scenarios) {
        EXPECT(same_signature(item.metrics, by_id(backward, item.config.id).metrics));
    }
}

INCIDENTLAB_TEST(repeated_execution_is_identical) {
    incidentlab::ScenarioRunner runner;
    const incidentlab::ScenarioConfig config = incidentlab::predefined_scenarios()[1];
    const incidentlab::ScenarioResult first = runner.run(config);
    const incidentlab::ScenarioResult second = runner.run(config);
    EXPECT(same_signature(first.metrics, second.metrics));
    EXPECT(first.simulation_end_time_ms == second.simulation_end_time_ms);
}

INCIDENTLAB_TEST(scenario_a_does_not_affect_scenario_b) {
    incidentlab::ScenarioRunner runner;
    const std::vector<incidentlab::ScenarioConfig> scenarios = incidentlab::predefined_scenarios();
    const incidentlab::ScenarioResult ignored = runner.run(scenarios[3]);
    const incidentlab::ScenarioResult after = runner.run(scenarios[0]);
    EXPECT(same_signature(after.metrics, by_id(cached(), "baseline").metrics));
    EXPECT(ignored.metrics.global.circuit_openings == 2);
    EXPECT(after.metrics.global.circuit_openings == 0);
}

INCIDENTLAB_TEST(comparison_does_not_mutate_the_live_simulation) {
    incidentlab::SimulationController controller;
    EXPECT(controller.step());
    const incidentlab::VisualizationSnapshot before = controller.snapshot();
    const incidentlab::ComparisonReport report = incidentlab::run_predefined_comparison();
    const incidentlab::VisualizationSnapshot after = controller.snapshot();
    EXPECT(report.scenarios.size() == 4);
    EXPECT(before.simulation_time_ms == after.simulation_time_ms);
    EXPECT(before.playback == after.playback);
    EXPECT(before.metrics.timeline.size() == after.metrics.timeline.size());
    EXPECT(controller.step());
    EXPECT(controller.snapshot().simulation_time_ms >= before.simulation_time_ms);
}

INCIDENTLAB_TEST(scenarios_share_the_logical_workload) {
    const std::vector<incidentlab::WorkloadCall>& workload = incidentlab::comparison_workload();
    EXPECT(workload.size() == 10);
    const incidentlab::ComparisonReport& report = cached();
    for (const incidentlab::ScenarioResult& item : report.scenarios) {
        for (const incidentlab::WorkloadCall& call : workload) {
            bool seen = false;
            for (const incidentlab::TimelineEntry& entry : item.metrics.timeline) {
                if (entry.request_id == call.request_id && entry.service_name == call.service_name) {
                    seen = true;
                }
            }
            EXPECT(seen);
        }
    }
}

INCIDENTLAB_TEST(scenarios_share_arrival_times) {
    const std::vector<incidentlab::WorkloadCall>& workload = incidentlab::comparison_workload();
    const incidentlab::SimulationClock::Time expected[] = {0, 10, 20, 30, 40, 50, 60, 200, 700, 1000};
    for (std::size_t index = 0; index < workload.size(); ++index) {
        EXPECT(workload[index].arrival_ms == expected[index]);
    }
    for (const incidentlab::ScenarioResult& item : cached().scenarios) {
        for (const incidentlab::TimelineEntry& entry : item.metrics.timeline) {
            if (entry.request_id == "REQ-001" && entry.type == incidentlab::EventType::RequestReceived) {
                EXPECT(entry.time_ms == 0);
            }
        }
    }
}

INCIDENTLAB_TEST(failure_script_is_deterministic) {
    const std::vector<incidentlab::FaultRule>& faults = incidentlab::comparison_faults();
    EXPECT(faults.size() == 10);
    EXPECT(faults[1].request_id == "REQ-002");
    EXPECT(faults[1].attempts[0] == incidentlab::ServiceStatus::TimedOut);
    EXPECT(faults[2].attempts[1] == incidentlab::ServiceStatus::Success);
    incidentlab::ScenarioRunner runner;
    const incidentlab::ScenarioResult first = runner.run(incidentlab::predefined_scenarios()[0]);
    const incidentlab::ScenarioResult second = runner.run(incidentlab::predefined_scenarios()[0]);
    EXPECT(same_signature(first.metrics, second.metrics));
    EXPECT(first.metrics.global.outcomes.timed_out == 1);
}

INCIDENTLAB_TEST(retry_scenarios_schedule_retries) {
    const auto baseline = by_id(cached(), "baseline").metrics.global;
    const auto retry = by_id(cached(), "retry");
    EXPECT(baseline.retries.retry_attempts == 0);
    EXPECT(count_type(by_id(cached(), "baseline").metrics, incidentlab::EventType::RetryScheduled) == 0);
    EXPECT(retry.metrics.global.retries.retry_attempts == 6);
    EXPECT(count_type(retry.metrics, incidentlab::EventType::RetryScheduled) == 6);
    EXPECT(retry.metrics.global.retries.recovered_after_retry == 3);
}

INCIDENTLAB_TEST(circuit_scenarios_open_the_breaker) {
    const auto circuit = by_id(cached(), "circuit");
    const auto full = by_id(cached(), "full");
    EXPECT(circuit.metrics.global.circuit_openings == 2);
    EXPECT(count_type(circuit.metrics, incidentlab::EventType::CircuitOpened) == 2);
    EXPECT(full.metrics.global.circuit_openings == 2);
    EXPECT(count_type(full.metrics, incidentlab::EventType::CircuitOpened) == 2);
    EXPECT(by_id(cached(), "baseline").metrics.global.circuit_openings == 0);
}

INCIDENTLAB_TEST(comparison_metrics_match_stage6_definitions) {
    for (const incidentlab::ScenarioResult& item : cached().scenarios) {
        const incidentlab::GlobalMetrics& global = item.metrics.global;
        EXPECT(global.outcomes.completed == 10);
        const double completed = static_cast<double>(global.outcomes.completed);
        EXPECT(global.success_rate_percent ==
               static_cast<double>(global.outcomes.successful) * 100.0 / completed);
        EXPECT(global.failure_rate_percent ==
               static_cast<double>(global.outcomes.completed - global.outcomes.successful) * 100.0 /
                   completed);
        EXPECT(global.throughput_requests_per_second.has_value());
        EXPECT(*global.throughput_requests_per_second == 10.0 * 1000.0 / 1020.0);
    }
}

INCIDENTLAB_TEST(success_rate_deltas_are_percentage_points) {
    const incidentlab::ScenarioDelta retry = incidentlab::delta_from_baseline(cached(), "retry");
    EXPECT(retry.success_rate.baseline == 40.0);
    EXPECT(retry.success_rate.value == 70.0);
    EXPECT(retry.success_rate.absolute_difference == 30.0);
    EXPECT(retry.success_rate.relative_difference_available);
    EXPECT(retry.success_rate.relative_difference_percent == 75.0);

    const incidentlab::ScenarioDelta circuit = incidentlab::delta_from_baseline(cached(), "circuit");
    EXPECT(circuit.success_rate.absolute_difference == -10.0);
    EXPECT(circuit.success_rate.relative_difference_percent == -25.0);
}

INCIDENTLAB_TEST(zero_baseline_relative_change_is_unavailable) {
    const incidentlab::ScenarioDelta retry = incidentlab::delta_from_baseline(cached(), "retry");
    EXPECT(retry.retry_attempts.baseline == 0.0);
    EXPECT(retry.retry_attempts.value == 6.0);
    EXPECT(retry.retry_attempts.absolute_difference == 6.0);
    EXPECT(retry.retry_attempts.relative_difference_available == false);
    EXPECT(std::isfinite(retry.retry_attempts.relative_difference_percent));
}

INCIDENTLAB_TEST(missing_throughput_does_not_produce_nan) {
    incidentlab::ScenarioResult baseline;
    baseline.config.id = "baseline";
    baseline.config.name = "Baseline";
    baseline.metrics.global.success_rate_percent = 0.0;
    baseline.metrics.global.throughput_requests_per_second = std::nullopt;
    incidentlab::ScenarioResult other;
    other.config.id = "retry";
    other.config.name = "Retry Only";
    other.metrics.global.success_rate_percent = 25.0;
    other.metrics.global.throughput_requests_per_second = std::nullopt;
    incidentlab::ComparisonReport report;
    report.scenarios.push_back(baseline);
    report.scenarios.push_back(other);
    const incidentlab::ScenarioDelta delta = incidentlab::delta_from_baseline(report, "retry");
    EXPECT(delta.success_rate.absolute_difference == 25.0);
    EXPECT(delta.success_rate.relative_difference_available == false);
    EXPECT(delta.throughput_requests_per_second.comparable == false);
    EXPECT(std::isfinite(delta.throughput_requests_per_second.absolute_difference));
    EXPECT(std::isfinite(delta.success_rate.relative_difference_percent));
}

INCIDENTLAB_TEST(circuit_rejections_follow_stage6) {
    const auto circuit = by_id(cached(), "circuit").metrics.global;
    EXPECT(circuit.outcomes.circuit_rejected == 1);
    EXPECT(circuit.outcomes.failed == 5);
    EXPECT(circuit.outcomes.successful == 3);
    EXPECT(circuit.failure_rate_percent == 70.0);
    EXPECT(circuit.outcomes.failed + circuit.outcomes.timed_out + circuit.outcomes.circuit_rejected +
               circuit.outcomes.successful ==
           circuit.outcomes.completed);
    EXPECT(count_type(by_id(cached(), "circuit").metrics, incidentlab::EventType::CircuitRejected) == 1);
}

INCIDENTLAB_TEST(dlq_entries_match_dead_letter_events) {
    EXPECT(by_id(cached(), "baseline").metrics.global.dead_lettered_requests == 0);
    EXPECT(by_id(cached(), "retry").metrics.global.dead_lettered_requests == 3);
    EXPECT(by_id(cached(), "circuit").metrics.global.dead_lettered_requests == 7);
    EXPECT(by_id(cached(), "full").metrics.global.dead_lettered_requests == 6);
    for (const incidentlab::ScenarioResult& item : cached().scenarios) {
        EXPECT(item.metrics.global.dead_lettered_requests == item.metrics.dead_letters.size());
        EXPECT(item.metrics.global.dead_lettered_requests ==
               count_type(item.metrics, incidentlab::EventType::DeadLettered));
    }
}

INCIDENTLAB_TEST(retry_attempts_match_retry_events) {
    EXPECT(by_id(cached(), "full").metrics.global.retries.retry_attempts == 3);
    EXPECT(by_id(cached(), "full").metrics.global.retries.recovered_after_retry == 1);
    EXPECT(by_id(cached(), "full").metrics.global.retries.exhausted_retries == 0);
    EXPECT(by_id(cached(), "retry").metrics.global.retries.exhausted_retries == 3);
    for (const incidentlab::ScenarioResult& item : cached().scenarios) {
        EXPECT(item.metrics.global.retries.retry_attempts ==
               count_type(item.metrics, incidentlab::EventType::RetryScheduled));
    }
}

INCIDENTLAB_TEST(comparison_view_contains_four_scenarios) {
    const incidentlab::ComparisonView view = incidentlab::ComparisonView::predefined();
    EXPECT(view.report().scenarios.size() == 4);
    EXPECT(view.report().scenarios[0].config.name == "Baseline");
    EXPECT(view.report().scenarios[1].config.name == "Retry Only");
    EXPECT(view.report().scenarios[2].config.name == "Circuit Breaker");
    EXPECT(view.report().scenarios[3].config.name == "Full Resilience");
}

INCIDENTLAB_TEST(selected_scenario_starts_valid) {
    const incidentlab::ComparisonView view = incidentlab::ComparisonView::predefined();
    EXPECT(view.selected_index() == 3);
    EXPECT(view.selected() != nullptr);
    EXPECT(view.selected()->config.id == "full");
    EXPECT(view.baseline() != nullptr);
    EXPECT(view.baseline()->config.id == "baseline");
}

INCIDENTLAB_TEST(invalid_selection_is_ignored) {
    incidentlab::ComparisonView view = incidentlab::ComparisonView::predefined();
    view.select_index(99);
    EXPECT(view.selected_index() == 3);
    view.select_id("missing");
    EXPECT(view.selected()->config.id == "full");
    view.select_index(1);
    EXPECT(view.selected()->config.id == "retry");
}

INCIDENTLAB_TEST(baseline_selection_has_zero_deltas) {
    incidentlab::ComparisonView view = incidentlab::ComparisonView::predefined();
    view.select_id("baseline");
    const incidentlab::ScenarioDelta delta = view.selected_delta();
    EXPECT(delta.baseline_found);
    EXPECT(delta.success_rate.absolute_difference == 0.0);
    EXPECT(delta.success_rate.relative_difference_available);
    EXPECT(delta.success_rate.relative_difference_percent == 0.0);
    EXPECT(delta.failure_rate.absolute_difference == 0.0);
    EXPECT(delta.average_latency_ms.absolute_difference == 0.0);
    EXPECT(delta.total_attempts.absolute_difference == 0.0);
    EXPECT(delta.circuit_rejections.absolute_difference == 0.0);
    EXPECT(delta.dlq_entries.absolute_difference == 0.0);
    EXPECT(delta.retry_attempts.absolute_difference == 0.0);
    EXPECT(delta.retry_attempts.relative_difference_available == false);
}

INCIDENTLAB_TEST(comparison_view_does_not_change_live_simulation) {
    incidentlab::SimulationController controller;
    controller.start();
    EXPECT(controller.step());
    const incidentlab::VisualizationSnapshot before = controller.snapshot();
    incidentlab::ComparisonView view = incidentlab::ComparisonView::predefined();
    view.select_index(2);
    view.rerun();
    const incidentlab::VisualizationSnapshot after = controller.snapshot();
    EXPECT(view.selected()->config.id == "circuit");
    EXPECT(before.playback == after.playback);
    EXPECT(before.simulation_time_ms == after.simulation_time_ms);
    EXPECT(before.metrics.timeline.size() == after.metrics.timeline.size());
    EXPECT(controller.state() == incidentlab::PlaybackState::Running);
}

INCIDENTLAB_TEST(stage7_step_still_pauses_from_ready) {
    incidentlab::SimulationController controller;
    EXPECT(controller.state() == incidentlab::PlaybackState::Ready);
    EXPECT(controller.step());
    EXPECT(controller.state() == incidentlab::PlaybackState::Paused);
}

INCIDENTLAB_TEST(stage7_reset_restores_the_ready_clock) {
    incidentlab::SimulationController controller;
    controller.step();
    controller.step();
    EXPECT(controller.snapshot().simulation_time_ms > 0);
    controller.reset();
    EXPECT(controller.state() == incidentlab::PlaybackState::Ready);
    EXPECT(controller.snapshot().simulation_time_ms == 0);
    EXPECT(controller.snapshot().metrics.timeline.empty());
}

INCIDENTLAB_TEST(predefined_results_stay_exact) {
    const auto baseline = by_id(cached(), "baseline").metrics.global;
    EXPECT(baseline.outcomes.successful == 4);
    EXPECT(baseline.outcomes.failed == 5);
    EXPECT(baseline.outcomes.timed_out == 1);
    EXPECT(baseline.latency.average_ms == 20.0);
    EXPECT(baseline.latency.p95_ms == 20.0);

    const auto retry = by_id(cached(), "retry").metrics.global;
    EXPECT(retry.outcomes.successful == 7);
    EXPECT(retry.retries.total_attempts == 16);
    EXPECT(retry.latency.average_ms == 92.0);
    EXPECT(retry.latency.p50_ms == 140.0);
    EXPECT(retry.latency.p95_ms == 140.0);

    const auto circuit = by_id(cached(), "circuit").metrics.global;
    EXPECT(circuit.outcomes.successful == 3);
    EXPECT(circuit.retries.total_attempts == 9);
    EXPECT(circuit.latency.average_ms == 18.0);
    EXPECT(circuit.latency.p50_ms == 20.0);

    const auto full = by_id(cached(), "full").metrics.global;
    EXPECT(full.outcomes.successful == 4);
    EXPECT(full.outcomes.failed == 3);
    EXPECT(full.outcomes.circuit_rejected == 3);
    EXPECT(full.latency.average_ms == 50.0);
    EXPECT(full.latency.p50_ms == 20.0);
    EXPECT(full.latency.p95_ms == 140.0);
    EXPECT(full.latency.p99_ms == 140.0);
}

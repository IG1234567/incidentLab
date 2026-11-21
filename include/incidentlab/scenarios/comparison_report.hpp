#pragma once

#include "incidentlab/observability/metrics.hpp"
#include "incidentlab/scenarios/scenario_config.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace incidentlab {

/// One finished scenario. The report is owned. It does not point at the engine that produced it.
struct ScenarioResult {
    ScenarioConfig config;
    ObservabilityReport metrics;
    std::uint64_t simulation_end_time_ms = 0;
};

/// Results in the order the scenarios were requested.
struct ComparisonReport {
    std::vector<ScenarioResult> scenarios;
};

/// Difference of one numeric metric against the baseline.
///
/// absolute_difference is value - baseline. For a success rate or a failure rate, that
/// difference is in percentage points, not a relative percent.
/// relative_difference_percent is (value - baseline) / baseline * 100.
/// It is omitted when the baseline is zero, so the result is never NaN and never infinite.
/// comparable is false when either side has no observation, which is how missing throughput
/// is reported.
struct MetricDelta {
    double baseline = 0.0;
    double value = 0.0;
    double absolute_difference = 0.0;
    double relative_difference_percent = 0.0;
    bool relative_difference_available = false;
    bool comparable = true;
};

/// Deltas of one scenario against Scenario A. Counts use the Stage 6 definitions.
struct ScenarioDelta {
    std::string scenario_id;
    std::string scenario_name;
    bool baseline_found = false;
    MetricDelta completed;
    MetricDelta successful;
    MetricDelta failed;
    MetricDelta circuit_rejections;
    MetricDelta success_rate;
    MetricDelta failure_rate;
    MetricDelta average_latency_ms;
    MetricDelta p50_latency_ms;
    MetricDelta p95_latency_ms;
    MetricDelta p99_latency_ms;
    MetricDelta throughput_requests_per_second;
    MetricDelta total_attempts;
    MetricDelta retry_attempts;
    MetricDelta recovered_after_retry;
    MetricDelta exhausted_retries;
    MetricDelta circuit_openings;
    MetricDelta dlq_entries;
};

/// Empty when the report has no scenario with that id, or when the report is empty.
const ScenarioResult* find_scenario(const ComparisonReport& report, const std::string& id);

/// Zero deltas when the report is empty or the id is missing. Baseline against itself is zero.
ScenarioDelta delta_from_baseline(const ComparisonReport& report, const std::string& scenario_id);

/// Sentences that are true of this report. A difference that is not present is not described.
std::vector<std::string> comparison_observations(const ComparisonReport& report);

/// Selected row over a cached comparison. Selection does not run the simulation.
class ComparisonView {
public:
    /// Runs the four predefined scenarios once.
    static ComparisonView predefined();

    /// Runs the four predefined scenarios again and keeps a still-valid selection.
    void rerun();

    /// Ignored when index is past the end. The previous selection stays.
    void select_index(std::size_t index);

    /// Ignored when no scenario has this id.
    void select_id(const std::string& id);

    std::size_t selected_index() const noexcept;
    const ComparisonReport& report() const noexcept;
    const ScenarioResult* selected() const;
    const ScenarioResult* baseline() const;
    ScenarioDelta selected_delta() const;

private:
    ComparisonView(ComparisonReport report, std::size_t selected);

    ComparisonReport report_;
    std::size_t selected_ = 0;
};

}  // namespace incidentlab

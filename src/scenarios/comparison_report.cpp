#include "incidentlab/scenarios/comparison_report.hpp"

#include "incidentlab/scenarios/scenario_runner.hpp"

#include <cmath>
#include <sstream>
#include <string>

namespace incidentlab {
namespace {

MetricDelta delta_of(double baseline, double value) {
    MetricDelta delta;
    delta.baseline = baseline;
    delta.value = value;
    delta.absolute_difference = value - baseline;
    delta.comparable = true;
    if (baseline == 0.0) {
        delta.relative_difference_available = false;
        delta.relative_difference_percent = 0.0;
        return delta;
    }
    delta.relative_difference_available = true;
    delta.relative_difference_percent = (value - baseline) / baseline * 100.0;
    return delta;
}

MetricDelta missing_delta() {
    MetricDelta delta;
    delta.comparable = false;
    delta.relative_difference_available = false;
    return delta;
}

MetricDelta throughput_delta(const std::optional<double>& baseline,
                             const std::optional<double>& value) {
    if (!baseline.has_value() || !value.has_value() || !std::isfinite(*baseline) ||
        !std::isfinite(*value)) {
        return missing_delta();
    }
    return delta_of(*baseline, *value);
}

std::string count_phrase(std::size_t count, const char* singular, const char* plural) {
    return std::to_string(count) + " " + (count == 1 ? singular : plural);
}

std::string format_number(double value) {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(2);
    out << value;
    return out.str();
}

const ScenarioResult* find_id(const ComparisonReport& report, const char* id) {
    return find_scenario(report, id);
}

}  // namespace

const ScenarioResult* find_scenario(const ComparisonReport& report, const std::string& id) {
    for (const ScenarioResult& result : report.scenarios) {
        if (result.config.id == id) {
            return &result;
        }
    }
    return nullptr;
}

ScenarioDelta delta_from_baseline(const ComparisonReport& report, const std::string& scenario_id) {
    ScenarioDelta delta;
    delta.scenario_id = scenario_id;
    const ScenarioResult* selected = find_scenario(report, scenario_id);
    const ScenarioResult* baseline = find_scenario(report, "baseline");
    if (selected == nullptr || baseline == nullptr) {
        delta.baseline_found = false;
        delta.throughput_requests_per_second = missing_delta();
        return delta;
    }
    delta.baseline_found = true;
    delta.scenario_name = selected->config.name;
    const GlobalMetrics& base = baseline->metrics.global;
    const GlobalMetrics& current = selected->metrics.global;
    delta.completed = delta_of(static_cast<double>(base.outcomes.completed),
                               static_cast<double>(current.outcomes.completed));
    delta.successful = delta_of(static_cast<double>(base.outcomes.successful),
                                static_cast<double>(current.outcomes.successful));
    delta.failed = delta_of(static_cast<double>(base.outcomes.failed),
                            static_cast<double>(current.outcomes.failed));
    delta.circuit_rejections =
        delta_of(static_cast<double>(base.outcomes.circuit_rejected),
                 static_cast<double>(current.outcomes.circuit_rejected));
    delta.success_rate = delta_of(base.success_rate_percent, current.success_rate_percent);
    delta.failure_rate = delta_of(base.failure_rate_percent, current.failure_rate_percent);
    delta.average_latency_ms = delta_of(base.latency.average_ms, current.latency.average_ms);
    delta.p50_latency_ms = delta_of(base.latency.p50_ms, current.latency.p50_ms);
    delta.p95_latency_ms = delta_of(base.latency.p95_ms, current.latency.p95_ms);
    delta.p99_latency_ms = delta_of(base.latency.p99_ms, current.latency.p99_ms);
    delta.throughput_requests_per_second = throughput_delta(base.throughput_requests_per_second,
                                                            current.throughput_requests_per_second);
    delta.total_attempts = delta_of(static_cast<double>(base.retries.total_attempts),
                                    static_cast<double>(current.retries.total_attempts));
    delta.retry_attempts = delta_of(static_cast<double>(base.retries.retry_attempts),
                                    static_cast<double>(current.retries.retry_attempts));
    delta.recovered_after_retry =
        delta_of(static_cast<double>(base.retries.recovered_after_retry),
                 static_cast<double>(current.retries.recovered_after_retry));
    delta.exhausted_retries = delta_of(static_cast<double>(base.retries.exhausted_retries),
                                       static_cast<double>(current.retries.exhausted_retries));
    delta.circuit_openings = delta_of(static_cast<double>(base.circuit_openings),
                                      static_cast<double>(current.circuit_openings));
    delta.dlq_entries = delta_of(static_cast<double>(base.dead_lettered_requests),
                                 static_cast<double>(current.dead_lettered_requests));
    return delta;
}

std::vector<std::string> comparison_observations(const ComparisonReport& report) {
    std::vector<std::string> notes;
    const ScenarioResult* baseline = find_id(report, "baseline");
    const ScenarioResult* retry = find_id(report, "retry");
    const ScenarioResult* circuit = find_id(report, "circuit");
    const ScenarioResult* full = find_id(report, "full");
    if (baseline == nullptr) {
        return notes;
    }
    const GlobalMetrics& base = baseline->metrics.global;
    if (retry != nullptr) {
        const GlobalMetrics& current = retry->metrics.global;
        if (current.outcomes.successful > base.outcomes.successful) {
            notes.push_back(
                "Retry Only completed " +
                count_phrase(current.outcomes.successful - base.outcomes.successful, "more successful request",
                             "more successful requests") +
                " than Baseline and scheduled " +
                count_phrase(current.retries.retry_attempts, "retry attempt", "retry attempts") + ".");
        }
        if (current.latency.average_ms > base.latency.average_ms) {
            notes.push_back("Retry Only raised average latency from " +
                            format_number(base.latency.average_ms) + " ms to " +
                            format_number(current.latency.average_ms) + " ms.");
        }
    }
    if (circuit != nullptr) {
        const GlobalMetrics& current = circuit->metrics.global;
        if (current.outcomes.circuit_rejected > 0) {
            notes.push_back("Circuit Breaker rejected " +
                            count_phrase(current.outcomes.circuit_rejected, "request", "requests") +
                            " without calling the service.");
        }
        if (current.retries.total_attempts < base.retries.total_attempts) {
            notes.push_back("Circuit Breaker reached the service on " +
                            std::to_string(current.retries.total_attempts) + " attempts, compared with " +
                            std::to_string(base.retries.total_attempts) + " for Baseline.");
        }
    }
    if (full != nullptr) {
        const GlobalMetrics& current = full->metrics.global;
        if (current.outcomes.successful != base.outcomes.successful) {
            notes.push_back("Full Resilience completed " + std::to_string(current.outcomes.successful) +
                            " successful requests. Baseline completed " +
                            std::to_string(base.outcomes.successful) + ".");
        }
        if (current.dead_lettered_requests > base.dead_lettered_requests) {
            notes.push_back("Full Resilience dead-lettered " +
                            count_phrase(current.dead_lettered_requests, "logical request",
                                         "logical requests") +
                            ".");
        }
        if (current.outcomes.circuit_rejected > 0 && current.retries.retry_attempts > 0) {
            notes.push_back("Full Resilience scheduled retries, and the open circuit blocked " +
                            std::to_string(current.outcomes.circuit_rejected) +
                            " of the later calls.");
        }
    }
    bool same_throughput = true;
    const std::optional<double> base_throughput = base.throughput_requests_per_second;
    if (!base_throughput.has_value()) {
        same_throughput = false;
    }
    for (const ScenarioResult& result : report.scenarios) {
        const std::optional<double> throughput = result.metrics.global.throughput_requests_per_second;
        if (!throughput.has_value() || !base_throughput.has_value() || *throughput != *base_throughput) {
            same_throughput = false;
        }
    }
    if (same_throughput && report.scenarios.size() > 1) {
        notes.push_back("Throughput is the same in every scenario because the observation window "
                        "still runs from the first start to the last completion.");
    }
    return notes;
}

ComparisonView::ComparisonView(ComparisonReport report, std::size_t selected)
    : report_(std::move(report)), selected_(selected) {}

ComparisonView ComparisonView::predefined() {
    ComparisonReport report = run_predefined_comparison();
    const std::size_t selected = report.scenarios.size() > 3 ? 3 : 0;
    return ComparisonView{std::move(report), selected};
}

void ComparisonView::rerun() {
    const std::string id =
        selected_ < report_.scenarios.size() ? report_.scenarios[selected_].config.id : std::string{};
    report_ = run_predefined_comparison();
    selected_ = 0;
    select_id(id);
}

void ComparisonView::select_index(std::size_t index) {
    if (index < report_.scenarios.size()) {
        selected_ = index;
    }
}

void ComparisonView::select_id(const std::string& id) {
    for (std::size_t index = 0; index < report_.scenarios.size(); ++index) {
        if (report_.scenarios[index].config.id == id) {
            selected_ = index;
            return;
        }
    }
}

std::size_t ComparisonView::selected_index() const noexcept { return selected_; }

const ComparisonReport& ComparisonView::report() const noexcept { return report_; }

const ScenarioResult* ComparisonView::selected() const {
    if (selected_ >= report_.scenarios.size()) {
        return nullptr;
    }
    return &report_.scenarios[selected_];
}

const ScenarioResult* ComparisonView::baseline() const { return find_scenario(report_, "baseline"); }

ScenarioDelta ComparisonView::selected_delta() const {
    const ScenarioResult* current = selected();
    if (current == nullptr) {
        return delta_from_baseline(report_, "");
    }
    return delta_from_baseline(report_, current->config.id);
}

}  // namespace incidentlab

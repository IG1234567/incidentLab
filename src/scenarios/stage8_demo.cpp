#include "incidentlab/scenarios/stage8_demo.hpp"

#include "incidentlab/scenarios/scenario_runner.hpp"

#include <iomanip>
#include <sstream>
#include <string>

namespace incidentlab {
namespace {

const ScenarioResult* by_id(const ComparisonReport& report, const char* id) {
    return find_scenario(report, id);
}

std::string count_text(std::size_t value) { return std::to_string(value); }

std::string percent_text(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value << '%';
    return out.str();
}

std::string ms_text(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value << " ms";
    return out.str();
}

std::string throughput_text(const std::optional<double>& value) {
    if (!value.has_value()) {
        return "N/A";
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << *value;
    return out.str();
}

void print_row(std::ostream& out, const std::string& label, const std::string& baseline,
               const std::string& retry, const std::string& circuit, const std::string& full) {
    out << std::left << std::setw(22) << label << std::setw(14) << baseline << std::setw(14) << retry
        << std::setw(14) << circuit << full << '\n';
}

}  // namespace

void run_stage8_demo(std::ostream& out) {
    const ComparisonReport report = run_predefined_comparison();
    const ScenarioResult* baseline = by_id(report, "baseline");
    const ScenarioResult* retry = by_id(report, "retry");
    const ScenarioResult* circuit = by_id(report, "circuit");
    const ScenarioResult* full = by_id(report, "full");
    const ScenarioResult* columns[] = {baseline, retry, circuit, full};

    out << "============================================================\n";
    out << "INCIDENTLAB — SCENARIO COMPARISON\n";
    out << "============================================================\n\n";
    out << "The four scenarios schedule the same ten requests. Faults are tied to the\n";
    out << "request id and that request's own attempt. Every delivery waits "
        << kComparisonProcessingDelayMs << " ms.\n";
    out << "The engine dead-letters a terminal failure when retry or a circuit breaker\n";
    out << "is configured. There is no separate dead-letter switch.\n\n";

    auto value_of = [](const ScenarioResult* result, auto reader) -> std::string {
        if (result == nullptr) {
            return "n/a";
        }
        return reader(*result);
    };

    print_row(out, "Metric", "Baseline", "Retry", "Circuit", "Full");
    out << "------------------------------------------------------------------------\n";
    print_row(out, "Completed",
              value_of(baseline, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.outcomes.completed);
              }),
              value_of(retry, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.outcomes.completed);
              }),
              value_of(circuit, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.outcomes.completed);
              }),
              value_of(full, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.outcomes.completed);
              }));
    print_row(out, "Successful",
              value_of(baseline, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.outcomes.successful);
              }),
              value_of(retry, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.outcomes.successful);
              }),
              value_of(circuit, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.outcomes.successful);
              }),
              value_of(full, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.outcomes.successful);
              }));
    print_row(out, "Failed",
              value_of(baseline, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.outcomes.failed);
              }),
              value_of(retry, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.outcomes.failed);
              }),
              value_of(circuit, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.outcomes.failed);
              }),
              value_of(full, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.outcomes.failed);
              }));
    print_row(out, "Success rate",
              value_of(baseline, [](const ScenarioResult& item) {
                  return percent_text(item.metrics.global.success_rate_percent);
              }),
              value_of(retry, [](const ScenarioResult& item) {
                  return percent_text(item.metrics.global.success_rate_percent);
              }),
              value_of(circuit, [](const ScenarioResult& item) {
                  return percent_text(item.metrics.global.success_rate_percent);
              }),
              value_of(full, [](const ScenarioResult& item) {
                  return percent_text(item.metrics.global.success_rate_percent);
              }));
    print_row(out, "Failure rate",
              value_of(baseline, [](const ScenarioResult& item) {
                  return percent_text(item.metrics.global.failure_rate_percent);
              }),
              value_of(retry, [](const ScenarioResult& item) {
                  return percent_text(item.metrics.global.failure_rate_percent);
              }),
              value_of(circuit, [](const ScenarioResult& item) {
                  return percent_text(item.metrics.global.failure_rate_percent);
              }),
              value_of(full, [](const ScenarioResult& item) {
                  return percent_text(item.metrics.global.failure_rate_percent);
              }));
    print_row(out, "Average latency",
              value_of(baseline, [](const ScenarioResult& item) {
                  return ms_text(item.metrics.global.latency.average_ms);
              }),
              value_of(retry, [](const ScenarioResult& item) {
                  return ms_text(item.metrics.global.latency.average_ms);
              }),
              value_of(circuit, [](const ScenarioResult& item) {
                  return ms_text(item.metrics.global.latency.average_ms);
              }),
              value_of(full, [](const ScenarioResult& item) {
                  return ms_text(item.metrics.global.latency.average_ms);
              }));
    print_row(out, "P95 latency",
              value_of(baseline, [](const ScenarioResult& item) {
                  return ms_text(item.metrics.global.latency.p95_ms);
              }),
              value_of(retry, [](const ScenarioResult& item) {
                  return ms_text(item.metrics.global.latency.p95_ms);
              }),
              value_of(circuit, [](const ScenarioResult& item) {
                  return ms_text(item.metrics.global.latency.p95_ms);
              }),
              value_of(full, [](const ScenarioResult& item) {
                  return ms_text(item.metrics.global.latency.p95_ms);
              }));
    print_row(out, "Throughput",
              value_of(baseline, [](const ScenarioResult& item) {
                  return throughput_text(item.metrics.global.throughput_requests_per_second);
              }),
              value_of(retry, [](const ScenarioResult& item) {
                  return throughput_text(item.metrics.global.throughput_requests_per_second);
              }),
              value_of(circuit, [](const ScenarioResult& item) {
                  return throughput_text(item.metrics.global.throughput_requests_per_second);
              }),
              value_of(full, [](const ScenarioResult& item) {
                  return throughput_text(item.metrics.global.throughput_requests_per_second);
              }));
    print_row(out, "Total attempts",
              value_of(baseline, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.retries.total_attempts);
              }),
              value_of(retry, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.retries.total_attempts);
              }),
              value_of(circuit, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.retries.total_attempts);
              }),
              value_of(full, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.retries.total_attempts);
              }));
    print_row(out, "Retry attempts",
              value_of(baseline, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.retries.retry_attempts);
              }),
              value_of(retry, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.retries.retry_attempts);
              }),
              value_of(circuit, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.retries.retry_attempts);
              }),
              value_of(full, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.retries.retry_attempts);
              }));
    print_row(out, "Recovered",
              value_of(baseline, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.retries.recovered_after_retry);
              }),
              value_of(retry, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.retries.recovered_after_retry);
              }),
              value_of(circuit, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.retries.recovered_after_retry);
              }),
              value_of(full, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.retries.recovered_after_retry);
              }));
    print_row(out, "Exhausted retries",
              value_of(baseline, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.retries.exhausted_retries);
              }),
              value_of(retry, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.retries.exhausted_retries);
              }),
              value_of(circuit, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.retries.exhausted_retries);
              }),
              value_of(full, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.retries.exhausted_retries);
              }));
    print_row(out, "Circuit openings",
              value_of(baseline, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.circuit_openings);
              }),
              value_of(retry, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.circuit_openings);
              }),
              value_of(circuit, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.circuit_openings);
              }),
              value_of(full, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.circuit_openings);
              }));
    print_row(out, "Circuit rejected",
              value_of(baseline, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.outcomes.circuit_rejected);
              }),
              value_of(retry, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.outcomes.circuit_rejected);
              }),
              value_of(circuit, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.outcomes.circuit_rejected);
              }),
              value_of(full, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.outcomes.circuit_rejected);
              }));
    print_row(out, "DLQ entries",
              value_of(baseline, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.dead_lettered_requests);
              }),
              value_of(retry, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.dead_lettered_requests);
              }),
              value_of(circuit, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.dead_lettered_requests);
              }),
              value_of(full, [](const ScenarioResult& item) {
                  return count_text(item.metrics.global.dead_lettered_requests);
              }));

    out << "\nBASELINE DIFFERENCES\n\n";
    for (const ScenarioResult* result : columns) {
        if (result == nullptr || result->config.id == "baseline") {
            continue;
        }
        const ScenarioDelta delta = delta_from_baseline(report, result->config.id);
        out << result->config.name << '\n';
        out << "  Success rate: " << std::showpos << std::fixed << std::setprecision(2)
            << delta.success_rate.absolute_difference << std::noshowpos << " percentage points\n";
        out << "  Additional attempts: " << std::showpos << std::fixed << std::setprecision(0)
            << delta.total_attempts.absolute_difference << std::noshowpos << '\n';
        if (delta.success_rate.relative_difference_available) {
            out << "  Relative success-rate change: " << std::showpos << std::fixed
                << std::setprecision(2) << delta.success_rate.relative_difference_percent
                << std::noshowpos << "%\n";
        } else {
            out << "  Relative success-rate change: unavailable\n";
        }
        out << '\n';
    }

    out << "OBSERVATIONS\n\n";
    const std::vector<std::string> notes = comparison_observations(report);
    if (notes.empty()) {
        out << "No supported difference was found.\n";
    }
    for (const std::string& note : notes) {
        out << note << '\n';
    }
    out << "\n============================================================\n";
}

}  // namespace incidentlab

#include "incidentlab/observability/metrics_calculator.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace incidentlab {
namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::logic_error(message);
    }
}

void add_outcome(OutcomeCounts& counts, ServiceStatus status) {
    ++counts.completed;
    switch (status) {
    case ServiceStatus::Success:
        ++counts.successful;
        break;
    case ServiceStatus::Rejected:
        ++counts.rejected;
        break;
    case ServiceStatus::Failed:
        ++counts.failed;
        break;
    case ServiceStatus::TimedOut:
        ++counts.timed_out;
        break;
    case ServiceStatus::Dropped:
        ++counts.dropped;
        break;
    case ServiceStatus::CircuitRejected:
        ++counts.circuit_rejected;
        break;
    }
}

bool is_attempt(EventType type) noexcept {
    return type == EventType::RequestReceived || type == EventType::RequestDropped;
}

SimulationClock::Time latency_of(const ExecutionRecord& execution) {
    if (execution.simulation_time < execution.start_time) {
        throw std::logic_error("completion time is earlier than the logical start");
    }
    return execution.simulation_time - execution.start_time;
}

double percentile(const std::vector<std::uint64_t>& sorted, std::size_t numerator,
                  std::size_t denominator) {
    if (sorted.empty()) {
        return 0.0;
    }
    const std::size_t count = sorted.size();
    if (count > (std::numeric_limits<std::size_t>::max() - (denominator - 1)) / numerator) {
        throw std::overflow_error("percentile rank overflow");
    }
    const std::size_t rank = (numerator * count + denominator - 1) / denominator;
    require(rank >= 1 && rank <= count, "percentile rank is outside the sample");
    return static_cast<double>(sorted[rank - 1]);
}

LatencyMetrics latency_metrics(std::vector<std::uint64_t> latencies) {
    LatencyMetrics metrics;
    if (latencies.empty()) {
        return metrics;
    }
    std::sort(latencies.begin(), latencies.end());
    double sum = 0.0;
    for (const std::uint64_t latency : latencies) {
        sum += static_cast<double>(latency);
    }
    metrics.average_ms = sum / static_cast<double>(latencies.size());
    metrics.min_ms = static_cast<double>(latencies.front());
    metrics.max_ms = static_cast<double>(latencies.back());
    metrics.p50_ms = percentile(latencies, 50, 100);
    metrics.p95_ms = percentile(latencies, 95, 100);
    metrics.p99_ms = percentile(latencies, 99, 100);
    return metrics;
}

void apply_rates(double& success_rate, double& failure_rate, const OutcomeCounts& counts) {
    if (counts.completed == 0) {
        success_rate = 0.0;
        failure_rate = 0.0;
        return;
    }
    require(counts.successful <= counts.completed, "successful requests exceed completed requests");
    const double completed = static_cast<double>(counts.completed);
    success_rate = static_cast<double>(counts.successful) * 100.0 / completed;
    const std::size_t unsuccessful = counts.completed - counts.successful;
    failure_rate = static_cast<double>(unsuccessful) * 100.0 / completed;
}

struct ServiceAccum {
    OutcomeCounts outcomes;
    std::size_t total_attempts = 0;
    std::size_t retry_attempts = 0;
    std::size_t recovered_after_retry = 0;
    std::size_t exhausted_retries = 0;
    std::size_t circuit_openings = 0;
    std::size_t dead_lettered_requests = 0;
    std::unordered_set<std::string> retry_ids;
    std::vector<std::uint64_t> latencies;
};

struct CircuitAccum {
    std::size_t opened = 0;
    std::size_t half_opened = 0;
    std::size_t closed = 0;
    std::size_t blocked_requests = 0;
    std::optional<CircuitState> final_state;
};

void validate_report(const SimulationReport& report) {
    require(report.history.size() == report.event_times.size(),
            "event history and timestamps have different lengths");
    require(report.executions.size() == report.processed_requests,
            "processed requests do not match execution records");
    require(report.dead_letters.size() == report.dead_lettered_requests,
            "dead-letter count does not match the queue");

    OutcomeCounts counted;
    std::unordered_set<std::string> request_ids;
    for (const ExecutionRecord& execution : report.executions) {
        require(request_ids.insert(execution.request_id).second,
                "duplicate logical request in execution records");
        require(execution.simulation_time >= execution.start_time,
                "completion time is earlier than the logical start");
        add_outcome(counted, execution.result.status);
    }
    require(counted.successful == report.successful_requests, "successful request count mismatch");
    require(counted.rejected == report.rejected_requests, "rejected request count mismatch");
    require(counted.failed == report.failed_requests, "failed request count mismatch");
    require(counted.timed_out == report.timed_out_requests, "timed out request count mismatch");
    require(counted.dropped == report.dropped_requests, "dropped request count mismatch");
    require(counted.circuit_rejected == report.circuit_rejected_requests,
            "circuit rejection count mismatch");
    require(counted.completed == report.processed_requests, "completed request count mismatch");

    std::unordered_set<std::string> dead_ids;
    for (const DeadLetterEntry& entry : report.dead_letters) {
        require(dead_ids.insert(entry.request.request_id).second, "duplicate dead-letter request");
    }

    std::unordered_set<std::string> circuit_names;
    for (const CircuitStateSnapshot& snapshot : report.circuit_states) {
        require(circuit_names.insert(snapshot.service_name).second,
                "duplicate circuit breaker snapshot");
    }
}

}  // namespace

ObservabilityReport MetricsCalculator::calculate(const SimulationReport& report) {
    validate_report(report);

    ObservabilityReport result;
    std::map<std::string, ServiceAccum> services;
    std::map<std::string, CircuitAccum> circuits;
    std::unordered_map<std::string, std::string> retry_owner;
    std::vector<std::uint64_t> latencies;
    std::size_t attempt_events = 0;
    std::size_t retry_events = 0;
    std::size_t circuit_rejected_events = 0;
    std::size_t recovered = 0;
    std::size_t exhausted = 0;

    const std::vector<Event>& events = report.history.events();
    result.timeline.reserve(events.size());
    for (std::size_t index = 0; index < events.size(); ++index) {
        const Event& event = events[index];
        result.timeline.push_back(TimelineEntry{report.event_times[index], event.event_id,
                                                event.request_id, event.trace_id,
                                                event.service_name, event.type, event.message});
        ServiceAccum& service = services[event.service_name];
        if (is_attempt(event.type)) {
            ++attempt_events;
            ++service.total_attempts;
        } else if (event.type == EventType::RetryScheduled) {
            ++retry_events;
            ++service.retry_attempts;
            const auto existing = retry_owner.find(event.request_id);
            if (existing == retry_owner.end()) {
                retry_owner.emplace(event.request_id, event.service_name);
            } else {
                require(existing->second == event.service_name,
                        "retry events for one request name more than one service");
            }
            service.retry_ids.insert(event.request_id);
        } else if (event.type == EventType::CircuitOpened) {
            ++service.circuit_openings;
            ++circuits[event.service_name].opened;
        } else if (event.type == EventType::CircuitHalfOpened) {
            ++circuits[event.service_name].half_opened;
        } else if (event.type == EventType::CircuitClosed) {
            ++circuits[event.service_name].closed;
        } else if (event.type == EventType::CircuitRejected) {
            ++circuit_rejected_events;
            ++circuits[event.service_name].blocked_requests;
        }
    }

    require(attempt_events == report.total_attempts, "attempt events do not match total attempts");
    require(retry_events == report.retry_attempts, "retry events do not match retry attempts");
    require(circuit_rejected_events == report.circuit_rejected_requests,
            "circuit rejection events do not match the report");

    std::optional<SimulationClock::Time> window_start;
    std::optional<SimulationClock::Time> window_end;
    for (const ExecutionRecord& execution : report.executions) {
        const SimulationClock::Time latency = latency_of(execution);
        latencies.push_back(latency);
        ServiceAccum& service = services[execution.service_name];
        service.latencies.push_back(latency);
        add_outcome(result.global.outcomes, execution.result.status);
        add_outcome(service.outcomes, execution.result.status);
        if (execution.retries_exhausted) {
            ++exhausted;
            ++service.exhausted_retries;
        }
        if (execution.result.status == ServiceStatus::Success &&
            retry_owner.find(execution.request_id) != retry_owner.end()) {
            ++recovered;
            ++service.recovered_after_retry;
        }
        if (!window_start.has_value() || execution.start_time < *window_start) {
            window_start = execution.start_time;
        }
        if (!window_end.has_value() || execution.simulation_time > *window_end) {
            window_end = execution.simulation_time;
        }
    }

    for (const DeadLetterEntry& entry : report.dead_letters) {
        const std::string& service_name = entry.request.target_service;
        ++services[service_name].dead_lettered_requests;
        result.dead_letters.push_back(
            DeadLetterObservation{entry.request.request_id, entry.request.trace_id, service_name,
                                  entry.final_status, entry.failed_at_ms, entry.attempts,
                                  entry.reason});
    }

    for (const CircuitStateSnapshot& snapshot : report.circuit_states) {
        circuits[snapshot.service_name].final_state = snapshot.state;
    }

    result.global.retries.total_attempts = report.total_attempts;
    result.global.retries.retry_attempts = report.retry_attempts;
    result.global.retries.requests_requiring_retry = retry_owner.size();
    result.global.retries.recovered_after_retry = recovered;
    result.global.retries.exhausted_retries = exhausted;
    result.global.dead_lettered_requests = result.dead_letters.size();
    result.global.latency = latency_metrics(latencies);
    result.global.observation_start_ms = window_start;
    result.global.observation_end_ms = window_end;
    apply_rates(result.global.success_rate_percent, result.global.failure_rate_percent,
                result.global.outcomes);
    if (result.global.outcomes.completed == 0 || !window_start.has_value() ||
        !window_end.has_value() || *window_end == *window_start) {
        result.global.throughput_requests_per_second = std::nullopt;
    } else {
        const SimulationClock::Time duration = *window_end - *window_start;
        result.global.throughput_requests_per_second =
            static_cast<double>(result.global.outcomes.completed) * 1000.0 /
            static_cast<double>(duration);
    }

    std::size_t completed_sum = 0;
    std::size_t attempt_sum = 0;
    std::size_t retry_sum = 0;
    std::size_t requiring_sum = 0;
    std::size_t recovered_sum = 0;
    std::size_t exhausted_sum = 0;
    std::size_t dead_sum = 0;
    std::size_t opening_sum = 0;
    result.services.reserve(services.size());
    for (auto& entry : services) {
        ServiceAccum& accum = entry.second;
        ServiceMetrics metrics;
        metrics.service_name = entry.first;
        metrics.outcomes = accum.outcomes;
        metrics.retries.total_attempts = accum.total_attempts;
        metrics.retries.retry_attempts = accum.retry_attempts;
        metrics.retries.requests_requiring_retry = accum.retry_ids.size();
        metrics.retries.recovered_after_retry = accum.recovered_after_retry;
        metrics.retries.exhausted_retries = accum.exhausted_retries;
        metrics.latency = latency_metrics(std::move(accum.latencies));
        metrics.circuit_openings = accum.circuit_openings;
        metrics.dead_lettered_requests = accum.dead_lettered_requests;
        apply_rates(metrics.success_rate_percent, metrics.failure_rate_percent, metrics.outcomes);
        completed_sum += metrics.outcomes.completed;
        attempt_sum += metrics.retries.total_attempts;
        retry_sum += metrics.retries.retry_attempts;
        requiring_sum += metrics.retries.requests_requiring_retry;
        recovered_sum += metrics.retries.recovered_after_retry;
        exhausted_sum += metrics.retries.exhausted_retries;
        dead_sum += metrics.dead_lettered_requests;
        opening_sum += metrics.circuit_openings;
        result.services.push_back(std::move(metrics));
    }

    std::size_t circuit_blocked = 0;
    result.circuits.reserve(circuits.size());
    for (auto& entry : circuits) {
        CircuitMetrics metrics;
        metrics.service_name = entry.first;
        metrics.opened = entry.second.opened;
        metrics.half_opened = entry.second.half_opened;
        metrics.closed = entry.second.closed;
        metrics.blocked_requests = entry.second.blocked_requests;
        metrics.final_state = entry.second.final_state;
        result.global.circuit_openings += metrics.opened;
        result.global.circuit_half_openings += metrics.half_opened;
        result.global.circuit_closings += metrics.closed;
        circuit_blocked += metrics.blocked_requests;
        result.circuits.push_back(std::move(metrics));
    }

    require(completed_sum == result.global.outcomes.completed,
            "per-service completed requests do not add up");
    require(attempt_sum == result.global.retries.total_attempts,
            "per-service attempts do not add up");
    require(retry_sum == result.global.retries.retry_attempts, "per-service retries do not add up");
    require(requiring_sum == result.global.retries.requests_requiring_retry,
            "per-service retry requests do not add up");
    require(recovered_sum == result.global.retries.recovered_after_retry,
            "per-service recoveries do not add up");
    require(exhausted_sum == result.global.retries.exhausted_retries,
            "per-service exhausted retries do not add up");
    require(dead_sum == result.global.dead_lettered_requests,
            "per-service dead letters do not add up");
    require(opening_sum == result.global.circuit_openings,
            "per-service circuit openings do not add up");
    require(circuit_blocked == result.global.outcomes.circuit_rejected,
            "per-service circuit rejections do not add up");
    return result;
}

namespace {

std::vector<TimelineEntry> filter_timeline(
    const std::vector<TimelineEntry>& entries,
    bool (*matches)(const TimelineEntry&, const std::string&), const std::string& value) {
    std::vector<TimelineEntry> selected;
    for (const TimelineEntry& entry : entries) {
        if (matches(entry, value)) {
            selected.push_back(entry);
        }
    }
    return selected;
}

bool service_matches(const TimelineEntry& entry, const std::string& value) {
    return entry.service_name == value;
}

bool request_matches(const TimelineEntry& entry, const std::string& value) {
    return entry.request_id == value;
}

bool trace_matches(const TimelineEntry& entry, const std::string& value) {
    return entry.trace_id == value;
}

}  // namespace

std::vector<TimelineEntry> timeline_for_service(const ObservabilityReport& report,
                                                const std::string& service_name) {
    return filter_timeline(report.timeline, service_matches, service_name);
}

std::vector<TimelineEntry> timeline_for_request(const ObservabilityReport& report,
                                                const std::string& request_id) {
    return filter_timeline(report.timeline, request_matches, request_id);
}

std::vector<TimelineEntry> timeline_for_trace(const ObservabilityReport& report,
                                              const std::string& trace_id) {
    return filter_timeline(report.timeline, trace_matches, trace_id);
}

}  // namespace incidentlab

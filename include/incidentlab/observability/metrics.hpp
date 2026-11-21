#pragma once

#include "incidentlab/event.hpp"
#include "incidentlab/service_result.hpp"
#include "incidentlab/simulation/circuit_breaker.hpp"
#include "incidentlab/simulation/simulation_clock.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace incidentlab {

/// Terminal outcomes of logical requests. Each logical request is counted once.
///
/// Retries are not extra completed requests. Pending and in-progress requests are not included.
/// When completed is 0, success and failure rates are 0. That means the sample is empty,
/// not that the service was observed to be reliable.
struct OutcomeCounts {
    std::size_t completed = 0;
    std::size_t successful = 0;
    std::size_t rejected = 0;
    std::size_t failed = 0;
    std::size_t timed_out = 0;
    std::size_t dropped = 0;
    std::size_t circuit_rejected = 0;
};

/// Latency of completed logical requests, in virtual milliseconds.
///
/// An empty sample leaves every field at 0. Those zeros are not an observed latency.
/// Percentiles use nearest-rank: rank = ceil(percentile * count), index = max(1, rank) - 1.
/// P50, P95, and P99 use the fractions 50/100, 95/100, and 99/100 with integer arithmetic.
struct LatencyMetrics {
    double average_ms = 0.0;
    double min_ms = 0.0;
    double max_ms = 0.0;
    double p50_ms = 0.0;
    double p95_ms = 0.0;
    double p99_ms = 0.0;
};

/// Retry totals derived from the report.
///
/// total_attempts counts starts that reached a service, matching Stage 5.
/// retry_attempts counts RetryScheduled events.
/// requests_requiring_retry counts distinct logical requests that have a RetryScheduled event.
/// recovered_after_retry counts those requests whose final status is Success.
/// exhausted_retries counts terminal failures that used the last allowed attempt.
struct RetryMetrics {
    std::size_t total_attempts = 0;
    std::size_t retry_attempts = 0;
    std::size_t requests_requiring_retry = 0;
    std::size_t recovered_after_retry = 0;
    std::size_t exhausted_retries = 0;
};

/// One service, in alphabetical order when stored on an ObservabilityReport.
struct ServiceMetrics {
    std::string service_name;
    OutcomeCounts outcomes;
    RetryMetrics retries;
    double success_rate_percent = 0.0;
    double failure_rate_percent = 0.0;
    LatencyMetrics latency;
    std::size_t circuit_openings = 0;
    std::size_t dead_lettered_requests = 0;
};

/// Circuit activity for one service. final_state is empty when the report has no snapshot.
struct CircuitMetrics {
    std::string service_name;
    std::size_t opened = 0;
    std::size_t half_opened = 0;
    std::size_t closed = 0;
    std::size_t blocked_requests = 0;
    std::optional<CircuitState> final_state;
};

/// Correlation for one dead-lettered logical request. The full request payload is not copied.
struct DeadLetterObservation {
    std::string request_id;
    std::string trace_id;
    std::string service_name;
    ServiceStatus final_status = ServiceStatus::Failed;
    std::uint64_t failed_at_ms = 0;
    std::size_t attempts = 0;
    std::string reason;
};

/// One history event with the virtual timestamp stored beside it. Order is the history order.
struct TimelineEntry {
    SimulationClock::Time time_ms = 0;
    std::string event_id;
    std::string request_id;
    std::string trace_id;
    std::string service_name;
    EventType type = EventType::RequestReceived;
    std::string message;
};

/// Whole-run metrics. The observation window uses completed logical requests only.
///
/// observation_start_ms is the earliest original start. observation_end_ms is the latest
/// terminal completion. Throughput is completed requests per 1000 ms of that window.
/// It is empty when nothing completed, and when the window duration is zero.
/// It is never infinite and never NaN.
struct GlobalMetrics {
    OutcomeCounts outcomes;
    RetryMetrics retries;
    std::size_t circuit_openings = 0;
    std::size_t circuit_half_openings = 0;
    std::size_t circuit_closings = 0;
    std::size_t dead_lettered_requests = 0;
    double success_rate_percent = 0.0;
    double failure_rate_percent = 0.0;
    std::optional<double> throughput_requests_per_second;
    std::optional<SimulationClock::Time> observation_start_ms;
    std::optional<SimulationClock::Time> observation_end_ms;
    LatencyMetrics latency;
};

/// Owned snapshot. Safe to keep after the SimulationEngine that produced the report is gone.
struct ObservabilityReport {
    GlobalMetrics global;
    std::vector<ServiceMetrics> services;
    std::vector<CircuitMetrics> circuits;
    std::vector<DeadLetterObservation> dead_letters;
    std::vector<TimelineEntry> timeline;
};

std::vector<TimelineEntry> timeline_for_service(const ObservabilityReport& report,
                                                const std::string& service_name);
std::vector<TimelineEntry> timeline_for_request(const ObservabilityReport& report,
                                                const std::string& request_id);
std::vector<TimelineEntry> timeline_for_trace(const ObservabilityReport& report,
                                              const std::string& trace_id);

}  // namespace incidentlab

#include "incidentlab/observability/stage6_demo.hpp"
#include "incidentlab/version.hpp"

#include <exception>
#include <iomanip>
#include <iostream>

namespace {

constexpr const char* kName = "INCIDENTLAB";
constexpr const char* kTagline = "Distributed Systems Failure Simulator";
constexpr const char* kStatus = "Status: Application initialized successfully.";

void print_counts(std::ostream& out, const incidentlab::OutcomeCounts& counts) {
    out << "Completed Requests: " << counts.completed << '\n';
    out << "Successful Requests: " << counts.successful << '\n';
    out << "Rejected Requests: " << counts.rejected << '\n';
    out << "Failed Requests: " << counts.failed << '\n';
    out << "Timed Out Requests: " << counts.timed_out << '\n';
    out << "Dropped Requests: " << counts.dropped << '\n';
    out << "Circuit Rejections: " << counts.circuit_rejected << '\n';
}

void print_latency(std::ostream& out, const incidentlab::LatencyMetrics& latency) {
    out << "Average Latency: " << latency.average_ms << " ms\n";
    out << "Min Latency: " << latency.min_ms << " ms\n";
    out << "Max Latency: " << latency.max_ms << " ms\n";
    out << "P50 Latency: " << latency.p50_ms << " ms\n";
    out << "P95 Latency: " << latency.p95_ms << " ms\n";
    out << "P99 Latency: " << latency.p99_ms << " ms\n";
}

void print_global(std::ostream& out, const incidentlab::GlobalMetrics& global) {
    out << "GLOBAL METRICS\n\n";
    print_counts(out, global.outcomes);
    out << '\n';
    out << "Success Rate: " << global.success_rate_percent << "%\n";
    out << "Failure Rate: " << global.failure_rate_percent << "%\n\n";
    out << "Total Attempts: " << global.retries.total_attempts << '\n';
    out << "Retry Attempts: " << global.retries.retry_attempts << '\n';
    out << "Requests Requiring Retry: " << global.retries.requests_requiring_retry << '\n';
    out << "Recovered After Retry: " << global.retries.recovered_after_retry << '\n';
    out << "Exhausted Retries: " << global.retries.exhausted_retries << "\n\n";
    print_latency(out, global.latency);
    out << '\n';
    if (global.observation_start_ms.has_value() && global.observation_end_ms.has_value()) {
        out << "Observation Window: " << *global.observation_start_ms << " ms to "
            << *global.observation_end_ms << " ms\n";
    } else {
        out << "Observation Window: unavailable\n";
    }
    if (global.throughput_requests_per_second.has_value()) {
        out << "Throughput: " << *global.throughput_requests_per_second << " requests/second\n";
    } else {
        out << "Throughput: unavailable\n";
    }
}

void print_resilience(std::ostream& out, const incidentlab::ObservabilityReport& report) {
    const incidentlab::GlobalMetrics& global = report.global;
    out << "\nRESILIENCE METRICS\n\n";
    out << "Circuit Openings: " << global.circuit_openings << '\n';
    out << "Half-Open Transitions: " << global.circuit_half_openings << '\n';
    out << "Closed Transitions: " << global.circuit_closings << '\n';
    out << "Circuit Rejections: " << global.outcomes.circuit_rejected << '\n';
    out << "Dead-Lettered Requests: " << global.dead_lettered_requests << '\n';
    for (const incidentlab::CircuitMetrics& circuit : report.circuits) {
        out << '\n' << circuit.service_name << " Circuit Breaker\n";
        out << "  Opened: " << circuit.opened << '\n';
        out << "  Half-Open Transitions: " << circuit.half_opened << '\n';
        out << "  Closed Transitions: " << circuit.closed << '\n';
        out << "  Blocked Requests: " << circuit.blocked_requests << '\n';
        if (circuit.final_state.has_value()) {
            out << "  Final State: " << incidentlab::to_string(*circuit.final_state) << '\n';
        } else {
            out << "  Final State: unavailable\n";
        }
    }
    out << "\nDEAD-LETTER QUEUE\n\n";
    out << "Total Entries: " << report.dead_letters.size() << '\n';
    for (const incidentlab::ServiceMetrics& service : report.services) {
        if (service.dead_lettered_requests > 0) {
            out << service.service_name << ": " << service.dead_lettered_requests << '\n';
        }
    }
    for (const incidentlab::DeadLetterObservation& entry : report.dead_letters) {
        out << entry.request_id << " " << entry.trace_id << " " << entry.service_name << " "
            << incidentlab::to_string(entry.final_status) << " attempts " << entry.attempts
            << " at " << entry.failed_at_ms << " ms: " << entry.reason << '\n';
    }
}

void print_service(std::ostream& out, const incidentlab::ServiceMetrics& service) {
    out << '\n' << service.service_name << '\n';
    out << "  Completed Requests: " << service.outcomes.completed << '\n';
    out << "  Successful Requests: " << service.outcomes.successful << '\n';
    out << "  Failed Requests: " << service.outcomes.failed << '\n';
    out << "  Rejected Requests: " << service.outcomes.rejected << '\n';
    out << "  Timed Out Requests: " << service.outcomes.timed_out << '\n';
    out << "  Dropped Requests: " << service.outcomes.dropped << '\n';
    out << "  Circuit Rejections: " << service.outcomes.circuit_rejected << '\n';
    out << "  Success Rate: " << service.success_rate_percent << "%\n";
    out << "  Failure Rate: " << service.failure_rate_percent << "%\n";
    out << "  Total Attempts: " << service.retries.total_attempts << '\n';
    out << "  Retry Attempts: " << service.retries.retry_attempts << '\n';
    out << "  Requests Requiring Retry: " << service.retries.requests_requiring_retry << '\n';
    out << "  Recovered After Retry: " << service.retries.recovered_after_retry << '\n';
    out << "  Exhausted Retries: " << service.retries.exhausted_retries << '\n';
    out << "  Average Latency: " << service.latency.average_ms << " ms\n";
    out << "  P50 Latency: " << service.latency.p50_ms << " ms\n";
    out << "  P95 Latency: " << service.latency.p95_ms << " ms\n";
    out << "  P99 Latency: " << service.latency.p99_ms << " ms\n";
    out << "  Circuit Openings: " << service.circuit_openings << '\n';
    out << "  Dead-Lettered Requests: " << service.dead_lettered_requests << '\n';
}

void print_timeline(std::ostream& out, const incidentlab::ObservabilityReport& report) {
    out << "\nINCIDENT TIMELINE\n\n";
    for (const incidentlab::TimelineEntry& entry : report.timeline) {
        out << "[" << entry.time_ms << " ms] " << entry.service_name << " | " << entry.request_id
            << " | " << entry.trace_id << " | " << incidentlab::to_string(entry.type) << '\n';
    }
}

}  // namespace

int main() {
    std::cout << kName << '\n'
              << kTagline << '\n'
              << "Version " << incidentlab::version_string() << "\n\n"
              << kStatus << '\n';

    try {
        const incidentlab::Stage6Demo demo = incidentlab::run_stage6_demo();
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "\n========== STAGE 6 METRICS & OBSERVABILITY ==========\n\n";
        print_global(std::cout, demo.observability.global);
        print_resilience(std::cout, demo.observability);
        std::cout << "\nPER-SERVICE METRICS\n";
        for (const incidentlab::ServiceMetrics& service : demo.observability.services) {
            print_service(std::cout, service);
        }
        print_timeline(std::cout, demo.observability);
        std::cout << "\n========== END OF REPORT ==========\n";
    } catch (const std::exception& ex) {
        std::cerr << "IncidentLab: stage 6 metrics failed: " << ex.what() << '\n';
        return 1;
    }

    if (!std::cout) {
        std::cerr << "IncidentLab: failed to write the startup message.\n";
        return 1;
    }
    return 0;
}

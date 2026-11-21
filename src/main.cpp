#include "incidentlab/simulation/stage5_demo.hpp"
#include "incidentlab/version.hpp"

#include <cstddef>
#include <exception>
#include <iostream>
#include <map>
#include <string>

namespace {

constexpr const char* kName = "INCIDENTLAB";
constexpr const char* kTagline = "Distributed Systems Failure Simulator";
constexpr const char* kStatus = "Status: Application initialized successfully.";

bool is_attempt_boundary(incidentlab::EventType type) {
    return type == incidentlab::EventType::RequestReceived ||
           type == incidentlab::EventType::RequestDropped ||
           type == incidentlab::EventType::CircuitRejected;
}

const char* outcome_label(incidentlab::EventType type) {
    switch (type) {
    case incidentlab::EventType::RequestCompleted:
        return "SUCCESS";
    case incidentlab::EventType::RequestRejected:
        return "REJECTED";
    case incidentlab::EventType::RequestFailed:
        return "FAILED";
    case incidentlab::EventType::RequestTimedOut:
        return "TIMED OUT";
    case incidentlab::EventType::RequestDropped:
        return "DROPPED";
    case incidentlab::EventType::CircuitRejected:
        return "BLOCKED";
    default:
        return "UNKNOWN";
    }
}

bool is_outcome(incidentlab::EventType type) {
    return type == incidentlab::EventType::RequestCompleted ||
           type == incidentlab::EventType::RequestRejected ||
           type == incidentlab::EventType::RequestFailed ||
           type == incidentlab::EventType::RequestTimedOut ||
           type == incidentlab::EventType::RequestDropped ||
           type == incidentlab::EventType::CircuitRejected;
}

void print_trace(std::ostream& out, const incidentlab::SimulationReport& report) {
    std::map<std::string, std::size_t> attempts;
    const auto& events = report.history.events();
    for (std::size_t index = 0; index < events.size(); ++index) {
        const incidentlab::Event& event = events[index];
        const auto time = report.event_times[index];
        if (is_attempt_boundary(event.type)) {
            ++attempts[event.request_id];
        }
        if (event.type == incidentlab::EventType::RequestReceived) {
            continue;
        }
        if (is_outcome(event.type)) {
            out << "[" << time << " ms] " << event.service_name << " " << event.request_id
                << " - Attempt " << attempts[event.request_id] << ": " << outcome_label(event.type)
                << '\n';
            continue;
        }
        if (event.type == incidentlab::EventType::RetryScheduled) {
            out << "[" << time << " ms] " << event.message << '\n';
            continue;
        }
        if (event.type == incidentlab::EventType::CircuitOpened) {
            out << "[" << time << " ms] Circuit State: OPEN\n";
            continue;
        }
        if (event.type == incidentlab::EventType::CircuitHalfOpened) {
            out << "[" << time << " ms] Circuit State: HALF_OPEN\n";
            continue;
        }
        if (event.type == incidentlab::EventType::CircuitClosed) {
            out << "[" << time << " ms] Circuit State: CLOSED\n";
            continue;
        }
        if (event.type == incidentlab::EventType::DeadLettered) {
            out << "[" << time << " ms] Request moved to DLQ\n";
        }
    }
}

void print_summary(std::ostream& out, const incidentlab::SimulationReport& report) {
    out << "\nProcessed Logical Requests: " << report.processed_requests << '\n';
    out << "Successful Logical Requests: " << report.successful_requests << '\n';
    out << "Failed Logical Requests: " << report.failed_requests << '\n';
    out << "Timed Out Logical Requests: " << report.timed_out_requests << '\n';
    out << "Dropped Logical Requests: " << report.dropped_requests << '\n';
    out << "Circuit Rejections: " << report.circuit_rejected_requests << '\n';
    out << "Total Attempts: " << report.total_attempts << '\n';
    out << "Retry Attempts: " << report.retry_attempts << '\n';
    out << "Dead-Letter Queue Size: " << report.dead_lettered_requests << '\n';
    out << "Final Virtual Time: " << report.current_time << " ms\n";
}

void print_dead_letters(std::ostream& out, const incidentlab::SimulationReport& report) {
    for (const incidentlab::DeadLetterEntry& entry : report.dead_letters) {
        out << "\nRequest: " << entry.request.request_id << '\n';
        out << "Trace: " << entry.request.trace_id << '\n';
        out << "Attempts: " << entry.attempts << '\n';
        out << "Final Status: " << incidentlab::to_string(entry.final_status) << '\n';
        out << "Failed At: " << entry.failed_at_ms << " ms\n";
        out << "Reason: " << entry.reason << '\n';
    }
}

}  // namespace

int main() {
    std::cout << kName << '\n'
              << kTagline << '\n'
              << "Version " << incidentlab::version_string() << "\n\n"
              << kStatus << '\n';

    try {
        const incidentlab::Stage5Demo demo = incidentlab::run_stage5_demo();
        std::cout << "\n========== STAGE 5 RESILIENCE PATTERNS ==========\n\n";
        std::cout << "SCENARIO A: RETRY\n\n";
        print_trace(std::cout, demo.retry);
        print_summary(std::cout, demo.retry);

        std::cout << "\nSCENARIO B: CIRCUIT BREAKER\n\n";
        print_trace(std::cout, demo.circuit_breaker);
        print_summary(std::cout, demo.circuit_breaker);

        std::cout << "\nSCENARIO C: DEAD-LETTER QUEUE\n\n";
        print_trace(std::cout, demo.dead_letter);
        print_dead_letters(std::cout, demo.dead_letter);
        print_summary(std::cout, demo.dead_letter);
        std::cout << "\nSimulation completed.\n";
    } catch (const std::exception& ex) {
        std::cerr << "IncidentLab: stage 5 simulation failed: " << ex.what() << '\n';
        return 1;
    }

    if (!std::cout) {
        std::cerr << "IncidentLab: failed to write the startup message.\n";
        return 1;
    }
    return 0;
}

#include "incidentlab/simulation/stage4_demo.hpp"
#include "incidentlab/version.hpp"

#include <cstddef>
#include <exception>
#include <iostream>
#include <string>

namespace {

constexpr const char* kName = "INCIDENTLAB";
constexpr const char* kTagline = "Distributed Systems Failure Simulator";
constexpr const char* kStatus = "Status: Application initialized successfully.";

bool terminal_at_same_time(const incidentlab::SimulationReport& report, std::size_t index) {
    const incidentlab::Event& event = report.history.events()[index];
    if (event.type != incidentlab::EventType::RequestReceived) {
        return false;
    }
    const auto& events = report.history.events();
    for (std::size_t later = index + 1; later < events.size(); ++later) {
        if (events[later].request_id == event.request_id &&
            report.event_times[later] == report.event_times[index] &&
            events[later].type != incidentlab::EventType::RequestReceived) {
            return true;
        }
    }
    return false;
}

const char* status_line(incidentlab::EventType type) {
    switch (type) {
    case incidentlab::EventType::RequestReceived:
        return "PROCESSING";
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
    }
    return "REJECTED";
}

void print_simulation(std::ostream& out, const incidentlab::SimulationReport& report) {
    out << "\n========== STAGE 4 FAILURE INJECTION ==========\n\n";

    const auto& events = report.history.events();
    for (std::size_t index = 0; index < events.size(); ++index) {
        if (terminal_at_same_time(report, index)) {
            continue;
        }
        out << "[" << report.event_times[index] << " ms] " << events[index].request_id << " -> "
            << events[index].service_name << '\n'
            << "Status: " << status_line(events[index].type) << "\n\n";
    }

    const std::string trace = events.empty() ? "" : events.front().trace_id;
    out << "========== SIMULATION REPORT ==========\n\n";
    out << "Trace: " << trace << "\n\n";
    out << "Processed Requests: " << report.processed_requests << '\n';
    out << "Successful Requests: " << report.successful_requests << '\n';
    out << "Rejected Requests: " << report.rejected_requests << '\n';
    out << "Failed Requests: " << report.failed_requests << '\n';
    out << "Timed Out Requests: " << report.timed_out_requests << '\n';
    out << "Dropped Requests: " << report.dropped_requests << '\n';
    out << "Pending Requests: " << report.pending_requests << '\n';
    out << "In Progress Requests: " << report.in_progress_requests << "\n\n";
    out << "Total Events: " << report.history.size() << '\n';
    out << "Final Virtual Time: " << report.current_time << " ms\n\n";
    out << "Simulation completed.\n";
}

}  // namespace

// Console presentation only. Domain decisions stay in incidentlab_core.
int main() {
    std::cout << kName << '\n'
              << kTagline << '\n'
              << "Version " << incidentlab::version_string() << "\n\n"
              << kStatus << '\n';

    try {
        print_simulation(std::cout, incidentlab::run_stage4_demo());
    } catch (const std::exception& ex) {
        std::cerr << "IncidentLab: stage 4 simulation failed: " << ex.what() << '\n';
        return 1;
    }

    if (!std::cout) {
        std::cerr << "IncidentLab: failed to write the startup message.\n";
        return 1;
    }

    return 0;
}

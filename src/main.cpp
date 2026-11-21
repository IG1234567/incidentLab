#include "incidentlab/simulation/stage3_demo.hpp"
#include "incidentlab/version.hpp"

#include <exception>
#include <iostream>

namespace {

constexpr const char* kName = "INCIDENTLAB";
constexpr const char* kTagline = "Distributed Systems Failure Simulator";
constexpr const char* kStatus = "Status: Application initialized successfully.";

void print_simulation(std::ostream& out, const incidentlab::SimulationReport& report) {
    out << "\n========== STAGE 3 SIMULATION ==========\n\n";
    out << "Simulation started.\n";
    out << "Virtual time: 0 ms\n\n";

    for (const incidentlab::ExecutionRecord& step : report.executions) {
        out << "[" << step.simulation_time << " ms] " << step.request_id << " -> "
            << step.service_name << '\n'
            << "Operation: " << step.operation << '\n'
            << "Status: " << incidentlab::to_string(step.result.status) << "\n\n";
    }

    const std::string trace = report.executions.empty() ? "" : report.executions.front().trace_id;
    out << "========== SIMULATION REPORT ==========\n\n";
    out << "Trace: " << trace << "\n\n";
    out << "Processed Requests: " << report.processed_requests << '\n';
    out << "Successful Requests: " << report.successful_requests << '\n';
    out << "Rejected Requests: " << report.rejected_requests << '\n';
    out << "Pending Requests: " << report.pending_requests << "\n\n";
    out << "Total Events: " << report.history.size() << '\n';
    out << "Final Virtual Time: " << report.current_time << " ms\n\n";
    out << "Simulation completed successfully.\n";
}

}  // namespace

// Console presentation only. Domain decisions stay in incidentlab_core.
int main() {
    std::cout << kName << '\n'
              << kTagline << '\n'
              << "Version " << incidentlab::version_string() << "\n\n"
              << kStatus << '\n';

    try {
        print_simulation(std::cout, incidentlab::run_stage3_demo());
    } catch (const std::exception& ex) {
        std::cerr << "IncidentLab: stage 3 simulation failed: " << ex.what() << '\n';
        return 1;
    }

    if (!std::cout) {
        std::cerr << "IncidentLab: failed to write the startup message.\n";
        return 1;
    }

    return 0;
}

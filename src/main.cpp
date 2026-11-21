#include "incidentlab/stage2_demo.hpp"
#include "incidentlab/version.hpp"

#include <cstddef>
#include <exception>
#include <iostream>
#include <vector>

namespace {

constexpr const char* kName = "INCIDENTLAB";
constexpr const char* kTagline = "Distributed Systems Failure Simulator";
constexpr const char* kStatus = "Status: Application initialized successfully.";

void print_demo(std::ostream& out, const incidentlab::DemoReport& demo) {
    out << "\n========== STAGE 2 DEMO ==========\n\n";
    out << "Trace: " << demo.trace_id << "\n\n";

    for (const incidentlab::DemoStep& step : demo.steps) {
        out << "[" << step.request.request_id << "] " << step.service_name << '\n'
            << "Operation: " << step.request.operation << '\n'
            << "Status: " << incidentlab::to_string(step.result.status) << "\n\n";
    }

    out << "========== EVENT HISTORY ==========\n\n";
    const std::vector<incidentlab::Event>& events = demo.history.events();
    for (std::size_t index = 0; index < events.size(); ++index) {
        if (index > 0 && events[index].request_id != events[index - 1].request_id) {
            out << '\n';
        }
        out << "[" << events[index].event_id << "] " << events[index].request_id << " "
            << incidentlab::to_string(events[index].type) << '\n';
    }

    out << "\nTotal Requests: " << demo.steps.size() << "\nTotal Events: " << demo.history.size()
        << "\n\nStage 2 demonstration completed successfully.\n";
}

}  // namespace

// Console presentation only. Domain decisions stay in incidentlab_core.
int main() {
    std::cout << kName << '\n'
              << kTagline << '\n'
              << "Version " << incidentlab::version_string() << "\n\n"
              << kStatus << '\n';

    try {
        print_demo(std::cout, incidentlab::run_stage2_demo());
    } catch (const std::exception& ex) {
        std::cerr << "IncidentLab: stage 2 demonstration failed: " << ex.what() << '\n';
        return 1;
    }

    if (!std::cout) {
        std::cerr << "IncidentLab: failed to write the startup message.\n";
        return 1;
    }

    return 0;
}

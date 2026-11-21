#include "incidentlab/version.hpp"

#include <iostream>

namespace {

constexpr const char* kName = "INCIDENTLAB";
constexpr const char* kTagline = "Distributed Systems Failure Simulator";
constexpr const char* kStatus = "Status: Application initialized successfully.";

}  // namespace

// Console presentation only. Simulation logic belongs in incidentlab_core, not here.
int main() {
    std::cout << kName << '\n'
              << kTagline << '\n'
              << "Version " << incidentlab::version_string() << "\n\n"
              << kStatus << '\n';

    if (!std::cout) {
        std::cerr << "IncidentLab: failed to write the startup message.\n";
        return 1;
    }

    return 0;
}

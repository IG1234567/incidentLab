#pragma once

#include "incidentlab/simulation/simulation_report.hpp"

namespace incidentlab {

/// Three separate simulations: a recovering retry, a circuit breaker, and a dead letter.
///
/// Scenarios A and B use a service that fails a fixed number of calls, then succeeds.
/// Periodic failure injection cannot express that pattern. Scenario C uses failure injection.
/// Each report is produced by SimulationEngine. The console does not invent the outcomes.
struct Stage5Demo {
    SimulationReport retry;
    SimulationReport circuit_breaker;
    SimulationReport dead_letter;
};

Stage5Demo run_stage5_demo();

}  // namespace incidentlab

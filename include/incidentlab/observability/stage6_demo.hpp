#pragma once

#include "incidentlab/observability/metrics.hpp"
#include "incidentlab/simulation/simulation_report.hpp"

namespace incidentlab {

/// One combined run: successes, a recovered retry, an exhausted retry, and a circuit breaker.
///
/// The numbers in ObservabilityReport are calculated from the engine report. They are not
/// written in advance.
struct Stage6Demo {
    SimulationReport simulation;
    ObservabilityReport observability;
};

Stage6Demo run_stage6_demo();

}  // namespace incidentlab

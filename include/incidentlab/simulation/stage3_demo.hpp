#pragma once

#include "incidentlab/simulation/simulation_report.hpp"

namespace incidentlab {

/// Schedules the three Stage 2 services at 10 ms, 30 ms, and 50 ms and runs the engine.
///
/// The calls are not inserted in time order. The engine sorts them. This replaces the
/// console's Stage 2 script. run_stage2_demo() is still available for the fixed script.
SimulationReport run_stage3_demo();

}  // namespace incidentlab

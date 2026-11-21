#pragma once

#include "incidentlab/simulation/simulation_report.hpp"

namespace incidentlab {

/// Order at 10 ms, Payment timeout at 30 ms, Inventory at 50 ms.
///
/// Payment is received at 30 ms and times out at 130 ms. Inventory finishes at 50 ms,
/// before that timeout. run_stage2_demo() and run_stage3_demo() stay available.
SimulationReport run_stage4_demo();

}  // namespace incidentlab

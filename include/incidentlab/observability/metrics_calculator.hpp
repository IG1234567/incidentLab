#pragma once

#include "incidentlab/observability/metrics.hpp"
#include "incidentlab/simulation/simulation_report.hpp"

namespace incidentlab {

/// Reads a SimulationReport and returns an owned metrics snapshot.
///
/// The report is not modified. Nothing is printed and no clock is read.
/// A report whose history and timestamps disagree, or whose counters disagree with its
/// executions, throws std::logic_error. A completion earlier than its start does too.
class MetricsCalculator {
public:
    static ObservabilityReport calculate(const SimulationReport& report);
};

}  // namespace incidentlab

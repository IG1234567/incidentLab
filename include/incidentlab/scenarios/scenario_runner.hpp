#pragma once

#include "incidentlab/scenarios/comparison_report.hpp"

#include <vector>

namespace incidentlab {

/// Builds a fresh engine for one scenario, runs the shared workload, and returns owned metrics.
///
/// The engine, the services, the breaker, the retry state, and the dead-letter queue exist
/// only for that call. A later scenario cannot see them. The config is taken by const
/// reference and is not changed.
class ScenarioRunner {
public:
    ScenarioResult run(const ScenarioConfig& config) const;
};

/// Validates every config, then runs each one on its own engine. An empty list returns an
/// empty report. Duplicate ids and invalid policies throw std::invalid_argument before any run.
ComparisonReport run_comparison(const std::vector<ScenarioConfig>& scenarios);

/// The four predefined scenarios, in baseline, retry, circuit, full order.
ComparisonReport run_predefined_comparison();

}  // namespace incidentlab

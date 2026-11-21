#pragma once

#include "dashboard_renderer.hpp"

#include "incidentlab/scenarios/comparison_report.hpp"

namespace incidentlab {

/// Clicks on the scenario row and the rerun control. Does not touch the live simulation.
void update_comparison(ComparisonView& view);

/// Draws the cached comparison. Does not run the scenarios again.
void draw_comparison(const ComparisonView& view, GuiMode mode);

}  // namespace incidentlab

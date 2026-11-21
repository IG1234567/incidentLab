#pragma once

#include "incidentlab/visualization/simulation_controller.hpp"

namespace incidentlab {

/// Reads keyboard and mouse input and updates the controller. Does not draw.
void update_dashboard(SimulationController& controller);

/// Draws one frame from the snapshot. Does not change the simulation.
void draw_dashboard(const VisualizationSnapshot& snapshot);

}  // namespace incidentlab

#pragma once

#include "incidentlab/visualization/simulation_controller.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "raylib.h"

namespace incidentlab {

enum class GuiMode { LiveSimulation, ScenarioComparison };

struct ModeButtons {
    Rectangle live;
    Rectangle comparison;
};

/// The two mode buttons under the title. Geometry matches the live layout.
ModeButtons mode_buttons();

/// True when the click was on a mode button. The mode is updated, and the caller
/// should not also treat that click as a simulation control.
bool consume_mode_click(GuiMode& mode);

void draw_mode_buttons(const ModeButtons& buttons, GuiMode mode);

/// Reads keyboard and mouse input and updates the controller. Does not draw.
void update_dashboard(SimulationController& controller);

/// Draws one frame from the snapshot, including the mode buttons. Does not change the simulation.
void draw_dashboard(const VisualizationSnapshot& snapshot, GuiMode mode);

}  // namespace incidentlab

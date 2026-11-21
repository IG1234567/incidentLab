#include "comparison_renderer.hpp"
#include "dashboard_renderer.hpp"
#include "ui_font.hpp"

#include "incidentlab/scenarios/comparison_report.hpp"
#include "incidentlab/visualization/simulation_controller.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "raylib.h"

#include <cstdio>

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI);
    InitWindow(1440, 900, "INCIDENTLAB");
    if (!IsWindowReady()) {
        std::fprintf(stderr, "INCIDENTLAB: the window could not be created.\n");
        return 1;
    }
    SetWindowMinSize(1024, 700);
    SetTargetFPS(60);
    incidentlab::load_ui_fonts();

    incidentlab::SimulationController controller;
    incidentlab::ComparisonView comparison = incidentlab::ComparisonView::predefined();
    incidentlab::GuiMode mode = incidentlab::GuiMode::LiveSimulation;
    while (!WindowShouldClose()) {
        const bool switched = incidentlab::consume_mode_click(mode);
        if (mode == incidentlab::GuiMode::LiveSimulation) {
            if (!switched) {
                incidentlab::update_dashboard(controller);
            }
            controller.tick(static_cast<double>(GetFrameTime()));
            BeginDrawing();
            incidentlab::draw_dashboard(controller.snapshot(), mode);
            EndDrawing();
        } else {
            if (!switched) {
                incidentlab::update_comparison(comparison);
            }
            BeginDrawing();
            incidentlab::draw_comparison(comparison, mode);
            EndDrawing();
        }
    }
    incidentlab::unload_ui_fonts();
    CloseWindow();
    return 0;
}

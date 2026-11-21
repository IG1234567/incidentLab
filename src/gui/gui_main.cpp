#include "dashboard_renderer.hpp"

#include "incidentlab/visualization/simulation_controller.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "raylib.h"

#include <cstdio>

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1440, 900, "INCIDENTLAB");
    if (!IsWindowReady()) {
        std::fprintf(stderr, "INCIDENTLAB: the window could not be created.\n");
        return 1;
    }
    SetWindowMinSize(1024, 700);
    SetTargetFPS(60);

    incidentlab::SimulationController controller;
    while (!WindowShouldClose()) {
        incidentlab::update_dashboard(controller);
        controller.tick(static_cast<double>(GetFrameTime()));
        BeginDrawing();
        incidentlab::draw_dashboard(controller.snapshot());
        EndDrawing();
    }
    CloseWindow();
    return 0;
}

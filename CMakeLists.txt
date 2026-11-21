cmake_minimum_required(VERSION 3.20)

# Reject this before project(), so a mistaken `cmake .` does not run the compiler
# check inside the source tree.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are not supported. Configure with: cmake -S . -B build")
endif()

project(
    IncidentLab
    VERSION 0.1.0
    DESCRIPTION "Distributed Systems Failure Simulator"
    LANGUAGES CXX
)

# C++17 for every target. Extensions (gnu++17) stay off so the code remains portable.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# incidentlab_core holds simulation code from Stage 2 onward.
# The executable is only a presentation shell and links this library.
function(incidentlab_apply_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(incidentlab_core STATIC
    src/version.cpp
    src/request.cpp
    src/order_service.cpp
    src/payment_service.cpp
    src/inventory_service.cpp
    src/event.cpp
    src/event_history.cpp
    src/stage2_demo.cpp
    src/simulation/simulation_clock.cpp
    src/simulation/event_scheduler.cpp
    src/simulation/circuit_breaker.cpp
    src/simulation/dead_letter_queue.cpp
    src/simulation/simulation_engine.cpp
    src/simulation/stage3_demo.cpp
    src/simulation/stage4_demo.cpp
    src/simulation/stage5_demo.cpp
    src/observability/metrics_calculator.cpp
    src/observability/stage6_demo.cpp
    src/visualization/visualization_model.cpp
    src/visualization/simulation_controller.cpp
    src/scenarios/scenario_runner.cpp
    src/scenarios/comparison_report.cpp
    src/scenarios/stage8_demo.cpp
)
add_library(incidentlab::core ALIAS incidentlab_core)

target_include_directories(incidentlab_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# Single source of truth: the VERSION in project() above.
target_compile_definitions(incidentlab_core
    PRIVATE
        INCIDENTLAB_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        INCIDENTLAB_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        INCIDENTLAB_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

target_compile_features(incidentlab_core PUBLIC cxx_std_17)
incidentlab_apply_warnings(incidentlab_core)

add_executable(incidentlab
    src/main.cpp
)

target_link_libraries(incidentlab PRIVATE incidentlab::core)
target_compile_features(incidentlab PRIVATE cxx_std_17)
incidentlab_apply_warnings(incidentlab)

# In-tree assertions registered with CTest. GoogleTest is not fetched:
# the build must succeed without a network download.
enable_testing()

add_executable(incidentlab_tests
    tests/domain_tests.cpp
    tests/simulation_tests.cpp
    tests/failure_tests.cpp
    tests/resilience_tests.cpp
    tests/observability_tests.cpp
    tests/visualization_tests.cpp
    tests/scenario_comparison_tests.cpp
)
target_link_libraries(incidentlab_tests PRIVATE incidentlab::core)
target_include_directories(incidentlab_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_features(incidentlab_tests PRIVATE cxx_std_17)
incidentlab_apply_warnings(incidentlab_tests)
add_test(NAME incidentlab_domain COMMAND incidentlab_tests)

# The window is optional. Debug and Release presets leave this OFF, so they do not need raylib.
# IncidentLab does not download raylib.
option(INCIDENTLAB_BUILD_GUI "Build the IncidentLab raylib GUI" OFF)

if(INCIDENTLAB_BUILD_GUI)
    find_package(raylib CONFIG QUIET)
    if(NOT raylib_FOUND)
        find_package(raylib QUIET)
    endif()
    if(NOT TARGET raylib AND NOT TARGET raylib::raylib)
        message(FATAL_ERROR
            "INCIDENTLAB_BUILD_GUI is ON, but raylib was not found.\n"
            "Install raylib locally and point CMake at it, for example with "
            "-Draylib_DIR=<prefix>/lib/cmake/raylib.\n"
            "The core library, the console executable, and the tests do not need raylib. "
            "Configure with -DINCIDENTLAB_BUILD_GUI=OFF to build those.\n"
            "IncidentLab does not download raylib.")
    endif()

    add_executable(incidentlab_gui
        src/gui/gui_main.cpp
        src/gui/ui_font.cpp
        src/gui/dashboard_renderer.cpp
        src/gui/comparison_renderer.cpp
    )
    target_include_directories(incidentlab_gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/gui)
    target_link_libraries(incidentlab_gui PRIVATE incidentlab::core)
    if(TARGET raylib::raylib)
        target_link_libraries(incidentlab_gui PRIVATE raylib::raylib)
    else()
        target_link_libraries(incidentlab_gui PRIVATE raylib)
    endif()
    target_compile_features(incidentlab_gui PRIVATE cxx_std_17)
    target_compile_definitions(incidentlab_gui PRIVATE
        "INCIDENTLAB_FONT_DIR=\"${CMAKE_SOURCE_DIR}/assets/fonts\"")
    add_custom_command(TARGET incidentlab_gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:incidentlab_gui>/fonts
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/assets/fonts/DejaVuSans.ttf
            ${CMAKE_SOURCE_DIR}/assets/fonts/DejaVuSans-Bold.ttf
            ${CMAKE_SOURCE_DIR}/assets/fonts/LICENSE
            $<TARGET_FILE_DIR:incidentlab_gui>/fonts
        VERBATIM)
    incidentlab_apply_warnings(incidentlab_gui)
endif()

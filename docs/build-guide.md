# Build guide

IncidentLab builds out of source with CMake 3.20 or newer. The language standard is C++17. Extensions are off. In-source builds are rejected.

This guide was checked on Ubuntu 24.04 with GCC 13.3 and Clang 18.1. A Windows MSVC build is accepted by the warning flags in `CMakeLists.txt` and was not run for this release check.

## Requirements

| Tool | Required for | Notes |
| --- | --- | --- |
| A C++17 compiler | Every target | GCC and Clang are verified. |
| CMake 3.20+ | Every target | Presets live in `CMakePresets.json`. |
| Make | The default generator | The presets do not force Ninja. |
| Python 3 | The `coverage` target only | Used by `tools/coverage_summary.py`. |
| raylib | `incidentlab_gui` only | Not downloaded by CMake. |
| A display | Running the GUI | Not required to compile the core or the tests. |

Installing system packages such as `build-essential` and `cmake` needs administrator privileges. Configuring and building the project does not.

The default presets set `CMAKE_BUILD_TYPE` only. They leave the GUI, sanitizers, and coverage off.

## Headless debug build

```bash
cmake --preset debug
cmake --build --preset debug
```

The binaries are `build/debug/incidentlab` and `build/debug/incidentlab_tests`.

## Release build

```bash
cmake --preset release
cmake --build --preset release
ctest --test-dir build/release --output-on-failure
```

## Tests

```bash
ctest --test-dir build/debug --output-on-failure
```

Run one suite, or list every case:

```bash
./build/debug/incidentlab_tests --list
./build/debug/incidentlab_tests --suite quality
```

With no arguments the executable runs all 245 cases.

## GUI

```bash
cmake -S . -B build/gui \
  -DCMAKE_BUILD_TYPE=Debug \
  -DINCIDENTLAB_BUILD_GUI=ON
cmake --build build/gui
./build/gui/incidentlab_gui
```

If raylib is installed in a custom prefix:

```bash
cmake -S . -B build/gui \
  -DCMAKE_BUILD_TYPE=Debug \
  -DINCIDENTLAB_BUILD_GUI=ON \
  -DCMAKE_PREFIX_PATH=/path/to/raylib
```

The build copies DejaVu Sans, DejaVu Sans Bold, and `assets/fonts/LICENSE` next to the executable. Ubuntu 24.04's default archives did not supply a CMake package for raylib on the machine used for this check, so that build passed a local prefix. Do not expect `apt install libraylib-dev` to work everywhere.

## Sanitizers

Use GCC. Its packages include the AddressSanitizer and UndefinedBehaviorSanitizer runtimes. Do not combine this option with coverage.

```bash
cmake -S . -B build/sanitizers \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=g++ \
  -DINCIDENTLAB_BUILD_GUI=OFF \
  -DINCIDENTLAB_ENABLE_SANITIZERS=ON
cmake --build build/sanitizers
ctest --test-dir build/sanitizers --output-on-failure
```

Leak detection stays on. A sanitizer finding fails the test process. Clang can compile the normal Debug build. A Clang sanitizer build also needs the matching compiler-rt libraries, which were not present on the check machine.

## Coverage

```bash
cmake -S . -B build/coverage \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=g++ \
  -DINCIDENTLAB_BUILD_GUI=OFF \
  -DINCIDENTLAB_ENABLE_COVERAGE=ON
cmake --build build/coverage
cmake --build build/coverage --target coverage
```

The target runs CTest and writes `build/coverage/coverage.txt`. `gcov` comes with GCC. `gcovr` is not required.

## Warnings as errors

The workflow passes `-DINCIDENTLAB_WARNINGS_AS_ERRORS=ON` for the Debug, Release, Clang, and sanitizer jobs. A local preset build does not, unless you pass the same option.

## CMake options

| Option | Default | Effect |
| --- | --- | --- |
| `INCIDENTLAB_BUILD_GUI` | OFF | Builds `incidentlab_gui` if raylib is found. |
| `INCIDENTLAB_ENABLE_SANITIZERS` | OFF | Adds `-fsanitize=address,undefined` to the core and its dependents. |
| `INCIDENTLAB_ENABLE_COVERAGE` | OFF | Adds `--coverage` to the core and its dependents. |
| `INCIDENTLAB_WARNINGS_AS_ERRORS` | OFF | Adds `-Werror` or `/WX`. |

## Troubleshooting

**CMake is not found.** Install CMake 3.20 or newer with the system package manager. That install step may require `sudo`. The project build itself does not.

**The compiler is not found.** Install `g++` or `clang++`. Select it with `-DCMAKE_CXX_COMPILER=` when configuring a fresh directory. Do not change the compiler in a directory that was already configured for a different one. Delete that build directory and configure again.

**raylib is not found.** Configure with `-DINCIDENTLAB_BUILD_GUI=OFF` to build the console and the tests. For the window, install raylib so its CMake package is on `CMAKE_PREFIX_PATH`. IncidentLab does not fetch raylib.

**The GUI does not open.** It needs a working display. On WSL that usually means WSLg or an X server, and `DISPLAY` must be set. A missing font is not fatal: the window falls back to raylib's built-in bitmap, which looks coarse when scaled. The bundled DejaVu files avoid that when the post-build copy succeeds.

**In-source configuration fails.** `cmake .` is rejected. Use `cmake -S . -B build` or a preset.

**A build directory remembers the wrong compiler or options.** Sanitizers, coverage, and a normal Debug build need separate directories. Remove the directory and configure it again. Do not turn sanitizers and coverage on together.

**WSL is slow.** Building on the Linux filesystem is faster than building on a Windows mount such as `/mnt/c`. The project does not require a particular disk, but a mount across the Windows boundary makes compilation slower.

**Preset configure mentions an unused `CMAKE_C_COMPILER`.** The project enables only CXX. Set `CMAKE_CXX_COMPILER`. An unused C compiler variable is a CMake warning, not a failed build.

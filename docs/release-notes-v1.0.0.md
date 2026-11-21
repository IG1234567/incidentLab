# IncidentLab v1.0.0

The console prints version 1.0.0. The window does not display a version.

## Overview

IncidentLab is a deterministic C++17 simulator of a small service call path. It runs in one process. Virtual time replaces the wall clock. The project is an educational and portfolio artifact: it shows how failure injection, retry, a circuit breaker, and a dead-letter queue change measurable outcomes.

## Main features

- Request, service, and event models, with calls made through `Service::handle`.
- A discrete-event engine and scheduler.
- Service failure, slow response, timeout, and dropped request.
- Retry with exponential backoff, a per-service circuit breaker, and an in-memory dead-letter queue.
- Rates, nearest-rank latency, throughput, and an incident timeline.
- An optional raylib dashboard.
- A four-strategy comparison on one shared workload.
- Headless tests, sanitizers, coverage, and GitHub Actions.

## Simulation

Time starts at 0 and never moves backward. The scheduler orders equal timestamps by insertion. `run` drains the queue. `step_once` runs one action and is what the window's Step control uses. A thrown exception from `handle` leaves the start queued. A simulated failure is a status, not an exception.

One service has one failure policy. Configuring another policy replaces the first.

## Resilience

Retry applies to `Failed`, `TimedOut`, and `Dropped`. It does not apply to `Rejected` or to an unexpected exception. The breaker opens on consecutive finished failures, rejects new attempts while open, and allows one probe at `opened_at + reset_timeout`. A terminal failure is dead-lettered only when that service has retry or a breaker. The queue is not replayed.

## Visualization

`incidentlab_gui` is optional and needs a local raylib. The core does not link it. The window plays a fixed scenario, trace `TRACE-VIZ`, and can switch to the comparison without resetting the live controller. Screenshots from that window are in `docs/screenshots/`.

## Scenario comparison

`incidentlab --compare` and the comparison mode of the window report Baseline, Retry Only, Circuit Breaker, and Full Resilience. On the locked workload the success rates are 40%, 70%, 30%, and 40%. Full Resilience does not improve on Baseline's success rate. Differences in success rate are percentage points.

## Testing and CI

The test executable contains 245 cases in eight CTest suites. After the version change, this check passed GCC Debug, GCC Release, the GUI target, GCC sanitizers, and coverage generation, with no compiler warnings. Regenerated core coverage was 84.3% of lines (1472/1747) and 91.8% of functions (167/182). GUI sources and tests are excluded.

GitHub Actions on commit `09dca6f` succeeded for GCC Debug, GCC Release, Clang Debug, GCC ASan and UBSan, and GCC coverage: https://github.com/IG1234567/incidentLab/actions/runs/44957235284. All five jobs succeeded.

## Build requirements

CMake 3.20 or newer, a C++17 compiler, and a build directory that is not the source tree. The verified compiler runs were GCC 13.3 and Clang 18.1 on Ubuntu 24.04. raylib is required only for the window. Sanitizer and coverage jobs use GCC. See `docs/build-guide.md`.

## Known limitations

- The simulator does not open sockets, store data, or call a cloud API.
- The application uses the MIT License, Copyright (c) 2026 Inga Gaidukove. DejaVu fonts in `assets/fonts/` keep their own notice.
- MSVC was not built in this check.
- Headless tests do not prove the pixels of the window. The screenshots were captured separately from the running application.
- Coverage below 100% is reported as measured. No gate was added.
- A clean sanitizer run does not prove every future lifetime is safe.

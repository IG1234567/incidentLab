# Design decisions

Each decision below is one that the code actually follows.

## Discrete-event simulation

**Context.** A failure that lasts 60 seconds, or a retry that waits 100 ms, has to be repeatable in a test. Waiting on the wall clock would make the suite slow and sensitive to the machine.

**Decision.** `SimulationClock` stores virtual milliseconds. The scheduler jumps to the next event. `SlowResponse` of 60 seconds advances the virtual clock and returns immediately.

**Benefits.** Tests can assert exact timestamps. A long delay costs no real time. Overlapping calls stay ordered by the scheduler instead of by threads.

**Trade-offs.** The model does not represent CPU scheduling, queues inside an operating system, or network delay distributions. Virtual time is only as detailed as the events the engine schedules.

## C++17

**Context.** The project needs a typed domain, deterministic resource lifetimes, and a build that does not download a framework.

**Decision.** The code is C++17 with extensions disabled. It uses the standard library: `std::vector`, `std::optional`, `std::unique_ptr`, `std::unordered_map`, and `std::priority_queue`. CMake is the build system.

**Benefits.** Types such as `Request` and `ServiceResult` make illegal states visible. RAII matches the ownership rules. The tests compile as a normal executable.

**Trade-offs.** C++17 does not give the project a package manager or a GUI. raylib is optional and isolated. Error handling mixes exceptions for programmer errors with status codes for simulated failures. That split is easy to miss if a new failure is reported by throwing.

## Deterministic failures

**Context.** A resilience comparison is useful only when the workload and the faults are the same for every strategy.

**Decision.** Failure policies are data: a type, a delay, a timeout, and `every_nth_request`. The Stage 8 workload goes further and scripts the status of each request id on each attempt. Nothing is random.

**Benefits.** A failing test names the request and the timestamp. Two runs of the same scenario match. The comparison can attribute a change to retry or to the breaker, not to a different fault draw.

**Trade-offs.** The simulator does not show rare timing accidents or partial network partitions. `every_nth_request` counts deliveries, including retries, so it is not a stable "fail this logical request" switch. The comparison therefore uses the per-request script.

## GUI separated from the core

**Context.** The window needs a display and raylib. The tests and the continuous-integration jobs should not.

**Decision.** `incidentlab_core` has no raylib include. `incidentlab_gui` is built only when `INCIDENTLAB_BUILD_GUI` is ON and raylib is already installed. `SimulationController` lives in the core and is tested without a window. The GUI draws a snapshot.

**Benefits.** Debug, Release, sanitizer, and coverage jobs stay headless. A controller bug can be asserted without pixel tests. The core can be reused by the console.

**Trade-offs.** Headless tests do not prove that text fits, that a click lands, or that the font is sharp. Those checks are manual. The GUI sources are outside the published coverage number.

## Retry and the circuit breaker

**Context.** Repeating a failed call and stopping calls to a failing dependency solve different problems.

**Decision.** Retry schedules another attempt after a virtual backoff. The circuit breaker counts consecutive finished failures and then rejects new attempts without calling `handle`. They can be enabled separately. If a failure opens the circuit, the engine does not also schedule the retry.

**Benefits.** The comparison can show the cost of each mechanism. Retry can raise the success count and the latency. The breaker can lower the attempt count by rejecting work. The metrics keep those outcomes in different fields.

**Trade-offs.** The breaker does not measure error rate over a sliding window. It uses a consecutive count. The dead-letter queue is not a third independent switch: a terminal failure is stored when retry or a breaker is configured. Baseline, which has neither, records no dead letters.

## Scenario comparison

**Context.** Reading one simulation trace does not show whether a strategy helped.

**Decision.** Four named configurations share `comparison_workload()` and `comparison_faults()`. `ScenarioRunner` builds a new engine for each one and stores a `ScenarioResult`. Deltas use the Baseline result. Success-rate differences are percentage points.

**Benefits.** The table is reproducible. Tests lock the exact counts. The GUI can show the bars without running the live scenario.

**Trade-offs.** The numbers belong to this workload. Full Resilience is not the winner here: it stays at four successes because the breaker blocks later retries. A different fault script would need its own expected values. The tests do not treat "highest success rate" as the definition of success.

## CTest and GitHub Actions

**Context.** The project needs a repeatable check that fails on a broken assertion, a sanitizer report, or a warning in the workflow, without fetching GoogleTest.

**Decision.** Tests are functions registered by `INCIDENTLAB_TEST` in one executable. CTest runs that executable once per source file through `--suite`. The workflow runs GCC Debug, GCC Release, Clang Debug, GCC sanitizers, and GCC coverage. Sanitizers and coverage are off unless the matching CMake option is ON.

**Benefits.** A laptop without GitHub can build and test. CI uses the same commands. The suite name in the CTest output points at the file. Sanitizer failures fail the process because leak detection and `halt_on_error` stay on.

**Trade-offs.** CTest shows eight lines, not 245. The executable prints the individual names. Coverage does not include the GUI. A green run applies to the commit that was built. The workflow on `92db659` passed. Later local edits, including the version change to 1.0.0, are not in that run until they are pushed.

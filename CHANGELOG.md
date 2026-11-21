# Changelog

The version in this file is the release candidate. It has not been tagged or published. The console reports `1.0.0` from `project(VERSION)` in `CMakeLists.txt`.

## [1.0.0] — Unreleased

### Added

- A C++17 core library, a console executable, and an optional raylib window.
- Domain types for requests, services, results, events, and event history.
- A discrete-event engine with a virtual clock and a stable scheduler.
- Failure injection for service failure, slow response, timeout, and dropped requests.
- Retry with exponential backoff, a per-service circuit breaker, and an in-memory dead-letter queue.
- Metrics for rates, nearest-rank latency, throughput, retries, circuit activity, and an incident timeline.
- A headless simulation controller and a raylib dashboard for playback, topology, and the timeline.
- A comparison of Baseline, Retry Only, Circuit Breaker, and Full Resilience on one deterministic workload.
- CTest suites, optional AddressSanitizer and UndefinedBehaviorSanitizer, optional `gcov` coverage, and a GitHub Actions workflow.

### Documentation

- Architecture, object-oriented design, and design decisions.
- User guide, build guide, and testing guide.
- Demonstration script, portfolio text, release checklist, and v1.0.0 release-candidate notes.
- Screenshots captured from the raylib window.

### Not in this candidate

- A Git tag or a GitHub Release. The MIT License is present, and the release is still unpublished.

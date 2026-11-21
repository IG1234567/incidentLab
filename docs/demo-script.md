# Demonstration script

A four-to-five minute walkthrough of the application as it exists. Times are a guide. Use a Debug build of the console and a built `incidentlab_gui`. The window opens at 1440×900.

Before you start, have these ready:

- `./build/debug/incidentlab`
- `./build/debug/incidentlab --compare`
- `./build/gui/incidentlab_gui`
- The GitHub Actions run for commit `36ae182`: https://github.com/IG1234567/incidentLab/actions/runs/44953893069

Do not press Escape until you intend to close the window.

## 0:00–0:30 — Introduction

IncidentLab is a single-process simulator of a small service call path. It asks what happens when a payment call fails, an order times out, or inventory is retried, and it answers with virtual time rather than with a deployed system.

The interesting part is not the happy path. It is whether retry, a circuit breaker, or both, change the number of successful calls, the latency, and the number of attempts. The same engine produces the console report, the window, and the comparison table.

## 0:30–1:15 — Architecture

Show the layer diagram in `docs/architecture.md`.

Three concrete services implement `Service`. The engine holds a non-owning reference and calls `handle`. Failure policies, retry settings, and circuit breakers are optional data on a service name. `MetricsCalculator` reads the finished report and returns a value, so the numbers remain after the engine is gone.

The window is a separate executable. It is not in the headless CI jobs. That split is why the tests can run without a display.

Mention one ownership rule: the services are members before the engine, so the engine is destroyed first.

## 1:15–2:15 — Live simulation

Launch `./build/gui/incidentlab_gui`. The header says Ready and 0 ms.

Press N once. One order request is received and completed at 0 ms. OrderService becomes healthy. Throughput stays unavailable because the window has zero duration.

Keep pressing N. The visualization scenario, trace `TRACE-VIZ`, then shows the behaviors that are actually configured:

- Order deliveries time out on every second match. The node can show TIMED OUT.
- Payment fails, opens its circuit after two failures, and later rejects a call. The node can show CIRCUIT OPEN.
- Inventory retries. A recovered call and a later failure both appear in the timeline, including `RetryScheduled` and `DeadLettered` when the rules record them.

Click PaymentService. The footer names the service and shows its success rate, failure rate, breaker state, rejections, and dead-letter count. Point out that a rejection is not a success.

Click a timeline row if you want a selected event id. Press R to show that Reset returns to Ready at 0 ms without leaving the old requests behind.

Speed can be mentioned in one sentence: it changes how quickly Running mode steps, not the virtual timestamps.

## 2:15–3:15 — Scenario comparison

Leave the live view in a non-zero state, then click SCENARIO COMPARISON. The live controller is not reset.

The four buttons are Baseline, Retry Only, Circuit Breaker, and Full Resilience. The bars are success rates: 40%, 70%, 30%, and 40% on the locked workload. The table uses the Stage 6 definitions.

Select Baseline and read +0.00 percentage points. Select Retry Only and read +30.00 percentage points, +6 attempts, and +75% relative success-rate change. Say explicitly that the extra successes cost latency: average latency moves from 20 ms to 92 ms.

Select Circuit Breaker. Success falls to 30%. Attempts fall to 9. One request is rejected without being served. The lower latency is not a faster success. It includes an immediate rejection.

Select Full Resilience. The success rate returns to 40%, not above Baseline. The breaker blocks later retries. That is the result of this fault script, not a general claim that combining both mechanisms is best.

Click LIVE SIMULATION. The clock and the timeline are the ones you left, not the comparison's `REQ-001` workload.

If you also want the text form, show `./build/debug/incidentlab --compare` and point at the same rates.

## 3:15–4:00 — Testing and CI

Open the Actions run. Five jobs are green on `36ae182`: GCC Debug, GCC Release, Clang Debug, GCC ASan and UBSan, and GCC coverage.

Locally, `ctest --test-dir build/debug --output-on-failure` reports eight suites. Say that each suite is a source file and that the executable contains 245 cases. `./build/debug/incidentlab_tests --list` shows the names if someone asks.

The coverage line to quote is the regenerated figure: 84.3% of lines and 91.8% of functions in `incidentlab_core`. Tests and the GUI are excluded. A sanitizer pass means this run reported no address or undefined-behavior error. It does not mean every future change is safe.

## 4:00–5:00 — Conclusion

The hard parts were keeping one definition of a completed request, making retry and the breaker share a virtual clock, and stopping the comparison from leaking state between strategies. The design choice that makes the tests possible is determinism: the same request ids fail on the same attempts every time.

What this project practices is object-oriented C++, a discrete-event model, measurable resilience patterns, and a presentation layer that can be absent in CI.

What it does not do: talk to real services, replay the dead-letter queue, or claim that Full Resilience wins on every workload. A license has not been chosen. The binary still reports 0.1.0. Those are the release notes, not missing simulation features.

Possible later work, not part of the demo: a chosen license, a version bump, dead-letter replay, and a verified Windows build.

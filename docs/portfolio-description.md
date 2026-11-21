# Portfolio description

IncidentLab is an independent educational and portfolio project. It is not a production system and not a job engagement.

## GitHub description

160 characters is the field limit. This text is 149 characters:

```text
Deterministic C++17 distributed systems failure simulator with resilience patterns, raylib visualization, scenario comparison, and automated testing.
```

## Suggested topics

These are recommendations. They were not applied to the GitHub repository from this checkout.

```text
cpp
cpp17
object-oriented-programming
distributed-systems
simulation
resilience
circuit-breaker
retry-pattern
raylib
cmake
github-actions
```

## CV

- Built IncidentLab, a deterministic C++17 discrete-event simulator of service calls under injected failures.
- Modeled retry with exponential backoff, a circuit breaker, and a dead-letter queue, and compared four strategies on one fixed workload.
- Tested the core with CTest, AddressSanitizer, UndefinedBehaviorSanitizer, and GitHub Actions. An optional raylib view plays the same engine without becoming a build dependency.

## LinkedIn

IncidentLab is an independent C++17 project I built to study a small service path under failure. It is a simulator, not a deployed system. A virtual clock runs requests against interchangeable service objects. Failures, timeouts, slow responses, and dropped calls are configured data, so a trace can be repeated exactly.

Retry and a circuit breaker are separate. Retry spends attempts to recover a call. The breaker stops calling a dependency after consecutive failures. A comparison runs Baseline, Retry Only, Circuit Breaker, and Full Resilience on one workload. On that workload, retry raises both the success rate and the latency. Using both mechanisms does not beat the baseline success rate, because the open circuit blocks later retries.

Metrics come from the finished history: rates, latency percentiles, throughput, and a timeline. An optional raylib window plays one scenario. The core does not link the GUI, so tests stay headless. GitHub Actions builds with GCC and Clang, runs sanitizers, and reports core coverage. It was practice in object-oriented design and in checking behavior that is easy to get wrong.

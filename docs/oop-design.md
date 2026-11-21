# Object-oriented design

IncidentLab uses a small set of types with explicit ownership. The examples below are the production types, not a separate sample.

## Encapsulation

`SimulationClock` keeps its timestamp private. Callers can read `now()` and move forward with `advance_to`. They cannot assign the clock backward, and there is no public field to edit.

`CircuitBreaker` hides the consecutive-failure count, the time it opened, and whether a probe is in flight. Callers ask `admission(now)` and then record an outcome with `note_failure` or `note_success`. The transition rules stay in one class.

`DeadLetterQueue` rejects a second entry with the same request id inside `push`. Callers cannot append to the vector directly.

`Request` and `Event` check their identifiers in the constructor. An empty request id never becomes a valid object.

## Abstraction

`Service` is the engine's view of a callee:

```cpp
class Service {
public:
    virtual ~Service() = default;
    virtual std::string name() const = 0;
    virtual ServiceResult handle(const Request& request) = 0;
};
```

`SimulationEngine::register_service(Service&)` does not mention `OrderService`. `MetricsCalculator` reads a finished `SimulationReport`. It does not know how the engine scheduled the work. The console formats an `ObservabilityReport`. It does not calculate rates itself.

## Inheritance

Inheritance is used where a caller must treat several callees uniformly.

`OrderService`, `PaymentService`, and `InventoryService` derive from `Service` and are marked `final`. The visualization controller and the scenario runner also define local `Service` subclasses that script a sequence of statuses. Those subclasses exist so the engine can be given a deterministic callee without changing the engine.

Inheritance is not used for the clock, the scheduler, the metrics report, or the comparison result. Those are concrete types. A deeper hierarchy would not remove any real variation.

## Polymorphism

The engine calls `service.handle(request)` through a base reference. The concrete class supplies the result. Replacing `PaymentService` with a test double does not require an edit in the scheduler.

The test executable also uses `std::unique_ptr<Service>` to call `OrderService` through the base. That is runtime polymorphism. Compile-time polymorphism appears in the test harness, where a small function template checks that a call throws. The simulation itself does not depend on that template.

## Composition

`SimulationEngine` contains a clock, a scheduler, an event history, failure policies, retry settings, circuit breakers, and a dead-letter queue. It does not inherit from any of them.

`SimulationController` contains a session. The session contains the service objects and then the engine. `ComparisonReport` contains `ScenarioResult` values. Each result contains a copy of the scenario configuration and an `ObservabilityReport`.

The window composes panels. It does not contain a second copy of the failure rules.

## SOLID

### Single responsibility

`SimulationClock` only tracks virtual time. `EventScheduler` only orders future work. `MetricsCalculator` only reads a report. `SimulationController` decides when the next engine step may run. The raylib files draw a snapshot. They do not classify failures.

`SimulationEngine` is the coordinator. It is larger than the other types because scheduling, injection, retry, and recording have to commit together. Splitting it further was not required for the current behavior.

### Open/closed

A new callee can be added by deriving from `Service` and registering it. The engine does not need a new branch for that class.

A new failure type is not open in that way. `FailureType` is an enumeration, and the engine interprets it. Adding a type means changing the engine. That is a deliberate limit: failure behavior is part of the simulator, not a plugin.

### Liskov substitution

Any `Service` passed to the engine must accept `handle(const Request&)` and return a `ServiceResult`, or throw. The three production services do that. A throwing service is defined behavior: the start stays queued and nothing new is recorded. A substitute that deleted the engine, or called back into it, would break the contract documented on `SimulationEngine`. The production services do not do that.

### Interface segregation

`Service` has two operations. Callees are not asked to expose metrics, retries, or drawing. Metrics types are separate from the service interface. The GUI does not need `handle`.

### Dependency inversion

The engine depends on the `Service` abstraction and receives the concrete objects from its caller. `MetricsCalculator` depends on the report value, not on `SimulationEngine`. The console and the GUI depend on the core. The core does not depend on either executable or on the test harness.

These five ideas are visible in the design. They are not a claim that every class was forced through a checklist.

## RAII and ownership

Service objects are values owned by the caller. The engine stores raw non-owning pointers and does not delete them. Sessions that own both the services and the engine declare the services first, so the engine is destroyed while the services still exist.

`SimulationController` holds its session in a `std::unique_ptr`. Reset replaces that session. The destructor is defined in the `.cpp` file, where the session type is complete.

Reports are values. `MetricsCalculator::calculate` returns an `ObservabilityReport` that remains readable after the engine and the services are gone. Comparison tests copy a `ScenarioResult` for the same reason. A reference into a temporary report is not how the tests keep a result.

Fonts loaded by the GUI are released in `unload_ui_fonts`. The raylib default font is not unloaded, because the library owns it.

There is no shared ownership and no `shared_ptr` in the core.

## const, values, and the standard library

Observers such as `SimulationClock::now`, `DeadLetterQueue::entries`, and `SimulationEngine::circuit_state` are `const`. Inputs that must not be mutated, including `const Request&` and `const ScenarioConfig&`, are passed by const reference.

`ObservabilityReport`, `ScenarioResult`, and `ComparisonReport` are ordinary values. Copying them is the supported way to keep a result.

The scheduler uses `std::priority_queue`. Service lookup uses `std::unordered_map`. Outcomes and timelines use `std::vector`. Missing throughput and a missing breaker state use `std::optional`. Time is `std::uint64_t`. The core does not use a third-party container library.

## Deterministic execution

The engine does not draw random numbers, read the wall clock, or sleep for a virtual delay. Equal timestamps keep insertion order. The same scenario configuration produces the same event sequence. Playback speed changes frame time, not the virtual timestamps.

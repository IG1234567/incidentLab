#include "incidentlab/scenarios/scenario_runner.hpp"

#include "incidentlab/observability/metrics_calculator.hpp"
#include "incidentlab/request.hpp"
#include "incidentlab/simulation/failure_config.hpp"
#include "incidentlab/simulation/retry_config.hpp"
#include "incidentlab/simulation/simulation_engine.hpp"

#include <map>
#include <stdexcept>
#include <utility>

namespace incidentlab {
namespace {

class ScriptedService final : public Service {
public:
    ScriptedService(std::string name, std::map<std::string, std::vector<ServiceStatus>> script)
        : name_(std::move(name)), script_(std::move(script)) {}

    std::string name() const override { return name_; }

    ServiceResult handle(const Request& request) override {
        std::size_t& seen = attempts_[request.request_id];
        const auto found = script_.find(request.request_id);
        ServiceStatus status = ServiceStatus::Success;
        if (found != script_.end() && !found->second.empty()) {
            const std::size_t index = seen < found->second.size() ? seen : found->second.size() - 1;
            status = found->second[index];
        }
        ++seen;
        return ServiceResult{status, message_for(status)};
    }

private:
    static const char* message_for(ServiceStatus status) {
        switch (status) {
        case ServiceStatus::Success:
            return "Processed";
        case ServiceStatus::Failed:
            return "Service failed";
        case ServiceStatus::TimedOut:
            return "Request timed out";
        case ServiceStatus::Dropped:
            return "Request dropped";
        case ServiceStatus::Rejected:
            return "Rejected";
        case ServiceStatus::CircuitRejected:
            return "Circuit breaker open";
        }
        return "Processed";
    }

    std::string name_;
    std::map<std::string, std::vector<ServiceStatus>> script_;
    std::map<std::string, std::size_t> attempts_;
};

std::map<std::string, std::vector<ServiceStatus>> script_for(const std::string& service_name) {
    std::map<std::string, std::vector<ServiceStatus>> script;
    for (const FaultRule& rule : comparison_faults()) {
        bool belongs = false;
        for (const WorkloadCall& call : comparison_workload()) {
            if (call.request_id == rule.request_id && call.service_name == service_name) {
                belongs = true;
                break;
            }
        }
        if (belongs) {
            script.emplace(rule.request_id, rule.attempts);
        }
    }
    return script;
}

}  // namespace

void validate_scenario_config(const ScenarioConfig& config) {
    if (config.id.empty()) {
        throw std::invalid_argument("scenario id must not be empty");
    }
    if (config.name.empty()) {
        throw std::invalid_argument("scenario name must not be empty");
    }
    if (config.retry_enabled) {
        validate_retry_config(RetryConfig{config.max_attempts, config.retry_initial_delay_ms,
                                          config.retry_backoff_multiplier});
    }
    if (config.circuit_breaker_enabled) {
        validate_circuit_breaker_config(
            CircuitBreakerConfig{config.circuit_failure_threshold, config.circuit_reset_timeout_ms});
    }
}

void validate_scenario_set(const std::vector<ScenarioConfig>& scenarios) {
    for (const ScenarioConfig& config : scenarios) {
        validate_scenario_config(config);
    }
    for (std::size_t left = 0; left < scenarios.size(); ++left) {
        for (std::size_t right = left + 1; right < scenarios.size(); ++right) {
            if (scenarios[left].id == scenarios[right].id) {
                throw std::invalid_argument("duplicate scenario id: " + scenarios[left].id);
            }
        }
    }
}

const std::vector<WorkloadCall>& comparison_workload() {
    static const std::vector<WorkloadCall> calls = {
        {"REQ-001", "OrderService", "CreateOrder", 0},
        {"REQ-002", "OrderService", "CreateOrder", 10},
        {"REQ-003", "PaymentService", "ProcessPayment", 20},
        {"REQ-004", "PaymentService", "ProcessPayment", 30},
        {"REQ-005", "PaymentService", "ProcessPayment", 40},
        {"REQ-006", "InventoryService", "ReserveInventory", 50},
        {"REQ-007", "InventoryService", "ReserveInventory", 60},
        {"REQ-008", "PaymentService", "ProcessPayment", 200},
        {"REQ-009", "InventoryService", "ReserveInventory", 700},
        {"REQ-010", "PaymentService", "ProcessPayment", 1000},
    };
    return calls;
}

const std::vector<FaultRule>& comparison_faults() {
    static const std::vector<FaultRule> rules = {
        {"REQ-001", {ServiceStatus::Success}},
        {"REQ-002", {ServiceStatus::TimedOut, ServiceStatus::Success}},
        {"REQ-003", {ServiceStatus::Failed, ServiceStatus::Success}},
        {"REQ-004", {ServiceStatus::Failed, ServiceStatus::Failed}},
        {"REQ-005", {ServiceStatus::Failed, ServiceStatus::Failed}},
        {"REQ-006", {ServiceStatus::Failed, ServiceStatus::Success}},
        {"REQ-007", {ServiceStatus::Failed, ServiceStatus::Failed}},
        {"REQ-008", {ServiceStatus::Success}},
        {"REQ-009", {ServiceStatus::Success}},
        {"REQ-010", {ServiceStatus::Success}},
    };
    return rules;
}

std::vector<ScenarioConfig> predefined_scenarios() {
    ScenarioConfig baseline;
    baseline.id = "baseline";
    baseline.name = "Baseline";

    ScenarioConfig retry = baseline;
    retry.id = "retry";
    retry.name = "Retry Only";
    retry.retry_enabled = true;
    retry.max_attempts = 2;
    retry.retry_initial_delay_ms = 100;
    retry.retry_backoff_multiplier = 2.0;

    ScenarioConfig circuit = baseline;
    circuit.id = "circuit";
    circuit.name = "Circuit Breaker";
    circuit.circuit_breaker_enabled = true;
    circuit.circuit_failure_threshold = 2;
    circuit.circuit_reset_timeout_ms = 400;

    ScenarioConfig full = retry;
    full.id = "full";
    full.name = "Full Resilience";
    full.circuit_breaker_enabled = true;
    full.circuit_failure_threshold = 2;
    full.circuit_reset_timeout_ms = 400;

    return {baseline, retry, circuit, full};
}

ScenarioResult ScenarioRunner::run(const ScenarioConfig& config) const {
    validate_scenario_config(config);

    // Services are declared before the engine so destruction destroys the engine first.
    struct Session {
        ScriptedService order;
        ScriptedService payment;
        ScriptedService inventory;
        SimulationEngine engine;

        explicit Session(const ScenarioConfig& config)
            : order("OrderService", script_for("OrderService")),
              payment("PaymentService", script_for("PaymentService")),
              inventory("InventoryService", script_for("InventoryService")) {
            engine.register_service(order);
            engine.register_service(payment);
            engine.register_service(inventory);

            const FailureConfig delay{FailureType::SlowResponse, kComparisonProcessingDelayMs, 0, 1};
            engine.configure_failure(order.name(), delay);
            engine.configure_failure(payment.name(), delay);
            engine.configure_failure(inventory.name(), delay);

            if (config.retry_enabled) {
                const RetryConfig retry{config.max_attempts, config.retry_initial_delay_ms,
                                        config.retry_backoff_multiplier};
                engine.configure_retry(order.name(), retry);
                engine.configure_retry(payment.name(), retry);
                engine.configure_retry(inventory.name(), retry);
            }
            if (config.circuit_breaker_enabled) {
                const CircuitBreakerConfig breaker{config.circuit_failure_threshold,
                                                   config.circuit_reset_timeout_ms};
                engine.configure_circuit_breaker(order.name(), breaker);
                engine.configure_circuit_breaker(payment.name(), breaker);
                engine.configure_circuit_breaker(inventory.name(), breaker);
            }

            for (const WorkloadCall& call : comparison_workload()) {
                engine.schedule_request(Request{call.request_id, kComparisonTraceId, "client",
                                                call.service_name, call.operation},
                                        call.arrival_ms);
            }
        }
    };

    Session session(config);
    session.engine.run();
    const SimulationReport snapshot = session.engine.report();
    ScenarioResult result;
    result.config = config;
    result.metrics = MetricsCalculator::calculate(snapshot);
    result.simulation_end_time_ms = session.engine.clock().now();
    return result;
}

ComparisonReport run_comparison(const std::vector<ScenarioConfig>& scenarios) {
    validate_scenario_set(scenarios);
    ComparisonReport report;
    ScenarioRunner runner;
    report.scenarios.reserve(scenarios.size());
    for (const ScenarioConfig& config : scenarios) {
        report.scenarios.push_back(runner.run(config));
    }
    return report;
}

ComparisonReport run_predefined_comparison() { return run_comparison(predefined_scenarios()); }

}  // namespace incidentlab

#include "incidentlab/visualization/simulation_controller.hpp"

#include "incidentlab/order_service.hpp"
#include "incidentlab/service.hpp"
#include "incidentlab/simulation/simulation_engine.hpp"

#include <utility>
#include <vector>

namespace incidentlab {
namespace {

class StatusScript : public Service {
public:
    StatusScript(std::string name, std::vector<ServiceStatus> results)
        : name_(std::move(name)), results_(std::move(results)) {}

    std::string name() const override { return name_; }

    ServiceResult handle(const Request&) override {
        const ServiceStatus status = results_.at(calls_);
        ++calls_;
        if (status == ServiceStatus::Success) {
            return ServiceResult{status, "recovered"};
        }
        return ServiceResult{status, "Service failed"};
    }

private:
    std::string name_;
    std::vector<ServiceStatus> results_;
    std::size_t calls_ = 0;
};

Request make_request(const char* request_id, const char* service, const char* operation) {
    return Request{request_id, "TRACE-VIZ", "client", service, operation};
}

std::vector<std::string> scenario_services() {
    return {"InventoryService", "OrderService", "PaymentService"};
}

std::vector<ScheduledFlow> scenario_flows() {
    const char* trace = "TRACE-VIZ";
    return {
        ScheduledFlow{"REQ-O1", trace, "client", "OrderService"},
        ScheduledFlow{"REQ-O2", trace, "client", "OrderService"},
        ScheduledFlow{"REQ-P1", trace, "client", "PaymentService"},
        ScheduledFlow{"REQ-P2", trace, "client", "PaymentService"},
        ScheduledFlow{"REQ-P3", trace, "client", "PaymentService"},
        ScheduledFlow{"REQ-P4", trace, "client", "PaymentService"},
        ScheduledFlow{"REQ-I1", trace, "client", "InventoryService"},
        ScheduledFlow{"REQ-I2", trace, "client", "InventoryService"},
    };
}

}  // namespace

struct SimulationController::Session {
    OrderService orders;
    StatusScript payments;
    StatusScript inventory;
    SimulationEngine engine;

    Session()
        : payments{"PaymentService",
                   {ServiceStatus::Failed, ServiceStatus::Failed, ServiceStatus::Success}},
          inventory{"InventoryService",
                    {ServiceStatus::Failed, ServiceStatus::Success, ServiceStatus::Failed,
                     ServiceStatus::Failed}} {
        engine.register_service(orders);
        engine.register_service(payments);
        engine.register_service(inventory);
        engine.configure_failure("OrderService",
                                 FailureConfig{FailureType::Timeout, 80, 40, 2});
        // The third Payment attempt is the half-open probe. Delaying it leaves HALF-OPEN
        // visible between the probe start and its finish. The first two attempts stay immediate.
        engine.configure_failure("PaymentService",
                                 FailureConfig{FailureType::SlowResponse, 100, 0, 3});
        engine.configure_circuit_breaker("PaymentService", CircuitBreakerConfig{2, 500});
        engine.configure_retry("InventoryService", RetryConfig{2, 100, 2.0});

        engine.schedule_request(make_request("REQ-O1", "OrderService", "CreateOrder"), 0);
        engine.schedule_request(make_request("REQ-O2", "OrderService", "CreateOrder"), 10);
        engine.schedule_request(make_request("REQ-P1", "PaymentService", "ProcessPayment"), 20);
        engine.schedule_request(make_request("REQ-P2", "PaymentService", "ProcessPayment"), 30);
        engine.schedule_request(make_request("REQ-P3", "PaymentService", "ProcessPayment"), 40);
        engine.schedule_request(make_request("REQ-I1", "InventoryService", "ReserveInventory"), 70);
        engine.schedule_request(make_request("REQ-I2", "InventoryService", "ReserveInventory"),
                                200);
        engine.schedule_request(make_request("REQ-P4", "PaymentService", "ProcessPayment"), 530);
    }
};

SimulationController::SimulationController() : session_(std::make_unique<Session>()) {
    rebuild_snapshot();
}

SimulationController::~SimulationController() = default;

PlaybackState SimulationController::state() const noexcept { return state_; }

PlaybackSpeed SimulationController::speed() const noexcept { return speed_; }

double SimulationController::playback_interval_seconds() const noexcept {
    return incidentlab::playback_interval_seconds(speed_);
}

void SimulationController::set_speed(PlaybackSpeed speed) {
    speed_ = speed;
    accumulator_seconds_ = 0.0;
    snapshot_.speed = speed_;
}

void SimulationController::start() {
    if (state_ != PlaybackState::Ready) {
        return;
    }
    state_ = PlaybackState::Running;
    accumulator_seconds_ = 0.0;
    snapshot_.playback = state_;
}

void SimulationController::pause() {
    if (state_ != PlaybackState::Running) {
        return;
    }
    state_ = PlaybackState::Paused;
    accumulator_seconds_ = 0.0;
    snapshot_.playback = state_;
}

void SimulationController::resume() {
    if (state_ != PlaybackState::Paused) {
        return;
    }
    state_ = PlaybackState::Running;
    accumulator_seconds_ = 0.0;
    snapshot_.playback = state_;
}

bool SimulationController::step() {
    if (state_ == PlaybackState::Completed) {
        return false;
    }
    if (!session_->engine.step_once()) {
        finish_if_idle();
        snapshot_.playback = state_;
        return false;
    }
    if (state_ == PlaybackState::Ready) {
        state_ = PlaybackState::Paused;
    }
    finish_if_idle();
    rebuild_snapshot();
    return true;
}

void SimulationController::reset() {
    session_ = std::make_unique<Session>();
    state_ = PlaybackState::Ready;
    accumulator_seconds_ = 0.0;
    selected_service_.reset();
    selected_event_id_.reset();
    rebuild_snapshot();
}

bool SimulationController::tick(double frame_seconds) {
    if (state_ != PlaybackState::Running) {
        return false;
    }
    if (frame_seconds > 0.0) {
        accumulator_seconds_ += frame_seconds;
    }
    const double interval = playback_interval_seconds();
    if (accumulator_seconds_ < interval) {
        return false;
    }
    accumulator_seconds_ -= interval;
    if (accumulator_seconds_ > interval) {
        accumulator_seconds_ = interval;
    }
    return step();
}

bool SimulationController::select_service(const std::string& service_name) {
    for (const ServiceNodeView& node : snapshot_.services) {
        if (node.name != service_name) {
            continue;
        }
        selected_service_ = service_name;
        for (ServiceNodeView& service : snapshot_.services) {
            service.selected = service.name == service_name;
        }
        snapshot_.selected_service = selected_service_;
        return true;
    }
    return false;
}

void SimulationController::clear_service_selection() {
    selected_service_.reset();
    snapshot_.selected_service.reset();
    for (ServiceNodeView& service : snapshot_.services) {
        service.selected = false;
    }
}

bool SimulationController::select_event(const std::string& event_id) {
    for (const TimelineEntry& entry : snapshot_.metrics.timeline) {
        if (entry.event_id != event_id) {
            continue;
        }
        selected_event_id_ = event_id;
        snapshot_.selected_event_id = selected_event_id_;
        return true;
    }
    return false;
}

void SimulationController::clear_event_selection() {
    selected_event_id_.reset();
    snapshot_.selected_event_id.reset();
}

const VisualizationSnapshot& SimulationController::snapshot() const noexcept { return snapshot_; }

void SimulationController::rebuild_snapshot() {
    snapshot_ = build_visualization_snapshot(session_->engine.report(), scenario_services(),
                                             scenario_flows(), state_, speed_, selected_service_,
                                             selected_event_id_);
}

void SimulationController::finish_if_idle() {
    const SimulationReport report = session_->engine.report();
    if (report.pending_requests == 0 && report.in_progress_requests == 0) {
        state_ = PlaybackState::Completed;
    }
}

}  // namespace incidentlab

#define INCIDENTLAB_TEST_SUITE "domain"
#include "test_harness.hpp"

#include "incidentlab/event.hpp"
#include "incidentlab/event_history.hpp"
#include "incidentlab/inventory_service.hpp"
#include "incidentlab/order_service.hpp"
#include "incidentlab/payment_service.hpp"
#include "incidentlab/request.hpp"
#include "incidentlab/service.hpp"
#include "incidentlab/stage2_demo.hpp"

#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

template <typename Function>
void expect_invalid_argument(Function&& function, const char* field) {
    try {
        function();
    } catch (const std::invalid_argument& ex) {
        if (std::string(ex.what()).find(field) == std::string::npos) {
            throw incidentlab_test::Failure(std::string("message does not name ") + field + ": " +
                                            ex.what());
        }
        return;
    } catch (const std::exception& ex) {
        throw incidentlab_test::Failure(std::string("unexpected exception: ") + ex.what());
    }
    throw incidentlab_test::Failure(std::string("expected std::invalid_argument for ") + field);
}

incidentlab::Request sample_request() {
    return incidentlab::Request{"REQ-001", "TRACE-001", "client", "OrderService", "CreateOrder"};
}

incidentlab::Event sample_event(const std::string& event_id, const std::string& request_id,
                                const std::string& trace_id) {
    return incidentlab::Event{event_id,          request_id,
                              trace_id,          "OrderService",
                              incidentlab::EventType::RequestReceived, "Request received"};
}

}  // namespace

INCIDENTLAB_TEST(valid_request_is_accepted) {
    const incidentlab::Request request = sample_request();
    EXPECT(request.request_id == "REQ-001");
    EXPECT(request.trace_id == "TRACE-001");
    EXPECT(request.source_service == "client");
    EXPECT(request.target_service == "OrderService");
    EXPECT(request.operation == "CreateOrder");

    const incidentlab::Request external{"REQ-010", "TRACE-010", "", "OrderService", "CreateOrder"};
    EXPECT(external.source_service.empty());
}

INCIDENTLAB_TEST(empty_request_id_is_rejected) {
    expect_invalid_argument(
        [] { incidentlab::Request{"", "TRACE-001", "client", "OrderService", "CreateOrder"}; },
        "request_id");
}

INCIDENTLAB_TEST(empty_trace_id_is_rejected) {
    expect_invalid_argument(
        [] { incidentlab::Request{"REQ-001", "", "client", "OrderService", "CreateOrder"}; },
        "trace_id");
}

INCIDENTLAB_TEST(empty_target_service_is_rejected) {
    expect_invalid_argument(
        [] { incidentlab::Request{"REQ-001", "TRACE-001", "client", "", "CreateOrder"}; },
        "target_service");
}

INCIDENTLAB_TEST(empty_operation_is_rejected) {
    expect_invalid_argument(
        [] { incidentlab::Request{"REQ-001", "TRACE-001", "client", "OrderService", ""}; },
        "operation");
}

INCIDENTLAB_TEST(order_service_handles_create_order) {
    incidentlab::OrderService service;
    const incidentlab::ServiceResult result = service.handle(sample_request());
    EXPECT(result.status == incidentlab::ServiceStatus::Success);
    EXPECT(result.message == "Order created successfully");
    EXPECT(service.name() == "OrderService");
}

INCIDENTLAB_TEST(payment_service_handles_process_payment) {
    incidentlab::PaymentService service;
    const incidentlab::Request request{"REQ-002", "TRACE-001", "OrderService", "PaymentService",
                                       "ProcessPayment"};
    const incidentlab::ServiceResult result = service.handle(request);
    EXPECT(result.status == incidentlab::ServiceStatus::Success);
    EXPECT(result.message == "Payment processed successfully");
    EXPECT(service.name() == "PaymentService");
}

INCIDENTLAB_TEST(inventory_service_handles_reserve_inventory) {
    incidentlab::InventoryService service;
    const incidentlab::Request request{"REQ-003", "TRACE-001", "OrderService", "InventoryService",
                                       "ReserveInventory"};
    const incidentlab::ServiceResult result = service.handle(request);
    EXPECT(result.status == incidentlab::ServiceStatus::Success);
    EXPECT(result.message == "Inventory reserved successfully");
    EXPECT(service.name() == "InventoryService");
}

INCIDENTLAB_TEST(unsupported_operation_returns_rejected) {
    incidentlab::OrderService orders;
    incidentlab::PaymentService payments;
    incidentlab::InventoryService inventory;
    const incidentlab::Request request{"REQ-004", "TRACE-002", "client", "OrderService",
                                       "ShipOrder"};

    const incidentlab::ServiceResult order_result = orders.handle(request);
    const incidentlab::ServiceResult payment_result = payments.handle(request);
    const incidentlab::ServiceResult inventory_result = inventory.handle(request);

    EXPECT(order_result.status == incidentlab::ServiceStatus::Rejected);
    EXPECT(payment_result.status == incidentlab::ServiceStatus::Rejected);
    EXPECT(inventory_result.status == incidentlab::ServiceStatus::Rejected);
    EXPECT(order_result.message == "Unsupported operation: ShipOrder");
    EXPECT(payment_result.message == "Unsupported operation: ShipOrder");
    EXPECT(inventory_result.message == "Unsupported operation: ShipOrder");
}

INCIDENTLAB_TEST(polymorphic_invocation_through_service_works) {
    std::unique_ptr<incidentlab::Service> service = std::make_unique<incidentlab::OrderService>();
    const incidentlab::ServiceResult result = service->handle(sample_request());
    EXPECT(service->name() == "OrderService");
    EXPECT(result.status == incidentlab::ServiceStatus::Success);
    EXPECT(result.message == "Order created successfully");
}

INCIDENTLAB_TEST(event_preserves_request_id_and_trace_id) {
    const incidentlab::Event event = sample_event("EVT-001", "REQ-001", "TRACE-001");
    EXPECT(event.event_id == "EVT-001");
    EXPECT(event.request_id == "REQ-001");
    EXPECT(event.trace_id == "TRACE-001");
    EXPECT(event.service_name == "OrderService");
    EXPECT(event.type == incidentlab::EventType::RequestReceived);
    EXPECT(event.message == "Request received");
}

INCIDENTLAB_TEST(invalid_event_data_is_rejected) {
    expect_invalid_argument(
        [] {
            incidentlab::Event{"", "REQ-001", "TRACE-001", "OrderService",
                               incidentlab::EventType::RequestReceived, "Request received"};
        },
        "event_id");
    expect_invalid_argument(
        [] {
            incidentlab::Event{"EVT-001", "", "TRACE-001", "OrderService",
                               incidentlab::EventType::RequestReceived, "Request received"};
        },
        "request_id");
    expect_invalid_argument(
        [] {
            incidentlab::Event{"EVT-001", "REQ-001", "", "OrderService",
                               incidentlab::EventType::RequestReceived, "Request received"};
        },
        "trace_id");
    expect_invalid_argument(
        [] {
            incidentlab::Event{"EVT-001", "REQ-001", "TRACE-001", "",
                               incidentlab::EventType::RequestReceived, "Request received"};
        },
        "service_name");
}

INCIDENTLAB_TEST(events_preserve_insertion_order) {
    incidentlab::EventHistory history;
    history.record(sample_event("EVT-001", "REQ-001", "TRACE-001"));
    history.record(sample_event("EVT-002", "REQ-002", "TRACE-001"));
    history.record(sample_event("EVT-003", "REQ-001", "TRACE-002"));

    const std::vector<incidentlab::Event>& events = history.events();
    EXPECT(events.size() == 3);
    EXPECT(events[0].event_id == "EVT-001");
    EXPECT(events[1].event_id == "EVT-002");
    EXPECT(events[2].event_id == "EVT-003");
}

INCIDENTLAB_TEST(filtering_by_trace_id_works) {
    incidentlab::EventHistory history;
    history.record(sample_event("EVT-001", "REQ-001", "TRACE-001"));
    history.record(sample_event("EVT-002", "REQ-002", "TRACE-002"));
    history.record(sample_event("EVT-003", "REQ-003", "TRACE-001"));

    const std::vector<incidentlab::Event> trace = history.by_trace_id("TRACE-001");
    EXPECT(trace.size() == 2);
    EXPECT(trace[0].event_id == "EVT-001");
    EXPECT(trace[1].event_id == "EVT-003");
    EXPECT(history.by_trace_id("TRACE-404").empty());
}

INCIDENTLAB_TEST(filtering_by_request_id_works) {
    incidentlab::EventHistory history;
    history.record(sample_event("EVT-001", "REQ-001", "TRACE-001"));
    history.record(sample_event("EVT-002", "REQ-002", "TRACE-001"));
    history.record(sample_event("EVT-003", "REQ-001", "TRACE-001"));

    const std::vector<incidentlab::Event> request = history.by_request_id("REQ-001");
    EXPECT(request.size() == 2);
    EXPECT(request[0].event_id == "EVT-001");
    EXPECT(request[1].event_id == "EVT-003");
    EXPECT(history.by_request_id("REQ-404").empty());
}

INCIDENTLAB_TEST(empty_history_returns_no_events) {
    const incidentlab::EventHistory history;
    EXPECT(history.size() == 0);
    EXPECT(history.events().empty());
    EXPECT(history.by_trace_id("TRACE-001").empty());
    EXPECT(history.by_request_id("REQ-001").empty());
}

INCIDENTLAB_TEST(demo_processes_exactly_three_requests) {
    const incidentlab::DemoReport report = incidentlab::run_stage2_demo();
    EXPECT(report.steps.size() == 3);
    EXPECT(report.steps[0].request.request_id == "REQ-001");
    EXPECT(report.steps[0].service_name == "OrderService");
    EXPECT(report.steps[0].request.operation == "CreateOrder");
    EXPECT(report.steps[0].result.status == incidentlab::ServiceStatus::Success);
    EXPECT(report.steps[1].request.request_id == "REQ-002");
    EXPECT(report.steps[1].service_name == "PaymentService");
    EXPECT(report.steps[1].request.operation == "ProcessPayment");
    EXPECT(report.steps[2].request.request_id == "REQ-003");
    EXPECT(report.steps[2].service_name == "InventoryService");
    EXPECT(report.steps[2].request.operation == "ReserveInventory");
}

INCIDENTLAB_TEST(demo_generates_exactly_six_events) {
    const incidentlab::DemoReport report = incidentlab::run_stage2_demo();
    EXPECT(report.history.size() == 6);
    const std::vector<incidentlab::Event>& events = report.history.events();
    EXPECT(events[0].type == incidentlab::EventType::RequestReceived);
    EXPECT(events[1].type == incidentlab::EventType::RequestCompleted);
    EXPECT(events[2].type == incidentlab::EventType::RequestReceived);
    EXPECT(events[3].type == incidentlab::EventType::RequestCompleted);
    EXPECT(events[4].type == incidentlab::EventType::RequestReceived);
    EXPECT(events[5].type == incidentlab::EventType::RequestCompleted);
}

INCIDENTLAB_TEST(demo_requests_share_one_trace_id) {
    const incidentlab::DemoReport report = incidentlab::run_stage2_demo();
    EXPECT(report.trace_id == "TRACE-001");
    for (const incidentlab::DemoStep& step : report.steps) {
        EXPECT(step.request.trace_id == report.trace_id);
    }
    for (const incidentlab::Event& event : report.history.events()) {
        EXPECT(event.trace_id == report.trace_id);
    }
    EXPECT(report.history.by_trace_id("TRACE-001").size() == 6);
}

INCIDENTLAB_TEST(event_identifiers_are_deterministic_and_unique) {
    const incidentlab::DemoReport first = incidentlab::run_stage2_demo();
    const incidentlab::DemoReport second = incidentlab::run_stage2_demo();
    const std::vector<incidentlab::Event>& events = first.history.events();
    EXPECT(events.size() == 6);

    std::set<std::string> ids;
    for (std::size_t index = 0; index < events.size(); ++index) {
        EXPECT(events[index].event_id == second.history.events()[index].event_id);
        ids.insert(events[index].event_id);
    }
    EXPECT(ids.size() == 6);
    EXPECT(events[0].event_id == "EVT-001");
    EXPECT(events[1].event_id == "EVT-002");
    EXPECT(events[2].event_id == "EVT-003");
    EXPECT(events[3].event_id == "EVT-004");
    EXPECT(events[4].event_id == "EVT-005");
    EXPECT(events[5].event_id == "EVT-006");
}

INCIDENTLAB_TEST(rejected_operation_is_recorded_as_request_rejected) {
    incidentlab::EventHistory history;
    incidentlab::OrderService orders;
    const incidentlab::Request request{"REQ-009", "TRACE-009", "client", "OrderService",
                                       "ShipOrder"};
    int next_event_number = 1;
    const incidentlab::ServiceResult result =
        incidentlab::handle_and_record(history, orders, request, next_event_number);

    EXPECT(result.status == incidentlab::ServiceStatus::Rejected);
    EXPECT(history.size() == 2);
    EXPECT(history.events()[0].type == incidentlab::EventType::RequestReceived);
    EXPECT(history.events()[1].type == incidentlab::EventType::RequestRejected);
    EXPECT(history.events()[0].request_id == "REQ-009");
    EXPECT(history.events()[1].trace_id == "TRACE-009");
    EXPECT(history.events()[1].message == "Unsupported operation: ShipOrder");
    EXPECT(next_event_number == 3);
}

int main(int argc, char** argv) {
    return incidentlab_test::run_all(argc, argv) == 0 ? 0 : 1;
}

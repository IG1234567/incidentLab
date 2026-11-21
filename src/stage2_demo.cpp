#include "incidentlab/stage2_demo.hpp"

#include "incidentlab/inventory_service.hpp"
#include "incidentlab/order_service.hpp"
#include "incidentlab/payment_service.hpp"

#include <iomanip>
#include <sstream>
#include <string>

namespace incidentlab {
namespace {

std::string format_event_id(int sequence) {
    std::ostringstream out;
    out << "EVT-" << std::setw(3) << std::setfill('0') << sequence;
    return out.str();
}

}  // namespace

ServiceResult handle_and_record(EventHistory& history, Service& service, const Request& request,
                                int& next_event_number) {
    history.record(Event{format_event_id(next_event_number), request.request_id, request.trace_id,
                         service.name(), EventType::RequestReceived, "Request received"});
    ++next_event_number;

    const ServiceResult result = service.handle(request);
    const EventType outcome = result.status == ServiceStatus::Success ? EventType::RequestCompleted
                                                                      : EventType::RequestRejected;
    history.record(Event{format_event_id(next_event_number), request.request_id, request.trace_id,
                         service.name(), outcome, result.message});
    ++next_event_number;
    return result;
}

DemoReport run_stage2_demo() {
    DemoReport report;
    report.trace_id = "TRACE-001";

    const Request requests[] = {
        Request{"REQ-001", report.trace_id, "client", "OrderService", "CreateOrder"},
        Request{"REQ-002", report.trace_id, "OrderService", "PaymentService", "ProcessPayment"},
        Request{"REQ-003", report.trace_id, "OrderService", "InventoryService", "ReserveInventory"},
    };

    OrderService order_service;
    PaymentService payment_service;
    InventoryService inventory_service;
    // The static type of each element is Service. The dynamic type is the concrete service.
    Service* const services[] = {&order_service, &payment_service, &inventory_service};

    int next_event_number = 1;
    for (std::size_t index = 0; index < 3; ++index) {
        Service& service = *services[index];
        const ServiceResult result =
            handle_and_record(report.history, service, requests[index], next_event_number);
        report.steps.push_back(DemoStep{requests[index], service.name(), result});
    }
    return report;
}

}  // namespace incidentlab

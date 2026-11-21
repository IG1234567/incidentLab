#pragma once

#include "incidentlab/event_history.hpp"
#include "incidentlab/request.hpp"
#include "incidentlab/service.hpp"
#include "incidentlab/service_result.hpp"

#include <string>
#include <vector>

namespace incidentlab {

/// One polymorphic call in the fixed Stage 2 demonstration.
struct DemoStep {
    Request request;
    std::string service_name;
    ServiceResult result;
};

/// Data produced by the demonstration. The console formats this. It does not decide it.
///
/// This is a fixed script, not the discrete-event engine. Stage 3 can replace the script
/// without changing Service, Request, or Event.
struct DemoReport {
    std::string trace_id;
    std::vector<DemoStep> steps;
    EventHistory history;
};

/// Records RequestReceived, calls service.handle, then records the outcome.
///
/// next_event_number is the 1-based id of the next event. Each call consumes two numbers.
/// Event ids are EVT-001, EVT-002, and so on. They do not use the wall clock.
ServiceResult handle_and_record(EventHistory& history, Service& service, const Request& request,
                                int& next_event_number);

/// TRACE-001 with three requests, one per concrete service, all invoked through Service.
DemoReport run_stage2_demo();

}  // namespace incidentlab

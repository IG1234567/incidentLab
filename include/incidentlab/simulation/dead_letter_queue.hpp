#pragma once

#include "incidentlab/request.hpp"
#include "incidentlab/service_result.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace incidentlab {

/// One logical request that will not be tried again.
struct DeadLetterEntry {
    Request request;
    std::size_t attempts = 0;
    ServiceStatus final_status = ServiceStatus::Failed;
    std::uint64_t failed_at_ms = 0;
    std::string reason;
};

/// In-memory dead-letter queue. Insertion order is kept. Nothing is written to disk.
class DeadLetterQueue {
public:
    void push(DeadLetterEntry entry);
    void pop_last() noexcept;

    const std::vector<DeadLetterEntry>& entries() const noexcept;
    std::size_t size() const noexcept;

private:
    std::vector<DeadLetterEntry> entries_;
};

}  // namespace incidentlab

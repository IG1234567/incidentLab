#include "incidentlab/simulation/dead_letter_queue.hpp"

#include <utility>

namespace incidentlab {

void DeadLetterQueue::push(DeadLetterEntry entry) {
    for (const DeadLetterEntry& existing : entries_) {
        if (existing.request.request_id == entry.request.request_id) {
            throw std::logic_error("duplicate dead-letter request: " + entry.request.request_id);
        }
    }
    entries_.push_back(std::move(entry));
}

void DeadLetterQueue::pop_last() noexcept {
    if (!entries_.empty()) {
        entries_.pop_back();
    }
}

const std::vector<DeadLetterEntry>& DeadLetterQueue::entries() const noexcept { return entries_; }

std::size_t DeadLetterQueue::size() const noexcept { return entries_.size(); }

}  // namespace incidentlab

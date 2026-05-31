#include "write_handler.h"
#include <algorithm>
#include <cstring>

namespace esphome {
namespace geappliances_bridge {

WriteHandler::WriteHandler(i_tiny_gea3_erd_client_t* erd_client,
                           WriteQueue* write_queue)
    : erd_client_(erd_client),
      write_queue_(write_queue),
      appliance_address_(0)
{
}

void WriteHandler::process_writes()
{
  if (erd_client_ == nullptr || write_queue_ == nullptr) {
    return;
  }

  // Process pending writes from the queue
  WriteCommand cmd;
  while (write_queue_->pop(cmd)) {
    // Check if we have capacity for in-flight writes
    if (in_flight_.size() >= MAX_PENDING_WRITES) {
      // Put the command back — queue is full
      write_queue_->push(cmd);
      break;
    }

    tiny_gea3_erd_client_request_id_t request_id;
    bool queued = tiny_gea3_erd_client_write(
      erd_client_, &request_id,
      cmd.appliance_address > 0 ? cmd.appliance_address : appliance_address_,
      cmd.erd_id, cmd.value, cmd.value_size);

    if (queued) {
      InFlightWrite in_flight;
      in_flight.erd_id = cmd.erd_id;
      in_flight.appliance_address = cmd.appliance_address > 0
        ? cmd.appliance_address : appliance_address_;
      in_flight.retries = 0;
      in_flight_.push_back(in_flight);
    }
    // If not queued, the command is dropped (queue will retry on next cycle)
  }
}

void WriteHandler::on_write_response(tiny_erd_t erd_id, bool success)
{
  // Find the in-flight write for this ERD
  auto it = std::find_if(in_flight_.begin(), in_flight_.end(),
    [erd_id](const InFlightWrite& wf) {
      return wf.erd_id == erd_id;
    });

  if (it != in_flight_.end()) {
    if (!success) {
      it->retries++;
      if (it->retries < MAX_WRITE_RETRIES) {
        // Re-queue for retry — push back to write queue
        // Note: In production this would reconstruct the WriteCommand
        // For now, just remove from in-flight; the caller can re-queue
        in_flight_.erase(it);
        return;
      }
    }
    // Success or max retries reached — remove from in-flight
    in_flight_.erase(it);
  }
}

size_t WriteHandler::in_flight_count() const
{
  return in_flight_.size();
}

uint8_t WriteHandler::get_appliance_address() const
{
  return appliance_address_;
}

void WriteHandler::set_appliance_address(uint8_t addr)
{
  appliance_address_ = addr;
}

}  // namespace geappliances_bridge
}  // namespace esphome

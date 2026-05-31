// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Consume write commands from WriteQueue and send them to the appliance
//       via the GEA3 ERD client.
//
// Responsibilities:
//   - In each process_writes() call, check the write queue for pending commands
//   - For each command, send the write to the appliance using the GEA3 ERD client
//   - Track in-flight writes and handle responses
//   - Handle write failures (log, retry up to N times)
//
// NOT responsible for:
//   - Any MQTT publishing
//   - Queue management (WriteQueue handles that)
//
// Dependencies:
//   - write_queue.h (consumes commands from here)
//   - i_tiny_gea3_erd_client.h (sends writes via this)
// =============================================================================

#pragma once

#include <cstdint>
#include <vector>

extern "C" {
#include "i_tiny_gea3_erd_client.h"
#include "tiny_erd.h"
}

#include "write_queue.h"

namespace esphome {
namespace geappliances_bridge {

static constexpr uint8_t MAX_WRITE_RETRIES = 3;

struct InFlightWrite {
  tiny_erd_t erd_id;
  uint8_t appliance_address;
  uint8_t retries;
};

class WriteHandler {
 public:
  WriteHandler(i_tiny_gea3_erd_client_t* erd_client,
               WriteQueue* write_queue);

  /// Called from FSM loop each cycle. Processes pending writes from the queue.
  void process_writes();

  /// Called by GEA3 client callback when write response arrives.
  void on_write_response(tiny_erd_t erd_id, bool success);

  /// Returns the number of in-flight writes.
  size_t in_flight_count() const;

  /// Returns the current appliance address for writes.
  uint8_t get_appliance_address() const;

  /// Set the appliance address for writes.
  void set_appliance_address(uint8_t addr);

 private:
  i_tiny_gea3_erd_client_t* erd_client_;
  WriteQueue* write_queue_;
  uint8_t appliance_address_;
  std::vector<InFlightWrite> in_flight_;
};

}  // namespace geappliances_bridge
}  // namespace esphome

// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Provide a bounded FIFO for write commands. Used by WriteRouter (Phase 3)
//       to queue ERD write requests before the WriteHandler (Phase 2) processes them.
//
// Responsibilities:
//   - Fixed-size ring buffer of WriteCommand structs
//   - push() returns false when full — caller retries on next main loop iteration
//   - pop() returns false when empty
//   - No mutex needed (all access from ESPHome main loop task)
//
// NOT responsible for:
//   - Any appliance communication
//   - Retry logic (caller's responsibility)
//
// Dependencies:
//   - geappliances_bridge_constants.h (MAX_ERD_VALUE_SIZE, MAX_PENDING_WRITES)
//   - tiny_erd.h (tiny_erd_t type)
// =============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

extern "C" {
#include "tiny_erd.h"
}

#include "geappliances_bridge_constants.h"

namespace esphome {
namespace geappliances_bridge {

struct WriteCommand {
  tiny_erd_t erd_id;
  uint8_t value[MAX_ERD_VALUE_SIZE];
  uint8_t value_size;
  uint8_t appliance_address;
};

class WriteQueue {
 public:
  WriteQueue();

  /// Enqueue a write command. Returns false if full — caller retries.
  bool push(const WriteCommand& cmd);

  /// Dequeue a write command. Returns false if empty.
  bool pop(WriteCommand& cmd_out);

  bool is_empty() const;
  size_t size() const;

 private:
  WriteCommand queue_[MAX_PENDING_WRITES];
  size_t head_;
  size_t tail_;
  size_t count_;
};

}  // namespace geappliances_bridge
}  // namespace esphome

// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Route pre-parsed MQTT write events to WriteQueue. Subscribes to the
//       adapter's on_write_request event at construction and pushes parsed
//       WriteCommand structs to the queue.
//
// Responsibilities:
//   - Subscribe to mqtt_client_on_write_request() at construction
//   - On event, fill a WriteCommand and push to WriteQueue
//   - Handle full queue gracefully (log warning, discard the command)
//   - Unsubscribe in destructor
//
// NOT responsible for:
//   - Any MQTT subscribe function calls (MqttSideStateMachine does that)
//   - Parsing topics or payloads (already done by the adapter's lambda)
//   - Appliance communication (WriteHandler does that)
//
// Dependencies:
//   - i_mqtt_client.h (event interface)
//   - write_queue.h (WriteCommand, WriteQueue)
//   - tiny_event.h / i_tiny_event.h (subscription)
//
// Construction order constraint:
//   esphome_mqtt_client_adapter_init() must be called before WriteRouter is
//   constructed. Subscribing before tiny_event_init() on the event is UB.
// =============================================================================

#pragma once

#include <cstdint>

extern "C" {
#include "i_mqtt_client.h"
#include "i_tiny_event.h"
#include "tiny_event_subscription.h"
}

#include "write_queue.h"

// ESPHome log header — stubbed in tests via test/include/esphome/core/log.h
#ifdef USE_ESP_IDF
#include "esphome/core/log.h"
#else
#include "double/esphome_hal_double.hpp"
#endif

namespace esphome {
namespace geappliances_bridge {

class WriteRouter {
 public:
  // mqtt_client: the adapter that fires on_write_request events.
  //   MUST be fully initialized (esphome_mqtt_client_adapter_init() already
  //   called) before constructing this. Pass nullptr to skip subscription
  //   (useful for unit tests that drive the event manually).
  // write_queue: destination for parsed write commands.
  WriteRouter(i_mqtt_client_t* mqtt_client, WriteQueue* write_queue);

  ~WriteRouter();

 private:
  tiny_event_subscription_t write_request_subscription_;
  i_mqtt_client_t* mqtt_client_;
  WriteQueue* write_queue_;

  static void on_write_request_handler(void* context, const void* args);
};

}  // namespace geappliances_bridge
}  // namespace esphome

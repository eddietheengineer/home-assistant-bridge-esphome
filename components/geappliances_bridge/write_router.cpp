#include "write_router.h"
#include <cstring>

namespace esphome {
namespace geappliances_bridge {

WriteRouter::WriteRouter(i_mqtt_client_t* mqtt_client, WriteQueue* write_queue)
    : mqtt_client_(mqtt_client),
      write_queue_(write_queue)
{
  // Set up the subscription callback
  tiny_event_subscription_init(&write_request_subscription_, this,
                               [](void* ctx, const void* args) {
                                 reinterpret_cast<WriteRouter*>(ctx)
                                   ->on_write_request_handler(ctx, args);
                               });

  // Subscribe to the adapter's on_write_request event.
  // Guard against nullptr — allows unit tests to skip the real adapter.
  if (mqtt_client_ != nullptr) {
    i_tiny_event_t* event = mqtt_client_on_write_request(mqtt_client_);
    tiny_event_subscribe(event, &write_request_subscription_);
  }
}

WriteRouter::~WriteRouter()
{
  if (mqtt_client_ != nullptr) {
    i_tiny_event_t* event = mqtt_client_on_write_request(mqtt_client_);
    tiny_event_unsubscribe(event, &write_request_subscription_);
  }
}

void WriteRouter::on_write_request_handler(void* context, const void* args)
{
  WriteRouter* self = reinterpret_cast<WriteRouter*>(context);
  const auto* a = reinterpret_cast<const mqtt_client_on_write_request_args_t*>(args);

  WriteCommand cmd;
  cmd.erd_id = a->erd;
  cmd.value_size = a->size;
  cmd.appliance_address = 0xC0;  // Default appliance address

  // Copy value bytes — clamp to MAX_ERD_VALUE_SIZE
  uint8_t copy_size = (a->size < MAX_ERD_VALUE_SIZE) ? a->size : MAX_ERD_VALUE_SIZE;
  std::memset(cmd.value, 0, sizeof(cmd.value));
  if (a->value != nullptr && copy_size > 0) {
    std::memcpy(cmd.value, a->value, copy_size);
  }

  // Push to queue — if full, log and discard
  if (!self->write_queue_->push(cmd)) {
    ESP_LOGW("write_router", "Write queue full, discarding write for ERD 0x%04X", a->erd);
  }
}

}  // namespace geappliances_bridge
}  // namespace esphome

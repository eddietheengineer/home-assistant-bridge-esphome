/*!
 * @file
 * @brief Test double implementation for GEA2 ERD client interface.
 */

#include "CppUTestExt/MockSupport.h"
#include "double/tiny_gea2_erd_client_double.hpp"

static bool gea2_read(
  i_tiny_gea2_erd_client_t* self,
  tiny_gea2_erd_client_request_id_t* request_id,
  uint8_t address,
  tiny_erd_t erd)
{
  return mock()
    .actualCall("read")
    .onObject(self)
    .withOutputParameter("request_id", request_id)
    .withParameter("address", address)
    .withParameter("erd", erd)
    .returnBoolValueOrDefault(true);
}

static bool gea2_write(
  i_tiny_gea2_erd_client_t* self,
  tiny_gea2_erd_client_request_id_t* request_id,
  uint8_t address,
  tiny_erd_t erd,
  const void* data,
  uint8_t data_size)
{
  return mock()
    .actualCall("write")
    .onObject(self)
    .withOutputParameter("request_id", request_id)
    .withParameter("address", address)
    .withParameter("erd", erd)
    .withMemoryBufferParameter("data", reinterpret_cast<const unsigned char*>(data), data_size)
    .returnBoolValueOrDefault(true);
}

static i_tiny_event_t* gea2_on_activity(i_tiny_gea2_erd_client_t* _self)
{
  auto self = reinterpret_cast<tiny_gea2_erd_client_double_t*>(_self);
  return &self->on_activity.interface;
}

static const i_tiny_gea2_erd_client_api_t gea2_api = {
  gea2_read,
  gea2_write,
  gea2_on_activity
};

void tiny_gea2_erd_client_double_init(tiny_gea2_erd_client_double_t* self)
{
  self->interface.api = &gea2_api;
  tiny_event_init(&self->on_activity);
}

void tiny_gea2_erd_client_double_trigger_activity_event(
  tiny_gea2_erd_client_double_t* self,
  const tiny_gea2_erd_client_on_activity_args_t* args)
{
  tiny_event_publish(&self->on_activity, args);
}

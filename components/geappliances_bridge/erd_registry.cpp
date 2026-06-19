#include "erd_registry.h"

namespace esphome {
namespace geappliances_bridge {


void ErdRegistry::set_valid_erds(const std::set<tiny_erd_t>& erds)
{
  // An empty set would silently suppress all publishes; ignore it so filtering
  // stays disabled and all ERDs continue to be published.
  if (erds.empty()) {
    return;
  }
  valid_erds_ = erds;
  valid_erds_ready_ = true;
}

void ErdRegistry::clear_registered_erds()
{
  registered_erds_.clear();
}

void ErdRegistry::register_erd(tiny_erd_t erd)
{
  registered_erds_.insert(erd);
}

bool ErdRegistry::is_valid(tiny_erd_t erd) const
{
  if (!valid_erds_ready_) {
    return true;  // No filter active: all ERDs are valid.
  }
  return valid_erds_.count(erd) > 0;
}


}  // namespace geappliances_bridge
}  // namespace esphome

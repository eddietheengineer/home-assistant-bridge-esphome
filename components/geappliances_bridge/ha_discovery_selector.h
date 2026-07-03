/*!
 * @file
 * @brief Selector for HA discovery data variant.
 *
 * Includes either the filtered or unfiltered discovery data based on
 * the HA_DISCOVERY_UNFILTERED compile-time define.
 *
 * - Default (filtered): Smaller footprint, excludes diagnostics/config topics.
 * - Unfiltered: All entities, including diagnostics and internal state.
 *
 * Define HA_DISCOVERY_UNFILTERED at compile time to use the full dataset.
 * In ESPHome YAML, add:
 *   esphome:
 *     platformio_options:
 *       build_flags: -DHA_DISCOVERY_UNFILTERED
 */

#ifndef HA_DISCOVERY_SELECTOR_H
#define HA_DISCOVERY_SELECTOR_H

#ifdef HA_DISCOVERY_UNFILTERED
#include "ha_discovery_data_unfiltered.h"
#else
#include "ha_discovery_data.h"
#endif

#endif /* HA_DISCOVERY_SELECTOR_H */
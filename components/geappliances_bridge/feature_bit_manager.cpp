/*!
 * @file
 * @brief FeatureBitManager implementation.
 *
 * Fully self-driving: subscribes to ERD client activity events and
 * uses a periodic timer for incremental parsing.
 */

#include "feature_bit_manager.h"
#include "geappliances_bridge_constants.h"
#include "appliance_api_feature_lists.h"
#include "esphome/core/log.h"
#include <cstring>

namespace esphome {
namespace geappliances_bridge {

static const char* const TAG __attribute__((unused)) = "feature_bit";

// =============================================================================
// Public API
// =============================================================================

void FeatureBitManager::init(i_tiny_gea3_erd_client_t* erd_client,
                              uint8_t host_address,
                              tiny_timer_group_t* timer_group)
{
  if (erd_client == nullptr) {
    ESP_LOGE(TAG, "init() called with null erd_client");
    return;
  }
  if (timer_group == nullptr) {
    ESP_LOGE(TAG, "init() called with null timer_group");
    return;
  }

  this->erd_client_    = erd_client;
  this->host_address_  = host_address;
  this->timer_group_   = timer_group;
  this->state_         = FEATURE_BIT_STATE_READING_0008;
  this->read_queued_   = false;
  this->parse_erd_idx_ = 0;
  this->common_parse_idx_ = 0;
  this->parse_common_done_ = false;
  this->valid_list_ready_ = false;
  this->valid_erds_.clear();
  this->valid_erds_vec_.clear();

  // Reset ERD data buffers (struct has default initializers, so just re-default-construct)
  this->erd_data_ = FeatureBitErdData{};

  // Subscribe to ERD client activity events so we can drive the read sequence
  // without the bridge polling us.
  tiny_event_subscription_init(
    &this->erd_activity_subscription_,
    this,
    +[](void* ctx, const void* args) {
      reinterpret_cast<FeatureBitManager*>(ctx)->on_erd_activity_(args);
    });
  tiny_event_subscribe(
    tiny_gea3_erd_client_on_activity(this->erd_client_),
    &this->erd_activity_subscription_);
}

void FeatureBitManager::start()
{
  // Defensive: don't dereference null erd_client_
  if (this->erd_client_ == nullptr) {
    return;
  }
  // Idempotent: only queue the first read if we're at the start and haven't queued yet.
  if (this->state_ != FEATURE_BIT_STATE_READING_0008 || this->read_queued_) {
    return;
  }
  this->queue_erd_read_();
}

const std::set<tiny_erd_t>& FeatureBitManager::get_valid_erds() const
{
  return this->valid_erds_;
}

const std::vector<tiny_erd_t>& FeatureBitManager::get_valid_erds_vec() const
{
  return this->valid_erds_vec_;
}

// =============================================================================
// ERD client activity event handler
// =============================================================================

void FeatureBitManager::on_erd_activity_(const void* args)
{
  const tiny_gea3_erd_client_on_activity_args_t* a =
    reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(args);

  // Ignore events for other appliances (e.g., GEA2 adapter responses)
  if (a->address != this->host_address_) {
    return;
  }

  // Ignore events once we're done reading (PARSING or COMPLETE).
  // The parse timer handles the PARSING phase independently.
  if (this->state_ == FEATURE_BIT_STATE_PARSING || this->state_ == FEATURE_BIT_STATE_COMPLETE) {
    return;
  }

  // Determine which ERD we're currently waiting for.
  tiny_erd_t expected_erd = this->get_expected_erd_();

  // If we haven't queued a read yet (queue was full), retry now.
  if (!this->read_queued_) {
    this->queue_erd_read_();
    return;
  }

  // Only process events for the ERD we're actually waiting for.
  // This prevents unrelated reads (e.g., from the polling bridge) from
  // clearing read_queued_ and corrupting the read sequence.
  if (a->type == tiny_gea3_erd_client_activity_type_read_completed) {
    if (a->read_completed.erd == expected_erd) {
      this->handle_read_completed_(a->read_completed.erd,
                                    a->read_completed.data,
                                    a->read_completed.data_size);
    }
  } else if (a->type == tiny_gea3_erd_client_activity_type_read_failed) {
    if (a->read_failed.erd == expected_erd) {
      this->skip_to_next_erd_(a->read_failed.erd);
    }
  }
}

// =============================================================================
// Handle a successful ERD read
// =============================================================================

void FeatureBitManager::handle_read_completed_(tiny_erd_t erd, const void* data, uint8_t size)
{
  this->read_queued_ = false;  // Read completed, clear the guard

  if (data == nullptr) {
    ESP_LOGW(TAG, "Feature bit ERD 0x%04X: null data pointer, skipping", erd);
    this->skip_to_next_erd_(erd);
    return;
  }

  uint8_t copy_size = (size <= 8u) ? size : 8u;

  // Store the ERD data and advance to the next state
  if      (erd == ERD_APPLIANCE_TYPE)        {
    ESP_LOGD(TAG, "Re-read appliance type (0x0008): %u bytes", copy_size);
    this->state_ = FEATURE_BIT_STATE_READING_0001;
  }
  else if (erd == ERD_MODEL_NUMBER)          {
    ESP_LOGD(TAG, "Re-read model number (0x0001): %u bytes", copy_size);
    this->state_ = FEATURE_BIT_STATE_READING_0002;
  }
  else if (erd == ERD_SERIAL_NUMBER)         {
    ESP_LOGD(TAG, "Re-read serial number (0x0002): %u bytes", copy_size);
    this->state_ = FEATURE_BIT_STATE_READING_0092;
  }
  else if (erd == ERD_COMMON_FEATURE_API)    {
    memcpy(this->erd_data_.erd_0092, data, copy_size);
    this->erd_data_.erd_0092_size = copy_size;
    ESP_LOGD(TAG, "Read common feature API (0x0092): %u bytes", copy_size);
    this->state_ = FEATURE_BIT_STATE_READING_0093;
  }
  else if (erd == ERD_APPLIANCE_FEATURE_API_0) {
    memcpy(this->erd_data_.erd_0093, data, copy_size);
    this->erd_data_.erd_0093_size = copy_size;
    ESP_LOGD(TAG, "Read appliance feature API 0 (0x0093): %u bytes", copy_size);
    this->state_ = FEATURE_BIT_STATE_READING_0094;
  }
  else if (erd == ERD_APPLIANCE_FEATURE_API_1) {
    memcpy(this->erd_data_.erd_0094, data, copy_size);
    this->erd_data_.erd_0094_size = copy_size;
    ESP_LOGD(TAG, "Read appliance feature API 1 (0x0094): %u bytes", copy_size);
    this->state_ = FEATURE_BIT_STATE_READING_0095;
  }
  else if (erd == ERD_APPLIANCE_FEATURE_API_2) {
    memcpy(this->erd_data_.erd_0095, data, copy_size);
    this->erd_data_.erd_0095_size = copy_size;
    ESP_LOGD(TAG, "Read appliance feature API 2 (0x0095): %u bytes", copy_size);
    this->state_ = FEATURE_BIT_STATE_READING_0096;
  }
  else if (erd == ERD_APPLIANCE_FEATURE_API_3) {
    memcpy(this->erd_data_.erd_0096, data, copy_size);
    this->erd_data_.erd_0096_size = copy_size;
    ESP_LOGD(TAG, "Read appliance feature API 3 (0x0096): %u bytes", copy_size);
    this->state_ = FEATURE_BIT_STATE_READING_0097;
  }
  else if (erd == ERD_APPLIANCE_FEATURE_API_4) {
    memcpy(this->erd_data_.erd_0097, data, copy_size);
    this->erd_data_.erd_0097_size = copy_size;
    ESP_LOGD(TAG, "Read appliance feature API 4 (0x0097): %u bytes", copy_size);
    this->state_ = FEATURE_BIT_STATE_READING_0109;
  }
  else if (erd == ERD_APPLIANCE_FEATURE_API_5) {
    memcpy(this->erd_data_.erd_0109, data, copy_size);
    this->erd_data_.erd_0109_size = copy_size;
    ESP_LOGD(TAG, "Read appliance feature API 5 (0x0109): %u bytes", copy_size);
    this->state_ = FEATURE_BIT_STATE_READING_010A;
  }
  else if (erd == ERD_APPLIANCE_FEATURE_API_6) {
    memcpy(this->erd_data_.erd_010A, data, copy_size);
    this->erd_data_.erd_010A_size = copy_size;
    ESP_LOGD(TAG, "Read appliance feature API 6 (0x010A): %u bytes", copy_size);
    this->state_ = FEATURE_BIT_STATE_READING_010B;
  }
  else if (erd == ERD_APPLIANCE_FEATURE_API_7) {
    memcpy(this->erd_data_.erd_010B, data, copy_size);
    this->erd_data_.erd_010B_size = copy_size;
    ESP_LOGD(TAG, "Read appliance feature API 7 (0x010B): %u bytes", copy_size);
    this->state_ = FEATURE_BIT_STATE_READING_010C;
  }
  else if (erd == ERD_APPLIANCE_FEATURE_API_8) {
    memcpy(this->erd_data_.erd_010C, data, copy_size);
    this->erd_data_.erd_010C_size = copy_size;
    ESP_LOGD(TAG, "Read appliance feature API 8 (0x010C): %u bytes", copy_size);
    this->state_ = FEATURE_BIT_STATE_READING_010D;
  }
  else if (erd == ERD_APPLIANCE_FEATURE_API_9) {
    memcpy(this->erd_data_.erd_010D, data, copy_size);
    this->erd_data_.erd_010D_size = copy_size;
    ESP_LOGD(TAG, "Read appliance feature API 9 (0x010D): %u bytes", copy_size);
    // All ERDs read - transition to parsing
    this->state_ = FEATURE_BIT_STATE_PARSING;
    this->start_parse_timer_();
    return;
  }
  else {
    // Unexpected ERD - ignore
    return;
  }

  // Queue the next ERD in the sequence
  this->queue_erd_read_();
}

// =============================================================================
// Queue the next ERD read based on current state
// =============================================================================

void FeatureBitManager::queue_erd_read_()
{
  tiny_erd_t feature_erd = 0;
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
  const char* feature_name = nullptr;
#ifdef __clang__
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

  switch (this->state_) {
    case FEATURE_BIT_STATE_READING_0008: feature_erd = ERD_APPLIANCE_TYPE;        feature_name = "appliance type (0x0008)";              break;
    case FEATURE_BIT_STATE_READING_0001: feature_erd = ERD_MODEL_NUMBER;          feature_name = "model number (0x0001)";                break;
    case FEATURE_BIT_STATE_READING_0002: feature_erd = ERD_SERIAL_NUMBER;         feature_name = "serial number (0x0002)";               break;
    case FEATURE_BIT_STATE_READING_0092: feature_erd = ERD_COMMON_FEATURE_API;    feature_name = "common feature API (0x0092)";          break;
    case FEATURE_BIT_STATE_READING_0093: feature_erd = ERD_APPLIANCE_FEATURE_API_0; feature_name = "appliance feature API 0 (0x0093)";       break;
    case FEATURE_BIT_STATE_READING_0094: feature_erd = ERD_APPLIANCE_FEATURE_API_1; feature_name = "appliance feature API 1 (0x0094)";       break;
    case FEATURE_BIT_STATE_READING_0095: feature_erd = ERD_APPLIANCE_FEATURE_API_2; feature_name = "appliance feature API 2 (0x0095)";       break;
    case FEATURE_BIT_STATE_READING_0096: feature_erd = ERD_APPLIANCE_FEATURE_API_3; feature_name = "appliance feature API 3 (0x0096)";       break;
    case FEATURE_BIT_STATE_READING_0097: feature_erd = ERD_APPLIANCE_FEATURE_API_4; feature_name = "appliance feature API 4 (0x0097)";       break;
    case FEATURE_BIT_STATE_READING_0109: feature_erd = ERD_APPLIANCE_FEATURE_API_5; feature_name = "appliance feature API 5 (0x0109)";       break;
    case FEATURE_BIT_STATE_READING_010A: feature_erd = ERD_APPLIANCE_FEATURE_API_6; feature_name = "appliance feature API 6 (0x010A)";       break;
    case FEATURE_BIT_STATE_READING_010B: feature_erd = ERD_APPLIANCE_FEATURE_API_7; feature_name = "appliance feature API 7 (0x010B)";       break;
    case FEATURE_BIT_STATE_READING_010C: feature_erd = ERD_APPLIANCE_FEATURE_API_8; feature_name = "appliance feature API 8 (0x010C)";       break;
    case FEATURE_BIT_STATE_READING_010D: feature_erd = ERD_APPLIANCE_FEATURE_API_9; feature_name = "appliance feature API 9 (0x010D)";       break;
    default: return;  // Not in a READING state - nothing to queue
  }

  // Try to queue the read. If the queue is full, stay in the current state
  // and schedule a retry timer.
  tiny_gea3_erd_client_request_id_t req_id;
  if (tiny_gea3_erd_client_read(this->erd_client_, &req_id, this->host_address_, feature_erd)) {
    ESP_LOGD(TAG, "Queued read for %s", feature_name);
    this->read_queued_ = true;
  } else {
    // Queue is full — arm a one-shot retry timer so we don't stall
    // indefinitely when no other ERD activity occurs.
    ESP_LOGD(TAG, "Queue full for %s, scheduling retry in %u ms",
             feature_name, static_cast<uint32_t>(QUEUE_RETRY_MS));
    tiny_timer_start(this->timer_group_,
                     &this->queue_retry_timer_,
                     QUEUE_RETRY_MS,
                     this,
                     FeatureBitManager::queue_retry_timer_callback_);
  }
}

// =============================================================================
// Map a READING state to the ERD value (for event filtering)
// =============================================================================

tiny_erd_t FeatureBitManager::get_expected_erd_() const
{
  switch (this->state_) {
    case FEATURE_BIT_STATE_READING_0008: return ERD_APPLIANCE_TYPE;
    case FEATURE_BIT_STATE_READING_0001: return ERD_MODEL_NUMBER;
    case FEATURE_BIT_STATE_READING_0002: return ERD_SERIAL_NUMBER;
    case FEATURE_BIT_STATE_READING_0092: return ERD_COMMON_FEATURE_API;
    case FEATURE_BIT_STATE_READING_0093: return ERD_APPLIANCE_FEATURE_API_0;
    case FEATURE_BIT_STATE_READING_0094: return ERD_APPLIANCE_FEATURE_API_1;
    case FEATURE_BIT_STATE_READING_0095: return ERD_APPLIANCE_FEATURE_API_2;
    case FEATURE_BIT_STATE_READING_0096: return ERD_APPLIANCE_FEATURE_API_3;
    case FEATURE_BIT_STATE_READING_0097: return ERD_APPLIANCE_FEATURE_API_4;
    case FEATURE_BIT_STATE_READING_0109: return ERD_APPLIANCE_FEATURE_API_5;
    case FEATURE_BIT_STATE_READING_010A: return ERD_APPLIANCE_FEATURE_API_6;
    case FEATURE_BIT_STATE_READING_010B: return ERD_APPLIANCE_FEATURE_API_7;
    case FEATURE_BIT_STATE_READING_010C: return ERD_APPLIANCE_FEATURE_API_8;
    case FEATURE_BIT_STATE_READING_010D: return ERD_APPLIANCE_FEATURE_API_9;
    default: return 0;  // Not in a READING state
  }
}

// =============================================================================
// Queue retry timer callback
// =============================================================================

void FeatureBitManager::queue_retry_timer_callback_(void* context)
{
  reinterpret_cast<FeatureBitManager*>(context)->queue_retry_();
}

void FeatureBitManager::queue_retry_()
{
  // Don't retry if we've moved past the READING state (e.g., an event
  // arrived and completed the read before the timer fired).
  if (this->state_ == FEATURE_BIT_STATE_PARSING || this->state_ == FEATURE_BIT_STATE_COMPLETE) {
    return;
  }
  if (this->read_queued_) {
    // Read was already queued by an intervening event — nothing to do.
    return;
  }
  this->queue_erd_read_();
}

// =============================================================================
// Skip to the next ERD in the sequence (on failure)
// =============================================================================

void FeatureBitManager::skip_to_next_erd_(tiny_erd_t failed_erd)
{
  this->read_queued_ = false;  // Read failed, clear the guard

  ESP_LOGD(TAG, "Feature bit ERD 0x%04X failed or not supported, skipping", failed_erd);

  switch (failed_erd) {
    case ERD_APPLIANCE_TYPE:          this->state_ = FEATURE_BIT_STATE_READING_0001; break;
    case ERD_MODEL_NUMBER:            this->state_ = FEATURE_BIT_STATE_READING_0002; break;
    case ERD_SERIAL_NUMBER:           this->state_ = FEATURE_BIT_STATE_READING_0092; break;
    case ERD_COMMON_FEATURE_API:      this->state_ = FEATURE_BIT_STATE_READING_0093; break;
    case ERD_APPLIANCE_FEATURE_API_0: this->state_ = FEATURE_BIT_STATE_READING_0094; break;
    case ERD_APPLIANCE_FEATURE_API_1: this->state_ = FEATURE_BIT_STATE_READING_0095; break;
    case ERD_APPLIANCE_FEATURE_API_2: this->state_ = FEATURE_BIT_STATE_READING_0096; break;
    case ERD_APPLIANCE_FEATURE_API_3: this->state_ = FEATURE_BIT_STATE_READING_0097; break;
    case ERD_APPLIANCE_FEATURE_API_4: this->state_ = FEATURE_BIT_STATE_READING_0109; break;
    case ERD_APPLIANCE_FEATURE_API_5: this->state_ = FEATURE_BIT_STATE_READING_010A; break;
    case ERD_APPLIANCE_FEATURE_API_6: this->state_ = FEATURE_BIT_STATE_READING_010B; break;
    case ERD_APPLIANCE_FEATURE_API_7: this->state_ = FEATURE_BIT_STATE_READING_010C; break;
    case ERD_APPLIANCE_FEATURE_API_8: this->state_ = FEATURE_BIT_STATE_READING_010D; break;
    case ERD_APPLIANCE_FEATURE_API_9:
      // Last ERD failed - transition to parsing with whatever we have
      this->state_ = FEATURE_BIT_STATE_PARSING;
      this->start_parse_timer_();
      return;
    default: break;
  }

  // Queue the next ERD read
  this->queue_erd_read_();
}

// =============================================================================
// Timer-driven incremental parsing
// =============================================================================

void FeatureBitManager::start_parse_timer_()
{
  // Start periodic timer for incremental parsing
  tiny_timer_start_periodic(this->timer_group_,
                            &this->parse_timer_,
                            PARSE_TICK_MS,
                            this,
                            FeatureBitManager::parse_timer_callback_);
}

void FeatureBitManager::parse_timer_callback_(void* context)
{
  reinterpret_cast<FeatureBitManager*>(context)->parse_next_step_();
}

void FeatureBitManager::parse_next_step_()
{
  // First call: initialize (clear state).
  if (this->parse_erd_idx_ == 0 && !this->parse_common_done_ && this->common_parse_idx_ == 0) {
    this->valid_erds_.clear();
    this->valid_erds_vec_.clear();
    this->valid_list_ready_ = false;

    if (this->erd_data_.erd_0092_size > 0) {
      uint32_t common_bits = static_cast<uint32_t>(
        read_be64(this->erd_data_.erd_0092, this->erd_data_.erd_0092_size) & 0xFFFFFFFFu);
      ESP_LOGI(TAG, "Common feature API (0x0092) value: 0x%08X", common_bits);
      (void)common_bits; /* Used in ESP_LOGI that may be compiled out. */
    }
  }

  // Parse common features incrementally (up to COMMON_PARSE_PER_CALL descriptors per call).
  if (!this->parse_common_done_) {
    uint16_t start = this->common_parse_idx_;
    uint16_t end = (start + COMMON_PARSE_PER_CALL > common_feature_descriptor_count)
                   ? common_feature_descriptor_count : start + COMMON_PARSE_PER_CALL;

    if (this->erd_data_.erd_0092_size > 0) {
      uint32_t common_bits = static_cast<uint32_t>(
        read_be64(this->erd_data_.erd_0092, this->erd_data_.erd_0092_size) & 0xFFFFFFFFu);
      for (uint16_t i = start; i < end; i++) {
        const auto& desc = common_feature_descriptors[i];
        if (common_bits & desc.bit_mask) {
          ESP_LOGI(TAG, "  [SET] Common feature: %s (mask 0x%08X, %u ERDs)",
                   desc.name, desc.bit_mask, desc.erd_count);
          for (uint16_t j = 0; j < desc.erd_count; j++) {
            this->valid_erds_.insert(desc.erds[j]);
          }
        }
      }
    }

    this->common_parse_idx_ = end;
    if (this->common_parse_idx_ >= common_feature_descriptor_count) {
      this->parse_common_done_ = true;
      // Fall through to parse the first appliance ERD on the next tick.
      return;
    }
    // Not done with common features yet - return to keep timer tick short.
    return;
  }

  // Static tables for appliance ERDs (indexed 0-9).
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
  static const char* const erd_names[10] = {
    "0x0093", "0x0094", "0x0095", "0x0096", "0x0097",
    "0x0109", "0x010A", "0x010B", "0x010C", "0x010D"
  };
#ifdef __clang__
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

  // Process one appliance ERD per tick to avoid blocking the timer for too long.
  if (this->parse_erd_idx_ < 10) {
    uint8_t idx = this->parse_erd_idx_;

    // Map index to the actual erd_data_ member.
    const uint8_t* api_bufs[10] = {
      this->erd_data_.erd_0093, this->erd_data_.erd_0094,
      this->erd_data_.erd_0095, this->erd_data_.erd_0096,
      this->erd_data_.erd_0097, this->erd_data_.erd_0109,
      this->erd_data_.erd_010A, this->erd_data_.erd_010B,
      this->erd_data_.erd_010C, this->erd_data_.erd_010D
    };
    const uint8_t api_sizes[10] = {
      this->erd_data_.erd_0093_size, this->erd_data_.erd_0094_size,
      this->erd_data_.erd_0095_size, this->erd_data_.erd_0096_size,
      this->erd_data_.erd_0097_size, this->erd_data_.erd_0109_size,
      this->erd_data_.erd_010A_size, this->erd_data_.erd_010B_size,
      this->erd_data_.erd_010C_size, this->erd_data_.erd_010D_size
    };

    if (api_sizes[idx] < APPLIANCE_FEATURE_ERD_SIZE) {
      this->parse_erd_idx_++;
      return;  // Will process next ERD on next tick
    }
    const uint8_t* api_buf = api_bufs[idx];
    uint16_t appliance_type    = (static_cast<uint16_t>(api_buf[0]) << 8) | api_buf[1];
    uint16_t version           = (static_cast<uint16_t>(api_buf[2]) << 8) | api_buf[3];
    uint32_t feature_bitmap    = (static_cast<uint32_t>(api_buf[4]) << 24) |
                                 (static_cast<uint32_t>(api_buf[5]) << 16) |
                                 (static_cast<uint32_t>(api_buf[6]) <<  8) | api_buf[7];

    if (appliance_type == 0 && version == 0 && feature_bitmap == 0) {
      this->parse_erd_idx_++;
      return;  // Will process next ERD on next tick
    }
    ESP_LOGI(TAG, "Appliance feature ERD %s: type 0x%04X, version %u, features 0x%08X",
             erd_names[idx], appliance_type, version, feature_bitmap);
    bool found_descriptor = false;
    for (uint16_t i = 0; i < appliance_feature_api_descriptor_count; i++) {
      const auto& desc = appliance_feature_api_descriptors[i];
      if (desc.feature_type != appliance_type || desc.version != version) continue;
      found_descriptor = true;
      if (feature_bitmap & desc.bit_mask) {
        ESP_LOGI(TAG, "  [SET] %s (mask 0x%08X, %u ERDs)",
                 desc.name, desc.bit_mask, desc.erd_count);
        for (uint16_t j = 0; j < desc.erd_count; j++) {
          this->valid_erds_.insert(desc.erds[j]);
        }
      }
    }
    if (!found_descriptor) {
      ESP_LOGW(TAG, "  No descriptor for appliance type 0x%04X version %u",
               appliance_type, version);
    }

    this->parse_erd_idx_++;
    return;  // Process only one ERD per tick.
  }

  // All appliance ERDs parsed - finalize.
  // Add mandatory ERDs (always published regardless of feature bits).
  static const tiny_erd_t mandatory_erds[] = {
    ERD_MODEL_NUMBER, ERD_SERIAL_NUMBER, ERD_APPLIANCE_TYPE,
    ERD_COMMON_FEATURE_API, ERD_APPLIANCE_FEATURE_API_0,
    ERD_APPLIANCE_FEATURE_API_1, ERD_APPLIANCE_FEATURE_API_2,
    ERD_APPLIANCE_FEATURE_API_3, ERD_APPLIANCE_FEATURE_API_4,
    ERD_APPLIANCE_FEATURE_API_5, ERD_APPLIANCE_FEATURE_API_6,
    ERD_APPLIANCE_FEATURE_API_7, ERD_APPLIANCE_FEATURE_API_8,
    ERD_APPLIANCE_FEATURE_API_9
  };
  for (auto erd : mandatory_erds) {
    this->valid_erds_.insert(erd);
  }

  this->valid_erds_vec_.assign(this->valid_erds_.begin(), this->valid_erds_.end());
  this->valid_list_ready_ = true;
  ESP_LOGI(TAG, "Feature bit parsing complete: %zu valid ERDs", this->valid_erds_.size());

  // Stop the parse timer and transition to COMPLETE
  tiny_timer_stop(this->timer_group_, &this->parse_timer_);
  this->state_ = FEATURE_BIT_STATE_COMPLETE;
}

}  // namespace geappliances_bridge
}  // namespace esphome

#pragma once
// One-time cleanup used only by aircon-setup.yaml.
//
// Both buttons refuse to run while an OTA upload holds the update lock, while the next boot
// would start a different slot than the one running (an update is pending), or while the
// running image is still waiting to be confirmed (ESPHome confirms it about a minute after
// boot). They do not touch the bootloader, the partition table or otadata.

#include <algorithm>
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esphome/components/ota/ota_backend.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace aircon_cleanup {

static const char *const TAG = "cleanup";

// True when the running image is the one that will boot next and it is confirmed.
inline bool safe_to_clean() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *boot = esp_ota_get_boot_partition();
  if (running == nullptr || boot == nullptr) {
    ESP_LOGE(TAG, "Refused: cannot identify the running or boot slot");
    return false;
  }
  if (running->address != boot->address) {
    ESP_LOGE(TAG, "Refused: next boot is %s but %s is running (an update is pending)", boot->label,
             running->label);
    return false;
  }
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
    ESP_LOGE(TAG, "Refused: running image %s is not confirmed yet, try again in a minute", running->label);
    return false;
  }
  return true;
}

// Erase a partition in 64 KiB steps, feeding the watchdog between steps.
inline bool erase_partition(const esp_partition_t *part) {
  for (size_t offset = 0; offset < part->size; offset += 0x10000) {
    const size_t len = std::min<size_t>(0x10000, part->size - offset);
    const esp_err_t err = esp_partition_erase_range(part, offset, len);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Erase of %s failed at 0x%06X: %s", part->label, (unsigned) offset, esp_err_to_name(err));
      return false;
    }
    esphome::App.feed_wdt();
  }
  return true;
}

}  // namespace aircon_cleanup

// Erase the OTA slot we are not running from, so nothing of an older firmware is left there.
inline void aircon_erase_standby_slot() {
  using namespace aircon_cleanup;
  esphome::ota::UpdateLock lock;
  if (!lock.try_acquire()) {
    ESP_LOGE(TAG, "Refused: an update is in progress");
    return;
  }
  if (!safe_to_clean())
    return;
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *standby = esp_ota_get_next_update_partition(nullptr);
  if (standby == nullptr || standby->address == running->address) {
    ESP_LOGE(TAG, "Refused: no standby OTA slot found");
    return;
  }
  ESP_LOGW(TAG, "Erasing standby slot %s at 0x%06X (%u bytes), running from %s", standby->label,
           (unsigned) standby->address, (unsigned) standby->size, running->label);
  if (erase_partition(standby)) {
    ESP_LOGW(TAG, "Done: standby slot %s erased", standby->label);
  }
}

// Erase the whole settings storage (NVS), including saved Wi-Fi credentials, then restart at
// once so nothing is written back. The fallback hotspot then asks for Wi-Fi again.
inline void aircon_wipe_settings() {
  using namespace aircon_cleanup;
  esphome::ota::UpdateLock lock;
  if (!lock.try_acquire()) {
    ESP_LOGE(TAG, "Refused: an update is in progress");
    return;
  }
  if (!safe_to_clean())
    return;
  const esp_partition_t *nvs =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, nullptr);
  if (nvs == nullptr) {
    ESP_LOGE(TAG, "Refused: NVS partition not found");
    return;
  }
  ESP_LOGW(TAG, "Erasing settings storage %s at 0x%06X (%u bytes), then restarting", nvs->label,
           (unsigned) nvs->address, (unsigned) nvs->size);
  nvs_flash_deinit();
  if (erase_partition(nvs)) {
    ESP_LOGW(TAG, "Done: settings storage erased, restarting");
  } else {
    ESP_LOGE(TAG, "Erase incomplete, restarting; press again after boot");
  }
  esphome::delay(200);  // let the log line go out
  esp_restart();
}

#pragma once
// One-time cleanup used only by aircon-setup.yaml.

#include <algorithm>
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"

// Erase the OTA slot we are not running from. ESPHome's OTA only erases as far as the new
// image reaches, so an older, longer image would otherwise leave its tail behind.
inline void aircon_erase_standby_slot() {
  static const char *const TAG = "cleanup";
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *standby = esp_ota_get_next_update_partition(nullptr);
  if (standby == nullptr || standby == running) {
    ESP_LOGE(TAG, "No standby OTA slot found");
    return;
  }
  ESP_LOGW(TAG, "Erasing standby slot %s at 0x%06X (%u bytes)", standby->label,
           (unsigned) standby->address, (unsigned) standby->size);
  for (size_t offset = 0; offset < standby->size; offset += 0x10000) {
    const size_t len = std::min<size_t>(0x10000, standby->size - offset);
    const esp_err_t err = esp_partition_erase_range(standby, offset, len);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Erase failed at 0x%06X: %s", (unsigned) offset, esp_err_to_name(err));
      return;
    }
    esphome::App.feed_wdt();
  }
  ESP_LOGW(TAG, "Standby slot %s erased", standby->label);
}

// Erase the whole settings storage (NVS), including saved Wi-Fi credentials, then restart
// at once so nothing is written back. The fallback hotspot then asks for Wi-Fi again.
inline void aircon_wipe_settings() {
  static const char *const TAG = "cleanup";
  ESP_LOGW(TAG, "Erasing the whole NVS partition and restarting");
  esphome::App.feed_wdt();
  const esp_err_t err = nvs_flash_erase();
  ESP_LOGW(TAG, "nvs_flash_erase: %s", esp_err_to_name(err));
  esp_restart();
}

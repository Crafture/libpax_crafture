/*
LICENSE

Copyright  2020      Deutsche Bahn Station&Service AG

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include <libpax_api.h>
#include "blescan.h"
#include "libpax.h"
#include "wifiscan.h"
#include "Ticker.h"

#include <esp_event.h>  // needed for Wifi event handler
#include <esp_log.h>
#include <esp_spi_flash.h>  // needed for reading ESP32 chip attributes
#include <string.h>
#include <iostream>
#include "freertos/task.h"    // needed for tasks
#include "freertos/timers.h"  // TimerHandle_t
#include "crontab.h"


#define SECOND 1000// 1 second 
#define MINUTE 1000 * 60 // 1 minute
#define RESET_BLE_SET_INTERVAL 1000 * 60 * 60 // 1 hour

struct libpax_config_t current_config;
int config_set = 0;

void (*report_callback)(void);
struct count_payload_t* pCurrent_count;
int counter_mode;

void fill_counter(struct count_payload_t* pCount) {
  pCount->wifi_count = libpax_wifi_counter_count();
  pCount->ble_count = libpax_ble_counter_count();
  pCount->pax = pCount->wifi_count + pCount->ble_count;
}

void libpax_counter_reset() {
  macs_wifi = 0;
  macs_ble = 0;
}

void report(TimerHandle_t xTimer) {
  fill_counter(pCurrent_count);
  report_callback();
  // clear counter if not in cumulative counter mode
  if (counter_mode != 1) {
    libpax_counter_reset();
  }
}

void libpax_serialize_config(char* store_addr,
                             struct libpax_config_t* configuration) {
  struct libpax_config_storage_t storage_buffer;
  storage_buffer.major_version = CONFIG_MAJOR_VERSION;
  storage_buffer.minor_version = CONFIG_MINOR_VERSION;
  memcpy(&(storage_buffer.config), configuration,
         sizeof(struct libpax_config_t));
  memset(storage_buffer.checksum, 0, sizeof(storage_buffer.checksum));
  memcpy(store_addr, &storage_buffer, sizeof(struct libpax_config_storage_t));
}

int libpax_deserialize_config(char* source,
                              struct libpax_config_t* configuration) {
  struct libpax_config_storage_t storage_buffer;
  memcpy(&storage_buffer, source, sizeof(struct libpax_config_storage_t));
  if (storage_buffer.major_version != CONFIG_MAJOR_VERSION) {
    ESP_LOGE("configuration",
             "Restoring incompatible config with different MAJOR version: "
             "%d.%d instead of %d.%d",
             storage_buffer.major_version, storage_buffer.minor_version,
             CONFIG_MAJOR_VERSION, CONFIG_MINOR_VERSION);
    return -1;
  }
  if (storage_buffer.minor_version != CONFIG_MINOR_VERSION) {
    ESP_LOGW(
        "configuration",
        "Restoring config with different MINOR version: %d.%d instead of %d.%d",
        storage_buffer.major_version, storage_buffer.minor_version,
        CONFIG_MAJOR_VERSION, CONFIG_MINOR_VERSION);
  }
  memcpy(configuration, &(storage_buffer.config),
         sizeof(struct libpax_config_t));
  return 0;
}

void libpax_default_config(struct libpax_config_t* configuration) {
  memset(configuration, 0, sizeof(struct libpax_config_t));
  configuration->blecounter = 0;
  configuration->wificounter = 1;
  strcpy(configuration->wifi_my_country_str, "01");
  configuration->wifi_channel_map = 0b100010100100100;
  configuration->wifi_channel_switch_interval = 50;
  configuration->wifi_rssi_threshold = 0;
  configuration->ble_rssi_threshold = 0;
  configuration->blescaninterval = 80;
  configuration->blescantime = 0;
  configuration->blescanwindow = 80;
}

Ticker periodicResetMacAddr;
void resetMacAddressesWrapper() {
  std::cout << "IN RESETMAC WRAPPER" << std::endl;
  periodicResetMacAddr.attach_ms(RESET_BLE_SET_INTERVAL, resetMacAddresses);
  std::cout << "MAC ID's ticket initialized, reset every " << RESET_BLE_SET_INTERVAL << " ms" << std::endl;
}

void  checkandResetBleCounter() {
  uint8_t hour = 23;
  uint8_t minute = 59;
  if (crontab(hour, minute)) {
    resetBleCounter();
    std::cout << "BLE Counter reset in checkAndResetBleCounter" << std::endl;
    return ;
  }
  std::cout << "checkandResetBleCounter: Condition false" << std::endl;
}

Ticker periodicResetBleCounter;
void resetBLECounterRoutine(){
    periodicResetCounter.attach_ms(MINUTE, checkandResetBleCounter)
}

void libpax_get_current_config(struct libpax_config_t* configuration) {
  memcpy(configuration, &current_config, sizeof(struct libpax_config_t));
}

int libpax_update_config(struct libpax_config_t* configuration) {
  int result = 0;

#ifndef LIBPAX_WIFI
  if (configuration->wificounter) {
    ESP_LOGE("configuration",
             "Configuration requests Wi-Fi but was disabled at compile time.");
    result &= LIBPAX_ERROR_WIFI_NOT_AVAILABLE;
  }
#endif

#ifndef LIBPAX_BLE
  if (configuration->blecounter) {
    ESP_LOGE("configuration",
             "Configuration requests BLE but was disabled at compile time.");
    result &= LIBPAX_ERROR_BLE_NOT_AVAILABLE;
  }
#endif

  if (result == 0) {
    memcpy(&current_config, configuration, sizeof(struct libpax_config_t));
    // this if to keep v1.0.1 backward compatibility
    if (strcmp(current_config.wifi_my_country_str, "")) {
      strcpy(current_config.wifi_my_country_str, current_config.wifi_my_country ? "DE" : "01");
    }
    config_set = 1;
  }
  return result;
}

TimerHandle_t PaxReportTimer = NULL;
int libpax_counter_init(void (*init_callback)(void),
                        struct count_payload_t* init_current_count,
                        uint16_t init_pax_report_interval_sec,
                        int init_counter_mode) {
  if (PaxReportTimer != NULL && xTimerIsTimerActive(PaxReportTimer)) {
    ESP_LOGW("initialization", "lib already active. Ignoring new init.");
    return -1;
  }

  report_callback = init_callback;
  pCurrent_count = init_current_count;
  counter_mode = init_counter_mode;

  /* Initialize NVS — it is used to store PHY calibration data. */
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  libpax_counter_reset();

  PaxReportTimer = xTimerCreate(
      "PaxReportTimer", pdMS_TO_TICKS(init_pax_report_interval_sec * 1000),
      pdTRUE, (void*)0, report);
  xTimerStart(PaxReportTimer, 0);
  return 0;
}

int libpax_counter_start() {
  if (config_set == 0) {
    ESP_LOGE("configuration", "Configuration was not yet set.");
    return -1;
  }
  // turn on BT before Wifi, since the ESP32 API coexistence configuration option depends 
  // on the Bluetooth configuration option
    if (current_config.blecounter) {
    set_BLE_rssi_filter(current_config.ble_rssi_threshold);
    start_BLE_scan(current_config.blescantime, current_config.blescanwindow,
                   current_config.blescaninterval);
  }
  if (current_config.wificounter) {
    wifi_sniffer_init(current_config.wifi_channel_switch_interval);
    set_wifi_country(current_config.wifi_my_country_str);
    set_wifi_channels(current_config.wifi_channel_map);
    set_wifi_rssi_filter(current_config.wifi_rssi_threshold);
  }
  return 0;
}

int libpax_counter_stop() {
  if (PaxReportTimer == NULL) {
    return -1;
  }
  wifi_sniffer_stop();
  stop_BLE_scan();
  xTimerStop(PaxReportTimer, 0);
  PaxReportTimer = NULL;
  return 0;
}

int libpax_counter_count(struct count_payload_t* count) {
  fill_counter(count);
  return 0;
}

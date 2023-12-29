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
#include "globals.h"

#include <libpax_api.h>
#include <string.h>
#include <unistd.h>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include "esp_log.h"
#include "libpax.h"

typedef uint32_t bitmap_t;
enum { BITS_PER_WORD = sizeof(bitmap_t) * CHAR_BIT };
#define WORD_OFFSET(b) ((b) / BITS_PER_WORD)
#define BIT_OFFSET(b) ((b) % BITS_PER_WORD)
#define LIBPAX_MAX_SIZE 0xFFFF  // full enumeration of uint16_t
#define LIBPAX_MAP_SIZE (LIBPAX_MAX_SIZE / BITS_PER_WORD)

// DRAM_ATTR bitmap_t seen_ids_map[LIBPAX_MAP_SIZE];
int seen_ids_count = 0;

uint16_t volatile macs_wifi = 0;
uint16_t volatile macs_ble = 0;
uint32_t volatile counted_ble = 0;
uint8_t volatile channel = 0;         // channel rotation counter

std::set<std::string> macAddressIds;  // set of macaddresses


// IRAM_ATTR void set_id(bitmap_t *bitmap, uint16_t id) {
//   bitmap[WORD_OFFSET(id)] |= ((bitmap_t)1 << BIT_OFFSET(id));
// }

// IRAM_ATTR int get_id(bitmap_t *bitmap, uint16_t id) {
//   bitmap_t bit = bitmap[WORD_OFFSET(id)] & ((bitmap_t)1 << BIT_OFFSET(id));
//   return bit != 0;
// }

/** remember given id
 * returns 1 if id is new, 0 if already seen this is since last reset
 */
// IRAM_ATTR int add_to_bucket(uint16_t id) {
//   if (get_id(seen_ids_map, id)) {
//     return 0;  // already seen
//   } else {
//     set_id(seen_ids_map, id);
//     seen_ids_count++;
//     return 1;  // new
//   }
// }

// void reset_bucket() {
//   memset(seen_ids_map, 0, sizeof(seen_ids_map));
//   seen_ids_count = 0;
// }


void resetMacAddresses() {
  counted_ble += macAddressIds.size();
  std::cout << "MAC ID's size == " << macAddressIds.size() << std::endl;
  macAddressIds.clear();
  std::cout << "MAC ID's Cleared from set" << std::endl;
  std::cout << "MAC ID's size == " << macAddressIds.size() << std::endl;
}

void resetBleCounter() {
  std::cout << "BLE COUNTER before reset " << counted_ble << std::endl;
  counted_ble = 0;
  macAddressIds.clear();
  std::cout << "BLE COUNTER after reset " << counted_ble << std::endl;
}

int libpax_wifi_counter_count() { return macs_wifi; }

int libpax_ble_counter_count() { return macs_ble; }

std::string convert_mac_to_id(uint8_t *paddr) {
  std::stringstream ss;
  ss << std::hex << std::uppercase;

  // Begin at index 4 for the last two bytes of the MAC address
  for (int i = 4; i < 6; ++i) {
    ss << std::setw(2) << std::setfill('0') << static_cast<int>(paddr[i]);
  }

  return ss.str();
}

void print_mac_addresses(const std::set<std::string> &macAddressIds) {
  std::cout << "======================================\n";
  for (const auto &mac : macAddressIds) {
    std::cout << mac << std::endl;
  }
  std::cout << "======================================\n";
}

IRAM_ATTR int mac_add(uint8_t *paddr, snifftype_t sniff_type) {
  uint16_t *id;
  // mac addresses are 6 bytes long, we only use the last two bytes
  id = (uint16_t *)(paddr + 4);

  if (sniff_type == MAC_SNIFF_BLE) {
    if (!(paddr[0] & 0b10)) {
      return 0;
    }
    std::string id = convert_mac_to_id(paddr);
    macAddressIds.insert(id);
    macs_ble = (counted_ble + macAddressIds.size());
    // std::cout << "BLE SCANNED: " << macAddressIds.size() << std::endl;
    return 1;
  }

  return 0;  // function returns bool if a new and unique Wifi or BLE mac
             // was counted (true) or not (false)
}
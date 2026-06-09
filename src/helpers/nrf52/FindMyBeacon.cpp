#include "FindMyBeacon.h"

// Gated on the opt-in feature flag so the unit stays inert even if a variant's build_src_filter
// happens to glob helpers/nrf52/*.cpp.
#ifdef WITH_FINDMY_BEACON

#include <string.h>
#include <stdio.h>
#include <bluefruit.h>
#include <InternalFileSystem.h>
#include <base64.hpp>
#include "ble_gap.h"
#include "../NRF52Board.h"

using namespace Adafruit_LittleFS_Namespace;

// Advertising interval in units of 0.625 ms. ~2s by default to keep idle current low;
// override per-build with -D FINDMY_ADV_INTERVAL=<units>.
#ifndef FINDMY_ADV_INTERVAL
#define FINDMY_ADV_INTERVAL 3200
#endif

#define FINDMY_FILE "/findmy"

FindMyBeacon findmy_beacon;

bool FindMyBeacon::load() {
  _enabled = 0;
  memset(_key, 0, sizeof(_key));
  if (!InternalFS.exists(FINDMY_FILE)) return false;
  File f = InternalFS.open(FINDMY_FILE);
  if (!f) return false;
  f.read((uint8_t *)&_enabled, sizeof(_enabled));
  f.read(_key, sizeof(_key));
  f.close();
  return true;
}

void FindMyBeacon::save() {
  InternalFS.remove(FINDMY_FILE);
  File f = InternalFS.open(FINDMY_FILE, FILE_O_WRITE);
  if (!f) return;
  f.write((uint8_t *)&_enabled, sizeof(_enabled));
  f.write(_key, sizeof(_key));
  f.close();
}

void FindMyBeacon::begin(int8_t tx_dbm) {
  if (_started) return;

  load();
  if (!_enabled) return;

  bool key_set = false;
  for (size_t i = 0; i < sizeof(_key); i++) { if (_key[i]) { key_set = true; break; } }
  if (!key_set) return;

  // Bring up the SoftDevice/Bluefruit stack (shared one-shot guard - see NRF52Board).
  if (!NRF52Board::beginBluefruitOnce()) return;

  Bluefruit.setTxPower(tx_dbm);

  // Static-random BLE address derived from the first 6 key bytes. ble_gap_addr_t.addr is
  // little-endian (addr[0] = LSB); the MSB's top two bits mark a static random address.
  ble_gap_addr_t addr;
  memset(&addr, 0, sizeof(addr));
  addr.addr_type = BLE_GAP_ADDR_TYPE_RANDOM_STATIC;
  addr.addr[5] = _key[0] | 0xC0;
  addr.addr[4] = _key[1];
  addr.addr[3] = _key[2];
  addr.addr[2] = _key[3];
  addr.addr[1] = _key[4];
  addr.addr[0] = _key[5];
  sd_ble_gap_addr_set(&addr);

  // 31-byte OpenHaystack advertisement payload.
  uint8_t adv[31];
  adv[0]  = 0x1E;        // length: 30 bytes follow
  adv[1]  = 0xFF;        // AD type: manufacturer specific data
  adv[2]  = 0x4C;        // company id: Apple (0x004C), little-endian
  adv[3]  = 0x00;
  adv[4]  = 0x12;        // Apple payload type: offline finding
  adv[5]  = 0x19;        // length of remaining offline-finding payload (25)
  adv[6]  = 0x00;        // status byte
  memcpy(&adv[7], &_key[6], 22);  // public key bytes 6..27
  adv[29] = _key[0] >> 6;         // top two bits of key[0]
  adv[30] = 0x00;                 // hint

  Bluefruit.Advertising.stop();
  Bluefruit.Advertising.clearData();
  Bluefruit.ScanResponse.clearData();
  Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_NONCONNECTABLE_NONSCANNABLE_UNDIRECTED);
  Bluefruit.Advertising.setData(adv, sizeof(adv));
  Bluefruit.Advertising.restartOnDisconnect(false);
  Bluefruit.Advertising.setInterval(FINDMY_ADV_INTERVAL, FINDMY_ADV_INTERVAL);
  Bluefruit.Advertising.setFastTimeout(0);

  _started = Bluefruit.Advertising.start(0);  // 0 = advertise forever
}

void FindMyBeacon::stop() {
  if (_started) {
    Bluefruit.Advertising.stop();
    _started = false;
  }
}

bool FindMyBeacon::handleCommand(const char* command, char* reply) {
  if (memcmp(command, "set findmy.key ", 15) == 0) {
    const char* b64 = &command[15];
    uint8_t decoded[40];   // 28-byte key encodes to 40 base64 chars
    unsigned int len = decode_base64((unsigned char *)b64, strlen(b64), (unsigned char *)decoded);
    if (len == sizeof(_key)) {
      memcpy(_key, decoded, sizeof(_key));
      save();
      strcpy(reply, "OK - reboot to apply");
    } else {
      sprintf(reply, "Error: decoded %u bytes, expected 28", len);
    }
    return true;
  }
  if (memcmp(command, "set findmy ", 11) == 0) {
    _enabled = memcmp(&command[11], "on", 2) == 0;
    save();
    strcpy(reply, _enabled ? "OK - on, reboot to apply" : "OK - off, reboot to apply");
    return true;
  }
  if (memcmp(command, "get findmy", 10) == 0) {
    // derived static-random MAC is key[0]|0xC0 : key[1] : ... : key[5]
    sprintf(reply, "> %s, mac %02X:%02X:%02X:%02X:%02X:%02X",
            _enabled ? "on" : "off",
            _key[0] | 0xC0, _key[1], _key[2], _key[3], _key[4], _key[5]);
    return true;
  }
  return false;
}

#endif

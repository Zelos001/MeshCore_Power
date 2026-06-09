#pragma once

#include <stdint.h>

// Apple FindMy / OpenHaystack locator beacon for nRF52 (Adafruit Bluefruit).
//
// Advertises a static, non-connectable OpenHaystack payload derived from a 28-byte
// advertising public key. The matching private key is held off-device (in the user's
// OpenHaystack / macless-haystack setup) and is required to actually locate the device.
//
// The algorithm is ported from https://github.com/pix/heystack-nrf5x (nRF5 SDK) and
// reimplemented here on the Bluefruit advertising API used by MeshCore.
//
// Self-contained: it persists its own config to "/findmy" on the internal filesystem and
// parses its own "set/get findmy" CLI commands, so it needs no changes to the shared
// NodePrefs/CommonCLI code. Provisioning: `set findmy.key <base64>`, `set findmy on`, reboot.
//
// Intended for always-on roles (repeater/sensor) where BLE is otherwise unused. It is not
// meant to run alongside the phone-companion firmware, which needs BLE for its own link.
class FindMyBeacon {
  bool _started = false;
  uint8_t _enabled = 0;
  uint8_t _key[28] = {0}; // OpenHaystack advertising public key

  bool load();  // read "/findmy" into _enabled/_key
  void save();  // write "/findmy"

public:
  // Load persisted config and, if enabled with a key set, start advertising. Call once at
  // boot after the internal filesystem is mounted. tx_dbm is the advertising TX power.
  void begin(int8_t tx_dbm = 4);
  void stop();
  bool isRunning() const { return _started; }

  // Handle "set findmy.key <b64>", "set findmy on|off", "get findmy".
  // Returns true if the command was recognised (and reply filled), false otherwise.
  bool handleCommand(const char* command, char* reply);
};

// Single shared instance (defined in FindMyBeacon.cpp).
extern FindMyBeacon findmy_beacon;

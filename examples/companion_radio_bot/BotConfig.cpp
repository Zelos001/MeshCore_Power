#include "BotConfig.h"
#include "MyMesh.h"   // DataStore + FILESYSTEM definitions

#include <string.h>

// The companion's DataStore instance, defined at global scope in the stock
// main.cpp (which the bot's main.cpp shim includes). Used only for the bot's
// own tiny settings file - DataStore's blob API is advert-specific.
extern DataStore store;

BotConfig bot_config;

void BotConfig::setDefaults() {
  memset(&_s, 0, sizeof(_s));
  _s.magic = BOT_SETTINGS_MAGIC;
  _s.version = BOT_SETTINGS_VERSION;
  _s.enabled = 1;
  // channels stays "" -> DM-only until a channel list is configured
}

void BotConfig::ensureLoaded() {
  if (_loaded) return;
  _loaded = true;   // only try once; missing/invalid file -> keep defaults

  File f = store.openRead(BOT_SETTINGS_FILE);
  if (f) {
    BotSettings tmp;
    if (f.read((uint8_t*)&tmp, sizeof(tmp)) == sizeof(tmp) &&
        tmp.magic == BOT_SETTINGS_MAGIC && tmp.version == BOT_SETTINGS_VERSION) {
      tmp.channels[sizeof(tmp.channels) - 1] = 0;
      _s = tmp;
    }
    f.close();
  }
}

bool BotConfig::save() {
  FILESYSTEM* fs = store.getPrimaryFS();
  if (fs == NULL) return false;
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  fs->remove(BOT_SETTINGS_FILE);
  File f = fs->open(BOT_SETTINGS_FILE, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File f = fs->open(BOT_SETTINGS_FILE, "w");
#else
  File f = fs->open(BOT_SETTINGS_FILE, "w", true);
#endif
  if (!f) return false;
  bool ok = f.write((const uint8_t*)&_s, sizeof(_s)) == sizeof(_s);
  f.close();
  return ok;
}

bool BotConfig::setEnabled(const char* value) {
  if (value == NULL) return false;
  ensureLoaded();
  if (!strcmp(value, "1") || !strcasecmp(value, "on") || !strcasecmp(value, "true")) {
    _s.enabled = 1;
  } else if (!strcmp(value, "0") || !strcasecmp(value, "off") || !strcasecmp(value, "false")) {
    _s.enabled = 0;
  } else {
    return false;
  }
  return save();
}

bool BotConfig::setChannels(const char* value) {
  ensureLoaded();
  if (value == NULL || value[0] == 0 || !strcasecmp(value, "none")) {
    _s.channels[0] = 0;
  } else if (strlen(value) < sizeof(_s.channels)) {
    strcpy(_s.channels, value);
  } else {
    return false;  // list too long
  }
  return save();
}

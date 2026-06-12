#pragma once

#include <Arduino.h>

/* ----------------------------------------------------------------------------
 *  BotConfig
 *
 *  Runtime bot configuration, persisted to flash so it survives reboots.
 *  Set locally over the companion protocol's custom-vars frames - e.g. via
 *  meshcore-cli or the phone app's custom variables - NOT via mesh messages,
 *  so only the paired/local user can change it:
 *
 *    bot          = 1 | 0      enable/disable all bot replies
 *    bot.channels = <list>     comma-separated channel allow-list the bot
 *                              answers in (e.g. "#test,#bots"); "none" = the
 *                              bot only answers direct messages
 *
 *  Factory defaults: enabled, no channels (DM-only).
 * ------------------------------------------------------------------------- */

#define BOT_SETTINGS_MAGIC    0xB0
#define BOT_SETTINGS_VERSION  1
#define BOT_SETTINGS_FILE     "/bot_prefs"

// Persisted verbatim; fixed size so a rewrite always fully overwrites the
// previous record (nRF52 FILE_O_WRITE does not truncate).
struct BotSettings {
  uint8_t magic;        // BOT_SETTINGS_MAGIC when the record is valid
  uint8_t version;      // BOT_SETTINGS_VERSION
  uint8_t enabled;      // 0 = bot completely silent
  uint8_t reserved;
  char    channels[64]; // comma-separated channel allow-list ("" = none)
};

class BotConfig {
public:
  BotConfig() { setDefaults(); }

  bool isEnabled() { ensureLoaded(); return _s.enabled != 0; }
  const char* channels() { ensureLoaded(); return _s.channels; }

  // For the custom-vars GET frame: show "none" rather than an empty value.
  const char* channelsForDisplay() { ensureLoaded(); return _s.channels[0] ? _s.channels : "none"; }
  const char* enabledForDisplay() { ensureLoaded(); return _s.enabled ? "1" : "0"; }

  // Setters for the custom-vars SET frame; persist on success.
  // Return false on an unparseable/oversized value (-> ERR frame to the app).
  bool setEnabled(const char* value);
  bool setChannels(const char* value);

private:
  void setDefaults();
  void ensureLoaded();
  bool save();

  BotSettings _s;
  bool _loaded = false;
};

extern BotConfig bot_config;

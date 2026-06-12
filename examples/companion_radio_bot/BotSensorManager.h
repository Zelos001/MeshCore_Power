#pragma once

#include "BotConfig.h"

#include <string.h>

/* ----------------------------------------------------------------------------
 *  BotSensorManagerT
 *
 *  Wraps the board's sensor manager to add the bot's settings as standard
 *  companion-protocol "custom vars" (CMD_GET_CUSTOM_VARS / CMD_SET_CUSTOM_VAR
 *  route through the global `sensors` object's setting accessors), alongside
 *  the stock ones like "gps":
 *
 *    bot          = 1 | 0
 *    bot.channels = <comma-separated list> | none
 *
 *  Templated on the concrete manager class because boards differ (most use
 *  EnvironmentSensorManager, the T1000-E has its own T1000SensorManager).
 *  Instantiated in BotTarget.cpp, which swaps the global `sensors` object
 *  for this wrapper.
 * ------------------------------------------------------------------------- */
template <class BASE>
class BotSensorManagerT : public BASE {
public:
  BotSensorManagerT(const BASE& src) : BASE(src) {}

  int getNumSettings() const override { return BASE::getNumSettings() + 2; }

  const char* getSettingName(int i) const override {
    int n = BASE::getNumSettings();
    if (i == n) return "bot";
    if (i == n + 1) return "bot.channels";
    return BASE::getSettingName(i);
  }

  const char* getSettingValue(int i) const override {
    int n = BASE::getNumSettings();
    if (i == n) return bot_config.enabledForDisplay();
    if (i == n + 1) return bot_config.channelsForDisplay();
    return BASE::getSettingValue(i);
  }

  bool setSettingValue(const char* name, const char* value) override {
    if (strcmp(name, "bot") == 0) return bot_config.setEnabled(value);
    if (strcmp(name, "bot.channels") == 0) return bot_config.setChannels(value);
    return BASE::setSettingValue(name, value);
  }
};

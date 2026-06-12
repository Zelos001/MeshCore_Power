/* ----------------------------------------------------------------------------
 *  Compiles the stock variants/<board>/target.cpp with the global `sensors`
 *  object swapped for a BotSensorManagerT wrapper, which adds the bot's
 *  settings ("bot", "bot.channels") to the companion protocol's custom-vars
 *  frames - settable from meshcore-cli / the phone app, persisted to flash.
 *
 *  Each bot env excludes the stock target.cpp from the build and compiles
 *  this file instead. <target.cpp> resolves through the board's existing
 *  "-I variants/<board>" include path, so this file is board-agnostic.
 *
 *  The rename trick: while target.cpp is included, `sensors` is #define'd to
 *  `bot_stock_sensors`, so the stock manager - with all its board-specific
 *  type and constructor args - is built under that name (and target.h's
 *  extern declaration is renamed consistently within this translation unit).
 *  The real `sensors` symbol is then defined below as the wrapper, copy-
 *  constructed from the stock instance; decltype picks up whatever concrete
 *  manager class the board uses. Other translation units (MyMesh.cpp etc.)
 *  declare `extern EnvironmentSensorManager sensors` and dispatch through
 *  the object's vtable, picking up the bot's overrides - same single-
 *  inheritance aliasing trick the bot already uses for `the_mesh`.
 * ------------------------------------------------------------------------- */
#define sensors bot_stock_sensors
#include <target.cpp>
#undef sensors

#include "BotSensorManager.h"

BotSensorManagerT<decltype(bot_stock_sensors)> sensors(bot_stock_sensors);

// Out-of-line accessor used by BotMyMesh.cpp: returning the wrapper through
// a base-class reference hides its dynamic type from the compiler, so the
// settings calls in MyMesh.cpp stay virtual instead of being devirtualized
// straight to the stock manager (see BotMyMesh.cpp).
SensorManager& botSensorsRef() { return sensors; }

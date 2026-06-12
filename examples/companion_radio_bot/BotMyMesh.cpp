/* ----------------------------------------------------------------------------
 *  Compiles the stock MyMesh.cpp with all uses of the global `sensors`
 *  object routed through botSensorsRef().
 *
 *  Why: `sensors` is a global OBJECT, so the compiler can prove its exact
 *  dynamic type and devirtualize calls like sensors.getNumSettings() into
 *  direct calls to the stock manager's methods, bypassing the
 *  BotSensorManagerT overrides that add the bot's custom vars (confirmed in
 *  the disassembly: literal-pool call, no vtable load). An out-of-line
 *  accessor returning a base-class reference makes the dynamic type opaque
 *  again, so the custom-vars frames dispatch through the vtable and reach
 *  the wrapper.
 *
 *  The stock headers are pre-included before the macro is defined (they are
 *  all #pragma once), so the macro only rewrites MyMesh.cpp's own code -
 *  notably NOT target.h's extern declaration, nor other headers' function
 *  parameters that happen to be named `sensors` (e.g. UITask::begin).
 *  Everything MyMesh.cpp does with `sensors` is plain member access
 *  (methods + node_lat/node_lon fields), all present on SensorManager.
 *
 *  The bot env excludes the stock MyMesh.cpp and compiles this instead;
 *  <MyMesh.cpp> resolves through the env's -I examples/companion_radio.
 * ------------------------------------------------------------------------- */
#include "MyMesh.h"
#include <Arduino.h>
#include <Mesh.h>

SensorManager& botSensorsRef();   // defined in BotTarget.cpp

#define sensors (botSensorsRef())
#include <MyMesh.cpp>
#undef sensors

#pragma once
#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/radiolib/CustomLR1110Wrapper.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/LocationProvider.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include "ThinkNodeM4Board.h"
#ifdef DISPLAY_CLASS
  #include "NullDisplayDriver.h"
#endif

class ThinkNodeM4SensorManager : public SensorManager {
  LocationProvider* _nmea;
  bool gps_active = false;
  void start_gps();
  void stop_gps();
public:
  ThinkNodeM4SensorManager(LocationProvider& nmea) : _nmea(&nmea) {}
  bool begin() override;
  bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) override;
  void loop() override;
  int getNumSettings() const override;
  const char* getSettingName(int i) const override;
  const char* getSettingValue(int i) const override;
  bool setSettingValue(const char* name, const char* value) override;
};

extern ThinkNodeM4Board board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern ThinkNodeM4SensorManager sensors;
#ifdef DISPLAY_CLASS
  extern NullDisplayDriver display;
#endif
bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(int8_t dbm);
mesh::LocalIdentity radio_new_identity();
#include <Arduino.h>
#include "target.h"
#include <helpers/sensors/MicroNMEALocationProvider.h>

ThinkNodeM4Board board;
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);
WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock);
ThinkNodeM4SensorManager sensors = ThinkNodeM4SensorManager(nmea);

#ifdef DISPLAY_CLASS
  NullDisplayDriver display;
#endif

#ifndef LORA_CR
  #define LORA_CR 5
#endif

#ifdef RF_SWITCH_TABLE
static const uint32_t rfswitch_dios[Module::RFSWITCH_MAX_PINS] = {
  RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6,
  RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC
};
static const Module::RfSwitchMode_t rfswitch_table[] = {
  // mode                DIO5   DIO6
  { LR11x0::MODE_STBY,  {LOW,  LOW  }},
  { LR11x0::MODE_RX,    {HIGH, LOW  }},
  { LR11x0::MODE_TX,    {HIGH, HIGH }},
  { LR11x0::MODE_TX_HP, {LOW,  HIGH }},
  { LR11x0::MODE_TX_HF, {LOW,  LOW  }},
  { LR11x0::MODE_GNSS,  {LOW,  LOW  }},
  { LR11x0::MODE_WIFI,  {LOW,  LOW  }},
  END_OF_MODE_TABLE,
};
#endif

bool radio_init() {
#ifdef LR11X0_DIO3_TCXO_VOLTAGE
  float tcxo = LR11X0_DIO3_TCXO_VOLTAGE;
#else
  float tcxo = 1.6f;
#endif

  SPI.setPins(P_LORA_MISO, P_LORA_SCLK, P_LORA_MOSI);
  SPI.begin();
  delay(10);

  int status = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR,
                           RADIOLIB_LR11X0_LORA_SYNC_WORD_PRIVATE,
                           LORA_TX_POWER, 16, tcxo);

  // Retry once — some units need extra TCXO stabilisation time
  if (status == RADIOLIB_ERR_SPI_CMD_FAILED) {
    delay(100);
    status = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR,
                         RADIOLIB_LR11X0_LORA_SYNC_WORD_PRIVATE,
                         LORA_TX_POWER, 16, tcxo);
  }

  if (status != RADIOLIB_ERR_NONE) return false;

  radio.setCRC(2);
  radio.explicitHeader();

#ifdef RF_SWITCH_TABLE
  radio.setRfSwitchTable(rfswitch_dios, rfswitch_table);
#endif

#ifdef RX_BOOSTED_GAIN
  radio.setRxBoostedGainMode(RX_BOOSTED_GAIN);
#endif

  return true;
}

uint32_t radio_get_rng_seed() {
  return radio.random(0x7FFFFFFF);
}

void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr) {
  radio.setFrequency(freq);
  radio.setSpreadingFactor(sf);
  radio.setBandwidth(bw);
  radio.setCodingRate(cr);
}

void radio_set_tx_power(int8_t dbm) {
  radio.setOutputPower(dbm);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}

// ── GPS (L76K) ────────────────────────────────────────────────────────────────

void ThinkNodeM4SensorManager::start_gps() {
  gps_active = true;
  pinMode(PIN_GPS_EN, OUTPUT);
  digitalWrite(PIN_GPS_EN, LOW);       // GPS_EN_ACTIVE is LOW
  delay(10);
  pinMode(PIN_GPS_RESET, OUTPUT);
  digitalWrite(PIN_GPS_RESET, HIGH);   // not in reset
  delay(10);
  pinMode(PIN_GPS_STANDBY, OUTPUT);
  digitalWrite(PIN_GPS_STANDBY, LOW);  // not in standby
}

void ThinkNodeM4SensorManager::stop_gps() {
  gps_active = false;
  digitalWrite(PIN_GPS_EN, HIGH);
  digitalWrite(PIN_GPS_STANDBY, HIGH);
}

bool ThinkNodeM4SensorManager::begin() {
  Serial1.begin(GPS_BAUDRATE);
  return true;
}

bool ThinkNodeM4SensorManager::querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) {
  if (requester_permissions & TELEM_PERM_LOCATION) {
    telemetry.addGPS(TELEM_CHANNEL_SELF, node_lat, node_lon, node_altitude);
  }
  return true;
}

void ThinkNodeM4SensorManager::loop() {
  // Drive board loop so battery serial gets polled
  board.loop();

  static long next_gps_update = 0;
  _nmea->loop();
  if (millis() > next_gps_update) {
    if (gps_active && _nmea->isValid()) {
      node_lat      = ((double)_nmea->getLatitude())  / 1000000.0;
      node_lon      = ((double)_nmea->getLongitude()) / 1000000.0;
      node_altitude = ((double)_nmea->getAltitude())  / 1000.0;
    }
    next_gps_update = millis() + 1000;
  }
}

int ThinkNodeM4SensorManager::getNumSettings() const { return 1; }

const char* ThinkNodeM4SensorManager::getSettingName(int i) const {
  return i == 0 ? "gps" : NULL;
}

const char* ThinkNodeM4SensorManager::getSettingValue(int i) const {
  return i == 0 ? (gps_active ? "1" : "0") : NULL;
}

bool ThinkNodeM4SensorManager::setSettingValue(const char* name, const char* value) {
  if (strcmp(name, "gps") == 0) {
    if (strcmp(value, "0") == 0) stop_gps();
    else start_gps();
    return true;
  }
  return false;
}
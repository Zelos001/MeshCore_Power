#include "target.h"
#include <Arduino.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/sensors/MicroNMEALocationProvider.h>
#include <helpers/sensors/EnvironmentSensorManager.h>

WaveshareBoard board;

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_0, P_LORA_RST, P_LORA_DIO_1, SPI);
WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

MicroNMEALocationProvider nmea(Serial1, &rtc_clock);
EnvironmentSensorManager sensors(nmea);

bool radio_init() {
  pinMode(25, OUTPUT);
  
  // step 1
  digitalWrite(25,HIGH); delay(300); digitalWrite(25,LOW); delay(300);
  Serial1.setRX(PIN_GPS_RX);
  
  // step 2
  digitalWrite(25,HIGH); delay(300); digitalWrite(25,LOW); delay(300);
  Serial1.setTX(PIN_GPS_TX);

  // step 3
  digitalWrite(25,HIGH); delay(300); digitalWrite(25,LOW); delay(300);
  Serial1.begin(9600);

  // step 4
  digitalWrite(25,HIGH); delay(300); digitalWrite(25,LOW); delay(300);
  rtc_clock.begin(Wire);

  // step 5
  digitalWrite(25,HIGH); delay(300); digitalWrite(25,LOW); delay(300);
  SPI.begin();

  // step 6
  digitalWrite(25,HIGH); delay(300); digitalWrite(25,LOW); delay(300);
  bool result = radio.std_init(NULL);

  if (result) {
    for (int i = 0; i < 10; i++) {
      digitalWrite(25,HIGH); delay(100); digitalWrite(25,LOW); delay(100);
    }
  } else {
    for (int i = 0; i < 3; i++) {
      digitalWrite(25,HIGH); delay(500); digitalWrite(25,LOW); delay(500);
    }
  }
  return result;
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
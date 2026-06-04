#include "ThinkNodeM4Board.h"
#include <bluefruit.h>

// ── Power enable pins ─────────────────────────────────────────────────────────
#define M4_PIN_POWER_EN   (11)   // LR1110 radio power enable (active HIGH)
#define M4_VEXT_ENABLE    (32)   // Peripheral power enable (active LOW)

// ── Battery ADC (fallback) ────────────────────────────────────────────────────
#define M4_VBAT_PIN       (2)
#define M4_ADC_MAX        (4096.0f)
#define M4_DIVIDER_COMP   (2.0f)

// ── Battery serial interface (power bank management chip) ─────────────────────
#define M4_BATT_SERIAL_BAUD  (4800)
#define M4_BATT_START_BYTE   (0xFE)
#define M4_BATT_END_BYTE     (0xFD)

// ── Battery gauge LED pins ────────────────────────────────────────────────────
#define M4_BATT_LED_1     (15)
#define M4_BATT_LED_2     (17)
#define M4_BATT_LED_3     (34)   // P1.02
#define M4_BATT_LED_4     (36)   // P1.04

// Voltage thresholds for 2x18650 in series (millivolts)
#define M4_BATT_LED4_MV   (8000)
#define M4_BATT_LED3_MV   (7400)
#define M4_BATT_LED2_MV   (6900)
#define M4_BATT_LED1_MV   (6400)

#define M4_BATT_UPDATE_MS (30000)

void ThinkNodeM4Board::updateBatteryLEDs(uint16_t mv) {
  digitalWrite(M4_BATT_LED_1, mv >= M4_BATT_LED1_MV ? HIGH : LOW);
  digitalWrite(M4_BATT_LED_2, mv >= M4_BATT_LED2_MV ? HIGH : LOW);
  digitalWrite(M4_BATT_LED_3, mv >= M4_BATT_LED3_MV ? HIGH : LOW);
  digitalWrite(M4_BATT_LED_4, mv >= M4_BATT_LED4_MV ? HIGH : LOW);
}

void ThinkNodeM4Board::readBatterySerial() {
  if (_batt_serial.available() < 6) return;

  // Flush stale data, keep only the latest packet
  while (_batt_serial.available() > 11) {
    _batt_serial.read();
  }

  // Find start byte
  int tries = 0;
  while (_batt_serial.read() != M4_BATT_START_BYTE) {
    if (++tries > 10) return;
  }

  uint8_t data[6] = {0};
  data[1] = _batt_serial.read();
  data[2] = _batt_serial.read();
  data[3] = _batt_serial.read();
  data[4] = _batt_serial.read();
  data[5] = _batt_serial.read();

  if (data[5] != M4_BATT_END_BYTE) return;

  _batt_percent = data[1];
  // Voltage: integer + decimal parts; multiply by 2 for 2x18650 series pack
  float voltage = data[2] + ((float)data[3] / 100.0f) + ((float)data[4] / 10000.0f);
  voltage *= 2.0f;
  _batt_mv = (uint16_t)(voltage * 1000.0f);
}

void ThinkNodeM4Board::begin() {
  // Enable LR1110 radio and peripheral power rails before anything else
  pinMode(M4_PIN_POWER_EN, OUTPUT);
  digitalWrite(M4_PIN_POWER_EN, HIGH);
  pinMode(M4_VEXT_ENABLE, OUTPUT);
  digitalWrite(M4_VEXT_ENABLE, LOW);   // active LOW
  delay(100);

  NRF52BoardDCDC::begin();

  // Start battery management serial
  _batt_serial.begin(M4_BATT_SERIAL_BAUD);

  // Try to get an initial battery reading
  for (int i = 0; i < 10; i++) {
    delay(200);
    readBatterySerial();
    if (_batt_mv > 0) break;
  }

  // Battery gauge LEDs
  pinMode(M4_BATT_LED_1, OUTPUT);
  pinMode(M4_BATT_LED_2, OUTPUT);
  pinMode(M4_BATT_LED_3, OUTPUT);
  pinMode(M4_BATT_LED_4, OUTPUT);

#ifdef LED_HEARTBEAT
  pinMode(LED_HEARTBEAT, OUTPUT);
  digitalWrite(LED_HEARTBEAT, LOW);
#endif
#ifdef LED_PAIRING
  pinMode(LED_PAIRING, OUTPUT);
  digitalWrite(LED_PAIRING, LOW);
#endif

  updateBatteryLEDs(_batt_mv);
}

uint16_t ThinkNodeM4Board::getBattMilliVolts() {
  if (_batt_mv > 0) return _batt_mv;

  // ADC fallback — reads battery voltage via resistor divider on PIN_A0
  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  delay(5);
  int raw = analogRead(M4_VBAT_PIN);
  analogReference(AR_DEFAULT);
  analogReadResolution(10);
  float mv = ((float)raw / M4_ADC_MAX) * 3000.0f * M4_DIVIDER_COMP;
  return (uint16_t)mv;
}

void ThinkNodeM4Board::loop() {
  readBatterySerial();

  uint32_t now = millis();
  if (now - _last_batt_update >= M4_BATT_UPDATE_MS) {
    _last_batt_update = now;
    updateBatteryLEDs(_batt_mv > 0 ? _batt_mv : getBattMilliVolts());
  }
}

void ThinkNodeM4Board::powerOff() {
  digitalWrite(M4_BATT_LED_1, LOW);
  digitalWrite(M4_BATT_LED_2, LOW);
  digitalWrite(M4_BATT_LED_3, LOW);
  digitalWrite(M4_BATT_LED_4, LOW);

#ifdef LED_HEARTBEAT
  digitalWrite(LED_HEARTBEAT, LOW);
#endif
#ifdef LED_PAIRING
  digitalWrite(LED_PAIRING, LOW);
#endif

#ifdef PIN_GPS_EN
  pinMode(PIN_GPS_EN, OUTPUT);
  digitalWrite(PIN_GPS_EN, HIGH);
#endif

  digitalWrite(M4_PIN_POWER_EN, LOW);
  digitalWrite(M4_VEXT_ENABLE, HIGH);

#ifdef PIN_BUTTON1
  nrf_gpio_cfg_sense_input(PIN_BUTTON1, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
#endif

  sd_power_system_off();
}

int ThinkNodeM4Board::buttonStateChanged() {
#ifdef PIN_BUTTON1
  static uint8_t prev = HIGH;
  uint8_t v = digitalRead(PIN_BUTTON1);
  if (v != prev) {
    prev = v;
    return (v == LOW) ? 1 : -1;
  }
#endif
  return 0;
}
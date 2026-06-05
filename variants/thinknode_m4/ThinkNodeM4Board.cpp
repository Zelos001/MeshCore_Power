#include "ThinkNodeM4Board.h"
#include <bluefruit.h>
#include <Uart.h>

// ── Power enable pins ─────────────────────────────────────────────────────────
#define M4_PIN_POWER_EN   (11)
#define M4_VEXT_ENABLE    (32)

// ── Battery ADC (fallback) ────────────────────────────────────────────────────
#define M4_VBAT_PIN       (2)
#define M4_ADC_MAX        (4096.0f)
#define M4_DIVIDER_COMP   (2.0f)

// ── Battery serial interface ──────────────────────────────────────────────────
#define M4_BATT_SERIAL_BAUD  (4800)
#define M4_BATT_START_BYTE   (0xFE)
#define M4_BATT_END_BYTE     (0xFD)

// ── Battery gauge LED pins ────────────────────────────────────────────────────
#define M4_BATT_LED_1     (15)
#define M4_BATT_LED_2     (17)
#define M4_BATT_LED_3     (34)
#define M4_BATT_LED_4     (36)

#define M4_BATT_UPDATE_MS  (30000)
#define M4_LED_TIMEOUT_MS  (10000)

void ThinkNodeM4Board::updateBatteryLEDs(uint16_t mv) {
  uint8_t pct = (_batt_percent > 0) ? _batt_percent : (uint8_t)((float)mv / 4200.0f * 100.0f);
  digitalWrite(M4_BATT_LED_1, pct >= 1  ? HIGH : LOW);
  digitalWrite(M4_BATT_LED_2, pct >= 25 ? HIGH : LOW);
  digitalWrite(M4_BATT_LED_3, pct >= 50 ? HIGH : LOW);
  digitalWrite(M4_BATT_LED_4, pct >= 75 ? HIGH : LOW);
}

void ThinkNodeM4Board::triggerBatteryLEDs() {
  _button_pressed_ms = millis();
  updateBatteryLEDs(_batt_mv > 0 ? _batt_mv : getBattMilliVolts());
}

void ThinkNodeM4Board::readBatterySerial() {
  if (Serial2.available() < 6) return;
  while (Serial2.available() > 11) Serial2.read();
  int tries = 0;
  while (true) {
    if (!Serial2.available()) return;
    if (Serial2.read() == M4_BATT_START_BYTE) break;
    if (++tries > 20) return;
  }
  if (Serial2.available() < 5) return;
  uint8_t data[6] = {0};
  for (int i = 1; i <= 5; i++) data[i] = Serial2.read();
  if (data[5] != M4_BATT_END_BYTE) return;
  _batt_percent = data[1];
  float voltage = data[2] + ((float)data[3] / 100.0f) + ((float)data[4] / 10000.0f);
  _batt_mv = (uint16_t)(voltage * 2000.0f);
}

void ThinkNodeM4Board::begin() {
  pinMode(M4_PIN_POWER_EN, OUTPUT);
  digitalWrite(M4_PIN_POWER_EN, HIGH);
  pinMode(M4_VEXT_ENABLE, OUTPUT);
  digitalWrite(M4_VEXT_ENABLE, LOW);
  delay(100);

  NRF52BoardDCDC::begin();

  Serial2.setPins(30, 5);
  Serial2.begin(M4_BATT_SERIAL_BAUD);

  // Try to get initial battery reading
  uint32_t start = millis();
  while (_batt_mv == 0 && millis() - start < 5000) {
    delay(50);
    readBatterySerial();
  }

  pinMode(M4_BATT_LED_1, OUTPUT); digitalWrite(M4_BATT_LED_1, LOW);
  pinMode(M4_BATT_LED_2, OUTPUT); digitalWrite(M4_BATT_LED_2, LOW);
  pinMode(M4_BATT_LED_3, OUTPUT); digitalWrite(M4_BATT_LED_3, LOW);
  pinMode(M4_BATT_LED_4, OUTPUT); digitalWrite(M4_BATT_LED_4, LOW);

#ifdef PIN_BUTTON1
  pinMode(PIN_BUTTON1, INPUT_PULLUP);
#endif
#ifdef LED_HEARTBEAT
  pinMode(LED_HEARTBEAT, OUTPUT);
  digitalWrite(LED_HEARTBEAT, LOW);
#endif
#ifdef LED_PAIRING
  pinMode(LED_PAIRING, OUTPUT);
  digitalWrite(LED_PAIRING, LOW);
#endif
}

uint16_t ThinkNodeM4Board::getBattMilliVolts() {
  if (_batt_mv > 0) return _batt_mv;
  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  delay(5);
  int raw = analogRead(M4_VBAT_PIN);
  analogReference(AR_DEFAULT);
  analogReadResolution(10);
  return (uint16_t)(((float)raw / M4_ADC_MAX) * 3000.0f * M4_DIVIDER_COMP);
}

void ThinkNodeM4Board::loop() {
  readBatterySerial();
  uint32_t now = millis();
  // Turn off LEDs after timeout
  if (_button_pressed_ms > 0 && now - _button_pressed_ms >= M4_LED_TIMEOUT_MS) {
    digitalWrite(M4_BATT_LED_1, LOW);
    digitalWrite(M4_BATT_LED_2, LOW);
    digitalWrite(M4_BATT_LED_3, LOW);
    digitalWrite(M4_BATT_LED_4, LOW);
    _button_pressed_ms = 0;
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
  delay(1000);
  nrf_gpio_cfg_sense_input(PIN_BUTTON1, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
#endif
  sd_power_system_off();
}

int ThinkNodeM4Board::buttonStateChanged() {
  return 0;
}
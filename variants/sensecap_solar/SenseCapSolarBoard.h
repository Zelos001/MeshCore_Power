#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

class SenseCapSolarBoard : public NRF52BoardDCDC {
protected:
#ifdef NRF52_POWER_MANAGEMENT
  void initiateShutdown(uint8_t reason) override;
#endif

private:
#ifdef PIN_USER_BTN
  unsigned long _btn_down_at = 0;  // user-button press timestamp (0 = not pressed)
  static constexpr unsigned long USER_BTN_HOLD_OFF_MILLIS = 1500;
#endif

public:
  SenseCapSolarBoard() : NRF52Board("SENSECAP_SOLAR_OTA") {}
  void begin();

#ifdef PIN_USER_BTN
  // Hold the user button for >= 1.5s to power off.
  void pollButton() override {
    int btnState = digitalRead(PIN_USER_BTN);
    if (btnState == LOW) {
      if (_btn_down_at == 0) {
        _btn_down_at = millis();
      } else if ((unsigned long)(millis() - _btn_down_at) >= USER_BTN_HOLD_OFF_MILLIS) {
        Serial.println("Powering off...");
        powerOff();  // does not return
      }
    } else {
      _btn_down_at = 0;
    }
  }
#endif

#if defined(P_LORA_TX_LED)
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
  }
  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED off
  }
#endif

  uint16_t getBattMilliVolts() override {
    digitalWrite(VBAT_ENABLE, LOW);
    int adcvalue = 0;
    analogReadResolution(12);
    analogReference(AR_INTERNAL_3_0);
    delay(10);
    adcvalue = analogRead(BATTERY_PIN);
    return (adcvalue * ADC_MULTIPLIER * AREF_VOLTAGE) / 4.096;
  }

  const char* getManufacturerName() const override {
    return "Seeed SenseCap Solar";
  }

  void powerOff() override {
    digitalWrite(LED_WHITE, LOW);
    digitalWrite(LED_BLUE, LOW);

#ifdef PIN_USER_BTN
    while (digitalRead(PIN_USER_BTN) == LOW);
    // Keep pull-up enabled in system-off so the wake line doesn't float low.
    nrf_gpio_cfg_sense_input(digitalPinToInterrupt(g_ADigitalPinMap[PIN_USER_BTN]), NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
#elif defined(PIN_BUTTON1)
    while (digitalRead(PIN_BUTTON1) == LOW);
    // Keep pull-up enabled in system-off so the wake line doesn't float low.
    nrf_gpio_cfg_sense_input(digitalPinToInterrupt(g_ADigitalPinMap[PIN_BUTTON1]), NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
#endif

#ifdef NRF52_POWER_MANAGEMENT
    initiateShutdown(SHUTDOWN_REASON_USER);
#else
    sd_power_system_off();
#endif
  }
};

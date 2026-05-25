#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>

#include <driver/rtc_io.h>

class ESP32C3SuperMiniDXLR20Board : public ESP32Board {
public:
  void begin() {
    ESP32Board::begin();

    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_DEEPSLEEP) {
      long wakeup_source = esp_sleep_get_gpio_wakeup_status();
      if (wakeup_source & (1 << P_LORA_DIO_1)) {
        startup_reason = BD_STARTUP_RX_PACKET;
      }
    }
  }

  void enterDeepSleep(uint32_t secs, int8_t wake_pin = -1) {
    gpio_set_direction((gpio_num_t)P_LORA_DIO_1, GPIO_MODE_INPUT);
    if (wake_pin >= 0) {
      gpio_set_direction((gpio_num_t)wake_pin, GPIO_MODE_INPUT);
    }

    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    uint64_t wake_mask = 1ULL << P_LORA_DIO_1;
    if (wake_pin >= 0) {
      wake_mask |= 1ULL << wake_pin;
    }
    esp_deep_sleep_enable_gpio_wakeup(wake_mask, ESP_GPIO_WAKEUP_GPIO_HIGH);

    if (secs > 0) {
      esp_sleep_enable_timer_wakeup(secs * 1000000ULL);
    }

    esp_deep_sleep_start();
  }

  void sleep(uint32_t secs) override {
    if (!inhibit_sleep) {
      enterDeepSleep(secs);
    }
  }

  const char *getManufacturerName() const override {
    return "ESP32-C3 SuperMini DX-LR20";
  }
};

#pragma once

#include <Arduino.h>
#include <MeshCore.h>
#include <helpers/NRF52Board.h>
#include <SoftwareSerial.h>

class ThinkNodeM4Board : public NRF52BoardDCDC {
  uint32_t _last_batt_update = 0;
  void updateBatteryLEDs(uint16_t mv);

  // Serial battery reader
  SoftwareSerial _batt_serial;
  uint8_t _batt_percent = 0;
  uint16_t _batt_mv = 0;
  void readBatterySerial();

public:
  ThinkNodeM4Board() : NRF52Board("THINKNODE_M4_OTA"), _batt_serial(5, 30) {}

  void begin() override;
  uint16_t getBattMilliVolts() override;
  void loop();
  void powerOff() override;
  int buttonStateChanged();

  const char* getManufacturerName() const override {
    return "Elecrow ThinkNode M4";
  }
};

extern ThinkNodeM4Board board;
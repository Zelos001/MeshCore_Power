#pragma once

#include "CustomLR1121.h"
#include "RadioLibWrappers.h"
#include "LR11x0Reset.h"

class CustomLR1121Wrapper : public RadioLibWrapper {
public:
  CustomLR1121Wrapper(CustomLR1121& radio, mesh::MainBoard& board)
    : RadioLibWrapper(radio, board) { }

  void setParams(float freq, float bw, uint8_t sf, uint8_t cr) override {
      auto* r = (CustomLR1121*)_radio;
      r->standby();
      r->setFrequency(freq);
      uint8_t ldro = (sf >= 11 || bw <= 125) ? 1 : 0;
      r->setModulationParamsLoRa(sf, bw, cr, ldro);
      updatePreamble(sf);
  }

  uint8_t getSpreadingFactor() const override {
    return ((CustomLR1121*)_radio)->getSpreadingFactor();
  }

  bool isReceivingPacket() override {
    return ((CustomLR1121*)_radio)->isReceiving();
  }

  float getCurrentRSSI() override {
    float rssi = -110;
    ((CustomLR1121*)_radio)->getRssiInst(&rssi);
    return rssi;
  }

  float getLastRSSI() const override {
    return ((CustomLR1121*)_radio)->getRSSI();
  }

  float getLastSNR() const override {
    return ((CustomLR1121*)_radio)->getSNR();
  }

  void onSendFinished() override {
    RadioLibWrapper::onSendFinished();
    _radio->setPreambleLength(
        preambleLengthForSF(getSpreadingFactor()));
  }

  void doResetAGC() override {
    auto* r = (CustomLR1121*)_radio;
    float freqHz = r->getFreqMHz() * 1e6;
    r->standby();
    r->calibrate(RADIOLIB_LR11X0_CALIBRATE_ALL);
    r->setFs();
    lr11x0ResetAGC((LR11x0*)_radio, freqHz);
  }

  void setRxBoostedGainMode(bool en) override {
    ((CustomLR1121*)_radio)->setRxBoostedGainMode(en);
  }

  bool getRxBoostedGainMode() const override {
    return ((CustomLR1121*)_radio)->getRxBoostedGainMode();
  }
};

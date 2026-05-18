#pragma once

#include <RadioLib.h>
#include "MeshCore.h"

class CustomLR1121 : public LR1121 {
  bool _rx_boosted = false;
  public:
    CustomLR1121(Module *mod) : LR1121(mod) { }

    float getFreqMHz() const { return freqMHz; }

    int16_t setRxBoostedGainMode(bool en) {
      _rx_boosted = en;
      return LR1121::setRxBoostedGainMode(en);
    }

    bool getRxBoostedGainMode() const { return _rx_boosted; }

    bool isReceiving() {
      uint16_t irq = getIrqStatus();
      bool detected = ((irq & RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID) || (irq & RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED));
      return detected;
    }
};
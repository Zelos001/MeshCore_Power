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
      return irq & (
          RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID |
          RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED |
          RADIOLIB_LR11X0_IRQ_CRC_ERR
      );
    }

    uint8_t getSpreadingFactor() const {
        return spreadingFactor;
    }

    size_t getPacketLength(bool update) override {
        size_t len = LR1121::getPacketLength(update);
        uint16_t irq = getIrqStatus();
        if (len == 0 && irq == 0) {
            return 0;
        }
        if (len == 0 && (irq & RADIOLIB_LR11X0_IRQ_HEADER_ERR)) {
            MESH_DEBUG_PRINTLN("LR1121: header err, calling standby()");
            standby();
            clearIrqState(RADIOLIB_LR11X0_IRQ_HEADER_ERR);
        }
        return len;
    }
};

#pragma once
// Elecrow ThinkNode M4 — nRF52840 + LR1110
// Pin definitions based on Meshtastic variant.h

// ── Pin counts ────────────────────────────────
#define PINS_COUNT           (48)
#define NUM_DIGITAL_PINS     (48)
#define NUM_ANALOG_INPUTS    (1)
#define NUM_ANALOG_OUTPUTS   (0)

// ── SPI (shared with LR1110) ──────────────────
#define PIN_SPI_MISO   (8)
#define PIN_SPI_MOSI   (7)
#define PIN_SPI_SCK    (6)
#define PIN_SPI_SS     (27)

// ── GPS UART ──────────────────────────────────
#define PIN_SERIAL1_RX (32 + 14)
#define PIN_SERIAL1_TX (32 + 12)

// Battery serial UART
#define PIN_SERIAL2_RX (30)
#define PIN_SERIAL2_TX (5)

// ── Status LEDs ───────────────────────────────
#define LED_BUILTIN    (41)
#define LED_CONN       LED_BUILTIN
#define LED_BLUE       (-1)  // Disable Bluefruit auto LED control

// ── Button ───────────────────────────────────
#define PIN_BUTTON1    (4)

// ── nRF52840 USB ─────────────────────────────
#define PIN_USBD_DP    25
#define PIN_USBD_DM    26

// ── I2C ──────────────────────────────────────
#define PIN_WIRE_SDA   (23)
#define PIN_WIRE_SCL   (25)

// ── ADC ──────────────────────────────────────
#define PIN_A0         (2)

// ── Wire / SPI interface counts ──────────────
#define HAS_WIRE              (1)
#define WIRE_INTERFACES_COUNT (1)
#define SPI_INTERFACES_COUNT  (1)

// ── Clock source (external 32kHz crystal) ────
#define USE_LFXO

#ifdef __cplusplus
extern "C" {
#endif
void initVariant();
#ifdef __cplusplus
}
#endif
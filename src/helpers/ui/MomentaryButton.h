#pragma once

#include <Arduino.h>

#define MULTI_CLICK_WINDOW_MS  280   // delay before a single click acts, used to detect double/triple-click

#define BUTTON_EVENT_NONE        0
#define BUTTON_EVENT_CLICK       1
#define BUTTON_EVENT_LONG_PRESS  2
#define BUTTON_EVENT_DOUBLE_CLICK 3
#define BUTTON_EVENT_TRIPLE_CLICK 4

class MomentaryButton {
  int8_t _pin;
  int8_t prev, cancel;
  bool _reverse, _pull;
  int _long_millis;
  int _threshold;  // analog mode
  unsigned long down_at;
  uint8_t _click_count;
  unsigned long _last_click_time;
  int _multi_click_window;
  bool _pending_click;

  // Interrupt-latch: presses caught by the edge ISR even when the main loop stalls (radio/
  // BLE/flash) and the poll misses them. check() reconciles these with live polling.
  // Debounce: the ISR counts at most one press, and only re-arms after check() has seen the
  // button cleanly released for the debounce period — so press/release bounce can't double-count.
  volatile uint8_t _irq_events;
  volatile bool _irq_armed;
  unsigned long _irq_last_ms;
  bool _irq_held;
  unsigned long _irq_release_ms;
  static MomentaryButton* _irq_self;
  static void _onIrq();

  bool isPressed(int level) const;

public:
  MomentaryButton(int8_t pin, int long_press_mills=0, bool reverse=false, bool pulldownup=false, bool multiclick=true);
  MomentaryButton(int8_t pin, int long_press_mills, int analog_threshold, int multi_click_window = MULTI_CLICK_WINDOW_MS);
  void begin();
  void enableInterrupt();  // attach a permanent edge IRQ so presses are never lost to a stalled loop
  int check(bool repeat_click=false);  // returns one of BUTTON_EVENT_*
  void cancelClick();  // suppress next BUTTON_EVENT_CLICK (if already in DOWN state)
  void reset();        // abandon the in-flight gesture: resync to current level, drop any pending click
  uint8_t getPin() { return _pin; }
  bool isPressed() const;
};

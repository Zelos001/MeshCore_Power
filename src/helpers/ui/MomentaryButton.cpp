#include "MomentaryButton.h"

#define IRQ_DEBOUNCE_MS  30   // ignore edges closer than this (contact bounce)

MomentaryButton* MomentaryButton::_irq_self = nullptr;

void MomentaryButton::_onIrq() {
  MomentaryButton* self = _irq_self;
  if (!self) return;
  // Count at most one press until check() re-arms us after a clean release. This rejects the
  // press- and release-bounce falling edges that would otherwise latch a phantom second press.
  if (!self->_irq_armed) return;
  unsigned long now = millis();
  if (now - self->_irq_last_ms < IRQ_DEBOUNCE_MS) return;   // also coalesce very fast chatter
  self->_irq_last_ms = now;
  self->_irq_armed = false;
  if (self->_irq_events < 200) self->_irq_events++;   // latch the press for check() to consume
}

void MomentaryButton::enableInterrupt() {
  if (_pin < 0) return;
  // Edge interrupts need the digital input buffer; on an analog-read (SAADC threshold) pin the
  // GPIOTE channel and analogRead() conflict and the pin reads stuck-low. Digital buttons only.
  if (_threshold > 0) return;
  _irq_self = this;   // single button per board; last caller wins
  pinMode(_pin, INPUT_PULLUP);   // active-low button
  attachInterrupt(digitalPinToInterrupt(_pin), _onIrq, FALLING);
}

MomentaryButton::MomentaryButton(int8_t pin, int long_press_millis, bool reverse, bool pulldownup, bool multiclick) {
  _pin = pin;
  _reverse = reverse;
  _pull = pulldownup;
  down_at = 0; 
  prev = _reverse ? HIGH : LOW;
  cancel = 0;
  _long_millis = long_press_millis;
  _threshold = 0;
  _click_count = 0;
  _last_click_time = 0;
  _multi_click_window = multiclick ? MULTI_CLICK_WINDOW_MS : 0;
  _pending_click = false;
  _irq_events = 0;
  _irq_last_ms = 0;
  _irq_armed = true;     // first press counts; ISR disarms until a clean release re-arms
  _irq_held = false;
  _irq_release_ms = 0;
}

MomentaryButton::MomentaryButton(int8_t pin, int long_press_millis, int analog_threshold, int multi_click_window) {
  _pin = pin;
  _reverse = false;
  _pull = false;
  down_at = 0;
  prev = LOW;
  cancel = 0;
  _long_millis = long_press_millis;
  _threshold = analog_threshold;
  _click_count = 0;
  _last_click_time = 0;
  _multi_click_window = multi_click_window;  // 0 = single click acts immediately (no double/triple-click)
  _pending_click = false;
  _irq_events = 0;
  _irq_last_ms = 0;
  _irq_armed = true;     // first press counts; ISR disarms until a clean release re-arms
  _irq_held = false;
  _irq_release_ms = 0;
}

void MomentaryButton::begin() {
  if (_pin >= 0 && _threshold == 0) {
    pinMode(_pin, _pull ? (_reverse ? INPUT_PULLUP : INPUT_PULLDOWN) : INPUT);
  }
}

bool  MomentaryButton::isPressed() const {
  int btn = _threshold > 0 ? (analogRead(_pin) < _threshold) : digitalRead(_pin);
  return isPressed(btn);
}

void MomentaryButton::cancelClick() {
  cancel = 1;
  down_at = 0;
  _click_count = 0;
  _last_click_time = 0;
  _pending_click = false;
}

void MomentaryButton::reset() {
  // Resync 'prev' to the current physical level and drop all gesture state. With the
  // button still held, prev becomes the pressed level and down_at stays 0, so the eventual
  // release records no click (check() guards the click on down_at > 0) — the gesture is
  // fully abandoned, including the click that would otherwise fire a multi-click window
  // after release. Used to swallow the press that only wakes the display.
  prev = _threshold > 0 ? (analogRead(_pin) < _threshold) : digitalRead(_pin);
  down_at = 0;
  cancel = 0;
  _click_count = 0;
  _last_click_time = 0;
  _pending_click = false;
  _irq_events = 0;   // drop latched presses too: the wake-press shouldn't queue navigation
  _irq_armed = true;
  _irq_held = false;
}

bool MomentaryButton::isPressed(int level) const {
  if (_threshold > 0) {
    return level;
  }
  if (_reverse) {
    return level == LOW;
  } else {
    return level != LOW;
  }
}

int MomentaryButton::check(bool repeat_click) {
  if (_pin < 0) return BUTTON_EVENT_NONE;

  int event = BUTTON_EVENT_NONE;
  int btn = _threshold > 0 ? (analogRead(_pin) < _threshold) : digitalRead(_pin);

  // Re-arm the IRQ latch only once the button has been cleanly released for the debounce
  // period. This is the debounce: the ISR can't count a new press (bounce or real) until here.
  if (isPressed(btn)) {
    _irq_held = true;
  } else {
    if (_irq_held) { _irq_held = false; _irq_release_ms = millis(); }
    if (!_irq_armed && (millis() - _irq_release_ms) >= IRQ_DEBOUNCE_MS) _irq_armed = true;
  }

  if (btn != prev) {
    if (isPressed(btn)) {
      down_at = millis();
      if (_irq_events > 0) _irq_events--;   // this live press was already latched by the ISR; claim it
    } else {
      // button UP
      if (_long_millis > 0) {
        if (down_at > 0 && (unsigned long)(millis() - down_at) < _long_millis) {  // only a CLICK if still within the long_press millis
            _click_count++;
            _last_click_time = millis();
            _pending_click = true;
        }
      } else {
          _click_count++;
          _last_click_time = millis();
          _pending_click = true;
      }
      if (event == BUTTON_EVENT_CLICK && cancel) {
        event = BUTTON_EVENT_NONE;
        _click_count = 0;
        _last_click_time = 0;
        _pending_click = false;
      }
      down_at = 0;
    }
    prev = btn;
  }
  if (!isPressed(btn) && cancel) {   // always clear the pending 'cancel' once button is back in UP state
    cancel = 0;
  }

  if (_long_millis > 0 && down_at > 0 && (unsigned long)(millis() - down_at) >= _long_millis) {
    if (_pending_click) {
      // long press during multi-click detection - cancel pending clicks
      cancelClick();
    } else {
      event = BUTTON_EVENT_LONG_PRESS;
      down_at = 0;
      _click_count = 0;
      _last_click_time = 0;
      _pending_click = false;
    }
  }
  if (down_at > 0 && repeat_click) {
    unsigned long diff = (unsigned long)(millis() - down_at);
    if (diff >= 700) {
      event = BUTTON_EVENT_CLICK;   // wait 700 millis before repeating the click events
    }
  }

  if (_pending_click && (millis() - _last_click_time) >= _multi_click_window) {
    if (down_at > 0) {
      // still pressed - wait for button release before processing clicks
      return event;
    }
    switch (_click_count) {
      case 1:
        event = BUTTON_EVENT_CLICK;
        break;
      case 2:
        event = BUTTON_EVENT_DOUBLE_CLICK;
        break;
      case 3:
        event = BUTTON_EVENT_TRIPLE_CLICK;
        break;
      default:
        // For 4+ clicks, treat as triple click?
        event = BUTTON_EVENT_TRIPLE_CLICK;
        break;
    }
    _click_count = 0;
    _last_click_time = 0;
    _pending_click = false;
  }

  // Flush a press the live poll never saw (loop stalled through its entire down→up window):
  // the ISR latched it but no edge was detected here. Only when idle (released, nothing
  // pending) so we never interfere with an in-progress long-press or multi-click.
  if (event == BUTTON_EVENT_NONE && _irq_events > 0 && !isPressed(btn) && down_at == 0 && !_pending_click) {
    _irq_events--;
    event = BUTTON_EVENT_CLICK;
  }

  return event;
}
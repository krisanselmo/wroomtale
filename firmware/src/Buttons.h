#pragma once
#include <Arduino.h>

// *Repeat is the same gesture going on: emitted while a long press is still
// held, so the volume ramps rather than stepping once per press. PLAY has no
// repeat -- holding it stops playback, and stopping twice means nothing.
enum class BtnEvent : uint8_t { None, PrevShort, PrevLong, PrevRepeat,
                                PlayShort, PlayLong, PlayDouble,
                                NextShort, NextLong, NextRepeat };

namespace Buttons {
void begin();

// One event per call.
BtnEvent poll();

bool isHeld(gpio_num_t pin);

// Bit 0/1/2 = PREV/PLAY/NEXT.
uint8_t downMask();
// Sticky, so the dashboard catches a press between two polls.
uint8_t seenMask();
void clearSeen();
} // namespace Buttons

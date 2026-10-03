#pragma once
#include <Arduino.h>

// Deep sleep when nobody uses the box or the cell is flat; PLAY wakes it, and
// waking is a cold boot. Play mode only: the portal keeps its own timeout.
namespace Power {
// Undoes what sleep left behind (held pins), and logs why the chip woke.
// Before Buttons::begin(): PLAY is still an RTC pin after an ext0 wake.
void begin();

// A flat cell at boot sleeps again at once, before audio and the radio start.
void sleepIfFlat();

// A button, a tag, a console byte. Playback counts on its own in tick().
void noteActivity();

void tick();

// Minutes of idleness before sleeping, 0 = never. Kept in NVS.
uint16_t idleMinutes();
void setIdleMinutes(uint16_t minutes);

[[noreturn]] void sleepNow(const char *reason);
} // namespace Power

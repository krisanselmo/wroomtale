#pragma once
#include <Arduino.h>
#include "Features.h"

// Deep sleep when nobody uses the box or the cell is flat; PLAY wakes it, and
// waking is a cold boot. The idle delay is play mode's: the portal has its own.
namespace Power {
#if FEAT_SLEEP
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

// Never returns.
void sleepNow(const char *reason);
#else
inline void begin() {}
inline void sleepIfFlat() {}
inline void noteActivity() {}
inline void tick() {}
inline uint16_t idleMinutes() { return 0; }
inline void setIdleMinutes(uint16_t) {}
inline void sleepNow(const char *) { log_w("deep sleep is cut from this build"); }
#endif
} // namespace Power

#pragma once
#include <Arduino.h>
#include "Features.h"

namespace Rfid {
#if FEAT_RFID
bool begin();

// Lowercase hex, and only on a change: holding a card does not retrigger.
String poll();

String current();

bool ready();

// The PN532 sits at 0x24.
void scanI2c();

void stats(uint32_t &polls, uint32_t &hits, uint32_t &changes);
void setVerbose(bool on);
String firmwareVersion();
#else
inline bool begin() { return false; }
inline String poll() { return String(); }
inline String current() { return String(); }
inline bool ready() { return false; }
inline String firmwareVersion() { return String(); }
inline void scanI2c() {}
inline void stats(uint32_t &p, uint32_t &h, uint32_t &c) { p = h = c = 0; }
inline void setVerbose(bool) {}
#endif
} // namespace Rfid

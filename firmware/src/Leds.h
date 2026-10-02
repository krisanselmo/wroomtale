#pragma once
#include <Arduino.h>
#include "Features.h"

// A tone is swallowed while a track plays; a flash never is.
enum class LedEvent : uint8_t { Boot, TagOk, TagUnknown, Volume };

namespace Leds {
#if FEAT_LEDS
bool begin();

// Never blocks.
void tick();

// Transient overlay over the ambient state.
void event(LedEvent e);

void setConfigMode(bool on);
void setPlaying(bool on);
// Bit 0/1/2 = PREV/PLAY/NEXT offered; non-zero wins over the ambient state.
void setChoices(uint8_t mask);
// Audio peak, 0-255.
void setLevel(uint8_t level);
void setHue(uint8_t hue);

// Diagnostics: bypasses every animation.
void solid(uint8_t r, uint8_t g, uint8_t b);
uint32_t ticks();
void setBrightness(uint8_t b);
// Isolates a dead pixel: if n works and n+1 does not, the chain breaks at n.
void setActive(uint8_t n);
// One LED at a time: near-zero current separates a data fault from a weak supply.
void one(uint8_t index, uint8_t r, uint8_t g, uint8_t b);
#else
inline bool begin() { return false; }
inline void tick() {}
inline void event(LedEvent) {}
inline void setConfigMode(bool) {}
inline void setPlaying(bool) {}
inline void setChoices(uint8_t) {}
inline void setLevel(uint8_t) {}
inline void setHue(uint8_t) {}
inline void solid(uint8_t, uint8_t, uint8_t) {}
inline uint32_t ticks() { return 0; }
inline void setBrightness(uint8_t) {}
inline void setActive(uint8_t) {}
inline void one(uint8_t, uint8_t, uint8_t, uint8_t) {}
#endif
} // namespace Leds

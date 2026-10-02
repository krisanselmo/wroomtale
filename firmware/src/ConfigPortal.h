#pragma once
#include <Arduino.h>
#include "Features.h"
#include <functional>

// WiFi and the web dashboard used to bind tags.
namespace ConfigPortal {
#if FEAT_PORTAL
// loop() never runs while the portal is up, so `pump` carries the input.
void run(const std::function<void()> &pump);

// Persisted: every boot enters config mode, no gesture needed.
bool sticky();
void setSticky(bool on);
#else
inline void run(const std::function<void()> &) {}
inline bool sticky() { return false; }
inline void setSticky(bool) {}
#endif
} // namespace ConfigPortal

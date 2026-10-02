#pragma once
#include <Arduino.h>

// Tag UID -> target, in NVS.
namespace TagMap {
void begin();

String lookup(const String &uid);
bool assign(const String &uid, const String &folder);
bool forget(const String &uid);

// Callback rather than a list: the table never lands in RAM whole.
void forEach(const std::function<void(const String &uid, const String &folder)> &fn);

void noteSeen(const String &uid);
String lastSeen();
// Two taps of the same card are two events: the panel keys its binding prompt
// on this, so dismissing one tap does not deafen it to the next.
uint32_t lastSeenSeq();
} // namespace TagMap

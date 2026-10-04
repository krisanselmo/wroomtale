#pragma once
#include <Arduino.h>

// What the box did, in /logs/events.csv, and the columns every log row starts
// with: boot number, seconds since boot, and UTC time when the box knows it.
// It knows it once NTP or a browser has set it; deep sleep keeps it, with the
// drift of the RTC oscillator. A restart or a power cut loses it.
namespace Journal {
// Counts the boot and records its cause. First thing in setup().
void begin();
uint32_t boot();

// "boot,uptime_s,time," into out; returns its length.
int prefix(char *out, size_t size);

// Where the time came from: "ntp", "web", or "" when it is unknown.
const char *clockSource();
void setClock(time_t utc, const char *source);
// From the SNTP callback, on its own task: the clock is logged in tick().
void noteNtpSync();

void event(const char *kind, const String &detail = "");

// Notices track changes and an NTP answer, writes when the bus is free.
void tick();
bool flush();
bool clear();
uint32_t rows();
size_t buffered();
size_t capacity();
} // namespace Journal

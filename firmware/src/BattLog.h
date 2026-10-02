#pragma once
#include <Arduino.h>

namespace BattLog {

void begin();

// Periodic tick to sample battery and opportunistically flush to SD.
// State string describes current device state (e.g. "idle", "playing", "wifi_portal").
void tick(const char *state = nullptr);

// Force writing buffered entries to SD card.
bool flush();

// Clear the log file on SD and reset the RAM buffer.
bool clear();

bool active();
uint32_t entriesWritten();
size_t bufferedBytes();
size_t bufferCapacity();

} // namespace BattLog

#pragma once
#include <Arduino.h>

// Synthesised, not stored: a sine plus an envelope costs no flash.
struct Note {
	uint16_t hz; // 0 = rest
	uint16_t ms;
};

enum class Sfx : uint8_t { Boot, Play, Stop, Next, Prev, VolumeUp, VolumeDown, Error, Test, Tag, Ready, BatteryLow, Count };

const Note *sfxNotes(Sfx id, size_t &count);

// Stable ids, shared by the portal, the console and the NVS mask.
const char *sfxName(Sfx id);
// Sfx::Count when unknown.
Sfx sfxFromName(const char *name);

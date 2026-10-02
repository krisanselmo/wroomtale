#pragma once
#include <Arduino.h>

// Synthesised, not stored: a sine plus an envelope costs no flash.
struct Note {
	uint16_t hz; // 0 = rest
	uint16_t ms;
};

enum class Sfx : uint8_t { Boot, Play, Stop, Next, Prev, VolumeUp, VolumeDown, Error, Test, Tag, Count };

const Note *sfxNotes(Sfx id, size_t &count);

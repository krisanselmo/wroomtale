#include "Tones.h"

namespace {
// A C major triad and its neighbours: a music box, not an alarm clock.
const Note BOOT[] = {{523, 90}, {659, 90}, {784, 130}};
const Note PLAY[] = {{784, 60}, {1047, 90}};
const Note STOP[] = {{1047, 60}, {784, 90}};
const Note NEXT[] = {{880, 45}, {1047, 65}};
const Note PREV[] = {{1047, 45}, {880, 65}};
const Note VOL_UP[] = {{1319, 40}};
const Note VOL_DOWN[] = {{880, 40}};
const Note ERROR[] = {{247, 110}, {0, 50}, {247, 110}};
// 1 kHz: where small speakers and ears are both at their best.
const Note TEST[] = {{1000, 3000}};
// An acknowledgement, not a melody: playback follows it.
const Note TAG[] = {{1047, 55}, {1319, 85}};
// A rising fourth, unlike any button: the reader is listening.
const Note READY[] = {{1175, 60}, {1568, 110}};
// Winding down, slower than any button: the box is about to go dark.
const Note BATTERY_LOW[] = {{784, 160}, {659, 160}, {523, 160}, {392, 320}};

struct Entry {
	const Note *notes;
	size_t count;
	const char *name;
};

const Entry TABLE[] = {
	{BOOT, sizeof(BOOT) / sizeof(Note), "boot"},
	{PLAY, sizeof(PLAY) / sizeof(Note), "play"},
	{STOP, sizeof(STOP) / sizeof(Note), "stop"},
	{NEXT, sizeof(NEXT) / sizeof(Note), "next"},
	{PREV, sizeof(PREV) / sizeof(Note), "prev"},
	{VOL_UP, sizeof(VOL_UP) / sizeof(Note), "volup"},
	{VOL_DOWN, sizeof(VOL_DOWN) / sizeof(Note), "voldown"},
	{ERROR, sizeof(ERROR) / sizeof(Note), "error"},
	{TEST, sizeof(TEST) / sizeof(Note), "test"},
	{TAG, sizeof(TAG) / sizeof(Note), "tag"},
	{READY, sizeof(READY) / sizeof(Note), "ready"},
	{BATTERY_LOW, sizeof(BATTERY_LOW) / sizeof(Note), "lowbatt"},
};
static_assert(sizeof(TABLE) / sizeof(Entry) == static_cast<size_t>(Sfx::Count),
              "TABLE and Sfx must stay in sync");
} // namespace

const Note *sfxNotes(Sfx id, size_t &count) {
	const size_t index = static_cast<size_t>(id);
	if (index >= static_cast<size_t>(Sfx::Count)) {
		count = 0;
		return nullptr;
	}
	count = TABLE[index].count;
	return TABLE[index].notes;
}

const char *sfxName(Sfx id) {
	const size_t index = static_cast<size_t>(id);
	return index < static_cast<size_t>(Sfx::Count) ? TABLE[index].name : "";
}

Sfx sfxFromName(const char *name) {
	for (size_t i = 0; i < static_cast<size_t>(Sfx::Count); i++) {
		if (strcmp(TABLE[i].name, name) == 0) return static_cast<Sfx>(i);
	}
	return Sfx::Count;
}

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

struct Entry {
	const Note *notes;
	size_t count;
};

const Entry TABLE[] = {
	{BOOT, sizeof(BOOT) / sizeof(Note)},
	{PLAY, sizeof(PLAY) / sizeof(Note)},
	{STOP, sizeof(STOP) / sizeof(Note)},
	{NEXT, sizeof(NEXT) / sizeof(Note)},
	{PREV, sizeof(PREV) / sizeof(Note)},
	{VOL_UP, sizeof(VOL_UP) / sizeof(Note)},
	{VOL_DOWN, sizeof(VOL_DOWN) / sizeof(Note)},
	{ERROR, sizeof(ERROR) / sizeof(Note)},
	{TEST, sizeof(TEST) / sizeof(Note)},
	{TAG, sizeof(TAG) / sizeof(Note)},
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

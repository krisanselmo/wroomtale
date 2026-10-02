#pragma once
#include "Buttons.h"

// What a gesture means, apart from which gesture it was. The mapping is chosen
// at build time (BTN_SCHEME in Features.h) and lives in the .cpp, so main.cpp
// reads one switch over intentions and carries no #ifdef.
enum class BtnAction : uint8_t {
	None,
	PlayPause,
	Stop,
	NextTrack,
	PrevTrack,
	VolUp,
	VolDown,
	// Same move, no blip: a ramp beeping every 120 ms would drown the volume
	// it is setting.
	VolUpSilent,
	VolDownSilent,
};

namespace ButtonScheme {
BtnAction actionFor(BtnEvent ev);
// False when nothing in the scheme is bound to a double click: the detection
// window is then never armed, and the single click answers at once.
bool usesDoubleClick();
// For the boot log: knowing which box you are holding.
const char *name();
} // namespace ButtonScheme

#include "ButtonScheme.h"
#include "Features.h"

BtnAction ButtonScheme::actionFor(BtnEvent ev) {
	switch (ev) {
	case BtnEvent::PlayShort: return BtnAction::PlayPause;
	case BtnEvent::PlayLong: return BtnAction::Stop;
	case BtnEvent::PrevRepeat: return BtnAction::VolDownSilent;
	case BtnEvent::NextRepeat: return BtnAction::VolUpSilent;

#if BTN_SCHEME == 1
	// Volume first: the ends belong to it on a click and on a hold, and the
	// tracks move to a double click. There is no previous track in this one --
	// both ends are spent on the volume, by design.
	case BtnEvent::PrevShort:
	case BtnEvent::PrevLong: return BtnAction::VolDown;
	case BtnEvent::NextShort:
	case BtnEvent::NextLong: return BtnAction::VolUp;
	case BtnEvent::PlayDouble: return BtnAction::NextTrack;
#else
	// Default: the ends walk the tracks, and only a hold moves the volume.
	case BtnEvent::PrevShort: return BtnAction::PrevTrack;
	case BtnEvent::PrevLong: return BtnAction::VolDown;
	case BtnEvent::NextShort: return BtnAction::NextTrack;
	case BtnEvent::NextLong: return BtnAction::VolUp;
	// PLAY declares no double here, so this never arrives.
	case BtnEvent::PlayDouble: return BtnAction::None;
#endif

	case BtnEvent::None: return BtnAction::None;
	}
	return BtnAction::None;
}

bool ButtonScheme::usesDoubleClick() {
#if BTN_SCHEME == 1
	return true;
#else
	return false;
#endif
}

const char *ButtonScheme::name() {
#if BTN_SCHEME == 1
	return "volume-first (red/blue = volume, double click on green = next)";
#else
	return "classic (red/blue = tracks, hold = volume)";
#endif
}

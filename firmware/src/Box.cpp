#include "Box.h"
#include "ButtonScheme.h"
#include "Leds.h"
#include "Player.h"
#include "Power.h"
#include "Story.h"
#include "StoryRunner.h"
#include "TagMap.h"
#include "Target.h"

namespace {
// Same card, same colour: recognised before the sound starts.
uint8_t hueFor(const String &s) {
	uint32_t h = 2166136261u;
	for (char c : s) { h ^= (uint8_t)c; h *= 16777619u; }
	return (uint8_t)(h >> 13);
}

void volume(bool up, bool beep) {
	if (up) Player::volumeUp();
	else Player::volumeDown();
	if (beep) Player::play(up ? Sfx::VolumeUp : Sfx::VolumeDown);
	Leds::event(LedEvent::Volume);
}
} // namespace

void Box::press(BtnEvent ev) {
	if (ev != BtnEvent::None) Power::noteActivity();
	// A loaded story owns the three colours: the same button picks the same
	// branch whatever the scheme makes it mean elsewhere.
	if (StoryRunner::press(ev)) return;

	switch (ButtonScheme::actionFor(ev)) {
	case BtnAction::PlayPause: Player::togglePause(); Player::play(Sfx::Play); break;
	case BtnAction::Stop: Player::stop(); Player::play(Sfx::Stop); break;
	// A single sound, a story node or an embedded clip carries no playlist.
	// Beeping "next" over one of those is a button telling a lie.
	case BtnAction::NextTrack:
		if (Player::queueCount()) { Player::next(); Player::play(Sfx::Next); }
		break;
	case BtnAction::PrevTrack:
		if (Player::queueCount()) { Player::prev(); Player::play(Sfx::Prev); }
		break;
	case BtnAction::VolUp: volume(true, true); break;
	case BtnAction::VolDown: volume(false, true); break;
	case BtnAction::VolUpSilent: volume(true, false); break;
	case BtnAction::VolDownSilent: volume(false, false); break;
	case BtnAction::None: break;
	}
}

void Box::playTarget(const String &target) {
	StoryRunner::stop();

	if (Target::isBuiltin(target)) {
		Player::playFolder(target);
		return;
	}

	const String folder = Target::stripStoryPrefix(target);
	if (Story::hasManifest(folder)) {
		if (!StoryRunner::startFromSd(folder)) Player::play(Sfx::Error);
		return;
	}

	// One sound, not the folder around it: no playlist, so PREV/NEXT find
	// nothing to walk and the clip simply ends.
	if (Target::isFile(folder)) Player::playFile(folder);
	else Player::playFolder(folder);
}

void Box::presentTag(const String &uid) {
	Power::noteActivity();
	TagMap::noteSeen(uid);
	Leds::setHue(hueFor(uid));

	const String target = TagMap::lookup(uid);
	if (target.isEmpty()) {
		log_w("unknown tag %s", uid.c_str());
		Player::play(Sfx::Error);
		// The tone is dropped while a track runs; the flash never is.
		Leds::event(LedEvent::TagUnknown);
		return;
	}

	log_i("tag %s -> %s", uid.c_str(), target.c_str());
	// Stop first, or the acknowledgement is swallowed mid-album.
	Player::stop();
	Player::play(Sfx::Tag);
	Leds::event(LedEvent::TagOk);
	playTarget(target);
}

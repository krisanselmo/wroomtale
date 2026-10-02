#include "StoryRunner.h"
#include "Leds.h"
#include "Player.h"
#include "Story.h"
#include "Target.h"

namespace {
// A tale waits only once its segment has ended. Paused is not waiting: asking
// the player would have green pick a branch instead of resuming the narration.
bool g_waiting = false;

String choicesText(uint8_t mask) {
	String out;
	if (mask & 1) out += "RED ";
	if (mask & 2) out += "GREEN ";
	if (mask & 4) out += "BLUE";
	return out;
}

void playNode() {
	const String audio = Story::currentAudio();
	if (Target::isBuiltin(audio)) Player::playFolder(audio);
	else Player::playFile(audio);
}

void offerChoices(uint8_t mask) {
	g_waiting = mask != 0;
	Leds::setChoices(mask);
}

bool begin(bool loaded) {
	offerChoices(0);
	if (loaded) playNode();
	return loaded;
}
} // namespace

bool StoryRunner::startFromSd(const String &folder) { return begin(Story::loadFromSd(folder)); }
bool StoryRunner::startDemo() { return begin(Story::loadDemo()); }

void StoryRunner::stop() {
	Story::stop();
	offerChoices(0);
}

bool StoryRunner::press(BtnEvent ev) {
	if (!Story::active()) return false;

	uint8_t button;
	switch (ev) {
	case BtnEvent::PrevShort: button = Story::BTN_PREV; break;
	case BtnEvent::PlayShort: button = Story::BTN_PLAY; break;
	case BtnEvent::NextShort: button = Story::BTN_NEXT; break;
	// A tale has no next track. Left to the scheme this walked out of it, or
	// beeped as if it had.
	case BtnEvent::PlayDouble: return true;
	// Held gestures stay with the scheme, so the volume is still reachable
	// halfway through a tale.
	default: return false;
	}

	// Not at a fork yet: the colours wait, and green still pauses the narration.
	if (!g_waiting) return ev != BtnEvent::PlayShort;

	if (!Story::choose(button)) {
		Player::play(Sfx::Error);
		Leds::event(LedEvent::TagUnknown);
		return true;
	}
	offerChoices(0);
	playNode();
	return true;
}

void StoryRunner::onClipEnd() {
	if (!Story::active()) return;
	const uint8_t mask = Story::choiceMask();
	if (mask) {
		log_i("story: waiting -- %s", choicesText(mask).c_str());
		offerChoices(mask);
		return;
	}
	log_i("story: end of \"%s\"", Story::currentId().c_str());
	stop();
	Player::play(Sfx::Stop);
}

void StoryRunner::logStatus() {
	if (!Story::active()) { log_i("story: none running"); return; }
	log_i("story: at \"%s\" -- %s", Story::currentId().c_str(),
	      choicesText(Story::choiceMask()).c_str());
	const String flags = Story::flagList();
	log_i("story: flags = %s", flags.length() ? flags.c_str() : "(none)");
}

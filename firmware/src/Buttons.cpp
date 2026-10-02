#include "Buttons.h"
#include "ButtonScheme.h"
#include "Config.h"

namespace {
struct Button {
	gpio_num_t pin;
	BtnEvent shortEv;
	BtnEvent longEv;
	BtnEvent repeatEv; // None: the long press fires once and that is all
	BtnEvent doubleEv; // None: the click fires on release, with no waiting
	bool pressed;
	bool longFired;
	bool awaitingDouble;
	uint32_t changedAt;
	uint32_t repeatedAt;
	uint32_t releasedAt;
};

// Only PLAY declares a double, so only PLAY's click waits on the window.
Button g_buttons[] = {
	{PIN_BTN_PREV, BtnEvent::PrevShort, BtnEvent::PrevLong, BtnEvent::PrevRepeat,
	 BtnEvent::None, false, false, false, 0, 0, 0},
	{PIN_BTN_PLAY, BtnEvent::PlayShort, BtnEvent::PlayLong, BtnEvent::None,
	 BtnEvent::PlayDouble, false, false, false, 0, 0, 0},
	{PIN_BTN_NEXT, BtnEvent::NextShort, BtnEvent::NextLong, BtnEvent::NextRepeat,
	 BtnEvent::None, false, false, false, 0, 0, 0},
};
uint8_t g_seen = 0;
} // namespace

void Buttons::begin() {
	for (auto &b : g_buttons) pinMode(b.pin, INPUT_PULLUP);
	// A scheme that binds nothing to a double click must not make the single
	// one wait on a window it will never use.
	if (!ButtonScheme::usesDoubleClick())
		for (auto &b : g_buttons) b.doubleEv = BtnEvent::None;
}

bool Buttons::isHeld(gpio_num_t pin) { return digitalRead(pin) == LOW; }

uint8_t Buttons::downMask() {
	uint8_t mask = 0;
	for (size_t i = 0; i < sizeof(g_buttons) / sizeof(g_buttons[0]); i++) {
		if (digitalRead(g_buttons[i].pin) == LOW) mask |= 1 << i;
	}
	return mask;
}

uint8_t Buttons::seenMask() { return g_seen; }
void Buttons::clearSeen() { g_seen = 0; }

BtnEvent Buttons::poll() {
	const uint32_t now = millis();
	g_seen |= downMask();

	for (auto &b : g_buttons) {
		const bool down = digitalRead(b.pin) == LOW;

		if (down != b.pressed) {
			if (now - b.changedAt < BTN_DEBOUNCE_MS) continue;
			b.changedAt = now;
			b.pressed = down;

			if (down) {
				b.longFired = false;
			} else if (!b.longFired) {
				if (b.doubleEv == BtnEvent::None) return b.shortEv;
				if (b.awaitingDouble) {      // the second release closes it
					b.awaitingDouble = false;
					return b.doubleEv;
				}
				b.awaitingDouble = true;     // hold the click back for now
				b.releasedAt = now;
			}
			continue;
		}

		if (down && !b.longFired && now - b.changedAt >= BTN_LONGPRESS_MS) {
			b.longFired = true;
			b.repeatedAt = now;
			// Clicked then held: the hold is what was meant, so drop the click.
			b.awaitingDouble = false;
			return b.longEv;
		}

		if (down && b.longFired && b.repeatEv != BtnEvent::None &&
		    now - b.repeatedAt >= BTN_REPEAT_MS) {
			b.repeatedAt = now;
			return b.repeatEv;
		}

		// Nobody came back in time: it really was a single click.
		if (!down && b.awaitingDouble && now - b.releasedAt >= BTN_DOUBLE_MS) {
			b.awaitingDouble = false;
			return b.shortEv;
		}
	}
	return BtnEvent::None;
}

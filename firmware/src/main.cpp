#include "Battery.h"
#include "BattLog.h"
#include "Box.h"
#include "Buttons.h"
#include "ButtonScheme.h"
#include "Config.h"
#include "ConfigPortal.h"
#include "Console.h"
#include "Leds.h"
#include "Player.h"
#include "Power.h"
#include "Rfid.h"
#include "Story.h"
#include "StoryRunner.h"
#include "TagMap.h"

#include <Arduino.h>
#include <WiFi.h>

namespace {

bool g_inPortal = false;

const char *currentState() {
	if (g_inPortal) return "wifi_portal";
	if (Player::isPlaying()) return "playing";
	if (Story::active()) return "story_wait";
	return "idle";
}

// The PSRAM figure cannot be read over the flash port.
void logChipInfo() {
	// major*100 + minor.
	const uint16_t rev = ESP.getChipRevision();
	log_i("%s v%u.%02u, %d core(s), %lu MHz",
	      ESP.getChipModel(), rev / 100, rev % 100, ESP.getChipCores(),
	      (unsigned long)getCpuFrequencyMhz());
	// The largest block, not the total, is what an allocation hits.
	log_i("flash %lu KB, PSRAM %lu KB, heap %lu / %lu KB, largest block %lu KB",
	      (unsigned long)(ESP.getFlashChipSize() / 1024),
	      (unsigned long)(ESP.getPsramSize() / 1024),
	      (unsigned long)(ESP.getFreeHeap() / 1024),
	      (unsigned long)(ESP.getHeapSize() / 1024),
	      (unsigned long)(ESP.getMaxAllocHeap() / 1024));
}

// PLAY held, or any byte on the console -- which works before the buttons exist.
bool wantsConfigMode() {
	log_i("console: press any key within %lu ms for config mode",
	      (unsigned long)CONSOLE_ESCAPE_MS);

	const uint32_t escapeUntil = millis() + CONSOLE_ESCAPE_MS;
	while (millis() < escapeUntil) {
		if (Serial.available()) {
			while (Serial.available()) Serial.read();
			log_i("console escape taken");
			return true;
		}
		delay(10);
	}

	if (!Buttons::isHeld(PIN_BTN_PLAY)) return false;
	const uint32_t startedAt = millis();
	while (millis() - startedAt < CONFIG_GESTURE_MS) {
		if (!Buttons::isHeld(PIN_BTN_PLAY)) return false;
		delay(20);
	}
	return true;
}

void startBootSound() {
	if (Player::selfTestEnabled()) {
		log_i("self-test: repeating 1 kHz, press any button or key to stop");
		Player::startSelfTest();
	} else {
		Player::play(Sfx::Boot);
	}
}

// Called from loop() and from the portal, which runs its own.
void pumpInput() {
	Battery::tick();
	BattLog::tick(currentState());

	if (Player::selfTestRunning() && (Buttons::downMask() || Serial.available())) {
		Player::stopSelfTest();
	}
	Console::poll();
	Box::press(Buttons::poll());

	// On the first poll, not at boot: in config mode the WiFi join comes first.
	// Queued, so it follows the jingle rather than cutting it.
	static bool announced = false;
	if (!announced) {
		announced = true;
		if (Rfid::ready()) Player::play(Sfx::Ready);
	}

	const String uid = Rfid::poll();
	if (!uid.isEmpty()) Box::presentTag(uid);

	if (Player::takeFinished()) StoryRunner::onClipEnd();

	// Reading it is cheap; saying it once a minute is what makes it useful.
	static uint32_t lastBattWarn = 0;
	if (millis() - lastBattWarn > BATT_WARN_EVERY_MS) {
		lastBattWarn = millis();
		const uint16_t mv = Battery::millivolts();
		if (Battery::present() && mv < BATT_LOW_MV) {
			log_w("battery low: %u mV, %u %%", mv, Battery::percent());
		}
	}

	Leds::setLevel(Player::audioLevel());
	Leds::setPlaying(Player::isPlaying());
	Leds::tick();

	if (!g_inPortal) Power::tick();
}

} // namespace

void setup() {
	Serial.begin(115200);
	delay(200);

	logChipInfo();
	Power::begin();
	Leds::begin();
	Leds::event(LedEvent::Boot);
	Buttons::begin();
	Battery::begin();
	Power::sleepIfFlat();
	TagMap::begin();
	BattLog::begin();

	if (Battery::present()) log_i("battery: %u mV, %u %%", Battery::millivolts(), Battery::percent());
	else log_i("battery: no reading on GPIO %u (divider missing?)", PIN_BATT);

	if (ConfigPortal::sticky() || wantsConfigMode()) {
		log_i("entering config mode");
		Leds::setConfigMode(true);
		Rfid::begin();
		// Audio too: the dashboard and the self-test are usable together.
		Player::begin();
		startBootSound();
		g_inPortal = true;
		ConfigPortal::run(pumpInput); // never returns
	}

	// The radio stays off: ~50 kB of heap goes to audio instead.
	WiFi.mode(WIFI_OFF);
	btStop();

	if (!Rfid::begin()) log_w("no RFID reader, tags disabled");
	if (!Player::begin()) log_e("audio init failed");

	log_i("buttons: %s", ButtonScheme::name());
	log_i("ready, free heap %u -- type ? then Enter on the console", ESP.getFreeHeap());
	startBootSound();
}

void loop() {
	pumpInput();
	delay(5);
}

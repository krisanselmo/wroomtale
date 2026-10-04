#include "Power.h"
#include "Battery.h"
#include "BattLog.h"
#include "Config.h"
#include "Journal.h"
#include "Leds.h"
#include "Nvs.h"
#include "Player.h"
#include "PowerPolicy.h"

#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <esp_system.h>

namespace {
constexpr char NS[] = "power";

PowerPolicy g_policy(IDLE_SLEEP_DEFAULT_MIN * 60000UL, BATT_CRITICAL_MV, BATT_CRITICAL_HOLD_MS,
                     BATT_CHARGE_RISE_MV);

// The red pulses, and the tone queued with them, before the lights go out.
void windDown(uint32_t forMs) {
	const uint32_t until = millis() + forMs;
	while ((int32_t)(until - millis()) > 0) {
		Leds::tick();
		delay(5);
	}
}

// Held through deep sleep: a floating data line lights random pixels, and a
// PN532 out of reset draws tens of mA for nothing.
void holdLow(gpio_num_t pin) {
	pinMode(pin, OUTPUT);
	digitalWrite(pin, LOW);
	gpio_hold_en(pin);
}
} // namespace

void Power::begin() {
	if (PIN_PN532_RESET >= 0) gpio_hold_dis(static_cast<gpio_num_t>(PIN_PN532_RESET));
	gpio_hold_dis(PIN_LEDS);
	gpio_deep_sleep_hold_dis();

	const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
	if (cause == ESP_SLEEP_WAKEUP_EXT0) {
		rtc_gpio_deinit(PIN_BTN_PLAY);
		log_i("power: woken by PLAY");
	}

	g_policy.setIdleMs(idleMinutes() * 60000UL);
	g_policy.activity(millis());
}

void Power::sleepIfFlat() {
	const bool woke = esp_reset_reason() == ESP_RST_DEEPSLEEP;
	if (!g_policy.sleepAtBoot(woke, Battery::present(), Battery::millivolts())) return;
	log_w("power: cell flat at boot (%u mV)", Battery::millivolts());
	Leds::event(LedEvent::BatteryLow);
	windDown(1800);
	sleepNow("flat cell at boot");
}

void Power::noteActivity() { g_policy.activity(millis()); }

void Power::tick() {
	const uint32_t now = millis();
	if (Player::isPlaying() || Player::selfTestRunning()) g_policy.activity(now);

	switch (g_policy.check(now, Battery::present(), Battery::millivolts())) {
	case PowerPolicy::Verdict::Awake: break;
	case PowerPolicy::Verdict::Idle:
		sleepNow("idle");
		break;
	case PowerPolicy::Verdict::Flat:
		log_w("power: cell below %u mV for %lu s (%u mV)", BATT_CRITICAL_MV,
		      (unsigned long)(BATT_CRITICAL_HOLD_MS / 1000), Battery::millivolts());
		Player::stop();
		Player::play(Sfx::BatteryLow);
		Leds::event(LedEvent::BatteryLow);
		windDown(1800);
		sleepNow("flat cell");
		break;
	}
}

uint16_t Power::idleMinutes() {
	return Nvs(NS, true)->getUShort("idlemin", IDLE_SLEEP_DEFAULT_MIN);
}

void Power::setIdleMinutes(uint16_t minutes) {
	if (minutes > IDLE_SLEEP_MAX_MIN) minutes = IDLE_SLEEP_MAX_MIN;
	Nvs(NS, false)->putUShort("idlemin", minutes);
	g_policy.setIdleMs(minutes * 60000UL);
	g_policy.activity(millis()); // a new delay counts from now
}

void Power::sleepNow(const char *reason) {
	log_i("power: going to sleep (%s), PLAY wakes the box", reason);

	// The logs first: Player::shutdown() unmounts the card.
	Journal::event("sleep", reason);
	BattLog::flush();
	Journal::flush();
	Player::shutdown();
	const uint32_t startedAt = millis();
	while (!Player::isShutDown() && millis() - startedAt < 3000) {
		Leds::tick();
		delay(5);
	}
	Leds::off();

	if (PIN_PN532_RESET >= 0) holdLow(static_cast<gpio_num_t>(PIN_PN532_RESET));
	holdLow(PIN_LEDS);
	gpio_deep_sleep_hold_en();

	// The buttons pull to ground; the RTC pull-up keeps PLAY high while asleep.
	rtc_gpio_pullup_en(PIN_BTN_PLAY);
	rtc_gpio_pulldown_dis(PIN_BTN_PLAY);
	esp_sleep_enable_ext0_wakeup(PIN_BTN_PLAY, 0);

	Serial.flush();
	esp_deep_sleep_start();
}

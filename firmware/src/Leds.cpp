#include "Leds.h"
#include "Config.h"
#include "Player.h"

#include <FastLED.h>

namespace {
CRGB g_leds[LED_COUNT];
bool g_ready = false;

bool g_configMode = false;
bool g_playing = false;
uint8_t g_choices = 0;
uint8_t g_hue = 96;
uint8_t g_level = 0;     // smoothed peak
uint8_t g_hold = 0;      // decaying peak marker
uint32_t g_holdAt = 0;
uint32_t g_ticks = 0;
uint8_t g_active = LED_COUNT;

LedEvent g_event = LedEvent::Boot;
uint32_t g_eventUntil = 0;
uint32_t g_eventStart = 0;

constexpr uint32_t BOOT_MS = 1100;
constexpr uint32_t FLASH_MS = 450;
constexpr uint32_t UNKNOWN_MS = 700;
constexpr uint32_t VOLUME_MS = 1600;

void fill(const CRGB &c) {
	for (uint8_t i = 0; i < LED_COUNT; i++) g_leds[i] = i < g_active ? c : CRGB::Black;
}

float phase(uint32_t span) {
	const uint32_t elapsed = millis() - g_eventStart;
	return elapsed >= span ? 1.0f : (float)elapsed / span;
}

void drawEvent() {
	switch (g_event) {
	case LedEvent::Boot: {
		// Doubles as a wiring check: the walk stops where the data stops.
		uint8_t head = (uint8_t)(phase(BOOT_MS) * LED_COUNT);
		if (head >= LED_COUNT) head = LED_COUNT - 1;
		for (uint8_t i = 0; i < LED_COUNT; i++) {
			g_leds[i] = (i == head) ? CRGB(CHSV(96, 200, 255)) : CRGB(CRGB::Black);
		}
		break;
	}
	case LedEvent::TagOk:
		fill(CRGB(0, 255, 40));
		break;
	case LedEvent::TagUnknown: {
		// Two beats: a refusal, not a status colour.
		const float p = phase(UNKNOWN_MS);
		const bool lit = (p < 0.25f) || (p > 0.5f && p < 0.75f);
		fill(lit ? CRGB(255, 0, 0) : CRGB::Black);
		break;
	}
	case LedEvent::Volume: {
		const uint8_t vol = Player::volume();
		const uint8_t lit = (uint16_t)vol * LED_COUNT / VOLUME_MAX;
		for (uint8_t i = 0; i < LED_COUNT; i++) {
			g_leds[i] = i < lit ? CRGB(255, 150, 0) : CRGB(12, 6, 0);
		}
		break;
	}
	}
}

void drawAmbient() {
	if (g_choices) {
		// One zone per button; lit means that button leads somewhere.
		const uint8_t zone = LED_COUNT / 3 ? LED_COUNT / 3 : 1;
		const float breath = (1 + sinf(millis() / 400.0f)) / 2;
		for (uint8_t i = 0; i < LED_COUNT; i++) {
			const uint8_t b = i / zone;
			if (b < 3 && (g_choices & (1 << b))) {
				// The button's own colour: the strip and the narration must agree.
				const uint32_t c = BTN_COLOUR[b];
				g_leds[i] = CRGB((uint8_t)(((c >> 16) & 0xFF) * (0.45f + 0.55f * breath)),
				                 (uint8_t)(((c >> 8) & 0xFF) * (0.45f + 0.55f * breath)),
				                 (uint8_t)((c & 0xFF) * (0.45f + 0.55f * breath)));
			} else {
				g_leds[i] = CHSV(0, 0, 10);
			}
		}
		return;
	}
	if (g_playing) {
		// Falls back to a breath when quiet, so the box never looks dead.
		const uint8_t half = LED_COUNT / 2;
		const uint8_t lit = (uint16_t)g_level * half / 255;

		for (uint8_t i = 0; i < LED_COUNT; i++) {
			const uint8_t d = i < half ? half - 1 - i : i - half; // distance from centre
			if (d < lit) {
				g_leds[i] = CHSV(g_hue + d * 4, 220, 255);
			} else if (d == g_hold && g_hold) {
				g_leds[i] = CHSV(g_hue + 32, 120, 200);
			} else {
				g_leds[i] = CHSV(g_hue, 200, 40);
			}
		}
	} else if (g_configMode) {
		fill(CHSV(150, 255, 110));
	} else {
		fill(CRGB::Black);
	}
}
} // namespace

bool Leds::begin() {
	FastLED.addLeds<WS2812B, PIN_LEDS, GRB>(g_leds, LED_COUNT);
	FastLED.setBrightness(LED_BRIGHTNESS);
	FastLED.setMaxPowerInVoltsAndMilliamps(5, LED_MAX_MA);
	fill(CRGB::Black);
	FastLED.show();
	g_ready = true;
	log_i("leds: %u on GPIO %d, brightness %u, %u mA cap", LED_COUNT, PIN_LEDS,
	      LED_BRIGHTNESS, LED_MAX_MA);
	return true;
}

uint32_t Leds::ticks() { return g_ticks; }

void Leds::solid(uint8_t r, uint8_t g, uint8_t b) {
	if (!g_ready) return;
	fill(CRGB(r, g, b));
	FastLED.show();
}

void Leds::setActive(uint8_t n) {
	g_active = n > LED_COUNT ? LED_COUNT : n;
	log_i("led: %u of %u LEDs driven", g_active, LED_COUNT);
}

void Leds::setBrightness(uint8_t b) {
	FastLED.setBrightness(b);
	log_i("led: brightness %u/255", b);
}

void Leds::one(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
	if (!g_ready) return;
	fill(CRGB::Black);
	if (index < g_active) g_leds[index] = CRGB(r, g, b);
	FastLED.show();
}

void Leds::tick() {
	g_ticks++;
	if (!g_ready) return;

	// 50 Hz: keeps RMT transfers away from the audio task.
	static uint32_t lastFrame = 0;
	const uint32_t now = millis();
	if (now - lastFrame < 20) return;
	lastFrame = now;

	if (now < g_eventUntil) drawEvent();
	else drawAmbient();

	FastLED.show();
}

void Leds::event(LedEvent e) {
	g_event = e;
	g_eventStart = millis();
	switch (e) {
	case LedEvent::Boot: g_eventUntil = g_eventStart + BOOT_MS; break;
	case LedEvent::TagUnknown: g_eventUntil = g_eventStart + UNKNOWN_MS; break;
	case LedEvent::Volume: g_eventUntil = g_eventStart + VOLUME_MS; break;
	default: g_eventUntil = g_eventStart + FLASH_MS; break;
	}
}

void Leds::setConfigMode(bool on) { g_configMode = on; }
void Leds::setPlaying(bool on) {
	if (!on) { g_level = 0; g_hold = 0; }
	g_playing = on;
}

void Leds::setLevel(uint8_t level) {
	// Attack fast, release slow.
	g_level = level > g_level ? level : (uint8_t)(g_level - (g_level >> 3));

	const uint8_t half = LED_COUNT / 2;
	const uint8_t lit = (uint16_t)g_level * half / 255;
	const uint32_t now = millis();
	if (lit > g_hold) { g_hold = lit; g_holdAt = now; }
	else if (g_hold && now - g_holdAt > 120) { g_hold--; g_holdAt = now; }
}

void Leds::setHue(uint8_t hue) { g_hue = hue; }
void Leds::setChoices(uint8_t mask) { g_choices = mask; }

#include "Battery.h"
#include "Config.h"

#include "Nvs.h"

namespace {
constexpr char NS[] = "battery";

float g_trim = 1.0f;
float g_filteredMv = 0.0f;
uint16_t g_rawMv = 0;
uint16_t g_rawPinMv = 0;
bool g_initialized = false;
uint32_t g_lastSampleMs = 0;

// A lithium cell spends most of its charge between 3.9 and 3.6 V, so a linear
// gauge reads 70 % for hours and then collapses. These are the corners of the
// curve at a light load.
struct Point {
	uint16_t mv;
	uint8_t pct;
};
constexpr Point CURVE[] = {
    {4200, 100}, {4100, 92}, {4000, 85}, {3900, 72}, {3800, 58},
    {3700, 44},  {3600, 30}, {3500, 18}, {3400, 10}, {3300, 4}, {3000, 0},
};

void sample() {
	uint32_t sum = 0;
	for (uint8_t i = 0; i < BATT_SAMPLES; i++) sum += analogReadMilliVolts(PIN_BATT);
	g_rawPinMv = (uint16_t)(sum / BATT_SAMPLES);
	const uint32_t rawCell = (uint32_t)(g_rawPinMv * BATT_DIVIDER * g_trim);
	g_rawMv = rawCell > 6000 ? 0 : (uint16_t)rawCell;

	if (!g_initialized) {
		g_filteredMv = (float)g_rawMv;
		g_initialized = true;
	} else {
		g_filteredMv = BATT_EMA_ALPHA * (float)g_rawMv + (1.0f - BATT_EMA_ALPHA) * g_filteredMv;
	}
}

} // namespace

namespace Battery {

void begin() {
	// 11 dB reads up to ~3,1 V: 4,2 V halved is 2,1 V, with room to spare. The
	// pin only becomes an ADC channel on its first read.
	analogReadMilliVolts(PIN_BATT);
	analogSetPinAttenuation(PIN_BATT, ADC_11db);

	g_trim = Nvs(NS, true)->getFloat("trim", 1.0f);

	if (g_trim < 0.5f || g_trim > 1.5f) g_trim = 1.0f;

	// Initial reading precharges the EMA filter to avoid a slow ramp from 0 V.
	sample();
}

void tick() {
	const uint32_t now = millis();
	if (now - g_lastSampleMs < BATT_SAMPLE_MS && g_initialized) return;
	g_lastSampleMs = now;
	sample();
}

uint16_t pinMillivolts() {
	if (!g_initialized) sample();
	return g_rawPinMv;
}

uint16_t rawMillivolts() {
	if (!g_initialized) sample();
	return g_rawMv;
}

uint16_t millivolts() {
	if (!g_initialized) sample();
	return (uint16_t)(g_filteredMv + 0.5f);
}

bool present() {
	const uint16_t mv = millivolts();
	return mv >= BATT_ABSENT_MV && mv <= BATT_FULL_MV + 200;
}

uint8_t percent() {
	const uint16_t mv = millivolts();
	if (!present()) return 0;
	if (mv >= CURVE[0].mv) return 100;

	for (size_t i = 1; i < sizeof(CURVE) / sizeof(CURVE[0]); i++) {
		if (mv >= CURVE[i].mv) {
			const Point &hi = CURVE[i - 1], &lo = CURVE[i];
			return lo.pct + (uint8_t)((uint32_t)(mv - lo.mv) * (hi.pct - lo.pct) / (hi.mv - lo.mv));
		}
	}
	return 0;
}

void calibrate(uint16_t realMv) {
	if (!g_initialized) sample();
	const uint16_t raw = (uint16_t)(g_rawPinMv * BATT_DIVIDER);
	if (raw < BATT_ABSENT_MV || realMv < BATT_ABSENT_MV) {
		log_w("battery: nothing measurable, calibration ignored");
		return;
	}

	g_trim = (float)realMv / (float)raw;
	Nvs(NS, false)->putFloat("trim", g_trim);
	log_i("battery: trim %.4f (%u mV read -> %u mV actual)", g_trim, raw, realMv);

	// Recompute instantaneous and reset filtered voltage with new trim.
	g_initialized = false;
	sample();
}

void clearCalibration() {
	g_trim = 1.0f;
	Nvs(NS, false)->remove("trim");

	g_initialized = false;
	sample();
}

float trim() { return g_trim; }

} // namespace Battery

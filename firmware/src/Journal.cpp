#include "Journal.h"
#include "CsvLog.h"
#include "Nvs.h"
#include "Player.h"

#include <esp_system.h>
#include <sys/time.h>

namespace {
constexpr time_t CLOCK_FLOOR = 1767225600; // 2026-01-01: anything earlier was never set
constexpr uint32_t TRACK_POLL_MS = 100;

CsvLog g_log("/logs/events.csv", "boot,uptime_s,time,event,detail");
uint32_t g_boot = 0;
// RTC memory: survives deep sleep only, the bootloader reloads it otherwise.
RTC_DATA_ATTR char g_clockSource[4] = "";
volatile bool g_ntpAnswered = false;
uint32_t g_starts = 0;
bool g_playing = false;

const char *resetCause() {
	switch (esp_reset_reason()) {
	case ESP_RST_POWERON: return "power-on";
	case ESP_RST_EXT: return "reset pin";
	case ESP_RST_SW: return "restart";
	case ESP_RST_PANIC: return "panic";
	case ESP_RST_INT_WDT:
	case ESP_RST_TASK_WDT:
	case ESP_RST_WDT: return "watchdog";
	case ESP_RST_DEEPSLEEP: return "wake";
	case ESP_RST_BROWNOUT: return "brownout";
	default: return "other";
	}
}

bool clockValid() { return g_clockSource[0] && time(nullptr) >= CLOCK_FLOOR; }
} // namespace

namespace Journal {

void begin() {
	{
		Nvs nvs("journal", false);
		g_boot = nvs->getULong("boot", 0) + 1;
		nvs->putULong("boot", g_boot);
	}
	if (!clockValid()) g_clockSource[0] = '\0';
	event("boot", resetCause());
	if (clockValid()) event("clock", String("kept, set by ") + g_clockSource);
}

uint32_t boot() { return g_boot; }

int prefix(char *out, size_t size) {
	char when[24] = "";
	if (clockValid()) {
		const time_t now = time(nullptr);
		struct tm utc;
		gmtime_r(&now, &utc);
		// Not strftime(): newlib places it in IRAM, which the BT build has no room for.
		snprintf(when, sizeof(when), "%04d-%02d-%02dT%02d:%02d:%02dZ", utc.tm_year + 1900,
		         utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec);
	}
	return snprintf(out, size, "%lu,%lu,%s,", (unsigned long)g_boot,
	                (unsigned long)(millis() / 1000), when);
}

const char *clockSource() { return clockValid() ? g_clockSource : ""; }

void setClock(time_t utc, const char *source) {
	if (utc < CLOCK_FLOOR) return;
	const struct timeval tv = {utc, 0};
	settimeofday(&tv, nullptr);
	strlcpy(g_clockSource, source, sizeof(g_clockSource));
	event("clock", String("set by ") + source);
}

void noteNtpSync() { g_ntpAnswered = true; }

void event(const char *kind, const String &detail) {
	char row[192];
	int len = prefix(row, sizeof(row));
	// Quoted: file names carry commas.
	String quoted = detail;
	quoted.replace("\"", "\"\"");
	len += snprintf(row + len, sizeof(row) - len, "%s,\"%s\"\n", kind, quoted.c_str());
	if (len >= (int)sizeof(row)) {
		row[sizeof(row) - 3] = '"';
		row[sizeof(row) - 2] = '\n';
		len = sizeof(row) - 1;
	}
	g_log.append(row, len);
}

void tick() {
	if (g_ntpAnswered) {
		g_ntpAnswered = false;
		strlcpy(g_clockSource, "ntp", sizeof(g_clockSource));
		event("clock", "set by ntp");
	}

	static uint32_t lastPoll = 0;
	if (millis() - lastPoll >= TRACK_POLL_MS) {
		lastPoll = millis();
		String track;
		uint32_t starts = g_starts;
		if (Player::lastStarted(starts, track) && starts != g_starts) {
			g_starts = starts;
			event("track", track);
			g_playing = true;
		}
		if (g_playing && !Player::isPlaying()) {
			g_playing = false;
			event("stop");
		}
	}

	g_log.tick();
}

bool flush() { return g_log.flush(); }
bool clear() { return g_log.clear(); }
uint32_t rows() { return g_log.rows(); }
size_t buffered() { return g_log.buffered(); }
size_t capacity() { return g_log.capacity(); }

} // namespace Journal

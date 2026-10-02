#include "BattLog.h"
#include "Battery.h"
#include "Config.h"
#include "Player.h"

#include <SD.h>

namespace {

char g_buffer[BATTLOG_BUFFER_SIZE];
size_t g_bufferLen = 0;
uint32_t g_entriesWritten = 0;
uint32_t g_lastSampleMs = 0;
uint32_t g_lastFlushMs = 0;

} // namespace

namespace BattLog {

void begin() {
	g_bufferLen = 0;
	g_entriesWritten = 0;
	g_lastSampleMs = 0;
	g_lastFlushMs = millis();
}

bool flush() {
	if (!Player::sdReady()) return false;
	if (g_bufferLen == 0) return true;

	if (!SD.exists("/logs")) {
		if (!SD.mkdir("/logs")) {
			log_w("battlog: failed to create /logs directory");
			return false;
		}
	}

	const bool fileExists = SD.exists("/logs/batt.csv");
	File f = SD.open("/logs/batt.csv", FILE_APPEND);
	if (!f) {
		log_w("battlog: failed to open /logs/batt.csv");
		return false;
	}

	if (!fileExists || f.size() == 0) {
		f.println("uptime_s,raw_mv,filtered_mv,percent,state");
	}

	const size_t written = f.write(reinterpret_cast<const uint8_t *>(g_buffer), g_bufferLen);
	f.close();

	if (written == g_bufferLen) {
		g_bufferLen = 0;
		g_lastFlushMs = millis();
		return true;
	}

	if (written > 0) {
		memmove(g_buffer, g_buffer + written, g_bufferLen - written);
		g_bufferLen -= written;
		g_lastFlushMs = millis();
		return true;
	}

	return false;
}

bool clear() {
	g_bufferLen = 0;
	if (!Player::sdReady()) return false;

	if (SD.exists("/logs/batt.csv")) {
		return SD.remove("/logs/batt.csv");
	}
	return true;
}

void tick(const char *state) {
	if (!Player::sdReady()) {
		g_bufferLen = 0;
		return;
	}

	const uint32_t now = millis();

	if (now - g_lastSampleMs >= BATTLOG_INTERVAL_MS || g_lastSampleMs == 0) {
		g_lastSampleMs = now;
		const char *st = state ? state : (Player::isPlaying() ? "playing" : "idle");

		char line[64];
		const int len = snprintf(line, sizeof(line), "%lu,%u,%u,%u,%s\n",
		                         (unsigned long)(now / 1000),
		                         Battery::rawMillivolts(),
		                         Battery::millivolts(),
		                         Battery::percent(),
		                         st);

		if (len > 0 && static_cast<size_t>(len) < sizeof(line)) {
			if (g_bufferLen + len > sizeof(g_buffer)) {
				flush();
			}

			if (g_bufferLen + len <= sizeof(g_buffer)) {
				memcpy(g_buffer + g_bufferLen, line, len);
				g_bufferLen += len;
				g_entriesWritten++;
			} else {
				log_w("battlog: buffer full, dropping entry");
			}
		}
	}

	// Opportunistic flush logic:
	// Flush when player is idle. If continuous playback exceeds hold time or buffer
	// is nearly full, flush only when audio buffer has enough headroom.
	if (g_bufferLen > 0) {
		if (!Player::isPlaying()) {
			flush();
		} else if (g_bufferLen >= sizeof(g_buffer) - 64 || (now - g_lastFlushMs >= BATTLOG_MAX_HOLD_MS)) {
			if (Player::audioBufferHealthy()) {
				flush();
			}
		}
	}
}

bool active() {
	return Player::sdReady();
}

uint32_t entriesWritten() {
	return g_entriesWritten;
}

size_t bufferedBytes() {
	return g_bufferLen;
}

size_t bufferCapacity() {
	return sizeof(g_buffer);
}

} // namespace BattLog

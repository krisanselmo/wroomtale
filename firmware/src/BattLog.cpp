#include "BattLog.h"
#include "Battery.h"
#include "Config.h"
#include "CsvLog.h"
#include "Journal.h"
#include "Player.h"

namespace {
CsvLog g_log("/logs/batt.csv", "boot,uptime_s,time,raw_mv,filtered_mv,percent,state");
uint32_t g_lastSampleMs = 0;
bool g_sampled = false;
} // namespace

namespace BattLog {

void begin() { g_sampled = false; }

bool flush() { return g_log.flush(); }
bool clear() { return g_log.clear(); }

void tick(const char *state) {
	if (!Player::sdReady()) return;

	const uint32_t now = millis();
	if (!g_sampled || now - g_lastSampleMs >= BATTLOG_INTERVAL_MS) {
		g_sampled = true;
		g_lastSampleMs = now;
		const char *st = state ? state : (Player::isPlaying() ? "playing" : "idle");
		char row[96];
		int len = Journal::prefix(row, sizeof(row));
		len += snprintf(row + len, sizeof(row) - len, "%u,%u,%u,%s\n", Battery::rawMillivolts(),
		                Battery::millivolts(), Battery::percent(), st);
		if (len < (int)sizeof(row)) g_log.append(row, len);
	}
	g_log.tick();
}

bool active() { return Player::sdReady(); }
uint32_t entriesWritten() { return g_log.rows(); }
size_t bufferedBytes() { return g_log.buffered(); }
size_t bufferCapacity() { return g_log.capacity(); }

} // namespace BattLog

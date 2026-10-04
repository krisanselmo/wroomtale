#include "CsvLog.h"
#include "Player.h"

#include <SD.h>

namespace {
// The card, not Player::sdReady(): a sleep at boot mounts it before the player.
bool mounted() { return SD.cardType() != CARD_NONE; }
} // namespace

bool CsvLog::setAsideStale() {
	File f = SD.open(_path);
	if (!f) return true;
	String first = f.readStringUntil('\n');
	f.close();
	first.trim();
	if (first == _header) return true;

	String old = _path;
	old.replace(".csv", "-old.csv");
	SD.remove(old);
	if (!SD.rename(_path, old)) return false;
	log_i("log: %s had another header, kept as %s", _path, old.c_str());
	return true;
}

void CsvLog::append(const char *row, size_t len) {
	if (_len + len > sizeof(_buf)) flush();
	if (_len + len > sizeof(_buf)) {
		log_w("log: %s buffer full, row dropped", _path);
		return;
	}
	memcpy(_buf + _len, row, len);
	_len += len;
	_rows++;
}

bool CsvLog::flush() {
	if (!mounted()) return false;
	if (_len == 0) return true;

	if (!SD.exists("/logs") && !SD.mkdir("/logs")) {
		log_w("log: cannot create /logs");
		return false;
	}
	if (!_checked) _checked = setAsideStale();

	const bool fresh = !SD.exists(_path);
	File f = SD.open(_path, FILE_APPEND);
	if (!f) {
		log_w("log: cannot open %s", _path);
		return false;
	}
	if (fresh || f.size() == 0) {
		f.print(_header);
		f.print('\n');
	}
	const size_t written = f.write(reinterpret_cast<const uint8_t *>(_buf), _len);
	f.close();
	if (written == 0) return false;

	memmove(_buf, _buf + written, _len - written);
	_len -= written;
	_lastFlushMs = millis();
	return _len == 0;
}

void CsvLog::tick() {
	if (_len == 0) return;
	// The decoder and the card share the SPI bus: write between tracks, or
	// mid-track only once the audio buffer has room to spare.
	if (!Player::isPlaying()) {
		flush();
	} else if ((_len >= sizeof(_buf) - 64 || millis() - _lastFlushMs >= LOG_MAX_HOLD_MS) &&
	           Player::audioBufferHealthy()) {
		flush();
	}
}

bool CsvLog::clear() {
	_len = 0;
	if (!mounted()) return false;
	return !SD.exists(_path) || SD.remove(_path);
}

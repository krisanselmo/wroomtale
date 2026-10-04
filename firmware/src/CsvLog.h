#pragma once
#include <Arduino.h>
#include "Config.h"

// A CSV file on the card, fed through a RAM buffer. A file written under
// another header is set aside as name-old.csv rather than mixed with new rows.
class CsvLog {
public:
	CsvLog(const char *path, const char *header) : _path(path), _header(header) {}

	// One row, newline included.
	void append(const char *row, size_t len);
	// Writes when the decoder can spare the bus.
	void tick();
	bool flush();
	bool clear();

	uint32_t rows() const { return _rows; }
	size_t buffered() const { return _len; }
	size_t capacity() const { return sizeof(_buf); }

private:
	bool setAsideStale();

	const char *_path;
	const char *_header;
	char _buf[LOG_BUFFER_SIZE];
	size_t _len = 0;
	uint32_t _rows = 0;
	uint32_t _lastFlushMs = 0;
	bool _checked = false;
};

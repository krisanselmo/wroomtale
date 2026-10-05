#pragma once
#include "Target.h"
#include <Arduino.h>
#include <stdlib.h>
#include <vector>

// Where a folder was left: the track by name and the byte reached in it. The
// name, not the index: a file added to the folder shifts every index after it,
// and resuming the wrong story halfway through is worse than starting over.
namespace Resume {
// NVS keys stop at 15 chars and folder paths do not: a hash of the path.
inline String key(const String &folder) {
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < folder.length(); i++) {
		h ^= (uint8_t)folder[i];
		h *= 16777619u;
	}
	char buf[10];
	snprintf(buf, sizeof buf, "f%08lx", (unsigned long)h);
	return String(buf);
}

// "<offset> <track>": the track is a bare name, which may hold spaces.
inline String format(uint32_t offset, const String &track) {
	return String((unsigned long)offset) + " " + track;
}

inline bool parse(const String &value, uint32_t &offset, String &track) {
	const int space = value.indexOf(' ');
	if (space <= 0 || space + 1 >= (int)value.length()) return false;
	const String digits = value.substring(0, space);
	char *end = nullptr;
	const unsigned long n = strtoul(digits.c_str(), &end, 10);
	if (!end || *end != '\0') return false;
	offset = (uint32_t)n;
	track = value.substring(space + 1);
	return true;
}

// -1 when the track has left the folder.
inline int find(const std::vector<String> &paths, const String &track) {
	for (size_t i = 0; i < paths.size(); i++)
		if (Target::baseName(paths[i]) == track) return (int)i;
	return -1;
}
} // namespace Resume

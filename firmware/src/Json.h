#pragma once
#include <Arduino.h>

// Quotes and backslashes only: control characters pass through unescaped.
inline String jsonEscape(const String &in) {
	String out;
	for (char c : in) {
		if (c == '"' || c == '\\') out += '\\';
		out += c;
	}
	return out;
}

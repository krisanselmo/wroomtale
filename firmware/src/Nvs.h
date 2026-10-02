#pragma once
#include <Preferences.h>

// A namespace held open for one scope: begin() and end() cannot drift apart.
class Nvs {
public:
	Nvs(const char *ns, bool readOnly) { _prefs.begin(ns, readOnly); }
	~Nvs() { _prefs.end(); }
	Nvs(const Nvs &) = delete;
	Nvs &operator=(const Nvs &) = delete;

	Preferences *operator->() { return &_prefs; }

private:
	Preferences _prefs;
};

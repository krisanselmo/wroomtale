#pragma once
#include <Preferences.h>
#include <nvs.h>

// A namespace held open for one scope: begin() and end() cannot drift apart.
// One never written is a fresh box, not an error: it stays closed, and reads
// return their defaults without Preferences logging a failed open.
class Nvs {
public:
	Nvs(const char *ns, bool readOnly) {
		if (readOnly && !exists(ns)) return;
		_prefs.begin(ns, readOnly);
	}
	~Nvs() { _prefs.end(); }
	Nvs(const Nvs &) = delete;
	Nvs &operator=(const Nvs &) = delete;

	Preferences *operator->() { return &_prefs; }

private:
	static bool exists(const char *ns) {
		nvs_handle_t h;
		if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return false;
		nvs_close(h);
		return true;
	}

	Preferences _prefs;
};

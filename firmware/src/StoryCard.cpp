#include "Story.h"
#include <SD.h>

namespace {
String manifestPath(const String &folder) { return folder + "/histoire.txt"; }
} // namespace

bool Story::hasManifest(const String &folder) { return SD.exists(manifestPath(folder)); }

bool Story::loadFromSd(const String &folder) {
	const String path = manifestPath(folder);
	File f = SD.open(path);
	if (!f) {
		// Failing to load still ends the story already running.
		stop();
		log_e("story: %s not found", path.c_str());
		return false;
	}
	// MAX_NODES short lines: a couple of kB at most.
	const String text = f.readString();
	f.close();

	log_i("story: reading %s", path.c_str());
	return loadFromText(text, folder);
}

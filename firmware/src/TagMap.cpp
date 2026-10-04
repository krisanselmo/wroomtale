#include "TagMap.h"
#include "Nvs.h"
#include <Preferences.h>
#include <vector>

namespace {
Preferences prefs;
// NVS keys cap at 15 characters: a 7-byte UID in hex fits exactly.
constexpr char NS[] = "tags";
constexpr char NS_INDEX[] = "tagidx";
String g_lastSeen;
uint32_t g_lastSeq = 0;

// The Arduino wrapper cannot enumerate keys, hence this hand-kept index.
std::vector<String> loadIndex() {
	std::vector<String> out;
	const String blob = Nvs(NS_INDEX, true)->getString("list", "");

	int start = 0;
	while (start < blob.length()) {
		int sep = blob.indexOf(',', start);
		if (sep < 0) sep = blob.length();
		if (sep > start) out.push_back(blob.substring(start, sep));
		start = sep + 1;
	}
	return out;
}

void saveIndex(const std::vector<String> &uids) {
	String blob;
	for (const auto &uid : uids) {
		if (blob.length()) blob += ',';
		blob += uid;
	}
	Nvs(NS_INDEX, false)->putString("list", blob);
}
} // namespace

void TagMap::begin() {
	prefs.begin(NS, false);
}

String TagMap::lookup(const String &uid) {
	// isKey first: an unknown card is routine, and getString() logs it as an error.
	if (!prefs.isKey(uid.c_str())) return "";
	return prefs.getString(uid.c_str(), "");
}

bool TagMap::assign(const String &uid, const String &folder) {
	if (uid.isEmpty() || uid.length() > 15) return false;
	if (!prefs.putString(uid.c_str(), folder)) return false;

	auto uids = loadIndex();
	for (const auto &known : uids) {
		if (known == uid) return true;
	}
	uids.push_back(uid);
	saveIndex(uids);
	return true;
}

bool TagMap::forget(const String &uid) {
	prefs.remove(uid.c_str());

	auto uids = loadIndex();
	for (auto it = uids.begin(); it != uids.end(); ++it) {
		if (*it == uid) {
			uids.erase(it);
			saveIndex(uids);
			return true;
		}
	}
	return false;
}

void TagMap::forEach(const std::function<void(const String &, const String &)> &fn) {
	for (const auto &uid : loadIndex()) {
		fn(uid, prefs.getString(uid.c_str(), ""));
	}
}

void TagMap::noteSeen(const String &uid) {
	g_lastSeen = uid;
	g_lastSeq++;
}
String TagMap::lastSeen() { return g_lastSeen; }
uint32_t TagMap::lastSeenSeq() { return g_lastSeq; }

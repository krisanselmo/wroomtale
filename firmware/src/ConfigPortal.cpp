#include "ConfigPortal.h"
#include "Config.h"
#include "Features.h"
#include "Battery.h"
#include "Buttons.h"
#include "Player.h"
#include "Power.h"
#include "Story.h"
#include "Rfid.h"
#include "TagMap.h"
#include "WifiCreds.h"
#include "PortalPage.h"
#include "Json.h"
#include "Nvs.h"
#include "Target.h"

#include <ESPmDNS.h>

#include <SD.h>
#include <SPI.h>
#include <WebServer.h>
#include <WiFi.h>
#include <vector>

namespace {
WebServer g_server(80);
String g_url;
bool g_station = false;
// The page polls, so this tracks whether anyone still has it open.
uint32_t g_lastTouch = 0;
// The main loop's work, run from every wait here: tags are read from the
// first second, not once the radio and the card scan are done.
std::function<void()> g_pump;

// Only what can be played: a folder of MP3s, or one holding a manifest.
constexpr uint8_t MAX_TARGETS = 40;
constexpr uint8_t MAX_DEPTH = 3;
// Listed on demand, one folder at a time, so this can be generous where the
// target list cannot: nothing is held between requests.
constexpr uint8_t MAX_TRACKS = 80;

void scanTargets(const String &path, uint8_t depth, String &json, uint8_t &found) {
	if (depth > MAX_DEPTH || found >= MAX_TARGETS) return;

	File dir = SD.open(path);
	if (!dir || !dir.isDirectory()) return;

	uint16_t tracks = 0;
	// Names only: openNextFile() opens every file just to say what it is.
	bool isDir = false;
	for (String entry = dir.getNextFileName(&isDir); entry.length();
	     entry = dir.getNextFileName(&isDir)) {
		if (g_pump) g_pump();
		const String name = Target::baseName(entry);
		if (name.startsWith(".") || name.equalsIgnoreCase("System Volume Information")) continue;

		if (isDir) {
			scanTargets(entry, depth + 1, json, found);
		} else if (Target::isMp3(name)) {
			tracks++;
		}
	}
	dir.close();

	if (path == "/" || found >= MAX_TARGETS) return;

	const bool story = Story::hasManifest(path);
	if (!story && !tracks) return;

	if (found) json += ',';
	json += "{\"chemin\":\"" + jsonEscape(path) + "\",\"type\":\"" +
	        (story ? "histoire" : "dossier") + "\",\"pistes\":" + String(tracks) + "}";
	found++;
}

// Once, not per poll: walking the card every 700 ms made the portal unreachable.
String g_targets;
// A card fuller than MAX_TARGETS loses its tail; say so rather than let the
// missing folders look like a card that was never written.
bool g_truncated = false;

void rescanTargets() {
	const uint32_t startedAt = millis();
	String json = "[";
	uint8_t found = 0;
	scanTargets("/", 0, json, found);
	g_truncated = found >= MAX_TARGETS;

#if FEAT_CLIPS
	// The boot jingle is a target too.
	if (found) json += ',';
	json += "{\"chemin\":\"" + String(Target::BUILTIN_PREFIX) + "boot\",\"type\":\"embarque\",\"pistes\":1}";
	found++;
#endif
	g_targets = json + "]";
	log_i("targets: %u found in %lu ms", found, (unsigned long)(millis() - startedAt));
}

// The amplifier is absent on purpose: I2S is write-only, so an unplugged
// MAX98357A looks exactly like a plugged one.
String hardwareJson() {
	String json = "{";

	const uint16_t rev = ESP.getChipRevision();
	json += "\"chip\":\"" + String(ESP.getChipModel()) + " v" + String(rev / 100) + "." +
	        (rev % 100 < 10 ? "0" : "") + String(rev % 100) + "\"";
	json += ",\"cores\":" + String(ESP.getChipCores());
	json += ",\"mhz\":" + String(getCpuFrequencyMhz());
	json += ",\"flashKB\":" + String(ESP.getFlashChipSize() / 1024);
	json += ",\"psramKB\":" + String(ESP.getPsramSize() / 1024);
	json += ",\"heapKB\":" + String(ESP.getFreeHeap() / 1024);
	json += ",\"blockKB\":" + String(ESP.getMaxAllocHeap() / 1024);

	json += ",\"rfid\":{\"ok\":" + String(Rfid::ready() ? "true" : "false") +
	        ",\"fw\":\"" + jsonEscape(Rfid::firmwareVersion()) + "\"}";

	const uint8_t cardType = SD.cardType();
	const char *typeName = "?";
	switch (cardType) {
	case CARD_NONE: typeName = "aucune"; break;
	case CARD_MMC: typeName = "MMC"; break;
	case CARD_SD: typeName = "SDSC"; break;
	case CARD_SDHC: typeName = "SDHC"; break;
	default: typeName = "inconnue"; break;
	}
	json += ",\"sd\":{\"ok\":" + String(cardType != CARD_NONE ? "true" : "false") +
	        ",\"type\":\"" + String(typeName) + "\"" +
	        ",\"sizeMB\":" + String((uint32_t)(SD.cardSize() / (1024ULL * 1024ULL))) +
	        ",\"usedMB\":" + String((uint32_t)(SD.usedBytes() / (1024ULL * 1024ULL))) + "}";

	const uint16_t battMv = Battery::millivolts();
	json += ",\"batt\":{\"ok\":" + String(Battery::present() ? "true" : "false") +
	        ",\"mv\":" + String(battMv) + ",\"pct\":" + String(Battery::percent()) +
	        ",\"low\":" + String(battMv && battMv < BATT_LOW_MV ? "true" : "false") + "}";

	json += ",\"net\":{\"mode\":\"" + String(g_station ? "station" : "point d'acces") +
	        "\",\"ssid\":\"" + jsonEscape(g_station ? WiFi.SSID() : String(AP_SSID)) +
	        "\",\"url\":\"" + jsonEscape(g_url) + "\"}";
	json += ",\"volume\":" + String(Player::volume());
	json += ",\"volumeMax\":" + String(VOLUME_MAX);
	json += ",\"volumeCap\":" + String(Player::volumeCap());
	json += ",\"volumeFloor\":" + String(Player::volumeFloor());
	json += ",\"playing\":\"" + jsonEscape(Player::currentTrack()) + "\"";
	json += ",\"live\":" + String(Player::isPlaying() ? "true" : "false");

	// Only the shape of the queue: the page fetches the tracks themselves from
	// /api/queue, and only when this says they changed.
	String folder;
	std::vector<String> tracks;
	int index = -1;
	Player::queue(folder, tracks, index);
	json += ",\"queue\":{\"folder\":\"" + jsonEscape(folder) + "\",\"index\":" + String(index) +
	        ",\"count\":" + String((unsigned)tracks.size()) + "}";
	json += ",\"sticky\":" + String(ConfigPortal::sticky() ? "true" : "false");
	json += ",\"idleMin\":" + String(Power::idleMinutes());
	json += ",\"btn\":[";
	for (uint8_t i = 0; i < 3; i++) {
		char hex[8];
		snprintf(hex, sizeof(hex), "#%06X", (unsigned)BTN_COLOUR[i]);
		json += String(i ? "," : "") + "{\"nom\":\"" + BTN_NAME[i] + "\",\"couleur\":\"" + hex + "\"}";
	}
	json += "]";
	json += ",\"sons\":[";
	bool firstSfx = true;
	for (uint8_t i = 0; i < static_cast<uint8_t>(Sfx::Count); i++) {
		const Sfx id = static_cast<Sfx>(i);
		if (id == Sfx::Test) continue;
		json += String(firstSfx ? "" : ",") + "{\"id\":\"" + sfxName(id) + "\",\"on\":" +
		        (Player::sfxEnabled(id) ? "true" : "false") + "}";
		firstSfx = false;
	}
	json += "]";
	json += ",\"btnDown\":" + String(Buttons::downMask());
	json += ",\"btnSeen\":" + String(Buttons::seenMask());
	return json + "}";
}

void handleState() {
	g_lastTouch = millis();
	String json = "{\"hw\":" + hardwareJson() + ",\"last\":\"" +
	              jsonEscape(TagMap::lastSeen()) + "\",\"lastSeq\":" +
	              String(TagMap::lastSeenSeq()) + ",\"tronque\":" +
	              (g_truncated ? "true" : "false") + ",\"folders\":" + g_targets + ",\"tags\":[";
	bool first = true;
	TagMap::forEach([&](const String &uid, const String &folder) {
		if (!first) json += ',';
		first = false;
		json += "{\"uid\":\"" + jsonEscape(uid) + "\",\"folder\":\"" + jsonEscape(folder) + "\"}";
	});
	json += "]}";
	g_server.send(200, "application/json", json);
}

void handleQueue() {
	g_lastTouch = millis();
	String folder;
	std::vector<String> tracks;
	int index = -1;
	Player::queue(folder, tracks, index);

	String json = "{\"folder\":\"" + jsonEscape(folder) + "\",\"index\":" + String(index) +
	              ",\"tracks\":[";
	for (size_t i = 0; i < tracks.size(); i++)
		json += String(i ? "," : "") + "\"" + jsonEscape(Target::baseName(tracks[i])) + "\"";
	g_server.send(200, "application/json", json + "]}");
}

void handleAssign() {
	const String uid = g_server.arg("uid");
	const String folder = g_server.arg("folder");
	if (uid.isEmpty() || folder.isEmpty()) {
		g_server.send(400, "text/plain", "uid and folder required");
		return;
	}
	g_server.send(TagMap::assign(uid, folder) ? 200 : 500, "text/plain", "");
}

void handleForget() {
	TagMap::forget(g_server.arg("uid"));
	g_server.send(200, "text/plain", "");
}

// --- File manager ---------------------------------------------------------
// One folder per request, streamed: nothing about the card is held between
// requests. The page sorts what it gets.
constexpr uint16_t MAX_FILES = 200;
// A deletion walks this deep at most, and refuses before touching anything
// rather than stop halfway down.
constexpr uint8_t MAX_DELETE_DEPTH = 6;

bool sdPresent() { return SD.cardType() != CARD_NONE; }

// Absolute, no parent step, no backslash: nothing outside the card's tree.
bool safePath(const String &p) {
	return p.length() && p.length() < 255 && p[0] == '/' && p.indexOf("..") < 0 && p.indexOf('\\') < 0;
}

String trimmed(String p) {
	while (p.length() > 1 && p.endsWith("/")) p.remove(p.length() - 1);
	return p;
}

String joinPath(const String &dir, const String &name) {
	return dir == "/" ? "/" + name : dir + "/" + name;
}

// Common to every route below: the card is there and each named path is sane.
bool filesGuard(std::initializer_list<const char *> args) {
	g_lastTouch = millis();
	if (!sdPresent()) {
		g_server.send(503, "text/plain", "no SD card");
		return false;
	}
	for (const char *a : args) {
		if (!safePath(g_server.arg(a))) {
			g_server.send(400, "text/plain", "bad path");
			return false;
		}
	}
	return true;
}

void handleFiles() {
	if (!filesGuard({"dir"})) return;
	const String dirPath = trimmed(g_server.arg("dir"));
	File dir = SD.open(dirPath);
	if (!dir || !dir.isDirectory()) {
		g_server.send(404, "text/plain", "no such folder");
		return;
	}

	g_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
	g_server.send(200, "application/json", "");
	String chunk = "{\"dir\":\"" + jsonEscape(dirPath) + "\",\"entries\":[";
	uint16_t count = 0;
	bool truncated = false;
	while (File entry = dir.openNextFile()) {
		const String name = Target::baseName(entry.name());
		const bool isDir = entry.isDirectory();
		const uint32_t size = isDir ? 0 : entry.size();
		entry.close();
		if (name.startsWith(".")) continue;
		if (count >= MAX_FILES) {
			truncated = true;
			break;
		}
		chunk += String(count ? "," : "") + "{\"nom\":\"" + jsonEscape(name) + "\",\"dossier\":" +
		         (isDir ? "true" : "false") + ",\"taille\":" + String(size) + "}";
		count++;
		if (chunk.length() > 1024) {
			g_server.sendContent(chunk);
			chunk = "";
		}
	}
	dir.close();
	chunk += String("],\"tronque\":") + (truncated ? "true" : "false") + "}";
	g_server.sendContent(chunk);
	g_server.sendContent("");
}

// The upload lands in a .part file renamed at the end: a dropped connection
// never leaves a truncated MP3 that looks whole.
File g_upload;
String g_uploadPath;
bool g_uploadFailed = false;

void discardUpload() {
	if (g_upload) g_upload.close();
	if (g_uploadPath.length()) SD.remove(g_uploadPath + ".part");
	g_uploadPath = "";
	g_uploadFailed = true;
}

void handleUploadChunk() {
	HTTPUpload &up = g_server.upload();
	g_lastTouch = millis();
	switch (up.status) {
	case UPLOAD_FILE_START: {
		const String dir = trimmed(g_server.arg("dir"));
		const String name = Target::baseName(up.filename);
		g_uploadPath = "";
		g_uploadFailed = false;
		if (!sdPresent() || !safePath(dir) || name.isEmpty() || name.startsWith(".") ||
		    !safePath(joinPath(dir, name))) {
			g_uploadFailed = true;
			return;
		}
		// The decoder and this writer would share the SPI bus and both stutter.
		Player::stop();
		g_uploadPath = joinPath(dir, name);
		g_upload = SD.open(g_uploadPath + ".part", FILE_WRITE);
		if (!g_upload) discardUpload();
		else log_i("upload: %s", g_uploadPath.c_str());
		break;
	}
	case UPLOAD_FILE_WRITE:
		if (g_upload && g_upload.write(up.buf, up.currentSize) != up.currentSize) {
			log_w("upload: write failed, card full?");
			discardUpload();
		}
		break;
	case UPLOAD_FILE_END:
		if (!g_upload) break;
		g_upload.close();
		if (SD.exists(g_uploadPath)) SD.remove(g_uploadPath);
		if (!SD.rename(g_uploadPath + ".part", g_uploadPath)) discardUpload();
		else log_i("upload: %s, %u bytes", g_uploadPath.c_str(), (unsigned)up.totalSize);
		g_uploadPath = "";
		break;
	case UPLOAD_FILE_ABORTED:
		log_w("upload: aborted");
		discardUpload();
		break;
	}
}

void handleUploadDone() {
	g_lastTouch = millis();
	if (!sdPresent()) return (void)g_server.send(503, "text/plain", "no SD card");
	if (!safePath(g_server.arg("dir"))) return (void)g_server.send(400, "text/plain", "bad path");
	if (g_uploadFailed) return (void)g_server.send(500, "text/plain", "upload failed");
	rescanTargets();
	g_server.send(200, "text/plain", "");
}

void handleMkdir() {
	if (!filesGuard({"path"})) return;
	const String path = trimmed(g_server.arg("path"));
	if (SD.exists(path)) return (void)g_server.send(409, "text/plain", "already there");
	g_server.send(SD.mkdir(path) ? 200 : 409, "text/plain", "");
}

void handleRename() {
	if (!filesGuard({"from", "to"})) return;
	const String from = trimmed(g_server.arg("from"));
	const String to = trimmed(g_server.arg("to"));
	if (from == "/" || !SD.exists(from) || SD.exists(to) || to.startsWith(from + "/"))
		return (void)g_server.send(409, "text/plain", "");
	// A track that moves under the decoder's feet is a read error mid-frame.
	Player::stop();
	if (!SD.rename(from, to)) return (void)g_server.send(409, "text/plain", "");
	rescanTargets();
	g_server.send(200, "text/plain", "");
}

bool tooDeep(const String &path, uint8_t depth) {
	if (depth > MAX_DELETE_DEPTH) return true;
	File dir = SD.open(path);
	if (!dir || !dir.isDirectory()) return false;
	bool isDir = false;
	bool deep = false;
	for (String e = dir.getNextFileName(&isDir); e.length() && !deep; e = dir.getNextFileName(&isDir)) {
		if (isDir) deep = tooDeep(e, depth + 1);
	}
	dir.close();
	return deep;
}

// The first entry each time, the folder reopened: no listing is walked while
// its own entries disappear.
bool removeTree(const String &path) {
	File f = SD.open(path);
	if (!f) return false;
	if (!f.isDirectory()) {
		f.close();
		return SD.remove(path);
	}
	for (;;) {
		bool isDir = false;
		const String e = f.getNextFileName(&isDir);
		f.close();
		if (e.isEmpty()) break;
		if (!(isDir ? removeTree(e) : SD.remove(e))) return false;
		if (g_pump) g_pump();
		f = SD.open(path);
		if (!f) return false;
	}
	return SD.rmdir(path);
}

void handleDelete() {
	if (!filesGuard({"path"})) return;
	const String path = trimmed(g_server.arg("path"));
	if (path == "/") return (void)g_server.send(400, "text/plain", "not the root");
	if (!SD.exists(path)) return (void)g_server.send(404, "text/plain", "");
	if (tooDeep(path, 0)) return (void)g_server.send(507, "text/plain", "too deep");
	Player::stop();
	const bool ok = removeTree(path);
	rescanTargets();
	g_server.send(ok ? 200 : 500, "text/plain", "");
}
} // namespace

bool ConfigPortal::sticky() { return Nvs("portal", true)->getBool("sticky", CONFIG_STICKY_DEFAULT); }
void ConfigPortal::setSticky(bool on) { Nvs("portal", false)->putBool("sticky", on); }

void ConfigPortal::run(const std::function<void()> &pump) {
	g_pump = pump;
	SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
	if (!SD.begin(PIN_SD_CS)) log_e("SD mount failed, folder list will be empty");

	// Joining keeps the phone on its own WiFi; the AP is the fallback.
	const String home = WifiCreds::ssid();
	if (home.length()) {
		log_i("joining \"%s\"...", home.c_str());
		WiFi.mode(WIFI_STA);
		WiFi.setHostname(MDNS_NAME);
		WiFi.begin(home.c_str(), WifiCreds::password().c_str());

		const uint32_t deadline = millis() + STA_CONNECT_TIMEOUT_MS;
		while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
			if (pump) pump();
			delay(5);
		}
		g_station = WiFi.status() == WL_CONNECTED;
		if (!g_station) log_w("could not join \"%s\", falling back to access point", home.c_str());
	}

	if (g_station) {
		g_url = "http://" + WiFi.localIP().toString();
		if (MDNS.begin(MDNS_NAME)) {
			MDNS.addService("http", "tcp", 80);
			log_i("also reachable at http://%s.local", MDNS_NAME);
		}
	} else {
		WiFi.mode(WIFI_AP);
		// WPA2 wants 8 characters at least; a shorter one would open the AP.
		const bool custom = WifiCreds::hasApPassword();
		const String secret = custom ? WifiCreds::apPassword() : String(AP_DEFAULT_PASS);
		WiFi.softAP(AP_SSID, secret.c_str());
		if (!custom) log_w("access point on the default password, change it with `appass <secret>`");
		g_url = "http://" + WiFi.softAPIP().toString();
	}
	log_i("config portal at %s (%s)", g_url.c_str(),
	      g_station ? home.c_str() : AP_SSID);
	log_i("hw: %s", hardwareJson().c_str());
	log_i("heap after radio up: %lu KB free, largest block %lu KB",
	      (unsigned long)(ESP.getFreeHeap() / 1024),
	      (unsigned long)(ESP.getMaxAllocHeap() / 1024));

	g_server.on("/", HTTP_GET, [] {
		g_server.sendHeader("Content-Encoding", "gzip");
		g_server.send_P(200, "text/html", (PGM_P)PORTAL_PAGE_GZ, PORTAL_PAGE_GZ_LEN);
	});
	g_server.on("/api/state", HTTP_GET, handleState);
	// The tracks of one folder, so a tag can name a single sound instead of the
	// whole playlist around it. Read on demand: never held between requests.
	g_server.on("/api/tracks", HTTP_GET, [] {
		g_lastTouch = millis();
		const String folder = g_server.arg("folder");
		if (folder.isEmpty()) return g_server.send(400, "text/plain", "folder required");

		String json = "{\"folder\":\"" + jsonEscape(folder) + "\",\"tracks\":[";
		uint8_t found = 0;
		File dir = SD.open(folder);
		if (dir && dir.isDirectory()) {
			while (File entry = dir.openNextFile()) {
				const String name = Target::baseName(entry.name());
				const bool isDir = entry.isDirectory();
				entry.close();
				if (isDir || name.startsWith(".") || !Target::isMp3(name)) continue;
				if (found >= MAX_TRACKS) break;
				json += String(found ? "," : "") + "\"" + jsonEscape(name) + "\"";
				found++;
			}
			dir.close();
		}
		g_server.send(200, "application/json", json + "]}");
	});
	g_server.on("/api/queue", HTTP_GET, handleQueue);
	g_server.on("/api/assign", HTTP_POST, handleAssign);
	// Auditioning from the panel: a folder, optionally at one of its tracks.
	g_server.on("/api/play", HTTP_POST, [] {
		const String target = g_server.arg("folder");
		if (target.isEmpty()) return g_server.send(400, "text/plain", "folder required");
		if (Target::isFile(target)) Player::playFile(target);
		else Player::playFolderAt(target, (uint16_t)g_server.arg("i").toInt());
		g_server.send(200, "text/plain", "");
	});
	g_server.on("/api/stop", HTTP_POST, [] {
		Player::stop();
		g_server.send(200, "text/plain", "");
	});
	g_server.on("/api/forget", HTTP_POST, handleForget);
	g_server.on("/api/rescan", HTTP_POST, [] {
		rescanTargets();
		g_server.send(200, "text/plain", "");
	});
	g_server.on("/api/volume", HTTP_POST, [] {
		if (g_server.arg("d") == "1") Player::volumeUp();
		else Player::volumeDown();
		g_server.send(200, "text/plain", "");
	});
	// The two ends of what the buttons and this panel can reach: a ceiling for
	// small ears, a floor to calibrate the quietest audible step.
	g_server.on("/api/volcap", HTTP_POST, [] {
		const int cap = g_server.arg("v").toInt();
		if (cap < VOLUME_MIN || cap > VOLUME_MAX)
			return g_server.send(400, "text/plain", "v out of range");
		Player::setVolumeCap((uint8_t)cap);
		g_server.send(200, "text/plain", "");
	});
	g_server.on("/api/volfloor", HTTP_POST, [] {
		const int floor = g_server.arg("v").toInt();
		if (floor < VOLUME_MIN || floor > VOLUME_MAX)
			return g_server.send(400, "text/plain", "v out of range");
		Player::setVolumeFloor((uint8_t)floor);
		g_server.send(200, "text/plain", "");
	});
	// Switching a sound back on plays it, so the panel says which one it was.
	g_server.on("/api/sfx", HTTP_POST, [] {
		const Sfx id = sfxFromName(g_server.arg("id").c_str());
		if (id == Sfx::Count || id == Sfx::Test) return g_server.send(400, "text/plain", "unknown id");
		const bool on = g_server.arg("on") == "1";
		Player::setSfxEnabled(id, on);
		if (on) Player::play(id);
		g_server.send(200, "text/plain", "");
	});
	g_server.on("/api/sticky", HTTP_POST, [] {
		ConfigPortal::setSticky(g_server.arg("on") == "1");
		g_server.send(200, "text/plain", "");
	});
	// The panel mirrors the three physical buttons; this drives them remotely.
	g_server.on("/api/transport", HTTP_POST, [] {
		const String cmd = g_server.arg("cmd");
		if (cmd == "prev") Player::prev();
		else if (cmd == "next") Player::next();
		else if (cmd == "play") Player::togglePause();
		else return g_server.send(400, "text/plain", "");
		g_server.send(200, "text/plain", "");
	});
	g_server.on("/api/idle", HTTP_POST, [] {
		const int v = g_server.arg("v").toInt();
		if (g_server.arg("v").isEmpty() || v < 0 || v > IDLE_SLEEP_MAX_MIN)
			return g_server.send(400, "text/plain", "v out of range");
		Power::setIdleMinutes((uint16_t)v);
		g_server.send(200, "text/plain", "");
	});
	g_server.on("/api/files", HTTP_GET, handleFiles);
	g_server.on("/api/upload", HTTP_POST, handleUploadDone, handleUploadChunk);
	g_server.on("/api/mkdir", HTTP_POST, handleMkdir);
	g_server.on("/api/rename", HTTP_POST, handleRename);
	g_server.on("/api/delete", HTTP_POST, handleDelete);
	g_server.on("/api/btnreset", HTTP_POST, [] {
		Buttons::clearSeen();
		g_server.send(200, "text/plain", "");
	});
	rescanTargets();
	g_server.begin();

	uint32_t idleSince = millis();
	g_lastTouch = 0;
	uint8_t lastStations = 0;

	while (millis() - idleSince < CONFIG_PORTAL_TIMEOUT_MS) {
		g_server.handleClient();
		if (pump) pump(); // console, buttons, tags, and the self-test stop

		// Must not fire while someone is using the portal.
		const uint8_t stations = WiFi.softAPgetStationNum();
		if (stations > 0 || millis() - g_lastTouch < PORTAL_TOUCH_GRACE_MS) idleSince = millis();
		if (!g_station && stations != lastStations) {
			log_i("%u client(s) connected", stations);
			lastStations = stations;
		}
		delay(2);
	}

	log_i("config portal timed out, rebooting");
	ESP.restart();
}

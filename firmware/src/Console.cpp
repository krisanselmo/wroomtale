#include "Console.h"
#include "Battery.h"
#include "BattLog.h"
#include "Box.h"
#include "Bt.h"
#include "ButtonScheme.h"
#include "Config.h"
#include "ConfigPortal.h"
#include "Leds.h"
#include "Player.h"
#include "Rfid.h"
#include "StoryRunner.h"
#include "TagMap.h"
#include "WifiCreds.h"

#include <Arduino.h>
#include <WiFi.h>

namespace {

// `args` set: the command also takes the rest of the line, after a space.
// The help is printed from this table, so it cannot drift from the commands.
struct Command {
	const char *name;
	const char *args;
	const char *help;
	void (*run)(const String &arg);
};

bool parseOnOff(const String &arg, bool &on) {
	if (arg != "on" && arg != "off") return false;
	on = arg == "on";
	return true;
}

void audioLevel(const String &) {
	uint8_t peak = 0;
	for (int i = 0; i < 40; i++) { // ~1 s of sampling
		const uint8_t l = Player::audioLevel();
		if (l > peak) peak = l;
		delay(25);
	}
	log_i("audio peak over 1 s: %u / 255", peak);
}

void battery(const String &arg) {
	if (arg.startsWith("cal ")) { Battery::calibrate((uint16_t)arg.substring(4).toInt()); return; }
	if (arg == "clear") { Battery::clearCalibration(); log_i("battery: calibration cleared"); return; }
	log_i("battery: raw %u mV, filtered %u mV, %u %%, trim %.4f%s",
	      Battery::rawMillivolts(), Battery::millivolts(), Battery::percent(),
	      Battery::trim(), Battery::present() ? "" : "  -- out of range, divider missing?");
}

void battLog(const String &arg) {
	if (arg.isEmpty()) {
		log_i("battlog: %s, %lu entries written, buffer %u/%u B",
		      Player::sdReady() ? "active" : "inactive (no SD)",
		      (unsigned long)BattLog::entriesWritten(),
		      (unsigned)BattLog::bufferedBytes(),
		      (unsigned)BattLog::bufferCapacity());
		return;
	}
	if (!Player::sdReady()) { log_w("battlog: no SD card"); return; }
	if (arg == "flush") {
		if (BattLog::flush()) log_i("battlog: flushed to SD");
		else log_w("battlog: flush failed");
	} else if (arg == "clear") {
		if (BattLog::clear()) log_i("battlog: /logs/batt.csv cleared");
		else log_w("battlog: clear failed");
	}
}

void rfid(const String &arg) {
	bool on;
	if (parseOnOff(arg, on)) { Rfid::setVerbose(on); return; }
	uint32_t polls = 0, hits = 0, changes = 0;
	Rfid::stats(polls, hits, changes);
	log_i("rfid: %lu polls, %lu detections, %lu changes, current card \"%s\"",
	      (unsigned long)polls, (unsigned long)hits, (unsigned long)changes,
	      Rfid::current().c_str());
}

void story(const String &arg) {
	if (arg.isEmpty()) { StoryRunner::logStatus(); return; }
	if (arg == "stop") { StoryRunner::stop(); return; }
	if (arg != "demo" && !arg.startsWith("/")) { log_w("usage: story [demo|/folder|stop]"); return; }
	Player::stop();
	const bool ok = arg == "demo" ? StoryRunner::startDemo() : StoryRunner::startFromSd(arg);
	if (!ok) Player::play(Sfx::Error);
}

void ledColours() {
	// Each colour redrawn: one corrupted frame stops masking the rest.
	log_i("led: red, green, blue, white -- 3 s each");
	const uint8_t seq[4][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}};
	for (const auto &c : seq) {
		for (int k = 0; k < 30; k++) { Leds::solid(c[0], c[1], c[2]); delay(100); }
	}
	Leds::solid(0, 0, 0);
	log_i("led: %lu refreshes since boot", (unsigned long)Leds::ticks());
}

void ledSweep(uint32_t stepMs, bool announce) {
	for (uint8_t i = 0; i < LED_COUNT; i++) {
		if (announce) log_i("led %u", i);
		Leds::one(i, 255, 255, 255);
		delay(stepMs);
	}
	Leds::solid(0, 0, 0);
}

void led(const String &arg) {
	if (arg == "boot") Leds::event(LedEvent::Boot);
	else if (arg == "ok") Leds::event(LedEvent::TagOk);
	else if (arg == "ko") Leds::event(LedEvent::TagUnknown);
	else if (arg == "vol") Leds::event(LedEvent::Volume);
	else if (arg == "test") ledColours();
	else if (arg == "chase") {
		// A lone LED lighting anywhere means the data reaches it.
		log_i("led: sweep, one LED at a time");
		ledSweep(700, true);
	} else if (arg == "nowifi") {
		// WiFi disturbs the RMT refill.
		log_i("led: WiFi off, sweep");
		WiFi.mode(WIFI_OFF);
		delay(200);
		ledSweep(500, false);
		log_i("led: reboot the board to get the portal back");
	} else if (arg.startsWith("count ")) {
		Leds::setActive((uint8_t)arg.substring(6).toInt());
		Leds::solid(0, 200, 0);
	} else if (arg.startsWith("bright ")) {
		Leds::setBrightness((uint8_t)arg.substring(7).toInt());
		Leds::solid(255, 255, 255);
	} else {
		log_w("usage: led test|chase|nowifi|count <n>|bright <0-255>|boot|ok|ko|vol");
	}
}

void listTags(const String &) {
	TagMap::forEach([](const String &u, const String &t) { log_i("  %s -> %s", u.c_str(), t.c_str()); });
}

void bind(const String &arg) {
	const int sp = arg.indexOf(' ');
	if (sp < 0) { log_w("usage: bind <uid> <folder|file|builtin:boot>"); return; }
	const String uid = arg.substring(0, sp);
	const String target = arg.substring(sp + 1);
	log_i("%s: %s -> %s", TagMap::assign(uid, target) ? "bound" : "FAILED",
	      uid.c_str(), target.c_str());
}

void configAtBoot(const String &arg) {
	bool on;
	if (!parseOnOff(arg, on)) { log_w("usage: config on|off"); return; }
	ConfigPortal::setSticky(on);
	log_i("config mode at boot %s", on ? "enabled" : "disabled");
}

void volumeLimit(const String &arg, bool ceiling) {
	const int v = arg.toInt();
	if (arg.isEmpty() || v < VOLUME_MIN || v > VOLUME_MAX) {
		log_w("%s wants %u to %u, got \"%s\"", ceiling ? "volmax" : "volmin",
		      VOLUME_MIN, VOLUME_MAX, arg.c_str());
		return;
	}
	if (ceiling) Player::setVolumeCap((uint8_t)v);
	else Player::setVolumeFloor((uint8_t)v);
	// Read back: the floor never passes the ceiling, so the two settle.
	delay(30);
	log_i("volume limits now %u..%u of %u", Player::volumeFloor(),
	      Player::volumeCap(), VOLUME_MAX);
}

void selfTest(const String &arg) {
	bool on;
	if (!parseOnOff(arg, on)) { log_w("usage: selftest on|off"); return; }
	Player::setSelfTestEnabled(on);
	log_i("boot self-test %s", on ? "enabled" : "disabled");
}

void apPassword(const String &arg) {
	if (arg == "clear") {
		WifiCreds::setApPassword("");
		log_i("access point back to its default password");
		return;
	}
	if (arg.length() < 8 || arg.length() > 63) {
		log_w("WPA2 wants 8 to 63 characters, got %u", arg.length());
		return;
	}
	WifiCreds::setApPassword(arg);
	log_i("access point password saved (%u characters)", arg.length());
}

void wifi(const String &arg) {
	if (arg == "clear") { WifiCreds::clear(); log_i("credentials cleared"); return; }
	log_i("SSID \"%s\", password %s", WifiCreds::ssid().c_str(),
	      WifiCreds::hasPassword() ? "set" : "MISSING");
	log_i("access point \"%s\", %s password", AP_SSID,
	      WifiCreds::hasApPassword() ? "own" : "default");
	log_i("reboot and press a key during the first %lu ms to join it",
	      (unsigned long)CONSOLE_ESCAPE_MS);
}

void help(const String &);

const Command COMMANDS[] = {
	{"p", nullptr, "play/pause", [](const String &) { Box::press(BtnEvent::PlayShort); }},
	{"pp", nullptr, "double click on green", [](const String &) { Box::press(BtnEvent::PlayDouble); }},
	{"n", nullptr, "next", [](const String &) { Box::press(BtnEvent::NextShort); }},
	{"b", nullptr, "previous", [](const String &) { Box::press(BtnEvent::PrevShort); }},
	{"s", nullptr, "stop", [](const String &) { Box::press(BtnEvent::PlayLong); }},
	{"+", nullptr, "volume up", [](const String &) { Box::press(BtnEvent::NextLong); }},
	{"-", nullptr, "volume down", [](const String &) { Box::press(BtnEvent::PrevLong); }},
	{"o", nullptr, "boot sound", [](const String &) { Player::play(Sfx::Boot); }},
	{"e", nullptr, "error sound", [](const String &) { Player::play(Sfx::Error); }},
	{"g", nullptr, "tag sound", [](const String &) { Player::play(Sfx::Tag); }},
	{"t", nullptr, "1 kHz tone", [](const String &) { Player::play(Sfx::Test); }},
	{"level", nullptr, "audio peak over one second", audioLevel},
	{"tags", nullptr, "list tag bindings", listTags},
	{"bind", "<uid> <target>", "bind a tag to a folder, a file or builtin:boot", bind},
	{"tag", "<uid>", "act as if a tag were presented", Box::presentTag},
	{"play", "<target>", "play without a tag", Box::playTarget},
	{"story", "[demo|/folder|stop]", "story status, demo, start or stop", story},
	{"batt", "[cal <mV>|clear]", "cell reading, multimeter calibration", battery},
	{"battlog", "[flush|clear]", "battery log on the SD card", battLog},
	{"volmax", "<0-21>", "volume ceiling (NVS)", [](const String &a) { volumeLimit(a, true); }},
	{"volmin", "<0-21>", "volume floor (NVS)", [](const String &a) { volumeLimit(a, false); }},
	{"selftest", "on|off", "audio self-test at boot (NVS)", selfTest},
	{"config", "on|off", "boot straight into config mode (NVS)", configAtBoot},
	{"ssid", "<name>", "home WiFi network",
	 [](const String &a) { WifiCreds::setSsid(a); log_i("SSID saved: \"%s\"", WifiCreds::ssid().c_str()); }},
	{"pass", "<secret>", "home WiFi password, never logged",
	 [](const String &a) { WifiCreds::setPassword(a); log_i("password saved (%u characters)", a.length()); }},
	{"appass", "<secret>|clear", "access point password, 8 to 63 characters", apPassword},
	{"wifi", "[clear]", "saved network, or forget it", wifi},
	{"led", "<test|chase|nowifi|count n|bright n>", "LED strip diagnostics", led},
	{"rfid", "[on|off]", "reader counters, or verbose log", rfid},
	{"i2c", nullptr, "scan the I2C bus", [](const String &) { Rfid::scanI2c(); }},
	{"bt", "[...]", "A2DP spike, bt env only", Bt::command},
	{"r", nullptr, "reboot", [](const String &) { log_i("rebooting"); delay(50); ESP.restart(); }},
	{"?", nullptr, "this help and the box state", help},
};

void help(const String &) {
	for (const Command &c : COMMANDS) {
		const String usage = c.args ? String(c.name) + " " + c.args : String(c.name);
		log_i("  %-28s %s", usage.c_str(), c.help);
	}
	log_i("buttons: %s", ButtonScheme::name());
	log_i("volume %u/%u (limits %u..%u), self-test %s, config at boot %s",
	      Player::volume(), VOLUME_MAX, Player::volumeFloor(), Player::volumeCap(),
	      Player::selfTestEnabled() ? "on" : "off", ConfigPortal::sticky() ? "on" : "off");
	const String batt = Battery::present()
	                        ? String(Battery::millivolts()) + " mV / " + Battery::percent() + " %"
	                        : String("not read");
	log_i("SD %s, heap %lu KB, battery %s", Player::sdReady() ? "mounted" : "absent",
	      (unsigned long)(ESP.getFreeHeap() / 1024), batt.c_str());
}

void run(const String &line) {
	for (const Command &c : COMMANDS) {
		if (line != c.name) continue;
		// "<...>" is a required argument: a bare "pass" must not save an empty one.
		if (c.args && c.args[0] == '<') log_w("usage: %s %s", c.name, c.args);
		else c.run(String());
		return;
	}
	for (const Command &c : COMMANDS) {
		const size_t len = strlen(c.name);
		if (c.args && line.length() > len && line[len] == ' ' && line.startsWith(c.name)) {
			c.run(line.substring(len + 1));
			return;
		}
	}
	help(String());
}

} // namespace

void Console::poll() {
	static String line;
	while (Serial.available()) {
		const char c = Serial.read();
		if (c == '\r' || c == '\n') {
			if (line.length()) { run(line); line = ""; }
			continue;
		}
		line += c;
		if (line.length() > 128) line = ""; // stray binary, drop it
	}
}

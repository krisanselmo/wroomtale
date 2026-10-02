#include "Rfid.h"
#include "Config.h"
#include <Adafruit_PN532.h>
#include <Wire.h>

namespace {
Adafruit_PN532 g_nfc(PIN_PN532_IRQ, PIN_PN532_RESET, &Wire);
String g_current;
bool g_ready = false;
String g_firmware;
uint32_t g_lastPoll = 0;
uint8_t g_missCount = 0;
uint32_t g_polls = 0, g_hits = 0, g_changes = 0;
bool g_verbose = false;

// A single miss is normal while a card moves.
constexpr uint8_t MISS_THRESHOLD = 3;

String toHex(const uint8_t *uid, uint8_t len) {
	String out;
	out.reserve(len * 2);
	for (uint8_t i = 0; i < len; i++) {
		if (uid[i] < 0x10) out += '0';
		out += String(uid[i], HEX);
	}
	return out;
}
} // namespace

bool Rfid::begin() {
	// Adafruit_PN532::begin() calls Wire.begin(); twice leaves the bus in
	// ESP_ERR_INVALID_STATE.
	Wire.setPins(PIN_I2C_SDA, PIN_I2C_SCL);
	g_nfc.begin();

	const uint32_t version = g_nfc.getFirmwareVersion();
	if (!version) {
		log_e("PN532 not found on I2C");
		return false;
	}
	g_ready = true;
	g_firmware = String((version >> 16) & 0xFF) + "." + String((version >> 8) & 0xFF);
	log_i("PN532 firmware %s", g_firmware.c_str());

	g_nfc.SAMConfig();
	return true;
}

String Rfid::poll() {
	if (!g_ready) return "";

	const uint32_t now = millis();
	if (now - g_lastPoll < RFID_POLL_MS) return "";
	g_lastPoll = now;
	g_polls++;

	uint8_t uid[7] = {0};
	uint8_t uidLen = 0;

	const bool found = g_nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 50);
	if (!found) {
		if (!g_current.isEmpty() && ++g_missCount >= MISS_THRESHOLD) {
			if (g_verbose) log_i("rfid: card %s removed", g_current.c_str());
			g_current = "";
			g_missCount = 0;
		}
		return "";
	}

	g_missCount = 0;
	g_hits++;
	const String seen = toHex(uid, uidLen);
	if (seen == g_current) {
		if (g_verbose) log_i("rfid: same card %s", seen.c_str());
		return "";
	}

	g_changes++;
	if (g_verbose) log_i("rfid: NEW card %s (was \"%s\")", seen.c_str(), g_current.c_str());
	g_current = seen;
	return seen;
}

void Rfid::scanI2c() {
	// No pull-up reads low then high; a line stuck low reads low in both.
	Wire.end();
	for (int pass = 0; pass < 2; pass++) {
		const int mode = pass ? INPUT_PULLUP : INPUT;
		pinMode(PIN_I2C_SDA, mode);
		pinMode(PIN_I2C_SCL, mode);
		delay(5);
		log_i("i2c idle (%s): SDA=%s SCL=%s", pass ? "internal pull-up" : "no pull-up",
		      digitalRead(PIN_I2C_SDA) ? "HIGH" : "LOW",
		      digitalRead(PIN_I2C_SCL) ? "HIGH" : "LOW");
	}

	Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

	uint8_t found = 0;
	for (uint8_t addr = 1; addr < 127; addr++) {
		Wire.beginTransmission(addr);
		if (Wire.endTransmission() == 0) {
			log_i("i2c: device at 0x%02X%s", addr, addr == 0x24 ? "  <- PN532" : "");
			found++;
		}
	}
	log_i("i2c: %u device(s) on SDA=%d SCL=%d", found, PIN_I2C_SDA, PIN_I2C_SCL);
	if (!found) log_w("i2c: nothing answered -- check the mode switches, the pull-ups and SDA/SCL");
}

String Rfid::current() { return g_current; }
bool Rfid::ready() { return g_ready; }
String Rfid::firmwareVersion() { return g_firmware; }

void Rfid::stats(uint32_t &polls, uint32_t &hits, uint32_t &changes) {
	polls = g_polls; hits = g_hits; changes = g_changes;
}
void Rfid::setVerbose(bool on) { g_verbose = on; log_i("rfid verbose %s", on ? "on" : "off"); }

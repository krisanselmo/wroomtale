#include "WifiCreds.h"
#include "Nvs.h"

namespace {
constexpr char NS[] = "wifi";

String read(const char *key) { return Nvs(NS, true)->getString(key, ""); }
void write(const char *key, const String &value) { Nvs(NS, false)->putString(key, value); }
} // namespace

String WifiCreds::ssid() { return read("ssid"); }
String WifiCreds::password() { return read("pass"); }
bool WifiCreds::hasPassword() { return read("pass").length() > 0; }
void WifiCreds::setSsid(const String &value) { write("ssid", value); }
void WifiCreds::setPassword(const String &value) { write("pass", value); }

String WifiCreds::apPassword() { return read("appass"); }
bool WifiCreds::hasApPassword() { return read("appass").length() >= 8; }
void WifiCreds::setApPassword(const String &value) { write("appass", value); }

void WifiCreds::clear() { Nvs(NS, false)->clear(); }

#pragma once
#include <Arduino.h>
#include "Features.h"

// Home-network credentials, so the portal can be reached without leaving that
// network for the board's own access point.
namespace WifiCreds {
#if FEAT_PORTAL
String ssid();
bool hasPassword();
void setSsid(const String &value);
void setPassword(const String &value);
void clear();

// Password is only ever handed to WiFi.begin(), never logged or served.
String password();

// SoftAP password in NVS. Shorter than the 8 characters WPA2 needs counts as unset.
String apPassword();
bool hasApPassword();
void setApPassword(const String &value);
#else
inline String ssid() { return String(); }
inline String password() { return String(); }
inline bool hasPassword() { return false; }
inline void setSsid(const String &) {}
inline void setPassword(const String &) {}
inline String apPassword() { return String(); }
inline bool hasApPassword() { return false; }
inline void setApPassword(const String &) {}
inline void clear() {}
#endif
} // namespace WifiCreds

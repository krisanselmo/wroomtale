#pragma once
#include <Arduino.h>
#include "Features.h"

// Spike, not a feature. Answers two questions and nothing else: how much heap
// survives Bluedroid coming up as an A2DP source, and whether BT discovery
// still works with the WiFi radio on. Nothing is ever connected and no audio
// is ever streamed -- see the PR for why that is the useful measurement.
namespace Bt {

struct Peer {
	String name;
	String mac;
	int rssi;
};

#if FEAT_BT
// Brings the stack up. With an address, arms auto-reconnect before start so a
// speaker that is merely on -- not in pairing mode -- is picked up again.
bool up(const char *mac = nullptr);

// releaseMemory frees the BT controller memory for good: nothing can bring
// the stack back up afterwards, so it measures the floor, not the steady state.
void down(bool releaseMemory = false);

bool isUp();
bool scanning();

// Harvested by the discovery pass, strongest first.
size_t peers(Peer *out, size_t max);

// Does a decoder-free feed fit next to Bluedroid? Allocates the ring buffer an
// A2DP callback would need, then streams a file off the card into it at the
// 44.1k stereo rate and throws it away. Measures the path, not the sound.
bool probeFeed(const char *path, size_t ringBytes, uint32_t ms);

// Connects to the first discovered device whose name contains this, then
// feeds it. Until one is set the discovery pass only harvests.
void setTarget(const char *nameFragment);
bool isConnected();

// Straight to an address, no inquiry: what the portal would do with a MAC
// picked once and kept in NVS.
bool connectTo(const char *mac);

// What the data callback hands Bluedroid to encode: silence, or a quiet sine
// so the link can be heard, not just measured.
void setTone(bool on);

// Call rate and block sizes the sink actually asked for.
void feedStats();

// The console's "bt ..." commands, prefix stripped.
void command(const String &arg);

// Streams a headerless PCM file -- signed 16 bit little endian, 22050 Hz,
// mono, exactly what the stories already are -- to the connected speaker.
// A task fills a ring off the card; the A2DP callback drains it and doubles
// each sample to 44.1k stereo, which is all the conversion A2DP needs.
bool playFile(const char *path);
void stopFile();
#else
inline bool up(const char * = nullptr) { return false; }
inline void down(bool = false) {}
inline bool isUp() { return false; }
inline bool scanning() { return false; }
inline size_t peers(Peer *, size_t) { return 0; }
inline bool probeFeed(const char *, size_t, uint32_t) { return false; }
inline void setTarget(const char *) {}
inline bool isConnected() { return false; }
inline bool connectTo(const char *) { return false; }
inline void setTone(bool) {}
inline void feedStats() {}
inline void command(const String &) { log_w("built without FEAT_BT: use the bt env"); }
inline bool playFile(const char *) { return false; }
inline void stopFile() {}
#endif

} // namespace Bt

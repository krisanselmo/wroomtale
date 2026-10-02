#include "Bt.h"
#if FEAT_BT

#include <BluetoothA2DPSource.h>
#include <SD.h>
#include <WiFi.h>
#include <esp_gap_bt_api.h>
#include <freertos/stream_buffer.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <vector>

namespace {

BluetoothA2DPSource g_source;
bool g_up = false;
String g_target;
bool g_tone = false;
volatile bool g_tearing = false;

// Single producer, single consumer: the reader task fills, the A2DP callback
// drains. No mutex on the audio path.
// 22050 mono is 44100 B/s, so 8 KB is 186 ms of slack -- room for an SD
// latency spike, and 8 KB back on a heap that measured 4.9 KB free.
constexpr size_t RING_BYTES = 8 * 1024;
StreamBufferHandle_t g_ring = nullptr;
TaskHandle_t g_reader = nullptr;
volatile bool g_stop = false;
volatile uint32_t g_starved = 0;
String g_path;

void readerTask(void *) {
	File f = SD.open(g_path.c_str());
	if (!f) {
		log_e("cannot open %s", g_path.c_str());
		g_reader = nullptr;
		vTaskDelete(nullptr);
		return;
	}
	log_i("streaming %s, %lu bytes", g_path.c_str(), (unsigned long)f.size());
	uint8_t chunk[1024];
	while (!g_stop) {
		const int n = f.read(chunk, sizeof(chunk));
		if (n <= 0) break;
		size_t sent = 0;
		while (sent < (size_t)n && !g_stop)
			sent += xStreamBufferSend(g_ring, chunk + sent, n - sent, pdMS_TO_TICKS(200));
	}
	f.close();
	log_i("stream ended, %lu starved callback(s)", (unsigned long)g_starved);
	g_reader = nullptr;
	vTaskDelete(nullptr);
}

// Discovery is asynchronous and the callback runs on the Bluedroid task.
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
std::vector<Bt::Peer> g_peers;

String macToString(esp_bd_addr_t a) {
	char buf[18];
	snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x", a[0], a[1], a[2], a[3], a[4], a[5]);
	return String(buf);
}

// Measured: leaving the target set keeps the ssid callback saying yes, so the
// library re-runs the inquiry while streaming and the radio is no longer the
// link's. The feed then gets called at 83 kB/s against the 176 kB/s it owes
// the sink, which is what a starved speaker sounds like.
void onConnectionState(esp_a2d_connection_state_t state, void *) {
	if (state != ESP_A2D_CONNECTION_STATE_CONNECTED) return;
	g_target = "";
	esp_bt_gap_cancel_discovery();
	log_i("connected: target cleared, no more inquiry over the stream");
}

// Returning false means "not this one": the pass harvests and never connects.
bool onDevice(const char *ssid, esp_bd_addr_t address, int rssi) {
	const String mac = macToString(address);
	portENTER_CRITICAL(&g_mux);
	const bool known = std::any_of(g_peers.begin(), g_peers.end(),
	                               [&](const Bt::Peer &p) { return p.mac == mac; });
	if (!known) g_peers.push_back({String(ssid ? ssid : ""), mac, rssi});
	portEXIT_CRITICAL(&g_mux);
	if (!known) log_i("bt peer \"%s\" %s rssi %d", ssid ? ssid : "", mac.c_str(), rssi);

	if (g_target.isEmpty()) return false; // harvest pass: connect to nothing
	// Match the address too: remote name resolution often comes back empty,
	// which is why the real feature would store the MAC, not the name.
	String name(ssid ? ssid : "");
	name.toLowerCase();
	const bool match = (!name.isEmpty() && name.indexOf(g_target) >= 0) ||
	                   mac.indexOf(g_target) >= 0;
	if (match) log_i("bt connecting to \"%s\" %s", ssid ? ssid : "", mac.c_str());
	return match;
}

// Never reached while the ssid callback refuses every device; set anyway so a
// stray connection cannot walk into a null pointer.
constexpr size_t SINE_LEN = 100; // 44100 / 100 = 441 Hz
int16_t SINE[SINE_LEN];

void buildSine() {
	for (size_t i = 0; i < SINE_LEN; i++)
		SINE[i] = (int16_t)(3000.0f * sinf(2.0f * PI * i / SINE_LEN));
}

// Underrun is what a starved sink sounds like, so count what we are asked for.
volatile uint32_t g_calls = 0, g_bytes = 0, g_minLen = 0xffffffff, g_maxLen = 0, g_odd = 0;
volatile uint32_t g_firstMs = 0, g_lastMs = 0;

int32_t feed(uint8_t *data, int32_t len) {
	// end() tears the stack down while this is still being called, and the
	// buffer it hands over is not ours to touch any more.
	if (g_tearing || !data || len <= 0) return 0;

	const uint32_t now = millis();
	if (!g_calls) g_firstMs = now;
	g_lastMs = now;
	g_calls++;
	g_bytes += len;
	if ((uint32_t)len < g_minLen) g_minLen = len;
	if ((uint32_t)len > g_maxLen) g_maxLen = len;
	if (len % 4) g_odd++;

	// Order matters: the file wins, then the tone, and silence is the fallback.
	// Testing !g_tone first sent nothing but zeros while every counter stayed
	// green, because they count callback entries, not the branch taken.
	if (g_ring && g_reader) {
		// 22050 mono in, 44100 stereo out. len bytes out is len/4 stereo
		// frames; each mono sample becomes two of them, so len/8 samples --
		// len/4 bytes -- come in.
		const size_t want = len / 4;
		int16_t mono[256];
		const size_t take = want > sizeof(mono) ? sizeof(mono) : want;
		const size_t got = xStreamBufferReceive(g_ring, mono, take, 0);
		if (got < take) g_starved++;
		int16_t *pcm = (int16_t *)data;
		const size_t samples = got / 2;
		size_t o = 0;
		for (size_t i = 0; i < samples; i++) {
			pcm[o++] = mono[i]; pcm[o++] = mono[i];   // frame 1
			pcm[o++] = mono[i]; pcm[o++] = mono[i];   // frame 2, the upsample
		}
		while (o < (size_t)(len / 2)) pcm[o++] = 0;   // underrun pads with silence
		configASSERT(o == (size_t)(len / 2));        // never write past the buffer
		return len;
	}

	if (!g_tone) {
		memset(data, 0, len);
		return len;
	}

	// 441 Hz from a table: no float in a Bluedroid callback, and 100 samples
	// divide 44100 exactly so the loop point is silent.
	static uint32_t phase = 0;
	int16_t *pcm = (int16_t *)data;
	const int32_t frames = len / 4; // 16 bit stereo
	for (int32_t i = 0; i < frames; i++) {
		const int16_t v = SINE[phase % SINE_LEN];
		pcm[2 * i] = v;
		pcm[2 * i + 1] = v;
		phase++;
	}
	return len;
}

} // namespace

bool Bt::up(const char *mac) {
	if (g_up) return true;
	g_peers.clear();
	buildSine();
	g_source.set_local_name("WroomTale");
	if (mac && *mac) {
		unsigned b[6];
		if (sscanf(mac, "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) {
			esp_bd_addr_t addr;
			for (int i = 0; i < 6; i++) addr[i] = (uint8_t)b[i];
			g_source.set_auto_reconnect(addr, 5);
			log_i("auto-reconnect armed for %s", mac);
		}
	} else {
		g_source.set_auto_reconnect(false);
	}
	g_source.set_data_callback(feed);
	g_source.set_ssid_callback(onDevice);
	g_source.set_on_connection_state_changed(onConnectionState);
	g_source.start();
	g_up = true;
	return true;
}

void Bt::down(bool releaseMemory) {
	if (!g_up) return;
	stopFile();
	g_tearing = true;
	delay(100);            // let any callback in flight return
	g_source.end(releaseMemory);
	g_up = false;
	g_tearing = false;
}

bool Bt::isUp() { return g_up; }

void Bt::setTarget(const char *nameFragment) {
	g_target = nameFragment ? nameFragment : "";
	g_target.toLowerCase();
}

bool Bt::isConnected() { return g_up && g_source.is_connected(); }

bool Bt::connectTo(const char *mac) {
	unsigned b[6];
	if (sscanf(mac, "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
		log_e("bad address: %s", mac);
		return false;
	}
	esp_bd_addr_t addr;
	for (int i = 0; i < 6; i++) addr[i] = (uint8_t)b[i];
	if (!g_up) up();
	log_i("connecting straight to %s", mac);
	return g_source.connect_to(addr);
}

void Bt::setTone(bool on) { g_tone = on; }

void Bt::feedStats() {
	const uint32_t span = g_lastMs - g_firstMs;
	log_i("feed: %lu calls, %lu B in %lu ms = %lu B/s (needs 176400)",
	      (unsigned long)g_calls, (unsigned long)g_bytes, (unsigned long)span,
	      (unsigned long)(span ? (uint64_t)g_bytes * 1000 / span : 0));
	log_i("feed: len min %lu max %lu, %lu call(s) not a multiple of 4",
	      (unsigned long)g_minLen, (unsigned long)g_maxLen, (unsigned long)g_odd);
	log_i("feed: reader %s, ring holds %u B, %lu starved call(s)",
	      g_reader ? "ALIVE" : "DEAD",
	      g_ring ? (unsigned)xStreamBufferBytesAvailable(g_ring) : 0,
	      (unsigned long)g_starved);
	g_starved = 0;
	g_calls = g_bytes = 0; g_minLen = 0xffffffff; g_maxLen = 0; g_odd = 0;
}

bool Bt::scanning() { return g_up && g_source.is_discovery_active(); }

size_t Bt::peers(Peer *out, size_t max) {
	portENTER_CRITICAL(&g_mux);
	std::vector<Peer> copy = g_peers;
	portEXIT_CRITICAL(&g_mux);
	std::sort(copy.begin(), copy.end(), [](const Peer &a, const Peer &b) { return a.rssi > b.rssi; });
	const size_t n = std::min(max, copy.size());
	for (size_t i = 0; i < n; i++) out[i] = copy[i];
	return n;
}


bool Bt::probeFeed(const char *path, size_t ringBytes, uint32_t ms) {
	uint8_t *ring = (uint8_t *)malloc(ringBytes);
	if (!ring) {
		log_e("probe: %u B ring refused, largest block %u B", (unsigned)ringBytes,
		      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
		return false;
	}
	File f = SD.open(path);
	if (!f) {
		log_e("probe: cannot open %s", path);
		free(ring);
		return false;
	}
	log_i("probe: ring %u B ok, heap %u B free, largest block %u B", (unsigned)ringBytes,
	      (unsigned)ESP.getFreeHeap(),
	      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

	// 44100 Hz, 2 channels, 16 bit: what an A2DP source has to keep up with.
	const uint32_t bytesPerSec = 44100 * 2 * 2;
	const uint32_t startedAt = millis();
	uint32_t total = 0;
	size_t chunk = ringBytes / 4;
	while (millis() - startedAt < ms) {
		const int n = f.read(ring, chunk);
		if (n <= 0) { f.seek(0); continue; }
		total += n;
		// Pace the read to the rate the callback would drain it at.
		const uint32_t due = (uint32_t)((uint64_t)total * 1000 / bytesPerSec);
		const uint32_t spent = millis() - startedAt;
		if (due > spent) delay(due - spent);
	}
	const uint32_t elapsed = millis() - startedAt;
	f.close();
	free(ring);
	log_i("probe: %lu B in %lu ms = %lu B/s (needs %lu), heap %u B free",
	      (unsigned long)total, (unsigned long)elapsed,
	      (unsigned long)(total * 1000UL / (elapsed ? elapsed : 1)),
	      (unsigned long)bytesPerSec, (unsigned)ESP.getFreeHeap());
	return true;
}

bool Bt::playFile(const char *path) {
	stopFile();
	if (!g_ring) g_ring = xStreamBufferCreate(RING_BYTES, 1);
	if (!g_ring) {
		log_e("ring of %u B refused, largest block %u B", (unsigned)RING_BYTES,
		      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
		return false;
	}
	g_path = path;
	g_stop = false;
	g_starved = 0;
	g_tone = false;
	// Core 0 is where Bluedroid lives; the reader goes on core 1 with the app.
	return xTaskCreatePinnedToCore(readerTask, "btread", 4096, nullptr, 4, &g_reader, 1) == pdPASS;
}

void Bt::stopFile() {
	if (!g_reader) return;
	g_stop = true;
	while (g_reader) delay(10);
	xStreamBufferReset(g_ring);
}

namespace {
// The whole point of the spike is the two numbers this prints.
void logHeap(const char *when) {
	log_i("heap %s: %u B free, largest block %u B", when,
	      (unsigned)ESP.getFreeHeap(),
	      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

void upAndMeasure(const char *mac) {
	logHeap("before bt up");
	Bt::up(mac);
	delay(1500); // the stack allocates as it comes up
	logHeap("after bt up");
}

void logStatus() {
	log_i("bt %s, link %s, discovery %s, wifi %s", Bt::isUp() ? "up" : "down",
	      Bt::isConnected() ? "CONNECTED" : "-",
	      Bt::scanning() ? "running" : "idle",
	      WiFi.status() == WL_CONNECTED ? WiFi.SSID().c_str() : "off");
	logHeap("now");
	Bt::Peer found[16];
	const size_t n = Bt::peers(found, 16);
	log_i("%u peer(s) harvested", (unsigned)n);
	for (size_t i = 0; i < n; i++)
		log_i("  %3d dBm  %s  %s", found[i].rssi, found[i].mac.c_str(), found[i].name.c_str());
}
} // namespace

void Bt::command(const String &arg) {
	if (arg.isEmpty()) { logStatus(); return; }
	// Auto-reconnect alone: no target, so the ssid callback never matches and
	// nothing waits on an inquiry.
	if (arg.startsWith("up ")) { logHeap("before bt up"); up(arg.substring(3).c_str()); return; }
	if (arg == "up") { upAndMeasure(nullptr); return; }
	if (arg == "down" || arg == "down free") {
		const bool release = arg.endsWith("free");
		down(release);
		delay(500);
		logHeap(release ? "after bt down free" : "after bt down");
		return;
	}
	if (arg.startsWith("pair ")) {
		setTarget(arg.substring(5).c_str());
		if (!isUp()) upAndMeasure(arg.substring(5).c_str());
		log_i("target set, discovery will connect on a name match");
		return;
	}
	if (arg == "feedstat") { feedStats(); return; }
	if (arg.startsWith("play ")) { log_i("playFile: %d", playFile(arg.substring(5).c_str())); return; }
	if (arg == "stop") { stopFile(); log_i("stream stopped"); return; }
	if (arg.startsWith("connect ")) {
		log_i("connect_to returned %d", connectTo(arg.substring(8).c_str()));
		return;
	}
	if (arg == "tone" || arg == "silence") {
		setTone(arg == "tone");
		log_i("feed: %s", arg.c_str());
		return;
	}
	if (arg.startsWith("probe ")) { probeFeed(arg.substring(6).c_str(), 16 * 1024, 4000); return; }
	log_w("usage: bt | bt up [mac] | bt pair <name> | bt tone | bt silence | bt down [free] | "
	      "bt probe <f> | bt play <f> | bt stop | bt connect <mac> | bt feedstat");
}

#endif // FEAT_BT

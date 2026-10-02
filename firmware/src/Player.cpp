#include "Player.h"
#include "Config.h"
#include "Features.h"
#include "Nvs.h"
#include "Target.h"
#include "Tones.h"

#include <algorithm>
#include <cmath>

#include <AudioFileSourceBuffer.h>
#include <AudioFileSourcePROGMEM.h>
#include <AudioFileSourceSD.h>

#if FEAT_CLIPS
#include "sounds/BootMp3.h"
#endif
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
#include <SD.h>
#include <SPI.h>
#include <vector>

namespace {

constexpr char NS[] = "player";

enum class Cmd : uint8_t { PlayFolder, Stop, TogglePause, Next, Prev, VolUp, VolDown, VolCap, VolFloor, Tone, SelfTest, PlayFile };

struct Message {
	Cmd cmd;
	uint8_t arg;
	uint16_t index;
	char *folder; // strdup'd by send(), freed once handled: SD names run past 100 chars
};

QueueHandle_t g_queue = nullptr;
TaskHandle_t g_task = nullptr;

AudioGeneratorMP3 *g_mp3 = nullptr;
AudioFileSourceSD *g_file = nullptr;
AudioFileSourceBuffer *g_buffer = nullptr;
AudioFileSourcePROGMEM *g_progmem = nullptr;
class LevelTap : public AudioOutput {
public:
	explicit LevelTap(AudioOutput *next) : _next(next) {}
	bool SetRate(int hz) override { return _next->SetRate(hz); }
	bool SetChannels(int c) override { return _next->SetChannels(c); }
	bool SetGain(float f) override { return _next->SetGain(f); }
	bool begin() override { return _next->begin(); }
	bool stop() override { _peak = 0; return _next->stop(); }
	void flush() override { _next->flush(); }
	bool loop() override { return _next->loop(); }

	bool ConsumeSample(int16_t sample[2]) override {
		const int16_t l = sample[0] < 0 ? -sample[0] : sample[0];
		const int16_t r = sample[1] < 0 ? -sample[1] : sample[1];
		const int16_t m = l > r ? l : r;
		if (m > _peak) _peak = m;
		return _next->ConsumeSample(sample);
	}

	// Reading clears it.
	uint8_t takeLevel() {
		const int16_t p = _peak;
		_peak = 0;
		return (uint8_t)(p >> 7);
	}

private:
	AudioOutput *_next;
	volatile int16_t _peak = 0;
};

AudioOutputI2S *g_i2s = nullptr;
LevelTap *g_out = nullptr;

std::vector<String> g_playlist;
String g_folder;
bool g_sdReady = false;
volatile bool g_selfTestStop = false;
volatile bool g_selfTestRunning = false;
volatile bool g_finished = false;
int g_index = -1;
bool g_paused = false;
volatile uint8_t g_volume = VOLUME_DEFAULT;
volatile uint8_t g_volumeCap = VOLUME_CAP_DEFAULT;
volatile uint8_t g_volumeFloor = VOLUME_FLOOR_DEFAULT;
// A held button steps every 120 ms; writing NVS that often would wear it out
// for nothing. Mark it and flush once the hand comes off.
bool g_volDirty = false;
uint32_t g_volTouchedAt = 0;

// One bit per Sfx, set when silenced. Written by the web task, read here.
static_assert(static_cast<size_t>(Sfx::Count) <= 32, "the mask holds 32 sounds");
volatile uint32_t g_sfxOff = 0;

// A mutex, not a portMUX: copying a String allocates.
SemaphoreHandle_t g_stateMutex = nullptr;
String g_currentTrack;
bool g_playing = false;

void publishState(const String &track, bool playing) {
	if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(20)) != pdTRUE) return;
	g_currentTrack = track;
	g_playing = playing;
	xSemaphoreGive(g_stateMutex);
}

// g_playlist and g_folder are read by the web task, so every change to them is
// made under the mutex. g_index is a plain int: the audio task is its only
// writer and a 32-bit read cannot tear.
void publishQueue(String folder, std::vector<String> tracks) {
	if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
	g_folder = std::move(folder);
	g_playlist = std::move(tracks);
	xSemaphoreGive(g_stateMutex);
}

void clearQueue() {
	publishQueue(String(), {});
	g_index = -1;
}

// Only the flag moves: a story node plays with no playlist behind it, so the
// track name must not be looked up again here.
void publishPlaying(bool playing) {
	if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(20)) != pdTRUE) return;
	g_playing = playing;
	xSemaphoreGive(g_stateMutex);
}

void applyVolume() {
	if (!g_out) return;
	// Squared: a linear gain jumps from silent to loud over the first steps.
	const float ratio = static_cast<float>(g_volume) / VOLUME_MAX;
	g_out->SetGain(ratio * ratio);
}

void saveVolume() {
	Nvs(NS, false)->putUChar("vol", g_volume);
	g_volDirty = false;
}

void touchVolume() {
	g_volDirty = true;
	g_volTouchedAt = millis();
}

void flushVolume() {
	if (g_volDirty && millis() - g_volTouchedAt >= VOLUME_SAVE_IDLE_MS) saveVolume();
}

void saveVolumeLimits() {
	Nvs nvs(NS, false);
	nvs->putUChar("volcap", g_volumeCap);
	nvs->putUChar("volmin", g_volumeFloor);
}

// One place decides what the limits allow, so the two can never cross.
void clampToLimits() {
	if (g_volumeCap > VOLUME_MAX) g_volumeCap = VOLUME_MAX;
	if (g_volumeFloor > g_volumeCap) g_volumeFloor = g_volumeCap;
	if (g_volume > g_volumeCap) g_volume = g_volumeCap;
	if (g_volume < g_volumeFloor) g_volume = g_volumeFloor;
}

void loadVolume() {
	{
		Nvs nvs(NS, true);
		g_volumeCap = nvs->getUChar("volcap", VOLUME_CAP_DEFAULT);
		g_volumeFloor = nvs->getUChar("volmin", VOLUME_FLOOR_DEFAULT);
		g_volume = nvs->getUChar("vol", VOLUME_DEFAULT);
	}
	// Limits set on a previous run must bite from the first note.
	clampToLimits();
}

void releaseChain() {
	if (g_mp3) {
		if (g_mp3->isRunning()) g_mp3->stop();
		delete g_mp3;
		g_mp3 = nullptr;
	}
	// The buffer wraps the source, so it goes first.
	delete g_buffer;
	g_buffer = nullptr;
	delete g_file;
	g_file = nullptr;
	delete g_progmem;
	g_progmem = nullptr;

	publishState("", false);
}

void scanFolder(const String &folder) {
	clearQueue();

	File dir = SD.open(folder);
	if (!dir || !dir.isDirectory()) {
		log_e("folder not found: %s", folder.c_str());
		return;
	}

	// Built aside, then published in one go: the walk takes hundreds of ms and
	// the web task must not wait on it.
	std::vector<String> tracks;
	while (File entry = dir.openNextFile()) {
		if (!entry.isDirectory() && Target::isMp3(entry.name())) {
			// openNextFile may return a bare name or a full path.
			const String name = entry.name();
			tracks.push_back(name.startsWith("/") ? name : folder + "/" + name);
		}
		entry.close();
	}
	dir.close();

	std::sort(tracks.begin(), tracks.end());
	if (tracks.empty()) {
		log_w("%s holds no .mp3 -- sub-folders are not searched, point at the "
		      "folder that actually contains the files", folder.c_str());
	} else {
		log_i("%u track(s) in %s", tracks.size(), folder.c_str());
	}
	publishQueue(folder, std::move(tracks));
}

// Flash is already memory-mapped: no card, no read-ahead buffer.
struct Builtin {
	const char *id;
	const uint8_t *data;
	size_t len;
};

const Builtin BUILTINS[] = {
#if FEAT_CLIPS
	{"boot", BOOT_MP3, BOOT_MP3_LEN},
#endif
};

// The source chain is already in place; on failure it is released whole.
bool startDecoder(AudioFileSource *src, const String &name) {
	g_mp3 = new AudioGeneratorMP3();
	g_paused = false;
	if (!g_mp3->begin(src, g_out)) {
		log_e("decoder refused %s", name.c_str());
		releaseChain();
		return false;
	}
	publishState(name, true);
	log_i("playing %s, heap %lu KB", name.c_str(), (unsigned long)(ESP.getFreeHeap() / 1024));
	return true;
}

bool startBuiltin(const String &id) {
	releaseChain();

	const Builtin *clip = nullptr;
	for (const Builtin &b : BUILTINS) {
		if (id == b.id) { clip = &b; break; }
	}
	if (!clip) {
		log_e("unknown builtin \"%s\"", id.c_str());
		return false;
	}

	g_progmem = new AudioFileSourcePROGMEM(clip->data, clip->len);
	return startDecoder(g_progmem, Target::BUILTIN_PREFIX + id);
}

bool startFile(const String &path) {
	releaseChain();
	if (!g_sdReady) {
		log_e("no SD card, cannot play %s", path.c_str());
		return false;
	}

	g_file = new AudioFileSourceSD(path.c_str());
	if (!g_file->isOpen()) {
		log_e("cannot open %s", path.c_str());
		releaseChain();
		return false;
	}
	g_buffer = new AudioFileSourceBuffer(g_file, AUDIO_BUFFER_BYTES);
	return startDecoder(g_buffer, path);
}

bool startTrack(int index) {
	if (index < 0 || index >= static_cast<int>(g_playlist.size())) {
		releaseChain();
		return false;
	}
	g_index = index;
	return startFile(g_playlist[index]);
}

// Skips tracks that fail to open. `wrap` is false at end of track: a folder
// plays once and stops rather than looping until the battery dies.
void advance(int delta, bool wrap = true) {
	if (g_playlist.empty()) return;
	const int count = static_cast<int>(g_playlist.size());
	int candidate = g_index;

	for (int attempt = 0; attempt < count; attempt++) {
		const int next = candidate + delta;
		if (!wrap && (next < 0 || next >= count)) break;
		candidate = (next + count) % count;
		if (startTrack(candidate)) return;
	}

	releaseChain();
	if (!wrap) {
		g_finished = true;
		log_i("end of folder");
	}
}

constexpr int SFX_RATE = 44100;
constexpr int16_t SFX_PEAK = 12000;

void renderNote(const Note &note) {
	const uint32_t total = (uint32_t)SFX_RATE * note.ms / 1000;
	if (!total) return;
	const uint32_t fade = std::min<uint32_t>(total / 4, SFX_RATE / 200); // <= 5 ms

	int16_t block[64 * 2];
	const float step = 2.0f * (float)M_PI * note.hz / SFX_RATE;
	float phase = 0.0f;

	for (uint32_t done = 0; done < total;) {
		const uint32_t n = std::min<uint32_t>(64, total - done);
		for (uint32_t i = 0; i < n; i++) {
			const uint32_t pos = done + i;
			float env = 1.0f;
			if (fade) {
				if (pos < fade) env = (float)pos / fade;
				else if (total - pos < fade) env = (float)(total - pos) / fade;
			}
			const int16_t s = note.hz ? (int16_t)(sinf(phase) * SFX_PEAK * env) : 0;
			phase += step;
			if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
			block[i * 2] = s;
			block[i * 2 + 1] = s;
		}

		uint16_t written = 0;
		while (written < n) {
			const uint16_t got = g_out->ConsumeSamples(block + written * 2, n - written);
			written += got;
			if (!got) vTaskDelay(1); // DMA buffer full
		}
		done += n;
	}
}

#if FEAT_CLIPS
// Decoded in place rather than through startBuiltin: it is the box saying
// hello, not a track, so the panel never shows it as playing.
void renderJingle() {
	AudioFileSourcePROGMEM *src = new AudioFileSourcePROGMEM(BOOT_MP3, BOOT_MP3_LEN);
	AudioGeneratorMP3 *mp3 = new AudioGeneratorMP3();
	const uint32_t startedAt = millis();
	if (mp3->begin(src, g_out)) {
		while (mp3->loop()) vTaskDelay(1);
		mp3->stop();
		log_i("boot jingle in %lu ms", (unsigned long)(millis() - startedAt));
	} else {
		log_e("decoder refused the boot jingle");
	}
	delete mp3;
	delete src;
}
#endif

void renderSfx(Sfx id) {
	if (!Player::sfxEnabled(id)) return;
	// No mixing: a running track wins.
	if (g_mp3 && g_mp3->isRunning()) return;

#if FEAT_CLIPS
	if (id == Sfx::Boot) { renderJingle(); return; }
#endif

	size_t count = 0;
	const Note *notes = sfxNotes(id, count);
	if (!notes) return;

	g_out->SetRate(SFX_RATE);
	g_out->SetChannels(2);
	if (!g_out->begin()) {
		log_e("I2S refused to start, no tone");
		return;
	}

	const uint32_t startedAt = millis();
	for (size_t i = 0; i < count; i++) renderNote(notes[i]);

	g_out->flush();
	g_out->stop();
	log_i("sfx %u: %u note(s) in %lu ms", (unsigned)id, (unsigned)count,
	      (unsigned long)(millis() - startedAt));
}

void handle(const Message &msg) {
	switch (msg.cmd) {
	case Cmd::PlayFolder: {
		const String folder(msg.folder);
		if (Target::isBuiltin(folder)) {
			clearQueue();
			if (!startBuiltin(Target::builtinId(folder))) renderSfx(Sfx::Error);
			break;
		}
		if (!g_sdReady) {
			log_w("no SD card, cannot play %s", msg.folder);
			renderSfx(Sfx::Error);
			break;
		}
		// Already loaded: a track jump must not walk the card again.
		if (folder != g_folder) scanFolder(folder);
		if (g_playlist.empty()) {
			renderSfx(Sfx::Error);
			break;
		}
		const int last = static_cast<int>(g_playlist.size()) - 1;
		if (!startTrack(std::min<int>(msg.index, last))) renderSfx(Sfx::Error);
		break;
	}
	case Cmd::Stop:
		releaseChain();
		clearQueue();
		break;
	case Cmd::TogglePause:
		if (g_mp3 && g_mp3->isRunning()) {
			g_paused = !g_paused;
			// Starving the DMA is not silence: it replays its last buffer until
			// something refills it. The peripheral itself has to go down.
			if (g_paused) g_out->stop();
			else g_out->begin();
			publishPlaying(!g_paused);
		}
		break;
	case Cmd::Next: advance(1); break;
	case Cmd::Prev: advance(-1); break;
	case Cmd::VolUp:
		if (g_volume < g_volumeCap) { g_volume++; applyVolume(); touchVolume(); }
		break;
	case Cmd::VolDown:
		if (g_volume > g_volumeFloor) { g_volume--; applyVolume(); touchVolume(); }
		break;
	// A limit that only bit at the next press would not be a limit: both apply
	// to the volume in hand, right away.
	case Cmd::VolCap:
	case Cmd::VolFloor: {
		const uint8_t before = g_volume;
		if (msg.cmd == Cmd::VolCap) g_volumeCap = msg.arg;
		else g_volumeFloor = msg.arg;
		clampToLimits();
		saveVolumeLimits();
		if (g_volume != before) { applyVolume(); saveVolume(); }
		break;
	}
	case Cmd::PlayFile:
		clearQueue();
		if (!startFile(msg.folder)) renderSfx(Sfx::Error);
		break;
	case Cmd::Tone:
		renderSfx(static_cast<Sfx>(msg.arg));
		break;
	case Cmd::SelfTest: {
		g_selfTestStop = false;
		g_selfTestRunning = true;
		uint16_t rounds = 0;
		while (!g_selfTestStop) {
			renderSfx(Sfx::Test);
			rounds++;
			// Split so a button press ends the test promptly.
			for (uint32_t w = 0; w < SELFTEST_GAP_MS && !g_selfTestStop; w += 20)
				vTaskDelay(pdMS_TO_TICKS(20));
		}
		g_selfTestRunning = false;
		log_i("self-test ended after %u tone(s)", rounds);
		break;
	}
	}
}

void audioTask(void *) {
	for (;;) {
		Message msg;
		while (xQueueReceive(g_queue, &msg, 0) == pdTRUE) {
			handle(msg);
			free(msg.folder);
		}
		flushVolume();

		if (g_mp3 && g_mp3->isRunning() && !g_paused) {
			if (!g_mp3->loop()) {
				if (g_playlist.empty()) {
					releaseChain();
					g_finished = true;
				} else {
					advance(1, false); // end of track: do not loop the folder
				}
			}
			// Not taskYIELD(): the control loop shares this core at a lower
			// priority and yielding never reaches it. A frame lasts ~26 ms.
			vTaskDelay(1);
		} else {
			vTaskDelay(pdMS_TO_TICKS(10));
		}
	}
}

void send(Cmd cmd, const String &folder = "", uint8_t arg = 0, uint16_t index = 0) {
	if (!g_queue) return;
	Message msg{cmd, arg, index, strdup(folder.c_str())};
	if (!msg.folder) return;
	if (xQueueSend(g_queue, &msg, pdMS_TO_TICKS(50)) != pdTRUE) free(msg.folder);
}

} // namespace

bool Player::begin() {
	loadVolume();
	g_sfxOff = Nvs(NS, true)->getULong("sfxoff", 0);

	g_i2s = new AudioOutputI2S();
	g_i2s->SetPinout(PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DOUT);
	g_out = new LevelTap(g_i2s);
	applyVolume();

	g_stateMutex = xSemaphoreCreateMutex();
	g_queue = xQueueCreate(8, sizeof(Message));

	// Core 1 for audio only.
	xTaskCreatePinnedToCore(audioTask, "audio", 8192, nullptr, 5, &g_task, 1);

	// Optional: without it the player still produces tones.
	SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
	g_sdReady = SD.begin(PIN_SD_CS);
	if (!g_sdReady) log_w("no SD card mounted: tones only");

	return true;
}

void Player::playFolder(const String &folder) { send(Cmd::PlayFolder, folder); }
void Player::playFolderAt(const String &folder, uint16_t index) {
	send(Cmd::PlayFolder, folder, 0, index);
}

void Player::queue(String &folder, std::vector<String> &tracks, int &index) {
	index = -1;
	if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
	folder = g_folder;
	tracks = g_playlist;
	xSemaphoreGive(g_stateMutex);
	index = g_index;
}
void Player::playFile(const String &path) { send(Cmd::PlayFile, path); }

bool Player::takeFinished() {
	if (!g_finished) return false;
	g_finished = false;
	return true;
}
void Player::stop() { send(Cmd::Stop); }
void Player::togglePause() { send(Cmd::TogglePause); }
void Player::next() { send(Cmd::Next); }
void Player::prev() { send(Cmd::Prev); }
void Player::volumeUp() { send(Cmd::VolUp); }
void Player::volumeDown() { send(Cmd::VolDown); }
void Player::setVolumeCap(uint8_t cap) { send(Cmd::VolCap, "", cap); }
void Player::setVolumeFloor(uint8_t floor) { send(Cmd::VolFloor, "", floor); }

void Player::play(Sfx id) { send(Cmd::Tone, "", static_cast<uint8_t>(id)); }

bool Player::sfxEnabled(Sfx id) {
	return id == Sfx::Test || !((g_sfxOff >> static_cast<uint8_t>(id)) & 1);
}

void Player::setSfxEnabled(Sfx id, bool on) {
	if (id == Sfx::Test || id >= Sfx::Count) return;
	const uint32_t bit = 1u << static_cast<uint8_t>(id);
	g_sfxOff = on ? (g_sfxOff & ~bit) : (g_sfxOff | bit);
	Nvs(NS, false)->putULong("sfxoff", g_sfxOff);
}

void Player::startSelfTest() { send(Cmd::SelfTest); }
void Player::stopSelfTest() { g_selfTestStop = true; }
bool Player::selfTestRunning() { return g_selfTestRunning; }

bool Player::selfTestEnabled() { return Nvs(NS, true)->getBool("selftest", SELFTEST_DEFAULT); }
void Player::setSelfTestEnabled(bool on) { Nvs(NS, false)->putBool("selftest", on); }

bool Player::sdReady() { return g_sdReady; }

uint8_t Player::audioLevel() { return g_out ? g_out->takeLevel() : 0; }

size_t Player::queueCount() {
	if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) != pdTRUE) return 0;
	const size_t n = g_playlist.size();
	xSemaphoreGive(g_stateMutex);
	return n;
}

uint8_t Player::volume() { return g_volume; }
uint8_t Player::volumeCap() { return g_volumeCap; }
uint8_t Player::volumeFloor() { return g_volumeFloor; }

bool Player::isPlaying() {
	if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(20)) != pdTRUE) return false;
	const bool playing = g_playing;
	xSemaphoreGive(g_stateMutex);
	return playing;
}

String Player::currentTrack() {
	if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(20)) != pdTRUE) return "";
	const String track = g_currentTrack;
	xSemaphoreGive(g_stateMutex);
	return track;
}

bool Player::audioBufferHealthy() {
	if (!isPlaying() || !g_buffer) return true;
	return g_buffer->getFillLevel() >= (AUDIO_BUFFER_BYTES * 3 / 4);
}

#pragma once
#include <Arduino.h>
#include <vector>
#include "Tones.h"

namespace Player {
// Starts the audio task on core 1. The card is optional.
bool begin();

// All of these enqueue a command; none touches the decoder.
void playFolder(const String &folder);
// Same folder, starting at a given track. The card is walked again only when
// the folder is not the one already loaded, so jumping tracks costs no I/O.
void playFolderAt(const String &folder, uint16_t index);
void playFile(const String &path);

// The folder loaded right now and its tracks, in play order. Empty for a story
// node or an embedded clip: those play outside any playlist.
void queue(String &folder, std::vector<String> &tracks, int &index);
// Just its size, cheap enough for a button press: nothing to walk means PREV
// and NEXT have nothing to say.
size_t queueCount();

// True once per finished clip, then cleared.
bool takeFinished();
// Also cuts a sound already playing, the boot jingle included.
void stop();
// Before deep sleep: saves a pending volume, unmounts the card, silences I2S.
// Runs after whatever is already queued; poll isShutDown() for the end.
void shutdown();
bool isShutDown();
void togglePause();
void next();
void prev();
void volumeUp();
void volumeDown();
// Ceiling and floor on what the buttons and the panel can reach, kept in NVS.
// The floor is a calibration knob: the squared gain makes the first steps
// near-silent, and how near depends on the speaker.
void setVolumeCap(uint8_t cap);
void setVolumeFloor(uint8_t floor);

// Skipped while a track plays: the output is not mixed.
void play(Sfx id);

// Any feedback sound can be silenced from the panel, kept in NVS. The self-test
// tone cannot: it is a measurement, not feedback.
bool sfxEnabled(Sfx id);
void setSfxEnabled(Sfx id, bool on);

void startSelfTest();
void stopSelfTest();
bool selfTestRunning();

bool selfTestEnabled();
void setSelfTestEnabled(bool on);

// Peak since the last call, 0-255.
uint8_t audioLevel();

uint8_t volume();
uint8_t volumeCap();
uint8_t volumeFloor();
bool isPlaying();
String currentTrack();

bool sdReady();
// Returns true if audio buffer has enough data to tolerate SD card writes.
bool audioBufferHealthy();
} // namespace Player

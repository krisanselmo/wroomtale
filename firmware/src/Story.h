#pragma once
#include <Arduino.h>
#include "Features.h"

// One clip per node, up to three choices. Authored offline; the box walks the tree.
namespace Story {
constexpr uint8_t MAX_NODES = 32;
constexpr uint8_t MAX_FLAGS = 16;
constexpr uint8_t BTN_PREV = 0;
constexpr uint8_t BTN_PLAY = 1;
constexpr uint8_t BTN_NEXT = 2;

#if FEAT_STORY
// Reads <folder>/histoire.txt; format in docs/histoires.md.
bool loadFromSd(const String &folder);
// Lets a plain folder target be recognised as a story, with no prefix.
bool hasManifest(const String &folder);
// A manifest's text; relative audio paths are resolved against `folder`.
bool loadFromText(const String &text, const String &folder);
// Built from the embedded clips: exercises the engine with no card.
bool loadDemo();
void stop();

bool active();
bool atEnd();

String currentAudio();
String currentId();

// Bit 0/1/2 = PREV/PLAY/NEXT lead somewhere.
uint8_t choiceMask();
String flagList();
bool choose(uint8_t button);
#else
inline bool loadFromSd(const String &) { return false; }
inline bool hasManifest(const String &) { return false; }
inline bool loadDemo() { return false; }
inline bool loadFromText(const String &, const String &) { return false; }
inline void stop() {}
inline bool active() { return false; }
inline bool atEnd() { return false; }
inline String currentAudio() { return String(); }
inline String currentId() { return String(); }
inline uint8_t choiceMask() { return 0; }
inline bool choose(uint8_t) { return false; }
inline String flagList() { return String(); }
#endif
} // namespace Story

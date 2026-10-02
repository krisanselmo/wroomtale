#pragma once
#include <Arduino.h>
#include "Buttons.h"

// A story being told: plays each node, offers its choices once the clip ends,
// and owns the three colours while it runs. Story is the graph; this is the
// telling.
namespace StoryRunner {
// Loads the story and plays its first node. False when it would not load.
bool startFromSd(const String &folder);
bool startDemo();
void stop();

// True when the press was the story's to take.
bool press(BtnEvent ev);
// Called once per finished clip: offers the choices, or ends the tale.
void onClipEnd();

void logStatus();
} // namespace StoryRunner

#pragma once
#include <Arduino.h>
#include "Buttons.h"

// What the box does with a press, a tag or a target, whichever way it came:
// the buttons, the reader or the console.
namespace Box {
void press(BtnEvent ev);
void presentTag(const String &uid);
// Builtin clip, folder of tracks, single track, or story -- the card tells which.
void playTarget(const String &target);
} // namespace Box

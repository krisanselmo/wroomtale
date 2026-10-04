#pragma once

// A disabled volet has its source excluded from the build and its header
// supplies inline no-ops, so call sites stay unguarded.
//
// Every volet ships on: one build does the lot, and what the box turns out to
// be is decided by what is plugged in and what the panel is told. These stay
// as a way to trim a board by hand -- pass -DFEAT_X=0 and drop the source in
// build_src_filter -- not as a set of profiles anyone has to keep in step.

#ifndef FEAT_PORTAL   // WiFi station/AP, web dashboard, mDNS
#define FEAT_PORTAL 1
#endif
#ifndef FEAT_RFID     // PN532 reader over I2C
#define FEAT_RFID 1
#endif
#ifndef FEAT_LEDS     // WS2812B strip
#define FEAT_LEDS 1
#endif
#ifndef FEAT_STORY    // branching stories
#define FEAT_STORY 1
#endif
#ifndef FEAT_CLIPS    // boot jingle in flash; without it, three plain tones
#define FEAT_CLIPS 1
#endif
#ifndef FEAT_SLEEP    // deep sleep when idle or on a flat cell
#define FEAT_SLEEP 1
#endif
#ifndef FEAT_BT       // A2DP source probe -- spike only, off everywhere else
#define FEAT_BT 0
#endif

// Button scheme -- behaviour, not content, which is why it is a build switch
// and not one of the volets above. 0: classic, PREV/NEXT walk the tracks and
// held ends move the volume. 1: volume first, for hands that reach
// for it constantly -- red and blue step the volume on a click and ramp when
// held, and the tracks move to a double click on green.
#ifndef BTN_SCHEME
#define BTN_SCHEME 0
#endif

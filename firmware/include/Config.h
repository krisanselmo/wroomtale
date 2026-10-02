#pragma once
#include <Arduino.h>

// --- Pin map (ESP32-WROOM-32 devkit) -------------------------------------
// Signals are assigned so each bundle reaches the header in the order it
// leaves its module: nothing crosses. SPI, I2C and I2S all go through the
// GPIO matrix, so the numbers are free -- only the order matters.
// GPIO 16 and 17 are free here because this module carries no PSRAM.
// GPIO 34-39 are avoided for buttons: input-only, no internal pull-up.
// GPIO 0/2/12/15 are avoided entirely: strapping pins.

// SD card over VSPI
constexpr gpio_num_t PIN_SD_SCK = GPIO_NUM_18;
constexpr gpio_num_t PIN_SD_MISO = GPIO_NUM_21;
constexpr gpio_num_t PIN_SD_MOSI = GPIO_NUM_19;
constexpr gpio_num_t PIN_SD_CS = GPIO_NUM_5;

// MAX98357A (I2S)
constexpr int PIN_I2S_BCLK = 26;
constexpr int PIN_I2S_LRC = 27;
constexpr int PIN_I2S_DOUT = 25;

// PN532 over I2C
constexpr int PIN_I2C_SDA = 17;
constexpr int PIN_I2C_SCL = 16;
// -1 disables the IRQ line: over I2C the library polls the status byte
// instead, and the pin was never used. Dropping it is what lets the whole
// right-hand header fit one 10-way crimp housing.
constexpr int PIN_PN532_IRQ = -1;
constexpr int PIN_PN532_RESET = 4;

// Each button carries a colour identity: the narration says "le rouge", the
// strip lights that colour. A child who cannot read navigates by colour alone.
constexpr uint32_t BTN_COLOUR[3] = {0xFF1818, 0x18C838, 0x2060FF};
constexpr const char *BTN_NAME[3] = {"rouge", "vert", "bleu"};

constexpr gpio_num_t PIN_LEDS = GPIO_NUM_13;
constexpr uint8_t LED_COUNT = 10;
// Colours are written at full scale; dimming happens here only.
constexpr uint8_t LED_BRIGHTNESS = 90;
constexpr uint16_t LED_MAX_MA = 150;

// Battery sense. GPIO 35 is input-only, which is all a divider needs, and it
// sits on ADC1 -- ADC2 stops working the moment the radio does. 34 would do
// as well; 35 is one pin further down, which is what brings the whole
// left-hand header inside a single 10-way crimp housing.
constexpr uint8_t PIN_BATT = 35;
// Two 100 k: 21 uA of permanent drain, and 4,2 V lands at 2,1 V.
constexpr float BATT_DIVIDER = 2.0f;
constexpr uint8_t BATT_SAMPLES = 16;
constexpr uint16_t BATT_FULL_MV = 4200;
constexpr uint16_t BATT_LOW_MV = 3400;
// Below this the divider is unwired, not the cell flat.
constexpr uint16_t BATT_ABSENT_MV = 2500;
constexpr uint32_t BATT_WARN_EVERY_MS = 60 * 1000;
constexpr uint32_t BATT_SAMPLE_MS = 1000;
constexpr float BATT_EMA_ALPHA = 0.08f;

constexpr uint32_t BATTLOG_INTERVAL_MS = 30 * 1000;
constexpr size_t BATTLOG_BUFFER_SIZE = 512;
constexpr uint32_t BATTLOG_MAX_HOLD_MS = 5 * 60 * 1000;

constexpr gpio_num_t PIN_BTN_PREV = GPIO_NUM_32;
constexpr gpio_num_t PIN_BTN_PLAY = GPIO_NUM_33;
constexpr gpio_num_t PIN_BTN_NEXT = GPIO_NUM_14;

// --- Behaviour -----------------------------------------------------------
constexpr uint32_t BTN_DEBOUNCE_MS = 25;
constexpr uint32_t BTN_LONGPRESS_MS = 700;
// Held volume ramps instead of stepping: 21 steps end to end in ~2,5 s, slow
// enough to stop where you meant to. PLAY never repeats -- it stops playback.
constexpr uint32_t BTN_REPEAT_MS = 120;
// A double click makes its button's single click wait to be sure no second one
// is coming. Only the button that declares a double pays it.
constexpr uint32_t BTN_DOUBLE_MS = 280;

constexpr uint32_t CONFIG_GESTURE_MS = 2000;
constexpr uint32_t CONSOLE_ESCAPE_MS = 600;
constexpr uint32_t CONFIG_PORTAL_TIMEOUT_MS = 5 * 60 * 1000;
constexpr uint32_t PORTAL_TOUCH_GRACE_MS = 5000;

// The box boots to play: radio off, and the welcome triad actually reached --
// ConfigPortal::run() never returns, so a sticky default made setup() stop
// short of it. Hold PLAY at boot, or flip this from the panel, to configure.
constexpr bool CONFIG_STICKY_DEFAULT = false;

constexpr char AP_SSID[] = "WroomTale";
constexpr char AP_DEFAULT_PASS[] = "wroomtale";
constexpr char MDNS_NAME[] = "wroomtale";
constexpr uint32_t STA_CONNECT_TIMEOUT_MS = 15000;
constexpr uint16_t RFID_POLL_MS = 200;

// Comes out of the ~200 kB heap: no PSRAM to put it in.
constexpr size_t AUDIO_BUFFER_BYTES = 12 * 1024;

constexpr bool SELFTEST_DEFAULT = false;
constexpr uint32_t SELFTEST_GAP_MS = 400;

constexpr uint8_t VOLUME_MIN = 0;
constexpr uint8_t VOLUME_MAX = 21;
constexpr uint8_t VOLUME_DEFAULT = 8;
// A ceiling the buttons cannot pass, set from the panel and kept in NVS. It
// caps what is reachable; it does not rescale, so a given step sounds the same
// whatever the ceiling. VOLUME_MAX means no ceiling.
constexpr uint8_t VOLUME_CAP_DEFAULT = VOLUME_MAX;
// And a floor, which is a calibration knob rather than a safety one: the gain
// is squared, so the first steps are near-silent, and how near depends on the
// speaker. Raise it until the lowest reachable step is still audible on yours.
// VOLUME_MIN means no floor. The floor never passes the ceiling.
constexpr uint8_t VOLUME_FLOOR_DEFAULT = VOLUME_MIN;
// A ramp would otherwise write NVS at every step: save once it settles.
constexpr uint32_t VOLUME_SAVE_IDLE_MS = 1500;

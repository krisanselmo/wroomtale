#pragma once
#include <Arduino.h>

// Cell voltage through a 2 x 100 k divider on ADC1. ADC2 is unusable while the
// radio runs, which rules out every pin on the right header.
namespace Battery {
void begin();

// Periodic sampling and EMA filtering. Called from pumpInput().
void tick();

// Filtered cell millivolts, 0 when nothing plausible is on the pin.
uint16_t millivolts();

// Instantaneous raw cell millivolts before filtering.
uint16_t rawMillivolts();

// Raw pin millivolts, before the divider ratio and the trim.
uint16_t pinMillivolts();

// A reading inside the range an 18650 can actually hold.
bool present();

// Charge left, from the discharge curve rather than a straight line.
uint8_t percent();

// Resistors are 1 % at best: give it what the multimeter says across the cell.
void calibrate(uint16_t realMv);
void clearCalibration();
float trim();
} // namespace Battery

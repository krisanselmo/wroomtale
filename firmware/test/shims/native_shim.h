#pragma once
// What the ESP32 core provides and ArduinoFake does not.
#include <stdio.h>

typedef int gpio_num_t;

#define log_e(fmt, ...) printf("[E] " fmt "\n", ##__VA_ARGS__)
#define log_w(fmt, ...) printf("[W] " fmt "\n", ##__VA_ARGS__)
#define log_i(fmt, ...) ((void)0)

// An ESP32 String extension. One member call, so `!s.isEmpty()` still parses.
#define isEmpty() equals("")

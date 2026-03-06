#pragma once
/**
 * @file config.h
 * @brief Single source of truth for all compile-time hardware constants,
 *        port numbers, and tuning thresholds.
 *
 * Pin assignments, port numbers, and threshold/timing values all live here.
 * Feature-selection flags (MNDEBUG, TELNET, UAP_SUPPORT, BME280_SUPPORT)
 * live in platformio.ini build_flags.
 */

#include <Arduino.h>

// ─── External RGB LED pins ────────────────────────────────────────────────────
constexpr pin_size_t RED_PIN = PIN_A4;
constexpr pin_size_t GREEN_PIN = PIN_A3;
constexpr pin_size_t BLUE_PIN = 10;

// ─── UAP garage door status input pins (must be interrupt-capable) ────────────
constexpr pin_size_t DOOR_IS_OPEN_STATUS_PIN = 9;
constexpr pin_size_t DOOR_IS_CLOSED_STATUS_PIN = 8;
constexpr pin_size_t LIGHT_IS_ON_STATUS_PIN = 7;
constexpr pin_size_t DOOR_SWITCH_INPUT_PIN = 0;

// ─── UAP garage door command output pins ─────────────────────────────────────
constexpr pin_size_t OPEN_DOOR_OUTPUT_PIN = 4;
constexpr pin_size_t CLOSE_DOOR_OUTPUT_PIN = 3;
constexpr pin_size_t STOP_DOOR_OUTPUT_PIN = 5;
constexpr pin_size_t TURN_LIGHT_ON_OUTPUT_PIN = 2;

// ─── Serial / Telnet ──────────────────────────────────────────────────────────
constexpr uint32_t BAUD_RATE = 115200;
constexpr uint16_t TELNET_PORT = 0xFEEE;

// ─── WiFi ─────────────────────────────────────────────────────────────────────
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 10000;

// ─── WiFi reconnection backoff ────────────────────────────────────────────────
constexpr uint32_t WIFI_RECONNECT_BASE_DELAY_MS = 5000UL;  // 5 s initial backoff
constexpr uint32_t WIFI_RECONNECT_MAX_DELAY_MS = 60000UL;  // 60 s maximum backoff
constexpr uint8_t WIFI_RECONNECT_MAX_ATTEMPTS = 10;        // reset after this many consecutive failures

// ─── Sensor polling ───────────────────────────────────────────────────────────
constexpr uint32_t SENSOR_READ_INTERVAL_MS = 30000;

// ─── Humidity LED thresholds ──────────────────────────────────────────────────
constexpr float HUMIDITY_MAX = 60.0f;
constexpr float HUMIDITY_MIN = 40.0f;
constexpr float HUMIDITY_MID = 50.0f;
constexpr uint8_t OUTSIDE_RANGE_FLASHTIME = 10U;

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
// V4 board - outside double garage uses different external RGB pins
// constexpr pin_size_t RED_PIN = PIN_A4;
// constexpr pin_size_t GREEN_PIN = PIN_A5;
// constexpr pin_size_t BLUE_PIN = PIN_A6;
// ─── External RGB LED pins ────────────────────────────────────────────────────
constexpr pin_size_t RED_PIN = PIN_A4;
constexpr pin_size_t GREEN_PIN = PIN_A3;
constexpr pin_size_t BLUE_PIN = 10;

// ─── UAP garage door status input pins (must be interrupt-capable) ────────────
#ifndef NOT_A_PIN
#define NOT_A_PIN 255
#endif
constexpr pin_size_t DOOR_IS_OPEN_STATUS_PIN = NOT_A_PIN;  // 9;		// set to NOT_A_PIN to disable door support
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
constexpr uint8_t WIFI_RECONNECT_MAX_ATTEMPTS = 10;        // cap reconnect backoff after this many failures

// ─── WiFi onboarding policy ───────────────────────────────────────────────────
constexpr uint32_t WIFI_AP_ENTRY_GRACE_MS = 10UL * 60UL * 1000UL;
constexpr uint8_t WIFI_AP_CREDENTIAL_FAILURE_THRESHOLD = 3U;
constexpr uint32_t WIFI_AP_IDLE_REBOOT_MS = 5UL * 60UL * 1000UL;

// ─── WiFi disconnect confirmation ─────────────────────────────────────────────
// The NINA SPI co-processor can return a transient non-WL_CONNECTED status for
// several seconds after a UDP receive burst.  Require both CONFIRM_COUNT missed
// polls AND at least MIN_WINDOW_MS elapsed before treating the drop as real.
constexpr uint8_t WIFI_DISCONNECT_CONFIRM_COUNT = 3U;
constexpr uint32_t WIFI_DISCONNECT_MIN_WINDOW_MS = 10000UL;  // 10 s — covers observed 3.5 s transient

// ─── WiFi NINA hard reset ─────────────────────────────────────────────────────
// WiFi.end() is a no-op on the MKR WiFi 1010 (wifiDriverDeinit() has empty body).
// SpiDrv::end() is the real hardware reset.  Limit hard resets to every Nth attempt
// to avoid over-cycling the NINA module and triggering the stuck NO_SHIELD state.
constexpr uint8_t WIFI_HARD_RESET_EVERY = 5U;

// ─── WiFi full system reset timeout ────────────────────────────────────────────
// If WiFi fails to connect for this duration, trigger a full SAMD+NINA reset.
// This prevents indefinite accumulation of firmware state corruption when WiFi
// is permanently unavailable.
constexpr uint32_t WIFI_FULL_RESET_TIMEOUT_MS = 15UL * 60UL * 1000UL;  // 15 minutes

// ─── Sensor polling ───────────────────────────────────────────────────────────
constexpr uint32_t SENSOR_READ_INTERVAL_MS = 30000;

// ─── Humidity LED thresholds ──────────────────────────────────────────────────
constexpr float HUMIDITY_MAX = 60.0f;
constexpr float HUMIDITY_MIN = 40.0f;
constexpr float HUMIDITY_MID = 50.0f;
constexpr uint8_t OUTSIDE_RANGE_FLASHTIME = 10U;

#pragma once
/**
 * @file ConfigStorage.h
 * @brief Persists and retrieves configuration using BlobStorage library.
 * @details Provides persistent storage of WiFi credentials and UDP port settings.
 *
 * Author: (c) M. Naylor 2025
 *
 * History:
 *     Ver 1.0        Initial version
 */

#include <Arduino.h>

struct GarageConfig
{
	char ssid[ 64 ];             // WiFi SSID
	char password[ 64 ];         // WiFi password
	uint16_t udpPort;            // UDP port for receiving messages
	uint16_t multicastPort;      // UDP port for sending multicast messages
	char hostname[ 32 ];         // Device hostname
	float altitudeCompensation;  // Altitude compensation in meters for BME280
	bool valid;                  // Flag indicating if the config is valid
};

namespace ConfigStorage
{
bool begin ();
bool load ( GarageConfig& cfg );
bool save ( const GarageConfig& cfg );
bool clear ();
}  // namespace ConfigStorage

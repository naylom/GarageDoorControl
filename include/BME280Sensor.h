#pragma once
/*
 * BME280Sensor.h
 *
 * Concrete implementation of IEnvironmentSensor for the Bosch BME280/BMP280
 * over I2C.  Uses the finitespace/BME280 Arduino library.
 *
 * IsPresent() performs a lightweight I2C probe so the application can detect
 * the sensor at runtime without any compile-time #ifdef.
 *
 * Author: (c) M. Naylor 2026
 *
 * History:
 *   Ver 1.0   Phase 5 — initial implementation
 */

#include "IEnvironmentSensor.h"

#include <BME280I2C.h>

class BME280Sensor : public IEnvironmentSensor
{
public:
	explicit BME280Sensor ( float altitudeMeters = 0.0f );

	// Probes the I2C bus — returns true if a device acknowledges at address 0x76.
	// Wire.begin() is called here so the bus is ready before any other I2C use.
	bool IsPresent () override;

	// Initialises the BME280 library — call only after IsPresent() returns true.
	// Returns false when the library fails to communicate with the chip.
	bool Begin () override;

	// Populates result with a fresh reading (temperature, humidity, sea-level
	// pressure, dew point, millis timestamp).  Returns false if Begin() was
	// never called successfully.
	bool Read ( EnvironmentReading& result ) override;

private:
	BME280I2C m_bme;
	bool m_initialized = false;
};

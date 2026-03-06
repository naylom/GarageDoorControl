#pragma once
/*
 * IEnvironmentSensor.h
 *
 * Abstract interface for any environment sensor.
 * First implementation: BME280Sensor
 *
 * Author: (c) M. Naylor 2026
 *
 * History:
 *   Ver 1.0   Phase 3 — interface definition only
 */

#include <stdint.h>

struct EnvironmentReading
{
	float temperature;     // Celsius
	float humidity;        // %RH
	float pressure;        // hPa, altitude-compensated to sea level
	float dewpoint;        // Celsius
	uint32_t timestampMs;  // millis() at time of reading
	bool valid;            // false until first successful read
};

class IEnvironmentSensor
{
public:
	// altitudeMeters — used for sea-level pressure compensation;
	// ignored by sensors that do not need it
	explicit IEnvironmentSensor ( float altitudeMeters = 0.0f ) : m_altitude ( altitudeMeters )
	{
	}
	virtual ~IEnvironmentSensor () = default;

	// Probes hardware (e.g. I2C scan) — call once in setup()
	virtual bool IsPresent () = 0;
	virtual bool Begin () = 0;

	// Populates result; returns false if sensor not ready
	virtual bool Read ( EnvironmentReading& result ) = 0;

protected:
	float m_altitude;  // metres above sea level, loaded from ConfigStore
};

/*
 * BME280Sensor.cpp
 *
 * See BME280Sensor.h for interface documentation.
 *
 * Author: (c) M. Naylor 2026
 *
 * History:
 *   Ver 1.0   Phase 5 — initial implementation
 */

#include "BME280Sensor.h"

#include "Display.h"

#include <EnvironmentCalculations.h>
#include <Wire.h>

// ─── Constructor ──────────────────────────────────────────────────────────────
/**
 * @brief Constructs the sensor wrapper with the given altitude compensation value.
 * @details Configures the BME280 with 2x oversampling on all channels, normal
 *          continuous mode, 250 ms standby, no IIR filter, and I2C address 0x76.
 *          Call IsPresent() then Begin() before using Read().
 * @param altitudeMeters Altitude above sea level in metres used to correct raw
 *                       pressure readings to sea-level equivalent.
 */
BME280Sensor::BME280Sensor ( float altitudeMeters )
    : IEnvironmentSensor ( altitudeMeters ), m_bme ( BME280I2C::Settings ( BME280::OSR_X2,
                                                                           BME280::OSR_X2,
                                                                           BME280::OSR_X2,
                                                                           BME280::Mode_Normal,
                                                                           BME280::StandbyTime_250ms,
                                                                           BME280::Filter_Off,
                                                                           BME280::SpiEnable_False,
                                                                           BME280I2C::I2CAddr_0x76 ) )
{
}

// ─── IsPresent ────────────────────────────────────────────────────────────────
/**
 * @brief Probes the I2C bus to determine whether a BME280 is physically connected.
 * @details Calls Wire.begin() (safe to call multiple times) and performs a
 *          zero-byte transmission to I2C address 0x76, checking for an ACK.
 * @return true if the device acknowledges (sensor is wired and powered).
 */
bool BME280Sensor::IsPresent ()
{
	Wire.begin();
	Wire.beginTransmission ( 0x76 );
	return Wire.endTransmission() == 0;
}

// ─── Begin ────────────────────────────────────────────────────────────────────
/**
 * @brief Initialises the BME280 sensor and verifies the chip model.
 * @details Must be called after IsPresent() returns true. Sets m_initialized
 *          so that subsequent Read() calls are permitted. Logs success or failure
 *          via Info()/Error().
 * @return true if the sensor initialised successfully and reported a supported
 *         chip model (BME280 or BMP280); false otherwise.
 */
bool BME280Sensor::Begin ()
{
	if ( !m_bme.begin() )
	{
		Error ( F ( "Could not find BME280 sensor!" ) );
		return false;
	}

	switch ( m_bme.chipModel() )
	{
		case BME280::ChipModel_BME280:
			Info ( F ( "Found BME280 sensor! Success." ) );
			break;
		case BME280::ChipModel_BMP280:
			Info ( F ( "Found BMP280 sensor! No Humidity available." ) );
			break;
		default:
			Error ( F ( "Found UNKNOWN sensor! Error!" ) );
			return false;
	}

	m_initialized = true;
	return true;
}

// ─── Read ─────────────────────────────────────────────────────────────────────
/**
 * @brief Reads the current temperature, humidity, pressure, and dew-point from the sensor.
 * @details Pressure is corrected to sea-level equivalent using the altitude set
 *          at construction. The result is also cached for retrieval via GetLastReading().
 *          Returns false immediately if Begin() has not been successfully called.
 * @param result Output structure that receives all four measurements plus a
 *               validity flag and the millis() timestamp at the time of reading.
 * @return true if the read succeeded and result is valid; false if not initialised.
 */
bool BME280Sensor::Read ( EnvironmentReading& result )
{
	if ( !m_initialized )
	{
		return false;
	}

	float temp = 0.0f, hum = 0.0f, pres = 0.0f;
	m_bme.read ( pres, temp, hum, BME280::TempUnit::TempUnit_Celsius, BME280::PresUnit::PresUnit_hPa );

	result.temperature = temp;
	result.humidity = hum;
	result.pressure = EnvironmentCalculations::EquivalentSeaLevelPressure ( m_altitude, temp, pres );
	result.dewpoint = EnvironmentCalculations::DewPoint ( temp, hum );
	result.timestampMs = millis();
	result.valid = true;
	m_lastReading = result;
	return true;
}

// ─── GetLastReading ───────────────────────────────────────────────────────────
/**
 * @brief Returns the most recently cached sensor reading without triggering a new I2C transaction.
 * @return Const reference to the internally stored EnvironmentReading. The
 *         `valid` field will be false until the first successful Read() call.
 */
const EnvironmentReading& BME280Sensor::GetLastReading () const
{
	return m_lastReading;
}

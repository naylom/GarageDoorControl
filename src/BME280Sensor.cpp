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
// Builds the BME280I2C object with the same oversampling / mode settings that
// were used in the original Application.cpp globals.

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
// Performs a lightweight I2C probe: sends the device address and checks
// whether it acknowledges.  Wire.begin() is safe to call multiple times.

bool BME280Sensor::IsPresent ()
{
	Wire.begin();
	Wire.beginTransmission ( 0x76 );
	return Wire.endTransmission() == 0;
}

// ─── Begin ────────────────────────────────────────────────────────────────────

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

const EnvironmentReading& BME280Sensor::GetLastReading () const
{
	return m_lastReading;
}

//
// Author M.Naylor
// Date Feb 2021
// derived class to encapsulate SHT35-D,SHT31 style Temperature and Humidity sensors.
//

#include "SHTTempHumSensors.h"
#include "logging.h"
extern void Error ( String s );
SHTTempHumSensorsClass::~SHTTempHumSensorsClass ()
{
	m_mySensor.reset ();
	Wire.end ();
}

uint8_t SHTTempHumSensorsClass::GetDeviceAddress ()
{
	return m_bAddressDevice;
}

bool SHTTempHumSensorsClass::ReadSensor ()
{
	bool   bResult = false;
	SHT31D result  = m_mySensor.readTempAndHumidity ( SHT3XD_REPEATABILITY_HIGH, SHT3XD_MODE_POLLING, 50 );
	if ( result.error == SHT3XD_NO_ERROR )
	{
		m_fLastTemperatureReading = result.t;
		m_fLastHumidityReading	  = result.rh;
		m_fLastDewPointReading	  = CalcDewPoint ( m_fLastTemperatureReading, m_fLastHumidityReading );
		bResult					  = true;
	}
	else
	{
		Error ( "SHTTempHumSensorsClass::ReadSensor : read error - " + String ( result.error ) );
	}

	return bResult;
}

void SHTTempHumSensorsClass::init ()
{
	Wire.begin ();
	SHT31D_ErrorCode result = m_mySensor.begin ( m_bAddressDevice );
	if ( result != SHT3XD_NO_ERROR )
	{
		Error ( "SHTTempHumSensorsClass::init : begin error - " + String ( result ) );
	}
}

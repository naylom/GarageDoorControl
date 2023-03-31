//
// Author: M Naylor Feb 2021
//
//  Base class for temperature and humidity sensors
//
#include <Arduino.h>
#include "TempHumSensor.h"
#include "Logging.h"

float TempHumSensorClass::CalcDewPoint ( float fTemperature, float fHumidity )
{
	const float a	  = 17.271;
	const float b	  = 237.7;
	float		fTemp = ( a * fTemperature ) / ( b + fTemperature ) + log ( fHumidity / 100.0 );
	float		Td	  = ( b * fTemp ) / ( a - fTemp );
	return Td;
}

TempHumSensorClass::TempHumSensorClass ()
{
	init ();
}

void TempHumSensorClass::init ()
{
	m_fLastDewPointReading	  = NAN;
	m_fLastHumidityReading	  = NAN;
	m_fLastTemperatureReading = NAN;
	m_ulTimeOfLastReading	  = 0UL;
	SetShelfLifems ( SHELFLIFE_MS );
	SetSensorTimeoutms ( SENSOR_TIMEOUT_MS );
}

void TempHumSensorClass::SetShelfLifems ( unsigned long ulShelfLife )
{
	m_ulShelfLifems = ulShelfLife;
}

unsigned long TempHumSensorClass::GetShelfLifems ()
{
	return m_ulShelfLifems;
}

void TempHumSensorClass::SetSensorTimeoutms ( unsigned long ulTimeOutms )
{
	m_ulSensorTimeoutms = ulTimeOutms;
}

unsigned long TempHumSensorClass::GetSensorTimeoutms ()
{
	return m_ulSensorTimeoutms;
}

void TempHumSensorClass::GetLastReading ( THSENSOR_RESULT &sResult )
{
	if ( millis () - m_ulTimeOfLastReading > GetShelfLifems () )
	{
		// current readings stale so try and get more recent
		if ( ReadSensor () )
		{
			// Got new reading
			m_ulTimeOfLastReading = millis ();
		}
	}
	sResult.fTemperature	  = m_fLastTemperatureReading;
	sResult.fHumidity		  = m_fLastHumidityReading;
	sResult.fDewPoint		  = m_fLastDewPointReading;
	sResult.ulTimeOfReadingms = m_ulTimeOfLastReading;
}

#pragma once

typedef struct
{
	float			fTemperature;
	float			fHumidity;
	float			fDewPoint;
	unsigned long	ulTimeOfReadingms;
	unsigned long	ulSensorTimeOut;
} THSENSOR_RESULT;

constexpr auto SHELFLIFE_MS			= 1000;
constexpr auto SENSOR_TIMEOUT_MS	= 5500;

class TempHumSensorClass
{
 protected:
	float      		m_fLastTemperatureReading;
	float      		m_fLastHumidityReading;
	float      		m_fLastDewPointReading;
	unsigned long  	m_ulTimeOfLastReading;
	unsigned long  	m_ulShelfLifems;          	// # ms after which data is considered stale and needs to be refreshed
	unsigned long  	m_ulSensorTimeoutms;		// # ms to wait for sensor reading

	float 			CalcDewPoint ( float fTemperature, float fHumidity );
	void 			init ();
	virtual bool 	ReadSensor () = 0;

 public:
					TempHumSensorClass ();  
	THSENSOR_RESULT GetLastReading ();
	void 			SetShelfLifems ( unsigned long ulShelfLife );
	unsigned long 	GetShelfLifems ();
	void 			SetSensorTimeoutms ( unsigned long ulTimeOutms );
	unsigned long 	GetSensorTimeoutms ();
};

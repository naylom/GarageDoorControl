#pragma once
#include "TempHumSensor.h"
// Wire for samd - Version: Latest
#include <Wire.h>
// ClosedCube SHT31D - Version: Latest
#include <ClosedCube_SHT31D.h>

class SHTTempHumSensorsClass : public TempHumSensorClass
{
	protected:
		ClosedCube_SHT31D m_mySensor;
		uint8_t			  m_bAddressDevice;

		void			  init ();
		bool			  ReadSensor ();

	public:

		SHTTempHumSensorsClass ( uint8_t bAddressDevice ) : m_bAddressDevice { bAddressDevice }
		{
			init ();
		}

		~SHTTempHumSensorsClass ();
		uint8_t GetDeviceAddress ();
};

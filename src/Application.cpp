/**
 * @file Application.cpp
 * @brief Implements the Application class (begin / loop lifecycle).
 *
 * File-scope globals are defined here rather than in main.cpp so that the
 * extern declarations in Display.cpp and DoorState.cpp continue to resolve
 * without modification.  These will be migrated to proper member variables
 * when Display.cpp and DoorState.cpp are refactored in later phases.
 */

#include "Application.h"

#include "Display.h"

#include <BME280.h>
#include <BME280I2C.h>
#include <EnvironmentCalculations.h>
#include <MNPCIHandler.h>
#include <MNRGBLEDBaseLib.h>
#include <MNTimerLib.h>
#include <time.h>
#include <WiFiNINA.h>
#include <WiFiUdp.h>
#include <Wire.h>

// ─── Version string (extern'd by Display.cpp) ────────────────────────────────
const char* VERSION = "1.0.17 Beta";

// ─── Logger (extern'd by Display.cpp) ────────────────────────────────────────
#ifdef MNDEBUG
#ifdef TELNET
ansiVT220Logger MyLogger ( Telnet );
#else
SerialLogger slog;
ansiVT220Logger MyLogger ( slog );
#endif
#endif

// ─── BME280 sensor and environment data (EnvironmentResults extern'd by Display.cpp) ──
#ifdef BME280_SUPPORT
struct TEMP_STATS
{
	float temperature;
	float pressure;  // at sea level
	float humidity;
	float dewpoint;
	uint32_t ulTimeOfReadingms;
} EnvironmentResults = { NAN, NAN, NAN, 0UL };

BME280I2C::Settings settings ( BME280::OSR_X2,
                               BME280::OSR_X2,
                               BME280::OSR_X2,
                               BME280::Mode_Normal,
                               BME280::StandbyTime_250ms,
                               BME280::Filter_Off,
                               BME280::SpiEnable_False,
                               BME280I2C::I2CAddr_0x76 );
BME280I2C MyBME280 ( settings );
#endif

// ─── Garage door state (pGarageDoor extern'd by Display.cpp) ─────────────────
#ifdef UAP_SUPPORT
HormannUAP1WithSwitch* pGarageDoor = nullptr;
#endif

// ─── UDP WiFi service (extern'd by Display.cpp and DoorState.cpp) ────────────
UDPWiFiService* pMyUDPService = nullptr;

// ─── External RGB LED ────────────────────────────────────────────────────────
MNRGBLEDBaseLib* pMyLED = nullptr;

// ─── Misc globals ─────────────────────────────────────────────────────────────
unsigned long ulLastClientReq = 0UL;

// ─── Application implementation ───────────────────────────────────────────────

Application::Application ()
{
	// Initialise the external LED object during global init so it exists before
	// begin() drives its pins low (matches original global-variable behaviour).
	pMyLED = new CRGBLED ( RED_PIN, GREEN_PIN, BLUE_PIN, 255, 180, 120 );
}

void Application::begin ()
{
	// Drive external LED pins low immediately — before any other peripheral
	// initialisation.  SAMD21 pins default to floating inputs; without this,
	// SPI/I2C/timer ISR activity induces noise on the pins causing visible
	// colour glitching on the external LED during startup.
	pMyLED->SetLEDColour ( MNRGBLEDBaseLib::eColour::BLACK, 0 );

	MyLogger.LogStart();
	MyLogger.ClearScreen();

	pMyUDPService = new UDPWiFiService();

	// now we have state table set up and temp sensor configured, allow users to query state

	TheMKR_RGB_LED.Invert();  // Only if required!

	// Generate dynamic AP SSID based on MAC address
	String apSSID;
	byte mac [ 6 ];
	WiFi.macAddress ( mac );

#ifdef UAP_SUPPORT
	apSSID = "GARAGE_CONTROL_";
#else
	apSSID = "TEMP_HUMID_";
#endif
	// Append last 3 bytes of MAC address
	char macStr [ 9 ];
	sprintf ( macStr, "%02X%02X%02X", mac [ 3 ], mac [ 4 ], mac [ 5 ] );
	apSSID += macStr;
	Info ( F ( "Starting WiFi with onboarding support" ) );
	Info ( "AP SSID will be: " + apSSID );
	if ( !pMyUDPService->Begin ( Application::processUDPMsg, apSSID.c_str(), nullptr, &TheMKR_RGB_LED ) )
	{
		Error ( F ( "WiFi initialization failed" ) );
	}

#ifdef BME280_SUPPORT
	Wire.begin();
	if ( !MyBME280.begin() )
	{
		Error ( F ( "Could not find BME280 sensor!" ) );
		delay ( 1000 );
	}
	else
	{
		switch ( MyBME280.chipModel() )
		{
			case BME280::ChipModel_BME280:
				Info ( F ( "Found BME280 sensor! Success." ) );
				break;
			case BME280::ChipModel_BMP280:
				Info ( F ( "Found BMP280 sensor! No Humidity available." ) );
				break;
			default:
				Error ( F ( "Found UNKNOWN sensor! Error!" ) );
		}
	}
	DisplaylastInfoErrorMsg();
#endif

#ifdef UAP_SUPPORT
	// Setup so we are called if the state of door changes
	pGarageDoor = new HormannUAP1WithSwitch ( OPEN_DOOR_OUTPUT_PIN,
	                                          CLOSE_DOOR_OUTPUT_PIN,
	                                          STOP_DOOR_OUTPUT_PIN,
	                                          TURN_LIGHT_ON_OUTPUT_PIN,
	                                          DOOR_IS_OPEN_STATUS_PIN,
	                                          DOOR_IS_CLOSED_STATUS_PIN,
	                                          LIGHT_IS_ON_STATUS_PIN,
	                                          DOOR_SWITCH_INPUT_PIN );
	setLED();
#endif
}

// ─── setLED ───────────────────────────────────────────────────────────────────
#ifdef UAP_SUPPORT
// set the colour of the inbuilt MKR WiFi 1010 RGB LED based on the current door state
void Application::setLED ()
{
	static IGarageDoor::State OldState = IGarageDoor::State::Opening;
	IGarageDoor::State currentState = IGarageDoor::State::Unknown;
	if ( pGarageDoor != nullptr )
	{
		currentState = pGarageDoor->GetState();
	}

	if ( currentState != OldState )
	{
		OldState = currentState;

		switch ( currentState )
		{
			case IGarageDoor::State::Closed:
				pMyLED->SetLEDColour ( DOOR_CLOSED_COLOUR, DOOR_STATIONARY_FLASHTIME );
				break;

			case IGarageDoor::State::Closing:
				pMyLED->SetLEDColour ( DOOR_CLOSED_COLOUR, DOOR_MOVING_FLASHTIME );
				break;

			case IGarageDoor::State::Open:
				pMyLED->SetLEDColour ( DOOR_OPEN_COLOUR, DOOR_STATIONARY_FLASHTIME );
				break;

			case IGarageDoor::State::Opening:
				pMyLED->SetLEDColour ( DOOR_OPEN_COLOUR, DOOR_MOVING_FLASHTIME );
				break;

			case IGarageDoor::State::Stopped:
				pMyLED->SetLEDColour ( DOOR_STOPPED_COLOUR, DOOR_STATIONARY_FLASHTIME );
				break;

			case IGarageDoor::State::Bad:
				pMyLED->SetLEDColour ( DOOR_BAD_COLOUR, DOOR_MOVING_FLASHTIME );
				break;

			case IGarageDoor::State::Unknown:
				pMyLED->SetLEDColour ( DOOR_UNKNOWN_COLOUR, DOOR_MOVING_FLASHTIME );
				break;
		}
	}
}
#else
// When not showing the door (UAP) status then show the humidity status
void Application::setLED ()
{
	// In AP mode the WiFiService owns the LED colour — don't override it.
	if ( pMyUDPService != nullptr && pMyUDPService->GetState() == WiFiService::Status::AP_MODE )
	{
		return;
	}

	uint8_t red, green, blue;
	bool bOutsideRange = false;
	uint8_t Flashtime = 0U;
	float constrainedHumidity;
	static float OldHumidity = NAN;

	// calculate color component — thresholds are in config.h

	// Guard against NaN (sensor not yet read or not present).
	if ( isnan ( EnvironmentResults.humidity ) )
	{
		return;
	}

	if ( EnvironmentResults.humidity == OldHumidity )
	{
		return;
	}
	else
	{
		OldHumidity = EnvironmentResults.humidity;
	}
	constrainedHumidity = max ( EnvironmentResults.humidity, HUMIDITY_MIN );
	constrainedHumidity = min ( constrainedHumidity, HUMIDITY_MAX );
	if ( EnvironmentResults.humidity > HUMIDITY_MAX || EnvironmentResults.humidity < HUMIDITY_MIN )
	{
		Flashtime = OUTSIDE_RANGE_FLASHTIME;
	}

	// red level indicates how dry
	if ( constrainedHumidity < HUMIDITY_MID )
	{
		red = ( HUMIDITY_MID - constrainedHumidity ) * 255.0 / ( HUMIDITY_MID - HUMIDITY_MIN );
	}
	else
	{
		red = 0;
	}
	// blue level indicates how wet
	if ( EnvironmentResults.humidity > HUMIDITY_MID )
	{
		blue = ( constrainedHumidity - HUMIDITY_MID ) * 255.0 / ( HUMIDITY_MAX - HUMIDITY_MID );
	}
	else
	{
		blue = 0;
	}
	// green level indicates how close to wanted level
	green =
	    255.0 - ( ( abs ( constrainedHumidity - HUMIDITY_MID ) * 255.0 ) / ( ( HUMIDITY_MAX - HUMIDITY_MIN ) / 2.0 ) );
	pMyLED->SetLEDColour ( RGB ( red, green, blue ), Flashtime );

	MyLogger.AT ( 3, 1, "Red   :" );
	MyLogger.AT ( 4, 1, "Green :" );
	MyLogger.AT ( 5, 1, "Blue  :" );
	MyLogger.ClearPartofLine ( 3, 8, 3 );
	MyLogger.ClearPartofLine ( 4, 8, 3 );
	MyLogger.ClearPartofLine ( 5, 8, 3 );
	MyLogger.AT ( 3, 8, String ( red ) );
	MyLogger.AT ( 4, 8, String ( green ) );
	MyLogger.AT ( 5, 8, String ( blue ) );
}
#endif

// ─── loop ─────────────────────────────────────────────────────────────────────
void Application::loop ()
{
#ifdef BME280_SUPPORT
	static unsigned long ulLastSensorTime = millis() - SENSOR_READ_INTERVAL_MS;
#endif

	static unsigned long ulLastDisplayTime = 0UL;

#ifdef UAP_SUPPORT
	static IGarageDoor::State LastDoorState = IGarageDoor::State::Unknown;
	static bool LastLightState = false;

	// set initial light state
	if ( pGarageDoor != nullptr && ulLastDisplayTime == 0UL )
	{
		LastLightState = !pGarageDoor->IsLit();
	}
#endif
	// set LED — noInterrupts() prevents Flash() ISR racing against analogWrite()
	// on the external LED's PWM registers, causing visible intensity flicker.
	noInterrupts();
	setLED();
	interrupts();

	// Process onboarding if in AP mode
	pMyUDPService->ProcessOnboarding();

	// See if we have any udp requests to action
	pMyUDPService->CheckUDP();

#ifdef BME280_SUPPORT
	if ( pMyUDPService->GetState() != WiFiService::Status::AP_MODE &&
	     millis() - ulLastSensorTime > SENSOR_READ_INTERVAL_MS )
	{
		MyBME280.read ( EnvironmentResults.pressure,
		                EnvironmentResults.temperature,
		                EnvironmentResults.humidity,
		                BME280::TempUnit::TempUnit_Celsius,
		                BME280::PresUnit::PresUnit_hPa );
		// Info ( "Temperature: " + String ( EnvironmentResults.temperature ) + "C" );
		// Use altitude compensation from config
		float altitudeCompensation = pMyUDPService->GetAltitudeCompensation();
		EnvironmentResults.pressure =
		    EnvironmentCalculations::EquivalentSeaLevelPressure ( altitudeCompensation,
		                                                          EnvironmentResults.temperature,
		                                                          EnvironmentResults.pressure );
		EnvironmentResults.dewpoint =
		    EnvironmentCalculations::DewPoint ( EnvironmentResults.temperature, EnvironmentResults.humidity );
		EnvironmentResults.ulTimeOfReadingms = pMyUDPService->GetTime();
		multicastMsg ( UDPWiFiService::ReqMsgType::TEMPDATA );
		// reset time counter
		ulLastSensorTime = millis();
	}
#endif

	// update debug stats every 1/2 second
	if ( millis() - ulLastDisplayTime > 500 )
	{
		ulLastDisplayTime = millis();
		DisplayStats();
	}

#ifdef UAP_SUPPORT
	// if door state has changed, multicast news
	if ( pGarageDoor != nullptr )
	{
		pGarageDoor->Update();
		if ( pGarageDoor->GetState() != LastDoorState || LastLightState != pGarageDoor->IsLit() )
		{
			LastDoorState = pGarageDoor->GetState();
			LastLightState = pGarageDoor->IsLit();
			multicastMsg ( UDPWiFiService::ReqMsgType::DOORDATA );
		}
	}
	if ( pGarageDoor->IsSwitchConfigured() && pMyUDPService != nullptr )
	{
		static uint16_t SwitchPressedCount = 0;
		uint16_t LatestSwitchPressedCount = pGarageDoor->GetSwitchMatchCount();
		if ( LatestSwitchPressedCount > SwitchPressedCount )
		{
			SwitchPressedCount = LatestSwitchPressedCount;
		}
	}
#endif
}

// ─── processUDPMsg (static — satisfies UDPWiFiServiceCallback signature) ──────
void Application::processUDPMsg ( UDPWiFiService::ReqMsgType eReqType )
{
	String sResponse;
	buildMessage ( eReqType, sResponse );
	if ( sResponse.length() > 0 )
	{
		pMyUDPService->SendReply ( sResponse );
	}
}

// ─── multicastMsg ─────────────────────────────────────────────────────────────
void Application::multicastMsg ( UDPWiFiService::ReqMsgType eReqType )
{
	String sResponse;
	buildMessage ( eReqType, sResponse );
	if ( sResponse.length() > 0 )
	{
		pMyUDPService->SendAll ( sResponse );
	}
}

// ─── buildMessage ─────────────────────────────────────────────────────────────
// Called to generate a response to a command.
// Returns an empty string if no response is required (i.e. action-only command).
void Application::buildMessage ( UDPWiFiService::ReqMsgType eReqType, String& sResponse )
{
	switch ( eReqType )
	{
		case UDPWiFiService::ReqMsgType::TEMPDATA:
#ifdef BME280_SUPPORT
			sResponse = F ( "T=" );
			sResponse += EnvironmentResults.temperature;
			sResponse += F ( ",H=" );
			sResponse += EnvironmentResults.humidity;
			sResponse += F ( ",D=" );
			sResponse += EnvironmentResults.dewpoint;
			sResponse += F ( ",P=" );
			sResponse += EnvironmentResults.pressure;
			sResponse += F ( ",A=" );
			sResponse += EnvironmentResults.ulTimeOfReadingms;
			sResponse += F ( "\r" );
#endif
			break;

#ifdef UAP_SUPPORT
		case UDPWiFiService::ReqMsgType::DOORDATA:
			if ( pGarageDoor != nullptr )
			{
				sResponse = F ( "S=" );
				sResponse += pGarageDoor->GetStateDisplayString();  // Door State
				sResponse += F ( ",L=" );
				sResponse += pGarageDoor->IsLit() ? F ( "On" ) : F ( "Off" );  // Light on or not
				sResponse += F ( ",C=" );
				sResponse += pGarageDoor->IsClosed() ? F ( "Y" ) : F ( "N" );  // Closed or not
				sResponse += F ( ",O=" );
				sResponse += pGarageDoor->IsOpen() ? F ( "Y" ) : F ( "N" );  // Open or not
				sResponse += F ( ",M=" );
				sResponse += pGarageDoor->IsMoving() ? F ( "Y" ) : F ( "N" );  // Moving or not
				sResponse += F ( ",A=" );
				sResponse += pMyUDPService->GetTime();  // current epoch time
				sResponse += F ( "\r" );
			}
			else
			{
				Error ( F ( "Door data unavailable: pGarageDoor is null" ) );
			}
			break;

		case UDPWiFiService::ReqMsgType::DOOROPEN:
			if ( pGarageDoor != nullptr )
			{
				pGarageDoor->Open();
			}
			break;

		case UDPWiFiService::ReqMsgType::DOORCLOSE:
			if ( pGarageDoor != nullptr )
			{
				pGarageDoor->Close();
			}
			break;

		case UDPWiFiService::ReqMsgType::DOORSTOP:
			if ( pGarageDoor != nullptr )
			{
				pGarageDoor->Stop();
			}
			break;

		case UDPWiFiService::ReqMsgType::LIGHTON:
			if ( pGarageDoor != nullptr )
			{
				pGarageDoor->LightOn();
			}
			break;

		case UDPWiFiService::ReqMsgType::LIGHTOFF:
			if ( pGarageDoor != nullptr )
			{
				pGarageDoor->LightOff();
			}
			break;
#endif
	}
}

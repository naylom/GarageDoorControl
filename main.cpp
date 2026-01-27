#include <BME280.h>
#include <BME280I2C.h>
#include <EnvironmentCalculations.h>
#include <Wire.h>
#include <MNPCIHandler.h>
#include <MNRGBLEDBaseLib.h>
#include <MNTimerLib.h>
#include <WiFiNINA.h>
#include <WiFiUdp.h>
#include <time.h>
#include "GarageControl.h"
#include "WiFiService.h"
#include "logging.h"
#include "Display.h"

/*

GarageDoorControl.ino

Arduino sketch to control a Hormann Garage door via a Horman UAP1
Also collects the temperature, humidity and barometric pressure
Door state and humidity can be queried by remote client sending and receiving UDP messages - see WiFiService files

This is designed to use two RGB LEDS to display status
An external RGB LED displays the door state when we are connected to a UAP
WHITE, no flash  - state unknown
GREEN, no flash	 - Door Closed
GREEN, flashing  - Door Closing
RED, no flash	 - Door Open
RED, flashing    - Door Opening
PURPLE, no flash - Door Stopped
BLUE, flashing	 - Door unknown state, not open and not closed ie in transit but we cannot determine which
YELLOW, flashing - Door in a BAD State, UAP says open and closed at same time.
When not connected to a UAP the colours indicate how close to teh desired humidity threshold it is
Green - at desired level, the more red the drier and the more blue the more humid it is. Will flash when above min or max threshold

The built in MKR WiFi 1010 RGB LED displays the WiFI status

Author: (c) M. Naylor 2022

History:
	Ver 1.0			Initial version
	Ver 1.0.4		Supports config to add/remove UAP, Distance Centre, Barometruc sensor
	Ver 1.0.5		Add ability to get loggging data over a telnet connection on 0xFEEE
	Ver 1.0.6       Changed input pins to be simply INPUT and use external pulldown resistors
	Ver 1.0.7		Moved all input and out pins into DoorState, added InputPin class
	Ver 1.0.8		Moved logging to object SerialLogger
	Ver 1.0.10      Added BME280 support and changed logging to inherit from Stream class
	Ver 1.0.11      Added external LED usage in Non UAP mode to show how far from desired humidity we are
	Ver 1.0.12		Detect door state in main loop rather than calc on pin change
	Ver 1.0.13		Improved encapsulation of InputPin
	Ver 1.0.15		Moved display code to own file
	Ver 1.0.16      Changed InputPin to use enum class for events and made action functions const
*/
const char * VERSION = "1.0.16 Beta";


#ifdef MNDEBUG
	#ifdef TELNET
		ansiVT220Logger MyLogger ( Telnet );
	#else
		SerialLogger	slog;
		ansiVT220Logger MyLogger ( slog ); // create serial comms object to log to
	#endif
#endif




#ifdef BME280_SUPPORT // Temp, humidity and pressure sensor

struct TEMP_STATS
{
		float	 temperature;
		float	 pressure; // at sea level
		float	 humidity;
		float	 dewpoint;
		uint32_t ulTimeOfReadingms;
} EnvironmentResults					  = { NAN, NAN, NAN, 0UL };

constexpr float		ALTITUDE_COMPENSATION = 131.0; // sensor is 135 metres aboves sea level, we need this to adjust pressure reading to sea level equivalent.
BME280I2C::Settings settings ( BME280::OSR_X2, BME280::OSR_X2, BME280::OSR_X2, BME280::Mode_Normal, BME280::StandbyTime_250ms, BME280::Filter_Off, BME280::SpiEnable_False, BME280I2C::I2CAddr_0x76 );
BME280I2C			MyBME280 ( settings );
#endif

#ifdef UAP_SUPPORT
	#include "DoorState.h"
// PIN allocations, input & output from arduino perspective

// Need to be interrupt pins, inputs of status from UAP
constexpr pin_size_t DOOR_IS_OPEN_STATUS_PIN   = 9;
constexpr uint8_t	 DOOR_IS_CLOSED_STATUS_PIN = 8;
constexpr uint8_t	 LIGHT_IS_ON_STATUS_PIN	   = 7;
constexpr uint8_t	 DOOR_SWITCH_INPUT_PIN	   = 0;
// Don't need to be interrupt pins, outputs to UAP
constexpr uint8_t	 TURN_LIGHT_ON_OUTPUT_PIN  = 2;
constexpr uint8_t	 CLOSE_DOOR_OUTPUT_PIN	   = 3;
constexpr uint8_t	 OPEN_DOOR_OUTPUT_PIN	   = 4;
constexpr uint8_t	 STOP_DOOR_OUTPUT_PIN	   = 5;

//constexpr uint32_t	 SWITCH_DEBOUNCE_MS		   = 100;  // min ms between consecutive pin interrupts before signal accepted from manual switch
//constexpr uint32_t	 MAX_SWITCH_MATCH_TIMER_MS = 2000; // max time pin should be in matched state to be considered a real signal
DoorState			*pGarageDoor			   = nullptr;
//DoorStatusPin		*pDoorSwitchPin			   = nullptr;

#endif

constexpr pin_size_t RED_PIN	= PIN_A4;
constexpr pin_size_t GREEN_PIN	= PIN_A3;
constexpr pin_size_t BLUE_PIN	= 10;
MNRGBLEDBaseLib	 *pMyLED	= new CRGBLED ( RED_PIN, GREEN_PIN, BLUE_PIN, 255, 180, 120 ); // new CRGBLED ( RED_PIN, GREEN_PIN, BLUE_PIN, 255, 90, 60 );

/*
	WiFi config
*/
constexpr char	  ssid []	= "Naylorfamily"; // your network SSID (name)
constexpr char	  pass []	= "welcome1";	  // your network password
#ifdef UAP_SUPPORT
constexpr char MyHostName [] = "GarageControl2";
#else
constexpr char MyHostName [] = "OfficeTHSensor";
#endif

UDPWiFiService	 *pMyUDPService	   = nullptr;

unsigned long	  ulLastClientReq  = 0UL; // millis of last wifi incoming message'

// forward references
void BuildMessage ( UDPWiFiService::ReqMsgType eReqType, String &sResponse );
void SetLED ();

void MulticastMsg ( UDPWiFiService::ReqMsgType eReqType )
{
	String sResponse;
	BuildMessage ( eReqType, sResponse );
	if ( sResponse.length () > 0 )
	{
		pMyUDPService->SendAll ( sResponse );
	}
}

void ProcessUDPMsg ( UDPWiFiService::ReqMsgType eReqType )
{
	String sResponse;
	BuildMessage ( eReqType, sResponse );
	if ( sResponse.length () > 0 )
	{
		pMyUDPService->SendReply ( sResponse );
	}
}

// main setup routine
void setup ()
{
	//Serial.begin ( 115200 );
	//while ( !Serial )
		;
	MyLogger.LogStart ();
	MyLogger.ClearScreen ();

	pMyUDPService = new UDPWiFiService ();

	// now we have state table set up and temp sensor configured, allow users to query state

	TheMKR_RGB_LED.Invert (); // Only if required!
	if ( !pMyUDPService->Begin ( ProcessUDPMsg, ssid, pass, MyHostName, &TheMKR_RGB_LED ) )
	{
		Error ( F ( "Cannot connect WiFI " ) );
	}

#ifdef BME280_SUPPORT
	Wire.begin ();
	if ( !MyBME280.begin () )
	{
		Error ( F ( "Could not find BME280 sensor!" ) );
		delay ( 1000 );
	}
	else
	{
		switch ( MyBME280.chipModel () )
		{
			case BME280::ChipModel_BME280:
				Info (  F ( "Found BME280 sensor! Success." ) );
				break;
			case BME280::ChipModel_BMP280:
				Info ( F ( "Found BMP280 sensor! No Humidity available." ) );
				break;
			default:
				Error ( F ( "Found UNKNOWN sensor! Error!" ) );
		}
	}
	DisplaylastInfoErrorMsg ();
#endif

#ifdef UAP_SUPPORT
	// Setup so we are called if the state of door changes
	pGarageDoor	   = new DoorState ( OPEN_DOOR_OUTPUT_PIN, CLOSE_DOOR_OUTPUT_PIN, STOP_DOOR_OUTPUT_PIN, TURN_LIGHT_ON_OUTPUT_PIN, DOOR_IS_OPEN_STATUS_PIN, DOOR_IS_CLOSED_STATUS_PIN, LIGHT_IS_ON_STATUS_PIN, DOOR_SWITCH_INPUT_PIN );
	// pDoorSwitchPin = new DoorStatusPin ( pGarageDoor, DoorState::Event::SwitchPress, DoorState::Event::Nothing, DOOR_SWITCH_INPUT_PIN, SWITCH_DEBOUNCE_MS, PinStatus::HIGH, PinMode::INPUT_PULLDOWN, PinStatus::CHANGE );
	//pDoorSwitchPin = new DoorStatusPin ( pGarageDoor, DoorState::Event::Nothing, DoorState::Event::SwitchPress, DOOR_SWITCH_INPUT_PIN, SWITCH_DEBOUNCE_MS, MAX_SWITCH_MATCH_TIMER_MS, PinStatus::HIGH, PinMode::INPUT_PULLDOWN, PinStatus::CHANGE );
	SetLED ();
#endif
}
#ifdef UAP_SUPPORT
// set the colour of the inbuilt MKR Wifi 1010 RGB LED based on the current door state
void SetLED ()
{
	static DoorState::State OldState	 = DoorState::State::Opening;
	DoorState::State		currentState = DoorState::State::Unknown;
	if ( pGarageDoor != nullptr )
	{
		currentState = pGarageDoor->GetDoorState ();
	}

	if ( currentState != OldState )
	{
		OldState = currentState;

		switch ( currentState )
		{
			case DoorState::State::Closed:
				pMyLED->SetLEDColour ( DOOR_CLOSED_COLOUR, DOOR_STATIONARY_FLASHTIME );
				break;

			case DoorState::State::Closing:
				pMyLED->SetLEDColour ( DOOR_CLOSED_COLOUR, DOOR_MOVING_FLASHTIME );
				break;

			case DoorState::State::Open:
				pMyLED->SetLEDColour ( DOOR_OPEN_COLOUR, DOOR_STATIONARY_FLASHTIME );
				break;

			case DoorState::State::Opening:
				pMyLED->SetLEDColour ( DOOR_OPEN_COLOUR, DOOR_MOVING_FLASHTIME );
				break;

			case DoorState::State::Stopped:
				pMyLED->SetLEDColour ( DOOR_STOPPED_COLOUR, DOOR_STATIONARY_FLASHTIME );
				break;

			case DoorState::State::Bad:
				pMyLED->SetLEDColour ( DOOR_BAD_COLOUR, DOOR_MOVING_FLASHTIME );
				break;

			case DoorState::State::Unknown:
				pMyLED->SetLEDColour ( DOOR_UNKNOWN_COLOUR, DOOR_MOVING_FLASHTIME );
				break;
		}
	}
}
#else
// When not showing the door (UAP) status then show the humidity status
void		   SetLED ()
{
	uint8_t			   red, green, blue;
	bool			   bOutsideRange = false;
	uint8_t			   Flashtime	 = 0U;
	float			   constrainedHumidity;
	static float	   OldHumidity			   = NAN;

	// calculate color component
	constexpr float	   HUMIDITY_MAX			   = 60.0;
	constexpr float	   HUMIDITY_MIN			   = 40.0;
	constexpr float	   HUMIDITY_MID			   = 50.0;
	constexpr uint32_t OUTSIDE_RANGE_FLASHTIME = 10U;
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
	green = 255.0 - ( ( abs ( constrainedHumidity - HUMIDITY_MID ) * 255.0 ) / ( ( HUMIDITY_MAX - HUMIDITY_MIN ) / 2.0 ) );
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
// main loop function
void loop ()
{
#ifdef BME280_SUPPORT
	//float				 pres ( NAN ), temp ( NAN ), hum ( NAN );
	static unsigned long ulLastSensorTime = millis () - ( 30UL * 1000UL );
#endif

	static unsigned long ulLastDisplayTime = 0UL;

#ifdef UAP_SUPPORT
	static DoorState::State LastDoorState  = DoorState::State::Unknown;
	static bool				LastLightState = false;

	// set initial light state
	if ( pGarageDoor != nullptr && ulLastDisplayTime == 0UL )
	{
		LastLightState = !pGarageDoor->IsLit ();
	}
#endif
	// set LED
	SetLED ();

	// See if we have any udp requests to action
	pMyUDPService->CheckUDP ();
#ifdef BME280_SUPPORT
	if ( millis () - ulLastSensorTime > 30 * 1000 )
	{
		MyBME280.read ( EnvironmentResults.pressure, EnvironmentResults.temperature, EnvironmentResults.humidity, BME280::TempUnit::TempUnit_Celsius, BME280::PresUnit::PresUnit_hPa );
		//Info ( "Temperature: " + String ( EnvironmentResults.temperature ) + "C" );
		EnvironmentResults.pressure			 = EnvironmentCalculations::EquivalentSeaLevelPressure ( ALTITUDE_COMPENSATION, EnvironmentResults.temperature, EnvironmentResults.pressure );
		EnvironmentResults.dewpoint			 = EnvironmentCalculations::DewPoint ( EnvironmentResults.temperature, EnvironmentResults.humidity );
		EnvironmentResults.ulTimeOfReadingms = pMyUDPService->GetTime ();
		MulticastMsg ( UDPWiFiService::ReqMsgType::TEMPDATA );
		// reset time counter
		ulLastSensorTime = millis ();
	}
#endif

	// update debug stats every 1/2 second
	if ( millis () - ulLastDisplayTime > 500 )
	{
		ulLastDisplayTime = millis ();
		DisplayStats ();
	}

#ifdef UAP_SUPPORT
	// if door state has changed, multicast news
	if ( pGarageDoor != nullptr )
	{
		pGarageDoor->UpdateDoorState ();
		if ( pGarageDoor->GetDoorState () != LastDoorState || LastLightState != pGarageDoor->IsLit () )
		{
			LastDoorState  = pGarageDoor->GetDoorState ();
			LastLightState = pGarageDoor->IsLit ();
			MulticastMsg ( UDPWiFiService::ReqMsgType::DOORDATA );
		}
	}
	if ( pGarageDoor->IsSwitchConfigured() && pMyUDPService != nullptr )
	{
		static uint16_t SwitchPressedCount		 = 0;
		uint16_t		LatestSwitchPressedCount = pGarageDoor->GetSwitchMatchCount ();
		if ( LatestSwitchPressedCount > SwitchPressedCount )
		{
			SwitchPressedCount = LatestSwitchPressedCount;
		}
	}
#endif
}

// called to generate a response to a command
// returns an empty string if no response required ie only do action
void BuildMessage ( UDPWiFiService::ReqMsgType eReqType, String &sResponse )
{
	switch ( eReqType )
	{
		case UDPWiFiService::ReqMsgType::TEMPDATA:
#ifdef BME280_SUPPORT
			sResponse  = F ( "T=" );
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
				sResponse  = F ( "S=" );
				sResponse += pGarageDoor->GetDoorDisplayState (); // Door State
				sResponse += F ( ",L=" );
				sResponse += pGarageDoor->IsLit () ? F ( "On" ) : F ( "Off" ); // Light on or not
				sResponse += F ( ",C=" );
				sResponse += pGarageDoor->IsClosed () ? F ( "Y" ) : F ( "N" ); // Closed or not
				sResponse += F ( ",O=" );
				sResponse += pGarageDoor->IsOpen () ? F ( "Y" ) : F ( "N" ); // Open or not
				sResponse += F ( ",M=" );
				sResponse += pGarageDoor->IsMoving () ? F ( "Y" ) : F ( "N" ); // Moving or not
				sResponse += F ( ",A=" );
				sResponse += pMyUDPService->GetTime (); // current epoch time
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
				pGarageDoor->DoRequest ( DoorState::Request::OpenDoor );
			}
			break;

		case UDPWiFiService::ReqMsgType::DOORCLOSE:
			if ( pGarageDoor != nullptr )
			{
				pGarageDoor->DoRequest ( DoorState::Request::CloseDoor );
			}
			break;

		case UDPWiFiService::ReqMsgType::DOORSTOP:
			if ( pGarageDoor != nullptr )
			{
				pGarageDoor->DoRequest ( DoorState::Request::StopDoor );
			}
			break;

		case UDPWiFiService::ReqMsgType::LIGHTON:
			if ( pGarageDoor != nullptr )
			{
				pGarageDoor->DoRequest ( DoorState::Request::LightOn );
			}
			break;

		case UDPWiFiService::ReqMsgType::LIGHTOFF:
			if ( pGarageDoor != nullptr )
			{
				pGarageDoor->DoRequest ( DoorState::Request::LightOff );
			}
			break;
#endif
	}
}


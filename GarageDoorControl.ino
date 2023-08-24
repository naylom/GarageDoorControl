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
#include "WiFiService.h"
#include "logging.h"

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
*/
#define VERSION "1.0.11 Beta"
#define TELNET
#ifdef MNDEBUG
	#ifdef TELNET
ansiVT220Logger MyLogger ( Telnet );
	#else
SerialLogger	slog;
ansiVT220Logger MyLogger ( slog ); // create serial comms object to log to
	#endif
#endif

#define UAP_SUPPORT
#define BME280_SUPPORT

#define MKR_RGB_INVERT // only required if Red and Green colours
// are inverted as found on some boards

#ifdef BME280_SUPPORT // Temp, humidity and pressure sensor
struct
{
		float	 temperature;
		float	 pressure; // at sea level
		float	 humidity;
		float	 dewpoint;
		uint32_t ulTimeOfReadingms;
} EnvironmentResults					  = { NAN, NAN, NAN, 0UL };

constexpr float		ALTITUDE_COMPENSATION = 135.0; // sensor is 135 metres aboves sea level, we need this to adjust pressure reading to sea level equivalent.
BME280I2C::Settings settings ( BME280::OSR_X2, BME280::OSR_X2, BME280::OSR_X2, BME280::Mode_Normal, BME280::StandbyTime_250ms, BME280::Filter_Off, BME280::SpiEnable_False, BME280I2C::I2CAddr_0x76 );
BME280I2C			MyBME280 ( settings );
#endif

#ifdef UAP_SUPPORT
	#include "DoorState.h"
// PIN allocations, input & output from arduino perspective

// Need to be interrupt pins
constexpr pin_size_t DOOR_IS_OPEN_STATUS_PIN   = 8;
constexpr uint8_t	 DOOR_IS_CLOSED_STATUS_PIN = 7;
constexpr uint8_t	 LIGHT_IS_ON_STATUS_PIN	   = 6;
constexpr uint8_t	 DOOR_SWITCH_INPUT_PIN	   = 0;
// Don't need to be interrupt pins
constexpr uint8_t	 TURN_LIGHT_ON_OUTPUT_PIN  = 2;
constexpr uint8_t	 CLOSE_DOOR_OUTPUT_PIN	   = 3;
constexpr uint8_t	 OPEN_DOOR_OUTPUT_PIN	   = 4;
constexpr uint8_t	 STOP_DOOR_OUTPUT_PIN	   = 5;

constexpr uint32_t	 SWITCH_DEBOUNCE_MS		   = 50; // min ms between consecutive pin interrupts before signal accepted from manual switch
DoorState			*pGarageDoor			   = nullptr;
DoorStatusPin		*pDoorSwitchPin			   = nullptr;

#endif

constexpr uint8_t RED_PIN		   = A4;
constexpr uint8_t GREEN_PIN		   = 10;
constexpr uint8_t BLUE_PIN		   = A3;
MNRGBLEDBaseLib	 *pMyLED		   = new CRGBLED ( RED_PIN, GREEN_PIN, BLUE_PIN, 255, 90, 60 );

/*
	WiFi config
*/
constexpr char	  ssid []		   = "Naylorfamily"; // your network SSID (name)
constexpr char	  pass []		   = "welcome1";	 // your network password
constexpr char	  MyHostName []	   = "GarageControl";

UDPWiFiService	 *pMyUDPService	   = nullptr;
constexpr uint8_t NWPrintStartLine = 15;  // Start line for network stats
unsigned long	  ulLastClientReq  = 0UL; // millis of last wifi incoming message

// Error message process used when generating messages during interrupt
constexpr uint8_t ERROR_LINE	   = 25;
time_t			  timeError;

void			  GetLocalTime ( String &Result )
{
	if ( pMyUDPService != nullptr && pMyUDPService->IsConnected () )
	{
		timeError = 0;
		pMyUDPService->GetLocalTime ( Result );
		Result += " ";
	}
}

String sInfoErrorMsg;
auto   fgInfoErrorColour = ansiVT220Logger::FG_WHITE;
auto   bgInfoErrorColour = ansiVT220Logger::BG_GREEN;

/// @brief Logs error to error line the provided error message prepeneded with local date and time
/// @param s message to be logged
void   Error ( String s )
{
	String Result;
	GetLocalTime ( Result );
	Result			  += s;
	sInfoErrorMsg	   = Result;
	fgInfoErrorColour  = ansiVT220Logger::FG_BRIGHTWHITE;
	bgInfoErrorColour  = ansiVT220Logger::BG_BRIGHTBLUE;
}

/// @brief Logs info to error line the provided error message prepeneded with local date and time
/// @param s message to be logged
void Info ( String s )
{
	String Result;
	GetLocalTime ( Result );
	Result			  += s;
	sInfoErrorMsg	   = Result;
	fgInfoErrorColour  = ansiVT220Logger::FG_WHITE;
	bgInfoErrorColour  = ansiVT220Logger::BG_BLUE;
}

void DisplaylastInfoErrorMsg ()
{
#ifdef MNDEBUG
	MyLogger.ClearLine ( ERROR_LINE );
	MyLogger.COLOUR_AT ( fgInfoErrorColour, bgInfoErrorColour, ERROR_LINE, 1, sInfoErrorMsg );
#endif
}

// Debug information for ANSI screen with cursor control
void DisplayStats ( void )
{
#ifdef MNDEBUG
	// display uptime
	DisplayUptime ( MyLogger, 1, 1, ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK );
	static time_t LastTime = 0;
	#ifdef UAP_SUPPORT
	String Heading = F ( "Garage Door Control -  ver " );
	#else
	String Heading = F ( "Temp Sensor - ver " );
	#endif
	Heading += String ( VERSION );
	MyLogger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, 1, 20, Heading );

	String sTime;
	pMyUDPService->GetLocalTime ( sTime );
	MyLogger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, 1, 60, sTime );

	#ifdef UAP_SUPPORT
	String result;
	if ( pGarageDoor != nullptr )
	{
		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, 4, 0, F ( "Light is " ) );
		MyLogger.ClearPartofLine ( 4, 10, 8 );
		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_CYAN, ansiVT220Logger::BG_BLACK, 4, 10, pGarageDoor->IsLit () ? "On" : "Off" );
		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, 5, 0, F ( "State is " ) );
		MyLogger.ClearPartofLine ( 5, 10, 8 );
		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_CYAN, ansiVT220Logger::BG_BLACK, 5, 10, pGarageDoor->GetDoorDisplayState () );

		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, 3, 41, "Count     Called Unchngd Matched UnMtchdSpurious Duration" );
		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, 4, 20, F ( "Light Off count     " ) );
		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_GREEN, ansiVT220Logger::BG_BLACK, 4, 41, String ( pGarageDoor->GetLightOffCount () ) );
		pGarageDoor->m_pDoorLightStatusPin->DebugStats ( result );
		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_GREEN, ansiVT220Logger::BG_BLACK, 4, 49, result );

		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, 5, 20, F ( "Light On count      " ) );
		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_GREEN, ansiVT220Logger::BG_BLACK, 5, 41, String ( pGarageDoor->GetLightOnCount () ) );

		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, 6, 20, F ( "Door Opened count   " ) );
		pGarageDoor->m_pDoorOpenStatusPin->DebugStats ( result );
		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_GREEN, ansiVT220Logger::BG_BLACK, 6, 41, String ( pGarageDoor->GetDoorOpenedCount () ) );
		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_GREEN, ansiVT220Logger::BG_BLACK, 6, 49, result );

		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, 7, 20, F ( "Door Opening count  " ) );
		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_GREEN, ansiVT220Logger::BG_BLACK, 7, 41, String ( pGarageDoor->GetDoorOpeningCount () ) );

		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, 8, 20, F ( "Door Closed count   " ) );
		pGarageDoor->m_pDoorClosedStatusPin->DebugStats ( result );
		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_GREEN, ansiVT220Logger::BG_BLACK, 8, 41, String ( pGarageDoor->GetDoorClosedCount () ) );
		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_GREEN, ansiVT220Logger::BG_BLACK, 8, 49, result );

		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, 9, 20, F ( "Door Closing count  " ) );
		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_GREEN, ansiVT220Logger::BG_BLACK, 9, 41, String ( pGarageDoor->GetDoorClosingCount () ) );
	}
	MyLogger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, 10, 20, F ( "Switch Presssed " ) );
	if ( pDoorSwitchPin != nullptr )
	{
		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_GREEN, ansiVT220Logger::BG_BLACK, 10, 41, String ( pDoorSwitchPin->GetMatchedCount () ) );
		pDoorSwitchPin->DebugStats ( result );
		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_GREEN, ansiVT220Logger::BG_BLACK, 10, 49, result );
	}
	#endif

	#ifdef BME280_SUPPORT
	MyLogger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, 12, 0, F ( "Temperature is " ) );
	MyLogger.ClearPartofLine ( 12, 16, 6 );
	MyLogger.COLOUR_AT ( ansiVT220Logger::FG_RED, ansiVT220Logger::BG_BLACK, 12, 16, String ( EnvironmentResults.temperature ) );
	MyLogger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, 13, 0, F ( "Humidity is " ) );
	MyLogger.ClearPartofLine ( 13, 16, 6 );
	MyLogger.COLOUR_AT ( ansiVT220Logger::FG_CYAN, ansiVT220Logger::BG_BLACK, 13, 16, String ( EnvironmentResults.humidity ) );
	MyLogger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, 14, 0, F ( "Pressure is " ) );
	MyLogger.ClearPartofLine ( 14, 16, 7 );
	MyLogger.COLOUR_AT ( ansiVT220Logger::FG_YELLOW, ansiVT220Logger::BG_BLACK, 14, 16, String ( EnvironmentResults.pressure ) );
	#endif
	DisplayNWStatus ( MyLogger );
	DisplaylastInfoErrorMsg ();
#endif
}

void DisplayUptime ( ansiVT220Logger logger, uint8_t line, uint8_t row, ansiVT220Logger::colours Foreground, ansiVT220Logger::colours Background )
{
	static uint32_t ulStartTime = 0UL;

	// set initial start time
	if ( ulStartTime == 0 )
	{
		ulStartTime = millis ();
	}

	int32_t ulNumSeconds = millis () - ulStartTime;
	if ( ulNumSeconds < 0 )
	{
		// wrapped around
	}
	else
	{
		uint32_t ulTotalNumSeconds = ulNumSeconds / 1000;

		uint32_t ulDays			   = ulTotalNumSeconds / ( 60 * 60 * 24 );
		uint32_t ulHours		   = ( ulTotalNumSeconds / ( 60 * 60 ) ) % 24;
		uint32_t ulMinutes		   = ( ulTotalNumSeconds / 60 ) % 60;
		uint32_t ulSecs			   = ulTotalNumSeconds % 60;

		char	 sUpTime [ 20 ];
		sprintf ( sUpTime, "%02d:%02d:%02d:%02d", ulDays, ulHours, ulMinutes, ulSecs );
		logger.COLOUR_AT ( Foreground, Background, line, row, sUpTime );
	}
}

void DisplayNWStatus ( ansiVT220Logger logger )
{
	// print the SSID of the network you're attached to:
	logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, NWPrintStartLine, 0, F ( "SSID: " ) );
	logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN, ansiVT220Logger::BG_BLACK, NWPrintStartLine, 23, WiFi.SSID () );
	if ( pMyUDPService != nullptr )
	{
		FixedIPList *m_pMulticastDestList = pMyUDPService->GetMulticastList ();
		if ( m_pMulticastDestList != nullptr )
		{
			uint8_t	  iterator = m_pMulticastDestList->GetIterator ();
			IPAddress mcastDest;
			while ( (long unsigned int)( mcastDest = m_pMulticastDestList->GetNext ( iterator ) ) != 0UL )
			{
				logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, NWPrintStartLine + iterator - 1, 41, "Mcast #" + String ( iterator ) + ": " );
				logger.ClearPartofLine ( NWPrintStartLine + iterator - 1, 61, 15 );
				logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN, ansiVT220Logger::BG_BLACK, NWPrintStartLine + iterator - 1, 61, pMyUDPService->ToIPString ( mcastDest ) );
			}
		}

		logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, NWPrintStartLine + 1, 0, F ( "My Hostname: " ) );
		logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN, ansiVT220Logger::BG_BLACK, NWPrintStartLine + 1, 23, pMyUDPService->GetHostName () );

		logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, NWPrintStartLine + 2, 0, F ( "IP Address: " ) );
		logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN, ansiVT220Logger::BG_BLACK, NWPrintStartLine + 2, 23, pMyUDPService->ToIPString ( WiFi.localIP () ) );

		logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, NWPrintStartLine + 3, 0, "Subnet Mask: " );
		logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN, ansiVT220Logger::BG_BLACK, NWPrintStartLine + 3, 23, pMyUDPService->ToIPString ( WiFi.subnetMask () ) );

		logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, NWPrintStartLine + 4, 0, "Local Multicast Addr: " );
		logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN, ansiVT220Logger::BG_BLACK, NWPrintStartLine + 4, 23, pMyUDPService->ToIPString ( pMyUDPService->GetMulticastAddress () ) );

		logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, NWPrintStartLine + 4, 41, "WiFi connect/fail: " );
		logger.ClearPartofLine ( NWPrintStartLine + 4, 61, 10 );
		logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN, ansiVT220Logger::BG_BLACK, NWPrintStartLine + 4, 61, String ( pMyUDPService->GetBeginCount () ) + "/" + String ( pMyUDPService->GetBeginTimeOutCount () ) );

		logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, NWPrintStartLine + 5, 41, "Multicasts sent: " );
		logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN, ansiVT220Logger::BG_BLACK, NWPrintStartLine + 5, 61, String ( pMyUDPService->GetMCastSentCount () ) );

		logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, NWPrintStartLine + 6, 41, "Requests recvd: " );
		logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN, ansiVT220Logger::BG_BLACK, NWPrintStartLine + 6, 61, String ( pMyUDPService->GetRequestsReceivedCount () ) );

		logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, NWPrintStartLine + 7, 41, "Replies sent: " );
		logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN, ansiVT220Logger::BG_BLACK, NWPrintStartLine + 7, 61, String ( pMyUDPService->GetReplySentCount () ) );

		logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, NWPrintStartLine + 5, 0, F ( "Mac address: " ) );
		byte bMac [ 6 ];
		WiFi.macAddress ( bMac );
		char s [ 18 ];
		sprintf ( s, "%02X:%02X:%02X:%02X:%02X:%02X", bMac [ 5 ], bMac [ 4 ], bMac [ 3 ], bMac [ 2 ], bMac [ 1 ], bMac [ 0 ] );
		logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN, ansiVT220Logger::BG_BLACK, NWPrintStartLine + 5, 23, s );

		logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, NWPrintStartLine + 6, 0, F ( "Gateway Address: " ) );
		logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN, ansiVT220Logger::BG_BLACK, NWPrintStartLine + 6, 23, pMyUDPService->ToIPString ( WiFi.gatewayIP () ) );
		// print the received signal strength:

		logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, NWPrintStartLine + 7, 0, F ( "Signal strength (RSSI):" ) );
		logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN, ansiVT220Logger::BG_BLACK, NWPrintStartLine + 7, 23, String ( WiFi.RSSI () ) );
		logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, NWPrintStartLine + 7, 30, F ( " dBm" ) );

		logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, NWPrintStartLine + 8, 0, F ( "WiFi Status: " ) );
		logger.ClearPartofLine ( NWPrintStartLine + 8, 23, 15 );
		logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN, ansiVT220Logger::BG_BLACK, NWPrintStartLine + 8, 23, pMyUDPService->WiFiStatusToString ( WiFi.status () ) );

		logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, NWPrintStartLine + 8, 41, F ( "WiFi Service State: " ) );
		logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN, ansiVT220Logger::BG_BLACK, NWPrintStartLine + 8, 61, String ( pMyUDPService->GetState () ) );
	}
}

// main setup routine
void setup ()
{
	MyLogger.LogStart ();
	MyLogger.ClearScreen ();

	pMyUDPService = new UDPWiFiService ();

	// now we have state table set up and temp sensor configured, allow users to query state
	if ( !pMyUDPService->Begin ( ProcessUDPMsg, ssid, pass, MyHostName, &TheMKR_RGB_LED ) )
	{
		Error ( "Cannot connect WiFI " );
	}

#ifdef BME280_SUPPORT
	Wire.begin ();
	if ( !MyBME280.begin () )
	{
		Error ( "Could not find BME280 sensor!" );
		delay ( 1000 );
	}
	else
	{
		switch ( MyBME280.chipModel () )
		{
			case BME280::ChipModel_BME280:
				Info ( "Found BME280 sensor! Success." );
				break;
			case BME280::ChipModel_BMP280:
				Info ( "Found BMP280 sensor! No Humidity available." );
				break;
			default:
				Error ( "Found UNKNOWN sensor! Error!" );
		}
	}
	DisplaylastInfoErrorMsg ();
#endif

#ifdef UAP_SUPPORT
	// Setup so we are called if the state of door changes
	pGarageDoor	   = new DoorState ( OPEN_DOOR_OUTPUT_PIN, CLOSE_DOOR_OUTPUT_PIN, STOP_DOOR_OUTPUT_PIN, TURN_LIGHT_ON_OUTPUT_PIN, DOOR_IS_OPEN_STATUS_PIN, DOOR_IS_CLOSED_STATUS_PIN, LIGHT_IS_ON_STATUS_PIN );
	pDoorSwitchPin = new DoorStatusPin ( pGarageDoor, DoorState::Event::SwitchPress, DoorState::Event::Nothing, DOOR_SWITCH_INPUT_PIN, SWITCH_DEBOUNCE_MS, PinStatus::HIGH, PinMode::INPUT_PULLDOWN, PinStatus::CHANGE );
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
void SetLED ()
{
	uint8_t			   red, green, blue;
	bool			   bOutsideRange = false;
	uint8_t			   Flashtime	 = 0U;
	float			   constrainedHumidity;
	static float OldHumidity = NAN;

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
		OldHumidity =  EnvironmentResults.humidity;
	}
	constrainedHumidity						   = max ( EnvironmentResults.humidity, HUMIDITY_MIN );
	constrainedHumidity						   = min ( constrainedHumidity, HUMIDITY_MAX );
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
	float				 pres ( NAN ), temp ( NAN ), hum ( NAN );
	static unsigned long ulLastSensorTime = millis () - ( 30UL * 1000UL );
#endif

	static unsigned long ulLastDisplayTime = 0UL;

#ifdef UAP_SUPPORT
	static DoorState::State LastDoorState  = DoorState::Unknown;
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
		if ( pGarageDoor->GetDoorState () != LastDoorState || LastLightState != pGarageDoor->IsLit () )
		{
			LastDoorState  = pGarageDoor->GetDoorState ();
			LastLightState = pGarageDoor->IsLit ();
			MulticastMsg ( UDPWiFiService::ReqMsgType::DOORDATA );
		}
	}
	if ( pDoorSwitchPin != nullptr && pMyUDPService != nullptr )
	{
		static uint16_t SwitchPressedCount		 = 0;
		uint16_t		LatestSwitchPressedCount = pDoorSwitchPin->GetMatchedCount ();
		if ( LatestSwitchPressedCount > SwitchPressedCount )
		{
			if ( pDoorSwitchPin->GetCurrentMatchedState () )
			{
				Info ( "Switch pressed" );
			}
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
				sResponse += pGarageDoor->IsLit () ? "On" : "Off"; // Light on or not
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
				Error ( "Doordata unavailable" );
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

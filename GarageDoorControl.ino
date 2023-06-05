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
The built in MKR WiFi 1010 RGB LED displays the door state
WHITE, no flash  - state unknown
GREEN, no flash	 - Door Closed
GREEN, flashing  - Door Closing
RED, no flash	 - Door Open
RED, flashing    - Door Opening
PURPLE, no flash - Door Stopped
BLUE, flashing	 - Door unknown state, not open and not closed ie in transit but we cannot determine which
YELLOW, flashing - Door in a BAD STate, UAP says open and closed at same time.
an external RGB LED displays the WiFI status


Author: (c) M. Naylor 2022

History:
	Ver 1.0			Initial version
	Ver 1.0.4		Supports config to add/remove UAP, Distance Centre, Barometruc sensor
	Ver 1.0.5		Add ability to get loggging data over a telnet connection on 0xFEEE
	Ver 1.0.6       Changed input pins to be simply INPUT and use external pulldown resistors
	Cer 1.0.7		Moved all input and out pins into DoorState, added InputPin class
*/
#define VERSION "1.0.7 Beta"

#define UAP_SUPPORT
#define BAROMETRIC_SUPPORT
#define TEMP_HUMIDITY_SUPPORT
#undef DISTANCE_SENSOR_SUPPORT
// #define 	MKR_RGB_INVERT														// only required if Red and Green colours
// are inverted as found on some boards

#ifdef TEMP_HUMIDITY_SUPPORT
	#include <ClosedCube_SHT31D.h>
	/*
		HumiditySensor config
	*/
	#define SHT35D // Configure to use SHT35D sensor for humidity and temperature
	#ifdef SHT35D
		#include "SHTTempHumSensors.h"
constexpr auto			SensorDeviceID		  = 0x44; // I2C device id for SHT35-D sensor
SHTTempHumSensorsClass *pmyHumidityTempSensor = nullptr;
	#else
		#include "DHTTempHumSensors.h"
constexpr auto			SensorDeviceID		  = 6; // Data Pin Num for DHT sensor
DHTTempHumSensorsClass *pmyHumidityTempSensor = nullptr;
	#endif

THSENSOR_RESULT sHTResults; // Holds last temperature and humidity results

#endif

#ifdef BAROMETRIC_SUPPORT
	#include <ClosedCube_LPS25HB.h>
/*
	Baramoter sensor, closedcube LPS25HB based on MEMS sensor
*/
ClosedCube_LPS25HB lps25hb;
// TN210TD height adjustment factor for pressure, see
// //https://www.engineeringtoolbox.com/barometers-elevation-compensation-d_1812.html
constexpr float	   ALTITUDE_COMPENSATION = 15.0; // TN210TD is 127m above sea level according to Google Maps, which the
												 // above link converts to 15 mbars (hPa) compensation
constexpr auto	   lps25hbDevID			 = 0x5C; // I2C device id
float			   fLatestPressure		 = NAN;
#endif

#ifdef UAP_SUPPORT
	#include "DoorState.h"
// PIN allocations, input & output from arduino perspective

constexpr pin_size_t DOOR_IS_OPEN_STATUS_PIN   = 0;
constexpr uint8_t	 DOOR_IS_CLOSED_STATUS_PIN = 1;
constexpr uint8_t	 LIGHT_IS_ON_STATUS_PIN	   = 4;
constexpr uint8_t	 DOOR_SWITCH_INPUT_PIN	   = 5;
constexpr uint8_t	 TURN_LIGHT_ON_OUTPUT_PIN  = 9;
constexpr uint8_t	 CLOSE_DOOR_OUTPUT_PIN	   = 8;
constexpr uint8_t	 OPEN_DOOR_OUTPUT_PIN	   = 7;
constexpr uint8_t	 STOP_DOOR_OUTPUT_PIN	   = 6;

constexpr uint8_t	 RED_PIN				   = A4;
constexpr uint8_t	 GREEN_PIN				   = A5;
constexpr uint8_t	 BLUE_PIN				   = A6;

constexpr uint32_t	 SWITCH_DEBOUNCE_MS		   = 50; // min ms between consecutive pin interrupts before signal accepted from manual switch
DoorState			*pGarageDoor			   = nullptr;
DoorStatusPin		*pDoorSwitchPin			   = nullptr;
MNRGBLEDBaseLib		*pMyLED					   = new CRGBLED ( RED_PIN, GREEN_PIN, BLUE_PIN );
#endif
/*
	WiFi config
*/
constexpr char	ssid []			= "Naylorfamily"; // your network SSID (name)
constexpr char	pass []			= "welcome1";	  // your network password
constexpr char	MyHostName []	= "GarageControl";

UDPWiFiService *pMyUDPService	= nullptr;

unsigned long	ulLastClientReq = 0UL; // millis of last wifi incoming message

// Error message
String ErrorMsg;
bool IsError = false;
time_t timeError;

// Debug information for ANSI screen with cursor control
void			DisplayStats ( void )
{
#ifdef MNDEBUG
	static time_t LastTime = 0;
	#ifdef UAP_SUPPORT
	String Heading = F ( "Garage Door Control -  ver " );
	#else
	String Heading = F ( "Temp Sensor - ver " );
	#endif
	Heading += String ( VERSION );
	COLOUR_AT ( FG_WHITE, BG_BLACK, 0, 20, Heading );
/*
	time_t tNow = (time_t)pMyUDPService->GetTime ();
	if ( tNow > LastTime )
	{
		LastTime	 = tNow;
		tm	*localtm = localtime ( &tNow );
		char sTime [ 20 ];
		sprintf ( sTime, "%02d/%02d/%02d %02d:%02d:%02d", localtm->tm_mday, localtm->tm_mon + 1, ( localtm->tm_year - 100 ), localtm->tm_hour, localtm->tm_min, localtm->tm_sec );
		COLOUR_AT ( FG_WHITE, BG_BLACK, 0, 60, sTime );
	}
*/	
	String sTime;
	pMyUDPService->GetLocalTime( sTime );
	COLOUR_AT ( FG_WHITE, BG_BLACK, 0, 60, sTime );
	#ifdef UAP_SUPPORT
	String result;
	COLOUR_AT ( FG_WHITE, BG_BLACK, 4, 0, F ( "Light is " ) );
	ClearPartofLine ( 4, 10, 8 );
	COLOUR_AT ( FG_CYAN, BG_BLACK, 4, 10, pGarageDoor->IsLit () ? "On" : "Off" );
	COLOUR_AT ( FG_WHITE, BG_BLACK, 5, 0, F ( "State is " ) );
	ClearPartofLine ( 5, 10, 8 );
	COLOUR_AT ( FG_CYAN, BG_BLACK, 5, 10, pGarageDoor->GetDoorDisplayState () );

	COLOUR_AT ( FG_WHITE, BG_BLACK, 4, 20, F ( "Light Off count     " ) );
	COLOUR_AT ( FG_GREEN, BG_BLACK, 4, 41, String ( pGarageDoor->GetLightOffCount () ) );
	pGarageDoor->m_pDoorLightStatusPin->DebugStats( result);
	COLOUR_AT ( FG_GREEN, BG_BLACK, 4, 45, result );	
	
	COLOUR_AT ( FG_WHITE, BG_BLACK, 5, 20, F ( "Light On count      " ) );
	COLOUR_AT ( FG_GREEN, BG_BLACK, 5, 41, String ( pGarageDoor->GetLightOnCount () ) );
	
	COLOUR_AT ( FG_WHITE, BG_BLACK, 6, 20, F ( "Door Opened count   " ) );
	pGarageDoor->m_pDoorOpenStatusPin->DebugStats( result);
	COLOUR_AT ( FG_GREEN, BG_BLACK, 6, 41, String ( pGarageDoor->GetDoorOpenedCount () ) );
	COLOUR_AT ( FG_GREEN, BG_BLACK, 6, 45, result );

	COLOUR_AT ( FG_WHITE, BG_BLACK, 7, 20, F ( "Door Opening count  " ) );
	COLOUR_AT ( FG_GREEN, BG_BLACK, 7, 41, String ( pGarageDoor->GetDoorOpeningCount () ) );

	COLOUR_AT ( FG_WHITE, BG_BLACK, 8, 20, F ( "Door Closed count   " ) );
	pGarageDoor->m_pDoorClosedStatusPin->DebugStats( result);
	COLOUR_AT ( FG_GREEN, BG_BLACK, 8, 41, String ( pGarageDoor->GetDoorClosedCount () ) );
	COLOUR_AT ( FG_GREEN, BG_BLACK, 8, 45, result );

	COLOUR_AT ( FG_WHITE, BG_BLACK, 9, 20, F ( "Door Closing count  " ) );
	COLOUR_AT ( FG_GREEN, BG_BLACK, 9, 41, String ( pGarageDoor->GetDoorClosingCount () ) );

	COLOUR_AT ( FG_WHITE, BG_BLACK, 10, 20, F ( "Switch Presssed " ) );
	COLOUR_AT ( FG_WHITE, BG_BLACK, 10, 41, String ( pDoorSwitchPin->GetMatchedCount() ) );
	pDoorSwitchPin->DebugStats( result );
	COLOUR_AT ( FG_WHITE, BG_BLACK, 10, 45, result );
	#endif

	#ifdef TEMP_HUMIDITY_SUPPORT
	COLOUR_AT ( FG_WHITE, BG_BLACK, 12, 0, F ( "Temperature is " ) );
	ClearPartofLine ( 12, 16, 6 );
	COLOUR_AT ( FG_RED, BG_BLACK, 12, 16, String ( sHTResults.fTemperature ) );
	COLOUR_AT ( FG_WHITE, BG_BLACK, 13, 0, F ( "Humidity is " ) );
	ClearPartofLine ( 13, 16, 6 );
	COLOUR_AT ( FG_CYAN, BG_BLACK, 13, 16, String ( sHTResults.fHumidity ) );
	#endif
	#ifdef BAROMETRIC_SUPPORT
	COLOUR_AT ( FG_WHITE, BG_BLACK, 14, 0, F ( "Pressure is " ) );
	ClearPartofLine ( 14, 16, 7 );
	COLOUR_AT ( FG_CYAN, BG_BLACK, 14, 16, String ( fLatestPressure ) );
	#endif
	if ( IsError )
	{
		IsError = false;
		String result;
		pMyUDPService->GetLocalTime ( result, timeError );
		result += ErrorMsg;
		Error ( result );
		ErrorMsg = "";
	}
	pMyUDPService->DisplayStatus ();
#endif
}

// main setup routine
void setup ()
{
	// Serial.begin ( BAUD_RATE );
	LogStart ();
	ClearScreen ();
#ifdef BAROMETRIC_SUPPORT
	// 	Start barometric sensor
	lps25hb.begin ( lps25hbDevID );
#endif
#ifdef UAP_SUPPORT
	// Setup so we are called if the state of door changes
	pGarageDoor = new DoorState ( OPEN_DOOR_OUTPUT_PIN, CLOSE_DOOR_OUTPUT_PIN, STOP_DOOR_OUTPUT_PIN, TURN_LIGHT_ON_OUTPUT_PIN, DOOR_IS_OPEN_STATUS_PIN, DOOR_IS_CLOSED_STATUS_PIN, LIGHT_IS_ON_STATUS_PIN  );
	pDoorSwitchPin = new DoorStatusPin ( pGarageDoor, DoorState::Event::SwitchPress, DoorState::Event::Nothing, DOOR_SWITCH_INPUT_PIN, SWITCH_DEBOUNCE_MS, PinStatus::LOW, PinMode::INPUT, PinStatus::CHANGE );

	SetLED ();
#endif

#ifdef TEMP_HUMIDITY_SUPPORT
	// cannot instantiate object as global - causes board to freeze, need to allocate when running
	pmyHumidityTempSensor = new SHTTempHumSensorsClass ( SensorDeviceID );
	// get initial reading
	pmyHumidityTempSensor->GetLastReading ( sHTResults );
#endif

	pMyUDPService = new UDPWiFiService ();

	// now we have state table set up and temp sensor configured, allow users to query state
	if ( !pMyUDPService->Begin ( ProcessUDPMsg, ssid, pass, MyHostName, &TheMKR_RGB_LED ) )
	{
		Error ( "Cannot connect WiFI " );
	}
}
#ifdef UAP_SUPPORT
// set the colour of the inbuilt MKR Wifi 1010 RGB LED based on the current door state
void SetLED ()
{
	static DoorState::State OldState	 = DoorState::State::Opening;
	DoorState::State		currentState = pGarageDoor->GetDoorState ();

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
#endif
// main loop function
void loop ()
{
#if defined TEMP_HUMIDITY_SUPPORT || defined BAROMETRIC_SUPPORT
	static unsigned long ulLastSensorTime = 0UL;
#endif
	static unsigned long ulLastDisplayTime = 0UL;
#ifdef UAP_SUPPORT
	static DoorState::State LastDoorState = DoorState::Unknown;
	static bool				LastLightState;

	// set initial light state
	if ( ulLastDisplayTime == 0UL )
	{
		LastLightState = !pGarageDoor->IsLit ();
	}
	// set LED to match Door State
	SetLED ();
#endif

	// See if we have any udp requests to action
	pMyUDPService->CheckUDP ();

#if defined TEMP_HUMIDITY_SUPPORT || defined BAROMETRIC_SUPPORT
	if ( millis () - ulLastSensorTime > 30 * 1000 )
	{
		//  30 secs have passed to get latest sesnor readings
	#ifdef BAROMETRIC_SUPPORT
		fLatestPressure = ( lps25hb.readPressure () + ALTITUDE_COMPENSATION );
	#endif
	#ifdef TEMP_HUMIDITY_SUPPORT
		pmyHumidityTempSensor->GetLastReading ( sHTResults );
		sHTResults.ulTimeOfReadingms = pMyUDPService->GetTime ();
	#endif
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
	if ( pGarageDoor->GetDoorState () != LastDoorState || LastLightState != pGarageDoor->IsLit () )
	{
		LastDoorState  = pGarageDoor->GetDoorState ();
		LastLightState = pGarageDoor->IsLit ();
		MulticastMsg ( UDPWiFiService::ReqMsgType::DOORDATA );
	}
#endif
}

// called to generate a response to a command
// returns an empty string if no response required ie only do action
void BuildMessage ( UDPWiFiService::ReqMsgType eReqType, String &sResponse )
{
	switch ( eReqType )
	{
#if defined TEMP_HUMIDITY_SUPPORT || defined BAROMETRIC_SUPPORT
		case UDPWiFiService::ReqMsgType::TEMPDATA:
			// check we have good data to share
			if ( sHTResults.fTemperature != NAN && sHTResults.fHumidity != NAN )
			{
	#ifdef TEMP_HUMIDITY_SUPPORT
				// send a reply, to the IP address and port that sent us the packet we received
				sResponse  = F ( "T=" );
				sResponse += sHTResults.fTemperature;
				sResponse += F ( ",H=" );
				sResponse += sHTResults.fHumidity;
				sResponse += F ( ",D=" );
				sResponse += sHTResults.fDewPoint;
				sResponse += F ( "," );
	#endif
	#ifdef BAROMETRIC_SUPPORT
				sResponse += F ( "P=" );
				sResponse += fLatestPressure;
				sResponse += F ( "," );
	#endif
				sResponse += F ( "A=" );
				sResponse += sHTResults.ulTimeOfReadingms;
				sResponse += F ( "\r" );
			}
			else
			{
				Error ( "Not responding to UDP request for data as no valid results" );
			}
			break;
#endif
#ifdef UAP_SUPPORT
		case UDPWiFiService::ReqMsgType::DOORDATA:
			sResponse  = F ( "S=" );
			sResponse += pGarageDoor->GetDoorDisplayState ();			   // Door State
			sResponse += F ( ",L=" );
			sResponse += pGarageDoor->IsLit () ? "On" : "Off";			   // Light on or not
			sResponse += F ( ",C=" );
			sResponse += pGarageDoor->IsClosed () ? F ( "Y" ) : F ( "N" ); // Closed or not
			sResponse += F ( ",O=" );
			sResponse += pGarageDoor->IsOpen () ? F ( "Y" ) : F ( "N" );   // Open or not
			sResponse += F ( ",M=" );
			sResponse += pGarageDoor->IsMoving () ? F ( "Y" ) : F ( "N" ); // Moving or not
			sResponse += F ( ",A=" );
			sResponse += pMyUDPService->GetTime ();						   // current epoch time
			sResponse += F ( "\r" );
			break;

		case UDPWiFiService::ReqMsgType::DOOROPEN:
			pGarageDoor->DoRequest ( DoorState::Request::OpenDoor );
			break;

		case UDPWiFiService::ReqMsgType::DOORCLOSE:
			pGarageDoor->DoRequest ( DoorState::Request::CloseDoor );
			break;

		case UDPWiFiService::ReqMsgType::DOORSTOP:
			pGarageDoor->DoRequest ( DoorState::Request::StopDoor );
			break;

		case UDPWiFiService::ReqMsgType::LIGHTON:
			pGarageDoor->DoRequest ( DoorState::Request::LightOn );
			break;

		case UDPWiFiService::ReqMsgType::LIGHTOFF:
			pGarageDoor->DoRequest ( DoorState::Request::LightOff );
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

/*

GarageDoorControl.ino

Arduino sketch to control a Hormann Garage door via a Horman UAP1
Also collects the temperature and humidity
Door state and humidity can be queried by remote client sending and receiving UDP messages - see WiFiService files

This is designed to use two RGB LEDS to display status
The built in MKR WiFi 1010 RGB LED displays the door state
WHITE, no flash  - state unknown
GREEN, no flash	 - Door Closed
GREEN, flashing  - Door Closing
RED, no flash	 - Door Open
RED, flashing    - Door Opening
PURPLE, no flash - Door Stopped

an external RGB LED displays the WiFI status


Author: (c) M. Naylor 2022

History:
	Ver 1.0			Initial version
*/
#define 	VERSION					"0.3 Beta"
#define 	MNDEBUG
#include <ClosedCube_SHT31D.h>
#include <MNPCIHandler.h>
#include <MNTimerLib.h>
#include <MNRGBLEDBaseLib.h>
#include "DoorState.h"
#include "logging.h"
#include "WiFiService.h"

#ifdef ARDUINO_AVR_UNO              // On UNO, I use the Cytron shield-esp-wifi
#include <CytronWiFiShield.h>
#include <CytronWiFiServer.h>
//#include <CytronWiFiClient.h>
#else                               // On MKR1010 WIFI with in buiilt wifi use nina libs
#include <WiFiNINA.h>
#include <WiFiUdp.h>
#endif
/*
    HumiditySensor config
*/
#define SHT35D                      // Configure to use SHT35D sensor for humidity and temperature

constexpr auto HUMIDITYSENSOR_READ_INTERVAL = 2000;

#ifdef SHT35D
#include "SHTTempHumSensors.h"
constexpr auto SensorDeviceID = 0x44;                               // I2C device id for SHT35-D sensor
SHTTempHumSensorsClass * pmyHumidityTempSensor = nullptr;
#else
#include "DHTTempHumSensors.h"
constexpr auto SensorDeviceID = 6;                                  // Data Pin Num for DHT sensor
DHTTempHumSensorsClass * pmyHumidityTempSensor = nullptr;
#endif

THSENSOR_RESULT sHTResults;											// Holds temperature and humidity results
/*
	WiFi config
*/
const 		char ssid[] 				= "Naylorfamily";                             // your network SSID (name)
const 		char pass[] 				= "welcome1";                                 // your network password
const 		char MyHostName[] 			= "GarageControl";
// PIN allocations, input & output from arduino perspective

const uint8_t DOOR_IS_OPEN_INPUT_PIN 	= 0;
const uint8_t DOOR__IS_CLOSED_INPUT_PIN	= 1;
const uint8_t LIGHT_IS_ON_INPUT_PIN		= 4;
const uint8_t DOOR_SWITCH_INPUT_PIN		= 5;
const uint32_t DEBOUNCE_MS				= 100;		// min ms between consecutive pin interrupts before signal accepted

const uint8_t TURN_LIGHT_ON_OUTPUT_PIN	= 6;
const uint8_t CLOSE_DOOR_OUTPUT_PIN		= 7;
const uint8_t OPEN_DOOR_OUTPUT_PIN		= 8;
const uint8_t STOP_DOOR_OUTPUT_PIN		= 9;

// external RGB LED pins
const uint8_t RED_PIN					= A4;
const uint8_t GREEN_PIN					= A5;
const uint8_t BLUE_PIN					= A6;

DoorState 		GarageDoor ( OPEN_DOOR_OUTPUT_PIN, CLOSE_DOOR_OUTPUT_PIN, STOP_DOOR_OUTPUT_PIN, TURN_LIGHT_ON_OUTPUT_PIN );
UDPWiFiService* 	pMyUDPService 		= nullptr;

bool 				bLightIsOn 			= false;	// LightOn state
bool				bChange 			= false;	// door open / close state has changed
unsigned long		ulLastClientReq		= 0UL;		// millis of last wifi incoming message
volatile bool		bSwitchPressed 		= false;	// door switch has been pressed
volatile uint32_t	ulCount 			= 0UL;

// following are routines to output ANSI style terminal emulation

void DisplayStats ( void )
{
#ifdef MNDEBUG	
	String Heading = F ( "Garage Door Control -  ver " );
	Heading += String ( VERSION );
	COLOUR_AT ( FG_WHITE, BG_BLACK, 0, 20, Heading ) ;
	COLOUR_AT ( FG_WHITE, BG_BLACK, 4, 0,  F ("Light is ") );
	ClearPartofLine ( 4, 10, 8 );
	COLOUR_AT ( FG_CYAN, BG_BLACK, 4, 10, GarageDoor.GetLightState() );
	COLOUR_AT ( FG_WHITE, BG_BLACK, 5, 0,  F ("Door is ") );	
	COLOUR_AT ( FG_CYAN, BG_BLACK, 5, 10, GarageDoor.IsClosed() ? F ( "Closed    " ) : F ( "Not Closed" ) );
	COLOUR_AT ( FG_WHITE, BG_BLACK, 6, 0,  F ("Door is ") );	
	COLOUR_AT ( FG_CYAN, BG_BLACK, 6, 10, GarageDoor.IsMoving() ? F ( "Moving    " ) : F ( "Not Moving" ) );
	COLOUR_AT ( FG_WHITE, BG_BLACK, 7, 0,  F ("Door is ") );	
	COLOUR_AT ( FG_CYAN, BG_BLACK, 7, 10, GarageDoor.IsOpen() ? F ( "Open      " ) : F ( "Not Open" ) );		
	COLOUR_AT ( FG_WHITE, BG_BLACK, 8, 0,  F ("State is ") );
	ClearPartofLine ( 8, 10, 8 );
	COLOUR_AT ( FG_CYAN, BG_BLACK, 8, 10, GarageDoor.GetDoorState() );		
	if ( bSwitchPressed )
	{
		// User has pressed garaghe door switch, determine required action by examining current state
		bSwitchPressed = false;
		COLOUR_AT ( FG_WHITE, BG_BLACK, 9, 0,  F ( "Switch Presssed " ) );
		COLOUR_AT ( FG_WHITE, BG_BLACK, 9, 17, String ( ulCount ) );
	}
	COLOUR_AT ( FG_WHITE, BG_BLACK, 12, 0,  F ("Temperature is ") );
	ClearPartofLine ( 12, 16, 6 );
	COLOUR_AT ( FG_RED, BG_BLACK, 12, 16, String ( sHTResults.fTemperature ) );
	COLOUR_AT ( FG_WHITE, BG_BLACK, 13, 0,  F ("Humidity is ") );
	ClearPartofLine ( 13, 16, 6 );
	COLOUR_AT ( FG_CYAN, BG_BLACK, 13, 16, String ( sHTResults.fHumidity ) );
	pMyUDPService->DisplayStatus();
#endif	
}

void setup()
{
	LogStart();
	pMyUDPService = new UDPWiFiService();
	// cannot instantiate object as global - causes board to freeze, need to allocate when running
	pmyHumidityTempSensor = new SHTTempHumSensorsClass(SensorDeviceID);
	// get initial reading
	sHTResults = pmyHumidityTempSensor->GetLastReading();
	// 
	if ( pMyUDPService->Begin ( ProcessUDPMsg, ssid, pass, MyHostName, new CRGBLED ( RED_PIN, GREEN_PIN, BLUE_PIN ) ) )
	{
		pMyUDPService->Start();	
	}
	else
	{
		Error ( "Cannot connect WiFI ");
	}

	TheMKR_RGB_LED.SetLEDColour( STATE_UNKNOWN, DOOR_STATIONARY_FLASHTIME );

	// Setup so we are called if the state of door changes
	PCIHandler.AddPin ( DOOR_IS_OPEN_INPUT_PIN, DoorOpened, FALLING, INPUT_PULLUP );
	PCIHandler.AddPin ( DOOR__IS_CLOSED_INPUT_PIN, DoorClosed, FALLING, INPUT_PULLUP );
	PCIHandler.AddPin ( LIGHT_IS_ON_INPUT_PIN, LightOn, FALLING, INPUT_PULLUP );
	PCIHandler.AddPin ( DOOR_SWITCH_INPUT_PIN, SwitchPressed, FALLING, INPUT_PULLUP );
	ClearScreen();
}

void loop()
{
	static uint8_t Count = 0;

	pMyUDPService->CheckUDP ();

	DisplayStats();
	delay (500);

	if ( ++Count % ( 2 * 60 ) == 0  )
	{
		Count = 0;
		// 120 half seconds have passed so get the sensor readings
		sHTResults = pmyHumidityTempSensor->GetLastReading();
	}	
}

void ProcessUDPMsg ( UDPWiFiService::ReqMsgType eReqType )
{
	String          sResponse;
	switch ( eReqType )
	{
		case UDPWiFiService::ReqMsgType::TEMPDATA:
			// check we have good data to share
			if ( sHTResults.fTemperature != NAN && sHTResults.fHumidity != NAN )
			{
				// send a reply, to the IP address and port that sent us the packet we received
				sResponse = F ( "T=" );
				sResponse += sHTResults.fTemperature;
				sResponse += F ( ",H=" );
				sResponse += sHTResults.fHumidity;
				sResponse += F ( ",D=" );
				sResponse += sHTResults.fDewPoint;
				sResponse += F ( ",A=" );
				sResponse += millis () - sHTResults.ulTimeOfReadingms;
				sResponse += F ( "\r" );
				pMyUDPService->SendReply ( sResponse );
			}
			else
			{
				Error ( F ( "Not responding to UDP request for data as no valid results" ) );
			}
			break;

		case UDPWiFiService::ReqMsgType::DOORDATA:
			sResponse = F ( "S=" );
			sResponse += GarageDoor.GetDoorState();							// Door State
			sResponse += F ( ",L=" );
			sResponse += GarageDoor.GetLightState();						// Light on or not
			sResponse += F ( ",C=" );
			sResponse += GarageDoor.IsClosed() ? F ( "Y" ) : F ( "N" );		// Closed or not
			sResponse += F ( ",O=" );
			sResponse += GarageDoor.IsOpen() ? F ( "Y" ) : F ( "N" );		// Open or not
			sResponse += F ( ",M=" );
			sResponse += GarageDoor.IsMoving() ? F ( "Y" ) : F ( "N" );		// Moving or not
			sResponse += F ( "\r" );
			pMyUDPService->SendReply ( sResponse );
			break;
	}
}

void DoorOpened()
{
	static unsigned long ulLastIntTime = 0UL;
	if ( millis() > ulLastIntTime + DEBOUNCE_MS )
	{
		ulLastIntTime = millis();
		GarageDoor.DoEvent ( DoorState::Event::DoorOpened , DoorState::State::Opened );
		TheMKR_RGB_LED.SetLEDColour ( DOOR_OPEN_COLOUR, DOOR_STATIONARY_FLASHTIME );
	}
}

void DoorClosed()
{
	static unsigned long ulLastIntTime = 0UL;
	if ( millis() > ulLastIntTime + DEBOUNCE_MS )
	{
		ulLastIntTime = millis();
		GarageDoor.DoEvent ( DoorState::Event::DoorClosed, DoorState::State::Closed );
		TheMKR_RGB_LED.SetLEDColour ( DOOR_CLOSED_COLOUR, DOOR_STATIONARY_FLASHTIME) ;
	}
}

void LightOn ()
{
	static unsigned long ulLastIntTime = 0UL;
	if ( millis() > ulLastIntTime + DEBOUNCE_MS )
	{
		ulLastIntTime = millis();

		if ( digitalRead ( LIGHT_IS_ON_INPUT_PIN ) ==  LOW )
		{
			bLightIsOn = true;
		}
		else
		{
			bLightIsOn = false;		
		}
	}
}

void SwitchPressed ()
{
	static unsigned long ulLastIntTime = 0UL;

	if ( millis() > ulLastIntTime + DEBOUNCE_MS )
	{	
		ulLastIntTime = millis();
		bSwitchPressed = true;
		ulCount++;
		GarageDoor.DoEvent ( DoorState::SwitchPressed, 0 );
	}
}





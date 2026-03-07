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

#include "BME280Sensor.h"
#include "ConfigStorage.h"
#include "Display.h"
#include "GarageMessageProtocol.h"

#include <MNPCIHandler.h>
#include <MNRGBLEDBaseLib.h>
#include <MNTimerLib.h>
#include <time.h>
#include <WiFiNINA.h>
#include <WiFiUdp.h>

// ─── Version string (extern'd by Display.cpp) ────────────────────────────────
const char* VERSION = "2.0.1";

// ─── Logger (extern'd by Display.cpp) ────────────────────────────────────────
#ifdef MNDEBUG
#ifdef TELNET
ansiVT220Logger MyLogger ( Telnet );
#else
SerialLogger slog;
ansiVT220Logger MyLogger ( slog );
#endif
#endif

// ─── Environment sensor and latest reading (EnvironmentResults extern'd by Display.cpp) ──
EnvironmentReading EnvironmentResults = { NAN, NAN, NAN, NAN, 0UL, false };
IEnvironmentSensor* pBME280Sensor = nullptr;

// ─── Garage door state ────────────────────────────────────────────────────────
HormannUAP1WithSwitch* pGarageDoor = nullptr;

// ─── UDP WiFi service (extern'd by Display.cpp and DoorState.cpp) ────────────
UDPWiFiService* pMyUDPService = nullptr;

// ─── Message protocol ─────────────────────────────────────────────────────────
IMessageProtocol* pMyProtocol = nullptr;

// ─── Display (extern'd nowhere — owned here) ──────────────────────────────────
Display* pMyDisplay = nullptr;

// ─── External RGB LED ────────────────────────────────────────────────────────
MNRGBLEDBaseLib* pMyLED = nullptr;

// ─── Misc globals ─────────────────────────────────────────────────────────────
unsigned long ulLastClientReq = 0UL;

// ─── Application implementation ───────────────────────────────────────────────

/**
 * @brief Constructor. Creates the external RGB LED object so it is available
 *        before begin() initialises other peripherals.
 */
Application::Application ()
{
	// Initialise the external LED object during global init so it exists before
	// begin() drives its pins low (matches original global-variable behaviour).
	pMyLED = new CRGBLED ( RED_PIN, GREEN_PIN, BLUE_PIN, 255, 180, 120 );
}

/**
 * @brief One-time setup performed in the Arduino setup() function.
 * @details Initialises all peripherals in order: LED, logger, WiFi/UDP service,
 *          BME280 sensor, Hormann UAP1 door controller, and display. Creates the
 *          GarageMessageProtocol instance that wires them together. Enters AP
 *          onboarding mode if no stored WiFi credentials are present.
 */
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

	//TheMKR_RGB_LED.Invert();  // Only if required!

	// Generate dynamic AP SSID based on MAC address
	String apSSID;
	byte mac [ 6 ];
	WiFi.macAddress ( mac );

	// Runtime detection: use a "GARAGE_CONTROL_" prefix when UAP pins are wired
	apSSID = ( DOOR_IS_OPEN_STATUS_PIN != NOT_A_PIN ) ? "GARAGE_CONTROL_" : "TEMP_HUMID_";
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

	{
		ConfigStorage::begin();
		GarageConfig cfg = {};
		cfg.altitudeCompensation = 131.0f;  // default matches OnboardingServer
		ConfigStorage::load ( cfg );

		pBME280Sensor = new BME280Sensor ( cfg.altitudeCompensation );
		if ( pBME280Sensor->IsPresent() )
		{
			if ( !pBME280Sensor->Begin() )
			{
				delete pBME280Sensor;
				pBME280Sensor = nullptr;
			}
		}
		else
		{
			Info ( F ( "No BME280 sensor detected" ) );
			delete pBME280Sensor;
			pBME280Sensor = nullptr;
		}
		DisplaylastInfoErrorMsg();
	}

	{
		auto* pDoor = new HormannUAP1WithSwitch ( OPEN_DOOR_OUTPUT_PIN,
		                                          CLOSE_DOOR_OUTPUT_PIN,
		                                          STOP_DOOR_OUTPUT_PIN,
		                                          TURN_LIGHT_ON_OUTPUT_PIN,
		                                          DOOR_IS_OPEN_STATUS_PIN,
		                                          DOOR_IS_CLOSED_STATUS_PIN,
		                                          LIGHT_IS_ON_STATUS_PIN,
		                                          DOOR_SWITCH_INPUT_PIN );
		if ( pDoor->IsPresent() )
		{
			pGarageDoor = pDoor;
			setLED();
		}
		else
		{
			Info ( F ( "No Hormann UAP1 garage door detected" ) );
			delete pDoor;
		}
	}

	pMyProtocol = new GarageMessageProtocol ( pGarageDoor, pBME280Sensor, EnvironmentResults, *pMyUDPService );

	pMyDisplay = new Display ( MyLogger, pMyUDPService, VERSION, pGarageDoor, pBME280Sensor );
}

/**
 * @brief Updates the inbuilt MKR WiFi 1010 RGB LED to reflect current system state.
 * @details If the Hormann UAP1 is present the LED colour reflects door state
 *          (green=closed, red=open, etc.). If no door is present the LED reflects
 *          the current relative humidity reading using a blue/green/red gradient.
 *          Does nothing when WiFiService is in AP/onboarding mode, as the service
 *          owns the LED colour in that state.
 */
void Application::setLED ()
{
	// In AP mode the WiFiService owns the LED colour — don't override it.
	if ( pMyUDPService != nullptr && pMyUDPService->GetState() == WiFiService::Status::AP_MODE )
	{
		return;
	}

	if ( pGarageDoor != nullptr )
	{
		static IGarageDoor::State OldState = IGarageDoor::State::Opening;
		IGarageDoor::State currentState = pGarageDoor->GetState();

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
		return;
	}

	// No garage door present — show humidity status on LED instead
	uint8_t red, green, blue;
	uint8_t Flashtime = 0U;
	float constrainedHumidity;
	static float OldHumidity = NAN;

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

	MyLogger.AT ( 3, 41, "Red   :" );
	MyLogger.AT ( 4, 41, "Green :" );
	MyLogger.AT ( 5, 41, "Blue  :" );
	MyLogger.ClearPartofLine ( 3, 48, 3 );
	MyLogger.ClearPartofLine ( 4, 48, 3 );
	MyLogger.ClearPartofLine ( 5, 48, 3 );
	MyLogger.AT ( 3, 48, String ( red ) );
	MyLogger.AT ( 4, 48, String ( green ) );
	MyLogger.AT ( 5, 48, String ( blue ) );
}

// ─── loop ─────────────────────────────────────────────────────────────────────
/**
 * @brief Main execution loop called repeatedly from the Arduino loop() function.
 * @details Each call: updates the LED, processes onboarding if in AP mode,
 *          checks for incoming UDP commands, reads the BME280 sensor at
 *          SENSOR_READ_INTERVAL_MS intervals (multicasting the result), refreshes
 *          the debug display every 500 ms, and polls the garage door state machine
 *          multicasting whenever door or light state changes.
 */
void Application::loop ()
{
	static unsigned long ulLastSensorTime = millis() - SENSOR_READ_INTERVAL_MS;
	static unsigned long ulLastDisplayTime = 0UL;

	static IGarageDoor::State LastDoorState = IGarageDoor::State::Unknown;
	static bool LastLightState = false;

	// set initial light state
	if ( pGarageDoor != nullptr && ulLastDisplayTime == 0UL )
	{
		LastLightState = !pGarageDoor->IsLit();
	}
	// set LED — noInterrupts() prevents Flash() ISR racing against analogWrite()
	// on the external LED's PWM registers, causing visible intensity flicker.
	noInterrupts();
	setLED();
	interrupts();

	// Process onboarding if in AP mode
	pMyUDPService->ProcessOnboarding();

	// See if we have any udp requests to action
	pMyUDPService->CheckUDP();

	if ( pBME280Sensor != nullptr && pMyUDPService->GetState() != WiFiService::Status::AP_MODE &&
	     millis() - ulLastSensorTime > SENSOR_READ_INTERVAL_MS )
	{
		if ( pBME280Sensor->Read ( EnvironmentResults ) )
		{
			multicastMsg ( UDPWiFiService::ReqMsgType::TEMPDATA );
		}
		ulLastSensorTime = millis();
	}

	// update debug stats every 1/2 second
	if ( millis() - ulLastDisplayTime > 500 )
	{
		ulLastDisplayTime = millis();
		if ( pMyDisplay != nullptr )
		{
			pMyDisplay->DisplayStats();
		}
	}

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
		if ( pGarageDoor->IsSwitchConfigured() && pMyUDPService != nullptr )
		{
			static uint16_t SwitchPressedCount = 0;
			uint16_t LatestSwitchPressedCount = pGarageDoor->GetSwitchMatchCount();
			if ( LatestSwitchPressedCount > SwitchPressedCount )
			{
				SwitchPressedCount = LatestSwitchPressedCount;
			}
		}
	}
}

// ─── processUDPMsg (static — satisfies UDPWiFiServiceCallback signature) ──────
/**
 * @brief Static UDP message callback invoked by UDPWiFiService when a request packet arrives.
 * @details Dispatches the command to the protocol handler, builds the response
 *          string, and sends a unicast reply to the requesting client.
 * @param eReqType The decoded request type (TEMPDATA, DOORDATA, DOOROPEN, etc.).
 */
void Application::processUDPMsg ( UDPWiFiService::ReqMsgType eReqType )
{
	if ( pMyProtocol != nullptr )
	{
		pMyProtocol->HandleCommand ( static_cast<uint8_t> ( eReqType ) );
		String sResponse = pMyProtocol->BuildResponse ( static_cast<uint8_t> ( eReqType ) );
		if ( sResponse.length() > 0 )
		{
			pMyUDPService->SendReply ( sResponse );
		}
	}
}

// ─── multicastMsg ─────────────────────────────────────────────────────────────
/**
 * @brief Builds and broadcasts an unsolicited UDP message to all known subnets.
 * @details Used to proactively push sensor readings or door-state changes to all
 *          listeners without waiting for a polling request.
 * @param eReqType The message type to build and broadcast (TEMPDATA or DOORDATA).
 */
void Application::multicastMsg ( UDPWiFiService::ReqMsgType eReqType )
{
	if ( pMyProtocol != nullptr )
	{
		String sResponse = pMyProtocol->BuildResponse ( static_cast<uint8_t> ( eReqType ) );
		if ( sResponse.length() > 0 )
		{
			pMyUDPService->SendAll ( sResponse );
		}
	}
}

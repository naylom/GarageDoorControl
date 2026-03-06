/*
 * Display.cpp
 *
 * Phase 7 refactoring: Display class accesses door and sensor state only
 * through IGarageDoor* and IEnvironmentSensor* — no extern globals for domain
 * objects remain here.
 *
 * Error(), Info() and DisplaylastInfoErrorMsg() remain as free functions so
 * that WiFiService and ISR callbacks can call them before the Display instance
 * is constructed.  They use extern references only for infrastructure objects
 * (MyLogger, pMyUDPService) which are not domain data.
 */

#include "Display.h"

#include "Logging.h"
#include "WiFiService.h"

#include <WiFiNINA.h>
#include <WiFiUdp.h>

// ─── Notification-bar module-scope state ─────────────────────────────────────
// Written by Error() / Info().  Read by DisplaylastInfoErrorMsg().

static bool s_bInfoUseBestTime = false;
static String s_sInfoErrorMsg;
static ansiVT220Logger::colours s_fgColour = ansiVT220Logger::FG_WHITE;
static ansiVT220Logger::colours s_bgColour = ansiVT220Logger::BG_GREEN;

// Infrastructure externs — not domain objects.
extern ansiVT220Logger MyLogger;
extern UDPWiFiService* pMyUDPService;

constexpr uint8_t ERROR_LINE = 25;
constexpr uint8_t NWPrintStartLine = 15;

// ─── Free function: Error ─────────────────────────────────────────────────────

/// @brief Records an error message for the notification bar, prepended with the current time.
/// @param s       Message text.
/// @param bInISR  Pass true when called from interrupt context (skips time lookup).
void Error ( String s, bool bInISR )
{
	String Result;
	if ( !bInISR && pMyUDPService != nullptr )
	{
		pMyUDPService->GetLocalTime ( Result );
	}
	s_sInfoErrorMsg = Result + s;
	s_fgColour = ansiVT220Logger::FG_BRIGHTWHITE;
	s_bgColour = ansiVT220Logger::BG_BRIGHTRED;
}

// ─── Free function: Info ──────────────────────────────────────────────────────

/// @brief Records an informational message for the notification bar, prepended with the current time.
/// @param s       Message text.
/// @param bInISR  Pass true when called from interrupt context (skips time lookup).
void Info ( String s, bool bInISR )
{
	String Result;
	if ( !bInISR )
	{
		s_bInfoUseBestTime = false;
		if ( pMyUDPService != nullptr && pMyUDPService->IsConnected() )
		{
			pMyUDPService->GetLocalTime ( Result );
			Result += " ";
		}
	}
	else
	{
		s_bInfoUseBestTime = true;
	}
	s_sInfoErrorMsg = Result + s;
	s_fgColour = ansiVT220Logger::FG_WHITE;
	s_bgColour = ansiVT220Logger::BG_BLUE;
}

// ─── Free function: DisplaylastInfoErrorMsg ───────────────────────────────────
/**
 * @brief Renders the most recent Info()/Error() message in the terminal notification bar.
 * @details When the message was recorded from ISR context, prepends the current
 *          NTP time (best-effort). Only compiled when MNDEBUG is defined.
 *          Safe to call frequently; does nothing in non-debug builds.
 */
void DisplaylastInfoErrorMsg ()
{
#ifdef MNDEBUG
	String sTime;
	if ( s_bInfoUseBestTime )
	{
		if ( pMyUDPService != nullptr )
		{
			pMyUDPService->GetLocalTime ( sTime );
		}
		s_sInfoErrorMsg = sTime + s_sInfoErrorMsg;
		s_bInfoUseBestTime = false;
	}
	MyLogger.ClearLine ( ERROR_LINE );
	MyLogger.COLOUR_AT ( s_fgColour, s_bgColour, ERROR_LINE, 1, s_sInfoErrorMsg );
#endif
}

// ─── Display constructor ──────────────────────────────────────────────────────
/**
 * @brief Constructs the Display, binding it to the logger and all data sources.
 * @param logger    Reference to the ANSI VT220 terminal logger used for output.
 * @param pUDPService Pointer to the WiFi/UDP service (used for time and network stats).
 * @param version   Firmware version string displayed in the heading row.
 * @param pDoor     Pointer to the garage door interface; may be nullptr if no door present.
 * @param pSensor   Pointer to the environment sensor interface; may be nullptr if no sensor.
 */
Display::Display ( ansiVT220Logger& logger,
                   UDPWiFiService* pUDPService,
                   const char* version,
                   IGarageDoor* pDoor,
                   IEnvironmentSensor* pSensor )
    : m_logger ( logger ), m_pUDPService ( pUDPService ), m_version ( version ), m_pDoor ( pDoor ),
      m_pSensor ( pSensor )
{
}

// ─── Display::DisplayUptime (private helper) ──────────────────────────────────
/**
 * @brief Renders the elapsed run time as "DD:HH:MM:SS" at the specified terminal position.
 * @details Records the first call time and computes relative elapsed time on
 *          subsequent calls. Resets the origin when millis() wraps around at ~49 days.
 * @param line Screen line (1-based) at which to print.
 * @param row  Screen column (1-based) at which to print.
 * @param fg   Foreground colour for the text.
 * @param bg   Background colour for the text.
 */
void Display::DisplayUptime ( uint8_t line, uint8_t row, ansiVT220Logger::colours fg, ansiVT220Logger::colours bg )
{
	static uint32_t ulStartTime = 0UL;
	uint32_t ulNow = millis();

	if ( ulStartTime == 0 )
	{
		ulStartTime = ulNow;
	}
	else
	{
		if ( ulNow > ulStartTime )
		{
			uint32_t ulTotal = ( ulNow - ulStartTime ) / 1000;
			uint32_t ulDays = ulTotal / ( 60 * 60 * 24 );
			uint32_t ulHours = ( ulTotal / ( 60 * 60 ) ) % 24;
			uint32_t ulMinutes = ( ulTotal / 60 ) % 60;
			uint32_t ulSecs = ulTotal % 60;

			char sUpTime [ 20 ];
			snprintf ( sUpTime,
			           sizeof ( sUpTime ),
			           "%02d:%02d:%02d:%02d",
			           (int)ulDays,
			           (int)ulHours,
			           (int)ulMinutes,
			           (int)ulSecs );
			m_logger.COLOUR_AT ( fg, bg, line, row, sUpTime );
		}
		else
		{
			// millis() wrapped around — reset origin
			ulStartTime = ulNow;
		}
	}
}

// ─── Display::DisplayStats ────────────────────────────────────────────────────
/**
 * @brief Renders the full debug status screen: uptime, heading, sensor readings,
 *        door state, network status, and the notification bar.
 * @details Compiled only when MNDEBUG is defined. Calls DisplayNWStatus() and
 *          DisplaylastInfoErrorMsg() as sub-steps. Intended to be called at
 *          approximately 2 Hz from Application::loop().
 */
void Display::DisplayStats ()
{
#ifdef MNDEBUG
	// Row 1: uptime | heading (with software version) | current time
	DisplayUptime ( 1, 1, ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK );

	String Heading = ( m_pDoor != nullptr ) ? F ( "Garage Door Control -  ver " ) : F ( "Temp Sensor - ver " );
	Heading += String ( m_version );
	m_logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, 1, 20, Heading );

	String sTime;
	if ( m_pUDPService != nullptr )
	{
		m_pUDPService->GetLocalTime ( sTime );
	}
	m_logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, 1, 60, sTime );

	// ── Garage door section ───────────────────────────────────────────────────
	if ( m_pDoor != nullptr )
	{
		m_logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, 4, 0, F ( "Light is " ) );
		m_logger.ClearPartofLine ( 4, 14, 3 );
		m_logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN,
		                     ansiVT220Logger::BG_BLACK,
		                     4,
		                     14,
		                     m_pDoor->IsLit() ? F ( "On" ) : F ( "Off" ) );

		m_logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, 5, 0, F ( "State is " ) );
		m_logger.ClearPartofLine ( 5, 14, 8 );
		const char* stateStr = m_pDoor->GetStateDisplayString();
		auto stateColour =
		    ( m_pDoor->GetState() == IGarageDoor::State::Closed ) ? ansiVT220Logger::FG_CYAN : ansiVT220Logger::FG_RED;
		m_logger.COLOUR_AT ( stateColour, ansiVT220Logger::BG_BLACK, 5, 14, stateStr );
	}
	else
	{
		m_logger.COLOUR_AT ( ansiVT220Logger::FG_YELLOW, ansiVT220Logger::BG_BLACK, 4, 0, F ( "No garage door" ) );
	}

	// ── Environment sensor section ────────────────────────────────────────────
	if ( m_pSensor != nullptr )
	{
		const EnvironmentReading& env = m_pSensor->GetLastReading();
		if ( env.valid )
		{
			m_logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, 12, 0, F ( "Temperature is " ) );
			m_logger.ClearPartofLine ( 12, 16, 6 );
			m_logger.COLOUR_AT ( ansiVT220Logger::FG_RED,
			                     ansiVT220Logger::BG_BLACK,
			                     12,
			                     16,
			                     String ( env.temperature ) );

			m_logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, 13, 0, F ( "Humidity is " ) );
			m_logger.ClearPartofLine ( 13, 16, 6 );
			m_logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN, ansiVT220Logger::BG_BLACK, 13, 16, String ( env.humidity ) );

			m_logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, 14, 0, F ( "Pressure is " ) );
			m_logger.ClearPartofLine ( 14, 16, 7 );
			m_logger.COLOUR_AT ( ansiVT220Logger::FG_YELLOW,
			                     ansiVT220Logger::BG_BLACK,
			                     14,
			                     16,
			                     String ( env.pressure ) );
		}
	}
	else
	{
		m_logger.COLOUR_AT ( ansiVT220Logger::FG_YELLOW, ansiVT220Logger::BG_BLACK, 12, 0, F ( "No sensor" ) );
	}

	DisplayNWStatus();
	DisplaylastInfoErrorMsg();
#endif
}

// ─── Display::DisplayNWStatus ─────────────────────────────────────────────────
/**
 * @brief Renders the network status panel: SSID, hostname, IP address, subnet mask,
 *        multicast destination list, gateway, MAC address, signal strength,
 *        WiFi connection counters, and message statistics.
 * @details Does nothing if m_pUDPService is nullptr. Called by DisplayStats().
 */
void Display::DisplayNWStatus ()
{
	m_logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, NWPrintStartLine, 0, F ( "SSID: " ) );
	m_logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN, ansiVT220Logger::BG_BLACK, NWPrintStartLine, 23, WiFi.SSID() );

	if ( m_pUDPService == nullptr )
	{
		return;
	}

	FixedIPList* pMulticastDestList = m_pUDPService->GetMulticastList();
	if ( pMulticastDestList != nullptr )
	{
		uint8_t iterator = pMulticastDestList->GetIterator();
		IPAddress mcastDest;
		while ( ( mcastDest = pMulticastDestList->GetNext ( iterator ) ) != IPAddress ( (uint32_t)0 ) )
		{
			m_logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE,
			                     ansiVT220Logger::BG_BLACK,
			                     NWPrintStartLine + iterator - 1,
			                     41,
			                     "Mcast #" + String ( iterator ) + ": " );
			m_logger.ClearPartofLine ( NWPrintStartLine + iterator - 1, 61, 15 );
			m_logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN,
			                     ansiVT220Logger::BG_BLACK,
			                     NWPrintStartLine + iterator - 1,
			                     61,
			                     m_pUDPService->ToIPString ( mcastDest ) );
		}
	}

	m_logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE,
	                     ansiVT220Logger::BG_BLACK,
	                     NWPrintStartLine + 1,
	                     0,
	                     F ( "My Hostname: " ) );
	m_logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN,
	                     ansiVT220Logger::BG_BLACK,
	                     NWPrintStartLine + 1,
	                     23,
	                     m_pUDPService->GetHostName() );

	m_logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE,
	                     ansiVT220Logger::BG_BLACK,
	                     NWPrintStartLine + 2,
	                     0,
	                     F ( "IP Address: " ) );
	m_logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN,
	                     ansiVT220Logger::BG_BLACK,
	                     NWPrintStartLine + 2,
	                     23,
	                     m_pUDPService->ToIPString ( WiFi.localIP() ) );

	m_logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE,
	                     ansiVT220Logger::BG_BLACK,
	                     NWPrintStartLine + 3,
	                     0,
	                     F ( "Subnet Mask: " ) );
	m_logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN,
	                     ansiVT220Logger::BG_BLACK,
	                     NWPrintStartLine + 3,
	                     23,
	                     m_pUDPService->ToIPString ( WiFi.subnetMask() ) );

	m_logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE,
	                     ansiVT220Logger::BG_BLACK,
	                     NWPrintStartLine + 4,
	                     0,
	                     F ( "Local Multicast Addr: " ) );
	m_logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN,
	                     ansiVT220Logger::BG_BLACK,
	                     NWPrintStartLine + 4,
	                     23,
	                     m_pUDPService->ToIPString ( m_pUDPService->GetMulticastAddress() ) );

	m_logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE,
	                     ansiVT220Logger::BG_BLACK,
	                     NWPrintStartLine + 4,
	                     41,
	                     F ( "WiFi connect/fail: " ) );
	m_logger.ClearPartofLine ( NWPrintStartLine + 4, 61, 10 );
	m_logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN,
	                     ansiVT220Logger::BG_BLACK,
	                     NWPrintStartLine + 4,
	                     61,
	                     String ( m_pUDPService->GetBeginCount() ) + "/" +
	                         String ( m_pUDPService->GetBeginTimeOutCount() ) );

	m_logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE,
	                     ansiVT220Logger::BG_BLACK,
	                     NWPrintStartLine + 5,
	                     41,
	                     F ( "Multicasts sent: " ) );
	m_logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN,
	                     ansiVT220Logger::BG_BLACK,
	                     NWPrintStartLine + 5,
	                     61,
	                     String ( m_pUDPService->GetMCastSentCount() ) );

	m_logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE,
	                     ansiVT220Logger::BG_BLACK,
	                     NWPrintStartLine + 6,
	                     41,
	                     F ( "Requests recvd: " ) );
	m_logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN,
	                     ansiVT220Logger::BG_BLACK,
	                     NWPrintStartLine + 6,
	                     61,
	                     String ( m_pUDPService->GetRequestsReceivedCount() ) );

	m_logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE,
	                     ansiVT220Logger::BG_BLACK,
	                     NWPrintStartLine + 7,
	                     41,
	                     F ( "Replies sent: " ) );
	m_logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN,
	                     ansiVT220Logger::BG_BLACK,
	                     NWPrintStartLine + 7,
	                     61,
	                     String ( m_pUDPService->GetReplySentCount() ) );

	m_logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE,
	                     ansiVT220Logger::BG_BLACK,
	                     NWPrintStartLine + 5,
	                     0,
	                     F ( "Mac address: " ) );
	byte bMac [ 6 ] = { 0 };
	WiFi.macAddress ( bMac );
	char s [ 18 ];
	snprintf ( s,
	           sizeof ( s ),
	           "%02X:%02X:%02X:%02X:%02X:%02X",
	           bMac [ 5 ],
	           bMac [ 4 ],
	           bMac [ 3 ],
	           bMac [ 2 ],
	           bMac [ 1 ],
	           bMac [ 0 ] );
	m_logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN, ansiVT220Logger::BG_BLACK, NWPrintStartLine + 5, 23, s );

	m_logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE,
	                     ansiVT220Logger::BG_BLACK,
	                     NWPrintStartLine + 6,
	                     0,
	                     F ( "Gateway Address: " ) );
	m_logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN,
	                     ansiVT220Logger::BG_BLACK,
	                     NWPrintStartLine + 6,
	                     23,
	                     m_pUDPService->ToIPString ( WiFi.gatewayIP() ) );

	m_logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE,
	                     ansiVT220Logger::BG_BLACK,
	                     NWPrintStartLine + 7,
	                     0,
	                     F ( "Signal strength (RSSI):" ) );
	m_logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN,
	                     ansiVT220Logger::BG_BLACK,
	                     NWPrintStartLine + 7,
	                     23,
	                     String ( WiFi.RSSI() ) );
	m_logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, NWPrintStartLine + 7, 30, F ( " dBm" ) );

	m_logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE,
	                     ansiVT220Logger::BG_BLACK,
	                     NWPrintStartLine + 8,
	                     0,
	                     F ( "WiFi Status: " ) );
	m_logger.ClearPartofLine ( NWPrintStartLine + 8, 23, 15 );
	m_logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN,
	                     ansiVT220Logger::BG_BLACK,
	                     NWPrintStartLine + 8,
	                     23,
	                     m_pUDPService->WiFiStatusToString ( WiFi.status() ) );

	m_logger.COLOUR_AT ( ansiVT220Logger::FG_WHITE,
	                     ansiVT220Logger::BG_BLACK,
	                     NWPrintStartLine + 8,
	                     41,
	                     F ( "WiFi Service State: " ) );
	m_logger.COLOUR_AT ( ansiVT220Logger::FG_CYAN,
	                     ansiVT220Logger::BG_BLACK,
	                     NWPrintStartLine + 8,
	                     61,
	                     String ( m_pUDPService->GetState() ) );
}

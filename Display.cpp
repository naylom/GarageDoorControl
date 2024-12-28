#include <WiFiNINA.h>
#include <WiFiUdp.h>
#include "logging.h"
#include "WiFiService.h"
#include "Display.h"

bool	bInfoUseBestTime = false;
extern ansiVT220Logger MyLogger;
extern UDPWiFiService	 *pMyUDPService;
extern const char * VERSION;
// Error message process used when generating messages during interrupt
constexpr uint8_t ERROR_LINE	   = 25;
constexpr uint8_t NWPrintStartLine = 15;  // Start line for network stats

auto   	fgInfoErrorColour = ansiVT220Logger::FG_WHITE;
auto   	bgInfoErrorColour = ansiVT220Logger::BG_GREEN;


String	sInfoErrorMsg;
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

/// @brief Logs error to error line the provided error message prepended with local date and time
/// @param s message to be logged
/// @param bInISR indicates if code being called from interrupt level code, defaults to false
void   Error ( String s, bool bInISR )
{
	String Result;
	// do not call when in ISR level code path
	if ( !bInISR )
	{
		pMyUDPService->GetLocalTime ( Result );
	}
	sInfoErrorMsg	  = Result + s;
	fgInfoErrorColour = ansiVT220Logger::FG_BRIGHTWHITE;
	bgInfoErrorColour = ansiVT220Logger::BG_BRIGHTRED;
}

/// @brief Logs info to error line the provided error message prepended with local date and time
/// @param s message to be logged
/// @param bInISR indicates if code being called from interrupt level code, defaults to false
void Info ( String s, bool bInISR )
{
	String Result;
	// do not call when in ISR level code path
	if ( !bInISR )
	{
		bInfoUseBestTime = false;
		GetLocalTime ( Result );
	}
	else
	{
		bInfoUseBestTime = true;
	}
	sInfoErrorMsg	  = Result + s;
	fgInfoErrorColour = ansiVT220Logger::FG_WHITE;
	bgInfoErrorColour = ansiVT220Logger::BG_BLUE;
}

void DisplaylastInfoErrorMsg ()
{
#ifdef MNDEBUG
	String sTime;
	if ( bInfoUseBestTime )
	{
		pMyUDPService->GetLocalTime( sTime );
		sInfoErrorMsg = sTime + sInfoErrorMsg;
		bInfoUseBestTime = false;
	}

	MyLogger.ClearLine ( ERROR_LINE );
	MyLogger.COLOUR_AT ( fgInfoErrorColour, bgInfoErrorColour, ERROR_LINE, 1, sInfoErrorMsg );
#endif
}

/**
 * @brief Display the uptime on the screen
 * @param logger the logger to use
 * @param line the line to display the uptime
 * @param row the row to display the uptime
 * @param Foreground the colour of the text
 * @param Background the colour of the background
 */
void DisplayUptime ( ansiVT220Logger logger, uint8_t line, uint8_t row, ansiVT220Logger::colours Foreground, ansiVT220Logger::colours Background )
{
	static uint32_t ulStartTime = 0UL;
	uint32_t 		ulNow = millis ();
	// set initial start time
	if ( ulStartTime == 0 )
	{
		ulStartTime = ulNow;
	}
	else
	{
		// check for wrap around
		if ( ulNow > ulStartTime )
		{
			uint32_t ulTotalNumSeconds = ( ulNow-ulStartTime ) / 1000;
			uint32_t ulDays			   = ulTotalNumSeconds / ( 60 * 60 * 24 );
			uint32_t ulHours		   = ( ulTotalNumSeconds / ( 60 * 60 ) ) % 24;
			uint32_t ulMinutes		   = ( ulTotalNumSeconds / 60 ) % 60;
			uint32_t ulSecs			   = ulTotalNumSeconds % 60;

			char	 sUpTime [ 20 ];
			sprintf ( sUpTime, "%02d:%02d:%02d:%02d", ulDays, ulHours, ulMinutes, ulSecs );
			logger.COLOUR_AT ( Foreground, Background, line, row, sUpTime );
		}
		else
		{
			// wrapped around
			ulStartTime = ulNow;
		}
	}
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
		MyLogger.ClearPartofLine ( 4, 14, 3 );
		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_CYAN, ansiVT220Logger::BG_BLACK, 4, 14, pGarageDoor->IsLit () ? "On" : "Off" );

		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, 5, 0, F ( "State is " ) );
		MyLogger.ClearPartofLine ( 5, 14, 8 );
		if ( pGarageDoor->GetDoorState() == DoorState::Closed )
		{
			MyLogger.COLOUR_AT ( ansiVT220Logger::FG_CYAN, ansiVT220Logger::BG_BLACK, 5, 14, pGarageDoor->GetDoorDisplayState () );
		}
		else
		{
			MyLogger.COLOUR_AT ( ansiVT220Logger::FG_RED, ansiVT220Logger::BG_BLACK, 5, 14, pGarageDoor->GetDoorDisplayState () );
		}

		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, 6, 0, F ( "Direction is " ) );
		MyLogger.ClearPartofLine ( 6, 14, 10 );
		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_CYAN, ansiVT220Logger::BG_BLACK, 6, 14, pGarageDoor->GetDoorDirection () );

		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, 4, 25, F ( "Light Off count     " ) );
		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_GREEN, ansiVT220Logger::BG_BLACK, 4, 43, String ( pGarageDoor->GetLightOffCount () ) );

		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, 5, 25, F ( "Door Opened count   " ) );
		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_GREEN, ansiVT220Logger::BG_BLACK, 5, 43, String ( pGarageDoor->GetDoorOpenedCount () ) );

		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, 6, 25, F ( "Door Closed count   " ) );
		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_GREEN, ansiVT220Logger::BG_BLACK, 6, 43, String ( pGarageDoor->GetDoorClosedCount () ) );
	}
	MyLogger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, 9, 43, "Count     Called Unchngd Matched UnMtchdSpurious Duration" );
	
	MyLogger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, 10, 25, F ( "Switch Presssed " ) );
	if ( pDoorSwitchPin != nullptr )
	{
		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_GREEN, ansiVT220Logger::BG_BLACK, 10, 43, String ( pDoorSwitchPin->GetMatchedCount () ) );
		pDoorSwitchPin->DebugStats ( result );
		MyLogger.COLOUR_AT ( ansiVT220Logger::FG_WHITE, ansiVT220Logger::BG_BLACK, 10, 50, result );
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

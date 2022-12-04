#include "WiFiService.h"
#include "DoorState.h"
/*

WiFiService

Arduino class to encapsulate UDP data service over WiFi

Author: (c) M. Naylor 2022

History:
	Ver 1.0			Initial version
*/
/*
    UDP config
*/

constexpr 	auto	WIFI_FLASHTIME 		 = 10;					// every 1 second

// Valid message parts
const char cVer1[] = "V001";
const char cMsg1[] = "M001";
const char cMsg2[] = "M002";
const char cMsg3[] = "M003";

constexpr 	auto MAX_INCOMING_UDP_MSG = 255;
const uint8_t	PrintStartLine = 15;

enum eResponseMessage { TEMPDATA, DOORDATA };


void TerminateProgram ( const __FlashStringHelper* pErrMsg )
{
	Error ( pErrMsg );
	LogFlush;
	while ( true )
        ;
}

						WiFiService::WiFiService ( )
{
}
const char *  WiFiService::GetHostName()
{
	return m_HostName;
}
WiFiService::Status	WiFiService::GetStatus( )
{
	return m_State;
}
void			WiFiService::SetLED ( RGBType theColour, uint8_t flashTime )
{
	if ( m_pLED != nullptr )
	{
		m_pLED->SetLEDColour( theColour, flashTime );
	}	
}
void WiFiService::SetState ( WiFiService::Status state )
{
	switch ( state )
	{
		case WiFiService::Status::CONNECTED:
			m_State = state;
			SetLED ( CONNECTED_COLOUR );
			break;
		case WiFiService::Status::UNCONNECTED:
			m_State = state;		
			SetLED ( UNCONNECTED_COLOUR, WIFI_FLASHTIME );
			break;
	}
}
void 	WiFiService::Begin ( const char * HostName, const char * WiFissid, const char * WiFipwd, MNRGBLEDBaseLib * pLED )
{
	m_SSID				= WiFissid;
	m_Pwd				= WiFipwd;
	m_HostName			= HostName;
	m_pLED				= pLED;

	WiFi.setHostname ( m_HostName );

	String fv = WiFi.firmwareVersion ();
	if ( fv < WIFI_FIRMWARE_LATEST_VERSION )
	{
		SetLED ( OLD_WIFI_FIRMWARE_COLOUR );
		Error ( "Please upgrade the firmware. Latest is " + String (WIFI_FIRMWARE_LATEST_VERSION) + ", board has " + String (fv)  );
	}
}
bool WiFiService::WiFiConnect()
{
	bool 	bResult = false;
	uint8_t iAttempts = 0;

	if ( WiFi.status() != WL_CONNECTED )
	{
		SetState ( WiFiService::Status::UNCONNECTED );
		// attempt to connect to WiFi network:
		Error ( "Attempting to connect to Network named: " + String ( m_SSID ) + String ( WiFi.status() ) );
		do
		{
			if ( WiFi.begin ( m_SSID, m_Pwd ) == WL_CONNECTED )
			{
				bResult = true;
				SetState ( WiFiService::Status::CONNECTED );			
				Error ( "Connected to Network named: " + String ( m_SSID ) );
				break;
			}
			delay ( 1000 );
			iAttempts ++;
		} while ( iAttempts < 20 );	
	}
	else
	{
		bResult = true;
	}
	return bResult;	
}
void 	WiFiService::Stop()
{
	WiFi.end();
	SetState ( WiFiService::Status::UNCONNECTED );
}
bool	WiFiService::Check()
{
	bool bResult = false;

	if ( WiFi.status () == WL_NO_MODULE )
	{
		SetState ( WiFiService::Status::UNCONNECTED );
	}
	else
	{
		if ( WiFiConnect() )
		{
			bResult = true;
		}
		else
		{
			SetState ( WiFiService::Status::UNCONNECTED );
			Error ( "Unable to connect to WiFi ");
		}
	}
	return bResult;
}
String	WiFiService::ToIPString ( const IPAddress& address )
{
	return String(address[0]) + "." + address[1] + "." + address[2] + "." + address[3];
}

		UDPWiFiService::UDPWiFiService(){}

bool	UDPWiFiService::Begin ( UDPWiFiServiceCallback  pHandleReqData, const char *WiFissid, const char *WiFipwd, const char * HostName, const uint16_t portUDP, MNRGBLEDBaseLib * pLED )
{
	bool bResult = false;

	WiFiService::Begin ( HostName, WiFissid, WiFipwd, pLED );
	m_Port					= portUDP;
	m_MsghHandlerCallback	= pHandleReqData;

	if ( m_sUDPReceivedMsg.reserve ( MAX_INCOMING_UDP_MSG ) )
	{
		bResult = true;			
	}
	return bResult;
}
bool	UDPWiFiService::Begin ( UDPWiFiServiceCallback  pHandleReqData, const char *WiFissid, const char *WiFipwd, const char * HostName,  MNRGBLEDBaseLib * pLED, const uint16_t portUDP )
{
	return Begin ( pHandleReqData, WiFissid, WiFipwd, HostName, portUDP, pLED) ;	
}

void	UDPWiFiService::CheckUDP()
{
	String Msg;
	if ( GetUDPMessage( Msg ) )
	{
		ProcessUDPMessage ( Msg );
	}
}

void	UDPWiFiService::DisplayStatus()
{
#ifdef MNDEBUG
#ifdef ARDUINO_AVR_UNO
    Log (F ("Connected to: "));
    Log (wifi.SSID ());
    Log (F (", "));
    Logln (wifi.RSSI ());
    Log (F ("IP address: "));
    Logln (wifi.localIP ());
    Log (F ("Status: "));
    Logln (wifi.status ());          //2- wifi connected with ip, 3- got connection with servers or clients, 4- disconnect with clients or servers, 5- no wifi
#else
	// print the SSID of the network you're attached to:
	COLOUR_AT ( FG_WHITE, BG_BLACK, PrintStartLine, 0,  F ("SSID: ") );
	COLOUR_AT ( FG_CYAN, BG_BLACK, PrintStartLine, 23, WiFi.SSID () );

	COLOUR_AT ( FG_WHITE, BG_BLACK, PrintStartLine + 1, 0,  F ("My Hostname: ") );
	COLOUR_AT ( FG_CYAN, BG_BLACK, PrintStartLine + 1, 23, GetHostName() );

	// print your board's IP address:
	COLOUR_AT ( FG_WHITE, BG_BLACK, PrintStartLine + 2, 0, F ( "IP Address: " ) );
	COLOUR_AT ( FG_CYAN, BG_BLACK, PrintStartLine + 2, 23, ToIPString ( WiFi.localIP () ) );

	COLOUR_AT ( FG_WHITE, BG_BLACK, PrintStartLine + 3, 0, "Subnet Mask: " );
	COLOUR_AT ( FG_CYAN, BG_BLACK, PrintStartLine + 3, 23,  ToIPString ( WiFi.subnetMask () ) );

	COLOUR_AT ( FG_WHITE, BG_BLACK, PrintStartLine + 4, 0,  F ( "Mac address: " ) );
	byte bMac [ 6 ];
	WiFi.macAddress ( bMac );
	char  s[18];
	sprintf ( s, "%02X:%02X:%02X:%02X:%02X:%02X", bMac[5], bMac[4], bMac[3], bMac[2],bMac[1], bMac[0] ); 
	COLOUR_AT ( FG_CYAN, BG_BLACK, PrintStartLine + 4, 23,  s );

	COLOUR_AT ( FG_WHITE, BG_BLACK, PrintStartLine + 5, 0,  F ( "Gateway Address: " ) );
	COLOUR_AT ( FG_CYAN, BG_BLACK, PrintStartLine + 5, 23, ToIPString ( WiFi.gatewayIP () ) );
	// print the received signal strength:
	
	COLOUR_AT ( FG_WHITE, BG_BLACK, PrintStartLine + 6, 0,  F ( "Signal strength (RSSI):" ) );
	COLOUR_AT ( FG_CYAN, BG_BLACK, PrintStartLine + 6, 23, String ( WiFi.RSSI () ) );
	COLOUR_AT ( FG_WHITE, BG_BLACK, PrintStartLine + 6, 30,  F ( " dBm" ) );

	COLOUR_AT ( FG_WHITE, BG_BLACK, PrintStartLine + 7, 0,  F ( "WiFi Status: " ) );
	COLOUR_AT ( FG_CYAN, BG_BLACK, PrintStartLine + 7, 23, String ( WiFi.status () ) );
#endif
#endif
}

bool	UDPWiFiService::GetUDPMessage ( String& sRecvMessage )
{
	bool bResult = false;
	char sBuffer [ MAX_UDP_RECV_LEN ];

	// check still connected
	if ( WiFiConnect() )
	{
		// wifi connected, so see if we have a message

		// if there's data available, read a packet
		unsigned int packetSize = m_myUDP.parsePacket ();
		if ( packetSize > 0 )
		{
			SetLED ( PROCESSING_MSG_COLOUR );
			delay ( 500 );
			String s = "Received packet of size " + String ( packetSize ) + " From " + ToIPString ( m_myUDP.remoteIP () ) +  ", port " + String ( m_myUDP.remotePort () );
			Error ( s );

			if ( packetSize < sizeof ( sBuffer ) - 1 )
			{
				// read the packet into packetBufffer
				int len = m_myUDP.read ( sBuffer, sizeof ( sBuffer ) - 1 );
				sBuffer [ len ] = 0;
				sRecvMessage = sBuffer;
				bResult = true;
			}
		}
	}
	else
	{
		// Could not connect to wifi
		Stop();
		Start();
	}	
	return bResult;
}
void	UDPWiFiService::SendReply ( String sMsg )
{
	if ( m_myUDP.beginPacket ( m_myUDP.remoteIP (), m_myUDP.remotePort () ) == 1 )
	{
		m_myUDP.write ( sMsg.c_str () );
		if ( m_myUDP.endPacket () == 0 )
		{
			Error ( F ( "Message Response failed" ) );
		}
		else
		{
			Error ( F ( "Sent reply: " ) ); // Log ( sResponse ); Log ( F ( " To " ) ); Log ( remoteIp ); Log ( F ( ":" ) ); Logln ( remotePort );
		}		
	}
	SetState ( WiFiService::Status::CONNECTED );
}
bool	UDPWiFiService::Start()
{
	bool bResult = false;

	if ( Check () )
	{

		// connected to set up UDP port to use
		if (  m_myUDP.begin ( m_Port ) == 1 )
		{
			m_ulLastClientReq = millis();
			bResult = true;
		}
		else
		{
			Error ( "Unable to allocate UDP Port" );
			Stop();
		}
	}
	else
	{
		Stop();
	}
	return bResult;	
}
void	UDPWiFiService::Stop()
{
	m_myUDP.stop();
	WiFiService::Stop();
	Error ( "Stopped WiFi");
}

void	UDPWiFiService::ProcessUDPMessage ( const String& sRecvMessage )
{
	if ( sRecvMessage.startsWith ( cVer1 ) )
	{
		Error ( F ( "V1 message received Type: " ) );
		// Version 1 message received
		if ( sRecvMessage.substring ( sizeof ( cVer1 ) ).startsWith ( cMsg1 ) )
		{
			// Got a data request
			Error ( F ( "Temp Data request" ) );
			m_MsghHandlerCallback ( UDPWiFiService::ReqMsgType::TEMPDATA );
		}
		else if ( sRecvMessage.substring ( sizeof ( cVer1 ) ).startsWith ( cMsg2 ) )
		{
			// Got a reset request
			ResetBoard ( F ( "Reset request" ) );
		}
		else if ( sRecvMessage.substring ( sizeof ( cVer1 ) ).startsWith ( cMsg3 ) )
		{
			// Got a door status request
			Error ( F ( "Door Data request" ) );			
			m_MsghHandlerCallback ( UDPWiFiService::ReqMsgType::DOORDATA );
		}		
		else
		{
			Error ( F ( "Unknown request" ) );
		}
	}
	else
	{
		Error ( F ( "Unknown message version" ) );
	}
}

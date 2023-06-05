/*

WiFiService

Arduino class to encapsulate UDP data service over WiFi

Author: (c) M. Naylor 2022

History:
	Ver 1.0			Initial version
*/
#include <WiFiNINA.h>
#include <time.h>
#include "WiFiService.h"
#include "DoorState.h"

const char		 *WiFiStatus []			= { "WL_IDLE_STATUS",																																											   // = 0,
											"WL_NO_SSID_AVAIL", "WL_SCAN_COMPLETED", "WL_CONNECTED", "WL_CONNECT_FAILED", "WL_CONNECTION_LOST", "WL_DISCONNECTED", "WL_AP_LISTENING", "WL_AP_CONNECTED", "WL_AP_FAILED", "WL_NO_MODULE" }; // = 255
/*
	UDP config
*/
constexpr auto	  WIFI_FLASHTIME		= 10; // every 1/2 second

// Valid message parts
constexpr char	  cMsgVersion1 []		= "V001"; // message version
constexpr char	  TempHumidityReqMsg [] = "M001"; // Req temp / humidity
constexpr char	  RestartReqMsg []		= "M002"; // Req restart
constexpr char	  DoorStatusReqMsg []	= "M003"; // Req Door status
constexpr char	  DoorOpenReqMsg []		= "M004"; // Req Door Open
constexpr char	  DoorCloseReqMsg []	= "M005"; // Req Door Close
constexpr char	  DoorStopReqMsg []		= "M006"; // Req Door Stop
constexpr char	  DoorLightOnReqMsg []	= "M007"; // Req Light On
constexpr char	  DoorLightOffReqMsg [] = "M008"; // Req Light off
constexpr char	  PartSeparator []		= ":";

constexpr auto	  MAX_INCOMING_UDP_MSG	= 255;
constexpr uint8_t PrintStartLine		= 15;

enum eResponseMessage { TEMPDATA, DOORDATA };

#define CALL_MEMBER_FN_BY_PTR( object, ptrToMember ) ( ( object )->*( ptrToMember ) )

void TerminateProgram ( const __FlashStringHelper *pErrMsg )
{
	Error ( pErrMsg );
	LogFlush;
	while ( true )
		;
}

WiFiService::WiFiService ()
{
	setenv("TZ","GMTGMT-1,M3.4.0/01,M10.4.0/02",1);
}

const char *WiFiService::WiFiStatusToString ( uint8_t iState )
{
	if ( iState == 255 )
	{
		return WiFiStatus [ 9 ];
	}
	else
	{
		if ( iState > sizeof ( WiFiStatus ) / sizeof ( WiFiStatus [ 0 ] ) - 1 )
		{
			return "Unknown";
		}
		else
		{
			return WiFiStatus [ iState ];
		}
	}
}

const char *WiFiService::GetHostName ()
{
	return m_HostName;
}

unsigned long WiFiService::GetTime ()
{
	return WiFi.getTime ();
}

WiFiService::Status WiFiService::GetState ()
{
	return m_State;
}

void WiFiService::SetLED ( RGBType theColour, uint8_t flashTime )
{
	if ( m_pLED != nullptr )
	{
		m_pLED->SetLEDColour ( theColour, flashTime );
	}
}

void WiFiService::SetState ( WiFiService::Status state )
{
	m_State = state;
	switch ( state )
	{
		case WiFiService::Status::CONNECTED:
			SetLED ( CONNECTED_COLOUR );
			break;

		case WiFiService::Status::UNCONNECTED:
			SetLED ( UNCONNECTED_COLOUR, WIFI_FLASHTIME );
			break;
	}
}

void WiFiService::Begin ( const char *HostName, const char *WiFissid, const char *WiFipwd, MNRGBLEDBaseLib *pLED )
{
	m_SSID	   = WiFissid;
	m_Pwd	   = WiFipwd;
	m_HostName = HostName;
	m_pLED	   = pLED;

	WiFi.setHostname ( m_HostName );

	String fv = WiFi.firmwareVersion ();
	if ( fv < WIFI_FIRMWARE_LATEST_VERSION )
	{
		SetLED ( OLD_WIFI_FIRMWARE_COLOUR );
		Error ( "Please upgrade the firmware. Latest is " + String ( WIFI_FIRMWARE_LATEST_VERSION ) + ", board has " + String ( fv ) );
	}
	else
	{
		SetState ( Status::UNCONNECTED );
	}
}

void WiFiService::CalcMyMulticastAddress ( IPAddress& result )
{
	CalcMulticastAddress ( WiFi.localIP (), result );
}

void WiFiService::CalcMulticastAddress ( IPAddress ip, IPAddress& subnetMask )
{
	subnetMask = IPAddress ( 0UL ); // WiFi.subnetMask();
	uint8_t	  firstOctet = ( ip & 0xff );
	if ( firstOctet > 0 && firstOctet <= 127 )
	{
		// class A
		subnetMask = IPAddress ( 255, 0, 0, 0 );
	}
	else if ( firstOctet > 127 && firstOctet <= 191 )
	{
		// Class B
		subnetMask = IPAddress ( 255, 255, 0, 0 );
	}
	else if ( firstOctet > 191 && firstOctet <= 223 )
	{
		// Class C
		subnetMask = IPAddress ( 255, 255, 255, 0 );
	}
	subnetMask = ( ip & subnetMask ) | ( ~subnetMask );
}

inline IPAddress WiFiService::GetMulticastAddress ()
{
	return m_multicastAddr;
}

bool WiFiService::WiFiConnect ()
{
	bool			bResult		= true;
	static uint32_t iStartCount = 0UL;

	if ( WiFi.status () != WL_CONNECTED )
	{
		Error ( "Starting WiFi, attempt " + String ( iStartCount ) );
		uint8_t	 status;
		uint32_t ulStart = millis ();

		WiFi.begin ( m_SSID, m_Pwd );
		String msg = "connecting ";
		do
		{
			status = WiFi.status ();
			delay ( 500 );
			Error ( msg );
			msg += ".";
		}
		while ( status != WL_CONNECTED && ( millis () - ulStart ) < 10000 );

		if ( status != WL_CONNECTED )
		{
			bResult = false;

			SetState ( WiFiService::Status::UNCONNECTED );
			Error ( "Connect failed, status is " + String ( WiFiStatusToString ( status ) ) );
			iStartCount++;
			m_beginTimeouts++;
		}
		else
		{
			CalcMyMulticastAddress ( m_multicastAddr );
			Error ( "Connected to " + String ( m_SSID ) );
			iStartCount = 0UL;
			m_beginConnects++;		
		}
	}

	return bResult;
}

void WiFiService::Stop ()
{
	WiFi.end ();
	Error ( "Stopping wifi" );
	SetState ( WiFiService::Status::UNCONNECTED );
}

String WiFiService::ToIPString ( const IPAddress &address )
{
	return String ( address [ 0 ] ) + "." + address [ 1 ] + "." + address [ 2 ] + "." + address [ 3 ];
}

/***************************************************************************************************************************************/
/*
 *
 *
 *
 *
 *
 *	UDPWiFiService Class
 *
 *
 *
 *
 *
 */
/***************************************************************************************************************************************/
UDPWiFiService::UDPWiFiService ()
{
	m_pMulticastDestList = new FixedIPList ( 4 );
}

bool UDPWiFiService::Begin ( UDPWiFiServiceCallback pHandleReqData, const char *WiFissid, const char *WiFipwd, const char *HostName, const uint16_t portUDP, MNRGBLEDBaseLib *pLED )
{
	bool bResult = false;

	WiFiService::Begin ( HostName, WiFissid, WiFipwd, pLED );
	m_Port				 = portUDP;
	m_MsgHandlerCallback = pHandleReqData;

	if ( m_sUDPReceivedMsg.reserve ( MAX_INCOMING_UDP_MSG ) )
	{
		bResult = true;
	}
	return bResult;
}

bool UDPWiFiService::Begin ( UDPWiFiServiceCallback pHandleReqData, const char *WiFissid, const char *WiFipwd, const char *HostName, MNRGBLEDBaseLib *pLED, const uint16_t portUDP )
{
	return Begin ( pHandleReqData, WiFissid, WiFipwd, HostName, portUDP, pLED );
}

void UDPWiFiService::CheckUDP ()
{
	String Msg = "?";
	if ( GetUDPMessage ( &Msg ) )
	{
		ProcessUDPMessage ( Msg );
	}
}

void UDPWiFiService::DisplayStatus ()
{
#ifdef MNDEBUG
	// print the SSID of the network you're attached to:
	COLOUR_AT ( FG_WHITE, BG_BLACK, PrintStartLine, 0, F ( "SSID: " ) );
	COLOUR_AT ( FG_CYAN, BG_BLACK, PrintStartLine, 23, WiFi.SSID () );
	if ( m_pMulticastDestList != nullptr )
	{
		uint8_t	  iterator = m_pMulticastDestList->GetIterator ();
		IPAddress mcastDest;
		while ( (long unsigned int)( mcastDest = m_pMulticastDestList->GetNext ( iterator ) ) != 0UL )
		{
			COLOUR_AT ( FG_WHITE, BG_BLACK, PrintStartLine + iterator - 1, 41, "Mcast #" + String ( iterator ) + ": " );
			ClearPartofLine ( PrintStartLine + iterator - 1, 61, 15 );
			COLOUR_AT ( FG_CYAN, BG_BLACK, PrintStartLine + iterator - 1, 61, ToIPString ( mcastDest ) );
		}
	}

	COLOUR_AT ( FG_WHITE, BG_BLACK, PrintStartLine + 1, 0, F ( "My Hostname: " ) );
	COLOUR_AT ( FG_CYAN, BG_BLACK, PrintStartLine + 1, 23, GetHostName () );

	COLOUR_AT ( FG_WHITE, BG_BLACK, PrintStartLine + 2, 0, F ( "IP Address: " ) );
	COLOUR_AT ( FG_CYAN, BG_BLACK, PrintStartLine + 2, 23, ToIPString ( WiFi.localIP () ) );

	COLOUR_AT ( FG_WHITE, BG_BLACK, PrintStartLine + 3, 0, "Subnet Mask: " );
	COLOUR_AT ( FG_CYAN, BG_BLACK, PrintStartLine + 3, 23, ToIPString ( WiFi.subnetMask () ) );

	COLOUR_AT ( FG_WHITE, BG_BLACK, PrintStartLine + 4, 0, "Local Multicast Addr: " );
	COLOUR_AT ( FG_CYAN, BG_BLACK, PrintStartLine + 4, 23, ToIPString ( GetMulticastAddress () ) );

	COLOUR_AT ( FG_WHITE, BG_BLACK, PrintStartLine + 4, 41, "WiFi connect/fail: " );
	ClearPartofLine ( PrintStartLine + 4, 61, 10 );
	COLOUR_AT ( FG_CYAN, BG_BLACK, PrintStartLine + 4, 61, String ( m_beginConnects ) + "/" + String ( m_beginTimeouts ) );

	COLOUR_AT ( FG_WHITE, BG_BLACK, PrintStartLine + 5, 41, "Multicasts sent: " );
	COLOUR_AT ( FG_CYAN, BG_BLACK, PrintStartLine + 5, 61, String ( m_ulMCastSentCount ) );

	COLOUR_AT ( FG_WHITE, BG_BLACK, PrintStartLine + 6, 41, "Requests recvd: " );
	COLOUR_AT ( FG_CYAN, BG_BLACK, PrintStartLine + 6, 61, String ( m_ulReqCount ) );

	COLOUR_AT ( FG_WHITE, BG_BLACK, PrintStartLine + 7, 41, "Replies sent: " );
	COLOUR_AT ( FG_CYAN, BG_BLACK, PrintStartLine + 7, 61, String ( m_ulReplyCount ) );

	COLOUR_AT ( FG_WHITE, BG_BLACK, PrintStartLine + 5, 0, F ( "Mac address: " ) );
	byte bMac [ 6 ];
	WiFi.macAddress ( bMac );
	char s [ 18 ];
	sprintf ( s, "%02X:%02X:%02X:%02X:%02X:%02X", bMac [ 5 ], bMac [ 4 ], bMac [ 3 ], bMac [ 2 ], bMac [ 1 ], bMac [ 0 ] );
	COLOUR_AT ( FG_CYAN, BG_BLACK, PrintStartLine + 5, 23, s );

	COLOUR_AT ( FG_WHITE, BG_BLACK, PrintStartLine + 6, 0, F ( "Gateway Address: " ) );
	COLOUR_AT ( FG_CYAN, BG_BLACK, PrintStartLine + 6, 23, ToIPString ( WiFi.gatewayIP () ) );
	// print the received signal strength:

	COLOUR_AT ( FG_WHITE, BG_BLACK, PrintStartLine + 7, 0, F ( "Signal strength (RSSI):" ) );
	COLOUR_AT ( FG_CYAN, BG_BLACK, PrintStartLine + 7, 23, String ( WiFi.RSSI () ) );
	COLOUR_AT ( FG_WHITE, BG_BLACK, PrintStartLine + 7, 30, F ( " dBm" ) );

	COLOUR_AT ( FG_WHITE, BG_BLACK, PrintStartLine + 8, 0, F ( "WiFi Status: " ) );
	ClearPartofLine ( PrintStartLine + 8, 23, 15 );
	COLOUR_AT ( FG_CYAN, BG_BLACK, PrintStartLine + 8, 23, WiFiStatusToString ( WiFi.status () ) );

	COLOUR_AT ( FG_WHITE, BG_BLACK, PrintStartLine + 8, 41, F ( "WiFi Service State: " ) );
	COLOUR_AT ( FG_CYAN, BG_BLACK, PrintStartLine + 8, 61, String ( GetState () ) );
#endif
}

/// Appends local time to provided String
void UDPWiFiService::GetLocalTime ( String & result, time_t timeError )
{
	if ( timeError == 0 )
	{
		timeError = (time_t)GetTime ();
	}
	tm	*localtm = localtime ( &timeError );
	char sTime [ 20 ];
	sprintf ( sTime, "%02d/%02d/%02d %02d:%02d:%02d", localtm->tm_mday, localtm->tm_mon + 1, ( localtm->tm_year - 100 ), localtm->tm_hour, localtm->tm_min, localtm->tm_sec );
	result += sTime;
}

bool UDPWiFiService::GetUDPMessage ( String *pRecvMessage )
{
	bool bResult = false;
	bResult		 = CALL_MEMBER_FN_BY_PTR ( this, StateTableFn [ GetState () ][ UDPWiFiService::WiFiEvent::GETREQUEST ] ) ( (void *)pRecvMessage );
	return bResult;
}

bool UDPWiFiService::ReadUDPMessage ( String &sRecvMessage )
{
	bool		 bResult = false;
	char		 sBuffer [ MAX_UDP_RECV_LEN ];

	// if there's data available, read a packet
	unsigned int packetSize = m_myUDP.parsePacket ();
	if ( packetSize > 0 )
	{
		SetLED ( PROCESSING_MSG_COLOUR );
		delay ( 500 );
		String s = "Received packet of size " + String ( packetSize ) + " From " + ToIPString ( m_myUDP.remoteIP () ) + ", port " + String ( m_myUDP.remotePort () );
		// Error ( s ) ;
		if ( packetSize < sizeof ( sBuffer ) - 1 )
		{
			// read the packet into packetBufffer
			int len			= m_myUDP.read ( sBuffer, sizeof ( sBuffer ) - 1 );
			sBuffer [ len ] = 0;
			sRecvMessage	= sBuffer;
			bResult			= true;
			m_ulReqCount++;
			// create multicast address from send ip and add to list of subnets to send multicasts to and add to list
			IPAddress result;
			CalcMulticastAddress ( m_myUDP.remoteIP (), result );
			m_pMulticastDestList->Add ( result );
		}
		else
		{
			m_ulBadRequests++;
		}
	}
	return bResult;
}

bool UDPWiFiService::SendAll ( String sMsg )
{
	bool bResult = false;
	bResult		 = CALL_MEMBER_FN_BY_PTR ( this, StateTableFn [ GetState () ][ UDPWiFiService::WiFiEvent::SENDMCAST ] ) ( (void *)&sMsg );
	return bResult;
}

bool UDPWiFiService::SendReply ( String sMsg )
{
	bool bResult = false;
	bResult		 = CALL_MEMBER_FN_BY_PTR ( this, StateTableFn [ GetState () ][ UDPWiFiService::WiFiEvent::SENDREPLY ] ) ( (void *)&sMsg );
	return bResult;
}

bool UDPWiFiService::Start ()
{
	bool bResult = false;

	if ( m_myUDP.begin ( m_Port ) == 1 )
	{
		bResult				  = true;
		// Error ( "Started UDP" );
		IPAddress localSubnet = GetMulticastAddress ();
		if ( (long unsigned int)localSubnet != 0UL )
		{
			m_pMulticastDestList->Add ( localSubnet );
		}
	}
	else
	{
		Error ( "Unable to allocate UDP Port" );
		Stop ();
	}
	return bResult;
}

bool UDPWiFiService::NowConnected ( void * )
{
	bool bResult = false;
	if ( Start () )
	{
		SetState ( WiFiService::Status::CONNECTED );
	}
	return bResult;
}

bool UDPWiFiService::DoNowt ( void * )
{
	return true;
}

bool UDPWiFiService::Connect ( void *paramPtr )
{
	bool bResult = false;
	if ( WiFiConnect () )
	{
		// setup udp
		bResult = CALL_MEMBER_FN_BY_PTR ( this, StateTableFn [ GetState () ][ UDPWiFiService::WiFiEvent::MADE_CONNECTION ] ) ( paramPtr );
	}
	return bResult;
}

bool UDPWiFiService::SendReply ( void *paramPtr )
{
	bool	bResult = false;
	String *pMsg	= (String *)( paramPtr );

	if ( m_myUDP.beginPacket ( m_myUDP.remoteIP (), m_myUDP.remotePort () ) == 1 )
	{
		m_myUDP.write ( pMsg->c_str () );
		if ( m_myUDP.endPacket () == 0 )
		{
			Error ( "Message Response failed" );
			Stop ();
		}
		else
		{
			m_ulReplyCount++;
			// Error ( "Sent " + String ( pMsg->substring(0, pMsg->length()-1) ) + " to " + ToIPString ( m_myUDP.remoteIP () ) + ":" + m_myUDP.remotePort() );
			SetState ( WiFiService::Status::CONNECTED );
		}
	}

	return bResult;
}

bool UDPWiFiService::SendMCast ( void *paramPtr )
{
	bool	bResult = false;
	String *pMsg	= (String *)paramPtr;
	if ( pMsg->length () > 0 )
	{
		uint8_t	  iterator = m_pMulticastDestList->GetIterator ();
		IPAddress nextIP;
		while ( (long unsigned int)( nextIP = m_pMulticastDestList->GetNext ( iterator ) ) != 0UL )
		{
			delay ( 200 );
			if ( m_myUDP.beginPacket ( nextIP, 0xCE5C ) == 1 )
			{
				m_myUDP.write ( pMsg->c_str () );
				if ( m_myUDP.endPacket () == 0 )
				{
					Error ( "Multicast Message failed" );
					Stop ();
				}
				else
				{
					SetLED ( PROCESSING_MSG_COLOUR );
					delay ( 500 );
					SetState ( WiFiService::Status::CONNECTED );
					bResult = true;
					m_ulMCastSentCount++;
				}
			}
		}
	}
	else
	{
		Error ( "Error: Empty message to be sent" );
	}
	return bResult;
}

bool UDPWiFiService::GetReq ( void *paramPtr )
{
	if ( WiFi.status () == WL_CONNECTED )
	{
		return ReadUDPMessage ( *( (String *)paramPtr ) );
	}
	else
	{
		SetState ( WiFiService::Status::UNCONNECTED );
		return false;
	}
}

void UDPWiFiService::Stop ()
{
	Error ( "Stopping WiFI" );
	m_myUDP.stop ();
	WiFiService::Stop ();
}

void UDPWiFiService::ProcessUDPMessage ( const String &sRecvMessage )
{
	if ( sRecvMessage.startsWith ( cMsgVersion1 ) )
	{
		// Version 1 message received
		if ( sRecvMessage.substring ( sizeof ( cMsgVersion1 ) + sizeof ( PartSeparator ) - 2 ).startsWith ( TempHumidityReqMsg ) )
		{
			// Got a data request
			// Error ( "Temp Data request" );
			m_MsgHandlerCallback ( UDPWiFiService::ReqMsgType::TEMPDATA );
		}
		else if ( sRecvMessage.substring ( sizeof ( cMsgVersion1 ) + sizeof ( PartSeparator ) - 2 ).startsWith ( RestartReqMsg ) )
		{
			// Got a reset request
			ResetBoard ( F ( "Reset request" ) );
		}
		else if ( sRecvMessage.substring ( sizeof ( cMsgVersion1 ) + sizeof ( PartSeparator ) - 2 ).startsWith ( DoorStatusReqMsg ) )
		{
			// Got a door status request
			// Error ( "Door Data request" );
			m_MsgHandlerCallback ( UDPWiFiService::ReqMsgType::DOORDATA );
		}
		else if ( sRecvMessage.substring ( sizeof ( cMsgVersion1 ) + sizeof ( PartSeparator ) - 2 ).startsWith ( DoorOpenReqMsg ) )
		{
			// Error ( "Door Open request" );
			m_MsgHandlerCallback ( UDPWiFiService::ReqMsgType::DOOROPEN );
		}
		else if ( sRecvMessage.substring ( sizeof ( cMsgVersion1 ) + sizeof ( PartSeparator ) - 2 ).startsWith ( DoorCloseReqMsg ) )
		{
			// Error ( "Door Close request" );
			m_MsgHandlerCallback ( UDPWiFiService::ReqMsgType::DOORCLOSE );
		}
		else if ( sRecvMessage.substring ( sizeof ( cMsgVersion1 ) + sizeof ( PartSeparator ) - 2 ).startsWith ( DoorStopReqMsg ) )
		{
			// Error ( "Door Stop request" );
			m_MsgHandlerCallback ( UDPWiFiService::ReqMsgType::DOORSTOP );
		}
		else if ( sRecvMessage.substring ( sizeof ( cMsgVersion1 ) + sizeof ( PartSeparator ) - 2 ).startsWith ( DoorLightOnReqMsg ) )
		{
			// Error ( "Light On request" );
			m_MsgHandlerCallback ( UDPWiFiService::ReqMsgType::LIGHTON );
		}
		else if ( sRecvMessage.substring ( sizeof ( cMsgVersion1 ) + sizeof ( PartSeparator ) - 2 ).startsWith ( DoorLightOffReqMsg ) )
		{
			// Error ( "Light Off request" );
			m_MsgHandlerCallback ( UDPWiFiService::ReqMsgType::LIGHTOFF );
		}
		else
		{
			m_ulBadRequests++;
			Error ( "Unknown request : " + sRecvMessage.substring ( sizeof ( cMsgVersion1 ) + sizeof ( PartSeparator ) - 1 ) );
		}
	}
	else
	{
		m_ulBadMgsVersion++;
		Error ( "Unknown message version : " + sRecvMessage );
	}
}

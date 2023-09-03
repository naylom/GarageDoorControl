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

const char		  *WiFiStatus []		   = { "WL_IDLE_STATUS",																																											  // = 0,
											   "WL_NO_SSID_AVAIL", "WL_SCAN_COMPLETED", "WL_CONNECTED", "WL_CONNECT_FAILED", "WL_CONNECTION_LOST", "WL_DISCONNECTED", "WL_AP_LISTENING", "WL_AP_CONNECTED", "WL_AP_FAILED", "WL_NO_MODULE" }; // = 255
/*
	UDP config
*/
constexpr auto	   WIFI_FLASHTIME		   = 10; // every 1/2 second

// Valid message parts
constexpr char	   cMsgVersion1 []		   = "V001"; // message version
constexpr char	   TempHumidityReqMsg []   = "M001"; // Req temp / humidity
constexpr char	   RestartReqMsg []		   = "M002"; // Req restart
constexpr char	   DoorStatusReqMsg []	   = "M003"; // Req Door status
constexpr char	   DoorOpenReqMsg []	   = "M004"; // Req Door Open
constexpr char	   DoorCloseReqMsg []	   = "M005"; // Req Door Close
constexpr char	   DoorStopReqMsg []	   = "M006"; // Req Door Stop
constexpr char	   DoorLightOnReqMsg []	   = "M007"; // Req Light On
constexpr char	   DoorLightOffReqMsg []   = "M008"; // Req Light off
constexpr char	   PartSeparator []		   = ":";

constexpr auto	   MAX_INCOMING_UDP_MSG	   = 255;
constexpr auto	   WIFI_CONNECT_TIMEOUT_MS = 10000;

constexpr uint16_t MulticastSendPort	   = 0xCE5C;

enum eResponseMessage { TEMPDATA, DOORDATA };

#define CALL_MEMBER_FN_BY_PTR( object, ptrToMember ) ( ( object )->*( ptrToMember ) )
extern void Error ( String s, bool bInISR = false );
extern void Info ( String s, bool bInISR = false );

void		TerminateProgram ( const __FlashStringHelper *pErrMsg )
{
	Error ( pErrMsg );
	while ( true )
		;
}

WiFiService::WiFiService ()
{
	setenv ( "TZ", "GMTGMT-1,M3.4.0/01,M10.4.0/02", 1 );
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

uint32_t WiFiService::GetBeginTimeOutCount ()
{
	return m_beginTimeouts;
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

void WiFiService::CalcMyMulticastAddress ( IPAddress &result )
{
	CalcMulticastAddress ( WiFi.localIP (), result );
}

void WiFiService::CalcMulticastAddress ( IPAddress ip, IPAddress &subnetMask )
{
	subnetMask		   = IPAddress ( 0UL ); // WiFi.subnetMask();
	uint8_t firstOctet = ( ip & 0xff );
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

IPAddress WiFiService::GetMulticastAddress ()
{
	return m_multicastAddr;
}

bool WiFiService::WiFiConnect ()
{
	bool			bResult		= true;
	static uint32_t iStartCount = 0UL;

	if ( !IsConnected () )
	{
		Info ( "Starting WiFi, attempt " + String ( iStartCount ) );
		uint8_t	 status;
		uint32_t ulStart = millis ();

		WiFi.begin ( m_SSID, m_Pwd );
		String msg = "connecting ";
		do
		{
			status = WiFi.status ();
			delay ( 500 );
			Info ( msg );
			msg += ".";
		}
		while ( status != WL_CONNECTED && ( millis () - ulStart ) < WIFI_CONNECT_TIMEOUT_MS );

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
			Info ( "Connected to " + String ( m_SSID ) );
			SetState ( WiFiService::Status::CONNECTED );
			iStartCount = 0UL;
			m_beginConnects++;
		}
	}

	return bResult;
}

void WiFiService::WiFiDisconnect ()
{
	WiFi.disconnect ();
	Info ( "Disconnecting wifi" );
	SetState ( WiFiService::Status::UNCONNECTED );
}

String WiFiService::ToIPString ( const IPAddress &address )
{
	return String ( address [ 0 ] ) + "." + address [ 1 ] + "." + address [ 2 ] + "." + address [ 3 ];
}

bool WiFiService::IsConnected ()
{
	return WiFi.status () == WL_CONNECTED;
}

uint32_t WiFiService::GetBeginCount ()
{
	return m_beginConnects;
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
		Start ();
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
	if ( GetUDPMessage ( Msg ) )
	{
		ProcessUDPMessage ( Msg );
	}
}

/// Appends local time to provided String
void UDPWiFiService::GetLocalTime ( String &result, time_t timeError )
{
	if ( timeError == 0 )
	{
		timeError = (time_t)GetTime ();
	}
	if ( timeError != 0 )
	{
		tm	*localtm = localtime ( &timeError );
		char sTime [ 20 ];
		sprintf ( sTime, "%02d/%02d/%02d %02d:%02d:%02d", localtm->tm_mday, localtm->tm_mon + 1, ( localtm->tm_year - 100 ), localtm->tm_hour, localtm->tm_min, localtm->tm_sec );
		result += sTime;
	}
}

bool UDPWiFiService::GetUDPMessage ( String &RecvMessage )
{
	if ( WiFiConnect () )
	{
		m_pMulticastDestList->Add ( GetMulticastAddress () );
		return ReadUDPMessage ( RecvMessage );
	}
	else
	{
		SetState ( WiFiService::Status::UNCONNECTED );
		return false;
	}
}

bool UDPWiFiService::ReadUDPMessage ( String &sRecvMessage )
{
	bool		 bResult = false;
	char		 sBuffer [ MAX_INCOMING_UDP_MSG ];

	// if there's data available, read a packet
	unsigned int packetSize = m_myUDP.parsePacket ();
	if ( packetSize > 0 )
	{
		SetLED ( PROCESSING_MSG_COLOUR );
		delay ( 500 );
		String s = "Received packet of size " + String ( packetSize ) + " From " + ToIPString ( m_myUDP.remoteIP () ) + ", port " + String ( m_myUDP.remotePort () );
		// Info ( s ) ;
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
		Error ( "Unable to allocate UDP Port, restarting" );
		delay ( 1000 * 20 );
		MN::Utils::ResetBoard ( F ( "" ) );
	}
	return bResult;
}

uint32_t UDPWiFiService::GetMCastSentCount ()
{
	return m_ulMCastSentCount;
}

uint32_t UDPWiFiService::GetRequestsReceivedCount ()
{
	return m_ulReqCount;
}

bool UDPWiFiService::SendReply ( String sMsg )
{
	bool bResult = false;
	if ( WiFiConnect () )
	{
		if ( sMsg.length () > 0 )
		{
			if ( m_myUDP.beginPacket ( m_myUDP.remoteIP (), m_myUDP.remotePort () ) == 1 )
			{
				m_myUDP.write ( sMsg.c_str () );
				if ( m_myUDP.endPacket () == 0 )
				{
					Error ( "Message Response failed" );
					WiFiDisconnect ();
				}
				else
				{
					m_ulReplyCount++;
					// Error ( "Sent " + String ( pMsg->substring(0, pMsg->length()-1) ) + " to " + ToIPString ( m_myUDP.remoteIP () ) + ":" + m_myUDP.remotePort() );
					SetState ( WiFiService::Status::CONNECTED );
					bResult = true;
				}
			}
			else
			{
				Error ( "Unable to send UDP message, beginpacket() failed sending to : " + ToIPString ( m_myUDP.remoteIP () ) + " : " + m_myUDP.remotePort () );
			}
		}
		else
		{
			Error ( "Empty reply to be sent" );
		}
	}
	return bResult;
}

/// @brief Checks WiFi connected and attempts to connect if not. If connected then will look for a message and store in parameter
/// @param paramPtr pointer to String to receive any available message
/// @return
bool UDPWiFiService::SendAll ( String sMsg )
{
	bool bResult = false;
	if ( WiFiConnect () )
	{
		if ( sMsg.length () > 0 )
		{
			uint8_t	  iterator = m_pMulticastDestList->GetIterator ();
			IPAddress nextIP;
			while ( (long unsigned int)( nextIP = m_pMulticastDestList->GetNext ( iterator ) ) != 0UL )
			{
				delay ( 200 );
				if ( m_myUDP.beginPacket ( nextIP, MulticastSendPort ) == 1 )
				{
					m_myUDP.write ( sMsg.c_str () );
					if ( m_myUDP.endPacket () == 0 )
					{
						Error ( "Multicast Message failed" );
						WiFiDisconnect ();
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
	}
	return bResult;
}

uint32_t UDPWiFiService::GetReplySentCount ()
{
	return m_ulReplyCount;
}

FixedIPList *UDPWiFiService::GetMulticastList ()
{
	return m_pMulticastDestList;
}

/// @brief Releases UDP port and disconnects from WiFi
void UDPWiFiService::Stop ()
{
	Info ( "Stopping WiFI" );
	m_myUDP.stop ();
	WiFiDisconnect ();
}

/// @brief Processes the UDP message that has been received
/// @param sRecvMessage String containing the messade received
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
			MN::Utils::ResetBoard ( F ( "Reset request" ) );
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
			// Info ( "Light Off request" );
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

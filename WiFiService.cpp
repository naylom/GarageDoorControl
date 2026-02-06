/*

WiFiService

Arduino class to encapsulate UDP data service over WiFi

Author: (c) M. Naylor 2022

History:
    Ver 1.0			Initial version
    Ver 2.0			Added onboarding support with BlobStorage
*/
#include "WiFiService.h"

#include "ConfigStorage.h"
#include "DoorState.h"

#include <time.h>
#include <WiFiNINA.h>

const char* WiFiStatus [] = { "WL_IDLE_STATUS",  // = 0,
                              "WL_NO_SSID_AVAIL",
                              "WL_SCAN_COMPLETED",
                              "WL_CONNECTED",
                              "WL_CONNECT_FAILED",
                              "WL_CONNECTION_LOST",
                              "WL_DISCONNECTED",
                              "WL_AP_LISTENING",
                              "WL_AP_CONNECTED",
                              "WL_AP_FAILED",
                              "WL_NO_MODULE" };  // = 255
/*
    UDP config
*/
constexpr auto WIFI_FLASHTIME = 10;  // every 1/2 second

// Valid message parts
constexpr char cMsgVersion1 [] 			= "V001";       // message version
constexpr char TempHumidityReqMsg [] 	= "M001";  		// Req temp / humidity
constexpr char RestartReqMsg [] 		= "M002";       // Req restart
constexpr char DoorStatusReqMsg [] 		= "M003";    	// Req Door status
constexpr char DoorOpenReqMsg [] 		= "M004";      	// Req Door Open
constexpr char DoorCloseReqMsg [] 		= "M005";     	// Req Door Close
constexpr char DoorStopReqMsg [] 		= "M006";      	// Req Door Stop
constexpr char DoorLightOnReqMsg [] 	= "M007";   	// Req Light On
constexpr char DoorLightOffReqMsg [] 	= "M008";   	// Req Light off
constexpr char PartSeparator [] = ":";

constexpr auto MAX_INCOMING_UDP_MSG = 255;
constexpr auto WIFI_CONNECT_TIMEOUT_MS = 10000;

enum class eResponseMessage : uint8_t
{
	TEMPDATA,
	DOORDATA
};

#define CALL_MEMBER_FN_BY_PTR( object, ptrToMember ) ( ( object )->*( ptrToMember ) )
extern void Error ( String s, bool bInISR = false );
extern void Info ( String s, bool bInISR = false );

// Helper function to log WiFi/UDP errors with context
static void logWiFiError ( const String& context, int errorCode )
{
	Error ( context + " failed with code: " + String ( errorCode ) );
}

// Helper function to convert IPAddress to String
static String ipToString ( const IPAddress& address )
{
	return String ( address [ 0 ] ) + "." + String ( address [ 1 ] ) + "." + String ( address [ 2 ] ) + "." +
	       String ( address [ 3 ] );
}

void TerminateProgram ( const __FlashStringHelper* pErrMsg )
{
	Error ( pErrMsg );
	while ( true )
		;
}

WiFiService::WiFiService ()
{
	// Set the timezone to GMT with daylight saving time adjustments
	setenv ( "TZ", "GMTGMT-1,M3.4.0/01,M10.4.0/02", 1 );

	// Initialize configuration storage
	if ( !ConfigStorage::begin() )
	{
		Error ( F ( "Failed to initialize configuration storage" ) );
	}
	memset ( &m_config, 0, sizeof ( m_config ) );
}
/// @brief Destructor to clean up the WiFiService
WiFiService::~WiFiService ()
{
	WiFiDisconnect();
	if ( m_pOnboarding != nullptr )
	{
		delete m_pOnboarding;
		m_pOnboarding = nullptr;
	}
}


const char* WiFiService::WiFiStatusToString ( uint8_t iState ) const
{
	static constexpr size_t statusCount = sizeof ( WiFiStatus ) / sizeof ( WiFiStatus [ 0 ] );

	return ( iState < statusCount ) ? WiFiStatus [ iState ] : "UNKNOWN";
}

uint32_t WiFiService::GetBeginTimeOutCount () const
{
	return m_beginTimeouts;
}

const char* WiFiService::GetHostName () const
{
	return m_HostName;
}

unsigned long WiFiService::GetTime () const
{
	return WiFi.getTime();
}

float WiFiService::GetAltitudeCompensation () const
{
	return m_config.altitudeCompensation;
}

WiFiService::Status WiFiService::GetState () const
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

		case WiFiService::Status::AP_MODE:
			SetLED ( MNRGBLEDBaseLib::eColour::DARK_BLUE, WIFI_FLASHTIME );
			break;
	}
}

/**
 * @brief Initializes the WiFi service with onboarding support.
 * @details Attempts to load stored credentials and connect. If no valid credentials
 *          are found or connection fails, enters AP mode for onboarding.
 *
 * @param apSSID The SSID to use for the onboarding AP.
 * @param apPassword The password to use for the onboarding AP (nullptr for open AP).
 * @param pLED Pointer to an LED object for indicating status.
 */
void WiFiService::Begin ( const char* apSSID, const char* apPassword, MNRGBLEDBaseLib* pLED )
{
	m_apSSID = apSSID;
	m_apPassword = apPassword;
	m_pLED = pLED;
	m_useOnboarding = true;

	WiFi.setHostname ( "GarageControl" );

	String fv = WiFi.firmwareVersion();
	if ( fv < WIFI_FIRMWARE_LATEST_VERSION )
	{
		SetLED ( OLD_WIFI_FIRMWARE_COLOUR );
		Error ( "Please upgrade the firmware. Latest is " + String ( WIFI_FIRMWARE_LATEST_VERSION ) + ", board has " +
		        String ( fv ) );
	}
	else
	{
		// Try to load configuration and connect
		LoadAndConnectFromStorage();
	}
}

/**
 * @brief Initializes the WiFi service with direct credentials (legacy mode).
 * @details Use this for backward compatibility when not using onboarding.
 *
 * @param HostName The hostname to set for the WiFi connection.
 * @param WiFissid The SSID of the WiFi network to connect to.
 * @param WiFipwd The password of the WiFi network.
 * @param pLED Pointer to an LED object for indicating status.
 */
void WiFiService::BeginWithConfig ( const char* HostName,
                                    const char* WiFissid,
                                    const char* WiFipwd,
                                    MNRGBLEDBaseLib* pLED )
{
	m_SSID = WiFissid;
	m_Pwd = WiFipwd;
	m_HostName = HostName;
	m_pLED = pLED;

	WiFi.setHostname ( m_HostName );

	String fv = WiFi.firmwareVersion();
	if ( fv < WIFI_FIRMWARE_LATEST_VERSION )
	{
		SetLED ( OLD_WIFI_FIRMWARE_COLOUR );
		Error ( "Please upgrade the firmware. Latest is " + String ( WIFI_FIRMWARE_LATEST_VERSION ) + ", board has " +
		        String ( fv ) );
	}
	else
	{
		// Set the initial state to UNCONNECTED as the WiFi connection has not been established yet
		SetState ( Status::UNCONNECTED );
	}
}

/**
 * @brief Loads configuration from storage and attempts to connect.
 * @details Falls back to AP mode if no valid configuration exists.
 */
void WiFiService::LoadAndConnectFromStorage ()
{
	if ( ConfigStorage::load ( m_config ) )
	{
		Info ( "Loaded configuration from storage" );
		Info ( "SSID: " + String ( m_config.ssid ) );
		Info ( "Hostname: " + String ( m_config.hostname ) );

		m_SSID = m_config.ssid;
		m_Pwd = m_config.password;
		m_HostName = m_config.hostname;

		WiFi.setHostname ( m_HostName );

		// Try to connect
		if ( !WiFiConnect() )
		{
			Error ( "Failed to connect with stored credentials" );
			if ( m_useOnboarding )
			{
				Info ( "Entering AP mode for reconfiguration" );
				StartAP();
			}
		}
		else
		{
			Info ( "Successfully connected to WiFi" );
			SetState ( Status::CONNECTED );
		}
	}
	else
	{
		Info ( "No valid configuration found" );
		if ( m_useOnboarding )
		{
			Info ( "Entering AP mode for initial configuration" );
			StartAP();
		}
		else
		{
			SetState ( Status::UNCONNECTED );
		}
	}
}

/**
 * @brief Starts Access Point mode for onboarding.
 * @details Creates an AP with the configured SSID. If password is provided, creates a secured AP,
 *          otherwise creates an open AP.
 */
void WiFiService::StartAP ()
{
	WiFi.disconnect();
	WiFi.end();

	Info ( "Starting AP mode: " + String ( m_apSSID ) );

	// If password is null or empty, create an open AP
	if ( m_apPassword == nullptr || strlen ( m_apPassword ) == 0 )
	{
		Info ( "Creating open AP (no password)" );
		WiFi.beginAP ( m_apSSID );
	}
	else
	{
		Info ( "Creating secured AP" );
		WiFi.beginAP ( m_apSSID, m_apPassword );
	}

	// Wait for AP to be started
	while ( WiFi.localIP() == IPAddress ( 0, 0, 0, 0 ) )
	{
		delay ( 100 );
	}

	Info ( "AP started. IP: " + ToIPString ( WiFi.localIP() ) );

	// Create and start onboarding server
	if ( m_pOnboarding == nullptr )
	{
		m_pOnboarding = new OnboardingServer();
	}
	m_pOnboarding->begin();

	SetState ( Status::AP_MODE );
}

void WiFiService::CalcMyMulticastAddress ( IPAddress& result ) const
{
	CalcMulticastAddress ( WiFi.localIP(), result );
}

void WiFiService::CalcMulticastAddress ( IPAddress ip, IPAddress& subnetMask ) const
{
	subnetMask = IPAddress ( 0UL );  // WiFi.subnetMask();
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

IPAddress WiFiService::GetMulticastAddress () const
{
	return m_multicastAddr;
}

bool WiFiService::WiFiConnect ()
{
	bool bResult = true;
	static uint32_t iStartCount = 0UL;

	if ( !IsConnected() )
	{
		Info ( "Starting WiFi, attempt " + String ( iStartCount ) );
		uint8_t status;
		uint32_t ulStart = millis();

		WiFi.begin ( m_SSID, m_Pwd );
		String msg = "connecting ";
		do
		{
			status = WiFi.status();
			delay ( 500 );
			Info ( msg );
			msg += ".";
		} while ( status != WL_CONNECTED && ( millis() - ulStart ) < WIFI_CONNECT_TIMEOUT_MS );

		if ( status != WL_CONNECTED )
		{
			bResult = false;
			SetState ( WiFiService::Status::UNCONNECTED );
			logWiFiError ( "Connect", status );
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
	WiFi.disconnect();
	Info ( "Disconnecting wifi" );
	SetState ( WiFiService::Status::UNCONNECTED );
}

String WiFiService::ToIPString ( const IPAddress& address )
{
	return ipToString ( address );
}

bool WiFiService::IsConnected () const
{
	return WiFi.status() == WL_CONNECTED;
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

/**
 * @brief Initialize UDP WiFi service with onboarding support.
 * @param pHandleReqData Callback function to handle received messages.
 * @param apSSID SSID for AP mode (onboarding).
 * @param apPassword Password for AP mode.
 * @param pLED Optional LED for status indication.
 * @return True if initialization succeeded.
 */
bool UDPWiFiService::Begin ( UDPWiFiServiceCallback pHandleReqData,
                             const char* apSSID,
                             const char* apPassword,
                             MNRGBLEDBaseLib* pLED )
{
	bool bResult = false;

	WiFiService::Begin ( apSSID, apPassword, pLED );
	m_MsgHandlerCallback = pHandleReqData;

	if ( m_sUDPReceivedMsg.reserve ( MAX_INCOMING_UDP_MSG ) )
	{
		// Check if we have valid configuration loaded
		if ( m_config.valid && GetState() == Status::CONNECTED )
		{
			m_Port = m_config.udpPort;
			Start();
			bResult = true;
		}
		else if ( GetState() == Status::AP_MODE )
		{
			// In AP mode, onboarding will handle configuration
			Info ( "In AP mode - waiting for configuration" );
			bResult = true;  // Consider initialization successful even in AP mode
		}
	}
	return bResult;
}

/**
 * @brief Process onboarding server when in AP mode.
 * @details Call this in the main loop to handle onboarding requests.
 */
void UDPWiFiService::ProcessOnboarding ()
{
	if ( GetState() == Status::AP_MODE && m_pOnboarding != nullptr )
	{
		m_pOnboarding->loop();
	}
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
void UDPWiFiService::GetLocalTime ( String& result, time_t timeError )
{
	if ( timeError == 0 )
	{
		timeError = GetTime();
	}
	if ( timeError != 0 )
	{
		tm localtm;
		localtime_r ( &timeError, &localtm );
		char sTime [ 20 ];
		// Format: DD/MM/YY HH:MM:SS
		sprintf ( sTime,
		          "%02d/%02d/%02d %02d:%02d:%02d",
		          localtm.tm_mday,
		          localtm.tm_mon + 1,
		          ( localtm.tm_year - 100 ),
		          localtm.tm_hour,
		          localtm.tm_min,
		          localtm.tm_sec );
		result += sTime;
	}
}

bool UDPWiFiService::GetUDPMessage ( String& RecvMessage )
{
	if ( WiFiConnect() )
	{
		m_pMulticastDestList->Add ( GetMulticastAddress() );
		return ReadUDPMessage ( RecvMessage );
	}
	else
	{
		SetState ( WiFiService::Status::UNCONNECTED );
		return false;
	}
}

bool UDPWiFiService::ReadUDPMessage ( String& sRecvMessage )
{
	bool bResult = false;
	char sBuffer [ MAX_INCOMING_UDP_MSG ];

	// if there's data available, read a packet
	unsigned int packetSize = m_myUDP.parsePacket();
	if ( packetSize > 0 )
	{
		SetLED ( PROCESSING_MSG_COLOUR );
		delay ( 500 );
		String logMessage = "Received packet of size " + String ( packetSize ) + " From " +
		                    ToIPString ( m_myUDP.remoteIP() ) + ", port " + String ( m_myUDP.remotePort() );
		// Info ( logMessage ) ;
		if ( packetSize < sizeof ( sBuffer ) - 1 )
		{
			// read the packet into packetBufffer
			int len = m_myUDP.read ( sBuffer, sizeof ( sBuffer ) - 1 );
			if ( len >= 0 )
			{
				sBuffer [ len ] = 0;
				sRecvMessage = sBuffer;
				bResult = true;
				m_ulReqCount++;
			}
			else
			{
				Error ( "Failed to read UDP packet" );
			}
			// create multicast address from send ip and add to list of subnets to send multicasts to and add to list
			IPAddress result;
			CalcMulticastAddress ( m_myUDP.remoteIP(), result );
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
		bResult = true;
		// Error ( "Started UDP" );
		IPAddress localSubnet = GetMulticastAddress();
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
	if ( WiFiConnect() )
	{
		if ( sMsg.length() > 0 )
		{
			int beginResult = m_myUDP.beginPacket ( m_myUDP.remoteIP(), m_myUDP.remotePort() );
			if ( beginResult == 1 )
			{
				m_myUDP.write ( sMsg.c_str() );
				if ( m_myUDP.endPacket() == 0 )
				{
					logWiFiError ( "Message Response", 0 );
					WiFiDisconnect();
				}
				else
				{
					m_ulReplyCount++;
					SetState ( WiFiService::Status::CONNECTED );
					bResult = true;
				}
			}
			else
			{
				logWiFiError ( "Unable to send UDP message, beginPacket() to: " + ToIPString ( m_myUDP.remoteIP() ) +
				                   " : " + m_myUDP.remotePort(),
				               beginResult );
			}
		}
		else
		{
			Error ( "Empty reply to be sent" );
		}
	}
	return bResult;
}

/// @brief Checks WiFi connected and attempts to connect if not. If connected then will look for a message and store in
/// parameter
/// @param paramPtr pointer to String to receive any available message
/// @return
bool UDPWiFiService::SendAll ( String sMsg )
{
	bool bResult = false;
	if ( WiFiConnect() )
	{
		if ( sMsg.length() > 0 )
		{
			uint8_t iterator = m_pMulticastDestList->GetIterator();
			IPAddress nextIP;
			while ( (long unsigned int)( nextIP = m_pMulticastDestList->GetNext ( iterator ) ) != 0UL )
			{
				delay ( 200 );
				if ( m_myUDP.beginPacket ( nextIP, m_config.multicastPort ) == 1 )
				{
					m_myUDP.write ( sMsg.c_str() );
					if ( m_myUDP.endPacket() == 0 )
					{
						Error ( "Multicast Message failed" );
						WiFiDisconnect();
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

FixedIPList* UDPWiFiService::GetMulticastList ()
{
	return m_pMulticastDestList;
}

/// @brief Releases UDP port and disconnects from WiFi
void UDPWiFiService::Stop ()
{
	Info ( "Stopping WiFI" );
	m_myUDP.stop();
	WiFiDisconnect();
}

/// @brief Processes the UDP message that has been received
/// @param sRecvMessage String containing the messade received
void UDPWiFiService::ProcessUDPMessage ( const String& sRecvMessage )
{
	if ( sRecvMessage.startsWith ( cMsgVersion1 ) )
	{
		// Version 1 message received
		if ( sRecvMessage.substring ( sizeof ( cMsgVersion1 ) + sizeof ( PartSeparator ) - 2 )
		         .startsWith ( TempHumidityReqMsg ) )
		{
			// Got a data request
			// Error ( "Temp Data request" );
			m_MsgHandlerCallback ( UDPWiFiService::ReqMsgType::TEMPDATA );
		}
		else if ( sRecvMessage.substring ( sizeof ( cMsgVersion1 ) + sizeof ( PartSeparator ) - 2 )
		              .startsWith ( RestartReqMsg ) )
		{
			// Got a reset request
			MN::Utils::ResetBoard ( F ( "Reset request" ) );
		}
		else if ( sRecvMessage.substring ( sizeof ( cMsgVersion1 ) + sizeof ( PartSeparator ) - 2 )
		              .startsWith ( DoorStatusReqMsg ) )
		{
			// Got a door status request
			// Error ( "Door Data request" );
			m_MsgHandlerCallback ( UDPWiFiService::ReqMsgType::DOORDATA );
		}
		else if ( sRecvMessage.substring ( sizeof ( cMsgVersion1 ) + sizeof ( PartSeparator ) - 2 )
		              .startsWith ( DoorOpenReqMsg ) )
		{
			// Error ( "Door Open request" );
			m_MsgHandlerCallback ( UDPWiFiService::ReqMsgType::DOOROPEN );
		}
		else if ( sRecvMessage.substring ( sizeof ( cMsgVersion1 ) + sizeof ( PartSeparator ) - 2 )
		              .startsWith ( DoorCloseReqMsg ) )
		{
			// Error ( "Door Close request" );
			m_MsgHandlerCallback ( UDPWiFiService::ReqMsgType::DOORCLOSE );
		}
		else if ( sRecvMessage.substring ( sizeof ( cMsgVersion1 ) + sizeof ( PartSeparator ) - 2 )
		              .startsWith ( DoorStopReqMsg ) )
		{
			// Error ( "Door Stop request" );
			m_MsgHandlerCallback ( UDPWiFiService::ReqMsgType::DOORSTOP );
		}
		else if ( sRecvMessage.substring ( sizeof ( cMsgVersion1 ) + sizeof ( PartSeparator ) - 2 )
		              .startsWith ( DoorLightOnReqMsg ) )
		{
			// Error ( "Light On request" );
			m_MsgHandlerCallback ( UDPWiFiService::ReqMsgType::LIGHTON );
		}
		else if ( sRecvMessage.substring ( sizeof ( cMsgVersion1 ) + sizeof ( PartSeparator ) - 2 )
		              .startsWith ( DoorLightOffReqMsg ) )
		{
			// Info ( "Light Off request" );
			m_MsgHandlerCallback ( UDPWiFiService::ReqMsgType::LIGHTOFF );
		}
		else
		{
			m_ulBadRequests++;
			Error ( "Unknown request : " +
			        sRecvMessage.substring ( sizeof ( cMsgVersion1 ) + sizeof ( PartSeparator ) - 1 ) );
		}
	}
	else
	{
		m_ulBadMgsVersion++;
		Error ( "Unknown message version : " + sRecvMessage );
	}
}

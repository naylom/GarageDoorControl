#include "api/IPAddress.h"
#pragma once
/*

WiFiService

Arduino class to encapsulate UDP data service over WiFi

Author: (c) M. Naylor 2022

History:
	Ver 1.0			Initial version
*/
#include "Logging.h"
#include "FixedIPList.h"
#include <MNRGBLEDBaseLib.h>

#ifdef ARDUINO_AVR_UNO              // On UNO, I use the Cytron shield-esp-wifi
//#include <CytronWiFiShield.h>
//#include <CytronWiFiServer.h>
//#include <CytronWiFiClient.h>
#else                               // On MKR1010 WIFI with in built wifi use nina libs
#include <WiFiNINA.h>
#include <WiFiUdp.h>
#endif

// Colours used on MKR RGB LED to indicate status
constexpr RGBType OLD_WIFI_FIRMWARE_COLOUR	= MNRGBLEDBaseLib::eColour::DARK_YELLOW ;
constexpr RGBType UNCONNECTED_COLOUR		= MNRGBLEDBaseLib::eColour::DARK_RED;
constexpr RGBType CONNECTED_COLOUR			= MNRGBLEDBaseLib::eColour::DARK_GREEN;
constexpr RGBType PROCESSING_MSG_COLOUR		= MNRGBLEDBaseLib::eColour::DARK_MAGENTA;

void TerminateProgram ( const __FlashStringHelper* pErrMsg );

class WiFiService
{
public:
	enum Status { UNCONNECTED, CONNECTED };
							WiFiService ( );
			void 			Begin ( const char * HostName, const char * WiFissid, const char * WiFipwd, MNRGBLEDBaseLib * pLED = nullptr  );
			const char *	GetHostName();
            IPAddress       GetMulticastAddress();
			Status			GetState();
	static 	String			ToIPString ( const IPAddress& address );
			unsigned long	GetTime();
			
protected:
			bool			Check();
			void 			Stop();
			bool 			WiFiConnect();
			void			SetLED ( RGBType theColour, uint8_t flashTime = 0 );
			void 			SetState ( WiFiService::Status state );
            IPAddress       CalcMyMulticastAddress ();
			IPAddress       CalcMulticastAddress ( IPAddress ip );
			const char *	WiFiStatusToString ( uint8_t iState );
			
			uint32_t			m_beginTimeouts		= 0UL;						// count of times WiFi.begin fails to connect within 10 secs
			uint32_t			m_beginConnects		= 0UL;						// count of times WiFi.begin has connected successfully			
private:

			const char * 		m_SSID				= nullptr;
			const char *		m_Pwd				= nullptr;
			const char *		m_HostName			= nullptr;
			Status				m_State				= Status::UNCONNECTED;
            IPAddress           m_multicastAddr     = 0UL;
			MNRGBLEDBaseLib *	m_pLED 				= nullptr;
};

constexpr auto	MAX_UDP_RECV_LEN	= 255;
constexpr auto	MAX_CONNECT_RETRIES	= 20;
class UDPWiFiService : public WiFiService
{
public:

	enum 	ReqMsgType : uint16_t { TEMPDATA, DOORDATA, DOOROPEN, DOORCLOSE, DOORSTOP, LIGHTON, LIGHTOFF };
	typedef void ( * UDPWiFiServiceCallback) ( UDPWiFiService::ReqMsgType uiParam );

					UDPWiFiService();
			bool 	Begin ( UDPWiFiServiceCallback pHandleReqData, const char * WiFissid, const char * WiFipwd, const char * HostName, const uint16_t portUDP = 0xFEED, MNRGBLEDBaseLib * pLED = nullptr );
			bool 	Begin ( UDPWiFiServiceCallback pHandleReqData, const char * WiFissid, const char * WiFipwd, const char * HostName, MNRGBLEDBaseLib * pLED = nullptr, const uint16_t portUDP = 0xFEED );
			void	CheckUDP();
			void 	DisplayStatus();
            bool    SendAll ( String sMsg );
			bool 	SendReply ( String sMsg );
			bool 	Start();
			void 	Stop ();

private:
	enum WiFiState { DISCONNECTED, ISCONNECTED };
	enum WiFiEvent { MADE_CONNECTION, LOST_CONNECTION, SENDREPLY, GETREQUEST, SENDMCAST };
	typedef bool ( UDPWiFiService::*WiFiStateFunction )( void * paramPtr );              	// prototype of function to handle event

			bool			Connect ( void * paramPtr );
			bool			DoNowt ( void * paramPtr );
			bool			GetReq ( void * paramPtr );
			bool			NowConnected ( void * paramPtr );			
			bool			SendMCast ( void * paramPtr );
			bool			SendReply ( void * paramPtr );


	WiFiStateFunction StateTableFn [ 5 ][ 5 ] =
	{
		{ &UDPWiFiService::NowConnected,  	&UDPWiFiService::DoNowt,		&UDPWiFiService::Connect,		&UDPWiFiService::Connect,		&UDPWiFiService::Connect },		// Actions when current state is UNCONNECTED
		{ &UDPWiFiService::DoNowt,    		&UDPWiFiService::Connect,     	&UDPWiFiService::SendReply, 	&UDPWiFiService::GetReq,  		&UDPWiFiService::SendMCast }	// Actions when current state is CONNECTED
	};
			uint16_t				m_Port				= 0;
			WiFiUDP 				m_myUDP;
			String 					m_sUDPReceivedMsg;
			UDPWiFiServiceCallback	m_MsghHandlerCallback;
			FixedIPList	*			m_pMulticastDestList = nullptr;			
			uint32_t				m_ulBadRequests		= 0UL;
			uint32_t				m_ulBadMgsVersion	= 0UL;
			uint32_t				m_ulReqCount		= 0UL;
			uint32_t				m_ulMCastSentCount  = 0UL;
			uint32_t 				m_ulReplyCount		= 0UL;
			WiFiState				m_WiFiState 		= WiFiState::DISCONNECTED;			

			bool GetUDPMessage ( String* pRecvMessage );
			void ProcessUDPMessage ( const String &sRecvMessage );
			bool ReadUDPMessage ( String& sRecvMessage );
};
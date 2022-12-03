#pragma once
/*

WiFiService

Arduino class to encapsulate UDP data service over WiFi

Author: (c) M. Naylor 2022

History:
	Ver 1.0			Initial version
*/
//#include <limits.h>
#include "Logging.h"
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
const RGBType OLD_WIFI_FIRMWARE_COLOUR	= MNRGBLEDBaseLib::eColour::DARK_YELLOW ;
const RGBType UNCONNECTED_COLOUR		= MNRGBLEDBaseLib::eColour::DARK_RED;
const RGBType CONNECTED_COLOUR			= MNRGBLEDBaseLib::eColour::DARK_GREEN;
const RGBType PROCESSING_MSG_COLOUR		= MNRGBLEDBaseLib::eColour::DARK_MAGENTA;

void TerminateProgram ( const __FlashStringHelper* pErrMsg );

class WiFiService
{
public:
	enum Status { UNCONNECTED, CONNECTED };
							WiFiService ( );
			void 			Begin ( const char * HostName, const char * WiFissid, const char * WiFipwd, MNRGBLEDBaseLib * pLED = nullptr  );
			const char *	GetHostName();
			Status			GetStatus();
	static 	String			ToIPString ( const IPAddress& address );
protected:
			bool			Check();
			void 			Stop();
			bool 			WiFiConnect();
			void			SetLED ( RGBType theColour, uint8_t flashTime = 0 );
			void 			SetState ( WiFiService::Status state );
private:
			const char * 		m_SSID				= nullptr;
			const char *		m_Pwd				= nullptr;
			const char *		m_HostName			= nullptr;
			Status				m_State				= Status::UNCONNECTED;
			MNRGBLEDBaseLib *	m_pLED 				= nullptr;
};

const auto	MAX_UDP_RECV_LEN	= 255;
const auto	MAX_CONNECT_RETRIES	= 20;
class UDPWiFiService : public WiFiService
{
public:

	enum 	ReqMsgType : uint16_t { TEMPDATA, DOORDATA };
	typedef void ( * UDPWiFiServiceCallback) ( UDPWiFiService::ReqMsgType uiParam );

					UDPWiFiService();
			bool 	Begin ( UDPWiFiServiceCallback pHandleReqData, const char * WiFissid, const char * WiFipwd, const char * HostName, const uint16_t portUDP = 0xFEED, MNRGBLEDBaseLib * pLED = nullptr );
			bool 	Begin ( UDPWiFiServiceCallback pHandleReqData, const char * WiFissid, const char * WiFipwd, const char * HostName, MNRGBLEDBaseLib * pLED = nullptr, const uint16_t portUDP = 0xFEED );
			void	CheckUDP();
			void 	DisplayStatus();
			void 	SendReply ( String sMsg );
			bool 	Start();
			void 	Stop ();

private:

	uint16_t				m_Port				= 0;
	WiFiUDP 				m_myUDP;
	String 					m_sUDPReceivedMsg;
	UDPWiFiServiceCallback	m_MsghHandlerCallback;
	uint32_t				m_ulBadRequests		= 0UL;
	uint32_t				m_ulBadMgsVersion	= 0UL;
	uint32_t				m_ulLastClientReq 	= 0UL;

	bool GetUDPMessage ( String& sRecvMessage );
	void ProcessUDPMessage ( const String &sRecvMessage );
};
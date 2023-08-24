#pragma once
/*

WiFiService

Arduino class to encapsulate UDP data service over WiFi

Author: (c) M. Naylor 2022

History:
	Ver 1.0			Initial version
*/
#include <MNRGBLEDBaseLib.h>
#include <WiFiNINA.h>
#include "Logging.h"
#include "FixedIPList.h"

// Colours used on MKR RGB LED to indicate status
constexpr RGBType OLD_WIFI_FIRMWARE_COLOUR = MNRGBLEDBaseLib::eColour::DARK_YELLOW;
constexpr RGBType UNCONNECTED_COLOUR	   = MNRGBLEDBaseLib::eColour::DARK_RED;
constexpr RGBType CONNECTED_COLOUR		   = MNRGBLEDBaseLib::eColour::DARK_GREEN;
constexpr RGBType PROCESSING_MSG_COLOUR	   = MNRGBLEDBaseLib::eColour::DARK_MAGENTA;

void			  TerminateProgram ( const __FlashStringHelper *pErrMsg );

class WiFiService
{
	public:
		enum Status { UNCONNECTED, CONNECTED };

		WiFiService ();
		void		  Begin ( const char *HostName, const char *WiFissid, const char *WiFipwd, MNRGBLEDBaseLib *pLED = nullptr );
		const char	 *GetHostName ();
		IPAddress	  GetMulticastAddress ();
		Status		  GetState ();
		static String ToIPString ( const IPAddress &address );
		unsigned long GetTime ();
		uint32_t	  GetBeginCount ();
		uint32_t	  GetBeginTimeOutCount ();
		bool		  IsConnected ();
		const char	 *WiFiStatusToString ( uint8_t iState );

	protected:
		void	 WiFiDisconnect ();
		bool	 WiFiConnect ();
		void	 SetLED ( RGBType theColour, uint8_t flashTime = 0 );
		void	 SetState ( WiFiService::Status state );
		void	 CalcMyMulticastAddress ( IPAddress &result );
		void	 CalcMulticastAddress ( IPAddress ip, IPAddress &result );

		uint32_t m_beginTimeouts = 0UL; // count of times WiFi.begin fails to connect within 10 secs
		uint32_t m_beginConnects = 0UL; // count of times WiFi.begin has connected successfully
	private:

		const char		*m_SSID			 = nullptr;
		const char		*m_Pwd			 = nullptr;
		const char		*m_HostName		 = nullptr;
		Status			 m_State		 = Status::UNCONNECTED;
		IPAddress		 m_multicastAddr = 0UL;
		MNRGBLEDBaseLib *m_pLED			 = nullptr;
};

class UDPWiFiService : public WiFiService
{
	public:

		enum ReqMsgType : uint16_t { TEMPDATA, DOORDATA, DOOROPEN, DOORCLOSE, DOORSTOP, LIGHTON, LIGHTOFF };

		typedef void ( *UDPWiFiServiceCallback ) ( UDPWiFiService::ReqMsgType uiParam );

		UDPWiFiService ();
		bool		 Begin ( UDPWiFiServiceCallback pHandleReqData, const char *WiFissid, const char *WiFipwd, const char *HostName, const uint16_t portUDP = 0xFEED, MNRGBLEDBaseLib *pLED = nullptr );
		bool		 Begin ( UDPWiFiServiceCallback pHandleReqData, const char *WiFissid, const char *WiFipwd, const char *HostName, MNRGBLEDBaseLib *pLED = nullptr, const uint16_t portUDP = 0xFEED );
		void		 CheckUDP ();
		// void		 DisplayStatus ( ansiVT220Logger logger );
		void		 GetLocalTime ( String &result, time_t timeError = 0 );
		FixedIPList *GetMulticastList ();
		uint32_t	 GetMCastSentCount ();
		uint32_t	 GetRequestsReceivedCount ();
		uint32_t	 GetReplySentCount ();
		bool		 SendAll ( String sMsg );
		bool		 SendReply ( String sMsg );
		bool		 Start ();
		void		 Stop ();

	private:
		enum WiFiState { DISCONNECTED, ISCONNECTED };

		uint16_t			   m_Port = 0;
		WiFiUDP				   m_myUDP;
		String				   m_sUDPReceivedMsg;
		UDPWiFiServiceCallback m_MsgHandlerCallback;
		FixedIPList			  *m_pMulticastDestList = nullptr;
		uint32_t			   m_ulBadRequests		= 0UL;
		uint32_t			   m_ulBadMgsVersion	= 0UL;
		uint32_t			   m_ulReqCount			= 0UL;
		uint32_t			   m_ulMCastSentCount	= 0UL;
		uint32_t			   m_ulReplyCount		= 0UL;
		WiFiState			   m_WiFiState			= WiFiState::DISCONNECTED;

		bool				   GetUDPMessage ( String &RecvMessage );
		void				   ProcessUDPMessage ( const String &sRecvMessage );
		bool				   ReadUDPMessage ( String &sRecvMessage );
};
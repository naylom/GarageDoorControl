#pragma once
#include <WiFiNINA.h>
/*
	Telnet
	This class is used to allow an incoling telnet connection. It only outputs and does not process incoming data
	It does require an active (connected) network WiFi session
*/
constexpr uint16_t default_mcast_port = 0xFEEE;

class CTelnet
{
	public:
			 operator bool ();
		void begin ( uint32_t s = default_mcast_port );
		void print ( char *s );
		void print ( const char *s );
		void print ( char &s );
		void print ( char c );
		void print ( String &s );
		void print ( int32_t s );
		void print ( int16_t s );
		void print ( const arduino::__FlashStringHelper *s );
		void println ( char *s );

	private:
		bool		isConnected ();
		bool		Send ( String Msg );
		WiFiServer *m_pmyServer = nullptr;
		WiFiClient	m_myClient;
		uint16_t	m_telnetPort;
		bool		m_bClientConnected = false;
};

extern CTelnet Telnet;

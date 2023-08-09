#pragma once
/*

Logging.h

Arduino macros and function definitions for logging

if MNDEBUG is defined then output is sent to the Serial port else it is not compiled.

When outputting the Log commands repace Serial.print(ln) output

There are also functions to send ANSI escape sequences to allow output in colour and at specific screen locations
The serial monitor needs to be able to support these ANSI sequences. A free example is PuTTY.

Author: (c) M. Naylor 2022

History:
	Ver 1.0			Initial version
*/

/*
	Debug Config
*/
constexpr auto BAUD_RATE = 115200;
#include <Arduino.h>
#include <WiFiNINA.h>
#define MNDEBUG

namespace MN ::Utils
{
	void ResetBoard ( const __FlashStringHelper *pErrMsg );
}

class Logger
{
	public:
		virtual size_t Log ( const __FlashStringHelper *ifsh ) = 0;
		virtual size_t Log ( const String &s )				   = 0;
		virtual size_t Log ( char c )						   = 0;
		virtual size_t Log ( const char str [] )			   = 0;
		virtual size_t Log ( unsigned char b, int base )	   = 0;
		virtual size_t Log ( int n, int base )				   = 0;
		virtual size_t Log ( unsigned int n, int base )		   = 0;
		virtual size_t Log ( long n, int base )				   = 0;
		virtual size_t Log ( unsigned long num, int base )	   = 0;
		virtual size_t Logln ( char x )						   = 0;
		virtual size_t Logln ( const char c [] )			   = 0;
		virtual size_t Logln ( const String &s )			   = 0;
		virtual size_t Logln ( void )						   = 0;
		virtual size_t Logln ( unsigned char b, int base )	   = 0;
		virtual size_t Logln ( int num, int base )			   = 0;
		virtual size_t Logln ( unsigned int num, int base )	   = 0;
		virtual size_t Logln ( long num, int base )			   = 0;
		virtual size_t Logln ( unsigned long num, int base )   = 0;
		virtual size_t Logln ( double num, int digits )		   = 0;
		virtual void   flush ()								   = 0;
		virtual void   LogStart ()							   = 0;
		virtual bool   CanDetectClientConnect ()			   = 0;
		virtual void   SetConnectCallback ( voidFuncPtrParam pConnectCallback ) {};

	private:
};

class SerialLogger : public Logger
{
	public:
		const uint32_t BAUD_RATE = 115200;
		size_t		   Log ( const __FlashStringHelper *ifsh );
		size_t		   Log ( const String &s );
		size_t		   Log ( char c );
		size_t		   Log ( const char str [] );
		size_t		   Log ( unsigned char b, int base );
		size_t		   Log ( int n, int base );
		size_t		   Log ( unsigned int n, int base );
		size_t		   Log ( long n, int base );
		size_t		   Log ( unsigned long num, int base );
		size_t		   Logln ( char x );
		size_t		   Logln ( const char c [] );
		size_t		   Logln ( const String &s );
		size_t		   Logln ( void );
		size_t		   Logln ( unsigned char b, int base );
		size_t		   Logln ( int num, int base );
		size_t		   Logln ( unsigned int num, int base );
		size_t		   Logln ( long num, int base );
		size_t		   Logln ( unsigned long num, int base );
		size_t		   Logln ( double num, int digits );
		void		   flush ();
		void		   LogStart ();
		bool		   CanDetectClientConnect ();
};

/*
	Telnet
	This class is used to allow an incoming telnet connection. It only outputs and does not process incoming data
	It does require an active (connected) network WiFi session
*/
constexpr uint16_t default_mcast_port = 0xFEEE;

class CTelnet : public Logger
{
	public:
		operator bool ();
		void   begin ( uint32_t s = default_mcast_port );
		size_t Log ( const __FlashStringHelper *ifsh );
		size_t Log ( const String &s );
		size_t Log ( char c );
		size_t Log ( const char str [] );
		size_t Log ( unsigned char b, int base );
		size_t Log ( int n, int base );
		size_t Log ( unsigned int n, int base );
		size_t Log ( long n, int base );
		size_t Log ( unsigned long num, int base );
		size_t Logln ( char x );
		size_t Logln ( const char c [] );
		size_t Logln ( const String &s );
		size_t Logln ( void );
		size_t Logln ( unsigned char b, int base );
		size_t Logln ( int num, int base );
		size_t Logln ( unsigned int num, int base );
		size_t Logln ( long num, int base );
		size_t Logln ( unsigned long num, int base );
		size_t Logln ( double num, int digits );
		void   flush ();
		void   LogStart ();
		bool   CanDetectClientConnect ();
		void   SetConnectCallback ( voidFuncPtrParam pConnectCallback ) override;

	private:
		// char			   buff [ 32 ];
		bool			 isConnected ();
		size_t			 Send ( String Msg );
		size_t			 Send ( char c );
		size_t			 Send ( const uint8_t *buffer, size_t size );
		void			 DoConnect ();

		WiFiServer		*m_pmyServer = nullptr;
		WiFiClient		 m_myClient;
		uint16_t		 m_telnetPort;
		bool			 m_bClientConnected = false;
		voidFuncPtrParam m_ConnectCallback	= nullptr;
};

extern CTelnet Telnet;

/// @brief This class sends VT220 sequences to the supplied logger
class ansiVT220Logger
{
	public:

		// defines for ansi terminal sequences
		// colours
		enum colours : uint8_t {
			FG_BLACK = 30,
			FG_RED,
			FG_GREEN,
			FG_YELLOW,
			FG_BLUE,
			FG_MAGENTA,
			FG_CYAN,
			FG_WHITE,
			BG_BLACK = 40,
			BG_RED,
			BG_GREEN,
			BG_YELLOW,
			BG_BLUE,
			BG_MAGENTA,
			BG_CYAN,
			BG_WHITE,
			FG_BRIGHTBLACK = 90,
			FG_BRIGHTRED,
			FG_BRIGHTGREEN,
			FG_BRIGHTYELLOW,
			FG_BRIGHTBLUE,
			FG_BRIGHTMAGENTA,
			FG_BRIGHTCYAN,
			FG_BRIGHTWHITE,
			BG_BRIGHTBLACK = 100,
			BG_BRIGHTRED,
			BG_BRIGHTGREEN,
			BG_BRIGHTYELLOW,
			BG_BRIGHTBLUE,
			BG_BRIGHTMAGENTA,
			BG_BRIGHTCYAN,
			BG_BRIGHTWHITE
		};

		const static uint8_t MAX_COLS = 132;
		const static uint8_t MAX_ROWS = 25;
		ansiVT220Logger ( Logger &logger ) : m_logger ( logger ) {};
		void		ClearScreen ();
		void		AT ( uint8_t row, uint8_t col, String s );
		void		COLOUR_AT ( colours FGColour, colours BGColour, uint8_t row, uint8_t col, String s );
		void		RestoreCursor ( void );
		void		SaveCursor ( void );
		void		ClearLine ( uint8_t row );
		void		ClearPartofLine ( uint8_t row, uint8_t start_col, uint8_t toclear );
		static void OnClientConnect ( void *ptr );
		void		LogStart ();

	private:
		Logger		 &m_logger;
		const String  CSI			 = F ( "\x1b[" );
		const String  SAVE_CURSOR	 = F ( "\x1b[s" );
		const String  RESTORE_CURSOR = F ( "\x1b[u" );
		const String  CLEAR_LINE	 = F ( "\x1b[2K" );
		const String  RESET_COLOURS	 = F ( "\x1b[0m" );
		const String  CLEAR_SCREEN	 = F ( "\x1b[2J" );
		static String SCREEN_SIZE132;
};

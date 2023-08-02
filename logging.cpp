/*

Logging.cpp

Implements logging.h

if MNDEBUG is defined then output is sent to the Serial port else it is not compiled.

When outputting the Log commands repace Serial.print(ln) output

There are also functions to send ANSI escape sequences to allow output in colour and at specific screen locations
The serial monitor needs to be able to support these ANSI sequences. A free example is PuTTY.

Author: (c) M. Naylor 2022

History:
	Ver 1.0			Initial version
*/
#include "logging.h"
#ifdef ARDUINO_AVR_UNO
extern "C"
{
	void ( *SWResetBoard ) ( void ) = 0; // declare reset function at address 0
}

void ResetBoard ( const __FlashStringHelper *pErrMsg )
{
	// Error ( pErrMsg );
	// LogFlush;
	SWResetBoard ();
}
#else
void ResetBoard ( const __FlashStringHelper *pErrMsg )
{
	// Error ( pErrMsg );
	// LogFlush();
	NVIC_SystemReset (); // processor software reset for ARM SAMD processor
}
#endif
void ansiVT220Logger::ClearScreen ()
{
	m_logger.Log ( CLEAR_SCREEN );
}

void ansiVT220Logger::AT ( uint8_t row, uint8_t col, String s )
{
	row		 = row == 0 ? 1 : row;
	col		 = col == 0 ? 1 : col;
	String m = String ( CSI ) + row + String ( ";" ) + col + String ( "H" ) + s;
	m_logger.Log ( m );
}

void ansiVT220Logger::COLOUR_AT ( colours FGColour, colours BGColour, uint8_t row, uint8_t col, String s )
{
	// set colours
	m_logger.Log ( String ( CSI ) + FGColour + ";" + BGColour + "m" );
	AT ( row, col, s );
	// reset colours
	m_logger.Log ( RESET_COLOURS );
}

void ansiVT220Logger::RestoreCursor ( void )
{
	m_logger.Log ( RESTORE_CURSOR );
}

void ansiVT220Logger::SaveCursor ( void )
{
	m_logger.Log ( SAVE_CURSOR );
}

void ansiVT220Logger::ClearLine ( uint8_t row )
{
	SaveCursor ();
	AT ( row, 1, String ( CLEAR_LINE ) );
	RestoreCursor ();
}

void ansiVT220Logger::ClearPartofLine ( uint8_t row, uint8_t start_col, uint8_t toclear )
{
	static char buf [ ansiVT220Logger::MAX_COLS + 1 ];
	memset ( buf, ' ', sizeof ( buf ) - 1 );
	buf [ ansiVT220Logger::MAX_COLS ]  = 0;

	// build string of toclear spaces
	toclear							  %= ansiVT220Logger::MAX_COLS + 1; // ensure toclear is <= MAX_COLS
	if ( start_col + toclear > ansiVT220Logger::MAX_COLS + 1 )
	{
		toclear = ansiVT220Logger::MAX_COLS - start_col + 1;
	}
	toclear = ( ansiVT220Logger::MAX_COLS - start_col + 1 ) % ( ansiVT220Logger::MAX_COLS + 1 ); // ensure toclear doesn't go past end of line
	SaveCursor ();
	buf [ toclear ] = 0;
	AT ( row, start_col, buf );
	RestoreCursor ();
}

void ansiVT220Logger::LogStart ()
{
	m_logger.LogStart ();
}

size_t SerialLogger::Log ( const __FlashStringHelper *ifsh )
{
	return Serial.print ( ifsh );
}

size_t SerialLogger::Log ( const String &s )
{
	return Serial.print ( s );
}

size_t SerialLogger::Log ( char c )
{
	return Serial.print ( c );
}

size_t SerialLogger::Log ( const char str [] )
{
	return Serial.print ( str );
}

size_t SerialLogger::Log ( unsigned char b, int base )
{
	return Serial.print ( b, base );
}

size_t SerialLogger::Log ( int n, int base )
{
	return Serial.print ( n, base );
}

size_t SerialLogger::Log ( unsigned int n, int base )
{
	return Serial.print ( n, base );
}

size_t SerialLogger::Log ( long n, int base )
{
	return Serial.print ( n, base );
}

size_t SerialLogger::Log ( unsigned long num, int base )
{
	return Serial.print ( num, base );
}

void SerialLogger::flush ()
{
	return Serial.flush ();
}

size_t SerialLogger::Logln ( const char c [] )
{
	return Serial.println ( c );
}

size_t SerialLogger::Logln ( const String &s )
{
	return Serial.println ( s );
}

size_t SerialLogger::Logln ( void )
{
	return Serial.println ();
}

size_t SerialLogger::Logln ( unsigned char b, int base )
{
	return Serial.println ( b, base );
}

size_t SerialLogger::Logln ( int num, int base )
{
	return Serial.println ( num, base );
}

size_t SerialLogger::Logln ( unsigned int num, int base )
{
	return Serial.println ( num, base );
}

size_t SerialLogger::Logln ( long num, int base )
{
	return Serial.println ( num, base );
}

size_t SerialLogger::Logln ( unsigned long num, int base )
{
	return Serial.println ( num, base );
}

size_t SerialLogger::Logln ( double num, int digits )
{
	return Serial.println ( num, digits );
}

size_t SerialLogger::Logln ( char x )
{
	return Serial.println ( x );
}

void SerialLogger::LogStart ()
{
	Serial.begin ( SerialLogger::BAUD_RATE );
	while ( !Serial )
		;
}

/* ----------------------------------------------------------------------------------------------------------------------- */
bool CTelnet::isConnected ()
{
	return WiFi.status () == WL_CONNECTED ? true : false;
}

CTelnet::operator bool ()
{
	return isConnected ();
}

void CTelnet::begin ( uint32_t port )
{
	m_telnetPort = port & 0xffff;
	m_pmyServer	 = new WiFiServer ( port & 0xffff );
	m_pmyServer->begin ();
}

size_t CTelnet::Send ( char c )
{
	size_t result = 0;
	if ( m_myClient )
	{
		size_t result = m_myClient.print ( c );
		if ( result <= 0 )
		{
			m_myClient.stop ();
			m_bClientConnected = false;
		}
	}
	else
	{
		m_myClient = m_pmyServer->available ();
		result	   = 0;
	}
	return result;
}

size_t CTelnet::Send ( const uint8_t *buffer, size_t size )
{
	size_t result = 0;
	if ( m_myClient )
	{
		size_t result = m_myClient.print ( (const char)*buffer, size );
		if ( result <= 0 )
		{
			m_myClient.stop ();
			m_bClientConnected = false;
		}
	}
	else
	{
		m_myClient = m_pmyServer->available ();
		result	   = 0;
	}
	return result;
}

size_t CTelnet::Send ( String Msg )
{
	size_t Result = 0;
	if ( m_myClient )
	{
		Result = m_myClient.print ( Msg );
		if ( Result <= 0 )
		{
			m_myClient.stop ();
			m_bClientConnected = false;
		}
	}
	else
	{
		m_myClient = m_pmyServer->available ();
	}
	return Result;
}

size_t CTelnet::Log ( char c )
{
	char buf [ 2 ];
	buf [ 0 ] = c;
	buf [ 1 ] = 0;
	return Send ( buf );
}

size_t CTelnet::Log ( const char str [] )
{
	return Send ( str );
}

size_t CTelnet::Log ( unsigned char b, int base )
{
	return Send ( String ( b, base ) );
}

size_t CTelnet::Log ( int n, int base )
{
	return Send ( String ( n, base ) );
}

size_t CTelnet::Log ( unsigned int n, int base )
{
	return Send ( String ( n, base ) );
}

size_t CTelnet::Log ( const __FlashStringHelper *s )
{
	return Send ( String ( s ) );
}

size_t CTelnet::Log ( const String &s )
{
	return Send ( String ( s ) );
}

size_t CTelnet::Log ( unsigned long num, int base )
{
	return Send ( String ( num, base ) );
}

size_t CTelnet::Log ( long n, int base )
{
	return Send ( String ( n, base ) );
}

size_t CTelnet::Logln ( char x )
{
	return Send ( String ( x ) + "\n" );
}

size_t CTelnet::Logln ( const char c [] )
{
	return Send ( String ( c ) + "\n" );
}

size_t CTelnet::Logln ( const String &s )
{
	return Send ( String ( s ) + "\n" );
}

size_t CTelnet::Logln ( void )
{
	return Send ( "\n" );
}

size_t CTelnet::Logln ( unsigned char b, int base )
{
	return Send ( String ( b, base ) + "\n" );
}

size_t CTelnet::Logln ( int num, int base )
{
	return Send ( String ( num, base ) + "\n" );
}

size_t CTelnet::Logln ( unsigned int num, int base )
{
	return Send ( String ( num, base ) + "\n" );
}

size_t CTelnet::Logln ( long num, int base )
{
	return Send ( String ( num, base ) + "\n" );
}

size_t CTelnet::Logln ( unsigned long num, int base )
{
	return Send ( String ( num, base ) + "\n" );
}

size_t CTelnet::Logln ( double num, int digits )
{
	return Send ( String ( num, digits ) + "\n" );
}

void CTelnet::flush ()
{
	m_myClient.flush ();
}

void CTelnet::LogStart ()
{
}

CTelnet Telnet;

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

namespace MN ::Utils
{
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
} // namespace MN::Utils

void ansiVT220Logger::ClearScreen ()
{
	m_logger.print ( CLEAR_SCREEN );
}

void ansiVT220Logger::AT ( uint8_t row, uint8_t col, String s )
{
	row		 = row == 0 ? 1 : row;
	col		 = col == 0 ? 1 : col;
	String m = String ( CSI ) + row + String ( ";" ) + col + String ( "H" ) + s;
	m_logger.print ( m );
}

void ansiVT220Logger::COLOUR_AT ( colours FGColour, colours BGColour, uint8_t row, uint8_t col, String s )
{
	// set colours

	m_logger.print ( String ( CSI ) + FGColour + ";" + BGColour + "m" );
	AT ( row, col, s );
	// reset colours
	m_logger.print ( RESET_COLOURS );
}

void ansiVT220Logger::RestoreCursor ( void )
{
	m_logger.print ( RESTORE_CURSOR );
}

void ansiVT220Logger::SaveCursor ( void )
{
	m_logger.print ( SAVE_CURSOR );
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

void ansiVT220Logger::OnClientConnect ( void *plog )
{
	Logger *pLog = (Logger *)plog;
	pLog->print ( SCREEN_SIZE132 );
	pLog->print ( ansiVT220Logger::OSC + "2;GarageControl Debug\x1b\\" + STRING_TERMINATOR );
	pLog->print ( "\x1b[63;2\"p" );
}

void ansiVT220Logger::LogStart ()
{
	m_logger.LogStart ();
	if ( m_logger.CanDetectClientConnect () )
	{
		m_logger.SetConnectCallback ( &ansiVT220Logger::OnClientConnect );
	}
}

String ansiVT220Logger::STRING_TERMINATOR = F ( "\x1b\\" );
String ansiVT220Logger::OSC				  = F ( "\x1b]" );
String ansiVT220Logger::SCREEN_SIZE132	  = F ( "\x1b[?3h" );
String ansiVT220Logger::WINDOW_TITLE	  = F ( "Debug" );

int	   SerialLogger::available ()
{
	return Serial.available ();
}

int SerialLogger::read ()
{
	return Serial.read ();
}

int SerialLogger::peek ()
{
	return Serial.peek ();
}

size_t SerialLogger::write ( String Msg )
{
	return Serial.print ( Msg );
}

size_t SerialLogger::write ( uint8_t c )
{
	return Serial.print ( c );
}

size_t SerialLogger::write ( const uint8_t *buffer, size_t size )
{
	return Serial.write ( (char *)buffer, size );
}

void SerialLogger::LogStart ()
{
	Serial.begin ( SerialLogger::BAUD_RATE );
	while ( !Serial )
		;
}

bool SerialLogger::CanDetectClientConnect ()
{
	return false;
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
	m_pmyServer	 = new WiFiServer ( m_telnetPort );
	m_pmyServer->begin ();
}

size_t CTelnet::write ( uint8_t c )
{
	size_t result = 0;
	if ( m_myClient.connected () )
	{
		size_t result = m_myClient.write ( c );
		if ( result <= 0 )
		{
			m_myClient.stop ();
			m_bClientConnected = false;
			// Serial.println ( "Client disconnected" );
		}
	}
	else
	{
		if ( m_pmyServer != nullptr )
		{
			m_myClient = m_pmyServer->available ();
			if ( m_myClient )
			{
				// Serial.println ( "Client connected" );
				m_bClientConnected = true;
			}
		}
	}
	return result;
}

void CTelnet::SetConnectCallback ( voidFuncPtrParam pConnectCallback )
{
	m_ConnectCallback = pConnectCallback;
}

void CTelnet::DoConnect ()
{
	if ( m_ConnectCallback != nullptr )
	{
		m_ConnectCallback ( this );
	}
}

size_t CTelnet::write ( const uint8_t *buffer, size_t size )
{
	size_t result = 0;
	if ( m_myClient.connected () && size > 0 )
	{
		size_t result = m_myClient.write ( (char* )buffer, size );
		if ( result <= 0 )
		{
			m_myClient.stop ();
			m_bClientConnected = false;
			// Serial.println ( "Client disconnected" );
		}
	}
	else
	{
		if ( m_pmyServer != nullptr )
		{
			m_myClient = m_pmyServer->available ();
			if ( m_myClient )
			{
				// Serial.println ( "Client connected" );
				m_bClientConnected = true;
				DoConnect ();
			}
		}
	}
	return result;
}

size_t CTelnet::write ( String Msg )
{
	size_t Result = 0;
	if ( m_myClient.connected () && Msg.length () > 0 )
	{
		Result = m_myClient.print ( Msg );
		if ( Result <= 0 )
		{
			m_myClient.stop ();
			m_bClientConnected = false;
			// Serial.println ( "Client disconnected" );
		}
	}
	else
	{
		if ( m_pmyServer != nullptr )
		{
			m_myClient = m_pmyServer->available ();
			if ( m_myClient )
			{
				// Serial.println ( "Client connected" );
				m_bClientConnected = true;
				DoConnect ();
			}
		}
	}
	return Result;
}

int CTelnet::available ()
{
	return m_myClient.available ();
}

int CTelnet::read ()
{
	return m_myClient.read ();
}

int CTelnet::peek ()
{
	return m_myClient.peek ();
}

void CTelnet::LogStart ()
{
	begin ();
}

bool CTelnet::CanDetectClientConnect ()
{
	return true;
}

CTelnet Telnet;
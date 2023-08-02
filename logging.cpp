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
	//Error ( pErrMsg );
	//LogFlush;
	SWResetBoard ();
}
#else
void ResetBoard ( const __FlashStringHelper *pErrMsg )
{
	//Error ( pErrMsg );
	//LogFlush();
	NVIC_SystemReset (); // processor software reset for ARM SAMD processor
}
#endif
/* ---------------------------------------- */
// Screen positioning support
/* ---------------------------------------- */
/*
void ClearScreen ()
{
	Log ( "\x1b[2J" );
}

void AT ( uint8_t row, uint8_t col, String s )
{
	row		 = row == 0 ? 1 : row;
	col		 = col == 0 ? 1 : col;
	String m = String ( CSI ) + row + String ( ";" ) + col + String ( "H" ) + s;
	Log ( m );
}

void COLOUR_AT ( uint8_t FGColour, uint8_t BGColour, uint8_t row, uint8_t col, String s )
{
	// set colours
	Log ( String ( CSI ) + FGColour + ";" + BGColour + "m" );
	AT ( row, col, s );
	// reset colours
	Log ( RESET_COLOURS );
}

void RestoreCursor ( void )
{
	Log ( RESTORE_CURSOR );
}

void SaveCursor ( void )
{
	Log ( SAVE_CURSOR );
}

void ClearLine ( uint8_t row )
{
	SaveCursor ();
	AT ( row, 1, String ( CLEAR_LINE ) );
	RestoreCursor ();
}

void ClearPartofLine ( uint8_t row, uint8_t start_col, uint8_t toclear )
{
	static char buf [ MAX_COLS + 1 ];
	memset ( buf, ' ', sizeof ( buf ) - 1 );
	buf [ MAX_COLS ]  = 0;

	// build string of toclear spaces
	toclear			 %= MAX_COLS + 1; // ensure toclear is <= MAX_COLS
	if ( start_col + toclear > MAX_COLS + 1 )
	{
		toclear = MAX_COLS - start_col + 1;
	}
	toclear = ( MAX_COLS - start_col + 1 ) % ( MAX_COLS + 1 ); // ensure toclear doesn't go past end of line
	SaveCursor ();
	buf [ toclear ] = 0;
	AT ( row, start_col, buf );
	RestoreCursor ();
}

void Error ( String s )
{
	// Clear error line
	ClearLine ( ERROR_ROW );
	// Output new error
	COLOUR_AT ( FG_WHITE, BG_RED, ERROR_ROW, ERROR_COL, s.substring ( 0, min ( s.length (), (unsigned int)MAX_COLS ) ) );
}
*/
void ansiVT220Logger::ClearScreen ()
{
	Log ( CLEAR_SCREEN );
}

void ansiVT220Logger::AT ( uint8_t row, uint8_t col, String s )
{
	row		 = row == 0 ? 1 : row;
	col		 = col == 0 ? 1 : col;
	String m = String ( CSI ) + row + String ( ";" ) + col + String ( "H" ) + s;
	Log ( m );
}

void ansiVT220Logger::COLOUR_AT ( colours FGColour, colours BGColour, uint8_t row, uint8_t col, String s )
{
	// set colours
	Log ( String ( CSI ) + FGColour + ";" + BGColour + "m" );
	AT ( row, col, s );
	// reset colours
	Log ( RESET_COLOURS );
}

void ansiVT220Logger::RestoreCursor ( void )
{
	Log ( RESTORE_CURSOR );
}

void ansiVT220Logger::SaveCursor ( void )
{
	Log ( SAVE_CURSOR );
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
	buf [ ansiVT220Logger::MAX_COLS ]	 = 0;

	// build string of toclear spaces
	toclear						%= ansiVT220Logger::MAX_COLS + 1; // ensure toclear is <= MAX_COLS
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

size_t SerialLogger::Logln ( long long num, int base )
{
	return Serial.println ( num, base );
}

size_t SerialLogger::Logln ( unsigned long long num, int base )
{
	return Serial.println ( num, base );
}

size_t SerialLogger::Logln ( double num, int digits )
{
	return Serial.println ( num, digits );
}

size_t SerialLogger::Logln ( const Printable &x )
{
	return Serial.println ( x );
}

size_t SerialLogger::Logln ( char x )
{
	return Serial.println ( x );
}

void SerialLogger::LogStart ()
{
	Serial.begin ( SerialLogger::BAUD_RATE );
	while ( !Serial );
}

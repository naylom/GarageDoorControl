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
	void ( *SWResetBoard ) ( void ) = 0;					        //declare reset function at address 0
}
void ResetBoard ( const __FlashStringHelper* pErrMsg )
{
	Logln ( pErrMsg );
	LogFlush;
	SWResetBoard ();
}
#else
void ResetBoard ( const __FlashStringHelper* pErrMsg )
{
	Logln ( pErrMsg );
	LogFlush;
	NVIC_SystemReset ();										// processor software reset for ARM SAMD processor
}
#endif
/* ---------------------------------------- */
// Screen positioning support
/* ---------------------------------------- */
void ClearScreen ()
{
	Log ( "\x1b[2J" );
}

void AT ( uint8_t row, uint8_t col, String s )
{
	row = row == 0 ? 1 : row;
	col = col == 0 ? 1 : col;
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
	buf [ MAX_COLS ] = 0;

	// build string of toclear spaces
	toclear %= MAX_COLS + 1;						// ensure toclear is <= MAX_COLS
	if ( start_col + toclear > MAX_COLS + 1 )
	{
		toclear = MAX_COLS - start_col + 1;
	}
	toclear = ( MAX_COLS - start_col + 1 ) % ( MAX_COLS + 1 );	// ensure toclear doesn't go past end of line
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
	COLOUR_AT ( FG_WHITE, BG_RED, ERROR_ROW, ERROR_COL, s.substring( 0, min ( s.length() ,80 ) ) );
}

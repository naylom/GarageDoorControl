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

#define MNDEBUG
#ifdef MNDEBUG
#define Log(x)			Serial.print ( x )
#define Log2(x, y)		Serial.print ( x, y )
#define Logln(x)		Serial.println ( x )
#define LogFlush		Serial.flush()
#define LogStart()		Serial.begin ( BAUD_RATE ); /*while ( !Serial )*/
#else
#define Log(x) 
#define Log2(x, y)
#define Logln(x)
#define LogFlush
#define LogStart()
#endif


void ResetBoard ( const __FlashStringHelper* pErrMsg );

/*  ---------------------------------------  */
// For ansi colour and cursor movement
/*  ---------------------------------------  */

// code to draw screen
constexpr auto      ERROR_ROW			= 25;
constexpr auto      ERROR_COL			= 1;
constexpr auto      STATUS_LINE			= 24;
constexpr auto      STATUS_START_COL	= 30;
constexpr auto      STATS_ROW			= 8;
constexpr auto      STATS_RESULT_COL	= 70;
constexpr auto      MODE_ROW			= 20;
constexpr auto      MODE_RESULT_COL		= 45;
constexpr auto      MAX_COLS			= 80;
constexpr auto      MAX_ROWS			= 25;

// defines for ansi terminal sequences
//#define CSI				F("\x1b[")
constexpr auto      CSI	                = F ( "\x1b[" );
constexpr auto      SAVE_CURSOR		    = F ( "\x1b[s" );
constexpr auto      RESTORE_CURSOR	    = F ( "\x1b[u" );
constexpr auto      CLEAR_LINE		    = F ( "\x1b[2K" );
constexpr auto      RESET_COLOURS       = F ("\x1b[0m" );

// colors
constexpr auto      FG_BLACK		    = 30;
constexpr auto      FG_RED			    = 31;
constexpr auto      FG_GREEN		    = 32;
constexpr auto      FG_YELLOW		    = 33;
constexpr auto      FG_BLUE			    = 34;
constexpr auto      FG_MAGENTA		    = 35;
constexpr auto      FG_CYAN			    = 36;
constexpr auto      FG_WHITE		    = 37;

constexpr auto      BG_BLACK		    = 40;
constexpr auto      BG_RED			    = 41;
constexpr auto      BG_GREEN		    = 42;
constexpr auto      BG_YELLOW		    = 43;
constexpr auto      BG_BLUE			    = 44;
constexpr auto      BG_MAGENTA		    = 45;
constexpr auto      BG_CYAN			    = 46;
constexpr auto      BG_WHITE		    = 47;


void ClearScreen ();
void AT ( uint8_t row, uint8_t col, String s );
void COLOUR_AT ( uint8_t FGColour, uint8_t BGColour, uint8_t row, uint8_t col, String s );
void RestoreCursor ( void );
void SaveCursor ( void );
void ClearLine ( uint8_t row );
void ClearPartofLine ( uint8_t row, uint8_t start_col, uint8_t toclear );
void Error ( String s );
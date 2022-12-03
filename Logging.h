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
#define LogStart()		Serial.begin ( BAUD_RATE ); while ( !Serial )
#else
#define Log(x) 
#define Log2(x, y)
#define Logln(x)
#define LogFlush
#define LogStart
#endif


void ResetBoard ( const __FlashStringHelper* pErrMsg );

/*  ---------------------------------------  */
// For ansi colour and cursor movement
/*  ---------------------------------------  */

// code to draw screen
#define ERROR_ROW			25
#define ERROR_COL			1
#define STATUS_LINE			24
#define STATUS_START_COL	30
#define STATS_ROW			8
#define STATS_RESULT_COL	70
#define MODE_ROW			20
#define MODE_RESULT_COL		45
#define MAX_COLS			80
#define MAX_ROWS			25

// defines for ansi terminal sequences
#define CSI				F("\x1b[")
#define SAVE_CURSOR		F("\x1b[s")
#define RESTORE_CURSOR	F("\x1b[u")
#define CLEAR_LINE		F("\x1b[2K")
#define RESET_COLOURS   F("\x1b[0m")

// colors
#define FG_BLACK		30
#define FG_RED			31
#define FG_GREEN		32
#define FG_YELLOW		33
#define FG_BLUE			34
#define FG_MAGENTA		35
#define FG_CYAN			36
#define FG_WHITE		37

#define BG_BLACK		40
#define BG_RED			41
#define BG_GREEN		42
#define BG_YELLOW		43
#define BG_BLUE			44
#define BG_MAGENTA		45
#define BG_CYAN			46
#define BG_WHITE		47


void ClearScreen ();
void AT ( uint8_t row, uint8_t col, String s );
void COLOUR_AT ( uint8_t FGColour, uint8_t BGColour, uint8_t row, uint8_t col, String s );
void RestoreCursor ( void );
void SaveCursor ( void );
void ClearLine ( uint8_t row );
void ClearPartofLine ( uint8_t row, uint8_t start_col, uint8_t toclear );
void Error ( String s );
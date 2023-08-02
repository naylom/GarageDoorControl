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
#include "CTelnet.h"
#define MNDEBUG
#undef TELNET
/*
#ifdef MNDEBUG
	#ifdef TELNET
		#define Log( x )	 Telnet.print ( x )
		#define Log2( x, y ) Telnet.print ( x, y )
		#define Logln( x )	 Telnet.Logln ( x )
		#define LogFlush
		#define LogStart() Telnet.begin ( 0xFEEE )
	#else
		#define Log( x )	 Serial.print ( x )
		#define Log2( x, y ) Serial.print ( x, y )
		#define Logln( x )	 Serial.Logln ( x )
		#define LogFlush	 Serial.flush ()
		#define LogStart()	 Serial.begin ( BAUD_RATE ); while ( !Serial )
	#endif
#else
	#define Log( x )
	#define Log2( x, y )
	#define Logln( x )
	#define LogFlush
	#define LogStart()
#endif
*/
void ResetBoard ( const __FlashStringHelper *pErrMsg );

/*  ---------------------------------------  */
// For ansi colour and cursor movement
/*  ---------------------------------------  */
/*
// code to draw screen
constexpr auto ERROR_ROW		= 25;
constexpr auto ERROR_COL		= 1;
constexpr auto STATUS_LINE		= 24;
constexpr auto STATUS_START_COL = 30;
constexpr auto STATS_ROW		= 8;
constexpr auto STATS_RESULT_COL = 70;
constexpr auto MODE_ROW			= 20;
constexpr auto MODE_RESULT_COL	= 45;
constexpr auto MAX_COLS			= 80;
constexpr auto MAX_ROWS			= 25;

// defines for ansi terminal sequences
// #define CSI				F("\x1b[")
const auto	   CSI				= F ( "\x1b[" );
const auto	   SAVE_CURSOR		= F ( "\x1b[s" );
const auto	   RESTORE_CURSOR	= F ( "\x1b[u" );
const auto	   CLEAR_LINE		= F ( "\x1b[2K" );
const auto	   RESET_COLOURS	= F ( "\x1b[0m" );

// colors
constexpr auto FG_BLACK			= 30;
constexpr auto FG_RED			= 31;
constexpr auto FG_GREEN			= 32;
constexpr auto FG_YELLOW		= 33;
constexpr auto FG_BLUE			= 34;
constexpr auto FG_MAGENTA		= 35;
constexpr auto FG_CYAN			= 36;
constexpr auto FG_WHITE			= 37;

constexpr auto BG_BLACK			= 40;
constexpr auto BG_RED			= 41;
constexpr auto BG_GREEN			= 42;
constexpr auto BG_YELLOW		= 43;
constexpr auto BG_BLUE			= 44;
constexpr auto BG_MAGENTA		= 45;
constexpr auto BG_CYAN			= 46;
constexpr auto BG_WHITE			= 47;

void		   ClearScreen ();
void		   AT ( uint8_t row, uint8_t col, String s );
void		   COLOUR_AT ( uint8_t FGColour, uint8_t BGColour, uint8_t row, uint8_t col, String s );
void		   RestoreCursor ( void );
void		   SaveCursor ( void );
void		   ClearLine ( uint8_t row );
void		   ClearPartofLine ( uint8_t row, uint8_t start_col, uint8_t toclear );
void		   Error ( String s );
*/
class SerialLogger
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
		size_t		   Logln ( long long num, int base );
		size_t		   Logln ( unsigned long long num, int base );
		size_t		   Logln ( double num, int digits );
		size_t		   Logln ( const Printable &x );
		void		   flush ();
		void		   LogStart ();

	private:
};

class ansiVT220Logger : public SerialLogger
{
	public:

		// defines for ansi terminal sequences
		// colours
		enum colours : uint8_t { FG_BLACK = 30, FG_RED, FG_GREEN, FG_YELLOW, FG_BLUE, FG_MAGENTA, FG_CYAN, FG_WHITE, BG_BLACK = 40, BG_RED, BG_GREEN, BG_YELLOW, BG_BLUE, BG_MAGENTA, BG_CYAN, BG_WHITE };

		const static uint8_t MAX_COLS = 80;
		const static uint8_t MAX_ROWS = 25;

		void				 ClearScreen ();
		void				 AT ( uint8_t row, uint8_t col, String s );
		void				 COLOUR_AT ( colours FGColour, colours BGColour, uint8_t row, uint8_t col, String s );
		void				 RestoreCursor ( void );
		void				 SaveCursor ( void );
		void				 ClearLine ( uint8_t row );
		void				 ClearPartofLine ( uint8_t row, uint8_t start_col, uint8_t toclear );

	private:
		const String CSI			= F ( "\x1b[" );
		const String SAVE_CURSOR	= F ( "\x1b[s" );
		const String RESTORE_CURSOR = F ( "\x1b[u" );
		const String CLEAR_LINE		= F ( "\x1b[2K" );
		const String RESET_COLOURS	= F ( "\x1b[0m" );
		const String CLEAR_SCREEN	= F ( "\x1b[2J" );
};
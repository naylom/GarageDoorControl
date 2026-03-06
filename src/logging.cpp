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
	void ( *SWResetBoard ) ( void ) = 0;  // declare reset function at address 0
}

void ResetBoard ( const __FlashStringHelper* pErrMsg )
{
	// Error ( pErrMsg );
	// LogFlush;
	SWResetBoard();
}
#else
/**
 * @brief Performs a hardware reset of the microcontroller.
 * @details On SAMD21/ARM targets calls NVIC_SystemReset(). On AVR targets jumps to
 *          address 0. The pErrMsg parameter is accepted for API compatibility but
 *          is not currently logged (callers should log before calling if needed).
 * @param pErrMsg Flash-string error message describing the reset reason.
 */
void ResetBoard ( const __FlashStringHelper* pErrMsg )
{
	// Error ( pErrMsg );
	// LogFlush();
	NVIC_SystemReset();  // processor software reset for ARM SAMD processor
}
#endif
}  // namespace MN::Utils

/**
 * @brief Sends an ANSI escape sequence to clear the entire terminal screen.
 */
void ansiVT220Logger::ClearScreen ()
{
	m_logger.print ( CLEAR_SCREEN );
}

/**
 * @brief Moves the terminal cursor to the specified position and prints a string.
 * @param row Screen row (1-based; 0 is treated as 1).
 * @param col Screen column (1-based; 0 is treated as 1).
 * @param s   String to print at that position.
 */
void ansiVT220Logger::AT ( uint8_t row, uint8_t col, String s )
{
	row = row == 0 ? 1 : row;
	col = col == 0 ? 1 : col;
	String m = String ( CSI ) + row + String ( ";" ) + col + String ( "H" ) + s;
	m_logger.print ( m );
}

/**
 * @brief Sets foreground/background colours, moves the cursor, prints a string,
 *        then resets the terminal colours to default.
 * @param FGColour Foreground colour (ansiVT220Logger::colours enum value).
 * @param BGColour Background colour (ansiVT220Logger::colours enum value).
 * @param row      Screen row (1-based).
 * @param col      Screen column (1-based).
 * @param s        String to print.
 */
void ansiVT220Logger::COLOUR_AT ( colours FGColour, colours BGColour, uint8_t row, uint8_t col, String s )
{
	// set colours

	m_logger.print ( String ( CSI ) + FGColour + ";" + BGColour + "m" );
	AT ( row, col, s );
	// reset colours
	m_logger.print ( RESET_COLOURS );
}

/**
 * @brief Sends the ANSI escape sequence to restore the previously saved cursor position.
 */
void ansiVT220Logger::RestoreCursor ( void )
{
	m_logger.print ( RESTORE_CURSOR );
}

/**
 * @brief Sends the ANSI escape sequence to save the current cursor position.
 */
void ansiVT220Logger::SaveCursor ( void )
{
	m_logger.print ( SAVE_CURSOR );
}

/**
 * @brief Saves the cursor, clears the entire specified screen row, then restores the cursor.
 * @param row Screen row (1-based) to clear.
 */
void ansiVT220Logger::ClearLine ( uint8_t row )
{
	SaveCursor();
	AT ( row, 1, String ( CLEAR_LINE ) );
	RestoreCursor();
}

/**
 * @brief Saves the cursor, overwrites a portion of a screen row with spaces, then
 *        restores the cursor.
 * @param row       Screen row (1-based) containing the region to clear.
 * @param start_col Starting column (1-based) of the region.
 * @param toclear   Number of characters to overwrite with spaces.
 */
void ansiVT220Logger::ClearPartofLine ( uint8_t row, uint8_t start_col, uint8_t toclear )
{
	static char buf [ ansiVT220Logger::MAX_COLS + 1 ];
	memset ( buf, ' ', sizeof ( buf ) - 1 );
	buf [ ansiVT220Logger::MAX_COLS ] = 0;

	// build string of toclear spaces
	toclear %= ansiVT220Logger::MAX_COLS + 1;  // ensure toclear is <= MAX_COLS
	if ( start_col + toclear > ansiVT220Logger::MAX_COLS + 1 )
	{
		toclear = ansiVT220Logger::MAX_COLS - start_col + 1;
	}
	toclear = ( ansiVT220Logger::MAX_COLS - start_col + 1 ) %
	          ( ansiVT220Logger::MAX_COLS + 1 );  // ensure toclear doesn't go past end of line
	SaveCursor();
	buf [ toclear ] = 0;
	AT ( row, start_col, buf );
	RestoreCursor();
}

/**
 * @brief Callback invoked by the logger backend when a client connects to the terminal.
 * @details Configures the terminal to 132-column mode, sets the window title to
 *          "GarageControl Debug", and enables 63-line page mode.
 * @param plog Pointer to the Logger (transport) instance for the new connection.
 */
void ansiVT220Logger::OnClientConnect ( void* plog )
{
	Logger* pLog = (Logger*)plog;
	pLog->print ( SCREEN_SIZE132 );
	pLog->print ( ansiVT220Logger::OSC + "2;GarageControl Debug\x1b\\" + STRING_TERMINATOR );
	pLog->print ( F ( "\x1b[63;2\"p" ) );
}

/**
 * @brief Starts the underlying logger transport and registers the client-connect
 *        callback if the transport supports connection detection.
 */
void ansiVT220Logger::LogStart ()
{
	m_logger.LogStart();
	if ( m_logger.CanDetectClientConnect() )
	{
		m_logger.SetConnectCallback ( &ansiVT220Logger::OnClientConnect );
	}
}

String ansiVT220Logger::STRING_TERMINATOR = F ( "\x1b\\" );
String ansiVT220Logger::OSC = F ( "\x1b]" );
String ansiVT220Logger::SCREEN_SIZE132 = F ( "\x1b[?3h" );
String ansiVT220Logger::WINDOW_TITLE = F ( "Debug" );

/**
 * @brief Returns the number of bytes available to read from the serial port.
 * @return Serial.available() byte count.
 */
int SerialLogger::available ()
{
	return Serial.available();
}

/**
 * @brief Reads and returns the next byte from the serial port.
 * @return Next byte, or -1 if no data is available.
 */
int SerialLogger::read ()
{
	return Serial.read();
}

/**
 * @brief Returns the next byte from the serial port without consuming it.
 * @return Next byte, or -1 if no data is available.
 */
int SerialLogger::peek ()
{
	return Serial.peek();
}

/**
 * @brief Writes a String to the serial port.
 * @param Msg The string to print.
 * @return Number of bytes written.
 */
size_t SerialLogger::write ( String Msg )
{
	return Serial.print ( Msg );
}

/**
 * @brief Writes a single byte to the serial port.
 * @param c The byte to write.
 * @return Number of bytes written.
 */
size_t SerialLogger::write ( uint8_t c )
{
	return Serial.print ( c );
}

/**
 * @brief Writes a byte buffer to the serial port.
 * @param buffer Pointer to the byte array to write.
 * @param size   Number of bytes to write.
 * @return Number of bytes written.
 */
size_t SerialLogger::write ( const uint8_t* buffer, size_t size )
{
	return Serial.write ( (char*)buffer, size );
}

/**
 * @brief Initialises the serial port at BAUD_RATE and blocks until it is ready.
 */
void SerialLogger::LogStart ()
{
	Serial.begin ( BAUD_RATE );
	while ( !Serial )
		;
}

/**
 * @brief Returns whether this transport can detect client connections.
 * @return Always false for SerialLogger (USB serial has no connection event).
 */
bool SerialLogger::CanDetectClientConnect ()
{
	return false;
}

/* -----------------------------------------------------------------------------------------------------------------------
 */
/**
 * @brief Returns whether the Telnet transport has an active WiFi and TCP client connection.
 * @return true if WiFi is connected and a Telnet client is established.
 */
bool CTelnet::isConnected ()
{
	return WiFi.status() == WL_CONNECTED ? true : false;
}

/**
 * @brief Explicit bool conversion operator — returns true when connected.
 */
CTelnet::operator bool ()
{
	return isConnected();
}

/**
 * @brief Starts the Telnet server listening on the specified TCP port.
 * @param port TCP port number (truncated to 16 bits).
 */
void CTelnet::begin ( uint32_t port )
{
	m_telnetPort = port & 0xffff;
	m_pmyServer = new WiFiServer ( m_telnetPort );
	m_pmyServer->begin();
}

size_t CTelnet::write ( uint8_t c )
{
	size_t result = 0;
	if ( m_myClient.connected() )
	{
		size_t result = m_myClient.write ( c );
		if ( result <= 0 )
		{
			m_myClient.stop();
			m_bClientConnected = false;
			// Serial.println ( "Client disconnected" );
		}
	}
	else
	{
		if ( m_pmyServer != nullptr )
		{
			m_myClient = m_pmyServer->available();
			if ( m_myClient )
			{
				// Serial.println ( "Client connected" );
				m_bClientConnected = true;
			}
		}
	}
	return result;
}

/**
 * @brief Registers a callback to be invoked when a new Telnet client connects.
 * @param pConnectCallback Pointer to the callback; receives `this` as a void* Logger
 *                         pointer so the handler can send initial terminal sequences.
 */
void CTelnet::SetConnectCallback ( voidFuncPtrParam pConnectCallback )
{
	m_ConnectCallback = pConnectCallback;
}

/**
 * @brief Fires the registered client-connect callback with `this` as the Logger pointer.
 */
void CTelnet::DoConnect ()
{
	if ( m_ConnectCallback != nullptr )
	{
		m_ConnectCallback ( this );
	}
}

size_t CTelnet::write ( const uint8_t* buffer, size_t size )
{
	size_t result = 0;
	if ( m_myClient.connected() && size > 0 )
	{
		size_t result = m_myClient.write ( (char*)buffer, size );
		if ( result <= 0 )
		{
			m_myClient.stop();
			m_bClientConnected = false;
			// Serial.println ( "Client disconnected" );
		}
	}
	else
	{
		if ( m_pmyServer != nullptr )
		{
			m_myClient = m_pmyServer->available();
			if ( m_myClient )
			{
				// Serial.println ( "Client connected" );
				m_bClientConnected = true;
				DoConnect();
			}
		}
	}
	return result;
}

size_t CTelnet::write ( String Msg )
{
	size_t Result = 0;
	if ( m_myClient.connected() && Msg.length() > 0 )
	{
		Result = m_myClient.print ( Msg );
		if ( Result <= 0 )
		{
			m_myClient.stop();
			m_bClientConnected = false;
			// Serial.println ( "Client disconnected" );
		}
	}
	else
	{
		if ( m_pmyServer != nullptr )
		{
			m_myClient = m_pmyServer->available();
			if ( m_myClient )
			{
				// Serial.println ( "Client connected" );
				m_bClientConnected = true;
				DoConnect();
			}
		}
	}
	return Result;
}

/**
 * @brief Returns the number of bytes available to read from the connected Telnet client.
 * @return Byte count from the active WiFiClient, or 0 if not connected.
 */
int CTelnet::available ()
{
	return m_myClient.available();
}

/**
 * @brief Reads and returns the next byte from the connected Telnet client.
 * @return Next byte, or -1 if no data is available.
 */
int CTelnet::read ()
{
	return m_myClient.read();
}

/**
 * @brief Returns the next byte from the Telnet client without consuming it.
 * @return Next byte, or -1 if no data is available.
 */
int CTelnet::peek ()
{
	return m_myClient.peek();
}

/**
 * @brief Starts the Telnet server using the default port. Delegates to begin(uint32_t).
 */
void CTelnet::LogStart ()
{
	begin();
}

/**
 * @brief Returns whether this transport can detect client connections.
 * @return Always true for CTelnet (TCP accept events are observable).
 */
bool CTelnet::CanDetectClientConnect ()
{
	return true;
}

CTelnet Telnet;
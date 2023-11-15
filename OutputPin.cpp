#include "OutputPin.h"

OutputPin::OutputPin ( pin_size_t Pin, PinStatus OnStatus ) : m_Pin ( Pin ), m_OnStatus ( OnStatus )
{
	pinMode ( m_Pin, OUTPUT );
	delay ( 10 );
	Off();
}

/// @brief Sets the pin to the ON value provided in the constructor
void OutputPin::On ()
{
	Write ( m_OnStatus ==  HIGH ? HIGH : LOW );	// Set to ON
}

/// @brief Sets the pin the logical NOT of the ON value provided in the constructor
void OutputPin::Off ()
{
	Write ( m_OnStatus ==  HIGH ? LOW : HIGH );	// Set to OFF
}

/// @brief Turns the pin ON, waits the millisec provided and turns the pin OFF
/// @param milliSec 
void OutputPin::Pulse ( uint32_t milliSec )
{
	On();
	delay ( milliSec );
	Off();
}

inline PinStatus OutputPin::Status ()
{
	return m_pinStatus;
}

pin_size_t OutputPin::pinNumber ()
{
	return m_Pin;
}

void OutputPin::Write ( PinStatus status )
{
	m_pinStatus = status;
	digitalWrite ( m_Pin, (int)m_pinStatus );
	m_ulTimeWritten = millis();
}

uint32_t  OutputPin::GetTimeWritten()
{
	return m_ulTimeWritten;
}

#include "OutputPin.h"

/**
 * @brief Constructs the output pin, configures it as an output, and drives it to the OFF state.
 * @param Pin      Arduino pin number to control.
 * @param OnStatus The PinStatus value (HIGH or LOW) that represents the logical ON state.
 *                 The constructor sets the pin to the inverse (OFF) immediately.
 */
OutputPin::OutputPin ( pin_size_t Pin, PinStatus OnStatus )
    : m_Pin ( Pin ), m_OnStatus ( OnStatus ), m_pinStatus ( LOW )
{
	pinMode ( m_Pin, OUTPUT );
	delay ( 10 );
	Off();
}

/// @brief Sets the pin to the ON value provided in the constructor
void OutputPin::On ()
{
	Write ( m_OnStatus == HIGH ? HIGH : LOW );  // Set to ON
}

/// @brief Sets the pin the logical NOT of the ON value provided in the constructor
void OutputPin::Off ()
{
	Write ( m_OnStatus == HIGH ? LOW : HIGH );  // Set to OFF
}

/// @brief Turns the pin ON, waits the millisec provided and turns the pin OFF
/// @param milliSec
void OutputPin::Pulse ( uint32_t milliSec )
{
	On();
	delay ( milliSec );
	Off();
}

/**
 * @brief Returns the last PinStatus written to the pin.
 * @return HIGH or LOW as last set by On(), Off(), or Write().
 */
inline PinStatus OutputPin::Status () const
{
	return m_pinStatus;
}

/**
 * @brief Returns the Arduino pin number managed by this object.
 * @return The pin number supplied to the constructor.
 */
pin_size_t OutputPin::pinNumber () const
{
	return m_Pin;
}

/**
 * @brief Writes a PinStatus directly to the hardware pin and records the time of the write.
 * @param status The PinStatus value to apply (HIGH or LOW).
 */
void OutputPin::Write ( PinStatus status )
{
	m_pinStatus = status;
	digitalWrite ( m_Pin, (int)m_pinStatus );
	m_ulTimeWritten = millis();
}

/**
 * @brief Returns the millis() timestamp of the most recent Write() call.
 * @return Time in milliseconds since boot at which the pin was last driven.
 */
uint32_t OutputPin::GetTimeWritten () const
{
	return m_ulTimeWritten;
}

#include "InputPin.h"

#include "Winterrupts_fixed.h"
/*
    InputPin.cpp - implemntation of InputPin.h

*/
/// @brief static method to call the instance method to process the ISR, this is required as the ISR cannot be a member
/// function and is called for any instance of the class
void InputPin::InputPinISR ( void* pParam )
{
	// get pointer to the instance of the InputPin object that was created for the pin
	// InputPin *pInputPinObj = (InputPin *)pParam;
	InputPin* pInputPinObj = static_cast<InputPin*> ( pParam );
	// Invoke instance method to process the ISR for the specific pin
	pInputPinObj->ProcessISR();
}
/// @brief Initialises the class with the pin to monitor, the debounce time, the maximum time the pin should be in the
/// matched state to be considered a real signal
/// @param pin must be a valid pin number that supports interrupts
/// @param debouncems minimum time between consecutive pin interrupts before signal accepted
/// @param maxMatchedTimems maximum time pin should be in matched state to be considered a real signal
/// @param matchStatus  the status that the pin should be in to be considered a match ie HIGH or LOW
/// @param mode  the mode of the pin INPUT, INPUT_PULLUP, INPUT_PULLDOWN
/// @param status  the type of interrupt to trigger on CHANGE, RISING, FALLING
InputPin::InputPin ( pin_size_t pin,
                     uint32_t debouncems,
                     uint32_t maxMatchedTimems,
                     PinStatus matchStatus,
                     PinMode mode,
                     PinStatus status )
    : m_Pin ( pin ), m_Debouncems ( debouncems ), m_maxMatchedTimems ( maxMatchedTimems ), m_MatchStatus ( matchStatus )
{
	pinMode ( m_Pin, mode );
	delay ( 10 );  // allow time before first read after setting mode
	m_LastChangedTime = millis();
	m_LastPinRead = digitalRead ( m_Pin );
	m_CurrentMatchedState = m_LastPinRead == m_MatchStatus ? true : false;
	attachInterruptParam ( digitalPinToInterrupt ( m_Pin ), InputPinISR, status, this );
}

/// @brief Destructor to detach the interrupt
InputPin::~InputPin ()
{
	detachInterrupt ( digitalPinToInterrupt ( m_Pin ) );
}

/// @brief Handles interrupts specifically for the pin being monitored
/// @param
void InputPin::ProcessISR ( void )
{
	PinStatus newReading = digitalRead ( m_Pin );

	m_ISRCalledCount++;
	if ( newReading != m_LastPinRead )
	{
		// different reading from last time, so check it
		uint32_t ulNow = millis();
		if ( newReading == m_MatchStatus )
		{
			if ( ulNow - m_LastChangedTime >= m_Debouncems )
			{
				// Wanted state
				m_MatchedCount++;
				m_CurrentMatchedState = true;
				// m_MatchedDuration	  = ulNow - m_LastChangedTime;
				MatchAction();
			}
			else
			{
				// changed state to wanted state but too quickly, ignore as a spurious chnage
				m_SpuriousCount++;
			}
		}
		else
		{
			// Unwanted state ie Not matched state
			if ( ulNow - m_LastChangedTime >= m_Debouncems )
			{
				// change back to not matched state, happened after debounce time threshold
				m_CurrentMatchedState = false;
				if ( m_maxMatchedTimems == 0 || ulNow - m_LastChangedTime < m_maxMatchedTimems )
				{
					// change back to unmatched state happened within maximum time allowed in matched state or no
					// maximum time set
					m_UnmatchedCount++;
					// calculate duration in matched state
					m_MatchedDuration = ulNow - m_LastChangedTime;
					UnmatchAction();
				}
				else
				{
					// change back to unmatched state happened but outside of maximum time allowed in matched state
					m_SpuriousCount++;
				}
			}
			else
			{
				// changed state but too quickly, ignore as a spurious change
				m_SpuriousCount++;
			}
		}
		m_LastChangedTime = ulNow;
		m_LastPinRead = newReading;
	}
	else
	{
		// repeat value
		m_DiscardedUnchangedCount++;
	}
}

/**
 * @brief Returns whether the pin is currently in the matched (active) state.
 * @return true if the last accepted interrupt left the pin in the matched state.
 */
bool InputPin::IsMatched () const
{
	return m_CurrentMatchedState;
}

/**
 * @brief Returns the cumulative count of accepted transitions to the matched state.
 * @return Number of debounced rising (match) transitions since construction.
 */
uint32_t InputPin::GetMatchedCount () const
{
	return m_MatchedCount;
}

/**
 * @brief Returns the cumulative count of accepted transitions from matched back to unmatched.
 * @return Number of debounced falling (unmatch) transitions since construction.
 */
uint32_t InputPin::GetUnmatchedCount () const
{
	return m_UnmatchedCount;
}

/**
 * @brief Returns the total number of times the ISR has been invoked for this pin.
 * @return Raw interrupt invocation count, including spurious and unchanged readings.
 */
uint32_t InputPin::GetInvokedCount () const
{
	return m_ISRCalledCount;
}

/**
 * @brief Returns the count of ISR calls that were rejected as spurious (debounce violations
 *        or transitions that exceeded the maximum matched-state duration).
 * @return Number of rejected pin change events since construction.
 */
uint32_t InputPin::GetSpuriousCount () const
{
	return m_SpuriousCount;
}

/**
 * @brief Returns the count of ISR calls discarded because the pin reading was
 *        the same as the previous reading (no actual edge).
 * @return Number of unchanged-value ISR invocations since construction.
 */
uint32_t InputPin::GetDiscardUnchangedCount () const
{
	return m_DiscardedUnchangedCount;
}

/**
 * @brief Returns the duration in milliseconds of the most recent valid matched-state period.
 * @return Time from the last accepted match transition until the subsequent accepted
 *         unmatch transition. 0 if no complete matched period has occurred yet.
 */
uint32_t InputPin::GetLastMatchedDuration () const
{
	return m_MatchedDuration;
}

/**
 * @brief Reads the actual current hardware state of the pin and returns whether
 *        it matches the configured match state.
 * @details Unlike IsMatched(), this performs a live digitalRead() rather than
 *          returning the cached interrupt-driven state.
 * @return true if the pin is currently in the matched state.
 */
bool InputPin::GetCurrentMatchedState () const
{
	return digitalRead ( m_Pin ) == m_MatchStatus;
}

/**
 * @brief Formats all interrupt diagnostic counters into a fixed-width column string.
 * @details Outputs: ISR calls, discarded-unchanged, matched, unmatched, spurious,
 *          and last-matched-duration — each in an 8-character column.
 * @param result Output String that receives the formatted counter values.
 */
void InputPin::DebugStats ( String& result ) const
{
	char cMsg [ 50 ];
	sprintf ( cMsg,
	          "%8ld%8ld%8ld%8ld%8ld %8ld",
	          m_ISRCalledCount,
	          m_DiscardedUnchangedCount,
	          m_MatchedCount,
	          m_UnmatchedCount,
	          m_SpuriousCount,
	          m_MatchedDuration );
	result = cMsg;
}
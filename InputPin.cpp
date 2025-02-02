#include "InputPin.h"

/*
	InputPin.cpp - implemntation of InputPin.h

*/
/// @brief static method to call the instance method to process the ISR, this is required as the ISR cannot be a member function and is called for any instance of the class
void InputPin::InputPinISR ( void *pParam )
{
	// get pointer to the instance of the InputPin object that was created for the pin
	//InputPin *pInputPinObj = (InputPin *)pParam;
	InputPin* pInputPinObj = static_cast<InputPin*>(pParam);
	// Invoke instance method to process the ISR for the specific pin
	pInputPinObj->ProcessISR ();
}
/// @brief Initialises the class with the pin to monitor, the debounce time, the maximum time the pin should be in the matched state to be considered a real signal
/// @param pin must be a valid pin number that supports interrupts
/// @param debouncems minimum time between consecutive pin interrupts before signal accepted
/// @param maxMatchedTimems maximum time pin should be in matched state to be considered a real signal
/// @param matchStatus  the status that the pin should be in to be considered a match ie HIGH or LOW
/// @param mode  the mode of the pin INPUT, INPUT_PULLUP, INPUT_PULLDOWN
/// @param status  the type of interrupt to trigger on CHANGE, RISING, FALLING
InputPin::InputPin ( pin_size_t pin, uint32_t debouncems, uint32_t maxMatchedTimems, PinStatus matchStatus, PinMode mode, PinStatus status ) 
		: m_Pin ( pin ), m_Debouncems ( debouncems ), m_maxMatchedTimems ( maxMatchedTimems ), m_MatchStatus ( matchStatus )
{
	pinMode ( m_Pin, mode );
	delay ( 10 ); // allow time before first read after setting mode
	m_LastChangedTime	  = millis ();
	m_LastPinRead		  = digitalRead ( m_Pin );
	m_CurrentMatchedState = m_LastPinRead == m_MatchStatus ? true : false;
	attachInterruptParam ( digitalPinToInterrupt ( m_Pin ), InputPinISR, status, this );
}

/// @brief Destructor to detach the interrupt
InputPin::~InputPin() 
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
		uint32_t ulNow = millis ();
		if ( newReading == m_MatchStatus )
		{
			if ( ulNow - m_LastChangedTime >= m_Debouncems )
			{
				// Wanted state
				m_MatchedCount++;
				m_CurrentMatchedState = true;
				// m_MatchedDuration	  = ulNow - m_LastChangedTime;
				MatchAction ();
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
					// change back to unmatched state happened within maximum time allowed in matched state or no maximum time set
					m_UnmatchedCount++;
					// calculate duration in matched state
					m_MatchedDuration	  = ulNow - m_LastChangedTime;
					UnmatchAction ();
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
		m_LastPinRead	  = newReading;
	}
	else
	{
		// repeat value
		m_DiscardedUnchangedCount++;
	}
}

bool InputPin::IsMatched () const
{
	return m_CurrentMatchedState;
}

uint32_t InputPin::GetMatchedCount () const
{
	return m_MatchedCount;
}

uint32_t InputPin::GetUnmatchedCount () const
{
	return m_UnmatchedCount;
}

uint32_t InputPin::GetInvokedCount () const
{
	return m_ISRCalledCount;
}

uint32_t InputPin::GetSpuriousCount () const
{
	return m_SpuriousCount;
}

uint32_t InputPin::GetDiscardUnchangedCount () const
{
	return m_DiscardedUnchangedCount;
}

uint32_t InputPin::GetLastMatchedDuration () const
{
	return m_MatchedDuration;
}

bool InputPin::GetCurrentMatchedState () const
{
	return digitalRead ( m_Pin ) == m_MatchStatus;
}

void InputPin::DebugStats ( String &result ) const
{
	char cMsg [ 50 ];
	sprintf ( cMsg, "%8ld%8ld%8ld%8ld%8ld %8ld", m_ISRCalledCount, m_DiscardedUnchangedCount, m_MatchedCount, m_UnmatchedCount, m_SpuriousCount, m_MatchedDuration );
	result = cMsg;
}
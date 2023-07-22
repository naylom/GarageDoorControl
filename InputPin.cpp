#include "InputPin.h"

/*
	InputPin.cpp - implemntation of InputPin.h

*/
void InputPinISR ( void *pParam )
{
	InputPin *pInputPinObj = (InputPin *)pParam;
	pInputPinObj->ProcessISR ();
}

InputPin::InputPin ( pin_size_t pin, uint32_t debouncems, PinStatus matchStatus, PinMode mode, PinStatus status ) : m_Pin ( pin ), m_Debouncems ( debouncems ), m_MatchStatus ( matchStatus )
{
	pinMode ( m_Pin, mode );
	m_LastPinRead		  = digitalRead ( m_Pin );
	m_CurrentMatchedState = m_LastPinRead == m_MatchStatus ? true : false;
	m_LastChangedTime	  = millis ();

	attachInterruptParam ( digitalPinToInterrupt ( m_Pin ), InputPinISR, status, this );
}

void InputPin::ProcessISR ( void )
{
	PinStatus	  newReading = digitalRead ( m_Pin );

	m_ISRCalledCount++;
	if ( newReading != m_LastPinRead )
	{
		// different reading from last time, so check it
		unsigned long ulNow		 = millis ();
		if ( newReading == m_MatchStatus )
		{
			if ( ulNow - m_LastChangedTime >= m_Debouncems && ulNow - m_LastChangedTime < 1000UL )
			{
				// Wanted state
				m_MatchedCount++;
				m_CurrentMatchedState = true;
				m_MatchedDuration = ulNow - m_LastChangedTime;
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
			m_UnmatchedCount++;
			m_CurrentMatchedState = false;
			UnmatchAction ();
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
/*
void InputPin::ProcessISR ( void )
{
	unsigned long ulNow = millis ();

	m_ISRCalledCount++;
	PinStatus newReading = digitalRead ( m_Pin );
	if ( newReading != m_LastPinRead )
	{
		// pin has changed state 
		m_LastChangedTime = ulNow;
		m_LastPinRead	  = newReading;
		// Check if this change lasted long enough
		if ( ulNow - m_LastChangedTime > m_Debouncems )
		{
			m_AfterDebounceCount++;
			if ( newReading == m_MatchStatus )
			{
				m_MatchedCount++;
				m_CurrentMatchedState = true;
				MatchAction ();
			}
			else
			{
				m_UnmatchedCount++;
				m_CurrentMatchedState = false;
				UnmatchAction ();
			}
		}
		else
		{
			// discard as spurious change
			m_ToQuick++;
		}
	}
	else
	{
		// Reading unchanged
		m_DiscardedUnchangedCount++;
	}
/*
	if ( ( ulNow - m_LastChangedTime ) > m_Debouncems )
	{
		// ISR fired and debouce threshold has passed
		m_AfterDebounceCount++;
		PinStatus newReading = digitalRead ( m_Pin );
		if ( newReading != m_LastPinRead )
		{
			m_LastChangedTime = ulNow;
			m_LastPinRead	  = newReading;
			if ( newReading == m_MatchStatus )
			{
				m_MatchedCount++;
				m_CurrentMatchedState = true;
				MatchAction ();
			}
			else
			{
				m_UnmatchedCount++;
				m_CurrentMatchedState = false;
				UnmatchAction ();
			}
		}
		else
		{
			// ISR fired after debounce threshold but reading unchanged
			m_DiscardedUnchangedCount++;
		}
	}
*/	
}
*/
bool InputPin::IsMatched ()
{
	return m_CurrentMatchedState;
}

uint32_t InputPin::GetMatchedCount ()
{
	return m_MatchedCount;
}

uint32_t InputPin::GetUnmatchedCount ()
{
	return m_UnmatchedCount;
}

uint32_t InputPin::GetInvokedCount ()
{
	return m_ISRCalledCount;
}

uint32_t InputPin::GetSpuriousCount ()
{
	return m_SpuriousCount;
}

uint32_t InputPin::GetDiscardUnchangedCount ()
{
	return m_DiscardedUnchangedCount;
}

uint32_t InputPin::GetLastMatchedDuration ()
{
	return m_MatchedDuration;
}

void InputPin::DebugStats ( String &result )
{
	result	= String ( m_ISRCalledCount ) + " : ";
	result += String ( m_DiscardedUnchangedCount ) + " : ";
	result += String ( m_MatchedCount ) + " : ";
	result += String ( m_SpuriousCount ) + " : ";	
	result += String ( m_UnmatchedCount ) + " : ";
	result += String ( m_MatchedDuration );
}
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
	unsigned long ulNow = millis ();

	m_ISRCalledCount++;
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
}

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

uint32_t InputPin::GetPostDebounceCount ()
{
	return m_AfterDebounceCount;
}

uint32_t InputPin::GetDiscardUnchangedCount ()
{
	return m_DiscardedUnchangedCount;
}

void InputPin::DebugStats ( String &result )
{
	result = String ( m_ISRCalledCount) + " : ";
	result += String ( m_AfterDebounceCount ) + " : ";
	result += String ( m_DiscardedUnchangedCount ) + " : ";
	result += String ( m_MatchedCount ) + " : ";
	result += String ( m_UnmatchedCount );
}
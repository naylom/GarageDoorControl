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
	delay ( 10 ); // allow time before first read after setting mode
	m_LastChangedTime	  = millis ();
	m_LastPinRead		  = digitalRead ( m_Pin );
	m_CurrentMatchedState = m_LastPinRead == m_MatchStatus ? true : false;
	attachInterruptParam ( digitalPinToInterrupt ( m_Pin ), InputPinISR, status, this );
}

void InputPin::ProcessISR ( void )
{
	PinStatus newReading = digitalRead ( m_Pin );

	m_ISRCalledCount++;
	if ( newReading != m_LastPinRead )
	{
		// different reading from last time, so check it
		unsigned long ulNow = millis ();
		if ( newReading == m_MatchStatus )
		{
			if ( ulNow - m_LastChangedTime >= m_Debouncems /* && ulNow - m_LastChangedTime < 1000UL */ )
			{
				// Wanted state
				m_MatchedCount++;
				m_CurrentMatchedState = true;
				m_MatchedDuration	  = ulNow - m_LastChangedTime;
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
			if ( ulNow - m_LastChangedTime >= m_Debouncems /* && ulNow - m_LastChangedTime < 1000UL */ )
			{
				m_UnmatchedCount++;
				m_CurrentMatchedState = false;
				m_MatchedDuration	  = ulNow - m_LastChangedTime;
				UnmatchAction ();
			}
			else
			{
				// changed state to wanted state but too quickly, ignore as a spurious chnage
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

bool InputPin::GetCurrentMatchedState ()
{
	return digitalRead ( m_Pin ) == m_MatchStatus;
}

void InputPin::DebugStats ( String &result )
{
	char cMsg [ 50 ];
	result = sprintf ( cMsg, "%8ld%8ld%8ld%8ld%8ld %8ld", m_ISRCalledCount, m_DiscardedUnchangedCount, m_MatchedCount, m_UnmatchedCount, m_SpuriousCount, m_MatchedDuration );
	result = cMsg;
}
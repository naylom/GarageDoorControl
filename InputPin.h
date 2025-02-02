#pragma once
#include <Arduino.h>

/*
	InputPin.h - class to encapusulate an input pin with interrupt handling

*/
class InputPin
{
	public:
		InputPin ( pin_size_t pin, uint32_t debouncems, uint32_t maxMatchedTimems, PinStatus matchStatus, PinMode mode = PinMode::INPUT, PinStatus status = PinStatus::CHANGE );
		~InputPin();

		bool	 IsMatched () const;
		uint32_t GetMatchedCount () const;
		uint32_t GetUnmatchedCount () const;
		uint32_t GetInvokedCount () const;
		uint32_t GetSpuriousCount () const;
		uint32_t GetDiscardUnchangedCount () const;
		uint32_t GetLastMatchedDuration() const;
		void	 DebugStats ( String &result ) const;
		bool	 GetCurrentMatchedState() const ;

	private:
		void	 		   ProcessISR ();
		virtual void	   MatchAction ()	= 0;
		virtual void	   UnmatchAction () = 0;
		static void		   InputPinISR ( void *pParam );

		const pin_size_t   m_Pin;
		const uint32_t	   m_Debouncems;
		const uint32_t	   m_maxMatchedTimems;
		const PinStatus	   m_MatchStatus;
		volatile PinStatus m_LastPinRead;
		volatile uint32_t  m_LastChangedTime;
		volatile bool	   m_CurrentMatchedState	 = false;

		// stat counters
		volatile uint32_t  m_ISRCalledCount			 = 0UL;
		volatile uint32_t  m_DiscardedUnchangedCount = 0UL;
		volatile uint32_t  m_MatchedCount			 = 0UL;
		volatile uint32_t  m_UnmatchedCount			 = 0UL;
		volatile uint32_t  m_SpuriousCount			 = 0UL;
		volatile uint32_t  m_MatchedDuration		 = 0UL;
};
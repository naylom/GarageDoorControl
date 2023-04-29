#pragma once
#include <Arduino.h>

/*
	OutputPin.h

	Class to manipulate a digital output pin

*/
class OutputPin
{
	public:
		OutputPin ( pin_size_t Pin, PinStatus OnStatus );
		void	  On ();
		void	  Off ();
		void	  Pulse ( uint32_t milliSec );
		PinStatus Status ();
		pin_size_t pinNumber();

	private:
		const pin_size_t m_Pin;
		const PinStatus	 m_OnStatus;
		PinStatus		 m_pinStatus;		// last status written to pin

		void			 Write ( PinStatus status );
};
#pragma once

#include <WiFiNINA.h>

class FixedIPList
{
	public:
		FixedIPList ( uint8_t MaxEntries );
		bool	  Add ( IPAddress addition );
		uint8_t	  GetIterator ();
		IPAddress GetNext ( uint8_t &iterator );
		uint8_t	  Count ();

	private:
		bool IsPresent ( IPAddress addr );

	private:
		const uint8_t m_maxEntries;
		uint8_t		  m_nextEntry = 0;
		IPAddress	 *m_pIPList;
};
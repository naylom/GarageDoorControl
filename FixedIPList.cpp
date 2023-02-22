#include "FixedIPList.h"
#include "Logging.h"
IPAddress EmptyAddress = IPAddress ( 0UL );

FixedIPList::FixedIPList ( uint8_t MaxEntries )
{
	m_maxEntries = MaxEntries;
	m_pIPList	 = new IPAddress [ m_maxEntries ];
	for ( uint8_t i = 0; i < m_maxEntries; i++ )
	{
		m_pIPList [ i ] = EmptyAddress;
	}
}

bool FixedIPList::Add ( IPAddress addition )
{
	bool bResult = false;
	if ( !IsPresent ( addition ) )
	{
		if ( m_nextEntry >= m_maxEntries )
		{
			// list full, so discard oldest
			for ( uint8_t i = 0; i < m_maxEntries - 1; i++ )
			{
				m_pIPList [ i ] = m_pIPList [ i + 1 ];
			}
			m_pIPList [ m_maxEntries - 1 ] = EmptyAddress;
			m_nextEntry					   = m_maxEntries - 1;
		}
		// Error ( "Adding macast entry" );
		m_pIPList [ m_nextEntry++ ] = addition;
	}
	return bResult;
}

uint8_t FixedIPList::Count ()
{
	return m_nextEntry;
}

uint8_t FixedIPList::GetIterator ()
{
	return 0;
}

IPAddress FixedIPList::GetNext ( uint8_t &iterator )
{
	IPAddress Result = EmptyAddress;
	if ( iterator < m_nextEntry )
	{
		Result = m_pIPList [ iterator++ ];
	}
	return Result;
}

bool FixedIPList::IsPresent ( IPAddress addr )
{
	bool bResult = false;
	for ( uint8_t i = 0; i < m_maxEntries; i++ )
	{
		if ( m_pIPList [ i ] == addr )
		{
			bResult = true;
			break;
		}
	}
	return bResult;
}

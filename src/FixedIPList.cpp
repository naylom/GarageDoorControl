#include "FixedIPList.h"

#include "Logging.h"
IPAddress EmptyAddress = IPAddress ( 0UL );

/**
 * @brief Constructs a fixed-capacity list for storing IPv4 broadcast/multicast addresses.
 * @details Allocates an array of `MaxEntries` IPAddress objects on the heap, all
 *          initialised to 0.0.0.0. When the list is full the oldest entry is
 *          silently evicted to make room for new ones.
 * @param MaxEntries Maximum number of IP addresses the list can hold at any time.
 */
FixedIPList::FixedIPList ( uint8_t MaxEntries ) : m_maxEntries ( MaxEntries )
{
	m_pIPList = new IPAddress [ m_maxEntries ];
	for ( uint8_t i = 0; i < m_maxEntries; i++ )
	{
		m_pIPList [ i ] = EmptyAddress;
	}
}

/**
 * @brief Adds an IP address to the list if it is not already present.
 * @details If the list is full the oldest entry (index 0) is discarded and all
 *          remaining entries are shifted down before the new address is appended.
 * @param addition The IP address to add.
 * @return Always returns false (return value reserved for future use).
 */
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
			m_nextEntry = m_maxEntries - 1;
		}
		// Error ( "Adding macast entry" );
		m_pIPList [ m_nextEntry++ ] = addition;
	}
	return bResult;
}

/**
 * @brief Returns the number of IP addresses currently stored in the list.
 * @return Count of entries in the range [0, MaxEntries].
 */
uint8_t FixedIPList::Count ()
{
	return m_nextEntry;
}

/**
 * @brief Returns an iterator initialised to the first entry in the list.
 * @details Pass the returned value to GetNext() to iterate all stored addresses.
 * @return An iterator value (currently always 0).
 */
uint8_t FixedIPList::GetIterator ()
{
	return 0;
}

/**
 * @brief Returns the next IP address in the list and advances the iterator.
 * @param iterator In/out: iterator position. Pass the value from GetIterator();
 *                 the function increments it on each call.
 * @return The IP address at the current iterator position, or 0.0.0.0 when
 *         there are no more entries.
 */
IPAddress FixedIPList::GetNext ( uint8_t& iterator )
{
	if ( iterator < m_nextEntry )
	{
		return m_pIPList [ iterator++ ];
	}
	return EmptyAddress;
}

/**
 * @brief Checks whether a given IP address is already stored in the list.
 * @param addr The IP address to search for.
 * @return true if `addr` is found in the list; false otherwise.
 */
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

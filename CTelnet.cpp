#include "CTelnet.h"

bool CTelnet::isConnected ()
{
	return WiFi.status () == WL_CONNECTED ? true : false;
}

CTelnet::operator bool ()
{
	return isConnected ();
}

void CTelnet::begin ( uint32_t port )
{
	m_telnetPort = port & 0xffff;
	m_pmyServer	 = new WiFiServer ( port & 0xffff );
	m_pmyServer->begin ();
}

bool CTelnet::Send ( String Msg )
{
	bool bResult = false;
	if ( m_myClient )
	{
		if ( m_myClient.print ( Msg ) <= 0 )
		{
			m_myClient.stop ();
			m_bClientConnected = false;
		}
	}
	else
	{
		m_myClient = m_pmyServer->available ();
	}
	return bResult;
}

void CTelnet::print ( char c )
{
	char buf [ 2 ];
	buf [ 0 ] = c;
	buf [ 1 ] = 0;
	Send ( buf );
}

void CTelnet::print ( char *s )
{
	Send ( s );
}

void CTelnet::print ( const char *s )
{
	Send ( s );
}

void CTelnet::print ( int32_t s )
{
	Send ( String ( s ) );
}

void CTelnet::print ( int16_t s )
{
	Send ( String ( s ) );
}

void CTelnet::print ( const arduino::__FlashStringHelper *s )
{
	Send ( String ( s ) );
}

void CTelnet::print ( String &s )
{
	Send ( s );
}

void CTelnet::println ( char *s )
{
	Send ( String ( s ) + "\n" );
}

CTelnet Telnet;

#include "OnboardingServer.h"

#include "Display.h"
#include "Logging.h"

namespace
{
constexpr uint16_t DEFAULT_UDP_PORT = 0xFEED;
constexpr uint16_t DEFAULT_MULTICAST_PORT = 0xCE5C;
constexpr float DEFAULT_ALTITUDE_COMP = 131.0f;
constexpr char DEFAULT_HOSTNAME [] = "GarageControl";
}  // namespace

OnboardingServer::OnboardingServer ( uint16_t port ) : OnboardingServerBase ( port )
{
	memset ( &_config, 0, sizeof ( _config ) );
	_config.udpPort = DEFAULT_UDP_PORT;
	_config.multicastPort = DEFAULT_MULTICAST_PORT;
	_config.altitudeCompensation = DEFAULT_ALTITUDE_COMP;
	strncpy ( _config.hostname, DEFAULT_HOSTNAME, sizeof ( _config.hostname ) - 1 );
	_config.hostname [ sizeof ( _config.hostname ) - 1 ] = '\0';
}

String OnboardingServer::getFormTitle () const
{
	return "Garage Control Setup";
}

String OnboardingServer::getAdditionalFields () const
{
	String fields;
	fields += "Device Hostname: <input name=\"hostname\" value=\"";
	fields += String ( _config.hostname );
	fields += "\"><br>";
	fields += "UDP Receive Port: <input name=\"udpPort\" value=\"";
	fields += String ( _config.udpPort );
	fields += "\"><br>";
	fields += "Multicast Send Port: <input name=\"multicastPort\" value=\"";
	fields += String ( _config.multicastPort );
	fields += "\"><br>";
#ifdef BME280_SUPPORT
	fields += "Altitude Compensation (m): <input name=\"altitude\" value=\"";
	fields += String ( _config.altitudeCompensation, 1 );
	fields += "\" step=\"0.1\"><br>";
#endif
	return fields;
}

String OnboardingServer::getAdditionalValidation () const
{
	String validation;
	validation += "var host=form.hostname.value.trim();";
	validation += "if(host.length===0){document.getElementById('error').innerHTML='Hostname required';return false;}";
	validation +=
	    "if(host.length>31){document.getElementById('error').innerHTML='Hostname max length 31';return false;}";
	validation += "var udpStr=form.udpPort.value.trim();";
	validation += "var udp=Number(udpStr);";
	validation += "if(!Number.isInteger(udp)||udp<1024||udp>65535){document.getElementById('error').innerHTML='UDP "
	              "port must be 1024-65535';return false;}";
	validation += "var mcastStr=form.multicastPort.value.trim();";
	validation += "var mcast=Number(mcastStr);";
	validation += "if(!Number.isInteger(mcast)||mcast<1024||mcast>65535){document.getElementById('error').innerHTML='"
	              "Multicast port must be 1024-65535';return false;}";
	return validation;
}

bool OnboardingServer::parseAdditionalFields ( const String& body )
{
	String hostname = extractField ( body, "hostname", "udpPort" );
	String udpPort = extractField ( body, "udpPort", "multicastPort" );
	String multicastPort = extractField ( body, "multicastPort" );
#ifdef BME280_SUPPORT
	String altitude = extractField ( body, "altitude" );
#endif

	if ( hostname.length() > 0 )
	{
		hostname.toCharArray ( _config.hostname, sizeof ( _config.hostname ) - 1 );
		_config.hostname [ sizeof ( _config.hostname ) - 1 ] = '\0';
	}
	else
	{
		strncpy ( _config.hostname, DEFAULT_HOSTNAME, sizeof ( _config.hostname ) - 1 );
		_config.hostname [ sizeof ( _config.hostname ) - 1 ] = '\0';
	}

	if ( udpPort.length() > 0 )
	{
		uint16_t parsed = (uint16_t)udpPort.toInt();
		if ( parsed >= 1024 && parsed <= 65535 )
		{
			_config.udpPort = parsed;
		}
		else
		{
			_config.udpPort = DEFAULT_UDP_PORT;
		}
	}

	if ( multicastPort.length() > 0 )
	{
		uint16_t parsed = (uint16_t)multicastPort.toInt();
		if ( parsed >= 1024 && parsed <= 65535 )
		{
			_config.multicastPort = parsed;
		}
		else
		{
			_config.multicastPort = DEFAULT_MULTICAST_PORT;
		}
	}

#ifdef BME280_SUPPORT
	if ( altitude.length() > 0 )
	{
		_config.altitudeCompensation = altitude.toFloat();
	}
#endif

	return true;
}

bool OnboardingServer::saveConfiguration ()
{
	strncpy ( _config.ssid, _wifiSsid, sizeof ( _config.ssid ) - 1 );
	_config.ssid [ sizeof ( _config.ssid ) - 1 ] = '\0';

	strncpy ( _config.password, _wifiPassword, sizeof ( _config.password ) - 1 );
	_config.password [ sizeof ( _config.password ) - 1 ] = '\0';

	_config.valid = true;

	Info ( "Saving config - SSID: " + String ( _config.ssid ) + ", Hostname: " + String ( _config.hostname ) +
	       ", UDP Port: " + String ( _config.udpPort ) + ", Multicast Port: " + String ( _config.multicastPort ) );

	return ConfigStorage::save ( _config );
}

String OnboardingServer::getFooterContent () const
{
	return "";
}

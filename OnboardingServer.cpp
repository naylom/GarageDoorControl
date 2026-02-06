/**
 * @file OnboardingServer.cpp
 * @brief Simple web server for onboarding WiFi and UDP port configuration.
 * @details Serves a web form to collect WiFi credentials and UDP port settings,
 *          validates input, persists the configuration, and reboots the device.
 *
 * Author: (c) M. Naylor 2025
 *
 * History:
 *     Ver 1.0        Initial version
 */

#include "OnboardingServer.h"

#include "Display.h"
#include "Logging.h"

// Default configuration values
constexpr uint16_t DEFAULT_UDP_PORT = 0xFEED;
constexpr uint16_t DEFAULT_MULTICAST_PORT = 0xCE5C;

/**
 * @brief Constructs the onboarding server listening on port 80.
 */
OnboardingServer::OnboardingServer () : _server ( 80 )
{
}

/**
 * @brief Starts the onboarding HTTP server.
 * @details Invoke after the device is in AP mode.
 */
void OnboardingServer::begin ()
{
	Info ( F ( "Starting Onboarding Web Server on port 80" ) );
	_server.begin();
}

/**
 * @brief Handles client connections and serves or processes the onboarding form.
 * @details Call repeatedly from the main loop.
 */
void OnboardingServer::loop ()
{
	WiFiClient client = _server.available();
	if ( !client )
	{
		return;
	}

	Info ( F ( "Onboarding: Client connected" ) );

	// Wait for data
	while ( client.connected() && !client.available() )
	{
		delay ( 1 );
	}

	String req = client.readStringUntil ( '\n' );

	if ( req.startsWith ( "POST" ) )
	{
		Info ( F ( "Onboarding: Handling POST request" ) );

		// Read headers to find Content-Length
		while ( client.available() )
		{
			String header = client.readStringUntil ( '\n' );
			if ( header == "\r" || header == "" )
			{
				break;
			}
		}

		// Read POST body
		String body = client.readString();

		GarageConfig cfg;
		memset ( &cfg, 0, sizeof ( cfg ) );
		cfg.udpPort = DEFAULT_UDP_PORT;
		cfg.multicastPort = DEFAULT_MULTICAST_PORT;
		cfg.altitudeCompensation = 131.0f;
		cfg.valid = true;

		parseForm ( body, cfg );

		if ( ConfigStorage::save ( cfg ) )
		{
			client.println ( "HTTP/1.1 200 OK" );
			client.println ( "Content-Type: text/html" );
			client.println();
			client.println ( "<!DOCTYPE html><html><head>" );
			client.println ( "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">" );
			client.println ( "<style>body{font-family:sans-serif;margin:2em;text-align:center;}</style>" );
			client.println ( "</head><body>" );
			client.println ( "<h2>Configuration Saved!</h2>" );
			client.println ( "<p>Device will reboot and connect to your network.</p>" );
			client.println ( "</body></html>" );
			client.stop();
			delay ( 500 );
			rebootDevice();
		}
		else
		{
			client.println ( "HTTP/1.1 500 Internal Server Error" );
			client.println ( "Content-Type: text/html" );
			client.println();
			client.println ( "<html><body>Error saving configuration!</body></html>" );
			client.stop();
		}
		return;
	}
	else
	{
		// Consume remaining headers
		while ( client.available() )
		{
			String line = client.readStringUntil ( '\n' );
			if ( line == "\r" || line == "" )
			{
				break;
			}
		}
		sendForm ( client );
		client.stop();
	}
}

/**
 * @brief Sends the onboarding HTML form to the connected client.
 * @param client Connected WiFi client.
 */
void OnboardingServer::sendForm ( WiFiClient& client )
{
	Info ( F ( "Onboarding: Sending configuration form" ) );

	client.println ( "HTTP/1.1 200 OK" );
	client.println ( "Content-Type: text/html" );
	client.println();
	client.println ( "<!DOCTYPE html><html><head>" );
	client.println ( "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">" );
	client.println ( "<style>" );
	client.println ( "body{font-family:sans-serif;margin:1.5em;background:#f5f5f5;}" );
	client.println ( "h2{color:#333;}" );
	client.println ( ".form-container{max-width:500px;margin:0 "
	                 "auto;background:white;padding:2em;border-radius:8px;box-shadow:0 2px 10px rgba(0,0,0,0.1);}" );
	client.println ( "label{display:block;margin-top:1em;font-weight:bold;color:#555;}" );
	client.println ( "input{width:100%;padding:0.5em;margin-top:0.3em;border:1px solid "
	                 "#ddd;border-radius:4px;box-sizing:border-box;}" );
	client.println ( "input[type='submit']{background:#4CAF50;color:white;border:none;cursor:pointer;margin-top:1.5em;"
	                 "font-size:1em;}" );
	client.println ( "input[type='submit']:hover{background:#45a049;}" );
	client.println ( ".info{color:#666;font-size:0.9em;margin-top:0.3em;}" );
	client.println ( "</style>" );
	client.println ( "</head><body>" );
	client.println ( "<div class='form-container'>" );
	client.println ( "<h2>Garage Control Setup</h2>" );
	client.println ( "<p>Configure your WiFi and network settings</p>" );
	client.println ( "<p class='info'><strong>Note:</strong> This setup network is open (no password required)</p>" );
	client.println ( "<form method='POST'>" );

	client.println ( "<label for='ssid'>WiFi Network (SSID):</label>" );
	client.println ( "<input type='text' id='ssid' name='ssid' required maxlength='63'>" );

	client.println ( "<label for='password'>WiFi Password:</label>" );
	client.println ( "<input type='password' id='password' name='password' maxlength='63'>" );
	client.println ( "<div class='info'>Leave blank for open networks</div>" );

	client.println ( "<label for='hostname'>Device Hostname:</label>" );
	client.println (
	    "<input type='text' id='hostname' name='hostname' value='GarageControl' required maxlength='31'>" );

	client.println ( "<label for='udpPort'>UDP Receive Port:</label>" );
	client.println ( "<input type='number' id='udpPort' name='udpPort' value='65261' min='1024' max='65535'>" );
	client.println ( "<div class='info'>Default: 65261 (0xFEED)</div>" );

	client.println ( "<label for='multicastPort'>Multicast Send Port:</label>" );
	client.println (
	    "<input type='number' id='multicastPort' name='multicastPort' value='52828' min='1024' max='65535'>" );
	client.println ( "<div class='info'>Default: 52828 (0xCE5C)</div>" );

#ifdef BME280_SUPPORT
	client.println ( "<label for='altitude'>Altitude Compensation (meters):</label>" );
	client.println (
	    "<input type='number' id='altitude' name='altitude' value='131' step='0.1' min='-500' max='9000'>" );
	client.println ( "<div class='info'>Altitude above sea level for barometric pressure compensation</div>" );
#endif

	client.println ( "<input type='submit' value='Save Configuration'>" );
	client.println ( "</form>" );
	client.println ( "</div>" );
	client.println ( "</body></html>" );
}

/**
 * @brief Parses form data and populates the configuration struct.
 * @param body POST body containing form data.
 * @param cfg Configuration struct to populate.
 */
void OnboardingServer::parseForm ( const String& body, GarageConfig& cfg )
{
	String ssid = extractFormParam ( body, "ssid" );
	String password = extractFormParam ( body, "password" );
	String hostname = extractFormParam ( body, "hostname" );
	String udpPort = extractFormParam ( body, "udpPort" );
	String multicastPort = extractFormParam ( body, "multicastPort" );
#ifdef BME280_SUPPORT
	String altitude = extractFormParam ( body, "altitude" );
#endif

	strncpy ( cfg.ssid, ssid.c_str(), sizeof ( cfg.ssid ) - 1 );
	cfg.ssid [ sizeof ( cfg.ssid ) - 1 ] = '\0';

	strncpy ( cfg.password, password.c_str(), sizeof ( cfg.password ) - 1 );
	cfg.password [ sizeof ( cfg.password ) - 1 ] = '\0';

	strncpy ( cfg.hostname, hostname.c_str(), sizeof ( cfg.hostname ) - 1 );
	cfg.hostname [ sizeof ( cfg.hostname ) - 1 ] = '\0';

	if ( udpPort.length() > 0 )
	{
		cfg.udpPort = udpPort.toInt();
		if ( cfg.udpPort < 1024 || cfg.udpPort > 65535 )
		{
			cfg.udpPort = DEFAULT_UDP_PORT;
		}
	}

	if ( multicastPort.length() > 0 )
	{
		cfg.multicastPort = multicastPort.toInt();
		if ( cfg.multicastPort < 1024 || cfg.multicastPort > 65535 )
		{
			cfg.multicastPort = DEFAULT_MULTICAST_PORT;
		}
	}

#ifdef BME280_SUPPORT
	if ( altitude.length() > 0 )
	{
		cfg.altitudeCompensation = altitude.toFloat();
	}
#endif

	Info ( "Parsed config - SSID: " + String ( cfg.ssid ) + ", Hostname: " + String ( cfg.hostname ) +
	       ", UDP Port: " + String ( cfg.udpPort ) + ", Multicast Port: " + String ( cfg.multicastPort ) );
}

/**
 * @brief Extracts a form parameter value from the POST body.
 * @param body POST body.
 * @param paramName Parameter name to extract.
 * @return Decoded parameter value.
 */
String OnboardingServer::extractFormParam ( const String& body, const String& paramName )
{
	String searchStr = paramName + "=";
	int startIdx = body.indexOf ( searchStr );
	if ( startIdx == -1 )
	{
		return "";
	}

	startIdx += searchStr.length();
	int endIdx = body.indexOf ( '&', startIdx );
	if ( endIdx == -1 )
	{
		endIdx = body.length();
	}

	String value = body.substring ( startIdx, endIdx );
	return urlDecode ( value );
}

/**
 * @brief URL-decodes a string.
 * @param s String to decode.
 * @return Decoded string.
 */
String OnboardingServer::urlDecode ( const String& s )
{
	String result = "";
	for ( unsigned int i = 0; i < s.length(); i++ )
	{
		char c = s.charAt ( i );
		if ( c == '+' )
		{
			result += ' ';
		}
		else if ( c == '%' && i + 2 < s.length() )
		{
			String hex = s.substring ( i + 1, i + 3 );
			result += (char)strtol ( hex.c_str(), NULL, 16 );
			i += 2;
		}
		else
		{
			result += c;
		}
	}
	return result;
}

/**
 * @brief Reboots the device.
 */
void OnboardingServer::rebootDevice ()
{
	Info ( F ( "Rebooting device..." ) );
	delay ( 1000 );
	MN::Utils::ResetBoard ( F ( "Configuration saved" ) );
}

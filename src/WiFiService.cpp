/*

WiFiService

Arduino class to encapsulate UDP data service over WiFi

Author: (c) M. Naylor 2022

History:
    Ver 1.0			Initial version
    Ver 2.0			Added onboarding support with BlobStorage
*/
#include "WiFiService.h"

#include "ConfigStorage.h"
#include "HormannUAP1.h"

#include <Arduino_SpiNINA.h>
#include <time.h>
#include <WiFiNINA.h>

const char* WiFiStatus [] = { "WL_IDLE_STATUS",  // = 0,
                              "WL_NO_SSID_AVAIL",
                              "WL_SCAN_COMPLETED",
                              "WL_CONNECTED",
                              "WL_CONNECT_FAILED",
                              "WL_CONNECTION_LOST",
                              "WL_DISCONNECTED",
                              "WL_AP_LISTENING",
                              "WL_AP_CONNECTED",
                              "WL_AP_FAILED",
                              "WL_NO_MODULE" };  // = 255
/*
    UDP config
*/
constexpr auto WIFI_FLASHTIME = 10;  // every 1/2 second

// Valid message parts
constexpr char cMsgVersion1 [] = "V001";        // message version
constexpr char TempHumidityReqMsg [] = "M001";  // Req temp / humidity
constexpr char RestartReqMsg [] = "M002";       // Req restart
constexpr char DoorStatusReqMsg [] = "M003";    // Req Door status
constexpr char DoorOpenReqMsg [] = "M004";      // Req Door Open
constexpr char DoorCloseReqMsg [] = "M005";     // Req Door Close
constexpr char DoorStopReqMsg [] = "M006";      // Req Door Stop
constexpr char DoorLightOnReqMsg [] = "M007";   // Req Light On
constexpr char DoorLightOffReqMsg [] = "M008";  // Req Light off
constexpr char PartSeparator [] = ":";

constexpr auto MAX_INCOMING_UDP_MSG = 255;

enum class eResponseMessage : uint8_t
{
	TEMPDATA,
	DOORDATA
};

#define CALL_MEMBER_FN_BY_PTR( object, ptrToMember ) ( ( object )->*( ptrToMember ) )
extern void Error ( String s, bool bInISR = false );
extern void Info ( String s, bool bInISR = false );

// TEMP_WIFI_DIAG_REMOVE_ME_BEGIN
// Temporary serial diagnostics for reboot WiFi troubleshooting.
// Remove this block after root cause is identified.
namespace
{
constexpr unsigned long WIFI_DIAG_SERIAL_WAIT_MS = 1500UL;
bool s_wifiDiagSerialReady = false;
constexpr bool WIFI_ENABLE_TEMP_SERIAL_DIAGNOSTICS = false;

void WiFiDiagEnsureSerial ()
{
	if ( !WIFI_ENABLE_TEMP_SERIAL_DIAGNOSTICS )
	{
		return;
	}

	if ( s_wifiDiagSerialReady )
	{
		return;
	}

	Serial.begin ( BAUD_RATE );
	unsigned long startMs = millis();
	while ( !Serial && ( millis() - startMs ) < WIFI_DIAG_SERIAL_WAIT_MS )
	{
		delay ( 10 );
	}
	Serial.println ( F ( "[TEMP_WIFI_DIAG] Serial diagnostics online" ) );
	s_wifiDiagSerialReady = true;
}

void WiFiDiagLog ( const String& msg )
{
	if ( !WIFI_ENABLE_TEMP_SERIAL_DIAGNOSTICS )
	{
		return;
	}

	WiFiDiagEnsureSerial();
	Serial.println ( "[TEMP_WIFI_DIAG] " + msg );
}

void WiFiDiagLogCritical ( const String& msg )
{
	WiFiDiagLog ( msg );
}
}  // namespace
// TEMP_WIFI_DIAG_REMOVE_ME_END

// Helper function to log WiFi/UDP errors with context
static void logWiFiError ( const String& context, int errorCode )
{
	Error ( context + " failed with code: " + String ( errorCode ) );
}

// Helper function to convert IPAddress to String
static String ipToString ( const IPAddress& address )
{
	return String ( address [ 0 ] ) + "." + String ( address [ 1 ] ) + "." + String ( address [ 2 ] ) + "." +
	       String ( address [ 3 ] );
}

void TerminateProgram ( const __FlashStringHelper* pErrMsg )
{
	Error ( pErrMsg );
	while ( true )
		;
}

/**
 * @brief Default constructor. Configures the timezone for UK (GMT/BST) and initialises
 *        the non-volatile configuration storage. Zeroes the configuration structure.
 */
WiFiService::WiFiService ()
{
	// Set the timezone to GMT with daylight saving time adjustments


	// GMT0BST: base is UTC+0 (GMT), DST is UTC+1 (BST)
	// M3.5.0/1  = last Sunday in March at 01:00, clocks go forward 1 hour
	// M10.5.0/2 = last Sunday in October at 02:00, clocks go back 1 hour
	setenv ( "TZ", "GMT0BST,M3.5.0/1,M10.5.0/2", 1 );
	// Initialize configuration storage
	if ( !ConfigStorage::begin() )
	{
		Error ( F ( "Failed to initialize configuration storage" ) );
	}
	memset ( &m_config, 0, sizeof ( m_config ) );
}
/// @brief Destructor to clean up the WiFiService
WiFiService::~WiFiService ()
{
	WiFiDisconnect();
	if ( m_pOnboardingPortal != nullptr )
	{
		delete m_pOnboardingPortal;
		m_pOnboardingPortal = nullptr;
	}
	if ( m_pOnboardingServer != nullptr )
	{
		delete m_pOnboardingServer;
		m_pOnboardingServer = nullptr;
	}
}


/**
 * @brief Converts a numeric WiFi status code to a human-readable string.
 * @param iState WiFi status code as returned by WiFi.status() (e.g. WL_CONNECTED == 3).
 * @return Pointer to a static string describing the status, or "UNKNOWN" if out of range.
 */
const char* WiFiService::WiFiStatusToString ( uint8_t iState ) const
{
	if ( iState == WL_NO_SHIELD )  // 255 — NINA SPI unresponsive; its numeric value exceeds the array bounds
	{
		return "NO_SHIELD";
	}
	static constexpr size_t statusCount = sizeof ( WiFiStatus ) / sizeof ( WiFiStatus [ 0 ] );

	return ( iState < statusCount ) ? WiFiStatus [ iState ] : "UNKNOWN";
}

/**
 * @brief Returns the cumulative count of WiFi connection attempts that timed out.
 * @return Number of WiFi.begin() calls that failed to connect within the timeout period.
 */
uint32_t WiFiService::GetBeginTimeOutCount () const
{
	return m_beginTimeouts;
}

/**
 * @brief Returns the hostname assigned to this device on the WiFi network.
 * @return Pointer to the hostname C-string, or nullptr if not yet configured.
 */
const char* WiFiService::GetHostName () const
{
	return m_HostName;
}

/**
 * @brief Returns the current UTC epoch time obtained from the WiFi module via NTP.
 * @return Seconds since 1 January 1970 (UTC), or 0 if the time is unavailable.
 */
unsigned long WiFiService::GetTime () const
{
	return WiFi.getTime();
}

/**
 * @brief Returns the altitude compensation value used for barometric pressure correction.
 * @return Altitude above sea level in metres, as loaded from stored configuration.
 */
float WiFiService::GetAltitudeCompensation () const
{
	return m_config.altitudeCompensation;
}

/**
 * @brief Returns the current connection state of the WiFi service.
 * @return One of UNCONNECTED, CONNECTED, or AP_MODE.
 */
WiFiService::Status WiFiService::GetState () const
{
	return m_State;
}

/**
 * @brief Sets the status LED colour and optional flash rate.
 * @param theColour RGB colour value to display.
 * @param flashTime Flash interval in units of 100 ms; 0 means solid (no flash).
 */
void WiFiService::SetLED ( RGBType theColour, uint8_t flashTime )
{
	if ( m_pLED == nullptr )
	{
		return;
	}

	if ( theColour == PROCESSING_MSG_COLOUR )
	{
		m_processingLedHoldUntilMs = millis() + WIFI_PROCESSING_LED_HOLD_MS;
		m_connectedLedDeferred = true;
	}

	if ( m_lastLedValid && m_lastLedColour == theColour && m_lastLedFlashTime == flashTime )
	{
		return;
	}

	m_pLED->SetLEDColour ( theColour, flashTime );
	m_lastLedColour = theColour;
	m_lastLedFlashTime = flashTime;
	m_lastLedValid = true;
}

/**
 * @brief Updates the internal connection state and reflects the change on the status LED.
 * @param state New state to apply: UNCONNECTED (flashing red), CONNECTED (green), or AP_MODE (solid blue).
 */
void WiFiService::SetState ( WiFiService::Status state )
{
	const uint32_t nowMs = millis();
	const bool processingHoldActive =
	    m_connectedLedDeferred && ( static_cast<int32_t> ( nowMs - m_processingLedHoldUntilMs ) < 0 );

	if ( m_State != state )
	{
		m_State = state;
	}
	switch ( state )
	{
		case WiFiService::Status::CONNECTED:
			if ( !processingHoldActive )
			{
				SetLED ( CONNECTED_COLOUR );
				m_connectedLedDeferred = false;
			}
			break;

		case WiFiService::Status::UNCONNECTED:
			m_connectedLedDeferred = false;
			m_processingLedHoldUntilMs = 0UL;
			SetLED ( UNCONNECTED_COLOUR, WIFI_FLASHTIME );
			break;

		case WiFiService::Status::AP_MODE:
			m_connectedLedDeferred = false;
			m_processingLedHoldUntilMs = 0UL;
			SetLED ( MNRGBLEDBaseLib::eColour::DARK_BLUE, 0 );
			break;
	}
}

/**
 * @brief Initializes the WiFi service with onboarding support.
 * @details Attempts to load stored credentials and connect. If no valid
 *          configuration is found, enters AP mode for onboarding. If stored
 *          credentials exist, STA retry/backoff policy determines whether AP
 *          mode is entered later.
 *
 * @param apSSID The SSID to use for the onboarding AP.
 * @param apPassword The password to use for the onboarding AP (nullptr for open AP).
 * @param pLED Pointer to an LED object for indicating status.
 */
void WiFiService::Begin ( const char* apSSID, const char* apPassword, MNRGBLEDBaseLib* pLED )
{
	WiFiDiagLog ( String ( "Begin() called. WiFi.status=" ) + WiFi.status() + " (" +
	              WiFiStatusToString ( WiFi.status() ) + ")" );
	m_apSSID = apSSID;
	m_apPassword = apPassword;
	m_pLED = pLED;
	m_lastLedValid = false;
	m_useOnboarding = true;

	WiFi.setHostname ( "GarageControl" );

	String fv = WiFi.firmwareVersion();
	WiFiDiagLog ( "NINA firmware=" + fv + ", required=" + String ( WIFI_FIRMWARE_LATEST_VERSION ) );
	if ( fv < WIFI_FIRMWARE_LATEST_VERSION )
	{
		SetLED ( OLD_WIFI_FIRMWARE_COLOUR );
		Error ( "Please upgrade the firmware. Latest is " + String ( WIFI_FIRMWARE_LATEST_VERSION ) + ", board has " +
		        String ( fv ) );
		WiFiDiagLog ( "Firmware too old, WiFi startup blocked" );
	}
	else
	{
		// Try to load configuration and connect
		WiFiDiagLog ( "Calling LoadAndConnectFromStorage()" );
		LoadAndConnectFromStorage();
	}
}

/**
 * @brief Initializes the WiFi service with direct credentials (legacy mode).
 * @details Use this for backward compatibility when not using onboarding.
 *
 * @param HostName The hostname to set for the WiFi connection.
 * @param WiFissid The SSID of the WiFi network to connect to.
 * @param WiFipwd The password of the WiFi network.
 * @param pLED Pointer to an LED object for indicating status.
 */
void WiFiService::BeginWithConfig ( const char* HostName,
                                    const char* WiFissid,
                                    const char* WiFipwd,
                                    MNRGBLEDBaseLib* pLED )
{
	m_SSID = WiFissid;
	m_Pwd = WiFipwd;
	m_HostName = HostName;
	m_pLED = pLED;
	m_lastLedValid = false;

	WiFi.setHostname ( m_HostName );

	String fv = WiFi.firmwareVersion();
	if ( fv < WIFI_FIRMWARE_LATEST_VERSION )
	{
		SetLED ( OLD_WIFI_FIRMWARE_COLOUR );
		Error ( "Please upgrade the firmware. Latest is " + String ( WIFI_FIRMWARE_LATEST_VERSION ) + ", board has " +
		        String ( fv ) );
	}
	else
	{
		// Set the initial state to UNCONNECTED as the WiFi connection has not been established yet
		SetState ( Status::UNCONNECTED );
	}
}

/**
 * @brief Loads configuration from storage and attempts to connect.
 * @details If valid configuration exists, initializes STA credentials and
 *          starts non-blocking connect/retry flow. If no valid configuration
 *          exists, enters AP mode for onboarding.
 */
void WiFiService::LoadAndConnectFromStorage ()
{
	WiFiDiagLog ( "LoadAndConnectFromStorage() entered" );
	if ( ConfigStorage::load ( m_config ) )
	{
		m_hasStoredConfig = true;
		ResetStaFailureTracking();
		WiFiDiagLog ( "Config load success: valid=true, ssid='" + String ( m_config.ssid ) + "', host='" +
		              String ( m_config.hostname ) + "'" );
		Info ( "Loaded configuration from storage" );
		Info ( "SSID: " + String ( m_config.ssid ) );
		Info ( "Hostname: " + String ( m_config.hostname ) );

		m_SSID = m_config.ssid;
		m_Pwd = m_config.password;
		m_HostName = m_config.hostname;
		m_staStartupGraceUntilMs = millis() + WIFI_STA_STARTUP_GRACE_MS;
		m_staStartupGraceApplied = false;
		m_firstStaPreflightResetPending = WIFI_ENABLE_FIRST_STA_PREFLIGHT_RESET;

		WiFi.setHostname ( m_HostName );

		// Try to connect
		if ( !WiFiConnect() )
		{
			const bool connectPending = m_staConnectInProgress;
			WiFiDiagLog ( String ( "Initial WiFiConnect() pending/deferred. state=" ) + GetState() +
			              ", status=" + WiFi.status() + " (" + WiFiStatusToString ( WiFi.status() ) +
			              "), inProgress=" + ( connectPending ? "1" : "0" ) );
			SetState ( Status::UNCONNECTED );
			if ( connectPending )
			{
				Info ( "Initial WiFi connect started; waiting for async completion" );
			}
			else
			{
				Info ( "Initial WiFi connect deferred; retry/backoff loop will continue" );
			}
		}
		else
		{
			WiFiDiagLog ( "Initial WiFiConnect() succeeded" );
			Info ( "Successfully connected to WiFi" );
			SetState ( Status::CONNECTED );
		}
	}
	else
	{
		m_hasStoredConfig = false;
		ResetStaFailureTracking();
		WiFiDiagLog ( "Config load failed or invalid; entering onboarding path" );
		Info ( "No valid configuration found" );
		if ( m_useOnboarding )
		{
			Info ( "Entering AP mode for initial configuration" );
			StartAP();
		}
		else
		{
			SetState ( Status::UNCONNECTED );
		}
	}
}

/**
 * @brief Starts Access Point mode for onboarding.
 * @details Creates an AP with the configured SSID. If password is provided, creates a secured AP,
 *          otherwise creates an open AP.
 */
void WiFiService::StartAP ()
{
	if ( GetState() == Status::AP_MODE )
	{
		return;
	}

	Info ( "Starting AP mode: " + String ( m_apSSID ) );

	if ( m_pOnboardingServer == nullptr )
	{
		m_pOnboardingServer = new OnboardingServer();
	}
	if ( m_pOnboardingPortal == nullptr )
	{
		m_pOnboardingPortal = new OnboardingPortal ( *m_pOnboardingServer, m_apSSID, m_apPassword );
	}
	if ( !m_pOnboardingPortal->begin() )
	{
		Error ( F ( "Failed to start onboarding portal" ) );
		return;
	}

	// AP onboarding indicator: green when a client is connected, blue while awaiting a client.
	m_pOnboardingPortal->setOnClientConnected (
	    [ this ] ()
	    {
		    m_apClientConnected = true;
		    SetLED ( CONNECTED_COLOUR, 0 );
	    } );
	m_pOnboardingPortal->setOnClientDisconnected (
	    [ this ] ()
	    {
		    m_apClientConnected = false;
		    SetLED ( MNRGBLEDBaseLib::eColour::DARK_BLUE, 0 );
	    } );

	Info ( "AP started. IP: " + ToIPString ( m_pOnboardingPortal->apIP() ) );
	m_apModeEnteredMs = millis();
	m_apClientConnected = false;
	SetState ( Status::AP_MODE );
}

void WiFiService::ResetStaFailureTracking ()
{
	m_lastConnectStatus = WL_IDLE_STATUS;
	m_consecutiveCredentialFailures = 0;
	m_firstStaFailureMs = 0UL;
	m_staConnectInProgress = false;
	m_staConnectStartMs = 0UL;
	m_staStartupGraceApplied = false;
	m_staStartupGraceUntilMs = 0UL;
	m_staStartupGraceLogged = false;
}

void WiFiService::NoteConnectFailure ( uint8_t status )
{
	m_lastConnectStatus = status;
	if ( m_firstStaFailureMs == 0UL )
	{
		m_firstStaFailureMs = millis();
	}

	if ( status == WL_CONNECT_FAILED )
	{
		if ( m_consecutiveCredentialFailures < 0xFF )
		{
			m_consecutiveCredentialFailures++;
		}
	}
	else
	{
		// Only count explicit connect failures as credential evidence.
		m_consecutiveCredentialFailures = 0;
	}
}

bool WiFiService::ShouldEnterAPMode () const
{
	if ( !m_useOnboarding || !m_hasStoredConfig )
	{
		return false;
	}

	if ( m_firstStaFailureMs == 0UL )
	{
		return false;
	}

	if ( ( millis() - m_firstStaFailureMs ) < WIFI_AP_ENTRY_GRACE_MS )
	{
		return false;
	}

	return m_consecutiveCredentialFailures >= WIFI_AP_CREDENTIAL_FAILURE_THRESHOLD;
}

/**
 * @brief Calculates the subnet broadcast address for the device's own IP address.
 * @param result Output parameter that receives the calculated broadcast address.
 */
void WiFiService::CalcMyMulticastAddress ( IPAddress& result ) const
{
	CalcMulticastAddress ( WiFi.localIP(), result );
}

/**
 * @brief Calculates the subnet broadcast address for a given IP using classful routing rules.
 * @param ip         Source IP address used to determine the network class and prefix.
 * @param subnetMask Output parameter that receives the broadcast address
 *                   (network prefix OR'd with all host bits set to 1).
 *                   Despite the parameter name this is a broadcast address, not a subnet mask.
 */
void WiFiService::CalcMulticastAddress ( IPAddress ip, IPAddress& subnetMask ) const
{
	subnetMask = IPAddress ( 0UL );  // WiFi.subnetMask();
	uint8_t firstOctet = ( ip & 0xff );
	if ( firstOctet > 0 && firstOctet <= 127 )
	{
		// class A
		subnetMask = IPAddress ( 255, 0, 0, 0 );
	}
	else if ( firstOctet > 127 && firstOctet <= 191 )
	{
		// Class B
		subnetMask = IPAddress ( 255, 255, 0, 0 );
	}
	else if ( firstOctet > 191 && firstOctet <= 223 )
	{
		// Class C
		subnetMask = IPAddress ( 255, 255, 255, 0 );
	}
	subnetMask = ( ip & subnetMask ) | ( ~subnetMask );
}

/**
 * @brief Returns the cached subnet broadcast address for outgoing multicast transmissions.
 * @return The broadcast address calculated at the time of the last successful WiFi connection.
 */
IPAddress WiFiService::GetMulticastAddress () const
{
	return m_multicastAddr;
}

/**
 * @brief Attempts to connect (or reconnect) to the configured WiFi network.
 * @details No-ops if already in AP mode or already connected. Uses non-blocking
 *          connect polling and capped exponential backoff between attempts.
 *          Optionally enters AP mode only after grace period and repeated
 *          credential-failure evidence.
 * @return true if the device is (or becomes) connected to the WiFi network; false otherwise.
 */
bool WiFiService::WiFiConnect ()
{
	const uint32_t nowMs = millis();
	const WiFiService::Status currentState = GetState();
	static uint8_t s_lastEntryStatus = WL_NO_SHIELD;
	static WiFiService::Status s_lastEntryState = WiFiService::Status::AP_MODE;
	static bool s_lastEntryInProgress = true;
	static uint8_t s_lastEntryAttempts = 0xFFU;
	static uint32_t s_lastBackoffLogMs = 0UL;
	static uint32_t s_lastNoShieldBackoffResetMs = 0UL;
	static uint32_t s_lastNoShieldSeenMs = 0UL;
	static uint32_t s_lastNoShieldDeferralLogMs = 0UL;
	static bool s_forceHardResetNextAttempt = false;
	static uint8_t s_cachedStatus = WL_IDLE_STATUS;
	static uint32_t s_lastStatusPollMs = 0UL;
	static bool s_noShieldQuarantine = false;
	static uint32_t s_noShieldQuarantineUntilMs = 0UL;
	static uint8_t s_noShieldRecoveryResets = 0U;
	static uint8_t s_consecutiveStaTimeouts = 0U;

	if ( m_connectedLedDeferred && GetState() == Status::CONNECTED &&
	     static_cast<int32_t> ( millis() - m_processingLedHoldUntilMs ) >= 0 )
	{
		SetLED ( CONNECTED_COLOUR );
		m_connectedLedDeferred = false;
	}

	auto GetThrottledStatus = [ & ] ( bool forcePoll ) -> uint8_t
	{
		if ( s_noShieldQuarantine && nowMs < s_noShieldQuarantineUntilMs )
		{
			s_cachedStatus = WL_NO_SHIELD;
			return s_cachedStatus;
		}

		if ( s_noShieldQuarantine && nowMs >= s_noShieldQuarantineUntilMs )
		{
			// Avoid an immediate WiFi.status() poll right after NO_SHIELD quarantine expiry,
			// because that call can stall in a wedged NINA state.
			s_noShieldQuarantine = false;
			s_cachedStatus = WL_IDLE_STATUS;
			return s_cachedStatus;
		}

		if ( forcePoll || ( nowMs - s_lastStatusPollMs ) >= WIFI_STATUS_POLL_INTERVAL_MS )
		{
			s_cachedStatus = WiFi.status();
			s_lastStatusPollMs = nowMs;
			if ( s_cachedStatus != WL_NO_SHIELD )
			{
				s_noShieldQuarantine = false;
			}
		}

		return s_cachedStatus;
	};

	// During the initial STA bring-up, avoid raw WiFi.status() polling until after
	// preflight/reset/startup-grace has completed and we are ready to issue WiFi.begin().
	// This avoids wedging inside WiFi.status() before the first begin attempt.
	const bool inInitialStaPreBeginPhase = !m_staConnectInProgress && !m_staStartupGraceApplied &&
	                                       ( m_reconnectAttempts == 0 ) && ( m_nextReconnectMs == 0 );
	const bool inReconnectBackoffWindow =
	    !m_staConnectInProgress && ( m_nextReconnectMs != 0UL ) && ( nowMs < m_nextReconnectMs );
	const uint8_t currentStatus =
	    ( inInitialStaPreBeginPhase || inReconnectBackoffWindow ) ? s_cachedStatus : GetThrottledStatus ( false );

	const bool entryChanged = ( currentStatus != s_lastEntryStatus ) || ( currentState != s_lastEntryState ) ||
	                          ( m_staConnectInProgress != s_lastEntryInProgress ) ||
	                          ( m_reconnectAttempts != s_lastEntryAttempts );
	if ( entryChanged )
	{
		WiFiDiagLog ( String ( "WiFiConnect() entry: state=" ) + currentState + ", wifiStatus=" + currentStatus + " (" +
		              WiFiStatusToString ( currentStatus ) + "), inProgress=" + ( m_staConnectInProgress ? "1" : "0" ) +
		              ", attempts=" + m_reconnectAttempts );
		s_lastEntryStatus = currentStatus;
		s_lastEntryState = currentState;
		s_lastEntryInProgress = m_staConnectInProgress;
		s_lastEntryAttempts = m_reconnectAttempts;
	}
	// In AP/onboarding mode do not attempt STA connection — it would call WiFi.begin()
	// which tears down the AP beacon and destroys AP mode state.
	if ( GetState() == Status::AP_MODE )
	{
		WiFiDiagLog ( "Skipping WiFiConnect() because AP_MODE is active" );
		return false;
	}

	if ( currentStatus == WL_CONNECTED )
	{
		const bool wasServiceConnected = ( GetState() == Status::CONNECTED );
		const bool wasAttemptInProgress = m_staConnectInProgress;

		if ( !wasServiceConnected )
		{
			CalcMyMulticastAddress ( m_multicastAddr );
			SetState ( WiFiService::Status::CONNECTED );
		}

		if ( wasAttemptInProgress || !wasServiceConnected )
		{
			m_beginConnects++;
		}

		if ( wasAttemptInProgress || !wasServiceConnected )
		{
			WiFiDiagLog ( "WiFi connected. localIP=" + ToIPString ( WiFi.localIP() ) +
			              ", beginConnects=" + m_beginConnects );
		}

		// Already up — reset counters so a future drop starts fresh backoff
		m_reconnectAttempts = 0;
		m_nextReconnectMs = 0;
		m_disconnectMissCount = 0;
		ResetStaFailureTracking();
		return true;
	}

	// Not connected — apply confirmation window before tearing down.
	// The NINA SPI co-processor can return a transient non-WL_CONNECTED status for
	// several seconds after a UDP receive burst.  Returning true during this window
	// suppresses a false reconnect cycle and avoids restarting the UDP listener.
	if ( GetState() == Status::CONNECTED && !m_staConnectInProgress )
	{
		if ( m_disconnectMissCount == 0 )
		{
			m_disconnectFirstMissTime = millis();
		}
		if ( ++m_disconnectMissCount < WIFI_DISCONNECT_CONFIRM_COUNT ||
		     millis() - m_disconnectFirstMissTime < WIFI_DISCONNECT_MIN_WINDOW_MS )
		{
			return true;  // pretend connected — drop not yet confirmed
		}
		m_disconnectMissCount = 0;
		// Confirmation window elapsed; fall through to start reconnect.
	}

	// Start a new reconnect attempt once the backoff window allows it.
	if ( !m_staConnectInProgress )
	{
		if ( currentStatus == WL_NO_SHIELD )
		{
			s_lastNoShieldSeenMs = nowMs;
			if ( !s_noShieldQuarantine || nowMs >= s_noShieldQuarantineUntilMs )
			{
				if ( ( nowMs - s_lastNoShieldBackoffResetMs ) >= WIFI_NO_SHIELD_RECOVERY_COOLDOWN_MS )
				{
					WiFiDiagLog ( "NO_SHIELD observed pre-begin; forcing NINA reset and deferring connect" );
					SpiDrv::end();
					s_lastNoShieldBackoffResetMs = nowMs;
				}
				else
				{
					WiFiDiagLog ( "NO_SHIELD observed pre-begin; recent reset already performed, deferring connect" );
				}
				s_noShieldQuarantine = true;
				s_noShieldQuarantineUntilMs = nowMs + WIFI_NO_SHIELD_RECOVERY_COOLDOWN_MS;
				if ( s_noShieldRecoveryResets < WIFI_NO_SHIELD_MAX_RECOVERY_RESETS )
				{
					s_noShieldRecoveryResets++;
				}
				if ( s_noShieldRecoveryResets >= WIFI_NO_SHIELD_MAX_RECOVERY_RESETS )
				{
					WiFiDiagLogCritical ( "Persistent NO_SHIELD across recovery resets; triggering board reset" );
					MN::Utils::ResetBoard ( F ( "Persistent NO_SHIELD" ) );
				}
			}

			if ( ( nowMs - s_lastNoShieldDeferralLogMs ) >= WIFI_DIAG_ENTRY_LOG_INTERVAL_MS )
			{
				WiFiDiagLog ( String ( "NO_SHIELD quarantine active; next status poll in ms=" ) +
				              ( s_noShieldQuarantineUntilMs - nowMs ) );
				s_lastNoShieldDeferralLogMs = nowMs;
			}
			return false;
		}

		if ( s_lastNoShieldSeenMs != 0UL && ( nowMs - s_lastNoShieldSeenMs ) < WIFI_NO_SHIELD_STABLE_WINDOW_MS )
		{
			if ( ( nowMs - s_lastNoShieldDeferralLogMs ) >= WIFI_DIAG_ENTRY_LOG_INTERVAL_MS )
			{
				WiFiDiagLog ( String ( "Deferring WiFi.begin until NO_SHIELD clears for ms=" ) +
				              ( WIFI_NO_SHIELD_STABLE_WINDOW_MS - ( nowMs - s_lastNoShieldSeenMs ) ) );
				s_lastNoShieldDeferralLogMs = nowMs;
			}
			return false;
		}

		if ( !m_staStartupGraceApplied && m_reconnectAttempts == 0 && m_nextReconnectMs == 0 )
		{
			if ( m_staStartupGraceUntilMs == 0UL )
			{
				m_staStartupGraceUntilMs = nowMs + WIFI_STA_STARTUP_GRACE_MS;
				m_staStartupGraceLogged = false;
			}

			if ( nowMs < m_staStartupGraceUntilMs )
			{
				if ( !m_staStartupGraceLogged )
				{
					WiFiDiagLog ( String ( "Startup grace active; deferring first WiFi.begin by ms=" ) +
					              ( m_staStartupGraceUntilMs - nowMs ) );
					m_staStartupGraceLogged = true;
				}
				return false;
			}

			if ( m_staStartupGraceLogged )
			{
				WiFiDiagLog ( "Startup grace complete; proceeding with first WiFi.begin" );
				m_staStartupGraceLogged = false;
			}

			if ( m_firstStaPreflightResetPending )
			{
				WiFiDiagLog ( "Preflight NINA reset before first STA begin" );
				SpiDrv::end();
				m_firstStaPreflightResetPending = false;
				// Leave initial-grace mode so reconnect flow performs NO_SHIELD quarantine
				// and status-stability gating before the first WiFi.begin().
				m_staStartupGraceApplied = true;
				m_staStartupGraceUntilMs = 0UL;
				m_staStartupGraceLogged = false;
				s_cachedStatus = WL_NO_SHIELD;
				s_noShieldQuarantine = true;
				s_noShieldQuarantineUntilMs = nowMs + WIFI_POST_HARD_RESET_SETTLE_MS;
				s_lastNoShieldSeenMs = nowMs;
				return false;
			}

			m_staStartupGraceApplied = true;
		}

		if ( m_nextReconnectMs != 0 && nowMs < m_nextReconnectMs )
		{
			const uint32_t remainingBackoffMs = m_nextReconnectMs - nowMs;

			if ( ( nowMs - s_lastBackoffLogMs ) >= WIFI_DIAG_BACKOFF_LOG_INTERVAL_MS )
			{
				WiFiDiagLog ( String ( "Backoff active; next reconnect in ms=" ) + remainingBackoffMs );
				s_lastBackoffLogMs = nowMs;
			}

			if ( currentStatus == WL_NO_SHIELD &&
			     ( nowMs - s_lastNoShieldBackoffResetMs ) >= WIFI_RECONNECT_BASE_DELAY_MS )
			{
				WiFiDiagLog ( "NO_SHIELD observed during backoff; forcing NINA hard reset" );
				SpiDrv::end();
				s_lastNoShieldBackoffResetMs = nowMs;
			}

			return false;
		}

		if ( m_nextReconnectMs != 0 && nowMs >= m_nextReconnectMs )
		{
			WiFiDiagLogCritical ( "Backoff elapsed; attempting reconnect now" );
			m_nextReconnectMs = 0;
		}

		if ( m_reconnectAttempts >= WIFI_RECONNECT_MAX_ATTEMPTS )
		{
			m_reconnectAttempts = WIFI_RECONNECT_MAX_ATTEMPTS;
		}

		Info ( "WiFi reconnect attempt " + String ( m_reconnectAttempts + 1 ) );
		WiFiDiagLog ( String ( "Starting reconnect attempt " ) + ( m_reconnectAttempts + 1 ) + ", ssid='" +
		              String ( m_SSID != nullptr ? m_SSID : "<null>" ) + "'" );

		WiFiDiagLog ( "Calling WiFi.disconnect() before retry" );
		WiFi.disconnect();
		WiFiDiagLog ( "WiFi.disconnect() returned" );
		// Perform a full hardware NINA reset every WIFI_HARD_RESET_EVERY attempts.
		// WiFi.end() is a no-op on MKR WiFi 1010 — SpiDrv::end() asserts SLAVERESET
		// and clears the SPI initialised flag so the next WiFi.begin() runs the full
		// SpiDrv::begin() hardware reset sequence (750 ms boot delay included).
		// Plain WiFi.disconnect()/WiFi.begin() is used for other attempts to avoid
		// over-cycling the NINA module and triggering the stuck NO_SHIELD state.
		if ( s_forceHardResetNextAttempt )
		{
			WiFiDiagLog ( "Forcing NINA hard reset before retry after previous timeout" );
			SpiDrv::end();
			s_forceHardResetNextAttempt = false;
			m_nextReconnectMs = nowMs + WIFI_POST_HARD_RESET_SETTLE_MS;
			WiFiDiagLog ( String ( "Deferring WiFi.begin after hard reset by ms=" ) + WIFI_POST_HARD_RESET_SETTLE_MS );
			return false;
		}
		else if ( m_reconnectAttempts > 0 && ( m_reconnectAttempts % WIFI_HARD_RESET_EVERY == 0 ) )
		{
			Info ( "Hard resetting NINA module via SpiDrv::end()" );
			WiFiDiagLog ( String ( "Performing NINA hard reset on attempt " ) + m_reconnectAttempts );
			SpiDrv::end();
		}
		const uint32_t beginCallStartMs = millis();
		WiFiDiagLogCritical ( "Calling WiFi.begin()" );
		WiFi.begin ( m_SSID, m_Pwd );
		const uint32_t beginCallDurationMs = millis() - beginCallStartMs;
		m_staConnectInProgress = true;
		m_staConnectStartMs = beginCallStartMs;
		m_lastConnectStatus = WL_IDLE_STATUS;

		const uint8_t immediateStatus = GetThrottledStatus ( true );
		WiFiDiagLogCritical ( String ( "WiFi.begin() issued after ms=" ) + beginCallDurationMs +
		                      ", status=" + immediateStatus + " (" + WiFiStatusToString ( immediateStatus ) + ")" );
		if ( immediateStatus == WL_CONNECTED )
		{
			CalcMyMulticastAddress ( m_multicastAddr );
			Info ( "Connected to " + String ( m_SSID ) + " (immediate)" );
			SetState ( WiFiService::Status::CONNECTED );
			m_staConnectInProgress = false;
			m_staConnectStartMs = 0UL;
			m_reconnectAttempts = 0;
			m_nextReconnectMs = 0;
			ResetStaFailureTracking();
			m_beginConnects++;
			s_noShieldRecoveryResets = 0U;
			WiFiDiagLog ( "Immediate WL_CONNECTED after WiFi.begin. localIP=" + ToIPString ( WiFi.localIP() ) );
			return true;
		}

		return false;
	}

	uint8_t status = GetThrottledStatus ( false );
	if ( status != m_lastConnectStatus )
	{
		WiFiDiagLog ( String ( "WiFi status change: " ) + m_lastConnectStatus + "->" + status + " (" +
		              WiFiStatusToString ( status ) + ")" );
		m_lastConnectStatus = status;
	}

	if ( status == WL_NO_SHIELD )
	{
		WiFiDiagLog ( "NO_SHIELD detected during connect; aborting attempt early" );
		m_staConnectInProgress = false;
		m_staConnectStartMs = 0UL;

		m_reconnectAttempts++;
		if ( m_reconnectAttempts > WIFI_RECONNECT_MAX_ATTEMPTS )
		{
			m_reconnectAttempts = WIFI_RECONNECT_MAX_ATTEMPTS;
		}
		NoteConnectFailure ( status );
		m_nextReconnectMs = millis() + WIFI_RECONNECT_BASE_DELAY_MS;

		Info ( "NINA unresponsive (NO_SHIELD); forcing module hard reset" );
		SpiDrv::end();
		s_noShieldQuarantine = true;
		s_noShieldQuarantineUntilMs = nowMs + WIFI_NO_SHIELD_RECOVERY_COOLDOWN_MS;
		s_cachedStatus = WL_NO_SHIELD;
		s_forceHardResetNextAttempt = false;
		s_consecutiveStaTimeouts = 0U;

		SetState ( WiFiService::Status::UNCONNECTED );
		logWiFiError ( "WiFi connect attempt " + String ( m_reconnectAttempts ), status );
		m_beginTimeouts++;
		return false;
	}

	if ( status == WL_CONNECTED )
	{
		CalcMyMulticastAddress ( m_multicastAddr );
		Info ( "Connected to " + String ( m_SSID ) );
		SetState ( WiFiService::Status::CONNECTED );
		m_reconnectAttempts = 0;
		m_nextReconnectMs = 0;
		ResetStaFailureTracking();
		m_beginConnects++;
		s_noShieldRecoveryResets = 0U;
		s_consecutiveStaTimeouts = 0U;
		WiFiDiagLog ( "Connected after async retry. localIP=" + ToIPString ( WiFi.localIP() ) +
		              ", gateway=" + ToIPString ( WiFi.gatewayIP() ) + ", rssi=" + WiFi.RSSI() );
		s_forceHardResetNextAttempt = false;
		return true;
	}

	if ( status == WL_CONNECT_FAILED )
	{
		WiFiDiagLog ( "WL_CONNECT_FAILED detected; hard-resetting NINA before retry" );
		m_staConnectInProgress = false;
		m_staConnectStartMs = 0UL;
		s_forceHardResetNextAttempt = false;
		s_consecutiveStaTimeouts = 0U;

		m_reconnectAttempts++;
		if ( m_reconnectAttempts > WIFI_RECONNECT_MAX_ATTEMPTS )
		{
			m_reconnectAttempts = WIFI_RECONNECT_MAX_ATTEMPTS;
		}
		NoteConnectFailure ( status );

		// WL_CONNECT_FAILED can leave NINA in a degraded state where immediate retries
		// block for ~10 s and return WL_DISCONNECTED. Force a hardware reset and
		// quarantine status polling briefly before the next retry attempt.
		SpiDrv::end();
		s_noShieldQuarantine = true;
		s_noShieldQuarantineUntilMs = nowMs + WIFI_POST_HARD_RESET_SETTLE_MS;
		s_cachedStatus = WL_NO_SHIELD;
		s_lastNoShieldSeenMs = nowMs;
		s_lastNoShieldBackoffResetMs = nowMs;

		m_nextReconnectMs = millis() + WIFI_CONNECT_FAILED_RETRY_DELAY_MS;
		WiFiDiagLog ( String ( "Scheduling WL_CONNECT_FAILED retry in ms=" ) + WIFI_CONNECT_FAILED_RETRY_DELAY_MS +
		              ", attempts=" + m_reconnectAttempts + ", credFails=" + m_consecutiveCredentialFailures );

		SetState ( WiFiService::Status::UNCONNECTED );
		logWiFiError ( "WiFi connect attempt " + String ( m_reconnectAttempts ), status );
		m_beginTimeouts++;
		return false;
	}

	if ( ( millis() - m_staConnectStartMs ) < WIFI_CONNECT_TIMEOUT_MS )
	{
		return false;
	}
	WiFiDiagLog ( String ( "Connect timeout after ms=" ) + WIFI_CONNECT_TIMEOUT_MS + ", final status=" + status + " (" +
	              WiFiStatusToString ( status ) + ")" );

	m_staConnectInProgress = false;
	m_staConnectStartMs = 0UL;

	if ( status != WL_CONNECTED )
	{
		if ( s_consecutiveStaTimeouts < WIFI_RECONNECT_MAX_ATTEMPTS )
		{
			s_consecutiveStaTimeouts++;
		}

		s_forceHardResetNextAttempt = ( s_consecutiveStaTimeouts >= WIFI_TIMEOUTS_BEFORE_FORCED_HARD_RESET );
		if ( s_forceHardResetNextAttempt )
		{
			WiFiDiagLog ( String ( "Consecutive timeout/disconnect failures=" ) + s_consecutiveStaTimeouts +
			              "; next retry will hard reset NINA" );
		}
		else
		{
			WiFiDiagLog ( String ( "Consecutive timeout/disconnect failures=" ) + s_consecutiveStaTimeouts +
			              "; next retry will use soft reconnect" );
		}

		m_reconnectAttempts++;
		if ( m_reconnectAttempts > WIFI_RECONNECT_MAX_ATTEMPTS )
		{
			m_reconnectAttempts = WIFI_RECONNECT_MAX_ATTEMPTS;
		}
		NoteConnectFailure ( status );

		// Compute capped exponential backoff: base * 2^(attempts-1)
		uint32_t backoffMs = WIFI_RECONNECT_BASE_DELAY_MS;
		for ( uint8_t i = 1; i < m_reconnectAttempts && backoffMs < WIFI_RECONNECT_MAX_DELAY_MS; i++ )
		{
			backoffMs *= 2;
		}
		if ( backoffMs > WIFI_RECONNECT_MAX_DELAY_MS )
		{
			backoffMs = WIFI_RECONNECT_MAX_DELAY_MS;
		}
		m_nextReconnectMs = millis() + backoffMs;
		WiFiDiagLog ( String ( "Scheduling retry: attempts=" ) + m_reconnectAttempts + ", backoffMs=" + backoffMs +
		              ", credFails=" + m_consecutiveCredentialFailures );

		if ( status == WL_NO_SSID_AVAIL )
		{
			Info ( F ( "WiFi SSID not visible; keeping STA retry mode" ) );
		}

		// Check if WiFi has been unavailable for too long — trigger full system reset
		// This prevents indefinite accumulation of firmware state corruption when WiFi is permanently unavailable
		if ( ( millis() - m_firstStaFailureMs ) >= WIFI_FULL_RESET_TIMEOUT_MS )
		{
			WiFiDiagLog ( String ( "Triggering full board reset after ms=" ) + WIFI_FULL_RESET_TIMEOUT_MS +
			              " of STA failures" );
			Error ( F ( "WiFi unavailable for 15 minutes; performing full system reset" ) );
			MN::Utils::ResetBoard ( F ( "WiFi unavailable timeout" ) );
		}

		if ( ShouldEnterAPMode() )
		{
			WiFiDiagLog ( "ShouldEnterAPMode() true: switching to onboarding AP" );
			Info ( F ( "Repeated credential failures detected; entering AP onboarding mode" ) );
			StartAP();
			return false;
		}

		SetState ( WiFiService::Status::UNCONNECTED );
		logWiFiError ( "WiFi connect attempt " + String ( m_reconnectAttempts ), status );
		m_beginTimeouts++;
		return false;
	}

	return false;
}

/**
 * @brief Disconnects from the WiFi network and sets the connection state to UNCONNECTED.
 */
void WiFiService::WiFiDisconnect ()
{
	WiFi.disconnect();
	Info ( "Disconnecting wifi" );
	SetState ( WiFiService::Status::UNCONNECTED );
}

/**
 * @brief Converts an IPAddress to its dotted-decimal string representation.
 * @param address The IP address to convert.
 * @return String in "A.B.C.D" format.
 */
String WiFiService::ToIPString ( const IPAddress& address )
{
	return ipToString ( address );
}

/**
 * @brief Checks whether the WiFi module currently reports a connected status.
 * @return true if WiFi.status() == WL_CONNECTED, false otherwise.
 */
bool WiFiService::IsConnected () const
{
	// Use service state instead of direct WiFi.status() polling.
	// WiFi.status() can block when NINA firmware is unstable, so raw polling
	// is centralized and throttled inside WiFiConnect().
	return GetState() == WiFiService::Status::CONNECTED;
}

/**
 * @brief Returns the cumulative count of successful WiFi connections established.
 * @return Number of times WiFi.begin() has completed with a successful connection.
 */
uint32_t WiFiService::GetBeginCount ()
{
	return m_beginConnects;
}

/***************************************************************************************************************************************/
/*
 *
 *
 *
 *
 *
 *	UDPWiFiService Class
 *
 *
 *
 *
 *
 */
/***************************************************************************************************************************************/
/**
 * @brief Constructor. Allocates the multicast destination IP list with an initial capacity of 4 entries.
 */
UDPWiFiService::UDPWiFiService ()
{
	delay ( 2000 );  // Allow time for WiFi module to initialize
	m_pMulticastDestList = new FixedIPList ( 4 );
}

/**
 * @brief Initialize UDP WiFi service with onboarding support.
 * @param pHandleReqData Callback function to handle received messages.
 * @param apSSID SSID for AP mode (onboarding).
 * @param apPassword Password for AP mode.
 * @param pLED Optional LED for status indication.
 * @return True if initialization succeeded.
 */
bool UDPWiFiService::Begin ( UDPWiFiServiceCallback pHandleReqData,
                             const char* apSSID,
                             const char* apPassword,
                             MNRGBLEDBaseLib* pLED )
{
	bool bResult = false;
	WiFiService::Begin ( apSSID, apPassword, pLED );
	m_MsgHandlerCallback = pHandleReqData;

	if ( m_sUDPReceivedMsg.reserve ( MAX_INCOMING_UDP_MSG ) )
	{
		// Check if we have valid configuration loaded
		if ( m_config.valid )
		{
			m_Port = m_config.udpPort;
			if ( GetState() == Status::CONNECTED )
			{
				Start();
			}
			bResult = true;
		}
		else if ( GetState() == Status::AP_MODE )
		{
			// In AP mode, onboarding will handle configuration
			Info ( "In AP mode - waiting for configuration" );
			bResult = true;  // Consider initialization successful even in AP mode
		}
		else
		{
			SetState ( Status::UNCONNECTED );
			// Stored credentials may still succeed after retries; don't force AP mode here.
			bResult = true;
		}
	}
	return bResult;
}

/**
 * @brief Process onboarding server when in AP mode.
 * @details Call this in the main loop to handle onboarding requests.
 */
void UDPWiFiService::ProcessOnboarding ()
{
	if ( GetState() == Status::AP_MODE && m_pOnboardingPortal != nullptr )
	{
		m_pOnboardingPortal->loop();

		if ( m_hasStoredConfig && !m_apClientConnected && m_apModeEnteredMs != 0UL &&
		     ( millis() - m_apModeEnteredMs ) >= WIFI_AP_IDLE_REBOOT_MS )
		{
			Info ( F ( "AP idle timeout reached; rebooting to retry stored WiFi credentials" ) );
			delay ( 1000 );
			MN::Utils::ResetBoard ( F ( "AP idle timeout" ) );
		}
	}
}

/**
 * @brief Polls for an incoming UDP message and, if one is available, dispatches it to the
 *        registered message handler callback.
 * @details Does nothing when in AP/onboarding mode. Attempts WiFi reconnection via GetUDPMessage()
 *          if the connection has dropped.
 */
void UDPWiFiService::CheckUDP ()
{
	// Never attempt UDP or WiFi reconnection while in AP/onboarding mode.
	if ( GetState() == Status::AP_MODE )
	{
		return;
	}
	String Msg = "?";
	if ( GetUDPMessage ( Msg ) )
	{
		ProcessUDPMessage ( Msg );
	}
}

/**
 * @brief Appends the current local time formatted as "DD/MM/YY HH:MM:SS" to the provided string.
 * @details Uses UK timezone (GMT/BST). Does nothing when in AP mode or when the time is unavailable.
 *          Note: GetTime() makes a blocking NTP call; avoid calling this frequently.
 * @param result    String to which the formatted timestamp is appended.
 * @param timeError Optional pre-fetched epoch time in seconds since 1970 UTC. If 0 the NTP time
 *                  is fetched internally via GetTime().
 */
void UDPWiFiService::GetLocalTime ( String& result, time_t timeError )
{
	// WiFi.getTime() makes a blocking NTP call via NINA firmware.
	// In AP mode or while disconnected there is no internet so it can block for
	// several seconds (or longer) and starve the main loop.
	if ( GetState() == Status::AP_MODE || !IsConnected() )
	{
		return;
	}

	if ( timeError == 0 )
	{
		timeError = GetTime();
	}
	if ( timeError != 0 )
	{
		tm localtm;
		localtime_r ( &timeError, &localtm );
		char sTime [ 20 ];
		// Format: DD/MM/YY HH:MM:SS
		sprintf ( sTime,
		          "%02d/%02d/%02d %02d:%02d:%02d",
		          localtm.tm_mday,
		          localtm.tm_mon + 1,
		          ( localtm.tm_year - 100 ),
		          localtm.tm_hour,
		          localtm.tm_min,
		          localtm.tm_sec );
		result += sTime;
	}
}

/**
 * @brief Attempts WiFi reconnection if needed, then reads the next available UDP packet.
 * @details On reconnection after a drop the UDP listener is restarted automatically. Also
 *          handles delayed-start cases where WiFi is up but the UDP listener is
 *          not active yet, and refreshes the multicast destination list with the
 *          current subnet broadcast address.
 * @param RecvMessage Output: receives the content of the received UDP packet.
 * @return true if a packet was read successfully; false if not connected or no packet is available.
 */
bool UDPWiFiService::GetUDPMessage ( String& RecvMessage )
{
	const bool wasConnected = ( GetState() == Status::CONNECTED );
	if ( WiFiConnect() )
	{
		if ( m_WiFiState == WiFiState::DISCONNECTED )
		{
			// Connection became active (or was missed during async startup) — ensure UDP listener is active.
			if ( !wasConnected )
			{
				Info ( F ( "WiFi reconnected — restarting UDP" ) );
			}
			m_myUDP.stop();
			Start();
		}
		m_pMulticastDestList->Add ( GetMulticastAddress() );
		return ReadUDPMessage ( RecvMessage );
	}
	else
	{
		m_WiFiState = WiFiState::DISCONNECTED;
		SetState ( WiFiService::Status::UNCONNECTED );
		return false;
	}
}

/**
 * @brief Reads one pending UDP packet into the provided string.
 * @details Packets larger than MAX_INCOMING_UDP_MSG bytes are silently discarded and counted
 *          as bad requests. The sender's subnet broadcast address is added to the multicast
 *          destination list so future broadcasts reach that subnet.
 * @param sRecvMessage Output: receives the null-terminated content of the UDP packet.
 * @return true if a packet was available and read successfully; false otherwise.
 */
bool UDPWiFiService::ReadUDPMessage ( String& sRecvMessage )
{
	bool bResult = false;
	char sBuffer [ MAX_INCOMING_UDP_MSG ];

	// if there's data available, read a packet
	unsigned int packetSize = m_myUDP.parsePacket();
	if ( packetSize > 0 )
	{
		SetLED ( PROCESSING_MSG_COLOUR );
		String logMessage = "Received packet of size " + String ( packetSize ) + " From " +
		                    ToIPString ( m_myUDP.remoteIP() ) + ", port " + String ( m_myUDP.remotePort() );
		// Info ( logMessage ) ;
		if ( packetSize < sizeof ( sBuffer ) - 1 )
		{
			// read the packet into packetBufffer
			int len = m_myUDP.read ( sBuffer, sizeof ( sBuffer ) - 1 );
			if ( len >= 0 )
			{
				sBuffer [ len ] = 0;
				sRecvMessage = sBuffer;
				bResult = true;
				m_ulReqCount++;
			}
			else
			{
				Error ( "Failed to read UDP packet" );
			}
			// create multicast address from send ip and add to list of subnets to send multicasts to and add to list
			IPAddress result;
			CalcMulticastAddress ( m_myUDP.remoteIP(), result );
			m_pMulticastDestList->Add ( result );
		}
		else
		{
			m_ulBadRequests++;
		}
	}
	return bResult;
}

/**
 * @brief Starts the UDP listener on the configured port.
 * @details Also adds the current subnet broadcast address to the multicast destination list.
 *          Leaves WiFi in UNCONNECTED state if the UDP port cannot be allocated,
 *          allowing normal reconnect retry logic to recover without hard reset.
 * @return true if the UDP listener was started successfully.
 */
bool UDPWiFiService::Start ()
{
	bool bResult = false;

	if ( m_myUDP.begin ( m_Port ) == 1 )
	{
		bResult = true;
		m_WiFiState = WiFiState::ISCONNECTED;
		// Error ( "Started UDP" );
		IPAddress localSubnet = GetMulticastAddress();
		if ( (long unsigned int)localSubnet != 0UL )
		{
			m_pMulticastDestList->Add ( localSubnet );
		}
	}
	else
	{
		m_WiFiState = WiFiState::DISCONNECTED;
		Error ( "Unable to allocate UDP Port; will retry" );
		SetState ( WiFiService::Status::UNCONNECTED );
		m_nextReconnectMs = millis() + WIFI_RECONNECT_BASE_DELAY_MS;
	}
	return bResult;
}

/**
 * @brief Returns the cumulative count of multicast/broadcast packets sent.
 * @return Number of UDP multicast packets successfully transmitted via SendAll().
 */
uint32_t UDPWiFiService::GetMCastSentCount ()
{
	return m_ulMCastSentCount;
}

/**
 * @brief Returns the cumulative count of valid UDP request packets received.
 * @return Number of incoming UDP packets successfully read by ReadUDPMessage().
 */
uint32_t UDPWiFiService::GetRequestsReceivedCount ()
{
	return m_ulReqCount;
}

/**
 * @brief Sends a UDP reply to the IP address and port of the most recently received packet.
 * @param sMsg The message string to send as the reply payload.
 * @return true if the packet was sent successfully; false on connection loss or send failure.
 */
bool UDPWiFiService::SendReply ( String sMsg )
{
	bool bResult = false;
	if ( WiFiConnect() )
	{
		if ( sMsg.length() > 0 )
		{
			int beginResult = m_myUDP.beginPacket ( m_myUDP.remoteIP(), m_myUDP.remotePort() );
			if ( beginResult == 1 )
			{
				m_myUDP.write ( sMsg.c_str() );
				if ( m_myUDP.endPacket() == 0 )
				{
					logWiFiError ( "Message Response", 0 );
					WiFiDisconnect();
				}
				else
				{
					m_ulReplyCount++;
					SetState ( WiFiService::Status::CONNECTED );
					bResult = true;
				}
			}
			else
			{
				logWiFiError ( "Unable to send UDP message, beginPacket() to: " + ToIPString ( m_myUDP.remoteIP() ) +
				                   " : " + m_myUDP.remotePort(),
				               beginResult );
			}
		}
		else
		{
			Error ( "Empty reply to be sent" );
		}
	}
	return bResult;
}

/**
 * @brief Broadcasts a UDP message to all known subnet multicast/broadcast addresses.
 * @param sMsg The message string to send to every entry in the multicast destination list.
 * @return true if at least one packet was sent successfully; false on connection loss or empty message.
 */
bool UDPWiFiService::SendAll ( String sMsg )
{
	bool bResult = false;
	if ( WiFiConnect() )
	{
		if ( sMsg.length() > 0 )
		{
			uint8_t iterator = m_pMulticastDestList->GetIterator();
			IPAddress nextIP;
			while ( (long unsigned int)( nextIP = m_pMulticastDestList->GetNext ( iterator ) ) != 0UL )
			{
				if ( m_myUDP.beginPacket ( nextIP, m_config.multicastPort ) == 1 )
				{
					m_myUDP.write ( sMsg.c_str() );
					if ( m_myUDP.endPacket() == 0 )
					{
						Error ( "Multicast Message failed" );
						WiFiDisconnect();
					}
					else
					{
						SetLED ( PROCESSING_MSG_COLOUR );
						SetState ( WiFiService::Status::CONNECTED );
						bResult = true;
						m_ulMCastSentCount++;
					}
				}
			}
		}
		else
		{
			Error ( "Error: Empty message to be sent" );
		}
	}
	return bResult;
}

/**
 * @brief Returns the cumulative count of unicast reply packets sent.
 * @return Number of reply packets successfully transmitted via SendReply().
 */
uint32_t UDPWiFiService::GetReplySentCount ()
{
	return m_ulReplyCount;
}

/**
 * @brief Returns a pointer to the list of known multicast/broadcast destination addresses.
 * @return Pointer to the FixedIPList populated with subnet broadcast addresses discovered
 *         from incoming UDP senders and the device's own subnet.
 */
FixedIPList* UDPWiFiService::GetMulticastList ()
{
	return m_pMulticastDestList;
}

/// @brief Releases UDP port and disconnects from WiFi
void UDPWiFiService::Stop ()
{
	Info ( "Stopping WiFI" );
	m_myUDP.stop();
	WiFiDisconnect();
}

/// @brief Processes the UDP message that has been received
/// @param sRecvMessage String containing the messade received
void UDPWiFiService::ProcessUDPMessage ( const String& sRecvMessage )
{
	if ( sRecvMessage.startsWith ( cMsgVersion1 ) )
	{
		// Version 1 message received
		if ( sRecvMessage.substring ( sizeof ( cMsgVersion1 ) + sizeof ( PartSeparator ) - 2 )
		         .startsWith ( TempHumidityReqMsg ) )
		{
			// Got a data request
			// Error ( "Temp Data request" );
			m_MsgHandlerCallback ( UDPWiFiService::ReqMsgType::TEMPDATA );
		}
		else if ( sRecvMessage.substring ( sizeof ( cMsgVersion1 ) + sizeof ( PartSeparator ) - 2 )
		              .startsWith ( RestartReqMsg ) )
		{
			// Got a reset request
			MN::Utils::ResetBoard ( F ( "Reset request" ) );
		}
		else if ( sRecvMessage.substring ( sizeof ( cMsgVersion1 ) + sizeof ( PartSeparator ) - 2 )
		              .startsWith ( DoorStatusReqMsg ) )
		{
			// Got a door status request
			// Error ( "Door Data request" );
			m_MsgHandlerCallback ( UDPWiFiService::ReqMsgType::DOORDATA );
		}
		else if ( sRecvMessage.substring ( sizeof ( cMsgVersion1 ) + sizeof ( PartSeparator ) - 2 )
		              .startsWith ( DoorOpenReqMsg ) )
		{
			// Error ( "Door Open request" );
			m_MsgHandlerCallback ( UDPWiFiService::ReqMsgType::DOOROPEN );
		}
		else if ( sRecvMessage.substring ( sizeof ( cMsgVersion1 ) + sizeof ( PartSeparator ) - 2 )
		              .startsWith ( DoorCloseReqMsg ) )
		{
			// Error ( "Door Close request" );
			m_MsgHandlerCallback ( UDPWiFiService::ReqMsgType::DOORCLOSE );
		}
		else if ( sRecvMessage.substring ( sizeof ( cMsgVersion1 ) + sizeof ( PartSeparator ) - 2 )
		              .startsWith ( DoorStopReqMsg ) )
		{
			// Error ( "Door Stop request" );
			m_MsgHandlerCallback ( UDPWiFiService::ReqMsgType::DOORSTOP );
		}
		else if ( sRecvMessage.substring ( sizeof ( cMsgVersion1 ) + sizeof ( PartSeparator ) - 2 )
		              .startsWith ( DoorLightOnReqMsg ) )
		{
			// Error ( "Light On request" );
			m_MsgHandlerCallback ( UDPWiFiService::ReqMsgType::LIGHTON );
		}
		else if ( sRecvMessage.substring ( sizeof ( cMsgVersion1 ) + sizeof ( PartSeparator ) - 2 )
		              .startsWith ( DoorLightOffReqMsg ) )
		{
			// Info ( "Light Off request" );
			m_MsgHandlerCallback ( UDPWiFiService::ReqMsgType::LIGHTOFF );
		}
		else
		{
			m_ulBadRequests++;
			Error ( "Unknown request : " +
			        sRecvMessage.substring ( sizeof ( cMsgVersion1 ) + sizeof ( PartSeparator ) - 1 ) );
		}
	}
	else
	{
		m_ulBadMgsVersion++;
		Error ( "Unknown message version : " + sRecvMessage );
	}
}

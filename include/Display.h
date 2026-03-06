#pragma once
/*
 * Display.h
 *
 * Phase 7 refactoring: Display class receives door and sensor state through
 * IGarageDoor* and IEnvironmentSensor* interfaces, removing all extern-global
 * access for domain objects from Display.cpp.
 *
 * Error(), Info() and DisplaylastInfoErrorMsg() are kept as free functions so
 * that WiFiService and ISR callbacks can call them before the Display instance
 * is created.
 */

#include "IEnvironmentSensor.h"
#include "IGarageDoor.h"
#include "Logging.h"
#include "WiFiService.h"

// ─── Display class ────────────────────────────────────────────────────────────

class Display
{
public:
	/**
	 * @param logger      ANSI logger used for all screen rendering.
	 * @param pUDPService UDP/WiFi service (time queries, network stats).
	 * @param version     Software version string shown on the status screen.
	 * @param pDoor       Garage door; may be nullptr (no door configured).
	 * @param pSensor     Environment sensor; may be nullptr (not present).
	 */
	Display ( ansiVT220Logger& logger,
	          UDPWiFiService* pUDPService,
	          const char* version,
	          IGarageDoor* pDoor,
	          IEnvironmentSensor* pSensor );

	// Refresh the full debug status screen (call ~500 ms from loop()).
	void DisplayStats ();

	// Display the network-status block only.
	void DisplayNWStatus ();

private:
	ansiVT220Logger& m_logger;
	UDPWiFiService* m_pUDPService;
	const char* m_version;
	IGarageDoor* m_pDoor;
	IEnvironmentSensor* m_pSensor;

	void DisplayUptime ( uint8_t line, uint8_t row, ansiVT220Logger::colours fg, ansiVT220Logger::colours bg );
};

// ─── Notification-bar free functions ─────────────────────────────────────────
// Called by WiFiService, ISR callbacks, and Application::begin() before the
// Display instance is created.  Internal state lives in Display.cpp.

void DisplaylastInfoErrorMsg ();
void Error ( String s, bool bInISR = false );
void Info ( String s, bool bInISR = false );

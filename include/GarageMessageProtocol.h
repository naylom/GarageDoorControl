#pragma once
/*
 * GarageMessageProtocol.h
 *
 * Implements IMessageProtocol for the GarageControl application.
 * Encapsulates all UDP payload string formatting and command dispatch;
 * Application has no knowledge of the wire message format.
 *
 * Author: (c) M. Naylor 2026
 *
 * History:
 *   Ver 1.0   Phase 6 — initial implementation
 */

#include "IEnvironmentSensor.h"
#include "IGarageDoor.h"
#include "IMessageProtocol.h"
#include "WiFiService.h"

class GarageMessageProtocol : public IMessageProtocol
{
public:
	/**
	 * @param pDoor     Pointer to garage door; may be nullptr (no door configured).
	 * @param pSensor   Pointer to environment sensor; may be nullptr (no sensor present).
	 * @param reading   Reference to the shared EnvironmentReading updated by Application::loop().
	 * @param service   Reference to the UDP WiFi service (used for GetTime()).
	 */
	GarageMessageProtocol ( IGarageDoor* pDoor,
	                        IEnvironmentSensor* pSensor,
	                        EnvironmentReading& reading,
	                        UDPWiFiService& service );

	// Returns the UDP payload string for the given message type,
	// or "" if no response is required (command-only messages).
	String BuildResponse ( uint8_t msgType ) override;

	// Executes any side-effect for the given command (open door, light on, etc.).
	// No-op for data-request message types (TEMPDATA, DOORDATA).
	void HandleCommand ( uint8_t msgType ) override;

private:
	IGarageDoor* m_pDoor;
	IEnvironmentSensor* m_pSensor;
	EnvironmentReading& m_reading;
	UDPWiFiService& m_service;
};

#pragma once
/*
 * IMessageProtocol.h
 *
 * Decouples UDP message format and command dispatch from WiFi transport.
 * First implementation: GarageMessageProtocol
 *
 * Author: (c) M. Naylor 2026
 *
 * History:
 *   Ver 1.0   Phase 3 — interface definition only
 */

#include <Arduino.h>

class IMessageProtocol
{
public:
	virtual ~IMessageProtocol () = default;

	// Returns the UDP payload string for a given message type,
	// or "" if no response needed
	virtual String BuildResponse ( uint8_t msgType ) = 0;

	// Executes any side-effect for a command (open door, light on, etc.)
	virtual void HandleCommand ( uint8_t msgType ) = 0;
};

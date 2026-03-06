#pragma once
/*
 * IGarageDoor.h
 *
 * Abstract interface for any garage door controller.
 * First implementation: HormannUAP1
 *
 * Author: (c) M. Naylor 2026
 *
 * History:
 *   Ver 1.0   Phase 3 — interface definition only
 */

#include <stdint.h>

class IGarageDoor
{
public:
	enum class State : uint8_t
	{
		Open,
		Opening,
		Closed,
		Closing,
		Stopped,
		Unknown,
		Bad
	};
	enum class LightState : uint8_t
	{
		On,
		Off,
		Unknown
	};
	using StateChangedCallback = void ( * ) ( State newState );

	virtual ~IGarageDoor () = default;

	// Returns false if all pins are NOT_A_PIN (hardware not wired)
	virtual bool IsPresent () const = 0;

	// Called from Application::loop() — drives internal state machine
	virtual void Update () = 0;

	// State queries
	virtual State GetState () const = 0;
	virtual LightState GetLightState () const = 0;
	virtual bool IsOpen () const = 0;
	virtual bool IsClosed () const = 0;
	virtual bool IsMoving () const = 0;
	virtual bool IsLit () const = 0;
	virtual const char* GetStateDisplayString () const = 0;

	// Commands
	virtual void Open () = 0;
	virtual void Close () = 0;
	virtual void Stop () = 0;
	virtual void LightOn () = 0;
	virtual void LightOff () = 0;

	virtual void SetStateChangedCallback ( StateChangedCallback cb ) = 0;
};

#pragma once
/*
 * HormannUAP1.h
 *
 * Implements IGarageDoor for the Hormann Universal Adapter Platine (UAP 1).
 * The UAP provides 3 status input signals (door open, door closed, light on)
 * and accepts 4 momentary control outputs (open, close, stop, toggle light).
 *
 * DoorStatusPin and DoorStatusCalc are helper classes defined below
 * HormannUAP1 in this header; they are implementation details and should
 * not be used directly outside this file.
 *
 * Author: (c) M. Naylor 2022 / 2026
 *
 * History:
 *   Ver 1.0   Initial version (as DoorState)
 *   Ver 2.0   Phase 4 — refactored from DoorState, now implements IGarageDoor
 */

#include "IGarageDoor.h"
#include "InputPin.h"
#include "OutputPin.h"

#include <memory>
#include <MNRGBLEDBaseLib.h>
#include <stdint.h>

typedef uint8_t pin_size_t;
#ifndef NOT_A_PIN
const uint8_t NOT_A_PIN = 255;
#endif

// ─── LED colour constants for door state indication ──────────────────────────
constexpr RGBType STATE_UNKNOWN_COLOUR = MNRGBLEDBaseLib::WHITE;
constexpr RGBType DOOR_CLOSED_COLOUR = MNRGBLEDBaseLib::GREEN;
constexpr RGBType DOOR_OPEN_COLOUR = MNRGBLEDBaseLib::RED;
constexpr RGBType DOOR_STOPPED_COLOUR = MNRGBLEDBaseLib::DARK_MAGENTA;
constexpr RGBType DOOR_BAD_COLOUR = MNRGBLEDBaseLib::DARK_YELLOW;
constexpr RGBType DOOR_UNKNOWN_COLOUR = MNRGBLEDBaseLib::BLUE;
constexpr uint8_t DOOR_STATIONARY_FLASHTIME = 0;
constexpr uint8_t DOOR_MOVING_FLASHTIME = 10;  // 20 = 1 sec
constexpr PinStatus RELAY_ON_VALUE = HIGH;
constexpr PinStatus RELAY_OFF = LOW;

// ─── Forward declarations of internal helper classes ─────────────────────────
class DoorStatusPin;
class DoorStatusCalc;

// ─────────────────────────────────────────────────────────────────────────────
// HormannUAP1
// ─────────────────────────────────────────────────────────────────────────────
class HormannUAP1 : public IGarageDoor
{
public:
	// Additional enums specific to the Hormann UAP 1 hardware
	enum class Direction : uint8_t
	{
		Up = 0,
		Down,
		None
	};
	enum class Event : uint8_t
	{
		DoorOpenTrue = 0,
		DoorOpenFalse,
		DoorClosedTrue,
		DoorClosedFalse,
		SwitchPress,
		Nothing
	};

	HormannUAP1 ( pin_size_t OpenPin,
	              pin_size_t ClosePin,
	              pin_size_t StopPin,
	              pin_size_t LightPin,
	              pin_size_t DoorOpenStatusPin,
	              pin_size_t DoorClosedStatusPin,
	              pin_size_t DoorLightStatusPin );

	// Explicit destructor declared here so unique_ptr<DoorStatusPin> is only
	// destroyed in HormannUAP1.cpp where DoorStatusPin is fully defined.
	virtual ~HormannUAP1 ();

	// ── IGarageDoor interface ─────────────────────────────────────────────────
	bool IsPresent () const override;
	void Update () override;
	IGarageDoor::State GetState () const override;
	IGarageDoor::LightState GetLightState () const override;
	bool IsOpen () const override;
	bool IsClosed () const override;
	bool IsMoving () const override;
	bool IsLit () const override;
	const char* GetStateDisplayString () const override;
	void Open () override;
	void Close () override;
	void Stop () override;
	void LightOn () override;
	void LightOff () override;
	void SetStateChangedCallback ( StateChangedCallback cb ) override;

	// ── Called by DoorStatusPin ISR handler — must be public ─────────────────
	void DoEvent ( Event eEvent );

	// ── Diagnostics / display helpers (Hormann-specific, not in interface) ────
	uint32_t GetLightOnCount () const;
	uint32_t GetLightOffCount () const;
	uint32_t GetDoorOpenedCount () const;
	uint32_t GetDoorOpeningCount () const;
	uint32_t GetDoorClosedCount () const;
	uint32_t GetDoorClosingCount () const;
	void GetPinStates ( String& states );
	const char* GetDoorDirectionName () const;

	// ── Switch support — virtual no-ops overridden by HormannUAP1WithSwitch ──
	virtual bool IsSwitchConfigured () const
	{
		return false;
	}
	virtual uint32_t GetSwitchMatchCount () const
	{
		return 0;
	}
	virtual void SwitchDebugStats ( String& /*r*/ ) const
	{
	}

private:
	// ── Control output pin numbers ────────────────────────────────────────────
	const pin_size_t m_DoorOpenCtrlPin;
	const pin_size_t m_DoorCloseCtrlPin;
	const pin_size_t m_DoorStopCtrlPin;
	const pin_size_t m_DoorLightCtrlPin;

	// ── Status input pin numbers ──────────────────────────────────────────────
	const pin_size_t m_DoorOpenStatusPin;
	const pin_size_t m_DoorClosedStatusPin;
	const pin_size_t m_DoorLightStatusPin;

	// ── Pin objects ───────────────────────────────────────────────────────────
	std::unique_ptr<DoorStatusPin> m_pDoorOpenStatusPin;
	std::unique_ptr<DoorStatusPin> m_pDoorClosedStatusPin;
	std::unique_ptr<DoorStatusPin> m_pDoorLightStatusPin;

	std::unique_ptr<OutputPin> m_pDoorOpenCtrlPin;
	std::unique_ptr<OutputPin> m_pDoorCloseCtrlPin;
	std::unique_ptr<OutputPin> m_pDoorStopCtrlPin;
	std::unique_ptr<OutputPin> m_pDoorLightCtrlPin;

	DoorStatusCalc* m_pDoorStatus = nullptr;
	volatile bool m_bDoorStateChanged = true;
	volatile uint32_t m_ulSwitchPressedTime = 0UL;
	StateChangedCallback m_stateChangedCallback = nullptr;

	// ── Internal helpers ──────────────────────────────────────────────────────
	void SetStateAndDirection ( IGarageDoor::State state, Direction direction );
	void SetDoorState ( IGarageDoor::State newState );
	void SetDoorDirection ( Direction direction );
	IGarageDoor::State GetDoorStateInternal () const;
	Direction GetDoorDirection () const;
	void ResetTimer ();
	void TurnOffControlPins ();

	// ── State table ───────────────────────────────────────────────────────────
	void DoNowt ( Event )
	{
	}
	void NowOpen ( Event event );
	void NowClosed ( Event event );
	void NowClosing ( Event event );
	void NowOpening ( Event event );
	void SwitchPressed ( Event event );

	typedef void ( HormannUAP1::*StateFunction ) ( Event );
	const StateFunction StateTableFn [ 7 ][ 6 ] = {
	    // Order: [State][Event]  Events: DoorOpenTrue, DoorOpenFalse, DoorClosedTrue, DoorClosedFalse, SwitchPress,
	    // Nothing
	    { &HormannUAP1::DoNowt,
	      &HormannUAP1::NowClosing,
	      &HormannUAP1::NowClosed,
	      &HormannUAP1::DoNowt,
	      &HormannUAP1::SwitchPressed,
	      &HormannUAP1::DoNowt },  // Open
	    { &HormannUAP1::NowOpen,
	      &HormannUAP1::DoNowt,
	      &HormannUAP1::NowClosed,
	      &HormannUAP1::DoNowt,
	      &HormannUAP1::SwitchPressed,
	      &HormannUAP1::DoNowt },  // Opening
	    { &HormannUAP1::NowOpen,
	      &HormannUAP1::DoNowt,
	      &HormannUAP1::DoNowt,
	      &HormannUAP1::NowOpening,
	      &HormannUAP1::SwitchPressed,
	      &HormannUAP1::DoNowt },  // Closed
	    { &HormannUAP1::NowOpen,
	      &HormannUAP1::DoNowt,
	      &HormannUAP1::NowClosed,
	      &HormannUAP1::DoNowt,
	      &HormannUAP1::SwitchPressed,
	      &HormannUAP1::DoNowt },  // Closing
	    { &HormannUAP1::NowOpen,
	      &HormannUAP1::DoNowt,
	      &HormannUAP1::NowClosed,
	      &HormannUAP1::DoNowt,
	      &HormannUAP1::SwitchPressed,
	      &HormannUAP1::DoNowt },  // Stopped
	    { &HormannUAP1::NowOpen,
	      &HormannUAP1::NowClosing,
	      &HormannUAP1::NowClosed,
	      &HormannUAP1::NowOpening,
	      &HormannUAP1::SwitchPressed,
	      &HormannUAP1::DoNowt },  // Unknown
	    { &HormannUAP1::NowOpen,
	      &HormannUAP1::NowClosing,
	      &HormannUAP1::NowClosed,
	      &HormannUAP1::NowOpening,
	      &HormannUAP1::SwitchPressed,
	      &HormannUAP1::DoNowt }  // Bad
	};
};

// ─────────────────────────────────────────────────────────────────────────────
// DoorStatusPin — InputPin that fires HormannUAP1 events on match/unmatch
// ─────────────────────────────────────────────────────────────────────────────
class DoorStatusPin : public InputPin
{
public:
	DoorStatusPin ( HormannUAP1* pDoor,
	                HormannUAP1::Event matchEvent,
	                HormannUAP1::Event unmatchEvent,
	                pin_size_t pin,
	                uint32_t debouncems,
	                uint32_t maxMatchedTimems,
	                PinStatus matchStatus,
	                PinMode mode = PinMode::INPUT,
	                PinStatus status = PinStatus::CHANGE );

private:
	HormannUAP1* m_pDoor;
	HormannUAP1::Event m_doorMatchEvent;
	HormannUAP1::Event m_doorUnmatchEvent;
	void MatchAction () const;
	void UnmatchAction () const;
};

// ─────────────────────────────────────────────────────────────────────────────
// DoorStatusCalc — derives door state from UAP status pin readings
// ─────────────────────────────────────────────────────────────────────────────
class DoorStatusCalc
{
public:
	DoorStatusCalc ( DoorStatusPin& openPin, DoorStatusPin& closePin );
	void UpdateStatus ();
	IGarageDoor::State GetDoorState () const;
	void SetDoorState ( IGarageDoor::State state );
	HormannUAP1::Direction GetDoorDirection () const;
	void SetDoorDirection ( HormannUAP1::Direction direction );
	const char* GetDoorDirectionName () const;
	void SetStopped ();

private:
	DoorStatusPin& m_openPin;
	DoorStatusPin& m_closePin;
	volatile IGarageDoor::State m_currentState;
	volatile HormannUAP1::Direction m_LastDirection = HormannUAP1::Direction::None;
};

#pragma once
/*

DoorState.h

DoorState is the definition of a class that handles a garage door and uses the StateTable library to maintain the door state and execute actions as
events occur. External events come from a transitory switch which is used to open / close and stop the door.

Further external events comne from a hormann UAP 1 controller that is used to get changes in door information (is closed / is open / light on)

The hormann UAP 1 is also used to control the door with commands to start opening, start closing, stop the door and to turn on the light.

Author: (c) M. Naylor 2022

History:
	Ver 1.0			Initial version
*/
#include <stdint.h>
#include <MNRGBLEDBaseLib.h>
#include "InputPin.h"

typedef uint8_t pin_size_t;
#ifndef NOT_A_PIN
const uint8_t NOT_A_PIN = 255;
#endif

// Colours used on MKR RGB LED to indicate status
constexpr RGBType STATE_UNKNOWN_COLOUR		= MNRGBLEDBaseLib::WHITE;
constexpr RGBType DOOR_CLOSED_COLOUR		= MNRGBLEDBaseLib::GREEN;
constexpr RGBType DOOR_OPEN_COLOUR			= MNRGBLEDBaseLib::RED;
constexpr RGBType DOOR_STOPPED_COLOUR		= MNRGBLEDBaseLib::DARK_MAGENTA;
constexpr RGBType DOOR_BAD_COLOUR			= MNRGBLEDBaseLib::DARK_YELLOW;
constexpr RGBType DOOR_UNKNOWN_COLOUR		= MNRGBLEDBaseLib::BLUE;
constexpr uint8_t DOOR_STATIONARY_FLASHTIME = 0;
constexpr uint8_t DOOR_MOVING_FLASHTIME		= 10; // 20 = 1 sec
constexpr uint8_t RELAY_ON					= LOW;
constexpr uint8_t RELAY_OFF					= HIGH;

class DoorStatusPin;

class DoorState
{
	protected:
	public:
		enum State : uint8_t { Open = 0, Opening, Closed, Closing, Stopped, Unknown, Bad };

		enum Event : uint8_t { DoorOpenTrue = 0, DoorOpenFalse, DoorClosedTrue, DoorClosedFalse, SwitchPress, Nothing };

		enum Request : uint8_t { LightOn = 0, LightOff, OpenDoor, CloseDoor, StopDoor };

	private:
		// State table functions called when event occurs
		void			 DoNowt ( Event ) {};
		void			 NowOpen ( Event event );
		void			 NowClosed ( Event event );
		void			 NowClosing ( Event event );
		void			 NowOpening ( Event event );
		void			 SwitchPressed ( Event event );

		DoorState::State GetDoorInitialState ();

		typedef void ( DoorState::*StateFunction ) ( Event );																						  // prototype of function to handle event
		const StateFunction StateTableFn [ 7 ][ 6 ] = { // [State][Event]
			{&DoorState::DoNowt,	  &DoorState::NowClosing, &DoorState::NowClosed, &DoorState::DoNowt,	 &DoorState::SwitchPressed, &DoorState::DoNowt}, // Actions when current state is Open
			{ &DoorState::NowOpen, &DoorState::DoNowt,	   &DoorState::NowClosed, &DoorState::DoNowt,	  &DoorState::SwitchPressed, &DoorState::DoNowt}, // Actions when current state is Opening
			{ &DoorState::NowOpen, &DoorState::DoNowt,	   &DoorState::DoNowt,	   &DoorState::NowOpening, &DoorState::SwitchPressed, &DoorState::DoNowt}, // Actions when current state is Closed
			{ &DoorState::NowOpen, &DoorState::DoNowt,	   &DoorState::NowClosed, &DoorState::DoNowt,	  &DoorState::SwitchPressed, &DoorState::DoNowt}, // Actions when current state is Closing
			{ &DoorState::NowOpen, &DoorState::DoNowt,	   &DoorState::NowClosed, &DoorState::DoNowt,	  &DoorState::SwitchPressed, &DoorState::DoNowt,},  // Actions when current state is Stopped
			{ &DoorState::NowOpen, &DoorState::NowClosing, &DoorState::NowClosed, &DoorState::NowOpening,  &DoorState::SwitchPressed, &DoorState::DoNowt},  // Actions when current state is Unknown
			{ &DoorState::NowOpen, &DoorState::NowClosing, &DoorState::NowClosed, &DoorState::NowOpening,  &DoorState::SwitchPressed, &DoorState::DoNowt}  // Actions when current state is Bad
		};

		volatile State	 m_theDoorState		 = State::Unknown;
		volatile bool	 m_bDoorStateChanged = true;
		const pin_size_t m_DoorOpenCtrlPin;				   // Used to request door is opened
		const pin_size_t m_DoorCloseCtrlPin;			   // Used to request door is closed
		const pin_size_t m_DoorStopCtrlPin;				   // Used to request door is stopped
		const pin_size_t m_DoorLightCtrlPin;			   // Used to request light is turned on or off
		const pin_size_t m_DoorOpenStatusPin;			   // Used to get status if door is open or not
		const pin_size_t m_DoorClosedStatusPin;			   // Used to get status if door is closed or not
		const pin_size_t m_DoorLightStatusPin;			   // Used to get status if door light is on or not

		DoorStatusPin	*m_pDoorOpenStatusPin	= nullptr; // Objects to handle pins giving door status
		DoorStatusPin	*m_pDoorClosedStatusPin = nullptr;
		DoorStatusPin	*m_pDoorLightStatusPin	= nullptr;

		enum Direction : uint8_t { Up, Down, None };

		volatile Direction m_LastDirection = Direction::None;
		void			   ClearRelayPin ( pin_size_t thePin );
		void			   ResetTimer ();
		void			   SetRelayPin ( pin_size_t thePin );
		void			   TurnOffControlPins (); // bring low all pins controlling garage functions

	public:
		DoorState ( pin_size_t OpenPin, pin_size_t ClosePin, pin_size_t StopPin, pin_size_t LightPin, pin_size_t DoorOpenStatusPin, pin_size_t DoorClosedStatusPin, pin_size_t DoorLightStatusPin );
		void		DoEvent ( Event eEvent );
		void		DoRequest ( Request eRequest );
		const char *GetDoorDisplayState ();
		State		GetDoorState ();
		bool		IsOpen ();
		bool		IsMoving ();
		bool		IsClosed ();
		bool		IsLit ();
		uint32_t	GetLightOnCount ();
		uint32_t	GetLightOffCount ();
		uint32_t	GetDoorOpenedCount ();
		uint32_t	GetDoorOpeningCount ();
		uint32_t	GetDoorClosedCount ();
		uint32_t	GetDoorClosingCount ();
};

/// @brief DoorStatusPin is a type of InputPin that performs additional actions when the pin matches or fails to match the required state
class DoorStatusPin : public InputPin
{
	public:
		DoorStatusPin ( DoorState *pDoor, DoorState::Event matchEvent, DoorState::Event unmatchEvent, pin_size_t pin, uint32_t debouncems, PinStatus matchStatus, PinMode mode = PinMode::INPUT, PinStatus status = PinStatus::CHANGE );

	private:
		DoorState		*m_pDoor;
		DoorState::Event m_doorMatchEvent;
		DoorState::Event m_doorUnmatchEvent;
		void			 MatchAction ();
		void			 UnmatchAction ();
};
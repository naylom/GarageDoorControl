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
typedef uint8_t Pin;
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

class DoorState
{
	protected:
	public:
		enum State : uint8_t { Open = 0, Opening, Closed, Closing, Stopped, Unknown, Bad };

		enum Event : uint8_t { DoorOpenTrue = 0, DoorOpenFalse, DoorClosedTrue, DoorClosedFalse, SwitchPress };

		enum Request : uint8_t { LightOn = 0, LightOff, OpenDoor, CloseDoor, StopDoor };

	private:
		// State table functions called when event occurs
		void DoNowt ( Event ) {};
		void NowOpen ( Event event );
		void NowClosed ( Event event );
		void NowClosing ( Event event );
		void NowOpening ( Event event );
		void SwitchPressed ( Event event );

		typedef void ( DoorState::*StateFunction ) ( Event ); // prototype of function to handle event
		StateFunction StateTableFn [ 5 ][ 5 ] = {
			{&DoorState::DoNowt,	  &DoorState::NowClosing, &DoorState::NowClosed, &DoorState::DoNowt,	 &DoorState::SwitchPressed}, // Actions when current state is Open
			{ &DoorState::NowOpen, &DoorState::DoNowt,	   &DoorState::NowClosed, &DoorState::DoNowt,	  &DoorState::SwitchPressed}, // Actions when current state is Opening
			{ &DoorState::NowOpen, &DoorState::DoNowt,	   &DoorState::DoNowt,	   &DoorState::NowOpening, &DoorState::SwitchPressed}, // Actions when current state is Closed
			{ &DoorState::NowOpen, &DoorState::DoNowt,	   &DoorState::NowClosed, &DoorState::DoNowt,	  &DoorState::SwitchPressed}, // Actions when current state is Closing
			{ &DoorState::NowOpen, &DoorState::DoNowt,	   &DoorState::NowClosed, &DoorState::DoNowt,	  &DoorState::SwitchPressed}  // Actions when current state is Stopped
		};

		volatile State m_theDoorState	   = State::Unknown;
		volatile bool  m_bDoorStateChanged = true;
		Pin			   m_OpenPin;
		Pin			   m_ClosePin;
		Pin			   m_StopPin;
		Pin			   m_LightPin;

		enum Direction { Up, Down, None };

		volatile Direction m_LastDirection = Direction::None;
		void			   ClearRelayPin ( Pin thePin );
		void			   ResetTimer ();
		void			   SetRelayPin ( Pin thePin );
		void			   TurnOffControlPins (); // bring low all pins controlling garage functions
		static void		   TurnOff ();

	public:
		DoorState ( Pin OpenPin, Pin ClosePin, Pin StopPin, Pin LightPin, State initialState );
		void		DoEvent ( Event eEvent );
		void		DoRequest ( Request eRequest );
		const char *GetDoorDisplayState ();
		State		GetDoorState ();
		bool		IsOpen ();
		bool		IsMoving ();
		bool		IsClosed ();
};
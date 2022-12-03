#pragma once

#define CALL_MEMBER_FN(object,ptrToMember)  ((object).*(ptrToMember))
typedef uint8_t Pin;
#ifndef NOT_A_PIN
const int NOT_A_PIN = -1;
#endif
#include <MNBaseStateTable.h>
#include "MNRGBLEDBaseLib.h"

// Colours used on MKR RGB LED to indicate status
const RGBType STATE_UNKNOWN				= MNRGBLEDBaseLib::WHITE;
const RGBType DOOR_CLOSED_COLOUR		= MNRGBLEDBaseLib::GREEN;
const RGBType DOOR_OPEN_COLOUR			= MNRGBLEDBaseLib::RED;
const RGBType DOOR_STOPPED_COLOUR		= MNRGBLEDBaseLib::MAGENTA;
const uint8_t DOOR_STATIONARY_FLASHTIME	= 0;
const uint8_t DOOR_MOVING_FLASHTIME		= 10;

class DoorState
{
protected:

public:
// State table
	enum State : uint16_t { Opened = 1, Closed, Stopped, Opening, Closing };
	enum Event : uint16_t { DoorOpened = 1, DoorClosed, SwitchPressed, LightOff, LightOn };
	enum Light : uint16_t { On, Off, Unknown };
private:
	// State table functions
	uint16_t		DoOpen ( uint32_t ulParam );				// function called to Open door
	uint16_t		DoClose ( uint32_t ulParam );				// function called to Close door
	uint16_t		SetState ( uint32_t ulParam );				// function to set the door state
	uint16_t		DoStop ( uint32_t ulParam );				// function called to Stop door Opening
	uint16_t		DoNowt ( uint32_t ulParam );				// function called when nothing to be done
	uint16_t		DoReverse ( uint32_t ulParam );				// function called when we need to reverse direction

	MNBaseStateTable<DoorState>::STATE_TABLE_ENTRY	m_DoorStateTable [15]
	{
		{ State::Opened,	Event::DoorOpened,		&DoorState::DoNowt },		// Already open, do nothing
		{ State::Opened,	Event::DoorClosed,		&DoorState::SetState },		// Set State
		{ State::Opened,	Event::SwitchPressed,	&DoorState::DoClose },		// Already open, so Close
		{ State::Opening,	Event::DoorOpened,		&DoorState::SetState },		// Set State
		{ State::Opening,	Event::DoorClosed,		&DoorState::SetState },		// Set State
		{ State::Opening,	Event::SwitchPressed,	&DoorState::DoStop },		// Already opening so stop
		{ State::Closed,	Event::DoorOpened,		&DoorState::SetState },		// Set State
		{ State::Closed,	Event::DoorClosed,		&DoorState::DoClose },		// Already closed  but be safe and try again
		{ State::Closed,	Event::SwitchPressed,	&DoorState::DoOpen },		// Already closed so now open
		{ State::Closing,	Event::DoorOpened,		&DoorState::SetState },		// Set State
		{ State::Closing,	Event::DoorClosed,		&DoorState::SetState },		// Set State
		{ State::Closing,	Event::SwitchPressed,	&DoorState::DoStop },		// Already closing so stop
		{ State::Stopped,	Event::DoorOpened,		&DoorState::SetState },		// Set State
		{ State::Stopped,	Event::DoorClosed,		&DoorState::SetState },		// Set State
		{ State::Stopped,	Event::SwitchPressed,	&DoorState::DoReverse }		// Already stopped so reverse direction
	};

	MNBaseStateTable<DoorState>	m_State;
	Pin			m_OpenPin;
	Pin			m_ClosePin;
	Pin			m_StopPin;
	Pin			m_LightPin;
	Light		m_LightState = Unknown;
	enum		Direction { Up, Down, None };
	Direction	m_LastDirection = Direction::None;

	void 		ResetTimer ();
	void 		TurnOffControlPins ();									// bring low all pins controlling garage functions
	static void TurnOff();
public:
				DoorState ( Pin OpenPin, Pin ClosePin, Pin StopPin, Pin LightPin );
	bool 		DoEvent ( Event eEvent, uint32_t ulParam = 0UL );
	String		GetDoorState ();
	State 		GetState ();
	bool		IsOpen ();
	bool		IsMoving ();
	bool		IsClosed ();
	bool		IsLightOn();
	bool		IsLightOff();
	String 		GetLightState ();
};
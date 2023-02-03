#include <MNTimerLib.h>
#include "DoorState.h"
#include "Logging.h"
/*

DoorState.cpp

Implments DoorState.h

DoorState is a class that handles a garage door and uses the StateTable library to maintain the door state and execute actions as
events occur. External events come from a transitory switch which is used to open / close and stop the door. 

Further external events comne from a hormann UAP 1 controller that is used to get changes in door information (is closed / is open / light on)

The hormann UAP 1 is also used to control the door with commands to start opening, start closing, stop the door and to turn on the light. These are
sent as 1 second pulses. The timing function relies on the MNTimerLib library.

The class also sets a colour RGB LED to indicate the door status. The LED depends on the MNRGBLEDBaseLib library.

Author: (c) M. Naylor 2022

History:
	Ver 1.0			Initial version
*/
#define CALL_MEMBER_FN(object,ptrToMember)  ((object)->*(ptrToMember))
constexpr 	auto 					DOOR_FLASHTIME 	= 10;					// every 2 seconds
const 		int16_t 				SIGNAL_PULSE	= 2000 * 5;				// 2000 per sec, so every 1/5 sec, 200 ms
const char* StateNames []= 								// In order of State enums!
{
	"Opened",
	"Opening",
	"Closed",	
	"Closing",
	"Stopped",
	"Bad",
	"Unknown"
};
					DoorState::DoorState ( Pin OpenPin, Pin ClosePin, Pin StopPin, Pin LightPin, State initialState )
{
	m_OpenPin = OpenPin;
    pinMode ( m_OpenPin, OUTPUT );
	m_ClosePin = ClosePin;
    pinMode ( m_ClosePin, OUTPUT );
	m_StopPin = StopPin;
    pinMode ( m_StopPin, OUTPUT );
	m_LightPin = LightPin;
    pinMode ( m_LightPin, OUTPUT );
    m_theDoorState = initialState;

	TurnOffControlPins();
}
// called to turn relay off
void				DoorState::ClearRelayPin ( Pin thePin )
{
	if (  thePin != NOT_A_PIN && digitalRead ( thePin ) == RELAY_ON )
	{
		digitalWrite ( thePin, RELAY_OFF );
	}
}
// called to turn relay on
void				DoorState::SetRelayPin ( Pin thePin )
{
	if (  thePin != NOT_A_PIN && digitalRead ( thePin ) == RELAY_OFF )
	{
		digitalWrite ( thePin, RELAY_ON );
	}
}
// routines called when event occurs, these are called from within an interrupt and need to be short
// care to be taken to not invoke WifiNina calls or SPI calls to built in LED.
void DoorState::NowOpen ( Event  )
{
	// State has changed
	m_theDoorState = State::Open;
	m_LastDirection = Direction::Up;
	m_bDoorStateChanged = true;
}
void DoorState::NowClosed ( Event  )
{
	// State has changed
	m_theDoorState = State::Closed;
	m_LastDirection = Direction::Down;
	m_bDoorStateChanged = true;
}
void DoorState::NowClosing ( Event  )
{
	// State has changed
	m_theDoorState = State::Closing;
	m_LastDirection = Direction::Down;
	m_bDoorStateChanged = true;	
}
void DoorState::NowOpening ( Event  )
{
	// State has changed
	m_theDoorState = State::Opening;
	m_LastDirection = Direction::Up;
	m_bDoorStateChanged = true;
}

/// <summary>
/// SwitchPressed - controls action when switch pressed
/// </summary>
/// <param name="Event">event that occurred
/// <returns>None
void DoorState::SwitchPressed ( Event  )
{
	Error( "Switch Pressed                  ");	
#ifndef MNDEBUG
    switch ( m_theDoorState )
	{
		case State::Closed:
			// Open door
			ResetTimer();
			// rely on UAP outpins to signal this is happening
			SetRelayPin ( m_OpenPin );
			break;

		case  State::Open:
			// Close Door
			ResetTimer();
			// rely on UAP outpins to signal this is happening
			Error( "Door open - close pin on                  ");
			SetRelayPin ( m_ClosePin );
			break;

		case State::Opening:
		case State::Closing:
			// Stop Door
			ResetTimer();
			SetRelayPin ( m_StopPin );
			// Have to set state since there is no UAP output that signals when this happens
			m_theDoorState = Stopped;
			break;

		case State::Stopped:
			// go in reverse
			switch ( m_LastDirection )
			{
				case  Direction::Down:
					// Were closing so now open
					ResetTimer();
					Error ( "Door stopped, was going down - open pin on");
					SetRelayPin ( m_OpenPin );
					break;

				case  Direction::Up:
					ResetTimer();
					Error ( "Door stopped, was going up - close pin on");
					SetRelayPin (m_ClosePin );
					break;

				default:
					break;
			}
			
			break;

		case State::Bad:
		case State::Unknown:
			break;
	}
#endif    
}

/// <summary>
/// ResetTimer - Clears any extant timer and recreates it
/// </summary>
/// <returns>None
void                DoorState::ResetTimer ()
{
	TurnOffControlPins();
	if ( !TheTimer.AddCallBack (  (MNTimerClass*)this, (aMemberFunction)&DoorState::TurnOffControlPins, SIGNAL_PULSE ) )
	{
		Error ( "Timer callback add failed" );
	}
}

/// <summary>
/// DoEvent - Invokes the statetable to execute appropriate action for provided event
/// </summary>
/// <param name="eEvent">event that occurred
/// <returns>None
void				DoorState::DoEvent ( DoorState::Event eEvent )
{
    CALL_MEMBER_FN ( this, StateTableFn [ m_theDoorState ][ eEvent ] )( eEvent ) ;
}

/// <summary>
/// DoRequest - processes request to manipulate door
/// </summary>
/// <param name="eRequest">Request that occurred
/// <returns>None
void				DoorState::DoRequest ( Request eRequest )
{
	switch ( eRequest )
	{
		case Request::LightOn:
			ResetTimer();
			SetRelayPin ( m_LightPin );
			break;

		case Request::LightOff:
			ClearRelayPin ( m_LightPin );
			break;

		case Request::CloseDoor:
			ResetTimer();
			SetRelayPin ( m_ClosePin );
			break;

		case Request::OpenDoor:
			ResetTimer();
			SetRelayPin ( m_OpenPin );		
			break;

		case Request::StopDoor:
			ResetTimer();
			SetRelayPin ( m_StopPin );		
			break;
	}
}

/// <summary>
/// GetState - Gets current state
/// </summary>
/// <returns> current state
DoorState::State	DoorState::GetDoorState ()
{
	return m_theDoorState;
}

/// <summary>
/// GetDoorDisplayState - Gets current state
/// </summary>
/// <returns> current state as string
const char *        DoorState::GetDoorDisplayState ()
{
	return StateNames [ m_theDoorState ];
}

/// <summary>
/// IsOpen - checks if door is open
/// </summary>
/// <returns> true if Open else false
bool				DoorState::IsOpen ()
{
	return m_theDoorState == DoorState::State::Open ? true : false;
}

/// <summary>
/// IsMoving - checks if door is moving
/// </summary>
/// <returns> true if opening or closing
bool				DoorState::IsMoving ()
{
	return m_theDoorState == DoorState::State::Opening || m_theDoorState == DoorState::State::Closing ? true : false;
}

/// <summary>
/// IsClosed - checks if door is closing
/// </summary>
/// <returns> true if Closed else false
bool				DoorState::IsClosed ()
{
	return m_theDoorState == DoorState::State::Closed ? true : false;
}

/// <summary>
/// TurnOffControlPins - Sets all relay control pins
/// </summary>
/// <returns>none
void DoorState::TurnOffControlPins ()
{
	TheTimer.RemoveCallBack ( (MNTimerClass*)this, (aMemberFunction)&DoorState::TurnOffControlPins );
	ClearRelayPin ( m_ClosePin );
	ClearRelayPin ( m_StopPin );
	ClearRelayPin ( m_LightPin );
	ClearRelayPin ( m_OpenPin );
}

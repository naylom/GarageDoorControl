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
#define CALL_MEMBER_FN( object, ptrToMember ) ( ( object )->*( ptrToMember ) )
constexpr auto		DOOR_FLASHTIME = 10;			 // every 2 seconds
const int16_t		SIGNAL_PULSE   = 2000 * 5;		 // 2000 per sec, so every 1/5 sec, 200 ms
constexpr uint32_t	DEBOUNCE_MS	   = 75;			 // min ms between consecutive pin interrupts before signal accepted
constexpr PinStatus UAP_TRUE	   = PinStatus::LOW; // UAP signals LOW when sensor is TRUE

const char		   *StateNames []  =				 // In order of State enums!
	{ "Opened", "Opening", "Closed", "Closing", "Stopped", "Bad", "Unknown" };

DoorState::DoorState ( pin_size_t OpenPin, pin_size_t ClosePin, pin_size_t StopPin, pin_size_t LightPin, pin_size_t DoorOpenStatusPin, pin_size_t DoorClosedStatusPin, pin_size_t DoorLightStatusPin )
	: m_DoorOpenCtrlPin ( OpenPin ), m_DoorCloseCtrlPin ( ClosePin ), m_DoorStopCtrlPin ( StopPin ), m_DoorLightCtrlPin ( LightPin ), m_DoorOpenStatusPin ( DoorOpenStatusPin ), m_DoorClosedStatusPin ( DoorClosedStatusPin ), m_DoorLightStatusPin ( DoorLightStatusPin )
{
	m_pDoorOpenStatusPin   = new DoorStatusPin ( this, DoorState::Event::DoorOpenTrue, DoorState::Event::DoorOpenFalse, m_DoorOpenStatusPin, DEBOUNCE_MS, UAP_TRUE, PinMode::INPUT, PinStatus::CHANGE );
	m_pDoorClosedStatusPin = new DoorStatusPin ( this, DoorState::Event::DoorClosedTrue, DoorState::Event::DoorClosedFalse, m_DoorClosedStatusPin, DEBOUNCE_MS, UAP_TRUE, PinMode::INPUT, PinStatus::CHANGE );
	m_pDoorLightStatusPin  = new DoorStatusPin ( nullptr, DoorState::Event::Nothing, DoorState::Event::Nothing, m_DoorLightStatusPin, DEBOUNCE_MS, UAP_TRUE, PinMode::INPUT, PinStatus::CHANGE );
	m_pDoorOpenCtrlPin	   = new OutputPin ( m_DoorOpenCtrlPin, RELAY_ON );
	m_pDoorCloseCtrlPin	   = new OutputPin ( m_DoorCloseCtrlPin, RELAY_ON );
	m_pDoorStopCtrlPin	   = new OutputPin ( m_DoorStopCtrlPin, RELAY_ON );
	m_pDoorLightCtrlPin	   = new OutputPin ( m_DoorLightCtrlPin, RELAY_ON );
	/*
		pinMode ( m_DoorOpenCtrlPin, OUTPUT );
		pinMode ( m_DoorCloseCtrlPin, OUTPUT );
		pinMode ( m_DoorStopCtrlPin, OUTPUT );
		pinMode ( m_DoorLightCtrlPin, OUTPUT );
	*/
	TurnOffControlPins ();

	m_theDoorState = GetDoorInitialState ();
}

// Find initial state of door
DoorState::State DoorState::GetDoorInitialState ()
{
	//  ensure we can read from pins connected to UAP1 outputs

	bool	OpenState;
	bool	CloseState;

	CloseState	   = m_pDoorClosedStatusPin->IsMatched ();
	OpenState	   = m_pDoorOpenStatusPin->IsMatched ();

	if ( OpenState == CloseState )
	{
		// still not resolved, assume stopped
		return DoorState::State::Stopped;
	}
	else
	{
		if ( OpenState )
		{
			return DoorState::State::Open;
		}
		else
		{
			return DoorState::State::Closed;
		}
	}
}
/*
// called to turn relay off
void DoorState::ClearRelayPin ( pin_size_t thePin )
{
	if ( thePin != NOT_A_PIN && digitalRead ( thePin ) == RELAY_ON )
	{
		digitalWrite ( thePin, (PinStatus)RELAY_OFF );
	}
}

// called to turn relay on
void DoorState::SetRelayPin ( pin_size_t thePin )
{
	if ( thePin != NOT_A_PIN && digitalRead ( thePin ) == RELAY_OFF )
	{
		digitalWrite ( thePin, (PinStatus)RELAY_ON );
	}
}
*/
// routines called when event occurs, these are called from within an interrupt and need to be short
// care to be taken to not invoke WifiNina calls or SPI calls to built in LED.
void DoorState::NowOpen ( Event )
{
	// State has changed
	m_theDoorState		= State::Open;
	m_LastDirection		= Direction::Up;
	m_bDoorStateChanged = true;
}

void DoorState::NowClosed ( Event )
{
	// State has changed
	m_theDoorState		= State::Closed;
	m_LastDirection		= Direction::Down;
	m_bDoorStateChanged = true;
}

void DoorState::NowClosing ( Event )
{
	// State has changed
	m_theDoorState		= State::Closing;
	m_LastDirection		= Direction::Down;
	m_bDoorStateChanged = true;
}

void DoorState::NowOpening ( Event )
{
	// State has changed
	m_theDoorState		= State::Opening;
	m_LastDirection		= Direction::Up;
	m_bDoorStateChanged = true;
}

/// <summary>
/// SwitchPressed - controls action when switch pressed
/// </summary>
/// <param name="Event">event that occurred
/// <returns>None
void DoorState::SwitchPressed ( Event )
{
	Error ( "Switch Pressed                  " );
#ifndef MNDEBUGxx
	switch ( m_theDoorState )
	{
		case State::Closed:
			// Open door
			ResetTimer ();
			// rely on UAP outpins to signal this is happening
			Error ( "Door closed - open pin on                  " );
			m_pDoorOpenCtrlPin->On();
			//SetRelayPin ( m_DoorOpenCtrlPin );
			break;

		case State::Open:
			// Close Door
			ResetTimer ();
			// rely on UAP outpins to signal this is happening
			Error ( "Door open - close pin on                  " );
			m_pDoorCloseCtrlPin->On();
			//SetRelayPin ( m_DoorCloseCtrlPin );
			break;

		case State::Opening:
		case State::Closing:
			// Stop Door
			ResetTimer ();
			m_pDoorStopCtrlPin->On();
			//SetRelayPin ( m_DoorStopCtrlPin );
			// Have to set state since there is no UAP output that signals when this happens
			m_theDoorState = Stopped;
			break;

		case State::Stopped:
			// go in reverse
			switch ( m_LastDirection )
			{
				case Direction::Down:
					// Were closing so now open
					ResetTimer ();
					Error ( "Door stopped, was going down - open pin on" );
					m_pDoorOpenCtrlPin->On();
					//SetRelayPin ( m_DoorOpenCtrlPin );
					break;

				case Direction::Up:
					ResetTimer ();
					Error ( "Door stopped, was going up - close pin on" );
					m_pDoorCloseCtrlPin->On();
					//SetRelayPin ( m_DoorCloseCtrlPin );
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
void DoorState::ResetTimer ()
{
	TurnOffControlPins ();
	if ( !TheTimer.AddCallBack ( (MNTimerClass *)this, (aMemberFunction)&DoorState::TurnOffControlPins, SIGNAL_PULSE ) )
	{
		Error ( "Timer callback add failed" );
	}
}

/// <summary>
/// DoEvent - Invokes the statetable to execute appropriate action for provided event
/// </summary>
/// <param name="eEvent">event that occurred
/// <returns>None
void DoorState::DoEvent ( DoorState::Event eEvent )
{
	CALL_MEMBER_FN ( this, StateTableFn [ m_theDoorState ][ eEvent ] ) ( eEvent );
}

/// <summary>
/// DoRequest - processes request to manipulate door
/// </summary>
/// <param name="eRequest">Request that occurred
/// <returns>None
void DoorState::DoRequest ( Request eRequest )
{
	switch ( eRequest )
	{
		case Request::LightOn:
			ResetTimer ();
			m_pDoorLightCtrlPin->On();
			//SetRelayPin ( m_DoorLightCtrlPin );
			break;

		case Request::LightOff:
			//ClearRelayPin ( m_DoorLightCtrlPin );
			m_pDoorLightCtrlPin->Off();
			break;

		case Request::CloseDoor:
			ResetTimer ();
			m_pDoorCloseCtrlPin->On();
			//SetRelayPin ( m_DoorCloseCtrlPin );
			break;

		case Request::OpenDoor:
			ResetTimer ();
			m_pDoorOpenCtrlPin->On();
			//SetRelayPin ( m_DoorOpenCtrlPin );
			break;

		case Request::StopDoor:
			ResetTimer ();
			m_pDoorStopCtrlPin->On();
			//SetRelayPin ( m_DoorStopCtrlPin );
			break;
	}
}

/// <summary>
/// GetState - Gets current state
/// </summary>
/// <returns> current state
DoorState::State DoorState::GetDoorState ()
{
	return m_theDoorState;
}

/// <summary>
/// GetDoorDisplayState - Gets current state
/// </summary>
/// <returns> current state as string
const char *DoorState::GetDoorDisplayState ()
{
	return StateNames [ m_theDoorState ];
}

/// <summary>
/// IsOpen - checks if door is open
/// </summary>
/// <returns> true if Open else false
bool DoorState::IsOpen ()
{
	return m_theDoorState == DoorState::State::Open ? true : false;
}

/// <summary>
/// IsMoving - checks if door is moving
/// </summary>
/// <returns> true if opening or closing
bool DoorState::IsMoving ()
{
	return m_theDoorState == DoorState::State::Opening || m_theDoorState == DoorState::State::Closing ? true : false;
}

/// <summary>
/// IsClosed - checks if door is closing
/// </summary>
/// <returns> true if Closed else false
bool DoorState::IsClosed ()
{
	return m_theDoorState == DoorState::State::Closed ? true : false;
}

/// @brief IsLit - checks if Door Light is on
/// @return true if On else false
bool DoorState::IsLit ()
{
	return m_pDoorLightStatusPin->IsMatched ();
}

/// @brief get the number of time the Light has switched on
/// @return count of times the light was on
uint32_t DoorState::GetLightOnCount ()
{
	return m_pDoorLightStatusPin->GetMatchedCount ();
}

/// @brief get the number of time the Light has switched off
/// @return count of times the light was off
uint32_t DoorState::GetLightOffCount ()
{
	return m_pDoorLightStatusPin->GetUnmatchedCount ();
}

/// @brief get the number of time the Door Opened
/// @return count of times the door was in fully opened state
uint32_t DoorState::GetDoorOpenedCount ()
{
	return m_pDoorOpenStatusPin->GetMatchedCount ();
}

/// @brief get the number of time the door started opening
/// @return count of times the door was no longer closed
uint32_t DoorState::GetDoorOpeningCount ()
{
	return m_pDoorClosedStatusPin->GetUnmatchedCount ();
}

/// @brief get the number of time the door Closed
/// @return count of times the light was fully closed
uint32_t DoorState::GetDoorClosedCount ()
{
	return m_pDoorClosedStatusPin->GetMatchedCount ();
}

/// @brief get the number of time the door started closing
/// @return count of times the foor was not fully open
uint32_t DoorState::GetDoorClosingCount ()
{
	return m_pDoorOpenStatusPin->GetUnmatchedCount ();
}

/// <summary>
/// TurnOffControlPins - Sets all relay control pins
/// </summary>
/// <returns>none
void DoorState::TurnOffControlPins ()
{
	TheTimer.RemoveCallBack ( (MNTimerClass *)this, (aMemberFunction)&DoorState::TurnOffControlPins );
	m_pDoorOpenCtrlPin->Off();
	m_pDoorCloseCtrlPin->Off();
	m_pDoorStopCtrlPin->Off();
	m_pDoorLightCtrlPin->Off();
/*	
	ClearRelayPin ( m_DoorCloseCtrlPin );
	ClearRelayPin ( m_DoorStopCtrlPin );
	ClearRelayPin ( m_DoorLightCtrlPin );
	ClearRelayPin ( m_DoorOpenCtrlPin );
*/
}

DoorStatusPin::DoorStatusPin ( DoorState *pDoor, DoorState::Event matchEvent, DoorState::Event unmatchEvent, pin_size_t pin, uint32_t debouncems, PinStatus matchStatus, PinMode mode, PinStatus status )
	: InputPin ( pin, debouncems, matchStatus, mode, status ), m_pDoor ( pDoor ), m_doorMatchEvent ( matchEvent ), m_doorUnmatchEvent ( unmatchEvent )
{
}

void DoorStatusPin::MatchAction ()
{
	if ( m_pDoor != nullptr )
	{
		m_pDoor->DoEvent ( m_doorMatchEvent );
	}
}

void DoorStatusPin::UnmatchAction ()
{
	if ( m_pDoor != nullptr )
	{
		m_pDoor->DoEvent ( m_doorUnmatchEvent );
	}
}

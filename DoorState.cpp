#include <MNTimerLib.h>
#include "DoorState.h"
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

constexpr 	auto 					DOOR_FLASHTIME 	= 10;					// every 2 seconds
const 		int16_t 				SIGNAL_PULSE	= 2000 / 5;				// 2000 per sec, so every 1/5 sec, 200 ms

					DoorState::DoorState ( Pin OpenPin, Pin ClosePin, Pin StopPin, Pin LightPin, State initialState ) : m_State ( &m_DoorStateTable [ 0 ], ( sizeof ( m_DoorStateTable ) / sizeof ( m_DoorStateTable [ 0 ] ) ), initialState )
{
	m_OpenPin = OpenPin;
	m_ClosePin = ClosePin;
	m_StopPin = StopPin;
	m_LightPin = LightPin;
	if (  m_OpenPin != NOT_A_PIN )
	{
		pinMode ( m_OpenPin, OUTPUT );
	}
	if (  m_ClosePin != NOT_A_PIN )
	{
		pinMode ( m_ClosePin, OUTPUT );
	}
	if (  m_StopPin != NOT_A_PIN )
	{
		pinMode ( m_StopPin, OUTPUT );
	}
	if (  m_LightPin != NOT_A_PIN )
	{
		pinMode ( m_LightPin, OUTPUT );
	}
}
/// <summary>
/// DoNowt - Called when no action needed
/// </summary>
/// <param name="ulParam">simply used in return value
/// <returns>lower 16 bits of ulParam
uint16_t			DoorState::DoNowt ( uint32_t ulParam )
{
	return ulParam & 0xFFFF;
}
/// <summary>
/// DoOpen - Called To instruct door to open
/// clears all control signals to low and pulls Open Door Pin high
/// sets last direction to Up
/// </summary>
/// <param name="ulParam">unused
/// <returns>new state of Opening
uint16_t 			DoorState::DoOpen ( uint32_t ulParam )
{
	// pull pin high
	ResetTimer();
	digitalWrite ( m_OpenPin, HIGH );
	m_LastDirection = Direction::Up;
	TheMKR_RGB_LED.SetLEDColour( DOOR_OPEN_COLOUR, DOOR_MOVING_FLASHTIME );
	return DoorState::Opening;
}
/// <summary>
/// DoClose - Called To instruct door to close
/// clears all control signals to low and pulls Close Door Pin high
/// sets last direction to Down
/// </summary>
/// <param name="ulParam">unused
/// <returns>new state of Closing
uint16_t	 			DoorState::DoClose ( uint32_t ulParam )
{
	// pull pin high
	ResetTimer();
	digitalWrite ( m_ClosePin, HIGH );
	m_LastDirection = Direction::Down;
	TheMKR_RGB_LED.SetLEDColour( DOOR_CLOSED_COLOUR, DOOR_MOVING_FLASHTIME );
	return DoorState::State::Closing;
}
/// <summary>
/// DoStop - Called To instruct door to stop
/// clears all control signals to low and pulls Stop Door Pin high
/// </summary>
/// <param name="ulParam">unused
/// <returns>new state of Stopped
uint16_t				DoorState::DoStop ( uint32_t ulParam )
{
	// pull pin high
	ResetTimer();
	digitalWrite ( m_StopPin, HIGH );
	TheMKR_RGB_LED.SetLEDColour( DOOR_STOPPED_COLOUR, DOOR_STATIONARY_FLASHTIME );
	return DoorState::State::Stopped;
}
/// <summary>
/// SetState - Called to set door state
/// </summary>
/// <param name="ulParam">Low word contains new state
/// <returns>lower 16 bits of ulParam
uint16_t				DoorState::SetState ( uint32_t ulParam )
{
	TurnOffControlPins();
	return ulParam & 0xFFFF;
}
/// <summary>
/// DoReverse - Called To instruct door to reverse direction
/// if last direction is not none then calls either DoClose or DoOpen to reverse direction
/// </summary>
/// <param name="ulParam">passed to DoOpen/DoClose
/// <returns>response from DoOpen/DoClose
uint16_t				DoorState::DoReverse ( uint32_t ulParam )
{
	switch ( m_LastDirection )
	{
		case  Direction::Down:
			return DoOpen ( ulParam );
		case  Direction::Up:
			return DoClose ( ulParam );
		default:
			return GetState();
	}
}
/// <summary>
/// DoLightOn - Called to set the light status On
/// </summary>
/// <param name="ulParam">simply used in return value
/// <returns>lower 16 bits of ulParam
uint16_t				DoorState::DoLightOn ( uint32_t ulParam )
{
	m_LightState = Light::On;
	return ulParam & 0xFFFF;
}
/// <summary>
/// DoLightOff - Called to set the light status Off
/// </summary>
/// <param name="ulParam">simply used in return value
/// <returns>lower 16 bits of ulParam
uint16_t				DoorState::DoLightOff ( uint32_t ulParam )
{
	m_LightState = Light::Off;
	return ulParam & 0xFFFF;
}
void DoorState::ResetTimer ()
{
	TheTimer.RemoveCallBack ( (MNTimerClass*)this, (aMemberFunction)&DoorState::TurnOffControlPins );
	TurnOffControlPins();
	TheTimer.AddCallBack (  (MNTimerClass*)this, (aMemberFunction)&DoorState::TurnOffControlPins, SIGNAL_PULSE );
}
/// <summary>
/// Event - Invokes the statetable to execute appropriate action for provided event
/// if the ulParam is 0 will replace with current state. ulParam is passed to state table
/// </summary>
/// <param name="ulParam">passed to statetable unless value is 0 in whcih case current state is passed
/// <returns>response from StateTable
bool				DoorState::DoEvent ( DoorState::Event eEvent, uint32_t ulParam )
{
	if ( ulParam == 0UL )
	{
		ulParam = GetState();
	}
	return m_State.ProcessEvent ( this, eEvent, ulParam );
}
/// <summary>
/// GetState - Gets current state
/// </summary>
/// <returns> response from StateTable
DoorState::State	DoorState::GetState ()
{
	return (DoorState::State)m_State.GetCurrentState();
}
String		DoorState::GetDoorState ()
{
	switch ( GetState() )
	{
		case State::Closed:
			return "Closed";
		case State::Closing:
			return "Closing";
		case State::Opened:
			return "Opened";
		case State::Opening:
			return "Opening";
		case State::Stopped:
			return "Stopped";
		default:
			return "Unknown state";
	}
}
String 		DoorState::GetLightState ()
{
	switch ( m_LightState )
	{
		case Light::On:
			return "On";
		case Light::Off:
			return "Off";
		case Light::Unknown:
			return "Unknown";
		default:
			return "Unexpected State";
	}
}
bool				DoorState::IsLightOn()
{
	return m_LightState == DoorState::Light::On ? true : false;
}
bool				DoorState::IsLightOff()
{
	return m_LightState == DoorState::Light::Off ? true : false;
}
/// <summary>
/// IsOpen - checks if door is open
/// </summary>
/// <returns> true if Open else false
bool				DoorState::IsOpen ()
{
	return m_State.GetCurrentState() == DoorState::State::Opened ? true : false;
}
/// <summary>
/// IsMoving - checks if door is moving
/// </summary>
/// <returns> true if opening or closing
bool				DoorState::IsMoving ()
{
	return m_State.GetCurrentState() == DoorState::State::Opening || m_State.GetCurrentState() == DoorState::State::Closing ? true : false;
}
/// <summary>
/// IsClosed - checks if door is closing
/// </summary>
/// <returns> true if Closed else false
bool				DoorState::IsClosed ()
{
	return m_State.GetCurrentState() == DoorState::State::Closed ? true : false;
}
/// <summary>
/// TurnOffControlPins - Sets all control pins low
/// </summary>
/// <returns>none
void DoorState::TurnOffControlPins ()
{
	//Logln ( F ( "in TurnOffControlPins" ) );
	digitalWrite ( m_OpenPin, LOW );
	digitalWrite ( m_ClosePin, LOW );
	digitalWrite ( m_StopPin, LOW );
	digitalWrite ( m_LightPin, LOW );
}

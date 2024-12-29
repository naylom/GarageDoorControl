#include <MNTimerLib.h>
#include "DoorState.h"
#include "Logging.h"
#include "WiFiService.h"
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

extern UDPWiFiService *pMyUDPService;
extern void			   Error ( String s, bool bInISR = false );
extern void			   Info ( String s, bool bInISR = false );

constexpr auto		   DOOR_FLASHTIME	  = 10;				 // every 2 seconds
const int16_t		   SIGNAL_PULSE		  = 2000 * 2;		 // 2000 per sec, so every 1/5 sec, 200 ms
constexpr uint32_t	   DEBOUNCE_MS		  = 50;				 // min ms between consecutive pin interrupts before signal accepted
constexpr uint32_t	   MAX_MATCH_TIMER_MS = 1000;			 // max time pin should be in matched state to be considered a real signal
constexpr PinStatus	   UAP_TRUE			  = PinStatus::HIGH; // UAP signals HIGH when sensor is TRUE

const char			  *StateNames []	  = // In order of State enums!
	{ "Opened", "Opening", "Closed", "Closing", "Stopped", "Unknown", "Bad" };
const char *DirectionNames [] = // In order of enums
	{ "Up", "Down", "Stationary" };

DoorState::DoorState ( pin_size_t OpenPin, pin_size_t ClosePin, pin_size_t StopPin, pin_size_t LightPin, pin_size_t DoorOpenStatusPin, pin_size_t DoorClosedStatusPin, pin_size_t DoorLightStatusPin )
	: m_DoorOpenCtrlPin ( OpenPin ), 
	  m_DoorCloseCtrlPin ( ClosePin ), 
	  m_DoorStopCtrlPin ( StopPin ), 
	  m_DoorLightCtrlPin ( LightPin ), 
	  m_DoorOpenStatusPin ( DoorOpenStatusPin ), 
	  m_DoorClosedStatusPin ( DoorClosedStatusPin ), 
	  m_DoorLightStatusPin ( DoorLightStatusPin ),
	  //m_DoorSwitchStatusPin ( DoorSwitchPin ),
	  m_pDoorOpenStatusPin ( new DoorStatusPin ( this, DoorState::Event::Nothing, DoorState::Event::Nothing, m_DoorOpenStatusPin, DEBOUNCE_MS, MAX_MATCH_TIMER_MS, PinStatus::HIGH, PinMode::INPUT_PULLDOWN, PinStatus::CHANGE ) ),
	  m_pDoorClosedStatusPin ( new DoorStatusPin ( this, DoorState::Event::Nothing, DoorState::Event::Nothing, m_DoorClosedStatusPin, DEBOUNCE_MS, MAX_MATCH_TIMER_MS, PinStatus::HIGH, PinMode::INPUT_PULLDOWN, PinStatus::CHANGE ) ),
	  m_pDoorLightStatusPin  ( new DoorStatusPin ( nullptr, DoorState::Event::Nothing, DoorState::Event::Nothing, m_DoorLightStatusPin, DEBOUNCE_MS, 0, PinStatus::HIGH, PinMode::INPUT_PULLDOWN, PinStatus::CHANGE ) ),
	//m_pDoorSwitchStatusPin = new DoorStatusPin ( this, DoorState::Event::Nothing, DoorState::Event::SwitchPress, m_DoorSwitchStatusPin, SWITCH_DEBOUNCE_MS, MAX_SWITCH_MATCH_TIMER_MS, PinStatus::HIGH, PinMode::INPUT_PULLDOWN, PinStatus::CHANGE );	  
	  m_pDoorOpenCtrlPin ( new OutputPin ( m_DoorOpenCtrlPin, RELAY_ON_VALUE ) ),
	  m_pDoorCloseCtrlPin ( new OutputPin ( m_DoorCloseCtrlPin, RELAY_ON_VALUE ) ),
	  m_pDoorStopCtrlPin ( new OutputPin ( m_DoorStopCtrlPin, RELAY_ON_VALUE ) ),
	  m_pDoorLightCtrlPin ( new OutputPin ( m_DoorLightCtrlPin, RELAY_ON_VALUE ) ),
	  m_pDoorStatus ( new DoorStatusCalc ( *m_pDoorOpenStatusPin, *m_pDoorClosedStatusPin ) )
{
	// m_pDoorOpenStatusPin   = new DoorStatusPin ( this, DoorState::Event::DoorOpenTrue, DoorState::Event::DoorOpenFalse, m_DoorOpenStatusPin, DEBOUNCE_MS, HIGH, PinMode::INPUT_PULLDOWN, PinStatus::CHANGE );
	// m_pDoorOpenStatusPin   = new DoorStatusPin ( this, DoorState::Event::Nothing, DoorState::Event::Nothing, m_DoorOpenStatusPin, DEBOUNCE_MS, MAX_MATCH_TIMER_MS, HIGH, PinMode::INPUT_PULLDOWN, PinStatus::CHANGE );
	// m_pDoorClosedStatusPin = new DoorStatusPin ( this, DoorState::Event::DoorClosedTrue, DoorState::Event::DoorClosedFalse, m_DoorClosedStatusPin, DEBOUNCE_MS, HIGH, PinMode::INPUT_PULLDOWN, PinStatus::CHANGE );
	// m_pDoorClosedStatusPin = new DoorStatusPin ( this, DoorState::Event::Nothing, DoorState::Event::Nothing, m_DoorClosedStatusPin, DEBOUNCE_MS, MAX_MATCH_TIMER_MS, HIGH, PinMode::INPUT_PULLDOWN, PinStatus::CHANGE );
	// m_pDoorLightStatusPin  = new DoorStatusPin ( nullptr, DoorState::Event::Nothing, DoorState::Event::Nothing, m_DoorLightStatusPin, DEBOUNCE_MS, 0, HIGH, PinMode::INPUT_PULLDOWN, PinStatus::CHANGE );
	// m_pDoorOpenCtrlPin	   = new OutputPin ( m_DoorOpenCtrlPin, RELAY_ON );
	// m_pDoorCloseCtrlPin	   = new OutputPin ( m_DoorCloseCtrlPin, RELAY_ON );
	// m_pDoorStopCtrlPin	   = new OutputPin ( m_DoorStopCtrlPin, RELAY_ON );
	// m_pDoorLightCtrlPin	   = new OutputPin ( m_DoorLightCtrlPin, RELAY_ON );
	TurnOffControlPins ();
	// m_pDoorStatus = new DoorStatusCalc ( *m_pDoorOpenStatusPin, *m_pDoorClosedStatusPin );
	SetState ( GetDoorInitialState () );
}

// Find initial state of door
DoorState::State DoorState::GetDoorInitialState ()
{
	//  ensure we can read from pins connected to UAP1 outputs

	bool OpenState = m_pDoorOpenStatusPin->IsMatched ();
	bool CloseState = m_pDoorClosedStatusPin->IsMatched ();

	if ( OpenState == CloseState )
	{
		// still not resolved, assume stopped
		return DoorState::State::Stopped;
	}
	else
	{
		return OpenState ? DoorState::State::Open : DoorState::State::Closed;
	}
}

void DoorState::SetState ( DoorState::State newState )
{
	m_theDoorState == newState;
}

// routines called when event occurs, these are called from within an interrupt and need to be short
// care to be taken to not invoke WifiNina calls or SPI calls to built in LED.
void DoorState::NowOpen ( Event )
{
	// State has changed
	SetState ( State::Open );
	m_LastDirection		= Direction::Up;
	m_bDoorStateChanged = true;
}

void DoorState::NowClosed ( Event )
{
	// State has changed
	SetState ( State::Closed );
	m_LastDirection		= Direction::Down;
	m_bDoorStateChanged = true;
}

void DoorState::NowClosing ( Event )
{
	// State has changed
	SetState ( State::Closing );
	m_LastDirection		= Direction::Down;
	m_bDoorStateChanged = true;
}

void DoorState::NowOpening ( Event )
{
	// State has changed
	SetState ( State::Opening );
	m_LastDirection		= Direction::Up;
	m_bDoorStateChanged = true;
}

/**
 * @brief Handles the event when the switch is pressed.
 * 
 * This function determines the action to take based on the current state of the door
 * and the timing of the switch press. It can open, close, or stop the door.
 * 
 * @param Event The event that occurred.
 */
void DoorState::SwitchPressed ( Event )
{
	uint32_t now = millis();
	static bool bAwaitingSecondPush = false;
	if ( now - m_ulSwitchPressedTime > 2000UL )
	{
		// been > 2 secs since last switch press so restart looking for 2 consecutive pushes
		bAwaitingSecondPush = true;
	}
	else
	{
		if ( bAwaitingSecondPush )
		{
			// second push within 2 secs
			bAwaitingSecondPush = false;	// reset
			switch ( GetDoorState () )
			{
				case State::Closed:
					// Open door
					ResetTimer ();
					// rely on UAP outpins to signal this is happening
					Info ( F ( "Switch pressed when door closed - opening" ), true );
					m_pDoorOpenCtrlPin->On ();
					break;

				case State::Open:
					// Close Door
					ResetTimer ();
					// rely on UAP outpins to signal this is happening
					Info ( F ( "Switch pressed when door open - closing" ), true );
					m_pDoorCloseCtrlPin->On ();
					break;

				case State::Opening:
				case State::Closing:
					// Stop Door
					ResetTimer ();
					m_pDoorStopCtrlPin->On ();
					//  Have to set state since there is no UAP output that signals when this happens
					Info ( F ( "Switch pressed during moving, stopping door" ), true );
					m_pDoorStatus->SetStopped ();
					break;

				case State::Stopped:
					// go in reverse
					switch ( m_LastDirection )
					{
						case Direction::Down:
							// Were closing so now open
							ResetTimer ();
							Info ( F ( "Switch pressed when door stopped, was going down - opening" ), true );
							m_pDoorOpenCtrlPin->On ();
							break;

						case Direction::Up:
							ResetTimer ();
							Info ( F ( "Switch pressed when door stopped, was going up - closing" ), true );
							m_pDoorCloseCtrlPin->On ();
							break;

						default:
							break;
					}
					break;

				case State::Bad:
				case State::Unknown:
					Info ( F ( "Switch pressed when state is bad / unknown, doing nothing" ), true );
					break;
			}
		}
	}
	m_ulSwitchPressedTime = now;
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
		Error ( F ( "Timer callback add failed" ), true );
	}
}
/// <summary>
/// DoEvent - Invokes the statetable to execute appropriate action for provided event
/// </summary>
/// <param name="eEvent">event that occurred
/// <returns>None
void DoorState::DoEvent ( DoorState::Event eEvent )
{
	CALL_MEMBER_FN ( this, StateTableFn [ GetDoorState () ][ eEvent ] ) ( eEvent );
}
/**
 * @brief Processes request to manipulate the door
 * @param eRequest The request that occurred
 */
void DoorState::DoRequest(Request eRequest) 
{
    String result;
    auto handleRequest = [&](const char* action, OutputPin* pin) 
	{
        ResetTimer();
        result = action;
        pin->On();
    };

    switch (eRequest) {
        case Request::LightOn:
            handleRequest ( "Toggle Light On request" , m_pDoorLightCtrlPin);
            break;

        case Request::LightOff:
            handleRequest("Toggle Light Off request", m_pDoorLightCtrlPin);
            break;

        case Request::CloseDoor:
            handleRequest("Close Door request", m_pDoorCloseCtrlPin);
            break;

        case Request::OpenDoor:
            handleRequest("Open Door request", m_pDoorOpenCtrlPin);
            break;

        case Request::StopDoor:
            handleRequest("Stop Door request", m_pDoorStopCtrlPin);
            break;
    }

    Info(result);
}

/// <summary>
/// GetState - Gets current state
/// </summary>
/// <returns> current state
DoorState::State DoorState::GetDoorState ()
{
	// return m_theDoorState;
	return m_pDoorStatus->GetDoorState ();
}

/// <summary>
/// GetDoorDisplayState - Gets current state
/// </summary>
/// <returns> current state as string
const char *DoorState::GetDoorDisplayState ()
{
	return StateNames [ GetDoorState () ];
}

/// <summary>
/// IsOpen - checks if door is open
/// </summary>
/// <returns> true if Open else false
bool DoorState::IsOpen ()
{
	return GetDoorState () == DoorState::State::Open ? true : false;
}

/// <summary>
/// IsMoving - checks if door is moving
/// </summary>
/// <returns> true if opening or closing
bool DoorState::IsMoving ()
{
	return GetDoorState () == DoorState::State::Opening || GetDoorState () == DoorState::State::Closing ? true : false;
}

/// <summary>
/// IsClosed - checks if door is closing
/// </summary>
/// <returns> true if Closed else false
bool DoorState::IsClosed ()
{
	return GetDoorState () == DoorState::State::Closed ? true : false;
}

/// @brief IsLit - checks if Door Light is on
/// @return true if On else false
bool DoorState::IsLit ()
{
	return m_pDoorLightStatusPin->IsMatched ();
}

const char *DoorState::GetDoorDirection ()
{
	return m_pDoorStatus->GetDoorDirectionName ();
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

void DoorState::GetPinStates ( String &states )
{
	states	= String ( F ( "Light: " ) );
	states += m_pDoorLightStatusPin->IsMatched () ? F ( "On" ) : F ( "Off" );
	states += String ( F ( " Open: " ) );
	states += m_pDoorOpenStatusPin->IsMatched () ? F ( "On" ) : F ( "Off" );
	states += String ( F ( " Closed: " ) );
	states += m_pDoorClosedStatusPin->IsMatched () ? F ( "On" ) : F ( "Off" );
	states += String ( F ( " Curr Light: " ) );
	states += m_pDoorLightStatusPin->GetCurrentMatchedState () ? F ( "On" ) : F ( "Off" );
	states += String ( F ( " Opn: " ) );
	states += m_pDoorOpenStatusPin->GetCurrentMatchedState () ? F ( "On" ) : F ( "Off" );
	states += String ( F ( " Clsed: " ) );
	states += m_pDoorClosedStatusPin->GetCurrentMatchedState () ? F ( "On" ) : F ( "Off" );
}

void DoorState::UpdateDoorState ()
{
	if ( m_pDoorStatus != nullptr )
	{
		m_pDoorStatus->UpdateStatus ();
	}
}

/// <summary>
/// TurnOffControlPins - Sets all relay control pins
/// </summary>
/// <returns>none
void DoorState::TurnOffControlPins ()
{
	TheTimer.RemoveCallBack ( (MNTimerClass *)this, (aMemberFunction)&DoorState::TurnOffControlPins );
	m_pDoorOpenCtrlPin->Off ();
	m_pDoorCloseCtrlPin->Off ();
	m_pDoorStopCtrlPin->Off ();
	m_pDoorLightCtrlPin->Off ();
}

DoorStatusPin::DoorStatusPin ( DoorState *pDoor, DoorState::Event matchEvent, DoorState::Event unmatchEvent, pin_size_t pin, uint32_t debouncems, uint32_t maxMatchedTimems, PinStatus matchStatus, PinMode mode, PinStatus status )
	: InputPin ( pin, debouncems, maxMatchedTimems, matchStatus, mode, status ), m_pDoor ( pDoor ), m_doorMatchEvent ( matchEvent ), m_doorUnmatchEvent ( unmatchEvent )
{
	// Set pin to source 7mA and sink 10mA rather than default 2 / 2.5 mA, see https://forum.arduino.cc/t/are-the-zeros-pins-set-to-strong-drive-strength-by-default/404930
	PORT->Group[g_APinDescription[pin].ulPort].PINCFG[g_APinDescription[pin].ulPin].bit.DRVSTR = 1;
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

DoorStatusCalc::DoorStatusCalc ( DoorStatusPin &openPin, DoorStatusPin &closePin ) : m_closePin ( closePin ), m_openPin ( openPin )
{
	m_currentState	= DoorState::Unknown;
	m_LastDirection = Direction::None;
}

/// @brief sets the door status based on current input pin readings and prior state
void DoorStatusCalc::UpdateStatus ()
{
	bool bIsClosed = m_closePin.GetCurrentMatchedState ();
	bool bIsOpen   = m_openPin.GetCurrentMatchedState ();

	if ( bIsClosed == false && bIsOpen == false )
	{
		switch ( m_currentState )
		{
			case DoorState::State::Open:
				m_LastDirection = Direction::Down;
				m_currentState	= DoorState::State::Closing;
				break;

			case DoorState::State::Closed:
				m_LastDirection = Direction::Up;
				m_currentState	= DoorState::State::Opening;
				break;

			case DoorState::State::Bad:
				m_LastDirection = Direction::None;
				m_currentState	= DoorState::State::Unknown;
				break;

			default:
				// no change
				break;
		}
	}
	else if ( bIsClosed == false && bIsOpen == true )
	{
		m_currentState	= DoorState::State::Open;
		m_LastDirection = Direction::None;
	}
	else if ( bIsClosed == true && bIsOpen == false )
	{
		m_currentState	= DoorState::State::Closed;
		m_LastDirection = Direction::None;
	}
	else
	{
		// both true!
		m_currentState	= DoorState::State::Bad;
		m_LastDirection = Direction::None;
	}
}

DoorState::State DoorStatusCalc::GetDoorState ()
{
	return m_currentState;
}

DoorStatusCalc::Direction DoorStatusCalc::GetDoorDirection ()
{
	return m_LastDirection;
}

const char *DoorStatusCalc::GetDoorDirectionName ()
{
	return DirectionNames [ GetDoorDirection () ];
}

void DoorStatusCalc::SetStopped ()
{
	m_currentState = DoorState::State::Stopped;
}

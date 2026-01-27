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

constexpr uint32_t	   SWITCH_DEBOUNCE_MS		   	= 100;  				// min ms between consecutive pin interrupts before signal accepted from manual switch
constexpr uint32_t	   MAX_SWITCH_MATCH_TIMER_MS 	= 2000; 				// max time pin should be in matched state to be considered a real signal
constexpr auto		   DOOR_FLASHTIME	  			= 10;				 	// every 2 seconds
const int16_t		   SIGNAL_PULSE		  			= 2000 * 10;		 	// 2000 per sec, so every 1/5 sec, 200 ms
constexpr uint32_t	   DEBOUNCE_MS		  			= 50;				 	// min ms between consecutive pin interrupts before signal accepted
constexpr uint32_t	   MAX_MATCH_TIMER_MS 			= 1000;			 		// max time pin should be in matched state to be considered a real signal
constexpr PinStatus	   UAP_TRUE			  			= PinStatus::HIGH; 		// UAP signals HIGH when sensor is TRUE

const char			  *StateNames []	  = // In order of State enums!
	{ "Opened", "Opening", "Closed", "Closing", "Stopped", "Unknown", "Bad" };
const char *DirectionNames [] = // In order of Direction enums
	{ "Up", "Down", "Stationary" };

/**
 * @brief Constructs a `DoorState` object.
 * 
 * This constructor initializes a `DoorState` object with the specified pin configurations for controlling and monitoring the door.
 * It sets up the control pins for opening, closing, stopping, and lighting the door, as well as the status pins for detecting the door's state.
 * The constructor also initializes the debounce and match timers for the status pins and sets the initial state of the control pins to off.
 * 
 * @param OpenPin The pin used to control opening the door.
 * @param ClosePin The pin used to control closing the door.
 * @param StopPin The pin used to control stopping the door.
 * @param LightPin The pin used to control the door light.
 * @param DoorOpenStatusPin The pin used to detect if the door is open.
 * @param DoorClosedStatusPin The pin used to detect if the door is closed.
 * @param DoorLightStatusPin The pin used to detect the status of the door light.
 * @param DoorSwitchStatusPin The pin used to detect the status of the door switch.
 */
DoorState::DoorState ( pin_size_t OpenPin, pin_size_t ClosePin, pin_size_t StopPin, pin_size_t LightPin, pin_size_t DoorOpenStatusPin, pin_size_t DoorClosedStatusPin, pin_size_t DoorLightStatusPin, pin_size_t DoorSwitchStatusPin )
	: m_DoorOpenCtrlPin ( OpenPin ), 
	  m_DoorCloseCtrlPin ( ClosePin ), 
	  m_DoorStopCtrlPin ( StopPin ), 
	  m_DoorLightCtrlPin ( LightPin ), 
	  m_DoorOpenStatusPin ( DoorOpenStatusPin ), 
	  m_DoorClosedStatusPin ( DoorClosedStatusPin ), 
						  m_DoorLightStatusPin ( DoorLightStatusPin ),
	  m_DoorSwitchStatusPin ( DoorSwitchStatusPin ),
	  m_pDoorOpenStatusPin ( new DoorStatusPin ( this, DoorState::Event::Nothing, DoorState::Event::Nothing, m_DoorOpenStatusPin, DEBOUNCE_MS, MAX_MATCH_TIMER_MS, PinStatus::HIGH, PinMode::INPUT_PULLDOWN, PinStatus::CHANGE ) ),
	  m_pDoorClosedStatusPin ( new DoorStatusPin ( this, DoorState::Event::Nothing, DoorState::Event::Nothing, m_DoorClosedStatusPin, DEBOUNCE_MS, MAX_MATCH_TIMER_MS, PinStatus::HIGH, PinMode::INPUT_PULLDOWN, PinStatus::CHANGE ) ),
	  m_pDoorLightStatusPin  ( new DoorStatusPin ( nullptr, DoorState::Event::Nothing, DoorState::Event::Nothing, m_DoorLightStatusPin, DEBOUNCE_MS, 0, PinStatus::HIGH, PinMode::INPUT_PULLDOWN, PinStatus::CHANGE ) ),
	  m_pDoorSwitchStatusPin ( new DoorStatusPin ( this, DoorState::Event::Nothing, DoorState::Event::SwitchPress, m_DoorSwitchStatusPin, SWITCH_DEBOUNCE_MS, MAX_SWITCH_MATCH_TIMER_MS, PinStatus::HIGH, PinMode::INPUT_PULLDOWN, PinStatus::CHANGE ) ),
	  m_pDoorOpenCtrlPin ( new OutputPin ( m_DoorOpenCtrlPin, RELAY_ON_VALUE ) ),
	  m_pDoorCloseCtrlPin ( new OutputPin ( m_DoorCloseCtrlPin, RELAY_ON_VALUE ) ),
	  m_pDoorStopCtrlPin ( new OutputPin ( m_DoorStopCtrlPin, RELAY_ON_VALUE ) ),
	  m_pDoorLightCtrlPin ( new OutputPin ( m_DoorLightCtrlPin, RELAY_ON_VALUE ) )
{
	TurnOffControlPins ();
	delay ( 10 );
	m_pDoorStatus = new DoorStatusCalc ( *m_pDoorOpenStatusPin, *m_pDoorClosedStatusPin );
}

/**
 * @brief Sets the state of the door.
 * 
 * This function updates the state of the door by calling the `SetDoorState` method of the `m_pDoorStatus` object.
 * 
 * @param newState The new state to set for the door.
 */
void DoorState::SetDoorState ( DoorState::State newState )
{
	if ( m_pDoorStatus != nullptr )
	{
		m_pDoorStatus->SetDoorState ( newState );
	}
}

// Helper function to convert bool to "On"/"Off" string
static String onOff(bool state) 
{
	return state ? F("On") : F("Off");
}

void DoorState::SetStateAndDirection(State state, Direction direction)
{
	SetDoorState(state);
	SetDoorDirection(direction);
	m_bDoorStateChanged = true;
}

// routines called when event occurs, these are called from within an interrupt and need to be short
// care to be taken to not invoke WifiNina calls or SPI calls to built in LED.

/**
 * @brief Record state of door when UAP has signalled door is open.
 * 
 * This function updates the door state to `Open` and sets the door direction to `Up`.
 * It also marks that the door state has changed.
 * 
 * @param Event unused
 */
void DoorState::NowOpen ( Event )
{
	SetStateAndDirection(State::Open, Direction::Up);
}

/**
 * @brief Record state of door when UAP has signalled door is closed.
 * 
 * This function updates the door state to `Closed` and sets the door direction to `Down`.
 * It also marks that the door state has changed.
 * 
 * @param Event unused
 */
void DoorState::NowClosed(Event) 
{
	SetStateAndDirection(State::Closed, Direction::Down);
}

/**
 * @brief Record state of door when UAP has signalled door is closing.
 * 
 * This function updates the door state to `Closing` and sets the door direction to `Down`.
 * It also marks that the door state has changed.
 * 
 * @param Event unused
 */
void DoorState::NowClosing(Event) {
	SetStateAndDirection(State::Closing, Direction::Down);
}

/// @brief Record state of door when UAP has signalled door is opening
/// @param  unused
void DoorState::NowOpening ( Event )
{
	SetStateAndDirection(State::Opening, Direction::Up);
}

/**
 * @brief Handles the event when the switch is pressed.
 * 
 * This function determines the action to take based on the current state of the door
 * It can open, close, or stop the door.
 * 
 * @param Event unused
 */
void DoorState::SwitchPressed ( Event )
{
	uint32_t now = millis();
	switch ( GetDoorState () )
	{
		case State::Closed:
			// Open door
			ResetTimer ();
			// rely on UAP outpins to signal this is happening
			m_pDoorOpenCtrlPin->On ();
			break;

		case State::Open:
			// Close Door
			ResetTimer ();
			// rely on UAP outpins to signal this is happening
			m_pDoorCloseCtrlPin->On ();
			break;

		case State::Opening:
		case State::Closing:
			// Stop Door
			ResetTimer ();
			m_pDoorStopCtrlPin->On ();
			//  Have to set state since there is no UAP output that signals when this happens
			// do not change direction as we need to know what it was doing before we stopped so we can reverse if needed
			SetDoorState ( DoorState::State::Stopped );
			break;

		case State::Stopped:
			// go in reverse
			switch ( GetDoorDirection() )
			{
				case Direction::Down:
					// Were closing so now open
					ResetTimer ();
					m_pDoorOpenCtrlPin->On ();
					break;

				case Direction::Up:
					// was going up when stopped so now close
					ResetTimer ();
					m_pDoorCloseCtrlPin->On ();
					break;

				default:
					Info ( "Switch pressed when door stopped, unknown last direction - doing nothing", true );
					break;
			}
			break;

		case State::Bad:
		case State::Unknown:
			Info ( "Switch pressed when state is bad / unknown, doing nothing", true );
			break;
	}
	m_ulSwitchPressedTime = now;
}

/// @brief called from ISR when a UAP status pin is activated
/// @param direction direction door is moving in
void DoorState::SetDoorDirection ( DoorState::Direction direction )
{
	if ( m_pDoorStatus != nullptr )
	{
		m_pDoorStatus->SetDoorDirection ( direction );
	}	
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
	//CALL_MEMBER_FN ( this, DoorState::StateTableFn [ GetDoorState () ][ eEvent ] ) ( eEvent );
	(this->*DoorState::StateTableFn[(uint8_t)GetDoorState()][(uint8_t)eEvent])(eEvent); // Call the appropriate member function based on the current state and event
}
/**
 * @brief Processes request to manipulate the door
 * @param eRequest The request that occurred
 */
void DoorState::DoRequest(Request eRequest) 
{
    String result;
	auto handleRequest = [&](const char* action, std::unique_ptr<OutputPin>& pin) 
	{
        ResetTimer();
        result = action;
        pin->On();
    };

    switch (eRequest) 
	{
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
DoorState::State DoorState::GetDoorState () const
{
	// return m_theDoorState;
	if ( m_pDoorStatus != nullptr )
	{
		return m_pDoorStatus->GetDoorState ();
	}
	else
	{
		return DoorState::State::Unknown;
	}
}
/// <summary>
/// GetDirection - Gets current direction
/// </summary>
/// <returns> current state
DoorState::Direction DoorState::GetDoorDirection() const
{
	if ( m_pDoorStatus != nullptr )
	{
		return m_pDoorStatus->GetDoorDirection ();
	}
	else
	{
		return DoorState::Direction::None;
	}
}

/// <summary>
/// GetDoorDisplayState - Gets current state
/// </summary>
/// <returns> current state as string
const char *DoorState::GetDoorDisplayState () const
{
	return StateNames [ (uint8_t)GetDoorState () % ( sizeof ( StateNames ) / sizeof ( StateNames [ 0 ] ) )];
}

/// <summary>
/// IsOpen - checks if door is open
/// </summary>
/// <returns> true if Open else false
bool DoorState::IsOpen () const
{
	return GetDoorState () == DoorState::State::Open ? true : false;
}

/// <summary>
/// IsMoving - checks if door is moving
/// </summary>
/// <returns> true if opening or closing
bool DoorState::IsMoving () const
{
	return GetDoorState () == DoorState::State::Opening || GetDoorState () == DoorState::State::Closing ? true : false;
}

/// <summary>
/// IsClosed - checks if door is closing
/// </summary>
/// <returns> true if Closed else false
bool DoorState::IsClosed () const
{
	return GetDoorState () == DoorState::State::Closed ? true : false;
}

/// @brief IsLit - checks if Door Light is on
/// @return true if On else false
bool DoorState::IsLit () const
{
	return m_pDoorLightStatusPin->IsMatched ();
}

const char *DoorState::GetDoorDirectionName () const
{
	return m_pDoorStatus->GetDoorDirectionName ();
}

/**
 * @brief Checks if the door switch is configured.
 * 
 * This function checks if the door switch status pin is configured (not null).
 * 
 * @return true if the door switch is configured, false otherwise.
 */
bool DoorState::IsSwitchConfigured () const
{
	return m_pDoorSwitchStatusPin != nullptr;
}

uint32_t DoorState::GetSwitchMatchCount () const
{
	if ( IsSwitchConfigured() )
	{
		return m_pDoorSwitchStatusPin->GetMatchedCount ();
	}
	else
	{
		return 0;
	}
}

void DoorState::SwitchDebugStats ( String& result ) const
{
	if ( IsSwitchConfigured() )
	{
		m_pDoorSwitchStatusPin->DebugStats ( result );
	}
}

/// @brief get the number of time the Light has switched on
/// @return count of times the light was on
uint32_t DoorState::GetLightOnCount () const
{
	return m_pDoorLightStatusPin->GetMatchedCount ();
}

/// @brief get the number of time the Light has switched off
/// @return count of times the light was off
uint32_t DoorState::GetLightOffCount () const
{
	return m_pDoorLightStatusPin->GetUnmatchedCount ();
}

/// @brief get the number of time the Door Opened
/// @return count of times the door was in fully opened state
uint32_t DoorState::GetDoorOpenedCount () const
{
	return m_pDoorOpenStatusPin->GetMatchedCount ();
}

/// @brief get the number of time the door started opening
/// @return count of times the door was no longer closed
uint32_t DoorState::GetDoorOpeningCount () const
{
	return m_pDoorClosedStatusPin->GetUnmatchedCount ();
}

/// @brief get the number of time the door Closed
/// @return count of times the light was fully closed
uint32_t DoorState::GetDoorClosedCount () const
{
	return m_pDoorClosedStatusPin->GetMatchedCount ();
}

/// @brief get the number of time the door started closing
/// @return count of times the foor was not fully open
uint32_t DoorState::GetDoorClosingCount () const
{
	return m_pDoorOpenStatusPin->GetUnmatchedCount ();
}
/**
 * @brief Retrieves the current states of various door pins and compiles them into a string.
 * 
 * This function collects the states of the light, door open, and door closed pins, both their matched and current states.
 * It then formats these states into a readable string.
 * 
 * @param states A reference to a string where the pin states will be stored.
 */
void DoorState::GetPinStates ( String &states )
{
		states  = String ( F ( "Light: " ) );
		states += onOff(m_pDoorLightStatusPin->IsMatched());
		states += String ( F ( " Open: " ) );
		states += onOff(m_pDoorOpenStatusPin->IsMatched());
		states += String ( F ( " Closed: " ) );
		states += onOff(m_pDoorClosedStatusPin->IsMatched());
		states += String ( F ( " Curr Light: " ) );
		states += onOff(m_pDoorLightStatusPin->GetCurrentMatchedState());
		states += String ( F ( " Opn: " ) );
		states += onOff(m_pDoorOpenStatusPin->GetCurrentMatchedState());
		states += String ( F ( " Clsed: " ) );
		states += onOff(m_pDoorClosedStatusPin->GetCurrentMatchedState());
}

/**
 * @brief Updates the door state.
 * 
 * This function calls the `UpdateStatus` method of the `m_pDoorStatus` object to update the current state of the door.
 */
void DoorState::UpdateDoorState ()
{
	if ( m_pDoorStatus != nullptr )
	{
		m_pDoorStatus->UpdateStatus ();
	}
}

/// <summary>
//// This function removes the callback for turning off control pins and sets all control pins (open, close, stop, light)
//// to the off state.
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
/**
 * @brief Constructs a `DoorStatusPin` object.
 * 
 * This constructor initializes a `DoorStatusPin` object with the specified parameters, including the door state, events, pin, debounce time, 
 * maximum matched time, match status, pin mode, and initial status. It also sets the pin drive strength - now unneeded.
 * 
 * @param pDoor Pointer to the `DoorState` object.
 * @param matchEvent Event to trigger when the pin matches the specified status.
 * @param unmatchEvent Event to trigger when the pin does not match the specified status.
 * @param pin The pin number.
 * @param debouncems Debounce time in milliseconds.
 * @param maxMatchedTimems Maximum matched time in milliseconds.
 * @param matchStatus The status to match.
 * @param mode The pin mode.
 * @param status The initial pin status.
 */
DoorStatusPin::DoorStatusPin ( DoorState *pDoor, DoorState::Event matchEvent, DoorState::Event unmatchEvent, pin_size_t pin, uint32_t debouncems, uint32_t maxMatchedTimems, PinStatus matchStatus, PinMode mode, PinStatus status )
	: InputPin ( pin, debouncems, maxMatchedTimems, matchStatus, mode, status ), m_pDoor ( pDoor ), m_doorMatchEvent ( matchEvent ), m_doorUnmatchEvent ( unmatchEvent )
{
	// Set pin to source 7mA and sink 10mA rather than default 2 / 2.5 mA, see https://forum.arduino.cc/t/are-the-zeros-pins-set-to-strong-drive-strength-by-default/404930
	//PORT->Group[g_APinDescription[pin].ulPort].PINCFG[g_APinDescription[pin].ulPin].bit.DRVSTR = 1;
}

void DoorStatusPin::MatchAction () const
{
	if ( m_pDoor != nullptr )
	{
		m_pDoor->DoEvent ( m_doorMatchEvent );
	}
}

void DoorStatusPin::UnmatchAction () const
{
	if ( m_pDoor != nullptr )
	{
		m_pDoor->DoEvent ( m_doorUnmatchEvent );
	}
}

/* -------------------------------------------------------------------------------------------------------------------------------------------------- */
//   DoorStatusCalc
/* -------------------------------------------------------------------------------------------------------------------------------------------------- */

/// @brief Initialises DoorStatusCalc with the pin objects that are used to determine the door status
/// @param openPin 
/// @param closePin 
DoorStatusCalc::DoorStatusCalc ( DoorStatusPin &openPin, DoorStatusPin &closePin ) : m_openPin ( openPin ), m_closePin ( closePin )
{
	SetDoorState ( DoorState::State::Unknown );
	SetDoorDirection ( DoorState::Direction::None );
	UpdateStatus ();
}

/// @brief sets the door status based on current input pin readings and prior state
/// @brief This function is called from the main loop and should not be called from an ISR
/// @param none
/// @return none
void DoorStatusCalc::UpdateStatus ()
{
	bool bIsClosed = m_closePin.GetCurrentMatchedState ();
	bool bIsOpen   = m_openPin.GetCurrentMatchedState ();
	
	if ( bIsClosed == false && bIsOpen == false )
	{
		switch ( GetDoorState () )
		{
			case DoorState::State::Open:
				SetDoorDirection ( DoorState::Direction::Down );
				SetDoorState ( DoorState::State::Closing );
				break;

			case DoorState::State::Closed:
				SetDoorDirection ( DoorState::Direction::Up );
				SetDoorState ( DoorState::State::Opening );
				break;

			case DoorState::State::Bad:
				SetDoorDirection ( DoorState::Direction::None );
				Info ( "State None false, false, Bad" );
				SetDoorState ( DoorState::State::Unknown );
				break;

			case DoorState::State::Stopped:
				//Info ( "State stopped, " + String (m_LastDirection) );
				break;

			default:
				// no change
				break;
		}
	}
	else if ( bIsClosed == false && bIsOpen == true )
	{
		SetDoorState ( DoorState::State::Open );
		SetDoorDirection ( DoorState::Direction::None );		
	}
	else if ( bIsClosed == true && bIsOpen == false )
	{
		SetDoorState ( DoorState::State::Closed );
		SetDoorDirection ( DoorState::Direction::None );			
	}
	else
	{
		// both true!
		Info ( "Setting door status as bad" );
		SetDoorState ( DoorState::State::Bad );
		SetDoorDirection ( DoorState::Direction::None );
	}
}

DoorState::State DoorStatusCalc::GetDoorState () const
{
	return m_currentState;
}

void DoorStatusCalc::SetDoorState(DoorState::State state)
{
	m_currentState = state;
}

DoorState::Direction DoorStatusCalc::GetDoorDirection () const
{
	return m_LastDirection;
}

void DoorStatusCalc::SetDoorDirection ( DoorState::Direction direction )
{
	m_LastDirection = direction;
}

const char *DoorStatusCalc::GetDoorDirectionName () const
{
	return DirectionNames [ (uint8_t)GetDoorDirection () % ( sizeof ( DirectionNames ) /  sizeof ( DirectionNames [ 0 ] ) ) ];
}

void DoorStatusCalc::SetStopped ()
{
	SetDoorState ( DoorState::State::Stopped );
}

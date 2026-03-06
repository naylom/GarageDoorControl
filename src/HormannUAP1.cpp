#include "HormannUAP1WithSwitch.h"
#include "Logging.h"
#include "WiFiService.h"

#include <MNTimerLib.h>

/*
 * HormannUAP1.cpp
 *
 * Implements HormannUAP1 — the state machine for the Hormann UAP 1 garage
 * door controller — and its subclass HormannUAP1WithSwitch.
 *
 * The UAP 1 sends 1-second relay pulses to command the door.  Pulse timing
 * uses MNTimerLib.  The internal RGB LED colour reflects door state.
 *
 * Author: (c) M. Naylor 2022 / 2026
 *
 * History:
 *   Ver 1.0   Initial version (as DoorState.cpp)
 *   Ver 2.0   Phase 4 — renamed/refactored, implements IGarageDoor
 */

#define CALL_MEMBER_FN( object, ptrToMember ) ( ( object )->*( ptrToMember ) )

extern UDPWiFiService* pMyUDPService;
extern void Error ( String s, bool bInISR = false );
extern void Info ( String s, bool bInISR = false );

// ── Timing constants ──────────────────────────────────────────────────────────
constexpr uint32_t SWITCH_DEBOUNCE_MS = 100;          // min ms between switch interrupts
constexpr uint32_t MAX_SWITCH_MATCH_TIMER_MS = 2000;  // max time switch pin stays high
constexpr auto DOOR_FLASHTIME = 10;                   // every 2 seconds
constexpr int32_t SIGNAL_PULSE = 2000 * 10;           // 200 ms relay pulse
constexpr uint32_t DEBOUNCE_MS = 50;                  // UAP status pin debounce
constexpr uint32_t MAX_MATCH_TIMER_MS = 1000;         // max time UAP status pin high
constexpr PinStatus UAP_TRUE = PinStatus::HIGH;

static const char* StateNames [] = { "Opened", "Opening", "Closed", "Closing", "Stopped", "Unknown", "Bad" };
static const char* DirectionNames [] = { "Up", "Down", "Stationary" };

static String onOff ( bool state )
{
	return state ? F ( "On" ) : F ( "Off" );
}

// ═════════════════════════════════════════════════════════════════════════════
// HormannUAP1 — constructor / destructor
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief Constructs the Hormann UAP1 controller, binding it to the seven control/status pins.
 * @details Creates DoorStatusPin interrupt-driven input objects for the open, closed, and
 *          light status pins, and OutputPin objects for each relay control line.
 *          All control pins are driven off after construction and the door state is
 *          determined immediately from the current pin readings via DoorStatusCalc.
 * @param OpenPin             Output pin number that pulses the open relay.
 * @param ClosePin            Output pin number that pulses the close relay.
 * @param StopPin             Output pin number that pulses the stop relay.
 * @param LightPin            Output pin number that toggles the light relay.
 * @param DoorOpenStatusPin   Input pin that goes HIGH when the door is fully open.
 * @param DoorClosedStatusPin Input pin that goes HIGH when the door is fully closed.
 * @param DoorLightStatusPin  Input pin that reflects the current light state.
 */
HormannUAP1::HormannUAP1 ( pin_size_t OpenPin,
                           pin_size_t ClosePin,
                           pin_size_t StopPin,
                           pin_size_t LightPin,
                           pin_size_t DoorOpenStatusPin,
                           pin_size_t DoorClosedStatusPin,
                           pin_size_t DoorLightStatusPin )
    : m_DoorOpenCtrlPin ( OpenPin ), m_DoorCloseCtrlPin ( ClosePin ), m_DoorStopCtrlPin ( StopPin ),
      m_DoorLightCtrlPin ( LightPin ), m_DoorOpenStatusPin ( DoorOpenStatusPin ),
      m_DoorClosedStatusPin ( DoorClosedStatusPin ), m_DoorLightStatusPin ( DoorLightStatusPin ),
      m_pDoorOpenStatusPin ( new DoorStatusPin ( this,
                                                 HormannUAP1::Event::Nothing,
                                                 HormannUAP1::Event::Nothing,
                                                 m_DoorOpenStatusPin,
                                                 DEBOUNCE_MS,
                                                 MAX_MATCH_TIMER_MS,
                                                 PinStatus::HIGH,
                                                 PinMode::INPUT_PULLDOWN,
                                                 PinStatus::CHANGE ) ),
      m_pDoorClosedStatusPin ( new DoorStatusPin ( this,
                                                   HormannUAP1::Event::Nothing,
                                                   HormannUAP1::Event::Nothing,
                                                   m_DoorClosedStatusPin,
                                                   DEBOUNCE_MS,
                                                   MAX_MATCH_TIMER_MS,
                                                   PinStatus::HIGH,
                                                   PinMode::INPUT_PULLDOWN,
                                                   PinStatus::CHANGE ) ),
      m_pDoorLightStatusPin ( new DoorStatusPin ( nullptr,
                                                  HormannUAP1::Event::Nothing,
                                                  HormannUAP1::Event::Nothing,
                                                  m_DoorLightStatusPin,
                                                  DEBOUNCE_MS,
                                                  0,
                                                  PinStatus::HIGH,
                                                  PinMode::INPUT_PULLDOWN,
                                                  PinStatus::CHANGE ) ),
      m_pDoorOpenCtrlPin ( new OutputPin ( m_DoorOpenCtrlPin, RELAY_ON_VALUE ) ),
      m_pDoorCloseCtrlPin ( new OutputPin ( m_DoorCloseCtrlPin, RELAY_ON_VALUE ) ),
      m_pDoorStopCtrlPin ( new OutputPin ( m_DoorStopCtrlPin, RELAY_ON_VALUE ) ),
      m_pDoorLightCtrlPin ( new OutputPin ( m_DoorLightCtrlPin, RELAY_ON_VALUE ) )
{
	TurnOffControlPins();
	delay ( 10 );
	m_pDoorStatus = new DoorStatusCalc ( *m_pDoorOpenStatusPin, *m_pDoorClosedStatusPin );
}

/**
 * @brief Destructor. Releases the DoorStatusCalc instance.
 */
HormannUAP1::~HormannUAP1 ()
{
	delete m_pDoorStatus;
	m_pDoorStatus = nullptr;
}

// ═════════════════════════════════════════════════════════════════════════════
// IGarageDoor implementation
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief Returns whether a UAP1 door controller is physically connected.
 * @details Presence is determined by checking whether at least one of the open
 *          or closed status pins is wired (not NOT_A_PIN).
 * @return true if the hardware is present.
 */
bool HormannUAP1::IsPresent () const
{
	return !(m_DoorOpenStatusPin == NOT_A_PIN || m_DoorClosedStatusPin == NOT_A_PIN);
}

/**
 * @brief Polls the current hardware pin states and updates the door state machine.
 * @details Must be called regularly from the main loop. Reads both open and closed
 *          status pins and resolves any transition that the ISR may have missed.
 */
void HormannUAP1::Update ()
{
	if ( m_pDoorStatus != nullptr )
	{
		m_pDoorStatus->UpdateStatus();
	}
}

/**
 * @brief Returns the current door state as determined by the state machine.
 * @return One of IGarageDoor::State: Open, Closed, Opening, Closing, Stopped, Unknown, or Bad.
 */
IGarageDoor::State HormannUAP1::GetState () const
{
	return GetDoorStateInternal();
}

/**
 * @brief Returns the current light state as reported by the light status pin.
 * @return IGarageDoor::LightState::On, Off, or Unknown if the pin is not wired.
 */
IGarageDoor::LightState HormannUAP1::GetLightState () const
{
	if ( m_pDoorLightStatusPin == nullptr )
	{
		return IGarageDoor::LightState::Unknown;
	}
	return m_pDoorLightStatusPin->IsMatched() ? IGarageDoor::LightState::On : IGarageDoor::LightState::Off;
}

/**
 * @brief Returns true when the door is fully open.
 * @return true if current state == IGarageDoor::State::Open.
 */
bool HormannUAP1::IsOpen () const
{
	return GetState() == IGarageDoor::State::Open;
}

/**
 * @brief Returns true when the door is fully closed.
 * @return true if current state == IGarageDoor::State::Closed.
 */
bool HormannUAP1::IsClosed () const
{
	return GetState() == IGarageDoor::State::Closed;
}

/**
 * @brief Returns true when the door is currently opening or closing.
 * @return true if current state is Opening or Closing.
 */
bool HormannUAP1::IsMoving () const
{
	auto s = GetState();
	return s == IGarageDoor::State::Opening || s == IGarageDoor::State::Closing;
}

/**
 * @brief Returns true when the light status pin indicates the light is on.
 * @return true if the light status InputPin is in the matched (HIGH) state.
 */
bool HormannUAP1::IsLit () const
{
	return m_pDoorLightStatusPin != nullptr && m_pDoorLightStatusPin->IsMatched();
}

/**
 * @brief Returns a human-readable C-string for the current door state.
 * @return One of: "Opened", "Opening", "Closed", "Closing", "Stopped", "Unknown", "Bad".
 */
const char* HormannUAP1::GetStateDisplayString () const
{
	return StateNames [ (uint8_t)GetState() % ( sizeof ( StateNames ) / sizeof ( StateNames [ 0 ] ) ) ];
}

/**
 * @brief Issues an Open command by asserting the open relay for SIGNAL_PULSE duration.
 * @details Calls ResetTimer() which first turns all control pins off and schedules
 *          TurnOffControlPins() via the timer to deassert the relay after the pulse.
 */
void HormannUAP1::Open ()
{
	ResetTimer();
	Info ( F ( "Open Door request" ) );
	m_pDoorOpenCtrlPin->On();
}

/**
 * @brief Issues a Close command by asserting the close relay for SIGNAL_PULSE duration.
 */
void HormannUAP1::Close ()
{
	ResetTimer();
	Info ( F ( "Close Door request" ) );
	m_pDoorCloseCtrlPin->On();
}

/**
 * @brief Issues a Stop command by asserting the stop relay for SIGNAL_PULSE duration.
 */
void HormannUAP1::Stop ()
{
	ResetTimer();
	Info ( F ( "Stop Door request" ) );
	m_pDoorStopCtrlPin->On();
}

/**
 * @brief Toggles the light on via the light relay for SIGNAL_PULSE duration.
 * @details The UAP1 light pin is a toggle; the same physical pin is used for
 *          both LightOn and LightOff.
 */
void HormannUAP1::LightOn ()
{
	ResetTimer();
	Info ( F ( "Toggle Light On request" ) );
	m_pDoorLightCtrlPin->On();
}

/**
 * @brief Toggles the light off via the light relay for SIGNAL_PULSE duration.
 * @details Uses the same toggle pin as LightOn (UAP1 hardware behaviour).
 */
void HormannUAP1::LightOff ()
{
	ResetTimer();
	Info ( F ( "Toggle Light Off request" ) );
	m_pDoorLightCtrlPin->On();  // UAP toggle — same pin for on and off
}

/**
 * @brief Registers a callback to be invoked whenever the door state changes.
 * @param cb Function pointer called with the new IGarageDoor::State value.
 *           Pass nullptr to deregister.
 */
void HormannUAP1::SetStateChangedCallback ( StateChangedCallback cb )
{
	m_stateChangedCallback = cb;
}

// ═════════════════════════════════════════════════════════════════════════════
// ISR-called event dispatcher
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief Dispatches an event through the door state-transition table.
 * @details Called from ISR context by DoorStatusPin. Selects the handler
 *          function for the current state and event combination and calls it.
 *          Must be short and must not make SPI/WiFi calls.
 * @param eEvent The event that occurred (DoorOpened, DoorClosed, SwitchPress, etc.).
 */
void HormannUAP1::DoEvent ( HormannUAP1::Event eEvent )
{
	( this->*StateTableFn [ (uint8_t)GetState() ][ (uint8_t)eEvent ] ) ( eEvent );
}

// ═════════════════════════════════════════════════════════════════════════════
// State table handlers (called from ISR — must be short, no SPI/WiFi calls)
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief State-table handler: the door has reached the fully-open position.
 * @details Updates state to Open and direction to Up. Called from ISR.
 */
void HormannUAP1::NowOpen ( Event )
{
	SetStateAndDirection ( IGarageDoor::State::Open, Direction::Up );
}

/**
 * @brief State-table handler: the door has reached the fully-closed position.
 * @details Updates state to Closed and direction to Down. Called from ISR.
 */
void HormannUAP1::NowClosed ( Event )
{
	SetStateAndDirection ( IGarageDoor::State::Closed, Direction::Down );
}

/**
 * @brief State-table handler: the door has started moving toward the closed position.
 * @details Updates state to Closing and direction to Down. Called from ISR.
 */
void HormannUAP1::NowClosing ( Event )
{
	SetStateAndDirection ( IGarageDoor::State::Closing, Direction::Down );
}

/**
 * @brief State-table handler: the door has started moving toward the open position.
 * @details Updates state to Opening and direction to Up. Called from ISR.
 */
void HormannUAP1::NowOpening ( Event )
{
	SetStateAndDirection ( IGarageDoor::State::Opening, Direction::Up );
}

/**
 * @brief State-table handler: the physical wall switch has been pressed.
 * @details Implements context-sensitive logic: opens a closed door, closes an open
 *          door, stops a moving door, or resumes movement based on last direction
 *          when the door is stopped. Called from ISR context.
 */
void HormannUAP1::SwitchPressed ( Event )
{
	uint32_t now = millis();
	switch ( GetState() )
	{
		case IGarageDoor::State::Closed:
			ResetTimer();
			m_pDoorOpenCtrlPin->On();
			break;

		case IGarageDoor::State::Open:
			ResetTimer();
			m_pDoorCloseCtrlPin->On();
			break;

		case IGarageDoor::State::Opening:
		case IGarageDoor::State::Closing:
			ResetTimer();
			m_pDoorStopCtrlPin->On();
			SetDoorState ( IGarageDoor::State::Stopped );
			break;

		case IGarageDoor::State::Stopped:
			switch ( GetDoorDirection() )
			{
				case Direction::Down:
					ResetTimer();
					m_pDoorOpenCtrlPin->On();
					break;
				case Direction::Up:
					ResetTimer();
					m_pDoorCloseCtrlPin->On();
					break;
				default:
					Info ( F ( "Switch pressed when door stopped, unknown last direction — doing nothing" ), true );
					break;
			}
			break;

		case IGarageDoor::State::Bad:
		case IGarageDoor::State::Unknown:
			Info ( F ( "Switch pressed when state is bad / unknown, doing nothing" ), true );
			break;
	}
	m_ulSwitchPressedTime = now;
}

// ═════════════════════════════════════════════════════════════════════════════
// Internal helpers
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief Atomically updates the door state and direction, sets the changed flag,
 *        and fires the optional state-changed callback.
 * @param state     New door state to store.
 * @param direction New direction to store.
 */
void HormannUAP1::SetStateAndDirection ( IGarageDoor::State state, Direction direction )
{
	SetDoorState ( state );
	SetDoorDirection ( direction );
	m_bDoorStateChanged = true;
	if ( m_stateChangedCallback != nullptr )
	{
		m_stateChangedCallback ( state );
	}
}

/**
 * @brief Sets the door state in the DoorStatusCalc object.
 * @param newState The state to store.
 */
void HormannUAP1::SetDoorState ( IGarageDoor::State newState )
{
	if ( m_pDoorStatus != nullptr )
	{
		m_pDoorStatus->SetDoorState ( newState );
	}
}

/**
 * @brief Sets the last-known door travel direction in the DoorStatusCalc object.
 * @param direction The direction to store (Up, Down, or None).
 */
void HormannUAP1::SetDoorDirection ( Direction direction )
{
	if ( m_pDoorStatus != nullptr )
	{
		m_pDoorStatus->SetDoorDirection ( direction );
	}
}

/**
 * @brief Retrieves the current door state from the DoorStatusCalc object.
 * @return The stored door state, or IGarageDoor::State::Unknown if not initialised.
 */
IGarageDoor::State HormannUAP1::GetDoorStateInternal () const
{
	if ( m_pDoorStatus != nullptr )
	{
		return m_pDoorStatus->GetDoorState();
	}
	return IGarageDoor::State::Unknown;
}

/**
 * @brief Returns the last known travel direction of the door.
 * @return Direction::Up, Direction::Down, or Direction::None if unknown.
 */
HormannUAP1::Direction HormannUAP1::GetDoorDirection () const
{
	if ( m_pDoorStatus != nullptr )
	{
		return m_pDoorStatus->GetDoorDirection();
	}
	return Direction::None;
}

/**
 * @brief Deasserts all control relay pins immediately and schedules a timer callback
 *        to deassert them again after SIGNAL_PULSE microseconds, producing a timed relay pulse.
 * @details Turning all pins off before scheduling prevents a second simultaneous pulse
 *          if the timer fires while a previous command is still asserting a pin.
 */
void HormannUAP1::ResetTimer ()
{
	TurnOffControlPins();
	if ( !TheTimer.AddCallBack ( (MNTimerClass*)this,
	                             (aMemberFunction)&HormannUAP1::TurnOffControlPins,
	                             SIGNAL_PULSE ) )
	{
		Error ( F ( "Timer callback add failed" ), true );
	}
}

/**
 * @brief Removes the pending timer callback and drives all four relay control
 *        output pins to their OFF state.
 * @details Called both at the start of a new command (via ResetTimer) and by the
 *          MNTimerLib callback to end the relay pulse.
 */
void HormannUAP1::TurnOffControlPins ()
{
	TheTimer.RemoveCallBack ( (MNTimerClass*)this, (aMemberFunction)&HormannUAP1::TurnOffControlPins );
	m_pDoorOpenCtrlPin->Off();
	m_pDoorCloseCtrlPin->Off();
	m_pDoorStopCtrlPin->Off();
	m_pDoorLightCtrlPin->Off();
}

// ═════════════════════════════════════════════════════════════════════════════
// Diagnostic helpers
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief Returns a human-readable string for the last-known door travel direction.
 * @return One of: "Up", "Down", "Stationary".
 */
const char* HormannUAP1::GetDoorDirectionName () const
{
	return m_pDoorStatus->GetDoorDirectionName();
}

/**
 * @brief Returns the cumulative number of times the light status pin went HIGH (light turned on).
 * @return Interrupt-driven matched-state counter for the light status pin.
 */
uint32_t HormannUAP1::GetLightOnCount () const
{
	return m_pDoorLightStatusPin->GetMatchedCount();
}

/**
 * @brief Returns the cumulative number of times the light status pin went LOW (light turned off).
 * @return Interrupt-driven unmatched-state counter for the light status pin.
 */
uint32_t HormannUAP1::GetLightOffCount () const
{
	return m_pDoorLightStatusPin->GetUnmatchedCount();
}

/**
 * @brief Returns the cumulative number of times the door reached the fully-open position.
 * @return Matched-state counter for the open status pin.
 */
uint32_t HormannUAP1::GetDoorOpenedCount () const
{
	return m_pDoorOpenStatusPin->GetMatchedCount();
}

/**
 * @brief Returns the cumulative number of times the door left the closed position (started opening).
 * @return Unmatched-state counter for the closed status pin.
 */
uint32_t HormannUAP1::GetDoorOpeningCount () const
{
	return m_pDoorClosedStatusPin->GetUnmatchedCount();
}

/**
 * @brief Returns the cumulative number of times the door reached the fully-closed position.
 * @return Matched-state counter for the closed status pin.
 */
uint32_t HormannUAP1::GetDoorClosedCount () const
{
	return m_pDoorClosedStatusPin->GetMatchedCount();
}

/**
 * @brief Returns the cumulative number of times the door left the open position (started closing).
 * @return Unmatched-state counter for the open status pin.
 */
uint32_t HormannUAP1::GetDoorClosingCount () const
{
	return m_pDoorOpenStatusPin->GetUnmatchedCount();
}

/**
 * @brief Builds a diagnostic string showing the current and latched state of all three
 *        status pins (light, open, closed).
 * @param states Output String that receives the formatted pin state description.
 */
void HormannUAP1::GetPinStates ( String& states )
{
	states = String ( F ( "Light: " ) );
	states += onOff ( m_pDoorLightStatusPin->IsMatched() );
	states += String ( F ( " Open: " ) );
	states += onOff ( m_pDoorOpenStatusPin->IsMatched() );
	states += String ( F ( " Closed: " ) );
	states += onOff ( m_pDoorClosedStatusPin->IsMatched() );
	states += String ( F ( " Curr Light: " ) );
	states += onOff ( m_pDoorLightStatusPin->GetCurrentMatchedState() );
	states += String ( F ( " Opn: " ) );
	states += onOff ( m_pDoorOpenStatusPin->GetCurrentMatchedState() );
	states += String ( F ( " Clsed: " ) );
	states += onOff ( m_pDoorClosedStatusPin->GetCurrentMatchedState() );
}

// ═════════════════════════════════════════════════════════════════════════════
// DoorStatusPin
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief Constructs a door status input pin that forwards pin events to the HormannUAP1 state machine.
 * @details Extends InputPin with match and unmatch events that are dispatched via
 *          HormannUAP1::DoEvent() when the pin transitions. When pDoor is nullptr no
 *          events are dispatched (used for the light status pin).
 * @param pDoor           Pointer to the owning HormannUAP1 instance; may be nullptr.
 * @param matchEvent      Event to fire when the pin reaches the matched (active) state.
 * @param unmatchEvent    Event to fire when the pin leaves the matched state.
 * @param pin             Arduino pin number to monitor.
 * @param debouncems      Minimum time in ms between accepted interrupts.
 * @param maxMatchedTimems Maximum time in ms the pin may remain in the matched state
 *                         before the transition is rejected as spurious. 0 = unlimited.
 * @param matchStatus     PinStatus value (HIGH/LOW) that constitutes a match.
 * @param mode            Pin input mode (INPUT, INPUT_PULLUP, INPUT_PULLDOWN).
 * @param status          Interrupt trigger type (CHANGE, RISING, FALLING).
 */
DoorStatusPin::DoorStatusPin ( HormannUAP1* pDoor,
                               HormannUAP1::Event matchEvent,
                               HormannUAP1::Event unmatchEvent,
                               pin_size_t pin,
                               uint32_t debouncems,
                               uint32_t maxMatchedTimems,
                               PinStatus matchStatus,
                               PinMode mode,
                               PinStatus status )
    : InputPin ( pin, debouncems, maxMatchedTimems, matchStatus, mode, status ), m_pDoor ( pDoor ),
      m_doorMatchEvent ( matchEvent ), m_doorUnmatchEvent ( unmatchEvent )
{
}

/**
 * @brief Called by InputPin when the pin reaches the match state.
 * @details Fires the configured matchEvent on the owning HormannUAP1 door object.
 *          Called from ISR context.
 */
void DoorStatusPin::MatchAction () const
{
	if ( m_pDoor != nullptr )
	{
		m_pDoor->DoEvent ( m_doorMatchEvent );
	}
}

/**
 * @brief Called by InputPin when the pin leaves the match state.
 * @details Fires the configured unmatchEvent on the owning HormannUAP1 door object.
 *          Called from ISR context.
 */
void DoorStatusPin::UnmatchAction () const
{
	if ( m_pDoor != nullptr )
	{
		m_pDoor->DoEvent ( m_doorUnmatchEvent );
	}
}

// ═════════════════════════════════════════════════════════════════════════════
// DoorStatusCalc
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief Constructs the door state calculator from the two status pin objects.
 * @details Sets initial state to Unknown/None then immediately calls UpdateStatus()
 *          to determine the true state from the current pin readings.
 * @param openPin   Reference to the DoorStatusPin monitoring the open sensor.
 * @param closePin  Reference to the DoorStatusPin monitoring the close sensor.
 */
DoorStatusCalc::DoorStatusCalc ( DoorStatusPin& openPin, DoorStatusPin& closePin )
    : m_openPin ( openPin ), m_closePin ( closePin )
{
	SetDoorState ( IGarageDoor::State::Unknown );
	SetDoorDirection ( HormannUAP1::Direction::None );
	UpdateStatus();
}

/**
 * @brief Evaluates the current open/closed pin states and updates the door state accordingly.
 * @details Uses a truth-table based on whether each pin currently reads in its matched
 *          (active HIGH) state. When neither pin is active the direction of travel is
 *          inferred from the previous state to determine Opening vs Closing.
 *          Sets state to Bad when both pins read active simultaneously.
 */
void DoorStatusCalc::UpdateStatus ()
{
	bool bIsClosed = m_closePin.GetCurrentMatchedState();
	bool bIsOpen = m_openPin.GetCurrentMatchedState();

	if ( bIsClosed == false && bIsOpen == false )
	{
		switch ( GetDoorState() )
		{
			case IGarageDoor::State::Open:
				SetDoorDirection ( HormannUAP1::Direction::Down );
				SetDoorState ( IGarageDoor::State::Closing );
				break;
			case IGarageDoor::State::Closed:
				SetDoorDirection ( HormannUAP1::Direction::Up );
				SetDoorState ( IGarageDoor::State::Opening );
				break;
			case IGarageDoor::State::Bad:
				SetDoorDirection ( HormannUAP1::Direction::None );
				Info ( F ( "State None false, false, Bad" ) );
				SetDoorState ( IGarageDoor::State::Unknown );
				break;
			case IGarageDoor::State::Stopped:
				break;
			default:
				break;
		}
	}
	else if ( bIsClosed == false && bIsOpen == true )
	{
		SetDoorState ( IGarageDoor::State::Open );
		SetDoorDirection ( HormannUAP1::Direction::None );
	}
	else if ( bIsClosed == true && bIsOpen == false )
	{
		SetDoorState ( IGarageDoor::State::Closed );
		SetDoorDirection ( HormannUAP1::Direction::None );
	}
	else
	{
		Info ( F ( "Setting door status as bad" ) );
		SetDoorState ( IGarageDoor::State::Bad );
		SetDoorDirection ( HormannUAP1::Direction::None );
	}
}

/**
 * @brief Returns the current door state stored in this calculator.
 * @return The last state set by UpdateStatus(), SetDoorState(), or SetStopped().
 */
IGarageDoor::State DoorStatusCalc::GetDoorState () const
{
	return m_currentState;
}

/**
 * @brief Stores a new door state.
 * @param state The state to persist in this calculator.
 */
void DoorStatusCalc::SetDoorState ( IGarageDoor::State state )
{
	m_currentState = state;
}

/**
 * @brief Returns the last-known door travel direction stored in this calculator.
 * @return Direction::Up, Direction::Down, or Direction::None.
 */
HormannUAP1::Direction DoorStatusCalc::GetDoorDirection () const
{
	return m_LastDirection;
}

/**
 * @brief Stores a new door travel direction.
 * @param direction The direction to persist.
 */
void DoorStatusCalc::SetDoorDirection ( HormannUAP1::Direction direction )
{
	m_LastDirection = direction;
}

/**
 * @brief Returns a human-readable string for the last-known direction stored in this calculator.
 * @return One of: "Up", "Down", "Stationary".
 */
const char* DoorStatusCalc::GetDoorDirectionName () const
{
	return DirectionNames [ (uint8_t)GetDoorDirection() %
	                        ( sizeof ( DirectionNames ) / sizeof ( DirectionNames [ 0 ] ) ) ];
}

/**
 * @brief Transitions the door state to Stopped.
 */
void DoorStatusCalc::SetStopped ()
{
	SetDoorState ( IGarageDoor::State::Stopped );
}

// ═════════════════════════════════════════════════════════════════════════════
// HormannUAP1WithSwitch
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief Constructs HormannUAP1 with an additional physical wall-switch input.
 * @details Delegates all UAP1 pins to the HormannUAP1 base constructor and creates
 *          an additional DoorStatusPin for the wall switch. The switch fires the
 *          SwitchPress event on the unmatched transition (button release after debounce).
 * @param OpenPin             UAP1 open relay output pin.
 * @param ClosePin            UAP1 close relay output pin.
 * @param StopPin             UAP1 stop relay output pin.
 * @param LightPin            UAP1 light relay output pin.
 * @param DoorOpenStatusPin   Door-open sensor input pin.
 * @param DoorClosedStatusPin Door-closed sensor input pin.
 * @param DoorLightStatusPin  Light-status sensor input pin.
 * @param DoorSwitchStatusPin Wall-switch input pin (active-HIGH, INPUT_PULLDOWN).
 */
HormannUAP1WithSwitch::HormannUAP1WithSwitch ( pin_size_t OpenPin,
                                               pin_size_t ClosePin,
                                               pin_size_t StopPin,
                                               pin_size_t LightPin,
                                               pin_size_t DoorOpenStatusPin,
                                               pin_size_t DoorClosedStatusPin,
                                               pin_size_t DoorLightStatusPin,
                                               pin_size_t DoorSwitchStatusPin )
    : HormannUAP1 ( OpenPin, ClosePin, StopPin, LightPin, DoorOpenStatusPin, DoorClosedStatusPin, DoorLightStatusPin ),
      m_DoorSwitchStatusPin ( DoorSwitchStatusPin ),
      m_pDoorSwitchStatusPin ( new DoorStatusPin ( this,
                                                   HormannUAP1::Event::Nothing,
                                                   HormannUAP1::Event::SwitchPress,
                                                   m_DoorSwitchStatusPin,
                                                   SWITCH_DEBOUNCE_MS,
                                                   MAX_SWITCH_MATCH_TIMER_MS,
                                                   PinStatus::HIGH,
                                                   PinMode::INPUT_PULLDOWN,
                                                   PinStatus::CHANGE ) )
{
}

/**
 * @brief Destructor. Releases the wall-switch DoorStatusPin object.
 */
HormannUAP1WithSwitch::~HormannUAP1WithSwitch ()
{
	// unique_ptr<DoorStatusPin> is destroyed here where DoorStatusPin is complete
}

/**
 * @brief Returns whether a wall switch is wired and configured.
 * @return true if the switch DoorStatusPin was successfully created.
 */
bool HormannUAP1WithSwitch::IsSwitchConfigured () const
{
	return m_pDoorSwitchStatusPin != nullptr;
}

/**
 * @brief Returns the cumulative number of times the wall switch has been activated.
 * @return Matched-count from the switch DoorStatusPin, or 0 if no switch is configured.
 */
uint32_t HormannUAP1WithSwitch::GetSwitchMatchCount () const
{
	return m_pDoorSwitchStatusPin ? m_pDoorSwitchStatusPin->GetMatchedCount() : 0;
}

/**
 * @brief Appends the switch pin interrupt counters to the provided string for diagnostics.
 * @param result Output String that receives the formatted counter values.
 *               Does nothing if no switch is configured.
 */
void HormannUAP1WithSwitch::SwitchDebugStats ( String& result ) const
{
	if ( m_pDoorSwitchStatusPin )
	{
		m_pDoorSwitchStatusPin->DebugStats ( result );
	}
}

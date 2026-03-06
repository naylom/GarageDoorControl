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

HormannUAP1::~HormannUAP1 ()
{
	delete m_pDoorStatus;
	m_pDoorStatus = nullptr;
}

// ═════════════════════════════════════════════════════════════════════════════
// IGarageDoor implementation
// ═════════════════════════════════════════════════════════════════════════════

bool HormannUAP1::IsPresent () const
{
	return m_DoorOpenStatusPin != NOT_A_PIN || m_DoorClosedStatusPin != NOT_A_PIN;
}

void HormannUAP1::Update ()
{
	if ( m_pDoorStatus != nullptr )
	{
		m_pDoorStatus->UpdateStatus();
	}
}

IGarageDoor::State HormannUAP1::GetState () const
{
	return GetDoorStateInternal();
}

IGarageDoor::LightState HormannUAP1::GetLightState () const
{
	if ( m_pDoorLightStatusPin == nullptr )
	{
		return IGarageDoor::LightState::Unknown;
	}
	return m_pDoorLightStatusPin->IsMatched() ? IGarageDoor::LightState::On : IGarageDoor::LightState::Off;
}

bool HormannUAP1::IsOpen () const
{
	return GetState() == IGarageDoor::State::Open;
}

bool HormannUAP1::IsClosed () const
{
	return GetState() == IGarageDoor::State::Closed;
}

bool HormannUAP1::IsMoving () const
{
	auto s = GetState();
	return s == IGarageDoor::State::Opening || s == IGarageDoor::State::Closing;
}

bool HormannUAP1::IsLit () const
{
	return m_pDoorLightStatusPin != nullptr && m_pDoorLightStatusPin->IsMatched();
}

const char* HormannUAP1::GetStateDisplayString () const
{
	return StateNames [ (uint8_t)GetState() % ( sizeof ( StateNames ) / sizeof ( StateNames [ 0 ] ) ) ];
}

void HormannUAP1::Open ()
{
	ResetTimer();
	Info ( F ( "Open Door request" ) );
	m_pDoorOpenCtrlPin->On();
}

void HormannUAP1::Close ()
{
	ResetTimer();
	Info ( F ( "Close Door request" ) );
	m_pDoorCloseCtrlPin->On();
}

void HormannUAP1::Stop ()
{
	ResetTimer();
	Info ( F ( "Stop Door request" ) );
	m_pDoorStopCtrlPin->On();
}

void HormannUAP1::LightOn ()
{
	ResetTimer();
	Info ( F ( "Toggle Light On request" ) );
	m_pDoorLightCtrlPin->On();
}

void HormannUAP1::LightOff ()
{
	ResetTimer();
	Info ( F ( "Toggle Light Off request" ) );
	m_pDoorLightCtrlPin->On();  // UAP toggle — same pin for on and off
}

void HormannUAP1::SetStateChangedCallback ( StateChangedCallback cb )
{
	m_stateChangedCallback = cb;
}

// ═════════════════════════════════════════════════════════════════════════════
// ISR-called event dispatcher
// ═════════════════════════════════════════════════════════════════════════════

void HormannUAP1::DoEvent ( HormannUAP1::Event eEvent )
{
	( this->*StateTableFn [ (uint8_t)GetState() ][ (uint8_t)eEvent ] ) ( eEvent );
}

// ═════════════════════════════════════════════════════════════════════════════
// State table handlers (called from ISR — must be short, no SPI/WiFi calls)
// ═════════════════════════════════════════════════════════════════════════════

void HormannUAP1::NowOpen ( Event )
{
	SetStateAndDirection ( IGarageDoor::State::Open, Direction::Up );
}

void HormannUAP1::NowClosed ( Event )
{
	SetStateAndDirection ( IGarageDoor::State::Closed, Direction::Down );
}

void HormannUAP1::NowClosing ( Event )
{
	SetStateAndDirection ( IGarageDoor::State::Closing, Direction::Down );
}

void HormannUAP1::NowOpening ( Event )
{
	SetStateAndDirection ( IGarageDoor::State::Opening, Direction::Up );
}

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

void HormannUAP1::SetDoorState ( IGarageDoor::State newState )
{
	if ( m_pDoorStatus != nullptr )
	{
		m_pDoorStatus->SetDoorState ( newState );
	}
}

void HormannUAP1::SetDoorDirection ( Direction direction )
{
	if ( m_pDoorStatus != nullptr )
	{
		m_pDoorStatus->SetDoorDirection ( direction );
	}
}

IGarageDoor::State HormannUAP1::GetDoorStateInternal () const
{
	if ( m_pDoorStatus != nullptr )
	{
		return m_pDoorStatus->GetDoorState();
	}
	return IGarageDoor::State::Unknown;
}

HormannUAP1::Direction HormannUAP1::GetDoorDirection () const
{
	if ( m_pDoorStatus != nullptr )
	{
		return m_pDoorStatus->GetDoorDirection();
	}
	return Direction::None;
}

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

const char* HormannUAP1::GetDoorDirectionName () const
{
	return m_pDoorStatus->GetDoorDirectionName();
}

uint32_t HormannUAP1::GetLightOnCount () const
{
	return m_pDoorLightStatusPin->GetMatchedCount();
}

uint32_t HormannUAP1::GetLightOffCount () const
{
	return m_pDoorLightStatusPin->GetUnmatchedCount();
}

uint32_t HormannUAP1::GetDoorOpenedCount () const
{
	return m_pDoorOpenStatusPin->GetMatchedCount();
}

uint32_t HormannUAP1::GetDoorOpeningCount () const
{
	return m_pDoorClosedStatusPin->GetUnmatchedCount();
}

uint32_t HormannUAP1::GetDoorClosedCount () const
{
	return m_pDoorClosedStatusPin->GetMatchedCount();
}

uint32_t HormannUAP1::GetDoorClosingCount () const
{
	return m_pDoorOpenStatusPin->GetUnmatchedCount();
}

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

// ═════════════════════════════════════════════════════════════════════════════
// DoorStatusCalc
// ═════════════════════════════════════════════════════════════════════════════

DoorStatusCalc::DoorStatusCalc ( DoorStatusPin& openPin, DoorStatusPin& closePin )
    : m_openPin ( openPin ), m_closePin ( closePin )
{
	SetDoorState ( IGarageDoor::State::Unknown );
	SetDoorDirection ( HormannUAP1::Direction::None );
	UpdateStatus();
}

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

IGarageDoor::State DoorStatusCalc::GetDoorState () const
{
	return m_currentState;
}

void DoorStatusCalc::SetDoorState ( IGarageDoor::State state )
{
	m_currentState = state;
}

HormannUAP1::Direction DoorStatusCalc::GetDoorDirection () const
{
	return m_LastDirection;
}

void DoorStatusCalc::SetDoorDirection ( HormannUAP1::Direction direction )
{
	m_LastDirection = direction;
}

const char* DoorStatusCalc::GetDoorDirectionName () const
{
	return DirectionNames [ (uint8_t)GetDoorDirection() %
	                        ( sizeof ( DirectionNames ) / sizeof ( DirectionNames [ 0 ] ) ) ];
}

void DoorStatusCalc::SetStopped ()
{
	SetDoorState ( IGarageDoor::State::Stopped );
}

// ═════════════════════════════════════════════════════════════════════════════
// HormannUAP1WithSwitch
// ═════════════════════════════════════════════════════════════════════════════

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

HormannUAP1WithSwitch::~HormannUAP1WithSwitch ()
{
	// unique_ptr<DoorStatusPin> is destroyed here where DoorStatusPin is complete
}

bool HormannUAP1WithSwitch::IsSwitchConfigured () const
{
	return m_pDoorSwitchStatusPin != nullptr;
}

uint32_t HormannUAP1WithSwitch::GetSwitchMatchCount () const
{
	return m_pDoorSwitchStatusPin ? m_pDoorSwitchStatusPin->GetMatchedCount() : 0;
}

void HormannUAP1WithSwitch::SwitchDebugStats ( String& result ) const
{
	if ( m_pDoorSwitchStatusPin )
	{
		m_pDoorSwitchStatusPin->DebugStats ( result );
	}
}

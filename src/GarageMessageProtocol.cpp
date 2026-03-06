/*
 * GarageMessageProtocol.cpp
 *
 * Implements GarageMessageProtocol — encapsulates all UDP payload string
 * formatting and command dispatch for GarageControl.
 *
 * Author: (c) M. Naylor 2026
 *
 * History:
 *   Ver 1.0   Phase 6 — initial implementation
 */

#include "GarageMessageProtocol.h"

extern void Error ( String s, bool bInISR = false );

// ─── Constructor ─────────────────────────────────────────────────────────────
/**
 * @brief Constructs the protocol handler,
 *    binding it to all runtime data sources.
 * @param pDoor    Pointer to the garage door controller; may be nullptr if no door is fitted.
 * @param pSensor  Pointer to the environment sensor; may be nullptr if no sensor is fitted.
 * @param reading  Reference to the shared EnvironmentReading struct populated by the sensor.
 * @param service  Reference to the UDPWiFiService used to query the current NTP timestamp.
 */
GarageMessageProtocol::GarageMessageProtocol ( IGarageDoor* pDoor,
                                               IEnvironmentSensor* pSensor,
                                               EnvironmentReading& reading,
                                               UDPWiFiService& service )
    : m_pDoor ( pDoor ), m_pSensor ( pSensor ), m_reading ( reading ), m_service ( service )
{
}

// ─── BuildResponse ───────────────────────────────────────────────────────────
/**
 * @brief Builds the UDP response payload string for the given message type.
 * @details TEMPDATA responses contain comma-separated key=value pairs for
 *          temperature, humidity, dew-point, pressure, and timestamp.
 *          DOORDATA responses contain door state, light state, open/closed/moving
 *          flags, and timestamp. Command-only types (DOOROPEN etc.) produce an
 *          empty string - no response is sent.
 * @param msgType Numeric value of a UDPWiFiService::ReqMsgType enum.
 * @return The formatted response string, or an empty String if no payload applies.
 */
String GarageMessageProtocol::BuildResponse ( uint8_t msgType )
{
	String sResponse;

	switch ( static_cast<UDPWiFiService::ReqMsgType> ( msgType ) )
	{
		case UDPWiFiService::ReqMsgType::TEMPDATA:
			if ( m_pSensor != nullptr && m_reading.valid )
			{
				sResponse = F ( "T=" );
				sResponse += m_reading.temperature;
				sResponse += F ( ",H=" );
				sResponse += m_reading.humidity;
				sResponse += F ( ",D=" );
				sResponse += m_reading.dewpoint;
				sResponse += F ( ",P=" );
				sResponse += m_reading.pressure;
				sResponse += F ( ",A=" );
				sResponse += m_reading.timestampMs;
				sResponse += F ( "\r" );
			}
			break;

		case UDPWiFiService::ReqMsgType::DOORDATA:
			if ( m_pDoor != nullptr )
			{
				sResponse = F ( "S=" );
				sResponse += m_pDoor->GetStateDisplayString();
				sResponse += F ( ",L=" );
				sResponse += m_pDoor->IsLit() ? F ( "On" ) : F ( "Off" );
				sResponse += F ( ",C=" );
				sResponse += m_pDoor->IsClosed() ? F ( "Y" ) : F ( "N" );
				sResponse += F ( ",O=" );
				sResponse += m_pDoor->IsOpen() ? F ( "Y" ) : F ( "N" );
				sResponse += F ( ",M=" );
				sResponse += m_pDoor->IsMoving() ? F ( "Y" ) : F ( "N" );
				sResponse += F ( ",A=" );
				sResponse += m_service.GetTime();
				sResponse += F ( "\r" );
			}
			else
			{
				Error ( F ( "Door data unavailable: pGarageDoor is null" ) );
			}
			break;

		default:
			// Command-only messages (DOOROPEN, DOORCLOSE, DOORSTOP, LIGHTON, LIGHTOFF)
			// produce no response payload.
			break;
	}

	return sResponse;
}

// ─── HandleCommand ───────────────────────────────────────────────────────────
/**
 * @brief Dispatches a command message to the appropriate garage door action.
 * @details Handles DOOROPEN, DOORCLOSE, DOORSTOP, LIGHTON, and LIGHTOFF by
 *          calling the corresponding IGarageDoor method. Data-request types
 *          (TEMPDATA, DOORDATA) are silently ignored - they have no side-effect.
 *          Guards against nullptr door pointer.
 * @param msgType Numeric value of a UDPWiFiService::ReqMsgType enum.
 */
void GarageMessageProtocol::HandleCommand ( uint8_t msgType )
{
	switch ( static_cast<UDPWiFiService::ReqMsgType> ( msgType ) )
	{
		case UDPWiFiService::ReqMsgType::DOOROPEN:
			if ( m_pDoor != nullptr )
			{
				m_pDoor->Open();
			}
			break;

		case UDPWiFiService::ReqMsgType::DOORCLOSE:
			if ( m_pDoor != nullptr )
			{
				m_pDoor->Close();
			}
			break;

		case UDPWiFiService::ReqMsgType::DOORSTOP:
			if ( m_pDoor != nullptr )
			{
				m_pDoor->Stop();
			}
			break;

		case UDPWiFiService::ReqMsgType::LIGHTON:
			if ( m_pDoor != nullptr )
			{
				m_pDoor->LightOn();
			}
			break;

		case UDPWiFiService::ReqMsgType::LIGHTOFF:
			if ( m_pDoor != nullptr )
			{
				m_pDoor->LightOff();
			}
			break;

		default:
			// TEMPDATA, DOORDATA — data-request messages; no side-effect to execute.
			break;
	}
}

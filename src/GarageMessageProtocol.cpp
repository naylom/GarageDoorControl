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

// ─── Constructor ──────────────────────────────────────────────────────────────
GarageMessageProtocol::GarageMessageProtocol ( IGarageDoor* pDoor,
                                               IEnvironmentSensor* pSensor,
                                               EnvironmentReading& reading,
                                               UDPWiFiService& service )
    : m_pDoor ( pDoor ), m_pSensor ( pSensor ), m_reading ( reading ), m_service ( service )
{
}

// ─── BuildResponse ────────────────────────────────────────────────────────────
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

// ─── HandleCommand ────────────────────────────────────────────────────────────
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

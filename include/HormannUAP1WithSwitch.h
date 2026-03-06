#pragma once
/*
 * HormannUAP1WithSwitch.h
 *
 * Extends HormannUAP1 with support for a manual momentary door-control switch
 * wired to a separate digital input pin.  When the switch pin is pressed the
 * door opens, closes, or stops — mirroring the behaviour of the Hormann
 * handset button.
 *
 * Author: (c) M. Naylor 2026
 *
 * History:
 *   Ver 1.0   Phase 4 — split out from DoorState
 */

#include "HormannUAP1.h"

#include <memory>

class HormannUAP1WithSwitch : public HormannUAP1
{
public:
	HormannUAP1WithSwitch ( pin_size_t OpenPin,
	                        pin_size_t ClosePin,
	                        pin_size_t StopPin,
	                        pin_size_t LightPin,
	                        pin_size_t DoorOpenStatusPin,
	                        pin_size_t DoorClosedStatusPin,
	                        pin_size_t DoorLightStatusPin,
	                        pin_size_t DoorSwitchStatusPin );

	// Destructor defined in HormannUAP1.cpp where DoorStatusPin is complete.
	~HormannUAP1WithSwitch () override;

	bool IsSwitchConfigured () const override;
	uint32_t GetSwitchMatchCount () const override;
	void SwitchDebugStats ( String& result ) const override;

private:
	const pin_size_t m_DoorSwitchStatusPin;
	std::unique_ptr<DoorStatusPin> m_pDoorSwitchStatusPin;
};

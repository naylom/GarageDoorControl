#pragma once
/**
 * @file OnboardingServer.h
 * @brief GarageControl onboarding server using the OnboardingServer library.
 * @details Captures WiFi credentials plus application settings and saves them
 *          using ConfigStorage.
 *
 * Author: (c) M. Naylor 2026
 */

#pragma once

#include "ConfigStorage.h"

#include <OnboardingServerBase.h>

class OnboardingServer : public OnboardingServerBase
{
public:
	explicit OnboardingServer ( uint16_t port = 80 );

protected:
	String getFormTitle () const override;
	String getAdditionalFields () const override;
	String getAdditionalValidation () const override;
	bool parseAdditionalFields ( const String& body ) override;
	bool saveConfiguration () override;
	String getFooterContent () const override;

private:
	GarageConfig _config;
};

#pragma once
/**
 * @file Application.h
 * @brief Top-level application class — owns the begin() / loop() lifecycle.
 *
 * Phase 2 refactoring: main.cpp delegates entirely to this class.
 * File-scope globals (MyLogger, pMyUDPService, etc.) remain in Application.cpp
 * so that extern declarations in Display.cpp and DoorState.cpp continue to link.
 */

#include "GarageControl.h"
#include "logging.h"
#include "WiFiService.h"

#ifdef UAP_SUPPORT
#include "HormannUAP1WithSwitch.h"
#endif

class Application
{
public:
	Application ();
	void begin ();
	void loop ();

private:
	// Helpers are static: they operate entirely on file-scope globals defined in
	// Application.cpp, so no implicit 'this' pointer is required.
	// processUDPMsg must be static to satisfy the UDPWiFiServiceCallback signature.
	static void setLED ();
	static void multicastMsg ( UDPWiFiService::ReqMsgType eReqType );
	static void processUDPMsg ( UDPWiFiService::ReqMsgType eReqType );
};

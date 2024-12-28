#pragma once
#include "Logging.h"
/*
Display.h

Routines to update the screen with status information

*/
void DisplaylastInfoErrorMsg ( void );
void Error ( String s, bool bInISR = false );
void Info ( String s, bool bInISR = false );
void DisplayStats ( void );
void DisplayUptime ( ansiVT220Logger logger, uint8_t line, uint8_t row, ansiVT220Logger::colours Foreground, ansiVT220Logger::colours Background );
void DisplayNWStatus ( ansiVT220Logger logger );
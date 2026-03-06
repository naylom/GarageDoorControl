#include "Application.h"

#include <Arduino.h>

Application theApp;

void setup ()
{
	theApp.begin();
}

void loop ()
{
	theApp.loop();
}
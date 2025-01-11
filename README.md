# GarageDoorControl Overview

Arduino MKR WiFi 1010 based project

Connects to Hormann UAP to monitor and control garage door

Has temperature, humidity and barometric pressure sensors

Has a manual momentary switch connected to open / close / stop door

Connects to wifi so status and door control functions can be achieved remotely

-----------------------------------------------------------------------

This started as a project to add remote control (from an app) to my Hormann 
garage door. I also wanted to measure the temp / humidity and barometric pressure
in the garage. The code is configurable to just be a temp / humidity etc sensor and / or 
a Hormann controller. Communication is achieved using wif-fi and UDP datagrams.

I choose the Arduino MKR WiFi 1010 because of its built in wi-fi. The nuilt in RGB LED is
used to show wifi status status and an additional external RGB led displays the door status;
when not connected to a Hormann UAP the extenal led indicates humidity level.

The Hormann UAP is a additional product they sell that attaches to the door motor and sends
status signals indicating when the door is open, closed and the light i son or off. Additionally
it will accept command signals to turn the light on or off, open close and stop the garage door.

The UAP will switch status signals using the connected Arduino MKR WiFI 1010 pins. The UAP has
built in relays to do this.
On the command side it is necessary to pull the UAP input command pins LOW from the default 24V
value when you want to initiate an action. Since the MKR WiFi is a 3.3V device it cannot directly
manipulate 24V signals. Either an external relay board can be used or as in my case I use EL815
optocouplers.

I also wanted to add a manual momentary switch in the garage to opwn and shut the door. This was
unexpectedly one of the more troublesome parts of the project. I found that using a MKR WiFI 1010
3.3V pin to detect the switch being pressed suffered from a lot of noise and even with debounce
logic gave false positives. After trying many things including using cat6 shielded FTP cable, 
I found the answer was to use 24V (borrowed from the Hormann UAP) to drive the switch connection
and again to use an EL815 optocoupler to allow the Arduino to monitor the result with a 3.3V pin.

Finally since I had a wifi connection to the Arduino I ran a TCP server as well and used that to 
display VT220 based diagnostic information over telnet. This removed the need to have a usb connection
and PC close to this garage controller.

In summary the hardware components are:

Arduino MKR WIFI 1010

5 EL815 optocouplers

1 Momentary switch

1 BME280 Temperature / Humnidity / Pressuree sensor

1 Hormann UAP connecte dto the Hormann door motor

The software components are

Software to process status signals from the UAP

Software to send command signals to the UAP

Software to process in coming UDP packets with commands from the user

Software to send UDP packets in response to the user commands

Software to send UDP broadcast packets when door status or temperature / humidity / pressure changes

Software to process manual switch presses.

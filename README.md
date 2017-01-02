# AquaLedDimControl
Control LED lighting for aquarium using an Arduino Uno

**AquaLedDimControl.ino** controls sunrise and sunset with power-LEDs that are driven by a Mean Well DC-DC Constant Current Step-Down LED driver. These drivers are controlled by a PWM input.

A *DS3231* real time clock is used to keep time, and a 16x2 LCD display to show date, time and current brightness. Both of these use the I2C protocol.

The *DS3231* is always on winter time, during *Daylight Saving Time* the time is adjusted one hour forward after reading it from the *DS3231*. The flavour of DST used here is the European version, where the change occurs at 02.00 on the last Sunday of March and October.

**SetRTC.ino** is used to set the time on the *DS3231*, and to test any time related routines (determining if Daylight Saving is in effect).

Code to set and read the *DS3231* RTC: http://www.goodliffe.org.uk/arduino/i2c_devices.php

## Controlling brightness
While the LED emits light that is directly proportional to the amount of current flowing through it, the human eye doesn't perceive brightness in this fashion.
It perceives brightness on a logarithmic scale. Thus, for the human eye to perceive a linear increase of brightness, the brightness must increase logarithmically.

The standard PWM registers are 8 bit, and with only 256 steps, there is a quite visible change of brightness between steps at the low end.
This is not acceptable, so timer 1 (with 2 outputs) is configured as a 16 bit timer, giving 65536 steps.
The frequency of this PWM output is 244 Hz, which is ideal for use with the Mean Well drivers.

The traditional method of creating a logarithmic increase of brightness for 8 bit PWM, is to use a lookup table (which would consume 256 bytes).
Having a table for 16 bit PWM would require 65536 words. This is not possible, so a different solution is required.

During a fade, a float (**currentBrightness**) is multiplied (fade up) or divided by (fade down) by a constant (**fadeFactor**) every 100 milliseconds, and the integer part of **currentBrightness** is set as the current PWM output. This results in the brightness change seeming to be linear to the human eye.

A **fadeFactor** of 1.001387 will result in a 10 minute end to end fade time.
A **fadeFactor** of 1.0006935 will result in a 20 minute end to end fade time.

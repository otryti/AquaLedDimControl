#ifdef __IN_ECLIPSE__
//This is a automatic generated file
//Please do not modify this file
//If you touch this file your change will be overwritten during the next build
//This file has been generated on 2017-02-15 21:39:15

#include "Arduino.h"
#include "Arduino.h"
#include "Wire.h"
#include "LiquidCrystal_I2C.h"
#include "IRReadOnlyRemote.h"
void setupPWM16() ;
void printDigits(byte val, String separator) ;
byte dateSunday() ;
bool isDST() ;
void adjustTime() ;
void showTime() ;
void analogWrite16(unsigned int pin, unsigned int val) ;
void adjustBrightness() ;
void setTargetBrightness() ;
void overrideTargetBrightness(boolean up) ;
char getIR() ;
byte decToBcd(byte val) ;
void setRTCTime() ;
int getDayOfWeek(int dowYear, int dowMonth, int dowDay) ;
boolean dateOk(int testDay, int testMonth, int testYear) ;
void setRTCDate() ;
void setup() ;
void loop() ;

#include "AquaLedDimControl.ino"


#endif

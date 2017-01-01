#include "Arduino.h"
#include "Wire.h"

#define DS3231_I2C_ADDRESS 104

// Acknowledgement: The code for reading and writing the DS3231 RTC is copied from http://www.goodliffe.org.uk/arduino/i2c_devices.php

// SCL - pin A5
// SDA - pin A4
// To set the clock, run the sketch and use the serial monitor.
// Enter T1124154091014; the code will read this and set the clock. See the code for full details.
//
byte second, minute, hour, dayOfWeek, day, month, year;
boolean daylightSavingTime;
char weekDay[4];

byte tMSB, tLSB;
float temp3231;

void setup() {
	Wire.begin();
	Serial.begin(9600);
}

void printDigits(byte val, String separator) {
	if (val < 10) {
		Serial.print("0");
	}
	Serial.print(val);
	Serial.print(separator);
}

void loop() {

	watchConsole();

	get3231Date();

	Serial.print(weekDay);
	Serial.print(", ");
	printDigits(day, "/");
	printDigits(month, "-");
	printDigits(year, " - ");
	printDigits(hour, ":");
	printDigits(minute, ":");
	printDigits(second, " - Temp: ");
	Serial.print(get3231Temp());
	if (daylightSavingTime) {
		Serial.print(" ST");
	}
	Serial.println();

	delay(1000);
}

// Convert normal decimal numbers to binary coded decimal
byte decToBcd(byte val) {
	return ((val / 10 * 16) + (val % 10));
}

void watchConsole() {
	if (Serial.available()) { // Look for char in serial queue and process if found
		if (Serial.read() == 84) {      //If command = "T" Set Date
			set3231Date();
			get3231Date();
			Serial.println(" ");
		}
	}
}

void set3231Date() {
//T(sec)(min)(hour)(dayOfWeek)(dayOfMonth)(month)(year)
//T(00-59)(00-59)(00-23)(1-7)(01-31)(01-12)(00-99)
//Example: 02-Feb-09 @ 19:57:11 for the 3rd day of the week -> T1157193020209
//Sample dates for testing:
// T3059017270316 - 30 seconds before start of DST
// T3059017301016 - 30 seconds before end of DST
// T3059225010716 - 30 seconds before new day with DST
// T3059224300616 - 30 seconds before new day on the last day of a month with DST
// T3059234300616 - 30 seconds before new day on the last day of a month with DST, real time on timer chip
// T1124154091014
	second = (byte) ((Serial.read() - 48) * 10 + (Serial.read() - 48)); // Use of (byte) type casting and ascii math to achieve result.
	minute = (byte) ((Serial.read() - 48) * 10 + (Serial.read() - 48));
	hour = (byte) ((Serial.read() - 48) * 10 + (Serial.read() - 48));
	dayOfWeek = (byte) (Serial.read() - 48);
	day = (byte) ((Serial.read() - 48) * 10 + (Serial.read() - 48));
	month = (byte) ((Serial.read() - 48) * 10 + (Serial.read() - 48));
	year = (byte) ((Serial.read() - 48) * 10 + (Serial.read() - 48));
	Wire.beginTransmission(DS3231_I2C_ADDRESS);
	Wire.write(0x00);
	Wire.write(decToBcd(second));
	Wire.write(decToBcd(minute));
	Wire.write(decToBcd(hour));
	Wire.write(decToBcd(dayOfWeek));
	Wire.write(decToBcd(day));
	Wire.write(decToBcd(month));
	Wire.write(decToBcd(year));
	Wire.endTransmission();
}

byte dateSunday() {
	byte sunday = day - dayOfWeek;
	if (sunday < 25) {
		sunday += 7;
	}
	return sunday;
}

boolean isDST() {
	switch (month) {
	case 1:
	case 2:
		return false;
	case 3:
		if (day < 25) {
			return false;
		}
		if (dayOfWeek == 7) {
			return (hour < 2) ? false : true;
		}
		return (day < dateSunday()) ? false : true;
	case 10:
		if (day < 25) {
			return true;
		}
		if (dayOfWeek == 7) {
			return (hour >= 2) ? false : true;
		}
		return (day > dateSunday()) ? false : true;
	case 11:
	case 12:
		return false;
	default: return true;	// For dst, no test for the summer months in the switch.
	}
}

byte daysInMonth[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};	// Only months with DST will be used in this array - no need to test for leap year.

void adjustTime() {		// Adjust for daylight saving time (clock is always winter time)
	if (daylightSavingTime) {
		hour += 1;
		if (hour > 23) {
			hour = 0;
			dayOfWeek++;
			if (dayOfWeek > 7) {
				dayOfWeek = 1;
			}
			day++;
			if (day > daysInMonth[month]) {
				day = 1;
				month++;
			}
		}
	}
}

void get3231Date() {
	// send request to receive data starting at register 0
	Wire.beginTransmission(DS3231_I2C_ADDRESS); // 104 is DS3231 device address
	Wire.write(0x00); // start at register 0
	Wire.endTransmission();
	Wire.requestFrom(DS3231_I2C_ADDRESS, 7); // request seven bytes

	if (Wire.available()) {
		second = Wire.read(); // get second
		minute = Wire.read(); // get minutes
		hour = Wire.read();   // get hour
		dayOfWeek = Wire.read();
		day = Wire.read();
		month = Wire.read(); //temp month
		year = Wire.read();

		second = (((second & B11110000) >> 4) * 10 + (second & B00001111)); // convert BCD to decimal
		minute = (((minute & B11110000) >> 4) * 10 + (minute & B00001111)); // convert BCD to decimal
		hour = (((hour & B00110000) >> 4) * 10 + (hour & B00001111)); // convert BCD to decimal (assume 24 hour mode)
		dayOfWeek = (dayOfWeek & B00000111); // 1-7
		day = (((day & B00110000) >> 4) * 10 + (day & B00001111)); // 1-31
		month = (((month & B00010000) >> 4) * 10 + (month & B00001111)); //msb7 is century overflow
		year = (((year & B11110000) >> 4) * 10 + (year & B00001111));
	} else {
		//oh noes, no data!
	}

	daylightSavingTime = isDST();
	adjustTime();

	switch (dayOfWeek) {
	case 1:
		strcpy(weekDay, "Mon");
		break;
	case 2:
		strcpy(weekDay, "Tue");
		break;
	case 3:
		strcpy(weekDay, "Wed");
		break;
	case 4:
		strcpy(weekDay, "Thu");
		break;
	case 5:
		strcpy(weekDay, "Fri");
		break;
	case 6:
		strcpy(weekDay, "Sat");
		break;
	case 7:
		strcpy(weekDay, "Sun");
		break;
	}
}

float get3231Temp() {
	//temp registers (11h-12h) get updated automatically every 64s
	Wire.beginTransmission(DS3231_I2C_ADDRESS);
	Wire.write(0x11);
	Wire.endTransmission();
	Wire.requestFrom(DS3231_I2C_ADDRESS, 2);

	if (Wire.available()) {
		tMSB = Wire.read(); //2's complement int portion
		tLSB = Wire.read(); //fraction portion

		temp3231 = (tMSB & B01111111); //do 2's math on Tmsb
		temp3231 += ((tLSB >> 6) * 0.25); //only care about bits 7 & 8
	} else {
		//oh noes, no data!
	}

	return temp3231;
}


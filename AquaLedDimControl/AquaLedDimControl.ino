#include "Arduino.h"
#include "Wire.h"
#include "LiquidCrystal_I2C.h"
#include "IRReadOnlyRemote.h"

/****************************************************************************************************************************************************************************
 * This sketch is to be used to control power-LEDs that are driven by a Mean Well DC-DC Constant Current Step-Down LED driver.
 * These drivers are controlled by a PWM input.
 * A DS3231 real time clock is used to keep time, and a 16x2 LCD display to show date, time and current brightness. Both of these use the I2C protocol.
 *
 * While the LED emits light that is directly proportional to the amount of current flowing through it, the human eye doesn't perceive brightness in this fashion.
 * It perceives brightness on a logarithmic scale. Thus, for the human eye to perceive a linear increase of brightness, the brightness must increase logarithmically.
 *
 * The standard PWM registers are 8 bit, and with only 256 steps, there is a quite visible change of brightness between steps at the low end.
 * This is not acceptable, so timer 1 (with 2 outputs) is configured as a 16 bit timer, giving 65536 steps.
 * The frequency of this PWM output is 244 Hz, which is ideal for use with the Mean Well drivers.
 *
 * The traditional method of creating a logarithmic increase of brightness for 8 bit PWM, is to use a lookup table (which would consume 256 bytes).
 * Having a table for 16 bit PWM would require 65536 words. This is not possible, so a different solution is required.
 *
 * During a fade, a float (brightness) is multiplied (fade up) or divided by (fade down) by a constant (fadeFactor) every 100 milliseconds, and the integer part of brightness
 * is set as the current PWM output. This results in the brightness change seeming to be linear to the human eye.
 ***************************************************************************************************************************************************************************/

// List of points in time (in minutes from midnight), and the brightness to fade to.
//                       00.00 07.00  11.00  15.00  22.00 22.30 23.00 sentinel
unsigned int setTime[]  = {  0,  420,   660,   900,  1320, 1350, 1380, 1441 };
unsigned int setValue[] = { 16, 4095, 16383, 65535, 16383, 4095,   16 };

// Test of change

byte second, minute, hour, dayOfWeek, day, month, year;
bool daylightSavingTime;

float targetBrightness;
float currentBrightness;
float brightnessOverride = 0.0;
//float fadeFactor = 1.001387;  	// 10 minutes fade time
float fadeFactor = 1.0006935;	// 20 minutes fade time
float rapidFadeFactor = 1.1;
int brightnessPin = 10;			// 16 bit PWM output
int firstRelayPin = 13;			// Also connected to the internal led, this led will be on when the relay is off
int secondRelayPin = 12;

LiquidCrystal_I2C lcd(0x3f, 16, 2); // set the LCD address to 0x3f for a 16 chars and 2 line display
IRReadOnlyRemote irRemote(2);

#define DS3231_I2C_ADDRESS 104

// Configure digital pins 9 and 10 as 16-bit PWM outputs.
void setupPWM16() {
	DDRB |= _BV(PB1) | _BV(PB2);        			// set pins as outputs
	TCCR1A = _BV(COM1A1) | _BV(COM1B1)  			// non-inverting PWM
			| _BV(WGM11);              				// mode 14: fast PWM, TOP=ICR1
	TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS10);   // no prescaling
	ICR1 = 0xffff;                      			// TOP counter value
}

void printDigits(byte val, String separator) {
	if (val < 10) {
		lcd.print("0");
	}
	lcd.print(val);
	lcd.print(separator);
}

byte dateSunday() {
	byte sunday = day - dayOfWeek;
	if (sunday < 25) {
		sunday += 7;
	}
	return sunday;
}

// Calculate if daylight saving time is in effect.
// This is for the European version, in which the change happens at 02.00 am on the last Sunday of March (change to summer time),
// and on the last Sunday of October (change back to winter time).
// Most often there are 4 Sundays in March and October, but once in a while there are 5.
// The last Sunday must be in the range of 25th to 31st of the month.
// The DS3231 keeps track of day of week, this simplifies calculating if DST is in effect or not.
bool isDST() {
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
	default: return true;	// For DST, no test for the summer months in the switch.
	}
}

byte daysInMonth[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};	// Only months with DST will be used in this array - no need to test for leap year.

// Adjust for daylight saving time (clock is always winter time)
void adjustTime() {
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

void showTime() {
	// The DS3231 RTC is used to keep time.
	// Reading the time with in-line code is easy enough to do without adding a RTC library to the project.
	// send request to receive data starting at register 0
	Wire.beginTransmission(DS3231_I2C_ADDRESS); // 104 is DS3231 device address
	Wire.write(0x00); // start at register 0
	Wire.endTransmission();
	Wire.requestFrom(DS3231_I2C_ADDRESS, 7); // request seven bytes

	if (Wire.available()) {
		second = Wire.read();
		minute = Wire.read();
		hour = Wire.read();
		dayOfWeek = Wire.read();
		day = Wire.read();
		month = Wire.read();
		year = Wire.read();

		second = (((second & B11110000) >> 4) * 10 + (second & B00001111)); // convert BCD to decimal
		minute = (((minute & B11110000) >> 4) * 10 + (minute & B00001111)); // convert BCD to decimal
		hour = (((hour & B00110000) >> 4) * 10 + (hour & B00001111)); 		// convert BCD to decimal (assume 24 hour mode)
		dayOfWeek = (dayOfWeek & B00000111); // 1-7
		day = (((day & B00110000) >> 4) * 10 + (day & B00001111)); 			// 1-31
		month = (((month & B00010000) >> 4) * 10 + (month & B00001111)); 	//msb7 is century overflow
		year = (((year & B11110000) >> 4) * 10 + (year & B00001111));
	} else {
		return;
	}

	daylightSavingTime = isDST();
	adjustTime();

	lcd.home();
	printDigits(hour, ":");
	printDigits(minute, ":");
	printDigits(second, "   ");
	lcd.setCursor(0, 1);
	switch (dayOfWeek) {
	case 1:
		lcd.print("Mon ");
		break;
	case 2:
		lcd.print("Tue ");
		break;
	case 3:
		lcd.print("Wed ");
		break;
	case 4:
		lcd.print("Thu ");
		break;
	case 5:
		lcd.print("Fri ");
		break;
	case 6:
		lcd.print("Sat ");
		break;
	case 7:
		lcd.print("Sun ");
		break;
	}
	printDigits(day, "/");
	printDigits(month, "-");
	printDigits(year, daylightSavingTime ? " ST " : "    ");
}

/* 16-bit version of analogWrite(). Works only on pins 9 and 10. */
void analogWrite16(unsigned int pin, unsigned int val) {
	switch (pin) {
	case 10:
		OCR1B = val;
		break;
	case 9:
		OCR1A = val;
		break;
	}

	// Set the relay pins. Relay is energized when the output is LOW.
	// First relay powers LED fans, turn on when brightness is 4096 or higher
	digitalWrite(firstRelayPin, val >= 4096 ? LOW : HIGH);
	// Second relay powers plant grow LED strips, turn on when brightness is maximum
	digitalWrite(secondRelayPin, val == 65535 ? LOW : HIGH);

	char buffer[8];
	sprintf(buffer, "%5u", val);
	lcd.setCursor(11, 0);
	lcd.print(buffer);
}

void adjustBrightness() {
	if (targetBrightness == currentBrightness) {
		return;
	}
	if (targetBrightness > currentBrightness) {
		currentBrightness *= fadeFactor;
		if (currentBrightness >= targetBrightness) {
			currentBrightness = targetBrightness;
		}
	}
	if (targetBrightness < currentBrightness) {
		currentBrightness /= fadeFactor;
		if (currentBrightness <= targetBrightness) {
			currentBrightness = targetBrightness;
		}
	}
	analogWrite16(brightnessPin, (unsigned int) currentBrightness);
}

void setTargetBrightness() {
	unsigned int currentMinutes = hour * 60 + minute;
	for (int i = 0;; i++) {
		if (currentMinutes >= setTime[i] && currentMinutes < setTime[i + 1]) {
			if (brightnessOverride == (float)setValue[i]) {
				return;
			}
			brightnessOverride = 0.0;
			targetBrightness = (float)setValue[i];
			return;
		}
	}
}

void overrideTargetBrightness(boolean up) {
	if (brightnessOverride == 0.0) {
		brightnessOverride = targetBrightness;
		targetBrightness = currentBrightness;
	}
	if (up) {
		targetBrightness *= rapidFadeFactor;
		if (targetBrightness > 65535.0) {
			targetBrightness = 65535.0;
		}
	} else {
		targetBrightness /= rapidFadeFactor;
		if (targetBrightness < 16.0) {
			targetBrightness = 16.0;
		}
	}
	currentBrightness = targetBrightness;
	analogWrite16(brightnessPin, (unsigned int) currentBrightness);
}

char getIR() {
	unsigned long irData = irRemote.read();
	switch (irData) {
	case 0xFF4AB5: return('0');
	case 0xFF6897: return('1');
	case 0xFF9867: return('2');
	case 0xFFB04F: return('3');
	case 0xFF30CF: return('4');
	case 0xFF18E7: return('5');
	case 0xFF7A85: return('6');
	case 0xFF10EF: return('7');
	case 0xFF38C7: return('8');
	case 0xFF5AA5: return('9');
	case 0xFF42BD: return('*');
	case 0xFF52AD: return('#');
	case 0xFF22DD: return('L');
	case 0xFFC23D: return('R');
	case 0xFF629D: return('U');
	case 0xFFA857: return('D');
	case 0xFF02FD: return('K');	// OK key
	case 0xFFFFFFFF: return('X');	// Repeat
	default: return(0);
	}
}

byte decToBcd(byte val) {
	return ((val / 10 * 16) + (val % 10));
}

unsigned int cursorOffset[] = { 6, 7, 9, 10, 12, 13 };

void setRTCTime() {
	unsigned long lastActivity = millis();
	lcd.home();
	lcd.print("Time: ");
	char timeBuffer[11];
	sprintf(timeBuffer, "%02u", hour);
	sprintf(timeBuffer+2, ":");
	sprintf(timeBuffer+3, "%02u", minute);
	sprintf(timeBuffer+5, ":");
	sprintf(timeBuffer+6, "%02u", second);
	sprintf(timeBuffer+8, "  ");
	lcd.print(timeBuffer);
	lcd.setCursor(6, 0);
	lcd.cursor();
	int pos = 0;
	for (;;) {
		if (millis() - lastActivity > 30000ul) {
			break;	// Inactivity timeout, abort
		}

		char irChar = getIR();
		if (irChar == 0) {
			continue;
		}

		if (irChar == '#') {
			break;	// Abort
		}

		lastActivity = millis();
		if (irChar >= '0' && irChar <= '9') {
			switch (pos) {
			case 0:
				if (irChar <= '2') {
					timeBuffer[0] = irChar;
					pos++;
				}
				break;
			case 1:
				if ((timeBuffer[0] == '2' && irChar <= '3') || (timeBuffer[0] < '2')) {
					timeBuffer[1] = irChar;
					pos++;
				}
				break;
			case 2:
			case 4:
				if (irChar <= '5') {
					timeBuffer[cursorOffset[pos] - 6] = irChar;
					pos++;
				}
				break;
			case 3:
			case 5:
				timeBuffer[cursorOffset[pos] - 6] = irChar;
				pos++;
				break;
			}
		}

		if (irChar == 'R') {
			pos++;
		}

		if (irChar == 'L') {
			pos--;
		}

		if (irChar == 'K') {
			// Set new time
			int testHour = (timeBuffer[0] - 48) * 10 + (timeBuffer[1] - 48);
			// If daylight saving is in effect, subtract one hour from the time entered.
			if (isDST()) {
				testHour--;
				if (testHour < 0) {
					testHour = 23;
				}
			}
			Wire.beginTransmission(DS3231_I2C_ADDRESS);
			Wire.write(0x00);
			Wire.write(((timeBuffer[6] - 48) * 16) + (timeBuffer[7] - 48));
			Wire.write(((timeBuffer[3] - 48) * 16) + (timeBuffer[4] - 48));
			Wire.write(decToBcd(testHour));
			Wire.endTransmission();
			break;
		}

		if (pos > 5) {
			pos = 0;
		}
		if (pos < 0) {
			pos = 5;
		}
		lcd.setCursor(6, 0);
		lcd.print(timeBuffer);
		lcd.setCursor(cursorOffset[pos], 0);
	}

	lcd.noCursor();
	char buffer[8];
	sprintf(buffer, "%5u", (unsigned int) currentBrightness);
	lcd.setCursor(11, 0);
	lcd.print(buffer);
}

// Calculation of day of week according to the Key Value method
// http://mathforum.org/dr.math/faq/faq.calendar.html
int monthKeyValue[] = { 0, 1, 4, 4, 0, 2, 5, 0, 3, 6, 1, 4, 6 };
int getDayOfWeek(int dowYear, int dowMonth, int dowDay) {
	int step = dowYear / 4;
	step += dowDay;
	step += monthKeyValue[dowMonth];
	if (dowMonth < 3 && (dowYear%4) == 0) {
		step--;
	}
	step += 6;
	step += dowYear;
	int dow = step % 7;
	dow--;
	if (dow < 1) {
		dow += 7;
	}

	return dow;
}

boolean dateOk(int testDay, int testMonth, int testYear) {
	if (testMonth < 1 || testMonth > 12) {
		return false;
	}
	if (testDay < 1) {
		return false;
	}
	if (testDay <= daysInMonth[testMonth]) {
		return true;
	}
	if (testMonth == 2 && testDay == 29 && testYear % 4 == 0) {
		return true;
	}
	return false;
}

void setRTCDate() {
	int testDay, testMonth, testYear;
	boolean badDateShown = false;
	unsigned long lastActivity = millis();
	lcd.home();
	lcd.print("Date: ");
	char dateBuffer[11];
	sprintf(dateBuffer, "%02u", day);
	sprintf(dateBuffer+2, "/");
	sprintf(dateBuffer+3, "%02u", month);
	sprintf(dateBuffer+5, "-");
	sprintf(dateBuffer+6, "%02u", year);
	sprintf(dateBuffer+8, "  ");
	lcd.print(dateBuffer);
	lcd.setCursor(6, 0);
	lcd.cursor();
	int pos = 0;
	for (;;) {
		if (millis() - lastActivity > 30000ul) {
			break;	// Inactivity timeout, abort
		}

		char irChar = getIR();
		if (irChar == 0) {
			continue;
		}

		if (irChar == '#') {
			break;	// Abort
		}

		lastActivity = millis();
		if (badDateShown) {
			lcd.setCursor(0, 1);
			lcd.print("                ");
			badDateShown = false;
			lcd.setCursor(6, 0);
		}

		if (irChar >= '0' && irChar <= '9') {
			switch (pos) {
			case 0:
				if (irChar <= '3') {
					dateBuffer[0] = irChar;
					pos++;
				}
				break;
			case 1:
				testDay = (dateBuffer[0] - 48) * 10 + (irChar - 48);
				if (testDay >= 1 && testDay <= 31) {
					dateBuffer[1] = irChar;
					pos++;
				}
				break;
			case 2:
				if (irChar <= '1') {
					dateBuffer[3] = irChar;
					pos++;
				}
				break;
			case 3:
				testMonth = (dateBuffer[3] - 48) * 10 + (irChar - 48);
				if (testMonth >= 1 && testMonth <= 12) {
					dateBuffer[4] = irChar;
					pos++;
				}
				break;
			case 4:
			case 5:
				dateBuffer[cursorOffset[pos] - 6] = irChar;
				pos++;
				break;
			}
		}

		if (irChar == 'R') {
			pos++;
		}

		if (irChar == 'L') {
			pos--;
		}

		if (irChar == 'K') {
			testDay = (dateBuffer[0] - 48) * 10 + (dateBuffer[1] - 48);
			testMonth = (dateBuffer[3] - 48) * 10 + (dateBuffer[4] - 48);
			testYear = (dateBuffer[6] - 48) * 10 + (dateBuffer[7] - 48);
			if (!dateOk(testDay, testMonth, testYear)) {
				lcd.setCursor(0, 1);
				lcd.print("Bad date        ");
				badDateShown = true;
				pos = 0;
				lcd.setCursor(6, 0);
				continue;
			}
			// Set new time
			Wire.beginTransmission(DS3231_I2C_ADDRESS);
			Wire.write(0x03);
			Wire.write(getDayOfWeek(testYear, testMonth, testDay));
			Wire.write(decToBcd(testDay));
			Wire.write(decToBcd(testMonth));
			Wire.write(decToBcd(testYear));
			Wire.endTransmission();
			break;
		}

		if (pos > 5) {
			pos = 0;
		}
		if (pos < 0) {
			pos = 5;
		}
		lcd.setCursor(6, 0);
		lcd.print(dateBuffer);
		lcd.setCursor(cursorOffset[pos], 0);
	}
	lcd.noCursor();
	char buffer[8];
	sprintf(buffer, "%5u", (unsigned int) currentBrightness);
	lcd.setCursor(11, 0);
	lcd.print(buffer);
}

void setup() {
//	Serial.begin(115200);
//	Serial.println("Serial monitor started");
	Wire.begin();
	lcd.init();
	lcd.backlight();
	pinMode(firstRelayPin, OUTPUT);
	pinMode(secondRelayPin, OUTPUT);
	setupPWM16();
	showTime();
	setTargetBrightness();
	currentBrightness = targetBrightness;
	analogWrite16(brightnessPin, (unsigned int)currentBrightness);
}

void loop() {
	unsigned long now = millis();

	// Update the clock once per second
	static unsigned long prevShowTime = 0xffffffff;
	if ((now / 1000ul) != prevShowTime) {
		prevShowTime = now / 1000ul;
		showTime();
		setTargetBrightness();
	}

	// Update the brightness once per 100 milliseconds
	static unsigned long prevAdjustBrightness = 0xffffffff;
	if ((now / 100ul) != prevAdjustBrightness) {
		prevAdjustBrightness = now / 100ul;
		adjustBrightness();
	}

	// Read the IR
	static char lastIrChar = 0;
	char irChar = getIR();
	if (irChar != 0) {
		if (irChar == 'X') {
			irChar = lastIrChar;
		}
		switch (irChar) {
		case 'U':
			lastIrChar = irChar;
			overrideTargetBrightness(true);
			break;
		case 'D':
			lastIrChar = irChar;
			overrideTargetBrightness(false);
			break;
		case 'K':
			lastIrChar = 0;
			brightnessOverride = 0.0;
			break;
		case '1':
			lastIrChar = 0;
			setRTCTime();
			break;
		case '2':
			lastIrChar = 0;
			setRTCDate();
			break;
		default:
			lastIrChar = 0;
		}
	}
}

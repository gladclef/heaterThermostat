/*
 *	Control the box fan and send temperature readings to Adafruit IO
 */

// Include the libraries we need
#include "Adafruit_MCP9808.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include "Adafruit_LEDBackpack.h"

#define DOTVAL (1 << 14)
#define FANPIN 0

// use Adafruit IO and set up the 'digital' feed
#define USEIO
#ifdef USEIO
#include "config.h"
AdafruitIO_Feed *digital = io.feed("House Temperature");
AdafruitIO_Feed *fanOn = io.feed("Fan On");
#endif

// Create the MCP9808 temperature sensor object
Adafruit_MCP9808 tempsensor = Adafruit_MCP9808();
int currentDirection = 0;
int previousDirection = 0;

// set up the LED backpack
Adafruit_AlphaNum4 alpha4 = Adafruit_AlphaNum4();
int packWriteOnOff = 0;

void setup() {
	Serial.begin(115200);
	delay(100);

	// connect to the LED backpack
	alpha4.begin(0x70);	// pass in the address
	
	// connect to io.adafruit.com
#ifdef USEIO
	Serial.print("Connecting to Adafruit IO");
	io.connect();
 
	// wait for a connection
	int dotIdx = 3;
	while(io.status() < AIO_CONNECTED) {
		Serial.print(".");
		alpha4.writeDigitAscii(0, ' ');
		alpha4.writeDigitAscii(1, ' ');
		alpha4.writeDigitAscii(2, ' ');
		alpha4.writeDigitAscii(3, ' ');
		alpha4.writeDigitRaw(dotIdx, DOTVAL);
		alpha4.writeDisplay();
		dotIdx--;
		if (dotIdx < 0)
			dotIdx = 3;
		delay(500);
	}
	
	// we are connected
	Serial.println();
	Serial.println(io.statusText());
#endif
	
	// Make sure the sensor is found, you can also pass in a different i2c
	// address with tempsensor.begin(0x19) for example
	if (!tempsensor.begin()) {
		Serial.println("Couldn't find MCP9808!");
		alpha4.writeDigitAscii(0, 'N');
		alpha4.writeDigitAscii(1, 'O');
		alpha4.writeDigitAscii(2, 'N');
		alpha4.writeDigitAscii(3, 'E');
		alpha4.writeDisplay();
		while (1);
	}

	// set up the fan pin output
	pinMode(FANPIN, OUTPUT);
}

uint16_t getChar(char* sVal, float fVal, int index) {
	int decimalPlace = 0;
	if (fVal > 9.999) decimalPlace = 1;
	if (fVal > 99.99) decimalPlace = 2;
	if (fVal > 999.9) decimalPlace = 3;

	char charVal = (index > decimalPlace) ? sVal[index+1] : sVal[index];
	alpha4.writeDigitAscii(index, charVal);
	uint16_t retval = alpha4.displaybuffer[index];
	if (decimalPlace == index ||
		(index == 3 && currentDirection == 1))
	{
		retval |= DOTVAL;
	}

	return retval;
}

unsigned long lastUpload = 0;
void saveData(float f, unsigned long currTime)
{
	if (currTime - lastUpload < 5000) {
		return;
	}
#ifdef USEIO
	digital->save(f);
#endif
	lastUpload = currTime;
}

// int initWaitForHigh = 1;
// float prevLowestTemp = 0;
// float currLowestTemp = 0;
// float prevHighestTemp = 0;
// float currHighestTemp = 0;
// void updateLowHighTemps(float currTemp)
// {
// 	switch (currentDirection)
// 	{
// 	case -1: // falling
// 		if (previousDirection == 1) {
// 			prevLowestTemp = currLowestTemp;
// 			currLowestTemp = f;
// 			initWaitForHigh = 0;
// 		}
// 		if (f < currLowestTemp) {
// 			currLowestTemp = f;
// 		}
// 	case 1: // rising
// 		if (previousDirection == -1) {
// 			prevHighestTemp = currHighestTemp;
// 			currHighestTemp = f;
// 		}
// 		if (f > currHighestTemp) {
// 			currHighestTemp = f;
// 		}
// 	}
// }

#define PREV_VAL_MAX 3
#define MIN_INCREASE_SPREAD 2.0
#define MIN_DECREASE_SPREAD 1.0
#define FAN_ON_TIME 120000 // 2 minute(s)
#define MIN_FAN_OFF_TIME 60000 // 1 minute(s)
int prevValCnt = 0;
unsigned long lastFanCheckTime = 0;
float prevVals[PREV_VAL_MAX];
unsigned long lastFanOnTime = 0, lastFanOffTime = 0;
int isFanOn = FALSE; // starts off
void turnOnHeaterFan(float f, unsigned long currTime)
{
	// is it time to check for an update?
	if (currTime - lastFanCheckTime < 5000)
	{
		return;
	}
	lastFanCheckTime = currTime;

	// initialization stuffs
	if (prevValCnt < PREV_VAL_MAX)
	{
		char str[100];
		sprintf(str, "loading previous value %.2f\n\r", f);
		Serial.print(str);
		prevVals[prevValCnt] = f;
		prevValCnt++;
		return;
	}

	// do we have a consistent increase or decrease?
	float firstVal = prevVals[0];
	int consistentIncrease = (f - firstVal) > MIN_INCREASE_SPREAD;
	int consistentDecrease = (firstVal - f) > MIN_DECREASE_SPREAD;
	char str[100];
	sprintf(str, "relVals: ");
	char *strPtr = str + (int)strlen(str);
	for (int i = 0; i < PREV_VAL_MAX; i++)
	{
		float last = prevVals[i];
		float next = (i == PREV_VAL_MAX-1) ? f : prevVals[i + 1];

		consistentIncrease = consistentIncrease & (next > last);
		consistentDecrease = consistentDecrease & (next < last);
		sprintf(strPtr, "(%.2f/%.2f %d) ", last, next, (next > last) ? 1 : ((next < last) ? -1 : 0));
		strPtr = str + (int)strlen(str);
	}
	sprintf(strPtr, "\n\r");
	Serial.print(str);

	// check for change in direction
	packWriteOnOff = 0;
	previousDirection = currentDirection;
	int changedDirection = 0;
	if (consistentIncrease)
	{
		// temperature rising!
		if (currentDirection == -1)
			changedDirection = 1;
		currentDirection = 1;
	}
	else if (consistentDecrease)
	{
		// temperature rising!
		if (currentDirection == 1)
			changedDirection = 1;
		currentDirection = -1;
	}
	// updateLowHighTemps(f);

	// turn on the fan if:
	// -the fan is currently off
	// -the temperature is rising
	// -the fan hasn't been on recently
	long timeSinceLastOn = currTime - lastFanOnTime;
	if (!isFanOn && currentDirection == 1 && timeSinceLastOn > MIN_FAN_OFF_TIME)
	{
		isFanOn = TRUE;
		lastFanOnTime = currTime;
		digitalWrite(FANPIN, HIGH);
		Serial.print("fan on\n\r");
		packWriteOnOff = 1;
#ifdef USEIO
		fanOn->save(1);
#endif
	}

	// turn off the fan if:
	// -the fan is currently on
	// -the fan on time has expired
	long timeSinceLastOff = currTime - lastFanOffTime;
	if (isFanOn && timeSinceLastOff > FAN_ON_TIME)
	{
		isFanOn = FALSE;
		lastFanOnTime = currTime;
		digitalWrite(FANPIN, LOW);
		Serial.print("fan off\n\r");
		packWriteOnOff = -1;
#ifdef USEIO
		fanOn->save(0);
#endif
	}

	// log the current temperature
	for (int i = 0; i < PREV_VAL_MAX; i++)
	{
		prevVals[i] = prevVals[i + 1];
	}
	prevVals[PREV_VAL_MAX - 1] = f;
}

void loop() 
{
	delay(500);
	unsigned long currTime = millis();

	// io.run(); is required for all sketches.
	// it should always be present at the top of your loop
	// function. it keeps the client connected to
	// io.adafruit.com, and processes any incoming data.
#ifdef USEIO
	io.run();
#endif

	// Read and print out the temperature, then convert to *F
	float c = tempsensor.readTempC();
	float f = c * 9.0 / 5.0 + 32;
	// Serial.print("Temp: "); Serial.print(c); Serial.print("*C\t"); 
	// Serial.print(f); Serial.println("*F");

	// write data to the display
	if (packWriteOnOff == 1)
	{
		// fan has recently turned on
		alpha4.writeDigitAscii(0, ' ');
		alpha4.writeDigitAscii(1, ' ');
		alpha4.writeDigitAscii(2, 'O');
		alpha4.writeDigitAscii(3, 'N');
	}
	else if (packWriteOnOff == -1)
	{
		// fan has recently turnned off
		alpha4.writeDigitAscii(0, ' ');
		alpha4.writeDigitAscii(1, 'O');
		alpha4.writeDigitAscii(2, 'F');
		alpha4.writeDigitAscii(3, 'F');
	}
	else
	{
		// read out the temperature on the display
		char dat[10];
		memset(dat, 0, 10);
		sprintf(dat, "%f", f);
		alpha4.writeDigitRaw(0, getChar(dat, f, 0));
		alpha4.writeDigitRaw(1, getChar(dat, f, 1));
		alpha4.writeDigitRaw(2, getChar(dat, f, 2));
		alpha4.writeDigitRaw(3, getChar(dat, f, 3));
	}
	alpha4.writeDisplay();

	// determine if we should turn on the blower across the heater
	turnOnHeaterFan(f, currTime);

	// save the temperature to IO
	saveData(f, currTime);
}

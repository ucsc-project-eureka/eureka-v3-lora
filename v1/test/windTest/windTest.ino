/*
Author: PaskKat
Date: 9/17/2026
Board in Arduino IDE: Arduino Zero (Native USB)

Purpose: 
--> Initialize I2C connections to sensors and initialize the wind and rainfall sensors.
--> Obtain voltage readings via I2C, successfully convert them to 

Hardware:
--> Atmos Lab V3 board, SAMD-21 microprocessor, 
    EUREKA wind sensor custom board with ADS1115 ADC and no ALERT pin
--> Sensors used: SEN-15901 Wind Sensor

Notes:
--> (J1 port) Wind speed: Switch closes once per second at 1.492mi/hr.
--> (J1 port) Wind direction: 16 different positions indicated by resistance levels. 
              - See SEN-15901 docs for resistance table. Corresponding voltages for 4.7K pull-up are used.
              - Gain set at x1 (ideal for 3V3). LSB conversion dictates float V = readV * (125.0e-6)
--> (J4 port) Rainfall: Every 0.011in more rain on the sensor, one interrupt triggered.
              - Count the interrupts within 24 hour blocks for daily rainfall.
              - Main issue: V1 EUREKA SEN-15901 breakout does not connect the ADS1115's ALERT
              --> so no interrupts are possible. This needs to be fixed in another version.
*/ 

#include <Wire.h>
#include <SPI.h>
#include <Adafruit_ADS1X15.h>

// Defs and instantiation ---------------------------------------------------------

#define DEBUG_PORT SerialUSB
#define ADS_I2C_ADDRESS 0x48
#define COMPASS_DIRECTIONS 16
#define WIND_MEASURE_PERIOD 3000
#define GAIN_ONE_CONVERSION_FACTOR 125.0e-6
#define FLOAT_SIMILARITY_BOUNDARY 0.05
#define DEBOUNCE_ERROR_INTERVAL 50
#define MPH_PER_SWITCH_HZ 1.492
#define TEST_TIMEOUT 5000

Adafruit_ADS1115 ads;

const float reading[COMPASS_DIRECTIONS] //map of 16 digital voltage values, 0-360 degrees by intervals of 22.5 degrees.
  = {2.899, 2.036, 2.184, 0.947, 0.985, 0.872, 1.337, 1.119, 1.684, 1.545, 2.585, 2.517, 3.177, 2.976, 3.080, 2.738};

// NOTE: annemometer angles are simply 22.5 * {index of the voltage reading directions}

// Globals -----------------------------------------------------------------------

bool dataReady = false;
unsigned long lastTestTime;
float windDirection;
int rotationCount;

// Helpers -----------------------------------------------------------------------

void initADS(){
  bool foundI2C = ads.begin(ADS_I2C_ADDRESS);
  if(!foundI2C){
    DEBUG_PORT.println("I2C is not found at given address!");
  }
  else{DEBUG_PORT.println("I2C verified at that address!");}
  ads.setGain(GAIN_ONE);
  return;
}

// Comparing readings that vary experimentally from theoretical ideals.
bool isCloseTo(float f1, float f2){
  if (abs(f1-f2) <= FLOAT_SIMILARITY_BOUNDARY){return true;}
  return false;
}

float getWindSpeed(){
  DEBUG_PORT.println("getting windspeed...");
  rotationCount = 0;
  unsigned long windReadingStartTime = millis();
  float adsReading = ads.readADC_SingleEnded(2) * GAIN_ONE_CONVERSION_FACTOR;
  unsigned long lastNewReadingTime = millis();
  while(millis() - windReadingStartTime < WIND_MEASURE_PERIOD){
    float newReading = ads.readADC_SingleEnded(2) * GAIN_ONE_CONVERSION_FACTOR;
    if(!isCloseTo(adsReading,newReading)&&
        (newReading > adsReading)&&
       (millis() - lastNewReadingTime >= DEBOUNCE_ERROR_INTERVAL)){
        rotationCount++;
        lastNewReadingTime = millis();
    }
    adsReading = newReading;
    }
  float speed = (rotationCount/3.0f) * MPH_PER_SWITCH_HZ;
  return speed;
}

float getWindDirVoltage(){
  float v = -1.0;
  int16_t adsReading = ads.readADC_SingleEnded(1);
  v = adsReading * GAIN_ONE_CONVERSION_FACTOR;
  return v;
}

float getWindDirection(){
  // Read from channel 1.
  DEBUG_PORT.println("getting wind direction...");
  float windDirection = -1.0;
  int16_t adsReading = ads.readADC_SingleEnded(1);
  float voltage = adsReading * GAIN_ONE_CONVERSION_FACTOR;
  for(int i = 0; i<COMPASS_DIRECTIONS;i++){
    if(isCloseTo(voltage,reading[i])){
      windDirection = i * 22.5;
      break;
    }
  }
  return windDirection;
}

// MAIN --------------------------------------------------------------------------

void setup() {
  // turn on the radio from the coproc
  PORT->Group[0].DIRSET.reg = PORT_PA17;
  PORT->Group[0].OUTSET.reg = PORT_PA17;
  // turn on sensor I2C
  PORT->Group[0].DIRSET.reg = PORT_PA05;
  PORT->Group[0].OUTSET.reg = PORT_PA05;

  DEBUG_PORT.begin(115200);
  delay(3000);
  DEBUG_PORT.println("Serial initialized");
  initADS();
  DEBUG_PORT.println("setup complete!");
}

void loop() {
  // every 5 seconds
  if (millis() - lastTestTime >= TEST_TIMEOUT){
    float windSpeed = getWindSpeed();
    // float windDirection = getWindDirection();
    float windDirection = getWindDirection();
    dataReady = true;

    DEBUG_PORT.println("Wind speed (mph): ");
    DEBUG_PORT.println(windSpeed);
    DEBUG_PORT.println("Wind Direction (degrees from north): ");
    DEBUG_PORT.println(windDirection);
    // reset time.
    lastTestTime = millis();
  }

}

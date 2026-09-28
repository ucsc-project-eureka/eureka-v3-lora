/*
Author: PaskKat
Date: 8/16/2026
Board in Arduino IDE: Arduino Zero (Native USB)

Purpose: 
--> Establish serial connection via UART Serial 1 with ESP32 Radio and SAMD21 coprocessor
--> Initialize I2C connections to sensors and initialize sensors
--> when prompted by radio, send latest data readings from sensors.

Hardware:
--> Atmos Lab V3 board
--> Sensors used: BME680, Adafruit Stemma Soil Sensor, SEN-15901 Wind Sensor
*/ 

#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>

// MODS
#include <Adafruit_seesaw.h>
#include "Adafruit_BME680.h"
// #include "SparkFun_Weather_Meter_Kit_Arduino_Library.h"

// Other setup pinouts --------------------------------------

#define DEBUG_PORT SerialUSB
#define ESP_PORT Serial1

#define ESP_BAUD 9600

#define SOIL_I2C 0x36

#define BME_SCK 13
#define BME_MISO 12
#define BME_MOSI 11
#define BME_CS 10

#define ADS_I2C_ADDRESS 0x48

// Reference values for sensor data processing.
#define SEALEVELPRESSURE_HPA (1013.25)
#define COMPASS_DIRECTIONS 16
#define WIND_MEASURE_PERIOD 3000
#define GAIN_ONE_CONVERSION_FACTOR 125.0e-6
#define FLOAT_SIMILARITY_BOUNDARY 0.05
#define DEBOUNCE_ERROR_INTERVAL 50
#define MPH_PER_SWITCH_HZ 1.492

// Reference all appropriate fields.
#define wirePort Wire               // I2C Bus port name.
Adafruit_BME680 bme(&wirePort);     // I2C
Adafruit_seesaw ss;                 // Soil sensor.
// SFEWeatherMeterKit weatherMeterKit(windDirectionPin, windSpeedPin, rainfallPin); // SEN-15901

// Packet types
enum{
  BEACON,
  SENSOR_DATA,
  AGG_DATA
};

// Packet defs
struct dataPacket_t {
  uint8_t type;
  float temperature;
  float humidity;
  uint16_t soilMoisture;
  float windDirection;
  float windSpeed;
  // float rainfall;
  unsigned long timestamp;
};

// Gen Defs
const float reading[COMPASS_DIRECTIONS] //map of 16 digital voltage values, 0-360 degrees by intervals of 22.5 degrees.
  = {2.899, 2.036, 2.184, 0.947, 0.985, 0.872, 1.337, 1.119, 1.684, 1.545, 2.585, 2.517, 3.177, 2.976, 3.080, 2.738};

// Helpers ----------------------------------------------------------------------

void initADS(){
  bool foundI2C = ads.begin(ADS_I2C_ADDRESS);
  if(!foundI2C){
    DEBUG_PORT.println("ADS not connected via I2C!");
  }
  else{DEBUG_PORT.println("ADS found via I2C!");}
  ads.setGain(GAIN_ONE);
  return;
}

// Comparing readings that vary experimentally from theoretical ideals.
bool isCloseTo(float f1, float f2){
  if (abs(f1-f2) <= FLOAT_SIMILARITY_BOUNDARY){return true;}
  return false;
}

float getWindSpeed(){
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

float getWindDirection(){
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

// Global variables: Mainly time keeping markers and flags.
int startTime;
int currentTime;
bool getDataFlag = false;

void setup(){
  // turn on the radio from the coproc
  PORT->Group[0].DIRSET.reg = PORT_PA17;
  PORT->Group[0].OUTSET.reg = PORT_PA17;
  // turn on sensor I2C
  PORT->Group[0].DIRSET.reg = PORT_PA05;
  PORT->Group[0].OUTSET.reg = PORT_PA05;

  DEBUG_PORT.begin(115200);
  while(!DEBUG_PORT);
  DEBUG_PORT.println("DEBUG_PORT initialized, ESP32 pins pulled HIGH");
  // Get UART connecting coproc and esp32 online.
  ESP_PORT.begin(ESP_BAUD); // UART, coproc->esp32 and vice versa.
  while(!ESP_PORT);

  // initialize the sensors.
  Wire.begin();
  bme.begin();
  ss.begin(SOIL_I2C); 
  initADS();
  
  startTime = millis();
  return;
}

void loop(){
  currentTime = millis() - startTime;
  if (ESP_PORT.available()){
    String input = ESP_PORT.readStringUntil('\n');
    input.trim();
    // NOTE: Assuming printed format for received "give data" message this way:
    if (input == "SENSOR_DATA"){
      getDataFlag = true;
    }
  }
  if (getDataFlag){
    DEBUG_PORT.println("received GET_DATA command from radio");
    dataPacket_t myData;
    myData.type = SENSOR_DATA;

    // NOTE: this assumes parsing on the other side will pick up string data sent in this format.
    ESP_PORT.println("SENSOR_DATA:");

    // get latest data.
    bme.performReading();
    myData.temperature = bme.temperature;
    myData.humidity = bme.humidity;
    myData.soilMoisture = ss.touchRead(0); 
    myData.windDirection = getWindDirection();
    myData.windSpeed = getWindSpeed();

    // myData.rainfall = weatherMeterKit.getTotalRainfall();
    myData.timestamp = currentTime;
    
    ESP_PORT.println(myData.temperature);
    ESP_PORT.println(myData.humidity);
    ESP_PORT.println(myData.soilMoisture);
    ESP_PORT.println(myData.windDirection);
    ESP_PORT.println(myData.windSpeed);
    // ESP_PORT.println(myData.rainfall);
    ESP_PORT.println(myData.timestamp);
    DEBUG_PORT.println("sent DATA");
    getDataFlag = false;
  }
}
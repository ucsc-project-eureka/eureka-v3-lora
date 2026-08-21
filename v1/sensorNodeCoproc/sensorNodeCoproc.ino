/*
Author: PaskKat
Date: 8/16/2026
Board in Arduino IDE: Arduino Zero (Native USB)

Purpose: 
--> Establish serial connection via UART Serial 1 with ESP32 Radio and SAMD21 coprocessor
--> Initialize I2C connections to sensors and initialize sensors

Hardware:
--> Atmos Lab V3 board
--> Sensors used: BME680, Adafruit Stemma Soil Sensor
*/ 

#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>

// MODS
#include <Adafruit_seesaw.h>
#include "Adafruit_BME680.h"

// Other setup pinouts --------------------------------------

#define DEBUG_PORT SerialUSB
#define ESP_PORT Serial1

#define ESP_BAUD 9600

#define SOIL_I2C 0x36

#define BME_SCK 13
#define BME_MISO 12
#define BME_MOSI 11
#define BME_CS 10

// Reference values for sensor data processing.
#define SEALEVELPRESSURE_HPA (1013.25)

// Reference all appropriate fields.
#define wirePort Wire               // I2C Bus port name.
Adafruit_BME680 bme(&wirePort);     // I2C
Adafruit_seesaw ss;                 // Soil sensor.

// Packet definitions
enum messageType : uint8_t {
  DISCOVERY = 1,
  JOIN_REQUEST,
  TDMA_SCHEDULE,
  SENSOR_DATA,
  AGGREGATE_DATA
};

struct sensorDataPacket_t {
  uint8_t type;
  float temperature;
  float humidity;
  uint16_t soilMoisture;
  unsigned long timestamp;
};

// Helpers ----------------------------------------------------------------------

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

  // Get UART connecting coproc and esp32 online.
  ESP_PORT.begin(ESP_BAUD); // UART, coproc->esp32 and vice versa.
  while(!ESP_PORT);

  // initialize the sensors.
  Wire.begin();
  bme.begin();
  ss.begin(SOIL_I2C); 
  
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
    sensorDataPacket_t myData;
    myData.type = SENSOR_DATA;

    // NOTE: this assumes parsing on the other side will pick up string data sent in this format.
    ESP_PORT.println("SENSOR_DATA:");

    // get latest data.
    bme.performReading();
    myData.temperature = bme.temperature;
    myData.humidity = bme.humidity;
    myData.soilMoisture = ss.touchRead(0); 

    myData.timestamp = currentTime;
    ESP_PORT.println(myData.temperature);
    ESP_PORT.println(myData.humidity);
    ESP_PORT.println(myData.soilMoisture);
    ESP_PORT.println(myData.timestamp);
    getDataFlag = false;
  }
}
/*
Author: PaskKat
Date: 9/17/2026
Board in Arduino IDE: Arduino Zero (Native USB)

Purpose: 
--> Initialize I2C connections to sensors and initialize the wind and rainfall sensors.
--> Obtain voltage readings via I2C, successfully convert them to 

Hardware:
--> Atmos Lab V3 board, SAMD-21 microprocessor, EUREKA wind sensor custom board
--> Sensors used: SEN-15901 Wind Sensor

Notes:
--> (J1 port) Wind speed: Switch closes once per second at 1.492mi/hr
--> (J1 port) Wind direction: 16 different positions indicated by resistance levels. 
                    See SEN-15901 docs for 5V supply, 10K div resistance table.
--> (J4 port) Rainfall: Every 0.011in more rain on the sensor, one interrupt triggered.
              - Count the interrupts within 24 hour blocks for daily rainfall.
*/ 

#include <Wire.h>
#include <SPI.h>
#include <Adafruit_sensors.h>

void setup() {
  // put your setup code here, to run once:

}

void loop() {
  // put your main code here, to run repeatedly:

}

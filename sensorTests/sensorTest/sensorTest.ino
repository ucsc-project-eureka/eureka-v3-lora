/*
Author: Kpask
Date: 8/16/2026
Board in Arduino IDE: Arduino Zero (Native USB)

Purpose: 
--> Establish serial connection via UART Serial 1 with ESP32 Radio and SAMD21 coprocessor
--> Initialize I2C connections to sensors and initialize sensors
--> Print all sensor readings to debug serial to confirm function

Hardware:
--> Atmos Lab V3 board
--> Sensors used: BMV080, INA3221, BME680, u-blox GNSS
*/ 

#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>

// MODS
#include <Adafruit_seesaw.h>
#include <Adafruit_INA3221.h>
#include "Adafruit_BME680.h"
#include "SparkFun_u-blox_GNSS_Arduino_Library.h" // for M10S GPS interfacing.
#include "SdFat.h"
#include "wiring_private.h" // Necessary for pin peripheral multiplexing (from Sankie)
/*
          ^ Note: SoftwareSerial.h on arduino also allows this functionality 
for more expensive time/no simultaneous UART transmissions.*/ 

// SD Card Setup (From Sankie)
// ── Pin definitions (SAMD21) (SD) ──────────────────────────────
#define SD_MISO_PIN 22
#define SD_MOSI_PIN 23
#define SD_SCK_PIN  24
#define SD_CS_PIN   38

// ── Software SPI (SD) ──────────────────────────────────────────
// Note: in default Arduino library:
// set SPI_DRIVER_SELECT 2 in SdFatConfig.h to set as a software SPI.
SoftSpiDriver<SD_MISO_PIN, SD_MOSI_PIN, SD_SCK_PIN> softSpi;
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SD_SCK_MHZ(4), &softSpi)

// ── Globals ───────────────────────────────────────────────
SdCardFactory cardFactory;
SdCard* card = nullptr;
uint8_t sectorBuf[512];

// Other setup pinouts --------------------------------------
// Use DEBUG_PORT for the Native Port
#define DEBUG_PORT SerialUSB

SFE_UBLOX_GNSS myGNSS;
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
Adafruit_INA3221 ina3221;
Adafruit_seesaw ss;

// Pins/custom serial port for the ESP32 - Credit, Airwise team
// Create a new Serial instance for SERCOM0 -------------------------------------

#define SEND_PERIOD 5000 // amount of time between sensor test readings.

// Helpers ---------------------------------------------------

// Print functions - Sensors

// Read data from BME680. Prints all relevant data from the reading to DEBUG_PORT.
void serialPrintBMEData(void){
  DEBUG_PORT.println(F("BME680 test"));

  DEBUG_PORT.print("Temperature = ");
  DEBUG_PORT.print(bme.temperature);
  DEBUG_PORT.println(" *C");

  DEBUG_PORT.print("Pressure = ");
  DEBUG_PORT.print(bme.pressure / 100.0);
  DEBUG_PORT.println(" hPa");

  DEBUG_PORT.print("Humidity = ");
  DEBUG_PORT.print(bme.humidity);
  DEBUG_PORT.println(" %");

  DEBUG_PORT.print("Gas = ");
  DEBUG_PORT.print(bme.gas_resistance / 1000.0);
  DEBUG_PORT.println(" KOhms");

  DEBUG_PORT.print("Approx. Altitude = ");
  DEBUG_PORT.print(bme.readAltitude(SEALEVELPRESSURE_HPA));
  DEBUG_PORT.println(" m");

  DEBUG_PORT.println();
  return;
}

// Read data from INA3221. Print all relevant data from reading to Native USB.
void serialPrintINAData(void){
  DEBUG_PORT.println("\n--- Power Monitor Data ---");

  for (uint8_t i = 0; i < 3; i++) {
    float voltage_V = ina3221.getBusVoltage(i);
    float current_A = ina3221.getCurrentAmps(i);
    float current_mA = current_A * 1000.0;

    DEBUG_PORT.print("Channel ");
    DEBUG_PORT.print(i + 1); 
    DEBUG_PORT.print(": ");
    DEBUG_PORT.print(voltage_V, 3); 
    DEBUG_PORT.print(" V | ");
    DEBUG_PORT.print(current_mA, 2);
    DEBUG_PORT.println(" mA");
  }
}

// Check power monitoring.
void checkBme680(void){
  // Check initialization of every sensor.
  if (!bme.begin()) {
    DEBUG_PORT.println("Could not find a valid BME680 sensor, check wiring!");
    while (1);
  }
  else{
    // Set up oversampling and filter initialization for BME
    bme.setTemperatureOversampling(BME680_OS_8X);
    bme.setHumidityOversampling(BME680_OS_2X);
    bme.setPressureOversampling(BME680_OS_4X);
    bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
    bme.setGasHeater(320, 150); // 320*C for 150 ms
    if (! bme.performReading()) {
      DEBUG_PORT.println("Failed to perform reading :(");
      return;}
    else{
      serialPrintBMEData();
    }
  }
  return;
}

void checkIna3221(void){
    // 1. Set PA17 to LOW immediately via direct register access
    // Ensure the pin is an output
    PORT->Group[0].DIRSET.reg = PORT_PA17; 
    // Clear the pin (Set to LOW)
    PORT->Group[0].OUTSET.reg = PORT_PA17;

    DEBUG_PORT.begin(115200);

    DEBUG_PORT.println("INA3221 port found and initialized!");
    serialPrintINAData();
}

// Checking GPS sensor activation and communication
void checkM10S(void){
  DEBUG_PORT.println("\nM10S GPS Test:\n");
  if (myGNSS.begin() == false){
    DEBUG_PORT.println("u-blox GNSS module not detected at default I2C address. Please check wiring. Freezing.");
    while (1);
  }

  //This will pipe all NMEA sentences to the serial port so we can see them
  DEBUG_PORT.println("u-blox GNSS module detected at default I2C address.");
  myGNSS.setI2COutput(COM_TYPE_NMEA);
  myGNSS.setNMEAOutputPort(DEBUG_PORT);
  return;
}

// Check SD access.
void checkSD(void){
  DEBUG_PORT.println("=== FAT32 Formatter ===");

  card = cardFactory.newCard(SD_CONFIG);
  if (!card || card->errorCode()) {
    DEBUG_PORT.println("Card init failed. Check wiring.");
    return;
  }
  DEBUG_PORT.println("Card detected.");

  uint32_t sectors = card->sectorCount();
  if (sectors == 0) {
    DEBUG_PORT.println("Could not read card sector count. Aborting.");
    return;
  }

  uint32_t sizeMB = sectors / 2048;
  DEBUG_PORT.print("Card size: ");
  DEBUG_PORT.print(sizeMB);
  DEBUG_PORT.println(" MB");

  if (sizeMB > 32768) {
    DEBUG_PORT.println("Card >32GB, aborting. This sketch is FAT32 only.");
    return;
  }

  DEBUG_PORT.println("Formatting as FAT32... (may take up to 60 seconds)");
  FatFormatter formatter;
  if (!formatter.format(card, sectorBuf, &DEBUG_PORT)) {
    DEBUG_PORT.println("Format FAILED.");
    return;
  }

  DEBUG_PORT.println("=== Format complete! Flash your main sketch now. ===");
  return;
}

void checkSoilSensor(void){
  DEBUG_PORT.println("seesaw Soil Sensor check:");
  if (!ss.begin(SOIL_I2C)) {
    DEBUG_PORT.println("ERROR! seesaw not found");
    // while(1) delay(1);
  } else {
    DEBUG_PORT.print("seesaw started! version: ");
    DEBUG_PORT.println(ss.getVersion(), HEX);
    float tempC = ss.getTemp();
    // Note: Capactative "touch" is the moisture level detected.
    uint16_t capread = ss.touchRead(0);
    
    DEBUG_PORT.print("Temperature: "); DEBUG_PORT.print(tempC); DEBUG_PORT.println("*C");
    DEBUG_PORT.print("Capacitive: "); DEBUG_PORT.println(capread);
  }
  return;
}

// Report device address over I2C.
void scanI2CWire(void){
  DEBUG_PORT.println("Scanning I2C bus...");
  for (byte address = 1; address < 127; address++) {
    wirePort.beginTransmission(address);
    byte error = wirePort.endTransmission();
    if (error == 0) {
      DEBUG_PORT.print("Device found at address 0x");
      if (address < 16) DEBUG_PORT.print("0");
      DEBUG_PORT.println(address, HEX);
    }
  }
  DEBUG_PORT.println("Done.");
  return;
}

// MAIN ------------------------------------------------------------------

uint32_t lastSendData = 0;
uint32_t currentTime = millis();
void setup(){

  // turn on the radio from the coproc
  PORT->Group[0].DIRSET.reg = PORT_PA17;
  PORT->Group[0].OUTSET.reg = PORT_PA17;
  // turn on sensor I2C
  PORT->Group[0].DIRSET.reg = PORT_PA05;
  PORT->Group[0].OUTSET.reg = PORT_PA05;

  Wire.begin();
  ina3221.begin();
  bme.begin();
  
  // ss.begin(SOIL_I2C);
  DEBUG_PORT.begin(115200);
  while(!DEBUG_PORT);
  DEBUG_PORT.println("Debug serial initialized!");
  DEBUG_PORT.println();
  DEBUG_PORT.print("Sending test data every ");
  DEBUG_PORT.print(SEND_PERIOD);
  DEBUG_PORT.println(" ms.");

  // Device at 0x40, 0x42, 0x77
  // Check initialization of every sensor relevant to EUREKA project.
  checkBme680();
  checkIna3221();

  checkM10S();
  checkSoilSensor(); // Note: I2C address not specified for soil sensor yet, this will be errored.
}

void loop(){
  // Print out data from sensors every five seconds.
  currentTime = millis();
  if(currentTime - lastSendData >= SEND_PERIOD){
    // update the time stamp.
    lastSendData = currentTime;
    
    // get sensor readings.
    bme.performReading();


    // print check.
    DEBUG_PORT.println("Temperature = ");
    DEBUG_PORT.print(bme.temperature);
    DEBUG_PORT.println(" *C");

    DEBUG_PORT.println("Humidity = ");
    DEBUG_PORT.print(bme.humidity);
    DEBUG_PORT.println(" %");

    // note: during some tests soil sensors were not connected. 

    // Capacitive reading is the one for overall soil moisture.
    // dummy value holds.
    // uint16_t moisture = ss.touchRead(0);
    uint16_t moisture = 500; // dummy value.

    DEBUG_PORT.print("Soil Moisture = ");
    DEBUG_PORT.println(moisture);

    DEBUG_PORT.println("");
    DEBUG_PORT.print("Timestamp = ");
    DEBUG_PORT.print(currentTime);
    DEBUG_PORT.println("");
  }
}
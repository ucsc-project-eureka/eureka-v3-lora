/*
Author: PaskKat
Date: 8/24/2026
Board in Arduino IDE: ESP32 S3 Dev Module

Purpose: 
--> Send testing pings via LoRa and RadioLib to test signal range

Hardware:
--> Atmos Lab V3 board, or ESP32-SX1262
--> Sensors used: N/A, only using the radio module.
*/ 

// include the library
#include <RadioLib.h>

#define DEBUG_PORT Serial

#define RADIO_INIT_TIMEOUT 1000
#define TX_TIMEOUT 500

// Heltec V3 Pin Mappings
#define LORA_NSS   8
#define LORA_DIO1  14
#define LORA_NRST  12
#define LORA_BUSY  13

// SPI connection pins from ESP32-v3 to SX1262
#define LORA_SCK   9
#define LORA_MISO  11
#define LORA_MOSI  10

// Defs --------------------------------------------------------------
enum{
  TEST_DATA,
  BEACON,
  SENSOR_DATA,
  AGG_DATA
};

struct testPacket{
  uint32_t type = TEST_DATA;
  uint32_t roundCount = 0;
};

SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_NRST, LORA_BUSY);

// Helpers ------------------------------------------------------------------

void initializeRadio(void){
  // turn on power circuit pin GPIO 36. Wait for 50 milliseconds to stabilize voltage.
  pinMode(36, OUTPUT);
  digitalWrite(36, LOW);
  int startTime = millis();
  while(millis() - startTime <= RADIO_INIT_TIMEOUT);

  // open SPI connection between ESP32-v3 Heltec microcontroller and SX1262 IC.
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);

  // initialize SX1262 at 915 MHz
  DEBUG_PORT.println("[SX1262] Initializing ... ");
  int state = radio.begin(915.0, 125.0, 9, 7, 0x12, 10, 8, 1.6);
  DEBUG_PORT.println("Initialized to 434 Mhz!");
}

// MAIN ---------------------------------------------------------------------

// Globals
testPacket myPacket;
uint32_t lastSendTime;

void setup() {
  DEBUG_PORT.begin(115200);
  while(!DEBUG_PORT);
  initializeRadio();
  lastSendTime = millis();
}

void loop(){
  // transmit every TX_TIMEOUT
  if (millis() - lastSendTime > TX_TIMEOUT){
    myPacket.roundCount++;
    DEBUG_PORT.printf("\n[SX1262] Sending TEST round: %lu", myPacket.roundCount);
    // using blocking transmit function, ie. it will execute and cleanup (and will not proceed until finished)
    radio.transmit((uint8_t*)&myPacket,sizeof(testPacket));
    lastSendTime = millis();
  }
}
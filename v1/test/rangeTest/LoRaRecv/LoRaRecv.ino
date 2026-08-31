/*
Author: PaskKat
Date: 8/24/2026
Board in Arduino IDE: ESP32 S3 Dev Module

Purpose: 
--> Receive, read, and repeat testing pings via LoRa and RadioLib to test signal range

Hardware:
--> Atmos Lab V3 board, or ESP32-SX1262
--> Sensors used: N/A, only using the radio module.
*/ 

// include the library
#include <RadioLib.h>

#define DEBUG_PORT Serial

#define RADIO_INIT_TIMEOUT 1000

// Heltec V3 Pin Mappings
#define LORA_NSS   8
#define LORA_DIO1  14
#define LORA_NRST  12
#define LORA_BUSY  13

// SPI connection pins from ESP32-v3 to SX1262
#define LORA_SCK   9
#define LORA_MISO  11
#define LORA_MOSI  10

#define TEST_TIMEOUT 65000 // timeout taking into account transmit delays

// Defs --------------------------------------------------------------
enum{
  TEST_DATA,
  BEACON,
  SENSOR_DATA,
  AGG_DATA
};

struct testPacket{
  uint8_t type = TEST_DATA;
  unsigned long roundCount = 0;
};

// Globals
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_NRST, LORA_BUSY);
volatile bool receivedFlag = false;
int numRecv = 0;
int avgRSSI;
float PDR;
int RSSIList[100];
float RSSISum;
unsigned long lastTime = millis();

// Helpers ------------------------------------------------------------------

// Received packet interrupt
void IRAM_ATTR onDataRecv(){
  receivedFlag = true;
}

// Initialize and start up SX1262 Heltec ESP32 v3 at 434MHz.
void initializeRadio(void){
  // turn on power circuit pin GPIO 36. Wait for 50 milliseconds to stabilize voltage.
  pinMode(36, OUTPUT);
  digitalWrite(36, LOW);
  int startTime = millis();
  while(millis() - startTime <= RADIO_INIT_TIMEOUT);

  // open SPI connection between ESP32-v3 Heltec microcontroller and SX1262 IC.
  SPI.begin(LORA_SCK,LORA_MISO, LORA_MOSI, LORA_NSS);

  // initialize SX1262 at 434 MHz
  DEBUG_PORT.println("[SX1262] Initializing ... ");
  int state = radio.begin(915.0, 125.0, 9, 7, 0x12, 10, 8, 1.6);
  DEBUG_PORT.printf("\nradio state: %d\n",state);
  DEBUG_PORT.println("Initialized to 915 Mhz!");
}

// Given the received buffer, determine the type of packet based off first 4 bytes.
uint8_t getPacketType(uint8_t recvPacket[]){
  return recvPacket[0];
}
// MAIN ---------------------------------------------------------------------

void setup() {
  DEBUG_PORT.begin(115200);
  while(!DEBUG_PORT);

  initializeRadio();
  // define and set interrupt to RX pins
  radio.setPacketReceivedAction(onDataRecv);
  // start listening.
  radio.startReceive();
  lastTime = millis();
}

void loop() {
  if(millis()-lastTime){

  }
  // check if the flag is set
  if(receivedFlag) {
    // reset flag
    receivedFlag = false;
    byte byteArr[255];
    int state = radio.readData(byteArr, 0);
    int numBytes = radio.getPacketLength();
    
    if (state == RADIOLIB_ERR_NONE) {
      // packet was successfully received
      uint32_t packetType = getPacketType(byteArr);

      if (packetType == TEST_DATA && (millis() - lastTime <= TEST_TIMEOUT)){
        testPacket* recvPacket = (testPacket*) byteArr;
        RSSIList[numRecv] = radio.getRSSI();
        numRecv ++;
        DEBUG_PORT.printf("\n[SX1262] Received test packet: %lu\n",recvPacket->roundCount);
        // // print RSSI (Received Signal Strength Indicator)
        // DEBUG_PORT.print(F("[SX1262] RSSI:\t\t"));
        // DEBUG_PORT.print(radio.getRSSI());
        // DEBUG_PORT.println(F(" dBm"));

        // // print SNR (Signal-to-Noise Ratio)
        // DEBUG_PORT.print(F("[SX1262] SNR:\t\t"));
        // DEBUG_PORT.print(radio.getSNR());
        // DEBUG_PORT.println(F(" dB"));

        // // print frequency error
        // DEBUG_PORT.print(F("[SX1262] Frequency error:\t"));
        // DEBUG_PORT.print(radio.getFrequencyError());
        // DEBUG_PORT.println(F(" Hz"));
        }
      else if(packetType == TEST_DATA && (millis() - lastTime > TEST_TIMEOUT)){
        PDR = 0;
        RSSISum = 0;
        avgRSSI = 0;
        RSSISum = 0;
        for (int i = 0; i<numRecv;i++){
          RSSISum += RSSIList[i];
        }
        avgRSSI = RSSISum/numRecv;
        PDR = numRecv;
        DEBUG_PORT.printf("\nAverage RSSI: %f", avgRSSI);
        DEBUG_PORT.printf("\nPDR: %f",PDR);
        numRecv = 0;
        delay(5000);
        lastTime = millis();
      }
      else{DEBUG_PORT.println("Packet received, but unable to identify");}
      }
    // stay in recv mode.
    radio.startReceive();
  }
}

/*
Author: PaskKat
Date: 8/27/2026
Board in Arduino IDE: ESP32 S3 Dev Module

Purpose: 
--> Receive give-data commands and query sensor node coproc for sensor data.
--> Transmit back to the parent the data it collected.

Hardware:
--> Atmos Lab V3 board, or Heltec v3 ESP32-SX1262
--> Sensors used: N/A, this is the source code for the radio microcontroller.
*/ 

// include the library
#include <RadioLib.h>

// Heltec V3 Pin Mappings
#define LORA_NSS   8
#define LORA_DIO1  14
#define LORA_NRST  12
#define LORA_BUSY  13

// SPI connection pins from ESP32-v3 to SX1262
#define LORA_SCK   9
#define LORA_MISO  11
#define LORA_MOSI  10

#define DEBUG_PORT Serial
#define COPROC_PORT Serial1
// From Airwise's ESP32 UART connections.
#define ESP_TX_PIN 43
#define ESP_RX_PIN 44
#define ESP_BAUD 9600

#define RADIO_INIT_TIMEOUT 1000
#define BEACON_TIMEOUT 10000      // For dev, initialized to every 10 seconds

#define PUBLIC_CHANNEL 0
#define SINK_CHANNEL 1
#define MAX_SENSOR_NODES 6
#define MAX_RANDOM 50

// Defs --------------------------------------------------------------

// Packet types
enum{
  BEACON,
  SENSOR_DATA,
  AGG_DATA
};

// state names
enum{
  RECEIVE,
  WAIT,
  GET_DATA,
  TX_DATA
};

// Channel names.
const float CHANNEL_FREQ[] = {
    903.0,   // 0 - public discovery
    904.0,   // 1 - sink channel
    905.0,   // 2 - free
    906.0,   // 3 - free
    907.0,   // 4 - free
    908.0,   // 5 - free
    909.0,   // 6 - free
    910.0    // 7 - free
};

// Packet types
struct beaconPacket_t{
  uint8_t type = BEACON;
  unsigned long hopCount;
  unsigned long roundCount;
  float privateChannel;
  float parentChannel;
};

struct dataPacket_t{
  uint8_t type = SENSOR_DATA;
  float temperature;
  float humidity;
  uint16_t soilMoisture;
  unsigned long timestamp;
};

struct aggPacket_t{
  uint8_t type = AGG_DATA;
  float temperatures[MAX_SENSOR_NODES];
  float humidities[MAX_SENSOR_NODES];
  uint16_t soilMoistures[MAX_SENSOR_NODES];
  unsigned long timestamps[MAX_SENSOR_NODES];
  uint8_t readingsCount;
};

// Globals
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_NRST, LORA_BUSY);
unsigned long lastSendTime;
float myChannel = CHANNEL_FREQ[PUBLIC_CHANNEL];

uint8_t state;
volatile bool recvFlag;
uint8_t packetType;
beaconPacket_t recvBeaconPacket;
dataPacket_t recvDataPacket;
aggPacket_t recvAggPacket;

dataPacket_t dataPacket;

unsigned long sinkRoundCount = 0;
unsigned long beaconTime;
unsigned long jitter;
bool sentPacket = false;

// Helpers ------------------------------------------------------------------

// Received packet interrupt
void IRAM_ATTR onDataRecv(){
  recvFlag = true;
}

// Initialize and start up SX1262 Heltec ESP32 v3 at public channel MHz.
void initializeRadio(void){
  // turn on power circuit pin GPIO 36. Wait for 50 milliseconds to stabilize voltage.
  pinMode(36, OUTPUT);
  digitalWrite(36, LOW);
  int startTime = millis();
  while(millis() - startTime <= RADIO_INIT_TIMEOUT);

  // open SPI connection between ESP32-v3 Heltec microcontroller and SX1262 IC.
  SPI.begin(LORA_SCK,LORA_MISO, LORA_MOSI, LORA_NSS);

  // initialize SX1262 at public channel
  DEBUG_PORT.println("[SX1262] Initializing ... ");
  int state = radio.begin(CHANNEL_FREQ[PUBLIC_CHANNEL], 125.0, 9, 7, 0x12, 10, 8, 1.6);
  DEBUG_PORT.println("Initialized to public channel!");
  return;
}

// Given the received buffer, determine the type of packet based off first 4 bytes.
uint8_t getPacketType(uint8_t recvPacket[]){
  uint8_t pktType;
  memcpy(&pktType,&recvPacket[0],sizeof(uint8_t));
  return pktType;
}

void handleRecvPacket(void){
  byte byteArr[255];
  int state = radio.readData(byteArr, 0);
  int numBytes = radio.getPacketLength();
  if (state == RADIOLIB_ERR_NONE){
    // packet was successfully received
    packetType = getPacketType(byteArr);
  }
  if (packetType == BEACON){
    memcpy(&recvBeaconPacket, byteArr, sizeof(beaconPacket_t));
  }
  return;
}

void getDataFromCoproc(void){
  // Trigger the coproc to send sensor data to ESP32.
  COPROC_PORT.println("SENSOR_DATA");
  // Wait a period to recieve data back. Wait for coproc to respond.
  while(!(COPROC_PORT.available()));
  if (COPROC_PORT.available() && !sentPacket) {
    String header = COPROC_PORT.readStringUntil('\n');
    header.trim();
    if (header == "SENSOR_DATA:") {
      // Get data from printline serial from coproc.
      dataPacket.type         = SENSOR_DATA;
      dataPacket.temperature  = COPROC_PORT.readStringUntil('\n').toFloat();
      dataPacket.humidity     = COPROC_PORT.readStringUntil('\n').toFloat();
      dataPacket.soilMoisture = COPROC_PORT.readStringUntil('\n').toInt();
      dataPacket.timestamp    = COPROC_PORT.readStringUntil('\n').toInt();
            
      DEBUG_PORT.println("Received data from coproc!");
      DEBUG_PORT.println("");
    }
  }
}

// MAIN --------------------------------------------------------------------------

void setup() {
  DEBUG_PORT.begin(115200);
  while(!DEBUG_PORT);
  COPROC_PORT.begin(ESP_BAUD, SERIAL_8N1, ESP_RX_PIN, ESP_TX_PIN);
  while(!COPROC_PORT);
  initializeRadio();                          // Initialized on public frequency.
  radio.setPacketReceivedAction(onDataRecv);
  radio.startReceive();
  state = RECEIVE;
}

void loop() {
  switch(state){
    case RECEIVE:{
      if (recvFlag){
        recvFlag = false;
        handleRecvPacket();
        if((packetType == BEACON) && (recvBeaconPacket.hopCount>0)){
          beaconTime = millis();
          myChannel = recvBeaconPacket.privateChannel;
          radio.setFrequency(myChannel);
          jitter = random(0, MAX_RANDOM);
          state = WAIT;
        }
        else{
        radio.startReceive(); // restart listening if interrupt blocks it.
        }
      }
      break;
    }
    case WAIT:{
      if (millis()-beaconTime > jitter){
        state = GET_DATA;
      }
      break;
    }
    case GET_DATA:{
      getDataFromCoproc();
      state = TX_DATA;
      break;
    }
    case TX_DATA:{
      radio.transmit((uint8_t*)&dataPacket,sizeof(dataPacket_t));
      myChannel = CHANNEL_FREQ[PUBLIC_CHANNEL];
      radio.setFrequency(myChannel);
      radio.startReceive();
      state = RECEIVE;
      break;
    }
  }
}


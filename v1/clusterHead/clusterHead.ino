/*
Author: PaskKat
Date: 8/28/2026
Board in Arduino IDE: ESP32 S3 Dev Module

Purpose: 
--> Receives beacons and retransmits with an updated hopcount
--> Receives data packets from sensor nodes and stitch it into aggregate packets
--> transmits aggregate packets back to the sink and other cluster heads via a broadcast.

Hardware:
--> Atmos Lab V3 board, or Heltec v3 ESP32-SX1262
--> Sensors used: N/A, this is the source code for the radio.
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

#define RADIO_INIT_TIMEOUT 1000
#define BEACON_TIMEOUT 10000      // For dev, initialized to every 10 seconds

#define PUBLIC_CHANNEL 0
#define SINK_CHANNEL 1
#define MAX_SENSOR_NODES 6
#define MAX_RANDOM 50
#define MAX_CLUSTER_HEADS 3
#define TIME_PER_CLUSTER_HEAD 500

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
  TX_BEACON,
  TX_AGG_DATA
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

struct dataPacket_t {
  uint8_t type;
  float temperature;
  float humidity;
  uint16_t soilMoisture;
  // float windDirection;
  // float windSpeed;
  // float rainfall;
  unsigned long timestamp;
};

struct aggPacket_t{
  uint8_t type = AGG_DATA;
  float temperatures[MAX_SENSOR_NODES];
  float humidities[MAX_SENSOR_NODES];
  uint16_t soilMoistures[MAX_SENSOR_NODES];
  // float windDirections[MAX_SENSOR_NODES];
  // float windSpeeds[MAX_SENSOR_NODES];
  // float rainfalls[MAX_SENSOR_NODES];
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

unsigned long startTime;
unsigned long reportTimeout;

beaconPacket_t myBeaconPacket;
dataPacket_t myDataPacket;
aggPacket_t myAggPacket;
float parentFrequency;
float randomChannel;
unsigned long level;
int numPacketsRecv = 0;

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
  switch(packetType){
    case BEACON:{
      memcpy(&recvBeaconPacket, byteArr, sizeof(beaconPacket_t));
      break;
    }
    case SENSOR_DATA:{
      memcpy(&recvDataPacket, byteArr, sizeof(dataPacket_t));
      break;
    }
    case AGG_DATA:{
      memcpy(&recvAggPacket, byteArr, sizeof(aggPacket_t));
      break;
    }
  }
}

float getRandomChannel(void){
  float chosenChannel = CHANNEL_FREQ[random(2,7)];
  if(chosenChannel == parentFrequency){
    while(chosenChannel == parentFrequency){
      chosenChannel = CHANNEL_FREQ[random(2,7)];
    }
  }
  return chosenChannel;
}

// MAIN --------------------------------------------------------------------------

void setup(){
  DEBUG_PORT.begin(115200);
  while(!DEBUG_PORT);
  initializeRadio();                          // Initialized on public frequency.
  radio.setPacketReceivedAction(onDataRecv);
  radio.startReceive();
  state = RECEIVE;
}

void loop(){
  switch(state){
    case RECEIVE:{
      if(recvFlag){
        switch(packetType){
          case BEACON:{
            memcpy(&myBeaconPacket, &recvBeaconPacket, sizeof(beaconPacket_t));
            myBeaconPacket.hopCount = recvBeaconPacket.hopCount++;
            parentFrequency = recvBeaconPacket.parentChannel;
            randomChannel = getRandomChannel();
            myBeaconPacket.parentChannel = recvBeaconPacket.privateChannel;
            myBeaconPacket.privateChannel = randomChannel;
            level = myBeaconPacket.hopCount;
            reportTimeout = (MAX_CLUSTER_HEADS - level + 1)*TIME_PER_CLUSTER_HEAD;
            startTime = millis();
            state = TX_BEACON;
            break;
          }
          case SENSOR_DATA:{
            myAggPacket.temperatures[numPacketsRecv] = recvDataPacket.temperature;
            myAggPacket.humidities[numPacketsRecv] = recvDataPacket.humidity;
            myAggPacket.soilMoistures[numPacketsRecv] = recvDataPacket.soilMoisture;
            // myAggPacket.windDirections[numPacketsRecv] = recvDataPacket.windDirection;
            // myAggPacket.windSpeeds[numPacketsRecv] = recvDataPacket.windSpeed;
            // myAggPacket.rainfalls[numPacketsRecv] = recvDataPacket.rainfall;
            myAggPacket.timestamps[numPacketsRecv] = recvDataPacket.timestamp;
            myAggPacket.readingsCount++;
            numPacketsRecv ++;
            radio.startReceive();
            state = RECEIVE;
            break;
          }
          case AGG_DATA:{
            for(int i = 0; i<recvAggPacket.readingsCount;i++){
              if(myAggPacket.readingsCount + 1 <= MAX_SENSOR_NODES){
                // append another packet.
                myAggPacket.temperatures[numPacketsRecv] = recvAggPacket.temperatures[i];
                myAggPacket.humidities[numPacketsRecv] = recvAggPacket.humidities[i];
                myAggPacket.soilMoistures[numPacketsRecv] = recvAggPacket.soilMoistures[i];
                // myAggPacket.windDirections[numPacketsRecv] = recvAggPacket.windDirections[i];
                // myAggPacket.windSpeeds[numPacketsRecv] = recvAggPacket.windSpeeds[i];
                // myAggPacket.rainfalls[numPacketsRecv] = recvAggPacket.rainfalls[i];
                myAggPacket.timestamps[numPacketsRecv] = recvAggPacket.timestamps[i];
                myAggPacket.readingsCount++;
                numPacketsRecv ++;
              }
            }
            break;
          }
        }
      }
      else if(millis() - startTime > reportTimeout){
        myChannel = parentFrequency;
        radio.setFrequency(myChannel);
        state = TX_AGG_DATA;
      }
      break;
    }
    case TX_BEACON:{
      radio.transmit((uint8_t*)&myBeaconPacket,sizeof(beaconPacket_t));
      myChannel = randomChannel;
      radio.setFrequency(myChannel);
      radio.startReceive();
      state = RECEIVE;
      break;
    }
    case TX_AGG_DATA:{
      radio.transmit((uint8_t*)&myAggPacket,sizeof(aggPacket_t));
      // refresh the slate.
      numPacketsRecv = 0;
      memset(&myAggPacket,0,sizeof(aggPacket_t));
      // restart.
      myChannel = CHANNEL_FREQ[PUBLIC_CHANNEL];
      radio.setFrequency(myChannel);
      radio.startReceive();
      state = RECEIVE;
      break;
    }
  }
}

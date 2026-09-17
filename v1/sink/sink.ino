/*
Author: PaskKat
Date: 8/28/2026
Board in Arduino IDE: ESP32 S3 Dev Module

Purpose: 
--> Receive and upload aggregate data packets to server.
--> Transmit a beacon for a determined amount of wait time to initiate data aggregation.
--> Upload received aggregate data to the backend via MQTT (WiFi).

Hardware:
--> Atmos Lab V3 board, or Heltec v3 ESP32-SX1262
--> Sensors used: N/A, only using the radio module.
*/ 

#include <RadioLib.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "config.h"

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
#define BEACON_TIMEOUT 20000      // For dev, initialized to every 20 seconds

#define PUBLIC_CHANNEL 0
#define SINK_CHANNEL 1
#define MAX_SENSOR_NODES 3

// Defs --------------------------------------------------------------

// Packet types
enum{
  BEACON,
  SENSOR_DATA,
  AGG_DATA
};

// state names
enum{
  TX_BEACON,
  RECEIVE
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
struct __attribute__((packed)) beaconPacket_t{
  uint8_t type = BEACON;
  unsigned long hopCount;
  unsigned long roundCount;
  float privateChannel;
  float parentChannel;
};

struct __attribute__((packed)) dataPacket_t{
  uint8_t type;
  float temperature;
  float humidity;
  uint16_t soilMoisture;
  // float windDirection;
  // float windSpeed;
  // float rainfall;
  unsigned long timestamp;
};

struct __attribute__((packed)) aggPacket_t{
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

unsigned long sinkRoundCount = 0;

// Network globals
WiFiClient netClient;
PubSubClient mqttClient(netClient);
unsigned long lastNetAttemptMs = 0;
unsigned long recordSeq = 0;

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
  return recvPacket[0];
}

void handleRecvPacket(void){
  DEBUG_PORT.println("Processing packet!");
  byte byteArr[255];
  int state = radio.readData(byteArr, 0);
  int numBytes = radio.getPacketLength();
  DEBUG_PORT.println("data has been read");
  if (state == RADIOLIB_ERR_NONE){
    // packet was successfully received
    DEBUG_PORT.println("packet successfully received");
    packetType = getPacketType(byteArr);
    DEBUG_PORT.printf("\nget packetType finished, packetType: %d\n",packetType);
  }
  if (packetType == AGG_DATA){
    DEBUG_PORT.println("Found packetType to be AGG_DATA");
    memcpy(&recvAggPacket,byteArr, sizeof(aggPacket_t));
  }
  return;
}

// Non-blockingly bring WiFi up (and back up) without stalling the LoRa loop.
void ensureWiFi(void){
  if (WiFi.status() != WL_CONNECTED){
    if (millis() - lastNetAttemptMs > MQTT_RECONNECT_MS){
      lastNetAttemptMs = millis();
      DEBUG_PORT.print("[WIFI] Connecting to ");
      DEBUG_PORT.println(WIFI_SSID);
      WiFi.mode(WIFI_STA);
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  }
}

// Non-blockingly connect (and reconnect) the MQTT client on top of WiFi.
void ensureMqtt(void){
  if (WiFi.status() == WL_CONNECTED && !mqttClient.connected()){
    if (millis() - lastNetAttemptMs > MQTT_RECONNECT_MS){
      lastNetAttemptMs = millis();
      char clientId[24];
      snprintf(clientId, sizeof(clientId), "eurekasink%lu", (unsigned long)random(1000, 9999));
      DEBUG_PORT.print("[MQTT] Connecting to ");
      DEBUG_PORT.println(MQTT_BROKER_HOST);
      if (mqttClient.connect(clientId, MQTT_USERNAME, MQTT_PASSWORD)){
        DEBUG_PORT.println("[MQTT] Connected.");
      } else {
        DEBUG_PORT.printf("[MQTT] Connect failed, rc=%d\n", mqttClient.state());
      }
    }
  }
}

// Upload the aggregate readings to the backend as JSON MQTT messages, one per
// sensor node.
void publishAggDataToServer(void){
  if (!mqttClient.connected()){
    DEBUG_PORT.println("[MQTT] Not connected, dropping AGG_DATA");
    return;
  }

  for (int i = 0; i < recvAggPacket.readingsCount; i++){
    unsigned long nodeId = (i < NUM_SENSOR_IDS) ? SENSOR_NODE_IDS[i] : (unsigned long)(i + 1);

    char topic[64];
    snprintf(topic, sizeof(topic), "%s/%lu", MQTT_BASE_TOPIC, nodeId);

    char json[256];
    snprintf(
      json, sizeof(json),
      "{\"protocol\":\"eureka\",\"message_type\":\"sensor_reading\","
      "\"sink_id\":%lu,\"node_id\":%lu,\"record_id\":%lu,"
      "\"sensor_epoch\":%lu,\"sink_received_millis\":%lu,"
      "\"temperature\":%.2f,\"humidity\":%.2f,\"soil_moisture\":%u}",
      SINK_NODE_ID, nodeId, recordSeq,
      recvAggPacket.timestamps[i], (unsigned long)millis(),
      recvAggPacket.temperatures[i], recvAggPacket.humidities[i],
      recvAggPacket.soilMoistures[i]);

    bool ok = mqttClient.publish(topic, json);
    DEBUG_PORT.printf("[MQTT] %s -> %u bytes: %s\n", topic, (unsigned)strlen(json), ok ? "ok" : "FAILED");
    recordSeq++;
  }
}

// MAIN --------------------------------------------------------------------------

void setup() {
  DEBUG_PORT.begin(115200);
  while(!DEBUG_PORT);
  initializeRadio();                          // Initialized on public frequency.
  radio.setPacketReceivedAction(onDataRecv);
  mqttClient.setServer(MQTT_BROKER_HOST, MQTT_BROKER_PORT);
  mqttClient.setBufferSize(512);
  state = TX_BEACON;
}

void loop(){
  ensureWiFi();
  ensureMqtt();
  mqttClient.loop();

  switch(state){
    // transmitting state
    case TX_BEACON:{
      beaconPacket_t beacon{
        .type = BEACON,
        .hopCount = 0, 
        .roundCount = sinkRoundCount++,
        .privateChannel = CHANNEL_FREQ[SINK_CHANNEL],
        .parentChannel = CHANNEL_FREQ[PUBLIC_CHANNEL]};
      radio.transmit((uint8_t*)&beacon,sizeof(beaconPacket_t));
      recvFlag = false; // interrupt gets pulled whenever TX finished.
      lastSendTime = millis();
      DEBUG_PORT.println("sent BEACON!");
      // set radio to sink channel.
      myChannel = beacon.privateChannel;
      radio.setFrequency(myChannel);
      radio.startReceive();
      state = RECEIVE;
      break;
    }
    // receiving state
    case RECEIVE:{
      if(millis() - lastSendTime > BEACON_TIMEOUT){
        myChannel = CHANNEL_FREQ[PUBLIC_CHANNEL];
        radio.setFrequency(myChannel);
        state = TX_BEACON;
      }
      else if(recvFlag){
        DEBUG_PORT.println("Got packet!");
        recvFlag = false;
        handleRecvPacket();
        if(packetType == AGG_DATA){
            DEBUG_PORT.println("it's an AGG_DATA!");
            // interrupt already saved recvAggData
            for (int i = 0; i<recvAggPacket.readingsCount;i++){
              DEBUG_PORT.printf("\nReading for Sensor Node %d\n",i);
              DEBUG_PORT.println("Temperature: ");
              DEBUG_PORT.println(recvAggPacket.temperatures[i]);
              DEBUG_PORT.println("Humidity: ");
              DEBUG_PORT.println(recvAggPacket.humidities[i]);
              DEBUG_PORT.println("Soil Moisture: ");
              DEBUG_PORT.println(recvAggPacket.soilMoistures[i]);
              // DEBUG_PORT.println("Wind Direction: ");
              // DEBUG_PORT.println(recvAggPacket.windDirections[i]);
              // DEBUG_PORT.println("Wind Speed: ");
              // DEBUG_PORT.println(recvAggPacket.windSpeeds[i]);
              // DEBUG_PORT.println("Rainfall: ");
              // DEBUG_PORT.println(recvAggPacket.rainfalls[i]);
              DEBUG_PORT.println("Time Stamp: ");
              DEBUG_PORT.println(recvAggPacket.timestamps[i]);
            }
            // Upload the aggregate to the backend over MQTT.
            publishAggDataToServer();
        }
        // set/stay in receive mode.
        radio.startReceive();
      }
      break;
    }
  }
}

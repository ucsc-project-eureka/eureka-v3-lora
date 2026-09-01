/*
Author: PaskKat
Date: 8/29/2026
Board in Arduino IDE: ESP32 s3 Dev Module (Tools > CDC on boot > Enabled)

Purpose: 
--> Establish serial connection via UART Serial 1 with ESP32 Radio and SAMD21 coprocessor
--> Send simple ping-pong messages between the two and have them process each to verify connection.

Hardware:
--> Atmos Lab V3 board
--> Sensors used: N/A, this is basic board testing.
*/ 

#define DEBUG_PORT Serial
#define COPROC_PORT Serial1
#define ESP_BAUD 9600
#define ESP_RX_PIN 44
#define ESP_TX_PIN 43

void setup() {
  DEBUG_PORT.begin(115200);
  while(!DEBUG_PORT);
  COPROC_PORT.begin(ESP_BAUD, SERIAL_8N1, ESP_RX_PIN, ESP_TX_PIN);
  while(!COPROC_PORT);
  DEBUG_PORT.println("ESP32 can read serials!");
  }

void loop() {
  if (COPROC_PORT.available()){
    if ((COPROC_PORT.readStringUntil('\n')) == "Hello world!\r"){
      DEBUG_PORT.println("got hello world from coproc, replying.");
      COPROC_PORT.println("why hello there from ESP32!");
    }
  }
}

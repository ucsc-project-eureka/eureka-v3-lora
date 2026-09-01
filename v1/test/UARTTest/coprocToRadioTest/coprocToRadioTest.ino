/*
Author: PaskKat
Date: 8/29/2026
Board in Arduino IDE: Arduino Zero (Native USB)

Purpose: 
--> Establish serial connection via UART Serial 1 with ESP32 Radio and SAMD21 coprocessor
--> Send simple ping-pong messages between the two and have them process each to verify connection.

Hardware:
--> Atmos Lab V3 board
--> Sensors used: N/A, this is basic board testing.
*/ 

#define DEBUG_PORT SerialUSB
#define ESP_PORT Serial1
#define ESP_BAUD 9600

void setup(){
  // turn on the radio from the coproc
  PORT->Group[0].DIRSET.reg = PORT_PA17;
  PORT->Group[0].OUTSET.reg = PORT_PA17;
  // turn on sensor I2C
  PORT->Group[0].DIRSET.reg = PORT_PA05;
  PORT->Group[0].OUTSET.reg = PORT_PA05;

  DEBUG_PORT.begin(115200);
  while(!DEBUG_PORT);
  DEBUG_PORT.println("ESP32 pins pulled HIGH");
  // Get UART connecting coproc and esp32 online.
  ESP_PORT.begin(ESP_BAUD); // UART, coproc->esp32 and vice versa.
  while(!ESP_PORT);

  delay(1000);

  ESP_PORT.println("Hello world!");
  DEBUG_PORT.println("sent HELLO WORLD");
  }
void loop() {
  if (ESP_PORT.available()){
    DEBUG_PORT.println("Received:");
    DEBUG_PORT.println(ESP_PORT.readStringUntil('\n'));
  }
}

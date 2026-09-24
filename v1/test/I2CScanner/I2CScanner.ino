#include <Wire.h>

#define DEBUG_PORT SerialUSB

void setup() {
  Wire.begin();

  // turn on the radio from the coproc
  PORT->Group[0].DIRSET.reg = PORT_PA17;
  PORT->Group[0].OUTSET.reg = PORT_PA17;
  // turn on sensor I2C
  PORT->Group[0].DIRSET.reg = PORT_PA05;
  PORT->Group[0].OUTSET.reg = PORT_PA05;
  
  DEBUG_PORT.begin(115200);
  delay(2000);
  DEBUG_PORT.println("Scanning I2C bus...");

  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();
    if (error == 0) {
      DEBUG_PORT.print("Device found at address 0x");
      if (address < 16) DEBUG_PORT.print("0");
      DEBUG_PORT.println(address, HEX);
    }
  }
  DEBUG_PORT.println("Done.");
}

void loop() {}

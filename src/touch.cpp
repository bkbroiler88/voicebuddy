#include "touch.h"
#include "config.h"
#include <Wire.h>

bool touchBegin() {
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);
  delay(10);
  digitalWrite(TOUCH_RST, HIGH);
  delay(300);
  Wire.beginTransmission(TOUCH_ADDR);
  return Wire.endTransmission() == 0;
}

bool touchRead(int &x, int &y) {
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)TOUCH_ADDR, 5) != 5) return false;
  uint8_t n  = Wire.read() & 0x0F;
  uint8_t xh = Wire.read(), xl = Wire.read();
  uint8_t yh = Wire.read(), yl = Wire.read();
  if (n == 0 || n > 2) return false;
  int rx = ((xh & 0x0F) << 8) | xl;   // portrait 0..239
  int ry = ((yh & 0x0F) << 8) | yl;   // portrait 0..319
  x = constrain(ry, 0, 319);
  y = constrain(239 - rx, 0, 239);
  return true;
}

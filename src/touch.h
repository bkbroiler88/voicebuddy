#pragma once
#include <Arduino.h>

// FT6336G over I2C, mapped to TFT rotation 1 (320x240 landscape)
bool touchBegin();
bool touchRead(int &x, int &y);

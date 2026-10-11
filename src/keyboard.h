#pragma once
#include <Arduino.h>

// On-screen keyboard for the WiFi password. Draws in the lower part of the screen
// (y 84..216) and edits a caller-owned buffer.

enum KbResult { KB_NONE, KB_CHANGED, KB_OK };

void     kbReset();
void     kbDraw();                                   // keys only
KbResult kbTap(int x, int y, char *buf, size_t cap); // edits buf, KB_OK when OK pressed

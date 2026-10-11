#pragma once
#include <Arduino.h>
#include "settings.h"

// WiFi CSI proximity: listens (promiscuous, no password needed) to frames from one
// router and measures how much a body near the board disturbs the channel.
// Output is a coarse zone, not a distance -- it must be calibrated per room.

struct ApInfo { char ssid[33]; uint8_t bssid[6]; int channel; int rssi; };

void  csiBegin(const uint8_t bssid[6], int channel);
void  csiStop();
bool  csiRunning();
int   csiScan(ApInfo *out, int maxN);   // blocking ~3 s; restarts CSI afterwards if it was running
void  csiTick();                        // call often from loop()

float csiScore();
int   csiPps();
Zone  csiZone();                        // stable zone (hysteresis applied)

// Calibration capture: lead-in then capture; results are written into g.calScore / g.baseline
void  csiStartCal(Zone z);
bool  csiCalBusy();
int   csiCalSecondsLeft();              // >0 lead-in, <0 capturing (negated), 0 idle
Zone  csiCalZone();
const char *zoneName(Zone z);

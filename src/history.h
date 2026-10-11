#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Day log for the parent page: average/peak voice level per 10 minutes, loud moments
// and stars. One small file per day in LittleFS, last 8 days kept. Needs real time
// (NTP), so nothing is recorded until the clock is set.

bool timeValid();
void timeBegin();                   // start NTP with g.tz (call once WiFi is up)
int  minuteOfDay();                 // -1 if no time yet

void histTick(float levelDb);       // call ~every second from the UI loop
void histLoud(int peakDb, int seconds);
void histStar();
void histToJson(JsonDocument &doc); // fills {timeValid, today:{date,slots,events}, week:[...]}

#pragma once
#include <Arduino.h>
#include "settings.h"

// WiFi CSI zones. While connected to the home router (see wifi_net), frames from the
// router are captured with their per-subcarrier amplitudes. Every 0.5 s a window
// feature is built: the amplitude shape of all 52 subcarriers plus how much each one
// wobbles. The first-run setup trains an average feature per zone (empty room, near,
// mid, far); live windows are matched to the closest trained zone with a confidence.

bool  csiStart();            // needs netConnected(); starts capture + router traffic
void  csiStop();
bool  csiRunning();
void  csiTick();             // call often from loop()

int   csiPps();
float csiMotion();           // 0..1, self-normalising movement level

Zone  csiZone();             // stable zone (held 2 s), Z_UNKNOWN if unsure
float csiConfidence();       // confidence of the stable zone 0..1
Zone  csiLiveZone();         // instant best match, for the live check screen
float csiLiveConfidence();

// Training (driven by the setup wizard)
void  csiTrainBegin(Zone z);
int   csiTrainEnd();         // finalises the zone, returns windows used
bool  csiTraining();
bool  csiModelReady();       // empty room + at least two places trained
bool  csiZoneTrained(Zone z);
void  csiModelSave();
void  csiModelLoad();
void  csiModelClear();

const char *zoneName(Zone z);

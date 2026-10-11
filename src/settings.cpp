#include "settings.h"
#include <Preferences.h>
#include <stddef.h>

#define SETTINGS_VERSION 7

Settings g;
static Preferences prefs;

void settingsDefaults() {
  memset(&g, 0, sizeof(g));
  g.version    = SETTINGS_VERSION;
  g.loudDb     = 70;
  g.warnMargin = 6;
  g.floorDb    = 45;
  g.micGain    = 24;
  g.calOffset  = 110;   // measured: quiet room -75 dBFS ~ 35 dB SPL
  g.distOn     = false;
  g.channel    = 0;
  g.midOffset  = -8;
  g.farOffset  = -14;
  for (int i = 0; i < 4; i++) g.calScore[i] = -1;
  g.hasBaseline = false;
  g.starEvery  = 45;
  g.dailyGoal  = 10;
  g.loudResets = false;
  g.chime      = false;
  g.stars      = 0;
  g.brightness = 80;
  g.ledGlow    = true;
  g.sleepAfter = 120;
  strcpy(g.pin, "1234");
  g.view       = 0;
  g.speechOnly = true;
  g.vadSens    = 6;
  g.noiseMargin = 6;
  // voice setup fields default to 0 / off until the setup is run
  g.bubbles    = true;
}

// Fields are only ever appended, so an older blob is a valid prefix of the current
// struct: read it over the defaults and the new fields keep their default values.
static size_t blobSizeForVersion(uint32_t v) {
  switch (v) {
    case 4: return offsetof(Settings, speechOnly);
    case 5: return offsetof(Settings, pitchLo);
    case 6: return offsetof(Settings, bubbles);
    case SETTINGS_VERSION: return sizeof(Settings);
    default: return 0;
  }
}

void settingsLoad() {
  settingsDefaults();
  prefs.begin("voicebuddy", true);
  Settings tmp = g;
  size_t stored = prefs.getBytesLength("cfg");
  size_t n = (stored && stored <= sizeof(tmp)) ? prefs.getBytes("cfg", &tmp, stored) : 0;
  prefs.end();
  if (n > 0 && n == blobSizeForVersion(tmp.version)) {
    tmp.version = SETTINGS_VERSION;
    g = tmp;
  }
}

void settingsSave() {
  prefs.begin("voicebuddy", false);
  prefs.putBytes("cfg", &g, sizeof(g));
  prefs.end();
}

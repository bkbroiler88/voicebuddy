#pragma once
#include <Arduino.h>

#define CSI_NSUB 52

enum Zone : int8_t { Z_UNKNOWN = -1, Z_EMPTY = 0, Z_FAR = 1, Z_MID = 2, Z_NEAR = 3 };

struct Settings {
  uint32_t version;

  // Sound (dB values are approximate SPL = dBFS + calOffset)
  int loudDb;        // "too loud" limit at the NEAR reference position
  int warnMargin;    // yellow zone starts this many dB below the limit
  int floorDb;       // below this = quiet (not talking)
  int micGain;       // ES8311 PGA, 0..42 dB in 6 dB steps
  int calOffset;     // dBFS -> approx dB SPL

  // Distance (WiFi CSI)
  bool distOn;
  uint8_t bssid[6];
  int channel;
  char ssid[33];
  int midOffset;     // dB added to limits when he is mid-room
  int farOffset;     // dB added to limits when he is far away
  float calScore[4]; // indexed by Zone, <0 = not calibrated
  bool hasBaseline;
  float baseline[CSI_NSUB];

  // Rewards
  int starEvery;     // seconds of nice talking per star
  int dailyGoal;
  bool loudResets;   // loud episode resets progress toward next star (default: just pauses)
  bool chime;
  int stars;

  // System
  int brightness;    // %
  bool ledGlow;
  int sleepAfter;    // s of silence before Buddy naps
  char pin[5];

  int view;          // 0 pup, 1 dial, 2 Saturn, 3 solar system (swipe to change)

  // Speech detection (v5)
  bool speechOnly;   // meter, moods and stars only respond to voice
  int vadSens;       // 1..10, higher = accepts less clearly pitched sound as voice
  int noiseMargin;   // dB above the learned background a voice must be

  // Voice setup (v6)
  int pitchLo, pitchHi;   // Hz band learned from his voice, 0 = not set up
  bool hisVoiceOnly;      // only accept voices inside that band
  int insideDb, excitedDb;// what the setup measured (for display), 0 = not measured

  // v7
  bool bubbles;           // Buddy says short phrases in speech bubbles

  // v8: first-run setup
  bool setupDone;
  bool lombard;           // louder room raises the line a little
  int roomNoiseDb;        // background measured during setup (approx dB SPL)
  int childP10, childP90; // his/her pitch range (Hz) measured during setup
  int parentP90;          // highest typical grown-up pitch (Hz), 0 = none recorded
  int nParents;

  // v9: web control + schedule
  bool webOn;             // HTTPS parent page (off by default)
  bool resetOn;           // reset stars each morning
  int resetMin;           // minute of day for the reset
  bool quietOn;           // quiet hours (bedtime line)
  int quietFromMin, quietToMin, quietLine;
  char tz[48];            // POSIX time zone
};

extern Settings g;

void settingsDefaults();
void settingsLoad();
void settingsSave();

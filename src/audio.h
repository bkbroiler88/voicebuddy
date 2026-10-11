#pragma once
#include <Arduino.h>

// ES8311 codec + I2S full duplex. Mic level is measured on a Core-0 task.
bool  audioBegin(int micGainDb);
bool  audioCodecOk();
void  audioSetGain(int micGainDb);   // call from the UI thread (shares I2C with touch)
float audioLevelDbfs();              // smoothed speech-band level, fast attack / slow release
void  audioDump();                   // serial: codec ID + register dump
// Speech detection + learned noise floor
void  audioSetVad(float voicingThresh, float noiseMarginDb);
float audioSpeechDbfs();             // like audioLevelDbfs() but only moves while voice is present
float audioNoiseDbfs();              // learned background level
float audioVoicing();                // last pitch-correlation peak 0..1
bool  audioSpeechActive();           // voice heard within the last 0.3 s
void  audioSetPitchBand(int loHz, int hiHz);   // only accept voices in this pitch range (0,0 = any)
float audioPitchHz();                // pitch of the last voiced frame
void  audioChime();                 // short two-note "you earned a star" sound

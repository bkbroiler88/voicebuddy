#pragma once
#include <Arduino.h>

// Offline phrase recognition (Espressif ESP-SR MultiNet, English) for the reading games.
// Give it a short list of phrases; it reports which one it heard. Audio comes from the
// microphone task through speechFeed(), so the level/pitch processing keeps running.
// Costs ~80 KB internal RAM + ~3.7 MB PSRAM and about a third of one core while running,
// so games start it when they open and stop it when they close.

bool speechBegin(const char *const *phrases, int count);   // <= 200 phrases, plain English
void speechEnd();
bool speechRunning();
int  speechPoll(uint32_t *heardMs = nullptr);   // index of the phrase heard since the last poll, or -1
void speechFeed(const int16_t *mono, size_t samples);
float speechFedPeakDb();   // peak level of the audio fed in the last second, dBFS   // called by the audio task (16 kHz mono)

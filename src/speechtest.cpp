// Speech test: ten words, one at a time. Shows a word, listens, and reports what the
// recognizer heard. The serial log keeps a line per word so results can be compared later.

#include <Arduino.h>
#include "ui.h"
#include "speech.h"
#include "audio.h"

static const char *const WORDS[] = {"cat", "dog", "ball", "red", "blue", "jump", "apple", "sun", "book", "happy"};
#define NWORDS (int)(sizeof(WORDS) / sizeof(WORDS[0]))
#define LISTEN_MS 8000
#define SHOW_MS   1800

enum TPhase { T_INTRO, T_LISTEN, T_RESULT, T_DONE, T_FAIL };
static TPhase phase;
static int idx, score, heard;
static uint32_t phaseMs, lastLive;

static void drawWord() {
  tft.fillRect(0, 50, 320, 140, C_SCREEN);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_DIM, C_SCREEN);
  char b[24]; snprintf(b, sizeof(b), "Word %d of %d", idx + 1, NWORDS);
  tft.drawString(b, 160, 62, 2);
  tft.setTextColor(C_TEXT, C_SCREEN);
  tft.setTextSize(2);                       // font 6 is digits-only; big font 4 shows every letter
  tft.drawString(WORDS[idx], 160, 108, 4);
  tft.setTextSize(1);
  tft.setTextColor(C_BTN, C_SCREEN);
  tft.drawString("Say it!", 160, 150, 4);
}

static void draw() {
  tft.fillScreen(C_SCREEN);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT, C_SCREEN);
  switch (phase) {
    case T_INTRO:
      tft.drawString("Speech test", 160, 30, 4);
      tft.setTextColor(C_DIM, C_SCREEN);
      tft.drawString("Ten words. Buddy shows a word,", 160, 76, 2);
      tft.drawString("your child says it, Buddy tells you", 160, 94, 2);
      tft.drawString("what it heard. About 1 m away.", 160, 112, 2);
      tft.drawString("Starting the recognizer takes a few seconds.", 160, 150, 2);
      drawButton(10, 198, 100, 34, "Cancel", C_OFF, C_BTN_TX);
      drawButton(210, 198, 100, 34, "Start", C_ON, C_BTN_TX);
      break;
    case T_LISTEN:
      drawWord();
      drawButton(10, 198, 100, 34, "Stop", C_OFF, C_BTN_TX);
      drawButton(210, 198, 100, 34, "Skip", C_BTN, C_BTN_TX);
      break;
    case T_RESULT: {
      bool ok = heard == idx;
      tft.fillRect(0, 50, 320, 140, C_SCREEN);
      tft.setTextColor(C_TEXT, C_SCREEN);
      tft.setTextSize(2);
      tft.drawString(WORDS[idx], 160, 90, 4);
      tft.setTextSize(1);
      char b[48];
      if (heard < 0) snprintf(b, sizeof(b), "Didn't catch it");
      else snprintf(b, sizeof(b), "Heard: %s", WORDS[heard]);
      tft.setTextColor(ok ? C_GOOD : C_LOUD, C_SCREEN);
      tft.drawString(b, 160, 140, 4);
      tft.drawString(ok ? "Yes!" : (heard < 0 ? "" : "Not quite"), 160, 168, 2);
      drawButton(10, 198, 100, 34, "Stop", C_OFF, C_BTN_TX);
      break;
    }
    case T_DONE: {
      tft.drawString("Speech test done", 160, 40, 4);
      char b[32]; snprintf(b, sizeof(b), "%d of %d words", score, NWORDS);
      tft.setTextSize(2);
      tft.drawString(b, 160, 96, 4);
      tft.setTextSize(1);
      tft.setTextColor(C_DIM, C_SCREEN);
      tft.drawString(score >= 8 ? "Good enough for reading games." : score >= 5 ? "Workable; shorter word lists will help." : "Not reliable yet for this voice.", 160, 140, 2);
      drawButton(10, 198, 100, 34, "Done", C_OFF, C_BTN_TX);
      drawButton(210, 198, 100, 34, "Again", C_ON, C_BTN_TX);
      break;
    }
    case T_FAIL:
      tft.drawString("Couldn't start", 160, 60, 4);
      tft.setTextColor(C_DIM, C_SCREEN);
      tft.drawString("Not enough free memory for the recognizer.", 160, 110, 2);
      tft.drawString("Turn web control off and try again.", 160, 128, 2);
      drawButton(210, 198, 100, 34, "Done", C_ON, C_BTN_TX);
      break;
  }
}

static void startWord() {
  phase = T_LISTEN; phaseMs = millis(); heard = -1;
  speechPoll();            // drop anything heard between words
  draw();
  Serial.printf("[srtest] word %d/%d: %s\n", idx + 1, NWORDS, WORDS[idx]);
}

static void finish() {
  speechEnd();
  phase = T_DONE;
  draw();
  Serial.printf("[srtest] score %d/%d\n", score, NWORDS);
}

void speechTestEnter() {
  screen = SCR_SPEECHTEST;
  phase = T_INTRO; idx = 0; score = 0;
  draw();
}

static void begin() {
  tft.fillRect(0, 140, 320, 50, C_SCREEN);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_BTN, C_SCREEN);
  tft.drawString("Starting the recognizer...", 160, 160, 2);
  if (!speechBegin(WORDS, NWORDS)) { phase = T_FAIL; draw(); return; }
  idx = 0; score = 0;
  startWord();
}

void speechTestTap(int x, int y) {
  bool left = y >= 190 && x < 120, right = y >= 190 && x >= 200;
  switch (phase) {
    case T_INTRO:  if (left) settingsEnter(TAB_DEVICE); else if (right) begin(); break;
    case T_LISTEN: if (left) finish(); else if (right) { heard = -1; phase = T_RESULT; phaseMs = millis(); draw(); Serial.printf("[srtest]   skipped\n"); } break;
    case T_RESULT: if (left) finish(); break;
    case T_DONE:   if (left) settingsEnter(TAB_DEVICE); else if (right) { phase = T_INTRO; draw(); begin(); } break;
    case T_FAIL:   if (right) settingsEnter(TAB_DEVICE); break;
  }
}

void speechTestTick() {
  uint32_t now = millis();
  if (phase == T_LISTEN) {
    uint32_t ms;
    int h = speechPoll(&ms);
    if (h >= 0 || now - phaseMs > LISTEN_MS) {
      heard = h;
      if (h == idx) score++;
      Serial.printf("[srtest]   heard=%s after %u ms  %s\n", h < 0 ? "(nothing)" : WORDS[h], (unsigned)(now - phaseMs), h == idx ? "OK" : "MISS");
      phase = T_RESULT; phaseMs = now; draw();
    } else if (now - lastLive > 250) {
      lastLive = now;
      static uint32_t lastStat = 0;
      if (now - lastStat > 1000) {
        lastStat = now;
        Serial.printf("[srtest]   fed peak %.0f dBFS  slot %c  pp L %u R %u  lvl %.0f dBFS\n", speechFedPeakDb(), g_srSlotR ? 'R' : 'L',
                      (unsigned)g_rawPpL, (unsigned)g_rawPpR, audioLevelDbfs());
      }
      // small listening indicator that follows the voice level
      int w = constrain((int)((audioLevelDbfs() + 90) * 3), 0, 240);
      tft.fillRect(40, 176, 240, 6, C_PANEL);
      tft.fillRect(40, 176, w, 6, audioSpeechActive() ? C_GOOD : C_DIM);
    }
  } else if (phase == T_RESULT && now - phaseMs > SHOW_MS) {
    if (++idx >= NWORDS) finish(); else startWord();
  }
}

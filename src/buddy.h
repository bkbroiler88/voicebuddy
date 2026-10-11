#pragma once
#include <TFT_eSPI.h>

enum Mood { M_CALM, M_HAPPY, M_ALERT, M_LOUD, M_SLEEPY, M_PROUD, M_CELEBRATE };

struct BuddyAnim {
  float t;          // seconds, drives breathing / bounce
  float earCover;   // 0..1 paws over ears
  float eyeWide;    // 0..1
  uint16_t bg;      // background (already faded by caller)
};

// Police pup into a sprite, centred.
void drawBuddy(TFT_eSprite &s, Mood m, const BuddyAnim &a);

// Half-circle dial. Levels are 0..1 positions along the dial.
void drawDial(TFT_eSprite &s, Mood m, const BuddyAnim &a, float kLevel, float kWarn, float kLoud);

// Speech bubble: left edge x, top y, tail pointing at (tx, ty). Width fits the text, capped at maxW.
void drawBubble(TFT_eSprite &s, const char *text, int x, int y, int maxW, int tx, int ty);

// Space views (full-width sprite, dark sky in a.bg)
void drawSaturn(TFT_eSprite &s, Mood m, const BuddyAnim &a, float kLevel, float kWarn, float kLoud);
void drawSolar(TFT_eSprite &s, Mood m, const BuddyAnim &a, float kLevel, float kWarn, float kLoud);

// Page dots (one per view) starting at x, active one filled
void drawPageDots(TFT_eSprite &s, int x, int y, int active, int count);

// Helpers shared with the main UI
void fillStar(TFT_eSPI &d, int cx, int cy, int r, uint16_t col);
uint16_t lerp565(uint16_t a, uint16_t b, float t);

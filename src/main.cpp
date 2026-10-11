// Voice Buddy -- an inside-voice coach for the LCDWIKI ES3C28P (ESP32-S3 2.8" CYD)
//
// Buddy the police pup listens through the onboard ES8311 mic:
//   quiet        -> calm / naps after a while
//   nice talking -> smiles, and earns stars over time
//   getting loud -> eyes go wide (soft yellow)
//   too loud     -> gently covers his ears (soft peach) until it is calm again
// WiFi-CSI zones (trained in the first-run setup) shift the limits so a normal voice
// from across the room is judged the same as from up close.
//
// Cores: Core 0 runs the audio/pitch task and the CSI sensing task; Core 1 (this loop)
// only draws and handles touch. Sprites live in PSRAM.
//
// Parent settings: hold the top-right corner for 3 s, then enter the PIN (default 1234).

#include <Arduino.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>

#include "config.h"
#include "settings.h"
#include "audio.h"
#include "touch.h"
#include "csi.h"
#include "wifi_net.h"
#include "buddy.h"
#include "ui.h"

TFT_eSPI tft;
static TFT_eSprite sprBuddy(&tft);
static TFT_eSprite sprMeter(&tft);
static TFT_eSprite sprDial(&tft);
static Adafruit_NeoPixel pixel(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return tft.color565(r, g, b); }

uint16_t C_SCREEN, C_PANEL, C_TEXT, C_DIM, C_BTN, C_BTN_TX, C_ON, C_OFF,
         C_GOOD, C_WARN, C_LOUD, C_GOLD, C_TAB;

// ---------------- layout ----------------
#define BUDDY_X 0
#define BUDDY_Y 30
#define BUDDY_W 210
#define BUDDY_H 210
#define METER_X 212
#define METER_Y 30
#define METER_W 108
#define METER_H 210
#define DIAL_Y 30
#define DIAL_W 320
#define DIAL_H 210
enum ViewId { V_PUP = 0, V_DIAL, V_SATURN, V_SOLAR, NVIEWS };   // swipe order

Screen screen = SCR_CHILD;

// ---------------- voice state ----------------
enum Voice { V_QUIET, V_NICE, V_ALERT, V_LOUD };
static Voice vs = V_QUIET;
static float loudT = 0, alertT = 0, exitT = 0, talkHold = 0, quietT = 0;
static float proudT = 0, celebrateT = 0, starProgress = 0;
static float distOffset = 0;     // glided dB offset from the zone
static float lvlSpl = 0;         // approx dB SPL
static Mood mood = M_CALM;

// animation
static float animT = 0, earCover = 0, eyeWide = 0;
static float bgR = 225, bgG = 245, bgB = 230;
static float skyR = 27, skyG = 36, skyB = 72;

static int  lastStarsDrawn = -1, lastGoalDrawn = -1, lastProgPx = -1;

void applyBrightness() {
  analogWrite(BACKLIGHT_PIN, map(g.brightness, 10, 100, 25, 255));
}

static float zoneOffset(Zone z) {
  if (!g.distOn || !csiRunning() || !csiModelReady()) return 0;
  switch (z) {
    case Z_NEAR: return 0;
    case Z_FAR:  return g.farOffset;
    case Z_MID:  return g.midOffset;
    default:     return g.midOffset;   // unknown / looks empty but someone is talking
  }
}

// People talk louder over a TV: when the room is much louder than it was during
// setup, let the line rise a little (half a dB per dB, at most 8 dB).
static float lombardBonus() {
  if (!g.lombard) return 0;
  float excess = (audioNoiseDbfs() + g.calOffset) - g.roomNoiseDb - 10.f;
  return excess > 0 ? fminf(8.f, 0.5f * excess) : 0;
}

void applyVad() {
  audioSetVad(0.80f - 0.05f * g.vadSens, g.noiseMargin);
  if (g.hisVoiceOnly && g.pitchHi > g.pitchLo) audioSetPitchBand(g.pitchLo, g.pitchHi);
  else audioSetPitchBand(0, 0);
}

// ======================================================================
//  Child screen
// ======================================================================
static void drawTopBar(bool force) {
  int progPx = (int)(150 * constrain(starProgress / max(1, g.starEvery), 0.f, 1.f));
  if (force || g.stars != lastStarsDrawn || g.dailyGoal != lastGoalDrawn) {
    tft.fillRect(0, 0, 100, 30, C_SCREEN);
    fillStar(tft, 16, 15, 12, C_GOLD);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(C_TEXT, C_SCREEN);
    char b[16];
    snprintf(b, sizeof(b), "%d/%d", g.stars, g.dailyGoal);
    tft.drawString(b, 32, 16, 4);
    lastStarsDrawn = g.stars;
    lastGoalDrawn = g.dailyGoal;
  }
  if (force) {
    tft.drawRoundRect(104, 9, 156, 14, 7, C_DIM);
    tft.fillCircle(304, 12, 3, C_DIM);   // the (quiet) parent corner
    lastProgPx = -1;
  }
  if (progPx != lastProgPx) {
    tft.fillRoundRect(107, 12, 150, 8, 4, C_PANEL);
    if (progPx > 0) tft.fillRoundRect(107, 12, max(8, progPx), 8, 4, C_GOLD);
    lastProgPx = progPx;
  }
}

static void drawParentHoldProgress(float k) {
  tft.fillRect(272, 24, 44, 4, C_SCREEN);
  if (k > 0) tft.fillRect(272, 24, (int)(44 * k), 4, C_DIM);
}

static void drawMeter(float effLoud, float effWarn, float effFloor) {
  TFT_eSprite &s = sprMeter;
  s.fillSprite(C_SCREEN);
  const int bx = 30, bw = 46, top = 12, bot = 172;
  float lo = effFloor - 6, hi = effLoud + 10;
  auto yOf = [&](float db) {
    float k = (db - lo) / (hi - lo);
    return (int)(bot - constrain(k, 0.f, 1.f) * (bot - top));
  };
  int yWarn = yOf(effWarn), yLoud = yOf(effLoud);

  // zone background
  s.fillRoundRect(bx, top, bw, bot - top, 12, C_PANEL);
  s.fillRect(bx + 4, yWarn, bw - 8, bot - yWarn - 4, lerp565(C_PANEL, C_GOOD, 0.35f));
  s.fillRect(bx + 4, yLoud, bw - 8, yWarn - yLoud, lerp565(C_PANEL, C_WARN, 0.35f));
  s.fillRect(bx + 4, top + 4, bw - 8, yLoud - top - 4, lerp565(C_PANEL, C_LOUD, 0.35f));

  // current level fill
  int yLvl = yOf(lvlSpl);
  uint16_t col = lvlSpl > effLoud ? C_LOUD : (lvlSpl > effWarn ? C_WARN : C_GOOD);
  if (yLvl < bot - 4) s.fillRoundRect(bx + 8, yLvl, bw - 16, bot - 4 - yLvl, 6, col);

  // the "line" Buddy cares about
  s.fillRect(bx - 6, yLoud - 1, bw + 12, 3, C_TEXT);

  // little faces at each end: quiet smile at the bottom, open mouth at top
  s.fillCircle(bx + bw / 2, bot + 18, 11, C_GOOD);
  s.drawCircle(bx + bw / 2 - 4, bot + 15, 1, C_TEXT);
  s.drawCircle(bx + bw / 2 + 4, bot + 15, 1, C_TEXT);
  s.drawLine(bx + bw / 2 - 4, bot + 21, bx + bw / 2 + 4, bot + 21, C_TEXT);

  // distance dots (only when distance mode is on)
  if (g.distOn && csiRunning()) {
    Zone z = csiZone();
    const Zone order[3] = {Z_NEAR, Z_MID, Z_FAR};
    for (int i = 0; i < 3; i++) {
      bool on = (z == order[i]) || (i == 1 && (z == Z_EMPTY || z == Z_UNKNOWN));
      s.fillCircle(bx + 4 + i * 19, METER_H - 6, on ? 4 : 2, on ? C_TEXT : C_DIM);
    }
  }
  s.pushSprite(METER_X, METER_Y);
}

void setPixel(uint8_t r, uint8_t g8, uint8_t b) {
  static uint32_t last = 0xFFFFFFFF;
  uint32_t c = pixel.Color(r, g8, b);
  if (c == last) return;
  last = c;
  pixel.setPixelColor(0, c);
  pixel.show();
}

static void earnStar() {
  g.stars++;
  starProgress = 0;
  proudT = PROUD_SHOW_S;
  if (g.stars == g.dailyGoal) celebrateT = CELEBRATE_SHOW_S;
  if (g.chime) audioChime();
  settingsSave();
  Serial.printf("[star] %d/%d\n", g.stars, g.dailyGoal);
}

void enterChild() {
  screen = SCR_CHILD;
  tft.fillScreen(C_SCREEN);
  drawTopBar(true);
}

void resetStarProgress() { starProgress = 0; }

static float limitNow() { return g.loudDb + distOffset + lombardBonus(); }
float currentLimitDb() { return limitNow(); }
float currentLevelDb() { return (g.speechOnly ? audioSpeechDbfs() : audioLevelDbfs()) + g.calOffset; }

static void updateVoice(float dt, float effLoud, float effWarn, float effFloor) {
  // voiceNow: sound that is actually voice right now (no long hold), used for stars
  bool voiceNow = g.speechOnly ? (audioSpeechActive() && lvlSpl > effFloor) : (lvlSpl > effFloor);
  if (voiceNow) talkHold = 0.8f; else talkHold -= dt;
  bool talking = talkHold > 0;   // smoothed, for Buddy's face only

  if (vs == V_LOUD) {
    if (lvlSpl < effLoud - LOUD_EXIT_HYST_DB) exitT += dt; else exitT = 0;
    if (exitT >= LOUD_EXIT_S) { vs = V_ALERT; exitT = 0; }
  } else {
    if (lvlSpl > effLoud) loudT += dt; else loudT = 0;
    if (loudT >= LOUD_ENTER_S) {
      vs = V_LOUD; loudT = alertT = exitT = 0;
      if (g.loudResets) starProgress = 0;
      Serial.printf("[voice] loud %.0f dB (limit %.0f)\n", lvlSpl, effLoud);
    } else if (vs == V_ALERT) {
      if (lvlSpl < effWarn - ALERT_EXIT_HYST_DB) exitT += dt; else exitT = 0;
      if (exitT >= ALERT_EXIT_S) { vs = talking ? V_NICE : V_QUIET; exitT = 0; }
    } else {
      if (lvlSpl > effWarn) alertT += dt; else alertT = 0;
      if (alertT >= ALERT_ENTER_S) { vs = V_ALERT; alertT = exitT = 0; }
      else vs = talking ? V_NICE : V_QUIET;
    }
  }

  if (vs == V_QUIET && !talking) quietT += dt; else quietT = 0;

  // stars only grow while his voice is actually heard
  if ((vs == V_NICE || vs == V_ALERT) && voiceNow) {
    starProgress += dt;
    if (starProgress >= g.starEvery) earnStar();
  }
  if (proudT > 0) proudT -= dt;
  if (celebrateT > 0) celebrateT -= dt;

  if (celebrateT > 0)                    mood = M_CELEBRATE;
  else if (proudT > 0)                   mood = M_PROUD;
  else if (vs == V_LOUD)                 mood = M_LOUD;
  else if (vs == V_ALERT)                mood = M_ALERT;
  else if (vs == V_NICE)                 mood = M_HAPPY;
  else if (quietT > g.sleepAfter)        mood = M_SLEEPY;
  else                                   mood = M_CALM;
}

// ---------------- speech bubbles ----------------
static const char *const P_LOUD[]    = {"That's too loud!", "Ouch, my ears!", "Inside voice, please", "Too loud for me!"};
static const char *const P_ALERT[]   = {"Getting loud...", "A little quieter?", "Easy does it"};
static const char *const P_THANKS[]  = {"Thank you!", "Much better!", "Ahh, that's nice"};
static const char *const P_PRAISE[]  = {"Nice and quiet", "Good job!", "Perfect volume!", "I like your voice"};
static const char *const P_STAR[]    = {"You earned a star!", "Great job!", "Super star!"};
static const char *const P_CELEB[]   = {"You did it!", "All your stars!"};
#define PICK(arr) arr[random(sizeof(arr) / sizeof(arr[0]))]
#define PRAISE_EVERY_S 15.f   // seconds of nice talking between compliments

static const char *bubble = nullptr;
static float bubbleLeft = 0;     // seconds; >= 1000 means "while this mood lasts"
static Mood prevMood = M_CALM;
static float praiseT = 0;
static int demoMood = -1;        // >= 0: mood forced over serial for screenshots

static void say(const char *text, float secs) { bubble = text; bubbleLeft = secs; }

static void updateBubble(float dt) {
  if (!g.bubbles) { bubble = nullptr; prevMood = mood; return; }

  if (mood != prevMood) {
    if (bubbleLeft >= 1000) bubble = nullptr;            // a "while it lasts" phrase ends with its mood
    switch (mood) {
      case M_LOUD:      say(PICK(P_LOUD), 1e6f); break;
      case M_ALERT:     if (prevMood == M_LOUD) say(PICK(P_THANKS), 3.f); else say(PICK(P_ALERT), 1e6f); break;
      case M_PROUD:     say(PICK(P_STAR), PROUD_SHOW_S); break;
      case M_CELEBRATE: say(PICK(P_CELEB), CELEBRATE_SHOW_S); break;
      case M_HAPPY:
      case M_CALM:      if (prevMood == M_LOUD) say(PICK(P_THANKS), 3.f);
                        else if (prevMood == M_ALERT) say(PICK(P_PRAISE), 2.5f);
                        break;
      default: break;
    }
    prevMood = mood;
  }

  // a compliment now and then while he keeps a nice voice
  if (mood == M_HAPPY) {
    praiseT += dt;
    if (praiseT >= PRAISE_EVERY_S && !bubble) { say(PICK(P_PRAISE), 3.f); praiseT = 0; }
  }

  if (bubble && bubbleLeft < 1000) {
    bubbleLeft -= dt;
    if (bubbleLeft <= 0) bubble = nullptr;
  }
}

static void childFrame(float dt) {
  float target = zoneOffset(csiZone());
  float step = OFFSET_GLIDE_DB_S * dt;
  if (distOffset < target) distOffset = fminf(target, distOffset + step);
  else                     distOffset = fmaxf(target, distOffset - step);

  float effLoud  = limitNow();
  float effWarn  = effLoud - g.warnMargin;
  float effFloor = g.floorDb + distOffset;
  lvlSpl = currentLevelDb();

  if (demoMood >= 0) {
    // screenshot/demo mode (serial 'S'): fixed mood and a level that matches it
    mood = (Mood)demoMood;
    switch (mood) {
      case M_LOUD:   lvlSpl = effLoud + 5; break;
      case M_ALERT:  lvlSpl = effWarn + 2; break;
      case M_CALM:
      case M_SLEEPY: lvlSpl = effFloor - 4; break;
      default:       lvlSpl = (effFloor + effWarn) / 2; break;
    }
  } else {
    updateVoice(dt, effLoud, effWarn, effFloor);
    updateBubble(dt);
  }

  // animation targets
  animT += dt;
  float k = 1.f - expf(-dt / 0.35f);
  earCover += ((mood == M_LOUD ? 1.f : 0.f) - earCover) * (1.f - expf(-dt / 0.25f));
  eyeWide  += ((mood == M_ALERT ? 1.f : 0.f) - eyeWide) * k;

  // light tints for pup/dial, night-sky tints for the space views
  float tr, tg, tb, sr, sg, sb;
  uint8_t pr, pg, pb;
  switch (mood) {
    case M_HAPPY:     tr = 200; tg = 240; tb = 210; sr = 28; sg = 53; sb = 82; pr = 0;  pg = 30; pb = 8;  break;
    case M_ALERT:     tr = 255; tg = 240; tb = 170; sr = 58; sg = 49; sb = 88; pr = 30; pg = 22; pb = 0;  break;
    case M_LOUD:      tr = 255; tg = 205; tb = 185; sr = 74; sg = 44; sb = 64; pr = 34; pg = 10; pb = 4;  break;
    case M_SLEEPY:    tr = 222; tg = 218; tb = 245; sr = 29; sg = 29; sb = 54; pr = 4;  pg = 2;  pb = 10; break;
    case M_PROUD:
    case M_CELEBRATE: tr = 255; tg = 245; tb = 200; sr = 42; sg = 48; sb = 96; pr = 30; pg = 24; pb = 4;  break;
    default:          tr = 225; tg = 245; tb = 230; sr = 27; sg = 36; sb = 72; pr = 0;  pg = 12; pb = 6;  break;
  }
  bgR += (tr - bgR) * k; bgG += (tg - bgG) * k; bgB += (tb - bgB) * k;
  skyR += (sr - skyR) * k; skyG += (sg - skyG) * k; skyB += (sb - skyB) * k;

  BuddyAnim a = {animT, earCover, eyeWide, rgb((uint8_t)bgR, (uint8_t)bgG, (uint8_t)bgB)};
  float lo = effFloor - 6, hi = effLoud + 10;
  auto kOf = [&](float db) { return (db - lo) / (hi - lo); };
  switch (g.view) {
    case V_DIAL:
      drawDial(sprDial, mood, a, kOf(lvlSpl), kOf(effWarn), kOf(effLoud));
      if (bubble) drawBubble(sprDial, bubble, 70, 176, 180, 160, 168);   // under the face
      break;
    case V_SATURN:
      a.bg = rgb((uint8_t)skyR, (uint8_t)skyG, (uint8_t)skyB);
      drawSaturn(sprDial, mood, a, kOf(lvlSpl), kOf(effWarn), kOf(effLoud));
      if (bubble) drawBubble(sprDial, bubble, 8, 6, 170, 130, 46);       // above Saturn
      break;
    case V_SOLAR:
      a.bg = rgb((uint8_t)skyR, (uint8_t)skyG, (uint8_t)skyB);
      drawSolar(sprDial, mood, a, kOf(lvlSpl), kOf(effWarn), kOf(effLoud));
      if (bubble) drawBubble(sprDial, bubble, 60, 10, 200, 40, 76);      // from the Sun
      break;
    default:
      drawBuddy(sprBuddy, mood, a);
      if (bubble) drawBubble(sprBuddy, bubble, 6, 2, 152, BUDDY_W / 2 - 12, 36);   // above his head
      drawPageDots(sprBuddy, 20, BUDDY_H - 8, 0, NVIEWS);
      sprBuddy.pushSprite(BUDDY_X, BUDDY_Y);
      drawMeter(effLoud, effWarn, effFloor);
      break;
  }
  if (g.view != V_PUP) {
    drawPageDots(sprDial, 20, DIAL_H - 8, g.view, NVIEWS);
    sprDial.pushSprite(0, DIAL_Y);
  }
  drawTopBar(false);

  if (g.ledGlow) setPixel(pr, pg, pb); else setPixel(0, 0, 0);
}

// ======================================================================
//  PIN pad
// ======================================================================
enum PinMode { PIN_UNLOCK, PIN_SET };
static PinMode pinMode_ = PIN_UNLOCK;
static char pinBuf[5];
static int pinLen = 0, pinFails = 0;
static uint32_t pinOpenedMs = 0;

static const char *PAD_KEYS[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "<", "0", "OK"};

static void padKeyRect(int i, int &x, int &y, int &w, int &h) {
  w = 70; h = 38;
  x = 50 + (i % 3) * 76;
  y = 74 + (i / 3) * 41;
}

static void drawPinDots() {
  tft.fillRect(100, 44, 120, 24, C_SCREEN);
  for (int i = 0; i < 4; i++) {
    int x = 124 + i * 24;
    if (i < pinLen) tft.fillCircle(x, 56, 7, C_TEXT);
    else            tft.drawCircle(x, 56, 7, C_DIM);
  }
}

static void enterPin(PinMode m) {
  screen = SCR_PIN;
  pinMode_ = m;
  pinLen = 0;
  pinOpenedMs = millis();
  tft.fillScreen(C_SCREEN);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT, C_SCREEN);
  tft.drawString(m == PIN_UNLOCK ? "Parent PIN" : "New PIN", 160, 20, 4);
  tft.fillRoundRect(4, 4, 44, 30, 6, C_BTN);
  tft.setTextColor(C_BTN_TX, C_BTN);
  tft.drawString("X", 26, 19, 4);
  for (int i = 0; i < 12; i++) {
    int x, y, w, h;
    padKeyRect(i, x, y, w, h);
    tft.fillRoundRect(x, y, w, h, 8, C_BTN);
    tft.setTextColor(C_BTN_TX, C_BTN);
    tft.drawString(PAD_KEYS[i], x + w / 2, y + h / 2 + 1, 4);
  }
  drawPinDots();
}

void enterPinSet() { enterPin(PIN_SET); }

static void pinTap(int tx, int ty) {
  if (tx < 50 && ty < 38) {
    if (pinMode_ == PIN_SET) settingsEnter(TAB_SCREEN); else enterChild();
    return;
  }
  for (int i = 0; i < 12; i++) {
    int x, y, w, h;
    padKeyRect(i, x, y, w, h);
    if (tx < x || tx >= x + w || ty < y || ty >= y + h) continue;
    if (i == 9) { if (pinLen > 0) pinLen--; }
    else if (i == 11) {
      if (pinLen != 4) break;
      pinBuf[4] = 0;
      if (pinMode_ == PIN_SET) {
        strcpy(g.pin, pinBuf);
        settingsSave();
        settingsEnter(TAB_SCREEN);
        return;
      }
      if (strcmp(pinBuf, g.pin) == 0) { pinFails = 0; settingsEnter(TAB_VOICE); return; }
      pinLen = 0;
      if (++pinFails >= 3) { pinFails = 0; enterChild(); return; }
    } else if (pinLen < 4) {
      pinBuf[pinLen++] = PAD_KEYS[i][0];
    }
    drawPinDots();
    break;
  }
}

// ======================================================================
//  Shared drawing helper (settings_ui.cpp and wizard.cpp use it too)
// ======================================================================
void drawButton(int x, int y, int w, int h, const char *label, uint16_t bg, uint16_t fg) {
  tft.fillRoundRect(x, y, w, h, 6, bg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(fg, bg);
  tft.drawString(label, x + w / 2, y + h / 2 + 1, 2);
}

// Serial 'P': dump the panel's frame memory as raw RGB565 (tools/screenshot.py turns it into a PNG)
static void screenshot() {
  static uint16_t row[320];
  Serial.printf("\nSHOT %d %d\n", tft.width(), tft.height());
  for (int y = 0; y < tft.height(); y++) {
    tft.readRect(0, y, tft.width(), 1, row);
    Serial.write((const uint8_t *)row, tft.width() * 2);
  }
  Serial.print("\nEND\n");
}

// ======================================================================
//  setup / loop
// ======================================================================
void setup() {
  Serial.begin(115200);
  delay(3000);   // USB-CDC enumerate
  Serial.println("\n[voice-buddy] boot");

  settingsLoad();

  tft.init();
  tft.setRotation(1);
  C_SCREEN = rgb(246, 244, 238);
  C_PANEL  = rgb(225, 222, 214);
  C_TEXT   = rgb(50, 46, 40);
  C_DIM    = rgb(150, 145, 135);
  C_BTN    = rgb(90, 110, 140);
  C_BTN_TX = TFT_WHITE;
  C_ON     = rgb(70, 160, 110);
  C_OFF    = rgb(160, 150, 150);
  C_GOOD   = rgb(90, 190, 120);
  C_WARN   = rgb(240, 190, 60);
  C_LOUD   = rgb(240, 130, 100);
  C_GOLD   = rgb(255, 190, 40);
  C_TAB    = rgb(90, 110, 140);
  tft.fillScreen(C_SCREEN);
  applyBrightness();

  sprBuddy.setColorDepth(16);
  if (!sprBuddy.createSprite(BUDDY_W, BUDDY_H)) Serial.println("[ui] buddy sprite alloc FAILED");
  sprMeter.setColorDepth(16);
  if (!sprMeter.createSprite(METER_W, METER_H)) Serial.println("[ui] meter sprite alloc FAILED");
  sprDial.setColorDepth(16);
  if (!sprDial.createSprite(DIAL_W, DIAL_H)) Serial.println("[ui] dial sprite alloc FAILED");

  pixel.begin();
  pixel.clear();
  pixel.show();

  Wire.begin(I2C_SDA, I2C_SCL, 400000);
  bool touchOk = touchBegin();
  bool audioOk = audioBegin(g.micGain);
  applyVad();
  Serial.printf("[boot] touch %s  codec %s  psram %u\n", touchOk ? "ok" : "MISSING",
                audioOk ? "ok" : "MISSING", ESP.getPsramSize());

  if (!audioOk) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_LOUD, C_SCREEN);
    tft.drawString("Mic codec not found", 160, 120, 4);
    delay(3000);
  }

  csiModelLoad();
  if (netHasCreds()) netBegin();          // joins in the background; loop() starts CSI once connected
  if (!g.setupDone) wizardEnter(WIZ_FULL);
  else enterChild();
}

// Keep CSI capture tied to the WiFi connection (the router can drop and come back)
static void serviceNet() {
  static uint32_t last = 0;
  if (millis() - last < 1000) return;
  last = millis();
  bool conn = netConnected();
  if (conn && !csiRunning()) csiStart();
  else if (!conn && csiRunning()) csiStop();
}

void loop() {
  static uint32_t lastFrame = millis();
  static bool wasDown = false;
  static uint32_t downMs = 0, lastRepeat = 0, lastLog = 0;
  static bool repeatable = false;
  static int downX = 0, downY = 0, lastX = 0, lastY = 0;

  if (screen != SCR_WIZARD) serviceNet();   // the wizard manages WiFi itself

  int tx, ty;
  bool down = touchRead(tx, ty);
  uint32_t now = millis();
  bool pressed = down && !wasDown;
  if (pressed) { downMs = now; downX = tx; downY = ty; }
  if (down) { lastX = tx; lastY = ty; }
  bool released = !down && wasDown;

  switch (screen) {
    case SCR_CHILD: {
      bool inCorner = down && tx >= 260 && ty <= 44;
      static bool holding = false;
      if (inCorner) {
        float k = (now - downMs) / (float)PARENT_HOLD_MS;
        drawParentHoldProgress(fminf(k, 1.f));
        holding = true;
        if (k >= 1.f) { holding = false; enterPin(PIN_UNLOCK); break; }
      } else if (holding) {
        holding = false;
        drawParentHoldProgress(0);
      }
      // horizontal swipe anywhere below the top bar switches view
      if (released && downY > 30) {
        int dx = lastX - downX, dy = lastY - downY;
        if (abs(dx) > 50 && abs(dy) < 70) {
          g.view = (g.view + (dx < 0 ? 1 : NVIEWS - 1)) % NVIEWS;
          tft.fillRect(0, 30, 320, 210, C_SCREEN);
          settingsSave();
          Serial.printf("[ui] view %d\n", g.view);
        }
      }
      if (now - lastFrame >= 40) {   // ~25 fps
        float dt = (now - lastFrame) / 1000.f;
        lastFrame = now;
        childFrame(fminf(dt, 0.2f));
      }
      break;
    }

    case SCR_PIN:
      if (pressed) pinTap(tx, ty);
      if (now - pinOpenedMs > 30000 && pinMode_ == PIN_UNLOCK) enterChild();
      lastFrame = now;
      break;

    case SCR_SETTINGS:
      if (pressed) { repeatable = settingsTap(tx, ty); lastRepeat = now; }
      else if (down && repeatable && now - downMs > 500 && now - lastRepeat > 110) {
        settingsTap(downX, downY);
        lastRepeat = now;
      }
      if (screen == SCR_SETTINGS) settingsTick();
      lastFrame = now;
      break;

    case SCR_WIZARD:
      if (pressed) wizardTap(tx, ty);
      if (screen == SCR_WIZARD) wizardTick();
      lastFrame = now;
      break;
  }
  wasDown = down;

  if (Serial.available()) {
    int c = Serial.read();
    if (c == 'd') {
      Serial.print("[i2c] found:");
      for (int a = 1; a < 127; a++) { Wire.beginTransmission(a); if (Wire.endTransmission() == 0) Serial.printf(" %02X", a); }
      Serial.println();
      audioDump();
    } else if (c == 'S') {
      // S<view><mood>: force a view + mood for screenshots, e.g. "S03" = pup, too loud
      int v = Serial.read() - '0', m = Serial.read() - '0';
      if (v >= 0 && v < NVIEWS && m >= 0 && m <= M_CELEBRATE) {
        if (screen != SCR_CHILD) enterChild();
        if (v != g.view) { g.view = v; tft.fillRect(0, 30, 320, 210, C_SCREEN); }
        demoMood = m;
        static const char *const demoSay[] = {nullptr, "Nice and quiet", "A little quieter?", "That's too loud!",
                                              nullptr, "Super star!", "You did it!"};
        bubble = g.bubbles ? demoSay[m] : nullptr;
        bubbleLeft = 1e6f;
        Serial.printf("[demo] view %d mood %d\n", v, m);
      }
    } else if (c == 'X') {
      demoMood = -1;
      bubble = nullptr;
      Serial.println("[demo] off");
    } else if (c == 'P') {
      screenshot();
    } else if (c == 'W') {
      // W<mode>: open a setup screen (0 full, 1 voice, 2 zones, 3 wifi, 4 grown-up, 5 zone check)
      int m = Serial.read() - '0';
      if (m >= WIZ_FULL && m <= WIZ_CHECK) { demoMood = -1; wizardEnter((WizardMode)m); }
    } else if (c == 'T') {
      // T<tab>: open a settings tab (0 Voice, 1 Room, 2 Stars, 3 Screen, 4 Setup)
      int t = Serial.read() - '0';
      if (t >= 0 && t < NTABS) { demoMood = -1; settingsEnter(t); }
    }
  }

  if (now - lastLog > 1000) {
    lastLog = now;
    Serial.printf("[lvl] %.0f dB  speech %.0f dB %s %.0fHz (%.2f)  noise %.0f dB  line %.0f  mood %d  "
                  "zone %s %.0f%% (live %s)  %dpps motion %.2f  stars %d prog %.0f/%d  heap %u psram %u\n",
                  audioLevelDbfs() + g.calOffset, audioSpeechDbfs() + g.calOffset,
                  audioSpeechActive() ? "VOICE" : "-", audioPitchHz(), audioVoicing(), audioNoiseDbfs() + g.calOffset,
                  limitNow(), (int)mood, zoneName(csiZone()), csiConfidence() * 100, zoneName(csiLiveZone()),
                  csiPps(), csiMotion(), g.stars, starProgress, g.starEvery, ESP.getFreeHeap(), ESP.getFreePsram());
  }
  delay(2);
}

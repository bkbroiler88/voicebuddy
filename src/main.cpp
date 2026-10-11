// Voice Buddy -- an inside-voice coach for the LCDWIKI ES3C28P (ESP32-S3 2.8" CYD)
//
// Buddy the police pup listens through the onboard ES8311 mic:
//   quiet        -> calm / naps after a while
//   nice talking -> smiles, and earns stars over time
//   getting loud -> eyes go wide (soft yellow)
//   too loud     -> gently covers his ears (soft peach) until it is calm again
// Optional WiFi-CSI proximity shifts the limits so a normal voice from across
// the room is judged the same as from up close (sound drops ~6 dB per doubling).
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
#include "buddy.h"

static TFT_eSPI tft;
static TFT_eSprite sprBuddy(&tft);
static TFT_eSprite sprMeter(&tft);
static TFT_eSprite sprDial(&tft);
static Adafruit_NeoPixel pixel(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return tft.color565(r, g, b); }

static uint16_t C_SCREEN, C_PANEL, C_TEXT, C_DIM, C_BTN, C_BTN_TX, C_ON, C_OFF,
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

enum Screen { SCR_CHILD, SCR_PIN, SCR_SETTINGS, SCR_VOICESETUP };
static Screen screen = SCR_CHILD;

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

static float applyBrightness() {
  analogWrite(BACKLIGHT_PIN, map(g.brightness, 10, 100, 25, 255));
  return 0;
}

static float zoneOffset(Zone z) {
  bool anyCal = false;
  for (int i = 0; i < 4; i++) if (g.calScore[i] >= 0) anyCal = true;
  if (!g.distOn || !csiRunning() || !anyCal) return 0;
  switch (z) {
    case Z_NEAR: return 0;
    case Z_FAR:  return g.farOffset;
    case Z_MID:  return g.midOffset;
    default:     return g.midOffset;   // unknown / looks empty but someone is talking
  }
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

static void setPixel(uint8_t r, uint8_t g8, uint8_t b) {
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

static void enterChild() {
  screen = SCR_CHILD;
  tft.fillScreen(C_SCREEN);
  drawTopBar(true);
}

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

  float effLoud  = g.loudDb + distOffset;
  float effWarn  = effLoud - g.warnMargin;
  float effFloor = g.floorDb + distOffset;
  lvlSpl = (g.speechOnly ? audioSpeechDbfs() : audioLevelDbfs()) + g.calOffset;

  updateVoice(dt, effLoud, effWarn, effFloor);
  updateBubble(dt);

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

static void enterSettings(int tab);

static void pinTap(int tx, int ty) {
  if (tx < 50 && ty < 38) {
    if (pinMode_ == PIN_SET) enterSettings(5); else enterChild();   // back to SYS tab
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
        enterSettings(5);   // SYS tab
        return;
      }
      if (strcmp(pinBuf, g.pin) == 0) { pinFails = 0; enterSettings(0); return; }
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
//  Settings
// ======================================================================
enum RowType : uint8_t { R_VALUE, R_TOGGLE, R_ACTION, R_READOUT };
typedef void (*TextFn)(char *, size_t);
typedef void (*ActFn)();

struct Row {
  RowType type;
  const char *label;
  int *val; int mn, mx, step; const char *unit;
  bool *flag;
  ActFn act; const char *btn;
  TextFn text;
  ActFn onChange;
};

// ---- live readouts / actions ----
static void txtLevel(char *b, size_t n) {
  float off = zoneOffset(csiZone());
  snprintf(b, n, "now %.0f dB   limit %.0f", audioLevelDbfs() + g.calOffset, g.loudDb + off);
}
static void txtDistLive(char *b, size_t n) {
  if (!g.distOn) { snprintf(b, n, "off"); return; }
  if (!csiRunning()) { snprintf(b, n, "pick a router first"); return; }
  snprintf(b, n, "%s  %d pkt/s  adj %+.0f dB", zoneName(csiZone()), csiPps(), zoneOffset(csiZone()));
}
static void txtCalLive(char *b, size_t n) {
  if (!csiRunning()) { snprintf(b, n, "turn Distance on first"); return; }
  snprintf(b, n, "score %.3f  %d pkt/s  %s", csiScore(), csiPps(), zoneName(csiZone()));
}
static void txtRouter(char *b, size_t n) {
  if (g.channel <= 0) snprintf(b, n, "none");
  else snprintf(b, n, "%.12s c%d", g.ssid, g.channel);
}
static void txtCal(Zone z, char *b, size_t n) {
  if (csiCalBusy() && csiCalZone() == z) {
    int s = csiCalSecondsLeft();
    if (s > 0) snprintf(b, n, "wait %d", s); else snprintf(b, n, "rec %d", -s);
  } else if (g.calScore[z] < 0) snprintf(b, n, "--");
  else snprintf(b, n, "%.3f", g.calScore[z]);
}
static void txtCalEmpty(char *b, size_t n) { txtCal(Z_EMPTY, b, n); }
static void txtCalNear(char *b, size_t n)  { txtCal(Z_NEAR, b, n); }
static void txtCalMid(char *b, size_t n)   { txtCal(Z_MID, b, n); }
static void txtCalFar(char *b, size_t n)   { txtCal(Z_FAR, b, n); }

static ApInfo aps[20];
static int apCount = 0, apIdx = 0;
static uint32_t apScanMs = 0;
static char routerBtn[8] = "Scan";
static void drawSettingsFull();
static void footerMsg(const char *m);

static void actRouter() {
  if (apCount == 0 || millis() - apScanMs > 60000) {
    footerMsg("Scanning WiFi...");
    apCount = csiScan(aps, 20);
    apScanMs = millis();
    apIdx = 0;
  } else {
    apIdx = (apIdx + 1) % apCount;
  }
  if (apCount > 0) {
    ApInfo &a = aps[apIdx];
    bool changed = memcmp(g.bssid, a.bssid, 6) != 0;
    memcpy(g.bssid, a.bssid, 6);
    g.channel = a.channel;
    strcpy(g.ssid, a.ssid);
    if (changed) { g.hasBaseline = false; for (int i = 0; i < 4; i++) g.calScore[i] = -1; }
    if (g.distOn) csiBegin(g.bssid, g.channel);
    strcpy(routerBtn, "Next");
    char m[48];
    snprintf(m, sizeof(m), "%d/%d  %d dBm", apIdx + 1, apCount, a.rssi);
    footerMsg(m);
  } else {
    footerMsg("No WiFi found");
  }
}
static void onDistToggle() {
  if (g.distOn && g.channel > 0) csiBegin(g.bssid, g.channel);
  else if (!g.distOn) csiStop();
}
static void actCalEmpty() { csiStartCal(Z_EMPTY); }
static void actCalNear()  { csiStartCal(Z_NEAR); }
static void actCalMid()   { csiStartCal(Z_MID); }
static void actCalFar()   { csiStartCal(Z_FAR); }
static void actResetStars() { g.stars = 0; starProgress = 0; settingsSave(); footerMsg("Stars reset"); }
static void actChangePin()  { enterPin(PIN_SET); }
static void actTestChime()  { audioChime(); }
static void onGain()        { audioSetGain(g.micGain); }
static void onBright()      { applyBrightness(); }
static void applyVad() {
  audioSetVad(0.80f - 0.05f * g.vadSens, g.noiseMargin);
  if (g.hisVoiceOnly && g.pitchHi > g.pitchLo) audioSetPitchBand(g.pitchLo, g.pitchHi);
  else audioSetPitchBand(0, 0);
}

static void txtVoice(char *b, size_t n) {
  if (audioSpeechActive()) snprintf(b, n, "VOICE  %.0f Hz  score %.2f", audioPitchHz(), audioVoicing());
  else                     snprintf(b, n, "-----        score %.2f", audioVoicing());
}
static void txtVoiceSetup(char *b, size_t n) {
  if (g.pitchHi <= 0) snprintf(b, n, "not done");
  else snprintf(b, n, "%d-%d Hz", g.pitchLo, g.pitchHi);
}
static void enterVoiceSetup();
static void actVoiceSetup() { enterVoiceSetup(); }
static void txtNoise(char *b, size_t n) {
  snprintf(b, n, "room %.0f dB   now %.0f dB", audioNoiseDbfs() + g.calOffset, audioLevelDbfs() + g.calOffset);
}

#define V(lbl, ptr, mn, mx, st, unit, cb) {R_VALUE, lbl, ptr, mn, mx, st, unit, nullptr, nullptr, nullptr, nullptr, cb}
#define T(lbl, ptr, cb)                   {R_TOGGLE, lbl, nullptr, 0, 0, 0, nullptr, ptr, nullptr, nullptr, nullptr, cb}
#define A(lbl, fn, btn, txt)              {R_ACTION, lbl, nullptr, 0, 0, 0, nullptr, nullptr, fn, btn, txt, nullptr}
#define RO(lbl, txt)                      {R_READOUT, lbl, nullptr, 0, 0, 0, nullptr, nullptr, nullptr, nullptr, txt, nullptr}

static Row pageSound[] = {
  RO("Level", txtLevel),
  V("Loud limit",  &g.loudDb,     40, 100, 1, "dB", nullptr),
  V("Warn margin", &g.warnMargin,  2,  15, 1, "dB", nullptr),
  V("Mic gain",    &g.micGain,     0,  42, 6, "dB", onGain),
  V("Mic cal",     &g.calOffset,  60, 140, 1, "dB", nullptr),
};
static Row pageDist[] = {
  T("Distance adjust", &g.distOn, onDistToggle),
  A("Router", actRouter, routerBtn, txtRouter),
  RO("Live", txtDistLive),
  V("Mid-room adj", &g.midOffset, -24, 0, 1, "dB", nullptr),
  V("Far adj",      &g.farOffset, -30, 0, 1, "dB", nullptr),
};
static Row pageCal[] = {
  A("Empty room", actCalEmpty, "Record", txtCalEmpty),
  A("Near (0.5m)", actCalNear, "Record", txtCalNear),
  A("Mid (1.5m)",  actCalMid,  "Record", txtCalMid),
  A("Far (3m)",    actCalFar,  "Record", txtCalFar),
  RO("Live", txtCalLive),
};
static Row pageReward[] = {
  V("Star every", &g.starEvery, 15, 300, 5, "s", nullptr),
  V("Daily goal", &g.dailyGoal,  3,  30, 1, "",  nullptr),
  T("Loud resets bar", &g.loudResets, nullptr),
  T("Chime sound",     &g.chime,      nullptr),
  T("Talk bubbles",    &g.bubbles,    nullptr),
  A("Stars", actResetStars, "Reset", nullptr),
};
static Row pageSys[] = {
  V("Voice floor", &g.floorDb,    20, 80, 1, "dB", nullptr),
  V("Brightness",  &g.brightness, 10, 100, 10, "%", onBright),
  T("LED glow",    &g.ledGlow, nullptr),
  V("Nap after",   &g.sleepAfter, 30, 900, 30, "s", nullptr),
  A("PIN", actChangePin, "Change", nullptr),
  V("Noise margin", &g.noiseMargin, 2, 20, 1, "dB", applyVad),
};

static Row pageMic[] = {
  RO("Voice", txtVoice),
  RO("Noise", txtNoise),
  A("Voice setup", actVoiceSetup, "Start", txtVoiceSetup),
  T("Only his voice", &g.hisVoiceOnly, applyVad),
  T("Speech only",    &g.speechOnly, nullptr),
  V("Speech sens",    &g.vadSens, 1, 10, 1, "", applyVad),
};

struct Page { const char *name; Row *rows; int n; };
static Page pages[] = {
  {"SOUND",  pageSound,  5},
  {"MIC",    pageMic,    6},
  {"DIST",   pageDist,   5},
  {"CALIB",  pageCal,    5},
  {"STARS",  pageReward, 6},
  {"SYS",    pageSys,    6},
};
#define PAGE_MIC 1
#define NPAGES 6
#define TAB_W  (320 / NPAGES)
#define PAGE_CALIB 3
static int curPage = 0;
static char footerText[48] = "";
static uint32_t footerMs = 0;

#define ROW_Y(i) (32 + (i) * 30)   // up to 6 rows per page
#define ROW_H 28
#define BTN_MINUS_X 196
#define BTN_PLUS_X 262
#define BTN_W 54
#define BTN_H 24

static void drawButton(int x, int y, int w, int h, const char *label, uint16_t bg, uint16_t fg) {
  tft.fillRoundRect(x, y, w, h, 6, bg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(fg, bg);
  tft.drawString(label, x + w / 2, y + h / 2 + 1, 2);
}

static void drawRowValue(int i) {
  Row &r = pages[curPage].rows[i];
  int y = ROW_Y(i), my = y + ROW_H / 2;
  char b[48] = "";
  tft.setTextColor(C_TEXT, C_SCREEN);
  switch (r.type) {
    case R_VALUE:
      snprintf(b, sizeof(b), "%d %s", *r.val, r.unit);
      tft.fillRect(120, y, 74, ROW_H, C_SCREEN);
      tft.setTextDatum(MR_DATUM);
      tft.drawString(b, 192, my, 2);
      break;
    case R_TOGGLE:
      drawButton(230, y + 2, 84, BTN_H, *r.flag ? "ON" : "OFF", *r.flag ? C_ON : C_OFF, C_BTN_TX);
      break;
    case R_ACTION:
      if (r.text) {
        r.text(b, sizeof(b));
        tft.fillRect(100, y, 108, ROW_H, C_SCREEN);
        tft.setTextDatum(MR_DATUM);
        tft.drawString(b, 206, my, 2);
      }
      break;
    case R_READOUT:
      r.text(b, sizeof(b));
      tft.fillRect(60, y, 258, ROW_H, C_SCREEN);
      tft.setTextDatum(ML_DATUM);
      tft.drawString(b, 62, my, 2);
      break;
  }
}

static void drawRow(int i) {
  Row &r = pages[curPage].rows[i];
  int y = ROW_Y(i), my = y + ROW_H / 2;
  tft.fillRect(0, y, 320, ROW_H, C_SCREEN);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(r.type == R_READOUT ? C_DIM : C_TEXT, C_SCREEN);
  tft.drawString(r.label, 6, my, 2);
  if (r.type == R_VALUE) {
    drawButton(BTN_MINUS_X, y + 2, BTN_W, BTN_H, "-", C_BTN, C_BTN_TX);
    drawButton(BTN_PLUS_X, y + 2, BTN_W, BTN_H, "+", C_BTN, C_BTN_TX);
  } else if (r.type == R_ACTION) {
    drawButton(214, y + 2, 100, BTN_H, r.btn, C_BTN, C_BTN_TX);
  }
  tft.drawFastHLine(4, y + ROW_H + 1, 312, C_PANEL);
  drawRowValue(i);
}

static void drawFooter() {
  tft.fillRect(0, 212, 236, 28, C_SCREEN);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(C_TEXT, C_SCREEN);
  // calibration gets a big countdown so it's readable from across the room
  if (csiCalBusy()) {
    int s = csiCalSecondsLeft();
    char b[32];
    if (s > 0) snprintf(b, sizeof(b), "Step away %d", s);
    else       snprintf(b, sizeof(b), "Recording %d", -s);
    tft.drawString(b, 6, 226, 4);
    return;
  }
  if (footerText[0] && millis() - footerMs < 4000) tft.drawString(footerText, 6, 226, 2);
  else if (curPage == PAGE_CALIB) {
    tft.setTextColor(C_DIM, C_SCREEN);
    tft.drawString("Empty first, then Near/Mid/Far", 6, 226, 2);
  }
}

static void footerMsg(const char *m) {
  strncpy(footerText, m, sizeof(footerText) - 1);
  footerMs = millis();
  if (screen == SCR_SETTINGS) drawFooter();
}

static void drawTabs() {
  for (int i = 0; i < NPAGES; i++) {
    int x = i * TAB_W;
    bool on = i == curPage;
    tft.fillRect(x, 0, TAB_W, 28, on ? C_TAB : C_PANEL);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(on ? C_BTN_TX : C_TEXT, on ? C_TAB : C_PANEL);
    tft.drawString(pages[i].name, x + TAB_W / 2, 14, 2);
  }
}

static void drawSettingsFull() {
  tft.fillScreen(C_SCREEN);
  drawTabs();
  for (int i = 0; i < pages[curPage].n; i++) drawRow(i);
  drawButton(240, 212, 76, 26, "DONE", C_ON, C_BTN_TX);
  drawFooter();
}

static void enterSettings(int tab) {
  screen = SCR_SETTINGS;
  curPage = tab;
  drawSettingsFull();
}

// returns true if the tap was a +/- (eligible for auto-repeat)
static bool settingsTap(int tx, int ty) {
  if (ty < 28) {
    int p = tx / TAB_W;
    if (p >= 0 && p < NPAGES && p != curPage) { curPage = p; drawSettingsFull(); }
    return false;
  }
  if (ty >= 210 && tx >= 236) {
    settingsSave();
    enterChild();
    return false;
  }
  for (int i = 0; i < pages[curPage].n; i++) {
    int y = ROW_Y(i);
    if (ty < y || ty >= y + ROW_H) continue;
    Row &r = pages[curPage].rows[i];
    switch (r.type) {
      case R_VALUE:
        if (tx >= BTN_MINUS_X && tx < BTN_MINUS_X + BTN_W) *r.val = max(r.mn, *r.val - r.step);
        else if (tx >= BTN_PLUS_X && tx < BTN_PLUS_X + BTN_W) *r.val = min(r.mx, *r.val + r.step);
        else return false;
        if (r.onChange) r.onChange();
        drawRowValue(i);
        return true;
      case R_TOGGLE:
        if (tx < 230) return false;
        *r.flag = !*r.flag;
        if (r.onChange) r.onChange();
        drawRowValue(i);
        return false;
      case R_ACTION:
        if (tx < 214) return false;
        r.act();
        if (screen == SCR_SETTINGS) drawRow(i);
        return false;
      default:
        return false;
    }
  }
  return false;
}

static void settingsRefresh() {
  for (int i = 0; i < pages[curPage].n; i++) {
    Row &r = pages[curPage].rows[i];
    if (r.text) drawRowValue(i);
  }
  drawFooter();
}

// ======================================================================
//  Voice setup wizard: learns his inside voice, excited voice and pitch range
// ======================================================================
#define VS_NEED     100     // voiced samples (~4 s of voice at 25 Hz)
#define VS_MIN      30      // accept a short take at timeout
#define VS_TIMEOUT  30000
enum VsStep { VS_INSIDE_INTRO, VS_INSIDE_REC, VS_EXCITED_INTRO, VS_EXCITED_REC, VS_RESULT };
static VsStep vsStep;
static float vsLvl[2][VS_NEED], vsPitch[2][VS_NEED * 2];
static int vsN[2], vsPN;
static uint32_t vsRecStart, vsLastSample;
static int vsNewLoud, vsNewFloor, vsNewLo, vsNewHi, vsInside, vsExcited;
static const char *vsNote = nullptr;

static void vsButton(int x, const char *label, uint16_t bg) { drawButton(x, 198, 100, 36, label, bg, C_BTN_TX); }

static void vsLines(const char *title, const char *l1, const char *l2, const char *l3) {
  tft.fillScreen(C_SCREEN);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT, C_SCREEN);
  tft.drawString(title, 160, 28, 4);
  tft.drawString(l1, 160, 70, 2);
  tft.drawString(l2, 160, 92, 2);
  tft.setTextColor(C_DIM, C_SCREEN);
  tft.drawString(l3, 160, 114, 2);
  if (vsNote) { tft.setTextColor(C_LOUD, C_SCREEN); tft.drawString(vsNote, 160, 140, 2); vsNote = nullptr; }
}

static float vsMedian(float *v, int n) {
  for (int i = 1; i < n; i++) { float k = v[i]; int j = i - 1; while (j >= 0 && v[j] > k) { v[j + 1] = v[j]; j--; } v[j + 1] = k; }
  return n ? v[n / 2] : 0;
}

static void vsDraw() {
  switch (vsStep) {
    case VS_INSIDE_INTRO:
      vsLines("Step 1: inside voice", "Ask him to talk normally, like", "telling you about his day.",
              "Device about 1 m away, room quiet.");
      vsButton(10, "Cancel", C_OFF);
      vsButton(210, "Start", C_ON);
      break;
    case VS_EXCITED_INTRO:
      vsLines("Step 2: excited voice", "Now his excited voice, like", "cheering for a favorite pup.",
              "Skip if he isn't up for it.");
      vsButton(10, "Skip", C_OFF);
      vsButton(210, "Start", C_ON);
      break;
    case VS_INSIDE_REC:
    case VS_EXCITED_REC:
      vsLines(vsStep == VS_INSIDE_REC ? "Listening: inside voice" : "Listening: excited voice",
              "Keep talking...", "", "");
      tft.drawRoundRect(40, 150, 240, 18, 9, C_DIM);
      vsButton(10, "Cancel", C_OFF);
      break;
    case VS_RESULT: {
      char a[48], b[48], c[48];
      if (vsExcited) snprintf(a, sizeof(a), "Inside %d dB   Excited %d dB", vsInside, vsExcited);
      else           snprintf(a, sizeof(a), "Inside voice %d dB", vsInside);
      snprintf(b, sizeof(b), "New loud limit: %d dB", vsNewLoud);
      snprintf(c, sizeof(c), "Listens for voices %d-%d Hz", vsNewLo, vsNewHi);
      vsLines("Voice learned", a, b, c);
      vsButton(10, "Redo", C_OFF);
      vsButton(210, "Save", C_ON);
      break;
    }
  }
}

static void vsFinish() {
  int nIn = vsN[0], nEx = vsN[1];
  vsInside  = (int)lroundf(vsMedian(vsLvl[0], nIn));
  vsExcited = nEx ? (int)lroundf(vsMedian(vsLvl[1], nEx)) : 0;
  // limit sits between his two voices; with no excited sample, 8 dB over the inside voice
  vsNewLoud  = constrain(vsExcited > vsInside + 4 ? (vsInside + vsExcited) / 2 : vsInside + 8, 40, 100);
  vsNewFloor = constrain(vsInside - 15, 20, 80);
  // pitch band: 10th..90th percentile, widened (excited shrieks go much higher)
  vsMedian(vsPitch[0], vsPN);   // sorts
  float p10 = vsPitch[0][vsPN / 10], p90 = vsPitch[0][vsPN * 9 / 10];
  vsNewLo = constrain((int)(p10 * 0.7f), 60, 1000);
  vsNewHi = constrain((int)(p90 * 1.8f), vsNewLo + 50, 2600);
  vsStep = VS_RESULT;
  Serial.printf("[voice-setup] inside %d excited %d -> limit %d floor %d, pitch %d-%d Hz (%d samples)\n",
                vsInside, vsExcited, vsNewLoud, vsNewFloor, vsNewLo, vsNewHi, vsPN);
}

static void vsStartRec(VsStep s) {
  vsStep = s;
  vsN[s == VS_EXCITED_REC] = 0;
  vsRecStart = millis();
  vsDraw();
}

static void enterVoiceSetup() {
  screen = SCR_VOICESETUP;
  audioSetPitchBand(0, 0);   // hear every voice while learning his
  vsPN = 0; vsN[0] = vsN[1] = 0;
  vsStep = VS_INSIDE_INTRO;
  vsDraw();
}

static void vsExit(bool save) {
  if (save) {
    g.loudDb = vsNewLoud;
    g.floorDb = vsNewFloor;
    g.pitchLo = vsNewLo;
    g.pitchHi = vsNewHi;
    g.insideDb = vsInside;
    g.excitedDb = vsExcited;
    g.hisVoiceOnly = true;
    settingsSave();
  }
  applyVad();
  enterSettings(PAGE_MIC);
}

static void vsTap(int tx, int ty) {
  bool left = ty >= 190 && tx < 120, right = ty >= 190 && tx >= 200;
  switch (vsStep) {
    case VS_INSIDE_INTRO:  if (left) vsExit(false); else if (right) vsStartRec(VS_INSIDE_REC); break;
    case VS_EXCITED_INTRO: if (left) vsFinish(), vsDraw(); else if (right) vsStartRec(VS_EXCITED_REC); break;
    case VS_INSIDE_REC:
    case VS_EXCITED_REC:   if (left) vsExit(false); break;
    case VS_RESULT:        if (left) enterVoiceSetup(); else if (right) vsExit(true); break;
  }
}

static void vsTick() {
  if (vsStep != VS_INSIDE_REC && vsStep != VS_EXCITED_REC) return;
  uint32_t now = millis();
  if (now - vsLastSample < 40) return;
  vsLastSample = now;
  int k = vsStep == VS_EXCITED_REC;

  if (audioSpeechActive() && vsN[k] < VS_NEED) {
    vsLvl[k][vsN[k]++] = audioLevelDbfs() + g.calOffset;
    if (vsPN < VS_NEED * 2 && audioPitchHz() > 0) vsPitch[0][vsPN++] = audioPitchHz();
  }

  // progress bar + live pitch
  int w = 236 * vsN[k] / VS_NEED;
  if (w > 0) tft.fillRoundRect(42, 152, w, 14, 7, C_GOOD);
  char b[32];
  snprintf(b, sizeof(b), audioSpeechActive() ? "  hearing voice  %.0f Hz  " : "      waiting for voice      ", audioPitchHz());
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT, C_SCREEN);
  tft.drawString(b, 160, 184, 2);

  bool done = vsN[k] >= VS_NEED;
  bool timeout = now - vsRecStart > VS_TIMEOUT;
  if (done || (timeout && vsN[k] >= VS_MIN)) {
    if (k == 0) vsStep = VS_EXCITED_INTRO; else vsFinish();
    vsDraw();
  } else if (timeout) {
    vsNote = "Didn't hear enough voice. Try again.";
    vsStep = k == 0 ? VS_INSIDE_INTRO : VS_EXCITED_INTRO;
    vsDraw();
  }
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

  if (g.distOn && g.channel > 0) csiBegin(g.bssid, g.channel);
  enterChild();
}

void loop() {
  static uint32_t lastFrame = millis();
  static bool wasDown = false;
  static uint32_t downMs = 0, lastRepeat = 0, lastRefresh = 0, lastLog = 0;
  static bool repeatable = false;
  static int downX = 0, downY = 0, lastX = 0, lastY = 0;

  csiTick();

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
      if (now - lastRefresh > 250) { lastRefresh = now; settingsRefresh(); }
      if (csiCalBusy()) setPixel(csiCalSecondsLeft() > 0 ? 30 : 0, csiCalSecondsLeft() > 0 ? 18 : 0,
                                 csiCalSecondsLeft() > 0 ? 0 : 30);
      else setPixel(0, 0, 0);
      lastFrame = now;
      break;

    case SCR_VOICESETUP:
      if (pressed) vsTap(tx, ty);
      if (screen == SCR_VOICESETUP) vsTick();
      setPixel(0, audioSpeechActive() ? 30 : 0, 0);   // green while it hears voice
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
    }
  }

  if (now - lastLog > 1000) {
    lastLog = now;
    extern volatile int16_t g_rawMinL, g_rawMaxL, g_rawMinR, g_rawMaxR;
    Serial.printf("[raw] L %d..%d  R %d..%d\n", g_rawMinL, g_rawMaxL, g_rawMinR, g_rawMaxR);
    Serial.printf("[lvl] %.1f dBFS (%.0f dB)  speech %.0f dB %s pitch %.2f  noise %.0f dB  mood %d  zone %s  csi %.3f/%dpps  stars %d prog %.0f/%d\n",
                  audioLevelDbfs(), audioLevelDbfs() + g.calOffset, audioSpeechDbfs() + g.calOffset,
                  audioSpeechActive() ? "VOICE" : "-", audioVoicing(), audioNoiseDbfs() + g.calOffset,
                  (int)mood, zoneName(csiZone()), csiScore(), csiPps(), g.stars, starProgress, g.starEvery);
  }
  delay(2);
}

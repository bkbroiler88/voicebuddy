// First-run setup: WiFi, room baseline, zone training, his/her voice (inside, excited,
// far away), grown-up voices, summary, live zone check. Parts can be re-run alone.

#include <Arduino.h>
#include <math.h>
#include "ui.h"
#include "settings.h"
#include "audio.h"
#include "csi.h"
#include "wifi_net.h"
#include "keyboard.h"

enum Step { S_WELCOME, S_WIFI_LIST, S_WIFI_PASS, S_WIFI_CONNECT, S_QUIET, S_ZONE, S_INSIDE, S_EXCITED,
            S_FARVOICE, S_GROWNUP, S_SUMMARY, S_CHECK };
enum Phase { P_INTRO, P_COUNT, P_REC, P_DONE };

#define SAMPLE_MS     40      // 25 Hz sampling of level / pitch
#define NEED_INSIDE   250     // voiced samples at 25 Hz = 10 s of actual voice
#define NEED_OTHER    150     // 6 s of voice
#define MIN_ACCEPT    75      // at a timeout, keep a take with at least 3 s of voice
#define VOICE_TIMEOUT 60000
#define QUIET_MS      15000
#define ZONE_MS       10000
#define COUNT_MS      3000

static WizardMode mode;
static Step step;
static Phase phase;
static uint32_t phaseStart, lastSample, lastLive;
static int zoneIdx;                    // 0 near, 1 mid, 2 far
static const Zone ZONES[3] = {Z_NEAR, Z_MID, Z_FAR};
static const char *const ZNAME[3] = {"Near", "Mid", "Far"};
static const char *note = nullptr;
static bool zonesTouched = false;

// WiFi
static ApInfo aps[6];
static int apCount = 0, apSel = 0;
static char pw[65];
static bool pwShow = false;

// measurements
static float roomLvl[400]; static int roomN;
static float inLvl[NEED_INSIDE]; static int inN;
static float exLvl[NEED_OTHER];  static int exN;
static float farLvl[NEED_OTHER]; static int farN;
static float kidHz[NEED_INSIDE + 2 * NEED_OTHER]; static int kidN;
static float parHz[3 * NEED_OTHER]; static int parN;    // pitch, kept across "Add another"
static float parLvl[NEED_OTHER]; static int parLvlN; // per take, only used for progress
static int quietDb = 0;

// results
static int rLoud, rFloor, rWarn, rFar, rLo, rHi, rIn, rEx, rKid10, rKid90, rPar90;
static bool rOverlap, rHaveVoice;

// ---------------------------------------------------------------- drawing helpers
static void title(const char *s, int y = 42) {
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT, C_SCREEN);
  int f = tft.textWidth(s, 4) <= 304 ? 4 : 2;
  tft.drawString(s, 160, y, f);
}

static int stepNumber(Step s) {
  switch (s) {
    case S_WIFI_LIST: case S_WIFI_PASS: case S_WIFI_CONNECT: return 1;
    case S_QUIET: return 2;
    case S_ZONE: return 3;
    case S_INSIDE: return 4;
    case S_EXCITED: case S_FARVOICE: return 5;
    case S_GROWNUP: return 6;
    case S_SUMMARY: return 7;
    default: return 0;
  }
}

static void frame(const char *t) {
  tft.fillScreen(C_SCREEN);
  int n = stepNumber(step);
  if (n && mode == WIZ_FULL) {
    for (int i = 0; i < 7; i++) tft.fillCircle(160 - 42 + i * 14, 12, i == n - 1 ? 4 : 3, i < n ? C_TEXT : C_PANEL);
    char b[8]; snprintf(b, sizeof(b), "%d/7", n);
    tft.setTextDatum(MR_DATUM); tft.setTextColor(C_DIM, C_SCREEN); tft.drawString(b, 312, 12, 2);
  }
  title(t);
}

static void line(const char *s, int y, uint16_t col = 0, int font = 2) {
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(col ? col : C_TEXT, C_SCREEN);
  tft.drawString(s, 160, y, font);
}

static void clearLine(int y, int h = 18) { tft.fillRect(0, y - h / 2, 320, h, C_SCREEN); }

static void buttons(const char *left, const char *right, uint16_t rcol = 0) {
  tft.fillRect(0, 196, 320, 44, C_SCREEN);
  if (left)  drawButton(10, 200, 100, 34, left, C_OFF, C_BTN_TX);
  if (right) drawButton(210, 200, 100, 34, right, rcol ? rcol : C_ON, C_BTN_TX);
}

static void bar(int y, float f, uint16_t col) {
  tft.drawRoundRect(40, y, 240, 14, 7, C_DIM);
  int w = (int)(236 * constrain(f, 0.f, 1.f));
  if (w > 6) tft.fillRoundRect(42, y + 2, w, 10, 5, col);
}

static void phrase(const char *s, int y) {
  tft.fillRoundRect(16, y, 288, 32, 10, TFT_WHITE);
  tft.drawRoundRect(16, y, 288, 32, 10, C_DIM);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT, TFT_WHITE);
  tft.drawString(s, 160, y + 17, 2);
}

// filled top half of a circle (a full circle would spill over the buttons below)
static void fillTopHalf(int cx, int cy, int r, uint16_t c) {
  for (int dy = 0; dy <= r; dy++) {
    int hw = (int)sqrtf((float)(r * r - dy * dy));
    tft.drawFastHLine(cx - hw, cy - dy, 2 * hw + 1, c);
  }
}

// top-down room: device at the bottom, three zone bands
static void room(int hi, int kidZone) {
  const int cx = 160, cy = 186;
  const int R[3] = {40, 75, 110};
  tft.fillRect(0, 70, 320, cy - 70 + 4, C_SCREEN);
  for (int i = 2; i >= 0; i--) {
    uint16_t c = (hi == i) ? rgb(207, 238, 216) : rgb(236, 233, 225);
    fillTopHalf(cx, cy, R[i], rgb(211, 207, 196));
    fillTopHalf(cx, cy, R[i] - 1, c);
  }
  tft.setTextDatum(MC_DATUM);
  for (int i = 0; i < 3; i++) {
    tft.setTextColor(C_DIM);
    tft.drawString(ZNAME[i], cx, cy - R[i] + 11, 1);
  }
  tft.fillRoundRect(cx - 9, cy - 6, 18, 12, 2, C_TEXT);
  if (kidZone >= 0) {
    const int kx[3] = {140, 112, 226}, ky[3] = {170, 134, 102};
    tft.fillCircle(kx[kidZone], ky[kidZone] - 9, 5, C_BTN);
    tft.fillRoundRect(kx[kidZone] - 5, ky[kidZone] - 4, 10, 11, 4, C_BTN);
  }
}

static void countdown(int secs) {
  tft.fillRect(110, 96, 100, 64, C_SCREEN);
  char b[4]; snprintf(b, sizeof(b), "%d", secs);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_BTN, C_SCREEN);
  tft.drawString(b, 160, 128, 6);
}

// ---------------------------------------------------------------- stats
static void sortf(float *v, int n) {
  for (int i = 1; i < n; i++) { float k = v[i]; int j = i - 1; while (j >= 0 && v[j] > k) { v[j + 1] = v[j]; j--; } v[j + 1] = k; }
}
static float pct(float *v, int n, float p) {   // sorts in place
  if (n <= 0) return 0;
  sortf(v, n);
  return v[constrain((int)(p * (n - 1) + 0.5f), 0, n - 1)];
}

// ---------------------------------------------------------------- flow
static void go(Step s);

static Step nextAfter(Step s) {
  switch (s) {
    case S_WELCOME:      return S_WIFI_LIST;
    case S_WIFI_LIST:    return S_WIFI_PASS;
    case S_WIFI_PASS:    return S_WIFI_CONNECT;
    case S_WIFI_CONNECT: return S_QUIET;
    case S_QUIET:        return (mode != WIZ_VOICE && csiRunning()) ? S_ZONE : S_INSIDE;
    case S_ZONE:         return S_INSIDE;   // zones advance themselves through zoneIdx
    case S_INSIDE:       return S_EXCITED;
    case S_EXCITED:      return S_FARVOICE;
    case S_FARVOICE:     return S_GROWNUP;
    case S_GROWNUP:      return S_SUMMARY;
    default:             return S_CHECK;
  }
}

static void finish() {
  applyVad();
  if (mode == WIZ_FULL) enterChild();
  else settingsEnter(mode == WIZ_WIFI || mode == WIZ_CHECK || mode == WIZ_ZONES ? TAB_ROOM : TAB_SETUP);
}

static void cancel() {
  if (zonesTouched) csiModelLoad();   // throw away half-trained zones
  if (csiTraining()) csiTrainEnd();
  finish();
}

static void advance() {
  if (step == S_WIFI_CONNECT && mode == WIZ_WIFI) { finish(); return; }
  if (step == S_ZONE && mode == WIZ_ZONES && zoneIdx >= 2) { go(S_CHECK); return; }
  if (step == S_QUIET && mode == WIZ_ZONES && !csiRunning()) { finish(); return; }
  go(nextAfter(step));
}

// ---------------------------------------------------------------- results
static void computeResults() {
  rHaveVoice = inN >= MIN_ACCEPT;
  float in50 = pct(inLvl, inN, 0.5f), in90 = pct(inLvl, inN, 0.9f);
  float exMed = pct(exLvl, exN, 0.5f), far90 = pct(farLvl, farN, 0.9f);
  if (rHaveVoice) {
    float loud = (exN >= MIN_ACCEPT) ? in90 + fmaxf(3.f, 0.6f * (exMed - in90)) : in90 + 6.f;
    rLoud  = constrain((int)lroundf(loud), 45, 100);
    rFloor = constrain((int)lroundf(in50 - 12.f), 25, 80);
    rWarn  = constrain((int)lroundf(fminf(6.f, rLoud - in90)), 2, 15);
    rIn    = (int)lroundf(in90);
    rEx    = exN >= MIN_ACCEPT ? (int)lroundf(exMed) : 0;
    rFar   = farN >= MIN_ACCEPT ? constrain((int)lroundf(far90 - in90), -20, -1) : g.farOffset;
  } else {
    rLoud = g.loudDb; rFloor = g.floorDb; rWarn = g.warnMargin; rFar = g.farOffset; rIn = g.insideDb; rEx = g.excitedDb;
  }
  rKid10 = kidN >= MIN_ACCEPT ? (int)pct(kidHz, kidN, 0.1f) : g.childP10;
  rKid90 = kidN >= MIN_ACCEPT ? (int)pct(kidHz, kidN, 0.9f) : g.childP90;
  int newPar = parN >= MIN_ACCEPT ? (int)pct(parHz, parN, 0.9f) : 0;
  rPar90 = (mode == WIZ_GROWNUP) ? max(g.parentP90, newPar) : (newPar ? newPar : (mode == WIZ_VOICE ? g.parentP90 : 0));
  rLo = (int)(rKid10 * 0.85f);
  rHi = (int)(rKid90 * 1.35f);
  rOverlap = false;
  if (rPar90 > 0 && rKid10 > 0) {
    if (rPar90 < rKid10) rLo = max(rLo, (rPar90 + rKid10) / 2);
    else rOverlap = true;
  }
}

static void saveResults() {
  g.loudDb = rLoud; g.floorDb = rFloor; g.warnMargin = rWarn;
  g.farOffset = rFar; g.midOffset = rFar / 2;
  g.insideDb = rIn; g.excitedDb = rEx;
  if (rKid10 > 0) {
    g.childP10 = rKid10; g.childP90 = rKid90;
    g.pitchLo = rLo; g.pitchHi = rHi;
    g.hisVoiceOnly = !rOverlap;
  }
  g.parentP90 = rPar90;
  if (parN >= MIN_ACCEPT) g.nParents = (mode == WIZ_GROWNUP ? g.nParents : 0) + 1;
  if (quietDb) g.roomNoiseDb = quietDb;
  if (zonesTouched) csiModelSave();
  g.distOn = csiModelReady();
  if (mode == WIZ_FULL) g.setupDone = true;
  settingsSave();
  applyVad();
}

// ---------------------------------------------------------------- screens
static void drawStep() {
  switch (step) {
    case S_WELCOME:
      frame("Let's set up Voice Buddy");
      line("About 3 minutes. You'll need:", 72);
      {
        const char *items[3] = {"Your WiFi password", "A quiet moment in the room", "Your child, and a grown-up"};
        for (int i = 0; i < 3; i++) {
          tft.fillCircle(62, 98 + i * 22, 4, C_GOOD);
          tft.setTextDatum(ML_DATUM); tft.setTextColor(C_TEXT, C_SCREEN);
          tft.drawString(items[i], 74, 98 + i * 22, 2);
        }
      }
      line("Put Buddy where it will stay.", 176, C_DIM);
      buttons("Later", "Start");
      break;

    case S_WIFI_LIST:
      frame("Connect to WiFi");
      line("Searching for networks...", 110, C_DIM);
      apCount = netScan(aps, 5);
      apSel = 0;
      if (netHasCreds()) for (int i = 0; i < apCount; i++) if (!strcmp(aps[i].ssid, netSsid())) apSel = i;
      tft.fillRect(0, 56, 320, 140, C_SCREEN);
      line("Buddy uses WiFi to sense where your child is.", 62, C_DIM);
      for (int i = 0; i < apCount; i++) {
        int y = 74 + i * 24;
        bool sel = i == apSel;
        tft.fillRoundRect(20, y, 280, 21, 6, sel ? rgb(223, 231, 242) : TFT_WHITE);
        tft.drawRoundRect(20, y, 280, 21, 6, sel ? C_BTN : C_PANEL);
        tft.setTextDatum(ML_DATUM); tft.setTextColor(C_TEXT, sel ? rgb(223, 231, 242) : TFT_WHITE);
        char b[48];
        bool cur = netConnected() && !strcmp(aps[i].ssid, netSsid());
        snprintf(b, sizeof(b), "%s%s", aps[i].ssid, cur ? "  (connected)" : "");
        tft.drawString(b, 30, y + 11, 2);
        int bars = aps[i].rssi > -55 ? 4 : aps[i].rssi > -65 ? 3 : aps[i].rssi > -75 ? 2 : 1;
        for (int k = 0; k < 4; k++) tft.fillRect(268 + k * 6, y + 17 - (k + 1) * 3, 4, (k + 1) * 3, k < bars ? C_TEXT : C_PANEL);
      }
      if (!apCount) line("No networks found. Tap Next to search again.", 120, C_LOUD);
      line("Only 2.4 GHz networks show up.", 192 - 6, C_DIM);
      buttons(mode == WIZ_FULL ? "Skip WiFi" : "Back", "Next");
      break;

    case S_WIFI_PASS: {
      char t[48]; snprintf(t, sizeof(t), "%.20s password", aps[apSel].ssid);
      frame(t);
      kbReset();
      kbDraw();
      drawButton(4, 2, 40, 22, "<", C_PANEL, C_TEXT);
    }
    // fallthrough to draw the field
    {
      tft.fillRect(0, 54, 320, 28, C_SCREEN);
      tft.fillRoundRect(20, 54, 236, 26, 6, TFT_WHITE);
      tft.drawRoundRect(20, 54, 236, 26, 6, note ? C_LOUD : C_DIM);
      char shown[66];
      size_t n = strlen(pw);
      if (pwShow) strcpy(shown, pw); else { memset(shown, '*', n); shown[n] = 0; }
      tft.setTextDatum(ML_DATUM); tft.setTextColor(C_TEXT, TFT_WHITE);
      tft.drawString(n > 26 ? shown + (n - 26) : shown, 28, 67, 2);
      drawButton(262, 55, 48, 24, pwShow ? "hide" : "show", C_PANEL, C_TEXT);
      if (note) { tft.fillRect(0, 214, 320, 26, C_SCREEN); line(note, 228, C_LOUD); }
    }
      break;

    case S_WIFI_CONNECT: {
      frame("Connecting...");
      char b[48]; snprintf(b, sizeof(b), "Joining %s", netSsid());
      line(b, 110);
      line("This takes up to 15 seconds.", 134, C_DIM);
      break;
    }

    case S_QUIET:
      frame("Shh... learning the room");
      line("Everyone stay quiet. Leave the TV, fans", 70);
      line("and lights the way they normally are.", 88);
      if (phase == P_INTRO) { line("Tap Start when the room is ready.", 150, C_DIM); buttons("Cancel", "Start"); }
      else buttons("Cancel", nullptr);
      break;

    case S_ZONE: {
      const char *titles[3] = {"Play near Buddy", "Middle of the room", "Far side of the room"};
      frame(titles[zoneIdx]);
      line("Your child plays in the green area for 10 s.", 60, C_DIM);
      room(zoneIdx, zoneIdx);
      if (phase == P_INTRO) buttons("Cancel", "Start");
      else buttons("Cancel", nullptr);
      break;
    }

    case S_INSIDE:
      frame("Inside voice");
      line("From the Near spot, have your child say:", 64, C_DIM);
      phrase("\"Hi Buddy, I'm using my inside voice!\"", 74);
      buttons("Cancel", nullptr);
      break;

    case S_EXCITED:
      frame("Excited voice");
      line("Same spot. Now say it like cheering!", 64, C_DIM);
      phrase("\"Hi Buddy, I'm SO excited!\"", 74);
      buttons("Skip", nullptr);
      break;

    case S_FARVOICE:
      frame("Inside voice, far away");
      line("From the far side of the room, say:", 64, C_DIM);
      phrase("\"Hi Buddy, I'm using my inside voice!\"", 74);
      buttons("Skip", nullptr);
      break;

    case S_GROWNUP:
      frame("Now a grown-up");
      line("A parent says the phrase, so Buddy can", 62, C_DIM);
      line("tell your voices apart.", 78, C_DIM);
      phrase("\"Hi Buddy, I'm a grown-up!\"", 88);
      if (phase == P_DONE) {
        line("Got it. Add another grown-up?", 150, C_GOOD);
        buttons("Add another", "Done");
      } else buttons("Skip", nullptr);
      break;

    case S_SUMMARY: {
      frame(mode == WIZ_GROWNUP ? "Grown-up voice saved?" : "All set!");
      char a[48];
      tft.setTextDatum(ML_DATUM); tft.setTextColor(C_TEXT, C_SCREEN);
      tft.drawString("Too-loud line", 16, 66, 2);
      tft.drawString("Room background", 16, 86, 2);
      tft.drawString("Voice pitch", 16, 106, 2);
      tft.setTextDatum(MR_DATUM);
      snprintf(a, sizeof(a), "%d dB near, %d dB far", rLoud, rLoud + rFar);
      tft.drawString(a, 304, 66, 2);
      snprintf(a, sizeof(a), "%d dB", quietDb ? quietDb : g.roomNoiseDb);
      tft.drawString(a, 304, 86, 2);
      // pitch ranges on a log scale 80..1000 Hz
      auto hx = [](float f) { return 40 + (int)((logf(fmaxf(f, 80)) - logf(80)) / (logf(1000) - logf(80)) * 240); };
      tft.fillRoundRect(40, 116, 240, 10, 5, C_PANEL);
      if (rPar90 > 0) {
        int x0 = hx(rPar90 * 0.6f), x1 = hx(rPar90);
        tft.fillRoundRect(x0, 116, max(8, x1 - x0), 10, 5, C_BTN);
        tft.setTextDatum(MC_DATUM); tft.setTextColor(C_BTN, C_SCREEN); tft.drawString("grown-up", (x0 + x1) / 2, 136, 1);
      }
      if (rKid10 > 0) {
        int x0 = hx(rKid10), x1 = hx(rKid90);
        tft.fillRoundRect(x0, 116, max(8, x1 - x0), 10, 5, C_GOOD);
        tft.setTextDatum(MC_DATUM); tft.setTextColor(C_GOOD, C_SCREEN); tft.drawString("your child", (x0 + x1) / 2, 146, 1);
      }
      if (rKid10 <= 0)      line("No child voice recorded yet", 160, C_DIM);
      else if (rOverlap)    line("Voices overlap: grown-ups may still count", 160, C_LOUD);
      else if (rPar90 > 0)  line("Voices are easy to tell apart", 160, C_GOOD);
      else                  line("No grown-up voice recorded", 160, C_DIM);
      if (mode == WIZ_FULL || mode == WIZ_ZONES)
        line(csiModelReady() ? "Zones: Near / Mid / Far learned" : "Zones: skipped (no WiFi)", 178, C_DIM);
      if (!rHaveVoice && mode != WIZ_GROWNUP) line("Not enough voice heard: limits unchanged", 178, C_LOUD);
      buttons("Redo", "Save");
      break;
    }

    case S_CHECK:
      frame("Check the zones");
      line(csiRunning() ? "Walk around: the green area should follow." : "WiFi isn't connected, so zones are off.", 60, C_DIM);
      room(-1, -1);
      buttons(nullptr, "Done");
      lastLive = 0;
      break;
  }
}

static void go(Step s) {
  step = s;
  phase = (s == S_QUIET || s == S_ZONE) ? P_INTRO : P_REC;
  phaseStart = millis();
  if (s == S_INSIDE) { inN = 0; }
  if (s == S_EXCITED) exN = 0;
  if (s == S_FARVOICE) farN = 0;
  if (s == S_GROWNUP) parLvlN = 0;   // parHz keeps earlier grown-ups
  if (s == S_SUMMARY) computeResults();
  drawStep();
  if (s == S_WIFI_CONNECT) {
    // blocking join: the screen already says what is happening
    bool ok = netConnectBlocking(15000);
    if (ok) { csiStart(); note = nullptr; advance(); }
    else { note = "Couldn't connect. Check the password."; go(S_WIFI_PASS); }
  }
}

void wizardEnter(WizardMode m) {
  mode = m;
  screen = SCR_WIZARD;
  note = nullptr;
  zonesTouched = false;
  roomN = inN = exN = farN = kidN = parN = 0;
  quietDb = 0;
  zoneIdx = 0;
  audioSetPitchBand(0, 0);    // hear every voice while learning
  switch (m) {
    case WIZ_FULL:    go(S_WELCOME); break;
    case WIZ_VOICE:   go(S_INSIDE); break;
    case WIZ_ZONES:   go(S_QUIET); break;
    case WIZ_WIFI:    go(S_WIFI_LIST); break;
    case WIZ_GROWNUP: go(S_GROWNUP); break;
    case WIZ_CHECK:   go(S_CHECK); break;
  }
}

// ---------------------------------------------------------------- input
void wizardTap(int x, int y) {
  bool left = y >= 196 && x < 120, right = y >= 196 && x >= 200;
  switch (step) {
    case S_WELCOME:
      if (left) { finish(); }
      else if (right) go(S_WIFI_LIST);
      break;

    case S_WIFI_LIST:
      if (y >= 74 && y < 74 + apCount * 24 && x >= 20 && x <= 300) { apSel = (y - 74) / 24; drawStep(); return; }
      if (left) { if (mode == WIZ_FULL) go(S_QUIET); else finish(); }
      else if (right) {
        if (!apCount) { drawStep(); return; }
        bool cur = netConnected() && !strcmp(aps[apSel].ssid, netSsid());
        if (cur) {                                   // already on this network
          if (!csiRunning()) csiStart();
          if (mode == WIZ_WIFI) finish(); else go(S_QUIET);
          return;
        }
        pw[0] = 0; pwShow = false; note = nullptr;
        if (!aps[apSel].secure) { netSetCreds(aps[apSel].ssid, ""); go(S_WIFI_CONNECT); }
        else go(S_WIFI_PASS);
      }
      break;

    case S_WIFI_PASS: {
      if (x < 48 && y < 28) { go(S_WIFI_LIST); return; }
      if (y >= 54 && y <= 80 && x >= 262) { pwShow = !pwShow; drawStep(); return; }
      KbResult r = kbTap(x, y, pw, sizeof(pw));
      if (r == KB_OK) {
        if (strlen(pw) < 8) { note = "WiFi passwords are at least 8 characters."; }
        else { netSetCreds(aps[apSel].ssid, pw); note = nullptr; go(S_WIFI_CONNECT); return; }
      } else if (r == KB_CHANGED) note = nullptr;
      // redraw just the field (and note) without redrawing the keyboard
      tft.fillRect(0, 54, 320, 28, C_SCREEN);
      tft.fillRoundRect(20, 54, 236, 26, 6, TFT_WHITE);
      tft.drawRoundRect(20, 54, 236, 26, 6, note ? C_LOUD : C_DIM);
      {
        char shown[66]; size_t n = strlen(pw);
        if (pwShow) strcpy(shown, pw); else { memset(shown, '*', n); shown[n] = 0; }
        tft.setTextDatum(ML_DATUM); tft.setTextColor(C_TEXT, TFT_WHITE);
        tft.drawString(n > 26 ? shown + (n - 26) : shown, 28, 67, 2);
      }
      drawButton(262, 55, 48, 24, pwShow ? "hide" : "show", C_PANEL, C_TEXT);
      tft.fillRect(0, 216, 320, 24, C_SCREEN);
      if (note) line(note, 228, C_LOUD);
      break;
    }

    case S_QUIET:
    case S_ZONE:
      if (left) { cancel(); return; }
      if (right && phase == P_INTRO) { phase = P_COUNT; phaseStart = millis(); buttons("Cancel", nullptr); }
      break;

    case S_INSIDE:
      if (left) cancel();
      break;
    case S_EXCITED:
    case S_FARVOICE:
      if (left) advance();     // skip
      break;

    case S_GROWNUP:
      if (phase == P_DONE) {
        if (left) { phase = P_REC; phaseStart = millis(); parLvlN = 0; drawStep(); }
        else if (right) go(S_SUMMARY);
      } else if (left) go(S_SUMMARY);
      break;

    case S_SUMMARY:
      if (left) wizardEnter(mode == WIZ_GROWNUP ? WIZ_GROWNUP : mode);
      else if (right) {
        saveResults();
        if ((mode == WIZ_FULL || mode == WIZ_ZONES) && csiModelReady()) go(S_CHECK);
        else finish();
      }
      break;

    case S_CHECK:
      if (right) finish();
      break;

    default: break;
  }
}

// ---------------------------------------------------------------- tick
static void voiceTick(float *lvl, int &n, int need, int barY, uint16_t col, bool kid) {
  uint32_t now = millis();
  if (now - lastSample < SAMPLE_MS) return;
  lastSample = now;
  bool v = audioSpeechActive();
  if (v && n < need) {
    lvl[n++] = audioLevelDbfs() + g.calOffset;
    float hz = audioPitchHz();
    if (hz > 0) {
      if (kid && kidN < (int)(sizeof(kidHz) / sizeof(kidHz[0]))) kidHz[kidN++] = hz;
      if (!kid && parN < (int)(sizeof(parHz) / sizeof(parHz[0]))) parHz[parN++] = hz;
    }
  }
  setPixel(0, v ? 30 : 0, 0);
  bar(barY, (float)n / need, col);
  char b[40];
  int got = n * SAMPLE_MS / 1000, want = need * SAMPLE_MS / 1000;
  if (v) snprintf(b, sizeof(b), "hearing voice   %d of %d s", got, want);
  else   snprintf(b, sizeof(b), "keep talking...   %d of %d s", got, want);
  clearLine(barY - 16);
  line(b, barY - 16, v ? col : C_DIM);

  bool done = n >= need, timeout = now - phaseStart > VOICE_TIMEOUT;
  if (done || (timeout && n >= MIN_ACCEPT)) {
    if (step == S_GROWNUP) { phase = P_DONE; drawStep(); }
    else advance();
  } else if (timeout) {
    note = "Didn't hear enough voice. Let's try again.";
    n = 0;
    phaseStart = now;
    clearLine(barY - 40);
    line(note, barY - 40, C_LOUD);
  }
}

#define VOICE_BAR_Y 172

void wizardTick() {
  uint32_t now = millis();
  switch (step) {
    case S_QUIET:
    case S_ZONE: {
      if (phase == P_COUNT) {
        int left = (COUNT_MS - (int)(now - phaseStart) + 999) / 1000;
        static int lastShown = -1;
        if (left != lastShown) {
          lastShown = left;
          if (step == S_QUIET) countdown(left);
          else { clearLine(60, 16); char b[32]; snprintf(b, sizeof(b), "Starting in %d...", left); line(b, 60, C_BTN); }
        }
        if (now - phaseStart >= COUNT_MS) {
          phase = P_REC; phaseStart = now; lastShown = -1;
          if (step == S_QUIET) { roomN = 0; if (csiRunning()) { csiTrainBegin(Z_EMPTY); zonesTouched = true; } tft.fillRect(100, 92, 120, 72, C_SCREEN); }
          else { csiTrainBegin(ZONES[zoneIdx]); zonesTouched = true; }
        }
        break;
      }
      if (phase != P_REC) break;
      uint32_t dur = step == S_QUIET ? QUIET_MS : ZONE_MS;
      float f = (float)(now - phaseStart) / dur;
      if (now - lastSample >= SAMPLE_MS) {
        lastSample = now;
        if (step == S_QUIET) {
          if (roomN < 400) roomLvl[roomN++] = audioLevelDbfs() + g.calOffset;
          // progress ring (TFT_eSPI arcs: 0 = 6 o'clock, clockwise, so 180 = top) + readouts
          tft.fillRect(100, 92, 120, 72, C_SCREEN);
          tft.drawArc(160, 128, 34, 26, 0, 360, C_PANEL, C_SCREEN, false);
          int sweep = (int)(359 * fminf(f, 1.f));
          if (sweep > 0) tft.drawArc(160, 128, 34, 26, 180, (180 + sweep) % 360, C_GOOD, C_SCREEN, true);
          char b[8]; snprintf(b, sizeof(b), "%d s", (int)ceilf((dur - (now - phaseStart)) / 1000.f));
          tft.setTextDatum(MC_DATUM); tft.setTextColor(C_TEXT, C_SCREEN); tft.drawString(b, 160, 128, 4);
          char r[48]; snprintf(r, sizeof(r), "Room sound: %.0f dB    WiFi: %d pkt/s", audioLevelDbfs() + g.calOffset, csiPps());
          clearLine(180); line(r, 180, C_DIM);
        } else {
          static int lastSec = -1;
          int secLeft = (int)ceilf((dur - (now - phaseStart)) / 1000.f);
          if (secLeft != lastSec) {
            lastSec = secLeft;
            char b[40]; snprintf(b, sizeof(b), "Learning this spot... %d s", secLeft);
            clearLine(60, 16); line(b, 60, C_BTN);
          }
        }
        setPixel(0, 0, 30);
      }
      if (now - phaseStart >= dur) {
        if (step == S_QUIET) {
          quietDb = roomN ? (int)lroundf(pct(roomLvl, roomN, 0.5f)) : 0;
          if (csiTraining()) csiTrainEnd();
          advance();
        } else {
          csiTrainEnd();
          if (zoneIdx < 2) { zoneIdx++; go(S_ZONE); }
          else advance();
        }
      }
      break;
    }

    case S_INSIDE:   voiceTick(inLvl, inN, NEED_INSIDE, VOICE_BAR_Y, C_GOOD, true); break;
    case S_EXCITED:  voiceTick(exLvl, exN, NEED_OTHER, VOICE_BAR_Y, C_WARN, true); break;
    case S_FARVOICE: {
      voiceTick(farLvl, farN, NEED_OTHER, VOICE_BAR_Y, C_GOOD, true);
      if (step == S_FARVOICE && now - lastLive > 500) {
        lastLive = now;
        char b[40];
        Zone z = csiLiveZone();
        if (csiRunning() && z != Z_UNKNOWN) snprintf(b, sizeof(b), "Detected zone: %s (%d%% sure)", zoneName(z), (int)(csiLiveConfidence() * 100));
        else snprintf(b, sizeof(b), " ");
        clearLine(118, 14); line(b, 118, C_DIM);
      }
      break;
    }
    case S_GROWNUP:
      if (phase == P_REC) voiceTick(parLvl, parLvlN, NEED_OTHER, VOICE_BAR_Y, C_BTN, false);
      break;

    case S_CHECK:
      if (now - lastLive > 400) {
        lastLive = now;
        Zone z = csiLiveZone();
        int idx = z == Z_NEAR ? 0 : z == Z_MID ? 1 : z == Z_FAR ? 2 : -1;
        static int lastIdx = -2;
        if (idx != lastIdx) { lastIdx = idx; room(idx, idx); }
        char b[48];
        if (!csiRunning()) snprintf(b, sizeof(b), "No WiFi connection");
        else if (z == Z_UNKNOWN) snprintf(b, sizeof(b), "Not sure where   %d pkt/s", csiPps());
        else snprintf(b, sizeof(b), "%s   %d%% sure   %d pkt/s", z == Z_EMPTY ? "NOBODY" : zoneName(z), (int)(csiLiveConfidence() * 100), csiPps());
        tft.fillRect(0, 196, 205, 44, C_SCREEN);
        tft.setTextDatum(ML_DATUM); tft.setTextColor(C_TEXT, C_SCREEN);
        tft.drawString(b, 10, 217, 2);
      }
      break;

    default: break;
  }
}

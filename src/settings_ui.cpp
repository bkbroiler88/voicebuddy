// Parent settings: five tabs of plain-language rows (label + one-line explanation).

#include <Arduino.h>
#include "ui.h"
#include "settings.h"
#include "audio.h"
#include "csi.h"
#include "wifi_net.h"
#include "sdcard.h"
#include "version.h"
#include <esp_ota_ops.h>
#include "web.h"
#include "certs.h"
#include <WiFi.h>
#include "qrcode.h"   // ESP-IDF QR encoder

typedef void (*TextFn)(char *, size_t);
typedef void (*ActFn)();
typedef void (*FmtFn)(int, char *, size_t);

enum Kind : uint8_t { K_PM, K_TOG, K_ACT, K_INFO };   // K_INFO: read-only text on the right (btnFn)
struct SRow {
  Kind kind;
  const char *label;
  const char *sub;  TextFn subFn;           // static or live explanation line
  int *val; int mn, mx, step; FmtFn fmt;    // K_PM
  bool *flag;                               // K_TOG
  const char *btn; TextFn btnFn; ActFn act; // K_ACT
  ActFn onChange;
};

#define PM(l, s, sf, v, mn, mx, st, f, cb) {K_PM, l, s, sf, v, mn, mx, st, f, nullptr, nullptr, nullptr, nullptr, cb}
#define TOG(l, s, sf, p, cb)               {K_TOG, l, s, sf, nullptr, 0, 0, 0, nullptr, p, nullptr, nullptr, nullptr, cb}
#define ACT(l, s, sf, b, bf, a)            {K_ACT, l, s, sf, nullptr, 0, 0, 0, nullptr, nullptr, b, bf, a, nullptr}
#define INFO(l, s, sf, bf)                 {K_INFO, l, s, sf, nullptr, 0, 0, 0, nullptr, nullptr, nullptr, bf, nullptr, nullptr}

static int curTab = 0;
static char toast[48] = "";
static uint32_t toastMs = 0;
static int napMin;                          // g.sleepAfter shown in minutes
static bool qrMode = false;                 // full-screen QR code over the web page
static bool resetMode = false;              // factory reset confirmation

static void aFactory();
static void say(const char *m) { strncpy(toast, m, sizeof(toast) - 1); toastMs = millis(); }

// ---------------- formatters / live text ----------------
static void fDb(int v, char *b, size_t n)   { snprintf(b, n, "%d dB", v); }
static void fSec(int v, char *b, size_t n)  { snprintf(b, n, "%d s", v); }
static void fNum(int v, char *b, size_t n)  { snprintf(b, n, "%d", v); }
static void fPct(int v, char *b, size_t n)  { snprintf(b, n, "%d%%", v); }
static void fMin(int v, char *b, size_t n)  { snprintf(b, n, "%d min", v); }

static void subWhere(char *b, size_t n) {
  if (!csiRunning()) { snprintf(b, n, "WiFi not connected"); return; }
  if (!csiModelReady()) { snprintf(b, n, "Room zones not set up yet"); return; }
  Zone z = csiZone();
  if (z == Z_UNKNOWN) snprintf(b, n, "Live: not sure");
  else snprintf(b, n, "Live: %s, %d%% sure", z == Z_EMPTY ? "nobody" : zoneName(z), (int)(csiConfidence() * 100));
}
static void subWifi(char *b, size_t n) {
  if (!netHasCreds()) snprintf(b, n, "Not set up");
  else if (!netConnected()) snprintf(b, n, "%.18s, not connected", netSsid());
  else snprintf(b, n, "%.18s, %d pkt/s", netSsid(), csiPps());
}
static void subVoiceOnly(char *b, size_t n) {
  if (g.pitchHi <= 0) snprintf(b, n, "Run his/her voice setup first");
  else snprintf(b, n, "Ignores grown-ups (%d-%d Hz)", g.pitchLo, g.pitchHi);
}
// subtitle is ~30 characters wide: live reading + the reminder to redo the voice after changing it
static void subCal(char *b, size_t n) { snprintf(b, n, "Hears %.0f dB. Then redo voice", audioLevelDbfs() + g.calOffset); }
static void subParents(char *b, size_t n) {
  if (g.nParents <= 0) snprintf(b, n, "None saved yet");
  else snprintf(b, n, "%d saved", g.nParents);
}
static void subSetup(char *b, size_t n) { snprintf(b, n, g.setupDone ? "Room, zones and voices (3 min)" : "Not finished yet (3 min)"); }

static void subWifiWeb(char *b, size_t n) {
  if (!netHasCreds()) snprintf(b, n, "Not set up");
  else snprintf(b, n, "%.16s - web %s", netSsid(), g.webOn ? (webRunning() ? "on" : "starting") : "off");
}
static void subWebOn(char *b, size_t n) {
  if (!g.webOn) snprintf(b, n, "Off: nothing listens on the network");
  else if (!netConnected()) snprintf(b, n, "On, waiting for WiFi");
  else snprintf(b, n, "Parents can use a phone on this WiFi");
}
static void subAddr(char *b, size_t n) { snprintf(b, n, webRunning() ? "https://voicebuddy.local" : "-"); }
static void btnIp(char *b, size_t n) {
  if (webRunning()) snprintf(b, n, "%s", WiFi.localIP().toString().c_str()); else b[0] = 0;
}
static void subQr(char *b, size_t n) { snprintf(b, n, webRunning() ? "QR code + certificate check" : "Turn web control on first"); }
static void subSess(char *b, size_t n) {
  int k = webSessionCount();
  if (k == 0) snprintf(b, n, "None"); else snprintf(b, n, "%d signed in", k);
}

static void subSd(char *b, size_t n) { sdStatus(b, n); }
static void subFw(char *b, size_t n) { snprintf(b, n, "Running from %s", esp_ota_get_running_partition()->label); }
static void btnFw(char *b, size_t n) { snprintf(b, n, "%s", FW_VERSION); }
static void subMem(char *b, size_t n) { snprintf(b, n, "%u KB RAM, %u KB PSRAM free", ESP.getFreeHeap() / 1024, ESP.getFreePsram() / 1024); }
static void aSdCheck() { say(sdBegin() ? "Card found" : "No card found"); }
static void aSpeechTest() { speechTestEnter(); }

static const char *const VIEWS[4] = {"Pup", "Dial", "Saturn", "Planets"};
static void btnView(char *b, size_t n) { snprintf(b, n, "%s", VIEWS[constrain(g.view, 0, 3)]); }

// ---------------- actions ----------------
static void aCheck()      { wizardEnter(WIZ_CHECK); }
static void aWifi()       { wizardEnter(WIZ_WIFI); }
static void aResetStars() { g.stars = 0; resetStarProgress(); settingsSave(); say("Stars reset to 0"); }
static void aView()       { g.view = (g.view + 1) % 4; }
static void aPin()        { enterPinSet(); }
static void aFull()       { wizardEnter(WIZ_FULL); }
static void aVoice()      { wizardEnter(WIZ_VOICE); }
static void aZones() {
  if (!netConnected()) { say("Connect WiFi first (Room tab)"); return; }
  if (!csiRunning()) csiStart();
  wizardEnter(WIZ_ZONES);
}
static void aParent() {
  if (g.childP10 <= 0) { say("Do his/her voice setup first"); return; }
  wizardEnter(WIZ_GROWNUP);
}
static void drawAll();
static void aWebPage() { settingsEnter(TAB_WEB); }

static void drawResetConfirm() {
  tft.fillScreen(C_SCREEN);
  tft.fillRect(0, 0, 320, 28, C_LOUD);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_BTN_TX, C_LOUD);
  tft.drawString("Factory reset", 160, 14, 2);
  tft.setTextColor(C_TEXT, C_SCREEN);
  tft.drawString("Erase everything?", 160, 54, 4);
  const char *lines[5] = {"Settings, PIN and stars", "WiFi and room zones", "His/her voice and grown-up voices",
                          "History and signed-in browsers", "The web certificate"};
  tft.setTextDatum(ML_DATUM);
  for (int i = 0; i < 5; i++) {
    tft.fillCircle(52, 84 + i * 20, 3, C_LOUD);
    tft.drawString(lines[i], 62, 84 + i * 20, 2);
  }
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_DIM, C_SCREEN);
  tft.drawString("Buddy restarts into first-time setup.", 160, 186, 2);
  drawButton(10, 204, 140, 32, "Cancel", C_OFF, C_BTN_TX);
  drawButton(170, 204, 140, 32, "Erase everything", C_LOUD, C_BTN_TX);
}

static void aFactory() { resetMode = true; drawResetConfirm(); }

static void doFactoryReset() {
  tft.fillScreen(C_SCREEN);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT, C_SCREEN);
  tft.drawString("Erasing...", 160, 120, 4);
  factoryReset();   // main.cpp: wipes NVS + storage, then restarts
}
static void aSignOutAll() { webRevokeAll(); say("All browsers signed out"); }

// QR: the ESP-IDF encoder hands us the finished code through this callback
static void drawQrModules(esp_qrcode_handle_t q) {
  int n = esp_qrcode_get_size(q);
  int px = max(2, 132 / (n + 2));
  int size = px * (n + 2), x0 = (320 - size) / 2, y0 = 36;
  tft.fillRect(x0, y0, size, size, TFT_WHITE);
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++)
      if (esp_qrcode_get_module(q, x, y)) tft.fillRect(x0 + (x + 1) * px, y0 + (y + 1) * px, px, px, TFT_BLACK);
}

static void drawQr() {
  tft.fillScreen(C_SCREEN);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT, C_SCREEN);
  tft.drawString("Scan to open Buddy's page", 160, 18, 2);
  char url[48];
  snprintf(url, sizeof(url), "https://%s/", WiFi.localIP().toString().c_str());
  esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
  cfg.display_func = drawQrModules;
  cfg.max_qrcode_version = 6;
  cfg.qrcode_ecc_level = ESP_QRCODE_ECC_MED;
  esp_qrcode_generate(&cfg, url);
  tft.setTextColor(C_TEXT, C_SCREEN);
  tft.drawString(url, 160, 184, 2);
  char fp[40];
  snprintf(fp, sizeof(fp), "Certificate ends %s", certFingerprintShort());
  tft.setTextColor(C_DIM, C_SCREEN);
  tft.drawString(fp, 160, 202, 2);
  drawButton(210, 212, 100, 26, "Back", C_BTN, C_BTN_TX);
}

static void aQr() {
  if (!webRunning()) { say("Turn on web control first (needs WiFi)"); return; }
  qrMode = true;
  drawQr();
}

static void cWeb() {
  if (g.webOn) {
    tft.fillRect(0, 210, 236, 30, C_SCREEN);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(C_BTN, C_SCREEN);
    tft.drawString("Creating certificate...", 8, 226, 2);
    if (!certEnsure()) { g.webOn = false; say("Couldn't create the certificate"); }
  }
  settingsSave();
}

static void cDist() {
  if (g.distOn && !csiModelReady()) say("Set up room zones to use this");
}
static void cFar()    { g.midOffset = g.farOffset / 2; }
static void cBright() { applyBrightness(); }
static void cNap()    { g.sleepAfter = napMin * 60; }
static void cVad()    { applyVad(); }

// ---------------- pages ----------------
static SRow tabVoice[] = {
  PM("Too-loud line", "Where Buddy covers his ears", nullptr, &g.loudDb, 40, 100, 1, fDb, nullptr),
  PM("Early warning", "Yellow starts this much sooner", nullptr, &g.warnMargin, 2, 15, 1, fDb, nullptr),
  TOG("Listen only for his/her voice", nullptr, subVoiceOnly, &g.hisVoiceOnly, cVad),
  TOG("Ignore bangs and noise", "Only voices move the meter", nullptr, &g.speechOnly, nullptr),
  TOG("Talk bubbles", "Buddy says short phrases", nullptr, &g.bubbles, nullptr),
};
static SRow tabRoom[] = {
  TOG("Adjust for distance", "Lower the line when he/she is far", nullptr, &g.distOn, cDist),
  ACT("Where is he/she?", nullptr, subWhere, "Check", nullptr, aCheck),
  PM("Far-away adjustment", "Measured during setup", nullptr, &g.farOffset, -20, 0, 1, fDb, cFar),
  TOG("Louder room, higher line", "Allow a bit more over the TV", nullptr, &g.lombard, nullptr),
  ACT("WiFi & web control", nullptr, subWifiWeb, "Open", nullptr, aWebPage),
};
static SRow tabWeb[] = {
  ACT("WiFi", nullptr, subWifi, "Change", nullptr, aWifi),
  TOG("Web control", nullptr, subWebOn, &g.webOn, cWeb),
  INFO("Address", nullptr, subAddr, btnIp),
  ACT("Connect a phone", nullptr, subQr, "Show QR", nullptr, aQr),
  ACT("Signed-in browsers", nullptr, subSess, "Sign out", nullptr, aSignOutAll),
};
static SRow tabStars[] = {
  PM("Star every", "His/her voice time per star", nullptr, &g.starEvery, 15, 300, 5, fSec, nullptr),
  PM("Daily goal", "Stars for the big celebration", nullptr, &g.dailyGoal, 3, 30, 1, fNum, nullptr),
  TOG("Loud empties the star bar", "Otherwise it only pauses", nullptr, &g.loudResets, nullptr),
  TOG("Chime", "Soft sound for each star", nullptr, &g.chime, nullptr),
  ACT("Today's stars", "Back to 0, e.g. each morning", nullptr, "Reset", nullptr, aResetStars),
};
static SRow tabScreen[] = {
  PM("Brightness", "Screen backlight", nullptr, &g.brightness, 10, 100, 10, fPct, cBright),
  TOG("Back light glow", "LED matches Buddy's mood", nullptr, &g.ledGlow, nullptr),
  PM("Nap after quiet", "Buddy naps when it's quiet", nullptr, &napMin, 1, 15, 1, fMin, cNap),
  ACT("Start on", "Main view after power-up", nullptr, nullptr, btnView, aView),
  ACT("Parent PIN", "Hold top-right corner 3 s to open", nullptr, "Change", nullptr, aPin),
};
static SRow tabSetup[] = {
  ACT("First-time setup", nullptr, subSetup, "Start", nullptr, aFull),
  ACT("Redo his/her voice only", "If he/she has grown or it's off", nullptr, "Start", nullptr, aVoice),
  ACT("Redo room zones only", "After moving furniture or Buddy", nullptr, "Start", nullptr, aZones),
  PM("Calibrate dB readings", nullptr, subCal, &g.calOffset, 60, 140, 1, fNum, nullptr),
  ACT("Grown-up voices", nullptr, subParents, "Add", nullptr, aParent),
};
static SRow tabDevice[] = {
  ACT("SD card", nullptr, subSd, "Check", nullptr, aSdCheck),
  ACT("Speech test", "10 words, for the reading games", nullptr, "Start", nullptr, aSpeechTest),
  INFO("Firmware", nullptr, subFw, btnFw),
  INFO("Memory", nullptr, subMem, nullptr),
  ACT("Factory reset", "Erase everything and start over", nullptr, "Reset", nullptr, aFactory),
};

struct Tab { const char *name; SRow *rows; int n; };
static Tab tabs[NTABS + 1] = {
  {"Voice", tabVoice, 5}, {"Room", tabRoom, 5}, {"Stars", tabStars, 5}, {"Screen", tabScreen, 5}, {"Setup", tabSetup, 5}, {"Device", tabDevice, 5},
  {"WiFi & web control", tabWeb, 5},
};

// 5 rows use a 35 px pitch; a 6-row tab tightens to 29 px
static int pitch() { return tabs[curTab].n > 5 ? 29 : 35; }
static int ROW_Y(int i) { return 32 + i * pitch(); }
static int ctlY(int y) { return y + (pitch() > 30 ? 5 : 3); }
static int ctlH() { return pitch() > 30 ? 24 : 22; }
#define TAB_W (320 / NTABS)

// ---------------- drawing ----------------
static void drawSub(int i) {
  SRow &r = tabs[curTab].rows[i];
  int y = ROW_Y(i);
  char b[56];
  if (r.subFn) r.subFn(b, sizeof(b)); else snprintf(b, sizeof(b), "%s", r.sub ? r.sub : "");
  int textW = (r.kind == K_PM) ? 186 : (r.kind == K_TOG ? 222 : (r.kind == K_INFO ? 200 : 202));
  int sy = y + (pitch() > 30 ? 21 : 18);
  tft.fillRect(6, sy, textW, 10, C_SCREEN);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_DIM, C_SCREEN);
  tft.drawString(b, 8, sy + 1, 1);
}

static void drawControl(int i) {
  SRow &r = tabs[curTab].rows[i];
  int y = ROW_Y(i);
  char b[24];
  switch (r.kind) {
    case K_PM:
      r.fmt(*r.val, b, sizeof(b));
      tft.fillRect(233, y + 2, 46, pitch() - 4, C_SCREEN);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(C_TEXT, C_SCREEN);
      tft.drawString(b, 256, ctlY(y) + ctlH() / 2, 2);
      break;
    case K_TOG:
      drawButton(232, ctlY(y), 80, ctlH(), *r.flag ? "ON" : "OFF", *r.flag ? C_ON : C_OFF, C_BTN_TX);
      break;
    case K_ACT:
      if (r.btnFn) r.btnFn(b, sizeof(b)); else snprintf(b, sizeof(b), "%s", r.btn);
      drawButton(212, ctlY(y), 100, ctlH(), b, r.act == aFactory ? C_LOUD : C_BTN, C_BTN_TX);
      break;
    case K_INFO:
      if (!r.btnFn) break;
      r.btnFn(b, sizeof(b));
      tft.fillRect(206, y + 2, 110, pitch() - 4, C_SCREEN);
      tft.setTextDatum(MR_DATUM);
      tft.setTextColor(C_TEXT, C_SCREEN);
      tft.drawString(b, 312, ctlY(y) + ctlH() / 2, 2);
      break;
  }
}

static void drawRow(int i) {
  SRow &r = tabs[curTab].rows[i];
  int y = ROW_Y(i);
  tft.fillRect(0, y, 320, pitch() - 1, C_SCREEN);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_TEXT, C_SCREEN);
  tft.drawString(r.label, 8, y + (pitch() > 30 ? 4 : 1), 2);
  if (r.kind == K_PM) {
    drawButton(196, ctlY(y), 36, ctlH(), "-", C_BTN, C_BTN_TX);
    drawButton(280, ctlY(y), 36, ctlH(), "+", C_BTN, C_BTN_TX);
  }
  drawSub(i);
  drawControl(i);
  tft.drawFastHLine(6, y + pitch() - 1, 308, C_PANEL);
}

static void drawFooter() {
  char b[56];
  bool t = toast[0] && millis() - toastMs < 3000;
  if (t) snprintf(b, sizeof(b), "%s", toast);
  else switch (curTab) {
    case TAB_VOICE:  snprintf(b, sizeof(b), "Now: %.0f dB  (line %.0f)", currentLevelDb(), currentLimitDb()); break;
    case TAB_ROOM:   snprintf(b, sizeof(b), "Room background: %.0f dB", audioNoiseDbfs() + g.calOffset); break;
    case TAB_STARS:  snprintf(b, sizeof(b), "Today: %d of %d stars", g.stars, g.dailyGoal); break;
    case TAB_SCREEN: snprintf(b, sizeof(b), "Saved when you tap Done"); break;
    case TAB_WEB:    snprintf(b, sizeof(b), g.webOn ? "Sign-in uses the parent PIN" : "Buddy works the same with it off"); break;
    case TAB_DEVICE: snprintf(b, sizeof(b), "Voice Buddy %s", FW_VERSION); break;
    default:         snprintf(b, sizeof(b), g.setupDone ? "Setup is done" : "Setup isn't finished yet"); break;
  }
  tft.fillRect(0, 210, 236, 30, C_SCREEN);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(t ? C_ON : C_DIM, C_SCREEN);
  tft.drawString(b, 8, 226, 2);
}

static void drawAll() {
  tft.fillScreen(C_SCREEN);
  if (curTab == TAB_WEB) {
    tft.fillRect(0, 0, 320, 28, C_TAB);
    drawButton(4, 3, 40, 22, "<", rgb(125, 140, 166), C_BTN_TX);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_BTN_TX, C_TAB);
    tft.drawString(tabs[TAB_WEB].name, 160, 14, 2);
  }
  else for (int i = 0; i < NTABS; i++) {
    bool on = i == curTab;
    tft.fillRect(i * TAB_W, 0, TAB_W, 28, on ? C_TAB : C_PANEL);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(on ? C_BTN_TX : C_TEXT, on ? C_TAB : C_PANEL);
    tft.drawString(tabs[i].name, i * TAB_W + TAB_W / 2, 14, 2);
  }
  for (int i = 0; i < tabs[curTab].n; i++) drawRow(i);
  drawButton(240, 212, 76, 26, "Done", C_ON, C_BTN_TX);
  drawFooter();
}

// ---------------- public ----------------
void settingsRedraw() { if (!qrMode && !resetMode) drawAll(); }

void settingsEnter(int tab) {
  screen = SCR_SETTINGS;
  qrMode = false;
  resetMode = false;
  curTab = constrain(tab, 0, NTABS);
  napMin = max(1, g.sleepAfter / 60);
  toast[0] = 0;
  setPixel(0, 0, 0);
  drawAll();
}

bool settingsTap(int x, int y) {
  if (qrMode) { qrMode = false; drawAll(); return false; }
  if (resetMode) {
    if (y >= 200 && x >= 166) doFactoryReset();
    else if (y >= 200 && x < 154) { resetMode = false; drawAll(); }
    return false;
  }
  if (curTab == TAB_WEB && y < 28) {
    if (x < 50) { curTab = TAB_ROOM; drawAll(); }
    return false;
  }
  if (y < 28) {
    int t = x / TAB_W;
    if (t >= 0 && t < NTABS && t != curTab) { curTab = t; drawAll(); }
    return false;
  }
  if (y >= 208 && x >= 236) { settingsSave(); applyVad(); enterChild(); return false; }
  for (int i = 0; i < tabs[curTab].n; i++) {
    int ry = ROW_Y(i);
    if (y < ry || y >= ry + pitch()) continue;
    SRow &r = tabs[curTab].rows[i];
    switch (r.kind) {
      case K_PM:
        if (x >= 190 && x < 240)      *r.val = max(r.mn, *r.val - r.step);
        else if (x >= 272)            *r.val = min(r.mx, *r.val + r.step);
        else return false;
        if (r.onChange) r.onChange();
        drawControl(i);
        return true;
      case K_TOG:
        if (x < 228) return false;
        *r.flag = !*r.flag;
        if (r.onChange) r.onChange();
        drawControl(i);
        drawSub(i);
        drawFooter();
        return false;
      case K_ACT:
        if (x < 208) return false;
        r.act();
        if (screen == SCR_SETTINGS && !qrMode && !resetMode) { drawControl(i); drawSub(i); drawFooter(); }
        return false;
      case K_INFO:
        return false;
    }
  }
  return false;
}

void settingsTick() {
  static uint32_t last = 0;
  if (qrMode || resetMode || millis() - last < 300) return;
  last = millis();
  if (curTab == TAB_WEB)
    for (int i = 0; i < tabs[curTab].n; i++) if (tabs[curTab].rows[i].kind == K_INFO) drawControl(i);
  for (int i = 0; i < tabs[curTab].n; i++) if (tabs[curTab].rows[i].subFn) drawSub(i);
  drawFooter();
}

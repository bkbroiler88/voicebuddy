#include "csi.h"
#include "config.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <math.h>

struct Pkt { uint8_t a[CSI_NSUB]; };

#define RING 48
static Pkt s_ring[RING];
static volatile int s_head = 0, s_count = 0;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_bssid[6];
static bool s_running = false;
static volatile uint32_t s_pktTotal = 0;

static float s_emaShort[CSI_NSUB];
static bool  s_emaInit = false;
static float s_score = 0;
static uint32_t s_lastProcMs = 0;
static int   s_pps = 0;
static uint32_t s_ppsMark = 0, s_ppsCount = 0;

static Zone s_zone = Z_UNKNOWN, s_cand = Z_UNKNOWN;
static uint32_t s_candSince = 0;

// calibration
static Zone s_calZone = Z_UNKNOWN;
static uint32_t s_calStart = 0;
static double s_calSum = 0; static uint32_t s_calN = 0;
static double s_baseSum[CSI_NSUB]; static uint32_t s_baseN = 0;

const char *zoneName(Zone z) {
  switch (z) {
    case Z_EMPTY: return "EMPTY";
    case Z_FAR:   return "FAR";
    case Z_MID:   return "MID";
    case Z_NEAR:  return "NEAR";
    default:      return "--";
  }
}

static inline uint8_t amp(const int8_t *b, int k) {
  int im = b[2 * k], re = b[2 * k + 1];
  int v = (int)sqrtf((float)(im * im + re * re));
  return v > 255 ? 255 : v;
}

// LLTF layout: subcarriers 0..31 then -32..-1. Use the 52 data/pilot ones (+-1..26).
static void csiCb(void *, wifi_csi_info_t *info) {
  if (!info || !info->buf || info->len < 128) return;
  if (memcmp(info->mac, s_bssid, 6) != 0) return;
  Pkt p;
  int j = 0;
  for (int k = 1; k <= 26; k++) p.a[j++] = amp(info->buf, k);
  for (int k = 38; k < 64; k++) p.a[j++] = amp(info->buf, k);
  portENTER_CRITICAL(&s_mux);
  s_ring[s_head] = p;
  s_head = (s_head + 1) % RING;
  if (s_count < RING) s_count++;
  portEXIT_CRITICAL(&s_mux);
  s_pktTotal++;
}

static void promCb(void *, wifi_promiscuous_pkt_type_t) {}

void csiBegin(const uint8_t bssid[6], int channel) {
  if (channel < 1 || channel > 14) return;
  memcpy(s_bssid, bssid, 6);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_promiscuous(false);
  wifi_promiscuous_filter_t f = {};
  f.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
  esp_wifi_set_promiscuous_filter(&f);
  esp_wifi_set_promiscuous_rx_cb(promCb);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

  wifi_csi_config_t c = {};
  c.lltf_en = true;
  c.htltf_en = false;
  c.stbc_htltf2_en = false;
  c.ltf_merge_en = false;
  c.channel_filter_en = false;
  c.manu_scale = false;
  c.shift = 0;
  esp_wifi_set_csi_config(&c);
  esp_wifi_set_csi_rx_cb(csiCb, nullptr);
  esp_wifi_set_csi(true);

  s_emaInit = false;
  s_zone = s_cand = Z_UNKNOWN;
  s_running = true;
  Serial.printf("[csi] listening to %02X:%02X:%02X:%02X:%02X:%02X ch%d\n",
                bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], channel);
}

void csiStop() {
  if (!s_running) return;
  esp_wifi_set_csi(false);
  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_OFF);
  s_running = false;
  s_zone = s_cand = Z_UNKNOWN;
  s_pps = 0;
}

bool csiRunning() { return s_running; }

int csiScan(ApInfo *out, int maxN) {
  bool was = s_running;
  if (was) { esp_wifi_set_csi(false); esp_wifi_set_promiscuous(false); }
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  int n = WiFi.scanNetworks(false, false);
  int m = 0;
  for (int i = 0; i < n && m < maxN; i++) {
    if (WiFi.channel(i) > 14) continue;   // S3 is 2.4 GHz only anyway
    strncpy(out[m].ssid, WiFi.SSID(i).c_str(), 32);
    out[m].ssid[32] = 0;
    if (!out[m].ssid[0]) strcpy(out[m].ssid, "(hidden)");
    memcpy(out[m].bssid, WiFi.BSSID(i), 6);
    out[m].channel = WiFi.channel(i);
    out[m].rssi = WiFi.RSSI(i);
    m++;
  }
  WiFi.scanDelete();
  // strongest first
  for (int i = 0; i < m; i++)
    for (int j = i + 1; j < m; j++)
      if (out[j].rssi > out[i].rssi) { ApInfo t = out[i]; out[i] = out[j]; out[j] = t; }
  if (was) csiBegin(s_bssid, g.channel);
  else WiFi.mode(WIFI_OFF);
  return m;
}

static Zone classify(float s) {
  Zone best = Z_UNKNOWN;
  float bestD = 1e9f;
  for (int z = 0; z < 4; z++) {
    if (g.calScore[z] < 0) continue;
    float d = fabsf(logf(s + 1e-3f) - logf(g.calScore[z] + 1e-3f));
    if (d < bestD) { bestD = d; best = (Zone)z; }
  }
  return best;
}

static void process(const Pkt &p, float dt) {
  float mean = 0;
  for (int k = 0; k < CSI_NSUB; k++) mean += p.a[k];
  mean /= CSI_NSUB;
  if (mean < 1.f) return;

  float an[CSI_NSUB];
  for (int k = 0; k < CSI_NSUB; k++) an[k] = p.a[k] / mean;   // remove AGC gain

  if (!s_emaInit) { memcpy(s_emaShort, an, sizeof(an)); s_emaInit = true; }

  // D: static shape change vs empty room   M: short-term motion
  float D = 0, M = 0;
  for (int k = 0; k < CSI_NSUB; k++) {
    if (g.hasBaseline) D += fabsf(an[k] - g.baseline[k]);
    s_emaShort[k] += (an[k] - s_emaShort[k]) * 0.25f;
    M += fabsf(an[k] - s_emaShort[k]);
  }
  D /= CSI_NSUB; M /= CSI_NSUB;
  float inst = D + 2.f * M;
  s_score += (inst - s_score) * (1.f - expf(-dt / 1.5f));

  // calibration capture
  if (s_calZone != Z_UNKNOWN && millis() - s_calStart >= CAL_LEADIN_MS) {
    uint32_t el = millis() - s_calStart - CAL_LEADIN_MS;
    if (s_calZone == Z_EMPTY && el < CAL_CAPTURE_MS / 2) {
      for (int k = 0; k < CSI_NSUB; k++) s_baseSum[k] += an[k];
      s_baseN++;
    } else {
      if (s_calZone == Z_EMPTY && !g.hasBaseline && s_baseN > 0) {
        for (int k = 0; k < CSI_NSUB; k++) g.baseline[k] = s_baseSum[k] / s_baseN;
        g.hasBaseline = true;
        s_score = 2.f * M;
      }
      if (s_calZone != Z_EMPTY || g.hasBaseline) { s_calSum += inst; s_calN++; }
    }
  }
}

void csiTick() {
  if (!s_running) return;
  uint32_t now = millis();

  Pkt local[RING];
  int n = 0;
  portENTER_CRITICAL(&s_mux);
  int start = (s_head - s_count + RING) % RING;
  for (int i = 0; i < s_count; i++) local[n++] = s_ring[(start + i) % RING];
  s_count = 0;
  portEXIT_CRITICAL(&s_mux);

  float span = (now - s_lastProcMs) / 1000.f;
  s_lastProcMs = now;
  if (n > 0) {
    float dt = fminf(span, 1.f) / n;
    for (int i = 0; i < n; i++) process(local[i], dt);
  }

  s_ppsCount += n;
  if (now - s_ppsMark >= 1000) {
    s_pps = s_ppsCount * 1000 / (now - s_ppsMark);
    s_ppsCount = 0;
    s_ppsMark = now;
  }

  // finish calibration
  if (s_calZone != Z_UNKNOWN && now - s_calStart >= CAL_LEADIN_MS + CAL_CAPTURE_MS) {
    if (s_calN > 0) g.calScore[s_calZone] = s_calSum / s_calN;
    Serial.printf("[csi] cal %s = %.4f (%u pkts)\n", zoneName(s_calZone), g.calScore[s_calZone], s_calN);
    s_calZone = Z_UNKNOWN;
    settingsSave();
  }

  // zone with hysteresis
  Zone c = (s_pps < 3) ? Z_UNKNOWN : classify(s_score);
  if (c != s_cand) { s_cand = c; s_candSince = now; }
  if (s_cand != s_zone && now - s_candSince >= ZONE_HOLD_MS) s_zone = s_cand;
}

float csiScore() { return s_score; }
int   csiPps()   { return s_pps; }
Zone  csiZone()  { return s_zone; }

void csiStartCal(Zone z) {
  if (!s_running) return;
  s_calZone = z;
  s_calStart = millis();
  s_calSum = 0; s_calN = 0;
  if (z == Z_EMPTY) {
    // new empty-room reference invalidates the others
    g.hasBaseline = false;
    for (int i = 0; i < 4; i++) g.calScore[i] = -1;
    memset(s_baseSum, 0, sizeof(s_baseSum));
    s_baseN = 0;
  }
}

bool csiCalBusy() { return s_calZone != Z_UNKNOWN; }
Zone csiCalZone() { return s_calZone; }

int csiCalSecondsLeft() {
  if (s_calZone == Z_UNKNOWN) return 0;
  uint32_t el = millis() - s_calStart;
  if (el < CAL_LEADIN_MS) return (CAL_LEADIN_MS - el + 999) / 1000;
  return -(int)((CAL_LEADIN_MS + CAL_CAPTURE_MS - el + 999) / 1000);
}

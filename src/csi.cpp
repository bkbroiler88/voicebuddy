#include "csi.h"
#include "wifi_net.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <math.h>

#define NSUB      CSI_NSUB          // 52 data/pilot subcarriers of the LLTF
#define NFEAT     (2 * NSUB)        // shape + wobble per subcarrier
#define WIN_MS    500
#define STD_W     3.0f              // wobble weight relative to shape
#define HOLD_MS   2000
#define MIN_CONF  0.5f
#define MAX_DIST  9.0f              // further than ~3 sigma from every zone = unknown

struct Pkt { uint8_t a[NSUB]; };
#define RING 96
static Pkt s_ring[RING];
static volatile int s_head = 0, s_count = 0;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_bssid[6];
static bool s_running = false;

// window accumulation
static float s_sum[NSUB], s_sq[NSUB];
static int s_n = 0;
static uint32_t s_winStart = 0;
static float s_feat[NFEAT];          // smoothed live feature
static bool s_featValid = false;

// self-normalising motion (adaptive floor + decaying max)
static float s_motion = 0, s_mFloor = 0, s_mMax = 1e-3f;

// rate
static int s_pps = 0;
static uint32_t s_ppsMark = 0, s_ppsCount = 0;

// classification
static Zone s_live = Z_UNKNOWN, s_zone = Z_UNKNOWN, s_cand = Z_UNKNOWN;
static float s_liveConf = 0, s_zoneConf = 0;
static uint32_t s_candSince = 0;

// model
#define MODEL_VER 1
struct Model {
  uint32_t ver;
  uint8_t trained[4];
  float mu[4][NFEAT];
  float sd[4][NFEAT];
};
static Model s_m;

// The sensing work runs on its own Core-0 task; the UI (Core 1) only reads results.
// s_lock guards the model and the training accumulators shared between the two.
static SemaphoreHandle_t s_lock = nullptr;
static TaskHandle_t s_task = nullptr;
#define LOCK()   xSemaphoreTake(s_lock, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_lock)

// training
static volatile bool s_training = false;
static Zone s_trainZone = Z_UNKNOWN;
static double s_tSum[NFEAT], s_tSq[NFEAT];
static int s_tN = 0;

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

// LLTF layout: subcarriers 0..31 then -32..-1. Keep +-1..26.
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
}

static void promCb(void *, wifi_promiscuous_pkt_type_t) {}

static void csiProcess();

static void senseTask(void *) {
  for (;;) {
    if (s_running) csiProcess();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

static void ensureTask() {
  if (!s_lock) s_lock = xSemaphoreCreateMutex();
  if (!s_task) xTaskCreatePinnedToCore(senseTask, "csi", 6144, nullptr, 3, &s_task, 0);
}

bool csiStart() {
  ensureTask();
  if (s_running) return true;
  if (!netConnected()) return false;
  memcpy(s_bssid, netBssid(), 6);

  // promiscuous on the router's channel also catches its frames to other devices
  wifi_promiscuous_filter_t f = {};
  f.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
  esp_wifi_set_promiscuous_filter(&f);
  esp_wifi_set_promiscuous_rx_cb(promCb);
  esp_wifi_set_promiscuous(true);

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

  netStartTraffic();
  s_n = 0; s_featValid = false; s_winStart = millis();
  s_zone = s_cand = s_live = Z_UNKNOWN;
  s_running = true;
  Serial.printf("[csi] started on %02X:%02X:%02X:%02X:%02X:%02X ch%d\n",
                s_bssid[0], s_bssid[1], s_bssid[2], s_bssid[3], s_bssid[4], s_bssid[5], netChannel());
  return true;
}

void csiStop() {
  if (!s_running) return;
  netStopTraffic();
  esp_wifi_set_csi(false);
  esp_wifi_set_promiscuous(false);
  s_running = false;
  s_zone = s_cand = s_live = Z_UNKNOWN;
  s_pps = 0;
}

bool csiRunning() { return s_running; }

// ---------------- model ----------------
bool csiZoneTrained(Zone z) { return z >= 0 && z < 4 && s_m.trained[z]; }

bool csiModelReady() {
  int places = csiZoneTrained(Z_NEAR) + csiZoneTrained(Z_MID) + csiZoneTrained(Z_FAR);
  return csiZoneTrained(Z_EMPTY) && places >= 2;
}

void csiModelClear() {
  ensureTask();
  LOCK();
  memset(&s_m, 0, sizeof(s_m));
  s_m.ver = MODEL_VER;
  UNLOCK();
}

void csiModelSave() {
  ensureTask();
  static Model copy;
  LOCK(); copy = s_m; UNLOCK();
  Preferences p;
  p.begin("csimodel", false);
  p.putBytes("m", &copy, sizeof(copy));
  p.end();
}

void csiModelLoad() {
  ensureTask();
  static Model tmp;
  bool ok = false;
  Preferences p;
  p.begin("csimodel", true);
  if (p.getBytesLength("m") == sizeof(tmp) && p.getBytes("m", &tmp, sizeof(tmp)) == sizeof(tmp) && tmp.ver == MODEL_VER) ok = true;
  p.end();
  LOCK();
  if (ok) s_m = tmp; else { memset(&s_m, 0, sizeof(s_m)); s_m.ver = MODEL_VER; }
  UNLOCK();
}

static void classify(const float *f) {
  if (!csiModelReady()) { s_live = Z_UNKNOWN; s_liveConf = 0; return; }
  // pooled spread per feature across trained zones
  float d[4];
  float dmin = 1e9f;
  int best = -1;
  for (int z = 0; z < 4; z++) {
    if (!s_m.trained[z]) { d[z] = 1e9f; continue; }
    float acc = 0;
    for (int i = 0; i < NFEAT; i++) {
      float s = 0; int k = 0;
      for (int q = 0; q < 4; q++) if (s_m.trained[q]) { s += s_m.sd[q][i]; k++; }
      s = fmaxf(s / k, 0.02f);
      float e = (f[i] - s_m.mu[z][i]) / s;
      acc += e * e;
    }
    d[z] = acc / NFEAT;
    if (d[z] < dmin) { dmin = d[z]; best = z; }
  }
  if (best < 0 || dmin > MAX_DIST) { s_live = Z_UNKNOWN; s_liveConf = 0; return; }
  float sum = 0;
  for (int z = 0; z < 4; z++) if (s_m.trained[z]) sum += expf(-2.f * (d[z] - dmin));
  s_live = (Zone)best;
  s_liveConf = 1.f / sum;
}

static void endWindow() {
  float w[NFEAT];
  for (int k = 0; k < NSUB; k++) {
    float m = s_sum[k] / s_n;
    float v = fmaxf(0.f, s_sq[k] / s_n - m * m);
    w[k] = m;
    w[NSUB + k] = sqrtf(v) * STD_W;
  }
  // smoothed live feature (~1 s)
  if (!s_featValid) { memcpy(s_feat, w, sizeof(w)); s_featValid = true; }
  else for (int i = 0; i < NFEAT; i++) s_feat[i] += (w[i] - s_feat[i]) * 0.4f;

  // motion: mean wobble, normalised against a slow floor and a decaying max
  float mw = 0;
  for (int k = 0; k < NSUB; k++) mw += w[NSUB + k];
  mw /= NSUB;
  if (s_mFloor <= 0) s_mFloor = mw;
  s_mFloor += (mw - s_mFloor) * (mw < s_mFloor ? 0.2f : 0.005f);
  s_mMax = fmaxf(mw, s_mMax + (mw - s_mMax) * 0.01f);
  float range = s_mMax - s_mFloor;
  s_motion = range > 1e-4f ? constrain((mw - s_mFloor) / range, 0.f, 1.f) : 0;

  LOCK();
  if (s_training) {
    for (int i = 0; i < NFEAT; i++) { s_tSum[i] += w[i]; s_tSq[i] += (double)w[i] * w[i]; }
    s_tN++;
  }
  classify(s_feat);
  UNLOCK();
}

void csiTick() {}   // processing now lives on the Core-0 sensing task

static void csiProcess() {
  uint32_t now = millis();

  static Pkt local[RING];   // 5 KB: keep it off the 8 KB loop stack
  int n = 0;
  portENTER_CRITICAL(&s_mux);
  int start = (s_head - s_count + RING) % RING;
  for (int i = 0; i < s_count; i++) local[n++] = s_ring[(start + i) % RING];
  s_count = 0;
  portEXIT_CRITICAL(&s_mux);

  for (int i = 0; i < n; i++) {
    float mean = 0;
    for (int k = 0; k < NSUB; k++) mean += local[i].a[k];
    mean /= NSUB;
    if (mean < 1.f) continue;
    for (int k = 0; k < NSUB; k++) {
      float a = local[i].a[k] / mean;   // remove AGC gain
      s_sum[k] += a; s_sq[k] += a * a;
    }
    s_n++;
  }
  s_ppsCount += n;
  if (now - s_ppsMark >= 1000) {
    s_pps = s_ppsCount * 1000 / (now - s_ppsMark);
    s_ppsCount = 0;
    s_ppsMark = now;
  }

  if (now - s_winStart >= WIN_MS) {
    if (s_n >= 8) endWindow();
    else { s_live = Z_UNKNOWN; s_liveConf = 0; }
    memset(s_sum, 0, sizeof(s_sum));
    memset(s_sq, 0, sizeof(s_sq));
    s_n = 0;
    s_winStart = now;

    Zone c = (s_liveConf >= MIN_CONF) ? s_live : Z_UNKNOWN;
    if (c != s_cand) { s_cand = c; s_candSince = now; }
    if (now - s_candSince >= HOLD_MS) { s_zone = s_cand; s_zoneConf = s_liveConf; }
  }
}

int   csiPps()            { return s_pps; }
float csiMotion()         { return s_motion; }
Zone  csiZone()           { return s_zone; }
float csiConfidence()     { return s_zoneConf; }
Zone  csiLiveZone()       { return s_live; }
float csiLiveConfidence() { return s_liveConf; }
bool  csiTraining()       { return s_training; }

void csiTrainBegin(Zone z) {
  ensureTask();
  LOCK();
  s_trainZone = z;
  memset(s_tSum, 0, sizeof(s_tSum));
  memset(s_tSq, 0, sizeof(s_tSq));
  s_tN = 0;
  s_training = true;
  UNLOCK();
}

int csiTrainEnd() {
  ensureTask();
  LOCK();
  s_training = false;
  Zone z = s_trainZone;
  int n = s_tN;
  if (z >= 0 && z <= 3 && n >= 4) {
    // sums are doubles but this runs once per zone, so soft-float cost doesn't matter
    for (int i = 0; i < NFEAT; i++) {
      double m = s_tSum[i] / n;
      double v = s_tSq[i] / n - m * m;
      s_m.mu[z][i] = (float)m;
      s_m.sd[z][i] = (float)sqrt(v > 0 ? v : 0);
    }
    s_m.trained[z] = 1;
    s_m.ver = MODEL_VER;
  }
  UNLOCK();
  Serial.printf("[csi] trained %s from %d windows\n", zoneName(z), n);
  return n;
}

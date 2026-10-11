#include "history.h"
#include "settings.h"
#include <LittleFS.h>
#include <time.h>

#define SLOTS      144      // 10-minute slots in a day
#define MAX_EVENTS 160
#define KEEP_DAYS  8

struct Event { uint16_t minute; uint8_t type; uint8_t db; uint8_t dur; };   // type 0 loud, 1 star
struct Day {
  uint32_t magic;
  uint32_t ymd;               // 20261011
  float sum[SLOTS];
  uint16_t n[SLOTS];
  uint8_t peak[SLOTS];
  uint16_t nEvents;
  uint16_t stars;
  Event ev[MAX_EVENTS];
};
#define DAY_MAGIC 0x56424831   // "VBH1"

static Day *s_day = nullptr;   // ~2.5 KB, lives in PSRAM
static uint32_t s_lastSave = 0;
static bool s_dirty = false;
static bool s_ntpStarted = false;

// written by the UI loop (Core 1), read by the web server task (Core 0)
static SemaphoreHandle_t s_mx = nullptr;
struct Guard {
  Guard() { if (!s_mx) s_mx = xSemaphoreCreateMutex(); xSemaphoreTake(s_mx, portMAX_DELAY); }
  ~Guard() { xSemaphoreGive(s_mx); }
};

bool timeValid() { return time(nullptr) > 1700000000; }

void timeBegin() {
  configTzTime(g.tz, "pool.ntp.org", "time.nist.gov");
  s_ntpStarted = true;
}

static bool nowTm(struct tm &t) {
  if (!timeValid()) return false;
  time_t n = time(nullptr);
  localtime_r(&n, &t);
  return true;
}

int minuteOfDay() {
  struct tm t;
  if (!nowTm(t)) return -1;
  return t.tm_hour * 60 + t.tm_min;
}

static uint32_t ymdOf(const struct tm &t) { return (t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday; }

static void pathFor(uint32_t ymd, char *out, size_t n) { snprintf(out, n, "/hist/%lu.bin", (unsigned long)ymd); }

static void save() {
  if (!s_day || !s_day->ymd) return;
  if (!LittleFS.exists("/hist")) LittleFS.mkdir("/hist");
  char p[32];
  pathFor(s_day->ymd, p, sizeof(p));
  File f = LittleFS.open(p, "w");
  if (!f) return;
  f.write((const uint8_t *)s_day, sizeof(Day));
  f.close();
  s_dirty = false;
  s_lastSave = millis();
}

static bool load(uint32_t ymd, Day *d) {
  char p[32];
  pathFor(ymd, p, sizeof(p));
  File f = LittleFS.open(p, "r");
  if (!f) return false;
  bool ok = f.size() == sizeof(Day) && f.read((uint8_t *)d, sizeof(Day)) == sizeof(Day) && d->magic == DAY_MAGIC;
  f.close();
  return ok;
}

static void prune(uint32_t todayYmd) {
  File dir = LittleFS.open("/hist");
  if (!dir) return;
  struct tm t = {};
  t.tm_year = todayYmd / 10000 - 1900; t.tm_mon = (todayYmd / 100) % 100 - 1; t.tm_mday = todayYmd % 100 - KEEP_DAYS;
  t.tm_hour = 12;
  mktime(&t);
  uint32_t oldest = ymdOf(t);
  for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
    uint32_t ymd = strtoul(e.name(), nullptr, 10);
    String name = String("/hist/") + e.name();
    e.close();
    if (ymd && ymd < oldest) LittleFS.remove(name);
  }
}

// make sure s_day is today's log; returns false without time
static bool ensureToday(struct tm &t) {
  if (!nowTm(t)) return false;
  if (!s_day) s_day = (Day *)ps_calloc(1, sizeof(Day));
  if (!s_day) return false;
  uint32_t ymd = ymdOf(t);
  if (s_day->ymd != ymd) {
    if (s_day->ymd) save();                         // close out yesterday
    if (!load(ymd, s_day)) { memset(s_day, 0, sizeof(Day)); s_day->magic = DAY_MAGIC; s_day->ymd = ymd; }
    prune(ymd);
  }
  return true;
}

void histTick(float levelDb) {
  Guard lock;
  struct tm t;
  if (!ensureToday(t)) return;
  int slot = (t.tm_hour * 60 + t.tm_min) / 10;
  s_day->sum[slot] += levelDb;
  s_day->n[slot]++;
  int pk = constrain((int)lroundf(levelDb), 0, 255);
  if (pk > s_day->peak[slot]) s_day->peak[slot] = pk;
  s_dirty = true;
  if (millis() - s_lastSave > 10UL * 60 * 1000) save();
}

static void addEvent(uint8_t type, int db, int dur) {
  Guard lock;
  struct tm t;
  if (!ensureToday(t)) return;
  if (s_day->nEvents < MAX_EVENTS) {
    Event &e = s_day->ev[s_day->nEvents++];
    e.minute = t.tm_hour * 60 + t.tm_min;
    e.type = type;
    e.db = constrain(db, 0, 255);
    e.dur = constrain(dur, 0, 255);
  }
  if (type == 1) s_day->stars++;
  save();
}

void histLoud(int peakDb, int seconds) { addEvent(0, peakDb, seconds); }
void histStar() { addEvent(1, 0, 0); }

void histToJson(JsonDocument &doc) {
  Guard lock;
  struct tm t;
  doc["timeValid"] = ensureToday(t);
  if (!doc["timeValid"]) return;
  char b[16];
  snprintf(b, sizeof(b), "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  JsonObject today = doc["today"].to<JsonObject>();
  today["date"] = b;
  JsonArray slots = today["slots"].to<JsonArray>();
  for (int i = 0; i < SLOTS; i++) {
    if (!s_day->n[i]) { slots.add(nullptr); continue; }
    JsonArray s = slots.add<JsonArray>();
    s.add((int)lroundf(s_day->sum[i] / s_day->n[i]));
    s.add(s_day->peak[i]);
  }
  JsonArray ev = today["events"].to<JsonArray>();
  for (int i = 0; i < s_day->nEvents; i++) {
    JsonObject e = ev.add<JsonObject>();
    snprintf(b, sizeof(b), "%02d:%02d", s_day->ev[i].minute / 60, s_day->ev[i].minute % 60);
    e["t"] = b;
    e["type"] = s_day->ev[i].type ? "star" : "loud";
    if (!s_day->ev[i].type) { e["db"] = s_day->ev[i].db; e["dur"] = s_day->ev[i].dur; }
  }
  // last 7 days, oldest first
  static const char *DOW[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  JsonArray week = doc["week"].to<JsonArray>();
  Day *tmp = (Day *)ps_malloc(sizeof(Day));
  for (int back = 6; back >= 0; back--) {
    struct tm d = t;
    d.tm_mday -= back; d.tm_hour = 12;
    mktime(&d);
    uint32_t ymd = ymdOf(d);
    int stars = 0;
    if (back == 0) stars = s_day->stars;
    else if (tmp && load(ymd, tmp)) stars = tmp->stars;
    JsonObject w = week.add<JsonObject>();
    snprintf(b, sizeof(b), "%04d-%02d-%02d", d.tm_year + 1900, d.tm_mon + 1, d.tm_mday);
    w["date"] = b;
    w["day"] = DOW[d.tm_wday];
    w["stars"] = stars;
  }
  free(tmp);
}

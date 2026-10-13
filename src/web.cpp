// HTTPS server for the parent page: PIN sign-in with 30-day sessions, a small JSON API,
// and signed firmware updates into the spare OTA slot.

#include "web.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_https_server.h>
#include <esp_ota_ops.h>
#include <lwip/sockets.h>
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"

#include "settings.h"
#include "certs.h"
#include "history.h"
#include "version.h"
#include "csi.h"
#include "wifi_net.h"
#include "buddy.h"
#include "signing_pubkey.h"
#include "web_index.h"

WebStatus webStatus;

static httpd_handle_t s_srv = nullptr;
static QueueHandle_t s_q = nullptr;
static bool s_mdns = false;

// ---------------------------------------------------------------- sessions
#define MAX_SESS     8
#define SESS_PATH    "/web/sessions.bin"
#define SESS_DAYS    30
struct Sess { char token[33]; char id[7]; char label[20]; uint32_t created; uint32_t lastSeen; };
static Sess s_sess[MAX_SESS];
static SemaphoreHandle_t s_mx = nullptr;
#define LOCK()   xSemaphoreTake(s_mx, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_mx)

// sign-in throttling
#define MAX_FAILS 5
#define LOCK_MS   (5UL * 60 * 1000)
static int s_fails = 0;
static uint32_t s_lockUntil = 0;

static void sessLoad() {
  memset(s_sess, 0, sizeof(s_sess));
  File f = LittleFS.open(SESS_PATH, "r");
  if (f && f.size() == sizeof(s_sess)) f.read((uint8_t *)s_sess, sizeof(s_sess));
  if (f) f.close();
}
static void sessSave() {
  File f = LittleFS.open(SESS_PATH, "w");
  if (f) { f.write((const uint8_t *)s_sess, sizeof(s_sess)); f.close(); }
}

static void randHex(char *out, int bytes) {
  uint8_t b[16];
  esp_fill_random(b, bytes);
  for (int i = 0; i < bytes; i++) sprintf(out + 2 * i, "%02x", b[i]);
  out[2 * bytes] = 0;
}

static bool ctEq(const char *a, const char *b, size_t n) {   // constant-time compare
  uint8_t d = 0;
  for (size_t i = 0; i < n; i++) d |= a[i] ^ b[i];
  return d == 0;
}

int webSessionCount() {
  int n = 0;
  LOCK();
  for (auto &s : s_sess) if (s.token[0]) n++;
  UNLOCK();
  return n;
}
void webRevokeAll() { LOCK(); memset(s_sess, 0, sizeof(s_sess)); sessSave(); UNLOCK(); }
void webRevokeById(const char *id) {
  LOCK();
  for (auto &s : s_sess) if (s.token[0] && !strcmp(s.id, id)) memset(&s, 0, sizeof(s));
  sessSave();
  UNLOCK();
}

static uint32_t nowEpoch() { return timeValid() ? (uint32_t)time(nullptr) : 0; }

// ---------------------------------------------------------------- request helpers
static bool header(httpd_req_t *r, const char *name, char *out, size_t n) {
  size_t len = httpd_req_get_hdr_value_len(r, name);
  if (!len || len >= n) { out[0] = 0; return false; }
  return httpd_req_get_hdr_value_str(r, name, out, n) == ESP_OK;
}

// index of the signed-in session, or -1
static int sessionOf(httpd_req_t *r) {
  char cookie[256];
  if (!header(r, "Cookie", cookie, sizeof(cookie))) return -1;
  char *p = strstr(cookie, "vb=");
  if (!p) return -1;
  p += 3;
  if (strlen(p) < 32) return -1;
  int found = -1;
  uint32_t now = nowEpoch();
  LOCK();
  for (int i = 0; i < MAX_SESS; i++) {
    Sess &s = s_sess[i];
    if (!s.token[0] || !ctEq(s.token, p, 32)) continue;
    if (now && s.created && now - s.created > SESS_DAYS * 86400UL) { memset(&s, 0, sizeof(s)); sessSave(); break; }
    s.lastSeen = now;
    found = i;
    break;
  }
  UNLOCK();
  return found;
}

static void secHeaders(httpd_req_t *r) {
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  httpd_resp_set_hdr(r, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(r, "X-Frame-Options", "DENY");
  httpd_resp_set_hdr(r, "Referrer-Policy", "no-referrer");
}

static esp_err_t sendJson(httpd_req_t *r, JsonDocument &doc, const char *status = "200 OK") {
  String out;
  serializeJson(doc, out);
  httpd_resp_set_status(r, status);
  httpd_resp_set_type(r, "application/json");
  secHeaders(r);
  return httpd_resp_send(r, out.c_str(), out.length());
}

static esp_err_t sendErr(httpd_req_t *r, const char *status, const char *err) {
  JsonDocument d;
  d["error"] = err;
  return sendJson(r, d, status);
}

static esp_err_t sendOk(httpd_req_t *r) {
  JsonDocument d;
  d["ok"] = true;
  return sendJson(r, d);
}

// signed in, and for POSTs the custom header that a cross-site form can't send
static bool guard(httpd_req_t *r, bool isPost) {
  if (sessionOf(r) < 0) { sendErr(r, "401 Unauthorized", "signin"); return false; }
  if (isPost) {
    char x[8];
    if (!header(r, "X-VB", x, sizeof(x))) { sendErr(r, "403 Forbidden", "csrf"); return false; }
  }
  return true;
}

static bool readBody(httpd_req_t *r, JsonDocument &doc) {
  if (r->content_len <= 0 || r->content_len > 4096) return false;
  char *buf = (char *)malloc(r->content_len + 1);
  if (!buf) return false;
  int got = 0;
  while (got < (int)r->content_len) {
    int n = httpd_req_recv(r, buf + got, r->content_len - got);
    if (n <= 0) { if (n == HTTPD_SOCK_ERR_TIMEOUT) continue; free(buf); return false; }
    got += n;
  }
  buf[got] = 0;
  bool ok = !deserializeJson(doc, buf);
  free(buf);
  return ok;
}

static void push(WebCmdType t, int a = 0, const char *text = nullptr) {
  WebCmd c = {};
  c.type = t;
  c.a = a;
  if (text) strncpy(c.text, text, sizeof(c.text) - 1);
  xQueueSend(s_q, &c, 0);
}
bool webPollCmd(WebCmd &c) { return s_q && xQueueReceive(s_q, &c, 0) == pdTRUE; }

static void hhmm(int minute, char *out) { snprintf(out, 6, "%02d:%02d", (minute / 60) % 24, minute % 60); }
static int parseHhmm(const char *s, int fallback) {
  int h, m;
  if (!s || sscanf(s, "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) return fallback;
  return h * 60 + m;
}

// ---------------------------------------------------------------- page
static esp_err_t hIndex(httpd_req_t *r) {
  httpd_resp_set_type(r, "text/html; charset=utf-8");
  httpd_resp_set_hdr(r, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(r, "Content-Security-Policy",
                     "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; "
                     "img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
  secHeaders(r);
  return httpd_resp_send(r, (const char *)WEB_INDEX_GZ, WEB_INDEX_GZ_LEN);
}

static esp_err_t hFavicon(httpd_req_t *r) {
  httpd_resp_set_status(r, "204 No Content");
  return httpd_resp_send(r, nullptr, 0);
}

// ---------------------------------------------------------------- sign in / out
static const char *uaLabel(httpd_req_t *r) {
  char ua[200];
  if (!header(r, "User-Agent", ua, sizeof(ua))) return "Browser";
  if (strstr(ua, "iPhone")) return "iPhone";
  if (strstr(ua, "iPad")) return "iPad";
  if (strstr(ua, "Android")) return "Android";
  if (strstr(ua, "Macintosh")) return "Mac";
  if (strstr(ua, "Windows")) return "Windows PC";
  if (strstr(ua, "CrOS")) return "Chromebook";
  if (strstr(ua, "Linux")) return "Linux PC";
  return "Browser";
}

static void peerIp(httpd_req_t *r, char *out, size_t n) {
  out[0] = 0;
  struct sockaddr_in6 a;
  socklen_t len = sizeof(a);
  if (getpeername(httpd_req_to_sockfd(r), (struct sockaddr *)&a, &len) != 0) return;
  if (a.sin6_family == AF_INET6) {
    const uint8_t *b = (const uint8_t *)&a.sin6_addr;   // IPv4-mapped: last 4 bytes
    snprintf(out, n, "%u.%u.%u.%u", b[12], b[13], b[14], b[15]);
  } else {
    struct sockaddr_in *a4 = (struct sockaddr_in *)&a;
    inet_ntoa_r(a4->sin_addr, out, n);
  }
}

static esp_err_t hLogin(httpd_req_t *r) {
  char x[8];
  if (!header(r, "X-VB", x, sizeof(x))) return sendErr(r, "403 Forbidden", "csrf");
  uint32_t now = millis();
  if (s_lockUntil && (int32_t)(s_lockUntil - now) > 0) {
    JsonDocument d;
    d["error"] = "locked";
    d["seconds"] = (s_lockUntil - now) / 1000 + 1;
    return sendJson(r, d, "429 Too Many Requests");
  }
  JsonDocument body;
  if (!readBody(r, body)) return sendErr(r, "400 Bad Request", "bad_json");
  const char *pin = body["pin"] | "";
  if (strlen(pin) != 4 || !ctEq(pin, g.pin, 4)) {
    if (++s_fails >= MAX_FAILS) { s_fails = 0; s_lockUntil = now + LOCK_MS; }
    JsonDocument d;
    d["error"] = s_lockUntil && (int32_t)(s_lockUntil - now) > 0 ? "locked" : "wrong_pin";
    d["left"] = MAX_FAILS - s_fails;
    if (s_lockUntil > now) d["seconds"] = LOCK_MS / 1000;
    return sendJson(r, d, s_lockUntil > now ? "429 Too Many Requests" : "401 Unauthorized");
  }
  s_fails = 0;
  s_lockUntil = 0;

  // new session (replace the oldest when full)
  Sess s = {};
  randHex(s.token, 16);
  randHex(s.id, 3);
  strncpy(s.label, uaLabel(r), sizeof(s.label) - 1);
  s.created = s.lastSeen = nowEpoch();
  LOCK();
  int slot = -1;
  for (int i = 0; i < MAX_SESS; i++) if (!s_sess[i].token[0]) { slot = i; break; }
  if (slot < 0) { slot = 0; for (int i = 1; i < MAX_SESS; i++) if (s_sess[i].lastSeen < s_sess[slot].lastSeen) slot = i; }
  s_sess[slot] = s;
  sessSave();
  UNLOCK();

  char cookie[128];
  snprintf(cookie, sizeof(cookie), "vb=%s; Path=/; Max-Age=%lu; Secure; HttpOnly; SameSite=Strict", s.token, SESS_DAYS * 86400UL);
  httpd_resp_set_hdr(r, "Set-Cookie", cookie);

  char ip[20], note[48];
  peerIp(r, ip, sizeof(ip));
  snprintf(note, sizeof(note), "%s|%s|%s", s.label, ip, s.id);
  push(WC_SIGNIN, 0, note);
  Serial.printf("[web] sign-in from %s %s\n", s.label, ip);
  return sendOk(r);
}

static esp_err_t hLogout(httpd_req_t *r) {
  int i = sessionOf(r);
  if (i >= 0) { LOCK(); memset(&s_sess[i], 0, sizeof(Sess)); sessSave(); UNLOCK(); }
  httpd_resp_set_hdr(r, "Set-Cookie", "vb=; Path=/; Max-Age=0; Secure; HttpOnly; SameSite=Strict");
  return sendOk(r);
}

// ---------------------------------------------------------------- status + settings
static const char *moodName(int m) {
  switch (m) {
    case -1: return "paused";
    case M_CALM: return "calm";
    case M_HAPPY: return "happy";
    case M_ALERT: return "alert";
    case M_LOUD: return "loud";
    case M_SLEEPY: return "sleepy";
    case M_PROUD: return "proud";
    case M_CELEBRATE: return "celebrate";
    default: return "calm";
  }
}

static esp_err_t hStatus(httpd_req_t *r) {
  if (!guard(r, false)) return ESP_OK;
  JsonDocument d;
  d["level"] = (int)lroundf(webStatus.level);
  d["line"] = (int)lroundf(webStatus.line);
  d["warn"] = (int)lroundf(webStatus.warn);
  d["mood"] = moodName(webStatus.paused ? -1 : webStatus.mood);
  Zone z = csiZone();
  d["zone"] = csiRunning() ? zoneName(z) : "--";
  d["zoneConf"] = (int)(csiConfidence() * 100);
  d["pps"] = csiPps();
  d["stars"] = g.stars;
  d["goal"] = g.dailyGoal;
  d["prog"] = webStatus.prog;
  d["starEvery"] = g.starEvery;
  d["view"] = g.view;
  d["paused"] = webStatus.paused;
  int m = minuteOfDay();
  char t[8] = "";
  if (m >= 0) hhmm(m, t);
  d["time"] = t;
  d["timeValid"] = m >= 0;
  d["fw"] = FW_VERSION;
  d["ssid"] = netSsid();
  d["ip"] = WiFi.localIP().toString();
  d["fingerprint"] = certFingerprint();
  d["uptime"] = millis() / 1000;
  return sendJson(r, d);
}

static void settingsToJson(JsonDocument &d) {
  d["loudDb"] = g.loudDb; d["warnMargin"] = g.warnMargin; d["hisVoiceOnly"] = g.hisVoiceOnly;
  d["speechOnly"] = g.speechOnly; d["bubbles"] = g.bubbles; d["distOn"] = g.distOn;
  d["farOffset"] = g.farOffset; d["lombard"] = g.lombard; d["starEvery"] = g.starEvery;
  d["dailyGoal"] = g.dailyGoal; d["loudResets"] = g.loudResets; d["chime"] = g.chime;
  d["brightness"] = g.brightness; d["ledGlow"] = g.ledGlow; d["napMin"] = max(1, g.sleepAfter / 60);
  d["calOffset"] = g.calOffset; d["resetOn"] = g.resetOn;
  char t[8];
  hhmm(g.resetMin, t); d["resetTime"] = t;
  d["quietOn"] = g.quietOn;
  hhmm(g.quietFromMin, t); d["quietFrom"] = t;
  hhmm(g.quietToMin, t); d["quietTo"] = t;
  d["quietLine"] = g.quietLine; d["tz"] = g.tz;
  d["pitchLo"] = g.pitchLo; d["pitchHi"] = g.pitchHi;
  d["setupDone"] = g.setupDone; d["zonesReady"] = csiModelReady();
}

// apply whatever keys are present, clamped to the same ranges as the device menus
static bool applySettings(JsonDocument &b) {
  bool tzChanged = false;
  auto setInt = [&](const char *k, int &dst, int mn, int mx) { if (b[k].is<int>()) dst = constrain(b[k].as<int>(), mn, mx); };
  auto setBool = [&](const char *k, bool &dst) { if (b[k].is<bool>()) dst = b[k].as<bool>(); };
  setInt("loudDb", g.loudDb, 40, 100);
  setInt("warnMargin", g.warnMargin, 2, 15);
  if (b["hisVoiceOnly"].is<bool>()) g.hisVoiceOnly = b["hisVoiceOnly"].as<bool>() && g.pitchHi > 0;
  setBool("speechOnly", g.speechOnly);
  setBool("bubbles", g.bubbles);
  setBool("distOn", g.distOn);
  if (b["farOffset"].is<int>()) { g.farOffset = constrain(b["farOffset"].as<int>(), -20, 0); g.midOffset = g.farOffset / 2; }
  setBool("lombard", g.lombard);
  setInt("starEvery", g.starEvery, 15, 300);
  setInt("dailyGoal", g.dailyGoal, 3, 30);
  setBool("loudResets", g.loudResets);
  setBool("chime", g.chime);
  setInt("brightness", g.brightness, 10, 100);
  setBool("ledGlow", g.ledGlow);
  if (b["napMin"].is<int>()) g.sleepAfter = constrain(b["napMin"].as<int>(), 1, 15) * 60;
  setInt("calOffset", g.calOffset, 60, 140);
  setBool("resetOn", g.resetOn);
  if (b["resetTime"].is<const char *>()) g.resetMin = parseHhmm(b["resetTime"], g.resetMin);
  setBool("quietOn", g.quietOn);
  if (b["quietFrom"].is<const char *>()) g.quietFromMin = parseHhmm(b["quietFrom"], g.quietFromMin);
  if (b["quietTo"].is<const char *>()) g.quietToMin = parseHhmm(b["quietTo"], g.quietToMin);
  setInt("quietLine", g.quietLine, 40, 100);
  if (b["tz"].is<const char *>()) {
    const char *tz = b["tz"];
    if (strlen(tz) > 0 && strlen(tz) < sizeof(g.tz) && strcmp(tz, g.tz)) { strcpy(g.tz, tz); tzChanged = true; }
  }
  return tzChanged;
}

static esp_err_t hSettingsGet(httpd_req_t *r) {
  if (!guard(r, false)) return ESP_OK;
  JsonDocument d;
  settingsToJson(d);
  return sendJson(r, d);
}

static esp_err_t hSettingsPost(httpd_req_t *r) {
  if (!guard(r, true)) return ESP_OK;
  JsonDocument b;
  if (!readBody(r, b)) return sendErr(r, "400 Bad Request", "bad_json");
  bool tz = applySettings(b);
  push(WC_SETTINGS, tz ? 1 : 0);
  JsonDocument d;
  d["ok"] = true;
  JsonDocument s;
  settingsToJson(s);
  d["settings"] = s;
  return sendJson(r, d);
}

static esp_err_t hBackup(httpd_req_t *r) {
  if (!guard(r, false)) return ESP_OK;
  JsonDocument d;
  settingsToJson(d);
  d["kind"] = "voicebuddy-settings";
  d["fw"] = FW_VERSION;
  httpd_resp_set_hdr(r, "Content-Disposition", "attachment; filename=\"voicebuddy-settings.json\"");
  return sendJson(r, d);
}

static esp_err_t hRestore(httpd_req_t *r) {
  if (!guard(r, true)) return ESP_OK;
  JsonDocument b;
  if (!readBody(r, b)) return sendErr(r, "400 Bad Request", "bad_json");
  bool tz = applySettings(b);
  push(WC_SETTINGS, tz ? 1 : 0);
  return sendOk(r);
}

// ---------------------------------------------------------------- actions
static esp_err_t hStar(httpd_req_t *r)       { if (!guard(r, true)) return ESP_OK; push(WC_STAR); return sendOk(r); }
static esp_err_t hResetStars(httpd_req_t *r) { if (!guard(r, true)) return ESP_OK; push(WC_RESET_STARS); return sendOk(r); }
static esp_err_t hRestart(httpd_req_t *r)    { if (!guard(r, true)) return ESP_OK; push(WC_RESTART); return sendOk(r); }

static esp_err_t hBubble(httpd_req_t *r) {
  if (!guard(r, true)) return ESP_OK;
  JsonDocument b;
  if (!readBody(r, b)) return sendErr(r, "400 Bad Request", "bad_json");
  String t = b["text"] | "";
  t.trim();
  if (!t.length() || t.length() > 24) return sendErr(r, "400 Bad Request", "bad_text");
  for (size_t i = 0; i < t.length(); i++) if ((uint8_t)t[i] < 32 || (uint8_t)t[i] > 126) t.setCharAt(i, ' ');   // panel font is ASCII
  push(WC_BUBBLE, 0, t.c_str());
  return sendOk(r);
}

static esp_err_t hView(httpd_req_t *r) {
  if (!guard(r, true)) return ESP_OK;
  JsonDocument b;
  if (!readBody(r, b) || !b["view"].is<int>()) return sendErr(r, "400 Bad Request", "bad_json");
  push(WC_VIEW, constrain(b["view"].as<int>(), 0, 3));
  return sendOk(r);
}

static esp_err_t hPause(httpd_req_t *r) {
  if (!guard(r, true)) return ESP_OK;
  JsonDocument b;
  if (!readBody(r, b) || !b["paused"].is<bool>()) return sendErr(r, "400 Bad Request", "bad_json");
  push(WC_PAUSE, b["paused"].as<bool>() ? 1 : 0);
  return sendOk(r);
}

static esp_err_t hHistory(httpd_req_t *r) {
  if (!guard(r, false)) return ESP_OK;
  JsonDocument d;
  histToJson(d);
  return sendJson(r, d);
}

static esp_err_t hSessions(httpd_req_t *r) {
  int me = sessionOf(r);
  if (me < 0) return sendErr(r, "401 Unauthorized", "signin");
  JsonDocument d;
  JsonArray a = d["sessions"].to<JsonArray>();
  LOCK();
  for (int i = 0; i < MAX_SESS; i++) {
    Sess &s = s_sess[i];
    if (!s.token[0]) continue;
    JsonObject o = a.add<JsonObject>();
    o["id"] = s.id;
    o["label"] = s.label;
    char b[16] = "";
    if (s.created) { time_t t = s.created; struct tm tm; localtime_r(&t, &tm); strftime(b, sizeof(b), "%Y-%m-%d", &tm); }
    o["created"] = b;
    o["current"] = i == me;
  }
  UNLOCK();
  return sendJson(r, d);
}

static esp_err_t hRevoke(httpd_req_t *r) {
  if (!guard(r, true)) return ESP_OK;
  int me = sessionOf(r);
  JsonDocument b;
  if (!readBody(r, b)) return sendErr(r, "400 Bad Request", "bad_json");
  LOCK();
  for (int i = 0; i < MAX_SESS; i++) {
    if (!s_sess[i].token[0] || i == me) continue;
    if ((b["all"] | false) || !strcmp(s_sess[i].id, b["id"] | "")) memset(&s_sess[i], 0, sizeof(Sess));
  }
  if (b["id"].is<const char *>() && me >= 0 && !strcmp(s_sess[me].id, b["id"])) memset(&s_sess[me], 0, sizeof(Sess));
  sessSave();
  UNLOCK();
  return sendOk(r);
}

// ---------------------------------------------------------------- signed firmware update
#define SIG_TRAILER 128
static const char SIG_MAGIC[8] = {'V', 'B', 'S', 'I', 'G', '0', '0', '1'};

static esp_err_t hOta(httpd_req_t *r) {
  if (!guard(r, true)) return ESP_OK;
  const esp_partition_t *part = esp_ota_get_next_update_partition(nullptr);
  int total = r->content_len;
  if (!part || total <= SIG_TRAILER + 1024) return sendErr(r, "400 Bad Request", "write_failed");
  int imageLen = total - SIG_TRAILER;
  if ((uint32_t)imageLen > part->size) return sendErr(r, "400 Bad Request", "too_big");

  esp_ota_handle_t h = 0;
  if (esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &h) != ESP_OK) return sendErr(r, "400 Bad Request", "write_failed");
  Serial.printf("[ota] receiving %d bytes into %s\n", imageLen, part->label);

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  uint8_t trailer[SIG_TRAILER];
  const int BUF = 4096;
  uint8_t *buf = (uint8_t *)malloc(BUF);
  int got = 0;
  bool ok = buf != nullptr;
  while (ok && got < total) {
    int n = httpd_req_recv(r, (char *)buf, min(BUF, total - got));
    if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (n <= 0) { ok = false; break; }
    // split this chunk into image bytes and trailer bytes
    int img = max(0, min(n, imageLen - got));
    if (img > 0) {
      mbedtls_sha256_update(&sha, buf, img);
      if (esp_ota_write(h, buf, img) != ESP_OK) { ok = false; break; }
    }
    for (int i = img; i < n; i++) trailer[got + i - imageLen] = buf[i];
    got += n;
  }
  free(buf);
  uint8_t hash[32];
  mbedtls_sha256_finish(&sha, hash);
  mbedtls_sha256_free(&sha);
  if (!ok) { esp_ota_abort(h); return sendErr(r, "400 Bad Request", "write_failed"); }

  // signature check with the owner's public key (compiled into this firmware)
  bool sigOk = false;
  uint16_t sigLen = trailer[8] | (trailer[9] << 8);
  if (!memcmp(trailer, SIG_MAGIC, 8) && sigLen > 0 && sigLen <= SIG_TRAILER - 10) {
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    if (mbedtls_pk_parse_public_key(&pk, (const unsigned char *)SIGNING_PUBKEY_PEM, sizeof(SIGNING_PUBKEY_PEM)) == 0)
      sigOk = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, sizeof(hash), trailer + 10, sigLen) == 0;
    mbedtls_pk_free(&pk);
  }
  if (!sigOk) {
    esp_ota_abort(h);
    Serial.println("[ota] REJECTED: signature check failed");
    return sendErr(r, "400 Bad Request", "bad_signature");
  }
  if (esp_ota_end(h) != ESP_OK || esp_ota_set_boot_partition(part) != ESP_OK) {   // esp_ota_end also validates the image
    Serial.println("[ota] image invalid");
    return sendErr(r, "400 Bad Request", "write_failed");
  }
  Serial.println("[ota] signature OK, restarting into the new firmware");
  push(WC_RESTART);
  return sendOk(r);
}

// ---------------------------------------------------------------- lifecycle
static bool start() {
  if (!certEnsure()) { Serial.println("[web] no certificate, not starting"); return false; }
  httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();
  conf.servercert = (const uint8_t *)certPem();
  conf.servercert_len = strlen(certPem()) + 1;
  conf.prvtkey_pem = (const uint8_t *)keyPem();
  conf.prvtkey_len = strlen(keyPem()) + 1;
  conf.transport_mode = HTTPD_SSL_TRANSPORT_SECURE;     // no plain HTTP at all
  conf.httpd.max_open_sockets = 3;                      // each TLS session costs ~40 KB of internal RAM
  conf.httpd.lru_purge_enable = true;
  conf.httpd.max_uri_handlers = 24;
  conf.httpd.stack_size = 10240;
  conf.httpd.core_id = 0;                               // keep Core 1 free for the screen
  conf.httpd.task_priority = 2;                         // below audio (5) and CSI (3)
  conf.httpd.recv_wait_timeout = 15;
  conf.httpd.send_wait_timeout = 15;
  if (httpd_ssl_start(&s_srv, &conf) != ESP_OK) { s_srv = nullptr; Serial.println("[web] httpd_ssl_start failed"); return false; }

  struct Route { const char *uri; httpd_method_t m; esp_err_t (*fn)(httpd_req_t *); };
  static const Route routes[] = {
    {"/", HTTP_GET, hIndex}, {"/index.html", HTTP_GET, hIndex}, {"/favicon.ico", HTTP_GET, hFavicon},
    {"/api/login", HTTP_POST, hLogin}, {"/api/logout", HTTP_POST, hLogout},
    {"/api/status", HTTP_GET, hStatus}, {"/api/settings", HTTP_GET, hSettingsGet}, {"/api/settings", HTTP_POST, hSettingsPost},
    {"/api/star", HTTP_POST, hStar}, {"/api/stars/reset", HTTP_POST, hResetStars}, {"/api/bubble", HTTP_POST, hBubble},
    {"/api/view", HTTP_POST, hView}, {"/api/pause", HTTP_POST, hPause}, {"/api/history", HTTP_GET, hHistory},
    {"/api/sessions", HTTP_GET, hSessions}, {"/api/sessions/revoke", HTTP_POST, hRevoke},
    {"/api/backup", HTTP_GET, hBackup}, {"/api/restore", HTTP_POST, hRestore}, {"/api/restart", HTTP_POST, hRestart},
    {"/api/ota", HTTP_POST, hOta},
  };
  for (auto &rt : routes) {
    httpd_uri_t u = {};
    u.uri = rt.uri;
    u.method = rt.m;
    u.handler = rt.fn;
    httpd_register_uri_handler(s_srv, &u);
  }
  if (MDNS.begin("voicebuddy")) { MDNS.addService("https", "tcp", 443); s_mdns = true; }
  Serial.printf("[web] HTTPS on https://voicebuddy.local (https://%s)\n", WiFi.localIP().toString().c_str());
  return true;
}

static void stop() {
  if (s_srv) { httpd_ssl_stop(s_srv); s_srv = nullptr; Serial.println("[web] stopped"); }
  if (s_mdns) { MDNS.end(); s_mdns = false; }
}

void webBegin() {
  if (!s_mx) s_mx = xSemaphoreCreateMutex();
  if (!s_q) s_q = xQueueCreate(16, sizeof(WebCmd));
  if (storageBegin()) sessLoad();
}

bool webRunning() { return s_srv != nullptr; }

void webService() {
  static uint32_t last = 0, failAt = 0;
  if (millis() - last < 1000) return;
  last = millis();
  bool want = g.webOn && netConnected();
  if (want && !s_srv) {
    if (failAt && millis() - failAt < 30000) return;   // don't hammer a failing start
    if (!start()) failAt = millis(); else failAt = 0;
  } else if (!want && s_srv) {
    stop();
  }
}

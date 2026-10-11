#include "wifi_net.h"
#include <WiFi.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include "ping/ping_sock.h"

#define PING_INTERVAL_MS 8

static char s_ssid[33] = "", s_pass[65] = "";
static bool s_loaded = false;
static uint8_t s_bssid[6];
static esp_ping_handle_t s_ping = nullptr;

static void loadCreds() {
  if (s_loaded) return;
  Preferences p;
  p.begin("wifi", true);
  p.getString("ssid", s_ssid, sizeof(s_ssid));
  p.getString("pass", s_pass, sizeof(s_pass));
  p.end();
  s_loaded = true;
}

bool netHasCreds() { loadCreds(); return s_ssid[0] != 0; }
const char *netSsid() { loadCreds(); return s_ssid; }

void netSetCreds(const char *ssid, const char *pass) {
  strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
  strncpy(s_pass, pass, sizeof(s_pass) - 1);
  s_loaded = true;
  Preferences p;
  p.begin("wifi", false);
  p.putString("ssid", s_ssid);
  p.putString("pass", s_pass);
  p.end();
}

int netScan(ApInfo *out, int maxN) {
  netStopTraffic();
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks(false, false);
  int m = 0;
  for (int i = 0; i < n && m < maxN; i++) {
    String name = WiFi.SSID(i);
    if (!name.length()) continue;                       // hidden networks can't be picked by name
    bool dup = false;
    for (int j = 0; j < m; j++) if (name == out[j].ssid) { dup = true; if (WiFi.RSSI(i) > out[j].rssi) out[j].rssi = WiFi.RSSI(i); }
    if (dup) continue;
    strncpy(out[m].ssid, name.c_str(), 32);
    out[m].ssid[32] = 0;
    out[m].rssi = WiFi.RSSI(i);
    out[m].secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    m++;
  }
  WiFi.scanDelete();
  for (int i = 0; i < m; i++)
    for (int j = i + 1; j < m; j++)
      if (out[j].rssi > out[i].rssi) { ApInfo t = out[i]; out[i] = out[j]; out[j] = t; }
  return m;
}

void netBegin() {
  loadCreds();
  if (!s_ssid[0]) return;
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);          // modem sleep would starve CSI
  WiFi.setAutoReconnect(true);
  WiFi.begin(s_ssid, s_pass);
}

bool netConnectBlocking(uint32_t timeoutMs) {
  netStopTraffic();
  WiFi.disconnect(true);
  delay(100);
  netBegin();
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) delay(100);
  return WiFi.status() == WL_CONNECTED;
}

bool netConnected() { return WiFi.status() == WL_CONNECTED; }

void netDisconnect() {
  netStopTraffic();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

const uint8_t *netBssid() {
  uint8_t *b = WiFi.BSSID();
  if (b) memcpy(s_bssid, b, 6);
  return s_bssid;
}

int netChannel() { return WiFi.channel(); }

void netStartTraffic() {
  if (s_ping || !netConnected()) return;
  IPAddress gw = WiFi.gatewayIP();
  esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
  IP_ADDR4(&cfg.target_addr, gw[0], gw[1], gw[2], gw[3]);
  cfg.count = ESP_PING_COUNT_INFINITE;
  cfg.interval_ms = PING_INTERVAL_MS;
  cfg.timeout_ms = 500;
  cfg.data_size = 8;
  cfg.task_prio = 2;
  esp_ping_callbacks_t cbs = {};
  if (esp_ping_new_session(&cfg, &cbs, &s_ping) == ESP_OK) {
    esp_ping_start(s_ping);
    Serial.printf("[net] pinging %s every %d ms\n", gw.toString().c_str(), PING_INTERVAL_MS);
  } else {
    s_ping = nullptr;
  }
}

void netStopTraffic() {
  if (!s_ping) return;
  esp_ping_stop(s_ping);
  esp_ping_delete_session(s_ping);
  s_ping = nullptr;
}

#include "speech.h"
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>
#include "sr_glue.h"

static StreamBufferHandle_t s_buf = nullptr;   // raw mono 16-bit from the mic task
static sr_cmd_t *s_cmds = nullptr;
static volatile bool s_running = false, s_feeding = false;
static volatile int s_heard = -1;
static volatile uint32_t s_heardMs = 0;

static volatile int s_peak = 0, s_peakAcc = 0;   // per-second peak of what we feed (diagnostics)
static size_t s_fedAcc = 0;

// Digital gain on the recognizer path only. The codec delivers normal talking at about -27 dBFS
// peak (room floor -63), while the AFE's VAD gate is -60 dBFS and MultiNet expects speech near
// -6 dBFS. x12 (+21.6 dB) puts talking at about -5 dBFS; shouting clips, which the recognizer
// tolerates. The level meter uses the unamplified samples, so its calibration is unaffected.
#define SR_GAIN 12
#define SR_FEED_MAX 512

void speechFeed(const int16_t *mono, size_t samples) {
  if (!s_feeding || !s_buf) return;
  static int16_t amp[SR_FEED_MAX];
  if (samples > SR_FEED_MAX) samples = SR_FEED_MAX;
  int pk = s_peakAcc;
  for (size_t i = 0; i < samples; i++) {
    int v = mono[i] * SR_GAIN;
    if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
    amp[i] = (int16_t)v;
    int a = abs(v); if (a > pk) pk = a;
  }
  s_peakAcc = pk;
  if ((s_fedAcc += samples) >= 16000) { s_peak = s_peakAcc; s_peakAcc = 0; s_fedAcc = 0; }
  xStreamBufferSend(s_buf, amp, samples * sizeof(int16_t), 0);
}

float speechFedPeakDb() { return 20.f * log10f(s_peak / 32768.f + 1e-6f); }

// The feed task asks for one AFE chunk at a time. Collect a FULL chunk of real audio so the
// pipeline runs at mic speed: the stream buffer returns as soon as any bytes are there, and
// returning a short, zero-padded chunk made the feed run far faster than real time, which
// turned the detect task (priority 5, core 1) into a busy loop that starved the UI loop.
// If the mic task is busy (e.g. playing the chime) hand back silence so the pipeline keeps time.
static esp_err_t fillCb(void *, void *out, size_t len, size_t *bytesRead, uint32_t) {
  size_t got = 0;
  uint32_t t0 = millis();
  while (got < len && millis() - t0 < 300)
    got += xStreamBufferReceive(s_buf, (uint8_t *)out + got, len - got, pdMS_TO_TICKS(50));
  if (got < len) memset((uint8_t *)out + got, 0, len - got);
  *bytesRead = len;
  return ESP_OK;
}

// Runs on the SR handler task. After every result the library drops to "off", so re-arm.
static void eventCb(void *, sr_event_t ev, int commandId, int) {
  switch (ev) {
    case SR_EVENT_COMMAND:
      Serial.printf("[sr] heard id %d prob %.2f '%s'\n", commandId, vbsr_last_prob(), vbsr_last_string());
      s_heard = commandId;
      s_heardMs = millis();
      vbsr_set_mode(SR_MODE_COMMAND);
      break;
    case SR_EVENT_TIMEOUT:
      vbsr_set_mode(SR_MODE_COMMAND);   // keep listening; games decide their own timeouts
      break;
    default:
      break;
  }
}

bool speechBegin(const char *const *phrases, int count) {
  if (s_running) speechEnd();
  if (count <= 0 || count > 200) return false;
  if (!s_buf) s_buf = xStreamBufferCreate(16000 * sizeof(int16_t), 1);   // one second of audio
  if (!s_buf) return false;
  free(s_cmds);
  s_cmds = (sr_cmd_t *)calloc(count, sizeof(sr_cmd_t));
  if (!s_cmds) return false;
  for (int i = 0; i < count; i++) {
    s_cmds[i].command_id = i;
    strncpy(s_cmds[i].str, phrases[i], sizeof(s_cmds[i].str) - 1);
  }
  xStreamBufferReset(s_buf);
  s_heard = -1;
  s_feeding = true;
  uint32_t heap0 = ESP.getFreeHeap(), ps0 = ESP.getFreePsram(), t0 = millis();
  // command mode from the start; the glue leaves the wake-word model out entirely
  esp_err_t e = vbsr_start(fillCb, nullptr, SR_CHANNELS_MONO, SR_MODE_COMMAND, "M", s_cmds, count, eventCb, nullptr);
  if (e != ESP_OK) {
    s_feeding = false;
    Serial.printf("[sr] start failed: %s (free heap %u)\n", esp_err_to_name(e), heap0);
    return false;
  }
  s_running = true;
  Serial.printf("[sr] listening for %d phrases (%u ms; heap %u->%u, psram %u->%u)\n", count, (unsigned)(millis() - t0),
                heap0, ESP.getFreeHeap(), ps0, ESP.getFreePsram());
  return true;
}

void speechEnd() {
  if (!s_running) return;
  s_feeding = false;
  vbsr_stop();
  s_running = false;
  Serial.printf("[sr] stopped (heap %u)\n", ESP.getFreeHeap());
}

bool speechRunning() { return s_running; }

int speechPoll(uint32_t *heardMs) {
  int h = s_heard;
  if (h < 0) return -1;
  s_heard = -1;
  if (heardMs) *heardMs = s_heardMs;
  return h;
}

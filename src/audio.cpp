#include "audio.h"
#include "config.h"
#include <Wire.h>
#include <ESP_I2S.h>

static I2SClass s_i2s;   // full duplex, 16 kHz stereo frames (mono codec)
#include <math.h>

static volatile float s_levelDb = -90.f;
static volatile float s_speechDb = -90.f;   // level that only moves while voice is present
static volatile float s_noiseDb  = -90.f;   // learned background floor
static volatile float s_voicing  = 0.f;     // last pitch-correlation peak (0..1)
static volatile bool  s_speech   = false;
static volatile float s_vadThresh = 0.50f;
static volatile float s_noiseMargin = 6.f;
volatile int16_t g_rawMinL, g_rawMaxL, g_rawMinR, g_rawMaxR;   // diagnostics
static volatile bool  s_chimeReq = false;
static bool s_codecOk = false;

// ---------------- ES8311 register access ----------------
static bool esW(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static uint8_t esR(uint8_t reg) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0;
  if (Wire.requestFrom((int)ES8311_ADDR, 1) != 1) return 0;
  return Wire.read();
}

// Sequence follows espressif/esp-bsp components/es8311 (es8311_init + microphone_config)
// for slave mode, MCLK from pin = 256*fs, 16 kHz, 16-bit I2S, analog mic.
static bool es8311Init(int micGainDb) {
  Wire.beginTransmission(ES8311_ADDR);
  if (Wire.endTransmission() != 0) return false;

  esW(0x00, 0x1F); delay(20);           // reset
  esW(0x00, 0x00);
  // esp_codec_dev es8311_open() power/bias defaults
  esW(0x44, 0x08);                      // I2C noise immunity
  esW(0x44, 0x08);
  esW(0x0B, 0x00);
  esW(0x0C, 0x00);
  esW(0x10, 0x1F);
  esW(0x11, 0x7F);
  esW(0x00, 0x80);                      // power on

  esW(0x01, 0x3F);                      // all clocks on, MCLK from pin
  esW(0x06, esR(0x06) & ~0x20);         // SCLK not inverted

  // coeff row {4096000, 16000, pre_div 1, pre_multi 0, adc_div 1, dac_div 1,
  //            fs_mode 0, lrck_h 0x00, lrck_l 0xff, bclk_div 4, osr 0x10/0x10}
  esW(0x02, esR(0x02) & 0x07);
  esW(0x03, 0x10);
  esW(0x04, 0x10);
  esW(0x05, 0x00);
  esW(0x06, (esR(0x06) & 0xE0) | 0x03);
  esW(0x07, esR(0x07) & 0xC0);
  esW(0x08, 0xFF);

  esW(0x00, esR(0x00) & 0xBF);          // slave mode
  esW(0x09, 0x0C);                      // SDP in  (DAC): I2S 16-bit
  esW(0x0A, 0x0C);                      // SDP out (ADC): I2S 16-bit

  esW(0x0D, 0x01);                      // analog power up
  esW(0x0E, 0x02);                      // PGA + ADC modulator on
  esW(0x12, 0x00);                      // DAC power up
  esW(0x13, 0x10);                      // output drive
  esW(0x1C, 0x6A);                      // ADC EQ bypass, DC offset cancel
  esW(0x37, 0x08);                      // DAC EQ bypass

  esW(0x17, 0xBF);                      // ADC digital volume (0 dB)
  esW(0x14, 0x1A);                      // analog mic, PGA max
  esW(0x15, 0x40);                      // ADC ramp / start (esp_codec_dev es8311_start)
  esW(0x45, 0x00);
  esW(0x0A, esR(0x0A) & ~0x40);         // ADC SDP unmute
  audioSetGain(micGainDb);

  esW(0x32, 0xA0);                      // DAC volume (moderate)
  esW(0x31, esR(0x31) & 0x9F);          // DAC unmute
  return true;
}

void audioSetGain(int micGainDb) {
  int code = constrain(micGainDb / 6, 0, 7);
  esW(0x16, (uint8_t)code);
}

bool audioCodecOk() { return s_codecOk; }

void audioDump() {
  Serial.printf("[es8311] id FD=%02X FE=%02X (expect 83 11)\n", esR(0xFD), esR(0xFE));
  for (int r = 0; r <= 0x45; r++) {
    Serial.printf("%02X=%02X ", r, esR(r));
    if ((r & 15) == 15) Serial.println();
  }
  Serial.println();
}
float audioLevelDbfs() { return s_levelDb; }
void audioChime() { s_chimeReq = true; }

void  audioSetVad(float voicingThresh, float noiseMarginDb) { s_vadThresh = voicingThresh; s_noiseMargin = noiseMarginDb; }
float audioSpeechDbfs() { return s_speechDb; }
float audioNoiseDbfs()  { return s_noiseDb; }
float audioVoicing()    { return s_voicing; }
bool  audioSpeechActive() { return s_speech; }

// ---------------- DSP ----------------
struct Biquad {
  float b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
  void highpass(float fc, float fs) {
    float w = 2.f * PI * fc / fs, c = cosf(w), s = sinf(w), al = s / (2.f * 0.7071f);
    float a0 = 1.f + al;
    b0 = (1.f + c) / 2.f / a0; b1 = -(1.f + c) / a0; b2 = b0;
    a1 = -2.f * c / a0;        a2 = (1.f - al) / a0;
  }
  void lowpass(float fc, float fs) {
    float w = 2.f * PI * fc / fs, c = cosf(w), s = sinf(w), al = s / (2.f * 0.7071f);
    float a0 = 1.f + al;
    b0 = (1.f - c) / 2.f / a0; b1 = (1.f - c) / a0; b2 = b0;
    a1 = -2.f * c / a0;        a2 = (1.f - al) / a0;
  }
  inline float run(float x) {
    float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }
};

// Chime: two soft notes with exponential decay
static int16_t chimeSample(int pos) {
  const float t = pos / (float)AUDIO_RATE;
  float f, tt;
  if (t < 0.18f) { f = 1046.5f; tt = t; }
  else           { f = 1568.0f; tt = t - 0.18f; }
  float env = expf(-tt * 9.f) * fminf(1.f, tt * 400.f);
  return (int16_t)(5000.f * env * sinf(2.f * PI * f * t));
}
#define CHIME_LEN (AUDIO_RATE / 2)

// ---------------- speech detection ----------------
// Voiced sound (talking, shouting, shrieking) is periodic: its autocorrelation has a
// strong peak at the pitch period. Claps, bangs, rustling and fan noise do not.
#define VAD_N      512              // 32 ms frame
#define VAD_LAGMIN 6                // 2.7 kHz  (kids' shrieks run high)
#define VAD_LAGMAX 250              // 64 Hz
static float s_vadBuf[VAD_N];
static int   s_vadPos = 0;

static volatile float s_bandLo = 0, s_bandHi = 0;   // accepted pitch band in Hz (0 = any)
static volatile float s_pitchHz = 0;
static float s_ac[VAD_LAGMAX + 1];

// Single-precision on purpose: the S3 FPU has no hardware double.
static inline float autocorr(const float *x, int lag) {
  float r = 0;
  int n = VAD_N - lag;
  for (int i = 0; i < n; i++) r += x[i] * x[i + lag];
  return r;
}

// Voicing strength 0..1 and the fundamental period, searched over the WHOLE range.
// The band check happens afterwards: searching only inside a band let an adult's
// overtones pass as a child's voice.
static float voicingPeak(const float *x, int &periodLag) {
  periodLag = 0;
  float r0 = autocorr(x, 0);
  if (r0 < 1.f) return 0;
  // skip the main lobe: start after the autocorrelation first goes negative
  int start = 1;
  while (start < VAD_LAGMAX && autocorr(x, start) >= 0) start++;
  if (start < VAD_LAGMIN) start = VAD_LAGMIN;
  float best = 0;
  for (int lag = start; lag <= VAD_LAGMAX; lag++) {
    float v = autocorr(x, lag) / r0 * VAD_N / (VAD_N - lag);   // unbias for the shorter overlap
    s_ac[lag] = v;
    if (v > best) best = v;
  }
  if (best <= 0) return 0;
  // a periodic sound also peaks at 2x, 3x the period; take the SHORTEST lag that
  // is a local peak and nearly as strong as the best (avoids octave-down errors)
  for (int lag = start + 1; lag < VAD_LAGMAX; lag++) {
    float v = s_ac[lag];
    if (v >= 0.85f * best && v >= s_ac[lag - 1] && v >= s_ac[lag + 1]) { periodLag = lag; break; }
  }
  return best > 1.f ? 1.f : best;
}

void audioSetPitchBand(int loHz, int hiHz) {
  if (loHz <= 0 || hiHz <= loHz) { s_bandLo = 0; s_bandHi = 0; return; }
  s_bandLo = loHz; s_bandHi = hiHz;
}
float audioPitchHz() { return s_pitchHz; }

static void audioTask(void *) {
  static int16_t rx[AUDIO_FRAMES * 2];
  static int16_t tx[AUDIO_FRAMES * 2];
  Biquad hpL, hpR, lpL, lpR;
  hpL.highpass(150.f, AUDIO_RATE); hpR = hpL;   // drop HVAC / table thumps
  lpL.lowpass(4000.f, AUDIO_RATE); lpR = lpL;   // speech band
  float lvl = -90.f, sp = -90.f, noise = -90.f;
  int chimePos = -1;
  int ampOffAt = -1;   // blocks until amp off

  for (;;) {
    size_t got = s_i2s.readBytes((char *)rx, sizeof(rx));
    if (got == 0) { vTaskDelay(1); continue; }
    int n = got / 4;
    double sL = 0, sR = 0;
    int16_t mnL = 32767, mxL = -32768, mnR = 32767, mxR = -32768;
    for (int i = 0; i < n; i++) {
      mnL = min(mnL, rx[2 * i]); mxL = max(mxL, rx[2 * i]);
      mnR = min(mnR, rx[2 * i + 1]); mxR = max(mxR, rx[2 * i + 1]);
    }
    g_rawMinL = mnL; g_rawMaxL = mxL; g_rawMinR = mnR; g_rawMaxR = mxR;
    for (int i = 0; i < n; i++) {
      float l = lpL.run(hpL.run(rx[2 * i]));
      float r = lpR.run(hpR.run(rx[2 * i + 1]));
      sL += l * l; sR += r * r;
      if (s_vadPos < VAD_N) s_vadBuf[s_vadPos++] = l;
    }
    // ES8311 is a mono ADC; use whichever slot carries it
    float rms = sqrtf((float)(fmax(sL, sR) / n));
    float db = 20.f * log10f(rms / 32768.f + 1e-7f);
    float dt = n / (float)AUDIO_RATE;

    // Don't let Buddy hear his own chime
    if (chimePos < 0 && ampOffAt < 0) {
      float tau = db > lvl ? 0.05f : 0.40f;
      lvl += (db - lvl) * (1.f - expf(-dt / tau));
      s_levelDb = lvl;

      // pitch check once per 32 ms frame
      static float hang = 0;
      if (s_vadPos >= VAD_N) {
        int lag;
        float v = voicingPeak(s_vadBuf, lag);
        s_voicing = v;
        s_vadPos = 0;
        bool loudEnough = db > noise + s_noiseMargin;
        if (v >= s_vadThresh && loudEnough && lag > 0) {
          float f0 = (float)AUDIO_RATE / lag;
          s_pitchHz = f0;                   // reported for any voice (setup measures grown-ups too)
          bool inBand = s_bandHi <= 0 || (f0 >= s_bandLo && f0 <= s_bandHi);
          if (inBand) hang = 0.30f;         // bridges consonants and short pauses
        }
      }
      hang -= dt;
      bool speech = hang > 0;
      s_speech = speech;

      // background floor: falls quickly to quiet moments, rises slowly and only without speech
      if (noise < -89.f) noise = db;
      if (db < noise)       noise += (db - noise) * (1.f - expf(-dt / 2.0f));
      else if (!speech)     noise += (db - noise) * (1.f - expf(-dt / 15.0f));
      s_noiseDb = noise;

      // speech-only level: follows the voice, otherwise sinks away
      float target = speech ? lvl : -90.f;
      float stau = target > sp ? 0.05f : 0.40f;
      sp += (target - sp) * (1.f - expf(-dt / stau));
      s_speechDb = sp;
    } else {
      s_vadPos = 0;
    }

    if (s_chimeReq && chimePos < 0) {
      s_chimeReq = false;
      chimePos = 0;
      digitalWrite(AMP_EN_PIN, AMP_ON_LEVEL);
    }
    if (chimePos >= 0) {
      for (int i = 0; i < AUDIO_FRAMES; i++) {
        int16_t s = (chimePos < CHIME_LEN) ? chimeSample(chimePos) : 0;
        tx[2 * i] = s; tx[2 * i + 1] = s;
        chimePos++;
      }
      s_i2s.write((const uint8_t *)tx, sizeof(tx));
      if (chimePos >= CHIME_LEN) { chimePos = -1; ampOffAt = 8; }
    } else if (ampOffAt >= 0) {
      if (--ampOffAt < 0) digitalWrite(AMP_EN_PIN, !AMP_ON_LEVEL);
    }
  }
}

bool audioBegin(int micGainDb) {
  pinMode(AMP_EN_PIN, OUTPUT);
  digitalWrite(AMP_EN_PIN, !AMP_ON_LEVEL);

  // ESP_I2S std mode: MCLK = 256 x fs = 4.096 MHz, which the ES8311 setup below expects
  s_i2s.setPins(I2S_BCLK_PIN, I2S_LRCK_PIN, I2S_DOUT_PIN, I2S_DIN_PIN, I2S_MCLK_PIN);
  if (!s_i2s.begin(I2S_MODE_STD, AUDIO_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) return false;
  delay(10);   // MCLK running before codec setup

  s_codecOk = es8311Init(micGainDb);
  xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 5, nullptr, 0);
  return s_codecOk;
}

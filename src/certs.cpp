#include "certs.h"
#include <LittleFS.h>
#include <esp_system.h>
#include "mbedtls/pk.h"
#include "mbedtls/ecp.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/sha256.h"

#define CERT_PATH "/web/cert.pem"
#define KEY_PATH  "/web/key.pem"

static char *s_cert = nullptr, *s_key = nullptr;   // PSRAM copies
static char s_fp[3 * 32] = "", s_fpShort[12] = "";
static bool s_fsOk = false;

bool storageBegin() {
  if (s_fsOk) return true;
  s_fsOk = LittleFS.begin(true, "/littlefs", 10, "spiffs");
  if (s_fsOk && !LittleFS.exists("/web")) LittleFS.mkdir("/web");
  Serial.printf("[fs] LittleFS %s (%u KB used of %u KB)\n", s_fsOk ? "ok" : "FAILED",
                s_fsOk ? (unsigned)(LittleFS.usedBytes() / 1024) : 0, s_fsOk ? (unsigned)(LittleFS.totalBytes() / 1024) : 0);
  return s_fsOk;
}

static char *readFile(const char *path) {
  File f = LittleFS.open(path, "r");
  if (!f) return nullptr;
  size_t n = f.size();
  char *b = (char *)ps_malloc(n + 1);
  if (!b) { f.close(); return nullptr; }
  f.readBytes(b, n);
  b[n] = 0;
  f.close();
  return b;
}

static bool writeFile(const char *path, const char *data) {
  File f = LittleFS.open(path, "w");
  if (!f) return false;
  size_t n = strlen(data);
  bool ok = f.write((const uint8_t *)data, n) == n;
  f.close();
  return ok;
}

static void computeFingerprint() {
  mbedtls_x509_crt crt;
  mbedtls_x509_crt_init(&crt);
  if (mbedtls_x509_crt_parse(&crt, (const unsigned char *)s_cert, strlen(s_cert) + 1) == 0) {
    unsigned char h[32];
    mbedtls_sha256(crt.raw.p, crt.raw.len, h, 0);
    char *p = s_fp;
    for (int i = 0; i < 32; i++) p += sprintf(p, i ? ":%02X" : "%02X", h[i]);
    snprintf(s_fpShort, sizeof(s_fpShort), "%02X:%02X:%02X", h[29], h[30], h[31]);
  }
  mbedtls_x509_crt_free(&crt);
}

static int rng(void *, unsigned char *out, size_t len) { esp_fill_random(out, len); return 0; }

static bool generate() {
  Serial.println("[cert] generating device certificate...");
  uint32_t t0 = millis();
  bool ok = false;
  mbedtls_pk_context key;
  mbedtls_x509write_cert crt;
  mbedtls_pk_init(&key);
  mbedtls_x509write_crt_init(&crt);
  const size_t BUF = 2048;
  unsigned char *certBuf = (unsigned char *)ps_calloc(1, BUF), *keyBuf = (unsigned char *)ps_calloc(1, BUF);
  unsigned char ser[16];

  do {
    if (!certBuf || !keyBuf) break;
    if (mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY))) break;
    if (mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key), rng, nullptr)) break;

    esp_fill_random(ser, sizeof(ser));
    ser[0] &= 0x7F;                                  // positive serial

    mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&crt, &key);
    mbedtls_x509write_crt_set_issuer_key(&crt, &key);
    if (mbedtls_x509write_crt_set_subject_name(&crt, "CN=voicebuddy.local,O=Voice Buddy")) break;
    if (mbedtls_x509write_crt_set_issuer_name(&crt, "CN=voicebuddy.local,O=Voice Buddy")) break;
    if (mbedtls_x509write_crt_set_serial_raw(&crt, ser, sizeof(ser))) break;
    if (mbedtls_x509write_crt_set_validity(&crt, "20240101000000", "20491231235959")) break;
    mbedtls_x509write_crt_set_basic_constraints(&crt, 0, -1);

    if (mbedtls_x509write_crt_pem(&crt, certBuf, BUF, rng, nullptr)) break;
    if (mbedtls_pk_write_key_pem(&key, keyBuf, BUF)) break;
    if (!writeFile(CERT_PATH, (const char *)certBuf) || !writeFile(KEY_PATH, (const char *)keyBuf)) break;
    ok = true;
  } while (0);

  mbedtls_x509write_crt_free(&crt);
  mbedtls_pk_free(&key);
  if (keyBuf) { memset(keyBuf, 0, BUF); free(keyBuf); }
  free(certBuf);
  Serial.printf("[cert] %s in %u ms\n", ok ? "created" : "FAILED", (unsigned)(millis() - t0));
  return ok;
}

bool certEnsure() {
  if (s_cert && s_key) return true;
  if (!storageBegin()) return false;
  if (!LittleFS.exists(CERT_PATH) || !LittleFS.exists(KEY_PATH)) {
    if (!generate()) return false;
  }
  s_cert = readFile(CERT_PATH);
  s_key = readFile(KEY_PATH);
  if (!s_cert || !s_key) return false;
  computeFingerprint();
  Serial.printf("[cert] fingerprint %s\n", s_fp);
  return true;
}

const char *certPem() { return s_cert; }
const char *keyPem() { return s_key; }
const char *certFingerprint() { return s_fp; }
const char *certFingerprintShort() { return s_fpShort; }

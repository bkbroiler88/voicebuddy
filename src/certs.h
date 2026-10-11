#pragma once
#include <Arduino.h>

// Per-device TLS identity for the parent web page. A P-256 key and a self-signed
// certificate are generated on the device the first time web control is turned on
// and kept in the LittleFS ("spiffs") partition. Nothing secret ships in the firmware.

bool        storageBegin();          // mounts LittleFS (formats on first use)
bool        certEnsure();            // load, or generate (~1-3 s) and save
const char *certPem();               // PEM, NUL-terminated (valid after certEnsure)
const char *keyPem();
const char *certFingerprint();       // SHA-256 of the DER cert, "3F:A2:..." (32 bytes)
const char *certFingerprintShort();  // last 3 bytes, "47:12:9C"

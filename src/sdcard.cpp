#include "sdcard.h"
#include "config.h"
#include <SD_MMC.h>

static bool s_mounted = false;

bool sdBegin() {
  if (s_mounted) return true;
  SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN, SD_D1_PIN, SD_D2_PIN, SD_D3_PIN);
  // 4-bit mode, never format a card we don't recognise, 20 MHz (safe for long traces)
  s_mounted = SD_MMC.begin("/sd", false, false, 20000);
  if (s_mounted && SD_MMC.cardType() == CARD_NONE) { SD_MMC.end(); s_mounted = false; }
  if (s_mounted) {
    if (!SD_MMC.exists("/apps")) SD_MMC.mkdir("/apps");
    if (!SD_MMC.exists("/update")) SD_MMC.mkdir("/update");
    Serial.printf("[sd] mounted: %llu MB card, %llu MB used\n", sdTotalMB(), sdUsedMB());
  } else {
    Serial.println("[sd] no card");
  }
  return s_mounted;
}

bool sdMounted() { return s_mounted; }
uint64_t sdTotalMB() { return s_mounted ? SD_MMC.cardSize() / (1024 * 1024) : 0; }
uint64_t sdUsedMB() { return s_mounted ? SD_MMC.usedBytes() / (1024 * 1024) : 0; }

void sdStatus(char *buf, size_t n) {
  if (!s_mounted) { snprintf(buf, n, "No card"); return; }
  uint64_t total = sdTotalMB();
  if (total >= 1024) snprintf(buf, n, "%llu GB card, %llu MB used", (total + 512) / 1024, sdUsedMB());
  else snprintf(buf, n, "%llu MB card, %llu MB used", total, sdUsedMB());
}

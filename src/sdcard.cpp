#include "sdcard.h"
#include "config.h"
#include <SD_MMC.h>
#include <driver/sdmmc_host.h>
#include <sdmmc_cmd.h>

static bool s_mounted = false;
static bool s_oneBit = false;      // fell back to a 1-bit bus (D1-D3 unusable)
static char s_why[48] = "No card"; // what the Device tab shows when nothing is mounted

// Look at the card without a filesystem: is one there, how big, what is it formatted as, and
// does the 4-bit bus actually read. Fills s_why; returns true if a card answered at all.
static bool probe(bool &fat) {
  fat = false;
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.max_freq_khz = 20000;
  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.clk = (gpio_num_t)SD_CLK_PIN; slot.cmd = (gpio_num_t)SD_CMD_PIN;
  slot.d0 = (gpio_num_t)SD_D0_PIN; slot.d1 = (gpio_num_t)SD_D1_PIN;
  slot.d2 = (gpio_num_t)SD_D2_PIN; slot.d3 = (gpio_num_t)SD_D3_PIN;
  slot.width = 4;
  slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  static sdmmc_card_t card;
  static uint8_t sector[512];
  bool present = false;
  for (int width = 4; width >= 1; width -= 3) {
    slot.width = width;
    if (sdmmc_host_init() != ESP_OK) break;
    bool ok = sdmmc_host_init_slot(host.slot, &slot) == ESP_OK && sdmmc_card_init(&host, &card) == ESP_OK;
    if (!ok) { sdmmc_host_deinit(); snprintf(s_why, sizeof(s_why), "No card"); continue; }
    present = true;
    uint64_t mb = (uint64_t)card.csd.capacity * card.csd.sector_size / (1024 * 1024);
    char size[16];
    if (mb >= 1024) snprintf(size, sizeof(size), "%llu GB", (mb + 512) / 1024); else snprintf(size, sizeof(size), "%llu MB", mb);
    if (sdmmc_read_sectors(&card, sector, 0, 1) != ESP_OK) {
      sdmmc_host_deinit();
      snprintf(s_why, sizeof(s_why), "%s card, %d-bit read failed", size, width);
      Serial.printf("[sd] %s\n", s_why);
      continue;                       // try the narrower bus
    }
    s_oneBit = width == 1;
    sdmmc_host_deinit();
    // MBR partition type, or a bare boot sector
    const char *fs = "unknown format";
    bool mbr = sector[510] == 0x55 && sector[511] == 0xAA;
    uint8_t pt = sector[0x1C2];
    if (!memcmp(sector + 3, "EXFAT   ", 8)) fs = "exFAT";
    else if (!memcmp(sector + 0x52, "FAT32", 5)) { fs = "FAT32"; fat = true; }
    else if (!memcmp(sector + 0x36, "FAT", 3)) { fs = "FAT16"; fat = true; }
    else if (mbr && pt == 0x07) fs = "exFAT/NTFS";
    else if (mbr && (pt == 0x0B || pt == 0x0C)) { fs = "FAT32"; fat = true; }
    else if (mbr && (pt == 0x01 || pt == 0x04 || pt == 0x06 || pt == 0x0E)) { fs = "FAT16"; fat = true; }
    else if (mbr && pt == 0xEE) fs = "GPT";
    else if (mbr && pt == 0) fs = "blank";
    snprintf(s_why, sizeof(s_why), fat ? "%s card, %s, won't mount" : "%s card is %s: use Format", size, fs);
    Serial.printf("[sd] probe: %s (%d-bit bus)\n", s_why, width);
    return true;
  }
  if (!present) Serial.println("[sd] no card");
  return present;
}

static bool mountOnce(bool format) {
  SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN, SD_D1_PIN, SD_D2_PIN, SD_D3_PIN);
  // 20 MHz is safe for the board's long traces
  bool ok = SD_MMC.begin("/sd", s_oneBit, format, 20000);
  if (ok && SD_MMC.cardType() == CARD_NONE) { SD_MMC.end(); ok = false; }
  return ok;
}

bool sdBegin() {
  if (s_mounted) return true;
  s_mounted = mountOnce(false);
  if (!s_mounted) {
    bool fat;
    if (probe(fat) && fat) s_mounted = mountOnce(false);   // probe may have switched to 1-bit
  }
  if (s_mounted) {
    if (!SD_MMC.exists("/apps")) SD_MMC.mkdir("/apps");
    if (!SD_MMC.exists("/update")) SD_MMC.mkdir("/update");
    Serial.printf("[sd] mounted: %llu MB card, %llu MB used%s\n", sdTotalMB(), sdUsedMB(), s_oneBit ? " (1-bit bus)" : "");
  }
  return s_mounted;
}

bool sdFormat() {
  if (s_mounted) { SD_MMC.end(); s_mounted = false; }
  uint32_t t0 = millis();
  s_mounted = mountOnce(true);
  Serial.printf("[sd] format %s in %u ms\n", s_mounted ? "ok" : "failed", (unsigned)(millis() - t0));
  if (s_mounted) {
    SD_MMC.mkdir("/apps");
    SD_MMC.mkdir("/update");
  } else {
    snprintf(s_why, sizeof(s_why), "Format failed");
  }
  return s_mounted;
}

bool sdMounted() { return s_mounted; }
uint64_t sdTotalMB() { return s_mounted ? SD_MMC.cardSize() / (1024 * 1024) : 0; }
uint64_t sdUsedMB() { return s_mounted ? SD_MMC.usedBytes() / (1024 * 1024) : 0; }

void sdStatus(char *buf, size_t n) {
  if (!s_mounted) { snprintf(buf, n, "%s", s_why); return; }
  uint64_t total = sdTotalMB();
  if (total >= 1024) snprintf(buf, n, "%llu GB card, %llu MB used", (total + 512) / 1024, sdUsedMB());
  else snprintf(buf, n, "%llu MB card, %llu MB used", total, sdUsedMB());
}

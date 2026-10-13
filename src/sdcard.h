#pragma once
#include <Arduino.h>

// microSD on the board's 4-bit SDIO bus. Optional: everything works without a card.
// Mounted at /sd. Content packs live in /sd/apps, firmware drops in /sd/update.

bool     sdBegin();                 // try to mount (safe to call again after inserting a card)
bool     sdMounted();
bool     sdFormat();                // erase the card and lay down FAT32, then mount (slow: up to a few minutes)
uint64_t sdTotalMB();
uint64_t sdUsedMB();
void     sdStatus(char *buf, size_t n);   // "15 GB card, 12 MB used" / "No card" / why it would not mount

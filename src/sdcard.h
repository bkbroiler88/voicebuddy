#pragma once
#include <Arduino.h>

// microSD on the board's 4-bit SDIO bus. Optional: everything works without a card.
// Mounted at /sd. Content packs live in /sd/apps, firmware drops in /sd/update.

bool     sdBegin();                 // try to mount (safe to call again after inserting a card)
bool     sdMounted();
uint64_t sdTotalMB();
uint64_t sdUsedMB();
void     sdStatus(char *buf, size_t n);   // "15 GB card, 12 MB used" / "No card"

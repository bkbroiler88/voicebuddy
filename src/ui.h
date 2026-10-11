#pragma once
#include <TFT_eSPI.h>

// Shared between main.cpp (child screen, PIN, loop), settings_ui.cpp and wizard.cpp

enum Screen { SCR_CHILD, SCR_PIN, SCR_SETTINGS, SCR_WIZARD, SCR_NOTICE };
extern Screen screen;

extern TFT_eSPI tft;
extern uint16_t C_SCREEN, C_PANEL, C_TEXT, C_DIM, C_BTN, C_BTN_TX, C_ON, C_OFF,
                C_GOOD, C_WARN, C_LOUD, C_GOLD, C_TAB;

uint16_t rgb(uint8_t r, uint8_t g, uint8_t b);
void drawButton(int x, int y, int w, int h, const char *label, uint16_t bg, uint16_t fg);
void setPixel(uint8_t r, uint8_t g, uint8_t b);

void enterChild();
void enterPinSet();             // change-PIN pad; returns to the Screen settings tab
void applyVad();                // push speech settings + his/her pitch band to the audio task
void applyBrightness();
void resetStarProgress();
float currentLimitDb();         // the too-loud line right now (distance + room adjusted)
float currentLevelDb();         // what the meter shows right now

// settings_ui.cpp
enum { TAB_VOICE, TAB_ROOM, TAB_STARS, TAB_SCREEN, TAB_SETUP, NTABS, TAB_WEB = NTABS };   // TAB_WEB: sub-page of Room
void settingsEnter(int tab);
void settingsRedraw();
bool settingsTap(int x, int y); // true = +/- (auto-repeat while held)
void settingsTick();

// wizard.cpp
enum WizardMode { WIZ_FULL, WIZ_VOICE, WIZ_ZONES, WIZ_WIFI, WIZ_GROWNUP, WIZ_CHECK };
void wizardEnter(WizardMode m);
void wizardTap(int x, int y);
void wizardTick();

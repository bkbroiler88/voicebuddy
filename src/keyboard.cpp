#include "keyboard.h"
#include "ui.h"

#define KW 28
#define KH 23
#define KY 84
#define KROW 26

static bool s_shift = false, s_sym = false;

static const char *const LETTERS[4] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"};
static const char *const SYMBOLS[4] = {"1234567890", "!@#$%^&*()", "-_=+[]{};:", "'\",.<>/?\\"};

static const char *rowKeys(int r) { return s_sym ? SYMBOLS[r] : LETTERS[r]; }

static int rowX0(int r) { return 160 - (int)strlen(rowKeys(r)) * (KW + 2) / 2; }

static char keyChar(int r, int i) {
  char c = rowKeys(r)[i];
  return (s_shift && !s_sym && c >= 'a' && c <= 'z') ? c - 32 : c;
}

void kbReset() { s_shift = false; s_sym = false; }

void kbDraw() {
  tft.fillRect(0, KY - 2, 320, 240 - KY + 2, C_SCREEN);
  tft.setTextDatum(MC_DATUM);
  for (int r = 0; r < 4; r++) {
    int x0 = rowX0(r), n = strlen(rowKeys(r));
    for (int i = 0; i < n; i++) {
      int x = x0 + i * (KW + 2), y = KY + r * KROW;
      tft.fillRoundRect(x, y, KW, KH, 4, TFT_WHITE);
      tft.drawRoundRect(x, y, KW, KH, 4, C_PANEL);
      char s[2] = {keyChar(r, i), 0};
      tft.setTextColor(C_TEXT, TFT_WHITE);
      tft.drawString(s, x + KW / 2, y + KH / 2 + 1, 2);
    }
  }
  int y = KY + 4 * KROW + 2;
  drawButton(10, y, 44, 24, "Aa", s_shift ? C_BTN : C_PANEL, s_shift ? C_BTN_TX : C_TEXT);
  drawButton(58, y, 44, 24, s_sym ? "abc" : "#+=", C_PANEL, C_TEXT);
  tft.fillRoundRect(106, y, 110, 24, 4, TFT_WHITE);
  tft.drawRoundRect(106, y, 110, 24, 4, C_PANEL);
  tft.setTextColor(C_DIM, TFT_WHITE);
  tft.drawString("space", 161, y + 13, 2);
  drawButton(220, y, 44, 24, "Del", C_PANEL, C_TEXT);
  drawButton(268, y, 44, 24, "OK", C_ON, C_BTN_TX);
}

KbResult kbTap(int x, int y, char *buf, size_t cap) {
  size_t len = strlen(buf);
  for (int r = 0; r < 4; r++) {
    int ky = KY + r * KROW;
    if (y < ky || y >= ky + KH + 2) continue;
    int x0 = rowX0(r), n = strlen(rowKeys(r));
    int i = (x - x0) / (KW + 2);
    if (x < x0 || i < 0 || i >= n) return KB_NONE;
    if (len + 1 < cap) { buf[len] = keyChar(r, i); buf[len + 1] = 0; }
    if (s_shift) { s_shift = false; kbDraw(); }
    return KB_CHANGED;
  }
  int by = KY + 4 * KROW + 2;
  if (y < by || y > by + 26) return KB_NONE;
  if (x < 56)       { s_shift = !s_shift; kbDraw(); return KB_NONE; }
  if (x < 104)      { s_sym = !s_sym; s_shift = false; kbDraw(); return KB_NONE; }
  if (x < 218)      { if (len + 1 < cap) { buf[len] = ' '; buf[len + 1] = 0; } return KB_CHANGED; }
  if (x < 266)      { if (len) buf[len - 1] = 0; return KB_CHANGED; }
  return KB_OK;
}

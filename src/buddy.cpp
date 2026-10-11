#include "buddy.h"
#include <math.h>

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

uint16_t lerp565(uint16_t a, uint16_t b, float t) {
  if (t <= 0) return a;
  if (t >= 1) return b;
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  int r = ar + (br - ar) * t, g = ag + (bg - ag) * t, bl = ab + (bb - ab) * t;
  return (r << 11) | (g << 5) | bl;
}

// Thick stroke along a circular arc. Angles in degrees, screen coords (90 = down).
static void arcStroke(TFT_eSPI &d, float cx, float cy, float r, float a0, float a1, int th, uint16_t c) {
  for (float a = a0; a <= a1 + 0.01f; a += 4.f) {
    float rad = a * DEG_TO_RAD;
    d.fillCircle(cx + r * cosf(rad), cy + r * sinf(rad), th, c);
  }
}

static void lineStroke(TFT_eSPI &d, float x0, float y0, float x1, float y1, int th, uint16_t c) {
  float len = hypotf(x1 - x0, y1 - y0);
  int n = max(1, (int)(len / 2));
  for (int i = 0; i <= n; i++) {
    float t = i / (float)n;
    d.fillCircle(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, th, c);
  }
}

// Filled ellipse rotated by angDeg, drawn as horizontal runs
static void fillRotEllipse(TFT_eSPI &d, float cx, float cy, float rx, float ry, float angDeg, uint16_t c) {
  float a = angDeg * DEG_TO_RAD, ca = cosf(a), sa = sinf(a);
  int R = (int)ceilf(fmaxf(rx, ry));
  for (int y = -R; y <= R; y++) {
    int x0 = 9999, x1 = -9999;
    for (int x = -R; x <= R; x++) {
      float u = (x * ca + y * sa) / rx, v = (-x * sa + y * ca) / ry;
      if (u * u + v * v <= 1.f) { if (x < x0) x0 = x; x1 = x; }
    }
    if (x1 >= x0) d.drawFastHLine(cx + x0, cy + y, x1 - x0 + 1, c);
  }
}

void fillStar(TFT_eSPI &d, int cx, int cy, int r, uint16_t col) {
  float px[10], py[10];
  for (int i = 0; i < 10; i++) {
    float rr = (i & 1) ? r * 0.45f : r;
    float a = (-90 + i * 36) * DEG_TO_RAD;
    px[i] = cx + rr * cosf(a);
    py[i] = cy + rr * sinf(a);
  }
  for (int i = 0; i < 10; i++) {
    int j = (i + 1) % 10;
    d.fillTriangle(cx, cy, px[i], py[i], px[j], py[j], col);
  }
}

static void drawConfetti(TFT_eSprite &s, float t) {
  const uint16_t cols[5] = {rgb(255, 190, 40), rgb(120, 200, 255), rgb(255, 140, 180), rgb(140, 220, 140), rgb(200, 160, 255)};
  int W = s.width(), H = s.height();
  for (int i = 0; i < 14; i++) {
    float ph = t * (0.6f + 0.07f * i) + i * 1.7f;
    float x = fmodf(i * 37.f + 13.f * sinf(ph), (float)W);
    float y = fmodf(ph * 40.f + i * 23.f, (float)H);
    fillStar(s, x, y, 5 + (i % 3) * 2, cols[i % 5]);
  }
}

void drawBubble(TFT_eSprite &s, const char *text, int x, int y, int maxW, int tx, int ty) {
  const uint16_t INK = rgb(50, 46, 40);
  const int H = 26;
  int w = min(maxW, (int)s.textWidth(text, 2) + 20);
  int midX = constrain(tx, x + 14, x + w - 14);
  bool tailDown = ty > y + H;
  int by = tailDown ? y + H - 2 : y + 2;
  // tail first (ink), then the body, then the white tail fill so the seam disappears
  s.fillTriangle(midX - 7, by, midX + 7, by, tx, ty, INK);
  s.fillRoundRect(x, y, w, H, 10, INK);
  s.fillRoundRect(x + 2, y + 2, w - 4, H - 4, 8, TFT_WHITE);
  int inset = tailDown ? -3 : 3;
  s.fillTriangle(midX - 4, by + inset, midX + 4, by + inset, tx + (tx > midX ? -1 : 1), ty + (tailDown ? -3 : 3), TFT_WHITE);
  s.setTextDatum(MC_DATUM);
  s.setTextColor(INK, TFT_WHITE);
  s.drawString(text, x + w / 2, y + H / 2 + 1, 2);
}

void drawPageDots(TFT_eSprite &s, int x, int y, int active, int count) {
  const uint16_t on = rgb(50, 46, 40), off = rgb(200, 195, 184);
  for (int i = 0; i < count; i++) {
    if (i == active) { s.fillCircle(x + i * 12, y, 4, TFT_WHITE); s.fillCircle(x + i * 12, y, 3, on); }
    else s.fillCircle(x + i * 12, y, 3, off);
  }
}

// ======================================================================
//  Police pup
// ======================================================================
void drawBuddy(TFT_eSprite &s, Mood m, const BuddyAnim &a) {
  const uint16_t INK    = rgb(27, 27, 27);
  const uint16_t WHITE  = TFT_WHITE;
  const uint16_t BLUE   = rgb(47, 90, 168);
  const uint16_t GOLD   = rgb(255, 190, 40);
  const uint16_t GOLD_D = rgb(216, 154, 16);
  const uint16_t TONGUE = rgb(240, 140, 140);

  s.fillSprite(a.bg);

  float bob;
  if (m == M_SLEEPY)                         bob = 2.f * sinf(a.t * 1.2f);
  else if (m == M_PROUD || m == M_CELEBRATE) bob = -6.f * fabsf(sinf(a.t * 6.f));
  else                                       bob = 2.f * sinf(a.t * 2.2f);

  float cx = s.width() / 2.f, cy = 82 + bob;

  // uniform: body, white collar, black tie, gold badge
  s.fillRoundRect(cx - 48, cy + 52, 96, 80, 22, BLUE);
  s.fillTriangle(cx - 14, cy + 52, cx + 14, cy + 52, cx, cy + 74, WHITE);
  s.fillTriangle(cx - 5, cy + 56, cx + 5, cy + 56, cx, cy + 92, INK);
  s.fillCircle(cx + 28, cy + 74, 9, GOLD);
  fillStar(s, cx + 28, cy + 74, 6, GOLD_D);

  // floppy black ears behind the head
  fillRotEllipse(s, cx - 50, cy + 2, 17, 34, 14, INK);
  fillRotEllipse(s, cx + 50, cy + 2, 17, 34, -14, INK);

  // head + snout with ink outlines
  s.fillEllipse(cx, cy, 52, 50, INK);
  s.fillEllipse(cx, cy, 49, 47, WHITE);
  s.fillEllipse(cx, cy + 26, 30, 20, INK);
  s.fillEllipse(cx, cy + 26, 27, 17, WHITE);

  float ex = 20, ey = cy - 12;

  switch (m) {
    case M_HAPPY:
    case M_PROUD:
    case M_CELEBRATE:
      arcStroke(s, cx - ex, ey + 6, 10, 205, 335, 2, INK);
      arcStroke(s, cx + ex, ey + 6, 10, 205, 335, 2, INK);
      if (m == M_HAPPY) {
        arcStroke(s, cx, cy + 27, 12, 35, 145, 1, INK);
        s.fillEllipse(cx + 4, cy + 42, 5, 6, TONGUE);
      } else {
        s.fillCircle(cx, cy + 32, 14, INK);
        s.fillRect(cx - 15, cy + 17, 31, 15, WHITE);
        s.fillCircle(cx, cy + 40, 5, TONGUE);
      }
      break;

    case M_ALERT: {
      float r = 10 + 2 * a.eyeWide;
      s.fillCircle(cx - ex, ey, r + 3, INK);
      s.fillCircle(cx + ex, ey, r + 3, INK);
      s.fillCircle(cx - ex, ey, r, WHITE);
      s.fillCircle(cx + ex, ey, r, WHITE);
      s.fillCircle(cx - ex, ey + 1, 6, INK);
      s.fillCircle(cx + ex, ey + 1, 6, INK);
      lineStroke(s, cx - ex - 10, ey - 20, cx - ex + 8, ey - 23, 1, INK);
      lineStroke(s, cx + ex - 8, ey - 23, cx + ex + 10, ey - 20, 1, INK);
      s.fillCircle(cx, cy + 37, 6, INK);
      break;
    }

    case M_LOUD:
      lineStroke(s, cx - ex - 9, ey - 7, cx - ex + 7, ey, 2, INK);
      lineStroke(s, cx - ex + 7, ey, cx - ex - 9, ey + 7, 2, INK);
      lineStroke(s, cx + ex + 9, ey - 7, cx + ex - 7, ey, 2, INK);
      lineStroke(s, cx + ex - 7, ey, cx + ex + 9, ey + 7, 2, INK);
      for (int i = 0; i < 4; i++) {
        float x0 = cx - 14 + i * 7;
        arcStroke(s, x0 + 3.5f, cy + 36, 3.5f, (i & 1) ? 0 : 180, (i & 1) ? 180 : 360, 1, INK);
      }
      break;

    case M_SLEEPY:
      arcStroke(s, cx - ex, ey - 4, 10, 30, 150, 2, INK);
      arcStroke(s, cx + ex, ey - 4, 10, 30, 150, 2, INK);
      arcStroke(s, cx, cy + 30, 7, 40, 140, 1, INK);
      {
        s.setTextColor(rgb(122, 110, 170));
        s.setTextDatum(MC_DATUM);
        float zt = fmodf(a.t, 3.f);
        s.drawString("z", cx + 56 + zt * 3, cy - 52 - zt * 6, 2);
        s.drawString("Z", cx + 72 + zt * 2, cy - 70 - zt * 5, 4);
      }
      break;

    case M_CALM:
    default:
      if (fmodf(a.t, 4.2f) < 0.15f) {   // blink
        lineStroke(s, cx - ex - 8, ey, cx - ex + 8, ey, 1, INK);
        lineStroke(s, cx + ex - 8, ey, cx + ex + 8, ey, 1, INK);
      } else {
        s.fillCircle(cx - ex, ey, 8, INK);
        s.fillCircle(cx + ex, ey, 8, INK);
        s.fillCircle(cx - ex + 3, ey - 3, 3, WHITE);
        s.fillCircle(cx + ex + 3, ey - 3, 3, WHITE);
      }
      arcStroke(s, cx, cy + 28, 10, 40, 140, 1, INK);
      break;
  }

  // nose last so the big smile never clips it
  s.fillEllipse(cx, cy + 14, 11, 8, INK);
  s.fillCircle(cx + 3, cy + 11, 2, rgb(120, 120, 120));

  // paws slide up over the ears when it is too loud
  if (a.earCover > 0.01f) {
    float k = a.earCover;
    float pyTop = cy - 22, pyStart = cy + 70;
    float py = pyStart + (pyTop - pyStart) * k;
    float sx[2] = {cx - 59, cx + 59};
    for (int i = 0; i < 2; i++) {
      float px = sx[i] + (i ? -1 : 1) * (1 - k) * 14;
      lineStroke(s, cx + (i ? 40 : -40), cy + 70, px, py + 20, 7, BLUE);   // sleeve
      s.fillRoundRect(px - 15, py - 2, 30, 40, 14, INK);
      s.fillRoundRect(px - 12, py + 1, 24, 34, 11, WHITE);
    }
  }

  if (m == M_PROUD || m == M_CELEBRATE) {
    // beside the head, so it doesn't collide with the speech bubble
    float sy = cy - 56 - 4 * sinf(a.t * 5);
    fillStar(s, cx + 74, sy, 16, GOLD);
    if (m == M_CELEBRATE) drawConfetti(s, a.t);
  }
}

// ======================================================================
//  Dial
// ======================================================================
void drawDial(TFT_eSprite &s, Mood m, const BuddyAnim &a, float kLevel, float kWarn, float kLoud) {
  const uint16_t INK    = rgb(50, 46, 40);
  const uint16_t TICK   = rgb(150, 143, 135);
  const uint16_t GOOD   = rgb(90, 190, 120);
  const uint16_t WARN   = rgb(240, 190, 60);
  const uint16_t LOUD   = rgb(240, 130, 100);
  const uint16_t GOLD   = rgb(255, 190, 40);

  s.fillSprite(a.bg);
  const int cx = s.width() / 2, cy = 140, R = 108, TH = 13;
  auto clamp01 = [](float k) { return k < 0 ? 0.f : (k > 1 ? 1.f : k); };
  // TFT_eSPI arc angles: 0 = 6 o'clock, clockwise. Left end = 90, top = 180, right = 270.
  auto arcDeg = [&](float k) { return (uint32_t)(90 + 180 * clamp01(k)); };

  uint32_t aW = arcDeg(kWarn), aL = arcDeg(kLoud);
  if (aW > 90)  s.drawArc(cx, cy, R + TH, R - TH, 90, aW, GOOD, a.bg, true);
  if (aL > aW)  s.drawArc(cx, cy, R + TH, R - TH, aW, aL, WARN, a.bg, true);
  if (270 > aL) s.drawArc(cx, cy, R + TH, R - TH, aL, 270, LOUD, a.bg, true);

  // ticks every 10% inside the band
  for (int i = 0; i <= 10; i++) {
    float ang = PI * (1 - i / 10.f);
    s.drawLine(cx + (R - 18) * cosf(ang), cy - (R - 18) * sinf(ang),
               cx + (R - 26) * cosf(ang), cy - (R - 26) * sinf(ang), TICK);
  }

  // the "too loud" line
  float la = PI * (1 - clamp01(kLoud));
  lineStroke(s, cx + (R + 16) * cosf(la), cy - (R + 16) * sinf(la),
             cx + (R - 16) * cosf(la), cy - (R - 16) * sinf(la), 2, INK);

  // needle (tiny wobble when too loud, like it's straining)
  float wob = (m == M_LOUD) ? sinf(a.t * 20) * 0.02f : 0;
  float na = PI * (1 - clamp01(kLevel)) + wob;
  lineStroke(s, cx, cy, cx + (R - 32) * cosf(na), cy - (R - 32) * sinf(na), 2, INK);

  // face in the hub
  s.fillCircle(cx, cy, 31, INK);
  s.fillCircle(cx, cy, 28, TFT_WHITE);
  switch (m) {
    case M_LOUD:
      lineStroke(s, cx - 12, cy - 9, cx - 5, cy - 5, 1, INK);
      lineStroke(s, cx - 5, cy - 5, cx - 12, cy - 1, 1, INK);
      lineStroke(s, cx + 12, cy - 9, cx + 5, cy - 5, 1, INK);
      lineStroke(s, cx + 5, cy - 5, cx + 12, cy - 1, 1, INK);
      for (int i = 0; i < 3; i++)
        arcStroke(s, cx - 6 + i * 6, cy + 12, 3, (i & 1) ? 0 : 180, (i & 1) ? 180 : 360, 1, INK);
      break;
    case M_ALERT:
      s.fillCircle(cx - 9, cy - 5, 5, INK);
      s.fillCircle(cx + 9, cy - 5, 5, INK);
      s.fillCircle(cx, cy + 12, 5, INK);
      break;
    case M_SLEEPY:
      arcStroke(s, cx - 9, cy - 8, 5, 30, 150, 1, INK);
      arcStroke(s, cx + 9, cy - 8, 5, 30, 150, 1, INK);
      lineStroke(s, cx - 5, cy + 12, cx + 5, cy + 12, 1, INK);
      break;
    default:
      s.fillCircle(cx - 9, cy - 5, 4, INK);
      s.fillCircle(cx + 9, cy - 5, 4, INK);
      arcStroke(s, cx, cy + 2, m == M_CALM ? 8 : 11, 30, 150, 1, INK);
      break;
  }

  if (m == M_PROUD || m == M_CELEBRATE) {
    fillStar(s, cx, 22 + 3 * sinf(a.t * 5), 15, GOLD);
    if (m == M_CELEBRATE) drawConfetti(s, a.t);
  }
}

// ======================================================================
//  Space views
// ======================================================================
static uint16_t levelColor(float k, float kWarn, float kLoud) {
  if (k > kLoud) return rgb(240, 130, 100);
  if (k > kWarn) return rgb(240, 190, 60);
  return rgb(90, 190, 120);
}

static void drawSky(TFT_eSprite &s, uint16_t bg, float t) {
  s.fillSprite(bg);
  for (int i = 0; i < 26; i++) {
    int x = (i * 67) % 320, y = 4 + (i * 41) % 170;
    float tw = 0.35f + 0.65f * fabsf(sinf(t * 1.5f + i));
    uint16_t c = lerp565(bg, TFT_WHITE, tw);
    if (i % 3 == 2) s.fillCircle(x, y, 1, c); else s.drawPixel(x, y, c);
  }
}

// Shooting star across the top while a star is being celebrated
static void drawShootingStar(TFT_eSprite &s, float t) {
  float sx = 40 + fmodf(t * 120.f, 280.f);
  float sy = 30 - sx * 0.12f;
  lineStroke(s, sx - 40, sy + 40 * 0.12f, sx, sy, 1, rgb(255, 224, 138));
  fillStar(s, sx, sy, 8, rgb(255, 190, 40));
}

// Simple round face used by Saturn and the Sun. base = skin colour under the mouth.
static void drawSimpleFace(TFT_eSprite &s, float cx, float cy, Mood m, uint16_t ink, uint16_t base, float k) {
  float e = 14 * k;
  switch (m) {
    case M_HAPPY:
    case M_PROUD:
    case M_CELEBRATE:
      arcStroke(s, cx - e, cy, 7 * k, 215, 325, 2, ink);
      arcStroke(s, cx + e, cy, 7 * k, 215, 325, 2, ink);
      if (m == M_HAPPY) arcStroke(s, cx, cy + 4 * k, 9 * k, 30, 150, 2, ink);
      else {
        s.fillCircle(cx, cy + 7 * k, 10 * k, ink);
        s.fillRect(cx - 11 * k, cy - 4 * k, 22 * k + 1, 11 * k, base);
      }
      break;
    case M_ALERT:
      s.fillCircle(cx - e, cy - 6 * k, 8 * k + 2, ink);
      s.fillCircle(cx + e, cy - 6 * k, 8 * k + 2, ink);
      s.fillCircle(cx - e, cy - 6 * k, 8 * k, TFT_WHITE);
      s.fillCircle(cx + e, cy - 6 * k, 8 * k, TFT_WHITE);
      s.fillCircle(cx - e, cy - 5 * k, 4 * k, ink);
      s.fillCircle(cx + e, cy - 5 * k, 4 * k, ink);
      s.fillCircle(cx, cy + 12 * k, 5 * k, ink);
      break;
    case M_LOUD:
      lineStroke(s, cx - e - 6 * k, cy - 12 * k, cx - e + 4 * k, cy - 6 * k, 2, ink);
      lineStroke(s, cx - e + 4 * k, cy - 6 * k, cx - e - 6 * k, cy, 2, ink);
      lineStroke(s, cx + e + 6 * k, cy - 12 * k, cx + e - 4 * k, cy - 6 * k, 2, ink);
      lineStroke(s, cx + e - 4 * k, cy - 6 * k, cx + e + 6 * k, cy, 2, ink);
      for (int i = 0; i < 4; i++)
        arcStroke(s, cx - 9 * k + i * 6 * k, cy + 12 * k, 3 * k, (i & 1) ? 0 : 180, (i & 1) ? 180 : 360, 1, ink);
      break;
    case M_SLEEPY:
      arcStroke(s, cx - e, cy - 8 * k, 7 * k, 30, 150, 2, ink);
      arcStroke(s, cx + e, cy - 8 * k, 7 * k, 30, 150, 2, ink);
      lineStroke(s, cx - 5 * k, cy + 10 * k, cx + 5 * k, cy + 10 * k, 1, ink);
      break;
    case M_CALM:
    default:
      s.fillCircle(cx - e, cy - 5 * k, 5 * k, ink);
      s.fillCircle(cx + e, cy - 5 * k, 5 * k, ink);
      s.fillCircle(cx - e + 2 * k, cy - 7 * k, 2 * k, TFT_WHITE);
      s.fillCircle(cx + e + 2 * k, cy - 7 * k, 2 * k, TFT_WHITE);
      arcStroke(s, cx, cy + 3 * k, 8 * k, 40, 140, 2, ink);
      break;
  }
}

static void drawZs(TFT_eSprite &s, float x, float y, float t) {
  s.setTextColor(rgb(184, 176, 232));
  s.setTextDatum(MC_DATUM);
  float zt = fmodf(t, 3.f);
  s.drawString("z", x + zt * 3, y - zt * 6, 2);
  s.drawString("Z", x + 16 + zt * 2, y - 18 - zt * 5, 4);
}

// ---------------- Saturn ----------------
void drawSaturn(TFT_eSprite &s, Mood m, const BuddyAnim &a, float kLevel, float kWarn, float kLoud) {
  const uint16_t TAN = rgb(232, 194, 122), BAND = rgb(212, 166, 90), INK = rgb(74, 52, 32);
  const uint16_t CHEEK = rgb(242, 160, 140), PALE = rgb(233, 217, 176);
  drawSky(s, a.bg, a.t);

  float bob = (m == M_PROUD || m == M_CELEBRATE) ? -6.f * fabsf(sinf(a.t * 6.f)) : 3.f * sinf(a.t * 1.6f);
  float cx = 160, cy = 104 + bob;
  float tilt = (-12.f + (m == M_LOUD ? 6.f * sinf(a.t * 14.f) : 0.f)) * DEG_TO_RAD;
  float ct = cosf(tilt), st = sinf(tilt);
  uint16_t rc = levelColor(kLevel, kWarn, kLoud);
  const float R = 56;

  // point on the tilted ring ellipse
  auto ringPt = [&](float ra, float rb, float th, float &x, float &y) {
    float ex = ra * cosf(th), ey = rb * sinf(th);
    x = cx + ex * ct - ey * st;
    y = cy + ex * st + ey * ct;
  };
  auto drawRing = [&](bool back) {
    float th0 = back ? PI : 0, th1 = back ? 2 * PI : PI;
    float x, y;
    for (float th = th0; th <= th1 + 0.001f; th += 0.05f) { ringPt(106, 27, th, x, y); s.fillCircle(x, y, 6, rc); }
    for (float th = th0; th <= th1 + 0.001f; th += 0.06f) { ringPt(88, 21, th, x, y); s.fillCircle(x, y, 2, PALE); }
  };

  // moons go behind the planet on the far side of their orbit
  float spd = m == M_SLEEPY ? 0.2f : (m == M_LOUD ? 2.4f : 0.8f);
  const float mr[2] = {128, 146}, mph[2] = {0.f, 2.2f}, msz[2] = {7, 5};
  const uint16_t mcol[2] = {rgb(184, 196, 214), rgb(214, 184, 184)};
  float mx[2], my[2];
  bool behind[2];
  for (int i = 0; i < 2; i++) {
    float ang = a.t * spd * (146.f / mr[i]) + mph[i];
    mx[i] = cx + mr[i] * cosf(ang);
    my[i] = cy + mr[i] * 0.32f * sinf(ang);
    behind[i] = sinf(ang) < 0;
  }
  for (int i = 0; i < 2; i++) if (behind[i]) s.fillCircle(mx[i], my[i], msz[i], mcol[i]);

  drawRing(true);

  // planet with two bands
  s.fillCircle(cx, cy, R, TAN);
  for (int dy = -22; dy <= 24; dy++) {
    if (!((dy >= -22 && dy <= -16) || (dy >= 19 && dy <= 24))) continue;
    int hw = (int)sqrtf(R * R - dy * dy);
    s.drawFastHLine(cx - hw, cy + dy, 2 * hw + 1, BAND);
  }
  s.fillEllipse(cx - 34, cy + 10, 9, 6, CHEEK);
  s.fillEllipse(cx + 34, cy + 10, 9, 6, CHEEK);
  drawSimpleFace(s, cx, cy, m, INK, TAN, 1.1f);

  drawRing(false);
  for (int i = 0; i < 2; i++) if (!behind[i]) s.fillCircle(mx[i], my[i], msz[i], mcol[i]);

  if (m == M_SLEEPY) drawZs(s, cx + 62, cy - 60, a.t);
  if (m == M_PROUD || m == M_CELEBRATE) drawShootingStar(s, a.t);
  if (m == M_CELEBRATE) drawConfetti(s, a.t);
}

// ---------------- Solar system ----------------
void drawSolar(TFT_eSprite &s, Mood m, const BuddyAnim &a, float kLevel, float kWarn, float kLoud) {
  const int pr[8] = {6, 9, 10, 8, 17, 14, 11, 11};
  const uint16_t pc[8] = {rgb(185, 178, 168), rgb(230, 197, 138), rgb(90, 160, 230), rgb(217, 113, 78),
                          rgb(216, 180, 138), rgb(232, 194, 122), rgb(143, 214, 220), rgb(91, 127, 224)};
  const int xs[8] = {70, 96, 122, 150, 190, 236, 272, 300};
  const uint16_t SUN = rgb(255, 200, 60), INK = rgb(122, 74, 16);
  const uint16_t WARN = rgb(240, 190, 60), LOUD = rgb(240, 130, 100);
  drawSky(s, a.bg, a.t);

  const int scx = -6, scy = 120;
  s.fillCircle(scx, scy, 54 + 2 * sinf(a.t * 3), lerp565(a.bg, rgb(255, 217, 112), 0.35f));
  s.fillCircle(scx, scy, 48, SUN);
  drawSimpleFace(s, scx + 18, scy - 2, m, INK, SUN, 0.75f);

  // Mercury..Saturn light up below the loud line, Uranus and Neptune above it
  auto kAt = [&](int i) {
    if (i < 6) return kLoud * (i + 0.5f) / 6.f;
    return kLoud + (1.f - kLoud) * (i - 5.5f) / 2.5f;
  };
  int lx = (xs[5] + xs[6]) / 2 + 6;
  for (int y = scy - 44; y < scy + 40; y += 9) s.fillRect(lx - 1, y, 3, 5, TFT_WHITE);

  uint16_t dim = lerp565(a.bg, rgb(120, 124, 150), 0.45f);
  for (int i = 0; i < 8; i++) {
    float ki = kAt(i);
    bool on = kLevel >= ki;
    float y = scy + ((m == M_LOUD && on && i >= 6) ? 3.f * sinf(a.t * 16.f + i) : 0.f);
    if (on && i >= 6)           s.fillCircle(xs[i], y, pr[i] + 5, lerp565(a.bg, LOUD, 0.6f));
    else if (on && ki >= kWarn) s.fillCircle(xs[i], y, pr[i] + 5, lerp565(a.bg, WARN, 0.6f));
    s.fillCircle(xs[i], y, pr[i], on ? pc[i] : dim);
    if (i == 2 && on) s.fillCircle(xs[i] - 3, y - 2, 4, rgb(108, 192, 112));   // land on Earth
    if (i == 5) {                                                                // ring on Saturn
      uint16_t rcol = on ? rgb(233, 217, 176) : lerp565(a.bg, rgb(140, 144, 170), 0.45f);
      s.drawEllipse(xs[i], y, pr[i] + 9, 4, rcol);
      s.drawEllipse(xs[i], y, pr[i] + 8, 3, rcol);
    }
  }
  s.setTextDatum(MC_DATUM);
  s.setTextColor(rgb(207, 211, 230));
  s.drawString("too loud", lx, scy + 54, 2);

  if (m == M_SLEEPY) drawZs(s, 54, 72, a.t);
  if (m == M_PROUD || m == M_CELEBRATE) drawShootingStar(s, a.t);
  if (m == M_CELEBRATE) drawConfetti(s, a.t);
}

#include "render.h"
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include "brain.h"
#include "config.h"
#include "net.h"
#include "physics.h"

namespace Render {

Mode mode = RM_EGG;
char eggText[48] = "";
float eggCrack = 0;
char signText[24] = "";

// clkDuring = clkAfter so the IMU (same bus) also gets 400 kHz
static Adafruit_SSD1306 oled(SCREEN_W, SCREEN_H, &Wire, -1, I2C_CLOCK, I2C_CLOCK);
static uint32_t flashUntil = 0, shakeUntil = 0, boltUntil = 0;
static uint8_t shakeAmp = 0;
static bool inverted = false;
static int8_t boltX[7];
static uint32_t nowMs = 0;

bool ok = false;

bool begin() {
  ok = oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (!ok) return false;
  oled.cp437(true);
  oled.setTextWrap(false);
  oled.clearDisplay();
  oled.display();
  return true;
}

void test() {
  ok = begin();
  Serial.print(F("[oled] begin="));
  Serial.println(ok);
  oled.fillScreen(WHITE);
  oled.display();
  delay(1500);
  oled.clearDisplay();
  oled.setTextColor(WHITE);
  oled.setTextSize(2);
  oled.setCursor(10, 24);
  oled.print(F("HELLO??"));
  oled.display();
  delay(1500);
}

void flash(uint16_t ms) { flashUntil = millis() + ms; }

void shake(uint16_t ms, uint8_t amp) {
  shakeUntil = millis() + ms;
  shakeAmp = amp;
}

void bolt() {
  boltUntil = millis() + 160;
  int x = random(20, 108);
  for (int i = 0; i < 7; i++) {
    boltX[i] = (int8_t)clampf(x, 1, 126);
    x += random(-7, 8);
  }
}

// ---------------------------------------------------------------------------
// pet-local drawing: (lx, ly) with +y toward the pet's floor
// ---------------------------------------------------------------------------
static float PX, PY;
static uint8_t PF;

static void lpt(float lx, float ly, int16_t &x, int16_t &y) {
  Vec2 o = World::rot(PF, lx, ly);
  x = (int16_t)lroundf(PX + o.x);
  y = (int16_t)lroundf(PY + o.y);
}

static void lpix(float lx, float ly, uint16_t c) {
  int16_t x, y;
  lpt(lx, ly, x, y);
  oled.drawPixel(x, y, c);
}

static void lline(float x0, float y0, float x1, float y1, uint16_t c) {
  int16_t a, b, d, e;
  lpt(x0, y0, a, b);
  lpt(x1, y1, d, e);
  oled.drawLine(a, b, d, e, c);
}

static void lrect(float x, float y, int w, int h, uint16_t c) {
  for (int i = 0; i < h; i++) lline(x, y + i, x + w - 1, y + i, c);
}

static void lfillEllipse(float cx, float cy, float rx, float ry, uint16_t c, bool topOnly = false) {
  if (rx < 0.5f || ry < 0.5f) return;
  int iry = (int)ceilf(ry);
  for (int dy = -iry; dy <= (topOnly ? 0 : iry); dy++) {
    float k = 1 - (dy * dy) / (ry * ry);
    if (k < 0) continue;
    float hw = rx * sqrtf(k);
    lline(cx - hw, cy + dy, cx + hw, cy + dy, c);
  }
}

static void lfillCircle(float lx, float ly, int r, uint16_t c) {
  int16_t x, y;
  lpt(lx, ly, x, y);
  oled.fillCircle(x, y, r, c);
}

static void lcircle(float lx, float ly, int r, uint16_t c) {
  int16_t x, y;
  lpt(lx, ly, x, y);
  oled.drawCircle(x, y, r, c);
}

static void lpoint(float lx, float ly, int16_t &x, int16_t &y) { lpt(lx, ly, x, y); }

static void tinyHeart(int x, int y, uint16_t c) {
  oled.drawPixel(x - 1, y - 1, c);
  oled.drawPixel(x + 1, y - 1, c);
  oled.drawFastHLine(x - 2, y, 5, c);
  oled.drawFastHLine(x - 1, y + 1, 3, c);
  oled.drawPixel(x, y + 2, c);
}

static void printClock(int16_t x, int16_t y) {
  int m = Net::localMinutes();
  char buf[12];
  if (m < 0) snprintf(buf, sizeof(buf), "--:--");
  else snprintf(buf, sizeof(buf), "%02d:%02d", m / 60, m % 60);
  oled.setCursor(x, y);
  oled.print(buf);
}

// ---------------------------------------------------------------------------
// the pet: a dark, round-faced cat drawn as a lit outline, so the face is lit too
// ---------------------------------------------------------------------------
static const uint16_t INK = WHITE;    // face features
static const uint16_t PAPER = BLACK;  // fur

static void drawEye(float x, float y, int side, Eyes e, float k) {
  int w = k > 1.4f ? 2 : 1;
  switch (e) {
    case EY_OPEN: {
      // big glossy eye: lit rim, dark pupil, shine top-left and a glint bottom-right
      int r = (int)lroundf(3 * k);
      lcircle(x, y, r, INK);
      lrect(x - w, y - w, w, w, INK);
      lpix(x + w, y + w, INK);
      break;
    }
    case EY_CLOSED: lline(x - 1.5f, y, x + 1.5f, y, INK); break;
    case EY_HAPPY:
      lpix(x - 2, y + 1, INK);
      lpix(x - 1, y, INK);
      lpix(x, y - 1, INK);
      lpix(x + 1, y, INK);
      lpix(x + 2, y + 1, INK);
      break;
    case EY_ANGRY:
      lrect(x - w, y - w + 1, 2 * w, 2 * w, INK);
      lline(x + side * 2, y - 3, x - side, y - 2, INK);
      break;
    case EY_X:
      lline(x - 1.5f, y - 1.5f, x + 1.5f, y + 1.5f, INK);
      lline(x - 1.5f, y + 1.5f, x + 1.5f, y - 1.5f, INK);
      break;
    case EY_SPIRAL: {
      lcircle(x, y, 2, INK);
      float a = nowMs / 90.0f + side;
      lpix(x + cosf(a), y + sinf(a), INK);
      break;
    }
    case EY_WIDE: {
      lfillCircle(x, y, 3 * w, INK);
      lpix(x + Brain::look.lookX, y, PAPER);
      break;
    }
    case EY_SLEEPY:
      lline(x - 1.5f, y, x + 1.5f, y, INK);
      lpix(x, y + 1, INK);
      break;
    case EY_HEART:
      lpix(x - 1, y - 1, INK);
      lpix(x + 1, y - 1, INK);
      lline(x - 2, y, x + 2, y, INK);
      lline(x - 1, y + 1, x + 1, y + 1, INK);
      lpix(x, y + 2, INK);
      break;
    case EY_SQUINT:
      lline(x - side, y - 1, x + side, y, INK);
      lline(x + side, y, x - side, y + 1, INK);
      break;
    case EY_SUS:
      lline(x - 2, y - 1, x + 2, y - 1, INK);
      lrect(x - 1, y, 2, 1, INK);
      break;
  }
}

static void drawMouth(float mx, float my, Mouth m) {
  switch (m) {
    case MO_NONE: break;
    case MO_SMILE:
      lpix(mx - 2, my - 1, INK);
      lline(mx - 1, my, mx + 1, my, INK);
      lpix(mx + 2, my - 1, INK);
      break;
    case MO_FROWN:
      lpix(mx - 2, my + 1, INK);
      lline(mx - 1, my, mx + 1, my, INK);
      lpix(mx + 2, my + 1, INK);
      break;
    case MO_O:
      lrect(mx - 1, my - 1, 3, 3, INK);
      lpix(mx, my, PAPER);
      break;
    case MO_SCREAM: lfillEllipse(mx, my + 1, 2, 3, INK); break;
    case MO_TEETH:
      lrect(mx - 3, my - 1, 7, 3, INK);
      lpix(mx - 2, my - 1, PAPER);
      lpix(mx, my - 1, PAPER);
      lpix(mx + 2, my - 1, PAPER);
      lpix(mx - 1, my + 1, PAPER);
      lpix(mx + 1, my + 1, PAPER);
      break;
    case MO_WAVY:
      for (int i = -3; i <= 3; i++) lpix(mx + i, my + ((i & 1) ? 0 : 1), INK);
      break;
    case MO_TONGUE:
      lline(mx - 2, my, mx + 2, my, INK);
      lrect(mx, my + 1, 2, 2, INK);
      break;
    case MO_FLAT: lline(mx - 2, my, mx + 2, my, INK); break;
    case MO_GRIN:
      lline(mx - 3, my - 1, mx + 3, my - 1, INK);
      lline(mx - 2, my, mx + 2, my, INK);
      lline(mx - 1, my + 1, mx + 1, my + 1, INK);
      break;
  }
}

static void drawPet() {
  const Look &L = Brain::look;
  const World::Body &B = World::pet;
  if (L.hidden) return;

  PF = B.floor;
  PX = B.p.x;
  PY = B.p.y;
  if (L.shiver > 0) {
    PX += frand(-L.shiver, L.shiver);
    PY += frand(-L.shiver * 0.5f, L.shiver * 0.5f);
  }
  float k = L.scale;
  float q = B.squash;
  float bob = (B.walking && B.grounded && fmodf(nowMs / 1000.0f, 0.25f) < 0.12f) ? -1 : 0;
  float rx = 11 * k * (1 + q + L.melt * 0.6f);
  float ry = 9 * k * (1 - q) * (1 - L.melt * 0.7f);
  float cy = 9 - ry + bob;  // body bottom stays at local y = 9
  if (k > 1) cy = 0;        // pressed against the glass: grow from the middle
  float top = cy - ry;

  // feet
  if (L.melt < 0.3f) {
    lfillEllipse(-5 * k, 10, 2.5f, 1.2f, WHITE);
    lfillEllipse(5 * k, 10, 2.5f, 1.2f, WHITE);
  }
  // head + folded ears as one shape: fill it all lit, then hollow it out 1px in,
  // which leaves a single outline around the union
  bool ears = L.melt < 0.5f;
  float erx = 3 * k, ery = 2.5f * k, eyy = top + 0.5f * k;
  if (ears)
    for (int side = -1; side <= 1; side += 2) lfillEllipse(side * 7.5f * k, eyy, erx, ery, WHITE);
  lfillEllipse(0, cy, rx, ry, WHITE);
  if (ears)
    for (int side = -1; side <= 1; side += 2) lfillEllipse(side * 7.5f * k, eyy, erx - 1, ery - 1, BLACK);
  lfillEllipse(0, cy, rx - 1, ry - 1, BLACK);

  // feral fur
  if (Brain::s.feral) {
    for (int i = 0; i < 14; i++) {
      float a = i * 0.449f + ((nowMs / 200 + i) % 3) * 0.05f;
      float ox = cosf(a) * rx, oy = sinf(a) * ry;
      lline(ox, cy + oy, ox * 1.25f, cy + oy * 1.3f, WHITE);
    }
  }

  // face
  if (L.back) {
    lline(-1, top + 3, 0, top + 5, INK);  // a cowlick. that's the back of its head.
    lline(1, top + 3, 0, top + 5, INK);
  } else {
    float lx = L.lookX * 1.5f * k;
    float ey = cy - 1 * k;
    Eyes e = L.eyes;
    if ((e == EY_OPEN || e == EY_SUS || e == EY_ANGRY) && (nowMs % 3700) < 130) e = EY_CLOSED;
    if (L.props & PR_SHADES) {
      lrect(-7 * k + lx, ey - 2, 5, 3, INK);
      lrect(3 * k + lx, ey - 2, 5, 3, INK);
      lline(-3 * k + lx, ey - 2, 3 * k + lx, ey - 2, INK);
    } else {
      drawEye(-5 * k + lx, ey, -1, e, k);
      drawEye(5 * k + lx, ey, 1, e, k);
    }
    drawMouth(L.lookX * k, cy + 3 * k, L.mouth);
    if (L.blush) {
      lline(-8 * k, cy + 4 * k, -7 * k, cy + 3 * k, INK);
      lline(7 * k, cy + 3 * k, 8 * k, cy + 4 * k, INK);
    }
  }

  // a poop swirl on its head, unless something else is up there
  uint16_t pr = L.props;
  if (!(pr & (PR_HAT | PR_CROWN | PR_SOCK | PR_ANTENNA | PR_UMBRELLA))) {
    static const char *const POOP[] = {
        ".....##....",
        "....##.....",
        "...#####...",
        "..##...##..",
        "..#######..",
        ".##.....##.",
        ".#########.",
        "##.......##",
        ".#########.",
    };
    int s = (int)ceilf(k);
    for (int r = 0; r < 9; r++)
      for (int c = 0; c < 11; c++)
        if (POOP[r][c] == '#') lrect((c - 5) * k, top + (r - 8) * k, s, s, WHITE);
  }

  // props
  if (pr & PR_BLANKET) {
    for (float y = cy; y <= cy + ry + 1; y++)
      for (float x = -rx - 2; x <= rx + 2; x++)
        if (((int)(x + y) & 1) == 0) lpix(x, y, ((int)(x + 2 * y) & 3) ? BLACK : WHITE);
    lline(-rx - 2, cy, rx + 2, cy, WHITE);
  }
  if (pr & PR_SCARF) {
    lline(-rx + 4, cy + 6, rx - 4, cy + 6, INK);
    lline(-rx + 4, cy + 7, rx - 4, cy + 7, INK);
    lline(-4, cy + 7, -6, cy + 10, INK);
  }
  if (pr & PR_UMBRELLA) {
    float ux = -L.lookX * 3, uy = top - 7;
    lline(ux, uy, ux, top - 1, WHITE);
    lfillEllipse(ux, uy, 13, 6, WHITE, true);
    for (int i = -12; i <= 12; i += 4) lpix(ux + i, uy, BLACK);
    lpix(ux, uy - 7, WHITE);
  }
  if (pr & PR_ANTENNA) {
    lline(3, top, 5, top - 8, WHITE);
    if ((nowMs / 300) & 1) lfillCircle(5, top - 9, 1, WHITE);
    else lcircle(5, top - 9, 1, WHITE);
  }
  if (pr & PR_HAT) {
    for (int b = -4; b <= 4; b++) lline(1, top - 9, 1 + b, top + 1, WHITE);
    lline(-1, top - 2, 3, top - 4, BLACK);
    lfillCircle(1, top - 10, 1, WHITE);
  }
  if (pr & PR_CROWN) {
    lrect(-5, top - 2, 11, 2, WHITE);
    lline(-5, top - 2, -5, top - 5, WHITE);
    lline(0, top - 2, 0, top - 6, WHITE);
    lline(5, top - 2, 5, top - 5, WHITE);
  }
  if (pr & PR_SOCK) {
    lrect(-2, top - 7, 4, 6, WHITE);
    lrect(-2, top - 3, 7, 3, WHITE);
    lline(-2, top - 5, 1, top - 5, BLACK);
  }
  if (pr & PR_PEN) lline(L.lookX * 9, cy + 2, L.lookX * 12, cy - 3, WHITE);
  if (pr & PR_CLOCK) {
    int16_t x, y;
    lpoint(0, top - 12, x, y);
    x = constrain(x - 16, 0, SCREEN_W - 33);
    y = constrain(y - 5, 0, SCREEN_H - 11);
    oled.fillRect(x, y, 33, 11, BLACK);
    oled.drawRect(x, y, 33, 11, WHITE);
    oled.setTextColor(WHITE);
    printClock(x + 2, y + 2);
  }
}

// ---------------------------------------------------------------------------
// the world around it
// ---------------------------------------------------------------------------
static void drawCloud(int x, int y) {
  static const int8_t C[3][3] = {{0, 2, 5}, {6, 0, 6}, {12, 2, 5}};
  for (auto &c : C) oled.fillCircle(x + c[0], y + c[1], c[2], WHITE);
  for (auto &c : C) oled.fillCircle(x + c[0], y + c[1], c[2] - 1, BLACK);
}

static void drawSky() {
  const Net::Weather &w = Net::wx;
  if (!w.valid) return;
  bool dark = Net::isDark();
  if (dark && (w.kind == W_CLEAR || w.kind == W_CLOUDY)) {
    uint32_t seed = 12345;
    for (int i = 0; i < 18; i++) {
      seed = seed * 1103515245UL + 12345;
      int x = (seed >> 8) % SCREEN_W;
      int y = (seed >> 20) % 30;
      if ((nowMs / 400 + i) % 7) oled.drawPixel(x, y, WHITE);
    }
    if (w.kind == W_CLEAR) {
      float p = Net::moonPhase();
      float illum = 1 - fabsf(p - 0.5f) * 2;
      oled.fillCircle(114, 9, 5, WHITE);
      if (illum < 0.97f) oled.fillCircle(114 + (p < 0.5f ? -1 : 1) * (int)(illum * 11), 9, 5, BLACK);
    }
  } else if (w.kind == W_CLEAR) {
    oled.fillCircle(114, 9, 4, WHITE);
    float a0 = nowMs / 2000.0f;
    for (int i = 0; i < 8; i++) {
      float a = a0 + i * 0.785f;
      oled.drawLine(114 + cosf(a) * 6, 9 + sinf(a) * 6, 114 + cosf(a) * 8, 9 + sinf(a) * 8, WHITE);
    }
  }
  if (w.kind != W_CLEAR && w.kind != W_FOG && w.kind != W_UNKNOWN) {
    int drift = (int)(nowMs / 180);
    drawCloud((drift % 170) - 20, 4);
    drawCloud(((drift + 85) % 170) - 20, 9);
  }
}

static void drawParticles() {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    const World::Particle &p = World::parts[i];
    int x = (int)p.x, y = (int)p.y;
    switch (p.type) {
      case World::P_RAIN: oled.drawLine(x, y, x - p.vx * 0.03f, y - p.vy * 0.03f, WHITE); break;
      case World::P_SNOW:
      case World::P_DRIP:
      case World::P_CRUMB:
      case World::P_SPARK: oled.drawPixel(x, y, WHITE); break;
      case World::P_PUKE: oled.fillRect(x, y, 2, 2, WHITE); break;
      case World::P_STAR:
        oled.drawFastHLine(x - 1, y, 3, WHITE);
        oled.drawFastVLine(x, y - 1, 3, WHITE);
        break;
      case World::P_HEART: tinyHeart(x, y, WHITE); break;
      case World::P_ZZZ:
        oled.drawFastHLine(x, y, 3, WHITE);
        oled.drawLine(x + 2, y, x, y + 2, WHITE);
        oled.drawFastHLine(x, y + 2, 3, WHITE);
        break;
      case World::P_WIND: oled.drawFastHLine(x, y, 5, WHITE); break;
      default: break;
    }
  }
}

static void drawGround() {
  for (int x = 0; x < SCREEN_W; x++)
    if (World::snowPile[x]) oled.drawFastVLine(x, SCREEN_H - World::snowPile[x], World::snowPile[x], WHITE);

  if (Brain::snowmanStage && Brain::snowmanX >= 0) {
    int x = Brain::snowmanX;
    oled.fillCircle(x, 59, 4, WHITE);
    if (Brain::snowmanStage > 1) oled.fillCircle(x, 52, 3, WHITE);
    if (Brain::snowmanStage > 2) {
      oled.fillCircle(x, 46, 2, WHITE);
      oled.drawPixel(x - 1, 45, BLACK);
      oled.drawPixel(x + 1, 45, BLACK);
      oled.drawFastHLine(x + 3, 46, 2, WHITE);  // carrot, allegedly
    }
  }

  for (int i = 0; i < MAX_PUDDLES; i++) {
    const World::Puddle &p = World::puddles[i];
    if (!p.active) continue;
    oled.fillCircle((int)p.x, (int)p.y, p.size / 2 + 1, WHITE);
    oled.drawPixel((int)p.x + p.size, (int)p.y, WHITE);
    oled.drawPixel((int)p.x - p.size, (int)p.y - 1, WHITE);
  }

  if (World::food.active) {
    oled.fillCircle((int)World::food.x, (int)World::food.y, 2, WHITE);
    oled.drawPixel((int)World::food.x, (int)World::food.y, BLACK);
  }
}

static void drawHud() {
  if (Brain::clockStolenUntil || Net::localMinutes() < 0) return;
  oled.setTextColor(WHITE, BLACK);
  printClock(1, 1);
  if (Net::wx.valid) {
    oled.print(' ');
    oled.print((int)lroundf(Net::wx.tempC));
    oled.print('C');
  }
}

static void drawBolt() {
  if (nowMs > boltUntil) return;
  for (int i = 0; i < 6; i++) oled.drawLine(boltX[i], i * 7, boltX[i + 1], (i + 1) * 7, WHITE);
}

// ---------------------------------------------------------------------------
// overlays
// ---------------------------------------------------------------------------
static void drawBubble() {
  if (nowMs > Brain::bubbleUntil || !Brain::bubble[0]) return;
  // greedy word wrap into up to 3 lines of 20 chars
  char lines[3][21];
  int n = 0, maxLen = 0;
  const char *s = Brain::bubble;
  while (*s && n < 3) {
    while (*s == ' ') s++;
    int len = strlen(s);
    int cut = len;
    if (len > 20) {
      cut = 20;
      while (cut > 0 && s[cut] != ' ') cut--;
      if (cut == 0) cut = 20;
    }
    memcpy(lines[n], s, cut);
    lines[n][cut] = 0;
    maxLen = max(maxLen, cut);
    n++;
    s += cut;
  }
  if (!n) return;
  int w = maxLen * 6 + 5, h = n * 9 + 3;
  int ax = 64, ay = 48;
  if (!Brain::look.hidden && !World::offscreen()) {
    ax = (int)World::pet.p.x;
    ay = (int)World::pet.p.y;
  }
  int bx = constrain(ax - w / 2, 0, SCREEN_W - w);
  int by = ay - 16 - h;
  bool below = false;
  bool hidden = Brain::look.hidden || World::offscreen();
  if (hidden) by = 0;  // pet's gone: talk from the top of the screen
  else if (by < 0) {
    by = ay + 15;
    below = true;
    if (by + h > SCREEN_H) by = max(0, SCREEN_H - h);
  }
  oled.fillRoundRect(bx, by, w, h, 3, BLACK);
  oled.drawRoundRect(bx, by, w, h, 3, WHITE);
  int tx = constrain(ax, bx + 3, bx + w - 4);
  if (hidden) {
  } else if (below) {
    oled.drawLine(tx, by, tx, by - 3, WHITE);
  } else {
    oled.drawLine(tx, by + h - 1, tx, by + h + 2, WHITE);
  }
  oled.setTextColor(WHITE);
  for (int i = 0; i < n; i++) {
    oled.setCursor(bx + 3, by + 2 + i * 9);
    oled.print(lines[i]);
  }
}

static void drawSign() {
  int len = max(3, (int)strlen(signText));
  int w = len * 6 + 6, x = 64 - w / 2, y = 30;
  oled.drawFastVLine(64, y + 20, SCREEN_H - y - 20, WHITE);
  oled.fillRect(x, y, w, 20, BLACK);
  oled.drawRect(x, y, w, 20, WHITE);
  oled.setTextColor(WHITE);
  oled.setCursor(64 - 9, y + 2);
  oled.print("BRB");
  oled.setCursor(x + 3, y + 11);
  oled.print(signText);
}

static void fog() {
  uint8_t *buf = oled.getBuffer();
  float off = nowMs / 60.0f;
  for (int x = 0; x < SCREEN_W; x++) {
    if (sinf((x + off) / 14.0f) < -0.1f) continue;
    uint8_t mask = ((x + (int)(off / 4)) & 1) ? 0x55 : 0xAA;
    for (int p = 0; p < SCREEN_H / 8; p++) buf[p * SCREEN_W + x] &= mask;
  }
}

static void shiftBuffer(int dx, int dy) {
  uint8_t *buf = oled.getBuffer();
  const int pages = SCREEN_H / 8;
  if (dx) {
    for (int p = 0; p < pages; p++) {
      uint8_t *row = buf + p * SCREEN_W;
      if (dx > 0) {
        memmove(row + dx, row, SCREEN_W - dx);
        memset(row, 0, dx);
      } else {
        memmove(row, row - dx, SCREEN_W + dx);
        memset(row + SCREEN_W + dx, 0, -dx);
      }
    }
  }
  if (dy) {
    for (int x = 0; x < SCREEN_W; x++) {
      uint64_t col = 0;
      for (int p = 0; p < pages; p++) col |= (uint64_t)buf[p * SCREEN_W + x] << (p * 8);
      col = dy > 0 ? col << dy : col >> -dy;
      for (int p = 0; p < pages; p++) buf[p * SCREEN_W + x] = (uint8_t)(col >> (p * 8));
    }
  }
}

// ---------------------------------------------------------------------------
// screens
// ---------------------------------------------------------------------------
static void drawEgg() {
  // instructions
  oled.setTextColor(WHITE);
  const char *t = eggText;
  for (int line = 0; line < 2 && *t; line++) {
    const char *nl = strchr(t, '\n');
    int len = nl ? nl - t : strlen(t);
    oled.setCursor(64 - len * 3, 2 + line * 10);
    for (int i = 0; i < len; i++) oled.write(t[i]);
    t = nl ? nl + 1 : t + len;
  }
  int wob = eggCrack > 0 ? (int)(sinf(nowMs / 60.0f) * 2 * eggCrack) : (int)(sinf(nowMs / 400.0f) * 1.5f);
  int cx = 64 + wob, cy = 46;
  PF = 0;
  PX = cx;
  PY = cy;
  lfillEllipse(0, 0, 10, 13, WHITE);
  lpix(-4, -5, BLACK);
  lpix(3, 2, BLACK);
  lpix(-2, 6, BLACK);
  if (eggCrack > 0) {
    int segs = (int)(eggCrack * 6);
    for (int i = 0; i < segs && i < 6; i++) lline(-10 + i * 3.5f, (i & 1) ? -1 : 1, -6.5f + i * 3.5f, (i & 1) ? 1 : -1, BLACK);
  }
}

static void drawCrash() {
  oled.drawRect(8, 8, 112, 48, WHITE);
  oled.fillRect(8, 8, 112, 10, WHITE);
  oled.setTextColor(BLACK);
  oled.setCursor(11, 9);
  oled.print("pet.exe");
  oled.setCursor(110, 9);
  oled.print('x');
  oled.setTextColor(WHITE);
  oled.setCursor(14, 22);
  oled.print("has stopped");
  oled.setCursor(14, 31);
  oled.print("responding.");
  oled.drawRect(44, 42, 40, 11, WHITE);
  oled.setCursor(58, 44);
  oled.print("OK");
  for (int i = 0; i < 3; i++)
    if (random(10) < 4) oled.fillRect(random(0, SCREEN_W), random(0, SCREEN_H), random(5, 50), 1, SSD1306_INVERSE);
}

static void drawDark() {
  // lights are off. you can only see its eyes.
  PF = World::pet.floor;
  PX = World::pet.p.x;
  PY = World::pet.p.y;
  if ((nowMs % 3000) > 150) {
    lcircle(-5, -1, 2, WHITE);
    lcircle(5, -1, 2, WHITE);
  }
}

void frame(uint32_t now) {
  nowMs = now;
  oled.setRotation(World::flipped ? 2 : 0);
  oled.clearDisplay();

  switch (mode) {
    case RM_EGG: drawEgg(); break;
    case RM_CRASH: drawCrash(); break;
    case RM_DARK:
      drawDark();
      drawBubble();
      break;
    case RM_BRB:
    case RM_WORLD:
      drawSky();
      oled.drawBitmap(0, 0, World::graffiti.getBuffer(), SCREEN_W, SCREEN_H, WHITE);
      drawGround();
      if (mode == RM_WORLD) drawPet();
      drawParticles();
      drawHud();
      if (Net::wx.valid && Net::wx.kind == W_FOG) fog();
      drawBolt();
      if (mode == RM_BRB) drawSign();
      drawBubble();
      break;
  }

  if (now < shakeUntil) shiftBuffer(random(-shakeAmp, shakeAmp + 1), random(-1, 2));

  bool inv = now < flashUntil;
  if (inv != inverted) {
    inverted = inv;
    oled.invertDisplay(inv);
  }
  oled.display();
}

}  // namespace Render

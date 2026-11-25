// Abyx: drawing layer (included by abyx.cpp only).
//
// Everything is drawn straight into the 16-bit sprite buffer (much faster than
// TFT_eSPI's per-pixel calls). Landscape turns the panel (orient.h), so the
// sprite is simply 240x135 then and the buffer is always row-major. Colours in
// the buffer are byte-swapped RGB565, so the palettes below are stored swapped
// once and the inner loops never swap.
#pragma once
#include "common.h"
#include "orient.h"

namespace abyx {

// ---------------- colours ----------------
#define AX_SW(c) ((uint16_t)((((c) >> 8) & 0xFF) | (((c) & 0xFF) << 8)))
static inline uint16_t sw16(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }
static inline uint16_t rgb(int r, int g, int b) {   // 0..255 each, plain RGB565
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
// blend plain RGB565 a -> b by t/32 (the usual 0x07E0F81F spread trick)
static inline uint16_t mix565(uint16_t a, uint16_t b, uint32_t t) {
  uint32_t A = (a | ((uint32_t)a << 16)) & 0x07E0F81F;
  uint32_t B = (b | ((uint32_t)b << 16)) & 0x07E0F81F;
  uint32_t r = ((((B - A) * t) >> 5) + A) & 0x07E0F81F;
  return (uint16_t)(r | (r >> 16));
}
// the same on swapped values
static inline uint16_t mixS(uint16_t a, uint16_t b, uint32_t t) { return sw16(mix565(sw16(a), sw16(b), t)); }

// ---------------- frame buffer ----------------
struct Fb { uint16_t *buf; int w, h; };
static Fb fb;

// after every orientation change (the sprite is recreated)
static void fbSetup() {
  fb.buf = (uint16_t *)spr.getPointer();
  fb.w = spr.width();
  fb.h = spr.height();
}
static inline uint16_t *fbAt(int x, int y) { return fb.buf + y * fb.w + x; }
static inline void pset(int x, int y, uint16_t c) {
  if ((unsigned)x < (unsigned)fb.w && (unsigned)y < (unsigned)fb.h) *fbAt(x, y) = c;
}
static inline void pmix(int x, int y, uint16_t c, int t) {
  if ((unsigned)x < (unsigned)fb.w && (unsigned)y < (unsigned)fb.h) { uint16_t *p = fbAt(x, y); *p = mixS(*p, c, t); }
}

// ---------------- primitives (logical coordinates, swapped colours) ----------------
static void hspan(int x0, int x1, int y, uint16_t c) {
  if ((unsigned)y >= (unsigned)fb.h) return;
  if (x0 < 0) x0 = 0;
  if (x1 >= fb.w) x1 = fb.w - 1;
  if (x0 > x1) return;
  uint16_t *p = fbAt(x0, y);
  for (int n = x1 - x0; n >= 0; n--) *p++ = c;
}
static void hspanMix(int x0, int x1, int y, uint16_t c, int t) {
  if ((unsigned)y >= (unsigned)fb.h) return;
  if (x0 < 0) x0 = 0;
  if (x1 >= fb.w) x1 = fb.w - 1;
  if (x0 > x1) return;
  uint16_t *p = fbAt(x0, y);
  for (int n = x1 - x0; n >= 0; n--, p++) *p = mixS(*p, c, t);
}
static void rectF(int x, int y, int w, int h, uint16_t c) {
  for (int j = 0; j < h; j++) hspan(x, x + w - 1, y + j, c);
}
static void rectMix(int x, int y, int w, int h, uint16_t c, int t) {
  for (int j = 0; j < h; j++) hspanMix(x, x + w - 1, y + j, c, t);
}
static int isqrtI(int v) {
  if (v <= 0) return 0;
  int r = (int)sqrtf((float)v);
  while (r * r > v) r--;
  while ((r + 1) * (r + 1) <= v) r++;
  return r;
}
static void circF(int cx, int cy, int r, uint16_t c) {
  if (r <= 0) { pset(cx, cy, c); return; }
  int rr = r * r + r;                               // + r: rounder small circles
  for (int dy = -r; dy <= r; dy++) {
    int dx = isqrtI(rr - dy * dy);
    hspan(cx - dx, cx + dx, cy + dy, c);
  }
}
static void circMix(int cx, int cy, int r, uint16_t c, int t) {
  int rr = r * r + r;
  for (int dy = -r; dy <= r; dy++) {
    int dx = isqrtI(rr - dy * dy);
    hspanMix(cx - dx, cx + dx, cy + dy, c, t);
  }
}
static void ellF(int cx, int cy, int rx, int ry, uint16_t c) {
  if (ry <= 0) { hspan(cx - rx, cx + rx, cy, c); return; }
  for (int dy = -ry; dy <= ry; dy++) {
    float f = 1.0f - (float)(dy * dy) / (float)(ry * ry + ry);
    int dx = f > 0 ? (int)(rx * sqrtf(f) + 0.5f) : 0;
    hspan(cx - dx, cx + dx, cy + dy, c);
  }
}
static void line(int x0, int y0, int x1, int y1, uint16_t c) {
  int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
  for (;;) {
    pset(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * e;
    if (e2 >= dy) { e += dy; x0 += sx; }
    if (e2 <= dx) { e += dx; y0 += sy; }
  }
}

// additive tint along a span: plain colour col scaled by k/32
static void addSpan(int x0, int x1, int y, uint16_t col, int k) {
  if ((unsigned)y >= (unsigned)fb.h || k <= 0) return;
  if (x0 < 0) x0 = 0;
  if (x1 >= fb.w) x1 = fb.w - 1;
  int ar = ((col >> 11) * k) >> 5, ag = (((col >> 5) & 63) * k) >> 5, ab = ((col & 31) * k) >> 5;
  uint16_t *p = fbAt(x0, y);
  for (int x = x0; x <= x1; x++, p++) {
    uint16_t v = sw16(*p);
    int R = min(31, (v >> 11) + ar), G = min(63, ((v >> 5) & 63) + ag), B = min(31, (v & 31) + ab);
    *p = sw16((uint16_t)((R << 11) | (G << 5) | B));
  }
}

// soft additive light: plain colour col, full at the centre, 0 at radius r
static void glow(int cx, int cy, int r, uint16_t col, int strength) {
  if (r < 1 || strength <= 0) return;
  if (cx + r < 0 || cx - r >= fb.w || cy + r < 0 || cy - r >= fb.h) return;
  if (strength > 32) strength = 32;
  int cr = col >> 11, cg = (col >> 5) & 63, cb = col & 31;
  // quadratic falloff through a small table (no division per pixel)
  uint8_t q[33];
  for (int i = 0; i <= strength; i++) q[i] = (uint8_t)(i * i / strength);
  int r2 = r * r;
  int32_t inv = ((int32_t)strength << 16) / r2;
  for (int dy = -r; dy <= r; dy++) {
    int y = cy + dy;
    if ((unsigned)y >= (unsigned)fb.h) continue;
    int dxm = isqrtI(r2 - dy * dy);
    int x0 = max(0, cx - dxm), x1 = min(fb.w - 1, cx + dxm);
    if (x0 > x1) continue;
    uint16_t *p = fbAt(x0, y);
    int rem = r2 - dy * dy;
    for (int x = x0; x <= x1; x++, p++) {
      int dx = x - cx;
      int f = (int)(((rem - dx * dx) * inv) >> 16);
      if (f <= 0) continue;
      f = q[f > strength ? strength : f];
      uint16_t v = sw16(*p);
      int R = (v >> 11) + ((cr * f) >> 5), G = ((v >> 5) & 63) + ((cg * f) >> 5), B = (v & 31) + ((cb * f) >> 5);
      if (R > 31) R = 31;
      if (G > 63) G = 63;
      if (B > 31) B = 31;
      *p = sw16((uint16_t)((R << 11) | (G << 5) | B));
    }
  }
}

// ---------------- pixel art from flash ----------------
// Art is a string of w*h chars; each char maps to a palette slot through a
// shared table ('.' or ' ' = transparent). Palettes hold swapped colours.
static int8_t artSlot[128];
static void artInit(const char *chars) {             // chars[i] -> slot i + 1
  memset(artSlot, 0, sizeof artSlot);
  for (int i = 0; chars[i]; i++) artSlot[(uint8_t)chars[i] & 127] = (int8_t)(i + 1);
}
// mode 0 normal, 1 = solid colour 'flat' (hurt flash, silhouettes)
static void blitArt(const char *art, int w, int h, int x, int y, bool flip, const uint16_t *pal, int mode = 0,
                    uint16_t flat = 0) {
  if (x >= fb.w || y >= fb.h || x + w <= 0 || y + h <= 0) return;
  for (int j = 0; j < h; j++) {
    int yy = y + j;
    if ((unsigned)yy >= (unsigned)fb.h) continue;
    const char *row = art + j * w;
    for (int i = 0; i < w; i++) {
      int s = artSlot[(uint8_t)row[flip ? w - 1 - i : i] & 127];
      if (!s) continue;
      int xx = x + i;
      if ((unsigned)xx >= (unsigned)fb.w) continue;
      *fbAt(xx, yy) = mode ? flat : pal[s];
    }
  }
}
// the same, scaled up by an integer factor (title screen)
static void blitArtBig(const char *art, int w, int h, int x, int y, bool flip, const uint16_t *pal, int k) {
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) {
      int s = artSlot[(uint8_t)art[j * w + (flip ? w - 1 - i : i)] & 127];
      if (s) rectF(x + i * k, y + j * k, k, k, pal[s]);
    }
}

// ---------------- text ----------------
// Text is drawn by TFT_eSPI into a small 1-bit sprite, then copied through the
// orientation transform (works the same in every orientation).
static TFT_eSprite tsp(&tft);
static bool tspOk = false;
static const int TSP_W = 240, TSP_H = 26;

static void textInit() {
  if (tspOk) return;
  tsp.setColorDepth(1);
  tspOk = tsp.createSprite(TSP_W, TSP_H) != nullptr;
}
static void textFree() {
  if (tspOk) tsp.deleteSprite();
  tspOk = false;
}
static int fontH(int font) { return font == 4 ? 26 : font == 2 ? 16 : 8; }
static int textW(const char *s, int font) { return tspOk ? tsp.textWidth(s, font) : 6 * (int)strlen(s); }

// align 0 left, 1 centre, 2 right; col / shadow are plain RGB565 (shadow 1 = none);
// scale 2 doubles every pixel (logo)
static int text(const char *s, int x, int y, int font, uint16_t col, int align = 0, uint16_t shadow = 1, int scale = 1) {
  if (!tspOk) return 0;
  int w = tsp.textWidth(s, font), h = fontH(font);
  if (w > TSP_W) w = TSP_W;
  tsp.fillSprite(0);
  tsp.setTextColor(1);
  tsp.setTextDatum(TL_DATUM);
  tsp.drawString(s, 0, 0, font);
  int x0 = align == 1 ? x - w * scale / 2 : align == 2 ? x - w * scale : x;
  const uint8_t *bits = (const uint8_t *)tsp.getPointer();
  const int stride = (TSP_W + 7) >> 3;
  uint16_t c = sw16(col), sh = sw16(shadow);
  for (int pass = shadow == 1 ? 1 : 0; pass < 2; pass++) {
    int o = pass ? 0 : 1;
    for (int j = 0; j < h; j++) {
      const uint8_t *row = bits + j * stride;
      for (int i = 0; i < w; i++) {
        if (!(row[i >> 3] & (0x80 >> (i & 7)))) continue;
        if (scale == 1) pset(x0 + i + o, y + j + o, pass ? c : sh);
        else rectF(x0 + i * scale + o, y + j * scale + o, scale, scale, pass ? c : sh);
      }
    }
  }
  return w * scale;
}

}  // namespace abyx

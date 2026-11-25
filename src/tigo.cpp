// Tigo, the hermit-crab mascot of TTGO-Arcade ("Ti-Ti-Go").
// No controls: drawing and shared state only.
//   drawTigo()  : the crab in any TigoPose, ~20 px (menu icon) to ~110 px tall
//   tigo::*     : needs (hunger / fun / clean / energy) saved in prefs, the
//                 shells unlocked by high scores, a few extra drawing helpers
//
// Art: everything is built from anti-aliased spots and wedges (TFT_eSPI 2.5),
// each part drawn twice, plum and 1 px larger first, so the parts merge into
// one outlined silhouette. Coordinates are in "Tigo units" (58 = his height),
// x grows towards his face, (0, 0) = bottom centre of the shell.
//
// Neon theme: "neon Tigo" (assets/tigo-neon-source.jpg). Same crab, more
// saturated, with a backwards snapback cap, rainbow sunglasses, a gold chain
// with a "T" medallion and a graffiti-sprayed shell. The outlines glow: the
// whole drawing runs two extra passes first that only paint the outlines,
// wider and in neon pink (shell) / cyan (crab), so every part gets a halo.

#include "tigo.h"

namespace tigo {

// ---------------- palette ----------------
// body colours per theme: Original, and Neon (hotter pink, deep violet outline)
struct BodyPal {
  uint16_t plum, coral, coralD, coralL, coralSh, peach, peachL, tip, aper, blush, mouth, tongue;
};
static const BodyPal PAL_ORIG = {0x38E7, 0xFB49, 0xE245, 0xFCAE, 0xDA25, 0xFD91, 0xFE96, 0xFCF0, 0x6989, 0xFBD1, 0x8947, 0xFC74};
static const BodyPal PAL_NEON = {0x2026, 0xFA4D, 0xF1AB, 0xFCB5, 0xC8EC, 0xFD12, 0xFE78, 0xFE47, 0x3848, 0xFAD9, 0x5847, 0xFB75};
static const uint16_t TEAR = 0x7E9F, SPARK = 0xFF0C, CRUMB = 0xCD0D;
static const uint16_t ZZZ_FAR = 0x8D19, WHITE = 0xFFFF, BLACK = 0x0000;
// neon Tigo's outfit
static const uint16_t N_PINK = 0xF959, N_CYAN = 0x073F, N_YELLOW = 0xFF45, N_LIME = 0x97E7, N_TEAL = 0x0739;
static const uint16_t CAP = 0x899F, CAP_D = 0x58D7, CAP_L = 0xC47F, LENS = 0x0822;
static const uint16_t GOLD = 0xFE65, GOLD_D = 0xBC00, GOLD_L = 0xFFB5;

struct ShellStyle { uint16_t base, shade, band, bandL; const char *name; };
static const ShellStyle SHELLS[NUM_SHELLS] = {
  {0xF6B4, 0xD58F, 0x2D95, 0x7F1A, "Sand"},
  {0xFF1D, 0xE598, 0xB35B, 0xDD5E, "Pearl"},
  {0xBF3F, 0x8DDC, 0x2AF8, 0x6CBE, "Lagoon"},
  {0xFEC9, 0xDD24, 0xFFFF, 0xFF98, "Gold"},
  {0x49D1, 0x290C, 0xFAF5, 0xFD19, "Galaxy"},
};
// the same five shells, graffiti-painted for the neon theme; tag = spray colours
struct NeonShell { uint16_t base, shade, band, bandL, tag[3]; };
static const NeonShell NEON_SHELLS[NUM_SHELLS] = {
  {0x915C, 0x60B5, 0xF955, 0xFF87, {N_TEAL, N_YELLOW, N_PINK}},     // purple, like the logo
  {0xFA97, 0xC952, 0x06FF, 0xDFFF, {0x915C, N_YELLOW, N_LIME}},     // hot pink
  {0x0659, 0x0474, 0xA19F, 0xFBDB, {N_PINK, N_YELLOW, 0x915C}},     // teal
  {0xFEA5, 0xDD00, 0xF952, 0xFFFF, {N_CYAN, 0x915C, N_PINK}},       // yellow
  {0x40B1, 0x284B, 0x079F, 0x97E7, {N_PINK, N_TEAL, N_YELLOW}},     // night violet
};
const uint8_t SHELL_AT[NUM_SHELLS] = {0, 5, 15, 30, 45};
const char *shellName(int i) { return SHELLS[constrain(i, 0, NUM_SHELLS - 1)].name; }

// ---------------- pen: Tigo units -> sprite pixels ----------------
// Passes: neon draws HALO and GLOW (outlines only, wider, in the glow colour)
// before ART (the normal drawing); the Original theme only runs ART.
enum Pass { PASS_HALO, PASS_GLOW, PASS_ART };

struct Pen {
  float cx, cy, k, oy, ol;
  int fl;            // +1 facing right, -1 facing left
  uint8_t dim;       // 255 = full colour
  bool neon;
  Pass pass;
  float gw;          // glow ring width, px
  uint16_t glow, halo;
};
static Pen P;
static const BodyPal *K = &PAL_ORIG;

static uint16_t C(uint16_t c) { return P.dim == 255 ? c : tft.alphaBlend(P.dim, c, TFT_BLACK); }
static float X(float x) { return P.cx + P.fl * x * P.k; }
static float Y(float y) { return P.cy + (y + P.oy) * P.k; }
static float R(float r) { return r * P.k; }

// sets the pen up for a drawing at scale k in the current theme
static void penBegin(float cx, float cy, float k, bool faceLeft, uint8_t dim) {
  P.cx = cx; P.cy = cy; P.k = k; P.oy = 0; P.fl = faceLeft ? -1 : 1; P.dim = dim;
  P.ol = fmaxf(0.7f, 1.15f * k);
  P.neon = theme().neon;
  P.pass = PASS_ART;
  P.gw = fmaxf(0.5f, 0.8f * k);
  K = P.neon ? &PAL_NEON : &PAL_ORIG;
}
static void setGlow(uint16_t c) {
  P.glow = c;
  P.halo = tft.alphaBlend(80, c, theme().bg);
}
// runs draw() once per pass: glow halo, glow ring, then the art
template <typename F> static void allPasses(F draw) {
  if (P.neon) {
    if (P.k >= 0.45f) { P.pass = PASS_HALO; draw(); }
    P.pass = PASS_GLOW; draw();
  }
  P.pass = PASS_ART;
  draw();
}

static bool art() { return P.pass == PASS_ART; }
// outline width and colour for the current pass
static float edgeW() { return P.ol + (P.pass == PASS_ART ? 0 : P.pass == PASS_GLOW ? P.gw : 2.2f * P.gw); }
static uint16_t edgeC() { return C(P.pass == PASS_ART ? K->plum : P.pass == PASS_GLOW ? P.glow : P.halo); }

static void spotU(float x, float y, float r, uint16_t c) {
  if (art()) spr.drawSpot(X(x), Y(y), R(r), C(c));
}
static void wedgeU(float ax, float ay, float bx, float by, float ar, float br, uint16_t c) {
  if (art()) spr.drawWedgeLine(X(ax), Y(ay), X(bx), Y(by), R(ar), R(br), C(c));
}
static void lineU(float ax, float ay, float bx, float by, float rr, uint16_t c) {
  if (art()) spr.drawWedgeLine(X(ax), Y(ay), X(bx), Y(by), R(rr), R(rr), C(c));
}
// outlines alone (glow passes draw them wider in the glow colour)
static void spotE(float x, float y, float r) { spr.drawSpot(X(x), Y(y), R(r) + edgeW(), edgeC()); }
static void wedgeE(float ax, float ay, float bx, float by, float ar, float br) {
  float e = edgeW();
  spr.drawWedgeLine(X(ax), Y(ay), X(bx), Y(by), R(ar) + e, R(br) + e, edgeC());
}
// a plum outline under the fill
static void spotO(float x, float y, float r, uint16_t c) {
  spotE(x, y, r);
  spotU(x, y, r, c);
}
static void wedgeO(float ax, float ay, float bx, float by, float ar, float br, uint16_t c) {
  wedgeE(ax, ay, bx, by, ar, br);
  wedgeU(ax, ay, bx, by, ar, br, c);
}

// Draws only the rows above sprite row yEnd (keeps the caller's viewport, e.g. the menu icon clip).
template <typename F> static void clipAbove(float yEnd, F draw) {
  int32_t vx = spr.getViewportX(), vy = spr.getViewportY();
  int32_t vw = spr.getViewportWidth(), vh = spr.getViewportHeight();
  bool vd = spr.getViewportDatum();
  int32_t h = (int32_t)yEnd + (vd ? 0 : -vy);
  if (h <= 0) return;
  if (h > vh) h = vh;
  spr.setViewport(vx, vy, vw, h, vd);
  draw();
  spr.setViewport(vx, vy, vw, vh, vd);
}

static uint32_t hash32(uint32_t n) {
  n *= 2654435761u;
  n ^= n >> 15;
  return n;
}

// ---------------- shell ----------------
// Spray paint on the neon shell, kept inside the rim: colour arcs under the
// spiral band, tags over it.
static void graffiti(float sx, float sy, const NeonShell &ns) {
  if (!art()) return;
  // thick sprayed arcs round the rim, one per colour (degrees, y down)
  static const int16_t ARC[3][2] = {{95, 200}, {215, 300}, {318, 385}};
  for (int c = 0; c < 3; c++) {
    const int n = 5;
    float px = 0, py = 0;
    for (int i = 0; i <= n; i++) {
      float a = (ARC[c][0] + (ARC[c][1] - ARC[c][0]) * i / (float)n) * (float)M_PI / 180.0f;
      float x = sx - 1 + cosf(a) * 16.5f, y = sy - 1 + sinf(a) * 16.5f;
      if (i) wedgeU(px, py, x, y, 3.3f, 3.3f, ns.tag[c]);
      px = x; py = y;
    }
  }
}

// tags sprayed over everything: drips, a zigzag, an X, an arrow, specks
static void graffitiTags(float sx, float sy, const NeonShell &ns) {
  if (!art() || P.k < 0.45f) return;
  const float dr[3][3] = {{-13, 12, 4.5f}, {-8, 14, 3.0f}, {13, 8.5f, 3.5f}};   // drips: x, y, length
  for (int i = 0; i < 3; i++) {
    uint16_t c = ns.tag[i < 2 ? 0 : 2];
    wedgeU(sx + dr[i][0], sy + dr[i][1], sx + dr[i][0], sy + dr[i][1] + dr[i][2], 1.2f, 0.6f, c);
    spotU(sx + dr[i][0], sy + dr[i][1] + dr[i][2], 0.85f, c);
  }
  static const int8_t ZZ[5][2] = {{-18, 1}, {-14, -4}, {-12, 1}, {-8, -4}, {-6, 0}};   // zigzag tag
  for (int i = 0; i < 4; i++)
    lineU(sx + ZZ[i][0], sy + ZZ[i][1], sx + ZZ[i + 1][0], sy + ZZ[i + 1][1], 0.8f, ns.tag[1]);
  lineU(sx - 8, sy - 17, sx - 4, sy - 13, 0.75f, ns.tag[2]);                     // X
  lineU(sx - 4, sy - 17, sx - 8, sy - 13, 0.75f, ns.tag[2]);
  lineU(sx + 3, sy + 16, sx + 11, sy + 12, 0.75f, ns.tag[1]);                    // arrow
  lineU(sx + 11, sy + 12, sx + 7.5f, sy + 11.2f, 0.75f, ns.tag[1]);
  lineU(sx + 11, sy + 12, sx + 9.6f, sy + 15.2f, 0.75f, ns.tag[1]);
  for (int i = 0; i < 8; i++) {                                                  // specks
    uint32_t h = hash32(i + 40);
    float a = (h & 255) / 40.7f, d = 7 + (h >> 8) % 12;
    spotU(sx + cosf(a) * d, sy + sinf(a) * d, 0.45f + ((h >> 16) & 3) * 0.15f, ns.tag[i % 3]);
  }
}

// Spiral shell centred on (sx, sy) in units, radius 22 units, apex pointing back-up.
static void shellAt(float sx, float sy, int style) {
  style = constrain(style, 0, NUM_SHELLS - 1);
  ShellStyle st = SHELLS[style];
  const NeonShell *ns = P.neon ? &NEON_SHELLS[style] : nullptr;
  if (ns) { st.base = ns->base; st.shade = ns->shade; st.band = ns->band; st.bandL = ns->bandL; }
  setGlow(N_PINK);
  const float SR = 22.0f;
  float ax = sx - 9, ay = sy - 12, tx = sx - 15, ty = sy - 22;
  wedgeO(ax, ay, tx, ty, 8, 2.2f, st.base);                // apex
  spotO(sx, sy, SR, st.shade);
  if (!art()) return;
  spotU(sx - 1.5f, sy - 2.0f, SR - 2.2f, st.base);           // lit from top-left: shaded crescent bottom-right
  wedgeU(ax + 1, ay + 1, tx, ty, 6.4f, 1.5f, st.base);
  if (ns) graffiti(sx, sy, *ns);
  // spiral band, widening outwards, with a thin light edge (neon: smaller, the paint shows round it)
  float ox = sx - 3.0f, oy = sy - 2.0f, sc = ns ? 0.72f : 1.0f;
  const int N = P.k < 0.5f ? 16 : 30;
  float px = 0, py = 0, pw = 0;
  for (int i = 0; i <= N; i++) {
    float f = (float)i / N;
    float th = -1.2f + f * 4.0f * (float)M_PI * 0.86f;
    float r = 1.5f + f * (SR - 6.5f) * sc;
    float x = ox + cosf(th) * r, y = oy + sinf(th) * r, w = (0.7f + f * 1.7f) * (ns ? 1.2f : 1.0f);
    if (i) wedgeU(px, py, x, y, pw, w, st.band);
    px = x; py = y; pw = w;
  }
  if (P.k >= 0.5f) {
    for (int i = 0; i <= N; i++) {
      float f = (float)i / N;
      float th = -1.2f + f * 4.0f * (float)M_PI * 0.86f;
      float r = 1.5f + f * (SR - 6.5f) * sc - 0.6f * f;
      float x = ox + cosf(th) * r, y = oy + sinf(th) * r, w = 0.25f + f * 0.5f;
      if (i && f > 0.15f) wedgeU(px, py, x, y, pw, w, st.bandL);
      px = x; py = y; pw = w;
    }
  }
  if (ns) graffitiTags(sx, sy, *ns);
  wedgeU(ax - 1.5f, ay - 3, tx + 0.5f, ty + 1.5f, 1.3f, 0.6f, st.band);   // band on the apex
  spotU(sx - 12, sy - 11, 3.6f, tft.alphaBlend(170, WHITE, st.base));    // highlights
  spotU(sx - 15, sy - 6, 1.6f, tft.alphaBlend(200, WHITE, st.base));
}

// ---------------- claws and legs ----------------
// Arm from the shoulder (sx, sy) to the hand (hx, hy), pincers along angle ang (deg);
// op = 0 closed .. 1 wide open. Outline pass first, then fill, so it reads as one shape.
static void claw(float sx, float sy, float hx, float hy, float ang, float op, float sc, uint16_t cbase, bool far) {
  float a = ang * (float)M_PI / 180.0f;
  float dx = cosf(a), dy = sinf(a), nx = -dy, ny = dx;
  float hr = 5.6f * sc;
  float h2x = hx + dx * 2.5f * sc, h2y = hy + dy * 2.5f * sc;
  float bx = hx + dx * 5.5f * sc, by = hy + dy * 5.5f * sc;
  float L = 5.8f * sc, oa = op * 0.75f;
  float f[2][4];
  for (int i = 0; i < 2; i++) {            // 0 = dactyl (moves), 1 = fixed finger
    float side = i ? 1.0f : -1.0f, rot = i ? oa * 0.2f : -oa;
    float b0x = bx + nx * side * 2.0f * sc, b0y = by + ny * side * 2.0f * sc;
    float tx0 = dx * L - nx * side * 2.0f * sc, ty0 = dy * L - ny * side * 2.0f * sc;   // closed: tips meet
    float ca = cosf(rot), sa = sinf(rot);
    f[i][0] = b0x; f[i][1] = b0y;
    f[i][2] = b0x + tx0 * ca - ty0 * sa; f[i][3] = b0y + tx0 * sa + ty0 * ca;
  }
  float parts[4][6] = {
    {sx, sy, hx, hy, 2.6f * sc, 3.0f * sc},
    {hx, hy, h2x, h2y, hr, hr * 0.92f},
    {f[0][0], f[0][1], f[0][2], f[0][3], 2.7f * sc, 1.0f * sc},
    {f[1][0], f[1][1], f[1][2], f[1][3], 2.5f * sc, 1.0f * sc},
  };
  for (auto &p : parts) wedgeE(p[0], p[1], p[2], p[3], p[4], p[5]);
  if (!art()) return;
  for (auto &p : parts) wedgeU(p[0], p[1], p[2], p[3], p[4], p[5], cbase);
  for (int i = 0; i < 2; i++) {            // lighter tips
    float mx = f[i][0] + (f[i][2] - f[i][0]) * 0.62f, my = f[i][1] + (f[i][3] - f[i][1]) * 0.62f;
    wedgeU(mx, my, f[i][2], f[i][3], 1.4f * sc, 0.7f * sc, K->tip);
  }
  wedgeU(hx - nx * 2.2f * sc + dx * 0.5f, hy - ny * 2.2f * sc + dy * 0.5f,      // shine on the hand
         h2x - nx * 2.4f * sc, h2y - ny * 2.4f * sc, 1.3f * sc, 1.0f * sc, far ? K->coral : K->coralL);
}

static void leg(float hx, float hy, int i, uint16_t c, bool far, float walk, bool walking) {
  float lift = 0, sw = 0;
  if (walking) {
    float p = walk + i * (float)M_PI * 0.5f + (far ? (float)M_PI : 0);
    sw = sinf(p) * 2.2f;
    lift = fmaxf(0.0f, cosf(p)) * 2.0f;
  }
  float kx = hx + 3.0f + sw * 0.5f, ky = hy - 2.5f - lift * 0.5f;
  float fx = hx + 4.5f + sw, fy = -lift;
  wedgeE(hx, hy, kx, ky, 2.0f, 1.6f);
  wedgeE(kx, ky, fx, fy, 1.6f, 0.7f);
  wedgeU(hx, hy, kx, ky, 2.0f, 1.6f, c);
  wedgeU(kx, ky, fx, fy, 1.6f, 0.7f, c);
  spotU(kx, ky, 1.6f, c);
}

// ---------------- eyes ----------------
enum EyeMode { EYE_OPEN, EYE_CLOSED, EYE_HAPPY, EYE_SAD, EYE_FOCUS };

static void eye(float ex, float ey, float er, float lx, float ly, EyeMode e) {
  spotE(ex, ey, er);
  if (!art()) return;
  if (e == EYE_CLOSED) {
    spotU(ex, ey, er, K->coral);
    lineU(ex - er * 0.7f, ey, ex, ey + er * 0.35f, 0.7f, K->plum);
    lineU(ex, ey + er * 0.35f, ex + er * 0.7f, ey, 0.7f, K->plum);
    return;
  }
  if (e == EYE_HAPPY) {   // ^ ^
    spotU(ex, ey, er, K->coral);
    lineU(ex - er * 0.65f, ey + er * 0.25f, ex, ey - er * 0.35f, 0.8f, K->plum);
    lineU(ex, ey - er * 0.35f, ex + er * 0.65f, ey + er * 0.25f, 0.8f, K->plum);
    return;
  }
  spotU(ex, ey, er, WHITE);
  float pr = er * (e == EYE_FOCUS ? 0.42f : 0.5f);
  float px = ex + lx * er * 0.38f, py = ey + ly * er * 0.38f;
  spotU(px, py, pr, BLACK);
  if (R(pr) >= 1.2f) {
    spotU(px - pr * 0.35f, py - pr * 0.4f, pr * 0.36f, WHITE);   // sparkle
    spotU(px + pr * 0.4f, py + pr * 0.35f, pr * 0.14f, WHITE);
  }
  if (e == EYE_SAD || e == EYE_FOCUS) {   // heavy (sad) or low (determined) upper lid
    float lid = ey - er * (e == EYE_SAD ? 0.15f : 0.45f);
    clipAbove(Y(lid), [&]() { spotU(ex, ey, er, K->coral); });
    if (e == EYE_SAD) lineU(ex - er * 0.95f, lid - 0.2f, ex + er * 0.95f, lid + 0.6f, 0.6f, K->plum);
  }
}

// ---------------- neon outfit ----------------
// Rainbow reflection across a lens centred on (x, y), half-length a, radius rr.
static void lensShine(float x, float y, float a, float rr, uint32_t t) {
  if (!art()) return;
  if (R(rr) < 2.2f) {                       // tiny: one glint
    spotU(x - a * 0.4f, y - rr * 0.3f, rr * 0.35f, N_CYAN);
    return;
  }
  static const uint16_t RAIN[4] = {N_CYAN, N_LIME, N_YELLOW, N_PINK};
  float step = (a + rr * 0.75f) * 0.5f, h = rr * 0.5f;
  for (int j = 0; j < 4; j++) {
    float sx = x - step * 1.5f + j * step;
    lineU(sx - 0.5f, y + h, sx + 0.5f, y - h, step * 0.3f, RAIN[j]);
  }
  uint32_t ph = t % 2600;                   // a white glint sweeps across now and then
  if (ph < 420) {
    float gx = x - a - rr * 0.5f + (2 * a + rr) * ph / 420.0f;
    lineU(gx - 0.6f, y + h * 0.8f, gx + 0.6f, y - h * 0.8f, 0.4f, WHITE);
  }
}

// Sunglasses over both eyes. tilt[i] > 0 lowers lens i's inner end (cross),
// < 0 raises it (worried). Lenses: a short wide wedge, wider at the top edge.
static void shades(const float *ex, const float *ey, const float *er, const float *tilt, float lift, uint32_t t) {
  float lx[2], ly[2], a[2], rr[2], dyIn[2];
  for (int i = 0; i < 2; i++) {
    a[i] = er[i] * 0.22f; rr[i] = er[i] * 0.7f;
    lx[i] = ex[i]; ly[i] = ey[i] + 0.4f + lift;
    dyIn[i] = tilt[i];
  }
  // inner ends face each other: lens 0's inner end is its +x end, lens 1's the -x end
  for (int i = 0; i < 2; i++) {
    float s = i ? -1.0f : 1.0f;
    wedgeE(lx[i] - s * a[i], ly[i] - dyIn[i], lx[i] + s * a[i], ly[i] + dyIn[i], rr[i], rr[i] * 0.92f);
  }
  wedgeE(lx[0] + a[0], ly[0] - 0.8f, lx[1] - a[1], ly[1] - 0.8f, 0.7f, 0.7f);   // bridge
  if (!art()) return;
  for (int i = 0; i < 2; i++) {
    float s = i ? -1.0f : 1.0f;
    wedgeU(lx[i] - s * a[i], ly[i] - dyIn[i], lx[i] + s * a[i], ly[i] + dyIn[i], rr[i], rr[i] * 0.92f, LENS);
  }
  lineU(lx[0] + a[0], ly[0] - 0.8f, lx[1] - a[1], ly[1] - 0.8f, 0.7f, LENS);
  for (int i = 0; i < 2; i++) lensShine(lx[i], ly[i], a[i], rr[i], t + i * 180);
}

// Snapback cap worn backwards: dome over the eyes, the brim sticking out at the
// back, the strap opening (teal) at the front and a teal button on top.
static void cap(float cx, float cy, float rc) {
  setGlow(N_CYAN);
  wedgeE(cx - rc + 1.0f, cy - 0.8f, cx - rc - 7.0f, cy + 1.0f, 2.2f, 1.4f);   // brim
  clipAbove(Y(cy + 0.6f), [&]() { spotE(cx, cy, rc); });
  spotE(cx, cy - rc - 0.4f, 1.3f);                                              // button
  wedgeE(cx - rc, cy, cx + rc, cy, 1.2f, 1.2f);                                 // band
  if (!art()) return;
  wedgeU(cx - rc + 1.0f, cy - 0.8f, cx - rc - 7.0f, cy + 1.0f, 2.2f, 1.4f, CAP_D);
  clipAbove(Y(cy + 0.6f), [&]() {
    spotU(cx, cy, rc, CAP);
    spotU(cx - rc * 0.35f, cy - rc * 0.55f, rc * 0.28f, CAP_L);                 // shine
    if (P.k >= 0.5f) lineU(cx, cy - rc, cx + rc * 0.25f, cy - 0.5f, 0.3f, CAP_D);   // panel seam
    // strap opening at the front, a teal snap strap across it
    spotU(cx + rc * 0.52f, cy, rc * 0.34f, K->aper);
    lineU(cx + rc * 0.26f, cy - rc * 0.2f, cx + rc * 0.8f, cy - rc * 0.2f, fmaxf(0.45f, rc * 0.08f), N_TEAL);
  });
  wedgeU(cx - rc, cy, cx + rc, cy, 1.2f, 1.2f, CAP_D);
  spotU(cx, cy - rc - 0.4f, 1.3f, N_TEAL);
}

// Gold chain hanging from (ax, ay) to (bx, by) with a round "T" medallion.
static void chain(float ax, float ay, float bx, float by, float sag) {
  setGlow(N_CYAN);
  float mx = (ax + bx) * 0.5f, my = (ay + by) * 0.5f + sag;      // Bezier control point
  float pts[9][2];
  for (int i = 0; i < 9; i++) {
    float u = i / 8.0f, v = 1 - u;
    pts[i][0] = v * v * ax + 2 * u * v * mx + u * u * bx;
    pts[i][1] = v * v * ay + 2 * u * v * my + u * u * by;
  }
  float px = pts[4][0], py = pts[4][1] + 2.4f, pr = 2.4f;        // medallion
  if (!art()) { spotE(px, py, pr); return; }
  if (R(0.9f) < 1.0f) {                                          // small: a plain gold line
    for (int i = 0; i < 8; i++) lineU(pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1], 0.55f, GOLD);
  } else {
    for (int i = 0; i < 8; i++) lineU(pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1], 0.9f, GOLD_D);
    for (int i = 0; i < 9; i++) spotU(pts[i][0], pts[i][1], 0.75f, (i & 1) ? GOLD : GOLD_L);
  }
  spotE(px, py, pr);
  spotU(px, py, pr, GOLD);
  if (R(pr) >= 2.6f) {
    spotU(px - 0.7f, py - 0.8f, pr * 0.35f, GOLD_L);
    lineU(px - 1.1f, py - 0.8f, px + 1.1f, py - 0.8f, 0.36f, K->plum);   // T
    lineU(px, py - 0.8f, px, py + 1.2f, 0.36f, K->plum);
  }
}

// ---------------- the crab ----------------
static void drawCrab(int cx, int cy, int size, TigoPose pose, uint32_t t, bool faceLeft, int shellStyle, uint8_t dim) {
  const float TAU = 6.2831853f;
  float k = size / 58.0f;
  penBegin(cx, cy, k, faceLeft, dim);
  const bool neon = P.neon;

  // ---- pose parameters ----
  float breath = sinf((t % 2400) * TAU / 2400.0f);
  float jump = 0, bdx = 0, bdy = breath * 0.6f;
  float openN = 0.35f, openF = 0.3f, raiseN = 0, raiseF = 0, droop = 0, walk = 0;
  float tilt = 0, shadesLift = 0;           // neon: how the sunglasses sit
  bool walking = false;
  EyeMode eyes = EYE_OPEN;
  enum { M_SMILE, M_GRIN, M_OPEN, M_CHOMP, M_FROWN, M_GRIT, M_SLEEP } mouth = M_SMILE;
  switch (pose) {
    case TIGO_IDLE: {
      uint32_t ph = t % 4200;
      if (ph < 520) openN = 0.35f + 0.65f * fabsf(sinf(ph * TAU / 520.0f));   // two quick snaps
      break;
    }
    case TIGO_WALK:
      walking = true;
      walk = (t % 5200) * TAU / 520.0f;
      bdy = -fabsf(sinf(walk)) * 1.0f;
      openN = 0.3f + 0.15f * sinf(walk);
      openF = 0.3f - 0.15f * sinf(walk);
      break;
    case TIGO_EAT: {
      float ph = (t % 900) / 900.0f;
      float s = sinf(ph * (float)M_PI);
      raiseN = s * s;                       // claw to mouth and back
      openN = 0.1f;
      mouth = (t / 150) % 2 ? M_CHOMP : M_OPEN;
      break;
    }
    case TIGO_SLEEP:                        // half way back into the shell
      bdx = -6.0f; bdy = 3.5f + breath * 0.5f;
      eyes = EYE_CLOSED; mouth = M_SLEEP; openN = openF = 0; droop = 1.0f;
      break;
    case TIGO_HAPPY:
      jump = -fabsf(sinf((t % 1000) * TAU / 1000.0f)) * 6.0f;
      eyes = EYE_HAPPY; mouth = M_GRIN; raiseN = raiseF = 0.6f;
      openN = openF = 0.5f + 0.5f * fabsf(sinf((t % 500) * TAU / 500.0f));
      shadesLift = -1.0f - 0.8f * fabsf(sinf((t % 500) * TAU / 500.0f));   // eyebrows-up bounce
      break;
    case TIGO_SAD:
      eyes = EYE_SAD; mouth = M_FROWN; bdy = 1.5f + breath * 0.4f; droop = 0.6f;
      openN = openF = 0.05f;
      tilt = -1.6f;
      break;
    case TIGO_PINCH: {
      float snap = fabsf(sinf((t % 360) * TAU / 360.0f));
      openN = snap; openF = 1 - snap; raiseN = raiseF = 0.35f;
      eyes = EYE_FOCUS; mouth = M_GRIT;
      tilt = 1.6f;
      break;
    }
    default: break;
  }
  P.oy = jump;

  float Sx = -6.0f, Sy = -23.0f;
  if (pose == TIGO_HIDE && (t % 2600) < 400) Sx += sinf((t % 90) * TAU / 90.0f) * 0.4f;   // trembles
  float Bx = 14.0f + bdx, By = -15.0f + bdy;

  // eyes on stalks: wander around, blink now and then
  uint32_t seg = t / 1700;
  float l0x, l0y, l1x, l1y;
  {
    uint32_t h0 = hash32(seg + 6), h1 = hash32(seg + 7);
    l0x = (h0 & 255) / 127.5f - 1.0f; l0y = (((h0 >> 8) & 255) / 127.5f - 1.0f) * 0.6f;
    l1x = (h1 & 255) / 127.5f - 1.0f; l1y = (((h1 >> 8) & 255) / 127.5f - 1.0f) * 0.6f;
  }
  float lf = fminf(1.0f, (t % 1700) / 160.0f);
  float lx = l0x + (l1x - l0x) * lf, ly = l0y + (l1y - l0y) * lf;
  if (pose == TIGO_WALK || pose == TIGO_PINCH) { lx = 0.9f; ly = 0.2f; }
  if (pose == TIGO_EAT) { lx = 0.9f; ly = 0.5f; }
  if (pose == TIGO_SAD) { lx = 0.3f; ly = 0.7f; }
  bool blink = ((t + 900) % 3700) < 120 || ((t + 2100) % 8300) < 120;
  float sway = sinf((t % 3100) * TAU / 3100.0f) * 0.8f;
  const float ex[2] = {Bx - 1.0f + sway * 0.6f - droop, Bx + 9.5f + sway - droop * 0.5f};
  const float ey[2] = {By - 21.0f + droop * 5.0f, By - 20.0f + droop * 5.5f};
  const float er[2] = {6.0f, 6.6f};
  const float sbx[2] = {Bx + 1.0f, Bx + 6.5f}, sby[2] = {By - 8.0f, By - 7.5f};

  allPasses([&]() {
    shellAt(Sx, Sy, shellStyle);
    setGlow(N_CYAN);

    if (pose == TIGO_HIDE) {   // dark opening with two eyes (neon: shades) and two claw tips peeking out
      spotO(Sx + 9, Sy + 9, 7.0f, K->aper);
      wedgeO(Sx + 12, Sy + 15, Sx + 16, Sy + 14, 1.8f, 1.2f, K->coralD);
      wedgeO(Sx + 6, Sy + 15.5f, Sx + 9, Sy + 16, 1.6f, 1.1f, K->coralD);
      if (!art()) return;
      float look = sinf((t % 3000) * TAU / 3000.0f);
      bool hblink = (t % 3100) < 120;
      const float hx[2] = {Sx + 6.5f, Sx + 11.8f}, hy[2] = {Sy + 6.5f, Sy + 7.0f};
      if (neon) {
        lineU(hx[0], hy[0], hx[1], hy[1], 0.6f, LENS);
        for (int i = 0; i < 2; i++) {
          spotO(hx[i] + look * 0.5f, hy[i], 2.5f, LENS);
          spotU(hx[i] - 0.7f + look * 0.5f, hy[i] - 0.6f, 0.8f, i ? N_PINK : N_CYAN);
        }
        return;
      }
      for (int i = 0; i < 2; i++) {
        if (hblink) { lineU(hx[i] - 2, hy[i], hx[i] + 2, hy[i], 0.6f, WHITE); continue; }
        spotO(hx[i], hy[i], 2.6f, WHITE);
        spotU(hx[i] + 0.6f + look * 0.8f, hy[i] + 0.3f, 1.4f, BLACK);
        if (k > 0.5f) spotU(hx[i] + 0.1f + look * 0.8f, hy[i] - 0.4f, 0.5f, WHITE);
      }
      return;
    }

    spotU(Sx + 13, Sy + 12, 8.0f, K->aper);       // shadow of the shell opening behind the body

    if (pose != TIGO_SLEEP) {
      leg(Bx - 7, By + 7.5f, 0, K->coralSh, true, walk, walking);
      leg(Bx - 2, By + 7.5f, 1, K->coralSh, true, walk, walking);
    }

    // far claw, behind the body
    claw(Bx + 6, By + 4 - raiseF * 4, Bx + 17 - raiseF * 2, By + 3 - raiseF * 11, -38 - raiseF * 40, openF, 0.8f, K->coralSh, true);

    // body: shaded base, lit top, peach belly
    wedgeO(Bx - 6, By, Bx + 5, By - 1, 11, 10, K->coralSh);
    wedgeU(Bx - 6.3f, By - 1.3f, Bx + 4.6f, By - 2.0f, 9.6f, 8.6f, K->coral);
    wedgeU(Bx - 4, By - 6, Bx + 3, By - 6.8f, 3.0f, 2.2f, K->coralL);
    wedgeU(Bx - 1, By + 4.2f, Bx + 6, By + 3.4f, 5.6f, 4.8f, K->peach);
    wedgeU(Bx + 0.5f, By + 3.0f, Bx + 5, By + 2.6f, 2.4f, 1.6f, K->peachL);

    // face: blush and mouth
    spotU(Bx + 8.0f, By - 2.5f, 2.0f, tft.alphaBlend(140, K->blush, K->coral));
    float mx = Bx + 11.5f, my = By - 5.5f;
    switch (mouth) {
      case M_SMILE:
        lineU(mx - 2.4f, my - 0.6f, mx, my + 1.0f, 0.75f, K->plum);
        lineU(mx, my + 1.0f, mx + 1.6f, my - 0.6f, 0.75f, K->plum);
        break;
      case M_GRIN:
        wedgeO(mx - 2.0f, my, mx + 1.2f, my + 0.3f, 1.2f, 1.6f, K->mouth);
        spotU(mx - 0.2f, my + 1.0f, 0.8f, K->tongue);
        break;
      case M_OPEN: spotO(mx - 0.5f, my + 0.5f, 1.8f, K->mouth); break;
      case M_CHOMP: spotO(mx - 0.5f, my + 0.5f, 0.9f, K->mouth); break;
      case M_FROWN:
        lineU(mx - 2.4f, my + 1.2f, mx - 0.4f, my - 0.2f, 0.6f, K->plum);
        lineU(mx - 0.4f, my - 0.2f, mx + 1.4f, my + 1.2f, 0.6f, K->plum);
        break;
      case M_GRIT:
        lineU(mx - 2.2f, my + 0.2f, mx + 1.2f, my + 0.2f, 0.9f, K->plum);
        if (k > 0.5f) lineU(mx - 1.5f, my + 0.2f, mx + 0.5f, my + 0.2f, 0.3f, WHITE);
        break;
      case M_SLEEP: spotO(mx - 0.8f, my + 0.6f, 0.9f, K->mouth); break;
    }

    // stalks (no glow: the gap between them would light up)
    if (art()) for (int i = 0; i < 2; i++) wedgeO(sbx[i], sby[i], ex[i] - (ex[i] - sbx[i]) * 0.2f, ey[i] + er[i] * 0.6f, 1.8f, 1.3f, K->coral);
    if (!neon) {
      for (int i = 0; i < 2; i++) eye(ex[i], ey[i], er[i], lx, ly, eyes == EYE_OPEN && blink ? EYE_CLOSED : eyes);
    } else {
      // neon: the eyes hide behind the shades under a backwards cap; asleep,
      // the shades are pushed up on the cap and the closed eyes show
      float capX = (ex[0] + ex[1]) * 0.5f + 0.3f, capY = fminf(ey[0], ey[1]) - 3.4f, rc = 9.5f;
      const float tl[2] = {tilt, tilt};
      if (pose == TIGO_SLEEP) {
        for (int i = 0; i < 2; i++) eye(ex[i], ey[i], er[i], 0, 0, EYE_CLOSED);
        cap(capX, capY, rc);
        const float sy[2] = {capY - rc * 0.55f, capY - rc * 0.5f}, sr[2] = {er[0] * 0.8f, er[1] * 0.8f};
        const float sx[2] = {capX - 4.6f, capX + 4.4f};
        shades(sx, sy, sr, tl, 0, t);
      } else {
        shades(ex, ey, er, tl, shadesLift, t);
        cap(capX, capY + shadesLift * 0.5f, rc);
      }
      // gold chain round the neck, under the mouth
      chain(Bx + 2.5f, By - 6.5f, Bx + 14.5f, By - 4.0f, 9.0f);
    }

    if (pose != TIGO_SLEEP) {
      leg(Bx + 1, By + 8.0f, 2, K->coral, false, walk, walking);
      leg(Bx + 6, By + 8.0f, 3, K->coral, false, walk, walking);
    }

    // near claw, in front
    if (pose == TIGO_EAT)
      claw(Bx + 5, By + 7, Bx + 13 - raiseN * 2, By + 8 - raiseN * 9, -12 - raiseN * 95, openN, 1.0f, K->coralD, false);
    else
      claw(Bx + 5, By + 7, Bx + 14 - raiseN, By + 8 - raiseN * 9, -12 - raiseN * 45, openN, 1.0f, K->coralD, false);
  });
  if (pose == TIGO_HIDE) return;

  // ---- extras ----
  if (pose == TIGO_SLEEP) {                 // Zzz drifting up
    for (int i = 0; i < 3; i++) {
      float ph = fmodf(t / 2400.0f + i / 3.0f, 1.0f);
      float zx = Bx + 6 + ph * 10, zy = By - 16 - ph * 22 - (neon ? 8 : 0), zs = 1.5f + ph * 2.2f;
      uint16_t c = C(tft.alphaBlend((uint8_t)(255 * sqrtf(1 - ph)), neon ? N_CYAN : WHITE, ZZZ_FAR));
      float w = fmaxf(1.0f, R(0.8f));
      spr.drawWideLine(X(zx - zs), Y(zy - zs), X(zx + zs), Y(zy - zs), w, c);
      spr.drawWideLine(X(zx + zs), Y(zy - zs), X(zx - zs), Y(zy + zs), w, c);
      spr.drawWideLine(X(zx - zs), Y(zy + zs), X(zx + zs), Y(zy + zs), w, c);
    }
  } else if (pose == TIGO_SAD) {            // a tear rolling down (from under the shades in neon)
    float ph = (t % 1600) / 1600.0f;
    float tx = ex[1] + 3, ty = ey[1] + 6 + ph * 10;
    wedgeU(tx, ty - 1.8f, tx, ty, 0.3f, 1.1f, neon ? N_CYAN : TEAR);
  } else if (pose == TIGO_HAPPY) {          // little sparkles
    for (int i = 0; i < 3; i++) {
      float ph = fmodf(t / 1200.0f + i / 3.0f, 1.0f);
      float a = i * 2.1f + 0.6f;
      float px = Bx + 4 + cosf(a) * (14 + ph * 8), py = By - 18 + sinf(a) * (8 + ph * 6) - ph * 6;
      float r = 1.6f * (1 - ph);
      uint16_t c = neon ? (i == 0 ? N_PINK : i == 1 ? N_CYAN : N_YELLOW) : SPARK;
      lineU(px - r * 2, py, px + r * 2, py, 0.4f, c);
      lineU(px, py - r * 2, px, py + r * 2, 0.4f, c);
    }
  } else if (pose == TIGO_EAT && raiseN > 0.6f) {   // crumbs
    for (int i = 0; i < 2; i++) {
      float ph = fmodf(t / 300.0f + i * 0.5f, 1.0f);
      spotU(Bx + 14 + i * 2, By - 1 + ph * 8, 0.8f, CRUMB);
    }
  }
}

void drawTigoEx(int cx, int cy, int size, TigoPose pose, uint32_t t, bool faceLeft, int shellStyle, uint8_t dim) {
  drawCrab(cx, cy, size, pose, t, faceLeft, shellStyle, dim);
}

void drawShell(float cx, float cy, float r, int style, bool faceLeft) {
  penBegin(cx, cy, r / 22.0f, faceLeft, 255);
  allPasses([&]() { shellAt(0, 0, style); });
}

void drawEye(float x, float y, float r, float lx, float ly, bool closed) {
  penBegin(x, y, 1, false, 255);
  P.ol = 1.1f;
  if (!P.neon) {
    eye(0, 0, r, lx, ly, closed ? EYE_CLOSED : EYE_OPEN);
    return;
  }
  // neon: a round sunglass lens on the periscope
  setGlow(N_CYAN);
  allPasses([&]() {
    spotO(0, 0, r, LENS);
    if (!art()) return;
    lineU(-r * 0.5f + lx, r * 0.3f, -r * 0.1f + lx, -r * 0.5f, r * 0.12f, N_PINK);
    lineU(r * 0.0f + lx, r * 0.3f, r * 0.4f + lx, -r * 0.5f, r * 0.12f, N_CYAN);
  });
}

void drawShadow(int cx, int cy, int size) {
  // a flat translucent ellipse: darken what is already in the sprite
  // (neon: a magenta glow on the ground instead)
  float k = size / 58.0f;
  int x0 = cx - (int)(29 * k), x1 = cx + (int)(36 * k);
  int hh = max(1, (int)(3.0f * k)), mid = (x0 + x1) / 2, rx = (x1 - x0) / 2;
  if (rx < 1) return;
  uint16_t col = theme().neon ? 0xA00F : PAL_ORIG.plum;
  for (int dy = -hh; dy <= hh; dy++) {
    float f = 1.0f - (float)(dy * dy) / ((hh + 0.5f) * (hh + 0.5f));
    if (f <= 0) continue;
    int w = (int)(rx * sqrtf(f));
    int y = cy + dy;
    for (int x = mid - w; x <= mid + w; x++) {
      int edge = min(x - (mid - w), (mid + w) - x);
      uint8_t a = edge < 3 ? 40 + edge * 20 : 100;
      spr.drawPixel(x, y, tft.alphaBlend(a, col, spr.readPixel(x, y)));
    }
  }
}

float clawReach(int size) { return 36.0f * size / 58.0f; }

void drawBubble(const char *txt, int headX, int headTop, int minY, uint32_t ms) {
  // greedy word wrap, at most 4 lines of 116 px
  const int MAXW = 116;
  int st[4], ln[4], wd[4], n = 0, maxW = 0;
  int len = strlen(txt), pos = 0;
  char tmp[100];
  if (len > (int)sizeof tmp - 1) len = sizeof tmp - 1;
  while (pos < len && n < 4) {
    int best = -1, bestW = 0, i = pos;
    while (true) {
      while (i < len && txt[i] != ' ') i++;
      int k = i - pos;
      memcpy(tmp, txt + pos, k); tmp[k] = 0;
      int w = spr.textWidth(tmp, 2);
      if (w > MAXW && best >= 0) break;
      best = k; bestW = w;
      if (i >= len) break;
      i++;
    }
    if (bestW > MAXW) {   // one long word: cut it
      while (best > 1) { tmp[--best] = 0; if ((bestW = spr.textWidth(tmp, 2)) <= MAXW) break; }
    }
    st[n] = pos; ln[n] = best; wd[n] = bestW; n++;
    maxW = max(maxW, bestW);
    pos += best;
    while (pos < len && txt[pos] == ' ') pos++;
  }
  if (!n) return;
  int w = maxW + 12, h = n * 16 + 8;
  int x = constrain(headX - w / 2, 2, 133 - w);
  int y = max(minY, headTop - 10 - h);
  int tail = constrain(headX, x + 9, x + w - 9);
  const Theme &th = theme();
  uint16_t edge = th.neon ? N_PINK : PAL_ORIG.plum, fill = th.neon ? th.panel : WHITE;
  if (th.neon) {   // neon sign: dark panel, glowing pink tube
    uint16_t halo = tft.alphaBlend(70, N_PINK, th.bg);
    spr.fillSmoothRoundRect(x - 3, y - 3, w + 6, h + 6, 10, halo);
    spr.fillTriangle(tail - 7, y + h, tail + 6, y + h, tail, y + h + 11, halo);
  }
  spr.fillSmoothRoundRect(x - 1, y - 1, w + 2, h + 2, 8, edge);
  spr.fillTriangle(tail - 5, y + h - 1, tail + 4, y + h - 1, tail, y + h + 8, edge);
  spr.fillSmoothRoundRect(x, y, w, h, 7, fill);
  spr.fillTriangle(tail - 4, y + h - 2, tail + 3, y + h - 2, tail, y + h + 6, fill);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(th.neon ? WHITE : PAL_ORIG.plum);
  int shown = ms / 30;   // typewriter
  for (int i = 0; i < n && shown > 0; i++) {
    int k = min(ln[i], shown);
    memcpy(tmp, txt + st[i], k); tmp[k] = 0;
    spr.drawString(tmp, x + 6 + (maxW - wd[i]) / 2, y + 4 + i * 16, 2);   // centred on its final width
    shown -= ln[i];
  }
}

// ---------------- needs ----------------
struct Saved {
  uint8_t hunger, fun, clean, energy, shellsSeen, pad[3];
  uint32_t epoch;   // time(nullptr) at the last save, 0 if the clock was unknown
};
static Needs gNeeds = {80, 80, 80, 80};
static uint8_t gShellsSeen = 1;
static bool loaded = false, dirty = false, offlineDone = false;
static uint32_t savedEpoch = 0, lastTick = 0, lastSave = 0;
static uint32_t acc[4];
static const uint32_t DECAY_MS[4] = {120000, 90000, 180000, 150000};   // -1 point per period while awake

Needs &needs() { return gNeeds; }

static int clamp100(int v) { return v < 0 ? 0 : v > 100 ? 100 : v; }

void load() {
  if (loaded) return;
  loaded = true;
  Saved s;
  if (prefs.isKey("tigo") && prefs.getBytesLength("tigo") == sizeof s && prefs.getBytes("tigo", &s, sizeof s) == sizeof s) {
    gNeeds.hunger = clamp100(s.hunger); gNeeds.fun = clamp100(s.fun);
    gNeeds.clean = clamp100(s.clean); gNeeds.energy = clamp100(s.energy);
    gShellsSeen = s.shellsSeen ? s.shellsSeen : 1;
    savedEpoch = s.epoch;
  }
  lastTick = lastSave = millis();
  dbg("tigo: food %d fun %d clean %d energy %d\n", gNeeds.hunger, gNeeds.fun, gNeeds.clean, gNeeds.energy);
}

void tick(uint32_t now) {
  load();
  int32_t dt = (int32_t)(now - lastTick);
  lastTick = now;
  if (dt <= 0) return;
  if (dt > 2000) dt = 2000;   // long gaps (a game in between) do not count
  int *v[4] = {&gNeeds.hunger, &gNeeds.fun, &gNeeds.clean, &gNeeds.energy};
  for (int i = 0; i < 4; i++) {
    acc[i] += dt;
    while (acc[i] >= DECAY_MS[i]) {
      acc[i] -= DECAY_MS[i];
      if (*v[i] > 0) { (*v[i])--; dirty = true; }
    }
  }
  // time away (deep sleep): he got hungry and bored, but he slept well
  if (!offlineDone && timeIsSet()) {
    offlineDone = true;
    time_t e = time(nullptr);
    if (savedEpoch > 1600000000u && (uint32_t)e > savedEpoch) {
      uint32_t hours = ((uint32_t)e - savedEpoch) / 3600;
      if (hours) {
        int h = hours > 20 ? 20 : (int)hours;
        gNeeds.hunger = clamp100(gNeeds.hunger - (h * 3 > 40 ? 40 : h * 3));
        gNeeds.fun = clamp100(gNeeds.fun - (h * 3 > 40 ? 40 : h * 3));
        gNeeds.clean = clamp100(gNeeds.clean - (h * 2 > 30 ? 30 : h * 2));
        gNeeds.energy = 100;
        dirty = true;
      }
    }
  }
}

void save(uint32_t now, bool force) {
  load();
  if (!dirty) return;
  if (!force && (int32_t)(now - lastSave) < 180000) return;
  Saved s;
  memset(&s, 0, sizeof s);
  s.hunger = gNeeds.hunger; s.fun = gNeeds.fun; s.clean = gNeeds.clean; s.energy = gNeeds.energy;
  s.shellsSeen = gShellsSeen;
  s.epoch = timeIsSet() ? (uint32_t)time(nullptr) : savedEpoch;
  prefs.putBytes("tigo", &s, sizeof s);
  lastSave = now;
  dirty = false;
}

void feed(int amount) { load(); gNeeds.hunger = clamp100(gNeeds.hunger + amount); dirty = true; }
void play(int amount) { load(); gNeeds.fun = clamp100(gNeeds.fun + amount); dirty = true; }
void wash() { load(); gNeeds.clean = 100; dirty = true; }
void rest(int amount) { load(); gNeeds.energy = clamp100(gNeeds.energy + amount); dirty = true; }
int mood() { return (gNeeds.hunger + gNeeds.fun + gNeeds.clean + gNeeds.energy) / 4; }

// ---------------- shells ----------------
int gamesWithScore() {
  int n = 0;
  for (int i = 0; i < numGames(); i++) {
    const char *key = nullptr;
    gameInfo(i, &key);
    if (key && prefs.isKey(key) && prefs.getUInt(key, 0) > 0) n++;
  }
  return n;
}

int shellsUnlocked() {
  int g = gamesWithScore(), n = 0;
  while (n < NUM_SHELLS && g >= SHELL_AT[n]) n++;
  return max(1, n);
}

int shell() {
  // drawn every frame (menu icon, screensaver): re-read the ~56 high scores only now and then
  static int cached = -1;
  static uint32_t at = 0;
  uint32_t now = millis();
  if (cached < 0 || (int32_t)(now - at) > 3000) {
    cached = shellsUnlocked() - 1;
    at = now;
  }
  return cached;
}

bool takeNewShell() {
  load();
  int n = shellsUnlocked();
  if (n <= gShellsSeen) return false;
  gShellsSeen = n;
  dirty = true;
  save(millis(), true);
  return true;
}

}  // namespace tigo

void drawTigo(int cx, int cy, int size, TigoPose pose, uint32_t t, bool faceLeft) {
  tigo::drawTigoEx(cx, cy, size, pose, t, faceLeft, tigo::shell(), 255);
}

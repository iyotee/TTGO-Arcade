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

#include "tigo.h"

namespace tigo {

// ---------------- palette ----------------
static const uint16_t PLUM = 0x38E7, CORAL = 0xFB49, CORAL_D = 0xE245, CORAL_L = 0xFCAE, CORAL_SH = 0xDA25;
static const uint16_t PEACH = 0xFD91, PEACH_L = 0xFE96, TIP = 0xFCF0, APER = 0x6989, BLUSH = 0xFBD1;
static const uint16_t MOUTH = 0x8947, TONGUE = 0xFC74, TEAR = 0x7E9F, SPARK = 0xFF0C, CRUMB = 0xCD0D;
static const uint16_t ZZZ_FAR = 0x8D19, WHITE = 0xFFFF, BLACK = 0x0000;

struct ShellStyle { uint16_t base, shade, band, bandL; const char *name; };
static const ShellStyle SHELLS[NUM_SHELLS] = {
  {0xF6B4, 0xD58F, 0x2D95, 0x7F1A, "Sand"},
  {0xFF1D, 0xE598, 0xB35B, 0xDD5E, "Pearl"},
  {0xBF3F, 0x8DDC, 0x2AF8, 0x6CBE, "Lagoon"},
  {0xFEC9, 0xDD24, 0xFFFF, 0xFF98, "Gold"},
  {0x49D1, 0x290C, 0xFAF5, 0xFD19, "Galaxy"},
};
const uint8_t SHELL_AT[NUM_SHELLS] = {0, 5, 15, 30, 45};
const char *shellName(int i) { return SHELLS[constrain(i, 0, NUM_SHELLS - 1)].name; }

// ---------------- pen: Tigo units -> sprite pixels ----------------
struct Pen {
  float cx, cy, k, oy, ol;
  int fl;          // +1 facing right, -1 facing left
  uint8_t dim;     // 255 = full colour
};
static Pen P;

static uint16_t C(uint16_t c) { return P.dim == 255 ? c : tft.alphaBlend(P.dim, c, TFT_BLACK); }
static float X(float x) { return P.cx + P.fl * x * P.k; }
static float Y(float y) { return P.cy + (y + P.oy) * P.k; }
static float R(float r) { return r * P.k; }

static void spotU(float x, float y, float r, uint16_t c) { spr.drawSpot(X(x), Y(y), R(r), C(c)); }
static void wedgeU(float ax, float ay, float bx, float by, float ar, float br, uint16_t c) {
  spr.drawWedgeLine(X(ax), Y(ay), X(bx), Y(by), R(ar), R(br), C(c));
}
// a plum outline under the fill
static void spotO(float x, float y, float r, uint16_t c) {
  spr.drawSpot(X(x), Y(y), R(r) + P.ol, C(PLUM));
  spotU(x, y, r, c);
}
static void wedgeO(float ax, float ay, float bx, float by, float ar, float br, uint16_t c) {
  spr.drawWedgeLine(X(ax), Y(ay), X(bx), Y(by), R(ar) + P.ol, R(br) + P.ol, C(PLUM));
  wedgeU(ax, ay, bx, by, ar, br, c);
}
static void lineU(float ax, float ay, float bx, float by, float rr, uint16_t c) {
  spr.drawWedgeLine(X(ax), Y(ay), X(bx), Y(by), R(rr), R(rr), C(c));
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
// Spiral shell centred on (sx, sy) in units, radius 22 units, apex pointing back-up.
static void shellAt(float sx, float sy, const ShellStyle &st) {
  const float SR = 22.0f;
  float ax = sx - 9, ay = sy - 12, tx = sx - 15, ty = sy - 22;
  wedgeO(ax, ay, tx, ty, 8, 2.2f, st.base);                // apex
  spotO(sx, sy, SR, st.shade);
  spotU(sx - 1.5f, sy - 2.0f, SR - 2.2f, st.base);           // lit from top-left: shaded crescent bottom-right
  wedgeU(ax + 1, ay + 1, tx, ty, 6.4f, 1.5f, st.base);
  // spiral band, widening outwards, with a thin light edge
  float ox = sx - 3.0f, oy = sy - 2.0f;
  const int N = P.k < 0.5f ? 16 : 30;
  float px = 0, py = 0, pw = 0;
  for (int i = 0; i <= N; i++) {
    float f = (float)i / N;
    float th = -1.2f + f * 4.0f * (float)M_PI * 0.86f;
    float r = 1.5f + f * (SR - 6.5f);
    float x = ox + cosf(th) * r, y = oy + sinf(th) * r, w = 0.7f + f * 1.7f;
    if (i) wedgeU(px, py, x, y, pw, w, st.band);
    px = x; py = y; pw = w;
  }
  if (P.k >= 0.5f) {
    for (int i = 0; i <= N; i++) {
      float f = (float)i / N;
      float th = -1.2f + f * 4.0f * (float)M_PI * 0.86f;
      float r = 1.5f + f * (SR - 6.5f) - 0.6f * f;
      float x = ox + cosf(th) * r, y = oy + sinf(th) * r, w = 0.25f + f * 0.5f;
      if (i && f > 0.15f) wedgeU(px, py, x, y, pw, w, st.bandL);
      px = x; py = y; pw = w;
    }
  }
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
  for (auto &p : parts) spr.drawWedgeLine(X(p[0]), Y(p[1]), X(p[2]), Y(p[3]), R(p[4]) + P.ol, R(p[5]) + P.ol, C(PLUM));
  for (auto &p : parts) wedgeU(p[0], p[1], p[2], p[3], p[4], p[5], cbase);
  for (int i = 0; i < 2; i++) {            // lighter tips
    float mx = f[i][0] + (f[i][2] - f[i][0]) * 0.62f, my = f[i][1] + (f[i][3] - f[i][1]) * 0.62f;
    wedgeU(mx, my, f[i][2], f[i][3], 1.4f * sc, 0.7f * sc, TIP);
  }
  wedgeU(hx - nx * 2.2f * sc + dx * 0.5f, hy - ny * 2.2f * sc + dy * 0.5f,      // shine on the hand
         h2x - nx * 2.4f * sc, h2y - ny * 2.4f * sc, 1.3f * sc, 1.0f * sc, far ? CORAL : CORAL_L);
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
  spr.drawWedgeLine(X(hx), Y(hy), X(kx), Y(ky), R(2.0f) + P.ol, R(1.6f) + P.ol, C(PLUM));
  spr.drawWedgeLine(X(kx), Y(ky), X(fx), Y(fy), R(1.6f) + P.ol, R(0.7f) + P.ol, C(PLUM));
  wedgeU(hx, hy, kx, ky, 2.0f, 1.6f, c);
  wedgeU(kx, ky, fx, fy, 1.6f, 0.7f, c);
  spotU(kx, ky, 1.6f, c);
}

// ---------------- eyes ----------------
enum EyeMode { EYE_OPEN, EYE_CLOSED, EYE_HAPPY, EYE_SAD, EYE_FOCUS };

static void eye(float ex, float ey, float er, float lx, float ly, EyeMode e) {
  spr.drawSpot(X(ex), Y(ey), R(er) + P.ol, C(PLUM));
  if (e == EYE_CLOSED) {
    spotU(ex, ey, er, CORAL);
    lineU(ex - er * 0.7f, ey, ex, ey + er * 0.35f, 0.7f, PLUM);
    lineU(ex, ey + er * 0.35f, ex + er * 0.7f, ey, 0.7f, PLUM);
    return;
  }
  if (e == EYE_HAPPY) {   // ^ ^
    spotU(ex, ey, er, CORAL);
    lineU(ex - er * 0.65f, ey + er * 0.25f, ex, ey - er * 0.35f, 0.8f, PLUM);
    lineU(ex, ey - er * 0.35f, ex + er * 0.65f, ey + er * 0.25f, 0.8f, PLUM);
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
    clipAbove(Y(lid), [&]() { spotU(ex, ey, er, CORAL); });
    if (e == EYE_SAD) lineU(ex - er * 0.95f, lid - 0.2f, ex + er * 0.95f, lid + 0.6f, 0.6f, PLUM);
  }
}

// ---------------- the crab ----------------
static void drawCrab(int cx, int cy, int size, TigoPose pose, uint32_t t, bool faceLeft, int shellStyle, uint8_t dim) {
  const float TAU = 6.2831853f;
  float k = size / 58.0f;
  P.cx = cx; P.cy = cy; P.k = k; P.oy = 0; P.fl = faceLeft ? -1 : 1; P.dim = dim;
  P.ol = fmaxf(0.7f, 1.15f * k);
  const ShellStyle &st = SHELLS[constrain(shellStyle, 0, NUM_SHELLS - 1)];

  // ---- pose parameters ----
  float breath = sinf((t % 2400) * TAU / 2400.0f);
  float jump = 0, bdx = 0, bdy = breath * 0.6f;
  float openN = 0.35f, openF = 0.3f, raiseN = 0, raiseF = 0, droop = 0, walk = 0;
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
      break;
    case TIGO_SAD:
      eyes = EYE_SAD; mouth = M_FROWN; bdy = 1.5f + breath * 0.4f; droop = 0.6f;
      openN = openF = 0.05f;
      break;
    case TIGO_PINCH: {
      float snap = fabsf(sinf((t % 360) * TAU / 360.0f));
      openN = snap; openF = 1 - snap; raiseN = raiseF = 0.35f;
      eyes = EYE_FOCUS; mouth = M_GRIT;
      break;
    }
    default: break;
  }
  P.oy = jump;

  float Sx = -6.0f, Sy = -23.0f;
  if (pose == TIGO_HIDE && (t % 2600) < 400) Sx += sinf((t % 90) * TAU / 90.0f) * 0.4f;   // trembles
  float Bx = 14.0f + bdx, By = -15.0f + bdy;

  shellAt(Sx, Sy, st);

  if (pose == TIGO_HIDE) {   // dark opening with two eyes and two claw tips peeking out
    spotO(Sx + 9, Sy + 9, 7.0f, APER);
    wedgeO(Sx + 12, Sy + 15, Sx + 16, Sy + 14, 1.8f, 1.2f, CORAL_D);
    wedgeO(Sx + 6, Sy + 15.5f, Sx + 9, Sy + 16, 1.6f, 1.1f, CORAL_D);
    float look = sinf((t % 3000) * TAU / 3000.0f);
    bool blink = (t % 3100) < 120;
    const float ex[2] = {Sx + 6.5f, Sx + 11.8f}, ey[2] = {Sy + 6.5f, Sy + 7.0f};
    for (int i = 0; i < 2; i++) {
      if (blink) { lineU(ex[i] - 2, ey[i], ex[i] + 2, ey[i], 0.6f, WHITE); continue; }
      spotO(ex[i], ey[i], 2.6f, WHITE);
      spotU(ex[i] + 0.6f + look * 0.8f, ey[i] + 0.3f, 1.4f, BLACK);
      if (k > 0.5f) spotU(ex[i] + 0.1f + look * 0.8f, ey[i] - 0.4f, 0.5f, WHITE);
    }
    return;
  }

  spotU(Sx + 13, Sy + 12, 8.0f, APER);          // shadow of the shell opening behind the body

  if (pose != TIGO_SLEEP) {
    leg(Bx - 7, By + 7.5f, 0, CORAL_SH, true, walk, walking);
    leg(Bx - 2, By + 7.5f, 1, CORAL_SH, true, walk, walking);
  }

  // far claw, behind the body
  claw(Bx + 6, By + 4 - raiseF * 4, Bx + 17 - raiseF * 2, By + 3 - raiseF * 11, -38 - raiseF * 40, openF, 0.8f, CORAL_SH, true);

  // body: shaded base, lit top, peach belly
  wedgeO(Bx - 6, By, Bx + 5, By - 1, 11, 10, CORAL_SH);
  wedgeU(Bx - 6.3f, By - 1.3f, Bx + 4.6f, By - 2.0f, 9.6f, 8.6f, CORAL);
  wedgeU(Bx - 4, By - 6, Bx + 3, By - 6.8f, 3.0f, 2.2f, CORAL_L);
  wedgeU(Bx - 1, By + 4.2f, Bx + 6, By + 3.4f, 5.6f, 4.8f, PEACH);
  wedgeU(Bx + 0.5f, By + 3.0f, Bx + 5, By + 2.6f, 2.4f, 1.6f, PEACH_L);

  // face: blush and mouth
  spotU(Bx + 8.0f, By - 2.5f, 2.0f, tft.alphaBlend(140, BLUSH, CORAL));
  float mx = Bx + 11.5f, my = By - 5.5f;
  switch (mouth) {
    case M_SMILE:
      lineU(mx - 2.4f, my - 0.6f, mx, my + 1.0f, 0.75f, PLUM);
      lineU(mx, my + 1.0f, mx + 1.6f, my - 0.6f, 0.75f, PLUM);
      break;
    case M_GRIN:
      wedgeO(mx - 2.0f, my, mx + 1.2f, my + 0.3f, 1.2f, 1.6f, MOUTH);
      spotU(mx - 0.2f, my + 1.0f, 0.8f, TONGUE);
      break;
    case M_OPEN: spotO(mx - 0.5f, my + 0.5f, 1.8f, MOUTH); break;
    case M_CHOMP: spotO(mx - 0.5f, my + 0.5f, 0.9f, MOUTH); break;
    case M_FROWN:
      lineU(mx - 2.4f, my + 1.2f, mx - 0.4f, my - 0.2f, 0.6f, PLUM);
      lineU(mx - 0.4f, my - 0.2f, mx + 1.4f, my + 1.2f, 0.6f, PLUM);
      break;
    case M_GRIT:
      lineU(mx - 2.2f, my + 0.2f, mx + 1.2f, my + 0.2f, 0.9f, PLUM);
      if (k > 0.5f) lineU(mx - 1.5f, my + 0.2f, mx + 0.5f, my + 0.2f, 0.3f, WHITE);
      break;
    case M_SLEEP: spotO(mx - 0.8f, my + 0.6f, 0.9f, MOUTH); break;
  }

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
  for (int i = 0; i < 2; i++) wedgeO(sbx[i], sby[i], ex[i] - (ex[i] - sbx[i]) * 0.2f, ey[i] + er[i] * 0.6f, 1.8f, 1.3f, CORAL);
  for (int i = 0; i < 2; i++) eye(ex[i], ey[i], er[i], lx, ly, eyes == EYE_OPEN && blink ? EYE_CLOSED : eyes);

  if (pose != TIGO_SLEEP) {
    leg(Bx + 1, By + 8.0f, 2, CORAL, false, walk, walking);
    leg(Bx + 6, By + 8.0f, 3, CORAL, false, walk, walking);
  }

  // near claw, in front
  if (pose == TIGO_EAT)
    claw(Bx + 5, By + 7, Bx + 13 - raiseN * 2, By + 8 - raiseN * 9, -12 - raiseN * 95, openN, 1.0f, CORAL_D, false);
  else
    claw(Bx + 5, By + 7, Bx + 14 - raiseN, By + 8 - raiseN * 9, -12 - raiseN * 45, openN, 1.0f, CORAL_D, false);

  // ---- extras ----
  if (pose == TIGO_SLEEP) {                 // Zzz drifting up
    for (int i = 0; i < 3; i++) {
      float ph = fmodf(t / 2400.0f + i / 3.0f, 1.0f);
      float zx = Bx + 6 + ph * 10, zy = By - 16 - ph * 22, zs = 1.5f + ph * 2.2f;
      uint16_t c = C(tft.alphaBlend((uint8_t)(255 * sqrtf(1 - ph)), WHITE, ZZZ_FAR));
      float w = fmaxf(1.0f, R(0.8f));
      spr.drawWideLine(X(zx - zs), Y(zy - zs), X(zx + zs), Y(zy - zs), w, c);
      spr.drawWideLine(X(zx + zs), Y(zy - zs), X(zx - zs), Y(zy + zs), w, c);
      spr.drawWideLine(X(zx - zs), Y(zy + zs), X(zx + zs), Y(zy + zs), w, c);
    }
  } else if (pose == TIGO_SAD) {            // a tear rolling down
    float ph = (t % 1600) / 1600.0f;
    float tx = ex[1] + 3, ty = ey[1] + 6 + ph * 10;
    wedgeU(tx, ty - 1.8f, tx, ty, 0.3f, 1.1f, TEAR);
  } else if (pose == TIGO_HAPPY) {          // little sparkles
    for (int i = 0; i < 3; i++) {
      float ph = fmodf(t / 1200.0f + i / 3.0f, 1.0f);
      float a = i * 2.1f + 0.6f;
      float px = Bx + 4 + cosf(a) * (14 + ph * 8), py = By - 18 + sinf(a) * (8 + ph * 6) - ph * 6;
      float r = 1.6f * (1 - ph);
      lineU(px - r * 2, py, px + r * 2, py, 0.4f, SPARK);
      lineU(px, py - r * 2, px, py + r * 2, 0.4f, SPARK);
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
  P.k = r / 22.0f; P.cx = cx; P.cy = cy; P.oy = 0; P.fl = faceLeft ? -1 : 1; P.dim = 255;
  P.ol = fmaxf(0.7f, 1.15f * P.k);
  shellAt(0, 0, SHELLS[constrain(style, 0, NUM_SHELLS - 1)]);
}

void drawEye(float x, float y, float r, float lx, float ly, bool closed) {
  P.k = 1; P.cx = x; P.cy = y; P.oy = 0; P.fl = 1; P.dim = 255; P.ol = 1.1f;
  eye(0, 0, r, lx, ly, closed ? EYE_CLOSED : EYE_OPEN);
}

void drawShadow(int cx, int cy, int size) {
  // a flat translucent ellipse: darken what is already in the sprite
  float k = size / 58.0f;
  int x0 = cx - (int)(29 * k), x1 = cx + (int)(36 * k);
  int hh = max(1, (int)(3.0f * k)), mid = (x0 + x1) / 2, rx = (x1 - x0) / 2;
  if (rx < 1) return;
  for (int dy = -hh; dy <= hh; dy++) {
    float f = 1.0f - (float)(dy * dy) / ((hh + 0.5f) * (hh + 0.5f));
    if (f <= 0) continue;
    int w = (int)(rx * sqrtf(f));
    int y = cy + dy;
    for (int x = mid - w; x <= mid + w; x++) {
      int edge = min(x - (mid - w), (mid + w) - x);
      uint8_t a = edge < 3 ? 40 + edge * 20 : 100;
      spr.drawPixel(x, y, tft.alphaBlend(a, PLUM, spr.readPixel(x, y)));
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
  spr.fillSmoothRoundRect(x - 1, y - 1, w + 2, h + 2, 8, PLUM);
  spr.fillTriangle(tail - 5, y + h - 1, tail + 4, y + h - 1, tail, y + h + 8, PLUM);
  spr.fillSmoothRoundRect(x, y, w, h, 7, WHITE);
  spr.fillTriangle(tail - 4, y + h - 2, tail + 3, y + h - 2, tail, y + h + 6, WHITE);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(PLUM);
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

// Pool (top-down billiards for one or two players)
//
// Controls (two buttons):
//   Hold L / Hold R : turn the aim (slowly at first, faster the longer you hold)
//   Tap L / Tap R   : nudge the aim by a small step
//   Hold L + R      : charge the power (the meter swings up and down),
//                     release to shoot
//   Ball in hand    : L / R slide the cue ball, press L + R to put it down
// Title: Tap L = 1 or 2 players, Tap R = start.
// 1 player: pot all 15 balls in as few shots (and as little time) as you can.
// 2 players, taking turns: simplified 8-ball. The first legal pot gives that
// player solids or stripes; pot your group, then the 8. A scratch hands the
// other player the cue ball behind the head string; the 8 sunk early loses.

#include "common.h"
#include "themeui.h"

namespace pool {

// ---------------- table geometry (px, portrait) ----------------
static const int SW = 135, SH = 240;
static const int HUD_H = 17;                                  // score strip above the table
static const float TBL_L = 13, TBL_R = 122, TBL_T = 30, TBL_B = 226;  // cushion faces
static const float TBL_MX = 67.5f;
static const float BALL_R = 4.5f;                                 // ball radius
static const float FOOT_Y = TBL_T + (TBL_B - TBL_T) / 4;            // apex of the rack
static const float HEAD_Y = TBL_B - (TBL_B - TBL_T) / 4;            // head string
static const float PLACE_Y = (HEAD_Y + TBL_B) / 2;              // ball in hand sits on this line

struct Pocket { float x, y, r, cap; };                         // hole radius, capture radius for a ball centre
static const Pocket POCKETS[6] = {
  {10, 27, 7.5f, 6.0f}, {125, 27, 7.5f, 6.0f},
  {8, 128, 7.0f, 6.0f}, {127, 128, 7.0f, 6.0f},
  {10, 229, 7.5f, 6.0f}, {125, 229, 7.5f, 6.0f},
};
// cushions, each back -> nose -> nose -> back: the angled ends are the pocket jaws
static const float CUSH[6][4][2] = {
  {{17, 26}, {22, 30}, {113, 30}, {118, 26}},                  // top
  {{17, 230}, {22, 226}, {113, 226}, {118, 230}},              // bottom
  {{9, 34}, {13, 39}, {13, 120}, {9, 122}},                    // left, upper
  {{9, 222}, {13, 217}, {13, 136}, {9, 134}},                  // left, lower
  {{126, 34}, {122, 39}, {122, 120}, {126, 122}},              // right, upper
  {{126, 222}, {122, 217}, {122, 136}, {126, 134}},            // right, lower
};

// ---------------- physics tuning (per 16 ms tick) ----------------
static const uint32_t TICK_MS = 16, HOLD_MS = 200;
static const float FRICT_MUL = 0.987f, FRICT_SUB = 0.006f;    // rolling resistance
static const float STOP_V = 0.02f;
static const float E_BALL = 0.94f, E_CUSH = 0.72f;
static const float FOLLOW = 0.22f;                            // natural roll: the cue ball follows through a bit
static const float MAX_SHOT = 10.5f, MIN_SHOT = 0.35f;
static const float MAX_STEP = 1.5f;                           // sub-step length: well under a ball radius
static const int POWER_PERIOD = 96;                           // the power meter goes up and down in ~1.5 s
static const int DROP_TICKS = 16, BANNER_TICKS = 80;

// ---------------- balls ----------------
// 0 = cue, 1..7 solids, 8 = black, 9..15 stripes
static const uint8_t BALL_RGB[9][3] = {
  {246, 244, 234}, {250, 196, 18}, {28, 72, 205}, {222, 32, 30}, {112, 42, 165},
  {250, 112, 22}, {30, 178, 96}, {135, 26, 38}, {24, 24, 28},
};

struct Ball {
  float x, y, vx, vy;
  float ax[3], az[3];          // body axes in table space: x = number spot, z = stripe axis
  bool on;
};
static Ball balls[16];

struct Drop { int8_t id, pk, t; float x, y; };               // a ball falling into a pocket
static Drop drops[16];
static int nDrops = 0;
static int8_t pocketFx[6];                                    // gold ring after a pot

enum State { TITLE, PLAY, GAMEOVER };
enum Phase { PH_PLACE, PH_AIM, PH_ROLL };
static State state = TITLE;
static Phase phase = PH_PLACE;

static bool twoPlayers = false;
static float aim = -PI / 2, power = 0, placeX = TBL_MX;
static bool charging = false, waitRelease = false;
static int powerT = 0, holdTL = 0, holdTR = 0, animT = 0;

// shot bookkeeping
static int firstHit = -1, firstPot = -1;
static uint16_t pottedMask = 0;
static bool scratch = false, breakShot = true, breakShown = false;

// players
static int cur = 0, winner = -1;
static uint8_t group[2];                                      // 0 = open, 1 = solids, 2 = stripes
static const char *endWhy = "";
static int shots = 0;
static uint32_t playTicks = 0;

// banner across the table
static const char *banTxt = nullptr, *banSub = nullptr;
static uint16_t banCol = TFT_WHITE;
static int banT = 0;

static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;
static uint32_t drawUs = 0, frameUs = 0, nFrames = 0, lastFrameAt = 0;

static int rnd(int n) { return esp_random() % n; }

// ---------------- pixel helpers ----------------
// The ball shading works per pixel; with the usual 16-bit portrait sprite it
// writes the buffer directly (stored byte-swapped), otherwise it falls back.
static uint16_t *fb = nullptr;

static inline uint16_t rgb(int r, int g, int b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }
static inline void unrgb(uint16_t c, int &r, int &g, int &b) {
  r = (c >> 8) & 0xF8; r |= r >> 5;
  g = (c >> 3) & 0xFC; g |= g >> 6;
  b = (c << 3) & 0xF8; b |= b >> 5;
}
static inline uint16_t getPx(int x, int y) {
  if (fb) { uint16_t v = fb[y * SW + x]; return (v >> 8) | (v << 8); }
  return spr.readPixel(x, y);
}
static inline void putPx(int x, int y, uint16_t c) {
  if (fb) fb[y * SW + x] = (c >> 8) | (c << 8);
  else spr.drawPixel(x, y, c);
}
static inline bool onScreen(int x, int y) { return x >= 0 && y >= 0 && x < SW && y < SH; }
// mixes (r, g, b) over the pixel with alpha a (0..255)
static inline void blendPx(int x, int y, int r, int g, int b, int a) {
  if (!onScreen(x, y)) return;
  int pr, pg, pb;
  unrgb(getPx(x, y), pr, pg, pb);
  putPx(x, y, rgb(pr + ((r - pr) * a >> 8), pg + ((g - pg) * a >> 8), pb + ((b - pb) * a >> 8)));
}
static inline uint16_t mix565(int r, int g, int b, float f) {
  return rgb(min(255, (int)(r * f)), min(255, (int)(g * f)), min(255, (int)(b * f)));
}

// ---------------- precomputed ball shading ----------------
// A ball is drawn on a 10x10 grid whose centre is a pixel corner. For each
// pixel: surface normal, light, highlight and edge coverage (anti-aliasing).
static const int GRID = 10, SGRID = 13;
struct PixInfo { float nx, ny, nz; uint8_t shade, spec, cov, ring; };
static PixInfo pix[GRID * GRID];
static uint8_t shadowA[SGRID * SGRID];                              // soft shadow, offset down-right
static bool tablesReady = false;

static void initTables() {
  if (tablesReady) return;
  const float lx = -0.45f, ly = -0.55f, lz = 0.70f;          // light from the upper left
  float ll = sqrtf(lx * lx + ly * ly + lz * lz);
  float hx = lx / ll, hy = ly / ll, hz = lz / ll + 1;         // half vector towards the viewer
  float hl = sqrtf(hx * hx + hy * hy + hz * hz);
  hx /= hl; hy /= hl; hz /= hl;
  for (int j = 0; j < GRID; j++)
    for (int i = 0; i < GRID; i++) {
      PixInfo &p = pix[j * GRID + i];
      float u = i + 0.5f - GRID / 2, v = j + 0.5f - GRID / 2;
      int in = 0, inRing = 0;
      for (int sy = 0; sy < 4; sy++)
        for (int sx = 0; sx < 4; sx++) {
          float su = i + (sx + 0.5f) / 4 - GRID / 2, sv = j + (sy + 0.5f) / 4 - GRID / 2;
          float d = sqrtf(su * su + sv * sv);
          if (d < BALL_R) in++;
          if (d < BALL_R - 0.2f && d > BALL_R - 1.3f) inRing++;
        }
      p.cov = in * 255 / 16;
      p.ring = inRing * 255 / 16;
      float d = sqrtf(u * u + v * v), dm = BALL_R * 0.97f;
      if (d > dm) { u *= dm / d; v *= dm / d; d = dm; }
      p.nx = u / BALL_R;
      p.ny = v / BALL_R;
      p.nz = sqrtf(max(0.0f, 1 - p.nx * p.nx - p.ny * p.ny));
      float diff = max(0.0f, (p.nx * lx + p.ny * ly + p.nz * lz) / ll);
      float sh = 0.32f + 0.80f * diff;
      sh *= 0.80f + 0.20f * p.nz;                             // darker towards the rim
      p.shade = (uint8_t)min(255.0f, sh * 255);
      float s = max(0.0f, p.nx * hx + p.ny * hy + p.nz * hz);
      s = powf(s, 28);
      p.spec = (uint8_t)min(255.0f, s * 240);
    }
  for (int j = 0; j < SGRID; j++)
    for (int i = 0; i < SGRID; i++) {
      float u = i + 0.5f - GRID / 2 - 1.4f, v = j + 0.5f - GRID / 2 - 2.0f;
      float d = sqrtf(u * u + v * v);
      float a = constrain((BALL_R + 0.6f - d) / 1.6f, 0.0f, 1.0f);
      shadowA[j * SGRID + i] = (uint8_t)(a * a * 120);
    }
  tablesReady = true;
}

// ---------------- rolling ----------------
static void rotateVec(float *v, float kx, float ky, float c, float s) {
  // Rodrigues around the axis (kx, ky, 0)
  float x = v[0], y = v[1], z = v[2];
  float kv = kx * x + ky * y;
  float cx = ky * z, cy = -kx * z, cz = kx * y - ky * x;
  v[0] = x * c + cx * s + kx * kv * (1 - c);
  v[1] = y * c + cy * s + ky * kv * (1 - c);
  v[2] = z * c + cz * s;
}

static void normalize3(float *v) {
  float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (l > 1e-6f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

// rolling by (dx, dy) without slipping turns the ball by distance / radius
static void roll(Ball &b, float dx, float dy) {
  float d = sqrtf(dx * dx + dy * dy);
  if (d < 1e-4f) return;
  float kx = -dy / d, ky = dx / d, a = d / BALL_R, c = cosf(a), s = sinf(a);
  rotateVec(b.ax, kx, ky, c, s);
  rotateVec(b.az, kx, ky, c, s);
  // keep the two axes unit length and perpendicular
  normalize3(b.ax);
  float dp = b.az[0] * b.ax[0] + b.az[1] * b.ax[1] + b.az[2] * b.ax[2];
  for (int k = 0; k < 3; k++) b.az[k] -= dp * b.ax[k];
  normalize3(b.az);
}

static void randomSpin(Ball &b) {
  // number spot roughly towards the viewer, stripe axis lying flat, then a random tilt
  b.ax[0] = 0; b.ax[1] = 0; b.ax[2] = 1;
  float a = rnd(628) / 100.0f;
  b.az[0] = cosf(a); b.az[1] = sinf(a); b.az[2] = 0;
  roll(b, (rnd(61) - 30) / 10.0f, (rnd(61) - 30) / 10.0f);
}

// ---------------- rules helpers ----------------
static int groupOf(int id) { return id >= 1 && id <= 7 ? 1 : id >= 9 ? 2 : 0; }
static int groupLeft(int g) {
  int n = 0;
  for (int i = 1; i < 16; i++) if (balls[i].on && groupOf(i) == g) n++;
  return n;
}
static int ballsLeft() {
  int n = 0;
  for (int i = 1; i < 16; i++) if (balls[i].on) n++;
  return n;
}
static bool isLegalTarget(int id) {
  if (!twoPlayers) return true;
  int g = group[cur];
  if (!g) return id != 8;
  if (groupLeft(g) == 0) return id == 8;
  return groupOf(id) == g;
}

static void banner(const char *t, const char *sub, uint16_t col) {
  banTxt = t;
  banSub = sub;
  banCol = col;
  banT = BANNER_TICKS;
}

static bool spotFree(float x, float y, int except) {
  for (int i = 0; i < 16; i++) {
    if (i == except || !balls[i].on) continue;
    float dx = balls[i].x - x, dy = balls[i].y - y;
    if (dx * dx + dy * dy < (2 * BALL_R + 0.6f) * (2 * BALL_R + 0.6f)) return false;
  }
  return true;
}

// aim at the nearest ball the shooter may hit first: a friendly start for each turn
static void autoAim() {
  const Ball &c = balls[0];
  float best = 1e9f;
  for (int i = 1; i < 16; i++) {
    if (!balls[i].on || !isLegalTarget(i)) continue;
    float dx = balls[i].x - c.x, dy = balls[i].y - c.y, d = dx * dx + dy * dy;
    if (d < best) { best = d; aim = atan2f(dy, dx); }
  }
}

// ---------------- game flow ----------------
static void rack() {
  // the 8 in the middle of the third row, a solid and a stripe in the back corners
  int order[15], solids[7], stripes[7], ns = 0, nt = 0;
  for (int i = 1; i <= 7; i++) solids[ns++] = i;
  for (int i = 9; i <= 15; i++) stripes[nt++] = i;
  for (int i = 6; i > 0; i--) { int j = rnd(i + 1); int t = solids[i]; solids[i] = solids[j]; solids[j] = t; }
  for (int i = 6; i > 0; i--) { int j = rnd(i + 1); int t = stripes[i]; stripes[i] = stripes[j]; stripes[j] = t; }
  int pool14[14], np = 0;
  for (int i = 0; i < 6; i++) { pool14[np++] = solids[i + 1]; pool14[np++] = stripes[i + 1]; }
  for (int i = 11; i > 0; i--) { int j = rnd(i + 1); int t = pool14[i]; pool14[i] = pool14[j]; pool14[j] = t; }
  // positions in row-major order: row k (0 = apex) has k + 1 balls
  int k = 0, take = 0;
  for (int row = 0; row < 5; row++)
    for (int j = 0; j <= row; j++, k++) {
      int id;
      if (row == 2 && j == 1) id = 8;
      else if (row == 4 && j == 0) id = solids[0];
      else if (row == 4 && j == 4) id = stripes[0];
      else id = pool14[take++];
      order[k] = id;
      Ball &b = balls[id];
      b.x = TBL_MX + (j - row / 2.0f) * (2 * BALL_R + 0.15f) + (rnd(5) - 2) * 0.02f;
      b.y = FOOT_Y - row * (2 * BALL_R + 0.15f) * 0.8660f;
      b.vx = b.vy = 0;
      b.on = true;
      randomSpin(b);
    }
  (void)order;
}

static void startPlace() {
  phase = PH_PLACE;
  Ball &c = balls[0];
  c.on = true;
  c.vx = c.vy = 0;
  c.y = PLACE_Y;
  placeX = TBL_MX;
  for (int k = 0; k < 40 && !spotFree(placeX, PLACE_Y, 0); k++) placeX += (k & 1 ? -1 : 1) * (k + 1) * 2.0f;
  placeX = constrain(placeX, TBL_L + BALL_R, TBL_R - BALL_R);
  c.x = placeX;
  waitRelease = bL.stable || bR.stable;
  charging = false;
}

static void startAim() {
  phase = PH_AIM;
  autoAim();
  waitRelease = bL.stable || bR.stable;
  charging = false;
  power = 0;
}

static void newGame() {
  for (int i = 0; i < 16; i++) balls[i].on = false;
  rack();
  randomSpin(balls[0]);
  nDrops = 0;
  for (int i = 0; i < 6; i++) pocketFx[i] = 0;
  group[0] = group[1] = 0;
  cur = 0;
  winner = -1;
  shots = 0;
  playTicks = 0;
  breakShot = true;
  breakShown = false;
  newHigh = false;
  score = 0;
  banT = 0;
  aim = -PI / 2;
  startPlace();
  if (twoPlayers) banner("PLAYER 1", "to break", TFT_YELLOW);
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void finishSolo() {
  uint32_t secs = playTicks * TICK_MS / 1000;
  // fewer shots and less time = more points; always above zero once cleared
  score = (150000UL / (uint32_t)(shots + 5)) * 600UL / (600UL + secs);
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("pool_hi", hiScore);
  }
  state = GAMEOVER;
  armed = false;
  lastInput = millis();
  dbg("pool: cleared in %d shots, %lu s, score %lu", shots, (unsigned long)secs, (unsigned long)score);
}

static void win(int p, const char *why) {
  winner = p;
  endWhy = why;
  state = GAMEOVER;
  armed = false;
  lastInput = millis();
}

static void respotEight() {
  Ball &b = balls[8];
  b.x = TBL_MX;
  b.y = FOOT_Y;
  b.vx = b.vy = 0;
  b.on = true;
  while (!spotFree(b.x, b.y, 8) && b.y > TBL_T + BALL_R) b.y -= 1;
}

static void endShotSolo() {
  if (scratch) {
    shots++;                                                  // a scratch costs one extra shot
    banner("SCRATCH", "+1 shot", TFT_RED);
  }
  if (ballsLeft() == 0) { finishSolo(); return; }
  if (scratch) startPlace();
  else startAim();
}

static void endShotDuo() {
  int me = cur, other = 1 - cur;
  int g = group[me];
  // was the shooter already on the 8 before this shot?
  int mineBefore = 0;
  if (g) {
    mineBefore = groupLeft(g);
    for (int i = 1; i < 16; i++) if ((pottedMask >> i & 1) && groupOf(i) == g) mineBefore++;
  }
  bool onEight = g && mineBefore == 0;
  bool foul = scratch;
  const char *why = "";
  if (!foul && firstHit < 0) { foul = true; why = "no ball hit"; }
  else if (!foul && g && (onEight ? firstHit != 8 : groupOf(firstHit) != g)) { foul = true; why = "wrong ball"; }
  else if (!foul && !g && firstHit == 8 && !breakShot) { foul = true; why = "8 hit first"; }

  if (pottedMask & (1 << 8)) {
    if (breakShot) {
      respotEight();                                          // on the break it just comes back
    } else {
      if (onEight && !foul) win(me, "sank the 8");
      else if (scratch) win(other, "scratch on the 8");
      else win(other, onEight ? "foul on the 8" : "8 sunk too early");
      return;
    }
  }

  bool assigned = false;
  if (!g && !foul && firstPot > 0) {
    group[me] = groupOf(firstPot);
    group[other] = 3 - group[me];
    assigned = true;
  }
  bool keep = false;
  if (!foul)
    for (int i = 1; i < 16; i++)
      if ((pottedMask >> i & 1) && i != 8 && groupOf(i) == group[me]) keep = true;

  static char sub[24];
  if (!keep) cur = other;
  if (scratch) {
    snprintf(sub, sizeof sub, "P%d: ball in hand", cur + 1);
    banner("SCRATCH", sub, TFT_RED);
  } else if (foul) {
    snprintf(sub, sizeof sub, "%s: P%d plays", why, cur + 1);
    banner("FOUL", sub, TFT_ORANGE);
  } else if (assigned) {
    snprintf(sub, sizeof sub, "for Player %d", me + 1);
    banner(group[me] == 1 ? "SOLIDS" : "STRIPES", sub, TFT_YELLOW);
  } else if (!keep) {
    banner(cur ? "PLAYER 2" : "PLAYER 1", "your turn", cur ? TFT_CYAN : TFT_YELLOW);
  }
  if (scratch) startPlace();
  else startAim();
}

static void endShot() {
  if (twoPlayers) endShotDuo();
  else endShotSolo();
  breakShot = false;
}

static void shoot() {
  float p = power, spd = MIN_SHOT + (MAX_SHOT - MIN_SHOT) * (0.4f * p + 0.6f * p * p);
  Ball &c = balls[0];
  c.vx = cosf(aim) * spd;
  c.vy = sinf(aim) * spd;
  firstHit = firstPot = -1;
  pottedMask = 0;
  scratch = false;
  shots++;
  phase = PH_ROLL;
  dbg("pool: shot %d power %d%%", shots, (int)(p * 100));
}

// ---------------- physics ----------------
static void pot(int i, int k) {
  Ball &b = balls[i];
  b.on = false;
  if (nDrops < 16) {
    Drop &d = drops[nDrops++];
    d.id = i; d.pk = k; d.t = 0; d.x = b.x; d.y = b.y;
  }
  pocketFx[k] = 18;
  pottedMask |= 1 << i;
  if (i == 0) scratch = true;
  else if (i != 8 && firstPot < 0) firstPot = i;
}

static void collideBalls() {
  for (int i = 0; i < 16; i++) {
    Ball &a = balls[i];
    if (!a.on) continue;
    for (int j = i + 1; j < 16; j++) {
      Ball &b = balls[j];
      if (!b.on) continue;
      float dx = b.x - a.x, dy = b.y - a.y, d2 = dx * dx + dy * dy;
      if (d2 >= 4 * BALL_R * BALL_R || d2 < 1e-6f) continue;
      float d = sqrtf(d2), nx = dx / d, ny = dy / d;
      float vrel = (a.vx - b.vx) * nx + (a.vy - b.vy) * ny;  // closing speed along the line of centres
      if (vrel > 0) {
        float imp = vrel * (1 + E_BALL) / 2;
        a.vx -= imp * nx; a.vy -= imp * ny;
        b.vx += imp * nx; b.vy += imp * ny;
        if (i == 0) { a.vx += FOLLOW * vrel * nx; a.vy += FOLLOW * vrel * ny; }
        if (i == 0 && firstHit < 0) firstHit = j;
        if (breakShot && !breakShown && i == 0) {
          breakShown = true;
          if (vrel > 3) banner("BREAK!", nullptr, TFT_WHITE);
        }
      }
      float push = (2 * BALL_R - d) / 2 + 0.01f;                 // separate the overlap
      a.x -= nx * push; a.y -= ny * push;
      b.x += nx * push; b.y += ny * push;
    }
  }
}

static void collideCushions(Ball &b) {
  // well inside the cushions nothing can touch
  if (b.x > TBL_L + BALL_R + 0.5f && b.x < TBL_R - BALL_R - 0.5f && b.y > TBL_T + BALL_R + 0.5f && b.y < TBL_B - BALL_R - 0.5f) return;
  for (int c = 0; c < 6; c++)
    for (int s = 0; s < 3; s++) {
      float ax = CUSH[c][s][0], ay = CUSH[c][s][1], bx = CUSH[c][s + 1][0], by = CUSH[c][s + 1][1];
      float ex = bx - ax, ey = by - ay, l2 = ex * ex + ey * ey;
      float t = ((b.x - ax) * ex + (b.y - ay) * ey) / l2;
      t = constrain(t, 0.0f, 1.0f);
      float qx = ax + ex * t, qy = ay + ey * t;              // nearest point of the segment
      float dx = b.x - qx, dy = b.y - qy, d2 = dx * dx + dy * dy;
      if (d2 >= BALL_R * BALL_R || d2 < 1e-6f) continue;
      float d = sqrtf(d2), nx = dx / d, ny = dy / d;
      float vn = b.vx * nx + b.vy * ny;
      if (vn < 0) {
        float tx = b.vx - vn * nx, ty = b.vy - vn * ny;      // the cushion grips a little
        b.vx = tx * 0.96f - vn * E_CUSH * nx;
        b.vy = ty * 0.96f - vn * E_CUSH * ny;
      }
      b.x = qx + nx * BALL_R;
      b.y = qy + ny * BALL_R;
    }
}

static void checkPockets(int i) {
  Ball &b = balls[i];
  int nearest = 0;
  float nd = 1e9f;
  for (int k = 0; k < 6; k++) {
    float dx = b.x - POCKETS[k].x, dy = b.y - POCKETS[k].y, d2 = dx * dx + dy * dy;
    if (d2 < POCKETS[k].cap * POCKETS[k].cap) { pot(i, k); return; }
    if (d2 < nd) { nd = d2; nearest = k; }
  }
  // safety net: a ball that somehow got past the jaws goes down the nearest hole
  if (b.x < TBL_L - 7 || b.x > TBL_R + 7 || b.y < TBL_T - 7 || b.y > TBL_B + 7) pot(i, nearest);
}

static void physicsTick() {
  float maxV2 = 0, ox[16], oy[16];
  for (int i = 0; i < 16; i++) {
    ox[i] = balls[i].x;
    oy[i] = balls[i].y;
    if (!balls[i].on) continue;
    maxV2 = max(maxV2, balls[i].vx * balls[i].vx + balls[i].vy * balls[i].vy);
  }
  // enough sub-steps that nothing moves more than MAX_STEP at once: no tunnelling
  int n = (int)(sqrtf(maxV2) / MAX_STEP) + 1;
  if (n > 12) n = 12;
  float h = 1.0f / n;
  for (int s = 0; s < n; s++) {
    for (int i = 0; i < 16; i++) {
      Ball &b = balls[i];
      if (!b.on) continue;
      b.x += b.vx * h;
      b.y += b.vy * h;
    }
    collideBalls();
    for (int i = 0; i < 16; i++) {
      if (!balls[i].on) continue;
      collideCushions(balls[i]);
      checkPockets(i);
    }
  }
  for (int i = 0; i < 16; i++) {
    Ball &b = balls[i];
    if (!b.on) continue;
    roll(b, b.x - ox[i], b.y - oy[i]);
    float v = sqrtf(b.vx * b.vx + b.vy * b.vy);
    if (v < 1e-6f) continue;
    float nv = v * FRICT_MUL - FRICT_SUB;
    if (nv < STOP_V) { b.vx = b.vy = 0; continue; }
    b.vx *= nv / v;
    b.vy *= nv / v;
  }
}

static bool allStill() {
  if (nDrops) return false;
  for (int i = 0; i < 16; i++) if (balls[i].on && (balls[i].vx != 0 || balls[i].vy != 0)) return false;
  return true;
}

// ---------------- input ----------------
static float ramp(int heldTicks, float slow, float fast, float accel) {
  return min(fast, slow + heldTicks * accel);
}

static void handleInput(uint32_t now, int eL, int eR) {
  bool l = bL.stable, r = bR.stable;
  if (waitRelease) {                                          // the last shot's buttons first
    if (!l && !r) waitRelease = false;
    return;
  }
  if (charging) {
    powerT++;
    int f = powerT % POWER_PERIOD;
    power = f < POWER_PERIOD / 2 ? f / (POWER_PERIOD / 2.0f) : (POWER_PERIOD - f) / (POWER_PERIOD / 2.0f);
    if (!l || !r) {
      charging = false;
      waitRelease = true;
      if (phase == PH_AIM) {
        shoot();
      } else if (spotFree(placeX, PLACE_Y, 0)) {
        startAim();
        waitRelease = true;
      }
    }
    return;
  }
  if (l && r) {                                               // both held: power (or "place" with ball in hand)
    charging = true;
    powerT = 0;
    power = 0;
    bL.consumed = bR.consumed = true;
    return;
  }
  bool place = phase == PH_PLACE;
  float d = 0;
  if (l && (int32_t)(now - bL.t0) >= (int32_t)HOLD_MS) {
    bL.consumed = true;
    d -= place ? ramp(holdTL++, 0.3f, 2.0f, 0.03f) : ramp(holdTL++, 0.0025f, 0.03f, 0.00035f);
  } else {
    holdTL = 0;
  }
  if (r && (int32_t)(now - bR.t0) >= (int32_t)HOLD_MS) {
    bR.consumed = true;
    d += place ? ramp(holdTR++, 0.3f, 2.0f, 0.03f) : ramp(holdTR++, 0.0025f, 0.03f, 0.00035f);
  } else {
    holdTR = 0;
  }
  float fine = place ? 1.0f : 0.005f;
  if (eL == 2 && !bL.consumed) d -= fine;
  if (eR == 2 && !bR.consumed) d += fine;
  if (d == 0) return;
  if (place) {
    placeX = constrain(placeX + d, TBL_L + BALL_R, TBL_R - BALL_R);
    balls[0].x = placeX;
    balls[0].y = PLACE_Y;
  } else {
    aim += d;
    if (aim > PI) aim -= 2 * PI;
    if (aim < -PI) aim += 2 * PI;
  }
}

static void tick(uint32_t now, int eL, int eR) {
  animT++;
  if (banT > 0) banT--;
  for (int k = 0; k < 6; k++) if (pocketFx[k] > 0) pocketFx[k]--;
  for (int i = 0; i < nDrops;) {
    if (++drops[i].t >= DROP_TICKS) drops[i] = drops[--nDrops];
    else i++;
  }
  if (shots > 0) playTicks++;
  if (phase == PH_ROLL) {
    physicsTick();
    if (allStill()) endShot();
  } else {
    handleInput(now, eL, eR);
  }
}

// ---------------- drawing: table ----------------
static void fillQuad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, uint16_t col) {
  spr.fillTriangle(lroundf(x0), lroundf(y0), lroundf(x1), lroundf(y1), lroundf(x2), lroundf(y2), col);
  spr.fillTriangle(lroundf(x0), lroundf(y0), lroundf(x2), lroundf(y2), lroundf(x3), lroundf(y3), col);
}

static const uint8_t WOOD_F[9] = {120, 190, 255, 225, 205, 195, 180, 150, 95};   // rail profile, outer to inner

static void drawTable(bool placing) {
  // wooden rails: nested outlines give the rounded profile
  for (int k = 0; k < 9; k++) {
    float f = WOOD_F[k] / 255.0f;
    spr.drawRoundRect(k, 17 + k, SW - 2 * k, 222 - 2 * k, max(0, 7 - k), mix565(118, 58, 26, f));
  }
  // a little grain along the rails
  for (int k = 0; k < 14; k++) {
    int a = (k * 37) % 190 + 30, len = 10 + (k * 13) % 18;
    uint16_t gc = mix565(118, 58, 26, 0.62f);
    if (k & 1) spr.drawFastVLine(k % 4 == 1 ? 5 : 129, a, len, gc);
    else spr.drawFastHLine((k * 23) % 80 + 20, k % 4 == 0 ? 21 : 235, len, gc);
  }
  // felt: darker towards the cushions, brighter under the lamp
  const int fx = 9, fy = 26, fw = 117, fh = 204, steps = 12;
  for (int s = 0; s < steps; s++) {
    float f = 0.80f + 0.20f * s / (steps - 1);
    uint16_t col = rgb((int)(30 * f), (int)(128 * f), (int)(66 * f));
    int ix = fx + s * 4, iy = fy + s * 6, iw = fw - s * 8, ih = fh - s * 12;
    if (s == steps - 1) { spr.fillRect(ix, iy, iw, ih, col); break; }
    spr.fillRect(ix, iy, iw, 6, col);
    spr.fillRect(ix, iy + ih - 6, iw, 6, col);
    spr.fillRect(ix, iy + 6, 4, ih - 12, col);
    spr.fillRect(ix + iw - 4, iy + 6, 4, ih - 12, col);
  }
  // markings: head string, foot spot
  uint16_t mark = placing ? rgb(150, 220, 170) : rgb(52, 150, 90);
  for (int x = (int)TBL_L + 2; x < (int)TBL_R - 2; x += 3) spr.drawPixel(x, (int)HEAD_Y, mark);
  spr.fillCircle((int)TBL_MX, (int)FOOT_Y, 1, rgb(52, 150, 90));
  // cushions with a lit nose and a soft shadow on the felt
  uint16_t cushCol = rgb(20, 104, 52), noseCol = rgb(70, 170, 100), shadowCol = rgb(16, 82, 42);
  for (int c = 0; c < 6; c++) {
    const float (*p)[2] = CUSH[c];
    fillQuad(p[0][0], p[0][1], p[1][0], p[1][1], p[2][0], p[2][1], p[3][0], p[3][1], cushCol);
    bool horiz = p[1][1] == p[2][1];
    int nx0 = (int)p[1][0], ny0 = (int)p[1][1], nx1 = (int)p[2][0], ny1 = (int)p[2][1];
    if (horiz) {
      int y = ny0 < 128 ? ny0 - 1 : ny0, ys = ny0 < 128 ? ny0 : ny0 - 1;
      spr.drawFastHLine(nx0, y, nx1 - nx0, noseCol);
      spr.drawFastHLine(nx0, ys, nx1 - nx0, shadowCol);
    } else {
      int lo = min(ny0, ny1), hi = max(ny0, ny1);
      int x = nx0 < 67 ? nx0 - 1 : nx0, xs = nx0 < 67 ? nx0 : nx0 - 1;
      spr.drawFastVLine(x, lo, hi - lo, noseCol);
      spr.drawFastVLine(xs, lo, hi - lo, shadowCol);
    }
  }
  // pockets: leather liner, then the dark hole, deeper towards the back
  for (int k = 0; k < 6; k++) {
    const Pocket &pk = POCKETS[k];
    int x = lroundf(pk.x), y = lroundf(pk.y), r = (int)pk.r;
    spr.fillCircle(x, y, r + 1, rgb(46, 30, 20));
    spr.fillCircle(x, y, r, rgb(14, 12, 12));
    int ox = x < 67 ? -1 : 1, oy = k < 2 ? -1 : k > 3 ? 1 : 0;
    spr.fillCircle(x + ox, y + oy, r - 2, TFT_BLACK);
    if (pocketFx[k] > 0) {
      int a = pocketFx[k];
      spr.drawCircle(x, y, r + 1 + (18 - a) / 4, a > 9 ? TFT_YELLOW : rgb(200, 150, 40));
    }
  }
  // diamonds (sights) on the rails
  uint16_t pearl = rgb(236, 230, 210);
  for (int k = 1; k < 8; k++) {
    if (k == 4) continue;                                   // side pocket
    int y = lroundf(TBL_T + (TBL_B - TBL_T) * k / 8);
    spr.drawFastHLine(3, y, 3, pearl); spr.drawPixel(4, y - 1, pearl); spr.drawPixel(4, y + 1, pearl);
    spr.drawFastHLine(SW - 6, y, 3, pearl); spr.drawPixel(SW - 5, y - 1, pearl); spr.drawPixel(SW - 5, y + 1, pearl);
  }
  for (int k = 1; k < 4; k++) {
    int x = lroundf(TBL_L + (TBL_R - TBL_L) * k / 4);
    spr.drawFastVLine(x, 20, 3, pearl); spr.drawPixel(x - 1, 21, pearl); spr.drawPixel(x + 1, 21, pearl);
    spr.drawFastVLine(x, 233, 3, pearl); spr.drawPixel(x - 1, 234, pearl); spr.drawPixel(x + 1, 234, pearl);
  }
}

// ---------------- drawing: balls ----------------
static void drawShadow(float fx, float fy) {
  int x0 = lroundf(fx) - GRID / 2, y0 = lroundf(fy) - GRID / 2;
  for (int j = 0; j < SGRID; j++)
    for (int i = 0; i < SGRID; i++) {
      int a = shadowA[j * SGRID + i];
      if (a) blendPx(x0 + i, y0 + j, 0, 20, 8, a);
    }
}

static void drawBall(float fx, float fy, int id, const float *ax, const float *az) {
  int x0 = lroundf(fx) - GRID / 2, y0 = lroundf(fy) - GRID / 2;
  const uint8_t *base = BALL_RGB[id <= 8 ? id : id - 8];
  bool stripe = id > 8;
  for (int k = 0; k < GRID * GRID; k++) {
    const PixInfo &p = pix[k];
    if (!p.cov) continue;
    int x = x0 + k % GRID, y = y0 + k / GRID;
    if (!onScreen(x, y)) continue;
    float bx = ax[0] * p.nx + ax[1] * p.ny + ax[2] * p.nz;
    int r = base[0], g = base[1], b = base[2];
    if (id == 0) {
      if (bx > 0.93f) { r = 205; g = 40; b = 40; }            // the cue ball's spot shows it rolling
    } else {
      if (stripe) {
        float bz = az[0] * p.nx + az[1] * p.ny + az[2] * p.nz;
        if (bz > 0.50f || bz < -0.50f) { r = 246; g = 244; b = 234; }
      }
      if (bx > 0.80f) {                                       // number disc with a dark "digit"
        if (bx > 0.955f) { r = 40; g = 40; b = 40; }
        else { r = 250; g = 250; b = 245; }
      }
    }
    int s = p.shade, sp = p.spec;
    r = min(255, (r * s >> 8) + sp);
    g = min(255, (g * s >> 8) + sp);
    b = min(255, (b * s >> 8) + sp);
    if (p.cov < 250) blendPx(x, y, r, g, b, p.cov);
    else putPx(x, y, rgb(r, g, b));
  }
}

static void drawRing(float fx, float fy, int r, int g, int b, int alpha) {
  int x0 = lroundf(fx) - GRID / 2, y0 = lroundf(fy) - GRID / 2;
  for (int k = 0; k < GRID * GRID; k++)
    if (pix[k].ring) blendPx(x0 + k % GRID, y0 + k / GRID, r, g, b, pix[k].ring * alpha >> 8);
}

static void drawDrops() {
  for (int i = 0; i < nDrops; i++) {
    const Drop &d = drops[i];
    const Pocket &pk = POCKETS[d.pk];
    float f = (float)d.t / DROP_TICKS;
    float x = d.x + (pk.x - d.x) * min(1.0f, f * 1.6f), y = d.y + (pk.y - d.y) * min(1.0f, f * 1.6f);
    int rad = lroundf(BALL_R * (1 - f * 0.8f));
    const uint8_t *c = BALL_RGB[d.id <= 8 ? d.id : d.id - 8];
    float dk = 1 - f * 0.85f;
    spr.fillCircle(lroundf(x), lroundf(y), rad, mix565(c[0], c[1], c[2], dk));
    if (d.id > 8 && rad > 1) spr.fillCircle(lroundf(x), lroundf(y), rad / 2, mix565(240, 240, 230, dk));
  }
}

static void drawAllBalls() {
  for (int i = 0; i < 16; i++) if (balls[i].on) drawShadow(balls[i].x, balls[i].y);
  for (int i = 15; i >= 0; i--) if (balls[i].on) drawBall(balls[i].x, balls[i].y, i, balls[i].ax, balls[i].az);
}

// ---------------- drawing: aim guide, cue, meter ----------------
static void dottedLine(float x0, float y0, float x1, float y1, int r, int g, int b, int a, int phase) {
  float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
  if (len < 1) return;
  dx /= len; dy /= len;
  for (int i = 0; i <= (int)len; i++)
    if (((i + phase) & 3) < 2) blendPx(lroundf(x0 + dx * i), lroundf(y0 + dy * i), r, g, b, a);
}

static void drawGuide() {
  const Ball &c = balls[0];
  float dx = cosf(aim), dy = sinf(aim);
  // first ball on the line: |C + tD - B| = 2R
  float tBall = 1e9f;
  int hit = -1;
  for (int i = 1; i < 16; i++) {
    if (!balls[i].on) continue;
    float mx = c.x - balls[i].x, my = c.y - balls[i].y;
    float bq = mx * dx + my * dy, cq = mx * mx + my * my - 4 * BALL_R * BALL_R;
    float disc = bq * bq - cq;
    if (disc < 0) continue;
    float t = -bq - sqrtf(disc);
    if (t > 0 && t < tBall) { tBall = t; hit = i; }
  }
  // the cushion it would reach otherwise
  float tx = dx > 1e-4f ? (TBL_R - BALL_R - c.x) / dx : dx < -1e-4f ? (TBL_L + BALL_R - c.x) / dx : 1e9f;
  float ty = dy > 1e-4f ? (TBL_B - BALL_R - c.y) / dy : dy < -1e-4f ? (TBL_T + BALL_R - c.y) / dy : 1e9f;
  float tc = min(tx, ty);
  int ph = (animT / 3) & 3;
  if (hit >= 0 && tBall < tc) {
    float gx = c.x + dx * tBall, gy = c.y + dy * tBall;
    dottedLine(c.x + dx * (BALL_R + 2), c.y + dy * (BALL_R + 2), gx - dx * BALL_R, gy - dy * BALL_R, 255, 255, 255, 150, 4 - ph);
    drawRing(gx, gy, 255, 255, 255, 200);                    // ghost ball
    const Ball &o = balls[hit];
    float nx = (o.x - gx) / (2 * BALL_R), ny = (o.y - gy) / (2 * BALL_R), cut = nx * dx + ny * dy;
    float lo = 6 + 30 * cut;
    dottedLine(o.x + nx * (BALL_R + 1), o.y + ny * (BALL_R + 1), o.x + nx * (BALL_R + 1 + lo), o.y + ny * (BALL_R + 1 + lo),
               255, 225, 120, 190, 4 - ph);
    float tgx = dx - nx * cut, tgy = dy - ny * cut;          // where the cue ball goes on
    float tl = sqrtf(tgx * tgx + tgy * tgy);
    if (tl > 0.05f) dottedLine(gx, gy, gx + tgx * 18, gy + tgy * 18, 170, 210, 255, 110, 4 - ph);
  } else {
    float px = c.x + dx * tc, py = c.y + dy * tc;
    dottedLine(c.x + dx * (BALL_R + 2), c.y + dy * (BALL_R + 2), px, py, 255, 255, 255, 150, 4 - ph);
    drawRing(px, py, 255, 255, 255, 170);
    float rx = tx < ty ? -dx : dx, ry = tx < ty ? dy : -dy; // bounce
    float ex = constrain(px + rx * 22, TBL_L + BALL_R, TBL_R - BALL_R), ey = constrain(py + ry * 22, TBL_T + BALL_R, TBL_B - BALL_R);
    dottedLine(px, py, ex, ey, 255, 255, 255, 90, 4 - ph);
  }
}

static void drawCue() {
  const Ball &c = balls[0];
  float dx = cosf(aim), dy = sinf(aim), px = -dy, py = dx;
  float breathe = charging ? 0 : 1.0f + sinf(animT * 0.08f);
  float gap = BALL_R + 2 + breathe + (charging ? power * 24 : 0);
  // sections from the tip backwards: chalk, ferrule, maple shaft, ring, dark butt
  static const float SEG[6] = {0, 2, 5, 58, 61, 100};
  static const uint8_t COL[5][3] = {{60, 110, 220}, {245, 245, 235}, {226, 196, 140}, {30, 20, 16}, {96, 38, 22}};
  for (int s = 0; s < 5; s++) {
    float a = gap + SEG[s], b = gap + SEG[s + 1];
    float wa = 1.1f + SEG[s] * 0.018f, wb = 1.1f + SEG[s + 1] * 0.018f;
    float ax = c.x - dx * a, ay = c.y - dy * a, bx = c.x - dx * b, by = c.y - dy * b;
    fillQuad(ax + px * wa, ay + py * wa, bx + px * wb, by + py * wb, bx - px * wb, by - py * wb, ax - px * wa, ay - py * wa,
             rgb(COL[s][0], COL[s][1], COL[s][2]));
  }
  // highlight along the shaft
  float a = gap + 5, b = gap + 58;
  spr.drawLine(lroundf(c.x - dx * a - px * 0.6f), lroundf(c.y - dy * a - py * 0.6f),
               lroundf(c.x - dx * b - px * 1.0f), lroundf(c.y - dy * b - py * 1.0f), rgb(250, 232, 190));
}

static void drawPowerMeter() {
  const int x = 127, y0 = 44, h = 168;
  spr.fillRect(x - 1, y0 - 1, 7, h + 2, rgb(30, 14, 8));
  int lv = (int)(power * h);
  for (int k = 0; k < lv; k++) {
    float f = (float)k / h;                                  // green -> yellow -> red
    int r = f < 0.5f ? (int)(60 + 390 * f) : 255, g = f < 0.5f ? 220 : (int)(220 - 340 * (f - 0.5f));
    spr.drawFastHLine(x, y0 + h - 1 - k, 5, rgb(r, max(0, g), 40));
  }
  spr.drawFastHLine(x - 1, y0 + h - 1 - lv, 7, TFT_WHITE);
}

// ---------------- drawing: HUD, banner, screens ----------------
static void miniBall(int x, int y, int id) {
  const uint8_t *c = BALL_RGB[id <= 8 ? id : id - 8];
  if (id > 8) {
    spr.fillCircle(x, y, 2, rgb(240, 240, 230));
    spr.fillRect(x - 2, y - 1, 5, 3, rgb(c[0], c[1], c[2]));
  } else {
    spr.fillCircle(x, y, 2, rgb(c[0], c[1], c[2]));
    if (id == 8) spr.drawPixel(x, y, TFT_WHITE);
  }
}

static void drawHud() {
  const Theme &th = theme();
  char buf[24];
  spr.fillRect(0, 0, SW, HUD_H, th.bg);
  if (!twoPlayers) {
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(th.text);
    snprintf(buf, sizeof buf, "Shots %d", shots);
    spr.drawString(buf, 3, 1, 2);
    uint32_t secs = playTicks * TICK_MS / 1000;
    snprintf(buf, sizeof buf, "%lu:%02lu", (unsigned long)(secs / 60), (unsigned long)(secs % 60));
    spr.setTextDatum(TR_DATUM);
    spr.setTextColor(th.neon ? th.accent : TFT_LIGHTGREY);
    spr.drawString(buf, 132, 1, 2);
    return;
  }
  for (int p = 0; p < 2; p++) {
    int x0 = p ? 69 : 2;
    bool act = p == cur && state == PLAY;
    uint16_t col = act ? (p ? TFT_CYAN : TFT_YELLOW) : th.textDim;
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(col);
    spr.drawString(p ? "P2" : "P1", x0 + 1, 1, 2);
    if (act) spr.fillRect(x0, 15, 64, 2, col);
    int g = group[p];
    if (!g) {
      spr.setTextColor(th.textDim);
      spr.drawString("open", x0 + 22, 5, 1);
    } else {
      int n = 0;
      for (int i = 1; i < 16; i++)
        if (balls[i].on && groupOf(i) == g) miniBall(x0 + 25 + 6 * n++, 8, i);
      if (!n) {
        miniBall(x0 + 25, 8, 8);
        spr.setTextColor(th.text);
        spr.drawString("8!", x0 + 31, 5, 1);
      }
    }
  }
}

static void drawBanner() {
  if (banT <= 0 || !banTxt) return;
  int age = BANNER_TICKS - banT;
  // slides in from the left, out to the right
  int off = age < 8 ? (8 - age) * -17 : banT < 8 ? (8 - banT) * 17 : 0;
  int h = banSub ? 44 : 32, y0 = 106;
  for (int y = y0; y < y0 + h; y++)
    for (int x = max(0, off); x < min(SW, SW + off); x++) blendPx(x, y, 0, 0, 0, 170);
  uint16_t gold = rgb(220, 180, 70);
  spr.drawFastHLine(max(0, off), y0, SW - abs(off), gold);
  spr.drawFastHLine(max(0, off), y0 + h - 1, SW - abs(off), gold);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(banCol);
  int font = spr.textWidth(banTxt, 4) <= 128 ? 4 : 2;
  spr.drawString(banTxt, 67 + off, y0 + (font == 4 ? 4 : 9), font);
  if (banSub) {
    spr.setTextColor(TFT_WHITE);
    spr.drawString(banSub, 67 + off, y0 + 32, 1);
  }
}

static void drawHint() {
  const char *t = nullptr;
  if (phase == PH_PLACE) t = "L/R move   L+R place";
  else if (phase == PH_AIM && shots == 0) t = charging ? "release to shoot" : "L/R aim   L+R power";
  if (!t) return;
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(rgb(170, 230, 190));
  spr.drawString(t, 67, PLACE_Y + 11, 1);
}

static void drawDuoOver() {
  const Theme &th = theme();
  char buf[24];
  uint16_t edge = th.neon ? th.frameSel : TFT_YELLOW;
  if (th.neon) {
    spr.fillRoundRect(6, 92, 123, 84, 6, th.panel);
    drawThemedBox(6, 92, 123, 84, 6, edge, true);
  } else {
    spr.fillRect(6, 92, 123, 84, TFT_BLACK);
    spr.drawRect(6, 92, 123, 84, edge);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(winner ? TFT_CYAN : TFT_YELLOW);
  snprintf(buf, sizeof buf, "PLAYER %d", winner + 1);
  spr.drawString(buf, 67, 97, 4);
  spr.setTextColor(th.text);
  spr.drawString("WINS!", 67, 122, 2);
  spr.setTextColor(themeSoft());
  spr.drawString(endWhy, 67, 140, 1);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(th.keyHint);
  spr.drawString("L", 16, 154, 2);
  spr.drawString("R", 68, 154, 2);
  spr.setTextColor(th.neon ? th.accent : TFT_GREEN);
  spr.drawString("menu", 28, 154, 2);
  spr.drawString("again", 80, 154, 2);
}

static void drawGame() {
  drawTable(state == PLAY && phase == PH_PLACE);
  drawAllBalls();
  drawDrops();
  if (state == PLAY && phase == PH_PLACE) {
    bool ok = spotFree(placeX, PLACE_Y, 0);
    int pulse = 120 + (int)(100 * triWave(animT * 16, 800));
    drawRing(placeX, PLACE_Y, ok ? 255 : 255, ok ? 255 : 60, ok ? 255 : 60, pulse);
    uint16_t ac = rgb(200, 240, 210);
    int x = lroundf(placeX), y = lroundf(PLACE_Y);
    spr.fillTriangle(x - 10, y, x - 7, y - 3, x - 7, y + 3, ac);
    spr.fillTriangle(x + 10, y, x + 7, y - 3, x + 7, y + 3, ac);
  }
  if (state == PLAY && phase == PH_AIM && !waitRelease) drawGuide();
  if (state == PLAY && phase == PH_AIM) drawCue();
  if (state == PLAY && charging && phase == PH_AIM) drawPowerMeter();
  if (state == PLAY) drawHint();
  drawHud();
  if (state == PLAY) drawBanner();
  if (state == GAMEOVER) {
    if (twoPlayers) {
      drawDuoOver();
    } else {
      drawGameOverBox("CLEARED!", score, hiScore, newHigh);
      char buf[28];
      uint32_t secs = playTicks * TICK_MS / 1000;
      snprintf(buf, sizeof buf, "%d shots  %lu:%02lu", shots, (unsigned long)(secs / 60), (unsigned long)(secs % 60));
      spr.fillRect(6, 176, 123, 14, TFT_BLACK);
      spr.setTextDatum(TC_DATUM);
      spr.setTextColor(themeSoft());
      spr.drawString(buf, 67, 179, 1);
    }
  }
}

static void drawTitle() {
  const Theme &th = theme();
  char buf[32];
  spr.fillSprite(th.bg);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(th.text);
  spr.drawString("POOL", 67, 6, 4);

  // a little table corner with a rack and the cue ball lined up
  for (int k = 0; k < 4; k++)
    spr.drawRoundRect(10 + k, 36 + k, 115 - 2 * k, 56 - 2 * k, 6 - k, mix565(118, 58, 26, WOOD_F[k * 2] / 255.0f));
  spr.fillRect(14, 40, 107, 48, rgb(28, 120, 62));
  static const int8_t IDS[10] = {1, 9, 2, 10, 8, 3, 4, 11, 5, 12};
  float ax[3] = {0, 0.3f, 0.95f}, az[3] = {1, 0, 0};
  int k = 0;
  for (int col = 0; col < 4; col++)
    for (int j = 0; j <= col; j++, k++) {
      float x = 76 + col * 7.9f, y = 64 + (j - col / 2.0f) * 9.1f;
      drawShadow(x, y);
      ax[0] = 0.25f * ((k * 7) % 5 - 2) / 2; ax[1] = 0.2f;
      float l = sqrtf(ax[0] * ax[0] + ax[1] * ax[1] + 0.9f);
      float an[3] = {ax[0] / l, ax[1] / l, 0.95f / l};
      drawBall(x, y, IDS[k], an, az);
    }
  float cue[3] = {0.2f, -0.2f, 0.96f};
  dottedLine(41, 64, 70, 64, 255, 255, 255, 140, 0);
  drawShadow(36, 64);
  drawBall(36, 64, 0, cue, az);

  // mode choice
  const char *modes[2] = {"1 Player", "2 Players"};
  for (int m = 0; m < 2; m++) {
    int y = 100 + m * 24;
    bool sel = (m == 1) == twoPlayers;
    drawSelBox(10, y, 115, 21, 4, sel);
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(sel ? th.text : th.textDim);
    spr.drawString(modes[m], 67, y + 11, 2);
  }
  spr.setTextColor(themeSoft());
  spr.drawString(twoPlayers ? "8-ball, take turns" : "clear the table", 67, 156, 1);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Hold L/R", "Tap L/R", "Hold L+R", "release"};
  const char *acts[] = {"aim", "fine aim", "power", "shoot"};
  for (int i = 0; i < 4; i++) {
    spr.setTextColor(th.keyHint);
    spr.drawString(keys[i], 14, 168 + i * 10, 1);
    spr.setTextColor(th.text);
    spr.drawString(acts[i], 72, 168 + i * 10, 1);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(themeSoft());
  snprintf(buf, sizeof buf, "Best %lu (1 player)", (unsigned long)hiScore);
  spr.drawString(buf, 67, 210, 1);
  drawThemedFooter("mode", "start", 224);
}

static void render() {
  fb = (spr.getColorDepth() == 16 && spr.getRotation() == 0) ? (uint16_t *)spr.getPointer() : nullptr;
  if (state == TITLE) drawTitle();
  else drawGame();
}

void drawIcon(int x, int y, bool dim, uint32_t t) {
  auto c = [dim](uint16_t col) { return dim ? tft.alphaBlend(80, col, TFT_BLACK) : col; };
  // a small rack (apex down) and the cue ball rolling up into it
  static const int8_t IDS[6] = {1, 8, 2, 3, 6, 5};
  static const int8_t PX[6] = {-6, 0, 6, -3, 3, 0}, PY[6] = {-10, -10, -10, -5, -5, 0};
  for (int i = 0; i < 6; i++) {
    const uint8_t *b = BALL_RGB[IDS[i]];
    int bx = x + PX[i], by = y + PY[i];
    spr.fillCircle(bx, by, 3, c(rgb(b[0], b[1], b[2])));
    spr.drawPixel(bx - 1, by - 1, c(TFT_WHITE));
  }
  int cy = y + 12 - (t ? (int)(4 * triWave(t, 1200)) : 0);
  spr.fillCircle(x, cy, 3, c(rgb(246, 244, 234)));
  spr.drawPixel(x - 1, cy - 1, c(TFT_WHITE));
}

void begin() {
  initTables();
  hiScore = prefs.getUInt("pool_hi", 0);
  twoPlayers = prefs.getUChar("pool_mode", 0) != 0;
  state = TITLE;
  armed = false;
  lastInput = millis();
  dirty = true;
}

static EvQ qL, qR;                 // button events waiting for the next tick

void update(uint32_t now) {
  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) lastInput = now;

  if (state == PLAY) {
    qL.push(eL);
    qR.push(eR);
    if ((int32_t)(now - lastTick) > 200) lastTick = now - TICK_MS;   // no catching up after a stall
    while (state == PLAY && (int32_t)(now - lastTick) >= (int32_t)TICK_MS) {
      lastTick += TICK_MS;
      tick(now, qL.pop(), qR.pop());
      dirty = true;
    }
  } else if (state == TITLE) {
    qL.clear();
    qR.clear();
    // Tap L: one or two players, Tap R: start (on the release of a press made here)
    if (eL == 1 || eR == 1) armed = true;
    if (armed && eL == 2) {
      armed = false;
      twoPlayers = !twoPlayers;
      prefs.putUChar("pool_mode", twoPlayers ? 1 : 0);
      dirty = true;
    } else if (armed && eR == 2) {
      armed = false;
      newGame();
    }
    if (state == TITLE && idleTooLong(now, lastInput)) goToSleep();
  } else {
    qL.clear();
    qR.clear();
    EndChoice ch = startOrEnd(true, eL, eR, armed);
    if (ch == END_PLAY) newGame();
    if (ch == END_MENU) return;
    if (idleTooLong(now, lastInput)) goToSleep();
  }

  if (dirty) {
    uint32_t t0 = micros();
    render();
    uint32_t t1 = micros();
    spr.pushSprite(0, 0);
    dirty = false;
    // frame timing in the USB log every ~5 s
    drawUs += t1 - t0;
    frameUs += micros() - t0;
    if (++nFrames >= 300) {
      uint32_t span = millis() - lastFrameAt;
      dbg("pool: draw %lu us, draw+push %lu us, %lu fps", (unsigned long)(drawUs / nFrames),
          (unsigned long)(frameUs / nFrames), (unsigned long)(span ? nFrames * 1000UL / span : 0));
      drawUs = frameUs = nFrames = 0;
      lastFrameAt = millis();
    }
  }
}

}  // namespace pool

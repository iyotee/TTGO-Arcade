// Crypt (Sunken Crypt)
//
// An original first-person dungeon shooter in the classic raycasting style.
// Tigo the hermit crab dives through a haunted sunken crypt with a magic coral
// wand: ten depths, keys and locked doors, drowned sailors, ghost jellies,
// lantern lurkers and the Warden at the bottom. Textured walls, floor and
// ceiling, fog, torch light, billboard sprites, doors that slide into the roof.
//
// Controls (two buttons):
//   Hold left / right : turn
//   Hold both         : swim forward (let go of one to curve while gliding)
//   Tap L or R        : cast a shard from the wand (aim assist on nearby foes)
//   Tap both together : minimap on / off
//   Doors open when you swim into them; gold / blue doors need their key.
//   Walk into the glowing arch to leave the depth.
// Title: Tap L = orientation (Portrait / Landscape > / < Landscape), Tap R = start.
//   Portrait: left = L, right = R.
//   Landscape: the buttons steer like the rim of a wheel. Buttons on the right
//   of the picture: top = turn left, bottom = turn right; buttons on the left:
//   top = turn right, bottom = turn left (either way GPIO35 turns left).
// Game over / victory: L = menu, R = play again.
//
// Score: foes + treasure + a bonus per depth (faster = more); best in crypt_hi.
//
// Drawing goes straight into the sprite's 16-bit buffer through a logical ->
// physical mapping (ox + x * ax + y * ay), so one code path serves portrait and
// both landscapes; a 3D column is then a contiguous run in memory in landscape.
// Text is drawn into a small 1-bit sprite and copied through the same mapping.

#include "common.h"
#include "orient.h"
#include "crypt_art.h"
#include "crypt_levels.h"

namespace cryptgame {

// ---------------- constants ----------------
static const uint32_t CR_TICK_MS = 16;
static const int CR_PW = 135, CR_PH = 240;            // the sprite, physically
static const int CR_MS = 32;                          // map stride (maps up to 32 x 32)
static const int CR_SH = 16;                          // fog / shade levels
static const int CR_MAX_FOES = 32, CR_MAX_THINGS = 80, CR_MAX_SHOTS = 24, CR_MAX_PARTS = 32;
static const int CR_MAX_BILLS = 110, CR_MAX_MOVING = 8;
static const uint32_t CR_TURN_DELAY = 110, CR_TAP_MS = 220, CR_CHORD_MS = 150, CR_WALK_DELAY = 70;
static const float CR_TURN_MAX = 0.046f, CR_ACC = 0.0058f, CR_PLAYER_R = 0.3f;
static const int CR_PORT_VH = 168, CR_LAND_VH = 112;  // 3D view height above the HUD
static const int CR_START_HP = 100, CR_START_MANA = 40, CR_LIVES = 3;

enum : uint8_t { CW_FLOOR, CW_STONE, CW_MOSS, CW_CORAL, CW_RUNE, CW_DOOR, CW_GDOOR, CW_BDOOR, CW_EXIT };
enum : uint8_t { FL_PROP = 1, FL_SEEN = 2 };

enum ThingKind : uint8_t {
  TK_HEALTH, TK_MANA, TK_GKEY, TK_BKEY, TK_GOLD, TK_WAND,    // pickups
  TK_TORCH, TK_PILLAR, TK_KELP, TK_BARREL, TK_BONES           // props
};
enum FoeType : uint8_t { FT_DROWNER, FT_JELLY, FT_LURKER, FT_WARDEN };
enum FoeState : uint8_t { FS_IDLE, FS_CHASE, FS_DYING };
enum PartKind : uint8_t { PK_BUBBLE, PK_SPARK, PK_INK, PK_GUTS };
enum BillMode : uint8_t { BM_NORMAL, BM_FLASH, BM_FADE, BM_DOT, BM_BOSS, BM_BRIGHT };

struct FoeDef {
  int16_t hp;
  float speed, width, height;
  uint8_t sprite;
  int16_t points;
  float sight;
  int8_t bite, ink, inkShots;     // damage per bite / per ink blob, blobs per spit
};
static const FoeDef CR_FOE[] = {
  {30, 0.024f, 0.80f, 0.92f, SP_DROWN0, 150, 9.0f, 8, 0, 0},     // drowner: melee
  {22, 0.017f, 0.72f, 0.72f, SP_JELLY0, 200, 10.0f, 0, 5, 1},    // gloomjelly: floats, spits ink
  {80, 0.015f, 0.95f, 0.95f, SP_LURK0, 400, 10.0f, 12, 6, 2},    // lurker: ink and bites
  {600, 0.013f, 1.70f, 1.70f, SP_LURK0, 3000, 14.0f, 16, 8, 3},  // the Warden (a red lurker, much bigger)
};

struct WandDef {
  const char *name;
  uint8_t cost, cd, dmg, bolts;
  float spread;
};
static const WandDef CR_WAND[] = {
  {"Coral wand", 1, 20, 12, 1, 0.0f},
  {"Twin shards", 1, 17, 12, 2, 0.05f},
  {"Pearl storm", 2, 15, 15, 3, 0.07f},
};
static const int CR_WAND_MAX = 3;

struct Foe {
  float x, y, z, heading;
  int16_t hp;
  uint8_t type, state, hitT, windup;
  uint16_t t, cd, anim;
  bool alive, sees;
};
struct Thing {
  float x, y;
  uint8_t kind;
  bool on;
};
struct Shot {
  float x, y, dx, dy;
  uint8_t dmg, life;
  bool enemy, on;
};
struct Part {
  float x, y, z, vz;
  uint8_t kind, t, life;
  bool on;
};
struct Bill {
  float depth, tx, z, wW, hW;
  uint8_t img, mode, extra, shade;
  bool warm;
};

// big arrays live on the heap while the game is open
struct CrWorld {
  uint8_t cell[CR_MS * CR_MS];
  uint8_t light[CR_MS * CR_MS];         // torch light, in shade steps
  uint8_t flag[CR_MS * CR_MS];
  uint8_t dopen[CR_MS * CR_MS];         // doors: 0 shut .. 255 open
  float zbuf[240];
  int32_t rowDist[241];                 // floor distance (Q8 cells) of view row k below the horizon
  uint8_t rowShade[241];
  uint16_t shade[CR_SH * CI_COUNT];     // byte-swapped RGB565 per fog level
  uint16_t warm[CR_SH * CI_COUNT];      // the same in torch light
  uint16_t flash[CI_COUNT];             // hit flash
  Bill bills[CR_MAX_BILLS];
};

enum State { ST_TITLE, ST_INTRO, ST_PLAY, ST_CLEAR, ST_LOST, ST_OVER, ST_WIN, ST_NOMEM };

// ---------------- state ----------------
static CrWorld *W = nullptr;
static uint8_t *texMem = nullptr, *sprMem = nullptr;
static uint16_t spOff[SP_COUNT];
static TFT_eSprite *txt = nullptr;              // 1-bit scratch for text
static uint16_t *fb;                             // the sprite's buffer

static State state = ST_TITLE;
static int ori = OR_PORTRAIT;
static int LW = 135, LH = 240, VW = 135, VH = 168, tablesVH = -1;
static int ox = 0, ax = 1, ay = CR_PW;
static float planeLen = 0.5f, focal = 135, fogK = 1.6f;
static uint8_t fogR, fogG, fogB;

// camera for the frame being drawn
static float camX, camY, dirX, dirY, plX, plY, invDet;
static int nBills;
static uint8_t lightLut[16];
static int glowCyc = 0, fireFlick = 256;

// level
static int lvl = 0;
static Foe foes[CR_MAX_FOES];
static Thing things[CR_MAX_THINGS];
static Shot shots[CR_MAX_SHOTS];
static Part parts[CR_MAX_PARTS];
static uint16_t moving[CR_MAX_MOVING];          // doors on their way up
static int nFoes, nThings, nMoving;
static int kills, killsTotal, gold, goldTotal;
static uint32_t lvlTicks;
static int curFloor = TX_FLOOR;

// player
static float px, py, pa, pvx, pvy, turnV, bob;
static int hp, mana, wand, keys, lives;
static int hurtT, pickT, fireCd, fireAnim, pendingFire, regenT, msgT, shakeT, bumpT;
static char msg[34];
static bool mapOn = false, hudDirty = true;
static TigoPose lastPose = TIGO_IDLE;
static uint32_t faceAt = 0;

// input
static bool chord = false, chordPure = false, soloL = false, soloR = false, armed = false;
static uint32_t chordT = 0;

// run
static uint32_t score = 0, hiScore = 0, levelBonus = 0;
static bool newHigh = false;
static uint32_t lastTick = 0, lastInput = 0, tickNow = 0, stateAt = 0, frameNo = 0;
static float demoA = 0;
static uint32_t statFrames = 0, statDraw = 0, statPush = 0, statAt = 0;

static int rnd(int n) { return esp_random() % n; }
static float frnd(float a, float b) { return a + (b - a) * (esp_random() % 10001) / 10000.0f; }
static inline uint16_t sw(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }
static inline uint16_t rgb(int r, int g, int b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
static float angDiff(float a, float b) {
  float d = a - b;
  while (d > PI) d -= 2 * PI;
  while (d < -PI) d += 2 * PI;
  return d;
}
static Btn &turnLeftBtn() { return ori == OR_PORTRAIT ? bL : bR; }   // see the header comment
static Btn &turnRightBtn() { return ori == OR_PORTRAIT ? bR : bL; }

// ---------------- memory ----------------
static void freeMem() {
  if (txt) { txt->deleteSprite(); delete txt; txt = nullptr; }
  free(W); W = nullptr;
  free(texMem); texMem = nullptr;
  free(sprMem); sprMem = nullptr;
}

static bool allocMem() {
  if (!W) W = (CrWorld *)malloc(sizeof(CrWorld));
  if (!texMem) texMem = (uint8_t *)malloc(TX_COUNT * CR_TEX_BYTES);
  if (!sprMem) sprMem = (uint8_t *)malloc(crSpriteBytes(nullptr));
  if (!txt) {
    txt = new TFT_eSprite(&tft);
    txt->setColorDepth(1);
    txt->createSprite(240, 26);
  }
  if (!W || !texMem || !sprMem || !txt->created() || spr.getColorDepth() != 16) {
    dbg("crypt: out of memory (heap %u)\n", ESP.getFreeHeap());
    freeMem();
    return false;
  }
  uint32_t t0 = millis();
  crBuildPalette();
  crGenTextures(texMem);
  crSpriteBytes(spOff);
  crGenSprites(sprMem, spOff);
  dbg("crypt: art built in %lu ms, %u bytes, heap %u\n", (unsigned long)(millis() - t0),
      (unsigned)(sizeof(CrWorld) + TX_COUNT * CR_TEX_BYTES + crSpriteBytes(nullptr)), ESP.getFreeHeap());
  return true;
}

// ---------------- drawing in logical coordinates ----------------
static inline uint16_t *colPtr(int x) { return fb + ox + x * ax; }
static inline void putL(int x, int y, uint16_t swc) {
  if ((unsigned)x < (unsigned)LW && (unsigned)y < (unsigned)LH) fb[ox + x * ax + y * ay] = swc;
}
static void rectL(int x, int y, int w, int h, uint16_t col) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > LW) w = LW - x;
  if (y + h > LH) h = LH - y;
  if (w <= 0 || h <= 0) return;
  uint16_t c = sw(col);
  for (int i = 0; i < w; i++) {
    uint16_t *p = colPtr(x + i) + y * ay;
    for (int j = 0; j < h; j++, p += ay) *p = c;
  }
}
// halves the brightness of what is already there (card panels)
static void dimRectL(int x, int y, int w, int h, int shift) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > LW) w = LW - x;
  if (y + h > LH) h = LH - y;
  if (w <= 0 || h <= 0) return;
  uint16_t m = shift == 1 ? 0x7BEF : 0x39E7;
  for (int i = 0; i < w; i++) {
    uint16_t *p = colPtr(x + i) + y * ay;
    for (int j = 0; j < h; j++, p += ay) *p = sw((sw(*p) >> shift) & m);
  }
}
static void frameL(int x, int y, int w, int h, uint16_t col) {
  rectL(x, y, w, 1, col);
  rectL(x, y + h - 1, w, 1, col);
  rectL(x, y, 1, h, col);
  rectL(x + w - 1, y, 1, h, col);
}
static void discL(int cx, int cy, int r, uint16_t col) {
  for (int dy = -r; dy <= r; dy++) {
    int dx = 0;
    while ((dx + 1) * (dx + 1) + dy * dy <= r * r + r) dx++;
    rectL(cx - dx, cy + dy, 2 * dx + 1, 1, col);
  }
}
// a small filled triangle pointing right (dir 1) or left (dir -1)
static void arrowL(int x, int y, int s, int dir, uint16_t col) {
  for (int i = 0; i < s; i++) rectL(dir > 0 ? x + i : x - i, y - (s - 1 - i), 1, 2 * (s - 1 - i) + 1, col);
}

static int textW(const char *s, int font) { return txt ? txt->textWidth(s, font) : 0; }
// align 0 = left, 1 = centre, 2 = right; y is the top of the text
static void textL(const char *s, int x, int y, int font, uint16_t col, int align = 0, bool shadow = false) {
  if (!txt || !*s) return;
  int w = txt->textWidth(s, font), h = txt->fontHeight(font);
  if (w > 240) w = 240;
  if (h > 26) h = 26;
  if (align == 1) x -= w / 2;
  else if (align == 2) x -= w;
  txt->fillSprite(0);
  txt->setTextColor(1);
  txt->setTextDatum(TL_DATUM);
  txt->drawString(s, 0, 0, font);
  const uint8_t *bits = (const uint8_t *)txt->getPointer();
  uint16_t c = sw(col), sc = 0;
  for (int yy = 0; yy < h; yy++) {
    const uint8_t *row = bits + yy * 30;           // 240 px / 8
    for (int xx = 0; xx < w; xx++)
      if (row[xx >> 3] & (0x80 >> (xx & 7))) {
        if (shadow) putL(x + xx + 1, y + yy + 1, sc);
        putL(x + xx, y + yy, c);
      }
  }
}
// font 4 if it fits in maxW, else font 2, else font 1
static int fitFont(const char *s, int maxW) {
  if (textW(s, 4) <= maxW) return 4;
  return textW(s, 2) <= maxW ? 2 : 1;
}

// an unscaled sprite image (HUD icons) in its plain colours; dim = darker
static void blitImg(int img, int x, int y, bool dim) {
  const uint8_t *p = sprMem + spOff[img];
  int w = SP_SIZE[img][0], h = SP_SIZE[img][1];
  for (int u = 0; u < w; u++)
    for (int v = 0; v < h; v++) {
      uint8_t c = p[u * h + v];
      if (!c) continue;
      int k = dim ? 3 : 1;
      putL(x + u, y + v, sw(rgb(crPal[c][0] / k, crPal[c][1] / k, crPal[c][2] / k)));
    }
}

// Tigo in a square box: drawn upright in physical coordinates (clipped to the
// box), then the box is turned in place to match the picture's orientation
static void tigoBox(int bx, int by, int S, TigoPose pose, uint32_t t, uint16_t bg) {
  rectL(bx, by, S, S, bg);
  int pbx = bx, pby = by;
  if (ori == OR_LAND_RIGHT) { pbx = CR_PW - by - S; pby = bx; }
  else if (ori == OR_LAND_LEFT) { pbx = by; pby = CR_PH - bx - S; }
  if (pbx < 0 || pby < 0 || pbx + S > CR_PW || pby + S > CR_PH) return;
  spr.setViewport(pbx, pby, S, S, false);
  drawTigo(pbx + S / 2, pby + S - 3, S - 5, pose, t, false);
  spr.resetViewport();
  if (ori == OR_PORTRAIT) return;
  uint16_t *b = fb + pby * CR_PW + pbx;
  for (int i = 0; i < S; i++)                      // transpose
    for (int j = i + 1; j < S; j++) {
      uint16_t a = b[j * CR_PW + i];
      b[j * CR_PW + i] = b[i * CR_PW + j];
      b[i * CR_PW + j] = a;
    }
  if (ori == OR_LAND_RIGHT) {                      // then mirror x
    for (int j = 0; j < S; j++)
      for (int i = 0; i < S / 2; i++) {
        uint16_t a = b[j * CR_PW + i];
        b[j * CR_PW + i] = b[j * CR_PW + S - 1 - i];
        b[j * CR_PW + S - 1 - i] = a;
      }
  } else {                                         // or mirror y
    for (int j = 0; j < S / 2; j++)
      for (int i = 0; i < S; i++) {
        uint16_t a = b[j * CR_PW + i];
        b[j * CR_PW + i] = b[(S - 1 - j) * CR_PW + i];
        b[(S - 1 - j) * CR_PW + i] = a;
      }
  }
}

// a button label next to the physical button. Portrait: L bottom left, R bottom
// right. Landscape: both on the button edge, top / bottom (see orient.h).
static void btnHint(bool isR, const char *label, uint16_t col) {
  char buf[24];
  if (ori == OR_PORTRAIT) {
    snprintf(buf, sizeof buf, isR ? "%s R" : "L %s", label);
    textL(buf, isR ? LW - 3 : 3, LH - 17, 2, col, isR ? 2 : 0, true);
    return;
  }
  bool top = (ori == OR_LAND_RIGHT) == isR;        // LAND_RIGHT: R on top; LAND_LEFT: L on top
  bool right = ori == OR_LAND_RIGHT;
  snprintf(buf, sizeof buf, right ? "%s >" : "< %s", label);
  textL(buf, right ? LW - 3 : 3, top ? 3 : LH - 17, 2, col, right ? 2 : 0, true);
}
// "the buttons are this side" marker for the landscape title
static void edgeMarker(uint16_t col) {
  if (ori == OR_PORTRAIT) return;
  bool right = ori == OR_LAND_RIGHT;
  int x = right ? LW - 4 : 3, y = LH / 2;
  arrowL(right ? x - 6 : x + 6, y, 7, right ? 1 : -1, col);
  textL("buttons", right ? x - 10 : x + 10, y - 4, 1, col, right ? 2 : 0, true);
}

// ---------------- palette / fog ----------------
// Rebuilt every frame (1024 entries, cheap): glow colours cycle, fire flickers,
// and the whole picture can be dimmed (cards) or tinted (hurt, pickups).
static void buildShades(int bright, int tr, int tg, int tb, int ta) {
  uint8_t base[CI_COUNT][3];
  memcpy(base, crPal, sizeof base);
  for (int k = 0; k < 4; k++) memcpy(base[CI_GLOW + k], crPal[CI_GLOW + ((k + glowCyc) & 3)], 3);
  for (int k = 0; k < 3; k++)
    for (int c = 0; c < 3; c++) base[CI_FIRE + k][c] = min(255, crPal[CI_FIRE + k][c] * fireFlick >> 8);
  for (int s = 0; s < CR_SH; s++) {
    uint16_t *row = W->shade + s * CI_COUNT;
    for (int i = 0; i < CI_COUNT; i++) {
      int ss = crEmissive(i) ? s / 3 : s;
      int f = ss * 17 + (ss == CR_SH - 1);          // 0 .. 256 towards the fog colour
      int r = (base[i][0] * (256 - f) + fogR * f) >> 8;
      int g = (base[i][1] * (256 - f) + fogG * f) >> 8;
      int b = (base[i][2] * (256 - f) + fogB * f) >> 8;
      if (bright != 256) { r = min(255, r * bright >> 8); g = min(255, g * bright >> 8); b = min(255, b * bright >> 8); }
      if (ta) { r += (tr - r) * ta >> 8; g += (tg - g) * ta >> 8; b += (tb - b) * ta >> 8; }
      row[i] = sw(rgb(r, g, b));
      // torch-lit version: warmer and a little brighter
      int wr = min(255, r * 5 / 4 + 18), wg = min(255, g + 6), wb = b * 3 / 4;
      W->warm[s * CI_COUNT + i] = crEmissive(i) ? row[i] : sw(rgb(wr, wg, wb));
    }
  }
  for (int i = 0; i < CI_COUNT; i++)
    W->flash[i] = sw(rgb((base[i][0] + 510) / 3, (base[i][1] + 300) / 3, (base[i][2] + 300) / 3));
}

// ---------------- layout ----------------
static void buildRows() {
  for (int k = 0; k <= 240; k++) {
    float d = 0.5f * focal / (k + 0.5f);
    if (d > 120) d = 120;
    W->rowDist[k] = (int32_t)(d * 256);
    int s = (int)(d * fogK);
    W->rowShade[k] = s > CR_SH - 1 ? CR_SH - 1 : s;
  }
  tablesVH = VH;
}

// full = the 3D view fills the screen (title, cards), else it leaves room for the HUD
static void setLayout(bool full) {
  int oldVH = VH;
  float oldF = focal;
  if (ori == OR_PORTRAIT) {
    LW = 135; LH = 240; ox = 0; ax = 1; ay = CR_PW;
    planeLen = 0.50f; VW = 135; VH = full ? 240 : CR_PORT_VH;
  } else {
    LW = 240; LH = 135;
    if (ori == OR_LAND_RIGHT) { ox = CR_PW - 1; ax = CR_PW; ay = -1; }
    else { ox = (CR_PH - 1) * CR_PW; ax = -CR_PW; ay = 1; }
    planeLen = 0.66f; VW = 240; VH = full ? 135 : CR_LAND_VH;
  }
  focal = VW * 0.5f / planeLen;
  if (tablesVH < 0 || oldVH != VH || oldF != focal) buildRows();
}

// ---------------- world ----------------
static inline uint8_t cellAt(int x, int y) {
  return ((unsigned)x < CR_MS && (unsigned)y < CR_MS) ? W->cell[y * CR_MS + x] : (uint8_t)CW_STONE;
}
static inline bool isDoor(uint8_t c) { return c >= CW_DOOR && c <= CW_BDOOR; }
// walls, shut doors and pillars / barrels stop bodies
static bool solidAt(int x, int y) {
  if ((unsigned)x >= CR_MS || (unsigned)y >= CR_MS) return true;
  int i = y * CR_MS + x;
  uint8_t c = W->cell[i];
  if (c && !(isDoor(c) && W->dopen[i] > 230)) return true;
  return W->flag[i] & FL_PROP;
}
// walls and doors that are not fully open stop sight and shots
static bool opaqueAt(int x, int y) {
  if ((unsigned)x >= CR_MS || (unsigned)y >= CR_MS) return true;
  int i = y * CR_MS + x;
  uint8_t c = W->cell[i];
  return c && !(isDoor(c) && W->dopen[i] > 200);
}
static bool blockedAt(float x, float y, float r) {
  return solidAt((int)(x - r), (int)(y - r)) || solidAt((int)(x + r), (int)(y - r)) ||
         solidAt((int)(x - r), (int)(y + r)) || solidAt((int)(x + r), (int)(y + r));
}
// moves with wall sliding; false if both axes were blocked
static bool moveBody(float &x, float &y, float dx, float dy, float r) {
  bool moved = false;
  if (dx != 0 && !blockedAt(x + dx, y, r)) { x += dx; moved = true; }
  if (dy != 0 && !blockedAt(x, y + dy, r)) { y += dy; moved = true; }
  return moved;
}
static bool los(float x0, float y0, float x1, float y1) {
  float dx = x1 - x0, dy = y1 - y0;
  int n = (int)(sqrtf(dx * dx + dy * dy) * 4) + 1;
  for (int i = 1; i < n; i++) {
    float t = (float)i / n;
    if (opaqueAt((int)(x0 + dx * t), (int)(y0 + dy * t))) return false;
  }
  return true;
}

// braziers light the cells around them (only cells they can see)
static void computeLights() {
  memset(W->light, 0, sizeof W->light);
  for (int i = 0; i < nThings; i++) {
    if (things[i].kind != TK_TORCH) continue;
    float tx = things[i].x, ty = things[i].y;
    for (int y = (int)ty - 5; y <= (int)ty + 5; y++)
      for (int x = (int)tx - 5; x <= (int)tx + 5; x++) {
        if ((unsigned)x >= CR_MS || (unsigned)y >= CR_MS || W->cell[y * CR_MS + x]) continue;
        float dx = x + 0.5f - tx, dy = y + 0.5f - ty, d = sqrtf(dx * dx + dy * dy);
        if (d > 5.0f || !los(tx, ty, x + 0.5f, y + 0.5f)) continue;
        int l = (int)((5.0f - d) * 1.7f);
        uint8_t &L = W->light[y * CR_MS + x];
        if (l > L) L = l;
      }
  }
  // the exit arch glows a little too
  for (int y = 0; y < CR_MS; y++)
    for (int x = 0; x < CR_MS; x++) {
      if (W->cell[y * CR_MS + x] != CW_EXIT) continue;
      for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) {
          int cx = x + dx, cy = y + dy;
          if ((unsigned)cx >= CR_MS || (unsigned)cy >= CR_MS || W->cell[cy * CR_MS + cx]) continue;
          int l = 5 - abs(dx) - abs(dy);
          uint8_t &L = W->light[cy * CR_MS + cx];
          if (l > L) L = l;
        }
    }
}

static void addThing(float x, float y, uint8_t kind) {
  if (nThings >= CR_MAX_THINGS) return;
  Thing &t = things[nThings++];
  t.x = x; t.y = y; t.kind = kind; t.on = true;
  if (kind == TK_PILLAR || kind == TK_BARREL) W->flag[(int)y * CR_MS + (int)x] |= FL_PROP;
}
static void addFoe(float x, float y, uint8_t type) {
  if (nFoes >= CR_MAX_FOES) return;
  Foe &f = foes[nFoes++];
  memset(&f, 0, sizeof f);
  f.x = x; f.y = y; f.type = type; f.hp = CR_FOE[type].hp;
  f.heading = rnd(628) / 100.0f;
  f.t = rnd(60);
  f.alive = true;
}

static void loadLevel(int n) {
  const CrLevel &L = CR_LEVELS[n];
  lvl = n;
  memset(W->cell, CW_STONE, sizeof W->cell);
  memset(W->flag, 0, sizeof W->flag);
  memset(W->dopen, 0, sizeof W->dopen);
  nFoes = nThings = nMoving = 0;
  memset(shots, 0, sizeof shots);
  memset(parts, 0, sizeof parts);
  px = py = 1.5f;
  for (int y = 0; y < L.h; y++)
    for (int x = 0; x < L.w; x++) {
      char c = (char)pgm_read_byte(L.map + y * L.w + x);
      float fx = x + 0.5f, fy = y + 0.5f;
      uint8_t cell = CW_FLOOR;
      switch (c) {
        case '#': cell = CW_STONE; break;
        case 'M': cell = CW_MOSS; break;
        case 'C': cell = CW_CORAL; break;
        case 'R': cell = CW_RUNE; break;
        case 'D': cell = CW_DOOR; break;
        case 'G': cell = CW_GDOOR; break;
        case 'B': cell = CW_BDOOR; break;
        case 'X': cell = CW_EXIT; break;
        case '@': px = fx; py = fy; break;
        case 'z': addFoe(fx, fy, FT_DROWNER); break;
        case 'j': addFoe(fx, fy, FT_JELLY); break;
        case 'a': addFoe(fx, fy, FT_LURKER); break;
        case 'W': addFoe(fx, fy, FT_WARDEN); break;
        case 'h': addThing(fx, fy, TK_HEALTH); break;
        case 'm': addThing(fx, fy, TK_MANA); break;
        case 'g': addThing(fx, fy, TK_GKEY); break;
        case 'b': addThing(fx, fy, TK_BKEY); break;
        case '$': addThing(fx, fy, TK_GOLD); break;
        case 'u': addThing(fx, fy, TK_WAND); break;
        case 't': addThing(fx, fy, TK_TORCH); break;
        case 'p': addThing(fx, fy, TK_PILLAR); break;
        case 'o': addThing(fx, fy, TK_BARREL); break;
        case 'k': addThing(fx, fy, TK_KELP); break;
        case 'x': addThing(fx, fy, TK_BONES); break;
        default: break;
      }
      W->cell[y * CR_MS + x] = cell;
    }
  // face the first open side (east, south, west, north)
  static const int8_t DX[4] = {1, 0, -1, 0}, DY[4] = {0, 1, 0, -1};
  pa = 0;
  for (int d = 0; d < 4; d++)
    if (!solidAt((int)px + DX[d], (int)py + DY[d])) { pa = d * PI / 2; break; }
  W->flag[(int)py * CR_MS + (int)px] |= FL_SEEN;
  fogR = L.fogR; fogG = L.fogG; fogB = L.fogB;
  fogK = L.fog10 / 10.0f;
  curFloor = L.floorTex;
  buildRows();
  computeLights();
  killsTotal = nFoes;
  goldTotal = 0;
  for (int i = 0; i < nThings; i++) goldTotal += things[i].kind == TK_GOLD;
  kills = gold = 0;
  keys = 0;
  lvlTicks = 0;
  pvx = pvy = turnV = 0;
  msgT = 0;
  demoA = pa;
}

static void startDoor(int i) {
  if (W->dopen[i] || nMoving >= CR_MAX_MOVING) return;
  W->dopen[i] = 1;
  moving[nMoving++] = i;
}
static void doorsTick() {
  for (int k = 0; k < nMoving;) {
    uint8_t &o = W->dopen[moving[k]];
    o = o > 255 - 9 ? 255 : o + 9;                 // ~0.45 s to open
    if (o == 255) moving[k] = moving[--nMoving];
    else k++;
  }
}

// ---------------- raycaster ----------------
static void camSetup(float x, float y, float a) {
  camX = x; camY = y;
  dirX = cosf(a); dirY = sinf(a);
  plX = -dirY * planeLen; plY = dirX * planeLen;
  invDet = 1.0f / (plX * dirY - dirX * plY);
}

static inline int shadeFor(float dist, int cellIdx, int extra) {
  int s = (int)(dist * fogK) + extra - lightLut[W->light[cellIdx]];
  return s < 0 ? 0 : s > CR_SH - 1 ? CR_SH - 1 : s;
}

// One DDA ray per column: textured wall slice, then floor and ceiling below /
// above it (floor casting with a per-row distance table, Q16 fixed point), then
// a part-open door on the way gets drawn over the far wall.
static void castView(int hor) {
  const int32_t pxQ = (int32_t)(camX * 65536.0f), pyQ = (int32_t)(camY * 65536.0f);
  const int mx0 = (int)camX, my0 = (int)camY;
  const uint8_t *floorT = texMem + curFloor * CR_TEX_BYTES, *ceilT = texMem + TX_CEIL * CR_TEX_BYTES;
  const int kMax = max(VH - hor, hor);
  const uint16_t *shade = W->shade, *warm = W->warm;
  const uint8_t *cell = W->cell, *light = W->light;
  for (int x = 0; x < VW; x++) {
    float cam = (2 * x + 1) / (float)VW - 1.0f;
    float rx = dirX + plX * cam, ry = dirY + plY * cam;
    float ddx = rx == 0 ? 1e30f : fabsf(1.0f / rx), ddy = ry == 0 ? 1e30f : fabsf(1.0f / ry);
    int mx = mx0, my = my0, sx, sy;
    float sdx, sdy;
    if (rx < 0) { sx = -1; sdx = (camX - mx) * ddx; } else { sx = 1; sdx = (mx + 1.0f - camX) * ddx; }
    if (ry < 0) { sy = -1; sdy = (camY - my) * ddy; } else { sy = 1; sdy = (my + 1.0f - camY) * ddy; }
    int side = 0, prev = my * CR_MS + mx;
    uint8_t hit = CW_STONE;
    int dCell = -1, dPrev = 0, dSide = 0;
    float dPerp = 0;
    for (int step = 0; step < 64; step++) {
      int pc = my * CR_MS + mx;
      if (sdx < sdy) { sdx += ddx; mx += sx; side = 0; }
      else { sdy += ddy; my += sy; side = 1; }
      if ((unsigned)mx >= CR_MS || (unsigned)my >= CR_MS) { prev = pc; break; }
      int ci = my * CR_MS + mx;
      uint8_t c = cell[ci];
      if (!c) { W->flag[ci] |= FL_SEEN; continue; }
      if (isDoor(c) && W->dopen[ci]) {
        if (W->dopen[ci] < 250 && dCell < 0) {     // part open: remember it, look past it
          dCell = ci; dPrev = pc; dSide = side;
          dPerp = side ? sdy - ddy : sdx - ddx;
        }
        W->flag[ci] |= FL_SEEN;
        continue;
      }
      hit = c; prev = pc;
      W->flag[ci] |= FL_SEEN;
      break;
    }
    float perp = side ? sdy - ddy : sdx - ddx;
    if (perp < 0.05f) perp = 0.05f;
    float wx = side ? camX + perp * rx : camY + perp * ry;
    wx -= floorf(wx);
    int u = (int)(wx * CR_TEX) & 31;
    if ((side == 0 && rx > 0) || (side == 1 && ry < 0)) u = 31 - u;
    int lineH = (int)(focal / perp);
    if (lineH > 8000) lineH = 8000;
    int half = lineH >> 1;
    uint16_t *base = colPtr(x);

    // wall slice
    {
      const uint8_t *tc = texMem + (hit - 1) * CR_TEX_BYTES + u * CR_TEX;
      const uint16_t *pal = (light[prev] ? warm : shade) + shadeFor(perp, prev, side) * CI_COUNT;
      int y0 = hor - half, ys = max(0, y0), ye = min(VH, hor + half);
      int32_t stepT = (CR_TEX << 16) / max(lineH, 1), tp = (ys - y0) * stepT;
      uint16_t *p = base + ys * ay;
      for (int y = ys; y < ye; y++, p += ay, tp += stepT) *p = pal[tc[(tp >> 16) & 31]];
    }

    // floor and ceiling
    {
      const int32_t rdx = (int32_t)(rx * 4096), rdy = (int32_t)(ry * 4096);
      int k = max(half, 0);
      uint16_t *pf = base + (hor + k) * ay, *pcl = base + (hor - 1 - k) * ay;
      for (; k < kMax; k++, pf += ay, pcl -= ay) {
        int32_t d = W->rowDist[k];
        int32_t fx = pxQ + ((rdx * d) >> 4), fy = pyQ + ((rdy * d) >> 4);
        int l = light[((fy >> 16) & 31) * CR_MS + ((fx >> 16) & 31)];
        int s = W->rowShade[k] - lightLut[l];
        if (s < 0) s = 0;
        const uint16_t *tab = l ? warm : shade;
        int ti = (((fx >> 11) & 31) << 5) | ((fy >> 11) & 31);
        if (hor + k < VH) *pf = tab[s * CI_COUNT + floorT[ti]];
        if (hor - 1 - k >= 0) *pcl = tab[min(s + 2, CR_SH - 1) * CI_COUNT + ceilT[ti]];
      }
    }
    float zb = perp;

    // a door sliding up into the roof: its lower part still hangs in view
    if (dCell >= 0) {
      float dp = max(dPerp, 0.05f);
      int o = W->dopen[dCell];
      int lh = (int)(focal / dp);
      if (lh > 8000) lh = 8000;
      int top = hor - (lh >> 1), vis = lh * (255 - o) / 255;
      float dwx = dSide ? camX + dp * rx : camY + dp * ry;
      dwx -= floorf(dwx);
      int du = (int)(dwx * CR_TEX) & 31;
      if ((dSide == 0 && rx > 0) || (dSide == 1 && ry < 0)) du = 31 - du;
      const uint8_t *tc = texMem + (cell[dCell] - 1) * CR_TEX_BYTES + du * CR_TEX;
      const uint16_t *pal = (light[dPrev] ? warm : shade) + shadeFor(dp, dPrev, dSide) * CI_COUNT;
      int32_t stepT = (CR_TEX << 16) / max(lh, 1);
      int ys = max(0, top), ye = min(VH, top + vis);
      int32_t tp = (ys - top) * stepT + (o * CR_TEX / 255 << 16);
      uint16_t *p = base + ys * ay;
      for (int y = ys; y < ye; y++, p += ay, tp += stepT) *p = pal[tc[(tp >> 16) & 31]];
      if (o < 140) zb = dp;
    }
    W->zbuf[x] = zb;
  }
}

// ---------------- billboards ----------------
static void addBill(float x, float y, float z, float wW, float hW, uint8_t img, uint8_t mode, uint8_t extra) {
  if (nBills >= CR_MAX_BILLS) return;
  float rx = x - camX, ry = y - camY;
  float ty = invDet * (-plY * rx + plX * ry);
  if (ty < 0.12f || ty > 18.0f) return;
  float tx = invDet * (dirY * rx - dirX * ry);
  if (fabsf(tx) > ty + wW / planeLen) return;
  Bill &b = W->bills[nBills++];
  b.depth = ty; b.tx = tx; b.z = z; b.wW = wW; b.hW = hW;
  b.img = img; b.mode = mode; b.extra = extra;
  int ci = ((int)y & 31) * CR_MS + ((int)x & 31);
  b.shade = mode == BM_BRIGHT ? 0 : shadeFor(ty, ci, 0);
  b.warm = W->light[ci] && mode != BM_BRIGHT;
}

static const uint8_t BAYER[4] = {0, 2, 3, 1};
// the Warden: the lurker's blues turned to blood reds
static uint8_t bossMap[CI_COUNT];

static void drawBill(const Bill &b, int hor) {
  float scale = focal / b.depth;
  int sx = (int)(VW * 0.5f * (1.0f + b.tx / b.depth));
  int yBot = hor + (int)((0.5f - b.z) * scale);
  if (b.mode == BM_DOT) {
    if (b.depth < 0.6f) return;                     // right in the face: skip
    int r = min(3, max(1, (int)(b.wW * scale)));
    if (sx < 0 || sx >= VW || b.depth >= W->zbuf[sx]) return;
    uint16_t c = W->shade[b.shade * CI_COUNT + b.extra];
    for (int x = max(0, sx - r / 2); x < min(VW, sx - r / 2 + r); x++) {
      uint16_t *p = colPtr(x);
      for (int y = max(0, yBot - r); y < min(VH, yBot); y++) p[y * ay] = c;
    }
    return;
  }
  int wPx = (int)(b.wW * scale), hPx = (int)(b.hW * scale);
  if (wPx < 1 || hPx < 1) return;
  int yTop = yBot - hPx, x0 = sx - wPx / 2;
  int iw = SP_SIZE[b.img][0], ih = SP_SIZE[b.img][1];
  const uint8_t *data = sprMem + spOff[b.img];
  int32_t uStep = (iw << 16) / wPx, vStep = (ih << 16) / hPx;
  int xs = max(0, x0), xe = min(VW, x0 + wPx), ys = max(0, yTop), ye = min(VH, yBot);
  if (xs >= xe || ys >= ye) return;
  const uint16_t *pal = b.mode == BM_FLASH ? W->flash : (b.warm ? W->warm : W->shade) + b.shade * CI_COUNT;
  int32_t u = (xs - x0) * uStep, v0 = (ys - yTop) * vStep;
  for (int x = xs; x < xe; x++, u += uStep) {
    if (b.depth >= W->zbuf[x]) continue;
    const uint8_t *col = data + (u >> 16) * ih;
    uint16_t *p = colPtr(x) + ys * ay;
    int32_t v = v0;
    if (b.mode == BM_NORMAL || b.mode == BM_FLASH || b.mode == BM_BRIGHT) {
      for (int y = ys; y < ye; y++, v += vStep, p += ay) {
        uint8_t c = col[v >> 16];
        if (c) *p = pal[c];
      }
    } else if (b.mode == BM_BOSS) {
      for (int y = ys; y < ye; y++, v += vStep, p += ay) {
        uint8_t c = col[v >> 16];
        if (c) *p = pal[bossMap[c]];
      }
    } else {                                        // BM_FADE: ordered dither, extra = 0..4
      for (int y = ys; y < ye; y++, v += vStep, p += ay) {
        uint8_t c = col[v >> 16];
        if (c && BAYER[(x & 1) | ((y & 1) << 1)] < b.extra) *p = pal[c];
      }
    }
  }
}

static void drawBills(int hor) {
  // far to near (insertion sort: the order barely changes between frames)
  Bill *B = W->bills;
  for (int i = 1; i < nBills; i++) {
    Bill t = B[i];
    int j = i - 1;
    while (j >= 0 && B[j].depth < t.depth) { B[j + 1] = B[j]; j--; }
    B[j + 1] = t;
  }
  for (int i = 0; i < nBills; i++) drawBill(B[i], hor);
}

// ---------------- game: effects ----------------
static void say(const char *s) {
  strncpy(msg, s, sizeof msg - 1);
  msg[sizeof msg - 1] = 0;
  msgT = 110;
}
static void addPart(float x, float y, float z, float vz, uint8_t kind, int life) {
  for (int i = 0; i < CR_MAX_PARTS; i++) {
    Part &p = parts[i];
    if (p.on) continue;
    p.x = x; p.y = y; p.z = z; p.vz = vz; p.kind = kind; p.t = 0; p.life = life; p.on = true;
    return;
  }
}
static void burst(float x, float y, float z, uint8_t kind, int n) {
  for (int i = 0; i < n; i++)
    addPart(x + frnd(-0.2f, 0.2f), y + frnd(-0.2f, 0.2f), z + frnd(-0.1f, 0.2f), frnd(-0.01f, 0.02f), kind, 20 + rnd(20));
}
static void addShot(float x, float y, float a, float speed, int dmg, bool enemy) {
  for (int i = 0; i < CR_MAX_SHOTS; i++) {
    Shot &s = shots[i];
    if (s.on) continue;
    s.x = x; s.y = y; s.dx = cosf(a) * speed; s.dy = sinf(a) * speed;
    s.dmg = dmg; s.life = 140; s.enemy = enemy; s.on = true;
    return;
  }
}

static void saveHigh() {
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("crypt_hi", hiScore);
  }
}
static void setState(State s) {
  state = s;
  stateAt = tickNow;
  armed = false;
  hudDirty = true;
}

static void hurtPlayer(int dmg) {
  if (state != ST_PLAY || hp <= 0) return;
  hp -= dmg;
  hurtT = min(40, 10 + dmg * 2);
  shakeT = 8;
  hudDirty = true;
  if (hp > 0) return;
  hp = 0;
  lives--;
  dbg("crypt: fainted at depth %d, lives %d\n", lvl + 1, lives);
  if (lives > 0) setState(ST_LOST);
  else { saveHigh(); setState(ST_OVER); }
}

// ---------------- game: player ----------------
static void castWand() {
  const WandDef &w = CR_WAND[wand - 1];
  if (mana < w.cost) {
    say("Out of mana!");
    fireCd = 24;
    return;
  }
  mana -= w.cost;
  // aim assist: the visible foe closest to the middle of the view
  float aim = pa, best = 0.16f;
  for (int i = 0; i < nFoes; i++) {
    Foe &f = foes[i];
    if (!f.alive || f.state == FS_DYING) continue;
    float dx = f.x - px, dy = f.y - py, d2 = dx * dx + dy * dy;
    if (d2 > 144) continue;
    float da = fabsf(angDiff(atan2f(dy, dx), pa));
    if (da < best && los(px, py, f.x, f.y)) { best = da; aim = atan2f(dy, dx); }
  }
  for (int k = 0; k < w.bolts; k++) {
    float a = aim + (k - (w.bolts - 1) * 0.5f) * w.spread * 2;
    addShot(px + cosf(a) * 0.25f, py + sinf(a) * 0.25f, a, 0.17f, w.dmg, false);
  }
  fireCd = w.cd;
  fireAnim = 12;
  hudDirty = true;
}

static void pickups() {
  for (int i = 0; i < nThings; i++) {
    Thing &t = things[i];
    if (!t.on || t.kind > TK_WAND) continue;
    float dx = t.x - px, dy = t.y - py;
    if (dx * dx + dy * dy > 0.45f * 0.45f) continue;
    switch (t.kind) {
      case TK_HEALTH:
        if (hp >= 100) continue;
        hp = min(100, hp + 25);
        say("Tonic +25");
        break;
      case TK_MANA:
        if (mana >= 99) continue;
        mana = min(99, mana + 15);
        say("Pearl +15 mana");
        break;
      case TK_GKEY: keys |= 1; say("Gold key"); break;
      case TK_BKEY: keys |= 2; say("Blue key"); break;
      case TK_GOLD: gold++; score += 250; say("Treasure +250"); break;
      case TK_WAND:
        mana = min(99, mana + 20);
        if (wand < CR_WAND_MAX) { wand++; say(CR_WAND[wand - 1].name); }
        else { score += 500; say("Wand surge +500"); }
        break;
    }
    t.on = false;
    pickT = 18;
    hudDirty = true;
    burst(t.x, t.y, 0.2f, PK_SPARK, 6);
  }
}

static void levelClear() {
  uint32_t secs = lvlTicks * CR_TICK_MS / 1000;
  uint16_t par = CR_LEVELS[lvl].par;
  levelBonus = 500 + (secs < par ? (par - secs) * 4 : 0) + (kills == killsTotal ? 500 : 0);
  score += levelBonus;
  dbg("crypt: depth %d clear in %lus, bonus %lu\n", lvl + 1, (unsigned long)secs, (unsigned long)levelBonus);
  setState(ST_CLEAR);
}

// something in front of a swimming player: doors open, the arch ends the depth
static void bump() {
  int cx = (int)(px + cosf(pa) * 0.6f), cy = (int)(py + sinf(pa) * 0.6f);
  uint8_t c = cellAt(cx, cy);
  if (c == CW_EXIT) { levelClear(); return; }
  if (!isDoor(c)) return;
  int i = cy * CR_MS + cx;
  if (W->dopen[i]) return;
  if ((c == CW_GDOOR && !(keys & 1)) || (c == CW_BDOOR && !(keys & 2))) {
    if (!bumpT) say(c == CW_GDOOR ? "Locked: gold key" : "Locked: blue key");
    bumpT = 60;
    return;
  }
  startDoor(i);
}

static void playerTick() {
  Btn &tl = turnLeftBtn(), &tr = turnRightBtn();
  bool hl = tl.stable, hr = tr.stable, both = hl && hr;
  float target = 0;
  if (!both) {
    if (hl && (int32_t)(tickNow - tl.t0) >= (int32_t)CR_TURN_DELAY) target = -CR_TURN_MAX;
    if (hr && (int32_t)(tickNow - tr.t0) >= (int32_t)CR_TURN_DELAY) target = CR_TURN_MAX;
  }
  // ramped turning: short holds turn finely, long holds turn fast
  turnV += (target - turnV) * (target != 0 ? 0.16f : 0.5f);
  pa += turnV;
  if (pa > PI) pa -= 2 * PI;
  if (pa < -PI) pa += 2 * PI;

  bool walk = both && (int32_t)(tickNow - chordT) >= (int32_t)CR_WALK_DELAY;
  if (walk) {
    pvx = (pvx + cosf(pa) * CR_ACC) * 0.9f;
    pvy = (pvy + sinf(pa) * CR_ACC) * 0.9f;
  } else {
    float f = (hl || hr) ? 0.9f : 0.8f;           // glide on while steering
    pvx *= f; pvy *= f;
  }
  moveBody(px, py, pvx, pvy, CR_PLAYER_R);
  float sp = sqrtf(pvx * pvx + pvy * pvy);
  bob += sp * 2.2f;
  if (walk) bump();
  if (state != ST_PLAY) return;
  if (bumpT) bumpT--;

  if (fireCd) fireCd--;
  if (fireAnim) fireAnim--;
  if (pendingFire) {
    pendingFire--;
    if (!fireCd) { castWand(); pendingFire = 0; }
  }
  // the wand slowly refills when nearly dry, so a run can't get stuck
  if (mana < 5 && ++regenT >= 90) { regenT = 0; mana++; hudDirty = true; }
  if (hurtT) hurtT--;
  if (pickT) pickT--;
  if (shakeT) shakeT--;
  if (msgT) msgT--;
  pickups();
}

// ---------------- game: foes ----------------
static void foeTick(Foe &f, int idx) {
  if (!f.alive) return;
  if (f.hitT) f.hitT--;
  if (f.state == FS_DYING) {
    if (++f.t > 40) f.alive = false;
    return;
  }
  const FoeDef &D = CR_FOE[f.type];
  float dx = px - f.x, dy = py - f.y, dist = sqrtf(dx * dx + dy * dy) + 1e-4f;
  f.anim++;
  if (((lvlTicks + idx) & 7) == 0) f.sees = dist < D.sight && los(f.x, f.y, px, py);
  if (f.sees && f.state == FS_IDLE) f.state = FS_CHASE;
  if (f.state == FS_CHASE && dist > D.sight * 1.6f) f.state = FS_IDLE;
  if (f.cd) f.cd--;
  bool floater = f.type != FT_DROWNER;
  if (floater) f.z = (f.type == FT_JELLY ? 0.22f : 0.08f) + sinf(f.anim * 0.05f + idx) * 0.06f;

  float ux = dx / dist, uy = dy / dist, mvx = 0, mvy = 0, sp = D.speed;
  if (f.state == FS_IDLE) {
    if (f.t) f.t--;
    else { f.heading = rnd(628) / 100.0f; f.t = 60 + rnd(140); }
    mvx = cosf(f.heading) * sp * 0.45f;
    mvy = sinf(f.heading) * sp * 0.45f;
  } else {
    float keep = f.type == FT_DROWNER ? 0.7f : f.type == FT_JELLY ? 3.2f : f.type == FT_LURKER ? 1.6f : 2.2f;
    if (dist > keep + 0.4f) { mvx = ux * sp; mvy = uy * sp; }
    else if (dist < keep - 0.6f) { mvx = -ux * sp * 0.6f; mvy = -uy * sp * 0.6f; }
    else {                                         // circle the player a little
      float s = (idx & 1) ? 0.5f : -0.5f;
      mvx = -uy * sp * s; mvy = ux * sp * s;
    }
    // melee wind-up; the bite lands if the player is still close
    if (D.bite && dist < (f.type == FT_WARDEN ? 1.6f : 0.95f) && !f.cd && !f.windup) {
      f.windup = 14;
      f.cd = f.type == FT_DROWNER ? 56 : 70;
    }
    // ink spit
    if (D.inkShots && f.sees && !f.cd && dist < 9 && dist > 1.2f) {
      float a = atan2f(dy, dx);
      int n = D.inkShots;
      for (int k = 0; k < n; k++) addShot(f.x + ux * 0.3f, f.y + uy * 0.3f, a + (k - (n - 1) * 0.5f) * 0.12f, 0.08f, D.ink, true);
      f.cd = f.type == FT_JELLY ? 100 + rnd(50) : f.type == FT_LURKER ? 110 + rnd(40) : 60 + rnd(30);
    }
  }
  if (f.windup && --f.windup == 0 && dist < (f.type == FT_WARDEN ? 1.9f : 1.15f)) hurtPlayer(D.bite);

  // keep apart from each other and out of the player
  for (int j = 0; j < nFoes; j++) {
    if (j == idx || !foes[j].alive) continue;
    float ex = f.x - foes[j].x, ey = f.y - foes[j].y, e2 = ex * ex + ey * ey;
    if (e2 < 0.36f && e2 > 1e-4f) { mvx += ex * 0.02f; mvy += ey * 0.02f; }
  }
  if (dist < 0.55f + (f.type == FT_WARDEN ? 0.5f : 0)) { mvx -= ux * sp; mvy -= uy * sp; }
  float r = f.type == FT_WARDEN ? 0.45f : 0.28f;
  if (!moveBody(f.x, f.y, mvx, mvy, r) && f.state == FS_IDLE) f.t = 0;
}

static void hitFoe(Foe &f, int dmg) {
  f.hp -= dmg;
  f.hitT = 6;
  f.state = FS_CHASE;
  f.sees = true;
  if (f.hp > 0) return;
  f.state = FS_DYING;
  f.t = 0;
  kills++;
  score += CR_FOE[f.type].points;
  burst(f.x, f.y, f.z + 0.3f, f.type == FT_DROWNER ? PK_GUTS : PK_INK, 10);
  hudDirty = true;
  if (f.type == FT_WARDEN) say("The Warden falls!");
}

static void shotsTick() {
  for (int i = 0; i < CR_MAX_SHOTS; i++) {
    Shot &s = shots[i];
    if (!s.on) continue;
    s.x += s.dx; s.y += s.dy;
    if (!--s.life || opaqueAt((int)s.x, (int)s.y)) {
      s.on = false;
      burst(s.x - s.dx, s.y - s.dy, 0.4f, s.enemy ? PK_INK : PK_SPARK, 4);
      continue;
    }
    if (s.enemy) {
      float dx = s.x - px, dy = s.y - py;
      if (dx * dx + dy * dy < 0.09f) { s.on = false; hurtPlayer(s.dmg); burst(s.x, s.y, 0.4f, PK_INK, 4); }
      continue;
    }
    for (int k = 0; k < nFoes; k++) {
      Foe &f = foes[k];
      if (!f.alive || f.state == FS_DYING) continue;
      float r = CR_FOE[f.type].width * 0.45f, dx = s.x - f.x, dy = s.y - f.y;
      if (dx * dx + dy * dy < r * r) {
        s.on = false;
        hitFoe(f, s.dmg);
        burst(s.x, s.y, 0.45f, PK_SPARK, 4);
        break;
      }
    }
  }
}

static void partsTick() {
  for (int i = 0; i < CR_MAX_PARTS; i++) {
    Part &p = parts[i];
    if (!p.on) continue;
    if (++p.t >= p.life) { p.on = false; continue; }
    p.z += p.vz;
    if (p.kind == PK_BUBBLE) p.x += sinf(p.t * 0.15f + i) * 0.003f;
    else p.vz -= 0.0015f;
    if (p.z < 0) { p.z = 0; p.vz = 0; }
    if (p.z > 1) p.on = false;
  }
  // bubbles drift up around the player
  if (rnd(100) < 8) {
    float bx = px + frnd(-3.5f, 3.5f), by = py + frnd(-3.5f, 3.5f);
    if (!solidAt((int)bx, (int)by)) addPart(bx, by, 0, frnd(0.004f, 0.009f), PK_BUBBLE, 110);
  }
}

static void playTick() {
  lvlTicks++;
  doorsTick();
  playerTick();
  if (state != ST_PLAY) return;
  for (int i = 0; i < nFoes; i++) foeTick(foes[i], i);
  shotsTick();
  partsTick();
}

// ---------------- frame: the 3D scene ----------------
static void sceneBills() {
  nBills = 0;
  for (int i = 0; i < nThings; i++) {
    const Thing &t = things[i];
    if (!t.on) continue;
    switch (t.kind) {
      case TK_TORCH: addBill(t.x, t.y, 0, 0.42f, 0.84f, SP_TORCH0 + ((frameNo / 6 + i) & 1), BM_NORMAL, 0); break;
      case TK_PILLAR: addBill(t.x, t.y, 0, 0.5f, 1.0f, SP_PILLAR, BM_NORMAL, 0); break;
      case TK_KELP: addBill(t.x, t.y, 0, 0.45f, 0.9f, SP_KELP0 + ((frameNo / 24 + i) & 1), BM_NORMAL, 0); break;
      case TK_BARREL: addBill(t.x, t.y, 0, 0.5f, 0.5f, SP_BARREL, BM_NORMAL, 0); break;
      case TK_BONES: addBill(t.x, t.y, 0, 0.42f, 0.42f, SP_BONES, BM_NORMAL, 0); break;
      default:                                     // pickups bob gently
        addBill(t.x, t.y, 0.08f + sinf(frameNo * 0.07f + i) * 0.04f, 0.34f, 0.34f, SP_HEALTH + t.kind, BM_NORMAL, 0);
        break;
    }
  }
  for (int i = 0; i < nFoes; i++) {
    const Foe &f = foes[i];
    if (!f.alive) continue;
    const FoeDef &D = CR_FOE[f.type];
    int img = D.sprite + (f.windup ? 1 : (f.anim / 12) & 1);
    uint8_t mode = f.type == FT_WARDEN ? BM_BOSS : BM_NORMAL, extra = 0;
    float z = f.z;
    if (f.state == FS_DYING) { mode = BM_FADE; extra = 4 - min(4, f.t / 9); z -= f.t * 0.008f; }
    else if (f.hitT) mode = BM_FLASH;
    addBill(f.x, f.y, z, D.width, D.height, img, mode, extra);
  }
  for (int i = 0; i < CR_MAX_SHOTS; i++) {
    const Shot &s = shots[i];
    if (s.on) addBill(s.x, s.y, 0.36f, 0.28f, 0.28f, s.enemy ? SP_INK : SP_BOLT, BM_BRIGHT, 0);
  }
  for (int i = 0; i < CR_MAX_PARTS; i++) {
    const Part &p = parts[i];
    if (!p.on) continue;
    static const uint8_t COL[4] = {CI_BLUE + 5, CI_GLOW + 3, CI_INK + 3, CI_MOSS + 4};
    addBill(p.x, p.y, p.z, p.kind == PK_BUBBLE ? 0.035f : 0.03f, 0, 0, BM_DOT, COL[p.kind]);
  }
}

static void animTick() {
  // brazier flicker: a slow random walk of the light
  static int flickV = 0;
  flickV += rnd(13) - 6;
  flickV = flickV < -30 ? -30 : flickV > 10 ? 10 : flickV;
  fireFlick = 246 + flickV;
  for (int i = 0; i < 16; i++) lightLut[i] = (uint8_t)(i * fireFlick >> 8);
  glowCyc = (frameNo / 5) & 3;
}

static void drawScene(float x, float y, float a, int hor) {
  camSetup(x, y, a);
  castView(hor);
  sceneBills();
  drawBills(hor);
}

// Tigo's claw and the wand at the bottom of the view
static void drawHand() {
  const uint8_t *img = sprMem + spOff[SP_HAND];
  const int w = 48, h = 48;
  int rec = fireAnim > 6 ? (fireAnim - 6) * 2 : 0;
  int x0 = VW / 2 + (VW > 200 ? 18 : 6) + (int)(sinf(bob) * 3) + rec / 2;
  int y0 = VH - 42 + (int)(fabsf(cosf(bob)) * 3) + rec;
  int s = 2 - lightLut[W->light[(int)py * CR_MS + (int)px]];
  const uint16_t *pal = (lightLut[W->light[(int)py * CR_MS + (int)px]] ? W->warm : W->shade) + max(0, s) * CI_COUNT;
  for (int u = 0; u < w; u++) {
    int x = x0 + u;
    if (x < 0 || x >= VW) continue;
    const uint8_t *col = img + u * h;
    for (int v = 0; v < h; v++) {
      int yy = y0 + v;
      if (yy >= VH) break;
      uint8_t c = col[v];
      if (c && yy >= 0) colPtr(x)[yy * ay] = pal[c];
    }
  }
  if (fireAnim > 3) {                               // cast flash at the pearl
    int r = fireAnim / 2;
    discL(x0 + 19, y0 + 8, r + 2, rgb(60, 200, 220));
    discL(x0 + 19, y0 + 8, r, 0xFFFF);
  }
}

static void drawMinimap() {
  const int C = 2, R = 11, S = (2 * R + 1) * C;
  int x0 = VW - S - 3, y0 = 3, pcx = (int)px, pcy = (int)py;
  dimRectL(x0 - 1, y0 - 1, S + 2, S + 2, 2);
  for (int dy = -R; dy <= R; dy++)
    for (int dx = -R; dx <= R; dx++) {
      int cx = pcx + dx, cy = pcy + dy;
      if ((unsigned)cx >= CR_MS || (unsigned)cy >= CR_MS) continue;
      int i = cy * CR_MS + cx;
      if (!(W->flag[i] & FL_SEEN)) continue;
      uint8_t c = W->cell[i];
      uint16_t col;
      if (c == CW_FLOOR) col = rgb(18, 44, 52);
      else if (c == CW_EXIT) col = rgb(80, 255, 255);
      else if (c == CW_GDOOR) col = rgb(255, 210, 40);
      else if (c == CW_BDOOR) col = rgb(70, 140, 255);
      else if (c == CW_DOOR) col = W->dopen[i] ? rgb(60, 40, 20) : rgb(170, 110, 50);
      else col = rgb(120, 130, 140);
      rectL(x0 + (dx + R) * C, y0 + (dy + R) * C, C, C, col);
    }
  int cx = x0 + R * C, cy = y0 + R * C;
  rectL(cx, cy, 2, 2, 0xFFFF);
  putL(cx + (int)roundf(cosf(pa) * 4), cy + (int)roundf(sinf(pa) * 4), sw(0xFFE0));
  putL(cx + 1 + (int)roundf(cosf(pa) * 3), cy + (int)roundf(sinf(pa) * 3), sw(0xFFE0));
  frameL(x0 - 2, y0 - 2, S + 4, S + 4, rgb(60, 110, 120));
}

// ---------------- HUD ----------------
static const uint16_t HUD_BG = 0x0861, HUD_EDGE = 0x2B0E, HUD_TEXT = 0xFFFF;

static TigoPose facePose() {
  if (hurtT) return TIGO_SAD;
  if (fireAnim) return TIGO_PINCH;
  if (pickT) return TIGO_HAPPY;
  if (hp < 30) return TIGO_SAD;
  return TIGO_IDLE;
}

static void pips(int x, int y) {
  for (int i = 0; i < CR_WAND_MAX; i++) {
    uint16_t c = i < wand ? rgb(255, 150, 120) : 0x2945;
    rectL(x + i * 6 + 2, y, 1, 1, c);              // little diamonds
    rectL(x + i * 6 + 1, y + 1, 3, 1, c);
    rectL(x + i * 6, y + 2, 5, 1, c);
    rectL(x + i * 6 + 1, y + 3, 3, 1, c);
    rectL(x + i * 6 + 2, y + 4, 1, 1, c);
  }
}

// lives: a tiny shell and "x3"
static void livesL(int x, int y) {
  char buf[8];
  discL(x + 3, y + 3, 3, rgb(246, 214, 160));
  putL(x + 3, y + 3, sw(rgb(150, 90, 60)));
  putL(x + 4, y + 2, sw(rgb(150, 90, 60)));
  snprintf(buf, sizeof buf, "x%d", lives);
  textL(buf, x + 9, y, 1, rgb(246, 214, 160));
}

static void bar(int x, int y, int w, int v, int vmax, uint16_t col) {
  rectL(x, y, w, 4, 0x18C3);
  rectL(x, y, w * min(v, vmax) / vmax, 4, col);
}

static void drawHud() {
  char buf[20];
  int y0 = VH;
  TigoPose pose = facePose();
  uint16_t faceBg = hurtT ? rgb(110, 0, 0) : pickT ? rgb(80, 70, 0) : rgb(8, 24, 30);
  uint16_t hpCol = hp < 30 ? rgb(255, 60, 40) : HUD_TEXT;
  rectL(0, y0, LW, LH - y0, HUD_BG);
  rectL(0, y0, LW, 1, HUD_EDGE);
  if (ori != OR_PORTRAIT) {
    blitImg(SP_HEALTH, 0, y0 + 4, false);
    snprintf(buf, sizeof buf, "%d", hp);
    textL(buf, 16, y0 + 4, 2, hpCol);
    blitImg(SP_MANA, 43, y0 + 4, false);
    snprintf(buf, sizeof buf, "%d", mana);
    textL(buf, 59, y0 + 4, 2, mana ? HUD_TEXT : rgb(255, 60, 40));
    blitImg(SP_GKEY, 82, y0 + 4, !(keys & 1));
    blitImg(SP_BKEY, 96, y0 + 4, !(keys & 2));
    tigoBox(116, y0 + 1, 22, pose, tickNow, faceBg);
    blitImg(SP_WAND, 140, y0 + 4, false);
    pips(155, y0 + 9);
    livesL(175, y0 + 3);
    snprintf(buf, sizeof buf, "D%d", lvl + 1);
    textL(buf, 175, y0 + 13, 1, rgb(120, 220, 230));
    snprintf(buf, sizeof buf, "%lu", (unsigned long)score);
    textL(buf, LW - 2, y0 + 4, 2, rgb(255, 220, 90), 2);
  } else {
    tigoBox(2, y0 + 3, 34, pose, tickNow, faceBg);
    blitImg(SP_HEALTH, 39, y0 + 2, false);
    snprintf(buf, sizeof buf, "%d", hp);
    textL(buf, 56, y0 + 2, 2, hpCol);
    bar(84, y0 + 8, 48, hp, 100, hp < 30 ? rgb(255, 60, 40) : rgb(230, 50, 60));
    blitImg(SP_MANA, 39, y0 + 20, false);
    snprintf(buf, sizeof buf, "%d", mana);
    textL(buf, 56, y0 + 20, 2, mana ? HUD_TEXT : rgb(255, 60, 40));
    bar(84, y0 + 26, 48, mana, 99, rgb(60, 170, 255));
    blitImg(SP_GKEY, 0, y0 + 38, !(keys & 1));
    blitImg(SP_BKEY, 15, y0 + 38, !(keys & 2));
    blitImg(SP_WAND, 36, y0 + 38, false);
    pips(52, y0 + 44);
    livesL(78, y0 + 43);
    snprintf(buf, sizeof buf, "Depth %d", lvl + 1);
    textL(buf, 3, y0 + 55, 2, rgb(120, 220, 230));
    snprintf(buf, sizeof buf, "%lu", (unsigned long)score);
    textL(buf, LW - 3, y0 + 55, 2, rgb(255, 220, 90), 2);
  }
  lastPose = pose;
  faceAt = tickNow;
  hudDirty = false;
}

static void renderPlay() {
  setLayout(false);
  if (hurtT) buildShades(256, 255, 0, 0, min(120, hurtT * 6));
  else if (pickT) buildShades(256, 255, 220, 80, pickT * 4);
  else buildShades(256, 0, 0, 0, 0);
  float sp = sqrtf(pvx * pvx + pvy * pvy);
  int hor = VH / 2 + (int)(sinf(bob * 2) * min(1.0f, sp * 30) * 1.6f);
  if (shakeT) hor += rnd(5) - 2;
  drawScene(px, py, pa, hor);
  drawHand();
  uint16_t ch = sw(rgb(120, 220, 230));              // a faint crosshair
  int cx = VW / 2, cy = VH / 2;
  putL(cx - 4, cy, ch); putL(cx - 3, cy, ch); putL(cx + 3, cy, ch); putL(cx + 4, cy, ch);
  putL(cx, cy - 3, ch); putL(cx, cy + 3, ch);
  if (mapOn) drawMinimap();
  if (msgT) {
    // beside the minimap in landscape, under it in portrait
    bool beside = mapOn && ori != OR_PORTRAIT;
    int room = beside ? VW - 56 : VW - 4;
    textL(msg, beside ? (VW - 52) / 2 : VW / 2, mapOn && !beside ? 56 : 5, textW(msg, 2) <= room ? 2 : 1, rgb(255, 230, 140), 1, true);
  }
  if (hudDirty || facePose() != lastPose || (int32_t)(tickNow - faceAt) > 800) drawHud();
}

// ---------------- cards, title ----------------
static int cardCX() { return ori == OR_LAND_RIGHT ? 104 : ori == OR_LAND_LEFT ? 136 : 67; }

// one line of font 2 if it fits, else two lines split at a space, else font 1;
// returns the height used
static int textBlock(const char *s, int cx, int y, int maxW, uint16_t col) {
  if (textW(s, 2) <= maxW) { textL(s, cx, y, 2, col, 1, true); return 16; }
  int n = strlen(s), best = -1;
  char a[40], b[40];
  for (int i = 1; i < n && i < 39; i++)
    if (s[i] == ' ' && (best < 0 || abs(i - n / 2) < abs(best - n / 2))) best = i;
  if (best > 0 && n < 78) {
    memcpy(a, s, best); a[best] = 0;
    strncpy(b, s + best + 1, sizeof b - 1); b[sizeof b - 1] = 0;
    int f = textW(a, 2) <= maxW && textW(b, 2) <= maxW ? 2 : 1, h = f == 2 ? 16 : 10;
    textL(a, cx, y, f, col, 1, true);
    textL(b, cx, y + h, f, col, 1, true);
    return 2 * h;
  }
  textL(s, cx, y, 1, col, 1, true);
  return 10;
}

static void backdrop(float x, float y, float a, int bright) {
  setLayout(true);
  buildShades(bright, 0, 0, 0, 0);
  drawScene(x, y, a, VH / 2);
}

static void panel(int w, int h) {
  int x = cardCX() - w / 2, y = (LH - h) / 2;
  dimRectL(x, y, w, h, 2);
  frameL(x, y, w, h, rgb(40, 120, 130));
  frameL(x + 2, y + 2, w - 4, h - 4, rgb(16, 50, 60));
}

static void controlsLines(int cx, int y) {
  const char *turn = ori == OR_PORTRAIT ? "Hold L / R    turn" :
                     ori == OR_LAND_RIGHT ? "Hold top/bottom: turn L/R" : "Hold top/bottom: turn R/L";
  uint16_t c = rgb(170, 210, 215);
  textL(turn, cx, y, 1, c, 1);
  textL(ori == OR_PORTRAIT ? "Hold both     swim" : "Hold both: swim", cx, y + 10, 1, c, 1);
  textL(ori == OR_PORTRAIT ? "Tap L or R    cast" : "Tap one: cast", cx, y + 20, 1, c, 1);
  textL(ori == OR_PORTRAIT ? "Tap both       map" : "Tap both: map", cx, y + 30, 1, c, 1);
}

static bool blink() { return (frameNo / 30) & 1; }

static void renderTitle() {
  char buf[32];
  backdrop(px, py, demoA, 150);
  int cx = cardCX();
  bool land = ori != OR_PORTRAIT;
  dimRectL(0, 0, LW, land ? 44 : 56, 1);
  int ty = land ? 5 : 12;
  for (int k = 0; k < 4; k++)                      // a soft teal glow behind the logo
    textL("CRYPT", cx + (k & 1 ? 1 : -1), ty + (k & 2 ? 1 : -1), 4, rgb(20, 110, 120), 1);
  textL("CRYPT", cx, ty, 4, rgb(255, 205, 90), 1);
  textL(land ? "Tigo's dive into the sunken crypt" : "the sunken depths", cx, land ? 32 : 42, 1, rgb(120, 220, 230), 1, true);
  int y = land ? 52 : 132;
  dimRectL(cx - (land ? 78 : 62), y - 4, land ? 156 : 124, 46, 1);
  controlsLines(cx, y);
  snprintf(buf, sizeof buf, "View: %s", orientName(ori));
  textL(buf, cx, land ? 98 : 186, 1, rgb(255, 230, 140), 1, true);
  snprintf(buf, sizeof buf, "Best %lu", (unsigned long)hiScore);
  textL(buf, cx, land ? 110 : 198, land ? 1 : 2, rgb(200, 200, 200), 1, true);
  btnHint(false, "view", rgb(120, 220, 230));
  btnHint(true, "start", rgb(120, 255, 140));
  edgeMarker(blink() ? rgb(255, 230, 140) : rgb(150, 130, 80));
}

static void renderIntro() {
  char buf[32];
  uint32_t t = tickNow - stateAt;
  backdrop(px, py, pa + sinf(t * 0.0012f) * 0.5f, 130);
  bool land = ori != OR_PORTRAIT, first = lvl == 0;
  int cx = cardCX(), pw = land ? 196 : 131, h = first ? (land ? 126 : 172) : (land ? 90 : 116);
  panel(pw, h);
  int y = (LH - h) / 2 + 8;
  snprintf(buf, sizeof buf, "DEPTH %d / %d", lvl + 1, CR_NUM_LEVELS);
  textL(buf, cx, y, 2, rgb(120, 220, 230), 1, true);
  const char *name = CR_LEVELS[lvl].name;
  y += 20;
  int f = fitFont(name, pw - 10);
  if (f == 1) y += textBlock(name, cx, y, pw - 10, rgb(255, 205, 90)) + 4;
  else { textL(name, cx, y, f, rgb(255, 205, 90), 1, true); y += f == 4 ? 30 : 20; }
  y += textBlock(CR_LEVELS[lvl].hint, cx, y, pw - 10, rgb(220, 220, 220));
  if (first) controlsLines(cx, y + 6);
  if (blink()) textL("press a button", cx, (LH + h) / 2 - 13, 1, rgb(140, 160, 160), 1);
}

static void renderClear() {
  char buf[32];
  uint32_t t = tickNow - stateAt;
  backdrop(px, py, pa + t * 0.0006f, 120);
  bool land = ori != OR_PORTRAIT;
  int cx = cardCX(), h = land ? 122 : 140, y = (LH - h) / 2 + 6, dy = land ? 14 : 16;
  panel(land ? 180 : 131, h);
  snprintf(buf, sizeof buf, "DEPTH %d CLEAR", lvl + 1);
  textL(buf, cx, y, 2, rgb(120, 255, 160), 1, true);
  uint32_t secs = lvlTicks * CR_TICK_MS / 1000;
  uint16_t c = rgb(220, 220, 220);
  int lx = cx - (land ? 70 : 56), rx = cx + (land ? 70 : 56);
  const char *lab[4] = {"Foes", "Treasure", "Time", "Bonus"};
  char val[4][16];
  snprintf(val[0], 16, "%d / %d", kills, killsTotal);
  snprintf(val[1], 16, "%d / %d", gold, goldTotal);
  snprintf(val[2], 16, "%lu:%02lu", (unsigned long)(secs / 60), (unsigned long)(secs % 60));
  snprintf(val[3], 16, "+%lu", (unsigned long)levelBonus);
  for (int i = 0; i < 4; i++) {
    textL(lab[i], lx, y + 20 + i * dy, 2, c);
    textL(val[i], rx, y + 20 + i * dy, 2, i == 3 ? rgb(255, 230, 140) : c, 2);
  }
  snprintf(buf, sizeof buf, "Score %lu", (unsigned long)score);
  textL(buf, cx, y + 22 + 4 * dy, 2, rgb(255, 205, 90), 1);
  if (blink() && t > 700) textL("press a button", cx, (LH + h) / 2 - 11, 1, rgb(140, 160, 160), 1);
}

static void renderLost() {
  char buf[32];
  uint32_t t = tickNow - stateAt;
  backdrop(px, py, pa + t * 0.0004f, 90);
  bool land = ori != OR_PORTRAIT;
  int cx = cardCX(), h = 80, y = (LH - h) / 2 + 8;
  panel(land ? 170 : 131, h);
  textL("YOU BLACKED OUT", cx, y, 2, rgb(255, 90, 70), 1, true);
  snprintf(buf, sizeof buf, "%d %s left", lives, lives == 1 ? "life" : "lives");
  textL(buf, cx, y + 22, 2, rgb(220, 220, 220), 1);
  textL("The depth restarts", cx, y + 42, 1, rgb(170, 190, 190), 1);
  if (blink() && t > 700) textL("press a button", cx, y + 56, 1, rgb(140, 160, 160), 1);
}

static void renderEnd(bool won) {
  char buf[32];
  uint32_t t = tickNow - stateAt;
  backdrop(px, py, pa + t * 0.0004f, won ? 140 : 80);
  bool land = ori != OR_PORTRAIT;
  int cx = cardCX(), h = land ? 92 : 110, y = (LH - h) / 2 + 8;
  panel(land ? 176 : 131, h);
  const char *title = won ? "CRYPT CLEANSED!" : "THE CRYPT WINS";
  int f = fitFont(title, land ? 166 : 120);
  textL(title, cx, y, f, won ? rgb(120, 255, 160) : rgb(255, 90, 70), 1, true);
  y += f == 4 ? 30 : 22;
  snprintf(buf, sizeof buf, "Score %lu", (unsigned long)score);
  textL(buf, cx, y, 2, HUD_TEXT, 1);
  if (newHigh) textL("NEW BEST!", cx, y + 18, 2, rgb(255, 230, 90), 1);
  else {
    snprintf(buf, sizeof buf, "Best %lu", (unsigned long)hiScore);
    textL(buf, cx, y + 18, 2, rgb(170, 190, 190), 1);
  }
  snprintf(buf, sizeof buf, won ? "All %d depths" : "Reached depth %d", won ? CR_NUM_LEVELS : lvl + 1);
  textL(buf, cx, y + 38, 1, rgb(120, 220, 230), 1);
  btnHint(false, "menu", rgb(120, 220, 230));
  btnHint(true, "again", rgb(120, 255, 140));
}

static void render() {
  uint32_t t0 = micros();
  switch (state) {
    case ST_TITLE: renderTitle(); break;
    case ST_INTRO: renderIntro(); break;
    case ST_PLAY: renderPlay(); break;
    case ST_CLEAR: renderClear(); break;
    case ST_LOST: renderLost(); break;
    case ST_OVER: renderEnd(false); break;
    case ST_WIN: renderEnd(true); break;
    default: break;
  }
  uint32_t t1 = micros();
  spr.pushSprite(0, 0);
  uint32_t t2 = micros();
  statFrames++;
  statDraw += t1 - t0;
  statPush += t2 - t1;
  if ((int32_t)(tickNow - statAt) >= 3000) {
    if (statFrames) dbg("crypt: %lu fps, draw %lu us, push %lu us (state %d)\n",
                        (unsigned long)(statFrames * 1000 / max<uint32_t>(1, tickNow - statAt)),
                        (unsigned long)(statDraw / statFrames), (unsigned long)(statPush / statFrames), (int)state);
    statFrames = statDraw = statPush = 0;
    statAt = tickNow;
  }
}

// ---------------- flow ----------------
static void newGame() {
  score = 0;
  lives = CR_LIVES;
  wand = 1;
  hp = CR_START_HP;
  mana = CR_START_MANA;
  newHigh = false;
  mapOn = false;
  loadLevel(0);
  setState(ST_INTRO);
}

static void leave() {
  resetOrient();
  freeMem();
}

static void cardAdvance() {
  if (state == ST_INTRO) {
    chord = false;
    pendingFire = 0;
    setState(ST_PLAY);
  } else if (state == ST_CLEAR) {
    if (lvl + 1 < CR_NUM_LEVELS) {
      loadLevel(lvl + 1);
      hp = max(hp, 50);                            // a breather between depths
      setState(ST_INTRO);
    } else {
      score += 5000 + lives * 1000;
      saveHigh();
      setState(ST_WIN);
    }
  } else if (state == ST_LOST) {
    loadLevel(lvl);
    hp = CR_START_HP;
    mana = max(mana, 30);
    setState(ST_INTRO);
  }
}

// Taps and chords, read between ticks (see the header comment). A press is
// "solo" when the other button was up; a solo press released quickly casts.
// Both pressed within CR_CHORD_MS make a chord: released quickly = minimap.
static void playInput(int eL, int eR, uint32_t now) {
  if (eL == 1) soloL = !bR.stable;
  if (eR == 1) soloR = !bL.stable;
  if ((eL == 1 || eR == 1) && bL.stable && bR.stable) {
    chord = true;
    chordPure = (int32_t)(eL == 1 ? now - bR.t0 : now - bL.t0) < (int32_t)CR_CHORD_MS;
    chordT = now;
  }
  for (int k = 0; k < 2; k++) {
    int e = k ? eR : eL;
    if (e != 2) continue;
    Btn &b = k ? bR : bL;
    bool tap = (int32_t)(now - b.t0) < (int32_t)CR_TAP_MS, bothUp = !bL.stable && !bR.stable;
    if (chord && chordPure) {
      if (bothUp) {
        if ((int32_t)(now - chordT) < 400) { mapOn = !mapOn; dbg("crypt: map %d\n", mapOn); }
        chord = false;
      }
    } else {
      if (tap && (k ? soloR : soloL)) pendingFire = 12;
      if (bothUp) chord = false;
    }
  }
}

static void tick() {
  frameNo++;
  animTick();
  switch (state) {
    case ST_TITLE: demoA += 0.004f; break;
    case ST_PLAY: playTick(); break;
    default:
      for (int i = 0; i < CR_MAX_PARTS; i++) if (parts[i].on && ++parts[i].t >= parts[i].life) parts[i].on = false;
      break;
  }
}

// ---------------- entry points ----------------
void drawIcon(int x, int y, bool dim, uint32_t t) {
  auto c = [dim](uint16_t col) { return dim ? tft.alphaBlend(90, col, TFT_BLACK) : col; };
  float g = t ? 0.5f + 0.5f * sinf(t * 0.006f) : 1.0f;
  spr.fillRect(x - 12, y - 15, 24, 30, c(0x2104));
  spr.fillRect(x - 12, y - 15, 24, 3, c(0x4208));
  spr.fillCircle(x, y - 2, 8, c(0xC4A0));                      // gold arch
  spr.fillRect(x - 8, y - 2, 17, 14, c(0xC4A0));
  spr.fillCircle(x, y - 2, 6, c(tft.alphaBlend((uint8_t)(120 + 135 * g), 0x9FFF, 0x0410)));
  spr.fillRect(x - 6, y - 2, 13, 14, c(tft.alphaBlend((uint8_t)(120 + 135 * g), 0x9FFF, 0x0410)));
  spr.fillCircle(x, y + 1, 2, c(TFT_WHITE));
  spr.fillRect(x - 12, y + 12, 24, 3, c(0x4208));
  // a brazier flame on each side
  int f = (t / 120) & 1;
  spr.fillTriangle(x - 11, y - 4, x - 9, y - 10 - f, x - 7, y - 4, c(0xFC00));
  spr.fillTriangle(x + 7, y - 4, x + 9, y - 10 + f, x + 11, y - 4, c(0xFC00));
}

void begin() {
  hiScore = prefs.getUInt("crypt_hi", 0);
  ori = prefs.getUChar("crypt_ori", OR_PORTRAIT) % NUM_ORIENTS;
  fb = (uint16_t *)spr.getPointer();
  tickNow = lastTick = lastInput = statAt = millis();
  armed = false;
  frameNo = 0;
  statFrames = statDraw = statPush = 0;
  tablesVH = -1;
  if (!allocMem()) {
    state = ST_NOMEM;
    spr.fillSprite(TFT_BLACK);
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_RED);
    spr.drawString("CRYPT", 67, 80, 4);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("Not enough memory", 67, 120, 2);
    spr.drawString("press a button", 67, 150, 2);
    spr.pushSprite(0, 0);
    return;
  }
  for (int i = 0; i < CI_COUNT; i++) bossMap[i] = i;
  for (int k = 0; k < 6; k++) bossMap[CI_BLUE + k] = k < 2 ? CI_INK + k : CI_RED + min(3, k - 2);
  bossMap[CI_GLOW + 2] = CI_FIRE + 1;
  bossMap[CI_CORAL + 1] = CI_INK + 2;
  setLayout(true);
  loadLevel(0);
  state = ST_TITLE;
  stateAt = tickNow;
  render();
}

void update(uint32_t now) {
  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) lastInput = now;

  if (state == ST_NOMEM) {
    if (eL == 1 || eR == 1) armed = true;
    if (armed && (eL == 2 || eR == 2)) { leave(); returnToMenu(); }
    return;
  }

  switch (state) {
    case ST_TITLE:
      if (eL == 1 || eR == 1) armed = true;
      if (armed && eL == 2) {
        armed = false;
        ori = (ori + 1) % NUM_ORIENTS;
        prefs.putUChar("crypt_ori", ori);
      } else if (armed && eR == 2) {
        armed = false;
        newGame();
      }
      break;
    case ST_PLAY:
      playInput(eL, eR, now);
      break;
    case ST_OVER:
    case ST_WIN: {
      EndChoice e = startOrEnd(true, eL, eR, armed);
      if (e == END_MENU) { leave(); return; }      // startOrEnd already went back to the menu
      if (e == END_PLAY) newGame();
      break;
    }
    default: {                                     // cards: a fresh press and its release
      if (eL == 1 || eR == 1) armed = true;
      int32_t age = (int32_t)(now - stateAt);
      if ((armed && (eL == 2 || eR == 2) && age > (state == ST_INTRO ? 300 : 700)) ||
          (state == ST_INTRO && age > 4000 && !bL.stable && !bR.stable))
        cardAdvance();
      break;
    }
  }

  // fixed 16 ms ticks; after a stall, don't try to catch up
  if ((int32_t)(now - lastTick) > 200) lastTick = now - CR_TICK_MS;
  bool ticked = false;
  while ((int32_t)(now - lastTick) >= (int32_t)CR_TICK_MS) {
    lastTick += CR_TICK_MS;
    tickNow = lastTick;
    tick();
    ticked = true;
  }

  if (state != ST_PLAY && idleTooLong(now, lastInput)) {
    leave();
    goToSleep();
    return;
  }
  if (ticked) render();
}

}  // namespace cryptgame

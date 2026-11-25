// Abyx (sunken-citadel action platformer starring Tigo the hermit crab)
//
// Controls (two buttons; in landscape the bottom button is "left" and the top
// one "right", the title screen points at them with arrows):
//   Hold L / R           : walk left / right
//   Press the other one  : jump (one held + press the other, or both together);
//                          keep both held = higher jump. Coyote time and a
//                          jump buffer forgive late / early presses.
//   Jump again mid-air   : bubble jump (power)
//   Hold into a wall     : cling (power); press the other button = wall jump
//   Double-tap L / R     : jet dash (power), harmless to touch hazards meanwhile
//   Hold both ~1 s, still: map (release to close); keep holding ~2.5 s more
//                          to save and go back to the menu
//   Tigo fires shrimps by himself at foes in front of him. With the Jumbo
//   claw, about every 1.3 s a big shrimp flies out that cracks glowing coral.
// Title: Tap L = next option, Tap R = choose (the view option cycles Portrait /
// Landscape > / < Landscape, saved). Fainted: L = menu, R = retry.
//
// Explore a sunken citadel: 28 rooms in five biomes (coral caves, kelp forest,
// drowned ruins, abyssal trench, lava vents) joined by open doors. Four
// guardians each leave a power (bubble jump, wall cling, jet dash, heat shell),
// the Jumbo claw waits on an altar, the Magma Heart ends the game. Hidden heart
// shells (+1 heart), power shells (+1 damage), secret walls and pearls reward
// backtracking. Save altars (giant clams) save and heal as Tigo passes (prefs
// "abyx_sv": rooms, powers, items, altar). Score = pearls, shells, foes,
// powers, guardians, rooms explored (+ a bonus at 100%); best in "abyx_hi".
//
// Rendering: straight into the 16-bit sprite buffer (abyx_gfx.h); landscape
// turns the panel through orient.h. Tile art is cached per biome, the parallax
// background is drawn as column runs. Fixed 16 ms ticks, one frame per tick.

#include "common.h"
#include "orient.h"
#include "abyx_gfx.h"
#include "abyx_art.h"
#include "abyx_rooms.h"

namespace abyx {

// ================= tuning =================
static const uint32_t TICK_MS = 16;
static const int TS = 8;                          // tile size, px
static const int CELL = 16;                       // tiles per map cell
static const int MARGIN = 8;                      // tiles of the neighbouring rooms kept around the room
static const int MAP_MAX = 3 * CELL + 2 * MARGIN; // rooms are at most 3x3 cells
static const int PW = 10, PH = 12;                // Tigo's hitbox
static const float RUN_MAX = 1.45f, RUN_ACC = 0.22f, AIR_ACC = 0.15f, RUN_DEC = 0.28f;
static const float GRAV = 0.30f, GRAV_HOLD = 0.15f, FALL_MAX = 2.8f;
static const float JUMP_V = 3.35f, BUBBLE_V = 3.0f, WALLJ_VX = 1.1f, WALLJ_VY = 3.1f, CLING_FALL = 0.6f;
static const float DASH_V = 4.0f;
static const int DASH_T = 14, COYOTE_T = 6, BUFFER_T = 8, TAP_T = 14, MAP_HOLD_T = 60, QUIT_HOLD_T = 150;
static const int INV_T = 70, SHOOT_CD = 15, CHARGE_T = 80;
static const int MAX_ENTS = 20, MAX_SHOTS = 32, MAX_PARTS = 90, MAX_ITEMS = 16, MAX_DECO = 4;

enum { AB_BUBBLE = 1, AB_JUMBO = 2, AB_CLING = 4, AB_DASH = 8, AB_HEAT = 16, NUM_ABIL = 5 };
static const uint8_t BOSS_ABIL[NUM_BIOMES] = {AB_BUBBLE, AB_CLING, AB_DASH, AB_HEAT, 0};
static const char *const ABIL_NAME[NUM_ABIL] = {"BUBBLE JUMP", "JUMBO CLAW", "WALL CLING", "JET DASH", "HEAT SHELL"};
// hint lines stay within 20 chars (font 1) so they fit the portrait banner
static const char *const ABIL_HINT1[NUM_ABIL] = {"Jump again mid-air", "Big shrimps crack", "Hold into a wall,",
                                                 "Tap a button twice", "Lava cannot hurt"};
static const char *const ABIL_HINT2[NUM_ABIL] = {"for a second jump", "glowing coral", "tap the other: climb",
                                                 "to dash that way", "you any more"};
static const uint16_t ABIL_COL[NUM_ABIL] = {0x9FFF, 0xFC10, 0x87F0, 0xFFE0, 0xFB00};
static const char *const BOSS_NAME[NUM_BIOMES] = {"URCHIN KING", "KELP WYRM", "STONE WARDEN", "GLOOM ANGLER", "MAGMA HEART"};

// ================= save =================
static const uint8_t SAVE_VER = 1;
struct Save {
  uint8_t ver, room, ax, ay;           // the altar Tigo wakes up at (room, tile)
  uint8_t abil, bosses, maxHp, power;  // powers, guardians beaten, hearts, extra damage
  uint32_t visited, opened;            // rooms seen / rooms whose cracked coral is gone
  uint16_t items[NUM_ROOMS];           // pearls and shells taken, by order of appearance in the room
  uint16_t kills, deaths;
};
static Save sv;
static bool haveSave = false;

// ================= state =================
enum State { TITLE, PLAY, OVER, END };
enum Mode { M_RUN, M_BANNER, M_MAP };
static State state = TITLE;
static Mode mode = M_RUN;
static int orient = OR_PORTRAIT;
static Btn *btnLeft = &bL, *btnRight = &bR;
static uint32_t hiScore = 0, lastTick = 0, lastInput = 0, tickN = 0;
static bool dirty = true, armed = false, newHigh = false;
static int titleSel = 0, titleConfirm = 0;

// room
static uint8_t tmap[MAP_MAX * MAP_MAX];
static int room = 0, roomW = 16, roomH = 16, mapW = 32, mapH = 32, biome = 0;
static bool xRevealed = false;

struct Player {
  float x, y, vx, vy, safeX, safeY;
  int8_t face, wallDir, dashDir;
  bool ground, dashUsed;
  int coyote, buffer, airJumps, dashT, wallLock, inv, shootCd, charge, anim, shootAnim, bothT, deadT, hp;
};
static Player pl;

struct Ent {
  float x, y, vx, vy, hx, hy;
  int16_t t, st, hit;
  int8_t hp, dir;
  uint8_t type;
  bool on;
};
static Ent ents[MAX_ENTS];

struct Shot { float x, y, vx, vy; uint8_t kind, life; int8_t dmg; bool on; };
enum { SH_SHRIMP, SH_JUMBO, SH_ORB, SH_SPINE, SH_SEED, SH_FIRE };
static Shot shots[MAX_SHOTS];

struct Part { float x, y, vx, vy; uint16_t col; uint8_t life, max, kind; };
enum { PK_BUBBLE, PK_SPARK, PK_DUST, PK_GLINT };
static Part parts[MAX_PARTS];

struct Item { int16_t x, y; uint8_t kind, bit; bool on; };   // kind: 'o' 'H' 'P' 'S' 'A'
static Item items[MAX_ITEMS];
static int nItems = 0;
static int16_t vents[MAX_DECO * 2];
static int nVents = 0;

struct Boss {
  bool on;
  uint8_t kind;
  int hp, maxHp, st, t, hit, cyc, side;
  float x, y, vx, vy, a, a2, hx, hy;
  int8_t rockHp[4];
  int16_t rockT[4];
};
static Boss boss;
static bool bossGate = false;

// view
static float camX = 0, camY = 0;
static int shakeT = 0, fadeT = 0, flashT = 0, toastT = 0, bannerT = 0, endT = 0;
static const char *toast = "";
static char bannerTitle[24], bannerL1[28], bannerL2[28];
static uint16_t bannerCol = 0xFFFF;
static bool bannerEnds = false;
static uint32_t drawUs = 0, frameUs = 0, statFrames = 0, statT = 0;
static int hudPearls = -1;                         // pearls shown in the HUD, -1 = count again

// ================= helpers =================
static int8_t sinLut[256];                         // sin(i * 2pi / 256) * 127
static inline int isin(int a) { return sinLut[a & 255]; }
static void initSin() { for (int i = 0; i < 256; i++) sinLut[i] = (int8_t)lroundf(sinf(i * 6.2831853f / 256) * 127); }
static inline int fl(float v) { return (int)floorf(v); }
static inline int fdiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
static inline bool has(int ab) { return (sv.abil & ab) != 0; }
static int rnd(int n) { return (int)(esp_random() % (uint32_t)n); }
static float frand(float a, float b) { return a + (b - a) * (float)(esp_random() % 10001) / 10000.0f; }
static int abilIndex(int ab) { for (int i = 0; i < NUM_ABIL; i++) if (ab == (1 << i)) return i; return 0; }

static inline char tileAt(int tx, int ty) {
  int mx = tx + MARGIN, my = ty + MARGIN;
  if ((unsigned)mx >= (unsigned)mapW || (unsigned)my >= (unsigned)mapH) return '#';
  return (char)tmap[my * mapW + mx];
}
static inline void setTile(int tx, int ty, char c) {
  int mx = tx + MARGIN, my = ty + MARGIN;
  if ((unsigned)mx < (unsigned)mapW && (unsigned)my < (unsigned)mapH) tmap[my * mapW + mx] = (uint8_t)c;
}
static inline bool inRoom(int tx, int ty) { return tx >= 0 && ty >= 0 && tx < roomW && ty < roomH; }
static inline bool solidT(int tx, int ty) {
  char c = tileAt(tx, ty);
  if (c == '#' || c == 'C') return true;
  return bossGate && !inRoom(tx, ty);
}
static inline bool solidPx(float x, float y) { return solidT(fl(x / TS), fl(y / TS)); }
static inline bool standPx(float x, float y) {
  int tx = fl(x / TS), ty = fl(y / TS);
  return solidT(tx, ty) || tileAt(tx, ty) == '=';
}

static int roomAtCell(int cx, int cy) {
  for (int i = 0; i < NUM_ROOMS; i++) {
    const RoomDef &d = ROOMS[i];
    if (cx >= d.cx && cy >= d.cy && cx < d.cx + d.cw && cy < d.cy + d.ch) return i;
  }
  return -1;
}
// a tile anywhere in the world, as stored in flash (with broken coral removed)
static char worldChar(int wx, int wy) {
  int r = roomAtCell(fdiv(wx, CELL), fdiv(wy, CELL));
  if (r < 0) return '#';
  const RoomDef &d = ROOMS[r];
  char c = d.tiles[(wy - d.cy * CELL) * (d.cw * CELL) + (wx - d.cx * CELL)];
  if (c == 'C' && (sv.opened >> r & 1)) c = '.';
  return c;
}
static bool isFoeChar(char c) { return c && strchr("cujpetwaqmr", c) != nullptr; }

// ================= score =================
static int countBits(uint32_t v) { int n = 0; while (v) { n += v & 1; v >>= 1; } return n; }
static int totalItems = 0, totalPearls = 0;
static void countWorld() {
  totalItems = totalPearls = 0;
  for (int r = 0; r < NUM_ROOMS; r++) {
    const RoomDef &d = ROOMS[r];
    int n = d.cw * CELL * d.ch * CELL;
    for (int i = 0; i < n; i++) {
      char c = d.tiles[i];
      if (c == 'o') totalPearls++;
      if (c == 'o' || c == 'H' || c == 'P') totalItems++;
    }
  }
}
static int itemsTaken(int *pearls) {
  int all = 0, pe = 0;
  for (int r = 0; r < NUM_ROOMS; r++) {
    const RoomDef &d = ROOMS[r];
    int n = d.cw * CELL * d.ch * CELL, bit = 0;
    for (int i = 0; i < n && bit < 16; i++) {
      char c = d.tiles[i];
      if (c != 'o' && c != 'H' && c != 'P') continue;
      if (sv.items[r] >> bit & 1) { all++; if (c == 'o') pe++; }
      bit++;
    }
  }
  if (pearls) *pearls = pe;
  return all;
}
static int completion() {
  int got = countBits(sv.visited) + itemsTaken(nullptr) + countBits(sv.bosses) + countBits(sv.abil);
  int all = NUM_ROOMS + totalItems + NUM_BIOMES + NUM_ABIL;
  return all ? got * 100 / all : 0;
}
static uint32_t score() {
  int pearls = 0, all = itemsTaken(&pearls);
  // foes come back when a room is re-entered: their points are capped
  int32_t s = pearls * 10 + (all - pearls) * 100 + min((int)sv.kills, 250) * 2 + countBits(sv.abil) * 150 +
              countBits(sv.bosses) * 500 + countBits(sv.visited) * 20 - sv.deaths * 20;
  if (completion() >= 100) s += 2000;
  return s > 0 ? (uint32_t)s : 0;
}

static void writeSave() {
  sv.ver = SAVE_VER;
  prefs.putBytes("abyx_sv", &sv, sizeof sv);
  haveSave = true;
  uint32_t s = score();
  if (s > hiScore) { hiScore = s; prefs.putUInt("abyx_hi", hiScore); newHigh = true; }
}
static bool readSave() {
  if (!prefs.isKey("abyx_sv") || prefs.getBytesLength("abyx_sv") != sizeof sv) return false;
  if (prefs.getBytes("abyx_sv", &sv, sizeof sv) != sizeof sv || sv.ver != SAVE_VER || sv.room >= NUM_ROOMS) return false;
  return true;
}
static void freshSave() {
  memset(&sv, 0, sizeof sv);
  sv.ver = SAVE_VER;
  sv.maxHp = 3;
  // the start marker 'T' in room 0
  const RoomDef &d = ROOMS[0];
  int w = d.cw * CELL;
  for (int i = 0; i < w * d.ch * CELL; i++)
    if (d.tiles[i] == 'T') { sv.ax = i % w; sv.ay = i / w; }
}

// ================= particles =================
static void addPart(float x, float y, float vx, float vy, uint16_t col, int life, int kind) {
  for (int i = 0; i < MAX_PARTS; i++) {
    Part &p = parts[i];
    if (p.life) continue;
    p.x = x; p.y = y; p.vx = vx; p.vy = vy; p.col = col; p.life = p.max = (uint8_t)life; p.kind = (uint8_t)kind;
    return;
  }
}
static void burst(float x, float y, int n, uint16_t col, float sp) {
  for (int i = 0; i < n; i++) {
    float a = frand(0, 6.2832f), s = frand(0.3f, sp);
    addPart(x, y, cosf(a) * s, sinf(a) * s - 0.3f, col, 18 + rnd(18), PK_SPARK);
  }
}
static void bubbles(float x, float y, int n) {
  for (int i = 0; i < n; i++) addPart(x + frand(-4, 4), y + frand(-3, 3), frand(-0.2f, 0.2f), frand(-0.9f, -0.4f), 0xDFFF, 40 + rnd(40), PK_BUBBLE);
}
static void shake(int t) { if (t > shakeT) shakeT = t; }

static void tickParts() {
  for (int i = 0; i < MAX_PARTS; i++) {
    Part &p = parts[i];
    if (!p.life) continue;
    p.life--;
    if (p.kind == PK_BUBBLE) {
      p.vy -= 0.01f;
      if (p.vy < -1.1f) p.vy = -1.1f;
      p.x += p.vx + sinf((p.life + i) * 0.25f) * 0.25f;
      p.y += p.vy;
      if (solidPx(p.x, p.y)) p.life = 0;
    } else {
      if (p.kind == PK_SPARK || p.kind == PK_DUST) p.vy += 0.06f;
      p.vx *= 0.96f; p.vy *= 0.97f;
      p.x += p.vx; p.y += p.vy;
    }
  }
}

// ================= foes =================
enum { E_CRAB, E_URCHIN, E_JELLY, E_PUFFER, E_EEL, E_TURRET, E_WISP, E_ANGLER, E_SQUID, E_MAGMA, E_FSHRIMP, E_COUNT };
struct EInfo { char ch; uint8_t hw, hh; int8_t hp; uint8_t pts; bool shootable; };
static const EInfo EINFO[E_COUNT] = {
  {'c', 6, 4, 4, 5, true}, {'u', 5, 5, 6, 5, true}, {'j', 5, 5, 2, 5, true}, {'p', 5, 5, 6, 8, true},
  {'e', 5, 3, 4, 8, true}, {'t', 6, 6, 8, 10, true}, {'w', 4, 4, 4, 8, true}, {'a', 8, 5, 8, 12, true},
  {'q', 4, 5, 4, 8, true}, {'m', 4, 4, 99, 0, false}, {'r', 5, 4, 6, 10, true},
};

static Ent *spawnEnt(int type, float x, float y) {
  for (int i = 0; i < MAX_ENTS; i++) {
    Ent &e = ents[i];
    if (e.on) continue;
    memset(&e, 0, sizeof e);
    e.on = true; e.type = (uint8_t)type; e.x = e.hx = x; e.y = e.hy = y;
    e.hp = EINFO[type].hp; e.dir = 1; e.t = (int16_t)rnd(200);
    return &e;
  }
  return nullptr;
}

static Shot *fireShot(int kind, float x, float y, float vx, float vy, int dmg, int life) {
  for (int i = 0; i < MAX_SHOTS; i++) {
    Shot &s = shots[i];
    if (s.on) continue;
    s.on = true; s.kind = (uint8_t)kind; s.x = x; s.y = y; s.vx = vx; s.vy = vy; s.dmg = (int8_t)dmg; s.life = (uint8_t)life;
    return &s;
  }
  return nullptr;
}
static void aimShot(int kind, float x, float y, float sp, float spread) {
  float dx = pl.x + PW / 2 - x, dy = pl.y + PH / 2 - y, d = sqrtf(dx * dx + dy * dy);
  if (d < 1) d = 1;
  float a = atan2f(dy, dx) + spread;
  fireShot(kind, x, y, cosf(a) * sp, sinf(a) * sp, 1, 200);
  (void)d;
}

// ================= room loading =================
static void spawnFromMap();
static void loadRoom(int id) {
  const RoomDef &d = ROOMS[id];
  room = id;
  roomW = d.cw * CELL;
  roomH = d.ch * CELL;
  mapW = roomW + 2 * MARGIN;
  mapH = roomH + 2 * MARGIN;
  biome = d.biome;
  int ox = d.cx * CELL - MARGIN, oy = d.cy * CELL - MARGIN;
  for (int my = 0; my < mapH; my++)
    for (int mx = 0; mx < mapW; mx++) {
      char c = worldChar(ox + mx, oy + my);
      bool in = mx >= MARGIN && my >= MARGIN && mx < MARGIN + roomW && my < MARGIN + roomH;
      if (!in && !strchr("#=^vCXLkygib.", c)) c = (c == 'm') ? 'L' : '.';   // others' foes and items stay home
      tmap[my * mapW + mx] = (uint8_t)c;
    }
  memset(ents, 0, sizeof ents);
  memset(shots, 0, sizeof shots);
  memset(parts, 0, sizeof parts);
  memset(&boss, 0, sizeof boss);
  bossGate = false;
  xRevealed = false;
  spawnFromMap();
  genTiles(biome);
  genBackground(biome, fb.h);
  bool first = !(sv.visited >> id & 1);
  sv.visited |= 1UL << id;
  static int lastBiome = -1;
  if (first || biome != lastBiome) { toast = BIOMES[biome].name; toastT = biome != lastBiome ? 150 : 0; }
  lastBiome = biome;
  dbg("abyx: room %d (biome %d)\n", id, biome);
}

static void startBoss(int kind, float x, float y);
static void spawnFromMap() {
  nItems = 0;
  nVents = 0;
  int bit = 0;
  for (int ty = 0; ty < roomH; ty++)
    for (int tx = 0; tx < roomW; tx++) {
      char c = tileAt(tx, ty);
      float x = tx * TS + 4, y = ty * TS + 4;
      if (c == 'o' || c == 'H' || c == 'P') {
        bool taken = sv.items[room] >> bit & 1;
        if (!taken && nItems < MAX_ITEMS) items[nItems++] = {(int16_t)x, (int16_t)y, (uint8_t)c, (uint8_t)bit, true};
        bit++;
        setTile(tx, ty, '.');
      } else if (c == 'S' || c == 'A') {
        bool on = c == 'S' || !has(ROOMS[room].abil);
        if (nItems < MAX_ITEMS) items[nItems++] = {(int16_t)x, (int16_t)(ty * TS + 8), (uint8_t)c, 0, on};
        setTile(tx, ty, '.');
      } else if (c == 'B') {
        setTile(tx, ty, '.');
        if (!(sv.bosses >> biome & 1)) startBoss(biome, x, y);
      } else if (c == 'T') {
        setTile(tx, ty, '.');
      } else if (c == 'b') {
        if (nVents < MAX_DECO) { vents[nVents * 2] = (int16_t)x; vents[nVents * 2 + 1] = (int16_t)(ty * TS + 6); nVents++; }
      } else if (isFoeChar(c)) {
        int type = 0;
        while (type < E_COUNT && EINFO[type].ch != c) type++;
        setTile(tx, ty, c == 'm' ? 'L' : '.');
        Ent *e = spawnEnt(type, x, y);
        if (!e) continue;
        const EInfo &in = EINFO[type];
        if (type == E_CRAB || type == E_FSHRIMP) e->y = e->hy = ty * TS + TS - in.hh;   // stand on the floor
        if (type == E_EEL) e->dir = solidT(tx - 1, ty) ? 1 : -1;                    // out of its hole
        if (type == E_TURRET || type == E_URCHIN) {
          if (solidT(tx, ty + 1)) e->y = e->hy = ty * TS + TS - in.hh;
          else if (solidT(tx, ty - 1)) e->y = e->hy = ty * TS + in.hh;
        }
        if (type == E_MAGMA) { e->y = e->hy = ty * TS + 6; e->st = 0; }
      }
    }
}

// ================= foe behaviour =================
static void hurtPlayer(int dmg, float fromX);
static bool foeHitsPlayer(const Ent &e) {
  const EInfo &in = EINFO[e.type];
  float hw = in.hw, hh = in.hh;
  if (e.type == E_PUFFER && e.st == 1) { hw += 3; hh += 3; }
  if (e.type == E_EEL && e.st == 0) return false;
  if (e.type == E_MAGMA && e.st == 0) return false;
  float cx = pl.x + PW / 2, cy = pl.y + PH / 2;
  return fabsf(e.x - cx) < hw + PW / 2 - 1 && fabsf(e.y - cy) < hh + PH / 2 - 1;
}

static void walker(Ent &e, float sp) {
  const EInfo &in = EINFO[e.type];
  float nx = e.x + e.dir * sp;
  float fx = nx + e.dir * (in.hw + 1);
  if (solidPx(fx, e.y) || !standPx(fx, e.y + in.hh + 2)) e.dir = -e.dir;
  else e.x = nx;
}

static void tickEnt(Ent &e) {
  const EInfo &in = EINFO[e.type];
  float px = pl.x + PW / 2, py = pl.y + PH / 2;
  float dx = px - e.x, dy = py - e.y;
  e.t++;
  if (e.hit) e.hit--;
  switch (e.type) {
    case E_CRAB:
      walker(e, 0.45f);
      break;
    case E_FSHRIMP:
      if (e.st > 0) { if (--e.st == 30) aimShot(SH_FIRE, e.x + e.dir * 6, e.y - 2, 1.5f, 0); }
      else {
        walker(e, 0.5f);
        if (e.t % 140 == 0 && fabsf(dx) < 110 && fabsf(dy) < 40) { e.dir = dx > 0 ? 1 : -1; e.st = 50; }
      }
      break;
    case E_URCHIN:
      e.st = (e.t / 90) & 1;                       // spines out every other 1.5 s
      break;
    case E_JELLY:
      e.y = e.hy + sinf(e.t * 0.035f) * 14;
      e.x = e.hx + sinf(e.t * 0.011f) * 10;
      break;
    case E_PUFFER:
      if (fabsf(dx) < 34 && fabsf(dy) < 30) e.st = 1, e.vx = 0;
      else if (e.st == 1 && (e.t & 127) == 0) e.st = 0;
      if (e.st == 0) {
        e.x += e.dir * 0.35f;
        if (fabsf(e.x - e.hx) > 36 || solidPx(e.x + e.dir * 7, e.y)) e.dir = -e.dir;
      }
      e.y = e.hy + sinf(e.t * 0.05f) * 3;
      break;
    case E_EEL: {
      // st 0 hidden, 1 lunging out, 2 out, 3 back in; vx = how far out (px)
      if (e.st == 0) {
        if (e.t > 60 && fabsf(dy) < 16 && dx * e.dir > 0 && fabsf(dx) < 80) { e.st = 1; e.t = 0; }
      } else if (e.st == 1) { e.vx += 3.2f; if (e.vx >= 40) { e.vx = 40; e.st = 2; e.t = 0; } }
      else if (e.st == 2) { if (e.t > 40) { e.st = 3; e.t = 0; } }
      else { e.vx -= 1.6f; if (e.vx <= 0) { e.vx = 0; e.st = 0; e.t = 0; } }
      e.x = e.hx + e.dir * e.vx;
      e.y = e.hy + (e.st ? sinf(e.t * 0.3f) * 1.5f : 0);
      break;
    }
    case E_TURRET:
      if (e.t % 110 == 0 && fabsf(dx) < 130 && fabsf(dy) < 90) aimShot(SH_ORB, e.x, e.y, 1.3f, 0);
      break;
    case E_WISP: {
      float d = sqrtf(dx * dx + dy * dy);
      if (d < 120 && d > 1) { e.vx += dx / d * 0.02f; e.vy += dy / d * 0.02f; }
      e.vx *= 0.97f; e.vy *= 0.97f;
      e.x += e.vx; e.y += e.vy + sinf(e.t * 0.08f) * 0.2f;
      break;
    }
    case E_ANGLER:
      if (e.st == 0) {                             // patrol
        e.x += e.dir * 0.4f;
        if (fabsf(e.x - e.hx) > 44 || solidPx(e.x + e.dir * 10, e.y)) e.dir = -e.dir;
        e.y = e.hy + sinf(e.t * 0.03f) * 4;
        if (fabsf(dx) < 76 && fabsf(dy) < 30 && dx * e.dir > 0) { e.st = 1; e.t = 0; }
      } else if (e.st == 1) {                      // lure flashes: about to lunge
        if (e.t > 30) { e.st = 2; e.t = 0; float d = sqrtf(dx * dx + dy * dy) + 0.1f; e.vx = dx / d * 2.6f; e.vy = dy / d * 2.6f; }
      } else if (e.st == 2) {
        e.x += e.vx; e.y += e.vy;
        if (e.t > 34 || solidPx(e.x + (e.vx > 0 ? 9 : -9), e.y)) { e.st = 3; e.t = 0; e.vx = e.vy = 0; }
      } else if (e.t > 50) { e.st = 0; e.hx = e.x; e.hy = e.y; }
      if (e.vx) e.dir = e.vx > 0 ? 1 : -1;
      break;
    case E_SQUID: {
      if (e.t % 70 == 0) { float d = sqrtf(dx * dx + dy * dy) + 0.1f; if (d < 120) { e.vx = dx / d * 2.0f; e.vy = dy / d * 2.0f - 0.6f; } }
      e.vx *= 0.95f; e.vy = e.vy * 0.95f + 0.01f;
      float nx = e.x + e.vx, ny = e.y + e.vy;
      if (!solidPx(nx, ny)) { e.x = nx; e.y = ny; } else { e.vx = -e.vx; e.vy = -e.vy; }
      break;
    }
    case E_MAGMA:
      // st 0 waits in the lava, 1 = in the air
      if (e.st == 0) { if (e.t > 100 + (int)(e.hx) % 60) { e.st = 1; e.vy = -4.6f; e.t = 0; } }
      else {
        e.vy += 0.12f; e.y += e.vy;
        if (e.t % 3 == 0) addPart(e.x + frand(-2, 2), e.y + 3, 0, 0.2f, 0xFD20, 20, PK_SPARK);
        if (e.vy > 0 && e.y >= e.hy) { e.y = e.hy; e.st = 0; e.t = 0; burst(e.x, e.y - 3, 6, 0xFC00, 1.2f); }
      }
      break;
  }
  (void)in;
}

static void killEnt(Ent &e) {
  burst(e.x, e.y, 14, e.type == E_JELLY ? 0x9FFF : 0xFE60, 1.6f);
  bubbles(e.x, e.y, 4);
  e.on = false;
  if (sv.kills < 60000) sv.kills++;
}

// ================= guardians =================
static void startBoss(int kind, float x, float y) {
  memset(&boss, 0, sizeof boss);
  boss.on = true;
  boss.kind = (uint8_t)kind;
  static const int HP[NUM_BIOMES] = {60, 80, 100, 120, 200};   // a shrimp does 2 + power shells
  boss.hp = boss.maxHp = HP[kind];
  boss.x = boss.hx = x; boss.y = boss.hy = y;
  boss.st = 0; boss.t = 0;
  boss.side = 1;
  for (int i = 0; i < 4; i++) boss.rockHp[i] = 3;
  bossGate = true;
}
static float floorY() { return (roomH - 2) * TS; }

// hit box half sizes per guardian (Kelp Wyrm: the head)
static void bossBox(float &hw, float &hh) {
  static const uint8_t W[NUM_BIOMES] = {12, 8, 12, 12, 13}, H[NUM_BIOMES] = {12, 7, 11, 9, 13};
  hw = W[boss.kind]; hh = H[boss.kind];
}
static bool bossVulnerable() {
  if (boss.kind == 1) return boss.st >= 1 && boss.st <= 3;
  return boss.st > 0;
}

static void tickBoss() {
  Boss &b = boss;
  float px = pl.x + PW / 2, py = pl.y + PH / 2;
  float W = roomW * TS, FY = floorY();
  bool rage = b.hp * 2 < b.maxHp;
  b.t++;
  if (b.hit) b.hit--;
  switch (b.kind) {
    case 0: {   // Urchin King: rolls, hops, rains spines
      const float R = 13;
      if (b.st == 0) { b.y = FY - R; if (b.t > 70) { b.st = 1; b.t = 0; } break; }
      if (b.st == 1) {
        float target = px > b.x ? 1 : -1, vmax = rage ? 2.3f : 1.7f;
        b.vx += target * 0.05f;
        if (b.vx > vmax) b.vx = vmax;
        if (b.vx < -vmax) b.vx = -vmax;
        b.x += b.vx;
        if (b.x < 16 + R || b.x > W - 16 - R) { b.x = constrain(b.x, 16 + R, W - 16 - R); b.vx = -b.vx * 0.8f; shake(6); }
        b.a += b.vx * 0.08f;
        if (b.t > (rage ? 150 : 200)) { b.st = 2; b.t = 0; b.vy = -5.2f; b.cyc = rage ? 2 : 1; }
      } else {
        b.vy += 0.2f;
        b.y += b.vy;
        b.x += b.vx * 0.5f;
        b.x = constrain(b.x, 16 + R, W - 16 - R);
        b.a += 0.15f;
        if (b.vy > 0 && b.y >= FY - R) {
          b.y = FY - R;
          shake(14);
          bubbles(b.x, FY - 2, 8);
          for (int i = 0; i < 9; i++) {
            float a = 3.1416f + i * 3.1416f / 8;
            fireShot(SH_SPINE, b.x + cosf(a) * R, b.y + sinf(a) * R, cosf(a) * 1.8f, sinf(a) * 1.8f, 1, 160);
          }
          if (--b.cyc > 0) b.vy = -4.4f;
          else { b.st = 1; b.t = 0; }
        }
      }
      break;
    }
    case 1: {   // Kelp Wyrm: lunges out of the side walls
      // st 0 hidden (bubbles mark the hole), 1 lunge, 2 spit, 3 back in
      float wallX = b.side > 0 ? 12 : W - 12;
      if (b.st == 0) {
        if (b.t == 1) {
          b.side = rnd(2) ? 1 : -1;
          static const float HY[3] = {14, 38, 62};
          b.hy = FY - HY[rnd(3)];
          wallX = b.side > 0 ? 12 : W - 12;
          b.x = wallX; b.y = b.hy;
        }
        if (b.t % 6 == 0) bubbles(wallX + b.side * 6, b.hy, 1);
        if (b.t > (rage ? 50 : 75)) { b.st = 1; b.t = 0; }
      } else if (b.st == 1) {
        b.x += b.side * (rage ? 2.8f : 2.3f);
        b.y = b.hy + sinf(b.t * 0.2f) * 3;
        if (fabsf(b.x - wallX) > W * 0.62f) { b.st = 2; b.t = 0; }
      } else if (b.st == 2) {
        b.y = b.hy + sinf(b.t * 0.1f) * 5;
        if (b.t % 14 == 7) aimShot(SH_SEED, b.x, b.y, 1.6f, 0);
        if (b.t > 46) { b.st = 3; b.t = 0; }
      } else {
        b.x -= b.side * 2.0f;
        if ((b.x - wallX) * b.side <= 0) { b.st = 0; b.t = 0; }
      }
      break;
    }
    case 2: {   // Stone Warden: hovers and fans orbs, then slams down
      float hoverY = 34;
      if (b.st == 0) {
        if (b.t < 60) { b.y = b.hy + (hoverY - b.hy) * b.t / 60; break; }
        b.st = 1; b.t = 0;
      }
      if (b.st == 1) {
        b.a += 0.015f;
        float tx = W / 2 + sinf(b.a) * (W / 2 - 40);
        b.x += (tx - b.x) * 0.05f;
        b.y = hoverY + sinf(b.t * 0.05f) * 4;
        if (b.t % (rage ? 55 : 75) == 40) for (int i = -2; i <= 2; i++) aimShot(SH_ORB, b.x, b.y + 6, 1.4f, i * 0.28f);
        if (b.t > 280) { b.st = 2; b.t = 0; }
      } else if (b.st == 2) {                     // line up above Tigo
        b.x += (px - b.x) * 0.06f;
        if (b.t > 50) { b.st = 3; b.t = 0; b.vy = 0; }
      } else if (b.st == 3) {                     // slam
        b.vy += 0.45f;
        b.y += b.vy;
        if (b.y >= FY - 11) {
          b.y = FY - 11; shake(16); b.st = 4; b.t = 0;
          for (int i = 0; i < 3; i++) {
            fireShot(SH_ORB, b.x - 10, FY - 4, -1.6f - i * 0.4f, -0.2f * i, 1, 120);
            fireShot(SH_ORB, b.x + 10, FY - 4, 1.6f + i * 0.4f, -0.2f * i, 1, 120);
          }
          burst(b.x, FY - 2, 16, 0xC618, 2.0f);
        }
      } else if (b.st == 4) {                     // stunned on the floor
        if (b.t > 80) { b.st = 5; b.t = 0; }
      } else {
        b.y -= 1.2f;
        if (b.y <= hoverY) { b.y = hoverY; b.st = 1; b.t = 0; }
      }
      break;
    }
    case 3: {   // Gloom Angler: swims a figure eight, flashes its lure, charges
      if (b.st == 0) {
        b.a += 0.012f;
        float tx = W / 2 + cosf(b.a) * (W / 2 - 40), ty = FY / 2 + 4 + sinf(b.a * 1.8f) * 28;
        b.vx = (tx - b.x) * 0.04f; b.vy = (ty - b.y) * 0.04f;
        b.x += b.vx; b.y += b.vy;
        if (b.t > (rage ? 120 : 170)) {
          b.st = 1; b.t = 0;
          if (++b.cyc % 2 == 0) {
            int n = 0;
            for (int i = 0; i < MAX_ENTS; i++) n += ents[i].on;
            if (n < 3) { spawnEnt(E_JELLY, b.x - 20, b.y); spawnEnt(E_JELLY, b.x + 20, b.y); }
          }
        }
      } else if (b.st == 1) {
        if (b.t > 40) {
          float dx = px - b.x, dy = py - b.y, d = sqrtf(dx * dx + dy * dy) + 0.1f;
          b.vx = dx / d * 3.2f; b.vy = dy / d * 3.2f; b.st = 2; b.t = 0;
        }
      } else if (b.st == 2) {
        b.x += b.vx; b.y += b.vy;
        bool wall = b.x < 22 || b.x > W - 22 || b.y < 20 || b.y > FY - 10;
        if (wall || b.t > 60) {
          b.x = constrain(b.x, 22.0f, W - 22); b.y = constrain(b.y, 20.0f, FY - 10);
          if (wall) shake(10);
          b.st = 3; b.t = 0;
        }
      } else if (b.t > 45) { b.st = 0; b.t = 0; b.a = atan2f(b.y - FY / 2, b.x - W / 2); }
      break;
    }
    default: {  // Magma Heart: shielded core, fire spirals
      b.x = W / 2;
      b.y = 46 + sinf(b.t * 0.03f) * 4;
      b.a += rage ? 0.03f : 0.02f;
      for (int i = 0; i < 4; i++) if (b.rockHp[i] <= 0 && --b.rockT[i] <= 0) b.rockHp[i] = 3;
      if (b.st == 0) { if (b.t > 70) { b.st = 1; b.t = 0; } break; }
      if (b.st == 1) {
        if (b.t % (rage ? 6 : 8) == 0) {
          b.a2 += 0.37f;
          for (int k = 0; k < (rage ? 2 : 1); k++) {
            float a = b.a2 + k * 3.1416f;
            fireShot(SH_FIRE, b.x + cosf(a) * 14, b.y + sinf(a) * 14, cosf(a) * 1.3f, sinf(a) * 1.3f, 1, 220);
          }
        }
        if (b.t > 150) { b.st = 2; b.t = 0; }
      } else {
        if (b.t % 30 == 15) for (int i = -1; i <= 1; i++) aimShot(SH_FIRE, b.x, b.y + 10, 1.7f, i * 0.22f);
        if (b.t > 100) { b.st = 1; b.t = 0; }
      }
      break;
    }
  }
}

// segment positions of the Kelp Wyrm's body (from the wall to the head)
static void wyrmSeg(int i, int n, float &x, float &y) {
  float wallX = boss.side > 0 ? 12 : roomW * TS - 12;
  float f = (float)i / n;
  x = wallX + (boss.x - wallX) * f;
  y = boss.hy + (boss.y - boss.hy) * f + sinf(f * 9 + boss.t * 0.15f) * 4 * (1 - f);
}
static bool bossTouches(float cx, float cy, float r) {
  if (!boss.on || (boss.kind == 1 && boss.st == 0)) return false;
  float hw, hh;
  bossBox(hw, hh);
  if (fabsf(boss.x - cx) < hw + r && fabsf(boss.y - cy) < hh + r) return true;
  if (boss.kind == 1) {
    for (int i = 0; i < 8; i++) {
      float x, y;
      wyrmSeg(i, 9, x, y);
      if (fabsf(x - cx) < 5 + r && fabsf(y - cy) < 5 + r) return true;
    }
  }
  return false;
}

// ================= Tigo =================
static bool quitReq = false;
static int altarTouch = -1;
static void showBanner(const char *title, const char *l1, const char *l2, uint16_t col, bool ends) {
  strncpy(bannerTitle, title, sizeof bannerTitle - 1);
  strncpy(bannerL1, l1, sizeof bannerL1 - 1);
  strncpy(bannerL2, l2, sizeof bannerL2 - 1);
  bannerCol = col;
  bannerEnds = ends;
  bannerT = 0;
  mode = M_BANNER;
}

static void placeAtAltar() {
  memset(&pl, 0, sizeof pl);
  pl.x = sv.ax * TS + 4 - PW / 2;
  pl.y = sv.ay * TS + TS - PH;
  pl.safeX = pl.x; pl.safeY = pl.y;
  pl.face = 1;
  pl.hp = sv.maxHp;
  pl.inv = 40;
}

static void die() {
  pl.deadT = 1;
  pl.hp = 0;
  shake(12);
  burst(pl.x + PW / 2, pl.y + PH / 2, 24, 0xFB49, 2.2f);
  bubbles(pl.x + PW / 2, pl.y, 10);
}

static void hurtPlayer(int dmg, float fromX) {
  if (pl.inv > 0 || pl.dashT > 0 || pl.deadT) return;
  pl.hp -= dmg;
  pl.inv = INV_T;
  flashT = 4;
  shake(10);
  pl.vx = (pl.x + PW / 2 < fromX) ? -1.8f : 1.8f;
  pl.vy = -2.2f;
  burst(pl.x + PW / 2, pl.y + PH / 2, 10, 0xFFFF, 1.5f);
  if (pl.hp <= 0) die();
}

static bool touchingWall(int dir) {
  float x = dir > 0 ? pl.x + PW + 0.5f : pl.x - 0.5f;
  return solidPx(x, pl.y + 1) || solidPx(x, pl.y + PH - 2);
}

static bool hazardUnder(bool &lava) {
  lava = false;
  bool spike = false;
  int tx0 = fl((pl.x + 2) / TS), tx1 = fl((pl.x + PW - 3) / TS);
  int ty0 = fl((pl.y + 3) / TS), ty1 = fl((pl.y + PH - 2) / TS);
  for (int ty = ty0; ty <= ty1; ty++)
    for (int tx = tx0; tx <= tx1; tx++) {
      char c = tileAt(tx, ty);
      if (c == '^' || c == 'v') spike = true;
      if (c == 'L') lava = true;
    }
  return spike;
}

static void moveX(float dx) {
  float nx = pl.x + dx;
  int ty0 = fl(pl.y / TS), ty1 = fl((pl.y + PH - 0.01f) / TS);
  if (dx > 0) {
    int tx = fl((nx + PW - 0.01f) / TS);
    for (int ty = ty0; ty <= ty1; ty++) if (solidT(tx, ty)) { nx = tx * TS - PW; pl.vx = 0; break; }
  } else if (dx < 0) {
    int tx = fl(nx / TS);
    for (int ty = ty0; ty <= ty1; ty++) if (solidT(tx, ty)) { nx = (tx + 1) * TS; pl.vx = 0; break; }
  }
  pl.x = nx;
}
static void moveY(float dy) {
  float ny = pl.y + dy;
  int tx0 = fl(pl.x / TS), tx1 = fl((pl.x + PW - 0.01f) / TS);
  pl.ground = false;
  if (dy > 0) {
    int ty = fl((ny + PH - 0.01f) / TS);
    for (int tx = tx0; tx <= tx1; tx++) {
      char c = tileAt(tx, ty);
      if (solidT(tx, ty) || (c == '=' && pl.y + PH <= ty * TS + 0.01f)) { ny = ty * TS - PH; pl.vy = 0; pl.ground = true; break; }
    }
  } else if (dy < 0) {
    int ty = fl(ny / TS);
    for (int tx = tx0; tx <= tx1; tx++) if (solidT(tx, ty)) { ny = (ty + 1) * TS; pl.vy = 0; break; }
  }
  pl.y = ny;
}

// something worth a shrimp ahead? Tigo aims at foes inside a forward cone
// he can see; with a Jumbo shot ready he also aims at cracked coral.
static bool lineClear(float x0, float y0, float x1, float y1) {
  float dx = x1 - x0, dy = y1 - y0;
  int n = (int)(max(fabsf(dx), fabsf(dy)) / 5) + 1;
  for (int i = 1; i < n; i++) if (solidPx(x0 + dx * i / n, y0 + dy * i / n)) return false;
  return true;
}
static bool inCone(float dx, float dy, float hh) { return dx > 2 && dx < 108 && fabsf(dy) < dx * 0.7f + hh; }
static bool findTarget(bool jumbo, float &tx, float &ty) {
  float cx = pl.x + PW / 2, cy = pl.y + PH / 2, best = 1e9f;
  for (int i = 0; i < MAX_ENTS; i++) {
    const Ent &e = ents[i];
    if (!e.on || !EINFO[e.type].shootable) continue;
    if (e.type == E_EEL && e.st == 0) continue;
    float dx = (e.x - cx) * pl.face, dy = e.y - cy;
    if (!inCone(dx, dy, EINFO[e.type].hh) || dx + fabsf(dy) >= best || !lineClear(cx, cy, e.x, e.y)) continue;
    best = dx + fabsf(dy); tx = e.x; ty = e.y;
  }
  if (boss.on && bossVulnerable()) {
    float hw, hh;
    bossBox(hw, hh);
    float dx = (boss.x - cx) * pl.face + hw * 0.5f, dy = boss.y - cy;
    if (inCone(dx, dy, hh) && dx < 140 && dx + fabsf(dy) < best) { best = dx; tx = boss.x; ty = boss.y; }
  }
  if (best < 1e8f) return true;
  if (jumbo) {
    for (int k = 0; k < 5; k++) {
      int bx = fl((cx + pl.face * (6 + k * TS)) / TS);
      for (int dyk = -1; dyk <= 1; dyk++) {
        int by = fl((cy + dyk * 4) / TS);
        if (tileAt(bx, by) == 'C') { tx = bx * TS + 4; ty = cy; return true; }
      }
      if (tileAt(bx, fl(cy / TS)) == '#') break;
    }
  }
  return false;
}

static void collectItem(Item &it) {
  if (it.kind == 'o') {
    burst(it.x, it.y, 8, 0xFFFF, 1.2f);
    toast = "";
  } else if (it.kind == 'H') {
    if (sv.maxHp < 12) sv.maxHp++;
    pl.hp = sv.maxHp;
    showBanner("HEART SHELL", "One more heart", "", 0xFB2C, false);
  } else if (it.kind == 'P') {
    if (sv.power < 4) sv.power++;
    showBanner("POWER SHELL", "Shrimps hit harder", "", 0xFE60, false);
  }
  sv.items[room] |= 1 << it.bit;
  it.on = false;
  hudPearls = -1;
  if (it.kind != 'o') writeSave();
}

static void useAltar(Item &it) {
  if (it.kind == 'S') {
    sv.room = (uint8_t)room;
    sv.ax = (uint8_t)(it.x / TS);
    sv.ay = (uint8_t)((it.y - 1) / TS);
    pl.hp = sv.maxHp;
    writeSave();
    burst(it.x, it.y - 8, 16, 0xAFFF, 1.6f);
    bubbles(it.x, it.y - 8, 6);
    // no pause: a passing touch saves and heals, the map is one long hold away
    toast = "SAVED";
    toastT = 110;
  } else if (it.kind == 'A' && it.on) {
    int ab = ROOMS[room].abil;
    sv.abil |= ab;
    it.on = false;
    writeSave();
    int i = abilIndex(ab);
    burst(it.x, it.y - 10, 30, ABIL_COL[i], 2.4f);
    shake(10);
    showBanner(ABIL_NAME[i], ABIL_HINT1[i], ABIL_HINT2[i], ABIL_COL[i], false);
  }
}

static void bossDefeated() {
  Boss &b = boss;
  b.on = false;
  bossGate = false;
  sv.bosses |= 1 << b.kind;
  shake(24);
  for (int k = 0; k < 4; k++) burst(b.x + frand(-12, 12), b.y + frand(-12, 12), 18, k & 1 ? 0xFFE0 : 0xFFFF, 2.8f);
  bubbles(b.x, b.y, 16);
  for (int i = 0; i < MAX_SHOTS; i++) if (shots[i].kind >= SH_ORB) shots[i].on = false;
  int ab = BOSS_ABIL[b.kind];
  if (ab) {
    sv.abil |= ab;
    int i = abilIndex(ab);
    writeSave();
    showBanner(ABIL_NAME[i], ABIL_HINT1[i], ABIL_HINT2[i], ABIL_COL[i], false);
  } else {
    writeSave();
    showBanner("THE ABYSS SLEEPS", "The citadel is calm", "Tigo is a legend!", 0xFFE0, true);
  }
}

static void damageBoss(int dmg) {
  if (boss.kind == 2 && boss.st == 4) dmg *= 2;    // stunned Warden takes double
  boss.hp -= dmg;
  boss.hit = 6;
  if (boss.hp <= 0) bossDefeated();
}

// ================= one tick of play =================
struct Input { bool hl, hr, pl, pr, rl, rr; };
static int32_t pressT[2] = {-100, -100}, tapEnd[2] = {-100, -100};

static void enterRoomAt(float wx, float wy);
static void playerTick(const Input &in) {
  Player &p = pl;
  if (p.deadT) {
    if (++p.deadT > 80) {
      state = OVER;
      armed = false;
      if (sv.deaths < 60000) sv.deaths++;
      newHigh = false;
      writeSave();
      lastInput = millis();
    }
    return;
  }
  int dir = (in.hr && !in.hl) ? 1 : (in.hl && !in.hr) ? -1 : 0;
  bool both = in.hl && in.hr;
  bool chord = (in.pl && in.hr) || (in.pr && in.hl);
  // taps (for the dash): a press that ended quickly
  for (int s = 0; s < 2; s++) {
    bool pr = s ? in.pr : in.pl, rl = s ? in.rr : in.rl;
    if (pr) pressT[s] = (int32_t)tickN;
    if (rl && (int32_t)tickN - pressT[s] < TAP_T) tapEnd[s] = (int32_t)tickN;
  }
  int dashReq = 0;
  if (has(AB_DASH)) {
    if (in.pl && !in.hr && (int32_t)tickN - tapEnd[0] < TAP_T) dashReq = -1;
    if (in.pr && !in.hl && (int32_t)tickN - tapEnd[1] < TAP_T) dashReq = 1;
  }
  if (chord) p.buffer = BUFFER_T;
  else if (p.buffer) p.buffer--;
  if (p.wallLock) p.wallLock--;
  if (p.inv) p.inv--;
  if (p.shootAnim) p.shootAnim--;
  p.anim++;

  bool lava = false;
  bool spike = hazardUnder(lava);
  float vyBefore = p.vy;
  bool wasGround = p.ground;

  if (p.dashT > 0) {
    p.dashT--;
    p.vx = p.dashDir * DASH_V;
    p.vy = 0;
    if (p.dashT == 0) p.vx = p.dashDir * RUN_MAX;
    if (p.anim % 2 == 0) addPart(p.x + PW / 2 - p.dashDir * 6, p.y + PH / 2 + frand(-3, 3), -p.dashDir * 0.4f, 0, 0x7FFF, 14, PK_GLINT);
  } else {
    // walking
    if (p.wallLock == 0) {
      float acc = p.ground ? RUN_ACC : AIR_ACC;
      if (!p.ground && dir * p.vx < 0) acc = 0.30f;   // quick turnarounds in the water
      if (dir) {
        p.vx += dir * acc;
        if (p.vx > RUN_MAX) p.vx = RUN_MAX;
        if (p.vx < -RUN_MAX) p.vx = -RUN_MAX;
        p.face = (int8_t)dir;
      } else if (p.ground && !chord) {              // (a jump keeps the run-up speed)
        if (p.vx > RUN_DEC) p.vx -= RUN_DEC;
        else if (p.vx < -RUN_DEC) p.vx += RUN_DEC;
        else p.vx = 0;
      } else if (!both) p.vx *= 0.96f;
    }
    // jumps: ground (with coyote time and a buffer), wall kick, bubble jump
    if (p.buffer) {
      bool nearFloor = p.vy > 0 && (standPx(p.x + 1, p.y + PH + 10) || standPx(p.x + PW - 1, p.y + PH + 10));
      if (p.ground || p.coyote > 0) {
        p.vy = -JUMP_V; p.ground = false; p.coyote = 0; p.buffer = 0;
        bubbles(p.x + PW / 2, p.y + PH, 3);
      } else if (p.wallDir && has(AB_CLING)) {
        p.vy = -WALLJ_VY; p.vx = -p.wallDir * WALLJ_VX; p.face = (int8_t)-p.wallDir;
        p.wallLock = 5; p.wallDir = 0; p.buffer = 0;
        burst(p.x + PW / 2 - p.face * 5, p.y + PH / 2, 6, 0xFFFF, 1.0f);
      } else if (chord && has(AB_BUBBLE) && p.airJumps > 0 && !nearFloor) {
        p.vy = -BUBBLE_V; p.airJumps--; p.buffer = 0;
        for (int i = 0; i < 8; i++) {
          float a = i * 0.785f;
          addPart(p.x + PW / 2 + cosf(a) * 5, p.y + PH + sinf(a) * 2, cosf(a) * 0.8f, sinf(a) * 0.3f + 0.2f, 0xBFFF, 24, PK_BUBBLE);
        }
      }
    }
    if (dashReq && !p.dashUsed) {
      p.dashT = DASH_T; p.dashDir = (int8_t)dashReq; p.face = (int8_t)dashReq;
      if (!p.ground) p.dashUsed = true;
      shake(3);
      bubbles(p.x + PW / 2, p.y + PH / 2, 4);
    }
    // gravity (lighter while both buttons hold the jump up)
    p.vy += (p.vy < 0 && both) ? GRAV_HOLD : GRAV;
    float fallMax = FALL_MAX;
    p.wallDir = 0;
    if (!p.ground && has(AB_CLING) && dir && touchingWall(dir) && p.vy > -0.5f) {
      p.wallDir = (int8_t)dir;
      fallMax = CLING_FALL;
      p.airJumps = has(AB_BUBBLE) ? 1 : 0;
      p.dashUsed = false;
      if (p.anim % 8 == 0) addPart(p.x + (dir > 0 ? PW : 0), p.y + PH, 0, 0.3f, 0xC618, 12, PK_DUST);
    }
    if (lava) fallMax = 1.0f;
    if (p.vy > fallMax) p.vy = fallMax;
  }

  moveX(p.vx);
  moveY(p.vy);
  if (p.ground) {
    p.coyote = COYOTE_T;
    p.airJumps = has(AB_BUBBLE) ? 1 : 0;
    p.dashUsed = false;
    if (!wasGround && vyBefore > 1.6f) for (int i = 0; i < 4; i++) addPart(p.x + PW / 2, p.y + PH, frand(-0.8f, 0.8f), -0.3f, 0xBDF7, 16, PK_DUST);
  } else if (p.coyote) p.coyote--;

  // hazards: spikes always hurt, lava unless the Heat Shell is on; both put
  // Tigo back on the last safe ground he stood on
  if ((spike || (lava && !has(AB_HEAT))) && p.inv == 0 && !p.dashT) {
    hurtPlayer(1, p.x + PW / 2 - p.face);
    if (!p.deadT) { p.x = p.safeX; p.y = p.safeY; p.vx = p.vy = 0; fadeT = 6; bubbles(p.x + PW / 2, p.y + PH / 2, 6); }
  }
  if (lava && has(AB_HEAT) && p.anim % 6 == 0) addPart(p.x + frand(0, PW), p.y + PH, 0, -0.5f, 0xFD20, 18, PK_SPARK);
  if (p.ground && !spike && !lava) {
    bool l = false;
    float ox = p.x, oy = p.y;
    bool safe = true;
    for (int k = -1; k <= 1 && safe; k++) {
      p.x = ox + k * TS;
      if (hazardUnder(l) || l) safe = false;
      if (!standPx(p.x + 1, oy + PH + 1) || !standPx(p.x + PW - 1, oy + PH + 1)) safe = false;
    }
    p.x = ox; p.y = oy;
    if (safe) { p.safeX = p.x; p.safeY = p.y; }
  }

  // map: both held while standing still
  if (both && p.ground && fabsf(p.vx) < 0.2f) {
    if (++p.bothT > MAP_HOLD_T) { mode = M_MAP; bannerT = 0; p.bothT = 0; }
  } else p.bothT = 0;

  // shooting
  if (p.shootCd) p.shootCd--;
  if (has(AB_JUMBO) && p.charge < CHARGE_T) p.charge++;
  bool jumboReady = has(AB_JUMBO) && p.charge >= CHARGE_T;
  float tgx = 0, tgy = 0;
  if (!p.dashT && p.shootCd == 0 && p.wallDir == 0 && findTarget(jumboReady, tgx, tgy)) {
    float sx = p.x + PW / 2 + p.face * 9, sy = p.y + PH / 2 + 1;
    float dx = tgx - sx, dy = tgy - sy, d = sqrtf(dx * dx + dy * dy);
    if (d < 1 || dx * p.face <= 0) { dx = p.face; dy = 0; d = 1; }
    int dmg = 2 + sv.power;
    if (jumboReady) { fireShot(SH_JUMBO, sx, sy, dx / d * 3.4f, dy / d * 3.4f, 6 + sv.power * 2, 44); p.charge = 0; shake(3); }
    else fireShot(SH_SHRIMP, sx, sy, dx / d * 3.8f, dy / d * 3.8f, dmg, 36);
    p.shootCd = SHOOT_CD;
    p.shootAnim = 8;
  }

  // items and altars
  float cx = p.x + PW / 2, cy = p.y + PH / 2;
  int touch = -1;
  for (int i = 0; i < nItems; i++) {
    Item &it = items[i];
    if (it.kind == 'S' || it.kind == 'A') {
      if (fabsf(it.x - cx) < 9 && fabsf(it.y - 8 - cy) < 12) touch = i;
      continue;
    }
    if (it.on && fabsf(it.x - cx) < 8 && fabsf(it.y - cy) < 9) collectItem(it);
  }
  if (touch >= 0 && touch != altarTouch) useAltar(items[touch]);
  altarTouch = touch;
  // X walls show themselves once Tigo is inside one
  if (!xRevealed && tileAt(fl(cx / TS), fl(cy / TS)) == 'X') { xRevealed = true; burst(cx, cy, 10, 0xFFE0, 1.4f); }

  // room exits: Tigo's centre left the room
  if (cx < 0 || cy < 0 || cx >= roomW * TS || cy >= roomH * TS) {
    const RoomDef &d = ROOMS[room];
    enterRoomAt(d.cx * CELL * TS + cx, d.cy * CELL * TS + cy);
  }
}

static void enterRoomAt(float wx, float wy) {
  int r = roomAtCell(fdiv(fl(wx), CELL * TS), fdiv(fl(wy), CELL * TS));
  if (r < 0) {                                       // no room there (map error): stay inside
    pl.x = constrain(pl.x, 0.0f, roomW * TS - PW - 0.0f);
    pl.y = constrain(pl.y, 0.0f, roomH * TS - PH - 0.0f);
    return;
  }
  float vx = pl.vx, vy = pl.vy;
  loadRoom(r);
  const RoomDef &d = ROOMS[r];
  pl.x = wx - d.cx * CELL * TS - PW / 2;
  pl.y = wy - d.cy * CELL * TS - PH / 2;
  pl.vx = vx; pl.vy = vy;
  pl.safeX = pl.x; pl.safeY = pl.y;
  // climbing up through a floor hole: a little boost so Tigo makes the ledge
  if (vy < 0 && pl.vy > -2.6f) pl.vy = -2.6f;
  camX = pl.x + PW / 2 - fb.w / 2.0f;
  camY = pl.y + PH / 2 - fb.h / 2.0f;
  fadeT = 8;
  writeSave();
}

static void tickShots() {
  float cx = pl.x + PW / 2, cy = pl.y + PH / 2;
  for (int i = 0; i < MAX_SHOTS; i++) {
    Shot &s = shots[i];
    if (!s.on) continue;
    s.x += s.vx; s.y += s.vy;
    if (s.kind == SH_SEED) s.vy += 0.02f;
    if (!s.life || --s.life == 0) { s.on = false; continue; }
    bool friendly = s.kind <= SH_JUMBO;
    if (friendly && (tickN & 1)) addPart(s.x - s.vx, s.y, 0, 0, s.kind == SH_JUMBO ? 0xFE19 : 0xFB96, 10, PK_GLINT);
    int tx = fl(s.x / TS), ty = fl(s.y / TS);
    if (solidT(tx, ty)) {
      if (s.kind == SH_JUMBO && tileAt(tx, ty) == 'C') {
        // crack the whole connected block of coral
        int stack[48], n = 0;
        stack[n++] = (tx + MARGIN) | ((ty + MARGIN) << 8);
        while (n) {
          int v = stack[--n], x = (v & 255) - MARGIN, y = (v >> 8) - MARGIN;
          if (tileAt(x, y) != 'C') continue;
          setTile(x, y, '.');
          burst(x * TS + 4, y * TS + 4, 6, BIOMES[biome].accent2, 1.6f);
          static const int8_t D4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
          for (int k = 0; k < 4 && n < 44; k++) if (tileAt(x + D4[k][0], y + D4[k][1]) == 'C') stack[n++] = (x + D4[k][0] + MARGIN) | ((y + D4[k][1] + MARGIN) << 8);
        }
        sv.opened |= 1UL << room;
        writeSave();
        shake(10);
      }
      burst(s.x - s.vx, s.y, friendly ? 5 : 3, friendly ? 0xFDB6 : 0xFC00, 1.0f);
      s.on = false;
      continue;
    }
    if (friendly) {
      for (int k = 0; k < MAX_ENTS && s.on; k++) {
        Ent &e = ents[k];
        if (!e.on || !EINFO[e.type].shootable || (e.type == E_EEL && e.st == 0)) continue;
        const EInfo &in = EINFO[e.type];
        if (fabsf(e.x - s.x) < in.hw + 2 && fabsf(e.y - s.y) < in.hh + 2) {
          if (e.hit > 0 && s.kind == SH_JUMBO) continue;
          e.hp -= s.dmg;
          e.hit = 8;
          e.x += s.vx > 0 ? 1.5f : -1.5f;
          burst(s.x, s.y, 4, 0xFFFF, 1.0f);
          if (e.hp <= 0) killEnt(e);
          if (s.kind == SH_SHRIMP) s.on = false;
        }
      }
      if (s.on && boss.on) {
        // the Magma Heart's orbiting rocks soak shots
        if (boss.kind == 4) {
          for (int r = 0; r < 4 && s.on; r++) {
            if (boss.rockHp[r] <= 0) continue;
            float a = boss.a + r * 1.5708f, rx = boss.x + cosf(a) * 27, ry = boss.y + sinf(a) * 27;
            if (fabsf(rx - s.x) < 6 && fabsf(ry - s.y) < 6) {
              if (--boss.rockHp[r] <= 0) { boss.rockT[r] = 420; burst(rx, ry, 12, 0xFC00, 1.8f); }
              burst(s.x, s.y, 4, 0xFD20, 1.0f);
              s.on = false;
            }
          }
        }
        float hw, hh;
        bossBox(hw, hh);
        if (s.on && bossVulnerable() && fabsf(boss.x - s.x) < hw + 2 && fabsf(boss.y - s.y) < hh + 2) {
          burst(s.x, s.y, 5, 0xFFFF, 1.2f);
          s.on = false;
          damageBoss(s.dmg);
        }
      }
    } else if (fabsf(s.x - cx) < 5 + PW / 2 - 2 && fabsf(s.y - cy) < 4 + PH / 2 - 2) {
      hurtPlayer(1, s.x);
      s.on = false;
    }
  }
}

static void tickPlay(const Input &in) {
  tickN++;
  if (shakeT) shakeT--;
  if (fadeT) fadeT--;
  if (flashT) flashT--;
  if (toastT) toastT--;
  if (mode == M_BANNER) {
    // paused: wait a moment, then any press continues
    bannerT++;
    if (bannerT > 45 && (in.pl || in.pr)) {
      mode = M_RUN;
      if (bannerEnds) { state = END; armed = false; endT = 0; lastInput = millis(); }
    }
    tickParts();
    return;
  }
  if (mode == M_MAP) {
    bannerT++;
    if (!in.hl || !in.hr) { mode = M_RUN; pl.bothT = 0; pl.buffer = 0; }
    else if (bannerT >= QUIT_HOLD_T) quitReq = true;   // still holding: back to the menu
    return;
  }
  playerTick(in);
  if (state != PLAY || mode != M_RUN) return;
  for (int i = 0; i < MAX_ENTS; i++) {
    Ent &e = ents[i];
    if (!e.on) continue;
    tickEnt(e);
    if (!pl.deadT && foeHitsPlayer(e)) hurtPlayer(1, e.x);
  }
  if (boss.on) {
    tickBoss();
    if (boss.on && !pl.deadT && bossTouches(pl.x + PW / 2, pl.y + PH / 2, 4)) hurtPlayer(1, boss.x);
  }
  tickShots();
  tickParts();
  // ambient: vents, drifting bubbles, embers
  for (int v = 0; v < nVents; v++) if ((tickN + v * 7) % 18 == 0) bubbles(vents[v * 2], vents[v * 2 + 1], 1);
  if (rnd(100) < (biome == BI_LAVA ? 10 : 4)) {
    float x = camX + rnd(fb.w), y = camY + fb.h + 2;
    if (biome == BI_LAVA) addPart(x, y - rnd(40), frand(-0.2f, 0.2f), frand(-0.8f, -0.3f), rnd(2) ? 0xFC60 : 0xFEA0, 90, PK_SPARK);
    else addPart(x, y - rnd(60), 0, frand(-0.7f, -0.3f), 0xBEFF, 120, PK_BUBBLE);
  }
  // camera: look ahead of Tigo, ease towards it, stay inside the room
  float look = (fb.w > fb.h ? 34 : 18) * pl.face;
  float tx = pl.x + PW / 2 + look - fb.w / 2.0f;
  float ty = pl.y + PH / 2 - fb.h / 2.0f + (pl.vy > 2 ? 18 : 0) - (fb.h > fb.w ? 16 : 6);
  camX += (tx - camX) * 0.10f;
  camY += (ty - camY) * (pl.vy > 2 ? 0.16f : 0.09f);
  float rw = roomW * TS, rh = roomH * TS;
  camX = rw <= fb.w ? (rw - fb.w) / 2 : constrain(camX, 0.0f, rw - fb.w);
  camY = rh <= fb.h ? (rh - fb.h) / 2 : constrain(camY, 0.0f, rh - fb.h);
}

// ================= drawing: world =================
static int icx = 0, icy = 0;                       // integer camera (with shake)
static inline int SX(float x) { return (int)floorf(x) - icx; }
static inline int SY(float y) { return (int)floorf(y) - icy; }

static void blitTile(const uint16_t *a, int sx, int sy) {
  int x0 = 0, x1 = 8, y0 = 0, y1 = 8;
  if (sx < 0) x0 = -sx;
  if (sx + 8 > fb.w) x1 = fb.w - sx;
  if (sy < 0) y0 = -sy;
  if (sy + 8 > fb.h) y1 = fb.h - sy;
  if (x0 >= x1 || y0 >= y1) return;
  for (int j = y0; j < y1; j++) {
    uint16_t *p = fbAt(sx + x0, sy + j);
    const uint16_t *s = a + j * 8 + x0;
    for (int i = x0; i < x1; i++, p++, s++) if (*s != KEY) *p = *s;
  }
}
static void blitTileDither(const uint16_t *a, int sx, int sy, int ph) {   // revealed secret walls
  for (int j = 0; j < 8; j++)
    for (int i = 0; i < 8; i++) if (((i + j + ph) & 1) == 0 && a[j * 8 + i] != KEY) pset(sx + i, sy + j, a[j * 8 + i]);
}

static inline bool rockish(char c) { return c == '#' || c == 'X' || c == 'C'; }

static void drawLavaTile(int tx, int ty, int sx, int sy, uint32_t t) {
  const BiomeDef &b = BIOMES[BI_LAVA];
  bool top = tileAt(tx, ty - 1) != 'L';
  uint16_t deep = sw16(RGB_(200, 40, 10)), mid = sw16(RGB_(250, 100, 20)), hot = sw16(b.accent2);
  for (int i = 0; i < 8; i++) {
    int x = sx + i;
    if ((unsigned)x >= (unsigned)fb.w) continue;
    int wx = tx * 8 + i;
    int surf = top ? 1 + (isin(wx * 14 + (int)(t >> 2)) + 127) * 5 / 254 : 0;   // 1..3 px of wave
    for (int j = 0; j < 8; j++) {
      int y = sy + j;
      if ((unsigned)y >= (unsigned)fb.h || j < surf) continue;
      uint16_t c = (j == surf && top) ? hot : (((wx + (ty * 8 + j) * 2 + (t >> 6)) % 7) < 2 ? mid : deep);
      *fbAt(x, y) = c;
    }
  }
}

static void drawDecor(char c, int tx, int ty, int sx, int sy, uint32_t t) {
  const BiomeDef &b = BIOMES[biome];
  uint32_t h = hash2(tx, ty + room * 64);
  int bx = sx + 4, by = sy + 8;                    // bottom centre of the tile
  switch (c) {
    case 'k': {                                    // kelp: a swaying strand with leaves
      int len = 20 + (h % 18);
      uint16_t c1 = sw16(mix565(b.rim, b.edge, 10)), c2 = sw16(b.rimHi), c0 = sw16(mix565(b.rock, b.edge, 16));
      int ph = (int)(h & 255), tp = (int)(t >> 4) * 3;
      for (int k = 0; k < len; k++) {
        int sway = isin(k * 7 + tp + ph) * k / 1060;   // grows towards the tip
        int x = bx + sway, y = by - k;
        pset(x - 1, y, c0);
        pset(x, y, c1);
        if (k % 6 == 3) { pset(x + 1, y, c1); pset(x + 2, y - 1, c2); }
        if (k % 6 == 0 && k) { pset(x - 2, y, c1); pset(x - 3, y - 1, c1); }
      }
      break;
    }
    case 'y': {                                    // coral fan: a few branches with bright tips
      uint16_t c1 = sw16((h & 1) ? b.accent : b.rim), c2 = sw16(b.rimHi), c0 = sw16(mix565(b.rim, b.edge, 14));
      int n = 3 + (h % 3);
      for (int k = 0; k < n; k++) {
        int ex = bx + (k - n / 2) * 3 + ((h >> (k + 3)) & 1), ey = by - 6 - ((h >> (k * 2)) & 3) - (k == n / 2 ? 3 : 0);
        line(bx, by - 1, ex, ey, c0);
        line(bx + 1, by - 1, ex + 1, ey, c1);
        pset(ex, ey - 1, c2);
        pset(ex + 1, ey - 1, c2);
      }
      break;
    }
    case 'g': {                                    // glowing bulb
      int r = 2, pulse = (int)(sinf(t * 0.004f + (h & 63)) * 6 + 12);
      int gy = sy + 4 + (int)(sinf(t * 0.002f + h) * 1.5f);
      glow(bx, gy, 9, b.accent2, pulse);
      circF(bx, gy, r, sw16(mix565(b.accent2, 0xFFFF, 14)));
      pset(bx - 1, gy - 1, 0xFFFF);
      break;
    }
    case 'i': {                                    // ruin column (background)
      int hgt = 18 + (h % 22);
      uint16_t c1 = sw16(mix565(b.rock, b.water1, 12)), c2 = sw16(mix565(b.rock2, b.water1, 8)), c0 = sw16(mix565(b.edge, b.water1, 8));
      rectF(bx - 3, by - hgt, 7, hgt, c1);
      for (int y = by - hgt; y < by; y++) { pset(bx - 3, y, c2); pset(bx + 3, y, c0); if (((y - by) & 7) == 0) hspan(bx - 3, bx + 3, y, c0); }
      rectF(bx - 4, by - hgt - 2, 9, 2, c2);
      rectF(bx - 4, by - 2, 9, 2, c2);
      break;
    }
    case 'b': {                                    // bubble vent
      uint16_t c1 = sw16(b.rock2), c2 = sw16(b.edge);
      hspan(bx - 3, bx + 3, by - 1, c1);
      hspan(bx - 2, bx + 2, by - 2, c1);
      pset(bx, by - 2, c2);
      break;
    }
  }
}

static void drawTiles(uint32_t t) {
  int tx0 = fdiv(icx, TS), ty0 = fdiv(icy, TS);
  int tx1 = fdiv(icx + fb.w - 1, TS), ty1 = fdiv(icy + fb.h - 1, TS);
  // decor first (it sits behind the rock), grown from the tile bottom upwards
  for (int ty = ty0; ty <= ty1 + 4; ty++)
    for (int tx = tx0 - 1; tx <= tx1 + 1; tx++) {
      char c = tileAt(tx, ty);
      if (c == 'k' || c == 'y' || c == 'g' || c == 'i' || c == 'b') drawDecor(c, tx, ty, tx * TS - icx, ty * TS - icy, t);
    }
  for (int ty = ty0; ty <= ty1; ty++) {
    int sy = ty * TS - icy;
    for (int tx = tx0; tx <= tx1; tx++) {
      char c = tileAt(tx, ty);
      if (c == '.' || c == 'k' || c == 'y' || c == 'g' || c == 'i' || c == 'b') continue;
      int sx = tx * TS - icx;
      if (c == '#' || c == 'X') {
        int m = (!rockish(tileAt(tx, ty - 1)) ? 1 : 0) | (!rockish(tileAt(tx + 1, ty)) ? 2 : 0) |
                (!rockish(tileAt(tx, ty + 1)) ? 4 : 0) | (!rockish(tileAt(tx - 1, ty)) ? 8 : 0);
        uint32_t hv = hash2(tx, ty) & 1;
        const uint16_t *a = tileArt[m + 16 * hv];
        if (m == 0 && rockish(tileAt(tx, ty - 2)) && rockish(tileAt(tx, ty + 2)) && rockish(tileAt(tx - 2, ty)) &&
            rockish(tileAt(tx + 2, ty)) && rockish(tileAt(tx - 1, ty - 1)) && rockish(tileAt(tx + 1, ty + 1)))
          a = tileArt[TA_DEEP + hv];
        if (c == 'X' && xRevealed) blitTileDither(a, sx, sy, (int)(t >> 7));
        else blitTile(a, sx, sy);
        if ((m & 1) && tileAt(tx, ty - 1) != '=') blitTile(tileArt[TA_FRINGE + (hash2(ty, tx) & 1)], sx, sy - 8);
      } else if (c == '=') blitTile(tileArt[TA_PLAT], sx, sy);
      else if (c == '^') blitTile(tileArt[TA_SPIKE], sx, sy);
      else if (c == 'v') blitTile(tileArt[TA_SPIKE_DN], sx, sy);
      else if (c == 'C') {
        blitTile(tileArt[TA_CRACK], sx, sy);
        if (((t >> 4) + tx * 3 + ty) % 40 == 0) addPart(tx * TS + rnd(8), ty * TS + rnd(8), 0, -0.2f, BIOMES[biome].accent2, 14, PK_GLINT);
      } else if (c == 'L') drawLavaTile(tx, ty, sx, sy, t);
    }
  }
  // lava light: a warm band rising off every lava surface
  if (biome == BI_LAVA)
    for (int ty = ty0; ty <= ty1 + 1; ty++)
      for (int tx = tx0; tx <= tx1; tx++)
        if (tileAt(tx, ty) == 'L' && tileAt(tx, ty - 1) != 'L') {
          int sx = tx * TS - icx, sy = ty * TS - icy;
          bool le = tileAt(tx - 1, ty) != 'L', re = tileAt(tx + 1, ty) != 'L';
          for (int i = 0; i < 8; i++) {
            int wx = tx * TS + i;
            int hgt = 7 + (isin(wx * 9 + (int)(t >> 3)) >> 5);            // flickering, wavy top
            if (le && i < 4) hgt -= (4 - i) * 2;                         // softer at the pool ends
            if (re && i > 3) hgt -= (i - 3) * 2;
            for (int j = 1; j <= hgt; j++) addSpan(sx + i, sx + i, sy - j, 0xFA00, (hgt - j + 1) * 12 / hgt);
          }
        }
}

// boss doors: bars across every opening while a guardian is awake
static void drawGates(uint32_t t) {
  if (!bossGate) return;
  uint16_t bar = sw16(0xC618), dark = sw16(0x4208), glowc = BIOMES[biome].accent2;
  for (int ty = -1; ty <= roomH; ty++)
    for (int tx = -1; tx <= roomW; tx++) {
      if (inRoom(tx, ty)) continue;
      char c = tileAt(tx, ty);
      if (c == '#' || c == 'C') continue;
      if (!(inRoom(tx + 1, ty) || inRoom(tx - 1, ty) || inRoom(tx, ty + 1) || inRoom(tx, ty - 1))) continue;
      int sx = tx * TS - icx, sy = ty * TS - icy;
      rectF(sx, sy, 8, 8, dark);
      for (int k = 1; k < 8; k += 3) { rectF(sx + k, sy, 2, 8, bar); }
      if ((t >> 8) & 1) glow(sx + 4, sy + 4, 6, glowc, 6);
    }
}

// ================= drawing: things =================
static uint16_t itemPal[16];
static void setItemPal() {
  // slots follow ITEM_CHARS: o O w W p P y Y r R g G k K
  static const uint16_t P[14] = {0x38E7, 0x38E7, 0xFFFF, 0xFFFF, 0xC8A9, 0xFB96, 0xC400, 0xFEA0, 0xF8A6, 0xFB2C,
                                 0x07E0, 0x07E0, 0x0000, 0x0000};
  for (int i = 0; i < 14; i++) itemPal[i + 1] = sw16(P[i]);
}

static void drawItems(uint32_t t) {
  artInit(ITEM_CHARS);
  for (int i = 0; i < nItems; i++) {
    const Item &it = items[i];
    int x = SX(it.x), y = SY(it.y);
    if (x < -20 || x > fb.w + 20 || y < -30 || y > fb.h + 20) continue;
    float bob = sinf(t * 0.004f + i) * 1.5f;
    if (it.kind == 'o' && it.on) {
      int yy = y + (int)bob;
      glow(x, yy, 6, 0xBFFF, 10);
      circF(x, yy, 2, sw16(0xF79E));
      pset(x - 1, yy - 1, 0xFFFF);
      if (((t >> 5) + i * 7) % 24 == 0) pset(x + 2, yy - 3, 0xFFFF);
    } else if (it.kind == 'H' && it.on) {
      int yy = y + (int)bob;
      glow(x, yy, 11, 0xF8B2, 16);
      blitArt(ART_HEART, 9, 8, x - 4, yy - 4, false, itemPal);
    } else if (it.kind == 'P' && it.on) {
      int yy = y + (int)bob;
      glow(x, yy, 11, 0xFE60, 16);
      blitArt(ART_POWER, 9, 9, x - 4, yy - 4, false, itemPal);
    } else if (it.kind == 'S') {
      // save altar: an open giant clam holding a glowing pearl
      bool cur = sv.room == room && sv.ax == it.x / TS && sv.ay == (it.y - 1) / TS;
      uint16_t sh = sw16(0xFE19), shd = sw16(0xC48F), ol = sw16(0x38E7);
      ellF(x, y - 3, 8, 3, ol);
      ellF(x, y - 3, 7, 2, shd);
      for (int k = -6; k <= 6; k += 3) line(x, y - 2, x + k, y - 5, sh);
      ellF(x, y - 11, 8, 4, ol);
      ellF(x, y - 11, 7, 3, sh);
      for (int k = -6; k <= 6; k += 3) line(x, y - 9, x + k, y - 13, shd);
      int pulse = (int)(sinf(t * 0.005f) * 6) + (cur ? 22 : 12);
      glow(x, y - 6, cur ? 16 : 11, 0x9FFF, pulse);
      circF(x, y - 6, 2, sw16(0xEFFF));
      pset(x - 1, y - 7, 0xFFFF);
    } else if (it.kind == 'A') {
      // ability altar: a stone pedestal; the power floats above it until taken
      uint16_t st = sw16(0x8C71), sd = sw16(0x4A49), sl = sw16(0xCE79);
      rectF(x - 6, y - 4, 13, 4, sd);
      rectF(x - 4, y - 10, 9, 6, st);
      hspan(x - 4, x + 4, y - 10, sl);
      rectF(x - 6, y - 12, 13, 2, sl);
      if (it.on) {
        int ai = abilIndex(ROOMS[room].abil);
        int yy = y - 20 + (int)(bob * 1.5f);
        glow(x, yy, 14, ABIL_COL[ai], 22);
        circF(x, yy, 4, sw16(mix565(ABIL_COL[ai], 0xFFFF, 10)));
        for (int k = 0; k < 3; k++) {
          float a = t * 0.004f + k * 2.094f;
          pset(x + (int)(cosf(a) * 8), yy + (int)(sinf(a) * 3), 0xFFFF);
        }
      }
    }
  }
}

static uint16_t hitCol(const Ent &e, uint16_t c) { return e.hit > 0 && (e.hit & 2) ? 0xFFFF : c; }

static void drawEnt(const Ent &e, uint32_t t) {
  int x = SX(e.x), y = SY(e.y);
  if (x < -40 || x > fb.w + 40 || y < -30 || y > fb.h + 30) return;
  int f = e.t;
  uint16_t ol = sw16(0x2104);
  switch (e.type) {
    case E_CRAB: case E_FSHRIMP: {
      bool fire = e.type == E_FSHRIMP;
      uint16_t body = sw16(hitCol(e, fire ? 0xF9A4 : 0xFB20)), dark = sw16(fire ? 0xA0A2 : 0xB1A0), hi = sw16(fire ? 0xFDB6 : 0xFDE0);
      int leg = (f >> 3) & 1;
      for (int k = -1; k <= 1; k++) {                // legs
        line(x + k * 3, y + 1, x + k * 4 - 2 + leg * 2 * (k & 1 ? 1 : -1), y + 4, dark);
      }
      ellF(x, y, 7, 4, ol);
      ellF(x, y, 6, 3, body);
      hspan(x - 3, x + 3, y - 2, hi);
      if (fire) {
        line(x + e.dir * 5, y - 2, x + e.dir * 10, y - 8, sw16(0xFE60));
        line(x + e.dir * 4, y - 3, x + e.dir * 8, y - 9, sw16(0xFE60));
        if (e.st > 30) glow(x + e.dir * 7, y - 3, 6, 0xFC00, 14);
      } else {
        int up = ((f >> 4) & 1);
        circF(x - 7, y - 3 - up, 2, body); circF(x + 7, y - 3 - (1 - up), 2, body);
      }
      pset(x - 2, y - 5, 0xFFFF); pset(x + 2, y - 5, 0xFFFF);
      pset(x - 2, y - 4, 0); pset(x + 2, y - 4, 0);
      break;
    }
    case E_URCHIN: {
      int len = e.st ? 6 : 3;
      uint16_t sp = sw16(hitCol(e, 0xB2DF)), body = sw16(hitCol(e, 0x6012));
      for (int k = 0; k < 12; k++) {
        float a = k * 0.5236f + f * 0.01f;
        line(x, y, x + (int)(cosf(a) * (5 + len)), y + (int)(sinf(a) * (5 + len)), sp);
      }
      circF(x, y, 5, ol);
      circF(x, y, 4, body);
      pset(x - 1, y - 2, sw16(0xDDFF));
      if (e.st) glow(x, y, 9, 0xB01F, 8);
      break;
    }
    case E_JELLY: {
      uint16_t rim = sw16(hitCol(e, 0xBFFF)), core = sw16(0xF81F), tent = sw16(0x7C1F);
      glow(x, y, 12, 0x67FF, 12 + (int)(sinf(f * 0.1f) * 4));
      for (int k = -2; k <= 2; k += 2)
        for (int j = 0; j < 7; j++) pset(x + k + (int)(sinf(j * 0.8f + f * 0.12f + k) * 1.2f), y + 2 + j, tent);
      for (int dy = -5; dy <= 1; dy++) {
        int w = isqrtI(25 - dy * dy);
        hspanMix(x - w, x + w, y + dy, rim, 18);
      }
      hspan(x - 5, x + 5, y + 1, rim);
      circF(x, y - 2, 1, core);
      break;
    }
    case E_PUFFER: {
      int r = e.st ? 7 : 5;
      uint16_t body = sw16(hitCol(e, 0xFEA0)), belly = sw16(0xFFD6), sp = sw16(0xC4C0);
      if (e.st) for (int k = 0; k < 10; k++) { float a = k * 0.628f; line(x + (int)(cosf(a) * r), y + (int)(sinf(a) * r), x + (int)(cosf(a) * (r + 3)), y + (int)(sinf(a) * (r + 3)), sp); }
      circF(x, y, r + 1, ol);
      circF(x, y, r, body);
      ellF(x, y + r / 2, r - 2, r / 3, belly);
      int ex = x + e.dir * (r - 3);
      circF(ex, y - 2, 1, 0xFFFF); pset(ex + e.dir, y - 2, 0);
      int tl = x - e.dir * (r + 1);
      line(tl, y, tl - e.dir * 3, y - 3 + ((f >> 3) & 1) * 2, body);
      line(tl, y, tl - e.dir * 3, y + 3 - ((f >> 3) & 1) * 2, body);
      break;
    }
    case E_EEL: {
      // body from the hole to the head, wiggling
      int hxs = SX(e.hx);
      uint16_t body = sw16(hitCol(e, 0x6E0A)), back = sw16(0x2B45), belly = sw16(0xCF2C);
      int n = (int)fabsf(e.x - e.hx);
      for (int k = 0; k <= n; k++) {
        int bx = hxs + e.dir * k, by = y + (int)(sinf(k * 0.3f - f * 0.3f) * 1.5f);
        pset(bx, by - 2, back); pset(bx, by - 1, body); pset(bx, by, body); pset(bx, by + 1, belly);
      }
      int hx = x;
      ellF(hx, y, 5, 3, ol);
      ellF(hx, y, 4, 2, body);
      pset(hx + e.dir * 2, y - 1, sw16(0xF800));
      if (e.st == 1 || e.st == 2) { pset(hx + e.dir * 4, y + 1, 0xFFFF); pset(hx + e.dir * 3, y + 2, 0xFFFF); }
      if (e.st == 0) { pset(hxs + e.dir, y - 1, sw16(0xFFE0)); pset(hxs + e.dir * 3, y - 1, sw16(0xFFE0)); }
      break;
    }
    case E_TURRET: {
      // carved stone head with a glowing eye that follows Tigo
      uint16_t st = sw16(hitCol(e, 0x9CF3)), sd = sw16(0x528A), sl = sw16(0xD6BA);
      rectF(x - 7, y - 7, 14, 14, ol);
      rectF(x - 6, y - 6, 12, 12, st);
      hspan(x - 6, x + 5, y - 6, sl);
      rectF(x - 4, y + 3, 8, 2, sd);
      float dx = pl.x + PW / 2 - e.x, dy = pl.y + PH / 2 - e.y, d = sqrtf(dx * dx + dy * dy) + 0.1f;
      int ex = x + (int)(dx / d * 2), ey = y - 1 + (int)(dy / d * 2);
      int charge = e.t % 110;
      glow(ex, ey, 7, 0xFD20, charge > 80 ? 24 : 10);
      circF(ex, ey, 1, sw16(0xFFE0));
      break;
    }
    case E_WISP: {
      glow(x, y, 10, 0x87FF, 14);
      for (int k = 1; k < 6; k++) pmix(x - (int)(e.vx * k * 3), y - (int)(e.vy * k * 3) + k / 2, sw16(0xBFFF), 26 - k * 4);
      circMix(x, y, 4, sw16(hitCol(e, 0xDFFF)), 22);
      pset(x - 1, y - 1, 0); pset(x + 2, y - 1, 0);
      break;
    }
    case E_ANGLER: {
      int d = e.dir;
      uint16_t body = sw16(hitCol(e, 0x3189)), fin = sw16(0x18E5), teeth = sw16(0xFFFF);
      ellF(x, y, 9, 6, ol);
      ellF(x, y, 8, 5, body);
      line(x - d * 8, y, x - d * 12, y - 4, fin); line(x - d * 8, y, x - d * 12, y + 4, fin);
      for (int k = 0; k < 4; k++) pset(x + d * (3 + k), y + 2 - (k & 1), teeth);
      circF(x + d * 3, y - 2, 1, sw16(0xFFE0));
      // lure on a stalk
      int lx = x + d * 9, ly = y - 9;
      line(x + d * 2, y - 5, lx, ly, fin);
      bool flash = e.st == 1 && (e.t & 4);
      glow(lx, ly, flash ? 16 : 11, 0x07FF, flash ? 30 : 18);
      circF(lx, ly, 1, 0xFFFF);
      break;
    }
    case E_SQUID: {
      uint16_t body = sw16(hitCol(e, 0xFD9B)), dk = sw16(0xC2D0);
      float a = atan2f(e.vy, e.vx);
      int tx = x - (int)(cosf(a) * 6), ty = y - (int)(sinf(a) * 6);
      for (int k = -1; k <= 1; k++) line(x, y, tx + k * 2, ty + (int)(sinf(f * 0.3f + k) * 2), dk);
      ellF(x, y - 1, 3, 5, ol);
      ellF(x, y - 1, 2, 4, body);
      pset(x - 1, y, 0); pset(x + 1, y, 0);
      break;
    }
    case E_MAGMA: {
      if (e.st == 0) { if ((f >> 3) & 1) pset(x, y - 2, sw16(0xFFE0)); break; }
      glow(x, y, 11, 0xFB00, 22);
      circF(x, y, 4, sw16(0xFA00));
      circF(x - 1, y - 1, 2, sw16(0xFFE0));
      break;
    }
  }
}

static void drawBoss(uint32_t t) {
  Boss &b = boss;
  if (!b.on) return;
  int x = SX(b.x), y = SY(b.y);
  bool wf = b.hit > 0 && (b.hit & 2);
  uint16_t ol = sw16(0x1082);
  switch (b.kind) {
    case 0: {
      uint16_t sp = sw16(wf ? 0xFFFF : 0xC2BF), body = sw16(wf ? 0xFFFF : 0x6810), hi = sw16(0x9A3A);
      for (int k = 0; k < 16; k++) {
        float a = b.a + k * 0.3927f;
        int len = 19 + ((k + (t >> 6)) & 1) * 3;
        line(x + (int)(cosf(a) * 11), y + (int)(sinf(a) * 11), x + (int)(cosf(a) * len), y + (int)(sinf(a) * len), sp);
      }
      circF(x, y, 14, ol);
      circF(x, y, 13, body);
      circF(x - 4, y - 5, 4, hi);
      // crown
      uint16_t gold = sw16(0xFEA0);
      for (int k = -1; k <= 1; k++) { line(x + k * 5, y - 12, x + k * 5, y - 18, gold); line(x + k * 5 - 1, y - 12, x + k * 5 - 1, y - 16, gold); }
      hspan(x - 7, x + 7, y - 12, gold);
      circF(x - 5, y, 3, 0xFFFF); circF(x + 5, y, 3, 0xFFFF);
      pset(x - 4, y + 1, sw16(0xF800)); pset(x + 4, y + 1, sw16(0xF800));
      hspan(x - 4, x + 4, y + 6, sw16(0x2000));
      glow(x, y, 26, 0xA01F, 8);
      break;
    }
    case 1: {
      uint16_t body = sw16(wf ? 0xFFFF : 0x4D44), back = sw16(0x2A62), belly = sw16(0xBF0B), fin = sw16(0xFEA0);
      if (b.st == 0) break;
      for (int i = 8; i >= 1; i--) {
        float sx, sy;
        wyrmSeg(i, 9, sx, sy);
        int r = 3 + i / 3;
        circF(SX(sx), SY(sy), r + 1, ol);
        circF(SX(sx), SY(sy), r, body);
        pset(SX(sx), SY(sy) - r, fin);
        pset(SX(sx), SY(sy) + r - 1, belly);
      }
      int d = b.side;
      ellF(x, y, 10, 7, ol);
      ellF(x, y, 9, 6, body);
      ellF(x, y - 3, 7, 2, back);
      hspan(x - 6, x + 6, y + 3, belly);
      circF(x + d * 4, y - 2, 2, sw16(0xFFE0)); pset(x + d * 5, y - 2, 0);
      for (int k = 0; k < 3; k++) pset(x + d * (6 + k), y + 2, 0xFFFF);
      line(x - d * 2, y - 6, x - d * 7, y - 12, fin);
      line(x - d * 4, y - 6, x - d * 9, y - 10, fin);
      break;
    }
    case 2: {
      uint16_t st = sw16(wf ? 0xFFFF : 0x8C92), sd = sw16(0x4A4D), sl = sw16(0xCE7B), gold = sw16(0xFEA0);
      rectF(x - 13, y - 12, 26, 24, ol);
      rectF(x - 12, y - 11, 24, 22, st);
      hspan(x - 12, x + 11, y - 11, sl);
      rectF(x - 12, y + 9, 24, 2, sd);
      rectF(x - 14, y - 14, 28, 3, gold);           // crown band
      for (int k = -10; k <= 10; k += 5) rectF(x + k - 1, y - 18, 3, 4, gold);
      bool angry = b.st == 2 || b.st == 3;
      uint16_t eye = angry ? 0xF800 : 0x07FF;
      glow(x - 5, y - 3, 8, eye, angry ? 26 : 14);
      glow(x + 5, y - 3, 8, eye, angry ? 26 : 14);
      rectF(x - 7, y - 4, 4, 3, sw16(mix565(eye, 0xFFFF, 12)));
      rectF(x + 3, y - 4, 4, 3, sw16(mix565(eye, 0xFFFF, 12)));
      rectF(x - 5, y + 4, 10, 2, sd);
      if (b.st == 4) for (int k = 0; k < 3; k++) {    // dizzy stars
        float a = t * 0.006f + k * 2.1f;
        pset(x + (int)(cosf(a) * 14), y - 20 + (int)(sinf(a) * 3), sw16(0xFFE0));
      }
      break;
    }
    case 3: {
      int d = b.vx >= 0 ? 1 : -1;
      if (b.st == 1 || b.st == 2) d = (pl.x > b.x) ? 1 : -1;
      uint16_t body = sw16(wf ? 0xFFFF : 0x29AB), fin = sw16(0x1906), belly = sw16(0x4A75), teeth = sw16(0xFFFF);
      ellF(x, y, 15, 10, ol);
      ellF(x, y, 14, 9, body);
      ellF(x, y + 4, 10, 4, belly);
      line(x - d * 14, y, x - d * 22, y - 8, fin); line(x - d * 14, y, x - d * 22, y + 8, fin);
      line(x - d * 15, y, x - d * 21, y - 6, fin); line(x - d * 15, y, x - d * 21, y + 6, fin);
      for (int k = 0; k < 7; k++) { pset(x + d * (4 + k), y + 3 - (k & 1) * 2, teeth); pset(x + d * (4 + k), y + 6 + (k & 1), teeth); }
      circF(x + d * 6, y - 4, 2, sw16(0xFFE0)); pset(x + d * 7, y - 4, 0);
      int lx = x + d * 16, ly = y - 16;
      line(x + d * 2, y - 9, x + d * 10, y - 18, fin); line(x + d * 10, y - 18, lx, ly, fin);
      bool flash = b.st == 1 && (b.t & 4);
      glow(lx, ly, flash ? 30 : 22, 0x07FF, flash ? 32 : 22);
      circF(lx, ly, 2, 0xFFFF);
      break;
    }
    default: {
      int pr = 13 + (int)(sinf(t * 0.01f) * 2);
      glow(x, y, 34, 0xFA00, 20);
      circF(x, y, pr + 2, sw16(0x4000));
      circF(x, y, pr, sw16(wf ? 0xFFFF : 0xF9C0));
      circF(x, y, pr - 4, sw16(wf ? 0xFFFF : 0xFC60));
      circF(x - 3, y - 3, 3, sw16(0xFFF0));
      for (int r = 0; r < 4; r++) {
        if (b.rockHp[r] <= 0) continue;
        float a = b.a + r * 1.5708f;
        int rx = x + (int)(cosf(a) * 27), ry = y + (int)(sinf(a) * 27);
        circF(rx, ry, 6, ol);
        circF(rx, ry, 5, sw16(0x5165));
        pset(rx - 2, ry - 2, sw16(0xFC00));
        pset(rx + 1, ry + 1, sw16(0xFB00));
      }
      break;
    }
  }
}

static void drawTigoAt(int sx, int sy, uint32_t t) {
  const Player &p = pl;
  if (p.inv > 0 && !p.deadT && (p.inv & 4)) return;   // blink while hurt
  bool flip = p.face < 0;
  int cx = sx + PW / 2, feet = sy + PH;
  int x0 = flip ? cx - 14 : cx - 10, y0 = feet - TG_H;
  artInit(TIGO_CHARS);
  int legs = 0, bob = 0;
  if (!p.ground) legs = 4;
  else if (fabsf(p.vx) > 0.2f) { legs = 1 + (p.anim / 5) % 3; bob = (legs == 2) ? 1 : 0; }
  else bob = (p.anim / 30) & 1;
  if (p.deadT) { legs = 4; y0 -= p.deadT / 4; }
  int am = (flashT || (p.deadT && (p.deadT & 4))) ? 1 : 0;
  auto layer = [&](const char *art, int w, int h, int ox, int oy) {
    int lx = flip ? x0 + TG_W - ox - w : x0 + ox;
    blitArt(art, w, h, lx, y0 + oy, flip, tgPal, am, 0xFFFF);
  };
  layer(TG_LEGS[legs], 22, 2, 0, 15);
  layer(TG_BODY, 22, 16, 0, bob);
  bool blink = (t % 3200) < 120 || p.deadT;
  if (blink) layer(TG_BLINK, 22, 5, 0, bob);
  if (p.shootAnim > 0) layer(TG_CLAW_OPEN, 8, 7, 15, 7 + bob);
  else layer(TG_CLAW, 6, 5, 15, 8 + bob);
  if (has(AB_JUMBO) && p.charge >= CHARGE_T) {
    int gx = flip ? x0 + TG_W - 19 : x0 + 19;
    glow(gx, y0 + 10 + bob, 7, 0xFC1F, 14 + (int)((t >> 5) & 7));
  }
}

static void drawPlayer(uint32_t t) {
  const Player &p = pl;
  int sx = SX(p.x), sy = SY(p.y);
  if (p.dashT > 0) {
    // after-images
    for (int k = 1; k <= 2; k++) {
      int gx = sx - p.dashDir * k * 7;
      rectMix(gx + 1, sy + 2, PW - 2, PH - 3, sw16(0x7FFF), 14 / k);
    }
  }
  if (biome == BI_ABYSS) glow(sx + PW / 2, sy + PH / 2, 30, 0x2A7F, 7);   // a little light of his own down there
  if (p.ground && !p.deadT) hspanMix(sx - 1, sx + PW, sy + PH, sw16(0x0000), 10);
  drawTigoAt(sx, sy, t);
  if (p.wallDir) pset(sx + (p.wallDir > 0 ? PW + 1 : -2), sy + 3, 0xFFFF);
}

static void drawShots(uint32_t t) {
  artInit(ITEM_CHARS);
  for (int i = 0; i < MAX_SHOTS; i++) {
    const Shot &s = shots[i];
    if (!s.on) continue;
    int x = SX(s.x), y = SY(s.y);
    switch (s.kind) {
      case SH_SHRIMP: glow(x, y, 6, 0xFB96, 10); blitArt(ART_SHRIMP, 8, 5, x - 4, y - 2, s.vx < 0, itemPal); break;
      case SH_JUMBO: glow(x, y, 12, 0xFC1F, 20); blitArt(ART_JUMBO, 12, 7, x - 6, y - 3, s.vx < 0, itemPal); break;
      case SH_ORB: glow(x, y, 7, 0xFD20, 18); circF(x, y, 2, sw16(0xFFE0)); break;
      case SH_SPINE: line(x, y, x - (int)(s.vx * 2.5f), y - (int)(s.vy * 2.5f), sw16(0xD39F)); pset(x, y, 0xFFFF); break;
      case SH_SEED: glow(x, y, 6, 0xAFE5, 14); circF(x, y, 2, sw16(0x6E0A)); pset(x - 1, y - 1, sw16(0xCFF0)); break;
      default: glow(x, y, 8, 0xFB00, 20); circF(x, y, 2, sw16(0xFE60)); pset(x, y, 0xFFFF); break;
    }
  }
}

static void drawParts() {
  for (int i = 0; i < MAX_PARTS; i++) {
    const Part &p = parts[i];
    if (!p.life) continue;
    int x = SX(p.x), y = SY(p.y);
    if ((unsigned)x >= (unsigned)fb.w || (unsigned)y >= (unsigned)fb.h) continue;
    int a = p.life * 32 / p.max;
    uint16_t c = sw16(p.col);
    switch (p.kind) {
      case PK_BUBBLE:
        if (p.life > p.max - 6 || (i & 3) == 0) pmix(x, y, c, 20);
        else { pmix(x, y - 1, c, 20); pmix(x - 1, y, c, 14); pmix(x + 1, y, c, 14); pmix(x, y + 1, c, 10); pset(x - 1, y - 1 + (i & 1), 0xFFFF); }
        break;
      case PK_GLINT: pmix(x, y, c, a); break;
      default: pmix(x, y, c, min(32, a + 8)); if (a > 20) pmix(x + 1, y, c, a / 2); break;
    }
  }
}

// ================= HUD and overlays =================
static void heart(int x, int y, bool full) {
  static const char *H[6] = {".##.##.", "#######", "#######", ".#####.", "..###..", "...#..."};
  uint16_t c = sw16(full ? 0xF8C9 : 0x4105), hi = sw16(0xFDF7), ol = sw16(0x1000);
  for (int j = 0; j < 6; j++)
    for (int i = 0; i < 7; i++)
      if (H[j][i] == '#') pset(x + i, y + j, (full && j == 1 && i == 1) ? hi : c);
  pset(x + 3, y + 6, ol);
}

static void drawHud(uint32_t t) {
  int maxHp = sv.maxHp;
  int perRow = fb.w > fb.h ? 12 : 8;
  for (int i = 0; i < maxHp; i++) {
    int hx = 3 + (i % perRow) * 8, hy = 3 + (i / perRow) * 7;
    rectMix(hx - 1, hy - 1, 9, 8, 0, 12);
    heart(hx, hy, i < pl.hp);
  }
  char buf[24];
  if (hudPearls < 0) itemsTaken(&hudPearls);       // walks the whole world: only after a change
  snprintf(buf, sizeof buf, "%d", hudPearls);
  int w = text(buf, fb.w - 3, 3, 1, 0xFFFF, 2, 0x0000);
  int px = fb.w - 3 - w - 6;
  glow(px, 6, 5, 0xBFFF, 8);
  circF(px, 6, 2, sw16(0xF79E));
  pset(px - 1, 5, 0xFFFF);
  // powers owned, as small coloured dots under the pearls
  for (int i = 0, n = 0; i < NUM_ABIL; i++)
    if (sv.abil >> i & 1) { circF(fb.w - 5 - n * 6, 15, 2, sw16(ABIL_COL[i])); n++; }
  if (boss.on && boss.st >= 0) {
    int bw = fb.w - 30, bx = 15, by = fb.h - 9;
    text(BOSS_NAME[boss.kind], fb.w / 2, by - 10, 1, 0xFFFF, 1, 0x0000);
    rectF(bx - 1, by - 1, bw + 2, 6, sw16(0x0000));
    rectF(bx, by, bw * max(0, boss.hp) / boss.maxHp, 4, sw16(boss.hit ? 0xFFFF : 0xF8A6));
    hspan(bx, bx + bw * max(0, boss.hp) / boss.maxHp - 1, by, sw16(0xFDF7));
  }
  if (toastT > 0 && toast[0]) {
    int a = toastT > 130 ? (150 - toastT) * 32 / 20 : toastT < 30 ? toastT * 32 / 30 : 32;
    uint16_t c = mix565(0x0000, 0xFFFF, a);
    int ty = fb.h / 3;
    text(toast, fb.w / 2, ty, 2, c, 1, a > 16 ? 0x0000 : 1);
  }
}

static void panel(int x, int y, int w, int h, uint16_t edge) {
  rectMix(x, y, w, h, sw16(0x0004), 26);
  hspan(x, x + w - 1, y, sw16(edge));
  hspan(x, x + w - 1, y + h - 1, sw16(edge));
  for (int j = y; j < y + h; j++) { pset(x, j, sw16(edge)); pset(x + w - 1, j, sw16(edge)); }
}

static void drawBanner(uint32_t t) {
  int w = min(fb.w - 8, 200), h = 66, x = (fb.w - w) / 2, y = (fb.h - h) / 2 - 6;
  panel(x, y, w, h, bannerCol);
  glow(fb.w / 2, y, 24, bannerCol, 10);
  text(bannerTitle, fb.w / 2, y + 8, 2, bannerCol, 1, 0x0000);
  text(bannerL1, fb.w / 2, y + 30, 1, 0xFFFF, 1);
  text(bannerL2, fb.w / 2, y + 41, 1, 0xFFFF, 1);
  if (bannerT > 45 && ((t >> 8) & 1)) text("press a button", fb.w / 2, y + h + 4, 1, 0xBDF7, 1, 0x0000);
}

static void drawMap(uint32_t t) {
  int minX = 99, minY = 99, maxX = 0, maxY = 0;
  for (int i = 0; i < NUM_ROOMS; i++) {
    const RoomDef &d = ROOMS[i];
    minX = min(minX, (int)d.cx); minY = min(minY, (int)d.cy);
    maxX = max(maxX, d.cx + d.cw); maxY = max(maxY, d.cy + d.ch);
  }
  int gw = maxX - minX, gh = maxY - minY;
  int cs = min((fb.w - 10) / gw, (fb.h - 48) / gh);
  // title (20 px), the grid, then the powers row (26 px), centred as one block; the hold bar at the bottom
  int top = (fb.h - (20 + gh * cs + 26)) / 2;
  int ox = (fb.w - gw * cs) / 2, oy = top + 20;
  rectMix(0, 0, fb.w, fb.h, sw16(0x0002), 24);
  char buf[24];
  snprintf(buf, sizeof buf, "MAP  %d%%", completion());
  text(buf, fb.w / 2, top, 2, 0xFFFF, 1, 0x0000);
  for (int i = 0; i < NUM_ROOMS; i++) {
    const RoomDef &d = ROOMS[i];
    bool seen = sv.visited >> i & 1;
    bool near = false;
    if (!seen) {
      // rooms next to a visited one show as faint outlines
      for (int j = 0; j < NUM_ROOMS && !near; j++) {
        if (!(sv.visited >> j & 1)) continue;
        const RoomDef &e = ROOMS[j];
        bool touchX = d.cx <= e.cx + e.cw && e.cx <= d.cx + d.cw, touchY = d.cy <= e.cy + e.ch && e.cy <= d.cy + d.ch;
        if (touchX && touchY) near = true;
      }
      if (!near) continue;
    }
    int x = ox + (d.cx - minX) * cs, y = oy + (d.cy - minY) * cs, w = d.cw * cs - 1, h = d.ch * cs - 1;
    uint16_t bc = BIOMES[d.biome].rim;
    if (seen) rectF(x, y, w, h, sw16(mix565(bc, 0x0000, 18)));
    uint16_t edge = sw16(seen ? bc : 0x4208);
    if (i == room && ((t >> 8) & 1)) edge = 0xFFFF;
    hspan(x, x + w - 1, y, edge); hspan(x, x + w - 1, y + h - 1, edge);
    for (int j = y; j < y + h; j++) { pset(x, j, edge); pset(x + w - 1, j, edge); }
    if (!seen) continue;
    // things inside: altar (white), unclaimed guardian (red), items left (yellow)
    int n = d.cw * CELL * d.ch * CELL, bit = 0, left = 0;
    bool altar = false, guard = false;
    for (int k = 0; k < n; k++) {
      char c = d.tiles[k];
      if (c == 'S') altar = true;
      if (c == 'B' && !(sv.bosses >> d.biome & 1)) guard = true;
      if (c == 'o' || c == 'H' || c == 'P') { if (bit < 16 && !(sv.items[i] >> bit & 1)) left++; bit++; }
    }
    if (altar) circF(x + w / 2, y + h / 2, 1, 0xFFFF);
    if (guard) circF(x + w / 2, y + h / 2, 2, sw16(0xF800));
    if (left) pset(x + w - 3, y + 2, sw16(0xFFE0));
  }
  // Tigo
  const RoomDef &d = ROOMS[room];
  int tx = ox + (d.cx - minX) * cs + (int)((pl.x + PW / 2) * cs / (CELL * TS));
  int ty = oy + (d.cy - minY) * cs + (int)((pl.y + PH / 2) * cs / (CELL * TS));
  glow(tx, ty, 5, 0xFB49, 20);
  circF(tx, ty, 1, sw16(0xFB49));
  // powers
  int ax = fb.w / 2 - NUM_ABIL * 6 + 6, ay = oy + gh * cs + 6;
  for (int i = 0; i < NUM_ABIL; i++) {
    bool on = sv.abil >> i & 1;
    circF(ax + i * 12, ay, 3, sw16(on ? ABIL_COL[i] : 0x2104));
    if (on) glow(ax + i * 12, ay, 6, ABIL_COL[i], 10);
  }
  {                                                  // keep holding both to leave for the menu
    int bw = 80, bx = (fb.w - bw) / 2, by = fb.h - 8;
    text("keep holding: menu", fb.w / 2, by - 11, 1, 0xBDF7, 1, 0x0000);
    rectF(bx, by, bw, 3, sw16(0x2104));
    rectF(bx, by, bw * min(bannerT, QUIT_HOLD_T) / QUIT_HOLD_T, 3, sw16(0xFFE0));
  }
}

// whatever the camera shows beyond the room (short rooms in portrait, narrow
// ones in landscape, the next room through a door) sinks into the gloom
static void dimOutside() {
  int x0 = -icx, y0 = -icy, x1 = roomW * TS - icx, y1 = roomH * TS - icy;
  if (x0 <= 0 && y0 <= 0 && x1 >= fb.w && y1 >= fb.h) return;
  uint16_t k = sw16(BIOMES[biome].edge);
  if (y0 > 0) rectMix(0, 0, fb.w, y0, k, 20);
  if (y1 < fb.h) rectMix(0, y1, fb.w, fb.h - y1, k, 20);
  int ya = max(0, y0), yb = min(fb.h, y1);
  if (x0 > 0) rectMix(0, ya, x0, yb - ya, k, 20);
  if (x1 < fb.w) rectMix(x1, ya, fb.w - x1, yb - ya, k, 20);
}

static void drawWorld(uint32_t t) {
  int sh = shakeT ? (int)(shakeT > 8 ? 3 : shakeT > 3 ? 2 : 1) : 0;
  icx = (int)floorf(camX) + (sh ? rnd(sh * 2 + 1) - sh : 0);
  icy = (int)floorf(camY) + (sh ? rnd(sh * 2 + 1) - sh : 0);
  drawBackground(icx, icy, roomH * TS, t);
  drawTiles(t);
  dimOutside();
  drawItems(t);
  for (int i = 0; i < MAX_ENTS; i++) if (ents[i].on) drawEnt(ents[i], t);
  drawBoss(t);
  if (state == PLAY || state == OVER) drawPlayer(t);
  drawShots(t);
  drawParts();
  drawGates(t);
  if (fadeT) rectMix(0, 0, fb.w, fb.h, 0, fadeT * 4);
  if (flashT) rectMix(0, 0, fb.w, fb.h, sw16(0xF800), flashT * 2);
}

// ================= title, game over, ending =================
static void setTigoPal() {
  int k = theme().neon ? 1 : 0;
  for (int i = 0; i < 17; i++) tgPal[i] = sw16(TG_PAL_SRC[k][i]);
}

// the logo: font 4 doubled, a deep outline, a top-to-bottom gradient and a glow
static void drawLogo(const char *s, int cx, int y, uint32_t t) {
  if (!tspOk) return;
  int w = tsp.textWidth(s, 4), h = 26, k = 2;
  tsp.fillSprite(0);
  tsp.setTextColor(1);
  tsp.setTextDatum(TL_DATUM);
  tsp.drawString(s, 0, 0, 4);
  const uint8_t *bits = (const uint8_t *)tsp.getPointer();
  const int stride = (TSP_W + 7) >> 3;
  int x0 = cx - w * k / 2;
  glow(cx, y + h, w * k / 2 + 10, 0x05FF, 12 + (int)(sinf(t * 0.002f) * 4));
  auto on = [&](int i, int j) { return i >= 0 && j >= 0 && i < w && j < h && (bits[j * stride + (i >> 3)] & (0x80 >> (i & 7))); };
  uint16_t ol = sw16(0x0846);
  for (int j = -1; j <= h; j++)
    for (int i = -1; i <= w; i++) {
      if (on(i, j)) continue;
      if (on(i - 1, j) || on(i + 1, j) || on(i, j - 1) || on(i, j + 1) || on(i - 1, j - 1) || on(i + 1, j + 1))
        rectF(x0 + i * k, y + j * k, k, k, ol);
    }
  int shine = (int)((t / 12) % (w + 60)) - 30;
  for (int j = 0; j < h; j++) {
    uint16_t c = j < h / 2 ? mix565(0xFFFF, 0x6FFF, j * 64 / h) : mix565(0x3D7F, 0xA81F, (j - h / 2) * 64 / h);
    for (int i = 0; i < w; i++) {
      if (!on(i, j)) continue;
      uint16_t cc = c;
      if (abs(i + j / 2 - shine) < 3) cc = mix565(c, 0xFFFF, 20);
      rectF(x0 + i * k, y + j * k, k, k, sw16(cc));
    }
  }
}

static float titleShrimp = -1;
static void titleItems(const char **lab, int &n) {
  n = 0;
  if (haveSave) lab[n++] = "Continue";
  lab[n++] = haveSave ? (titleConfirm ? "Erase save?" : "New game") : "Start";
  lab[n++] = orientName(orient);
}

// small triangle pointing at a physical button (landscape hints)
static void pointer(int x, int y, int dir, uint16_t c) {
  for (int k = 0; k < 5; k++)
    for (int j = -k; j <= k; j++) pset(x - dir * k, y + j, c);
}
// Landscape: the two buttons sit at the top and bottom of one short side; a
// yellow arrow points at each, with what it does. (btnRight = top button.)
static void landHints(const char *top, const char *bottom) {
  bool right = orient == OR_LAND_RIGHT;
  int ex = right ? fb.w - 3 : 2, dir = right ? 1 : -1, tx = right ? fb.w - 10 : 9;
  pointer(ex, 8, dir, sw16(0xFFE0));
  pointer(ex, fb.h - 9, dir, sw16(0xFFE0));
  text(top, tx, 4, 1, 0xFFE0, right ? 2 : 0, 0x0000);
  text(bottom, tx, fb.h - 12, 1, 0xFFE0, right ? 2 : 0, 0x0000);
}

static void drawTitle(uint32_t t) {
  bool land = fb.w > fb.h;
  // landscape: the menu sits on the side of the buttons, the picture on the other
  bool menuLeft = orient == OR_LAND_LEFT;
  int ox = land && menuLeft ? 104 : 0;               // shift of the picture half
  // backdrop: the coral caves, slowly drifting
  int cam = (int)(t / 40);
  drawBackground(cam, 0, fb.h, t);
  // a reef ledge for Tigo
  int ledgeY = land ? fb.h - 18 : 134, ledgeX0 = land ? 8 + ox : 18, ledgeX1 = land ? 132 + ox : 116;
  for (int x = ledgeX0; x < ledgeX1; x += 8) {
    int m = (x == ledgeX0 ? 8 : 0) | (x + 8 >= ledgeX1 ? 2 : 0) | 1;
    blitTile(tileArt[m], x, ledgeY);
    blitTile(tileArt[TA_FRINGE + ((x / 8) & 1)], x, ledgeY - 8);
    for (int y = ledgeY + 8; y < ledgeY + 24 && y < fb.h; y += 8) blitTile(tileArt[(m & 10) | 16], x, y);
  }
  // Tigo, three times bigger, now and then firing a shrimp
  int tx = (land ? 46 : 34) + ox, ty = ledgeY - TG_H * 3 + 3;
  artInit(TIGO_CHARS);
  int bob = (t / 500) & 1;
  bool shoot = titleShrimp >= 0 && titleShrimp < 10;
  blitArtBig(TG_LEGS[0], 22, 2, tx, ty + 15 * 3, false, tgPal, 3);
  blitArtBig(TG_BODY, 22, 16, tx, ty + bob * 3, false, tgPal, 3);
  if ((t % 3000) < 150) blitArtBig(TG_BLINK, 22, 5, tx, ty + bob * 3, false, tgPal, 3);
  if (shoot) blitArtBig(TG_CLAW_OPEN, 8, 7, tx + 45, ty + (7 + bob) * 3, false, tgPal, 3);
  else blitArtBig(TG_CLAW, 6, 5, tx + 45, ty + (8 + bob) * 3, false, tgPal, 3);
  if (titleShrimp >= 0) {
    artInit(ITEM_CHARS);
    int sx = tx + 66 + (int)titleShrimp * 3, sy = ty + 30 + bob * 3;
    if (sx < fb.w + 10) { glow(sx, sy, 9, 0xFB96, 16); blitArtBig(ART_SHRIMP, 8, 5, sx - 8, sy - 5, false, itemPal, 2); }
  }
  for (int i = 0; i < 7; i++) {                      // bubbles rising past him
    int bx = tx + 10 + (int)(hash32(i) % 60), by = ledgeY - (int)((t / (18 + i * 3) + hash32(i + 9)) % (ledgeY + 10));
    bx += (int)(sinf(t * 0.003f + i) * 3);
    pmix(bx, by, 0xFFFF, 18); pmix(bx + 1, by + 1, 0xFFFF, 10);
  }
  // logo and menu
  int lx = land ? 70 + ox : fb.w / 2;
  int mx = land ? (menuLeft ? 54 : 186) : fb.w / 2;
  drawLogo("ABYX", lx, land ? 14 : 8, t);
  text("a Tigo adventure", land ? mx : lx, land ? 16 : 62, 1, 0x9FFF, 1, 0x0000);
  const char *lab[3];
  int n;
  titleItems(lab, n);
  int my = land ? 30 : 160, step = land ? 18 : 17;
  for (int i = 0; i < n; i++) {
    bool sel = i == titleSel;
    int y = my + i * step;
    if (sel) {
      int w = min(land ? 104 : fb.w - 12, textW(lab[i], 2) + 18);
      panel(mx - w / 2, y - 2, w, 18, titleConfirm && i == n - 2 ? 0xF800 : 0xFFE0);
      glow(mx - w / 2 + 4, y + 7, 6, 0xFFE0, 10);
    }
    text(lab[i], mx, y, 2, sel ? 0xFFFF : 0x9CF3, 1, 0x0000);
  }
  char buf[24];
  snprintf(buf, sizeof buf, "Best %lu", (unsigned long)hiScore);
  if (land) {
    text(buf, mx, my + n * step + 2, 1, 0xBDF7, 1, 0x0000);
    text("hold: walk", mx, 98, 1, 0xFFFF, 1, 0x0000);
    text("both: jump", mx, 108, 1, 0xFFFF, 1, 0x0000);
    landHints("choose / walk >", "next / < walk");
  } else {
    text(buf, fb.w / 2, 73, 1, 0xBDF7, 1, 0x0000);
    text("hold L / R: walk", fb.w / 2, 203, 1, 0xFFFF, 1, 0x0000);
    text("L+R: jump, hold=high", fb.w / 2, 214, 1, 0xFFFF, 1, 0x0000);
    text("L next    R choose", fb.w / 2, 228, 1, 0xFFE0, 1, 0x0000);
  }
}

static void drawEndBox(bool victory) {
  int w = min(fb.w - 10, 150), h = 92, x = (fb.w - w) / 2, y = (fb.h - h) / 2;
  bool land = fb.w > fb.h;
  if (land) { h = 76; y = (fb.h - h) / 2; }
  uint16_t edge = victory ? 0xFFE0 : 0xF8A6;
  panel(x, y, w, h, edge);
  glow(fb.w / 2, y, 30, edge, 10);
  text(victory ? "VICTORY!" : "TIGO FAINTED", fb.w / 2, y + 6, 2, edge, 1, 0x0000);
  char buf[32];
  snprintf(buf, sizeof buf, "%lu", (unsigned long)score());
  text(buf, fb.w / 2, y + 26, 2, 0xFFFF, 1, 0x0000);
  if (newHigh) text("NEW BEST!", fb.w / 2, y + 45, 1, 0xFFE0, 1, 0x0000);
  else { snprintf(buf, sizeof buf, "Best %lu", (unsigned long)hiScore); text(buf, fb.w / 2, y + 45, 1, 0xBDF7, 1, 0x0000); }
  snprintf(buf, sizeof buf, "Map %d%%", completion());
  text(buf, fb.w / 2, y + 57, 1, 0x9FFF, 1, 0x0000);
  if (land) landHints(victory ? "explore" : "retry", "menu");
  else {
    text("L menu", x + 8, y + h - 14, 1, 0xFFE0, 0, 0x0000);
    text(victory ? "R explore" : "R retry", x + w - 8, y + h - 14, 1, 0xFFE0, 2, 0x0000);
  }
}

static void render(uint32_t now) {
  uint32_t t0 = micros();
  if (state == TITLE) drawTitle(now);
  else {
    drawWorld(now);
    if (state == PLAY) {
      drawHud(now);
      if (mode == M_BANNER) drawBanner(now);
      else if (mode == M_MAP) drawMap(now);
    } else drawEndBox(state == END);
  }
  uint32_t t1 = micros();
  spr.pushSprite(0, 0);
  uint32_t t2 = micros();
  // frame statistics on the USB log every few seconds
  drawUs += t1 - t0;
  frameUs += t2 - t0;
  statFrames++;
  if ((int32_t)(now - statT) > 3000) {
    if (statFrames) dbg("abyx: draw %lu us, draw+push %lu us, %lu fps\n", (unsigned long)(drawUs / statFrames),
                        (unsigned long)(frameUs / statFrames), (unsigned long)(statFrames * 1000 / ((now - statT) ? (now - statT) : 1)));
    drawUs = frameUs = statFrames = 0;
    statT = now;
  }
}

// ================= flow =================
// Turns the panel for the chosen view (orient.h recreates the sprite) and
// points the drawing layer at the new buffer. In landscape the bottom button
// walks left and the top one right. False when the sprite is not 16-bit.
static bool applyOrientation() {
  applyOrient(orient);
  fbSetup();
  if (orient == OR_PORTRAIT) { btnLeft = &bL; btnRight = &bR; }
  else { btnLeft = &orientBottomBtn(orient); btnRight = &orientTopBtn(orient); }
  bgBiome = -1;
  if (spr.getColorDepth() == 16 && fb.buf) return true;
  dbg("abyx: needs a 16-bit sprite\n");
  return false;
}

static void startPlay() {
  loadRoom(sv.room);
  placeAtAltar();
  // waking up on the altar must not count as stepping onto it
  altarTouch = -1;
  for (int i = 0; i < nItems; i++)
    if (items[i].kind == 'S' && fabsf(items[i].x - (pl.x + PW / 2)) < 9) altarTouch = i;
  camX = pl.x + PW / 2 - fb.w / 2.0f;
  camY = pl.y + PH / 2 - fb.h / 2.0f;
  camX = roomW * TS <= fb.w ? (roomW * TS - fb.w) / 2.0f : constrain(camX, 0.0f, (float)(roomW * TS - fb.w));
  camY = roomH * TS <= fb.h ? (roomH * TS - fb.h) / 2.0f : constrain(camY, 0.0f, (float)(roomH * TS - fb.h));
  state = PLAY;
  mode = M_RUN;
  quitReq = false;
  hudPearls = -1;
  fadeT = 8;
  toast = BIOMES[biome].name;
  toastT = 150;
  newHigh = false;
  lastTick = millis();
  dirty = true;
}

static void leaveGame() {
  textFree();
  resetOrient();
}

void drawIcon(int x, int y, bool dim, uint32_t t) {
  // a tiny Tigo (his real pixel art) firing a glowing shrimp, bubbles above
  auto c = [dim](uint16_t col) { return dim ? tft.alphaBlend(90, col, TFT_BLACK) : col; };
  int k = theme().neon ? 1 : 0;
  artInit(TIGO_CHARS);
  int x0 = x - 12, y0 = y - 6;
  for (int j = 0; j < 16; j++)
    for (int i = 0; i < 22; i++) {
      int s = artSlot[(uint8_t)TG_BODY[j * 22 + i] & 127];
      if (s) spr.drawPixel(x0 + i, y0 + j, c(TG_PAL_SRC[k][s]));
    }
  for (int j = 0; j < 2; j++)
    for (int i = 0; i < 22; i++) {
      int s = artSlot[(uint8_t)TG_LEGS[0][j * 22 + i] & 127];
      if (s) spr.drawPixel(x0 + i, y0 + 15 + j, c(TG_PAL_SRC[k][s]));
    }
  for (int j = 0; j < 5; j++)
    for (int i = 0; i < 6; i++) {
      int s = artSlot[(uint8_t)TG_CLAW[j * 6 + i] & 127];
      if (s) spr.drawPixel(x0 + 15 + i, y0 + 8 + j, c(TG_PAL_SRC[k][s]));
    }
  int ph = t ? (int)((t / 80) % 4) : 1;
  spr.fillCircle(x + 10 + ph, y + 3, 2, c(0xFB96));
  spr.drawCircle(x - 6, y - 10 - (t ? (int)((t / 90) % 4) : 0), 2, c(0xBFFF));
  spr.drawPixel(x + 2, y - 14 - (t ? (int)((t / 70) % 3) : 0), c(0xFFFF));
}

static EvQ qL, qR;                                 // button events waiting for the next tick
static bool tArmL = false, tArmR = false;          // title: presses that started there
static uint32_t lastAnim = 0;

void begin() {
  if (spr.getColorDepth() != 16) {                 // the direct buffer drawing needs the 16-bit sprite
    dbg("abyx: needs a 16-bit sprite\n");
    returnToMenu();
    return;
  }
  hiScore = prefs.getUInt("abyx_hi", 0);
  orient = prefs.getUChar("abyx_or", OR_PORTRAIT) % NUM_ORIENTS;
  if (!applyOrientation()) { resetOrient(); returnToMenu(); return; }
  haveSave = readSave();
  if (!haveSave) freshSave();
  countWorld();
  initSin();
  textInit();
  setItemPal();
  setTigoPal();
  setBiomePalettes(theme().neon);
  genTiles(BI_CORAL);
  genBackground(BI_CORAL, fb.h);
  state = TITLE;
  mode = M_RUN;
  titleSel = 0;
  titleConfirm = 0;
  titleShrimp = -1;
  armed = false;
  tArmL = tArmR = false;
  qL.clear();
  qR.clear();
  lastInput = millis();
  dirty = true;
}


void update(uint32_t now) {
  int eL = updateBtn(bL, now), eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) lastInput = now;
  int eLeft = btnLeft == &bL ? eL : eR, eRight = btnRight == &bR ? eR : eL;

  if (state == PLAY) {
    qL.push(eLeft);
    qR.push(eRight);
    if ((int32_t)(now - lastTick) > 200) lastTick = now - TICK_MS;   // after a stall: don't catch up
    while (state == PLAY && (int32_t)(now - lastTick) >= (int32_t)TICK_MS) {
      lastTick += TICK_MS;
      int a = qL.pop(), b = qR.pop();
      Input in = {btnLeft->stable, btnRight->stable, a == 1, b == 1, a == 2, b == 2};
      tickPlay(in);
      dirty = true;
    }
    // a power banner waits for a press; nobody there: save and doze off
    if (state == PLAY && mode == M_BANNER && idleTooLong(now, lastInput)) { writeSave(); leaveGame(); goToSleep(); return; }
    if (quitReq) {                                 // held both on the map: save and leave
      quitReq = false;
      writeSave();
      leaveGame();
      returnToMenu();
      return;
    }
  } else if (state == TITLE) {
    qL.clear();
    qR.clear();
    if (eLeft == 1) tArmL = true;
    if (eRight == 1) tArmR = true;
    const char *lab[3];
    int n;
    titleItems(lab, n);
    if (eLeft == 2 && tArmL) {                     // next option
      tArmL = false;
      titleSel = (titleSel + 1) % n;
      titleConfirm = 0;
      dirty = true;
    }
    if (eRight == 2 && tArmR) {                    // choose
      tArmR = false;
      dirty = true;
      if (titleSel == n - 1) {                     // view: cycle the orientation
        orient = (orient + 1) % NUM_ORIENTS;
        prefs.putUChar("abyx_or", (uint8_t)orient);
        if (!applyOrientation()) { leaveGame(); returnToMenu(); return; }
        genBackground(BI_CORAL, fb.h);
        tArmL = tArmR = false;
      } else if (haveSave && titleSel == 1) {      // new game over an old save: ask first
        if (!titleConfirm) titleConfirm = 1;
        else { freshSave(); writeSave(); newHigh = false; titleConfirm = 0; startPlay(); }
      } else {
        if (!haveSave) { freshSave(); writeSave(); newHigh = false; }
        startPlay();
      }
    }
    if ((int32_t)(now - lastAnim) >= 20) {
      lastAnim = now;
      if (titleShrimp >= 0) { titleShrimp += 1; if (titleShrimp > 60) titleShrimp = -1; }
      else if (rnd(160) == 0) titleShrimp = 0;
      dirty = true;
    }
    if (state == TITLE && idleTooLong(now, lastInput)) { leaveGame(); goToSleep(); return; }
  } else {                                         // fainted / victory
    qL.clear();
    qR.clear();
    if (armed && eLeft == 2) leaveGame();           // L leaves for the menu inside startOrEnd
    EndChoice ch = startOrEnd(true, eLeft, eRight, armed);
    if (ch == END_MENU) return;
    if (ch == END_PLAY) {
      if (state == OVER) startPlay();
      else { state = PLAY; mode = M_RUN; lastTick = now; }
      return;
    }
    if (idleTooLong(now, lastInput)) { leaveGame(); goToSleep(); return; }
    if ((int32_t)(now - lastAnim) >= 33) {         // keep the scene alive behind the box
      lastAnim = now;
      tickN++;
      tickParts();
      dirty = true;
    }
  }

  if (dirty) {
    render(now);
    dirty = false;
  }
}

}  // namespace abyx

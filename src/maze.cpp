// Maze (Dot Chaser)
//
// Controls (two buttons, in SCREEN directions - L is always left / up,
// R is always right / down):
//   Tap L : going up or down -> turn left;   going left or right -> turn up
//   Tap R : going up or down -> turn right;  going left or right -> turn down
//   Hold L or R : U-turn
// A turn waits (arrow on the hero and at the opening ahead) until the next
// opening that way, for about 2 s. Bends with only one way on are followed by
// themselves, so only real junctions need a decision.
// Eat all dots and power pellets while dodging ghosts! 3 lives.

#include "common.h"

namespace maze {

static const int COLS = 9, ROWS = 13, CELL = 15, BOARD_Y = 42;
static const int DX[4] = {0, 1, 0, -1};                    // up, right, down, left
static const int DY[4] = {-1, 0, 1, 0};
static const uint32_t HOLD_MS = 220, TICK_MS = 16;

// positions are in 1/16 px so speeds can be fine-grained without floats
static const int SUB = 16, CELL_S = CELL * SUB;
static const int HERO_SPD = 18;                            // 1.125 px per tick, ~70 px/s
static const int CORNER_S = 4 * SUB;                       // a turn is taken up to 4 px off the centre
static const int WANT_TICKS = 125;                         // a queued turn lasts ~2 s
static const int READY_TICKS = 80;                         // pause before each life
static const int CATCH_S = 8 * SUB;                        // touching distance hero / ghost

// 1 = wall, 0 = dot/path
static const uint8_t MAP[ROWS][COLS] = {
  {1,1,1,1,1,1,1,1,1},
  {1,0,0,0,1,0,0,0,1},
  {1,0,1,0,0,0,1,0,1},
  {1,0,1,1,0,1,1,0,1},
  {1,0,0,0,0,0,0,0,1},
  {1,1,0,1,0,1,0,1,1},
  {1,0,0,0,0,0,0,0,1},
  {1,0,1,0,1,0,1,0,1},
  {1,0,0,0,0,0,0,0,1},
  {1,0,1,1,0,1,1,0,1},
  {1,0,1,0,0,0,1,0,1},
  {1,0,0,0,1,0,0,0,1},
  {1,1,1,1,1,1,1,1,1},
};

struct Mover { int x, y, dir; bool moving; };
struct Ghost { Mover m; uint16_t col; int home; };          // home > 0: back in the box, harmless

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static uint8_t dots[ROWS][COLS];                          // 0 = empty, 1 = dot, 2 = power pellet
static Mover hero;
static int want = -1, wantT = 0;                          // queued absolute direction, ticks left
static Ghost ghosts[3];
static int lives, level, dotsLeft, frightT, readyT, ghostChain, animT;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static bool isWall(int r, int c) {
  if (r < 0 || r >= ROWS || c < 0 || c >= COLS) return true;
  return MAP[r][c] == 1;
}

// ---------------- movement on the grid ----------------
static int cellOf(int v) { return v / CELL_S; }
static int centreOf(int c) { return c * CELL_S + CELL_S / 2; }
static bool openDir(int r, int c, int d) { return !isWall(r + DY[d], c + DX[d]); }

// distance along the mover's direction to the centre of its cell:
// > 0 = centre still ahead, < 0 = already past it
static int toCentre(const Mover &m) {
  return DX[m.dir] * (centreOf(cellOf(m.x)) - m.x) + DY[m.dir] * (centreOf(cellOf(m.y)) - m.y);
}

static void advance(Mover &m, int d) {
  m.x += DX[m.dir] * d;
  m.y += DY[m.dir] * d;
}

static void heroStep(int spd) {
  int c = cellOf(hero.x), r = cellOf(hero.y);
  int along = toCentre(hero);
  // a queued turn on the same axis is meaningless now (taken by a bend or a U-turn)
  if (want >= 0 && (want & 1) == (hero.dir & 1)) want = -1;
  // forgiving corners: take the turn a few px before or after the centre
  if (want >= 0 && abs(along) <= CORNER_S && openDir(r, c, want)) {
    hero.x = centreOf(c);
    hero.y = centreOf(r);
    hero.dir = want;
    hero.moving = true;
    want = -1;
    along = 0;
  }
  if (along >= 0 && along <= spd) {
    // reaching the centre during this step: decide there
    hero.x = centreOf(c);
    hero.y = centreOf(r);
    int rest = spd - along;
    if (!openDir(r, c, hero.dir)) {
      int only = -1, n = 0;
      for (int d = 0; d < 4; d++)
        if (d != hero.dir && d != ((hero.dir + 2) & 3) && openDir(r, c, d)) { only = d; n++; }
      if (n != 1) { hero.moving = false; return; }       // T-junction or dead end: wait
      hero.dir = only;                                    // a bend: follow it
    }
    hero.moving = true;
    advance(hero, rest);
    return;
  }
  hero.moving = true;
  advance(hero, spd);
}

static void ghostTurn(Ghost &g, int r, int c, int pr, int pc) {
  int opts[4], nOpt = 0;
  for (int d = 0; d < 4; d++) {
    if (d == ((g.m.dir + 2) & 3)) continue;
    if (openDir(r, c, d)) opts[nOpt++] = d;
  }
  if (nOpt == 0) { g.m.dir = (g.m.dir + 2) & 3; return; }
  if (rnd(100) < 60) {
    int bestD = opts[0], bestDist = frightT ? -1 : 99999;
    for (int k = 0; k < nOpt; k++) {
      int d = opts[k];
      int dist = abs((c + DX[d]) - pc) + abs((r + DY[d]) - pr);
      if (frightT ? (dist > bestDist) : (dist < bestDist)) { bestDist = dist; bestD = d; }
    }
    g.m.dir = bestD;
  } else {
    g.m.dir = opts[rnd(nOpt)];
  }
}

static void ghostStep(Ghost &g, int spd, int pr, int pc) {
  int along = toCentre(g.m);
  if (along >= 0 && along <= spd) {
    int c = cellOf(g.m.x), r = cellOf(g.m.y);
    g.m.x = centreOf(c);
    g.m.y = centreOf(r);
    ghostTurn(g, r, c, pr, pc);
    advance(g.m, spd - along);
    return;
  }
  advance(g.m, spd);
}

// ---------------- game flow ----------------
static void placeGhost(Ghost &g, int i) {
  g.m.x = centreOf(3 + i);
  g.m.y = centreOf(4);
  g.m.dir = (i & 1) ? 1 : 3;
  g.m.moving = true;
}

static void resetPositions() {
  hero.x = centreOf(4);
  hero.y = centreOf(8);
  hero.dir = 1;
  hero.moving = true;
  want = -1;
  frightT = 0;
  readyT = READY_TICKS;
  const uint16_t gCol[3] = {TFT_RED, TFT_CYAN, TFT_MAGENTA};
  for (int i = 0; i < 3; i++) {
    placeGhost(ghosts[i], i);
    ghosts[i].col = gCol[i];
    ghosts[i].home = 0;
  }
}

static void initMaze() {
  dotsLeft = 0;
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) {
      if (MAP[r][c] == 0) {
        bool pellet = (r == 1 || r == 11) && (c == 1 || c == 7);
        dots[r][c] = pellet ? 2 : 1;
        dotsLeft++;
      } else {
        dots[r][c] = 0;
      }
    }
  resetPositions();
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("maze_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  lives = 3;
  level = 1;
  newHigh = false;
  initMaze();
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

// L / R in screen terms: L = left or up, R = right or down (across the current axis)
static int screenTurn(bool right) {
  bool vertical = (hero.dir & 1) == 0;
  if (vertical) return right ? 1 : 3;
  return right ? 2 : 0;
}

static void uTurn() {
  hero.dir = (hero.dir + 2) & 3;
  hero.moving = true;
  want = -1;
}

static void tick(uint32_t now, int eL, int eR) {
  animT++;
  if (bL.stable && !bL.consumed && (int32_t)(now - bL.t0) >= (int32_t)HOLD_MS) { bL.consumed = true; uTurn(); }
  if (bR.stable && !bR.consumed && (int32_t)(now - bR.t0) >= (int32_t)HOLD_MS) { bR.consumed = true; uTurn(); }
  if (eL == 2 && !bL.consumed) { want = screenTurn(false); wantT = WANT_TICKS; }
  if (eR == 2 && !bR.consumed) { want = screenTurn(true); wantT = WANT_TICKS; }
  if (want >= 0 && --wantT <= 0) want = -1;

  if (readyT > 0) {                                       // "READY!": nothing moves yet
    readyT--;
    return;
  }
  if (frightT > 0) frightT--;

  heroStep(HERO_SPD);

  int pr = constrain(cellOf(hero.y), 0, ROWS - 1);
  int pc = constrain(cellOf(hero.x), 0, COLS - 1);
  if (dots[pr][pc]) {
    if (dots[pr][pc] == 2) {
      frightT = max(110, 260 - (level - 1) * 25);
      ghostChain = 0;
      score += 50;
    } else {
      score += 10;
    }
    dots[pr][pc] = 0;
    if (--dotsLeft <= 0) {
      score += 200;
      level++;
      initMaze();
      return;
    }
  }

  // Ghosts: a little slower than the hero at first, never faster
  int gSpd = min(17, 11 + level);
  if (frightT > 0) gSpd = gSpd * 3 / 5;
  int numG = min(3, 1 + level);
  for (int i = 0; i < numG; i++) {
    Ghost &g = ghosts[i];
    if (g.home > 0) { if (--g.home == 0) placeGhost(g, 1); continue; }
    ghostStep(g, gSpd, pr, pc);

    int dx = g.m.x - hero.x, dy = g.m.y - hero.y;
    if (dx * dx + dy * dy < CATCH_S * CATCH_S) {
      if (frightT > 0) {
        score += 100 << min(ghostChain, 2);               // 100, 200, 400 for a chain
        ghostChain++;
        g.home = 90;
      } else {
        if (--lives <= 0) { gameOver(); return; }
        resetPositions();
        return;
      }
    }
  }
}

// ---------------- rendering ----------------
// a small filled arrow pointing in direction d with its tip at (x, y)
static void arrow(int x, int y, int d, int len, int half, uint16_t col) {
  int bx = x - DX[d] * len, by = y - DY[d] * len;
  spr.fillTriangle(x, y, bx + DY[d] * half, by + DX[d] * half, bx - DY[d] * half, by - DX[d] * half, col);
}

// where the queued turn will happen: the first cell ahead with an opening that way
static bool turnCell(int &rr, int &cc) {
  if (want < 0) return false;
  int c = cellOf(hero.x), r = cellOf(hero.y);
  if (toCentre(hero) < -CORNER_S) { r += DY[hero.dir]; c += DX[hero.dir]; }
  for (int k = 0; k < 9 && !isWall(r, c); k++) {
    if (openDir(r, c, want)) { rr = r; cc = c; return true; }
    r += DY[hero.dir];
    c += DX[hero.dir];
  }
  return false;
}

static void drawGame() {
  spr.fillSprite(TFT_BLACK);

  for (int r = 0; r < ROWS; r++) {
    for (int c = 0; c < COLS; c++) {
      int x = c * CELL, y = BOARD_Y + r * CELL;
      if (MAP[r][c]) {
        spr.fillRoundRect(x + 1, y + 1, CELL - 2, CELL - 2, 3, 0x11D3);
      } else if (dots[r][c] == 1) {
        spr.fillCircle(x + CELL / 2, y + CELL / 2, 1, TFT_YELLOW);
      } else if (dots[r][c] == 2) {
        spr.fillCircle(x + CELL / 2, y + CELL / 2, (animT / 12) & 1 ? 3 : 2, TFT_WHITE);
      }
    }
  }

  // the queued turn: a pulsing arrow in the opening where it will be taken
  int tr, tc;
  if (turnCell(tr, tc)) {
    uint16_t col = (animT / 8) & 1 ? TFT_WHITE : TFT_GREEN;
    int ox = tc * CELL + CELL / 2 + DX[want] * 9, oy = BOARD_Y + tr * CELL + CELL / 2 + DY[want] * 9;
    arrow(ox, oy, want, 5, 4, col);
  }

  // Ghosts
  int numG = min(3, 1 + level);
  for (int i = 0; i < numG; i++) {
    const Ghost &g = ghosts[i];
    int gx = g.m.x / SUB, gy = BOARD_Y + g.m.y / SUB;
    if (g.home > 0) {                                     // just eyes, going home
      gx = 4 * CELL + CELL / 2;
      gy = BOARD_Y + 4 * CELL + CELL / 2;
      spr.fillCircle(gx - 2, gy - 1, 2, TFT_WHITE);
      spr.fillCircle(gx + 2, gy - 1, 2, TFT_WHITE);
      continue;
    }
    uint16_t col = frightT > 0 ? ((frightT < 60 && (frightT & 8)) ? TFT_WHITE : TFT_BLUE) : g.col;
    spr.fillCircle(gx, gy, 5, col);
    spr.fillRect(gx - 5, gy, 11, 5, col);
    spr.fillRect(gx - 2, gy - 2, 2, 2, TFT_WHITE);
    spr.fillRect(gx + 1, gy - 2, 2, 2, TFT_WHITE);
  }

  // Hero: chomping while he moves
  int hx = hero.x / SUB, hy = BOARD_Y + hero.y / SUB, d = hero.dir;
  static const int8_t MOUTH[4] = {0, 2, 4, 2};
  int open = hero.moving && readyT == 0 ? MOUTH[(animT / 3) & 3] : 2;
  spr.fillCircle(hx, hy, 5, TFT_YELLOW);
  spr.fillTriangle(hx, hy, hx + DX[d] * 6 + DY[d] * open, hy + DY[d] * 6 + DX[d] * open,
                   hx + DX[d] * 6 - DY[d] * open, hy + DY[d] * 6 - DX[d] * open, TFT_BLACK);
  if (want >= 0) arrow(hx + DX[want] * 11, hy + DY[want] * 11, want, 4, 3, TFT_WHITE);

  drawScoreHeader(score, hiScore);
  for (int i = 0; i < lives; i++) spr.fillCircle(128 - i * 9, 9, 3, TFT_YELLOW);
  if (state == PLAY && readyT > 0) {
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(TFT_YELLOW, TFT_BLACK);
    spr.drawString("READY!", 67, BOARD_Y + 6 * CELL + CELL / 2, 2);
  }
  if (state == GAMEOVER) drawGameOverBox("CAUGHT!", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_YELLOW);
  spr.drawString("MAZE", 67, 14, 4);

  // the control idea as a picture: L = left / up, R = right / down
  spr.fillCircle(67, 66, 6, TFT_YELLOW);
  spr.fillTriangle(67, 66, 73, 63, 73, 69, TFT_BLACK);
  arrow(50, 66, 3, 5, 4, TFT_GREEN);
  arrow(84, 66, 1, 5, 4, TFT_GREEN);
  arrow(67, 49, 0, 5, 4, TFT_GREEN);
  arrow(67, 83, 2, 5, 4, TFT_GREEN);
  spr.setTextColor(TFT_YELLOW);
  spr.drawString("L", 41, 46, 2);
  spr.drawString("R", 93, 72, 2);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Tap L", "Tap R", "Hold"};
  const char *acts[] = {"left / up", "right / down", "U-turn"};
  for (int i = 0; i < 3; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 6, 98 + i * 18, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 50, 98 + i * 18, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("A turn waits for the", 67, 156, 1);
  spr.drawString("next opening (arrow).", 67, 166, 1);
  spr.drawString("Eat dots, dodge ghosts", 67, 178, 1);

  snprintf(buf, sizeof buf, "Best %lu", (unsigned long)hiScore);
  spr.drawString(buf, 67, 194, 2);
  spr.setTextColor(TFT_GREEN);
  spr.drawString("Press a button", 67, 216, 2);
}

static void render() {
  if (state == TITLE) drawTitle();
  else drawGame();
  spr.pushSprite(0, 0);
}

void drawIcon(int x, int y, bool dim, uint32_t t) {
  auto c = [dim](uint16_t col) { return dim ? tft.alphaBlend(80, col, TFT_BLACK) : col; };
  spr.fillCircle(x - 4, y, 6, c(TFT_YELLOW));
  int mouth = !dim && ((t / 200) & 1) ? 4 : 1;
  spr.fillTriangle(x - 4, y, x + 3, y - mouth, x + 3, y + mouth, TFT_BLACK);
  spr.fillCircle(x + 8, y, 2, c(TFT_WHITE));
}

void begin() {
  hiScore = prefs.getUInt("maze_hi", 0);
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
    if ((int32_t)(now - lastTick) > 200) lastTick = now - TICK_MS;   // no catching up after a stall
    qL.push(eL);
    qR.push(eR);
    while (state == PLAY && (int32_t)(now - lastTick) >= (int32_t)TICK_MS) {
      lastTick += TICK_MS;
      tick(now, qL.pop(), qR.pop());
      dirty = true;
    }
  } else {
    qL.clear();
    qR.clear();
    if (startOrEnd(state == GAMEOVER, eL, eR, armed) == END_PLAY) newGame();
    if (idleTooLong(now, lastInput)) goToSleep();
  }

  if (dirty) {
    render();
    dirty = false;
  }
}

}  // namespace maze

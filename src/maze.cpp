// Maze (Dot Chaser)
//
// Controls (two buttons):
//   Tap L / R : turn left / right (relative to hero, queues for next corner)
//   Hold L/R  : U-turn (180 deg)
// Eat all dots and power pellets while dodging ghosts! 3 lives.

#include "common.h"

namespace maze {

static const int COLS = 9, ROWS = 13, CELL = 15, BOARD_Y = 42;
static const int DX[4] = {0, 1, 0, -1};                    // up, right, down, left
static const int DY[4] = {-1, 0, 1, 0};
static const uint32_t HOLD_MS = 220, TICK_MS = 16;

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

struct Ghost { float x, y; int dir; uint16_t col; };

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static uint8_t dots[ROWS][COLS];                          // 0 = empty, 1 = dot, 2 = power pellet
static float px, py;
static int pDir, queuedTurn;
static Ghost ghosts[3];
static int lives, level, dotsLeft, frightT;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static bool isWall(int r, int c) {
  if (r < 0 || r >= ROWS || c < 0 || c >= COLS) return true;
  return MAP[r][c] == 1;
}

static void resetPositions() {
  px = 4 * CELL + CELL / 2.0f;
  py = 8 * CELL + CELL / 2.0f;
  pDir = 1;
  queuedTurn = 0;
  frightT = 0;
  const uint16_t gCol[3] = {TFT_RED, TFT_CYAN, TFT_MAGENTA};
  for (int i = 0; i < 3; i++) {
    ghosts[i] = {(3 + i) * CELL + CELL / 2.0f, 4 * CELL + CELL / 2.0f, (i & 1) ? 1 : 3, gCol[i]};
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

static void moveEntity(float &x, float &y, int &dir, float spd, int &turnQ) {
  int c = (int)(x / CELL), r = (int)(y / CELL);
  float cx = c * CELL + CELL / 2.0f, cy = r * CELL + CELL / 2.0f;
  bool atCenter = fabsf(x - cx) <= spd && fabsf(y - cy) <= spd;

  if (atCenter) {
    if (turnQ != 0) {
      int nd = (dir + turnQ + 4) & 3;
      if (!isWall(r + DY[nd], c + DX[nd])) {
        x = cx; y = cy;
        dir = nd;
        turnQ = 0;
      }
    }
    if (isWall(r + DY[dir], c + DX[dir])) {
      x = cx; y = cy;
      return;
    }
  }
  x += DX[dir] * spd;
  y += DY[dir] * spd;
}

static void tick(uint32_t now, int eL, int eR) {
  if ((bL.stable && !bL.consumed && (int32_t)(now - bL.t0) >= (int32_t)HOLD_MS)) {
    bL.consumed = true;
    pDir = (pDir + 2) & 3;
    queuedTurn = 0;
  }
  if ((bR.stable && !bR.consumed && (int32_t)(now - bR.t0) >= (int32_t)HOLD_MS)) {
    bR.consumed = true;
    pDir = (pDir + 2) & 3;
    queuedTurn = 0;
  }
  if (eL == 2 && !bL.consumed) queuedTurn = -1;
  if (eR == 2 && !bR.consumed) queuedTurn = 1;

  if (frightT > 0) frightT--;

  moveEntity(px, py, pDir, 1.35f, queuedTurn);

  int pr = constrain((int)(py / CELL), 0, ROWS - 1);
  int pc = constrain((int)(px / CELL), 0, COLS - 1);
  if (dots[pr][pc]) {
    if (dots[pr][pc] == 2) { frightT = 260; score += 50; }
    else score += 10;
    dots[pr][pc] = 0;
    if (--dotsLeft <= 0) {
      score += 200;
      level++;
      initMaze();
      return;
    }
  }

  // Ghosts
  float gSpd = min(1.3f, 0.78f + level * 0.08f) * (frightT > 0 ? 0.6f : 1.0f);
  int numG = min(3, 1 + level);
  for (int i = 0; i < numG; i++) {
    Ghost &g = ghosts[i];
    int gr = (int)(g.y / CELL), gc = (int)(g.x / CELL);
    float cx = gc * CELL + CELL / 2.0f, cy = gr * CELL + CELL / 2.0f;
    if (fabsf(g.x - cx) <= gSpd && fabsf(g.y - cy) <= gSpd) {
      g.x = cx; g.y = cy;
      int opts[4], nOpt = 0;
      for (int d = 0; d < 4; d++) {
        if (d == ((g.dir + 2) & 3)) continue;
        if (!isWall(gr + DY[d], gc + DX[d])) opts[nOpt++] = d;
      }
      if (nOpt > 0) {
        if (rnd(100) < 60) {
          int bestD = opts[0], bestDist = frightT ? -1 : 99999;
          for (int k = 0; k < nOpt; k++) {
            int d = opts[k];
            int dist = abs((gc + DX[d]) - pc) + abs((gr + DY[d]) - pr);
            if (frightT ? (dist > bestDist) : (dist < bestDist)) {
              bestDist = dist;
              bestD = d;
            }
          }
          g.dir = bestD;
        } else {
          g.dir = opts[rnd(nOpt)];
        }
      } else {
        g.dir = (g.dir + 2) & 3;
      }
    }
    g.x += DX[g.dir] * gSpd;
    g.y += DY[g.dir] * gSpd;

    float dx = g.x - px, dy = g.y - py;
    if (dx * dx + dy * dy < 64.0f) {
      if (frightT > 0) {
        score += 100;
        g.x = 4 * CELL + CELL / 2.0f;
        g.y = 4 * CELL + CELL / 2.0f;
      } else {
        if (--lives <= 0) { gameOver(); return; }
        resetPositions();
        return;
      }
    }
  }
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
        spr.fillCircle(x + CELL / 2, y + CELL / 2, 3, TFT_WHITE);
      }
    }
  }

  // Ghosts
  int numG = min(3, 1 + level);
  for (int i = 0; i < numG; i++) {
    uint16_t col = frightT > 0 ? ((frightT < 60 && (frightT & 8)) ? TFT_WHITE : TFT_BLUE) : ghosts[i].col;
    spr.fillCircle((int)ghosts[i].x, BOARD_Y + (int)ghosts[i].y, 5, col);
    spr.fillRect((int)ghosts[i].x - 5, BOARD_Y + (int)ghosts[i].y, 11, 5, col);
  }

  // Hero
  int hx = (int)px, hy = BOARD_Y + (int)py;
  spr.fillCircle(hx, hy, 5, TFT_YELLOW);
  spr.fillTriangle(hx, hy, hx + DX[pDir] * 6 + DY[pDir] * 3, hy + DY[pDir] * 6 + DX[pDir] * 3,
                   hx + DX[pDir] * 6 - DY[pDir] * 3, hy + DY[pDir] * 6 - DX[pDir] * 3, TFT_BLACK);

  drawScoreHeader(score, hiScore);
  for (int i = 0; i < lives; i++) spr.fillCircle(128 - i * 9, 9, 3, TFT_YELLOW);
  if (state == GAMEOVER) drawGameOverBox("CAUGHT!", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_YELLOW);
  spr.drawString("MAZE", 67, 18, 4);

  spr.fillCircle(52, 62, 7, TFT_YELLOW);
  spr.fillCircle(82, 62, 6, TFT_RED);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Tap L/R", "Hold L/R"};
  const char *acts[] = {"turn left/right", "U-turn"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 68, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Eat dots, dodge ghosts!", 67, 148, 1);

  snprintf(buf, sizeof buf, "Best %lu", (unsigned long)hiScore);
  spr.drawString(buf, 67, 190, 2);
  spr.setTextColor(TFT_GREEN);
  spr.drawString("Press a button", 67, 214, 2);
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
    if (now - lastTick > 200) lastTick = now - TICK_MS;
    qL.push(eL);
    qR.push(eR);
    while (state == PLAY && now - lastTick >= TICK_MS) {
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

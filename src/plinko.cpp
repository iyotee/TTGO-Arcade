// Plinko (Pegboard Ball Drop)
//
// Controls (two buttons):
//   Tap L or R : drop ball from the sweeping launcher
//   Hold L / R : nudge falling balls left / right
// 12 balls per game; center 100-pt slot awards a bonus ball!

#include "common.h"

namespace plinko {

static const int MAX_BALLS = 6, PEG_ROWS = 7;
static const uint32_t TICK_MS = 16;

struct Ball { float x, y, vx, vy; bool active; };

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static Ball balls[MAX_BALLS];
static float dropX, dropVx;
static int ballsLeft, activeBalls, coolT;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("plinko_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  ballsLeft = 12;
  activeBalls = 0;
  coolT = 0;
  dropX = 67;
  dropVx = 1.6f;
  newHigh = false;
  for (int i = 0; i < MAX_BALLS; i++) balls[i].active = false;
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void tick(int eL, int eR) {
  if (coolT > 0) coolT--;
  dropX += dropVx;
  if (dropX < 18) { dropX = 18; dropVx = fabsf(dropVx); }
  if (dropX > 117) { dropX = 117; dropVx = -fabsf(dropVx); }

  if ((eL == 1 || eR == 1) && ballsLeft > 0 && coolT == 0) {
    for (int i = 0; i < MAX_BALLS; i++) {
      if (!balls[i].active) {
        balls[i] = {dropX, 52.0f, (rnd(3) - 1) * 0.15f, 0.2f, true};
        ballsLeft--;
        activeBalls++;
        coolT = 12;
        break;
      }
    }
  }

  float nudge = 0;
  if (bL.stable && !bR.stable) nudge = -0.025f;
  if (bR.stable && !bL.stable) nudge = 0.025f;

  const int pts[5] = {10, 30, 100, 30, 10};
  for (int i = 0; i < MAX_BALLS; i++) {
    Ball &b = balls[i];
    if (!b.active) continue;
    b.vx += nudge;
    b.vy = min(4.2f, b.vy + 0.11f);
    b.x += b.vx;
    b.y += b.vy;

    if (b.x < 5) { b.x = 5; b.vx = fabsf(b.vx) * 0.7f; }
    if (b.x > 130) { b.x = 130; b.vx = -fabsf(b.vx) * 0.7f; }

    // Peg collisions
    for (int r = 0; r < PEG_ROWS; r++) {
      int py = 68 + r * 20;
      int cols = (r & 1) ? 5 : 6;
      int x0 = (r & 1) ? 23 : 12;
      for (int c = 0; c < cols; c++) {
        int px = x0 + c * 22;
        float dx = b.x - px, dy = b.y - py;
        float d2 = dx * dx + dy * dy;
        if (d2 < 36.0f && d2 > 0.01f) {
          float d = sqrtf(d2);
          float nx = dx / d, ny = dy / d;
          b.x = px + nx * 6.0f;
          b.y = py + ny * 6.0f;
          float vn = b.vx * nx + b.vy * ny;
          if (vn < 0) {
            b.vx -= 1.55f * vn * nx + (rnd(3) - 1) * 0.06f;
            b.vy -= 1.55f * vn * ny;
          }
        }
      }
    }

    if (b.y >= 220) {
      int slot = constrain((int)(b.x / 27), 0, 4);
      score += pts[slot];
      if (slot == 2) ballsLeft++;                           // center jackpot gives free ball
      b.active = false;
      activeBalls--;
      if (ballsLeft == 0 && activeBalls == 0) { gameOver(); return; }
    }
  }
}

static void drawGame() {
  char buf[16];
  spr.fillSprite(0x0842);

  // Sweeping dropper
  spr.fillTriangle((int)dropX - 5, 44, (int)dropX + 5, 44, (int)dropX, 51, TFT_YELLOW);

  // Pegs
  for (int r = 0; r < PEG_ROWS; r++) {
    int py = 68 + r * 20;
    int cols = (r & 1) ? 5 : 6;
    int x0 = (r & 1) ? 23 : 12;
    for (int c = 0; c < cols; c++) {
      spr.fillCircle(x0 + c * 22, py, 2, TFT_LIGHTGREY);
    }
  }

  // Bottom buckets
  const char *lbl[5] = {"10", "30", "100", "30", "10"};
  const uint16_t bcol[5] = {0x2945, 0x03EF, TFT_ORANGE, 0x03EF, 0x2945};
  for (int i = 0; i < 5; i++) {
    spr.fillRect(i * 27 + 1, 222, 25, 16, bcol[i]);
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(lbl[i], i * 27 + 13, 230, 1);
  }

  for (int i = 0; i < MAX_BALLS; i++) {
    if (balls[i].active) spr.fillCircle((int)balls[i].x, (int)balls[i].y, 3, TFT_WHITE);
  }

  drawScoreHeader(score, hiScore);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(TFT_YELLOW);
  snprintf(buf, sizeof buf, "Balls %d", ballsLeft);
  spr.drawString(buf, 132, 6, 2);

  if (state == GAMEOVER) drawGameOverBox("ALL DROPPED", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_YELLOW);
  spr.drawString("PLINKO", 67, 18, 4);

  for (int i = 0; i < 3; i++) spr.fillCircle(47 + i * 20, 62, 3, TFT_LIGHTGREY);
  spr.fillCircle(67, 50, 4, TFT_WHITE);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Tap L/R", "Hold L/R"};
  const char *acts[] = {"drop ball", "nudge"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 68, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("100 slot gives a free ball!", 67, 148, 1);

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
  spr.fillCircle(x - 6, y, 2, c(TFT_LIGHTGREY));
  spr.fillCircle(x + 6, y, 2, c(TFT_LIGHTGREY));
  spr.fillCircle(x, y + 7, 2, c(TFT_LIGHTGREY));
  int by = y - 10 + ((t / 40) % 20);
  spr.fillCircle(x + (by > y ? 4 : 0), by, 2, c(TFT_WHITE));
}

void begin() {
  hiScore = prefs.getUInt("plinko_hi", 0);
  state = TITLE;
  armed = false;
  lastInput = millis();
  dirty = true;
}

void update(uint32_t now) {
  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) lastInput = now;

  if (state == PLAY) {
    if (now - lastTick > 200) lastTick = now - TICK_MS;
    bool first = true;
    while (state == PLAY && now - lastTick >= TICK_MS) {
      lastTick += TICK_MS;
      tick(first ? eL : 0, first ? eR : 0);
      first = false;
      dirty = true;
    }
  } else {
    if (startOrEnd(state == GAMEOVER, eL, eR, armed) == END_PLAY) newGame();
    if (idleTooLong(now, lastInput)) goToSleep();
  }

  if (dirty) {
    render();
    dirty = false;
  }
}

}  // namespace plinko

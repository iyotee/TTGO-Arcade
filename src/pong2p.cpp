// Pong 2P (Local 2-Player)
//
// Controls (one button per player):
//   Tap L : Player 1 (bottom, Cyan) flips direction
//   Tap R : Player 2 (top, Magenta) flips direction
// First player to 5 points wins! Best score tracks your longest rally.

#include "common.h"

namespace pong2p {

static const int PAD_W = 32, PAD_H = 5;
static const int P1_Y = 226, P2_Y = 48;
static const int WIN_PTS = 5;
static const uint32_t TICK_MS = 16;

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static float p1x, p2x, p1dir, p2dir;
static float bx, by, vx, vy;
static int pts1, pts2, rally, serveT;
static uint32_t score = 0, hiScore = 0;                   // score = max rally this match
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static void serve(int dirY) {
  bx = 67;
  by = 138;
  float ang = (rnd(50) - 25) * DEG_TO_RAD;
  float spd = 2.4f;
  vx = spd * sinf(ang);
  vy = dirY * spd * cosf(ang);
  rally = 0;
  serveT = 35;
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("pong2p_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  pts1 = pts2 = 0;
  p1x = p2x = 67;
  p1dir = 1.0f;
  p2dir = -1.0f;
  newHigh = false;
  serve(rnd(2) ? 1 : -1);
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void tick(int eL, int eR) {
  if (eL == 1) p1dir = -p1dir;
  if (eR == 1) p2dir = -p2dir;

  p1x += p1dir * 2.5f;
  p2x += p2dir * 2.5f;
  if (p1x < PAD_W / 2) { p1x = PAD_W / 2; p1dir = 1.0f; }
  if (p1x > 135 - PAD_W / 2) { p1x = 135 - PAD_W / 2; p1dir = -1.0f; }
  if (p2x < PAD_W / 2) { p2x = PAD_W / 2; p2dir = 1.0f; }
  if (p2x > 135 - PAD_W / 2) { p2x = 135 - PAD_W / 2; p2dir = -1.0f; }

  if (serveT > 0) { serveT--; return; }

  bx += vx;
  by += vy;
  if (bx < 3) { bx = 3; vx = fabsf(vx); }
  if (bx > 132) { bx = 132; vx = -fabsf(vx); }

  if (vy > 0 && by >= P1_Y - 3 && by <= P1_Y + 4 && fabsf(bx - p1x) <= PAD_W / 2 + 3) {
    float off = constrain((bx - p1x) / (PAD_W / 2 + 3), -1.0f, 1.0f);
    float spd = min(4.6f, hypotf(vx, vy) + 0.14f);
    float ang = off * 55 * DEG_TO_RAD;
    vx = spd * sinf(ang);
    vy = -spd * cosf(ang);
    by = P1_Y - 3;
    rally++;
    if ((uint32_t)rally > score) score = rally;
  }

  if (vy < 0 && by <= P2_Y + PAD_H + 3 && by >= P2_Y && fabsf(bx - p2x) <= PAD_W / 2 + 3) {
    float off = constrain((bx - p2x) / (PAD_W / 2 + 3), -1.0f, 1.0f);
    float spd = min(4.6f, hypotf(vx, vy) + 0.14f);
    float ang = off * 55 * DEG_TO_RAD;
    vx = spd * sinf(ang);
    vy = spd * cosf(ang);
    by = P2_Y + PAD_H + 3;
    rally++;
    if ((uint32_t)rally > score) score = rally;
  }

  if (by > 242) {
    if (++pts2 >= WIN_PTS) { gameOver(); return; }
    serve(-1);
  } else if (by < HEADER_H) {
    if (++pts1 >= WIN_PTS) { gameOver(); return; }
    serve(1);
  }
}

static void drawGame() {
  char buf[24];
  spr.fillSprite(TFT_BLACK);
  spr.drawFastHLine(0, HEADER_H, 135, TFT_DARKGREY);
  for (int x = 4; x < 135; x += 10) spr.drawFastHLine(x, 138, 5, 0x39E7);

  spr.fillRoundRect((int)p2x - PAD_W / 2, P2_Y, PAD_W, PAD_H, 2, TFT_MAGENTA);
  spr.fillRoundRect((int)p1x - PAD_W / 2, P1_Y, PAD_W, PAD_H, 2, TFT_CYAN);
  spr.fillCircle((int)bx, (int)by, 3, TFT_WHITE);

  // Header: P1 vs P2 and Rally
  spr.fillRect(0, 0, 135, HEADER_H, TFT_BLACK);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_CYAN);
  snprintf(buf, sizeof buf, "P1:%d", pts1);
  spr.drawString(buf, 4, 4, 2);
  spr.setTextColor(TFT_MAGENTA);
  snprintf(buf, sizeof buf, "P2:%d", pts2);
  spr.drawString(buf, 48, 4, 2);
  spr.setTextColor(TFT_LIGHTGREY);
  snprintf(buf, sizeof buf, "Rally %d  Best %lu", rally, (unsigned long)hiScore);
  spr.drawString(buf, 4, 24, 1);

  if (state == GAMEOVER) {
    drawGameOverBox(pts1 >= WIN_PTS ? "P1 WINS!" : "P2 WINS!", score, hiScore, newHigh);
  }
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_CYAN);
  spr.drawString("PONG 2P", 67, 18, 4);

  spr.fillRoundRect(47, 54, 40, 5, 2, TFT_MAGENTA);
  spr.fillCircle(67, 68, 3, TFT_WHITE);
  spr.fillRoundRect(47, 78, 40, 5, 2, TFT_CYAN);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Tap L", "Tap R"};
  const char *acts[] = {"P1 flip dir", "P2 flip dir"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 98 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 60, 98 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("First to 5! Best = max rally", 67, 148, 1);

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
  spr.fillRect(x - 7, y - 11, 14, 3, c(TFT_MAGENTA));
  spr.fillRect(x - 7, y + 8, 14, 3, c(TFT_CYAN));
  int by = y - 6 + (int)roundf(12 * triWave(t, 900));
  spr.fillCircle(x, by, 2, c(TFT_WHITE));
}

void begin() {
  hiScore = prefs.getUInt("pong2p_hi", 0);
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

}  // namespace pong2p

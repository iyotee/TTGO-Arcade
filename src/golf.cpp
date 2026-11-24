// Golf (Mini Golf)
//
// Controls (two buttons):
//   Hold L / R : aim angle left / right
//   Hold L + R : charge power, release to putt!
// Sink each hole before running out of strokes (+3 strokes per hole cleared).

#include "common.h"

namespace golf {

static const uint32_t TICK_MS = 16;

struct Block { int x, y, w, h; };

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static float bx, by, vx, vy, aimAng, power, powerDir;
static int holeX, holeY, strokes, holeNum;
static Block blocks[3];
static int numBlocks;
static bool moving, charging;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static void genHole() {
  bx = 22 + rnd(90);
  by = 205 + rnd(15);
  vx = vy = 0;
  moving = false;
  charging = false;
  aimAng = -PI / 2;
  holeX = 22 + rnd(90);
  holeY = 58 + rnd(28);
  numBlocks = min(3, 1 + holeNum / 2);
  for (int i = 0; i < numBlocks; i++) {
    int bw = 28 + rnd(34);
    blocks[i] = {10 + rnd(115 - bw), 102 + i * 32, bw, 8};
  }
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("golf_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  holeNum = 1;
  strokes = 12;
  newHigh = false;
  genHole();
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void tick() {
  if (!moving) {
    if (bL.stable && bR.stable) {
      if (!charging) {
        charging = true;
        power = 0.1f;
        powerDir = 1.0f;
      } else {
        power += powerDir * 0.03f;
        if (power >= 1.0f) { power = 1.0f; powerDir = -1.0f; }
        if (power <= 0.1f) { power = 0.1f; powerDir = 1.0f; }
      }
    } else if (charging) {
      charging = false;
      if (strokes > 0) {
        strokes--;
        float spd = 1.6f + power * 4.4f;
        vx = cosf(aimAng) * spd;
        vy = sinf(aimAng) * spd;
        moving = true;
      }
    } else {
      if (bL.stable) aimAng -= 0.045f;
      if (bR.stable) aimAng += 0.045f;
    }
    return;
  }

  const int steps = 3;
  for (int s = 0; s < steps; s++) {
    bx += vx / steps;
    by += vy / steps;
    if (bx < 7) { bx = 7; vx = fabsf(vx) * 0.8f; }
    if (bx > 128) { bx = 128; vx = -fabsf(vx) * 0.8f; }
    if (by < HEADER_H + 7) { by = HEADER_H + 7; vy = fabsf(vy) * 0.8f; }
    if (by > 233) { by = 233; vy = -fabsf(vy) * 0.8f; }

    for (int i = 0; i < numBlocks; i++) {
      const Block &b = blocks[i];
      float cx = constrain(bx, (float)b.x, (float)(b.x + b.w));
      float cy = constrain(by, (float)b.y, (float)(b.y + b.h));
      float dx = bx - cx, dy = by - cy;
      if (dx * dx + dy * dy < 9.0f) {
        if (fabsf(dx) > fabsf(dy)) vx = -vx * 0.8f;
        else vy = -vy * 0.8f;
        bx += vx / steps;
        by += vy / steps;
      }
    }

    float dx = bx - holeX, dy = by - holeY;
    float spd2 = vx * vx + vy * vy;
    if (dx * dx + dy * dy < 30.0f && spd2 < 6.5f) {
      score++;
      holeNum++;
      strokes += 3;
      genHole();
      return;
    }
  }

  vx *= 0.968f;
  vy *= 0.968f;
  if (vx * vx + vy * vy < 0.006f) {
    vx = vy = 0;
    moving = false;
    if (strokes <= 0) gameOver();
  }
}

static void drawGame() {
  char buf[24];
  spr.fillSprite(0x1B83);                               // fairway green
  spr.drawRect(3, HEADER_H + 2, 129, 235 - HEADER_H, 0x8A22);

  // Hole & flag
  spr.fillCircle(holeX, holeY, 5, TFT_BLACK);
  spr.drawFastVLine(holeX, holeY - 14, 14, TFT_WHITE);
  spr.fillTriangle(holeX, holeY - 14, holeX + 8, holeY - 11, holeX, holeY - 8, TFT_RED);

  // Obstacles
  for (int i = 0; i < numBlocks; i++) {
    spr.fillRoundRect(blocks[i].x, blocks[i].y, blocks[i].w, blocks[i].h, 2, 0x8A22);
  }

  // Aim line & power bar
  if (!moving && state == PLAY) {
    int ax = (int)(bx + cosf(aimAng) * 22), ay = (int)(by + sinf(aimAng) * 22);
    spr.drawLine((int)bx, (int)by, ax, ay, TFT_YELLOW);
    if (charging) {
      spr.drawRect(14, 224, 107, 8, TFT_WHITE);
      spr.fillRect(16, 226, (int)(power * 103), 4, TFT_ORANGE);
    }
  }

  spr.fillCircle((int)bx, (int)by, 3, TFT_WHITE);

  drawScoreHeader(score, hiScore);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(strokes <= 2 ? TFT_RED : TFT_YELLOW);
  snprintf(buf, sizeof buf, "Shots %d", strokes);
  spr.drawString(buf, 132, 6, 2);

  if (state == GAMEOVER) drawGameOverBox("OUT OF SHOTS", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_GREEN);
  spr.drawString("GOLF", 67, 18, 4);

  spr.fillCircle(67, 68, 5, TFT_DARKGREY);
  spr.drawFastVLine(67, 50, 18, TFT_WHITE);
  spr.fillTriangle(67, 50, 77, 54, 67, 58, TFT_RED);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Hold L/R", "Hold L+R"};
  const char *acts[] = {"aim", "charge & shoot"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 68, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("+3 shots for every hole", 67, 148, 1);

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
  spr.fillCircle(x + 4, y + 8, 3, c(TFT_DARKGREY));
  spr.drawFastVLine(x + 4, y - 10, 18, c(TFT_WHITE));
  spr.fillTriangle(x + 4, y - 10, x + 11, y - 7, x + 4, y - 4, c(TFT_RED));
  int bx = x - 8 + (int)roundf(10 * triWave(t, 1400));
  spr.fillCircle(bx, y + 7, 2, c(TFT_WHITE));
}

void begin() {
  hiScore = prefs.getUInt("golf_hi", 0);
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
    while (state == PLAY && now - lastTick >= TICK_MS) {
      lastTick += TICK_MS;
      tick();
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

}  // namespace golf

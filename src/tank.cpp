// Tank (Artillery Duel)
//
// Controls (two buttons):
//   Hold L / R : adjust barrel angle
//   Hold L + R : charge power, release to fire!
// Account for wind and hills to destroy the red enemy tank! 3 HP.

#include "common.h"

namespace tank {

static const int COLS = 28;
static const uint32_t TICK_MS = 16;

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static int16_t hill[COLS];
static int pCol, eCol, pHp, eHp, roundNum;
static float pAng, power, powerDir, wind;
static bool charging, firing, pTurn;
static float sx, sy, svx, svy;
static int expT, expX, expY;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static void genTerrain() {
  pCol = 2 + rnd(4);
  eCol = 21 + rnd(4);
  int h = 190;
  for (int i = 0; i < COLS; i++) {
    if (abs(i - pCol) <= 1) hill[i] = hill[max(0, i - 1)];
    else if (abs(i - eCol) <= 1) hill[i] = hill[max(0, i - 1)];
    else h = constrain(h + rnd(25) - 12, 130, 222);
    hill[i] = h;
  }
  hill[pCol - 1] = hill[pCol + 1] = hill[pCol];
  hill[eCol - 1] = hill[eCol + 1] = hill[eCol];
  wind = (rnd(11) - 5) * 0.004f;
  eHp = 3;
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("tank_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  roundNum = 1;
  pHp = 3;
  pAng = 45.0f * DEG_TO_RAD;
  charging = firing = false;
  pTurn = true;
  expT = 0;
  newHigh = false;
  genTerrain();
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void launchShot(float x, float y, float ang, float pwr, int dir) {
  sx = x;
  sy = y - 6;
  float spd = 1.6f + pwr * 3.8f;
  svx = dir * cosf(ang) * spd;
  svy = -sinf(ang) * spd;
  firing = true;
}

static void tick() {
  if (expT > 0) expT--;

  if (!firing) {
    if (!pTurn) {
      // Enemy fires back
      float ex = eCol * 5 + 2, ey = hill[eCol];
      float err = max(0.08f, 0.35f - roundNum * 0.04f);
      float ang = (38 + rnd(26)) * DEG_TO_RAD;
      float pwr = constrain(0.55f + ((rnd(100) - 50) / 100.0f) * err, 0.25f, 0.95f);
      launchShot(ex, ey, ang, pwr, -1);
      return;
    }

    if (bL.stable && bR.stable) {
      if (!charging) { charging = true; power = 0.15f; powerDir = 1.0f; }
      else {
        power += powerDir * 0.025f;
        if (power >= 1.0f) { power = 1.0f; powerDir = -1.0f; }
        if (power <= 0.15f) { power = 0.15f; powerDir = 1.0f; }
      }
    } else if (charging) {
      charging = false;
      launchShot(pCol * 5 + 2, hill[pCol], pAng, power, 1);
    } else {
      if (bL.stable) pAng = min(82.0f * (float)DEG_TO_RAD, pAng + 0.025f);
      if (bR.stable) pAng = max(12.0f * (float)DEG_TO_RAD, pAng - 0.025f);
    }
    return;
  }

  // Projectile flight
  svx += wind;
  svy += 0.09f;
  sx += svx;
  sy += svy;

  if (sx < 0 || sx > 134 || sy > 240) {
    firing = false;
    pTurn = !pTurn;
    return;
  }

  int col = constrain((int)(sx / 5), 0, COLS - 1);
  float px = pCol * 5 + 2, py = hill[pCol] - 3;
  float ex = eCol * 5 + 2, ey = hill[eCol] - 3;
  bool hitGround = (sy >= hill[col]);
  bool hitP = hypotf(sx - px, sy - py) < 8;
  bool hitE = hypotf(sx - ex, sy - ey) < 8;

  if (hitGround || hitP || hitE) {
    firing = false;
    expX = (int)sx;
    expY = (int)sy;
    expT = 12;

    if (hypotf(sx - ex, sy - ey) < 12) {
      eHp--;
      score += 50;
      if (eHp <= 0) {
        score += 150;
        roundNum++;
        pHp = min(3, pHp + 1);
        genTerrain();
        pTurn = true;
        return;
      }
    }
    if (hypotf(sx - px, sy - py) < 12) {
      if (--pHp <= 0) { gameOver(); return; }
    }
    // Crater
    if (hitGround) hill[col] = min(232, hill[col] + 5);
    pTurn = !pTurn;
  }
}

static void drawGame() {
  char buf[20];
  spr.fillSprite(0x0842);

  for (int i = 0; i < COLS - 1; i++) {
    spr.fillRect(i * 5, hill[i], 5, 240 - hill[i], 0x3C82);
  }

  // Player tank
  int px = pCol * 5 + 2, py = hill[pCol];
  spr.fillRect(px - 6, py - 5, 12, 5, TFT_CYAN);
  spr.drawLine(px, py - 4, px + (int)(cosf(pAng) * 10), py - 4 - (int)(sinf(pAng) * 10), TFT_WHITE);

  // Enemy tank
  int ex = eCol * 5 + 2, ey = hill[eCol];
  spr.fillRect(ex - 6, ey - 5, 12, 5, TFT_RED);
  spr.drawLine(ex, ey - 4, ex - 7, ey - 10, TFT_ORANGE);

  if (firing) spr.fillCircle((int)sx, (int)sy, 2, TFT_YELLOW);
  if (expT > 0) spr.fillCircle(expX, expY, 13 - expT, TFT_ORANGE);

  if (charging) {
    spr.drawRect(14, 224, 107, 8, TFT_WHITE);
    spr.fillRect(16, 226, (int)(power * 103), 4, TFT_RED);
  }

  drawScoreHeader(score, hiScore);
  for (int i = 0; i < pHp; i++) spr.fillRect(96 + i * 11, 7, 8, 4, TFT_CYAN);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  int w = (int)roundf(wind * 1000);
  snprintf(buf, sizeof buf, "W:%s%d", w > 0 ? ">" : w < 0 ? "<" : "", abs(w));
  spr.drawString(buf, 132, 20, 1);

  if (state == GAMEOVER) drawGameOverBox("DESTROYED!", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_CYAN);
  spr.drawString("TANK", 67, 18, 4);

  spr.fillRect(55, 62, 24, 8, TFT_CYAN);
  spr.drawLine(67, 62, 79, 50, TFT_WHITE);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Hold L/R", "Hold L+R"};
  const char *acts[] = {"aim barrel", "charge & fire"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 68, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Watch the wind!", 67, 148, 1);

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
  spr.fillRect(x - 8, y + 2, 16, 6, c(TFT_CYAN));
  spr.drawLine(x, y + 2, x + 8, y - 6, c(TFT_WHITE));
  if (!dim && ((t / 350) & 1)) spr.fillCircle(x + 10, y - 8, 2, TFT_ORANGE);
}

void begin() {
  hiScore = prefs.getUInt("tank_hi", 0);
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

}  // namespace tank

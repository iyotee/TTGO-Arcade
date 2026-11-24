// Tank (Artillery Duel)
//
// Controls (two buttons):
//   Hold L / R : adjust barrel angle
//   Hold L + R : charge power, release to fire!
// Account for wind and hills to destroy the red enemy tank! Both tanks have
// 3 HP; destroying the enemy starts a new round (+1 HP back, smarter enemy).

#include "common.h"

namespace tank {

static const int COLS = 28;
static const uint32_t TICK_MS = 16;

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static int16_t hill[COLS];
static int pCol, eCol, pHp, eHp, roundNum;
static float pAng, power, powerDir, wind;
static bool charging, firing, pTurn, aimLock, shotByPlayer;
static int waitT, shotAge;                 // pause between turns; ticks since launch
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
    // flat ground under (and next to) each tank
    if (abs(i - pCol) > 1 && abs(i - eCol) > 1) h = constrain(h + rnd(25) - 12, 130, 222);
    hill[i] = h;
  }
  hill[pCol - 1] = hill[pCol + 1] = hill[pCol];
  hill[eCol - 1] = hill[eCol + 1] = hill[eCol];
  wind = (rnd(11) - 5) * 0.004f;
  eHp = 3;
  waitT = 30;
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
  charging = firing = aimLock = false;
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
  shotByPlayer = dir > 0;
  shotAge = 0;
}

// Where a shot would come down (x), using the same physics as the real one.
static float landingX(float x, float y, float ang, float pwr, int dir) {
  float spd = 1.6f + pwr * 3.8f;
  float vx = dir * cosf(ang) * spd, vy = -sinf(ang) * spd;
  y -= 6;
  for (int i = 0; i < 600; i++) {
    vx += wind;
    vy += 0.09f;
    x += vx;
    y += vy;
    if (x < 0 || x > 134) return x;
    if (y >= hill[constrain((int)(x / 5), 0, COLS - 1)]) return x;
  }
  return x;
}

static void enemyFire() {
  float ex = eCol * 5 + 2, ey = hill[eCol], target = pCol * 5 + 2;
  float ang = (38 + rnd(26)) * DEG_TO_RAD;
  // best power for this angle, then miss by an error that shrinks each round
  float bestP = 0.55f, bestErr = 1e9f;
  for (int i = 0; i <= 40; i++) {
    float p = 0.15f + i * 0.85f / 40;
    float e = fabsf(landingX(ex, ey, ang, p, -1) - target);
    if (e < bestErr) { bestErr = e; bestP = p; }
  }
  float err = max(0.03f, 0.16f - roundNum * 0.02f);
  float pwr = constrain(bestP + ((rnd(201) - 100) / 100.0f) * err, 0.15f, 1.0f);
  launchShot(ex, ey, ang, pwr, -1);
}

static void endTurn() {
  pTurn = !pTurn;
  waitT = 45;
}

static void tick() {
  if (expT > 0) expT--;

  if (!firing) {
    if (waitT > 0) { waitT--; charging = false; return; }
    if (!pTurn) { enemyFire(); return; }

    // after a shot, wait until both buttons are up before aiming again
    if (aimLock) {
      if (!bL.stable && !bR.stable) aimLock = false;
      return;
    }
    if (bL.stable && bR.stable) {
      if (!charging) { charging = true; power = 0.15f; powerDir = 1.0f; }
      else {
        power += powerDir * 0.02f;
        if (power >= 1.0f) { power = 1.0f; powerDir = -1.0f; }
        if (power <= 0.15f) { power = 0.15f; powerDir = 1.0f; }
      }
    } else if (charging) {
      charging = false;
      aimLock = true;
      launchShot(pCol * 5 + 2, hill[pCol], pAng, power, 1);
    } else {
      if (bL.stable) pAng = min(82.0f * (float)DEG_TO_RAD, pAng + 0.02f);
      if (bR.stable) pAng = max(12.0f * (float)DEG_TO_RAD, pAng - 0.02f);
    }
    return;
  }

  // Projectile flight
  shotAge++;
  svx += wind;
  svy += 0.09f;
  sx += svx;
  sy += svy;

  if (sx < 0 || sx > 134 || sy > 240) {
    firing = false;
    endTurn();
    return;
  }

  int col = constrain((int)(sx / 5), 0, COLS - 1);
  float px = pCol * 5 + 2, py = hill[pCol] - 3;
  float ex = eCol * 5 + 2, ey = hill[eCol] - 3;
  // a fresh shell can't hit the tank that fired it
  bool armedShell = shotAge > 12;
  bool hitGround = sy >= hill[col];
  bool hitP = (armedShell || !shotByPlayer) && hypotf(sx - px, sy - py) < 8;
  bool hitE = (armedShell || shotByPlayer) && hypotf(sx - ex, sy - ey) < 8;

  if (hitGround || hitP || hitE) {
    firing = false;
    expX = (int)sx;
    expY = (int)sy;
    expT = 12;

    if ((armedShell || shotByPlayer) && hypotf(sx - ex, sy - ey) < 12) {
      eHp--;
      if (shotByPlayer) score += 50;
      if (eHp <= 0) {
        score += 150;
        roundNum++;
        pHp = min(3, pHp + 1);
        genTerrain();
        pTurn = true;
        return;
      }
    }
    if ((armedShell || !shotByPlayer) && hypotf(sx - px, sy - py) < 12) {
      if (--pHp <= 0) { gameOver(); return; }
    }
    // Crater
    if (hitGround) {
      hill[col] = min(232, hill[col] + 5);
      if (col > 0) hill[col - 1] = min(232, hill[col - 1] + 2);
      if (col < COLS - 1) hill[col + 1] = min(232, hill[col + 1] + 2);
    }
    endTurn();
  }
}

static void drawGame() {
  char buf[20];
  spr.fillSprite(0x0842);

  for (int i = 0; i < COLS; i++) {
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
  // HP: yours (cyan) above the enemy's (red)
  for (int i = 0; i < pHp; i++) spr.fillRect(99 + i * 11, 4, 8, 4, TFT_CYAN);
  for (int i = 0; i < eHp; i++) spr.fillRect(99 + i * 11, 11, 8, 4, TFT_RED);
  if (state == PLAY && !firing && waitT == 0 && pTurn) {
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_YELLOW);
    spr.drawString("YOUR TURN", 67, 44, 1);
  }
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  int w = (int)roundf(wind * 1000);
  snprintf(buf, sizeof buf, "W:%s%d", w > 0 ? ">" : w < 0 ? "<" : "", abs(w));
  spr.drawString(buf, 132, 24, 1);

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

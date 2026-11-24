// River (River Raid)
//
// Controls (two buttons):
//   Hold L / R : steer jet left / right
//   L + R      : speed boost
// Jet fires automatically. Fly OVER pink FUEL tanks to refuel (don't shoot them!).

#include "common.h"

namespace river {

static const int SLICES = 54;                             // 54 x 4 px = 216 px
static const int SLICE_H = 4;
static const int MAX_OBJ = 10, MAX_BUL = 6;
static const int JET_Y = 214;
static const uint32_t TICK_MS = 16;

enum ObjKind : uint8_t { O_HELI, O_SHIP, O_FUEL };
struct Obj { float x, y, vx; ObjKind kind; bool active; };
struct Bul { float x, y; bool active; };

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static int16_t bankL[SLICES], bankR[SLICES];
static int curL, curR, spawnT, fireT;
static float jetX, fuel, scrollAcc;
static Obj objs[MAX_OBJ];
static Bul buls[MAX_BUL];
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static void genSlice(int idx) {
  int minW = max(56, 86 - (int)(score / 80));
  curL = constrain(curL + rnd(7) - 3, 6, 42);
  curR = constrain(curR + rnd(7) - 3, curL + minW, 129);
  bankL[idx] = curL;
  bankR[idx] = curR;
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("river_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  jetX = 67;
  fuel = 100.0f;
  scrollAcc = 0;
  curL = 18;
  curR = 117;
  spawnT = 20;
  fireT = 0;
  newHigh = false;
  for (int i = 0; i < SLICES; i++) { bankL[i] = curL; bankR[i] = curR; }
  for (int i = 0; i < MAX_OBJ; i++) objs[i].active = false;
  for (int i = 0; i < MAX_BUL; i++) buls[i].active = false;
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void tick() {
  if (bL.stable && !bR.stable) jetX -= 2.4f;
  if (bR.stable && !bL.stable) jetX += 2.4f;

  float spd = (bL.stable && bR.stable) ? 3.2f : 1.7f;
  fuel -= 0.085f;
  if (fuel <= 0) { fuel = 0; gameOver(); return; }

  scrollAcc += spd;
  while (scrollAcc >= SLICE_H) {
    scrollAcc -= SLICE_H;
    for (int i = SLICES - 1; i > 0; i--) {
      bankL[i] = bankL[i - 1];
      bankR[i] = bankR[i - 1];
    }
    genSlice(0);
  }

  // Bank collision at jet position
  int jSlice = constrain((JET_Y - HEADER_H) / SLICE_H, 0, SLICES - 1);
  if (jetX - 5 <= bankL[jSlice] || jetX + 5 >= bankR[jSlice]) {
    gameOver();
    return;
  }

  // Auto-fire
  if (++fireT >= 15) {
    fireT = 0;
    for (int i = 0; i < MAX_BUL; i++) {
      if (!buls[i].active) {
        buls[i] = {jetX, (float)JET_Y - 8, true};
        break;
      }
    }
  }

  if (--spawnT <= 0) {
    spawnT = 22 + rnd(18);
    for (int i = 0; i < MAX_OBJ; i++) {
      if (objs[i].active) continue;
      int w = bankR[0] - bankL[0];
      float ox = bankL[0] + 12 + rnd(max(1, w - 24));
      int r = rnd(100);
      ObjKind k = (r < 28 || fuel < 35.0f) ? O_FUEL : (r < 64) ? O_HELI : O_SHIP;
      float vx = (k == O_FUEL) ? 0.0f : ((rnd(2) ? 1 : -1) * (k == O_HELI ? 0.8f : 0.45f));
      objs[i] = {ox, (float)HEADER_H - 6, vx, k, true};
      break;
    }
  }

  for (int i = 0; i < MAX_BUL; i++) {
    if (!buls[i].active) continue;
    buls[i].y -= 5.0f;
    if (buls[i].y < HEADER_H) buls[i].active = false;
  }

  for (int i = 0; i < MAX_OBJ; i++) {
    if (!objs[i].active) continue;
    objs[i].y += spd;
    objs[i].x += objs[i].vx;
    int sIdx = constrain(((int)objs[i].y - HEADER_H) / SLICE_H, 0, SLICES - 1);
    if (objs[i].x - 7 <= bankL[sIdx]) objs[i].vx = fabsf(objs[i].vx);
    if (objs[i].x + 7 >= bankR[sIdx]) objs[i].vx = -fabsf(objs[i].vx);
    if (objs[i].y > 246) { objs[i].active = false; continue; }

    for (int b = 0; b < MAX_BUL; b++) {
      if (!buls[b].active) continue;
      if (fabsf(buls[b].x - objs[i].x) < 8 && fabsf(buls[b].y - objs[i].y) < 7) {
        buls[b].active = false;
        objs[i].active = false;
        if (objs[i].kind != O_FUEL) score += (objs[i].kind == O_HELI ? 60 : 30);
        break;
      }
    }
    if (!objs[i].active) continue;

    if (fabsf(objs[i].x - jetX) < 9 && fabsf(objs[i].y - JET_Y) < 9) {
      if (objs[i].kind == O_FUEL) {
        fuel = min(100.0f, fuel + 1.8f);
      } else {
        gameOver();
        return;
      }
    }
  }
}

static void drawGame() {
  spr.fillSprite(0x0012);                               // deep river blue

  for (int i = 0; i < SLICES; i++) {
    int y = HEADER_H + i * SLICE_H;
    spr.fillRect(0, y, bankL[i], SLICE_H, 0x1B42);
    spr.fillRect(bankR[i], y, 135 - bankR[i], SLICE_H, 0x1B42);
  }

  for (int i = 0; i < MAX_OBJ; i++) {
    if (!objs[i].active) continue;
    int x = (int)objs[i].x, y = (int)objs[i].y;
    if (objs[i].kind == O_FUEL) {
      spr.fillRect(x - 5, y - 7, 11, 14, TFT_MAGENTA);
      spr.fillRect(x - 3, y - 5, 7, 10, TFT_WHITE);
      spr.setTextDatum(MC_DATUM);
      spr.setTextColor(TFT_RED);
      spr.drawString("F", x, y, 1);
    } else if (objs[i].kind == O_HELI) {
      spr.fillRoundRect(x - 6, y - 3, 12, 6, 2, TFT_YELLOW);
      spr.drawFastHLine(x - 8, y - 4, 16, TFT_WHITE);
    } else {
      spr.fillRect(x - 7, y - 3, 14, 6, TFT_ORANGE);
    }
  }

  for (int i = 0; i < MAX_BUL; i++) {
    if (buls[i].active) spr.fillRect((int)buls[i].x - 1, (int)buls[i].y - 2, 2, 4, TFT_YELLOW);
  }

  // Jet
  int jx = (int)jetX;
  spr.fillTriangle(jx, JET_Y - 8, jx - 7, JET_Y + 5, jx + 7, JET_Y + 5, TFT_WHITE);
  spr.fillRect(jx - 2, JET_Y + 5, 5, 3, TFT_CYAN);

  drawScoreHeader(score, hiScore);
  spr.drawRect(88, 6, 42, 7, TFT_LIGHTGREY);
  spr.fillRect(89, 7, max(0, (int)(fuel * 0.40f)), 5, fuel > 30 ? TFT_GREEN : TFT_RED);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("FUEL", 131, 18, 1);

  if (state == GAMEOVER) drawGameOverBox("CRASHED!", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_CYAN);
  spr.drawString("RIVER", 67, 18, 4);

  spr.fillTriangle(67, 52, 58, 68, 76, 68, TFT_WHITE);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Hold L/R", "L + R"};
  const char *acts[] = {"steer", "boost"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 68, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Fly over F tanks to refuel", 67, 148, 1);

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
  spr.fillRect(x - 12, y - 12, 4, 24, c(0x1B42));
  spr.fillRect(x + 8, y - 12, 4, 24, c(0x1B42));
  int jx = x + (int)roundf(4 * (triWave(t, 1200) - 0.5f));
  spr.fillTriangle(jx, y - 5, jx - 5, y + 6, jx + 5, y + 6, c(TFT_WHITE));
}

void begin() {
  hiScore = prefs.getUInt("river_hi", 0);
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

}  // namespace river

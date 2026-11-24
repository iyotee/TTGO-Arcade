// Rhythm (2-Lane Beats)
//
// Controls (two buttons):
//   Tap L : hit left lane note
//   Tap R : hit right lane note
// Hit notes as they cross the target line at the bottom. Build combos for
// up to 4x multiplier. 5 misses = game over.

#include "common.h"

namespace rhythm {

static const int MAX_NOTES = 20;
static const int HIT_Y = 208;
static const int LANE_X[2] = {42, 92};
static const int MAX_MISS = 5;
static const uint32_t TICK_MS = 16;

struct Note { float y; uint8_t lane; bool active; };

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static Note notes[MAX_NOTES];
static float speed;
static int combo, misses, spawnTimer, patIdx;
static int flashT[2], judgeT;
static char judgeTxt[12];
static uint16_t judgeCol;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;

// Musical 16-beat rhythmic patterns (bit 0 = L, bit 1 = R)
static const uint8_t PATTERNS[4][16] = {
  {1,0,2,0, 1,0,2,0, 1,1,2,0, 1,2,3,0},
  {1,2,1,0, 2,1,2,0, 1,0,2,1, 2,0,3,0},
  {1,1,2,0, 2,2,1,0, 1,2,1,2, 3,0,3,0},
  {1,0,1,2, 0,2,2,1, 1,2,0,3, 1,0,2,3},
};

static int rnd(int n) { return esp_random() % n; }

static void setJudge(const char *txt, uint16_t col) {
  snprintf(judgeTxt, sizeof judgeTxt, "%s", txt);
  judgeCol = col;
  judgeT = 22;
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("rhythm_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  combo = 0;
  misses = 0;
  speed = 2.4f;
  spawnTimer = 0;
  patIdx = 0;
  flashT[0] = flashT[1] = judgeT = 0;
  newHigh = false;
  for (int i = 0; i < MAX_NOTES; i++) notes[i].active = false;
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void spawnLane(uint8_t lane) {
  for (int i = 0; i < MAX_NOTES; i++) {
    if (!notes[i].active) {
      notes[i] = {(float)HEADER_H - 6, lane, true};
      return;
    }
  }
}

static void hitLane(uint8_t lane) {
  flashT[lane] = 7;
  int bestIdx = -1;
  float bestD = 999.0f;
  for (int i = 0; i < MAX_NOTES; i++) {
    if (!notes[i].active || notes[i].lane != lane) continue;
    float d = fabsf(notes[i].y - HIT_Y);
    if (d < bestD) { bestD = d; bestIdx = i; }
  }
  if (bestIdx >= 0 && bestD <= 20.0f) {
    notes[bestIdx].active = false;
    combo++;
    int mult = min(4, 1 + combo / 10);
    if (bestD <= 8.0f) {
      score += 30 * mult;
      setJudge("PERFECT", TFT_YELLOW);
    } else {
      score += 15 * mult;
      setJudge("GOOD", TFT_GREEN);
    }
  } else {
    combo = 0;
    setJudge("BAD", TFT_ORANGE);
    if (++misses >= MAX_MISS) gameOver();
  }
}

static void tick(int eL, int eR) {
  if (flashT[0] > 0) flashT[0]--;
  if (flashT[1] > 0) flashT[1]--;
  if (judgeT > 0) judgeT--;

  if (eL == 1) hitLane(0);
  if (state == PLAY && eR == 1) hitLane(1);
  if (state != PLAY) return;

  speed = min(4.6f, 2.4f + score * 0.0005f);
  int beatTicks = max(11, 22 - (int)(score / 400));

  if (++spawnTimer >= beatTicks) {
    spawnTimer = 0;
    static int curPat = 0;
    if (patIdx == 0) curPat = rnd(4);
    uint8_t mask = PATTERNS[curPat][patIdx];
    patIdx = (patIdx + 1) & 15;
    if (mask & 1) spawnLane(0);
    if (mask & 2) spawnLane(1);
  }

  for (int i = 0; i < MAX_NOTES; i++) {
    if (!notes[i].active) continue;
    notes[i].y += speed;
    if (notes[i].y > HIT_Y + 22) {
      notes[i].active = false;
      combo = 0;
      setJudge("MISS", TFT_RED);
      if (++misses >= MAX_MISS) { gameOver(); return; }
    }
  }
}

static void drawGame() {
  char buf[24];
  spr.fillSprite(0x0842);

  // Lanes
  for (int l = 0; l < 2; l++) {
    int lx = LANE_X[l];
    spr.drawFastVLine(lx - 18, HEADER_H, 240 - HEADER_H, 0x2104);
    spr.drawFastVLine(lx + 18, HEADER_H, 240 - HEADER_H, 0x2104);
    uint16_t ringCol = flashT[l] ? TFT_WHITE : (l == 0 ? TFT_CYAN : TFT_MAGENTA);
    spr.drawCircle(lx, HIT_Y, 11, ringCol);
    spr.drawCircle(lx, HIT_Y, 10, ringCol);
  }
  spr.drawFastHLine(16, HIT_Y, 103, 0x4208);

  // Notes
  for (int i = 0; i < MAX_NOTES; i++) {
    if (!notes[i].active) continue;
    uint16_t col = notes[i].lane == 0 ? TFT_CYAN : TFT_MAGENTA;
    int nx = LANE_X[notes[i].lane], ny = (int)notes[i].y;
    spr.fillCircle(nx, ny, 8, col);
    spr.fillCircle(nx, ny, 4, TFT_WHITE);
  }

  if (judgeT > 0) {
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(judgeCol);
    spr.drawString(judgeTxt, 67, 152, 2);
  }

  drawScoreHeader(score, hiScore);
  // Lives / misses bar & combo
  int left = MAX_MISS - misses;
  for (int i = 0; i < MAX_MISS; i++) {
    spr.fillRect(92 + i * 8, 7, 6, 5, i < left ? TFT_GREEN : 0x3000);
  }
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(combo >= 10 ? TFT_YELLOW : TFT_LIGHTGREY);
  snprintf(buf, sizeof buf, "%dx (%d)", min(4, 1 + combo / 10), combo);
  spr.drawString(buf, 133, 22, 1);

  if (state == GAMEOVER) drawGameOverBox("GAME OVER", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_MAGENTA);
  spr.drawString("RHYTHM", 67, 18, 4);

  spr.fillCircle(47, 64, 8, TFT_CYAN);
  spr.fillCircle(87, 64, 8, TFT_MAGENTA);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Tap L", "Tap R"};
  const char *acts[] = {"left beat", "right beat"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 60, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Hit notes on the ring", 67, 148, 1);

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
  int y1 = y - 10 + ((t / 45) % 20);
  int y2 = y - 10 + (((t + 450) / 45) % 20);
  spr.drawCircle(x - 6, y + 8, 4, c(TFT_CYAN));
  spr.drawCircle(x + 6, y + 8, 4, c(TFT_MAGENTA));
  spr.fillCircle(x - 6, y1, 3, c(TFT_CYAN));
  spr.fillCircle(x + 6, y2, 3, c(TFT_MAGENTA));
}

void begin() {
  hiScore = prefs.getUInt("rhythm_hi", 0);
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

}  // namespace rhythm

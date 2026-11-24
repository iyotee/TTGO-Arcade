// Simon (Memory Sequence)
//
// Controls (two buttons):
//   Tap L : Cyan (left pad)
//   Tap R : Magenta (right pad)
// Watch the sequence flash, then repeat it from memory. Speeds up each round!

#include "common.h"

namespace simon {

static const int MAX_SEQ = 64;
static const uint32_t TICK_MS = 16;

enum State { TITLE, PLAY, GAMEOVER };
enum Phase { PH_SHOW, PH_INPUT };
static State state = TITLE;
static Phase phase = PH_SHOW;

static uint8_t seq[MAX_SEQ];
static int seqLen, stepIdx, litPad, phaseT;
static bool litOn;
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
    prefs.putUInt("simon_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void nextRound() {
  if (seqLen < MAX_SEQ) seq[seqLen++] = rnd(2);
  phase = PH_SHOW;
  stepIdx = 0;
  litPad = -1;
  litOn = false;
  phaseT = 24;
}

static void newGame() {
  score = 0;
  seqLen = 0;
  newHigh = false;
  nextRound();
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void tick(int eL, int eR) {
  if (phase == PH_SHOW) {
    if (--phaseT <= 0) {
      int onTicks = max(8, 22 - seqLen);
      int offTicks = max(4, 10 - seqLen / 2);
      if (!litOn) {
        if (stepIdx >= seqLen) {
          phase = PH_INPUT;
          stepIdx = 0;
          litPad = -1;
        } else {
          litPad = seq[stepIdx];
          litOn = true;
          phaseT = onTicks;
        }
      } else {
        litPad = -1;
        litOn = false;
        stepIdx++;
        phaseT = offTicks;
      }
    }
    return;
  }

  if (litPad >= 0 && --phaseT <= 0) litPad = -1;

  int press = (eL == 1) ? 0 : (eR == 1) ? 1 : -1;
  if (press >= 0) {
    litPad = press;
    phaseT = 10;
    if (press == seq[stepIdx]) {
      stepIdx++;
      if (stepIdx >= seqLen) {
        score = seqLen;
        nextRound();
      }
    } else {
      gameOver();
    }
  }
}

static void drawGame() {
  spr.fillSprite(TFT_BLACK);

  uint16_t cL = (litPad == 0) ? TFT_WHITE : 0x03EF;
  uint16_t cR = (litPad == 1) ? TFT_WHITE : 0xA014;
  spr.fillRoundRect(6, 56, 58, 150, 8, (litPad == 0) ? TFT_CYAN : 0x0250);
  spr.drawRoundRect(6, 56, 58, 150, 8, cL);
  spr.fillRoundRect(71, 56, 58, 150, 8, (litPad == 1) ? TFT_MAGENTA : 0x4809);
  spr.drawRoundRect(71, 56, 58, 150, 8, cR);

  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(litPad == 0 ? TFT_BLACK : TFT_CYAN);
  spr.drawString("L", 35, 131, 4);
  spr.setTextColor(litPad == 1 ? TFT_BLACK : TFT_MAGENTA);
  spr.drawString("R", 100, 131, 4);

  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(phase == PH_SHOW ? TFT_YELLOW : TFT_GREEN);
  spr.drawString(phase == PH_SHOW ? "WATCH..." : "YOUR TURN!", 67, 214, 2);

  drawScoreHeader(score, hiScore);
  if (state == GAMEOVER) drawGameOverBox("WRONG!", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_CYAN);
  spr.drawString("SIMON", 67, 18, 4);

  spr.fillRoundRect(36, 54, 26, 26, 4, TFT_CYAN);
  spr.fillRoundRect(72, 54, 26, 26, 4, TFT_MAGENTA);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Tap L", "Tap R"};
  const char *acts[] = {"cyan pad", "magenta pad"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 98 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 60, 98 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Repeat the sequence", 67, 150, 1);

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
  int lit = !dim ? ((t / 450) & 1) : -1;
  spr.fillRoundRect(x - 11, y - 10, 10, 20, 3, lit == 0 ? TFT_WHITE : c(TFT_CYAN));
  spr.fillRoundRect(x + 1, y - 10, 10, 20, 3, lit == 1 ? TFT_WHITE : c(TFT_MAGENTA));
}

void begin() {
  hiScore = prefs.getUInt("simon_hi", 0);
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

}  // namespace simon

// 21 (Blackjack)
//
// Controls (two buttons):
//   Tap L : HIT (draw card)
//   Tap R : STAND (or deal next hand)
// Start with 100 chips (bet 20). Best score tracks your peak chip stack!

#include "common.h"

namespace blackjack {

struct Card { uint8_t rank; uint8_t suit; };               // rank 1..13, suit 0..3

enum State { TITLE, PLAY, GAMEOVER };
enum Phase { PH_TURN, PH_RESULT };
static State state = TITLE;
static Phase phase = PH_TURN;

static Card pHand[9], dHand[9];
static int pCnt, dCnt, bet;
static char resultMsg[20];
static uint16_t resultCol;
static uint32_t score = 0, hiScore = 0;                   // score = current chips
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static Card drawCard() {
  return {(uint8_t)(1 + rnd(13)), (uint8_t)rnd(4)};
}

static int handValue(const Card *h, int cnt) {
  int sum = 0, aces = 0;
  for (int i = 0; i < cnt; i++) {
    int r = h[i].rank;
    if (r == 1) { sum += 11; aces++; }
    else if (r >= 10) sum += 10;
    else sum += r;
  }
  while (sum > 21 && aces > 0) { sum -= 10; aces--; }
  return sum;
}

static void recordPeak() {
  if (score > hiScore) {
    hiScore = score;
    newHigh = true;
    prefs.putUInt("bj21_hi", hiScore);
  }
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  lastInput = millis();
  dirty = true;
}

static void dealHand() {
  bet = min((int)score, 20);
  pCnt = dCnt = 0;
  pHand[pCnt++] = drawCard();
  dHand[dCnt++] = drawCard();
  pHand[pCnt++] = drawCard();
  dHand[dCnt++] = drawCard();
  phase = PH_TURN;

  if (handValue(pHand, pCnt) == 21) {
    score += bet + bet / 2;
    recordPeak();
    snprintf(resultMsg, sizeof resultMsg, "BLACKJACK!");
    resultCol = TFT_YELLOW;
    phase = PH_RESULT;
  }
}

static void newGame() {
  score = 100;
  newHigh = false;
  recordPeak();
  dealHand();
  state = PLAY;
  dirty = true;
}

static void resolveDealer() {
  while (handValue(dHand, dCnt) < 17 && dCnt < 9) {
    dHand[dCnt++] = drawCard();
  }
  int pv = handValue(pHand, pCnt);
  int dv = handValue(dHand, dCnt);
  if (dv > 21 || pv > dv) {
    score += bet;
    recordPeak();
    snprintf(resultMsg, sizeof resultMsg, "YOU WIN +%d", bet);
    resultCol = TFT_GREEN;
  } else if (pv < dv) {
    score -= bet;
    snprintf(resultMsg, sizeof resultMsg, "DEALER WINS");
    resultCol = TFT_RED;
  } else {
    snprintf(resultMsg, sizeof resultMsg, "PUSH");
    resultCol = TFT_YELLOW;
  }
  phase = PH_RESULT;
  if (score == 0) gameOver();
}

static void drawMiniCard(int x, int y, const Card &c, bool hidden) {
  if (hidden) {
    spr.fillRoundRect(x, y, 22, 32, 3, 0x194B);
    spr.drawRoundRect(x, y, 22, 32, 3, TFT_LIGHTGREY);
    return;
  }
  spr.fillRoundRect(x, y, 22, 32, 3, TFT_WHITE);
  uint16_t col = (c.suit == 1 || c.suit == 2) ? TFT_RED : TFT_BLACK;
  const char *ranks[] = {"", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};
  const char *suits[] = {"S", "H", "D", "C"};
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(col);
  spr.drawString(ranks[c.rank], x + 3, y + 3, 2);
  spr.setTextDatum(BR_DATUM);
  spr.drawString(suits[c.suit], x + 19, y + 29, 1);
}

static void drawGame() {
  char buf[24];
  spr.fillSprite(0x0B23);                               // casino felt green

  // Dealer
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  if (phase == PH_TURN) snprintf(buf, sizeof buf, "Dealer ?");
  else snprintf(buf, sizeof buf, "Dealer %d", handValue(dHand, dCnt));
  spr.drawString(buf, 8, 44, 2);
  int dStep = min(24, 100 / max(1, dCnt));
  for (int i = 0; i < dCnt; i++) {
    drawMiniCard(8 + i * dStep, 64, dHand[i], (phase == PH_TURN && i == 1));
  }

  // Player
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_WHITE);
  snprintf(buf, sizeof buf, "You %d  (Bet %d)", handValue(pHand, pCnt), bet);
  spr.drawString(buf, 8, 122, 2);
  int pStep = min(24, 100 / max(1, pCnt));
  for (int i = 0; i < pCnt; i++) {
    drawMiniCard(8 + i * pStep, 142, pHand[i], false);
  }

  if (phase == PH_RESULT && state == PLAY) {
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(resultCol);
    spr.drawString(resultMsg, 67, 186, 2);
  }

  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_YELLOW);
  if (phase == PH_TURN) {
    spr.drawString("L", 14, 216, 2);
    spr.drawString("R", 76, 216, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("hit", 26, 216, 2);
    spr.drawString("stand", 88, 216, 2);
  } else {
    spr.drawString("R", 34, 216, 2);
    spr.setTextColor(TFT_GREEN);
    spr.drawString("next hand", 46, 216, 2);
  }

  // Header shows Chips & Best
  spr.fillRect(0, 0, 135, HEADER_H, TFT_BLACK);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_YELLOW);
  snprintf(buf, sizeof buf, "Chips %lu", (unsigned long)score);
  spr.drawString(buf, 2, 2, 2);
  spr.setTextColor(TFT_LIGHTGREY);
  snprintf(buf, sizeof buf, "Best %lu", (unsigned long)hiScore);
  spr.drawString(buf, 2, 24, 1);

  if (state == GAMEOVER) drawGameOverBox("BROKE!", 0, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_GREEN);
  spr.drawString("21", 67, 18, 4);

  drawMiniCard(42, 50, {1, 0}, false);
  drawMiniCard(70, 50, {13, 1}, false);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Tap L", "Tap R"};
  const char *acts[] = {"hit card", "stand / next"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 60, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Beat dealer to 21!", 67, 148, 1);

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
  spr.fillRoundRect(x - 9, y - 10, 12, 16, 2, c(TFT_WHITE));
  spr.fillRoundRect(x - 1, y - 6, 12, 16, 2, c(TFT_LIGHTGREY));
  if (!dim && ((t / 500) & 1)) spr.fillCircle(x + 5, y + 2, 2, TFT_RED);
}

void begin() {
  hiScore = prefs.getUInt("bj21_hi", 0);
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
    if (phase == PH_TURN) {
      if (eL == 2 && pCnt < 9) {
        pHand[pCnt++] = drawCard();
        int pv = handValue(pHand, pCnt);
        if (pv > 21) {
          score -= bet;
          snprintf(resultMsg, sizeof resultMsg, "BUST!");
          resultCol = TFT_RED;
          phase = PH_RESULT;
          if (score == 0) gameOver();
        } else if (pv == 21) {
          resolveDealer();
        }
        dirty = true;
      } else if (eR == 2) {
        resolveDealer();
        dirty = true;
      }
    } else {
      if (eR == 2) {
        dealHand();
        dirty = true;
      }
    }
    if (idleTooLong(now, lastInput)) goToSleep();
  } else {
    if (startOrEnd(state == GAMEOVER, eL, eR, armed) == END_PLAY) newGame();
    if (idleTooLong(now, lastInput)) goToSleep();
  }

  if (dirty) {
    render();
    dirty = false;
  }
}

}  // namespace blackjack

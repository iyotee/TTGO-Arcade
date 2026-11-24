// Reversi (vs CPU)
//
// Controls (two buttons):
//   Tap L  : next legal move (Hold L: previous)
//   Tap R  : place your disc
// You play black and move first. Outflank white discs to flip them; when
// neither side can move, the most discs wins. Score = your discs (+30 for a win).

#include "common.h"

namespace reversi {

static const int N = 8, CELL = 15, BX = 7, BY = 52;
static const uint32_t CPU_DELAY_MS = 550, HOLD_MS = 400;

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static int8_t board[N][N];                         // 0 empty, 1 player (black), 2 CPU (white)
static int8_t moves[N * N];                        // legal moves for the side to play, as y*8+x
static int nMoves, cursor, lastMove;
static int turn;                                   // 1 or 2
static bool passMsg;
static uint32_t cpuAt = 0, lastInput = 0;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false, pressedL = false, pressedR = false;
static int resultWho;                              // 1 you win, 2 CPU wins, 0 draw

static const int8_t WEIGHT[N][N] = {
  {100, -20, 10,  5,  5, 10, -20, 100},
  {-20, -50, -2, -2, -2, -2, -50, -20},
  { 10,  -2,  1,  1,  1,  1,  -2,  10},
  {  5,  -2,  1,  0,  0,  1,  -2,   5},
  {  5,  -2,  1,  0,  0,  1,  -2,   5},
  { 10,  -2,  1,  1,  1,  1,  -2,  10},
  {-20, -50, -2, -2, -2, -2, -50, -20},
  {100, -20, 10,  5,  5, 10, -20, 100},
};

static int rnd(int n) { return esp_random() % n; }

// discs that player "who" would flip by playing at (x, y); flips them when apply is set
static int flips(int8_t b[N][N], int x, int y, int who, bool apply) {
  if (b[y][x]) return 0;
  int other = 3 - who, total = 0;
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++) {
      if (!dx && !dy) continue;
      int cx = x + dx, cy = y + dy, n = 0;
      while (cx >= 0 && cx < N && cy >= 0 && cy < N && b[cy][cx] == other) { cx += dx; cy += dy; n++; }
      if (n && cx >= 0 && cx < N && cy >= 0 && cy < N && b[cy][cx] == who) {
        total += n;
        if (apply)
          for (int k = 1; k <= n; k++) b[y + dy * k][x + dx * k] = who;
      }
    }
  if (apply && total) b[y][x] = who;
  return total;
}

static int listMoves(int8_t b[N][N], int who, int8_t *out) {
  int n = 0;
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++)
      if (flips(b, x, y, who, false)) { if (out) out[n] = y * N + x; n++; }
  return n;
}

static int count(int who) {
  int n = 0;
  for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) n += board[y][x] == who;
  return n;
}

static void gameOver() {
  int me = count(1), cpu = count(2);
  resultWho = me > cpu ? 1 : cpu > me ? 2 : 0;
  score = me + (resultWho == 1 ? 30 : 0);
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("reversi_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

// give the move to player "who"; they pass if they can't move, and the game
// ends when neither side can
static void giveTurn(int who) {
  turn = who;
  nMoves = listMoves(board, turn, moves);
  passMsg = false;
  if (!nMoves) {
    turn = 3 - who;
    nMoves = listMoves(board, turn, moves);
    if (!nMoves) { gameOver(); return; }
    passMsg = true;                                // the other side passed
  }
  if (turn == 2) cpuAt = millis() + CPU_DELAY_MS + (passMsg ? 600 : 0);
  else {
    // keep the cursor near where it was
    int best = 0, bestD = 1000;
    int cx = cursor % N, cy = cursor / N;
    for (int i = 0; i < nMoves; i++) {
      int d = abs(moves[i] % N - cx) + abs(moves[i] / N - cy);
      if (d < bestD) { bestD = d; best = i; }
    }
    cursor = moves[best];
  }
  dirty = true;
}

static void play(int pos) {
  flips(board, pos % N, pos / N, turn, true);
  lastMove = pos;
  giveTurn(3 - turn);
}

static void cpuMove() {
  int best = moves[0], bestV = -100000;
  for (int i = 0; i < nMoves; i++) {
    int8_t b[N][N];
    memcpy(b, board, sizeof b);
    int x = moves[i] % N, y = moves[i] / N;
    int f = flips(b, x, y, 2, true);
    // corners good, squares next to them bad, flips a little, leaving you few moves good
    int v = WEIGHT[y][x] * 3 + f - 4 * listMoves(b, 1, nullptr) + rnd(3);
    if (v > bestV) { bestV = v; best = moves[i]; }
  }
  play(best);
}

static void newGame() {
  memset(board, 0, sizeof board);
  board[3][3] = board[4][4] = 2;
  board[3][4] = board[4][3] = 1;
  score = 0;
  newHigh = false;
  lastMove = -1;
  cursor = 2 * N + 3;
  pressedL = pressedR = false;
  state = PLAY;
  giveTurn(1);
}

// ---------------- rendering ----------------
static void drawDisc(int cx, int cy, int who, int r) {
  if (who == 1) { spr.fillCircle(cx, cy, r, 0x2104); spr.drawCircle(cx, cy, r, TFT_DARKGREY); }
  else spr.fillCircle(cx, cy, r, TFT_WHITE);
}

static void drawBoard() {
  spr.fillRect(BX - 2, BY - 2, N * CELL + 4, N * CELL + 4, 0x0300);
  spr.fillRect(BX, BY, N * CELL, N * CELL, 0x03C0);
  for (int i = 0; i <= N; i++) {
    spr.drawFastHLine(BX, BY + i * CELL, N * CELL, 0x0200);
    spr.drawFastVLine(BX + i * CELL, BY, N * CELL, 0x0200);
  }
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++)
      if (board[y][x]) drawDisc(BX + x * CELL + CELL / 2, BY + y * CELL + CELL / 2, board[y][x], 6);
  if (lastMove >= 0)
    spr.fillCircle(BX + (lastMove % N) * CELL + CELL / 2, BY + (lastMove / N) * CELL + CELL / 2, 1, TFT_RED);
  if (state == PLAY && turn == 1) {
    for (int i = 0; i < nMoves; i++)
      spr.drawPixel(BX + (moves[i] % N) * CELL + CELL / 2, BY + (moves[i] / N) * CELL + CELL / 2, TFT_GREENYELLOW);
    int x = BX + (cursor % N) * CELL, y = BY + (cursor / N) * CELL;
    spr.drawRect(x, y, CELL + 1, CELL + 1, TFT_YELLOW);
    spr.drawRect(x + 1, y + 1, CELL - 1, CELL - 1, TFT_YELLOW);
  }
}

static void render() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  if (state == TITLE) {
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_GREEN);
    spr.drawString("REVERSI", 67, 18, 4);
    drawDisc(55, 64, 1, 8);
    drawDisc(79, 64, 2, 8);
    spr.setTextDatum(TL_DATUM);
    const char *keys[] = {"Tap L", "Hold L", "Tap R"};
    const char *acts[] = {"next move", "previous", "place disc"};
    for (int i = 0; i < 3; i++) {
      spr.setTextColor(TFT_YELLOW);
      spr.drawString(keys[i], 8, 90 + i * 20, 2);
      spr.setTextColor(TFT_WHITE);
      spr.drawString(acts[i], 68, 90 + i * 20, 2);
    }
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_LIGHTGREY);
    spr.drawString("You: black, CPU: white", 67, 156, 1);
    spr.drawString("Win = your discs + 30", 67, 168, 1);
    snprintf(buf, sizeof buf, "Best %lu", (unsigned long)hiScore);
    spr.drawString(buf, 67, 190, 2);
    spr.setTextColor(TFT_GREEN);
    spr.drawString("Press a button", 67, 214, 2);
    spr.pushSprite(0, 0);
    return;
  }

  drawBoard();
  // header: disc counts
  spr.setTextDatum(TL_DATUM);
  drawDisc(10, 14, 1, 6);
  spr.setTextColor(TFT_WHITE);
  snprintf(buf, sizeof buf, "%d", count(1));
  spr.drawString(buf, 20, 4, 4);
  drawDisc(76, 14, 2, 6);
  snprintf(buf, sizeof buf, "%d", count(2));
  spr.drawString(buf, 86, 4, 4);
  spr.setTextColor(TFT_LIGHTGREY);
  snprintf(buf, sizeof buf, "Best %lu", (unsigned long)hiScore);
  spr.drawString(buf, 4, 32, 1);

  spr.setTextDatum(TC_DATUM);
  int sy = BY + N * CELL + 8;
  if (state == PLAY) {
    if (passMsg) {
      spr.setTextColor(TFT_ORANGE);
      spr.drawString(turn == 1 ? "CPU passes" : "You pass", 67, sy, 2);
    } else {
      spr.setTextColor(turn == 1 ? TFT_GREEN : TFT_LIGHTGREY);
      spr.drawString(turn == 1 ? "Your move" : "CPU thinking...", 67, sy, 2);
    }
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(TFT_YELLOW);
    spr.drawString("L", 10, 218, 2);
    spr.drawString("R", 74, 218, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("next", 22, 218, 2);
    spr.drawString("place", 86, 218, 2);
  } else {
    drawGameOverBox(resultWho == 1 ? "YOU WIN!" : resultWho == 2 ? "CPU WINS" : "DRAW", score, hiScore, newHigh);
  }
  spr.pushSprite(0, 0);
}

void drawIcon(int x, int y, bool dim, uint32_t t) {
  auto c = [dim](uint16_t col) { return dim ? tft.alphaBlend(80, col, TFT_BLACK) : col; };
  spr.fillRect(x - 11, y - 11, 22, 22, c(0x03C0));
  spr.drawFastHLine(x - 11, y, 22, c(0x0200));
  spr.drawFastVLine(x, y - 11, 22, c(0x0200));
  bool flip = t && (t / 700) & 1;                  // one disc keeps turning over
  spr.fillCircle(x - 5, y - 5, 4, c(TFT_WHITE));
  spr.fillCircle(x + 5, y + 5, 4, c(TFT_WHITE));
  spr.fillCircle(x + 5, y - 5, 4, c(0x2104));
  spr.fillCircle(x - 5, y + 5, 4, flip ? c(TFT_WHITE) : c(0x2104));
}

void begin() {
  hiScore = prefs.getUInt("reversi_hi", 0);
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
    if (turn == 2) {
      if ((int32_t)(now - cpuAt) >= 0) cpuMove();
    } else if (nMoves) {
      if (eL == 1) { pressedL = true; bL.nextRepeat = now + HOLD_MS; }
      if (eR == 1) pressedR = true;
      int idx = 0;
      for (int i = 0; i < nMoves; i++) if (moves[i] == cursor) idx = i;
      // holding L steps backwards, repeating
      if (pressedL && bL.stable && (int32_t)(now - bL.nextRepeat) >= 0) {
        bL.consumed = true;
        bL.nextRepeat = now + 220;
        cursor = moves[(idx + nMoves - 1) % nMoves];
        dirty = true;
      }
      if (eL == 2 && pressedL) {
        pressedL = false;
        if (!bL.consumed) { cursor = moves[(idx + 1) % nMoves]; dirty = true; }
      }
      if (eR == 2 && pressedR) {
        pressedR = false;
        play(cursor);
      }
    }
    if (state == PLAY && turn == 1 && idleTooLong(now, lastInput)) goToSleep();
  } else {
    if (startOrEnd(state == GAMEOVER, eL, eR, armed) == END_PLAY) newGame();
    if (idleTooLong(now, lastInput)) goToSleep();
  }

  if (dirty) {
    render();
    dirty = false;
  }
}

}  // namespace reversi

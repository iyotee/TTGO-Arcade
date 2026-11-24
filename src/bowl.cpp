// Bowl (10-Pin Bowling)
//
// Controls (two buttons):
//   Tap L or R : stop the sliding ball (start spot), then stop the aim arrow
//   Hold L / R : hook the rolling ball left / right
// Ten frames with real scoring: a strike adds the next two rolls, a spare the
// next one; the 10th frame gives bonus rolls. A perfect game is 300.

#include "common.h"

namespace bowl {

static const int LANE_L = 24, LANE_R = 111, FOUL_Y = 206, END_Y = 40;
static const int NPINS = 10;
static const float RB = 5.0f, RP = 3.5f;          // ball / pin radius
static const float BALL_SPD = 3.4f;
static const uint32_t TICK_MS = 16;

struct Pin { float hx, hy, x, y, vx, vy; bool standing, out; };

enum State { TITLE, PLAY, GAMEOVER };
enum Phase { POS, AIM, ROLL, RESULT };
static State state = TITLE;
static Phase phase = POS;

static Pin pins[NPINS];
static float ballX, ballY, ballVx, ballVy, aimA, sweepV;
static bool gutter;
static int rolls[21], nRolls, frameNo, rollNo, phaseT;
static int rackBall;                               // balls thrown at the current rack
static const char *resultMsg = "";
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;

static void resetPins(bool all) {
  int k = 0;
  for (int r = 0; r < 4; r++)
    for (int i = 0; i <= r; i++, k++) {
      Pin &p = pins[k];
      p.hx = 67 + (i - r / 2.0f) * 12;
      p.hy = 92 - r * 11;
      if (all) { p.standing = true; rackBall = 0; }
      p.x = p.hx;
      p.y = p.hy;
      p.vx = p.vy = 0;
      p.out = !p.standing;
    }
}

static int totalScore() {
  int total = 0, i = 0;
  for (int f = 0; f < 10 && i < nRolls; f++) {
    int a = rolls[i], b = i + 1 < nRolls ? rolls[i + 1] : 0, c = i + 2 < nRolls ? rolls[i + 2] : 0;
    if (a == 10) { total += 10 + b + c; i += 1; }
    else if (i + 1 < nRolls && a + b == 10) { total += 10 + c; i += 2; }
    else { total += a + b; i += 2; }
  }
  return total;
}

static int standingCount() {
  int n = 0;
  for (int i = 0; i < NPINS; i++) n += pins[i].standing;
  return n;
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("bowl_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newBall() {
  phase = POS;
  ballX = 67;
  ballY = FOUL_Y;
  sweepV = 1.1f;
  aimA = 0;
  gutter = false;
}

static void newGame() {
  score = 0;
  nRolls = frameNo = rollNo = 0;
  newHigh = false;
  resetPins(true);
  newBall();
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

// count the pins this roll knocked down and move on in the scoring
static void finishRoll() {
  int before = standingCount();
  bool firstBall = rackBall++ == 0;
  for (int i = 0; i < NPINS; i++) {
    Pin &p = pins[i];
    if (!p.standing) continue;
    if (p.out || hypotf(p.x - p.hx, p.y - p.hy) > 4.0f) p.standing = false;
  }
  int knocked = before - standingCount();
  rolls[nRolls++] = knocked;
  bool allDown = standingCount() == 0;

  if (gutter && knocked == 0) resultMsg = "GUTTER";
  else if (allDown && firstBall) resultMsg = "STRIKE!";
  else if (allDown) resultMsg = "SPARE!";
  else resultMsg = "";

  bool nextFrame = false, done = false;
  if (frameNo < 9) {
    if (rollNo == 0 && !allDown) rollNo = 1;
    else nextFrame = true;
  } else {
    // 10th frame: a strike or spare earns up to three rolls
    rollNo++;
    int first = rolls[nRolls - rollNo];
    if (rollNo == 1) { if (allDown) resetPins(true); }
    else if (rollNo == 2) {
      int second = rolls[nRolls - 1];
      if (first == 10 || first + second == 10) { if (allDown) resetPins(true); }
      else done = true;
    } else done = true;
  }
  if (nextFrame) { frameNo++; rollNo = 0; resetPins(true); }
  else if (!done) resetPins(false);                // standing pins back on their spots
  score = totalScore();
  phase = RESULT;
  phaseT = done ? -1000 : 0;                       // negative: game ends after the message
}

static void physicsStep() {
  // half step for ball, pins and their contacts
  if (!gutter) {
    if (ballY > 110) {
      if (bL.stable && !bR.stable) ballVx -= 0.006f;
      if (bR.stable && !bL.stable) ballVx += 0.006f;
    }
    ballX += ballVx / 2;
    ballY += ballVy / 2;
    if (ballX < LANE_L || ballX > LANE_R) {        // into the gutter, past every pin
      gutter = true;
      ballX = ballX < LANE_L ? LANE_L - 4 : LANE_R + 4;
      ballVx = 0;
    }
  } else ballY += ballVy / 2;

  for (int i = 0; i < NPINS; i++) {
    Pin &p = pins[i];
    if (p.out || gutter) continue;
    float dx = p.x - ballX, dy = p.y - ballY, d = sqrtf(dx * dx + dy * dy);
    if (d < RB + RP && d > 0.01f) {
      float nx = dx / d, ny = dy / d;
      float rel = (ballVx - p.vx) * nx + (ballVy - p.vy) * ny;
      if (rel > 0) {
        p.vx += nx * rel * 1.5f;
        p.vy += ny * rel * 1.5f;
        ballVx -= nx * rel * 0.15f;               // the ball is much heavier
        ballVy -= ny * rel * 0.15f;
      }
      p.x = ballX + nx * (RB + RP);
      p.y = ballY + ny * (RB + RP);
    }
  }
  for (int a = 0; a < NPINS; a++) {
    for (int b = a + 1; b < NPINS; b++) {
      Pin &p = pins[a], &q = pins[b];
      if (p.out || q.out) continue;
      float dx = q.x - p.x, dy = q.y - p.y, d = sqrtf(dx * dx + dy * dy);
      if (d < 2 * RP && d > 0.01f) {
        float nx = dx / d, ny = dy / d;
        float rel = (p.vx - q.vx) * nx + (p.vy - q.vy) * ny;
        if (rel > 0) {
          float j = rel * 0.9f;
          p.vx -= nx * j; p.vy -= ny * j;
          q.vx += nx * j; q.vy += ny * j;
        }
        float o = (2 * RP - d) / 2;
        p.x -= nx * o; p.y -= ny * o;
        q.x += nx * o; q.y += ny * o;
      }
    }
  }
  for (int i = 0; i < NPINS; i++) {
    Pin &p = pins[i];
    if (p.out) continue;
    p.x += p.vx / 2;
    p.y += p.vy / 2;
    p.vx *= 0.97f;
    p.vy *= 0.97f;
    if (p.x < LANE_L || p.x > LANE_R || p.y < END_Y) p.out = true;
  }
}

static void tick(bool tap) {
  phaseT++;
  switch (phase) {
    case POS:
      ballX += sweepV;
      if (ballX < LANE_L + 8 || ballX > LANE_R - 8) sweepV = -sweepV;
      if (tap) { phase = AIM; aimA = 0; sweepV = 0.009f; }
      break;
    case AIM:
      aimA += sweepV;
      if (fabsf(aimA) > 0.2f) sweepV = -sweepV;
      if (tap) {
        phase = ROLL;
        phaseT = 0;
        ballVx = sinf(aimA) * BALL_SPD;
        ballVy = -cosf(aimA) * BALL_SPD;
      }
      break;
    case ROLL: {
      physicsStep();
      physicsStep();
      bool settled = true;
      for (int i = 0; i < NPINS; i++)
        if (!pins[i].out && fabsf(pins[i].vx) + fabsf(pins[i].vy) > 0.07f) settled = false;
      if ((ballY < END_Y && settled) || phaseT > 360) finishRoll();
      break;
    }
    default:   // RESULT
      if (phaseT < 0 && phaseT > -1000 + 70) { gameOver(); return; }
      if (phaseT >= 0 && (phaseT > 70 || (tap && phaseT > 15))) newBall();
      break;
  }
}

// ---------------- rendering ----------------
static void drawPin(const Pin &p) {
  int x = (int)roundf(p.x), y = (int)roundf(p.y);
  bool down = p.standing && hypotf(p.x - p.hx, p.y - p.hy) > 4.0f;
  if (down || (!p.standing)) {
    spr.fillRoundRect(x - 4, y - 2, 9, 4, 2, TFT_LIGHTGREY);   // lying on its side
    return;
  }
  spr.fillCircle(x, y, 3, TFT_WHITE);
  spr.drawFastHLine(x - 2, y, 5, TFT_RED);
}

static void drawLane() {
  spr.fillSprite(0x2104);
  spr.fillRect(LANE_L - 8, END_Y, 8, 240 - END_Y, 0x4208);          // gutters
  spr.fillRect(LANE_R + 1, END_Y, 8, 240 - END_Y, 0x4208);
  spr.fillRect(LANE_L, END_Y, LANE_R - LANE_L + 1, 240 - END_Y, 0xCC27);
  for (int x = LANE_L + 6; x < LANE_R; x += 11) spr.drawFastVLine(x, END_Y, 240 - END_Y, 0xBB85);
  for (int k = -2; k <= 2; k++) {                                   // target arrows
    int x = 67 + k * 14, y = 160 + abs(k) * 5;
    spr.fillTriangle(x - 3, y + 6, x + 3, y + 6, x, y, 0x8200);
  }
  spr.drawFastHLine(LANE_L, FOUL_Y + 8, LANE_R - LANE_L + 1, TFT_RED);
}

static void frameMarks(char *buf, size_t n) {
  // marks of the current frame, like "X", "7 /", "8 -"
  int start = 0;
  for (int f = 0; f < frameNo && start < nRolls; f++) start += rolls[start] == 10 ? 1 : 2;
  size_t len = 0;
  bool fresh = true;                               // this ball meets a full rack
  int prev = 0;
  buf[0] = 0;
  for (int i = start; i < nRolls && len + 3 < n; i++) {
    int r = rolls[i];
    char c = r == 0 ? '-' : (char)('0' + r);
    if (fresh) { if (r == 10) c = 'X'; fresh = r == 10; }
    else { if (prev + r == 10) c = '/'; fresh = true; }
    prev = r;
    buf[len++] = c;
    buf[len++] = ' ';
    buf[len] = 0;
  }
}

static void drawGame() {
  char buf[24];
  drawLane();
  for (int i = 0; i < NPINS; i++) if (pins[i].standing && !pins[i].out) drawPin(pins[i]);

  if (phase == AIM) {
    float ex = ballX + sinf(aimA) * 40, ey = ballY - cosf(aimA) * 40;
    spr.drawLine((int)ballX, (int)ballY, (int)ex, (int)ey, TFT_YELLOW);
    spr.fillCircle((int)ex, (int)ey, 2, TFT_YELLOW);
  }
  if (phase != RESULT) {
    spr.fillCircle((int)ballX, (int)ballY, 5, 0x18DF);
    spr.drawPixel((int)ballX - 2, (int)ballY - 2, TFT_WHITE);
  }

  spr.setTextDatum(TC_DATUM);
  if (phase == RESULT && resultMsg[0]) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(resultMsg, 67, 124, 4);
  } else if (phase == POS || phase == AIM) {
    spr.setTextColor(TFT_WHITE);
    spr.drawString(phase == POS ? "Tap: start spot" : "Tap: aim", 67, 222, 2);
  }

  drawScoreHeader(score, hiScore);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(TFT_YELLOW);
  snprintf(buf, sizeof buf, "Frame %d", min(frameNo + 1, 10));
  spr.drawString(buf, 132, 4, 2);
  frameMarks(buf, sizeof buf);
  spr.setTextColor(TFT_WHITE);
  spr.drawString(buf, 132, 24, 1);

  if (state == GAMEOVER) drawGameOverBox("FINAL", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_ORANGE);
  spr.drawString("BOWL", 67, 18, 4);

  for (int r = 0; r < 4; r++)
    for (int i = 0; i <= r; i++) spr.fillCircle(67 + (int)((i - r / 2.0f) * 9), 70 - r * 7, 2, TFT_WHITE);
  spr.fillCircle(67, 84, 5, 0x18DF);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Tap L/R", "Tap L/R", "Hold L/R"};
  const char *acts[] = {"start spot", "aim", "hook ball"};
  for (int i = 0; i < 3; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 72, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Perfect game = 300", 67, 162, 1);

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
  for (int r = 0; r < 3; r++)
    for (int i = 0; i <= r; i++) spr.fillCircle(x + (int)((i - r / 2.0f) * 7), y - 6 - r * 5, 2, c(TFT_WHITE));
  int by = y + 12 - (int)(18 * triWave(t, 1600));
  spr.fillCircle(x, by, 4, c(0x18DF));
}

void begin() {
  hiScore = prefs.getUInt("bowl_hi", 0);
  state = TITLE;
  armed = false;
  lastInput = millis();
  dirty = true;
}

static EvQ qL, qR;                 // button events waiting for the next tick

void update(uint32_t now) {
  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) lastInput = now;

  if (state == PLAY) {
    qL.push(eL);
    qR.push(eR);
    if (now - lastTick > 200) lastTick = now - TICK_MS;
    while (state == PLAY && now - lastTick >= TICK_MS) {
      lastTick += TICK_MS;
      int a = qL.pop(), b = qR.pop();
      tick(a == 1 || b == 1);
      dirty = true;
    }
    // between rolls nothing moves on its own: let the board sleep if left alone
    if (state == PLAY && (phase == POS || phase == AIM) && idleTooLong(now, lastInput)) goToSleep();
  } else {
    qL.clear();
    qR.clear();
    if (startOrEnd(state == GAMEOVER, eL, eR, armed) == END_PLAY) newGame();
    if (idleTooLong(now, lastInput)) goToSleep();
  }

  if (dirty) {
    render();
    dirty = false;
  }
}

}  // namespace bowl

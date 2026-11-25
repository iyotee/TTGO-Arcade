// Tigo: look after the mascot (a tiny tamagotchi)
//
// Controls (two buttons):
//   Tap R  : play (he snaps at a soap bubble)
//   Tap L  : feed (a fish cracker)
//   Hold R : bath time
//   Hold L : back to menu
//
// Hunger / fun / clean are the same needs the screensaver uses (tigo.h), saved
// in prefs. Shells unlock from your high scores: best score > 0 in 5, 15, 30
// and 45 games; he always wears the fanciest one. Neon theme: the little beach
// at night with neon-glow bars, and neon Tigo.

#include "tigo.h"

namespace tigoapp {

// ---------------- layout ----------------
static const int SKY_Y = 40, SHORE_Y = 136, GROUND = 186;
static const int TIGO_SIZE = 64;
static const int HOME_X = 58;
static const uint32_t FRAME_MS = 33;

static const uint16_t PLUM = tigo::COL_PLUM, WHITE = 0xFFFF;
static const uint16_t FISH = 0xFD88, POO = 0x8A45, SOAP = 0xBF3F;

// scene colours per theme: Original (sunny beach) and Neon (the beach at night)
struct AppPal {
  uint16_t sky0, sky1, sea0, sea1, sand0, sand1, foam, cloud, drop, label, barBg, barOk, barMid, barLow, mark;
};
static const AppPal PAL_ORIG = {0x4D1D, 0xCF5F, 0x2D37, 0x6E99, 0xF6B4, 0xE611, 0xEF7D, 0xCE9C, 0x7E9F,
                                TFT_LIGHTGREY, 0x2945, 0x4ECC, 0xFE60, 0xF9C6, WHITE};
static const AppPal PAL_NEON = {0x0802, 0x406A, 0x0845, 0x1889, 0x30A8, 0x1844, 0x073F, 0x5913, 0x073F,
                                0x8C5F, 0x1825, 0x97E7, 0xFF45, 0xF8C7, 0x073F};
static const AppPal *pal = &PAL_ORIG;

enum Mode { M_IDLE, M_FEED, M_PLAY, M_WASH, M_REFUSE };

static Mode mode = M_IDLE;
static uint32_t modeAt = 0, t0 = 0, lastFrame = 0, lastInput = 0;
static bool pressedL = false, pressedR = false;
static float tx = HOME_X, walkTo = HOME_X;
static bool faceL = false;
static float foodY = 0, soapX = 0, soapY = 0;
static bool eating = false, popped = false;
static uint32_t popAt = 0;
static char say[64];
static uint32_t sayAt = 0, sayLen = 0;
static int shellsN = 1, gamesN = 0;

// ---------------- helpers ----------------
static float reach() { return tigo::clawReach(TIGO_SIZE); }
static float tipX() { return tx + (faceL ? -reach() : reach()); }

static void speak(uint32_t now, const char *s, uint32_t len = 0) {
  strncpy(say, s, sizeof say - 1);
  say[sizeof say - 1] = 0;
  sayAt = now;
  sayLen = len ? len : 1500 + strlen(say) * 50;
}

static void setMode(Mode m, uint32_t now) {
  mode = m;
  modeAt = now;
}

static uint16_t barCol(int v) { return v > 60 ? pal->barOk : v > 30 ? pal->barMid : pal->barLow; }

// ---------------- rendering ----------------
static void drawBar(int y, const char *label, int v) {
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(pal->label);
  spr.drawString(label, 4, y, 1);
  spr.fillSmoothRoundRect(38, y - 1, 93, 9, 4, pal->barBg);
  if (theme().neon) drawThemedBox(38, y - 1, 93, 9, 4, tft.alphaBlend(150, barCol(v), pal->barBg), true);
  int w = 89 * constrain(v, 0, 100) / 100;
  if (w >= 6) spr.fillSmoothRoundRect(40, y + 1, w, 5, 2, barCol(v));
  else if (w > 0) spr.fillRect(40, y + 1, w, 5, barCol(v));
}

static void drawShellsRow() {
  // the collection: unlocked shells in colour, locked ones as dark outlines with their goal
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(WHITE);
  for (int i = 0; i < tigo::NUM_SHELLS; i++) {
    float x = 16 + i * 25.5f, y = 58;
    if (i < shellsN) {
      tigo::drawShell(x, y, 7.0f, i);
      if (i == tigo::shell()) spr.drawWideLine(x - 6, y + 11, x + 6, y + 11, 2.0f, pal->mark);
    } else {
      if (theme().neon) {   // a dim neon ring: plum would vanish in the night sky
        spr.drawSpot(x, y, 8, theme().frame);
        spr.drawSpot(x, y, 6.8f, theme().panel);
      } else {
        spr.drawSpot(x, y, 8, tft.alphaBlend(90, PLUM, pal->sky0));
      }
      char buf[4];
      snprintf(buf, sizeof buf, "%d", tigo::SHELL_AT[i]);
      spr.setTextDatum(MC_DATUM);
      spr.drawString(buf, (int)x, (int)y + 1, 1);
    }
  }
}

static void drawPoos(uint32_t t) {
  int n = tigo::needs().clean < 30 ? 2 : tigo::needs().clean < 60 ? 1 : 0;
  const float xs[2] = {14, 122};
  for (int i = 0; i < n; i++) {
    float x = xs[i], y = GROUND + 2;
    spr.drawSpot(x, y - 3, 5, PLUM);   spr.drawSpot(x, y - 3, 4.2f, POO);
    spr.drawSpot(x, y - 7, 3.4f, PLUM); spr.drawSpot(x, y - 7, 2.6f, POO);
    spr.drawSpot(x + 0.5f, y - 10.5f, 1.9f, PLUM); spr.drawSpot(x + 0.5f, y - 10.5f, 1.2f, POO);
    float ph = fmodf(t / 900.0f + i * 0.5f, 1.0f);
    uint16_t c = tft.alphaBlend((uint8_t)(200 * (1 - ph)), 0x8E8B, pal->sky1);
    spr.drawWideLine(x - 3, y - 14 - ph * 8, x - 1.5f, y - 17 - ph * 8, 1.0f, c);
    spr.drawWideLine(x - 1.5f, y - 17 - ph * 8, x - 3, y - 20 - ph * 8, 1.0f, c);
  }
}

static void drawFish(float x, float y) {
  spr.drawWedgeLine(x - 3, y, x + 2, y, 3.0f, 2.4f, PLUM);
  spr.fillTriangle(x - 8, y - 4, x - 8, y + 4, x - 3, y, PLUM);
  spr.drawWedgeLine(x - 3, y, x + 2, y, 2.2f, 1.6f, FISH);
  spr.fillTriangle(x - 7, y - 3, x - 7, y + 3, x - 3, y, FISH);
  spr.drawSpot(x + 1.5f, y - 0.5f, 0.6f, PLUM);
}

static void drawSoap(float x, float y, uint32_t t) {
  float r = 6 + sinf(t / 120.0f) * 0.6f;
  spr.drawSpot(x, y, r + 1, tft.alphaBlend(160, WHITE, SOAP));
  spr.drawSpot(x, y, r, tft.alphaBlend(70, SOAP, pal->sky1));
  spr.drawSpot(x - r * 0.4f, y - r * 0.4f, r * 0.28f, WHITE);
  spr.drawSpot(x + r * 0.35f, y + r * 0.3f, r * 0.12f, WHITE);
}

static void render(uint32_t now) {
  uint32_t t = now - t0;
  tigo::Needs &n = tigo::needs();
  const bool neon = theme().neon;
  pal = neon ? &PAL_NEON : &PAL_ORIG;
  spr.fillSprite(theme().bg);

  drawBar(4, "FOOD", n.hunger);
  drawBar(16, "FUN", n.fun);
  drawBar(28, "BATH", n.clean);

  // little beach
  spr.fillRectVGradient(0, SKY_Y, 135, SHORE_Y - SKY_Y, pal->sky0, pal->sky1);
  if (neon) {   // night: twinkling stars, a moon and a pink glow on the horizon
    for (int i = 0; i < 14; i++) {
      uint32_t h = (i + 3) * 2654435761u;
      int x = (h >> 8) % 135, y = SKY_Y + 2 + (h >> 16) % (SHORE_Y - SKY_Y - 30);
      spr.drawPixel(x, y, tft.alphaBlend(60 + (uint8_t)(150 * triWave(t + (h >> 4) % 2000, 2200)), WHITE, pal->sky0));
    }
    spr.drawSpot(118, 100, 9, tft.alphaBlend(50, 0xF959, pal->sky1));
    spr.drawSpot(118, 100, 6, 0xFF95);
    spr.drawSpot(121, 97, 5, tft.alphaBlend(170, pal->sky1, pal->sky0));
  }
  spr.fillRectVGradient(0, SHORE_Y, 135, 14, pal->sea0, pal->sea1);
  if (neon) spr.drawFastHLine(0, SHORE_Y, 135, tft.alphaBlend(130, 0xF959, pal->sea0));
  spr.fillRectVGradient(0, SHORE_Y + 14, 135, 196 - SHORE_Y - 14, pal->sand0, pal->sand1);
  for (int x = 0; x < 135; x++) {
    int y = SHORE_Y + 14 + (int)(sinf(x * 0.09f + t * 0.0012f) * 1.5f + 1.5f);
    spr.drawFastVLine(x, SHORE_Y + 14, y - SHORE_Y - 14 + 1, pal->sea1);
    spr.drawPixel(x, y, pal->foam);
    if (neon) spr.drawPixel(x, y + 1, tft.alphaBlend(80, pal->foam, pal->sand0));
  }
  drawShellsRow();
  drawPoos(t);

  // Tigo: mood decides the resting pose
  TigoPose pose = TIGO_IDLE;
  int low = min(n.hunger, min(n.fun, n.clean));
  if (low < 25) pose = TIGO_SAD;
  if (fabsf(walkTo - tx) > 0.5f) pose = TIGO_WALK;
  uint32_t me = now - modeAt;
  switch (mode) {
    case M_FEED: pose = eating ? TIGO_EAT : TIGO_IDLE; break;
    case M_PLAY: pose = popped ? TIGO_HAPPY : (me > 700 ? TIGO_PINCH : TIGO_IDLE); break;
    case M_WASH: pose = TIGO_HAPPY; break;
    case M_REFUSE: pose = TIGO_SAD; break;
    default: break;
  }
  tigo::drawShadow((int)tx, GROUND + 1, TIGO_SIZE);
  tigo::drawTigoEx((int)tx, GROUND, TIGO_SIZE, pose, t, faceL, tigo::shell(), 255);

  if (mode == M_FEED && !eating) drawFish(tipX(), foodY);
  if (mode == M_PLAY) {
    if (!popped) drawSoap(soapX, soapY, t);
    else {   // pop: a ring of droplets
      float f = min(1.0f, (now - popAt) / 350.0f);
      for (int i = 0; i < 8; i++) {
        float a = i * 0.785f;
        spr.drawSpot(soapX + cosf(a) * (4 + f * 9), soapY + sinf(a) * (4 + f * 9), 1.4f * (1 - f) + 0.3f, WHITE);
      }
    }
  }
  if (mode == M_WASH) {   // a rain cloud gives him a shower
    float cx = tx + (faceL ? -8 : 8), cy = 76;
    for (int i = 0; i < 10; i++) {
      float ph = fmodf(t / 600.0f + i * 0.37f, 1.0f);
      float x = cx - 16 + (i * 37 % 32), y = cy + 6 + ph * (GROUND - 30 - cy);
      spr.drawWedgeLine(x, y - 3, x, y, 0.4f, 1.2f, pal->drop);
    }
    uint16_t cc = pal->cloud;
    if (neon) {   // a glowing outline round the cloud
      uint16_t g = 0xF959;
      spr.drawSpot(cx - 9, cy, 7, g); spr.drawSpot(cx, cy - 4, 9, g); spr.drawSpot(cx + 10, cy, 7, g);
      spr.fillRect((int)cx - 9, (int)cy, 19, 7, g);
    }
    spr.drawSpot(cx - 9, cy, 6, cc); spr.drawSpot(cx, cy - 4, 8, cc); spr.drawSpot(cx + 10, cy, 6, cc);
    spr.fillRect((int)cx - 9, (int)cy, 19, 6, cc);
    for (int i = 0; i < 5; i++) {   // foam on the shell
      float ph = fmodf(t / 800.0f + i * 0.2f, 1.0f);
      float x = tx + (faceL ? 6 : -6) + cosf(i * 1.3f) * 14, y = GROUND - 30 - ph * 18 + sinf(i * 2.1f) * 6;
      spr.drawSpot(x, y, 2.2f * (1 - ph) + 0.6f, WHITE);
    }
  }

  if (sayAt && (int32_t)(now - sayAt) < (int32_t)sayLen) {
    int headX = (int)(tx + (faceL ? -1 : 1) * 11), headTop = GROUND - 60;
    tigo::drawBubble(say, headX, headTop, 70, now - sayAt);
  }

  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(theme().textDim);
  spr.drawString("hold R: bath", 67, 197, 1);
  drawToolFooter("feed", "play");
  spr.pushSprite(0, 0);
}

// ---------------- actions ----------------
static void feed(uint32_t now) {
  if (tigo::needs().hunger >= 95) {
    speak(now, "I'm full! Maybe later.");
    setMode(M_REFUSE, now);
    return;
  }
  foodY = SKY_Y - 6;
  eating = false;
  setMode(M_FEED, now);
}

static void play(uint32_t now) {
  soapX = faceL ? -8 : 143;
  soapY = 100;
  popped = false;
  setMode(M_PLAY, now);
}

static void bath(uint32_t now) {
  speak(now, "Splish splash!", 1800);
  setMode(M_WASH, now);
}

static void step(uint32_t now, float dt) {
  uint32_t me = now - modeAt;
  tigo::Needs &n = tigo::needs();
  switch (mode) {
    case M_IDLE:
      // shuffle around a bit now and then
      if (fabsf(walkTo - tx) > 0.5f) {
        faceL = walkTo < tx;
        float s = 20 * dt;
        tx += walkTo > tx ? min(s, walkTo - tx) : -min(s, tx - walkTo);
      } else if (esp_random() % 300 == 0) {
        walkTo = HOME_X - 14 + (float)(esp_random() % 28);
      }
      break;
    case M_FEED:
      if (!eating) {
        foodY += 160 * dt;
        if (foodY >= GROUND - 18) { eating = true; modeAt = now; }
      } else if (me > 1500) {
        tigo::feed(20);
        speak(now, n.hunger >= 90 ? "So full. Happy crab." : "Yum! Crunchy!");
        setMode(M_IDLE, now);
      }
      break;
    case M_PLAY:
      if (!popped) {
        float f = min(1.0f, me / 1300.0f);
        float sx = faceL ? -8 : 143;
        soapX = sx + (tipX() - sx) * f;
        soapY = 100 + (GROUND - 30 - 100) * f + sinf(me / 160.0f) * 4 * (1 - f);
        if (f >= 1) { popped = true; popAt = now; tigo::play(15); speak(now, (esp_random() & 1) ? "Pop! Got it!" : "Snip! Again?"); }
      } else if ((int32_t)(now - popAt) > 900) setMode(M_IDLE, now);
      break;
    case M_WASH:
      if (me > 1800) {
        tigo::wash();
        speak(now, "Squeaky clean!");
        setMode(M_IDLE, now);
      }
      break;
    case M_REFUSE:
      if (me > 1500) setMode(M_IDLE, now);
      break;
  }
}

// ---------------- entry points ----------------
void drawIcon(int x, int y, bool dim, uint32_t t) {
  // ~24 x 17 px: shifted so his whole body sits in the icon box
  tigo::drawTigoEx(x - 1, y + 8, 20, TIGO_IDLE, t ? t + 1000 : 1000, false, tigo::shell(), dim ? 90 : 255);
}

void menuInfo(char *buf, size_t n) {
  static char cached[24];
  static uint32_t at = 0;
  uint32_t now = millis();
  if (!at || (int32_t)(now - at) > 5000) {   // the shell count reads every high score
    at = now;
    tigo::load();
    tigo::Needs &nd = tigo::needs();
    if (nd.hunger < 30) snprintf(cached, sizeof cached, "Hungry!");
    else if (nd.clean < 30) snprintf(cached, sizeof cached, "Needs a bath");
    else if (nd.fun < 30) snprintf(cached, sizeof cached, "Bored...");
    else snprintf(cached, sizeof cached, "Shells %d/%d", tigo::shellsUnlocked(), tigo::NUM_SHELLS);
  }
  snprintf(buf, n, "%s", cached);
}

void begin() {
  uint32_t now = millis();
  tigo::load();
  pressedL = pressedR = false;
  lastInput = t0 = modeAt = now;
  lastFrame = 0;
  tx = walkTo = HOME_X;
  faceL = false;
  mode = M_IDLE;
  gamesN = tigo::gamesWithScore();
  shellsN = tigo::shellsUnlocked();
  sayAt = 0;
  char buf[48];
  if (tigo::takeNewShell()) {
    snprintf(buf, sizeof buf, "New shell: %s! Shiny!", tigo::shellName(tigo::shell()));
    speak(now, buf, 3500);
  } else {
    tigo::Needs &n = tigo::needs();
    if (n.hunger < 30) speak(now, "Food? Please? Tap L!");
    else if (n.fun < 30) speak(now, "Play with me! Tap R!");
    else if (n.clean < 30) speak(now, "I need a bath... hold R.");
    else if (shellsN < tigo::NUM_SHELLS) {
      snprintf(buf, sizeof buf, "Best scores in %d more games = new shell!", tigo::SHELL_AT[shellsN] - gamesN);
      speak(now, buf, 3500);
    } else speak(now, "Hi! I'm Tigo!");
  }
  dbg("tigo app: %d games scored, %d shells\n", gamesN, shellsN);
}

void update(uint32_t now) {
  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) lastInput = now;
  if (eL == 1) pressedL = true;
  if (eR == 1) pressedR = true;

  if (bL.stable && pressedL && (int32_t)(now - bL.t0) >= (int32_t)TOOL_HOLD_MS) {
    pressedL = false;
    tigo::save(now, true);
    returnToMenu();
    return;
  }
  if (bR.stable && pressedR && !bR.consumed && (int32_t)(now - bR.t0) >= (int32_t)TOOL_HOLD_MS) {
    bR.consumed = true;
    if (mode == M_IDLE) bath(now);
  }
  if (eL == 2 && pressedL) {
    pressedL = false;
    if (mode == M_IDLE) feed(now);
  }
  if (eR == 2 && pressedR) {
    pressedR = false;
    if (!bR.consumed && mode == M_IDLE) play(now);
  }

  if (mode == M_IDLE && idleTooLong(now, lastInput)) {
    tigo::save(now, true);
    goToSleep();
    lastInput = millis();       // back from the screensaver
    pressedL = pressedR = false;
    return;
  }

  if ((int32_t)(now - lastFrame) < (int32_t)FRAME_MS) return;
  float dt = lastFrame ? min(0.1f, (now - lastFrame) / 1000.0f) : 0;
  lastFrame = now;
  tigo::tick(now);
  step(now, dt);
  render(now);
  tigo::save(now, false);
}

}  // namespace tigoapp

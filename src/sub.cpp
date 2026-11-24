// Sub (Deep Sea Diver)
//
// Controls (two buttons):
//   Hold L : dive
//   Hold R : rise
//   L + R  : fire a torpedo
// Grab treasure chests on the sea floor (+50), torpedo sharks (+20) and mines
// (+10). Oxygen runs out under water: surface to refill. 3 hulls.

#include "common.h"

namespace subgame {

static const int MAX_ENT = 10, MAX_TORP = 3, SEG = 9;
static const int SURFACE_Y = HEADER_H + 14;        // water line
static const int SUB_X = 26, SUB_W = 20, SUB_H = 9;
static const uint32_t TICK_MS = 16;

// kinds: 0 = mine on a chain, 1 = shark, 2 = treasure chest
struct Ent { float x, y, vx; float phase; uint8_t kind; bool active; };
struct Torp { float x, y; bool active; };

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static Ent ents[MAX_ENT];
static Torp torps[MAX_TORP];
static int16_t floorY[136 / SEG + 3];              // sea floor, one height per segment
static float subY, subVy, speed, oxygen, floorOff, spawnD, dist;
static int hulls, fireCool, hurtT, frame;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false, drowned = false;
static uint32_t lastTick = 0, lastInput = 0;

static const int NSEG = sizeof floorY / sizeof floorY[0];

static int rnd(int n) { return esp_random() % n; }

static int floorAt(float x) {                      // sea floor height at screen x
  float fx = (x + floorOff) / SEG;
  int i = constrain((int)fx, 0, NSEG - 2);
  float f = fx - i;
  return (int)(floorY[i] + (floorY[i + 1] - floorY[i]) * f);
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("sub_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  subY = 110;
  subVy = 0;
  speed = 1.2f;
  oxygen = 100;
  hulls = 3;
  fireCool = hurtT = frame = 0;
  floorOff = 0;
  spawnD = 60;
  dist = 0;
  drowned = newHigh = false;
  int h = 214;
  for (int i = 0; i < NSEG; i++) { h = constrain(h + rnd(13) - 6, 196, 230); floorY[i] = h; }
  for (int i = 0; i < MAX_ENT; i++) ents[i].active = false;
  for (int i = 0; i < MAX_TORP; i++) torps[i].active = false;
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void spawn() {
  for (int i = 0; i < MAX_ENT; i++) {
    if (ents[i].active) continue;
    Ent &e = ents[i];
    int r = rnd(100);
    e.x = 145;
    e.phase = rnd(628) / 100.0f;
    e.active = true;
    if (r < 40) {                                  // mine, floating at a random depth
      e.kind = 0;
      e.y = SURFACE_Y + 30 + rnd(120);
      e.vx = 0;
    } else if (r < 75) {                           // shark, swims at you
      e.kind = 1;
      e.y = SURFACE_Y + 20 + rnd(130);
      e.vx = -(0.6f + rnd(8) * 0.1f + min(1.0f, dist / 6000.0f));
    } else {                                       // chest, on the floor
      e.kind = 2;
      e.y = 0;                                     // follows the floor
      e.vx = 0;
    }
    return;
  }
}

static void hurt() {
  if (hurtT > 0) return;                           // brief invulnerability after a hit
  hurtT = 70;
  if (--hulls <= 0) gameOver();
}

static void tick() {
  frame++;
  if (fireCool > 0) fireCool--;
  if (hurtT > 0) hurtT--;

  bool l = bL.stable, r = bR.stable;
  if (l && r) {
    if (fireCool == 0) {
      for (int i = 0; i < MAX_TORP; i++) {
        if (torps[i].active) continue;
        torps[i] = {(float)(SUB_X + SUB_W), subY + SUB_H / 2.0f, true};
        fireCool = 28;
        break;
      }
    }
    subVy *= 0.9f;
  } else if (l) subVy += 0.12f;
  else if (r) subVy -= 0.12f;
  else subVy *= 0.94f;                             // neutral buoyancy: drift to a stop
  subVy = constrain(subVy, -2.2f, 2.2f);
  subY += subVy;

  if (subY < SURFACE_Y - SUB_H / 2) { subY = SURFACE_Y - SUB_H / 2; if (subVy < 0) subVy = 0; }
  int fl = min(floorAt(SUB_X), floorAt(SUB_X + SUB_W));
  if (subY + SUB_H > fl) { subY = fl - SUB_H; subVy = 0; hurt(); if (state != PLAY) return; }

  // oxygen: drains under water, refills fast at the surface
  if (subY <= SURFACE_Y) oxygen = min(100.0f, oxygen + 1.2f);
  else oxygen -= 0.055f + min(0.04f, dist / 100000.0f);
  if (oxygen <= 0) { oxygen = 0; drowned = true; gameOver(); return; }

  // scroll the world
  speed = min(2.6f, 1.2f + dist / 5000.0f);
  dist += speed;
  if ((int)dist / 40 != (int)(dist - speed) / 40) score++;
  floorOff += speed;
  while (floorOff >= SEG) {
    floorOff -= SEG;
    for (int i = 0; i < NSEG - 1; i++) floorY[i] = floorY[i + 1];
    floorY[NSEG - 1] = constrain(floorY[NSEG - 2] + rnd(13) - 6, 190, 230);
  }
  spawnD -= speed;
  if (spawnD <= 0) { spawn(); spawnD = max(34.0f, 80.0f - dist / 400.0f) + rnd(40); }

  for (int i = 0; i < MAX_TORP; i++) {
    Torp &t = torps[i];
    if (!t.active) continue;
    t.x += 4.5f;
    if (t.x > 140) t.active = false;
  }

  for (int i = 0; i < MAX_ENT; i++) {
    Ent &e = ents[i];
    if (!e.active) continue;
    e.x += e.vx - speed;
    if (e.x < -20) { e.active = false; continue; }
    float ey;
    int hw, hh;                                    // half size of the hit box
    if (e.kind == 0) { ey = e.y + sinf(frame * 0.05f + e.phase) * 4; hw = hh = 5; }
    else if (e.kind == 1) { ey = e.y + sinf(frame * 0.08f + e.phase) * 6; hw = 9; hh = 4; }
    else { ey = floorAt(e.x) - 5; hw = 6; hh = 5; }

    // torpedoes
    if (e.kind != 2) {
      for (int k = 0; k < MAX_TORP; k++) {
        Torp &t = torps[k];
        if (t.active && fabsf(t.x - e.x) < hw + 3 && fabsf(t.y - ey) < hh + 2) {
          t.active = false;
          e.active = false;
          score += e.kind == 1 ? 20 : 10;
          break;
        }
      }
      if (!e.active) continue;
    }

    // the submarine
    float sx = SUB_X + SUB_W / 2.0f, sy = subY + SUB_H / 2.0f;
    if (fabsf(sx - e.x) < SUB_W / 2 + hw - 2 && fabsf(sy - ey) < SUB_H / 2 + hh - 1) {
      e.active = false;
      if (e.kind == 2) score += 50;
      else { hurt(); if (state != PLAY) return; }
    }
  }
}

// ---------------- rendering ----------------
static void drawSub(int x, int y, bool blink) {
  if (blink) return;
  spr.fillRoundRect(x, y, SUB_W, SUB_H, 4, TFT_YELLOW);
  spr.fillRect(x + 7, y - 4, 6, 4, TFT_YELLOW);          // conning tower
  spr.drawFastVLine(x + 11, y - 7, 3, TFT_LIGHTGREY);    // periscope
  spr.fillCircle(x + 14, y + 4, 2, TFT_CYAN);            // porthole
  spr.fillRect(x - 3, y + 2, 3, 5, TFT_ORANGE);          // propeller
}

static void drawGame() {
  spr.fillSprite(0x0010);
  // lighter water near the surface
  spr.fillRect(0, SURFACE_Y, 135, 40, 0x01B5);
  spr.fillRect(0, SURFACE_Y + 40, 135, 50, 0x0112);
  spr.fillRect(0, HEADER_H, 135, SURFACE_Y - HEADER_H, 0x5D7F);   // sky
  for (int x = -((int)floorOff % 12); x < 135; x += 12)            // waves
    spr.fillTriangle(x, SURFACE_Y, x + 12, SURFACE_Y, x + 6, SURFACE_Y - 3, 0x01B5);

  // sea floor
  for (int x = 0; x < 135; x += 3) {
    int fy = floorAt(x);
    spr.fillRect(x, fy, 3, 240 - fy, 0x6A43);
  }

  for (int i = 0; i < MAX_ENT; i++) {
    const Ent &e = ents[i];
    if (!e.active) continue;
    int x = (int)e.x;
    if (e.kind == 0) {
      int y = (int)(e.y + sinf(frame * 0.05f + e.phase) * 4);
      spr.drawLine(x, y + 5, x, floorAt(x), TFT_DARKGREY);     // chain
      spr.fillCircle(x, y, 5, TFT_DARKGREY);
      spr.drawFastHLine(x - 7, y, 15, TFT_LIGHTGREY);
      spr.drawFastVLine(x, y - 7, 15, TFT_LIGHTGREY);
      if ((frame / 20) & 1) spr.drawPixel(x, y, TFT_RED);
    } else if (e.kind == 1) {
      int y = (int)(e.y + sinf(frame * 0.08f + e.phase) * 6);
      spr.fillEllipse(x, y, 9, 4, TFT_LIGHTGREY);
      spr.fillTriangle(x + 7, y, x + 13, y - 5, x + 13, y + 5, TFT_LIGHTGREY);   // tail
      spr.fillTriangle(x - 1, y - 3, x + 3, y - 3, x + 2, y - 8, TFT_LIGHTGREY);  // fin
      spr.drawPixel(x - 6, y - 1, TFT_BLACK);
    } else {
      int y = floorAt(x) - 10;
      spr.fillRect(x - 6, y, 12, 10, 0x8A22);
      spr.fillRect(x - 6, y, 12, 3, TFT_GOLD);
      spr.fillRect(x - 1, y + 3, 2, 3, TFT_GOLD);
    }
  }

  for (int i = 0; i < MAX_TORP; i++)
    if (torps[i].active) spr.fillRect((int)torps[i].x, (int)torps[i].y - 1, 7, 2, TFT_WHITE);

  drawSub(SUB_X, (int)subY, hurtT > 0 && (hurtT / 5) & 1);

  drawScoreHeader(score, hiScore);
  // oxygen bar and hulls on the right of the header
  uint16_t oc = oxygen > 35 ? TFT_CYAN : ((frame / 8) & 1 ? TFT_RED : TFT_ORANGE);
  for (int i = 0; i < hulls; i++) spr.fillRoundRect(98 + i * 12, 5, 9, 5, 2, TFT_YELLOW);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("O2", 84, 18, 1);
  spr.drawRect(98, 18, 34, 8, TFT_LIGHTGREY);
  spr.fillRect(100, 20, (int)(30 * oxygen / 100), 4, oc);

  if (state == PLAY && oxygen < 35 && subY > SURFACE_Y) {
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor((frame / 10) & 1 ? TFT_RED : TFT_WHITE);
    spr.drawString("SURFACE!", 67, SURFACE_Y + 6, 2);
  }

  if (state == GAMEOVER) drawGameOverBox(drowned ? "NO OXYGEN!" : "SUNK!", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_YELLOW);
  spr.drawString("SUB", 67, 18, 4);

  drawSub(57, 62, false);
  spr.fillRect(82, 66, 7, 2, TFT_WHITE);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Hold L", "Hold R", "L + R"};
  const char *acts[] = {"dive", "rise", "torpedo"};
  for (int i = 0; i < 3; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 90 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 68, 90 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Grab chests, watch O2", 67, 156, 1);
  spr.drawString("Surface to breathe!", 67, 168, 1);

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
  int dy = (int)roundf(4 * (triWave(t, 1800) - 0.5f));
  spr.fillRoundRect(x - 10, y - 2 + dy, 20, 9, 4, c(TFT_YELLOW));
  spr.fillRect(x - 3, y - 6 + dy, 6, 4, c(TFT_YELLOW));
  spr.fillCircle(x + 4, y + 2 + dy, 2, c(TFT_CYAN));
  // bubbles rise from the propeller
  if (!dim && t) {
    int b = (t / 60) % 20;
    spr.drawCircle(x - 11, y - b + 4, 1, TFT_CYAN);
    spr.drawCircle(x - 8, y - ((b + 10) % 20) + 4, 1, TFT_CYAN);
  }
}

void begin() {
  hiScore = prefs.getUInt("sub_hi", 0);
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

}  // namespace subgame

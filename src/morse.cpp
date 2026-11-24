// Morse (Morse Code Keyer)
//
// Controls (two buttons):
//   Tap L  : dot
//   Tap R  : dash
//   Hold R : delete the last character
//   Hold L : back to menu
// A pause of ~0.7 s ends a letter, a longer pause adds a space. While you key,
// the letters that still match are listed at the bottom as a cheat sheet.

#include "common.h"

namespace morse {

static const uint32_t LETTER_MS = 700, WORD_MS = 1800, DEL_MS = 600;
static const int MAX_TEXT = 80, MAX_CODE = 6;

static const char *CODES[36] = {
  ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---", "-.-", ".-..", "--",
  "-.", "---", ".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--..",
  "-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----.",
};
static char symFor(int i) { return i < 26 ? 'A' + i : '0' + (i - 26); }

static char text[MAX_TEXT + 1];
static int textLen = 0;
static char code[MAX_CODE + 1];
static int codeLen = 0;
static uint32_t lastKey = 0, lastInput = 0;
static bool spaced = true, dirty = true, pressedL = false, pressedR = false;
static char lastSym = 0;
static uint32_t lastSymAt = 0;

static void addChar(char c) {
  if (textLen == MAX_TEXT) { memmove(text, text + 1, MAX_TEXT - 1); textLen--; }
  text[textLen++] = c;
  text[textLen] = 0;
}

static void commitLetter(uint32_t now) {
  if (!codeLen) return;
  char c = '?';
  for (int i = 0; i < 36; i++) if (!strcmp(CODES[i], code)) c = symFor(i);
  addChar(c);
  lastSym = c;
  lastSymAt = now;
  codeLen = 0;
  code[0] = 0;
  spaced = false;
}

static void key(char k, uint32_t now) {
  if (codeLen < MAX_CODE) { code[codeLen++] = k; code[codeLen] = 0; }
  lastKey = now;
}

// ---------------- rendering ----------------
static void drawCode(const char *c, int x, int y, int unit, uint16_t col) {
  // dots and dashes as pills, left to right from (x, y)
  for (; *c; c++) {
    if (*c == '.') { spr.fillCircle(x + unit / 2, y, unit / 2, col); x += unit + unit / 2; }
    else { spr.fillRoundRect(x, y - unit / 2, unit * 3, unit, unit / 2, col); x += unit * 3 + unit / 2; }
  }
}

static int codeWidth(const char *c, int unit) {
  int w = 0;
  for (; *c; c++) w += (*c == '.' ? unit : unit * 3) + unit / 2;
  return w - unit / 2;
}

static void render(uint32_t now) {
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_GREEN);
  spr.drawString("MORSE", 67, 6, 4);

  // decoded text: the last 4 lines of 10 characters (fixed-width font, double size)
  static const int LINE_CH = 10;
  spr.fillRoundRect(4, 34, 127, 78, 4, 0x0841);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_WHITE);
  spr.setTextSize(2);
  int lines = textLen / LINE_CH + 1;
  int first = max(0, lines - 4);
  for (int l = first; l < lines; l++) {
    char line[LINE_CH + 1];
    int n = max(0, min(LINE_CH, textLen - l * LINE_CH));
    memcpy(line, text + l * LINE_CH, n);
    line[n] = 0;
    spr.drawString(line, 8, 38 + (l - first) * 18, 1);
  }
  spr.setTextSize(1);
  if ((now / 500) & 1) {                           // blinking cursor
    int l = textLen / LINE_CH - first, col = textLen % LINE_CH;
    spr.fillRect(8 + col * 12, 38 + l * 18 + 15, 10, 2, TFT_GREEN);
  }

  // the letter being keyed
  if (codeLen) {
    int w = codeWidth(code, 7);
    drawCode(code, 67 - w / 2, 132, 7, TFT_YELLOW);
  } else if (lastSym && (int32_t)(now - lastSymAt) < 900) {
    char s[2] = {lastSym, 0};
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(s, 67, 132, 4);
  }

  // cheat sheet: letters still matching what was keyed (or the basics)
  int shown = 0;
  for (int i = 0; i < 36 && shown < 6; i++) {
    if (codeLen && strncmp(CODES[i], code, codeLen)) continue;
    if (!codeLen && !strchr("ETAINS", symFor(i))) continue;
    int x = 6 + (shown % 2) * 66, y = 152 + (shown / 2) * 16;
    char s[2] = {symFor(i), 0};
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(TFT_CYAN);
    spr.drawString(s, x, y, 2);
    drawCode(CODES[i], x + 14, y + 8, 3, TFT_LIGHTGREY);
    shown++;
  }

  drawToolFooter("dot", "dash");
  spr.pushSprite(0, 0);
}

void drawIcon(int x, int y, bool dim, uint32_t t) {
  auto c = [dim](uint16_t col) { return dim ? tft.alphaBlend(80, col, TFT_BLACK) : col; };
  // "... ---" lights up one element at a time
  static const char *ICON_CODE = "..--";
  int on = t ? (t / 300) % 6 : -1;
  int px = x - 12;
  for (int i = 0; ICON_CODE[i]; i++) {
    uint16_t col = c(i == on ? TFT_YELLOW : TFT_GREEN);
    if (ICON_CODE[i] == '.') { spr.fillCircle(px + 2, y, 2, col); px += 6; }
    else { spr.fillRoundRect(px, y - 2, 8, 5, 2, col); px += 10; }
  }
  spr.drawRoundRect(x - 13, y - 9, 26, 18, 4, c(TFT_DARKGREY));
}

void menuInfo(char *buf, size_t n) {
  snprintf(buf, n, "Tap out . and -");
}

void begin() {
  textLen = codeLen = 0;
  text[0] = code[0] = 0;
  spaced = true;
  lastSym = 0;
  pressedL = pressedR = false;
  lastInput = lastKey = millis();
  dirty = true;
}

void update(uint32_t now) {
  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) lastInput = now;

  if (eL == 1) pressedL = true;
  if (eR == 1) pressedR = true;
  if (pressedL && bL.stable && (int32_t)(now - bL.t0) >= (int32_t)TOOL_HOLD_MS) {
    pressedL = false;
    returnToMenu();
    return;
  }
  if (pressedR && bR.stable && !bR.consumed && (int32_t)(now - bR.t0) >= (int32_t)DEL_MS) {
    bR.consumed = true;
    if (codeLen) { codeLen = 0; code[0] = 0; }      // drop the half-keyed letter first
    else if (textLen) text[--textLen] = 0;
    spaced = true;
    dirty = true;
  }
  if (eL == 2 && pressedL) { pressedL = false; key('.', now); dirty = true; }
  if (eR == 2 && pressedR) { pressedR = false; if (!bR.consumed) { key('-', now); dirty = true; } }

  // keying timing: a short pause ends a letter, a long one a word
  bool keying = bL.stable || bR.stable;
  if (!keying && codeLen && (int32_t)(now - lastKey) >= (int32_t)LETTER_MS) { commitLetter(now); dirty = true; }
  if (!keying && !codeLen && !spaced && textLen && (int32_t)(now - lastKey) >= (int32_t)WORD_MS) {
    addChar(' ');
    spaced = true;
    dirty = true;
  }

  static uint32_t lastBlink = 0;
  if ((int32_t)(now - lastBlink) >= 250) { lastBlink = now; dirty = true; }
  if (idleTooLong(now, lastInput)) goToSleep();
  if (dirty) {
    render(now);
    dirty = false;
  }
}

}  // namespace morse

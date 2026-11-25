// Screensaver: Tigo's beach, a little living tamagotchi scene (Neon theme:
// the same beach under a neon-lit skyline with glowing signs).
//
// The sky follows the real sun of the time zone's city (clockSun): night, dawn,
// day and dusk, the palettes blending smoothly through the twilights, the sun
// crossing the sky on an arc by day and the moon by night, stars fading in and
// out. The Neon theme's day is synthwave: a pink / orange sky and a striped sun.
// While the time is unknown it is always day.
//
// Controls (two buttons):
//   Any button : wake up (the caller then shows the PIN lock if it is on, then the menu)
//
// Tigo wanders on his own, picked by a small behaviour machine that follows his
// needs (hunger / fun / clean / energy, shared with the Tigo app through tigo.h):
// he talks, eats snacks that drop from the sky, poops and buries it, naps,
// collects washed-up shells, chases a beach ball and plays with the status
// badges: he pinches, drags and sits on the battery, dances when a cable is
// plugged in, catches WiFi waves (or hunts for a signal with an eye-periscope)
// and reads messages / eats treats sent by the phone over Bluetooth.

#include "tigo.h"
#include "clock.h"

namespace saver {

// ---------------- layout ----------------
static const int SCR_W = 135;
static const int HORIZON = 118, SHORE = 150, GROUND = 214;
static const int TIGO_SIZE = 62;
static const int X_MIN = 32, X_MAX = 102;           // Tigo's cx range
static const int BATT_W = 24, BATT_H = 13, BATT_HOME = 9;
static const int SIGN_X = 121, SIGN_Y = 152;         // WiFi sign board top
static const uint32_t FRAME_MS = 35;                 // ~28 fps
static const uint32_t BATT_READ_MS = 10000;
static const float WALK_SPEED = 24.0f;               // px / s

// ---------------- colours ----------------
static const uint16_t PLUM = tigo::COL_PLUM, WHITE = 0xFFFF;
static const uint16_t WOOD = 0x9B48, CREAM = 0xFFBC, POO = 0x8A45, POO_L = 0xB347;
static const uint16_t BALL_R = 0xFA6D, BALL_B = 0x4D1F, BALL_Y = 0xFE87, TEAL = tigo::COL_TEAL;
static const uint16_t GREY = 0xB5D9, BAT_OK = 0x4ECC, BAT_MID = 0xFE60, BAT_LOW = 0xF9C6;
static const uint16_t CABLE = 0x5ACD, STAR_C = 0xFD88, BT_BLUE = 0x3BFF, TREAT = 0xEB52, FISH = 0xFD88;

// sky: top, middle (half way to the horizon), horizon
struct Pal { uint16_t sky0, skyM, sky1, sea0, sea1, sand0, sand1, wet; };
static constexpr uint16_t avg565(uint16_t a, uint16_t b) { return ((a >> 1) & 0x7BEF) + ((b >> 1) & 0x7BEF); }
static const Pal DAY   = {0x4D1D, avg565(0x4D1D, 0xCF5F), 0xCF5F, 0x2D37, 0x6E99, 0xF6B4, 0xE611, 0xD58F};
static const Pal DAWN  = {0x6356, 0xCCB7, 0xFE12, 0x6BD6, 0xED74, 0xF655, 0xDD92, 0xC4F1};
static const Pal DUSK  = {0x5A53, 0xD370, 0xFD8F, 0x4B74, 0xDCD1, 0xEDF2, 0xD52F, 0xC4CE};
static const Pal NIGHT = {0x0044, avg565(0x0044, 0x192A), 0x192A, 0x0949, 0x1A4D, 0x8BED, 0x6B09, 0x62C8};
static const Pal NEON  = {0x0802, avg565(0x0802, 0x406A), 0x406A, 0x0845, 0x1889, 0x30A8, 0x1844, 0x20CA};
static const Pal NEON_DAY  = {0x48B0, 0xE1F2, 0xFCEA, 0xCA52, 0x40AE, 0x694E, 0x38A9, 0x510C};   // synthwave
static const Pal NEON_DUSK = {0x1847, 0x78CE, 0xFB09, 0x914D, 0x2067, 0x40CA, 0x2065, 0x38A8};   // dawn and dusk
// neon tubes and the city (by night; *_D by day)
static const uint16_t N_PINK = 0xF959, N_CYAN = 0x073F, N_YELLOW = 0xFF45, N_LIME = 0x97E7, N_RED = 0xF8C7;
static const uint16_t BLD = 0x1023, BLD2 = 0x1845, POST = 0x3068, ARC_OFF = 0x398B, N_PANEL = 0x1825;
static const uint16_t BLD_D = 0x38AA, BLD2_D = 0x50ED;
static const uint16_t RSUN_TOP = 0xFF4C, RSUN_MID = 0xFC87, RSUN_BOT = 0xF1B1;   // the striped retro sun
static const uint16_t SUN_HI = 0xFF0C, SUN_LOW = 0xFD88, MOON = 0xFF95;
static const uint16_t WIN[3] = {0x93C5, 0x1C12, 0x914F};

// ---------------- state ----------------
enum Act { A_IDLE, A_WANDER, A_TALK, A_EAT, A_POOP, A_NAP, A_SHELL, A_BALL, A_BATT, A_DRAG, A_SIT,
           A_WIFI, A_PERI, A_DANCE, A_HIDE, A_PHONE };

struct Prop { bool on; float x, y, vx, vy; uint8_t kind; };

static float tx = 64, tyOff = 0;          // Tigo: cx and lift (sitting on the battery)
static bool faceL = false;
static TigoPose pose = TIGO_IDLE;
static Act act = A_IDLE;
static int phase = 0;
static uint32_t actAt = 0, phaseAt = 0, actLen = 0;
static uint32_t t0 = 0, lastFrame = 0, lastStep = 0, lastPoll = 0, lastPoo = 0;
static int counter = 0;                   // kicks, catches...

static Prop food[4], ball, shellP, poo, sand[8], wave[3];
static float ballRot = 0, battX = BATT_HOME, battJig = 0, mound = 0;
static bool holdingShell = false;
static uint8_t heldStyle = 0;

static int mv = 4000, pct = 100;
static bool usb = false, usbWas = false;
static uint32_t battAt = 0;
static const Pal *pal = &DAY;
static Pal curPal;                        // this frame's blend
static bool night = false, haveTime = false;
static bool neon = false;                 // Neon theme: neon city, neon signs
static struct tm tmNow;

// the sun: the phase and its twilight windows (minutes around sunrise / sunset)
enum SunPhase { SUN_NIGHT, SUN_DAWN, SUN_DAY, SUN_DUSK };
static const int DAWN_PRE = 45, DAWN_POST = 30, DUSK_PRE = 45, DUSK_POST = 40;
static SunPhase sunPh = SUN_DAY;
static float dark = 0;                    // 0 day .. 1 night
static float warm = 0;                    // 1 = the sun on the horizon: the warm twilight palette
static float sunP = 0.5f, moonP = -1;     // along their arcs: 0 rising .. 1 setting
static float sunX, sunY, sunR, moonX, moonY, moonA, starA, glow;
static uint16_t sunC = SUN_HI;
static bool sunUp = false, moonUp = false;

static int pendingTreats = 0;
static bool treatRun = false;
static char phoneMsg[64];
static bool havePhone = false;

// speech bubble
static char btxt[96];
static uint32_t bubbleAt = 0, bubbleLen = 0;
// a phone message can outgrow one bubble (4 lines): it is read out in parts,
// "..." marking each cut, phonePos = where the next part starts
static char phoneTxt[96];
static int phonePos = 0;

// ---------------- helpers ----------------
static float frand(float a, float b) { return a + (b - a) * (esp_random() % 10000) / 10000.0f; }
static int irand(int n) { return n > 0 ? (int)(esp_random() % (uint32_t)n) : 0; }
static uint32_t hash32(uint32_t n) { n *= 2654435761u; n ^= n >> 15; n *= 2246822519u; n ^= n >> 13; return n; }
static float reach() { return tigo::clawReach(TIGO_SIZE); }
static float clampX(float x) { return x < X_MIN ? X_MIN : x > X_MAX ? X_MAX : x; }

static void say(uint32_t now, uint32_t len, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(btxt, sizeof btxt, fmt, ap);
  va_end(ap);
  bubbleAt = now;
  bubbleLen = len ? len : 1800 + strlen(btxt) * 55;
  dbg("tigo says: %s\n", btxt);
}

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static uint16_t mix565(uint16_t a, uint16_t b, float f) {   // a (f = 0) .. b (f = 1)
  if (f <= 0) return a;
  if (f >= 1) return b;
  return tft.alphaBlend((uint8_t)(f * 255 + 0.5f), b, a);
}
static uint16_t byNight(uint16_t dayC, uint16_t nightC) { return mix565(dayC, nightC, dark); }

static Pal mixPal(const Pal &a, const Pal &b, float f) {
  return {mix565(a.sky0, b.sky0, f), mix565(a.skyM, b.skyM, f), mix565(a.sky1, b.sky1, f),
          mix565(a.sea0, b.sea0, f), mix565(a.sea1, b.sea1, f), mix565(a.sand0, b.sand0, f),
          mix565(a.sand1, b.sand1, f), mix565(a.wet, b.wet, f)};
}

// the sky's colour at row y (the gradient drawn by fillSky)
static uint16_t skyAt(float y) {
  const int mid = HORIZON / 2;
  if (y < mid) return mix565(pal->sky0, pal->skyM, y / mid);
  return mix565(pal->skyM, pal->sky1, (y - mid) / (HORIZON - mid));
}

static void fillSky() {
  const int mid = HORIZON / 2;
  spr.fillRectVGradient(0, 0, SCR_W, mid, pal->sky0, pal->skyM);
  spr.fillRectVGradient(0, mid, SCR_W, HORIZON - mid, pal->skyM, pal->sky1);
}

// a point on the sky's arc, for a disc of radius r: p = 0 just risen on the left,
// 1 setting on the right, the top of the arc at peakY (it climbs quickly off the
// horizon, then slowly); outside 0..1 it sinks below
static void arcPos(float p, float r, float peakY, float *x, float *y) {
  float q = clamp01(p), base = HORIZON + r, x0 = neon ? 20 : 8;
  *x = x0 + q * (SCR_W - 2 * x0);
  *y = base - sqrtf(sinf(q * (float)M_PI)) * (base - peakY) + (p < 0 ? -p : p > 1 ? p - 1 : 0) * 400;
}

// The phase of the day from today's sunrise and sunset, how dark and how warm the
// sky is, and where the sun and the moon are on their way.
static void readSun() {
  sunPh = SUN_DAY; dark = 0; warm = 0; sunP = 0.5f; moonP = -1;
  int rise, set;
  if (!haveTime || !clockSun(&rise, &set)) return;        // unknown time: a plain day, never night
  if (set - rise >= 1440) return;                          // the sun never sets
  if (set <= rise) { sunPh = SUN_NIGHT; dark = 1; sunP = -1; moonP = 0.5f; return; }   // nor rises
  float m = tmNow.tm_hour * 60 + tmNow.tm_min + tmNow.tm_sec / 60.0f;
  sunP = (m - rise) / (set - rise);
  moonP = (m >= set ? m - set : m + 1440 - set) / (1440 - (set - rise));   // night: sunset .. sunrise
  // u: 0 = the night end of the twilight .. 1 = its day end; f0 = the sun on the horizon
  float u, f0;
  if (m >= rise - DAWN_PRE && m < rise + DAWN_POST) {
    sunPh = SUN_DAWN;
    u = (m - (rise - DAWN_PRE)) / (DAWN_PRE + DAWN_POST);
    f0 = (float)DAWN_PRE / (DAWN_PRE + DAWN_POST);
  } else if (m >= set - DUSK_PRE && m < set + DUSK_POST) {
    sunPh = SUN_DUSK;
    u = (set + DUSK_POST - m) / (DUSK_PRE + DUSK_POST);
    f0 = (float)DUSK_POST / (DUSK_PRE + DUSK_POST);
  } else {
    if (m < rise || m >= set) { sunPh = SUN_NIGHT; dark = 1; }
    return;
  }
  if (u < f0) { warm = u / f0; dark = 1 - 0.5f * warm; }
  else { warm = (1 - u) / (1 - f0); dark = 0.5f * warm; }
}

static void readEnv(uint32_t now) {
  if (!battAt || (int32_t)(now - battAt) >= (int32_t)BATT_READ_MS) {
    battAt = now;
    mv = batteryMilliVolts();
    pct = batteryPercent(mv);
    usb = onUsbPower(mv);
  }
  haveTime = timeIsSet() && localTimeNow(&tmNow);
  neon = theme().neon;
  readSun();
  night = sunPh == SUN_NIGHT;
  // the palette: day .. night by the darkness, then the twilight's warm colours on top
  const Pal &dayP = neon ? NEON_DAY : DAY, &nightP = neon ? NEON : NIGHT;
  const Pal &warmP = neon ? NEON_DUSK : sunPh == SUN_DAWN ? DAWN : DUSK;
  curPal = mixPal(mixPal(dayP, nightP, dark), warmP, warm);
  pal = &curPal;
  // the sky's lights
  starA = clamp01((dark - 0.5f) * 2);                      // gone by sunrise, out after sunset
  moonA = clamp01((dark - 0.3f) / 0.5f);
  glow = 0.4f + 0.6f * dark;                               // neon signs glow softer by day
  float low = 1 - clamp01(sinf(clamp01(sunP) * (float)M_PI) * 2.5f);   // 1 near the horizon
  sunC = mix565(SUN_HI, SUN_LOW, low);
  sunR = neon ? 16 : 8.5f + 1.5f * low;
  arcPos(sunP, sunR, neon ? 76 : 84, &sunX, &sunY);
  sunUp = sunY - sunR < HORIZON;
  float mr = neon ? 6.5f : 9;
  arcPos(moonP, mr, neon ? 70 : 80, &moonX, &moonY);
  moonUp = moonA > 0 && moonY - mr < HORIZON;
}

// ---------------- talking ----------------
static const char *const LINES[] = {
  "Pinch L, pinch R!", "Is that a high score?", "Battery tastes like 3.7 V",
  "Snip snap!", "Nice shell, huh?", "Psst... press a button.", "Sand in my shell. Again.",
  "Ti-Ti-Go, that's me!", "Waves are just sea hugs.", "I could pinch a pixel.",
  "Beep boop. Crab mode.", "Plan: nap, snack, nap.", "One more game? Pleeease?",
  "Do crabs dream of RAM?", "Side-stepping is my cardio.", "I'm not shellfish, I share!",
  "My claws are your buttons!", "ESP32 and me: besties.", "Hermit by name, social by nature.",
  "Achievement: stood still.", "Crab rave in 3... 2...", "Shell yeah!", "Let me pinch that for you.",
  "Who needs hands? I have claws.", "135 pixels of pure beach.", "I walk sideways. With style.",
  "Is it snack o'clock yet?", "This shell? Vintage.", "Small crab, big dreams.",
};
static const int NUM_LINES = sizeof LINES / sizeof LINES[0];
// extra swagger for neon Tigo
static const char *const NEON_LINES[] = {
  "Stay fresh, stay crabby", "Neon mode: ON", "Shades on. Claws up.", "Too cool for the shell.",
  "Cap backwards = speed +10", "This chain? Solid pixels.", "Glow hard or go home.",
  "Night shift, crab style.", "Sprayed my shell myself.", "Can't see the screen. Worth it.",
};
static const int NUM_NEON_LINES = sizeof NEON_LINES / sizeof NEON_LINES[0];

static const char *randomLine() {
  if (neon && irand(2)) return NEON_LINES[irand(NUM_NEON_LINES)];
  return LINES[irand(NUM_LINES)];
}

static void sayContext(uint32_t now) {
  tigo::Needs &n = tigo::needs();
  for (int tries = 0; tries < 8; tries++) {
    switch (irand(9)) {
      case 0:   // battery
        if (usb) { say(now, 0, irand(2) ? "Charging! I feel zappy!" : "Mmm, fresh electrons."); return; }
        if (pct < 12) { say(now, 0, "%d%%... I feel faint...", pct); return; }
        if (pct < 30) { say(now, 0, "Only %d%%! Find a cable!", pct); return; }
        if (pct >= 80) { say(now, 0, "Battery %d%%. Full of beans!", pct); return; }
        say(now, 0, "Battery at %d%%. Snack-sized.", pct); return;
      case 1:   // WiFi
        if (wifiConnected()) {
          if (wifiRssi() > -60) say(now, 0, "Strong WiFi! %d dBm. Surf's up!", wifiRssi());
          else say(now, 0, "WiFi %d dBm. Wavy.", wifiRssi());
          return;
        }
        if (wifiEnabled()) { say(now, 0, "No WiFi... anyone out there?"); return; }
        say(now, 0, "WiFi off. Digital detox!"); return;
      case 2: {  // time of day, by the sun
        if (!haveTime) break;
        int h = tmNow.tm_hour;
        if (sunPh == SUN_NIGHT) {
          if (h >= 22 || h < 5)
            say(now, 0, irand(2) ? "It's %d:%02d. Bed time?" : "Why are we up so late?", h, tmNow.tm_min);
          else if (h < 12) say(now, 0, irand(2) ? "Still dark... five more minutes?" : "Is the sun up yet?");
          else say(now, 0, irand(2) ? "Dark already? Cozy." : "Look, the stars are out!");
        } else if (sunPh == SUN_DAWN) say(now, 0, irand(2) ? "Sunrise! Good morning!" : "Here comes the sun!");
        else if (sunPh == SUN_DUSK) say(now, 0, irand(2) ? "Pretty sunset, huh?" : "The sun is going to bed.");
        else if (h < 11) say(now, 0, irand(2) ? "Good morning!" : "Breakfast time?");
        else if (h < 14) say(now, 0, "Lunch o'clock!");
        else if (h < 18) say(now, 0, "Afternoon beach time.");
        else say(now, 0, "Long evening, still sunny!");
        return;
      }
      case 3: {  // high scores
        int g = numGames();
        for (int k = 0; k < 6 && g > 0; k++) {
          int i = irand(g);
          const char *key = nullptr;
          const char *name = gameInfo(i, &key);
          if (!key || !prefs.isKey(key)) continue;
          uint32_t best = prefs.getUInt(key, 0);
          if (!best) continue;
          if (irand(2)) say(now, 0, "Your %s best: %lu. Beat it!", name, (unsigned long)best);
          else say(now, 0, "%lu in %s? Is that a high score?", (unsigned long)best, name);
          return;
        }
        say(now, 0, "Play a game! Unlock shells!");
        return;
      }
      case 4:   // needs
        if (n.hunger < 30) { say(now, 0, "My tummy is rumbling..."); return; }
        if (n.fun < 30) { say(now, 0, "Sooo bored... play with me?"); return; }
        if (n.clean < 30) { say(now, 0, "I smell like low tide."); return; }
        if (n.energy < 25) { say(now, 0, "Yaaawn..."); return; }
        break;
      case 5: {  // shells
        int s = tigo::shellsUnlocked();
        if (s < tigo::NUM_SHELLS) {
          int need = tigo::SHELL_AT[s] - tigo::gamesWithScore();
          say(now, 0, "Score in %d more game%s for a new shell!", need, need == 1 ? "" : "s");
        } else say(now, 0, "All %d shells! I'm fancy.", tigo::NUM_SHELLS);
        return;
      }
      case 6:
        if (bleConnected()) { say(now, 0, "Hello phone! *waves claw*"); return; }
        break;
      default:
        say(now, 0, "%s", randomLine());
        return;
    }
  }
  say(now, 0, "%s", randomLine());
}

// Lines tigo::drawBubble needs for s: the same greedy wrap (116 px, font 2,
// one over-long word cut), without its 4-line stop.
static int bubbleLines(const char *s) {
  const int MAXW = 116;
  char tmp[100];
  int len = min((int)strlen(s), (int)sizeof tmp - 1), pos = 0, n = 0;
  while (pos < len) {
    int best = -1, bestW = 0, i = pos;
    while (true) {
      while (i < len && s[i] != ' ') i++;
      int k = i - pos;
      memcpy(tmp, s + pos, k); tmp[k] = 0;
      int w = spr.textWidth(tmp, 2);
      if (w > MAXW && best >= 0) break;
      best = k; bestW = w;
      if (i >= len) break;
      i++;
    }
    if (bestW > MAXW)
      while (best > 1) { tmp[--best] = 0; if (spr.textWidth(tmp, 2) <= MAXW) break; }
    n++;
    pos += max(best, 1);
    while (pos < len && s[pos] == ' ') pos++;
  }
  return n;
}

// The next part of phoneTxt in the bubble: all that is left if it fits in
// 4 lines, else the longest start that fits with "..." (cut at a space if
// one works, else inside the word); a later part opens with "...".
static void sayPhonePart(uint32_t now) {
  const char *rest = phoneTxt + phonePos;
  const char *lead = phonePos ? "..." : "";
  int len = strlen(rest), cut = len;
  char part[sizeof btxt];
  snprintf(part, sizeof part, "%s%s", lead, rest);
  if (bubbleLines(part) > 4) {
    cut = 0;
    for (int pass = 0; pass < 2 && !cut; pass++)
      for (int i = len - 1; i > 0; i--) {
        if (pass == 0 && rest[i] != ' ') continue;
        snprintf(part, sizeof part, "%s%.*s...", lead, i, rest);
        if (bubbleLines(part) <= 4) { cut = i; break; }
      }
    if (!cut) cut = 1;                     // can't happen with 116 px lines
    snprintf(part, sizeof part, "%s%.*s...", lead, cut, rest);
  }
  phonePos += cut;
  while (phoneTxt[phonePos] == ' ') phonePos++;
  say(now, 2500 + strlen(part) * 50, "%s", part);
}

// ---------------- behaviour ----------------
static void start(Act a, uint32_t now) {
  act = a; phase = 0; actAt = phaseAt = now; counter = 0;
  tyOff = 0;
  switch (a) {
    case A_IDLE: actLen = 1800 + irand(2500); break;
    case A_WANDER: actLen = 0; break;
    case A_TALK: sayContext(now); actLen = bubbleLen; break;
    case A_NAP: actLen = (night ? 16000 : 9000) + irand(6000); break;
    case A_DANCE: actLen = 4200; say(now, 0, irand(2) ? "Charging! Wheee!" : "Zap zap! Power dance!"); break;
    case A_HIDE: actLen = 2600; break;
    case A_PHONE:
      snprintf(phoneTxt, sizeof phoneTxt, "Phone says: %s", phoneMsg);
      phonePos = 0;
      sayPhonePart(now);
      havePhone = false;
      actLen = bubbleLen;
      break;
    case A_EAT:
      if (!treatRun) {   // one snack from the sky
        for (auto &f : food) if (!f.on) { f = {true, frand(40, 100), -10, 0, 0, 0}; break; }
        say(now, 1500, irand(2) ? "Snack time!" : "Ooh, food!");
      }
      break;
    case A_SHELL:
      shellP = {true, frand(45, 95), (float)SHORE + 4, 0, 0, (uint8_t)irand(tigo::NUM_SHELLS)};
      break;
    case A_BALL:
      ball.on = true;
      ball.x = irand(2) ? -10 : SCR_W + 10;
      ball.y = GROUND - 7;
      ball.vx = ball.x < 0 ? 50 : -50;
      break;
    default: break;
  }
}

static void pick(uint32_t now) {
  tigo::Needs &n = tigo::needs();
  struct W { Act a; int w; };
  W opts[] = {
    {A_IDLE, 10},
    {A_WANDER, 18},
    {A_TALK, 22},
    {A_EAT, n.hunger < 40 ? 40 : 4},
    {A_NAP, n.energy < 30 ? 40 : night ? 18 : 3},
    {A_POOP, (int32_t)(now - lastPoo) > 60000 ? (n.clean < 50 ? 12 : 5) : 0},
    {A_SHELL, 6},
    {A_BALL, n.fun < 50 ? 18 : 7},
    {A_BATT, 12},
    {A_WIFI, wifiConnected() ? 10 : 0},
    {A_PERI, wifiConnected() ? 0 : 6},
    {A_DANCE, usb ? 8 : 0},
    {A_HIDE, 3},
  };
  int total = 0;
  for (auto &o : opts) if (o.a != act) total += o.w;
  int r = irand(total);
  for (auto &o : opts) {
    if (o.a == act) continue;
    if (r < o.w) { start(o.a, now); return; }
    r -= o.w;
  }
  start(A_IDLE, now);
}

// walks towards x; true once there
static bool walkTo(float x, float dt) {
  x = clampX(x);
  float d = x - tx;
  if (fabsf(d) < 1.0f) { tx = x; return true; }
  faceL = d < 0;
  float step = WALK_SPEED * dt;
  tx += d > 0 ? min(step, d) : max(-step, d);
  pose = TIGO_WALK;
  return false;
}

// stands so that the claws reach x, facing it
static bool reachFor(float x, float dt) {
  bool left = x < tx;
  float target = clampX(left ? x + reach() - 2 : x - reach() + 2);
  if (fabsf(target - tx) < 1.0f) { faceL = left; return true; }
  if (walkTo(target, dt)) { faceL = left; return true; }
  return false;
}

static void newPhase(uint32_t now) { phase++; phaseAt = now; }

static void think(uint32_t now, float dt) {
  uint32_t e = now - actAt, pe = now - phaseAt;

  // interruptions: the phone first, then treats, then a cable plugged in
  if (havePhone && act != A_PHONE) { treatRun = false; start(A_PHONE, now); }
  if (pendingTreats > 0 && act != A_PHONE && !(act == A_EAT && treatRun)) {
    int k = 0;
    for (auto &f : food) if (!f.on && k < pendingTreats) { f = {true, frand(30, 110), -10.0f - k * 25, 0, 0, 1}; k++; }
    pendingTreats = 0;
    treatRun = true;
    start(A_EAT, now);
    say(now, 2200, "Treats from the phone!");
  }
  if (usb && !usbWas) start(A_DANCE, now);
  usbWas = usb;

  if (act != A_SIT && tyOff > 0) tyOff = max(0.0f, tyOff - 40 * dt);
  pose = TIGO_IDLE;
  switch (act) {
    case A_IDLE:
      if (e > actLen) pick(now);
      break;

    case A_WANDER:
      if (phase == 0) { phase = 1; counter = (int)frand(X_MIN, X_MAX); }
      if (walkTo(counter, dt)) pick(now);
      break;

    case A_TALK:
      if (e > actLen) pick(now);
      break;

    case A_PHONE:
      pose = e < 1200 ? TIGO_HAPPY : TIGO_IDLE;
      if (e > actLen) {
        if (phoneTxt[phonePos]) { sayPhonePart(now); actAt = now; actLen = bubbleLen; }   // the next part
        else pick(now);
      }
      break;

    case A_EAT: {
      if (phase == 1) {                    // chewing (the snack is already in the claw)
        pose = TIGO_EAT;
        if (pe > 1600) { tigo::feed(counter ? 15 : 25); phase = 0; phaseAt = now; }
        break;
      }
      // nearest snack, landed or about to
      Prop *f = nullptr;
      for (auto &c : food) if (c.on && (!f || fabsf(c.x - tx) < fabsf(f->x - tx))) f = &c;
      if (!f) {
        if (phase != 9) {
          phase = 9; phaseAt = now;
          if (treatRun) say(now, 2500, "Thanks, phone! <3");
          else say(now, 2000, irand(2) ? "Yum!" : "Crunchy!");
        }
        pose = TIGO_HAPPY;
        if (pe > 1500) { treatRun = false; pick(now); }
        break;
      }
      if (reachFor(f->x, dt) && f->y >= GROUND - 4) {
        counter = f->kind;
        f->on = false;
        newPhase(now);
      }
      break;
    }

    case A_POOP:
      if (phase == 0) {                     // ...
        if (pe > 700) {
          poo = {true, tx + (faceL ? 26 : -26), (float)GROUND, 0, 1, 0};
          if (poo.x < 6) poo.x = 6;
          if (poo.x > SCR_W - 6) poo.x = SCR_W - 6;
          lastPoo = now;
          say(now, 1600, irand(2) ? "Oops." : "Nature calls!");
          newPhase(now);
        }
      } else if (phase == 1) {              // look embarrassed, then bury it
        pose = TIGO_SAD;
        if (pe > 1600) newPhase(now);
      } else if (phase == 2) {
        if (reachFor(poo.x, dt)) newPhase(now);
      } else if (phase == 3) {              // dig: sand flies, the poo sinks
        pose = TIGO_PINCH;
        poo.vy = max(0.0f, 1.0f - pe / 2200.0f);
        if (irand(3) == 0)
          for (auto &s : sand) if (!s.on) { s = {true, poo.x, (float)GROUND - 2, frand(-40, 40), frand(-90, -50), 0}; break; }
        if (pe > 2200) {
          poo.on = false; mound = 1;
          tigo::wash();
          say(now, 1800, "All tidy!");
          newPhase(now);
        }
      } else {
        pose = TIGO_HAPPY;
        if (pe > 1400) pick(now);
      }
      break;

    case A_NAP:
      pose = TIGO_SLEEP;
      if ((now / 2000) != ((now - (uint32_t)(dt * 1000)) / 2000)) tigo::rest(1);
      if (e > actLen) { say(now, 1500, irand(2) ? "*yawn* Good nap." : "Was I snoring?"); start(A_IDLE, now); }
      break;

    case A_SHELL:
      if (phase == 0) {                     // a wave brings it up the beach
        shellP.y = SHORE + 6 + (GROUND - 5 - SHORE - 6) * min(1.0f, e / 1200.0f);
        if (e > 1200) newPhase(now);
      } else if (phase == 1) {
        if (reachFor(shellP.x, dt)) newPhase(now);
      } else if (phase == 2) {
        pose = TIGO_PINCH;
        if (pe > 700) {
          shellP.on = false; holdingShell = true; heldStyle = shellP.kind;
          say(now, 2400, irand(2) ? "Ooh! Shiny!" : "For my collection!");
          tigo::play(10);
          newPhase(now);
        }
      } else {
        pose = TIGO_HAPPY;
        if (pe > 2400) { holdingShell = false; pick(now); }
      }
      break;

    case A_BALL: {
      if (!ball.on) { pick(now); break; }
      if (e > 20000 && counter < 3) counter = 3;   // enough football
      if (phase == 0) {                      // chase it
        float tip = tx + (faceL ? -reach() : reach());
        if (fabsf(ball.x - tip) < 6 && fabsf(ball.vx) < 30) {
          ball.vx = (ball.x > tx ? 1 : -1) * frand(70, 110);
          counter++;
          newPhase(now);
        } else {
          reachFor(ball.x, dt);
        }
      } else {                               // a kick
        pose = TIGO_PINCH;
        if (pe > 500) {
          if (counter >= 3) {
            ball.vx = ball.x > SCR_W / 2 ? 140 : -140;   // kicked away for good
            tigo::play(20);
            say(now, 1800, irand(2) ? "Goal!" : "Again! Again!");
            start(A_IDLE, now);
          } else phase = 0;
        }
      }
      break;
    }

    case A_BATT:
      if (phase == 0) {
        if (reachFor(battX + BATT_W / 2, dt)) newPhase(now);
      } else if (phase == 1) {
        pose = TIGO_PINCH;
        battJig = sinf(pe * 0.04f) * 1.5f;
        if (!counter) {
          counter = 1;
          switch (irand(3)) {
            case 0: say(now, 2400, "Battery tastes like 3.7 V"); break;
            case 1: say(now, 2400, usb ? "It's tingly!" : "%d%%? Tasty.", pct); break;
            default: say(now, 2400, "Pinch! Pinch!"); break;
          }
        }
        if (pe > 2400) {
          battJig = 0;
          tigo::play(8);
          start(irand(2) ? A_DRAG : A_SIT, now);
        }
      }
      break;

    case A_DRAG: {                           // walks backwards, the battery follows the claws
      float hold = faceL ? -1 : 1;             // the side the battery is on
      if (phase == 0) {
        counter = (int)clampX(tx - hold * frand(12, 28));
        phase = 1;
        say(now, 1800, "Mine! All mine!");
      }
      float d = counter - tx;
      if (fabsf(d) > 1) {
        tx += (d > 0 ? 1 : -1) * WALK_SPEED * 0.6f * dt;
        pose = TIGO_WALK;
        float tip = tx + hold * reach();
        battX = hold < 0 ? tip + 4 - BATT_W : tip - 4;
        battX = constrain(battX, 6.0f, 60.0f);   // keep it clear of the WiFi sign
      } else if (pe > 3500) pick(now);
      break;
    }

    case A_SIT:
      // phase 0: stand next to it, 1: hop on, 2: sit, 3: hop off
      if (phase == 0) {
        if (walkTo(battX + 44, dt)) { faceL = true; newPhase(now); }
      } else if (phase == 1) {
        float f = min(1.0f, pe / 500.0f);
        tx = battX + 44 - 24 * f;
        tyOff = BATT_H * f + sinf(f * (float)M_PI) * 10;
        faceL = false;
        if (f >= 1) { newPhase(now); say(now, 2600, usb ? "Warm seat!" : "Comfy. %d%% comfy.", pct); }
      } else if (phase == 2) {
        tyOff = BATT_H;
        if (pe > 4500) newPhase(now);
      } else {
        float f = min(1.0f, pe / 450.0f);
        tx = battX + 20 + 24 * f;
        tyOff = BATT_H * (1 - f) + sinf(f * (float)M_PI) * 8;
        if (f >= 1) { tyOff = 0; pick(now); }
      }
      break;

    case A_WIFI:
      if (phase == 0) {
        if (walkTo(SIGN_X - reach() - 4, dt)) { faceL = false; newPhase(now); }
      } else if (phase == 1) {
        pose = TIGO_PINCH;
        // a wave leaves the sign every 700 ms and floats to the claws
        if ((int)(pe / 700) > counter && counter < 6) {
          counter++;
          for (auto &w : wave) if (!w.on) { w = {true, (float)SIGN_X, (float)SIGN_Y + 12, 0, 0, 0}; break; }
        }
        if (pe > 5200) {
          tigo::play(10);
          say(now, 2600, "Caught %d waves! %d dBm", counter, wifiRssi());
          newPhase(now);
        }
      } else {
        pose = TIGO_HAPPY;
        if (pe > 2000) pick(now);
      }
      break;

    case A_PERI:
      pose = TIGO_HIDE;
      if (phase == 0 && e > 600) {
        newPhase(now);
        if (wifiEnabled()) say(now, 3000, irand(2) ? "Searching for signal..." : "No WiFi... anyone?");
        else say(now, 3000, "WiFi is off. Quiet day.");
      }
      if (e > 6500) pick(now);
      break;

    case A_DANCE:
      pose = TIGO_HAPPY;
      faceL = (e / 450) % 2;
      if (e > actLen) pick(now);
      break;

    case A_HIDE:
      pose = e < 1900 ? TIGO_HIDE : TIGO_IDLE;
      if (e > 1900 && phase == 0) { phase = 1; say(now, 1400, "Phew."); }
      if (e > actLen + 1400) pick(now);
      break;
  }
}

// ---------------- physics of the props ----------------
static void stepProps(float dt) {
  for (auto &f : food) {
    if (!f.on) continue;
    if (f.y < GROUND - 4) { f.vy += 260 * dt; f.y += f.vy * dt; if (f.y > GROUND - 4) f.y = GROUND - 4; }
  }
  if (ball.on) {
    ball.x += ball.vx * dt;
    ballRot += ball.vx * dt / 7.0f;
    if (fabsf(ball.vx) < 130) ball.vx *= powf(0.55f, dt);   // sand slows it down
    if (fabsf(ball.vx) < 4) ball.vx = 0;
    bool leaving = fabsf(ball.vx) >= 115;
    if (!leaving) {
      if (ball.x < 8 && ball.vx < 0) ball.vx = -ball.vx * 0.7f;
      if (ball.x > SCR_W - 8 && ball.vx > 0) ball.vx = -ball.vx * 0.7f;
    }
    if ((ball.x < -20 || ball.x > SCR_W + 20) && (leaving || act != A_BALL)) ball.on = false;
  }
  for (auto &s : sand) {
    if (!s.on) continue;
    s.vy += 300 * dt; s.x += s.vx * dt; s.y += s.vy * dt;
    if (s.y > GROUND + 2) s.on = false;
  }
  float tipX = tx + (faceL ? -reach() : reach()), tipY = GROUND - tyOff - 24;
  for (auto &w : wave) {
    if (!w.on) continue;
    w.vx += dt / 1.1f;                       // progress 0..1
    if (w.vx >= 1) { w.on = false; continue; }
    w.x = SIGN_X + (tipX - SIGN_X) * w.vx;
    w.y = SIGN_Y + 12 + (tipY - SIGN_Y - 12) * w.vx - sinf(w.vx * (float)M_PI) * 14;
  }
  if (mound > 0) mound = max(0.0f, mound - dt / 4.0f);
}

// ---------------- rendering: the sky's lights ----------------
static void drawStars(int n, int seed, int yMax, uint8_t a0, uint8_t aT, bool neonTint, uint32_t t) {
  for (int i = 0; i < n; i++) {
    uint32_t h = hash32(i + seed);
    int x = h % SCR_W, y = (h >> 8) % yMax;
    uint8_t a = (uint8_t)((a0 + aT * triWave(t + (h >> 16) % 3000, 2600 + (h >> 20) % 1500)) * starA);
    uint16_t c = neonTint && !((h >> 4) % 3) ? (h & 2 ? N_CYAN : N_PINK) : WHITE;
    c = tft.alphaBlend(a, c, skyAt(y));
    if (neonTint || (h & 1)) spr.drawPixel(x, y, c);
    else spr.drawSpot(x, y, 0.8f, c);
  }
}

// a light's glittering path on the water, under x (strength a)
static void reflect(float x0, uint16_t c, float a0, float wide, int seed, uint32_t t) {
  for (int y = HORIZON + 3; y < SHORE - 1; y += 3) {
    float depth = (float)(y - HORIZON) / (SHORE - HORIZON);
    float x = x0 + sinf(t * 0.003f + y * 0.7f + seed) * (1.5f + depth * 2);
    int len = (int)((3 + (1 - depth) * 4) * wide);
    uint8_t a = (uint8_t)(170 * a0 * (1 - depth * 0.7f) * (0.6f + 0.4f * triWave(t + y * 40 + seed * 300, 1800)));
    spr.drawFastHLine((int)(x - len / 2), y, len, tft.alphaBlend(a, c, mix565(pal->sea0, pal->sea1, depth)));
  }
}

// ---------------- rendering: neon scene (Neon theme) ----------------
// a neon tube along a line: faint halo, glow (softer by day), bright core
static void tube(float ax, float ay, float bx, float by, float w, uint16_t c, uint16_t bg) {
  spr.drawWideLine(ax, ay, bx, by, w + 3.2f, tft.alphaBlend((uint8_t)(55 * glow), c, bg));
  spr.drawWideLine(ax, ay, bx, by, w + 1.4f, tft.alphaBlend((uint8_t)(60 + 70 * glow), c, bg));
  spr.drawWideLine(ax, ay, bx, by, w, c);
}

// the synthwave sun: a big disc, yellow at the top to hot pink at the bottom, cut by
// stripes of sky that widen towards its bottom; it sets behind the skyline
static void drawRetroSun(float cx, float cy, float r) {
  uint16_t bg = skyAt(min(cy, HORIZON - 1.0f));
  spr.drawSpot(cx, cy, r + 7, tft.alphaBlend(40, RSUN_MID, bg));
  spr.drawSpot(cx, cy, r + 3.5f, tft.alphaBlend(80, RSUN_MID, bg));
  for (int y = max(0, (int)ceilf(cy - r)); y <= (int)floorf(cy + r) && y < HORIZON; y++) {
    float dy = y - cy, hw = sqrtf(max(0.0f, r * r - dy * dy));
    float v = (dy + r) / (2 * r);
    uint16_t c = v < 0.5f ? mix565(RSUN_TOP, RSUN_MID, v * 2) : mix565(RSUN_MID, RSUN_BOT, v * 2 - 1);
    uint16_t sky = skyAt(y), under = tft.alphaBlend(80, RSUN_MID, sky);   // the inner halo there
    bool gap = false;
    for (int k = 0; k < 5 && !gap; k++) {
      float gy = cy + r * (-0.32f + 0.2f * k), gh = 1 + 0.6f * k;
      gap = y >= gy && y < gy + gh;
    }
    if (gap) {   // the stripe cuts through the disc and its inner halo
      float hh = sqrtf(max(0.0f, (r + 3.5f) * (r + 3.5f) - dy * dy));
      spr.drawFastHLine((int)ceilf(cx - hh), y, (int)floorf(cx + hh) - (int)ceilf(cx - hh) + 1, sky);
      continue;
    }
    int xl = (int)ceilf(cx - hw), xr = (int)floorf(cx + hw);
    if (xr >= xl) spr.drawFastHLine(xl, y, xr - xl + 1, c);
    spr.drawPixel(xl - 1, y, mix565(under, c, xl - (cx - hw)));   // soft edges
    spr.drawPixel(xr + 1, y, mix565(under, c, (cx + hw) - xr));
  }
}

// on/off flicker of a neon sign: mostly on, two quick drop-outs every few seconds
static bool flickerOn(uint32_t t, uint32_t period, uint32_t phase) {
  uint32_t p = (t + phase) % period;
  return !(p < 90 || (p > 180 && p < 260));
}

static const uint8_t BLD_X[] = {0, 13, 29, 41, 58, 73, 90, 105, 121};     // skyline: left edges
static const uint8_t BLD_H[] = {22, 34, 18, 40, 26, 31, 44, 24, 33};      // heights above the horizon
static const int NUM_BLD = sizeof BLD_X;

static void drawSkyNeon(uint32_t t) {
  fillSky();
  if (starA > 0) drawStars(20, 11, HORIZON - 40, 60, 150, true, t);   // a few stars over the city glow
  if (moonUp) {   // moon with a soft glow and a bite
    uint16_t mbg = skyAt(moonY);
    spr.drawSpot(moonX, moonY, 11, tft.alphaBlend((uint8_t)(40 * moonA), N_PINK, mbg));
    spr.drawSpot(moonX, moonY, 8.5f, tft.alphaBlend((uint8_t)(90 * moonA), N_PINK, mbg));
    spr.drawSpot(moonX, moonY, 6.5f, mix565(mbg, MOON, moonA));
    spr.drawSpot(moonX + 4, moonY - 3, 5.5f, tft.alphaBlend((uint8_t)(60 * moonA), N_PINK, skyAt(moonY - 3)));
  }
  if (sunUp) drawRetroSun(sunX, sunY, sunR);
  // skyline: dark blocks (purple silhouettes by day) with lit windows, some flickering;
  // by day most of the lights are off
  uint16_t bld = byNight(BLD_D, BLD), bld2 = byNight(BLD2_D, BLD2);
  uint16_t rim = byNight(RSUN_MID, N_CYAN);
  uint8_t rimA = (uint8_t)(90 + 90 * (1 - dark));
  uint32_t litMax = (uint32_t)(256 * (0.12f + 0.88f * dark));
  for (int b = 0; b < NUM_BLD; b++) {
    int x0 = BLD_X[b], x1 = b + 1 < NUM_BLD ? BLD_X[b + 1] : SCR_W, top = HORIZON - BLD_H[b];
    spr.fillRect(x0, top, x1 - x0, HORIZON - top, b & 1 ? bld : bld2);
    spr.drawFastHLine(x0, top, x1 - x0, tft.alphaBlend(rimA, rim, bld));   // rooftop rim light (sunlit by day)
    for (int y = top + 4; y < HORIZON - 3; y += 5)
      for (int x = x0 + 2; x < x1 - 2; x += 4) {
        uint32_t h = hash32(b * 97 + y * 13 + x);
        bool lit = (h & 7) < 3 && ((h >> 9) & 255) < litMax;
        if ((h & 63) == 5) lit = (t / (1500 + (h >> 8) % 2000)) % 2;   // someone flicks the light
        if (lit) spr.fillRect(x, y, 2, 2, WIN[(h >> 4) % 3]);
      }
  }
  // antenna with a blinking red light on the tallest tower
  spr.drawFastVLine(97, HORIZON - 44 - 9, 9, bld2);
  if ((t / 700) % 2) spr.drawSpot(97, HORIZON - 54, 1.4f, N_RED);
  // graffiti tags on the low walls
  uint16_t tagBg = bld2;
  spr.drawWideLine(31, 112, 34, 108, 1.0f, tft.alphaBlend(200, N_LIME, tagBg));
  spr.drawWideLine(34, 108, 36, 112, 1.0f, tft.alphaBlend(200, N_LIME, tagBg));
  spr.drawWideLine(36, 112, 39, 108, 1.0f, tft.alphaBlend(200, N_LIME, tagBg));
  spr.drawWideLine(107, 114, 111, 110, 1.0f, tft.alphaBlend(200, N_PINK, bld));
  spr.drawWideLine(111, 110, 115, 114, 1.0f, tft.alphaBlend(200, N_CYAN, bld));
  // signs: a pink "OPEN" box and a cyan wave
  bool on = flickerOn(t, 7300, 0);
  uint16_t sc = on ? N_PINK : tft.alphaBlend(90, N_PINK, bld);
  if (on) spr.fillSmoothRoundRect(90, 84, 34, 15, 4, tft.alphaBlend((uint8_t)(60 * glow), N_PINK, bld));
  spr.fillSmoothRoundRect(92, 86, 30, 11, 3, sc);
  spr.fillSmoothRoundRect(93, 87, 28, 9, 2, N_PANEL);
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(on ? tft.alphaBlend(120, WHITE, N_PINK) : sc);
  spr.drawString("OPEN", 107, 92, 1);
  bool on2 = flickerOn(t, 5100, 2300);
  uint16_t wc = on2 ? N_CYAN : tft.alphaBlend(90, N_CYAN, bld);
  for (int i = 0; i < 4; i++) {
    float x = 44 + i * 3.0f;
    float y0 = 86 + (i & 1 ? 2.5f : 0), y1 = 86 + (i & 1 ? 0 : 2.5f);
    if (on2) spr.drawWideLine(x, y0, x + 3, y1, 3.6f, tft.alphaBlend((uint8_t)(60 * glow), N_CYAN, bld2));
    spr.drawWideLine(x, y0, x + 3, y1, 1.3f, wc);
  }
  // a neon palm on the far shore
  const float px[4] = {8, 10, 13, 15}, py[4] = {HORIZON + 1.0f, 104, 92, 82};
  for (int i = 0; i < 3; i++) tube(px[i], py[i], px[i + 1], py[i + 1], 1.4f, N_PINK, pal->sky1);
  const float fx[5] = {-12, -7, 1, 8, 12}, fy[5] = {6, -4, -7, -3, 7};
  for (int i = 0; i < 5; i++) tube(15, 82, 15 + fx[i], 82 + fy[i], 1.2f, i & 1 ? N_CYAN : N_LIME, pal->sky1);
}

static void drawSeaNeon(uint32_t t) {
  spr.fillRectVGradient(0, HORIZON, SCR_W, SHORE - HORIZON + 6, pal->sea0, pal->sea1);
  spr.drawFastHLine(0, HORIZON, SCR_W, tft.alphaBlend(120, byNight(RSUN_TOP, N_PINK), pal->sea0));
  // the sun, the moon, the signs and the palm reflected as wobbling dashes
  if (sunUp) {
    reflect(sunX, RSUN_TOP, 1.0f, 2.4f, 7, t);
    reflect(sunX + 1, RSUN_BOT, 0.6f, 1.2f, 3, t);
  }
  if (moonUp) reflect(moonX, MOON, moonA, 1.0f, 3, t);
  const uint8_t rx[4] = {107, 49, 13, 70};
  const uint16_t rc[4] = {N_PINK, N_CYAN, N_LIME, WIN[0]};
  for (int i = 0; i < 4; i++) reflect(rx[i], rc[i], i == 3 ? dark : glow, i == 0 ? 1.6f : 1.0f, i, t);
}

static void drawNeonStar(float sx, float sy, float r, uint16_t c, uint16_t bg) {
  for (int pass = 0; pass < 3; pass++) {
    uint16_t col = pass == 0 ? tft.alphaBlend(60, c, bg) : pass == 1 ? c : N_PANEL;
    float w0 = pass == 0 ? 3.6f : pass == 1 ? 2.4f : 1.3f, w1 = pass == 0 ? 1.8f : pass == 1 ? 0.9f : 0.1f;
    for (int i = 0; i < 5; i++) {
      float a = -1.5708f + i * 1.2566f + 0.2f;
      spr.drawWedgeLine(sx, sy, sx + cosf(a) * (r - pass * 0.5f), sy + sinf(a) * (r - pass * 0.5f), w0, w1, col);
    }
  }
}

static void drawSandNeon(uint32_t t) {
  float swell = sinf((t % 7000) * 6.2831853f / 7000.0f);
  int base = SHORE + 2;
  spr.fillRectVGradient(0, base, SCR_W, 240 - base, pal->sand0, pal->sand1);
  for (int x = 0; x < SCR_W; x++) {          // dark water up to a glowing cyan foam line
    float yl = base + 3 + swell * 3 + sinf(x * 0.09f + t * 0.0012f) * 1.6f;
    int y = (int)yl;
    spr.drawFastVLine(x, base, max(0, y - base), pal->sea1);
    spr.drawFastVLine(x, y, 4, pal->wet);
    spr.drawPixel(x, y, tft.alphaBlend(230, N_CYAN, pal->sea1));
    spr.drawPixel(x, y - 1, tft.alphaBlend(90, N_CYAN, pal->sea1));
    spr.drawPixel(x, y + 1, tft.alphaBlend(80, N_CYAN, pal->wet));
  }
  for (int i = 0; i < 40; i++) {             // grains catching the neon
    uint32_t h = hash32(i + 501);
    int x = h % SCR_W, y = base + 10 + (h >> 8) % (240 - base - 12);
    spr.drawPixel(x, y, tft.alphaBlend(70 + (h >> 24) % 60, (h >> 20) & 1 ? N_PINK : N_CYAN, pal->sand0));
  }
  drawNeonStar(103, 229, 7, N_PINK, pal->sand1);
  spr.drawSpot(26, 231, 3.6f, tft.alphaBlend(70, N_CYAN, pal->sand1));     // pebble
  spr.drawSpot(26, 231, 2.6f, POST);
  // the bucket as a neon outline
  float bx = 14, by = 166;
  spr.drawWedgeLine(bx, by - 6, bx, by + 4, 8.5f, 6.8f, tft.alphaBlend(60, N_CYAN, pal->sand0));
  spr.drawWedgeLine(bx, by - 6, bx, by + 4, 7.2f, 5.5f, N_CYAN);
  spr.drawWedgeLine(bx, by - 6, bx, by + 4, 6.0f, 4.3f, N_PANEL);
  spr.drawWideLine(bx - 5, by - 6, bx + 5, by - 6, 1.4f, N_LIME);
  spr.drawWideLine(bx - 5, by - 8, bx - 1, by - 13, 1.2f, N_CYAN);
  spr.drawWideLine(bx - 1, by - 13, bx + 5, by - 8, 1.2f, N_CYAN);
}

static void drawBatteryNeon(uint32_t t) {
  float x = battX + battJig, y = GROUND - BATT_H + (battJig != 0 ? fabsf(battJig) * 0.5f : 0);
  uint16_t bg = pal->sand0;
  if (usb) {   // a glowing pink cable into the terminal (on the left)
    float px = x - 4, py = y + BATT_H / 2.0f;
    tube(-2, py - 26, px - 8, py - 6, 1.6f, N_PINK, bg);
    tube(px - 8, py - 6, px - 4, py, 1.6f, N_PINK, bg);
    spr.fillSmoothRoundRect((int)px - 6, (int)py - 3, 5, 6, 1, POST);
  }
  int ix = (int)x, iy = (int)y;
  uint16_t c = usb ? N_LIME : pct > 40 ? N_LIME : pct > 15 ? N_YELLOW : N_RED;
  spr.fillSmoothRoundRect(ix - 4, iy - 3, BATT_W + 7, BATT_H + 6, 6, tft.alphaBlend(55, c, bg));    // glow
  spr.fillSmoothRoundRect(ix - 4, iy + 3, 5, BATT_H - 6, 1, c);                                       // terminal
  spr.fillSmoothRoundRect(ix - 1, iy - 1, BATT_W + 2, BATT_H + 2, 4, c);                              // tube
  spr.fillSmoothRoundRect(ix + 1, iy + 1, BATT_W - 2, BATT_H - 2, 3, N_PANEL);
  int lvl = usb ? (int)((BATT_W - 6) * triWave(t, 2400)) : max(2, (BATT_W - 6) * pct / 100);
  spr.fillRect(ix + 3 + (BATT_W - 6 - lvl), iy + 3, lvl, BATT_H - 6, tft.alphaBlend(190, c, N_PANEL));
  if (usb) {   // bolt
    float cx = ix + BATT_W / 2.0f, cy = iy + BATT_H / 2.0f;
    spr.fillTriangle(cx + 1, cy - 5, cx - 3, cy + 1, cx, cy + 1, N_YELLOW);
    spr.fillTriangle(cx - 1, cy + 5, cx + 3, cy - 1, cx, cy - 1, N_YELLOW);
  }
  if (pct < 15 && !usb && (t / 500) % 2) spr.drawRoundRect(ix - 3, iy - 3, BATT_W + 6, BATT_H + 6, 5, N_RED);
  char buf[8];
  snprintf(buf, sizeof buf, usb ? "USB" : "%d%%", pct);
  spr.setTextDatum(BC_DATUM);
  spr.setTextColor(c);
  spr.drawString(buf, ix + BATT_W / 2, iy - 3, 1);
}

static void drawWifiSignNeon(uint32_t t) {
  spr.drawWideLine(SIGN_X, SIGN_Y + 18, SIGN_X, GROUND + 1, 4.0f, POST);
  spr.drawWideLine(SIGN_X - 1, SIGN_Y + 18, SIGN_X - 1, GROUND + 1, 1.0f, tft.alphaBlend(150, N_CYAN, POST));
  uint16_t bg = pal->sand0;
  spr.fillSmoothRoundRect(SIGN_X - 15, SIGN_Y - 2, 30, 26, 7, tft.alphaBlend(60, N_PINK, bg));
  spr.fillSmoothRoundRect(SIGN_X - 13, SIGN_Y, 26, 22, 5, N_PINK);
  spr.fillSmoothRoundRect(SIGN_X - 12, SIGN_Y + 1, 24, 20, 4, N_PANEL);
  bool on = wifiConnected();
  int bars = !on ? 0 : wifiRssi() > -60 ? 3 : wifiRssi() > -72 ? 2 : 1;
  float cx = SIGN_X, cy = SIGN_Y + 16;
  for (int i = 1; i <= 3; i++) {
    float r = 3.6f * i;
    bool lit = i <= bars && !(on && i == bars && (t / 400) % 3 == 0);   // the outer arc pulses
    for (int s = 0; s < 3; s++) {
      float a0 = -2.36f + s * 0.524f, a1 = a0 + 0.524f;
      float x0 = cx + cosf(a0) * r, y0 = cy + sinf(a0) * r, x1 = cx + cosf(a1) * r, y1 = cy + sinf(a1) * r;
      if (lit) spr.drawWideLine(x0, y0, x1, y1, 3.4f, tft.alphaBlend(80, N_CYAN, N_PANEL));
      spr.drawWideLine(x0, y0, x1, y1, 1.5f, lit ? N_CYAN : ARC_OFF);
    }
  }
  spr.drawSpot(cx, cy, 1.6f, on ? N_CYAN : ARC_OFF);
  if (!on) {
    spr.drawWideLine(cx + 4, cy - 12, cx + 9, cy - 7, 1.6f, N_RED);
    spr.drawWideLine(cx + 9, cy - 12, cx + 4, cy - 7, 1.6f, N_RED);
  }
}

static void drawBleNeon(uint32_t t) {
  float x = 13, y = 134 + sinf((t % 2000) * 0.00314f) * 2;
  uint16_t bg = pal->sea1;
  spr.drawSpot(x, y, 9.5f, tft.alphaBlend(60, BT_BLUE, bg));
  spr.drawSpot(x, y, 7.5f, BT_BLUE);
  spr.drawSpot(x, y, 6.3f, N_PANEL);
  const float L[5][4] = {{0, -4.5f, 0, 4.5f}, {0, -4.5f, 2.5f, -2.2f}, {2.5f, -2.2f, -2.5f, 2.2f},
                         {0, 4.5f, 2.5f, 2.2f}, {2.5f, 2.2f, -2.5f, -2.2f}};
  for (auto &l : L) spr.drawWideLine(x + l[0], y + l[1], x + l[2], y + l[3], 1.2f, N_CYAN);
  float s = 1.5f + 1.5f * triWave(t, 900);
  spr.drawWideLine(x + 8 - s, y - 7, x + 8 + s, y - 7, 1.0f, N_PINK);
  spr.drawWideLine(x + 8, y - 7 - s, x + 8, y - 7 + s, 1.0f, N_PINK);
}

// ---------------- rendering: scenery ----------------
static void drawDigit(float x, float y, int d, uint16_t c, float th = 4.6f) {
  static const uint8_t SEG[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
  const float w = 17, h = 32, g = 2.6f;
  float ax[6] = {x, x + w, x, x + w, x, x + w}, ay[6] = {y, y, y + h / 2, y + h / 2, y + h, y + h};
  static const uint8_t P[7][2] = {{0, 1}, {1, 3}, {3, 5}, {4, 5}, {2, 4}, {0, 2}, {2, 3}};
  for (int s = 0; s < 7; s++) {
    if (!(SEG[d] >> s & 1)) continue;
    float x0 = ax[P[s][0]], y0 = ay[P[s][0]], x1 = ax[P[s][1]], y1 = ay[P[s][1]];
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    dx /= l; dy /= l;
    spr.drawWideLine(x0 + dx * g, y0 + dy * g, x1 - dx * g, y1 - dy * g, th, c);
  }
}

// neon tube digits: a wide faint halo, a glow, then a pale core
static void drawClockNeon() {
  int h = tmNow.tm_hour, m = tmNow.tm_min;
  int dg[4] = {h / 10, h % 10, m / 10, m % 10};
  float xs[4] = {17, 40, 74, 97};
  const uint16_t cols[3] = {tft.alphaBlend(60, N_PINK, pal->sky0), tft.alphaBlend(150, N_PINK, pal->sky0),
                            tft.alphaBlend(110, WHITE, N_PINK)};
  const float th[3] = {9.0f, 6.4f, 2.6f};
  bool colon = tmNow.tm_sec % 2 == 0;
  for (int pass = 0; pass < 3; pass++) {
    for (int i = 0; i < 4; i++) drawDigit(xs[i], 10, dg[i], cols[pass], th[pass]);
    if (colon) {
      spr.drawSpot(67, 19, th[pass] * 0.55f, cols[pass]);
      spr.drawSpot(67, 34, th[pass] * 0.55f, cols[pass]);
    }
  }
}

static void drawClock(uint32_t now) {
  if (!haveTime) return;
  if (neon) drawClockNeon();
  int h = tmNow.tm_hour, m = tmNow.tm_min;
  int dg[4] = {h / 10, h % 10, m / 10, m % 10};
  float xs[4] = {17, 40, 74, 97};
  uint16_t c = byNight(WHITE, 0xFF95), sh = byNight(0x2A6E, 0x0000);
  if (!neon) {
    for (int pass = 0; pass < 2; pass++)
      for (int i = 0; i < 4; i++) drawDigit(xs[i] + (pass ? 0 : 1.5f), pass ? 10 : 11.5f, dg[i], pass ? c : sh);
    if (tmNow.tm_sec % 2 == 0) {
      spr.drawSpot(67, 19, 2.4f, c);
      spr.drawSpot(67, 34, 2.4f, c);
    }
  }
  static const char *const DAYS[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char *const MONTHS[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  char buf[20];
  snprintf(buf, sizeof buf, "%s %d %s", DAYS[tmNow.tm_wday % 7], tmNow.tm_mday, MONTHS[tmNow.tm_mon % 12]);
  spr.setTextDatum(TC_DATUM);
  if (neon) {
    spr.setTextColor(N_CYAN);
    spr.drawString(buf, 67, 48, 2);
    return;
  }
  spr.setTextColor(byNight(0x2A6E, 0xCE9C));
  spr.drawString(buf, 68, 49, 2);
  spr.setTextColor(byNight(WHITE, 0xCE9C));
  spr.drawString(buf, 67, 48, 2);
}

static void drawSky(uint32_t t) {
  if (neon) { drawSkyNeon(t); return; }
  fillSky();
  if (starA > 0) drawStars(26, 11, HORIZON - 8, 90, 165, false, t);   // twinkling stars
  if (moonUp) {                              // moon, with a bite
    spr.drawSpot(moonX, moonY, 9, mix565(skyAt(moonY), MOON, moonA));
    spr.drawSpot(moonX + 5, moonY - 4, 8, skyAt(moonY - 4));
  }
  if (sunUp) {
    uint16_t bg = skyAt(min(sunY, HORIZON - 1.0f));
    spr.drawSpot(sunX, sunY, sunR + 5.5f, tft.alphaBlend(60, sunC, bg));
    spr.drawSpot(sunX, sunY, sunR + 2.5f, tft.alphaBlend(120, sunC, bg));
    spr.drawSpot(sunX, sunY, sunR, sunC);
  }
  float cloudA = 1 - clamp01((dark - 0.35f) / 0.5f);   // the clouds fade into the night
  if (cloudA > 0) {
    uint16_t cc = mix565(WHITE, 0xFE96, warm);          // lit pink / peach by a low sun
    for (int i = 0; i < 2; i++) {            // clouds drifting by
      float x = fmodf(t / 1000.0f * (3 + i * 2) + i * 80, SCR_W + 60) - 30;
      float y = 74 + i * 16;
      uint16_t c = mix565(skyAt(y), cc, cloudA);
      spr.drawSpot(x, y, 6, c); spr.drawSpot(x + 8, y - 3, 7.5f, c); spr.drawSpot(x + 17, y, 6, c);
      spr.fillRect((int)x, (int)y, 18, 6, c);
    }
  }
}

static void drawSea(uint32_t t) {
  if (neon) { drawSeaNeon(t); return; }
  spr.fillRectVGradient(0, HORIZON, SCR_W, SHORE - HORIZON + 6, pal->sea0, pal->sea1);
  spr.drawFastHLine(0, HORIZON, SCR_W, tft.alphaBlend(120, WHITE, pal->sea0));
  for (int i = 0; i < 7; i++) {              // glints drifting on the water
    uint32_t h = hash32(i + 101);
    int y = HORIZON + 4 + h % (SHORE - HORIZON - 10);
    float x = fmodf((h >> 8) % SCR_W + t / 1000.0f * (2 + (h >> 4) % 4), SCR_W + 10) - 5;
    uint8_t a = (uint8_t)(200 * triWave(t + h % 2000, 2400));
    spr.drawFastHLine((int)x, y, 3 + (h >> 12) % 4, tft.alphaBlend(a, WHITE, pal->sea1));
  }
  if (sunUp) reflect(sunX, sunC, 0.35f + 0.55f * warm, 1.3f, 5, t);   // the sun's path on the water
  else if (moonUp) reflect(moonX, MOON, 0.6f * moonA, 1.0f, 5, t);
}

static void drawSand(uint32_t t) {
  if (neon) { drawSandNeon(t); return; }
  // the shoreline breathes in and out
  float swell = sinf((t % 7000) * 6.2831853f / 7000.0f);
  int base = SHORE + 2;
  spr.fillRectVGradient(0, base, SCR_W, 240 - base, pal->sand0, pal->sand1);
  for (int x = 0; x < SCR_W; x++) {
    float yl = base + 3 + swell * 3 + sinf(x * 0.09f + t * 0.0012f) * 1.6f;
    int y = (int)yl;
    spr.drawFastVLine(x, base, max(0, y - base), pal->sea1);
    spr.drawFastVLine(x, y, 4, pal->wet);                    // wet sand
    spr.drawPixel(x, y, tft.alphaBlend(200, WHITE, pal->sea1));   // foam
    spr.drawPixel(x, y - 1, tft.alphaBlend(110, WHITE, pal->sea1));
  }
  for (int i = 0; i < 46; i++) {             // grains
    uint32_t h = hash32(i + 501);
    int x = h % SCR_W, y = base + 10 + (h >> 8) % (240 - base - 12);
    spr.drawPixel(x, y, (h >> 20) & 1 ? pal->wet : tft.alphaBlend(120, WHITE, pal->sand0));
  }
  // a starfish and a pebble in front
  float sx = 103, sy = 229;
  for (int i = 0; i < 5; i++) {
    float a = -1.5708f + i * 1.2566f + 0.2f;
    spr.drawWedgeLine(sx, sy, sx + cosf(a) * 7, sy + sinf(a) * 7, 2.8f, 1.0f, PLUM);
  }
  for (int i = 0; i < 5; i++) {
    float a = -1.5708f + i * 1.2566f + 0.2f;
    spr.drawWedgeLine(sx, sy, sx + cosf(a) * 6.3f, sy + sinf(a) * 6.3f, 2.0f, 0.5f, byNight(STAR_C, 0xA2E7));
  }
  spr.drawSpot(sx, sy, 0.8f, byNight(0xFEEC, 0xB4AA));
  spr.drawSpot(26, 231, 3.2f, PLUM);
  spr.drawSpot(26, 231, 2.4f, byNight(0xB5D9, 0x6B4D));
  // the bucket near the water
  float bx = 14, by = 166;
  spr.fillTriangle(bx - 7, by - 6, bx + 7, by - 6, bx, by + 2, PLUM);
  spr.drawWedgeLine(bx, by - 6, bx, by + 4, 7.5f, 5.8f, PLUM);
  spr.drawWedgeLine(bx, by - 6, bx, by + 4, 6.5f, 4.8f, byNight(TEAL, 0x1B2B));
  spr.drawWideLine(bx - 6, by - 6, bx + 6, by - 6, 2.0f, byNight(0x7F1A, 0x3CD2));
  spr.drawWideLine(bx - 5, by - 8, bx - 1, by - 13, 1.0f, PLUM);
  spr.drawWideLine(bx - 1, by - 13, bx + 5, by - 8, 1.0f, PLUM);
}

// ---------------- rendering: badges ----------------
static void drawBattery(uint32_t t) {
  if (neon) { drawBatteryNeon(t); return; }
  float x = battX + battJig, y = GROUND - BATT_H + (battJig != 0 ? fabsf(battJig) * 0.5f : 0);
  if (usb) {   // a cable plugs into the terminal (on the left)
    float px = x - 4, py = y + BATT_H / 2.0f;
    spr.drawWideLine(-2, py - 26, px - 8, py - 6, 3.0f, PLUM);
    spr.drawWideLine(px - 8, py - 6, px - 4, py, 3.0f, PLUM);
    spr.drawWideLine(-2, py - 26, px - 8, py - 6, 1.6f, CABLE);
    spr.drawWideLine(px - 8, py - 6, px - 4, py, 1.6f, CABLE);
    spr.fillSmoothRoundRect((int)px - 6, (int)py - 3, 5, 6, 1, PLUM);
  }
  int ix = (int)x, iy = (int)y;
  spr.fillSmoothRoundRect(ix - 3, iy + 3, 5, BATT_H - 6, 1, PLUM);        // terminal
  spr.fillSmoothRoundRect(ix - 1, iy - 1, BATT_W + 2, BATT_H + 2, 4, PLUM);
  spr.fillSmoothRoundRect(ix, iy, BATT_W, BATT_H, 3, CREAM);
  uint16_t c = usb ? BAT_OK : pct > 40 ? BAT_OK : pct > 15 ? BAT_MID : BAT_LOW;
  int lvl = usb ? (int)((BATT_W - 4) * triWave(t, 2400)) : max(2, (BATT_W - 4) * pct / 100);
  spr.fillRect(ix + 2 + (BATT_W - 4 - lvl), iy + 2, lvl, BATT_H - 4, c);    // fills from the right, away from the terminal
  if (usb) {   // bolt
    float cx = ix + BATT_W / 2.0f, cy = iy + BATT_H / 2.0f;
    spr.fillTriangle(cx + 1, cy - 5, cx - 3, cy + 1, cx, cy + 1, PLUM);
    spr.fillTriangle(cx - 1, cy + 5, cx + 3, cy - 1, cx, cy - 1, PLUM);
  }
  if (pct < 15 && !usb && (t / 500) % 2) spr.drawRect(ix - 2, iy - 2, BATT_W + 4, BATT_H + 4, BAT_LOW);
  char buf[8];
  snprintf(buf, sizeof buf, usb ? "USB" : "%d%%", pct);
  spr.setTextDatum(BC_DATUM);
  spr.setTextColor(mix565(PLUM, 0xCE9C, (dark - 0.8f) * 5));   // dark text until the sand is dark too
  spr.drawString(buf, ix + BATT_W / 2, iy - 2, 1);
}

static void drawWifiSign(uint32_t t) {
  if (neon) { drawWifiSignNeon(t); return; }
  spr.drawWideLine(SIGN_X, SIGN_Y + 18, SIGN_X, GROUND + 1, 4.0f, PLUM);
  spr.drawWideLine(SIGN_X, SIGN_Y + 18, SIGN_X, GROUND + 1, 2.2f, WOOD);
  spr.fillSmoothRoundRect(SIGN_X - 13, SIGN_Y, 26, 22, 5, PLUM);
  spr.fillSmoothRoundRect(SIGN_X - 12, SIGN_Y + 1, 24, 20, 4, CREAM);
  bool on = wifiConnected();
  int bars = !on ? 0 : wifiRssi() > -60 ? 3 : wifiRssi() > -72 ? 2 : 1;
  float cx = SIGN_X, cy = SIGN_Y + 16;
  for (int i = 1; i <= 3; i++) {             // three arcs out of short segments
    float r = 3.6f * i;
    uint16_t c = i <= bars ? TEAL : GREY;
    if (on && i == bars && (t / 400) % 3 == 0) c = 0x7F1A;
    for (int s = 0; s < 3; s++) {
      float a0 = -2.36f + s * 0.524f, a1 = a0 + 0.524f;
      spr.drawWideLine(cx + cosf(a0) * r, cy + sinf(a0) * r, cx + cosf(a1) * r, cy + sinf(a1) * r, 2.0f, c);
    }
  }
  spr.drawSpot(cx, cy, 1.6f, on ? TEAL : GREY);
  if (!on) {   // a little red cross
    spr.drawWideLine(cx + 4, cy - 12, cx + 9, cy - 7, 1.6f, BAT_LOW);
    spr.drawWideLine(cx + 9, cy - 12, cx + 4, cy - 7, 1.6f, BAT_LOW);
  }
}

static void drawBle(uint32_t t) {
  if (!bleConnected()) return;
  if (neon) { drawBleNeon(t); return; }
  float x = 13, y = 134 + sinf((t % 2000) * 0.00314f) * 2;
  spr.drawSpot(x, y, 7.5f, PLUM);
  spr.drawSpot(x, y, 6.5f, BT_BLUE);
  spr.drawWideLine(x, y - 4.5f, x, y + 4.5f, 1.2f, WHITE);
  spr.drawWideLine(x, y - 4.5f, x + 2.5f, y - 2.2f, 1.2f, WHITE);
  spr.drawWideLine(x + 2.5f, y - 2.2f, x - 2.5f, y + 2.2f, 1.2f, WHITE);
  spr.drawWideLine(x, y + 4.5f, x + 2.5f, y + 2.2f, 1.2f, WHITE);
  spr.drawWideLine(x + 2.5f, y + 2.2f, x - 2.5f, y - 2.2f, 1.2f, WHITE);
  float s = 1.5f + 1.5f * triWave(t, 900);   // sparkle
  spr.drawWideLine(x + 8 - s, y - 7, x + 8 + s, y - 7, 1.0f, WHITE);
  spr.drawWideLine(x + 8, y - 7 - s, x + 8, y - 7 + s, 1.0f, WHITE);
}

// ---------------- rendering: props ----------------
static void drawFood(const Prop &f) {
  float x = f.x, y = f.y;
  if (f.kind == 1) {   // heart treat
    spr.drawSpot(x - 2, y - 2, 3.0f, PLUM); spr.drawSpot(x + 2, y - 2, 3.0f, PLUM);
    spr.fillTriangle(x - 5, y - 1, x + 5, y - 1, x, y + 5, PLUM);
    spr.drawSpot(x - 2, y - 2, 2.2f, TREAT); spr.drawSpot(x + 2, y - 2, 2.2f, TREAT);
    spr.fillTriangle(x - 4, y - 1, x + 4, y - 1, x, y + 4, TREAT);
    spr.drawSpot(x - 2.5f, y - 3, 0.8f, WHITE);
  } else {             // fish cracker
    spr.drawWedgeLine(x - 3, y, x + 2, y, 3.0f, 2.4f, PLUM);
    spr.fillTriangle(x - 8, y - 4, x - 8, y + 4, x - 3, y, PLUM);
    spr.drawWedgeLine(x - 3, y, x + 2, y, 2.2f, 1.6f, FISH);
    spr.fillTriangle(x - 7, y - 3, x - 7, y + 3, x - 3, y, FISH);
    spr.drawSpot(x + 1.5f, y - 0.5f, 0.6f, PLUM);
  }
}

static void drawBall() {
  float x = ball.x, y = ball.y, r = 7;
  spr.drawSpot(x, y, r + 1, PLUM);
  spr.drawSpot(x, y, r, WHITE);
  const uint16_t cols[3] = {BALL_R, BALL_B, BALL_Y};
  for (int i = 0; i < 3; i++) {   // three petals rolling with the ball
    float a = ballRot + i * 2.0944f;
    spr.drawWedgeLine(x + cosf(a) * 1.5f, y + sinf(a) * 1.5f, x + cosf(a) * 4.6f, y + sinf(a) * 4.6f, 1.2f, 2.4f, cols[i]);
  }
  spr.drawSpot(x, y, 1.3f, WHITE);
  spr.drawSpot(x - 3, y - 3.5f, 1.2f, WHITE);
}

static void drawPoo(uint32_t t) {
  if (poo.on) {
    float s = poo.vy, x = poo.x, y = GROUND + 1;
    if (s > 0.05f) {
      spr.drawSpot(x, y - 3 * s, 5.5f * s, PLUM);
      spr.drawSpot(x, y - 3 * s, 4.6f * s, POO);
      spr.drawSpot(x, y - 7.5f * s, 3.6f * s, PLUM);
      spr.drawSpot(x, y - 7.5f * s, 2.8f * s, POO);
      spr.drawSpot(x + 0.5f, y - 11 * s, 2.0f * s, PLUM);
      spr.drawSpot(x + 0.5f, y - 11 * s, 1.3f * s, POO);
      spr.drawSpot(x - 1.5f, y - 8.5f * s, 0.9f * s, POO_L);
      for (int i = 0; i < 2; i++) {          // stink lines
        float ph = fmodf(t / 900.0f + i * 0.5f, 1.0f);
        float sx = x - 4 + i * 8, sy = y - 14 - ph * 8;
        uint16_t c = tft.alphaBlend((uint8_t)(200 * (1 - ph)), 0x8E8B, pal->sand0);
        spr.drawWideLine(sx, sy, sx + 1.5f, sy - 3, 1.0f, c);
        spr.drawWideLine(sx + 1.5f, sy - 3, sx, sy - 6, 1.0f, c);
      }
    }
  }
  if (mound > 0) {   // a little sand hill where it was buried
    uint16_t c = tft.alphaBlend((uint8_t)(255 * mound), pal->wet, pal->sand1);
    spr.drawWedgeLine(poo.x - 5, GROUND + 1, poo.x + 5, GROUND + 1, 2.5f, 2.5f, c);
  }
}

static void drawWaveProp(const Prop &w) {
  float r = 4;
  uint16_t c = neon ? N_CYAN : TEAL;
  for (int s = 0; s < 3; s++) {
    float a0 = -2.36f + s * 0.524f, a1 = a0 + 0.524f;
    spr.drawWideLine(w.x + cosf(a0) * r, w.y + sinf(a0) * r, w.x + cosf(a1) * r, w.y + sinf(a1) * r, 1.8f, c);
    spr.drawWideLine(w.x + cosf(a0) * (r + 3), w.y + sinf(a0) * (r + 3), w.x + cosf(a1) * (r + 3), w.y + sinf(a1) * (r + 3), 1.8f, c);
  }
}

static void drawNote(float x, float y, uint16_t c) {
  spr.drawSpot(x, y, 2.0f, c);
  spr.drawWideLine(x + 1.6f, y, x + 1.6f, y - 8, 1.0f, c);
  spr.drawWideLine(x + 1.6f, y - 8, x + 4.5f, y - 6, 1.2f, c);
}

static void drawTigoScene(uint32_t t) {
  int cx = (int)tx, cy = GROUND - (int)tyOff;
  if (tyOff < 1) tigo::drawShadow(cx, GROUND + 1, TIGO_SIZE);
  drawTigo(cx, cy, TIGO_SIZE, pose, t, faceL);
  float k = TIGO_SIZE / 58.0f, fl = faceL ? -1 : 1;
  if (act == A_PERI && pose == TIGO_HIDE) {   // eye periscope out of the top of the shell
    uint32_t e = (t0 + t) - actAt;
    float up = min(1.0f, e / 700.0f) * (e > 5800 ? max(0.0f, (6500.0f - e) / 700.0f) : 1.0f);
    float bx = cx + fl * -6 * k, by = cy - 44 * k;
    float ey = by - up * 22;
    if (up > 0.05f) {
      spr.drawWedgeLine(bx, by, bx, ey + 3, 2.6f, 2.0f, PLUM);
      spr.drawWedgeLine(bx, by, bx, ey + 3, 1.6f, 1.1f, tigo::COL_CORAL);
      tigo::drawEye(bx, ey, 4.5f * min(1.0f, up * 1.5f), sinf(e / 500.0f), -0.2f, (e % 2900) < 110);
    }
  }
  if (holdingShell) {
    float hx = cx + fl * (reach() - 3), hy = cy - 30 * k + sinf(t / 150.0f) * 1.5f;
    tigo::drawShell(hx, hy, 5.5f, heldStyle, faceL);
  }
  if (act == A_DANCE) {
    for (int i = 0; i < 2; i++) {
      float ph = fmodf(t / 1300.0f + i * 0.5f, 1.0f);
      drawNote(cx + (i ? 30 : -20) + sinf(ph * 6.28f) * 4, cy - 50 - ph * 30, i ? 0xFE87 : WHITE);
    }
  }
}

static void drawBubble(uint32_t now) {
  if (!bubbleAt || (int32_t)(now - bubbleAt) > (int32_t)bubbleLen || !btxt[0]) return;
  float k = TIGO_SIZE / 58.0f;
  int headX = (int)(tx + (faceL ? -1 : 1) * 10 * k), headTop = GROUND - (int)tyOff - (int)(54 * k);
  tigo::drawBubble(btxt, headX, headTop, 62, now - bubbleAt);
}

static void render(uint32_t now) {
  uint32_t t = now - t0;
  drawSky(t);
  drawClock(now);
  drawSea(t);
  drawSand(t);
  if (shellP.on) {   // a washed-up shell, sparkling
    tigo::drawShell(shellP.x, shellP.y, 5.0f, shellP.kind);
    float s = 1.5f + 2 * triWave(t, 700);
    spr.drawWideLine(shellP.x + 6 - s, shellP.y - 9, shellP.x + 6 + s, shellP.y - 9, 1.0f, WHITE);
    spr.drawWideLine(shellP.x + 6, shellP.y - 9 - s, shellP.x + 6, shellP.y - 9 + s, 1.0f, WHITE);
  }
  drawWifiSign(t);
  drawBle(t);
  drawBattery(t);
  drawPoo(t);
  if (ball.on) drawBall();
  drawTigoScene(t);
  for (auto &f : food) if (f.on) drawFood(f);
  for (auto &s : sand) if (s.on) spr.drawSpot(s.x, s.y, 1.0f, pal->wet);
  for (auto &w : wave) if (w.on) drawWaveProp(w);
  drawBubble(now);
  spr.pushSprite(0, 0);
}

// ---------------- entry points ----------------
void begin() {
  uint32_t now = millis();
  tigo::load();
  t0 = lastStep = lastPoll = now;
  lastFrame = 0;
  battAt = 0;
  readEnv(now);
  usbWas = usb;
  for (auto &f : food) f.on = false;
  for (auto &s : sand) s.on = false;
  for (auto &w : wave) w.on = false;
  ball.on = shellP.on = poo.on = false;
  holdingShell = false; mound = 0; tyOff = 0; battX = BATT_HOME; battJig = 0;
  bubbleAt = 0; btxt[0] = 0; treatRun = false; havePhone = false; pendingTreats = 0;
  tx = frand(45, 85); faceL = irand(2);
  lastPoo = now;
  tigo::Needs &n = tigo::needs();
  if (n.energy < 25) start(A_NAP, now);
  else {
    start(A_TALK, now);
    if (tigo::takeNewShell()) { say(now, 3500, "New shell: %s! Look!", tigo::shellName(tigo::shell())); actLen = bubbleLen; }
  }
  dbg("saver: begin\n");
  render(now);
}

bool update(uint32_t now) {
  int eL = updateBtn(bL, now), eR = updateBtn(bR, now);
  if (eL == 1 || eR == 1) {
    tigo::save(now, true);
    dbg("saver: woken\n");
    return false;
  }
  if ((int32_t)(now - lastFrame) < (int32_t)FRAME_MS) return true;
  lastFrame = now;

  float dt = (int32_t)(now - lastStep) / 1000.0f;
  if (dt < 0) dt = 0;
  if (dt > 0.1f) dt = 0.1f;
  lastStep = now;

  readEnv(now);
  if ((int32_t)(now - lastPoll) >= 250) {     // messages and treats from the phone
    lastPoll = now;
    char buf[sizeof phoneMsg];
    if (bleTakeMessage(buf, sizeof buf) && buf[0]) {
      strncpy(phoneMsg, buf, sizeof phoneMsg - 1);
      phoneMsg[sizeof phoneMsg - 1] = 0;
      havePhone = true;
    }
    int tr = bleTakeTreats();
    if (tr > 0) pendingTreats = min(4, pendingTreats + tr);
  }

  tigo::tick(now);
  think(now, dt);
  stepProps(dt);
  render(now);
  tigo::save(now, false);
  return true;
}

}  // namespace saver

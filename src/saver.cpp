// Screensaver: placeholder, replaced by its real implementation.
#include "common.h"

namespace saver {
void begin() { spr.fillSprite(TFT_BLACK); spr.pushSprite(0, 0); }
bool update(uint32_t now) {
  int eL = updateBtn(bL, now), eR = updateBtn(bR, now);
  return !(eL == 1 || eR == 1);
}
}  // namespace saver

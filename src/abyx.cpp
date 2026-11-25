// Abyx: placeholder, replaced by its real implementation.
#include "common.h"

namespace abyx {
void drawIcon(int x, int y, bool dim, uint32_t t) { spr.drawCircle(x, y, 10, dim ? TFT_DARKGREY : TFT_WHITE); }
void begin() { returnToMenu(); }
void update(uint32_t now) {}
}  // namespace abyx

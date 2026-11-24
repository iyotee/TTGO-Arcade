// Tigo pet app: placeholder, replaced by its real implementation.
#include "common.h"

namespace tigoapp {
void drawIcon(int x, int y, bool dim, uint32_t t) { spr.drawCircle(x, y, 10, dim ? TFT_DARKGREY : TFT_WHITE); }
void menuInfo(char *buf, size_t n) { snprintf(buf, n, "Coming soon"); }
void begin() { returnToMenu(); }
void update(uint32_t now) {}
}  // namespace tigoapp

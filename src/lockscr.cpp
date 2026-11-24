// PIN lock screen: placeholder, replaced by its real implementation.
#include "common.h"

namespace lockscr {
bool enabled() { return false; }
void begin() {}
bool update(uint32_t now) { return false; }
}  // namespace lockscr

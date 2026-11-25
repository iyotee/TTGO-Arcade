// Cartridges: placeholder, replaced by its real implementation.
#include "common.h"

bool cartIsHeavy(const char *hiKey) { return false; }
int cartState() { return CART_NONE; }
bool cartLaunch(const char *hiKey) { return false; }
bool cartInstallBegin(uint32_t size, const uint8_t *sha256, uint32_t build, char *err, size_t errN) {
  snprintf(err, errN, "not available");
  return false;
}
bool cartInstallWrite(const uint8_t *data, size_t n) { return false; }
bool cartInstallEnd(char *err, size_t errN) { snprintf(err, errN, "not available"); return false; }
void cartInstallAbort() {}
int cartInstallProgress() { return 0; }

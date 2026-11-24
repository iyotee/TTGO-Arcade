// Bluetooth: placeholder, replaced by its real implementation.
#include "common.h"

void initBle() {}
void bleService(uint32_t now) {}
bool bleEnabled() { return false; }
void setBleEnabled(bool on) {}
bool bleConnected() { return false; }
bool bleTakeMessage(char *buf, size_t n) { return false; }
int bleTakeTreats() { return 0; }

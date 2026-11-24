// PIN lock extras for Settings and boot (lockscr.cpp). common.h declares the
// non-blocking lock screen (lockscr::enabled / begin / update).
#pragma once

namespace lockscr {
static const int PIN_MIN_LEN = 4, PIN_MAX_LEN = 8;

void run();          // blocking lock screen until the right PIN (deep sleep after 30 s idle)
bool setPin();       // blocking: new PIN twice, then saved and the lock turned on; false = cancelled
bool checkPin();     // blocking: type the current PIN; true = right, false = cancelled / idle
void disable();      // lock off, PIN forgotten
}  // namespace lockscr

// Crash reports: what the board was doing when it crashed (crashrec.cpp).
#pragma once
#include "common.h"

static const int CRASH_BT_MAX = 16;

struct CrashInfo {
  uint32_t build;                      // FW_BUILD of the firmware that crashed (0 = dev build)
  uint32_t fw;                         // 0 = launcher, 1 = Big Games cartridge
  uint32_t pc, cause, vaddr;           // faulting PC, Xtensa EXCCAUSE, EXCVADDR
  uint32_t n;                          // backtrace entries
  uint32_t bt[CRASH_BT_MAX];           // PCs, innermost first
  char where[24];                      // menu item that was active
};

// diag.cpp, on the boot after a panic / watchdog: moves the RTC record to NVS
void crashRecOnBoot(const char *where);
bool crashInfo(CrashInfo *out);        // false when no report is stored
void crashClear();                     // forgets the report and the "! crash in ..." line
// "TTGO b2 crash@Settings c28 v0\n400d1234 ..." (what the QR code holds)
void crashText(char *buf, size_t n);

namespace crashui { void open(); bool update(uint32_t now); }   // Settings > Crash report

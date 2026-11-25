// Crash forensics. The ESP-IDF panic handler is wrapped
// (-Wl,--wrap=esp_panic_handler in platformio.ini) so that the faulting PC, the
// exception cause and a short backtrace are written to RTC memory, which the
// reset that follows does not clear. On the next boot diag.cpp calls
// crashRecOnBoot(), which moves the record to NVS for Settings > Crash report.
// The addresses are decoded on the PC with the firmware.elf GitHub Actions
// keeps for every build (artifact "elf-<build>"):
//   xtensa-esp32-elf-addr2line -pfiaC -e firmware.elf <addresses>

#include "crashrec.h"
#include "esp_private/panic_internal.h"
#include "esp_debug_helpers.h"
#include "freertos/xtensa_context.h"

#ifndef FW_BUILD
#define FW_BUILD 0
#endif
#ifndef TTGO_CARTRIDGE
#define TTGO_CARTRIDGE 0
#endif

static const uint32_t REC_MAGIC = 0xC4A5B007;
struct RtcRec { uint32_t magic, build, fw, pc, cause, vaddr, n; uint32_t bt[CRASH_BT_MAX]; };
RTC_NOINIT_ATTR static RtcRec rtcRec;
static const char K_REC[] = "crashrec";

// return addresses on the stack carry the window size in their top bits
static inline IRAM_ATTR uint32_t stackPc(uint32_t pc) {
  if (pc & 0x80000000) pc = (pc & 0x3fffffff) | 0x40000000;
  return pc - 3;                       // the call instruction, not the one after it
}

extern "C" void __real_esp_panic_handler(panic_info_t *info);

// runs in the panic context: no allocation, no locks, nothing that can fault; in
// IRAM because the flash cache may be off (a crash during a flash write)
extern "C" IRAM_ATTR void __wrap_esp_panic_handler(panic_info_t *info) {
  const XtExcFrame *f = (const XtExcFrame *)info->frame;
  rtcRec.build = FW_BUILD;
  rtcRec.fw = TTGO_CARTRIDGE ? 1 : 0;
  rtcRec.n = 0;
  if (f) {
    rtcRec.pc = f->pc;
    rtcRec.cause = info->pseudo_excause ? 0x100 + f->exccause : f->exccause;
    rtcRec.vaddr = f->excvaddr;
    esp_backtrace_frame_t fr;
    fr.pc = f->pc;
    fr.sp = f->a1;
    fr.next_pc = f->a0;
    fr.exc_frame = f;
    rtcRec.bt[rtcRec.n++] = fr.pc;
    while (rtcRec.n < (uint32_t)CRASH_BT_MAX && fr.next_pc && esp_backtrace_get_next_frame(&fr))
      rtcRec.bt[rtcRec.n++] = stackPc(fr.pc);
  } else {
    rtcRec.pc = (uint32_t)info->addr;
    rtcRec.cause = 0xFFFF;
    rtcRec.vaddr = 0;
  }
  rtcRec.magic = REC_MAGIC;
  __real_esp_panic_handler(info);
}

void crashRecOnBoot(const char *where) {
  if (rtcRec.magic != REC_MAGIC) return;            // a watchdog that never reached the handler
  rtcRec.magic = 0;
  CrashInfo c;
  memset(&c, 0, sizeof c);
  c.build = rtcRec.build;
  c.fw = rtcRec.fw;
  c.pc = rtcRec.pc;
  c.cause = rtcRec.cause;
  c.vaddr = rtcRec.vaddr;
  c.n = min<uint32_t>(rtcRec.n, CRASH_BT_MAX);
  memcpy(c.bt, rtcRec.bt, sizeof c.bt);
  snprintf(c.where, sizeof c.where, "%s", where);
  prefs.putBytes(K_REC, &c, sizeof c);
  dbg("crash: build %u %s pc %08x cause %u\n", (unsigned)c.build, c.fw ? "cartridge" : "launcher",
      (unsigned)c.pc, (unsigned)c.cause);
}

bool crashInfo(CrashInfo *out) {
  if (!prefs.isKey(K_REC)) return false;
  return prefs.getBytes(K_REC, out, sizeof *out) == sizeof *out;
}

void crashClear() {
  if (prefs.isKey(K_REC)) prefs.remove(K_REC);
  if (prefs.isKey("lastcrash")) prefs.remove("lastcrash");
}

void crashText(char *buf, size_t n) {
  CrashInfo c;
  if (!crashInfo(&c)) { snprintf(buf, n, "no crash report"); return; }
  int len = snprintf(buf, n, "TTGO %s b%u @%s c%u v%x\n", c.fw ? "cart" : "fw", (unsigned)c.build, c.where,
                     (unsigned)c.cause, (unsigned)c.vaddr);
  for (uint32_t i = 0; i < c.n && len > 0 && (size_t)len < n; i++)
    len += snprintf(buf + len, n - len, i ? " %08x" : "%08x", (unsigned)c.bt[i]);
}

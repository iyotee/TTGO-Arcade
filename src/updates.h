// Updates over WiFi (updates.cpp): the manifest published by the CI on GitHub
// Pages, an hourly check, the background download job (launcher update or
// the Big Games cartridge, streamed into the other app slot with SHA-256
// checks) and the screens built on it: Settings > Updates (updui) and the
// cartridge download screen (cartui.cpp).
//
//   https://iyotee.github.io/TTGO-Arcade/fw/manifest.json
//   {"launcher":{"build","size","sha256","url"},
//    "cartridges":[{"id":"big","name","games":[...],"build","size","sha256","url"}]}
#pragma once
#include "common.h"

namespace updates {

struct Image {
  uint32_t build, size;               // size 0 = not in the manifest
  uint8_t sha[32];                    // SHA-256 of the .bin file
  char url[112];
};
struct Manifest {
  bool valid;                         // false until a fetch succeeded this boot
  Image launcher, cart;
};

void begin();                         // setup(): last known builds from prefs
void service(uint32_t now);           // launcher, Settings and cartridge screen loops (never in a game)
const Manifest &manifest();
uint32_t latestBuild();               // newest launcher build seen (kept in prefs), 0 = unknown
uint32_t latestCartBuild();           // same for the cartridge
bool updateAvailable();               // a newer launcher exists (never on a dev build, FW_BUILD 0)
bool autoCheck();                     // hourly check while WiFi is connected (saved)
void setAutoCheck(bool on);
int32_t checkedAgoS();                // seconds since the last successful check, -1 = none this boot

// ---- one background job at a time (a FreeRTOS task: the UI keeps running) ----
enum Job { JOB_NONE, JOB_CHECK, JOB_CART, JOB_FW };
enum Phase { PH_IDLE, PH_CONNECT, PH_DOWNLOAD, PH_VERIFY, PH_DONE, PH_FAILED };
// false (err filled) when it can't start: busy, no WiFi, not enough memory.
// A download may stop Bluetooth for its duration (TLS needs the heap); the
// setting is restored afterwards.
bool start(int job, char *err, size_t errN);
bool busy();
int job();                            // the current / last job
int phase();                          // PH_DONE / PH_FAILED once service() wrapped it up
void progress(uint32_t *done, uint32_t *total);
const char *error();                  // after PH_FAILED
bool cancelled();                     // the last job ended through cancel()
void cancel();

// ---- shared drawing for the update screens ----
void drawTitle(const char *title);    // small title + rule, like the OTA screen
// the running job: "Connecting...", progress bar with Tigo, "Checking..."
void drawJob(const char *what, uint32_t now);
// a bar with Tigo walking along it, percentage and KB below
void drawProgressBar(int y, uint32_t done, uint32_t total, uint32_t now);
void fmtSize(char *buf, size_t n, uint32_t bytes);   // "1.3 MB", "640 KB"
void drawWrapped(const char *s, int y, uint16_t col);   // centred font 1, on two lines if over 22 chars

}  // namespace updates

namespace updui { void open(); bool update(uint32_t now); }    // Settings > Updates; update(): false when leaving
namespace cartui { void open(const char *hiKey); bool update(uint32_t now); }   // cartui.cpp

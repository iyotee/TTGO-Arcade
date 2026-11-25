// Time service (clock.cpp): the saved time zone, NTP sync over WiFi and its status.
// Used by the Clock app, the screensaver, the menu and Bluetooth (phone time sync).
#pragma once

#include <Arduino.h>
#include <time.h>

static const int CLOCK_NUM_ZONES = 16;

int clockZone();                          // 0 .. CLOCK_NUM_ZONES - 1 (0 = Europe/Zurich)
void setClockZone(int idx, bool save);    // applies the TZ right away
const char *clockZoneCity(int idx);       // short, fits the menu: "Zurich", "New York"
const char *clockZoneLabel(int idx);      // "Europe/Zurich" (max 22 chars, font 1)
const char *clockZoneTz(int idx);         // POSIX TZ string
int clockUtcOffsetMin();                  // current local offset, DST included (0 if unknown)
void clockFmtOffset(char *buf, size_t n); // "UTC+1", "UTC+5:30", "UTC-7"
// Today's local sunrise and sunset at the zone's city, in minutes after local
// midnight (DST included), worked out once a day. False while the time is unknown.
// Where the sun stays up all day: 0 and 1440; where it stays down: both 720.
bool clockSun(int *riseMin, int *setMin);

// Sync status. The last sync survives deep sleep (RTC memory), like the time itself.
enum ClockSource { CLOCK_SRC_NONE, CLOCK_SRC_NTP, CLOCK_SRC_PHONE };
time_t clockLastSync();                   // epoch of the last sync, 0 = never since power-on
ClockSource clockSyncSource();
bool clockSyncing();                      // SNTP running, waiting for its first answer
// Call after settimeofday() with the phone's time (Bluetooth), so the status shows it.
void clockNoteSynced(ClockSource src);

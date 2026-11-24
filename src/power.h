// Screensaver and sleep delay options (power.cpp), for the Settings screen.
// The common.h API (idleSleepMs, goToSleep, deepSleepNow, ...) stays the entry
// point for everything else.
#pragma once
#include <stdint.h>

static const int NUM_SAVER_OPTS = 10;      // Off, 5s, 8s, 10s, 15s, 20s, 30s, 1m, 2m, 5m
static const int NUM_SLEEPDLY_OPTS = 11;   // 5s, 8s, 10s, 15s, 20s, 30s, 1m, 2m, 5m, 10m, Never

int saverOption();                         // 0 .. NUM_SAVER_OPTS - 1 (0 = Off)
void setSaverOption(int opt, bool save);
const char *saverLabel();
uint32_t saverDelayMs();                   // idle time before the screensaver, 0 = screensaver off

int sleepDelayOption();                    // 0 .. NUM_SLEEPDLY_OPTS - 1 (last = Never)
void setSleepDelayOption(int opt, bool save);
const char *sleepDelayLabel();
uint32_t sleepDelayMs();                   // time in the screensaver (or idle, saver off) before deep sleep, 0 = never

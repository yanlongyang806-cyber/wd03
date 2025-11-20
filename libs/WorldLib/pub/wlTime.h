#pragma once

void wlTimeSet(F32 time);
void wlTimeSetForce(F32 time);
F32 wlTimeGet(void);
void wlTimeSetScale(F32 timescale);
F32 wlTimeGetScale(void);
bool wlTimeIsForced(void);
void wlTimeClearIsForced(void);

void wlTimeSetStepScaleDebug(F32 timestepscale);
void wlTimeSetStepScaleGame(F32 timestepscale);
void wlTimeSetStepScaleLocal(F32 timestepscale);

F32 wlTimeGetStepScale(void);      // gets the overall timestepscale (debug*game)
F32 wlTimeGetStepScaleDebug(void); // gets the debug timestepscale
F32 wlTimeGetStepScaleGame(void);  // gets the game timestepscale
F32 wlTimeGetStepScaleLocal(void);  // gets the local timestepscale

void wlTimeUpdateServerTimeDiff(F32 serverTime);

const char *wlTimeGetTag(void);
F32 wlTimeGetClientTime(void);

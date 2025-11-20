#pragma once

typedef struct Entity Entity;
typedef struct PlayerHUDOptions PlayerHUDOptions;

void gclHUDOptionsEnable(void);
void gclHUDOptionsDisable(void);
PlayerHUDOptions* entGetCurrentHUDOptionsEx(Entity* pEnt, bool bGetRegionUI, bool bGetDefaults);
#define entGetCurrentHUDOptions(pEnt) entGetCurrentHUDOptionsEx(pEnt, false, true)

LATELINK;
void gameSpecific_HUDOptions_Init(const char* pchCategory);
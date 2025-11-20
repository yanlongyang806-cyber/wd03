#ifndef _HUDOPTIONS_COMMON_H_
#define _HUDOPTIONS_COMMON_H_

#define HUDOPTIONS_VERSION			1

typedef struct PlayerHUDOptions PlayerHUDOptions;
typedef struct PlayerHUDOptionsStruct PlayerHUDOptionsStruct;

bool upgradeHUDOptions(PlayerHUDOptions* pSrcOptions, PlayerHUDOptions* pDstOptions);
PlayerHUDOptions* getDefaultHUDOptions(S32 eSchemeRegion);

extern PlayerHUDOptionsStruct g_DefaultHUDOptions;

#endif
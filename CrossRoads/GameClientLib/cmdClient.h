#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

typedef struct Entity Entity;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct InventoryBag InventoryBag;
typedef struct PowerDef PowerDef;
//void hackyBlockCheck(Entity *e);
void entUsePower(int start, const char* powerName);
void entUsePowerID(int start, U32 id);

void RequestUsageSearch(const char *pDictName, const char *pResourceName);
void RequestReferencesSearch(const char *pDictName, const char *pResourceName);

void playVoiceSetSoundForTarget(U32 entRef, const char *filename);
void playVoiceSetSound(const char *soundName, const char *filename, U32 entRef);

void gclBagChangeActiveSlotWhenReady(Entity* pEnt, S32 eBagID, S32 iIndex, S32 iNewActiveSlot);
void gclUpdateActiveSlotRequests(Entity* pEnt, F32 fElapsed, GameAccountDataExtract* pExtract);

void gclHeadshotAttemptSaveScreenshotsForCostumes();
bool gclHeadshotHasPendingCostumeScreenshots();
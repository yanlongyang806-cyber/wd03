/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
**************************************************************************/

#pragma once
GCC_SYSTEM

#include "CostumeCommon.h"
#include "GlobalTypeEnum.h"

typedef struct Entity Entity;
typedef struct SpeciesDef SpeciesDef;
typedef struct CharacterClass CharacterClass;
typedef struct WLCostume WLCostume;
typedef struct PCFXTemp PCFXTemp;

extern bool g_bDisableClassStances;

// Create a world layer costumer from a player costume
WLCostume* costumeGenerate_CreateWLCostumeEx(PlayerCostume *pPCCostume, SpeciesDef *pSpecies, CharacterClass* pClass, const char** eaExtraStances, const PCSlotType *pSlotType,
PCMood *pMood, PCFXTemp ***eaAdditionalFX, char *pcNamePrefix, GlobalType type, ContainerID id, bool bIsPlayer, bool bGenerateSkelFX, WLCostume*** peaSubCostumes);
#define costumeGenerate_CreateWLCostume(pPCCostume, pSpeces, pClass, eaExtraStances, pSlotType, pMood, eaAdditionalFX, pcNamePrefix, type, id, bIsPlayer, peaSubCostumes)\
	costumeGenerate_CreateWLCostumeEx(pPCCostume, pSpeces, pClass, eaExtraStances, pSlotType, pMood, eaAdditionalFX, pcNamePrefix, type, id, bIsPlayer, true, peaSubCostumes)

// Get the name for a costume as if it was created by costumeCreateWLCostume.
const char *costumeGenerate_CreateWLCostumeName(PlayerCostume *pPCCostume, const char *pcNamePrefix, GlobalType type, ContainerID id, U32 uiSubCostumeIndex);

void costumeGenerate_FixEntityCostume(Entity* e);


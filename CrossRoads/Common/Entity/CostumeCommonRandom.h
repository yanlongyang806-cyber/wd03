/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
**************************************************************************/

#pragma once
GCC_SYSTEM

#include "CostumeCommon.h"

typedef struct Entity Entity;
typedef struct Guild Guild;
typedef struct MersenneTable MersenneTable;
typedef struct NOCONST(PlayerCostume) NOCONST(PlayerCostume);
typedef struct NOCONST(PCPart) NOCONST(PCPart);
typedef struct SpeciesDef SpeciesDef;


// Defines for the trimodal Gaussian distribution in the randomizer functions. 
// These are just arbitrary values that I thought graphed well. 
// Each of these is (mode, sigma, weight).
#define GAUSS_DIST_TRIPLE(min, max, mode, sigma, weight)\
	(min) + ((max)-(min))*(mode), ((max)-(min))*(sigma), (weight)
#define GAUSS_DIST_ONE(min, max)   GAUSS_DIST_TRIPLE(min, max, .1f, .075, 1)
#define GAUSS_DIST_TWO(min, max)   GAUSS_DIST_TRIPLE(min, max, .5f, .1, 4)
#define GAUSS_DIST_THREE(min, max) GAUSS_DIST_TRIPLE(min, max, .9f, .075, 1)
#define DEFAULT_TRIMODAL_DISRIBUTION(min, max) (min), (max), GAUSS_DIST_ONE((min), (max)), GAUSS_DIST_TWO((min), (max)), GAUSS_DIST_THREE((min), (max))


// Randomize the costume parts of the costume except for the given locked parts
void costumeRandom_ControlledRandomParts(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, Guild *pGuild, PlayerCostume **eaUnlockedCostumes, PCSlotType *pSlotType, PCRegion **eaLockedRegions, const char **eaPowerFXBones, PCStyle **eaStyles, bool bSymmetry, bool bBoneGroupMatching, bool bSortDisplay, bool bUnlockAll);

// Randomize the linked costume colors
void costumeRandom_ControlledRandomColors(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSotType,
	   PlayerCostume **eaUnlockedCostumes, PCControlledRandomLock eAllColorLocks, bool bSymmetry, bool bBoneGroupMatching, bool bSortDisplay, bool bUnlockAll);

// Randomize the colors of the costume part
void costumeRandom_ControlledRandomBoneColors(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType, PlayerCostume **eaUnlockedCostumes, PCBoneDef *pBone, bool bSymmetry, bool bBoneGroupMatching, bool bSortDisplay, bool bUnlockAll);

// Randomize the height of the costume
void costumeRandom_RandomHeight(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType);

// Randomize the muscles on the costume
void costumeRandom_RandomMuscle(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType);

// Randomize the body scales of the costume
void costumeRandom_RandomBodyScales(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType, const char* pchBodyScaleName);

// Randomize the bone scales of the costume for the given scale group (or "all" for all of them)
void costumeRandom_RandomBoneScales(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType, const char* pchGroupname);

// Randomize the stance of the costume
void costumeRandom_RandomStance(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType, bool bSortDisplay, PlayerCostume **eaUnlockedCostumes, bool bUnlockAll);

// Randomize the morphology of the costume
void costumeRandom_RandomMorphology(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType, const char *pchScaleGroup, bool bSortDisplay, bool bRandomizeBoneScales, bool bRandomizeNonBoneScales, PlayerCostume **eaUnlockedCostumes, bool bUnlockAll);


// Randomize the costume parts of the costume
void costumeRandom_RandomParts(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, Guild *pGuild, PCRegion *pRegion, PlayerCostume **eaUnlockedCostumes, PCSlotType *pSlotType, bool bSymmetry, bool bBoneGroupMatching, bool bSortDisplay, bool bUnlockAll, bool bRandomizeNonParts);

// Populates the costume with random parts based on the skeleton and type
void costumeRandom_FillRandom(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, Guild *pGuild, PCRegion *pRegion, const char *pchScaleGroup, PlayerCostume **eaUnlockCostumes, PCSlotType *pSlotType, bool bSymmetry, bool bBoneGroupMatching, bool bSortDisplay, bool bUnlockAll, bool bRandomizeNonParts, bool bRandomizeBoneScales, bool bRandomizeNonBoneScales);

// Sets the randomizer table to use
void costumeRandom_SetRandomTable(MersenneTable *pTable);


/***************************************************************************
*     Copyright (c) 2005-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef CHARACTERRESPECSERVER_H__
#define CHARACTERRESPECSERVER_H__

#include "PowerTree.h"

typedef struct Character Character;
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct PowerTreeSteps PowerTreeSteps;
typedef struct Entity Entity;
typedef enum enumTransactionOutcome enumTransactionOutcome;

// Performs a full respec on the Character
void character_RespecFull(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar);

// Performs a full advantage respec on the Character
void character_RespecAdvantages(SA_PARAM_NN_VALID Character *pchar);

U32 trhEntity_GetFreeRespecTime(ATH_ARG NOCONST(Entity) *e);

U32 trhEntity_GetForcedRespecTime(ATH_ARG NOCONST(Entity) *e);

// check if the character has a force respec to perform, and do it if it does
void character_CheckAndPerformForceRespec(Character *pChar);

// Set last respec to now if there is a respec available that we can't use (due to power qualifications)
// to prevent using it in the future

void RespecCheckResetTime(Entity *e);

enumTransactionOutcome trCharacter_AssignArchetype(ATR_ARGS, NOCONST(Entity)* e, const char* pchArchetype);

// Needed by UGC:
void PlayerRespecAsArchetype(Entity *pEnt, ACMD_NAMELIST("CharacterPath", REFDICTIONARY) const char* pchArchetype);

void Character_RemoveAllSecondaryCharacterPaths(Entity *pEnt, bool bRemoveTrees);

enumTransactionOutcome character_trh_RemoveAllSecondaryCharacterPaths(ATR_ARGS, ATH_ARG NOCONST(Entity)* e, S32 bRemoveTrees);

#endif

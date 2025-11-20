#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

typedef struct Character Character;
typedef struct Entity Entity;
typedef struct Item Item;
typedef struct PowerDef PowerDef;
typedef struct GameAccountDataExtract GameAccountDataExtract;

// Returns the DD AbilityMod for a given value
S32 combat_DDAbilityMod(F32 fValue);

// Returns the DD AbilityBonus for a given value
#define combat_DDAbilityBonus(fValue) MAX(0,combat_DDAbilityMod(fValue))

// Returns the DD AbilityPenalty for a given value
#define combat_DDAbilityPenalty(fValue) MIN(0,combat_DDAbilityMod(fValue))
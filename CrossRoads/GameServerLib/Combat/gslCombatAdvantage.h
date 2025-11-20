#ifndef _GSLCOMBATADVANTAGE_H__
#define _GSLCOMBATADVANTAGE_H__

#include "CharacterAttribsMinimal.h"

typedef struct Character Character;
typedef struct Entity Entity;
typedef U32 EntityRef;

void gslCombatAdvantage_CalculateFlankingForEntity(int iPartitionIdx, Entity *pEnt);

bool gslCombatAdvantage_HasFlankingImmunity(Character *pChar, EntityRef erTarget);

void gslCombatAdvantage_AddAdvantagedCharacter(Character *pChar, EntityRef erAdvantagedEnt, U32 applyID);
void gslCombatAdvantage_AddAdvantageToEveryone(Character *pChar, U32 applyID);
void gslCombatAdvantage_AddDisadvantageToEveryone(Character *pChar, U32 applyID);

void gslCombatAdvantage_RemoveAdvantagedCharacter(Character *pChar, EntityRef erAdvantagedEnt, U32 applyID);
void gslCombatAdvantage_RemoveAdvantageToEveryone(Character *pChar, U32 applyID);
void gslCombatAdvantage_RemoveDisadvantageToEveryone(Character *pChar, U32 applyID);

void gslCombatAdvantage_ClearPowersAppliedAdvantages(Character *pChar);

F32 gslCombatAdvantage_GetStrengthBonus(int iPartitionIdx, Character *pChar, EntityRef erTarget, AttribType offAttrib);

#endif
#pragma once
GCC_SYSTEM
/***************************************************************************
 *     Copyright (c) 2008, Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 ***************************************************************************/

// I think this could be made general and not used just for surveys. -poz

// After talking with Joe, we might want to make the gen's "glob" stuff
//   general, which would make all of this redundant. -poz

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

AUTO_STRUCT;
typedef struct IndexedPair
{
	const char *pchKey;			AST(KEY)
	const char *pchValue;
} IndexedPair;

AUTO_STRUCT;
typedef struct IndexedPairs
{
	IndexedPair **ppPairs;
} IndexedPairs;

#ifndef GAMECLIENT
typedef struct Entity Entity;
typedef struct MissionDef MissionDef;
void survey_Mission(Entity *playerEnt, MissionDef *missionDef);
#endif


/* End of File */


/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslMissionSet.h"

#include "entity.h"
#include "GlobalTypes.h"
#include "gslMission.h"
#include "mission_common.h"
#include "missionset_common.h"
#include "Player.h"
#include "ResourceManager.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// ----------------------------------------------------------------------------
//  Dictionary logic
// ----------------------------------------------------------------------------

AUTO_STARTUP(MissionSets) ASTRT_DEPS(Missions);
int missionset_LoadSets(void)
{
	static int loadedOnce = false;

	if (loadedOnce)
		return 1;

	resLoadResourcesFromDisk(g_MissionSetDictionary, MISSIONSET_BASE_DIR, MISSIONSET_DOTEXTENSION, NULL,  RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
	loadedOnce = true;

	return 1;
}


// ----------------------------------------------------------------------------
//  Functions to get a Mission from a MissionSet
// ----------------------------------------------------------------------------

MissionDef* missionset_RandomMissionFromSet(MissionSet *pSet)
{
	if (pSet){
		MissionSetEntry *pEntry = eaRandChoice(&pSet->eaEntries);
		return GET_REF(pEntry->hMissionDef);
	}
	return NULL;
}

// This should sort such that most recently completed missions are last
static int SortByLastCompleted(const Entity *pEnt, const MissionDef **ppDefA, const MissionDef **ppDefB)
{
	if (ppDefA && *ppDefA && ppDefB && *ppDefB && pEnt && pEnt->pPlayer && pEnt->pPlayer->missionInfo){
		return (mission_GetLastCompletedTimeByDef(pEnt->pPlayer->missionInfo, *ppDefA) - mission_GetLastCompletedTimeByDef(pEnt->pPlayer->missionInfo, *ppDefB));
	}
	return 0;
}

MissionDef* missionset_RandomAvailableMissionFromSet(Entity *pEnt, MissionSet *pSet)
{
	static MissionDef **eaMissionDefs = NULL;
	MissionDef *pChosenDef = NULL;
	int iPlayerLevel = entity_GetSavedExpLevel(pEnt);

	if (pSet){
		int i;
		for (i = eaSize(&pSet->eaEntries)-1; i>=0; --i){
			MissionDef *pDef = GET_REF(pSet->eaEntries[i]->hMissionDef);
			if (pDef
				&& (!pSet->eaEntries[i]->iMinLevel || iPlayerLevel >= pSet->eaEntries[i]->iMinLevel)
				&& (!pSet->eaEntries[i]->iMaxLevel || iPlayerLevel <= pSet->eaEntries[i]->iMaxLevel)){
				eaPush(&eaMissionDefs, pDef);
			}
		}
	}

	// Try to favor missions that the player hasn't completed recently
	if (eaSize(&eaMissionDefs) > 1 && pEnt && pEnt->pPlayer && pEnt->pPlayer->missionInfo){
		U32 uLastCompletedCutoff = 0;
		int iNewSize = eaSize(&eaMissionDefs)/2;

		eaQSort_s(eaMissionDefs, SortByLastCompleted, pEnt);

		uLastCompletedCutoff = mission_GetLastCompletedTimeByDef(pEnt->pPlayer->missionInfo, eaMissionDefs[iNewSize-1]);
		while ((iNewSize < eaSize(&eaMissionDefs)) && mission_GetLastCompletedTimeByDef(pEnt->pPlayer->missionInfo, eaMissionDefs[iNewSize]) == uLastCompletedCutoff){
			iNewSize++;
		}

		eaSetSize(&eaMissionDefs, iNewSize);
	}

	// Chose a random MissionDef
	pChosenDef = eaRandChoice(&eaMissionDefs);

	eaClearFast(&eaMissionDefs);
	return pChosenDef;
}


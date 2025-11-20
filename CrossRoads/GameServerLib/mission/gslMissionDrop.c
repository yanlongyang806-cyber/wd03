/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "encounter_common.h"
#include "entCritter.h"
#include "Entity.h"
#include "gslEncounter.h"
#include "gslMission.h"
#include "gslMissionDrop.h"
#include "gslMissionRequest.h"
#include "gslOldEncounter.h"
#include "mission_common.h"
#include "nemesis_common.h"
#include "oldencounter_common.h"
#include "Player.h"
#include "ReferenceSystem.h"
#include "WorldGrid.h"


// ----------------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------------

// Pre-grant MissionDrops that should happen on this map
static GlobalMissionDropContainer** g_GlobalMissionDropList = NULL;


// ----------------------------------------------------------------------------------
// Mission Drop Logic
// ----------------------------------------------------------------------------------

void missiondrop_RegisterGlobalMissionDrops(MissionDef *pDef)
{
	int i;

	if (!pDef) {
		return;
	}

	if (pDef->params) {
		for (i = 0; i < eaSize(&pDef->params->missionDrops); i++) {
			MissionDrop *pDrop = pDef->params->missionDrops[i];
			const char *pcMapName = zmapInfoGetPublicName(NULL);
			if ( pDrop
			    && pDrop->whenType == MissionDropWhenType_PreMission 
				&& pDef->eRequestGrantType != MissionRequestGrantType_Drop   // If this is a Requested mission, the drop will be handled by the Request system
				&& (!pDrop->pchMapName || (pcMapName && stricmp(pDrop->pchMapName, pcMapName) == 0)))
			{
				GlobalMissionDropContainer *pDropContainer = calloc(1, sizeof(GlobalMissionDropContainer));
				pDropContainer->pDrop = pDrop;
				if (GET_REF(pDef->parentDef)) {
					COPY_HANDLE(pDropContainer->hMissionDef, pDef->parentDef);
				} else {
					SET_HANDLE_FROM_STRING(g_MissionDictionary, pDef->name, pDropContainer->hMissionDef);
				}
				eaPush(&g_GlobalMissionDropList, pDropContainer);
			}
		}
	}

	// Recurse to all sub-missions
	for (i = 0; i < eaSize(&pDef->subMissions); i++) {
		missiondrop_RegisterGlobalMissionDrops(pDef->subMissions[i]);
	}
}


void missiondrop_UnregisterGlobalMissionDrops(MissionDef *pDef)
{
	int i;

	if (!pDef) {
		return;
	}

	for (i = eaSize(&g_GlobalMissionDropList)-1; i >= 0; --i) {
		GlobalMissionDropContainer *pDropContainer = g_GlobalMissionDropList[i];
		if (GET_REF(pDropContainer->hMissionDef) == pDef) {
			REMOVE_HANDLE(pDropContainer->hMissionDef);
			free(pDropContainer);
			eaRemove(&g_GlobalMissionDropList, i);
		}
	}
}


static bool missiondrop_MatchesCritter(MissionDrop *pDrop, Critter *pCritter, Entity *pPlayerEnt)
{
	CritterDef *pCritterDef = GET_REF(pCritter->critterDef);

	if (!pPlayerEnt || !pCritterDef) {
		return false;
	}

	switch(pDrop->type)
	{
		xcase MissionDropTargetType_Critter:
			if (pCritterDef->pchName && pDrop->value && !stricmp(pDrop->value, pCritterDef->pchName )) {
				return true;
			}

		xcase MissionDropTargetType_Group:
			if (GET_REF(pCritterDef->hGroup) && GET_REF(pCritterDef->hGroup)->pchName 
				&& pDrop->value && !stricmp(pDrop->value, GET_REF(pCritterDef->hGroup)->pchName)) {
				return true;
			}

		xcase MissionDropTargetType_Actor:
			if (pCritter && pCritter->encounterData.pGameEncounter) {
				const char *pcActorName = encounter_GetActorName(pCritter->encounterData.pGameEncounter, pCritter->encounterData.iActorIndex);
				if (pcActorName && pDrop->value && (stricmp(pcActorName,pDrop->value) == 0)) {
					return true;
				}
			} else if (gConf.bAllowOldEncounterData && pCritter && pCritter->encounterData.sourceActor && pCritter->encounterData.sourceActor->name 
						&& pDrop->value && !stricmp(pDrop->value, pCritter->encounterData.sourceActor->name)) {
				return true;
			}

		xcase MissionDropTargetType_EncounterGroup:
			if (pCritter && pCritter->encounterData.pGameEncounter)	{
				if (pCritter->encounterData.pGameEncounter->pEncounterGroup && pDrop->value &&
					(stricmp(pDrop->value, pCritter->encounterData.pGameEncounter->pEncounterGroup->pcName) == 0)) {
					return true;
				}
			} else if (gConf.bAllowOldEncounterData && pCritter && pCritter->encounterData.parentEncounter)	{
				OldStaticEncounter *staticEnc = GET_REF(pCritter->encounterData.parentEncounter->staticEnc);
				if (SAFE_MEMBER2(staticEnc, groupOwner, groupName) 
					&& pDrop->value && !stricmp(pDrop->value, staticEnc->groupOwner->groupName)) {
					return true;
				}
			}
			
		xcase MissionDropTargetType_Nemesis:
			if (pCritter && GET_REF(pCritter->hSavedPet) && (player_GetPrimaryNemesis(pPlayerEnt) == GET_REF(pCritter->hSavedPet))){
				return true;
			}

		xcase MissionDropTargetType_NemesisMinion:
			if (pCritter && GET_REF(pCritter->hSavedPetOwner) && (player_GetPrimaryNemesis(pPlayerEnt) == GET_REF(pCritter->hSavedPetOwner))){
				return true;
			}
	}

	return false;
}


static bool missiondrop_GlobalDropMatchesKill(GlobalMissionDropContainer *pDropContainer, Entity *pPlayerEnt, Critter *pCritter)
{
	if (pPlayerEnt && pCritter && pDropContainer && pDropContainer->pDrop) {
		MissionDef *pMissionDef = GET_REF(pDropContainer->hMissionDef);
		MissionDrop *pDrop = pDropContainer->pDrop;

		if (missiondrop_MatchesCritter(pDropContainer->pDrop, pCritter, pPlayerEnt)
			&& pDrop->whenType == MissionDropWhenType_PreMission 
			&& missiondef_CanBeOfferedAsPrimary(pPlayerEnt, pMissionDef, NULL, NULL)) {
			return true;
		}
	}
	return false;
}


static void missiondrop_GetMatchingDropsForMissionRecursive(Entity *pPlayerEnt, Mission *pMission, Critter *pCritter, MissionDrop ***peaMissionDropsOut)
{
	MissionDef *pMissionDef = mission_GetDef(pMission);
	int i;
	
	if (pMissionDef && pMissionDef->params && pMission && pMission->state == MissionState_InProgress) {
		for (i = 0; i < eaSize(&pMissionDef->params->missionDrops); i++) {
			MissionDrop *pDrop = pMissionDef->params->missionDrops[i];
			const char *pcMapName = zmapInfoGetPublicName(NULL);

			if ( pDrop && pDrop->RewardTableName &&	pDrop->RewardTableName[0] 
			    && pDrop->whenType == MissionDropWhenType_DuringMission 
				&& missiondrop_MatchesCritter(pDrop, pCritter, pPlayerEnt)
				&& (!pDrop->pchMapName || (pcMapName && (stricmp(pDrop->pchMapName, pcMapName) == 0))))
			{
				eaPush(peaMissionDropsOut, pDrop);
			}
		}
	}

	for (i = 0; i < eaSize(&pMission->children); i++) {
		missiondrop_GetMatchingDropsForMissionRecursive(pPlayerEnt, pMission->children[i], pCritter, peaMissionDropsOut);
	}
}


void missiondrop_GetMissionDropsForPlayerKill(Entity *pPlayerEnt, Critter *pCritter, MissionDrop ***peaMissionDropsOut)
{
	MissionInfo *pMissionInfo = SAFE_MEMBER2(pPlayerEnt, pPlayer, missionInfo);

	PERFINFO_AUTO_START_FUNC();

	// Get all drops from the player's current Missions
	if ( pPlayerEnt && pMissionInfo && pCritter) {
		int i, n = eaSize(&pMissionInfo->missions);
		for (i = 0; i < n; i++)	{
			missiondrop_GetMatchingDropsForMissionRecursive(pPlayerEnt, pMissionInfo->missions[i], pCritter, peaMissionDropsOut);
		}
		
		n = eaSize(&pMissionInfo->eaNonPersistedMissions);
		for (i = 0; i < n; i++) {
			missiondrop_GetMatchingDropsForMissionRecursive(pPlayerEnt, pMissionInfo->eaNonPersistedMissions[i], pCritter, peaMissionDropsOut);
		}

		n = eaSize(&pMissionInfo->eaDiscoveredMissions);
		for (i = 0; i < n; i++) {
			missiondrop_GetMatchingDropsForMissionRecursive(pPlayerEnt, pMissionInfo->eaDiscoveredMissions[i], pCritter, peaMissionDropsOut);
		}

		// Get all pre-Mission drops
		n = eaSize(&g_GlobalMissionDropList);
		for (i = 0; i < n; i++) {
			GlobalMissionDropContainer *pDropContainer = g_GlobalMissionDropList[i];
			if (missiondrop_GlobalDropMatchesKill(pDropContainer, pPlayerEnt, pCritter)) {
				eaPush(peaMissionDropsOut, g_GlobalMissionDropList[i]->pDrop);
			}
		}

		// Get all Pre-Mission drops from Mission Requests (they are not included in the global table)
		n = eaSize(&pMissionInfo->eaMissionRequests);
		for (i = 0; i < n; i++) {
			MissionDef *pRequestDef = GET_REF(pMissionInfo->eaMissionRequests[i]->hRequestedMission);
			if (pRequestDef && pRequestDef->params && missionrequest_IsRequestOpen(pPlayerEnt, pMissionInfo->eaMissionRequests[i])) {
				int j;
				for (j = eaSize(&pRequestDef->params->missionDrops)-1; j >= 0; --j) {
					MissionDrop *pDrop = pRequestDef->params->missionDrops[j];
					if (pDrop && pDrop->whenType == MissionDropWhenType_PreMission && missiondrop_MatchesCritter(pDrop, pCritter, pPlayerEnt)) {
						eaPush(peaMissionDropsOut, pDrop);
					}
				}
			}
		}
	}

	PERFINFO_AUTO_STOP();
}


void missiondrop_RefreshGlobalMissionDrops(void)
{
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct(g_MissionDictionary);
	int i, n = eaSize(&g_GlobalMissionDropList);
	for (i = n-1; i >= 0; --i)
	{
		REMOVE_HANDLE(g_GlobalMissionDropList[i]->hMissionDef);
		free(g_GlobalMissionDropList[i]);
	}
	eaClear(&g_GlobalMissionDropList);

	n = eaSize(&pStruct->ppReferents);
	for (i = 0; i < n; i++){
		missiondrop_RegisterGlobalMissionDrops((MissionDef*)pStruct->ppReferents[i]);
	}
}


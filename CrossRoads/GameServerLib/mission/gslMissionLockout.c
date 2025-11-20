/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Entity.h"
#include "EntityLib.h"
#include "gslMission.h"
#include "gslMissionLockout.h"
#include "gslEventSend.h"
#include "mission_common.h"
#include "team.h"


// ----------------------------------------------------------------------------
//  Static Data
// ----------------------------------------------------------------------------

// A MissionLockoutList is used to control Missions that have a MissionLockoutType
static MissionLockoutList **s_eaMissionLockoutLists = NULL;


static void missionlockout_ClearEscortsFromPlayers(MissionLockoutList *pList);


// ----------------------------------------------------------------------------
//  Lockout List Management
// ----------------------------------------------------------------------------

static MissionLockoutList *missionlockout_CreateList(int iPartitionIdx, MissionDef *pDef)
{
	MissionLockoutList *pLockoutList = calloc(1, sizeof(MissionLockoutList));

	assert(pDef && pDef->pchRefString); // Can't create a lockout list if lacking ref string

	pLockoutList->iPartitionIdx = iPartitionIdx;
	pLockoutList->pcMissionRefString = pDef->pchRefString;

	eaPush(&s_eaMissionLockoutLists, pLockoutList);

	eventsend_RecordMissionLockoutState(iPartitionIdx, pLockoutList->pcMissionRefString, missiondef_GetType(pDef), MissionLockoutState_Open, REF_STRING_FROM_HANDLE(pDef->hCategory));
	return pLockoutList;
}


static void missionlockout_DestroyList(MissionLockoutList *pList)
{
	MissionDef *pDef = missiondef_DefFromRefString(pList->pcMissionRefString);

	eventsend_RecordMissionLockoutState(pList->iPartitionIdx, pList->pcMissionRefString, missiondef_GetType(pDef), MissionLockoutState_Finished, REF_STRING_FROM_HANDLE(pDef->hCategory));

	missionlockout_ClearEscortsFromPlayers(pList);

	eaiDestroy(&pList->eaiPlayersWithCredit);
	eaiDestroy(&pList->eaiEscorting);

	free(pList);
}


// Returns a MissionLockoutList for the given MissionDef
MissionLockoutList* missionlockout_GetLockoutList(int iPartitionIdx, MissionDef *pDef)
{
	int i;

	if (!pDef || !pDef->pchRefString) {
		return NULL;
	}

	// Linear search for now, since list should be short
	for(i=eaSize(&s_eaMissionLockoutLists)-1; i>=0; --i) {
		MissionLockoutList *pList = s_eaMissionLockoutLists[i];
		if ((pList->iPartitionIdx == iPartitionIdx) && (pDef->pchRefString == pList->pcMissionRefString)) {
			return pList;
		}
	}

	return NULL;
}


// ----------------------------------------------------------------------------
//  Lockout List Access
// ----------------------------------------------------------------------------


static Entity *missionlockout_GetEntFromLockoutList(MissionLockoutList *pList, int iIndex)
{
	// Hardcoding GLOBALTYPE_ENTITYPLAYER is safe here because the type 
	// is checked before adding the entity to the list
	return entFromContainerID(pList->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pList->eaiPlayersWithCredit[iIndex]);
}


// Clear all escort entities from the players with credit
static void missionlockout_ClearEscortsFromPlayers(MissionLockoutList *pList)
{
	int iNumEscorting;
	int i, j, k;

	// Nothing to clear if not escorting
	if (eaiSize(&pList->eaiEscorting) == 0) {
		return;
	}

	for(i = 0; i < eaiSize(&pList->eaiPlayersWithCredit); ++i) {
		Entity *pPlayerEnt = entFromContainerID(pList->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pList->eaiPlayersWithCredit[i]);
		if (pPlayerEnt) {
			MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pPlayerEnt);
			if (pMissionInfo) {
				continue;
			}
			iNumEscorting = eaiSize(&pMissionInfo->eaiEscorting);

			if (iNumEscorting > 0) {
				for(j = 0; j < eaiSize(&pList->eaiEscorting); ++j) {
					for(k = iNumEscorting - 1; k >= 0; --k) {
						if (pMissionInfo->eaiEscorting[k] == pList->eaiEscorting[j]) {
							eaiRemove(&pMissionInfo->eaiEscorting, k);
						}
					}
				}
				mission_FlagInfoAsDirty(pMissionInfo);
			}
		}
	}
}


static void missionlockout_MarkListInProgress(MissionLockoutList *pList)
{
	if (pList && !pList->bInProgress) {
		MissionDef *pDef = missiondef_DefFromRefString(pList->pcMissionRefString);
		pList->bInProgress = true;
		eventsend_RecordMissionLockoutState(pList->iPartitionIdx, pList->pcMissionRefString, missiondef_GetType(pDef), MissionLockoutState_Locked, REF_STRING_FROM_HANDLE(pDef->hCategory));
	}
}


// Returns TRUE if this Lockout Mission is already in progress
bool missionlockout_MissionLockoutInProgress(int iPartitionIdx, MissionDef *pDef)
{
	MissionLockoutList *pList = missionlockout_GetLockoutList(iPartitionIdx, pDef);
	if (pList && pList->bInProgress) {
		return true;
	}
	return false;
}


// Returns TRUE if this player is on the lockout list for the given MissionDef
bool missionlockout_PlayerInLockoutList(MissionDef *pDef, Entity *pEnt, int iPartitionIdx)
{
	if (entGetType(pEnt) == GLOBALTYPE_ENTITYPLAYER) {
		MissionLockoutList *pList = missionlockout_GetLockoutList(iPartitionIdx, pDef);
		if (pList && (eaiFind(&pList->eaiPlayersWithCredit, entGetContainerID(pEnt)) != -1)) {
			return true;
		}
	}
	return false;
}


// update player mission info with the escorting entities
void missionlockout_UpdatePlayerEscorting(MissionLockoutList *pList)
{
	S32 j, k;
	for(j=eaiSize(&pList->eaiPlayersWithCredit)-1 ; j>=0; --j) {
		Entity *pPlayerEnt = entFromContainerID(pList->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pList->eaiPlayersWithCredit[j]);

		if (pPlayerEnt) {
			MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pPlayerEnt);
			if (pMissionInfo) {
				int old_size = eaiSize(&pMissionInfo->eaiEscorting);

				for(k=eaiSize(&pList->eaiEscorting)-1; k>=0; --k) {
					eaiPushUnique(&pMissionInfo->eaiEscorting, pList->eaiEscorting[k]);
				}

				if (eaiSize(&pMissionInfo->eaiEscorting) != old_size) {
					mission_FlagInfoAsDirty(pMissionInfo);
				}
			}
		}
	}
}


// Adds the specified player to the lockout list for the Mission
void missionlockout_AddPlayerToLockoutList(MissionDef *pDef, Entity *pEnt)
{
	MissionLockoutList *pList;
	int iPartitionIdx;

	if (entGetType(pEnt) != GLOBALTYPE_ENTITYPLAYER) {
		return;
	}

	iPartitionIdx = entGetPartitionIdx(pEnt);
	pList = missionlockout_GetLockoutList(iPartitionIdx, pDef);
	if (!pList) {
		pList = missionlockout_CreateList(iPartitionIdx, pDef);
	}
	if (!pList->bInProgress) {
		// Add Player
		eaiPushUnique(&pList->eaiPlayersWithCredit, entGetContainerID(pEnt));

		// If this is a Team lockout, add all team members
		if (pDef->lockoutType == MissionLockoutType_Team) {
			if (team_IsMember(pEnt)) {
				Team *pTeam = team_GetTeam(pEnt);
				if (pTeam && pEnt->pTeam->eState == TeamState_Member) {
					int i;
					for(i=eaSize(&pTeam->eaMembers)-1; i>=0; --i) {
						eaiPushUnique(&pList->eaiPlayersWithCredit, pTeam->eaMembers[i]->iEntID);
					}
				}
			}
		}

		// update player mission info with the escorting entities
		missionlockout_UpdatePlayerEscorting(pList);
	}
}


// Gets a list of all players on the LockoutList
void missionlockout_GetLockoutListEnts(int iPartitionIdx, MissionDef *pDef, Entity ***peaEntsOut)
{
	MissionLockoutList *pList = missionlockout_GetLockoutList(iPartitionIdx, pDef);
	if (pList) {
		int i, n = eaiSize(&pList->eaiPlayersWithCredit);
		for (i = 0; i < n; i++) {
			Entity *pEnt = missionlockout_GetEntFromLockoutList(pList, i);
			if (pEnt) {
				eaPush(peaEntsOut, pEnt);
			}
		}
	}
}


// Sets the list of players on the LockoutList
void missionlockout_SetLockoutListEnts(int iPartitionIdx, MissionDef *pDef, Entity ***peaEntsIn)
{
	MissionLockoutList *pList = missionlockout_GetLockoutList(iPartitionIdx, pDef);
	if (pList) {
		int i, n = eaSize(peaEntsIn);

		missionlockout_ClearEscortsFromPlayers(pList);
		eaiClear(&pList->eaiPlayersWithCredit);

		for (i = 0; i < n; i++) {
			Entity *pEnt = (*peaEntsIn)[i];
			if (pEnt && entGetType(pEnt) == GLOBALTYPE_ENTITYPLAYER) {
				eaiPush(&pList->eaiPlayersWithCredit, entGetContainerID(pEnt));
			}
		}

		missionlockout_UpdatePlayerEscorting(pList);
		eventsend_RecordMissionLockoutState(iPartitionIdx, pList->pcMissionRefString, missiondef_GetType(pDef), MissionLockoutState_ListUpdate, REF_STRING_FROM_HANDLE(pDef->hCategory));
	}
}


// ----------------------------------------------------------------------------
//  Lockout List Lifecycle
// ----------------------------------------------------------------------------


// Locks the Lockout List for the specified Mission
void missionlockout_BeginLockout(int iPartitionIdx, MissionDef *pDef)
{
	MissionLockoutList *pList = missionlockout_GetLockoutList(iPartitionIdx, pDef);
	if (pList) {
		missionlockout_MarkListInProgress(pList);
	}
}


// Destroys the specified Lockout List
void missionlockout_DestroyLockoutList(int iPartitionIdx, MissionDef *pDef)
{
	MissionLockoutList *pList = missionlockout_GetLockoutList(iPartitionIdx, pDef);
	if (pList && pDef) {
		missionlockout_DestroyList(pList);
		eaFindAndRemove(&s_eaMissionLockoutLists, pList);
	}
}


// Destroys all Mission Lockout Lists on a partition, resetting the state of all Mission Lockout
void missionlockout_ResetPartition(int iPartitionIdx)
{
	int i;
	for(i=eaSize(&s_eaMissionLockoutLists)-1; i>=0; --i) {
		MissionLockoutList *pList = s_eaMissionLockoutLists[i];
		if (pList->iPartitionIdx == iPartitionIdx) {
			missionlockout_DestroyList(pList);
			eaRemove(&s_eaMissionLockoutLists, i);
		}
	}
}


// Destroys all Mission Lockout Lists, resetting the state of all Mission Lockout
void missionlockout_ResetAllPartitions(void)
{
	eaDestroyEx(&s_eaMissionLockoutLists, missionlockout_DestroyList);
}



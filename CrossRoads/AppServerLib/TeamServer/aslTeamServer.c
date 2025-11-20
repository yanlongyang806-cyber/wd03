/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "AutoGen/aslTeamServer_c_ast.h"
#include "AppServerLib.h"
#include "aslTeamServer.h"
#include "objContainer.h"
#include "Team.h"
#include "autogen/controller_autogen_remotefuncs.h"
#include "autogen/AppServerLib_autogen_remotefuncs.h"
#include "autogen/GameServerLib_autogen_remotefuncs.h"
#include "serverlib.h"
#include "AutoTransDefs.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "logging.h"
#include "ResourceInfo.h"
#include "GlobalTypes.h"
#include "Player.h"
#include "EntityLib.h"
#include "GameSession.h"
#include "AutoGen/GameSession_h_ast.h"
#include "StashTable.h"
#include "mission_common.h"
#include "progression_common.h"
#include "progression_transact.h"
#include "Character.h"
#include "StringCache.h"
#include "queue_common.h"
#include "qsortG.h"

TimingHistory *gCreationHistory = NULL;
TimingHistory *gActionHistory = NULL;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Entity -> Team lookup management.
//   We keep around these stash tables of entity to team info for members and disconnecteds to allow
// us to correctly predict what transactions we will need in order to get a ForceJoin to work when all
// we have is an Entity ID. Since the actual Entity data may be on a GameServer or the LoginServer or the ObjectDB,
// it would involve much overhead to get the info through a read-only transaction or through a remote command with return.

static StashTable g_StashEntityMemberToTeamLookup;
static StashTable g_StashEntityDisconnectedToTeamLookup;

void aslTeam_InitStashedTeamLookups()
{
	if (!g_StashEntityMemberToTeamLookup)
	{
		g_StashEntityMemberToTeamLookup = stashTableCreateInt(256);
	}
	if (!g_StashEntityDisconnectedToTeamLookup)
	{
		g_StashEntityDisconnectedToTeamLookup = stashTableCreateInt(256);
	}
}

int aslTeam_GetStashedMemberTeamID(int iMemberID)
{
	int iReturnVal = 0;
	if (stashIntFindInt(g_StashEntityMemberToTeamLookup, iMemberID, &iReturnVal))
	{
		// Got it
	}
	return(iReturnVal);
}

void aslTeam_AddStashedMemberTeamID(int iMemberID, int iTeamID)
{
	stashIntAddInt(g_StashEntityMemberToTeamLookup, iMemberID, iTeamID, true); // Overwrite if found
}

void aslTeam_RemoveStashedMemberTeamID(int iMemberID)
{
	stashIntRemoveInt(g_StashEntityMemberToTeamLookup, iMemberID, NULL);
}

int aslTeam_GetStashedDisconnectedTeamID(int iDisconnectedID)
{
	int iReturnVal = 0;
	if (stashIntFindInt(g_StashEntityDisconnectedToTeamLookup, iDisconnectedID, &iReturnVal))
	{
		// Got it
	}
	return(iReturnVal);
}

void aslTeam_AddStashedDisconnectedTeamID(int iDisconnectedID, int iTeamID)
{
	stashIntAddInt(g_StashEntityDisconnectedToTeamLookup, iDisconnectedID, iTeamID, true); // Overwrite if found
}

void aslTeam_RemoveStashedDisconnectedTeamID(int iDisconnectedID)
{
	stashIntRemoveInt(g_StashEntityDisconnectedToTeamLookup, iDisconnectedID, NULL);
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// ObjRegisterContainer callbacks. Set up in aslTeamServerInit.

void aslTeam_StatePreChangeMembers_CB(Container *con, ObjectPathOperation **operations)
{
	Team *pTeam = con->containerData;
	if(pTeam)
	{
		S32 i;

		// Remove the members from the stash, we will readd them in postcommit
		for(i = 0; i < eaSize(&pTeam->eaMembers); i++)
		{
			aslTeam_RemoveStashedMemberTeamID(pTeam->eaMembers[i]->iEntID);
		}
	}
}

// (This was the only Old change callback)
void aslTeam_StatePostChangeMembers_CB(Container *con, ObjectPathOperation **operations)
{
	Team *pTeam = con->containerData;
	if(pTeam)
	{
		S32 i;
		QueueIntArray *pMemberList = StructCreate(parse_QueueIntArray);

		//Clone the team members
		for(i = 0; i < eaSize(&pTeam->eaMembers); i++)
		{
			eaiPush(&pMemberList->piArray, pTeam->eaMembers[i]->iEntID);
		}
		if(eaiSize(&pMemberList->piArray))
		{
			RemoteCommand_aslQueue_TeamUpdate(GLOBALTYPE_QUEUESERVER, 0, pTeam->iContainerID, pMemberList);
		}
		else
		{
			RemoteCommand_aslQueue_TeamRemove(GLOBALTYPE_QUEUESERVER, 0, pTeam->iContainerID);
		}

		StructDestroy(parse_QueueIntArray, pMemberList);

		/////////
		// Update the stash. Anything appropriate was removed in the pre commit

		for(i = 0; i < eaSize(&pTeam->eaMembers); i++)
		{
			aslTeam_AddStashedMemberTeamID(pTeam->eaMembers[i]->iEntID, pTeam->iContainerID);
		}

	}
}

void aslTeam_StatePreChangeDisconnecteds_CB(Container *con, ObjectPathOperation **operations)
{
	Team *pTeam = con->containerData;
	if(pTeam)
	{
		S32 i;

		// Remove the disconnecteds from the stash, we will readd them in postcommit
		for(i = 0; i < eaSize(&pTeam->eaDisconnecteds); i++)
		{
			aslTeam_RemoveStashedDisconnectedTeamID(pTeam->eaDisconnecteds[i]->iEntID);
		}
	}
}

// (This was the only Old change callback)
void aslTeam_StatePostChangeDisconnecteds_CB(Container *con, ObjectPathOperation **operations)
{
	Team *pTeam = con->containerData;
	if(pTeam)
	{
		S32 i;

		/////////
		// Update the stash. Anything appropriate was removed in the pre commit

		for(i = 0; i < eaSize(&pTeam->eaDisconnecteds); i++)
		{
			aslTeam_AddStashedDisconnectedTeamID(pTeam->eaDisconnecteds[i]->iEntID, pTeam->iContainerID);
		}

	}
}

// Called when a team is added. Including at startup when we claim ownership from the ObjectDB
void aslTeam_TeamAdd_CB(Container *con, Team *pTeam)
{
	if(pTeam->iContainerID)
	{
		int i;
		
		for(i = 0; i < eaSize(&pTeam->eaMembers); i++)
		{
			aslTeam_AddStashedMemberTeamID(pTeam->eaMembers[i]->iEntID, pTeam->iContainerID);
		}
		for(i = 0; i < eaSize(&pTeam->eaDisconnecteds); i++)
		{
			aslTeam_AddStashedDisconnectedTeamID(pTeam->eaDisconnecteds[i]->iEntID, pTeam->iContainerID);
		}
	}
}

void aslTeam_TeamRemove_CB(Container *con, Team *pTeam)
{
	if(pTeam->iContainerID)
	{
		int i;
		
		for(i = 0; i < eaSize(&pTeam->eaMembers); i++)
		{
			aslTeam_RemoveStashedMemberTeamID(pTeam->eaMembers[i]->iEntID);
		}
		for(i = 0; i < eaSize(&pTeam->eaDisconnecteds); i++)
		{
			aslTeam_RemoveStashedDisconnectedTeamID(pTeam->eaDisconnecteds[i]->iEntID);
		}
		
		RemoteCommand_aslQueue_TeamRemove(GLOBALTYPE_QUEUESERVER, 0, pTeam->iContainerID);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////

AUTO_STRUCT;
typedef struct GameSessionStashTableItem
{
	// The game session
	GameSession *pGameSession;

	// The name of the assigned group
	char *pchAssignedGroupName;
} GameSessionStashTableItem;

typedef struct GameSessionInfo
{
	// Stash table holding all game sessions
	StashTable stGameSessions;

	// Grouped game sessions
	GameSessionGroupInfo **ppGameSessionGroups;

	// Version number for the session groups
	U32 iGroupVersion;
} GameSessionInfo;

// Global variable to hold the list of game sessions active
static GameSessionInfo s_TeamServerGameSessionInfo;

// Initializes the global game session information
void aslTeam_InitGameSessionInfo(void)
{
	// Initialize the hash table which will hold all game sessions
	s_TeamServerGameSessionInfo.stGameSessions = stashTableCreateInt(512);

	// Enable indexing for the group list
	eaCreate(&s_TeamServerGameSessionInfo.ppGameSessionGroups);
	eaIndexedEnable(&s_TeamServerGameSessionInfo.ppGameSessionGroups, parse_GameSessionGroupInfo);
}

static GameSessionParticipant * aslTeam_GetOrCreateGameSessionParticipant(SA_PARAM_NN_VALID GameSession *pGameSession, ContainerID iEntID)
{
	GameSessionParticipant *pNewParticipant;

	FOR_EACH_IN_EARRAY_FORWARDS(pGameSession->eaParticipants, GameSessionParticipant, pParticipant)
	{
		if (pParticipant->iEntID == iEntID)
		{
			return pParticipant;
		}
	}
	FOR_EACH_END

	pNewParticipant = StructCreate(parse_GameSessionParticipant);
	pNewParticipant->iEntID = iEntID;
	eaPush(&pGameSession->eaParticipants, pNewParticipant);

	return pNewParticipant;
}

static GameSessionJoinRequest * aslTeam_GetOrCreateGameSessionJoinRequest(SA_PARAM_NN_VALID GameSession *pGameSession, ContainerID iEntID)
{
	GameSessionJoinRequest *pNewRequest;

	FOR_EACH_IN_EARRAY_FORWARDS(pGameSession->eaRequests, GameSessionJoinRequest, pRequest)
	{
		if (pRequest->iEntID == iEntID)
		{
			return pRequest;
		}
	}
	FOR_EACH_END

	pNewRequest = StructCreate(parse_GameSessionJoinRequest);
	pNewRequest->iEntID = iEntID;
	eaPush(&pGameSession->eaRequests, pNewRequest);

	return pNewRequest;
}

static void aslTeam_RemoveObsoleteGameSessionParticipants(SA_PARAM_NN_VALID GameSession *pGameSession, SA_PARAM_NN_VALID Team *pTeam)
{
	S32 i;

	// Iterate through all participants and remove the ones who are not in the team
	for (i = eaSize(&pGameSession->eaParticipants) - 1; i >= 0; i--)
	{
		bool bFound = false;

		bFound = (team_FindMemberID(pTeam,pGameSession->eaParticipants[i]->iEntID) != NULL);

		if (!bFound)
		{
			// Remove the participant from the list
			StructDestroy(parse_GameSessionParticipant, pGameSession->eaParticipants[i]);
			eaRemove(&pGameSession->eaParticipants, i);
		}
	}
}

static void aslTeam_RemoveObsoleteGameSessionJoinRequests(SA_PARAM_NN_VALID GameSession *pGameSession, SA_PARAM_NN_VALID Team *pTeam)
{
	S32 i;

	// Iterate through all requests and remove the ones which are no longer valid
	for (i = eaSize(&pGameSession->eaRequests) - 1; i >= 0; i--)
	{
		bool bFound = false;

		FOR_EACH_IN_EARRAY_FORWARDS(pTeam->eaRequests, TeamMember, pTeamMember)
		{
			if (pTeamMember->iEntID == pGameSession->eaRequests[i]->iEntID)
			{
				bFound = true;
				break;
			}
		}
		FOR_EACH_END

		if (!bFound)
		{
			// Remove the request from the list
			StructDestroy(parse_GameSessionJoinRequest, pGameSession->eaRequests[i]);
			eaRemove(&pGameSession->eaRequests, i);
		}
	}
}

static void aslTeam_SetRequestsInGameSession(GameSession *pGameSession, Team *pTeam)
{
	if (pGameSession && pTeam)
	{
		// Remove all obsolete requests
		aslTeam_RemoveObsoleteGameSessionJoinRequests(pGameSession, pTeam);

		// Populate the requests
		FOR_EACH_IN_EARRAY_FORWARDS(pTeam->eaRequests, TeamMember, pTeamMember)
		{
			GameSessionJoinRequest *pRequest = aslTeam_GetOrCreateGameSessionJoinRequest(pGameSession, pTeamMember->iEntID);

			// Compare values to prevent unnecessary memory allocations in heap
			if (pRequest->pchName == NULL || pTeamMember->pcName == NULL || strcmp(pRequest->pchName, pTeamMember->pcName))
			{
				StructFreeStringSafe((char **)&pRequest->pchName);
				pRequest->pchName = StructAllocString(pTeamMember->pcName);
			}
			if (pRequest->pchAccountHandle == NULL || pTeamMember->pcAccountHandle == NULL || strcmp(pRequest->pchAccountHandle, pTeamMember->pcAccountHandle))
			{
				StructFreeStringSafe((char **)&pRequest->pchAccountHandle);
				pRequest->pchAccountHandle = StructAllocString(pTeamMember->pcAccountHandle);
			}
		}
		FOR_EACH_END
	}
}

static void aslTeam_SetMembersInGameSession(GameSession *pGameSession, Team *pTeam)
{
	if (pGameSession && pTeam)
	{
		S32 iLeaderIndex = -1;
		S32 i = 0;
		ContainerID iLeaderId = 0;

		// Populate the leader
		if (pTeam->pLeader)
		{
			iLeaderId = pTeam->pLeader->iEntID;
		}

		// Remove all invalid participants
		aslTeam_RemoveObsoleteGameSessionParticipants(pGameSession, pTeam);

		// Populate participants
		FOR_EACH_IN_EARRAY_FORWARDS(pTeam->eaMembers, TeamMember, pTeamMember)
		{
			Entity *pTeamMemberEntity = GET_REF(pTeamMember->hEnt);
			GameSessionParticipant *pParticipant = aslTeam_GetOrCreateGameSessionParticipant(pGameSession, pTeamMember->iEntID);

			// Compare values to prevent unnecessary memory allocations in heap
			if (pParticipant->pchName == NULL || pTeamMember->pcName == NULL || strcmp(pParticipant->pchName, pTeamMember->pcName))
			{
				StructFreeStringSafe((char **)&pParticipant->pchName);
				pParticipant->pchName = StructAllocString(pTeamMember->pcName);
			}
			if (pParticipant->pchMapName == NULL || pTeamMember->pcMapName == NULL || strcmp(pParticipant->pchMapName, pTeamMember->pcMapName))
			{
				StructFreeStringSafe((char **)&pParticipant->pchMapName);
				pParticipant->pchMapName = StructAllocString(pTeamMember->pcMapName);
			}
			if (pParticipant->pchMapMsgKey == NULL || pTeamMember->pcMapMsgKey == NULL || strcmp(pParticipant->pchMapMsgKey, pTeamMember->pcMapMsgKey))
			{
				StructFreeStringSafe((char **)&pParticipant->pchMapMsgKey);
				pParticipant->pchMapMsgKey = StructAllocString(pTeamMember->pcMapMsgKey);
			}
			if (pParticipant->pchMapVars == NULL || pTeamMember->pcMapVars == NULL || strcmp(pParticipant->pchMapVars, pTeamMember->pcMapVars))
			{
				StructFreeStringSafe((char **)&pParticipant->pchMapVars);
				pParticipant->pchMapVars = StructAllocString(pTeamMember->pcMapVars);
			}
			if (pParticipant->ppchCompletedNodes == NULL && 
				pTeamMemberEntity && 
				pTeamMemberEntity->pPlayer && 
				pTeamMemberEntity->pPlayer->pProgressionInfo && 
				pTeamMemberEntity->pPlayer->pProgressionInfo->ppchCompletedNodes)
			{
				eaCopy(&pParticipant->ppchCompletedNodes, &pTeamMemberEntity->pPlayer->pProgressionInfo->ppchCompletedNodes);
			}
			pParticipant->iPartitionID = pTeamMember->uPartitionID;
			pParticipant->iMapContainerID = pTeamMember->iMapContainerID;
			pParticipant->uiMapInstanceNumber = pTeamMember->iMapInstanceNumber;
			pParticipant->bReady = pTeamMember->bLobbyReadyFlag;

			// Set the account name only once
			if (pParticipant->pchAccountName == NULL && 
				pTeamMemberEntity && pTeamMemberEntity->pPlayer && 
				pTeamMemberEntity->pPlayer->publicAccountName)
			{
				pParticipant->pchAccountName = StructAllocString(pTeamMemberEntity->pPlayer->publicAccountName);
			}
				
			if (iLeaderId == pParticipant->iEntID)
			{
				pParticipant->bLeader = true;
				iLeaderIndex = i;
			}

			i++;
		}
		FOR_EACH_END

		// Make sure the leader is the first element in the array
		if (iLeaderIndex > 0)
		{
			eaMove(&pGameSession->eaParticipants, 0, iLeaderIndex);
		}
	}
}


static bool aslTeam_IsValidGameSessionForGroup(SA_PARAM_NN_VALID GameSession *pGameSession)
{
	return pGameSession->eTeamMode == TeamMode_Open && eaSize(&pGameSession->eaParticipants) > 0 && eaSize(&pGameSession->eaParticipants) < TEAM_MAX_SIZE;
}

static void aslTeam_FillGameSessionByTeam(SA_PARAM_NN_VALID GameSession *pGameSession, SA_PARAM_NN_VALID Team *pTeam)
{
	TeamProgressionData *pCurrentTrackingData = progression_GetCurrentTeamProgress(pTeam);

	pGameSession->iVersion++;
	pGameSession->iTeamID = pTeam->iContainerID;
	pGameSession->eTeamMode = pTeam->eMode;
	pGameSession->eLootMode = pTeam->loot_mode;
	pGameSession->eLootModeItemQuality = allocAddString(pTeam->loot_mode_quality);
	estrCopy2(&pGameSession->pchStatusMessage, pTeam->pchStatusMessage);
	if (pCurrentTrackingData && IS_HANDLE_ACTIVE(pCurrentTrackingData->hNode))
	{
		COPY_HANDLE(pGameSession->destination.hNode, pCurrentTrackingData->hNode);
	}
	else
	{
		REMOVE_HANDLE(pGameSession->destination.hNode);
	}
	pGameSession->destination.iUGCProjectID = pTeam->iCurrentUGCProjectID;
	aslTeam_SetMembersInGameSession(pGameSession, pTeam);
	aslTeam_SetRequestsInGameSession(pGameSession, pTeam);
}

static bool aslTeam_RemoveSessionFromGroupInfo(SA_PARAM_NN_STR const char *pchGroup, SA_PARAM_NN_VALID GameSession *pGameSession)
{
	// Get the game session group info
	S32 iIndex = eaIndexedFindUsingString(&s_TeamServerGameSessionInfo.ppGameSessionGroups, pchGroup);

	if (iIndex >= 0)
	{
		GameSessionGroupInfo *pGroupInfo = s_TeamServerGameSessionInfo.ppGameSessionGroups[iIndex];
		S32 iNumSessions = eaiSize(&pGroupInfo->piSessions);
		S32 iSessionIndex = iNumSessions <= 0 ? -1 : (S32)eaiBFind(pGroupInfo->piSessions, pGameSession->iTeamID);

		// Make sure this party exists in the current list
		if (iSessionIndex >= 0 && iSessionIndex < iNumSessions)
		{
			// Remove the session
			eaiRemoveFast(&pGroupInfo->piSessions, iSessionIndex);

			// Sort the list
			eaiQSort(pGroupInfo->piSessions, intCmp);

			// Any parties left in this group?
			if (eaiSize(&pGroupInfo->piSessions) <= 0)
			{
				// Delete the whole group info
				eaRemove(&s_TeamServerGameSessionInfo.ppGameSessionGroups, iIndex);
			}
			else
			{
				// Increment the version number
				pGroupInfo->iVersion++;
			}

			// Increment the group version number
			s_TeamServerGameSessionInfo.iGroupVersion++;

			return true;
		}		
	}

	return false;
}

static bool aslTeam_IncrementGroupInfoVersion(SA_PARAM_NN_STR const char *pchGroup)
{
	S32 iIndex = eaIndexedFindUsingString(&s_TeamServerGameSessionInfo.ppGameSessionGroups, pchGroup);

	if (iIndex >= 0)
	{
		GameSessionGroupInfo *pGroupInfo = s_TeamServerGameSessionInfo.ppGameSessionGroups[iIndex];
		pGroupInfo->iVersion++;

		return true;
	}
	return false;
}

static bool aslTeam_AddSessionToGroupInfo(SA_PARAM_NN_STR const char *pchGroup, SA_PARAM_NN_VALID GameSession *pGameSession)
{
	// Get the game session group info
	S32 iIndex = eaIndexedFindUsingString(&s_TeamServerGameSessionInfo.ppGameSessionGroups, pchGroup);
	S32 iSessionIndex;
	S32 iNumSessions;

	GameSessionGroupInfo *pGroupInfo = NULL;

	if (iIndex >= 0)
	{
		pGroupInfo = s_TeamServerGameSessionInfo.ppGameSessionGroups[iIndex];
	}
	else
	{
		pGroupInfo = StructCreate(parse_GameSessionGroupInfo);
		pGroupInfo->iVersion = 1;
		pGroupInfo->pchName = StructAllocString(pchGroup);
		StructCopyAll(parse_GameContentNodeRef, &pGameSession->destination, &pGroupInfo->nodeRef);

		// Add to the list
		eaIndexedAdd(&s_TeamServerGameSessionInfo.ppGameSessionGroups, pGroupInfo);
	}

	iNumSessions = eaiSize(&pGroupInfo->piSessions);

	iSessionIndex = iNumSessions <= 0 ? -1 : (S32)eaiBFind(pGroupInfo->piSessions, pGameSession->iTeamID);

	if (iSessionIndex < 0 || iSessionIndex >= iNumSessions)
	{
		pGroupInfo->iVersion++;

		// Add to the session list
		eaiPush(&pGroupInfo->piSessions, pGameSession->iTeamID);

		// Sort the list
		eaiQSort(pGroupInfo->piSessions, intCmp);

		// Increment the group version number
		s_TeamServerGameSessionInfo.iGroupVersion++;

		return true;
	}

	return false;
}

static void aslTeam_UpdateGameSessionGroupInfo(SA_PARAM_NN_VALID GameSessionStashTableItem * pStashTableItem)
{
	if (pStashTableItem)
	{
		GameSession *pGameSession = pStashTableItem->pGameSession;
		bool bRemovedOldGroup = false;

		const char *pchCurrentDestination = gameSession_GetDestinationName(pGameSession);

		// Remove the association with the old group if the group has changed
		if (pStashTableItem->pchAssignedGroupName &&
			pStashTableItem->pchAssignedGroupName[0] &&
			(strcmp(pStashTableItem->pchAssignedGroupName, pchCurrentDestination) != 0 || !aslTeam_IsValidGameSessionForGroup(pGameSession)))
		{
			// Remove the party from the old group
			aslTeam_RemoveSessionFromGroupInfo(pStashTableItem->pchAssignedGroupName, pGameSession);

			StructFreeStringSafe(&pStashTableItem->pchAssignedGroupName);

			bRemovedOldGroup = true;
		}
		else if (pStashTableItem->pchAssignedGroupName&&
			pStashTableItem->pchAssignedGroupName[0])
		{
			// We still want to bump up the group info version, because session information might change
			aslTeam_IncrementGroupInfoVersion(pStashTableItem->pchAssignedGroupName);
		}

		// Add to the new group
		if ((bRemovedOldGroup || pStashTableItem->pchAssignedGroupName == NULL || pStashTableItem->pchAssignedGroupName[0] == '\0') && 
			pchCurrentDestination[0] && aslTeam_IsValidGameSessionForGroup(pGameSession))
		{
			aslTeam_AddSessionToGroupInfo(pchCurrentDestination, pGameSession);

			pStashTableItem->pchAssignedGroupName = StructAllocString(pchCurrentDestination);
		}
	}
}

static void aslTeam_AddGameSession(SA_PARAM_NN_VALID Team *pTeam)
{
	if (pTeam)
	{
		GameSession *pGameSession = StructCreate(parse_GameSession);						
		GameSessionStashTableItem *pStashTableItem = StructCreate(parse_GameSessionStashTableItem);

		// Fill the game session
		aslTeam_FillGameSessionByTeam(pGameSession, pTeam);

		// Assign the game session to the stash item
		pStashTableItem->pGameSession = pGameSession;

		// Add to the stash table
		stashIntAddPointer(s_TeamServerGameSessionInfo.stGameSessions, pGameSession->iTeamID, pStashTableItem, false);

		// Update session group info
		aslTeam_UpdateGameSessionGroupInfo(pStashTableItem);
	}	
}

static void aslTeam_UpdateGameSession(SA_PARAM_NN_VALID Team *pTeam, SA_PARAM_NN_VALID GameSessionStashTableItem * pStashTableItem)
{
	if (pTeam && pStashTableItem)
	{
		// Fill the game session
		aslTeam_FillGameSessionByTeam(pStashTableItem->pGameSession, pTeam);

		// Update session group info
		aslTeam_UpdateGameSessionGroupInfo(pStashTableItem);
	}
}

// Removes a game session from all the lists
bool aslTeam_RemoveGameSession(ContainerID iTeamID)
{
	GameSessionStashTableItem *pStashTableItem = NULL;
	if (stashIntFindPointer(s_TeamServerGameSessionInfo.stGameSessions, iTeamID, (void **)&pStashTableItem))
	{
		// Remove the party from the group info list
		if (pStashTableItem->pchAssignedGroupName)
		{
			aslTeam_RemoveSessionFromGroupInfo(pStashTableItem->pchAssignedGroupName, pStashTableItem->pGameSession);
		}
		
		// Remove the element from the stash table
		stashIntRemovePointer(s_TeamServerGameSessionInfo.stGameSessions, iTeamID, NULL);

		// Destroy the stash table element
		StructDestroy(parse_GameSessionStashTableItem, pStashTableItem);
	}
	return false;
}

// Adds a game session
void aslTeam_HandleTeamUpdatesForGameSessions(ContainerID iTeamID)
{
	Team *pTeam = iTeamID > 0 ? aslTeam_GetTeam(iTeamID) : NULL;

	if (pTeam)
	{
		GameSessionStashTableItem *pStashTableItem = NULL;
		if (stashIntFindPointer(s_TeamServerGameSessionInfo.stGameSessions, pTeam->iContainerID, (void **)&pStashTableItem))
		{
			if (team_NumPresentMembers(pTeam)>0)
			{
				// Update the game session information
				aslTeam_UpdateGameSession(pTeam, pStashTableItem);				
			}
			else
			{
				// Mark for deletion
				if (team_NumTotalMembers(pTeam) >= TEAM_MAX_SIZE)
				{
					// Do not remove immediately when the team is full. 
					// This is done so the player can join a game which is in progress via the lobby as the last team member.
					pStashTableItem->pGameSession->uiDeletionTimestamp = timeSecondsSince2000();
				}
				else
				{
					// Remove the session
					aslTeam_RemoveGameSession(iTeamID);
				}
			}
		}
		else if (team_NumPresentMembers(pTeam)>0)
		{
			// Add to the list
			aslTeam_AddGameSession(pTeam);
		}
	}
}

// Returns a game session based on the given team ID
// LOGIN2TODO - can this be removed?
GameSession * aslTeam_GetGameSessionByID(U32 uiTeamID, U32 uiCurrentVersion)
{	
	return NULL;
}

// Returns the number of sessions for the given game session group
GameContentNodePartyCountResult * aslTeam_GetGameSessionCountByGroup(const char *pchGroupName)
{
	GameContentNodeRef *pRef = gameContentNode_GetRefFromName(pchGroupName);

	if (pRef)
	{
		// Get the game session group info
		S32 iIndex = eaIndexedFindUsingString(&s_TeamServerGameSessionInfo.ppGameSessionGroups, pchGroupName);

		// Set the reference
		GameContentNodePartyCountResult *pResult = StructCreate(parse_GameContentNodePartyCountResult);
		StructCopyAll(parse_GameContentNodeRef, pRef, &pResult->nodeRef);
		StructDestroy(parse_GameContentNodeRef, pRef);

		if (iIndex >= 0)
		{
			// Set the number of sessions
			pResult->iNumSessions = ea32Size(&s_TeamServerGameSessionInfo.ppGameSessionGroups[iIndex]->piSessions);
		}

		return pResult;
	}
	else
	{
		return StructCreate(parse_GameContentNodePartyCountResult);
	}
}

// Sets the ready flag for a team member in a game session
void aslTeam_SetReadyFlagInGameSession(ContainerID uiTeamID, ContainerID uiEntID, bool bReady)
{
	Team *pTeam = aslTeam_GetTeam(uiTeamID);

	devassert(pTeam);

	if (pTeam)
	{
		// Find the correct participant
		FOR_EACH_IN_EARRAY_FORWARDS(pTeam->eaMembers, TeamMember, pTeamMember)
		{
			if (pTeamMember && pTeamMember->iEntID == uiEntID)
			{
				pTeamMember->bLobbyReadyFlag = bReady;
				aslTeam_HandleTeamUpdatesForGameSessions(uiTeamID);
				break;
			}
		}
		FOR_EACH_END
	}
}

bool aslTeam_IsEveryoneInSessionReady(SA_PARAM_NN_VALID GameSession *pGameSession)
{
	FOR_EACH_IN_EARRAY_FORWARDS(pGameSession->eaParticipants, GameSessionParticipant, pParticipant)
	{
		if (!pParticipant->bLeader && !pParticipant->bReady)
		{
			return false;
		}
	}
	FOR_EACH_END

	return true;
}


Team *aslTeam_GetTeam(ContainerID iTeamID)
{
	Container *pContainer = objGetContainer(GLOBALTYPE_TEAM, iTeamID);
	if (pContainer) {
		return (Team *)pContainer->containerData;
	} else {
		return NULL;
	}
}


void aslTeam_ValidateMembersInvitesAndRequests(Team *pTeam)
{
	Entity *pEnt;
	int i;
		
	for (i = eaSize(&pTeam->eaMembers)-1; i >= 0; i--) 
	{
		ResourceInfo *pResInfo;
		static char pcLocationTypeName[256];
		GlobalType eLocationType;
		char idBuf[128];
			
		pEnt = GET_REF(pTeam->eaMembers[i]->hEnt);
		if (!pEnt) {
			continue;
		}
		if (!pEnt->pTeam || !pEnt->pTeam->iTeamID || pEnt->pTeam->iTeamID != pTeam->iContainerID) {
			RemoteCommand_aslTeam_ValidateMember(GLOBALTYPE_TEAMSERVER, 0, pTeam->iContainerID, pEnt->myContainerID);
		}
				
		pResInfo = resGetInfo(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYPLAYER), ContainerIDToString(pEnt->myContainerID, idBuf));
		sscanf(pResInfo->resourceLocation, "%[a-zA-Z][%*d]", &pcLocationTypeName);
		eLocationType = NameToGlobalType(pcLocationTypeName);
		if (eLocationType == GLOBALTYPE_OBJECTDB)
		{
			RemoteCommand_aslTeam_ValidateOnline(GLOBALTYPE_TEAMSERVER, 0, pTeam->iContainerID, pEnt->myContainerID);
		}
	}
	for (i = eaSize(&pTeam->eaInvites)-1; i >= 0; i--) {
		pEnt = GET_REF(pTeam->eaInvites[i]->hEnt);
		if (!pEnt) {
			continue;
		}
		if (!pEnt->pTeam || !pEnt->pTeam->iTeamID || pEnt->pTeam->iTeamID != pTeam->iContainerID) {
			// Temporary code to prevent and log a weird crash, to aid in debugging. Normally, myContainerID should always resolve,
			// making this unnecessary, but for some reason this code sometimes encounters a myContainerID of 0.
			U32 iEntID = pEnt->myContainerID ? pEnt->myContainerID : StringToContainerID(REF_STRING_FROM_HANDLE(pTeam->eaInvites[i]->hEnt));
			if (pEnt->myContainerID == 0) {
				objLog(LOG_TEAM, GLOBALTYPE_TEAM, pTeam->iContainerID, 0, NULL, NULL, NULL, "Validation", NULL, "ERROR: Enity %s has ContainerID of %d.",
					pEnt->debugName, pEnt->myContainerID);
			}
			
			RemoteCommand_aslTeam_ValidateInvite(GLOBALTYPE_TEAMSERVER, 0, pTeam->iContainerID, iEntID);
		}
	}
	for (i = eaSize(&pTeam->eaRequests)-1; i >= 0; i--) {
		pEnt = GET_REF(pTeam->eaRequests[i]->hEnt);
		if (!pEnt) {
			continue;
		}
		if (!pEnt->pTeam || !pEnt->pTeam->iTeamID || pEnt->pTeam->iTeamID != pTeam->iContainerID) {
			RemoteCommand_aslTeam_ValidateRequest(GLOBALTYPE_TEAMSERVER, 0, pTeam->iContainerID, pEnt->myContainerID);
		}
	}
}


#define TEAM_LOCAL_TIMEOUT 120
#define TEAM_LOCAL_REQUEST_TIME 20

// We are associated with a LocalTeam on a game server. This team exists as long as that LocalTeam does.
//  Ping the GameServer the local team is on. Destroy this Team if we time out.
// Note we allow MAX_TEAM_DESTROY_PER_TICK teams to be destroyed if just this type in addition to
//  that many for regular destroys
static void aslTeam_ValidateForGameServerLocalTeam(Team *pTeam)
{
	U32 currentTime = timeSecondsSince2000();
	int teamsDeletedThisTick = 0;
	
	if(!pTeam->iOwnedLastRequest) {
		//Set the current time, then request
		pTeam->iOwnedLastUpdate = pTeam->iOwnedLastRequest = currentTime;
	} else {
		if(currentTime - pTeam->iOwnedLastUpdate > TEAM_LOCAL_TIMEOUT) {
			if (teamsDeletedThisTick < MAX_TEAM_DESTROY_PER_TICK) {
				RemoteCommand_aslTeam_DestroyWithReason(GLOBALTYPE_TEAMSERVER, 0, pTeam->iContainerID, "Local team timeout");
				teamsDeletedThisTick++;
			}
		} else if (currentTime - pTeam->iOwnedLastRequest >= TEAM_LOCAL_REQUEST_TIME) {
			RemoteCommand_gslTeam_FindOwnedTeam(GLOBALTYPE_GAMESERVER, pTeam->iGameServerOwnerID, pTeam->iContainerID);
			pTeam->iOwnedLastRequest = currentTime;
		}
	}
}


// tickCount can be from 0-7
#define TICK_COUNT_MASK 0x07

static void aslValidateTeams(void)
{
	ContainerIterator iter;
	Team *pTeam;
	U32 currentTime = timeSecondsSince2000();
	int teamsDeletedThisTick = 0;
	static int tickCount = 0;

	tickCount = ( tickCount + 1 ) & TICK_COUNT_MASK;
	
	printf("Creations in last %d seconds: %d\n", TEAM_CLEANUP_INTERVAL, timingHistoryInLastInterval(gCreationHistory, 30.0f));
	printf("Actions in last %d seconds: %d\n", TEAM_CLEANUP_INTERVAL, timingHistoryInLastInterval(gActionHistory, 30.0f));
	loadstart_printf("Beginning aslValidateTeams... ");
	objInitContainerIteratorFromType(GLOBALTYPE_TEAM, &iter);
	while (pTeam = objGetNextObjectFromIterator(&iter))
	{
		int i;
		U32 timeSinceCreate;
		int iNumPresentMembers = team_NumPresentMembers(pTeam);		// Members who are logged in and full entities
		int iNumTotalMembers = team_NumTotalMembers(pTeam);			// Both connected and disconnected members

		// Intentionally running this more often than the periodic team tick

		if(pTeam->iGameServerOwnerID)
		{
			// iGameServerOwnerID indicates we are associated with a GameServerLocalTeam. 
			aslTeam_ValidateForGameServerLocalTeam(pTeam);
		}

		timeSinceCreate = currentTime - pTeam->iCreatedOn;

		if ( ( pTeam->iContainerID & TICK_COUNT_MASK ) != (ContainerID)tickCount)
		{
			continue;
		}

		if(pTeam->iGameServerOwnerID)
		{
			aslTeam_ValidateMembersInvitesAndRequests(pTeam);
		}
		else if (pTeam->iVersion < 1 )
		{
			// special case code to deal with team containers that have been created but not yet initialized
			// do nothing if the container is not old enough or we have deleted too many already this tick
			if ( (timeSinceCreate > TEAM_MAX_INIT_TIME) && (teamsDeletedThisTick < MAX_TEAM_DESTROY_PER_TICK) ) {
				// The container has been around too long without being initialized, so delete it.
				RemoteCommand_aslTeam_DestroyWithReason(GLOBALTYPE_TEAMSERVER, 0, pTeam->iContainerID, "Team initialization timeout");
				teamsDeletedThisTick++;
			}
		}
		
		else if (iNumPresentMembers < 1 && pTeam->iGameServerOwnerID==0)
		{
			// There are no present members. We are only disconnecteds if anything. Goodbye.
			// Make sure we check if we are an owned team because we don't want to be destroyed until the server goes away
			// throttle number of deletes per tick
			if (teamsDeletedThisTick < MAX_TEAM_DESTROY_PER_TICK) {
				RemoteCommand_aslTeam_DestroyWithReason(GLOBALTYPE_TEAMSERVER, 0, pTeam->iContainerID, "Team had no members");
				teamsDeletedThisTick++;
			}
		}

		else if (pTeam->iGameServerOwnerID==0 && // We are associated with a GameServer/Local Team, don't do the normal deletion checks.
												// We would like the team to stay around in case someone gets added to the local team
				 
				 iNumTotalMembers==1 &&		// This will do the right thing for both manageddisconnects and not. FOr managed, wait 'til all disconnecteds are removed as well.
				 (gConf.bManageTeamDisconnecteds || timeSecondsSince2000() > pTeam->uBadLogoutTime + TEAM_BAD_LOGOUT_SECONDS) && // Don't disband if there was a bad logout
																														         // when disconnecteds are not managed
				 (eaSize(&pTeam->eaInvites) == 0))
		{
			// Team has only one member (and no disconnecteds if we are managing them). Remove that member, and decline any invites or requests.
			// If there is a bad logout wait two minutes before disbanding one man teams if disconnecteds are not managed
			//  trg_RemoveMember in TeamTransactions will delete the team itself as it becomes empty.
			RemoteCommand_aslTeam_Leave(GLOBALTYPE_TEAMSERVER, 0, pTeam->iContainerID, pTeam->eaMembers[0]->iEntID, pTeam->eaMembers[0]->iEntID, false, NULL, NULL, NULL);
// This code now doesn't do anything since we require that we have no Invites pending.
//   We used to need to do this because we considered it okay to disband if anyone had ever been accepted to the team. WOLF[6Dec2012]
//			for (i = eaSize(&pTeam->eaInvites)-1; i >= 0; i--) {
//				RemoteCommand_aslTeam_DeclineInvite(GLOBALTYPE_TEAMSERVER, 0, pTeam->iContainerID, pTeam->eaInvites[i]->iEntID, NULL, NULL, NULL);
//			}
			for (i = eaSize(&pTeam->eaRequests)-1; i >= 0; i--) {
				RemoteCommand_aslTeam_CancelRequest(GLOBALTYPE_TEAMSERVER, 0, pTeam->iContainerID, pTeam->eaRequests[i]->iEntID, NULL, NULL, NULL);
			}
		}
		else
		{
			aslTeam_ValidateMembersInvitesAndRequests(pTeam);
		}
	}
	objClearContainerIterator(&iter);
	loadend_printf("...done.");
}

static bool sAcquireTeamContainersComplete = false;

void
AcquireTeamContainersComplete_CB(void)
{
	sAcquireTeamContainersComplete = true;	
}

static void aslTeam_UpdateGameSessionTickHandler(GameSession *pGameSession)
{
	bool bUpdated = false;

	if (eaSize(&pGameSession->eaParticipants) > 0)
	{
		Team *pTeam = aslTeam_GetTeam(pGameSession->iTeamID);

		if (pTeam)
		{
			TeamProgressionData *pCurrentTrackingData = progression_GetCurrentTeamProgress(pTeam);
			GameProgressionNodeDef *pNode = pCurrentTrackingData ? GET_REF(pCurrentTrackingData->hNode) : NULL;
			GameProgressionNodeDef *pCurrentNode = GET_REF(pGameSession->destination.hNode);

			if (pCurrentNode != pNode)
			{
				if (pNode)
				{
					SET_HANDLE_FROM_REFERENT(g_hGameProgressionNodeDictionary, pNode, pGameSession->destination.hNode);
				}
				else
				{
					REMOVE_HANDLE(pGameSession->destination.hNode);
				}
				bUpdated = true;
			}			

			// Game session only looks at Present Members
			FOR_EACH_IN_EARRAY_FORWARDS(pTeam->eaMembers, TeamMember, pTeamMember)
			{
				Entity *pTeamMemberEntity = pTeamMember ? GET_REF(pTeamMember->hEnt) : NULL;
				if (pTeamMemberEntity && pTeamMemberEntity->pPlayer)
				{
					// Find the corresponding entity in the participant list
					FOR_EACH_IN_EARRAY_FORWARDS(pGameSession->eaParticipants, GameSessionParticipant, pParticipant)
					{
						if (pParticipant && pParticipant->iEntID == pTeamMemberEntity->myContainerID)
						{
							S32 iCharLevel = entity_GetSavedExpLevel(pTeamMemberEntity);

							// Update the public account name if necessary
							if (pParticipant->pchAccountName &&
								(pTeamMemberEntity->pPlayer->publicAccountName == NULL || stricmp(pTeamMemberEntity->pPlayer->publicAccountName, pParticipant->pchAccountName) != 0))
							{
								StructFreeString(pParticipant->pchAccountName);
								pParticipant->pchAccountName = StructAllocString(pTeamMemberEntity->pPlayer->publicAccountName);
								bUpdated = true;
							}
							else if (pParticipant->pchAccountName == NULL)
							{
								pParticipant->pchAccountName = StructAllocString(pTeamMemberEntity->pPlayer->publicAccountName);
								bUpdated = true;
							}

							if (pTeamMemberEntity->pChar)
							{
								// Update the class, species and the character path
								if (!REF_COMPARE_HANDLES(pParticipant->hClass, pTeamMemberEntity->pChar->hClass))
								{
									COPY_HANDLE(pParticipant->hClass, pTeamMemberEntity->pChar->hClass);
									bUpdated = true;
								}
								if (!REF_COMPARE_HANDLES(pParticipant->hPath, pTeamMemberEntity->pChar->hPath))
								{
									COPY_HANDLE(pParticipant->hPath, pTeamMemberEntity->pChar->hPath);
									bUpdated = true;
								}
								if (eaSize(&pParticipant->ppSecondaryPaths) != eaSize(&pTeamMemberEntity->pChar->ppSecondaryPaths))
								{
									eaCopyStructs(&pParticipant->ppSecondaryPaths, &pTeamMemberEntity->pChar->ppSecondaryPaths, parse_AdditionalCharacterPath);
									bUpdated = true;
								}
								else
								{
									int i;
									for (i = 0; i < eaSize(&pParticipant->ppSecondaryPaths); i++)
									{
										if (REF_COMPARE_HANDLES(pParticipant->ppSecondaryPaths[i]->hPath, pTeamMemberEntity->pChar->ppSecondaryPaths[i]->hPath) != 0)
										{
											eaCopyStructs(&pParticipant->ppSecondaryPaths, &pTeamMemberEntity->pChar->ppSecondaryPaths, parse_AdditionalCharacterPath);
											bUpdated = true;
										}
									}
								}
								if (!REF_COMPARE_HANDLES(pParticipant->hSpecies, pTeamMemberEntity->pChar->hSpecies))
								{
									COPY_HANDLE(pParticipant->hSpecies, pTeamMemberEntity->pChar->hSpecies);
									bUpdated = true;
								}
							}				
							// Get the XP level
							if (iCharLevel != pParticipant->iExpLevel)
							{
								pParticipant->iExpLevel = entity_GetSavedExpLevel(pTeamMemberEntity);
								bUpdated = true;
							}							

							if (pParticipant->pCostume == NULL && pTeamMemberEntity->pSaved && 
								eaSize(&pTeamMemberEntity->pSaved->costumeData.eaCostumeSlots) > 0 &&
								pTeamMemberEntity->pSaved->costumeData.eaCostumeSlots[0]->pCostume)
							{									
								// Copy the costume
								pParticipant->pCostume = StructClone(parse_PlayerCostume, pTeamMemberEntity->pSaved->costumeData.eaCostumeSlots[0]->pCostume);
								bUpdated = true;
							}

							if (pParticipant->ppchCompletedNodes == NULL && 
								pTeamMemberEntity->pPlayer->pProgressionInfo && 
								pTeamMemberEntity->pPlayer->pProgressionInfo->ppchCompletedNodes)
							{
								eaCopy(&pParticipant->ppchCompletedNodes, &pTeamMemberEntity->pPlayer->pProgressionInfo->ppchCompletedNodes);
							}

							break;
						}
					}
					FOR_EACH_END
				}
			}
			FOR_EACH_END
		}
	}

	if (bUpdated)
	{
		aslTeam_HandleTeamUpdatesForGameSessions(pGameSession->iTeamID);
	}
}

static int aslTeam_ForEachTeam(StashElement element)
{
	GameSessionStashTableItem *pStashTableItem = stashElementGetPointer(element);

	if (pStashTableItem)
	{
		if (pStashTableItem->pGameSession->uiDeletionTimestamp && (timeSecondsSince2000() - pStashTableItem->pGameSession->uiDeletionTimestamp) > 30)
		{
			aslTeam_RemoveGameSession(pStashTableItem->pGameSession->iTeamID);
		}
		else
		{
			aslTeam_UpdateGameSessionTickHandler(pStashTableItem->pGameSession);
		}
	}

	return true;
}

static void aslTeam_UpdateGameSessionsTick(void)
{
	stashForEachElement(s_TeamServerGameSessionInfo.stGameSessions, aslTeam_ForEachTeam);
}

int TeamServerLibOncePerFrame(F32 fElapsed)
{
	static U32 iLastDeleteTime = 0;
	static U32 iLastGameSessionUpdateTime = 0;
	static bool bOnce = false;
	
	if(!bOnce) {
		aslAcquireContainerOwnership(GLOBALTYPE_TEAM, AcquireTeamContainersComplete_CB);
		RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), gServerLibState.containerID, "ready");
		ATR_DoLateInitialization();

		gCreationHistory = timingHistoryCreate(10000);
		gActionHistory = timingHistoryCreate(10000);
		bOnce = true;
	}
	
	if (sAcquireTeamContainersComplete)
	{
		if (timeSecondsSince2000() - iLastDeleteTime >= TEAM_CLEANUP_INTERVAL)
		{
			aslValidateTeams();
			iLastDeleteTime = timeSecondsSince2000();
		}
	}
	return 1;
}

// Returns the game sessions in the given group
void aslTeam_GetGameSessions(SA_PARAM_NN_VALID GameSessionQuery *pQuery, SA_PARAM_NN_VALID GameSessionQueryResult *pResult)
{
	// Get the game session group info
	S32 iIndex = eaIndexedFindUsingString(&s_TeamServerGameSessionInfo.ppGameSessionGroups, pQuery->pchGroupName);

	if (iIndex >= 0)
	{
		GameSessionGroupInfo *pGroupInfo = s_TeamServerGameSessionInfo.ppGameSessionGroups[iIndex];

		StructCopyAll(parse_GameContentNodeRef, &pGroupInfo->nodeRef, &pResult->nodeRef);

		if (pResult->iVersion == 0 || pGroupInfo->iVersion != pResult->iVersion) // Sending in a version number of 0 means that they want to get the data regardless of current version
		{
			S32 i;
			pResult->iVersion = pGroupInfo->iVersion;
			eaIndexedEnable(&pResult->ppGameSessions, parse_GameSession);

			// Add sessions which friends participate in first
			for (i = 0; i < eaiSize(&pQuery->piTeamIDs); i++)
			{
				bool bFound = false;
				S32 iNumParties = eaiSize(&pGroupInfo->piSessions);

				if (eaSize(&pResult->ppGameSessions) >= pQuery->iNumRecords)
				{
					return;
				}

				// See if this team exists in the current group
				if (iNumParties > 0)
				{
					S32 iFoundIndex = (S32)eaiBFind(pGroupInfo->piSessions, pQuery->piTeamIDs[i]);
					if (iFoundIndex >= 0 && iFoundIndex < iNumParties)
					{
						bFound = true;
					}
				}

				if (bFound)
				{
					GameSessionStashTableItem *pStashTableItem = NULL;

					if (stashIntFindPointer(s_TeamServerGameSessionInfo.stGameSessions, 
						pQuery->piTeamIDs[i],
						(void **)&pStashTableItem))
					{
						eaIndexedAdd(&pResult->ppGameSessions, StructClone(parse_GameSession, pStashTableItem->pGameSession));
					}
				}
			}

			if (pQuery->bFriendsOnly)
			{
				return;
			}

			// Add all game sessions to the list up to the number requested
			for (i = 0; i < eaiSize(&pGroupInfo->piSessions); i++)
			{
				GameSessionStashTableItem *pStashTableItem = NULL;
				S32 iAdded = eaSize(&pResult->ppGameSessions);

				if (iAdded >= pQuery->iNumRecords)
				{
					return;
				}

				// Before we add make sure the team ID is not already added
				if (eaIndexedFindUsingInt(&pResult->ppGameSessions, pGroupInfo->piSessions[i]) >= 0)
				{
					continue;
				}
				
				if (stashIntFindPointer(s_TeamServerGameSessionInfo.stGameSessions, 
					pGroupInfo->piSessions[i],
					(void **)&pStashTableItem))
				{
					eaIndexedAdd(&pResult->ppGameSessions, StructClone(parse_GameSession, pStashTableItem->pGameSession));
				}
			}

			return;
		}
	}
	else
	{
		GameContentNodeRef *pRef = gameContentNode_GetRefFromName(pQuery->pchGroupName);

		devassert(pRef);

		if (pRef)
		{
			StructCopyAll(parse_GameContentNodeRef, pRef, &pResult->nodeRef);
			pResult->iVersion = 1; // Version 1 dedicated for empty groups
			
			return;
		}
	}
	

	pResult->iVersion = 0;
}
#include "AutoGen/aslTeamServer_c_ast.c"

/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslQueue.h"
#include "aslQueue_h_ast.h"

#include "AutoTransDefs.h"
#include "Entity.h"
#include "logging.h"
#include "objTransactions.h"
#include "queue_common.h"
#include "queue_common_structs.h"
#include "StringCache.h"
#include "StringUtil.h"

#include "Entity_h_ast.h"
#include "queue_common_h_ast.h"
#include "queue_common_structs_h_ast.h"

#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/AppServerLib_autotransactions_autogen_wrappers.h"
#include "autogen/AppServerLib_autogen_remotefuncs.h"

#define QUEUE_NOT_FOUND_MESG "QueueServer_QueueNotFound"
#define QUEUE_JOINED_MESG "QueueServer_JoinedQueue"
#define QUEUE_CREATED_MESG "QueueServer_CreatedQueue"
#define QUEUE_CREATE_FAILED_MESG "QueueServer_QueueCreateFailed"
#define QUEUE_INVITED_MESG "QueueServer_InvitedQueue"
#define QUEUE_INVITE_FAILED_MESG "QueueServer_QueueInviteFailed"
#define QUEUE_INVITEE_ACTIVE_MESG "QueueServer_InviteeAlreadyActive"
#define QUEUE_KICKED_MESG "QueueServer_KickedQueue"
#define QUEUE_KICK_FAILED_MESG "QueueServer_QueueKickFailed"
#define QUEUE_CHANGED_GROUP_MESG "QueueServer_ChangedGroup"
#define QUEUE_CHANGE_GROUP_FAILED_MESG "QueueServer_ChangeGroupFailed"
#define QUEUE_PLAYERS_NOT_READY_MESG "QueueServer_PlayersNotReady"
#define QUEUE_PLAYERS_NOT_ENOUGH_PLAYERS_MESG "QueueServer_NotEnoughPlayers"
#define QUEUE_PLAYER_IN_MAX_COUNT_MESG "QueueServer_PlayerInMaxQueueCount"
#define QUEUE_ALREADY_IN_QUEUE_MESG "QueueServer_AlreadyInQueue"
#define QUEUE_ALREADY_IN_THIS_QUEUE_MESG "QueueServer_AlreadyInThisQueue"
#define QUEUE_TEAM_ALREADY_IN_QUEUE_MESG "QueueServer_Team_AlreadyInQueue"
#define QUEUE_TEAM_TOO_LARGE_MESG "QueueServer_Team_TooLarge"
#define QUEUE_BAD_PASSWORD_MESG "QueueServer_BadPassword"
#define QUEUE_NOT_INVITED_MESG "QueueServer_NotInvited"
#define QUEUE_NOT_MEMBER_MESG "QueueServer_NotAMember"
#define QUEUE_NOT_OFFERED_MESG "QueueServer_NotOffered"
#define QUEUE_INVALID_GROUP_MESG "QueueServer_InvalidGroup"
#define QUEUE_LEFT_MESG "QueueServer_LeftQueue"
#define QUEUE_LEFT_FORCED_MESG "QueueServer_LeftQueueForced"
#define QUEUE_DELAYED_MESG "QueueServer_Delayed"
#define QUEUE_RESUMED_MESG "QueueServer_Resumed"
#define QUEUE_MAP_ACTIVE_MESG "QueueServer_MapActive"
#define QUEUE_MAP_CHANGED_MESG "QueueServer_ChangedMap"
#define QUEUE_MAP_CHANGE_FAILED_MESG "QueueServer_MapChangeFailed"
#define QUEUE_MAP_SETTING_CHANGED_MESG "QueueServer_MapSettingChanged"
#define QUEUE_MAP_SETTING_CHANGE_FAILED_MESG "QueueServer_MapSettingChangeFailed"
#define QUEUE_CHANGE_SETTING_MAP_ERROR_MESG "QueueServer_ChangeSettingMapError"
#define QUEUE_MAP_PASSWORD_CHANGED_MESG "QueueServer_PasswordChanged"
#define QUEUE_MAP_PASSWORD_CHANGE_FAILED_MESG "QueueServer_PasswordChangeFailed"
#define QUEUE_PLAYER_PENALIZED_MESG "QueueServer_PlayerPenalized"
#define QUEUE_MIGRATE_PRIVATE_OWNER_MESG "QueueServer_MigratePrivateOwner"
#define QUEUE_PLAYER_JOINED_PRIVATE_GAME_MESG "QueueServer_PlayerJoinedPrivateGame"
#define QUEUE_PLAYER_LEFT_PRIVATE_GAME_MESG "QueueServer_PlayerLeftPrivateGame"
#define QUEUE_PASSWORD_INVALID_MESG "QueueServer_PasswordInvalid"
#define QUEUE_CANCEL_LAUNCH_STRING "CancelLaunch"
#define QUEUE_MIGRATE_PRIVATE_OWNER "MigratePrivateOwner"
#define QUEUE_NOT_TEAMLEADER_MESG "QueueServer_Team_NotTeamLeader"
#define QUEUE_PRIVATE_NAME_IN_USE "QueueServer_PrivateGameNameInUse"
#define QUEUE_JOINED_ACTIVE_MAP_MESG "QueueServer_JoinedActiveMap"
#define QUEUE_TEAM_MEMBER_DECLINED_MESG "QueueServer_TeamMemberDeclined"
#define QUEUE_CHANGE_SETTING_LEVEL_ERROR_MESG "QueueServer_ChangeSettingLevelError"

typedef struct PlayerTeamMember PlayerTeamMember;

static QueueTeamJoinTime **s_eaTeamJoinTimes = NULL;
static QueuePenaltyData** s_eaPenaltyData = NULL;
static S32* s_piInviteDisallowStates = NULL;

static void aslQueue_InitInviteDisallowStates(void)
{
	if (!s_piInviteDisallowStates)
	{
		ea32Push(&s_piInviteDisallowStates, PlayerQueueState_Invited);
		ea32Push(&s_piInviteDisallowStates, PlayerQueueState_Accepted);
		ea32Push(&s_piInviteDisallowStates, PlayerQueueState_Countdown);
		ea32Push(&s_piInviteDisallowStates, PlayerQueueState_InMap);
	}
}

static QueueMap* aslQueue_FindMap(QueueInfo* pQueueContainer, S64 iMapKey, QueueInstance** ppInstance)
{
	S32 iInstanceIdx;
	for (iInstanceIdx = eaSize(&pQueueContainer->eaInstances)-1; iInstanceIdx >= 0; iInstanceIdx--)
	{
		QueueInstance* pInstance = pQueueContainer->eaInstances[iInstanceIdx];
		QueueMap* pMap = eaIndexedGetUsingInt(&pInstance->eaMaps, iMapKey);
		if (pMap)
		{
			if (ppInstance)
			{
				(*ppInstance) = pInstance;
			}
			return pMap;
		}
	}
	return NULL;
}

static void aslQueue_CleanupPreventMapLaunch(QueueCBStruct *pCBData)
{
	if (pCBData->bPreventMapLaunch)
	{
		QueueInfo *pLocalQueue = aslQueue_GetQueueLocal(pCBData->iQueueID);
		if (pLocalQueue)
		{
			QueueInstance* pInstance = eaIndexedGetUsingInt(&pLocalQueue->eaInstances, pCBData->iInstanceID);
			if (pInstance && pInstance->uiPlayersPreventingMapLaunch > 0)
			{
				pInstance->uiPlayersPreventingMapLaunch--;
			}
		}
	}
}

// Returns true if there are any maps which are in a state to accept new members.
// This includes maps that are already full.
static bool aslQueue_SomeMapCouldAcceptMembers(QueueInstance* pInstance, QueueDef *pQueueDef)
{
	S32 iMapIdx;

	for (iMapIdx = eaSize(&pInstance->eaMaps)-1; iMapIdx >= 0; iMapIdx--)
	{
		QueueMap *pMap = pInstance->eaMaps[iMapIdx];

		if (queue_MapAcceptingNewMembers(pMap, pQueueDef))
		{
			return(true);
		}
	}
	return(false);
}


static void aslQueue_ForcedLeave_CB(TransactionReturnVal *pVal, void *pData)
{
	QueueCBStruct *pCBData = (QueueCBStruct*)pData;
	QueueInfo *pQueue = aslQueue_GetQueueContainer(pCBData->iQueueID);
	
	if(pVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		log_printf(LOG_QUEUE, "EntID [%d] left the [%s] queue.\n",
			pCBData->iEntID,
			pQueue->pchName);
		if(pCBData->iMapKey)
		{
			U32 uMapID = queue_GetMapIDFromMapKey(pCBData->iMapKey);
			U32 uPartitionID = queue_GetPartitionIDFromMapKey(pCBData->iMapKey);
			RemoteCommand_gslQueue_RemoveMemberFromMatch(GLOBALTYPE_GAMESERVER, uMapID, uPartitionID, pCBData->iEntID);
		}
		queue_SendResultMessage(pCBData->iEntID, 0, pQueue->pchName, QUEUE_LEFT_FORCED_MESG);
	}
	aslQueue_CleanupPreventMapLaunch(pCBData);
	StructDestroy(parse_QueueCBStruct, pCBData);
}

//////////////////////////////////////////////////////////////////
//	Pending Local Team Timeout
//////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void aslQueue_HandlePendingLocalTeamTimeout(const char* pchQueueName, S64 iMapKey, ContainerID uEntID)
{
	QueueDef* pDef = queue_DefFromName(pchQueueName);
	QueueInfo* pQueue = aslQueue_FindQueueByNameLocal(pchQueueName);
	if (pDef && pQueue)
	{
		QueueInstance* pInstance = NULL;
		QueueMap* pMap = aslQueue_FindMap(pQueue, iMapKey, &pInstance);
		QueueMember* pMember = queue_FindPlayerInInstance(pInstance, uEntID);

		// Force the player to leave the queue
		if (pMember)
		{
			QueueCBStruct *pCBData = StructCreate(parse_QueueCBStruct);
			TransactionReturnVal *pReturn = objCreateManagedReturnVal(aslQueue_ForcedLeave_CB, pCBData);

			pCBData->iEntID = uEntID;
			pCBData->iMapKey = iMapKey;
			pCBData->iQueueID = pQueue->iContainerID;
			AutoTrans_aslQueue_tr_QueueLeave(pReturn, GetAppGlobalType(), GLOBALTYPE_QUEUEINFO, pQueue->iContainerID, uEntID, pInstance->uiID, true);
		}
	}
}

//////////////////////////////////////////////////////////////////
//	Leaver Penalty Handling
//////////////////////////////////////////////////////////////////

static QueuePenaltyData* aslQueue_GetPlayerPenaltyData(U32 uEntID, S32 eQueueCategory, QueuePenaltyCategoryData*** peaCategoryData)
{
	S32 iPenaltyIdx = eaIndexedFindUsingInt(&s_eaPenaltyData, uEntID);
	if (iPenaltyIdx >= 0)
	{
		int i;
		U32 uCurrentTime = timeSecondsSince2000();
		QueuePenaltyData* pPenalty = s_eaPenaltyData[iPenaltyIdx];
		
		for (i = eaSize(&pPenalty->eaCategoryData)-1; i >= 0; i--)
		{
			QueuePenaltyCategoryData* pCategory = pPenalty->eaCategoryData[i];

			if (uCurrentTime >= pCategory->uPenaltyEndTime)
			{
				StructDestroy(parse_QueuePenaltyCategoryData, eaRemove(&pPenalty->eaCategoryData, i));
			}
			else if (eQueueCategory < 0)
			{
				if (peaCategoryData)
				{
					if (!(*peaCategoryData))
						eaIndexedEnable(peaCategoryData, parse_QueuePenaltyCategoryData);
					eaPush(peaCategoryData, pCategory);
				}
			}
			else if (pPenalty->eaCategoryData[i]->eCategory == eQueueCategory)
			{
				if (peaCategoryData)
				{
					if (!(*peaCategoryData))
						eaIndexedEnable(peaCategoryData, parse_QueuePenaltyCategoryData);
					eaPush(peaCategoryData, pCategory);
				}
			}
		}
		if (eaSize(&pPenalty->eaCategoryData))
		{
			return pPenalty;
		}
		else
		{
			StructDestroy(parse_QueuePenaltyData, eaRemove(&s_eaPenaltyData, iPenaltyIdx));
		}
	}
	return NULL;
}

void aslQueue_PenalizePlayerByID(U32 uEntID, QueueDef* pDef)
{
	if (pDef && uEntID)
	{
		QueuePenaltyCategoryData** eaPenaltyCategories = NULL;
		QueuePenaltyData* pPenaltyData = aslQueue_GetPlayerPenaltyData(uEntID, pDef->eCategory, &eaPenaltyCategories);
		QueuePenaltyCategoryData* pPenaltyCategory = eaGet(&eaPenaltyCategories, 0);

		if (!pPenaltyData)
		{
			pPenaltyData = StructCreate(parse_QueuePenaltyData);
			pPenaltyData->uEntID = uEntID;
			if (!s_eaPenaltyData)
				eaIndexedEnable(&s_eaPenaltyData, parse_QueuePenaltyData);
			eaPush(&s_eaPenaltyData, pPenaltyData);
		}
		if (!pPenaltyCategory)
		{
			pPenaltyCategory = StructCreate(parse_QueuePenaltyCategoryData);
			pPenaltyCategory->eCategory = pDef->eCategory;
			eaPush(&pPenaltyData->eaCategoryData, pPenaltyCategory);
		}
		pPenaltyCategory->uPenaltyEndTime = timeSecondsSince2000() + queue_GetLeaverPenaltyDuration(pDef);
		eaDestroy(&eaPenaltyCategories);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void aslQueue_RemoveAllPenalties(U32 uEntID)
{
	S32 iPenaltyIdx = eaIndexedFindUsingInt(&s_eaPenaltyData, uEntID);
	if (iPenaltyIdx >= 0)
	{
		QueuePenaltyData* pPenaltyData = eaRemove(&s_eaPenaltyData, iPenaltyIdx);
		StructDestroy(parse_QueuePenaltyData, pPenaltyData);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
U32
aslQueue_GetInstanceOwnerID(const char* pchQueueName, S64 iMapKey)
{
    QueueInfo* pQueue = aslQueue_FindQueueByNameLocal(pchQueueName);
    ContainerID ownerID = 0;

    if (pQueue)
    {
        QueueInstance* pInstance = NULL;
        QueueMap* pMap = aslQueue_FindMap(pQueue, iMapKey, &pInstance);

        if ( pInstance && pInstance->pParams )
        {
            ownerID = pInstance->pParams->uiOwnerID;
        }
    }

    return ownerID;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void aslQueue_HandleLeaveMap(const char* pchQueueName, S64 iMapKey, ContainerID uEntID, bool bGameIsFinished, bool bLeaverPenaltyEnabled)
{
	QueueDef* pDef = queue_DefFromName(pchQueueName);
	QueueInfo* pQueue = aslQueue_FindQueueByNameLocal(pchQueueName);
	if (pDef && pQueue)
	{
		QueueInstance* pInstance = NULL;
		QueueMap* pMap = aslQueue_FindMap(pQueue, iMapKey, &pInstance);
		QueueMember* pMember = queue_FindPlayerInInstance(pInstance, uEntID);

		if (pMember)
		{
			bool bPenalty = false;

			// Issue a leaver penalty if this queue has the leaver penalty enabled and the game isn't finished
			if (!bGameIsFinished && bLeaverPenaltyEnabled)
			{
				aslQueue_PenalizePlayerByID(uEntID, pDef);
				bPenalty = true;
			}
				
			if (!bPenalty && SAFE_MEMBER(pInstance->pParams, uiOwnerID) && g_QueueConfig.bRemainInPrivateQueuesAfterMapFinishes)
			{
				QMemberUpdateList* pList = StructCreate(parse_QMemberUpdateList);

				// Place the member back into the InQueue state
				aslQueue_AddMemberStateChangeEx(&pList->eaUpdates, pInstance, pMember, PlayerQueueState_InQueue, 0, false, 0, false, 0, true);

				AutoTrans_aslQueue_tr_MembersStateUpdate(NULL, 
														 GLOBALTYPE_QUEUESERVER, 
														 GLOBALTYPE_QUEUEINFO, 
														 pQueue->iContainerID, 
														 pList);
				StructDestroySafe(parse_QMemberUpdateList, &pList);
			}
			else
			{
				//Remove the player from all queues
				aslQueue_LeaveAll(uEntID,true);
			}
		}
	}
}


AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void aslQueue_PenalizePlayer(ContainerID uEntID, const char* pchQueueName)
{
	QueueDef* pDef = queue_DefFromName(pchQueueName);
	if (pDef)
	{
		aslQueue_PenalizePlayerByID(uEntID, pDef);
	}
}


///////////////////////////////////////////////////////////////////////////////////////////
// Helper functions
///////////////////////////////////////////////////////////////////////////////////////////

// Each private game name must be unique to the queue, if specified
AUTO_TRANS_HELPER
ATR_LOCKS(pQueue, ".Eainstances");
S32 aslQueue_trh_IsPrivateNameUnique(ATH_ARG NOCONST(QueueInfo)* pQueue, const char* pchPrivateName)
{
	if (pchPrivateName && pchPrivateName[0])
	{
		S32 iInstanceIdx;
		for (iInstanceIdx = eaSize(&pQueue->eaInstances)-1; iInstanceIdx >= 0; iInstanceIdx--)
		{
			NOCONST(QueueInstance)* pInstance = pQueue->eaInstances[iInstanceIdx];
			if (NONNULL(pInstance->pParams) && stricmp(pInstance->pParams->pchPrivateName, pchPrivateName)==0)
			{
				return false;
			}
		}
	}
	return true;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pMember, ".Estate");
S32 aslQueue_trh_MemberActivated(ATH_ARG NOCONST(QueueMember) *pMember)
{
	if (NONNULL(pMember))
	{
		switch (pMember->eState)
		{
			case PlayerQueueState_Offered:
			case PlayerQueueState_Accepted:
			case PlayerQueueState_Countdown:
			case PlayerQueueState_InMap:
			{
				return true;
			}
		}
	}
	return false;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pMember, ".Estate");
bool aslQueue_trh_StateChange_CheckValid(ATH_ARG NOCONST(QueueMember) *pMember, PlayerQueueState eNewState)
{
	switch (eNewState)
	{
		xcase PlayerQueueState_Accepted:
		{
			if (pMember->eState != PlayerQueueState_Offered)
			{
				return false;
			}
		}
		xcase PlayerQueueState_Limbo:
		acase PlayerQueueState_InMap:
		{
			if (pMember->eState == PlayerQueueState_Invited ||
			    pMember->eState == PlayerQueueState_InQueue)
			{
				return false;
			}
		}
	}
	return true;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pMember, ".Estate, .Igroupindex, .Imapkey, .Istateenteredtime");
bool aslQueue_trh_MemberChangeState(ATR_ARGS, 
									ATH_ARG NOCONST(QueueMember) *pMember, 
									PlayerQueueState eNewState, 
									bool bIsPrivateInstance)
{
	U32 uiTimeNow = timeSecondsSince2000();

	if (!aslQueue_trh_StateChange_CheckValid(pMember, eNewState))
	{
		return false;
	}
	pMember->eState = eNewState;
	if(!bIsPrivateInstance && !aslQueue_trh_MemberActivated(pMember) && pMember->eState != PlayerQueueState_Limbo)
	{
		pMember->iGroupIndex = -1;
		pMember->iMapKey = 0;
	}
	pMember->iStateEnteredTime = uiTimeNow;
	return true;
}


AUTO_TRANS_HELPER
ATR_LOCKS(pInstance, ".Eaunorderedmembers[], .Eaorderedmemberentids");
void aslQueue_trh_RemoveMemberByEntID(ATH_ARG NOCONST(QueueInstance) *pInstance, int iEntID)
{
	ea32FindAndRemove(&pInstance->eaOrderedMemberEntIds,iEntID);
	StructDestroyNoConst(parse_QueueMember, eaIndexedRemoveUsingInt(&pInstance->eaUnorderedMembers, iEntID));
}

AUTO_TRANS_HELPER
ATR_LOCKS(pInstance, ".Eaunorderedmembers, .Eaorderedmemberentids");
void aslQueue_trh_InstanceRemoveMemberByMemberAndIndex(ATH_ARG NOCONST(QueueInstance) *pInstance, NOCONST(QueueMember) *pMember, int iMemberIdx)
{
	// Safety check. If this is ever not true something has gone wrong with the calling code.
	devassert(pInstance->eaUnorderedMembers[iMemberIdx]==pMember);
	if (pMember!=NULL)	// This check really shouldn't be necessary, but just in case this ends up getting called from some strange place.
	{
		ea32FindAndRemove(&pInstance->eaOrderedMemberEntIds,pMember->iEntID);
		StructDestroyNoConst(parse_QueueMember, eaRemove(&pInstance->eaUnorderedMembers, iMemberIdx));
	}
}

AUTO_TRANS_HELPER
ATR_LOCKS(pInstance, ".Eaunorderedmembers, .Eaorderedmemberentids, .Pparams");
void aslQueue_trh_RemoveAllPlayersFromInstance(ATH_ARG NOCONST(QueueInstance) *pInstance)
{
	S32 iMemberIdx;
	for (iMemberIdx = eaSize(&pInstance->eaUnorderedMembers)-1; iMemberIdx >= 0; iMemberIdx--)
	{
		NOCONST(QueueMember) *pMember = pInstance->eaUnorderedMembers[iMemberIdx];
		devassert(pMember!=NULL);	// How could this happen? It's bad because we can't then find the corresponding EntID in the order list

		if (pMember!=NULL)
		{
			if (!(pMember->eState == PlayerQueueState_InMap || pMember->eState == PlayerQueueState_Countdown ||
				(pMember->eState == PlayerQueueState_Accepted && pInstance->pParams && pInstance->pParams->uiOwnerID==0)))
			{
				aslQueue_trh_InstanceRemoveMemberByMemberAndIndex(pInstance, pMember, iMemberIdx);
			}
		}
	}
}

AUTO_TRANS_HELPER
ATR_LOCKS(pInstance, ".Eaunorderedmembers, .Eaorderedmemberentids, .Pparams, .Uorigownerid");
bool aslQueue_trh_RemovePlayerFromInstance(ATH_ARG NOCONST(QueueInstance) *pInstance, 
										   QueueDef *pDef,
										   ContainerID iEntID, 
										   PlayerQueueState eState)
{
	bool bRemoveSuccess=false;

	NOCONST(QueueMember) *pMember = eaIndexedGetUsingInt(&pInstance->eaUnorderedMembers, iEntID);

	if (pMember!=NULL)
	{
		if (eState == PlayerQueueState_None || pMember->eState == eState)
		{
			bRemoveSuccess=true;
			aslQueue_trh_RemoveMemberByEntID(pInstance, iEntID);
		}
	}

	//If the owner is leaving, attempt migrate the owner
	// Do not change the owner if the queue is flagged as bRequireAnyGuild, since there could be members that are not in the owner's guild,
	// which would violate the ownership rules for this type of queue
	if (pInstance->pParams && pInstance->pParams->uiOwnerID == iEntID)
	{
		pMember = NULL;
		if (pDef && !pDef->Requirements.bRequireAnyGuild)
		{
			S32 iMemIdx;
			for (iMemIdx = 0; iMemIdx < eaSize(&pInstance->eaUnorderedMembers); iMemIdx++)
			{
				switch (pInstance->eaUnorderedMembers[iMemIdx]->eState)
				{
					xcase PlayerQueueState_None:
					acase PlayerQueueState_Invited:
					acase PlayerQueueState_Limbo:
					acase PlayerQueueState_Exiting:
					{
						continue;
					}
				}
				pMember = pInstance->eaUnorderedMembers[iMemIdx];
				break;
			}
		}
		if (NONNULL(pMember))
		{
			// A valid owner was found, set the owner ID
			if (!pInstance->uOrigOwnerID)
			{
				pInstance->uOrigOwnerID = pInstance->pParams->uiOwnerID;
			}
			pInstance->pParams->uiOwnerID = pMember->iEntID;
		}
		else
		{
			// Couldn't find a new valid owner, boot all players
			aslQueue_trh_RemoveAllPlayersFromInstance(pInstance);
		}
	}
	
	return(bRemoveSuccess);
}

//Find and remove a specific member from the queue
AUTO_TRANS_HELPER
ATR_LOCKS(pQueueContainer, ".Eainstances, .Hdef");
bool aslQueue_trh_RemovePlayer(ATR_ARGS, 
							   ATH_ARG NOCONST(QueueInfo)* pQueueContainer, 
							   ContainerID iEntID,
							   PlayerQueueState eState)
{
	QueueDef* pDef = GET_REF(pQueueContainer->hDef);
	bool bSuccess = false;
	S32 iInstanceIdx;
	for (iInstanceIdx = eaSize(&pQueueContainer->eaInstances) - 1; iInstanceIdx >= 0; iInstanceIdx--)
	{
		NOCONST(QueueInstance)* pInstance = pQueueContainer->eaInstances[iInstanceIdx];
		if (aslQueue_trh_RemovePlayerFromInstance(pInstance, pDef, iEntID, eState))
		{
			bSuccess = true;
		}
	}
	return bSuccess;
}

AUTO_TRANSACTION
ATR_LOCKS(pQueue, "eaInstances[]");
enumTransactionOutcome aslQueue_tr_ClearInstance(ATR_ARGS, NOCONST(QueueInfo) *pQueue, U32 uiInstanceID)
{
	NOCONST(QueueInstance) *pInstance = eaIndexedGetUsingInt(&pQueue->eaInstances, uiInstanceID);
	
	if(NONNULL(pInstance))
	{
		S32 iMemberIdx;
		for(iMemberIdx = eaSize(&pInstance->eaUnorderedMembers)-1; iMemberIdx >= 0; iMemberIdx--)
		{
			NOCONST(QueueMember) *pMember = pInstance->eaUnorderedMembers[iMemberIdx];
			devassert(pMember!=NULL);	// How could this happen? It's bad because we can't then find the corresponding EntID in the order list
			
			//Only remove those that aren't active
			if( !aslQueue_trh_MemberActivated(pMember))
			{
				aslQueue_trh_InstanceRemoveMemberByMemberAndIndex(pInstance, pMember, iMemberIdx);
			}
		}
		
		return(TRANSACTION_OUTCOME_SUCCESS);
	}

	return(TRANSACTION_OUTCOME_FAILURE);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pInstance, ".Eaunorderedmembers, .Eaorderedmemberentids");
void aslQueue_trh_AddPlayerToInstance(ATR_ARGS, 
									  ATH_ARG NOCONST(QueueInstance)* pInstance, 
									  ContainerID iEntID, 
									  ContainerID iTeamID,
									  S32 iLevel, S32 iRank,
									  const char* pchAffiliation,
									  S32 iGroupRole,
									  S32 iGroupClass,
									  S64 iJoinMapKey,
									  QueueJoinFlags eJoinFlags,
									  S32 iJoinGroupID, 
									  S32 eState, 
									  QueueMemberPrefs *pPrefs,
									  U32 uiJoinTimeOverride)
{
	// See if they are already in the list. If not then add them.
	if (!eaIndexedGetUsingInt(&pInstance->eaUnorderedMembers, iEntID))
	{
		NOCONST(QueueMember)* pMember = StructCreateNoConst(parse_QueueMember);
	
		pMember->iEntID = iEntID;
		pMember->iTeamID = iTeamID;
		pMember->eState = eState;
		if(!uiJoinTimeOverride)
		{
			pMember->iQueueEnteredTime = pMember->iStateEnteredTime = timeSecondsSince2000();
		}
		else
		{
			pMember->iQueueEnteredTime = pMember->iStateEnteredTime = uiJoinTimeOverride;
		}
		pMember->iGroupIndex = iJoinGroupID;
		pMember->iJoinMapKey = iJoinMapKey;
		
		pMember->pchAffiliation = allocAddString(pchAffiliation);
		pMember->iLevel = iLevel;
		pMember->iRank = iRank;
		pMember->iGroupRole = iGroupRole;
		pMember->iGroupClass = iGroupClass;
		
		pMember->eJoinFlags = eJoinFlags;
		if(pPrefs)
			pMember->pJoinPrefs = (NOCONST(QueueMemberPrefs)*)StructClone(parse_QueueMemberPrefs,pPrefs);
		else
			pMember->pJoinPrefs = NULL;

		//Push them on the list of members. It's an unordered list so it can be indexed. This is good for data transfer efficiency
		eaIndexedEnableNoConst(&pInstance->eaUnorderedMembers, parse_QueueMember);
		eaPush(&pInstance->eaUnorderedMembers, pMember);
		
		//Push the ID onto the ordered list. Unindexed so it keeps the order. We can look up the EntID in the Unordered list to get the Member data
		ea32Push(&pInstance->eaOrderedMemberEntIds, pMember->iEntID);
	}
}

AUTO_TRANS_HELPER
ATR_LOCKS(pQueueContainer, ".Uicurrentid, .Eainstances");
NOCONST(QueueInstance)* aslQueue_trh_CreateInstance_Internal(ATH_ARG NOCONST(QueueInfo)* pQueueContainer, 
															 NON_CONTAINER QueueInstanceParams* pParams,
															 NON_CONTAINER QueueGameSettings* pSettings)
{
	NOCONST(QueueInstance)* pInstance = StructCreateNoConst(parse_QueueInstance);
	pInstance->pParams = StructCreateNoConst(parse_QueueInstanceParams);
	StructCopyAllDeConst(parse_QueueInstanceParams,pParams,pInstance->pParams);
	if (pInstance->pParams->pchPassword && !pInstance->pParams->pchPassword[0])
		StructFreeStringSafe(&pInstance->pParams->pchPassword);
	pInstance->uiID = ++pQueueContainer->uiCurrentID;
	if(pSettings)
		eaCopyStructs(&pSettings->eaSettings, (QueueGameSetting***)&pInstance->eaSettings, parse_QueueGameSetting);
	eaPush(&pQueueContainer->eaInstances, pInstance);
	return pInstance;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pQueueContainer, ".Eainstances, .Uicurrentid, .Hdef");
bool aslQueue_trh_Join(ATR_ARGS, 
					   ATH_ARG NOCONST(QueueInfo)* pQueueContainer,
					   ContainerID iEntID, 
					   ContainerID iTeamID,
				   QueueMemberJoinCriteria* pMemberJoinCriteria,
//					   S32 iLevel, S32 iRank,
//					   const char* pchAffiliation,
//					   S32 iGroupRole,
//					   S32 iGroupClass,
					   const char* pchPassword,
					   U32 uiInstanceID, 
					   S64 iMapKey,
					   QueueJoinFlags eJoinFlags,
					   NON_CONTAINER QueueInstanceParams* pParams, 
					   NON_CONTAINER QueueMemberPrefs* pPrefs,
					   U32 uiJoinTimeOverride)
{
	NOCONST(QueueInstance)* pInstance = NULL;
	QueueDef* pDef = GET_REF(pQueueContainer->hDef);
	S32 iGroupID = -1;

	if (!pDef)
	{
		return false;
	}
	if (uiInstanceID > 0)
	{
		pInstance = eaIndexedGetUsingInt(&pQueueContainer->eaInstances, uiInstanceID);
	}

	if (NONNULL(pInstance))
	{
		NOCONST(QueueMember)* pMember = queue_trh_FindPlayerInInstance(pInstance, iEntID);

		if (NONNULL(pInstance->pParams) && pInstance->pParams->uiOwnerID > 0)
		{
			// If the instance is private, check to see if the member is invited
			if (NONNULL(pMember) && pMember->eState == PlayerQueueState_Invited)
			{
				// If the member can join the private instance, change the member to the "in queue" state
				aslQueue_trh_MemberChangeState(ATR_PASS_ARGS, pMember, PlayerQueueState_InQueue, true);
				return true;
			}
			if (NONNULL(pMember) || 
				!pInstance->pParams->pchPrivateName || 
				strcmp_safe(pInstance->pParams->pchPassword, EMPTY_TO_NULL(pchPassword))!=0)
			{
				return false;
			}
		}
		else if (NONNULL(pMember))
		{
			return false;
		}
		else
		{
			// If the member is performing an action in another instance
			// or they are part of a private instance that is not this instance, then they cannot join this instance
			S32 i;
			for(i = eaSize(&pQueueContainer->eaInstances) - 1; i >= 0; i--)
			{
				NOCONST(QueueInstance)* pFindInstance = pQueueContainer->eaInstances[i];

				if (ISNULL(pFindInstance->pParams) || pFindInstance->uiID == pInstance->uiID)
					continue;

				if (NONNULL(pFindInstance))
				{
					pMember = queue_trh_FindPlayerInInstance(pFindInstance, iEntID);
					if (NONNULL(pMember))
					{
						if (pMember->eState == PlayerQueueState_Offered ||
							pMember->eState == PlayerQueueState_Delaying ||
							pMember->eState == PlayerQueueState_Accepted ||
							pMember->eState == PlayerQueueState_Countdown ||
							pMember->eState == PlayerQueueState_InMap)
						{
							return false;
						}
						if (pMember->eState != PlayerQueueState_Invited && pFindInstance->pParams->uiOwnerID)
						{
							return false;
						}
					}
				}
			}
		}
	}

	if (ISNULL(pInstance))
	{
		if (uiInstanceID == 0 && pParams)
		{
			pInstance = queue_trh_FindInstance(pQueueContainer, pParams);
			// create an instance if no instance is specified and there are no instances with those params
			if(ISNULL(pInstance) && aslQueue_trh_IsPrivateNameUnique(pQueueContainer, pParams->pchPrivateName))
			{
				pInstance = aslQueue_trh_CreateInstance_Internal(pQueueContainer, pParams, NULL); 
			}
		}

		//If still null, then fail
		if(ISNULL(pInstance))
		{
			return false;
		}
	}

	if (iMapKey) // If the player is trying to join an existing map, check to see if it's valid
	{
		NOCONST(QueueMap)* pMap = eaIndexedGetUsingInt(&pInstance->eaMaps, iMapKey);

		if (ISNULL(pMap))
			return false;
	}

	if (NONNULL(pInstance->pParams) && pInstance->pParams->uiOwnerID && iGroupID < 0)
	{
		// Private queue management
		QueueMatch QMatch = {0};
		queue_trh_GroupCache(pInstance, NULL, PlayerQueueState_None, PlayerQueueState_None, pDef, &QMatch);
		iGroupID = queue_GetBestGroupIndexForPlayer(pMemberJoinCriteria->pchAffiliation, iTeamID, -1, false, pDef, &QMatch);
		StructDeInit(parse_QueueMatch, &QMatch);
	}

	// Join the instance
	aslQueue_trh_AddPlayerToInstance(ATR_PASS_ARGS, 
									 pInstance, 
									 iEntID, 
									 iTeamID,
									 pMemberJoinCriteria->iLevel, 
									 pMemberJoinCriteria->iRank, 
									 pMemberJoinCriteria->pchAffiliation, 
									 pMemberJoinCriteria->iGroupRole, 
									 pMemberJoinCriteria->iGroupClass, 
									 iMapKey, 
									 eJoinFlags,
									 iGroupID, 
									 PlayerQueueState_InQueue, 
									 pPrefs,
									 uiJoinTimeOverride);
	return true;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pQueueContainer, ".Hdef, .Eainstances");
bool aslQueue_trh_Invite(ATR_ARGS, 
						 ATH_ARG NOCONST(QueueInfo)* pQueueContainer,
						 U32 uiInstanceID, 
						 ContainerID uiEntID, 
						 ContainerID uiInviteeID, 
						 ContainerID uiTeamID, 
						 S32 iLevel, S32 iRank,
						 const char* pchAffiliation)
{
	QueueDef* pDef = GET_REF(pQueueContainer->hDef);
	QueueMatch QMatch = {0};
	NOCONST(QueueInstance)* pInstance = eaIndexedGetUsingInt(&pQueueContainer->eaInstances, uiInstanceID);
	NOCONST(QueueMember)* pInviter;
	NOCONST(QueueMember)* pInvitee;
	S32 iGroupID;

	if (!pDef || ISNULL(pInstance) || ISNULL(pInstance->pParams) || pInstance->pParams->uiOwnerID == 0)
		return false;

	pInviter = queue_trh_FindPlayerInInstance(pInstance, uiEntID);

	if (ISNULL(pInviter) || pInviter->eState == PlayerQueueState_Invited)
		return false;

	pInvitee = queue_trh_FindPlayer(pQueueContainer, uiInviteeID, NULL);

	if (NONNULL(pInvitee))
		return false;

	// This is part of a long chain of private queue management
	queue_trh_GroupCache(pInstance, NULL, PlayerQueueState_None, PlayerQueueState_None, pDef, &QMatch);
	iGroupID = queue_GetBestGroupIndexForPlayer(pchAffiliation, uiTeamID, -1, false, pDef, &QMatch);
	StructDeInit(parse_QueueMatch, &QMatch);

	if (iGroupID < 0)
		return false;

	aslQueue_trh_AddPlayerToInstance(ATR_PASS_ARGS, 
									 pInstance, uiInviteeID, uiTeamID, iLevel, iRank, pchAffiliation, 0, 0,		// No group role or class here.
									 0, kQueueJoinFlags_None, iGroupID, PlayerQueueState_Invited, NULL, 0);

	return true;
}


AUTO_TRANS_HELPER
ATR_LOCKS(pQueueContainer, ".Hdef, .Eainstances, .Uicurrentid");
bool aslQueue_trh_CreateInstance(ATR_ARGS,
								 ATH_ARG NOCONST(QueueInfo)* pQueueContainer, 
								 ContainerID iEntID, 
								 U32 iTeamID,
								 S32 iLevel, S32 iRank,
								 const char* pchAffiliation, 
								 QueueInviteInfo* pInviteData,
								 QueueGameSettings* pSettings,
								 NON_CONTAINER QueueInstanceParams* pParams,
								 NON_CONTAINER QueueMemberPrefs* pPrefs)
{
	QueueDef* pDef = GET_REF(pQueueContainer->hDef);
	NOCONST(QueueInstance)* pInstance = NULL;
	NOCONST(QueueMember)* pMember = iEntID > 0 ? queue_trh_FindPlayer(pQueueContainer, iEntID, NULL) : NULL;
	S32 iGroupID = -1;

	if (!pDef || NONNULL(pMember))
		return false;

	if (pParams) // Try to find an instance with matching parameters
	{
		pInstance = queue_trh_FindInstance(pQueueContainer,pParams);
	}
	else
	{
		return false;
	}

	// Create a new instance with the specified parameters
	if (ISNULL(pInstance) && aslQueue_trh_IsPrivateNameUnique(pQueueContainer, pParams->pchPrivateName)) 
	{
		pInstance = aslQueue_trh_CreateInstance_Internal(pQueueContainer, pParams, pSettings);
	}
	else
	{
		return false;
	}

	if (pParams->uiOwnerID > 0) // If the game is private, find a group
	{
		QueueMatch QMatch = {0};
		queue_trh_GroupCache(pInstance, NULL, PlayerQueueState_None, PlayerQueueState_None, pDef, &QMatch);
		iGroupID = queue_GetBestGroupIndexForPlayer(pchAffiliation, iTeamID, -1, false, pDef, &QMatch);
		StructDeInit(parse_QueueMatch, &QMatch);
		
		// Pass back the instance ID to create the private chat channel
		TRANSACTION_APPEND_LOG_SUCCESS("%d", pInstance->uiID);
	}

	if (iEntID > 0)
	{
		aslQueue_trh_AddPlayerToInstance(ATR_PASS_ARGS, 
										 pInstance, iEntID, iTeamID, iLevel, iRank, pchAffiliation, 0, 0, // No group role or class here
										 0, kQueueJoinFlags_None, iGroupID, PlayerQueueState_InQueue, pPrefs, 0);
		if (pInviteData)
		{
			// Automatically invite the player to the match
			aslQueue_trh_Invite(ATR_PASS_ARGS, 
								pQueueContainer, 
								pInstance->uiID, 
								iEntID, 
								pInviteData->uEntID, 
								pInviteData->uTeamID, 
								pInviteData->iLevel, 
								pInviteData->iRank,
								pInviteData->pchAffiliation);
		}
	}
	return true;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pQueueContainer, "eaInstances[], .Hdef");
bool aslQueue_trh_Kick(ATR_ARGS, 
					   ATH_ARG NOCONST(QueueInfo)* pQueueContainer, 
					   U32 uiInstanceID, 
					   U32 uiEntID, 
					   U32 uiMemberID)
{
	NOCONST(QueueInstance)* pInstance = eaIndexedGetUsingInt(&pQueueContainer->eaInstances, uiInstanceID);
	QueueDef* pDef = GET_REF(pQueueContainer->hDef);

	if (ISNULL(pInstance) ||	
		ISNULL(pInstance->pParams) ||
		(uiEntID && pInstance->pParams->uiOwnerID != uiEntID) ||
		uiEntID == uiMemberID)
	{
		return false;
	}
	return aslQueue_trh_RemovePlayerFromInstance(pInstance, pDef, uiMemberID, PlayerQueueState_None);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pQueueContainer, ".Hdef, eaInstances[]");
bool aslQueue_trh_ChangeGroup(ATR_ARGS, 
							  ATH_ARG NOCONST(QueueInfo)* pQueueContainer, 
							  U32 uiInstanceID, 
							  U32 uiEntID, 
							  S32 iGroupID)
{
	QueueDef* pDef = GET_REF(pQueueContainer->hDef);
	QueueMatch QMatch = {0};
	NOCONST(QueueInstance)* pInstance = pDef ? eaIndexedGetUsingInt(&pQueueContainer->eaInstances, uiInstanceID) : NULL;
	NOCONST(QueueMember)* pMember;
	bool bMatchFailed = false;

	if (ISNULL(pInstance) || ISNULL(pInstance->pParams) || pInstance->pParams->uiOwnerID == 0)
		return false;

	pMember = queue_trh_FindPlayerInInstance(pInstance, uiEntID);

	if (ISNULL(pMember) || pMember->eState != PlayerQueueState_InQueue || pMember->iGroupIndex == iGroupID)
		return false;

	queue_trh_GroupCache(pInstance, NULL, PlayerQueueState_None, PlayerQueueState_None, pDef, &QMatch);
	if (!queue_IsValidGroupIndexForPlayer(iGroupID, pMember->pchAffiliation, false, pDef, &QMatch))
	{
		bMatchFailed = true;
	}
	StructDeInit(parse_QueueMatch, &QMatch);

	if (bMatchFailed)
		return false;

	pMember->iGroupIndex = iGroupID;

	return true;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pInstance, ".Eaunorderedmembers, .Eaorderedmemberentids, .Pparams, .Uorigownerid");
bool aslQueue_trh_MemberReenterQueue(ATR_ARGS, ATH_ARG NOCONST(QueueInstance) *pInstance, QueueDef* pDef, ContainerID iEntID)
{
	NOCONST(QueueMember) *pMember = queue_trh_FindPlayerInInstance(pInstance, iEntID);
	if(NONNULL(pMember))
	{
		U32 uTeamID = pMember->iTeamID;
		S32 iLevel = pMember->iLevel;
		S32 iRank = pMember->iRank;
		const char* pchAffiliation = allocAddString(pMember->pchAffiliation);
		S32 iGroupRole = pMember->iGroupRole;
		S32 iGroupClass = pMember->iGroupClass;
		S64 iJoinMapKey = pMember->iJoinMapKey;
		QueueJoinFlags eJoinFlags = pMember->eJoinFlags;

		if (aslQueue_trh_RemovePlayerFromInstance(pInstance, pDef, iEntID, 0))
		{
			aslQueue_trh_AddPlayerToInstance(ATR_PASS_ARGS, 
											 pInstance, 
											 iEntID, 
											 uTeamID, 
											 iLevel, 
											 iRank, 
											 pchAffiliation,
											 iGroupRole,
											 iGroupClass,
											 iJoinMapKey, 
											 eJoinFlags,
											 -1, 
											 PlayerQueueState_InQueue,
											 NULL,
											 0);
			return true;
		}
	}

	return false;
}

AUTO_TRANSACTION
ATR_LOCKS(pQueueContainer, "eaInstances[], .Hdef");
enumTransactionOutcome aslQueue_tr_MemberReenterQueue(ATR_ARGS, 
													  NOCONST(QueueInfo) *pQueueContainer, 
													  U32 uiInstanceID, 
													  ContainerID iEntID)
{
	NOCONST(QueueInstance)* pInstance = eaIndexedGetUsingInt(&pQueueContainer->eaInstances, uiInstanceID);

	if (NONNULL(pInstance) && aslQueue_trh_MemberReenterQueue(ATR_PASS_ARGS, pInstance, GET_REF(pQueueContainer->hDef), iEntID))
	{
		return TRANSACTION_OUTCOME_SUCCESS;
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANSACTION
ATR_LOCKS(pQueueContainer, ".Eainstances");
enumTransactionOutcome aslQueue_tr_MembersTeamRemove(ATR_ARGS, 
													 NOCONST(QueueInfo) *pQueueContainer, 
													 QueueIntArray *pMembers)
{
	S32 iMemberIdx;
	for(iMemberIdx = eaiSize(&pMembers->piArray)-1; iMemberIdx >= 0; iMemberIdx--)
	{
		NOCONST(QueueMember) *pMember = queue_trh_FindPlayer(pQueueContainer,pMembers->piArray[iMemberIdx], NULL);
		if(NONNULL(pMember) && pMember->iTeamID)
		{
			pMember->iTeamID = 0;
		}
	}
	return(TRANSACTION_OUTCOME_SUCCESS);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Get all the queues
///////////////////////////////////////////////////////////////////////////////////////////

//Called by the game server to get the list of all queues that it can reference
AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void aslQueue_GetQueueList(ContainerID uiGameServerID)
{
	QueueList *pList = NULL;
	if (aslQueue_ServerReady())
	{
		ContainerIterator queueIter;
		QueueInfo *pQueue = NULL;
		pList = StructCreate(parse_QueueList);
			
		objInitContainerIteratorFromType(GLOBALTYPE_QUEUEINFO, &queueIter);
		while (pQueue = objGetNextObjectFromIterator(&queueIter))
		{
			QueueRef *pRef = StructCreate(parse_QueueRef);
			pRef->iContID = pQueue->iContainerID;
			pRef->pchQueueName = allocAddString(pQueue->pchName);
			eaPush(&pList->eaQueueRefs, pRef);
		}
		objClearContainerIterator(&queueIter);
	}
	RemoteCommand_gslQueue_GetQueueList(GLOBALTYPE_GAMESERVER, uiGameServerID, pList);
}

// Send InstantiationInfo to the GameServer on request
AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void aslQueue_CheckQueueState(ContainerID uiEntID)
{
	ContainerIterator queueIter;
	QueueInfo *pQueue = NULL;
	QueuePenaltyCategoryData** eaPenaltyCategories = NULL;
	PlayerQueuePenaltyData* pPenaltyData = NULL;
	QueueInstantiationInfo* pInstantiationInfo = NULL;
	U32 bIsQueued = false;
		
	objInitContainerIteratorFromType(GLOBALTYPE_QUEUEINFO, &queueIter);
	while (pQueue = objGetNextObjectFromIterator(&queueIter))
	{
		QueueDef* pDef = GET_REF(pQueue->hDef);
		S32 i;
		for (i = eaSize(&pQueue->eaInstances)-1; i >= 0; i--)
		{
			QueueInstance* pInstance = pQueue->eaInstances[i];
			QueueMember* pMember = queue_FindPlayerInInstance(pInstance, uiEntID);
			if (pMember && (pMember->eState == PlayerQueueState_InQueue 
						|| pMember->eState == PlayerQueueState_Delaying
						|| pMember->eState == PlayerQueueState_Offered
						|| pMember->eState == PlayerQueueState_Invited))
			{	
				bIsQueued = true;
			}
			if (!pInstantiationInfo && pDef && pMember && pMember->iMapKey && 
				(pMember->eState == PlayerQueueState_InMap ||
				 pMember->eState == PlayerQueueState_Limbo))
			{
				QueueMap* pMap = eaIndexedGetUsingInt(&pInstance->eaMaps, pMember->iMapKey);
				QueueGroupDef* pGroupDef = eaGet(&pDef->eaGroupDefs, pMember->iGroupIndex);
				if (pMap && pGroupDef)
				{
					pInstantiationInfo = StructCreate(parse_QueueInstantiationInfo);
					pInstantiationInfo->pchQueueDef = allocAddString(pDef->pchName);
					pInstantiationInfo->pchMapName = allocAddString(pMap->pchMapName);
					pInstantiationInfo->pchSpawnName = allocAddString(pGroupDef->pchSpawnTargetName);
					pInstantiationInfo->iMapKey = pMap->iMapKey;
					pInstantiationInfo->uInstanceID = pInstance->uiID;
					pInstantiationInfo->iGroupIndex = pMember->iGroupIndex;
				}
			}
		}
	}
	objClearContainerIterator(&queueIter);
	if (aslQueue_GetPlayerPenaltyData(uiEntID, -1, &eaPenaltyCategories))
	{
		pPenaltyData = StructCreate(parse_PlayerQueuePenaltyData);
		eaIndexedEnable(&pPenaltyData->eaCategories, parse_QueuePenaltyCategoryData);
		eaCopyStructs(&eaPenaltyCategories, &pPenaltyData->eaCategories, parse_QueuePenaltyCategoryData);
		eaDestroy(&eaPenaltyCategories);
	}
	RemoteCommand_gslQueue_ReceiveQueueState(GLOBALTYPE_ENTITYPLAYER, uiEntID, uiEntID, bIsQueued, pPenaltyData, pInstantiationInfo);
	StructDestroy(parse_PlayerQueuePenaltyData, pPenaltyData);
	StructDestroy(parse_QueueInstantiationInfo, pInstantiationInfo);
}

//////////////////////////////////////////////////////////////////////////////////
// Queue Cleanup
//////////////////////////////////////////////////////////////////////////////////

AUTO_TRANSACTION
ATR_LOCKS(pQueueContainer, ".Eainstances");
enumTransactionOutcome aslQueue_tr_RemoveAllMembers(ATR_ARGS, NOCONST(QueueInfo) *pQueueContainer)
{
	S32 iInstanceIdx;
	for (iInstanceIdx = eaSize(&pQueueContainer->eaInstances)-1; iInstanceIdx >= 0; iInstanceIdx--)
	{
		NOCONST(QueueInstance)* pInstance = pQueueContainer->eaInstances[iInstanceIdx];
		aslQueue_trh_RemoveAllPlayersFromInstance(pInstance);
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pQueueContainer, ".Eainstances");
enumTransactionOutcome aslQueue_tr_RemovePlayerFromTeam(ATR_ARGS, NOCONST(QueueInfo) *pQueueContainer, ContainerID iEntID)
{
	NOCONST(QueueMember) *pMember = queue_trh_FindPlayer(pQueueContainer, iEntID, NULL);
	if(NONNULL(pMember) && pMember->iTeamID)
	{
		pMember->iTeamID = 0;
		return TRANSACTION_OUTCOME_SUCCESS;
	}

	return TRANSACTION_OUTCOME_FAILURE;
}
///////////////////////////////////////////////////////////////////////////////////////////
// Remote commands to enter and leave queues
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void aslQueue_TeamJoin(ContainerID iEntID, ContainerID iTeamID, const char* pchAffiliation, QueueIntArray *pTeamMemberList, const char* pchQueueName, U32 uiInstanceID, S64 iMapKey, U32 bJoinNewMap, QueueInstanceParams* pParams)
{
	QueueInfo* pQueue = aslQueue_FindQueueByNameLocal(pchQueueName);
	QueueInstance* pInstance = pQueue && uiInstanceID > 0 ? eaIndexedGetUsingInt(&pQueue->eaInstances, uiInstanceID) : NULL;
	QueueDef* pDef;
	S32 iMemIdx, iNumMembers;
	S32 iGroupIndex, iNumGroups;
	bool bOvertime = pInstance && pInstance->bOvertime;
	bool bTooLarge = true;
	
	if(!iTeamID)
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to team queue for [%s][%d] but they weren't on a team.\n",
			iEntID,
			pchQueueName,
			uiInstanceID);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_FOUND_MESG);
		return;
	}
	
	if(!pQueue || (uiInstanceID > 0 && !pInstance) || !(pDef = GET_REF(pQueue->hDef)))
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to team queue for [%s][%d] but it wasn't present.\n",
						iEntID,
						pchQueueName,
						uiInstanceID);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_FOUND_MESG);
		return;
	}

	if (pInstance && pParams)
	{
		StructCopyAll(parse_QueueInstanceParams, pInstance->pParams, pParams);
	}

	iNumMembers = eaiSize(&pTeamMemberList->piArray);
	iNumGroups = eaSize(&pDef->eaGroupDefs);

	if(pDef->Settings.bSplitTeams)
	{
		S32 iTotalSize = 0;
		// First check to see if there are more members than there are size for them to fit in the queue
		for(iGroupIndex = iNumGroups-1; iGroupIndex >= 0; iGroupIndex--)
		{
			QueueGroupDef* pGroupDef = pDef->eaGroupDefs[iGroupIndex];
			const char* pchGroupAffiliation = pGroupDef->pchAffiliation;
			
			if (pchGroupAffiliation && stricmp(pchAffiliation,pchGroupAffiliation) != 0)
				continue;

			iTotalSize += queue_GetGroupMaxSize(pGroupDef, bOvertime);
		}

		if(iNumMembers <= iTotalSize)
		{
			bTooLarge = false;
		}
	}
	else
	{
		// First check to see if there are more members than there are size for them to fit in the queue
		for(iGroupIndex = iNumGroups-1; iGroupIndex >= 0; iGroupIndex--)
		{
			QueueGroupDef *pGroupDef = pDef->eaGroupDefs[iGroupIndex];
			const char* pchGroupAffiliation = pGroupDef->pchAffiliation;

			if (pchGroupAffiliation && stricmp(pchAffiliation,pchGroupAffiliation) != 0)
				continue;
			
			if(queue_GetGroupMaxSize(pGroupDef, bOvertime) >= iNumMembers)
			{
				bTooLarge = false;
				break;
			}
		}
	}

	if(bTooLarge)
	{
		log_printf(LOG_QUEUE, "EntID [%d] had a team that was too large [%d] members.  Trying to queue for [%s].\n",
						iEntID,
						iNumMembers,
						pchQueueName);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_TEAM_TOO_LARGE_MESG);
		return;
	}

	if(iTeamID)
	{
		QueueTeamJoinTime *pJoinTime = eaIndexedGetUsingInt(&s_eaTeamJoinTimes, iTeamID);
		
		if(!pJoinTime)
		{
			pJoinTime = StructCreate(parse_QueueTeamJoinTime);
			pJoinTime->iTeamID = iTeamID;
			eaPush(&s_eaTeamJoinTimes, pJoinTime);
			eaIndexedEnable(&s_eaTeamJoinTimes, parse_QueueTeamJoinTime);
		}
		
		pJoinTime->uiTime = timeSecondsSince2000()+10;
	}

	for(iMemIdx = iNumMembers - 1; iMemIdx >= 0; iMemIdx--)
	{
		U32 iTeamMemberID = pTeamMemberList->piArray[iMemIdx];
		RemoteCommand_gslQueue_rcmd_JoinParams(GLOBALTYPE_ENTITYPLAYER, iTeamMemberID, iTeamMemberID, pchQueueName, uiInstanceID, iMapKey, bJoinNewMap, false, true, pParams);
	}

	log_printf(LOG_QUEUE, "EntID [%d] queued [%d] team members for queue [%s].\n",
						iEntID,
						iNumMembers,
						pchQueueName);
}

static void aslQueue_Join_HandleChatMessages(QueueInfo* pQueue,
											 QueueCBStruct *pCBData)
{
	aslQueue_SendPrivateChatMessageToMembers(pQueue, 
											 pCBData->iInstanceID, 
											 QUEUE_PLAYER_JOINED_PRIVATE_GAME_MESG,
											 pCBData->iEntID, 
											 pCBData->iEntID,
											 -1);
}

static void aslQueue_Join_HandleChatChannels(QueueInfo* pQueue,
											 ContainerID uInstanceID, 
											 ContainerID uEntID,
											 QueueInstanceParams *pParams)
{
	QueueInstance* pInstance = eaIndexedGetUsingInt(&pQueue->eaInstances, uInstanceID);
	U32 uOrigOwnerID = queue_GetOriginalOwnerID(pInstance);

	if (!pInstance)
	{
		//Best attempt at finding the correct instance using the original join, accept invite or create params
		if(!uInstanceID)
		{
			pInstance = queue_FindInstance(pQueue, pParams);
			if(pInstance)
				uOrigOwnerID = queue_GetOriginalOwnerID(pInstance);
		}
	}
	if (!uOrigOwnerID)
	{
		return;
	}
	RemoteCommand_gslQueue_CreateOrJoinPrivateChatChannel(GLOBALTYPE_ENTITYPLAYER,uEntID,uEntID,uOrigOwnerID,uInstanceID);
}

static void aslQueue_QueueJoin_CB(TransactionReturnVal *pReturn, void *pData)
{
	QueueCBStruct *pCBData = (QueueCBStruct*)pData;
	QueueInfo *pQueue = aslQueue_GetQueueContainer(pCBData->iQueueID);

	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		QueueMember *pMember = queue_FindPlayer(pQueue, pCBData->iEntID);
		if(pQueue && pMember)
		{
			aslQueue_Join_HandleChatMessages(pQueue, pCBData);
			aslQueue_Join_HandleChatChannels(pQueue, pCBData->iInstanceID, pCBData->iEntID, pCBData->pParams);
			
			log_printf(LOG_QUEUE,"Player [%d] joined queue [%s].\n", pMember->iEntID, pQueue->pchName);
			queue_SendResultMessage(pCBData->iEntID, 0, pQueue->pchName, QUEUE_JOINED_MESG);
		}
	}
	else
	{
		if(pQueue)
		{
			log_printf(LOG_QUEUE,"Player [%d] failed to joined queue [%s].\n", pCBData->iEntID, pQueue->pchName);
			queue_SendResultMessage(pCBData->iEntID, 0, pQueue->pchName, QUEUE_ALREADY_IN_THIS_QUEUE_MESG);
		}
	}
	StructDestroy(parse_QueueCBStruct, pCBData);
}

AUTO_TRANSACTION
ATR_LOCKS(pQueueContainer, ".Eainstances, .Uicurrentid, .Hdef");
enumTransactionOutcome aslQueue_tr_QueueJoin(ATR_ARGS, NOCONST(QueueInfo) *pQueueContainer, 
											 ContainerID iEntID, U32 iTeamID,
											 QueueMemberJoinCriteria* pMemberJoinCriteria,
//											 S32 iLevel, S32 iRank,
//											 const char* pchAffiliation, S32 iGroupRole, S32 iGroupClass,
											 const char* pchPassword,
											 U32 uiInstanceID, S64 iMapKey, S32 eJoinFlags,
											 NON_CONTAINER QueueInstanceParams* pParams, 
											 NON_CONTAINER QueueMemberPrefs *pPrefs,
											 U32 uiJoinTimeOverride)
{
	if(aslQueue_trh_Join(ATR_PASS_ARGS, pQueueContainer, iEntID, iTeamID, pMemberJoinCriteria,
						pchPassword, uiInstanceID, iMapKey, eJoinFlags, pParams, pPrefs, uiJoinTimeOverride))
	{	
		return TRANSACTION_OUTCOME_SUCCESS;
	}
	else
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}
}

// If either the player is found in a queue with any of the states in pePlayerQueueStates
// or is in a number of queues >= iQueueCount (when iQueueCount > 0), return true
static bool aslQueue_IsPlayerInQueues(ContainerID iEntID, S32* pePlayerQueueStates, S32 iQueueCount)
{
	ContainerIterator queueIter;
	QueueInfo *pQueue = NULL;
	S32 iInstanceIdx, iCount = 0;
	objInitContainerIteratorFromType(GLOBALTYPE_QUEUEINFO, &queueIter);
	while (pQueue = objGetNextObjectFromIterator(&queueIter))
	{
		for (iInstanceIdx = eaSize(&pQueue->eaInstances)-1; iInstanceIdx >= 0; iInstanceIdx--)
		{
			QueueInstance* pInstance = pQueue->eaInstances[iInstanceIdx];
			QueueMember* pMember = queue_FindPlayerInInstance(pInstance, iEntID);

			if (!pMember)
			{
				continue;
			}
			if (iQueueCount > 0)
			{
				if (++iCount >= iQueueCount)
				{
					objClearContainerIterator(&queueIter);
					return true;
				}
			}
			if (pePlayerQueueStates)
			{
				if (ea32Find(&pePlayerQueueStates, pMember->eState) >= 0)
				{
				objClearContainerIterator(&queueIter);
				return true;
				}
			}
		}
	}
	objClearContainerIterator(&queueIter);
	return false;
}

typedef struct RankRequestBeforeQueue
{
	U32 iContainerID;
	U32 iEntID;
	U32 iTeamID;
	U32 iLevel;
	U32 iRank;
	const char *pchAffiliation;
	const char *pchPassword;
	U32 uiInstanceID;
	U32 iMapKey;
	U32 eJoinFlags;
	U32 uiJoinTimeOverride;

	QueueCBStruct *pCBData;
}RankRequestBeforeQueue;

void aslQueue_ReturnLeaderboardRank(TransactionReturnVal *returnVal,RankRequestBeforeQueue *pRequest)
{
	if(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		TransactionReturnVal *pReturn;
		NOCONST(QueueMemberPrefs) *pPrefs = (NOCONST(QueueMemberPrefs)*)pRequest->pCBData->pPrefs;

		pPrefs->fCurrentRank = atof(returnVal->pBaseReturnVals->returnString);

		pReturn = objCreateManagedReturnVal(aslQueue_QueueJoin_CB, pRequest->pCBData);
		
		{
			QueueMemberJoinCriteria* pMemberJoinCriteria = StructCreate(parse_QueueMemberJoinCriteria);
			pMemberJoinCriteria->iLevel = pRequest->iLevel;
			pMemberJoinCriteria->iRank = pRequest->iRank;
			pMemberJoinCriteria->pchAffiliation = pRequest->pchAffiliation;
			pMemberJoinCriteria->iGroupRole = 0; // iGroupRole;
			pMemberJoinCriteria->iGroupClass = 0; // iGroupClass;


			AutoTrans_aslQueue_tr_QueueJoin(pReturn, GetAppGlobalType(), 
				GLOBALTYPE_QUEUEINFO, pRequest->iContainerID, 
				pRequest->iEntID, pRequest->iTeamID,
				pMemberJoinCriteria,
				pRequest->pchPassword, pRequest->uiInstanceID, pRequest->iMapKey,	
				pRequest->eJoinFlags, pRequest->pCBData->pParams, pRequest->pCBData->pPrefs, pRequest->uiJoinTimeOverride);

			StructDestroy(parse_QueueMemberJoinCriteria, pMemberJoinCriteria);
		}
	
		pRequest->pCBData = NULL;
	}

	if(pRequest->pCBData)
	{
		StructDestroy(parse_QueueCBStruct,pRequest->pCBData);
		pRequest->pCBData = NULL;
	}

	free(pRequest);
}

void aslQueue_GetLeaderboardRankBeforeQueue(const char *pchLeaderboard, RankRequestBeforeQueue *pRequest)
{
	RemoteCommand_leaderboard_RequestRankForQueue(objCreateManagedReturnVal(aslQueue_ReturnLeaderboardRank,pRequest),GLOBALTYPE_LEADERBOARDSERVER,0,pchLeaderboard,pRequest->iEntID);
}

AUTO_COMMAND_REMOTE;
void aslQueue_Join(ContainerID iEntID,
				   ContainerID iTeamID,
				   QueueMemberJoinCriteria* pMemberJoinCriteria,
				   const char* pchQueueName,
				   const char* pchPassword,
				   U32 uiInstanceID,
				   S64 iMapKey,
				   QueueJoinFlags eJoinFlags,
				   QueueInstanceParams* pParams,
				   QueueMemberPrefs* pPrefs)
{
	QueueInfo* pQueue = aslQueue_FindQueueByNameLocal(pchQueueName);
	QueueInstance* pInstance = NULL;
	QueueMember* pMember;
	QueueDef* pDef;
	bool bIsPrivate = false;

	if (!pQueue || !(pDef = GET_REF(pQueue->hDef)))
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to queue for [%s] but it wasn't present.\n",
							iEntID,
							pchQueueName);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_FOUND_MESG);
		return;
	}
	if (	(uiInstanceID > 0 && !(pInstance = eaIndexedGetUsingInt(&pQueue->eaInstances, uiInstanceID)))
		||	(iMapKey && (!pInstance || !eaIndexedGetUsingInt(&pInstance->eaMaps, iMapKey))))
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to queue for [%s] but the specified parameters were invalid.\n",
							iEntID,
							pchQueueName);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_FOUND_MESG);
		return;
	}

	bIsPrivate = SAFE_MEMBER2(pInstance, pParams, uiOwnerID) > 0;

	if (bIsPrivate)
	{
		pMember = queue_FindPlayerInInstance(pInstance, iEntID);
		if ((!pMember || pMember->eState != PlayerQueueState_Invited) && 
			(!pInstance->pParams->pchPrivateName || strcmp_safe(EMPTY_TO_NULL(pchPassword), pInstance->pParams->pchPassword)!=0))
		{
			if (pInstance->pParams->pchPassword && pInstance->pParams->pchPassword[0])
			{
				log_printf(LOG_QUEUE, "EntID [%d] tried to queue for [%s], but specified the wrong password.\n",
						   iEntID,
						   pchQueueName);
				queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_BAD_PASSWORD_MESG);
			}
			else
			{
				log_printf(LOG_QUEUE, "EntID [%d] tried to queue for [%s] but it's a private instance and he/she wasn't invited.\n",
						   iEntID,
						   pchQueueName);
				queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_INVITED_MESG);
			}
			return;
		}
	}
	else
	{
		pMember = pInstance ? queue_FindPlayerInInstance(pInstance, iEntID) : NULL;
		if (pMember)
		{
			log_printf(LOG_QUEUE, "EntID [%d] tried to queue for instance [%s][%d] but he/she was already in that queue.\n",
								iEntID,
								pchQueueName,
								uiInstanceID);
			queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_ALREADY_IN_THIS_QUEUE_MESG);
			return;
		}
	}

	if(!aslQueue_IsRandomQueueActive(pDef))
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to queue for [%s] but the queue is no longer being offered as a random queue\n",
							iEntID,
							pchQueueName);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_OFFERED_MESG);
	}

	if (aslQueue_GetPlayerPenaltyData(iEntID, pDef->eCategory, NULL))
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to queue for [%s] but the player is temp banned from joining queues.\n",
							iEntID,
							pchQueueName);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_PLAYER_PENALIZED_MESG);
		return;
	}

	if (aslQueue_IsPlayerInQueues(iEntID, NULL, g_QueueConfig.iMaxQueueCount))
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to queue for instance [%s][%d] but he/she was already in the max number of queues allowed per player.\n",
							iEntID,
							pchQueueName,
							uiInstanceID);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_PLAYER_IN_MAX_COUNT_MESG);
		return;
	}

	{
		U32 uiJoinTimeOverride = 0;
		QueueCBStruct *pCBData = StructCreate(parse_QueueCBStruct);
		TransactionReturnVal *pReturn = NULL;
		if(!bIsPrivate && iTeamID)
		{
			QueueTeamJoinTime *pJoinTime = eaIndexedGetUsingInt(&s_eaTeamJoinTimes, iTeamID);
			if(pJoinTime)
			{
				uiJoinTimeOverride = pJoinTime->uiTime;
			}
		}
		
		pCBData->iEntID = iEntID;
		pCBData->iQueueID = pQueue->iContainerID;
		pCBData->iInstanceID = uiInstanceID;
		pCBData->iMapKey = iMapKey;
		pCBData->pParams = StructClone(parse_QueueInstanceParams, pParams);
		pCBData->pPrefs = StructClone(parse_QueueMemberPrefs,pPrefs);

		if(pPrefs && 
			!pPrefs->fCurrentRank && 
			pDef->MapRules.pMatchMakingRules && 
			pDef->MapRules.pMatchMakingRules->pchMatchMakingLeaderboard)
		{
			RankRequestBeforeQueue *pNewRequest = calloc(sizeof(RankRequestBeforeQueue),1);

			pNewRequest->iContainerID = pQueue->iContainerID;
			pNewRequest->iEntID = iEntID;
			pNewRequest->iTeamID = iTeamID;
			pNewRequest->iLevel = pMemberJoinCriteria->iLevel;
			pNewRequest->iRank = pMemberJoinCriteria->iRank;
			pNewRequest->pchAffiliation = strdup(pMemberJoinCriteria->pchAffiliation);
			// Request does not carry group role
			pNewRequest->pchPassword = strdup(pchPassword);
			pNewRequest->uiInstanceID = uiInstanceID;
			pNewRequest->iMapKey = iMapKey;
			pNewRequest->eJoinFlags = eJoinFlags;
			pNewRequest->pCBData = pCBData;

			aslQueue_GetLeaderboardRankBeforeQueue(pDef->MapRules.pMatchMakingRules->pchMatchMakingLeaderboard,pNewRequest);
		}
		else
		{
			pReturn = objCreateManagedReturnVal(aslQueue_QueueJoin_CB, pCBData);

			AutoTrans_aslQueue_tr_QueueJoin(pReturn, GetAppGlobalType(), 
				GLOBALTYPE_QUEUEINFO, pQueue->iContainerID, 
				iEntID, iTeamID,
				pMemberJoinCriteria,
				pchPassword, uiInstanceID, iMapKey, 
				eJoinFlags, pParams, NULL, uiJoinTimeOverride);
		}
		
	}
}

static void aslQueue_SendInitialInvites(QueueInfo* pQueue, 
										U32 uInstanceID, 
										U32 uInviterID,
										U32 uInviterAccountID,
										QueueIntArray* pInviteList)
{
	QueueInstance *pInstance = eaIndexedGetUsingInt(&pQueue->eaInstances, uInstanceID);
	if (pInstance)
	{
		S32 i;
		for (i = ea32Size(&pInviteList->piArray)-1; i >= 0; i--)
		{
			U32 uEntID = pInviteList->piArray[i];
			if (uEntID)
			{
				RemoteCommand_gslQueue_SendInvite(GLOBALTYPE_ENTITYPLAYER,
												  uEntID,
												  uEntID,
												  uInviterID,
												  uInviterAccountID,
												  pQueue->pchName,
												  pInstance->uiID,
												  pInstance->pParams,
												  NULL);
			}
		}
	}
}


static void aslQueue_QueueCreate_CB(TransactionReturnVal *pReturn, void *pData)
{
	QueueCBStruct *pCBData = (QueueCBStruct*)pData;
	QueueInfo *pQueue = aslQueue_GetQueueContainer(pCBData->iQueueID);
	char* pchResult = objAutoTransactionGetResult(pReturn);
	U32 uInstanceID; 

	if (!pchResult || strlen(pchResult) <= 4 || !StringToInt(pchResult+4, &uInstanceID))
	{
		uInstanceID = 0;
	}
	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		if(pQueue)
		{
			if (uInstanceID > 0)
			{
				aslQueue_Join_HandleChatChannels(pQueue, uInstanceID, pCBData->iEntID, pCBData->pParams);
			}
			log_printf(LOG_QUEUE,"Player [%d] created queue [%s].\n", pCBData->iEntID, pQueue->pchName);
			queue_SendResultMessage(pCBData->iEntID, 0, pQueue->pchName, QUEUE_CREATED_MESG);

			if (pCBData->pInviteList)
			{
				aslQueue_SendInitialInvites(pQueue, 
											uInstanceID, 
											pCBData->iEntID, 
											pCBData->iEntAccountID, 
											pCBData->pInviteList);
			}
		}
	}
	else
	{
		if(pQueue)
		{
			log_printf(LOG_QUEUE,"Player [%d] failed to create queue [%s].\n", pCBData->iEntID, pQueue->pchName);
			queue_SendResultMessage(pCBData->iEntID, 0, pQueue->pchName, QUEUE_CREATE_FAILED_MESG);
		}
	}

	StructDestroy(parse_QueueCBStruct, pCBData);
}


AUTO_TRANSACTION
ATR_LOCKS(pQueueContainer, ".Hdef, .Eainstances, .Uicurrentid");
enumTransactionOutcome aslQueue_tr_CreateInstance(ATR_ARGS,
												  NOCONST(QueueInfo) *pQueueContainer,
												  ContainerID iEntID,
												  U32 iTeamID,
												  S32 iLevel,
												  S32 iRank,
												  const char* pchAffiliation,
												  QueueInviteInfo* pInviteData,
												  QueueGameSettings* pSettings,
												  NON_CONTAINER QueueInstanceParams* pParams,
												  NON_CONTAINER QueueMemberPrefs *pPrefs)
{
	if(aslQueue_trh_CreateInstance(ATR_PASS_ARGS,
								   pQueueContainer,
								   iEntID,
								   iTeamID,
								   iLevel,
								   iRank,
								   pchAffiliation,
								   pInviteData,
								   pSettings,
								   pParams,
								   pPrefs))
	{	
		return(TRANSACTION_OUTCOME_SUCCESS);
	}
	else
	{
		return(TRANSACTION_OUTCOME_FAILURE);
	}
}

bool aslQueue_CanCreate(QueueInfo* pQueue, const char *pchQueueName, QueueInstanceParams* pParams, U32 iEntID)
{
	QueueDef* pDef;

	if (!pQueue || !(pDef = GET_REF(pQueue->hDef)))
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to create a queue instance for [%s] but it wasn't present.\n",
			iEntID,
			pchQueueName);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_FOUND_MESG);
		return false;
	}
	// are private queues disabled?
	if(g_QueueConfig.bDisablePrivateQueues && pParams && pParams->uiOwnerID != 0)
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to create a private queue instance for [%s] but private queues are disabled.\n",
			iEntID,
			pchQueueName);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_FOUND_MESG);
		return false;
	}
	// Check to see that an instance with pParams doesn't already exist
	if (queue_FindInstance(pQueue,pParams))
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to create a queue instance for [%s] but the specified params already exist.\n",
			iEntID,
			pchQueueName);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_CREATE_FAILED_MESG);
		return false;
	}
	// Is the player already in this queue?
	if (iEntID && queue_FindPlayer(pQueue, iEntID))
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to create a queue instance for [%s] but he/she was already in that queue.\n",
			iEntID,
			pchQueueName);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_ALREADY_IN_THIS_QUEUE_MESG);
		return false;
	}

	if (iEntID && aslQueue_IsPlayerInQueues(iEntID, NULL, g_QueueConfig.iMaxQueueCount))
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to create a queue instance for [%s] but he/she was already in the max number of queues allowed per player.\n",
			iEntID,
			pchQueueName);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_PLAYER_IN_MAX_COUNT_MESG);
		return false;
	}

	if (!SAFE_MEMBER(pParams, uiOwnerID) && aslQueue_GetPlayerPenaltyData(iEntID, pDef->eCategory, NULL))
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to create a queue for [%s] but the player is temp banned from creating queues.\n",
			iEntID,
			pchQueueName);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_PLAYER_PENALIZED_MESG);
		return false;
	}

	if (pParams && !aslQueue_trh_IsPrivateNameUnique(CONTAINER_NOCONST(QueueInfo, pQueue), pParams->pchPrivateName))
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to create a queue for [%s] but the private game name is already in use.\n",
			iEntID,
			pchQueueName);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_PRIVATE_NAME_IN_USE);
		return false;
	}
	return true;
}


AUTO_COMMAND_REMOTE;
void aslQueue_Create(ContainerID iEntID,
					 U32 uEntAccountID,
					 ContainerID iTeamID,
					 S32 iLevel,
					 S32 iRank,
					 const char* pchAffiliation,
					 const char* pchQueueName,
					 QueueInstanceParams* pParams,
					 QueueInviteInfo* pChallengeData,
					 QueueIntArray* pInviteList,
					 QueueGameSettings* pSettings)
{
	QueueInfo* pQueue = aslQueue_FindQueueByNameLocal(pchQueueName);

	if(!aslQueue_CanCreate(pQueue,pchQueueName,pParams,iEntID))
		return;

	{
		QueueCBStruct *pCBData = StructCreate(parse_QueueCBStruct);
		TransactionReturnVal *pReturn = objCreateManagedReturnVal(aslQueue_QueueCreate_CB, pCBData);
		
		pCBData->iEntID = iEntID;
		pCBData->iEntAccountID = uEntAccountID;
		pCBData->iQueueID = pQueue->iContainerID;
		pCBData->pInviteList = StructClone(parse_QueueIntArray, pInviteList);
		pCBData->pParams = StructClone(parse_QueueInstanceParams, pParams);

		AutoTrans_aslQueue_tr_CreateInstance(pReturn, GetAppGlobalType(), GLOBALTYPE_QUEUEINFO, pQueue->iContainerID, iEntID, iTeamID, iLevel, iRank, pchAffiliation, pChallengeData, pSettings, pParams, NULL);
	}
}

static void aslQueue_QueueInvite_CB(TransactionReturnVal *pReturn, void *pData)
{
	QueueCBStruct *pCBData = (QueueCBStruct*)pData;
	QueueInfo *pQueue = aslQueue_GetQueueContainer(pCBData->iQueueID);

	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		if(pQueue)
		{
			log_printf(LOG_QUEUE,"Player [%d] invited [%d] to queue instance [%s][%d].\n", pCBData->iEntID, pCBData->iSubjectID1, pQueue->pchName, pCBData->iInstanceID);
			queue_SendResultMessage(pCBData->iEntID, pCBData->iSubjectID1, pQueue->pchName, QUEUE_INVITED_MESG);
		}
	}
	else
	{
		if(pQueue)
		{
			log_printf(LOG_QUEUE,"Player [%d] failed to invite [%d] to queue instance [%s][%d].\n", pCBData->iEntID, pCBData->iSubjectID1, pQueue->pchName, pCBData->iInstanceID);
			queue_SendResultMessage(pCBData->iEntID, pCBData->iSubjectID1, pQueue->pchName, QUEUE_INVITE_FAILED_MESG);
		}
	}

	StructDestroy(parse_QueueCBStruct, pCBData);
}

AUTO_TRANSACTION
ATR_LOCKS(pQueueContainer, ".Hdef, .Eainstances");
enumTransactionOutcome aslQueue_tr_QueueInvite(ATR_ARGS,
											   NOCONST(QueueInfo) *pQueueContainer,
											   U32 uiInstanceID,
											   U32 uiEntID,
											   U32 uiInviteeID,
											   U32 uiTeamID,
											   S32 iLevel,
											   S32 iRank,
											   const char* pchAffiliation)
{
	if(aslQueue_trh_Invite(ATR_PASS_ARGS,
						   pQueueContainer,
						   uiInstanceID,
						   uiEntID,
						   uiInviteeID,
						   uiTeamID,
						   iLevel,
						   iRank,
						   pchAffiliation))
	{	
		return(TRANSACTION_OUTCOME_SUCCESS);
	}
	else
	{
		return(TRANSACTION_OUTCOME_FAILURE);
	}
}

AUTO_COMMAND_REMOTE;
void aslQueue_Invite(ContainerID iEntID,
					 const char* pchQueueName,
					 U32 uiInstanceID,
					 U32 uiInviteeID,
					 U32 uiTeamID,
					 S32 iLevel,
					 S32 iRank,
					 const char* pchAffiliation)
{
	QueueInfo* pQueue;
	QueueDef* pDef;
	QueueInstance* pInstance;
	QueueMember* pMember;
	QueueMatch QMatch = {0};
	bool bMatchFailed = false;

	if (!(pQueue = aslQueue_FindQueueByNameLocal(pchQueueName)) || !(pDef = GET_REF(pQueue->hDef)))
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to invite [%d] to a private queue of type [%s], but it wasn't present.\n",
							iEntID,
							uiInviteeID,
							pchQueueName);
		queue_SendResultMessage(iEntID, uiInviteeID, pchQueueName, QUEUE_NOT_FOUND_MESG);
		return;
	}
	if (!(pInstance = eaIndexedGetUsingInt(&pQueue->eaInstances, uiInstanceID)) || !pInstance->pParams || pInstance->pParams->uiOwnerID == 0)
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to invite [%d] to a private queue of type [%s], but the instance [%d] was not found.\n",
							iEntID,
							uiInviteeID,
							pchQueueName,
							uiInstanceID);
		queue_SendResultMessage(iEntID, uiInviteeID, pchQueueName, QUEUE_NOT_FOUND_MESG);
		return;
	}

	if (eaSize(&pInstance->eaNewMaps) > 0 || aslQueue_SomeMapCouldAcceptMembers(pInstance, pDef))
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to invite [%d] to a private queue [%s][%d], but there was already an active map.\n",
							iEntID,
							uiInviteeID,
							pchQueueName,
							uiInstanceID);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_MAP_ACTIVE_MESG);
		return;
	}
	// The inviter must be in this instance and cannot be in the invited state themselves
	if (!(pMember = queue_FindPlayerInInstance(pInstance, iEntID)) || pMember->eState == PlayerQueueState_Invited)
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to invite [%d] to a private queue instance [%d] of type [%s], but is not a member.\n",
							iEntID,
							uiInviteeID,
							uiInstanceID,
							pchQueueName);
		queue_SendResultMessage(iEntID, uiInviteeID, pchQueueName, QUEUE_NOT_MEMBER_MESG);
		return;
	}
	
	aslQueue_InitInviteDisallowStates();
	if (aslQueue_IsPlayerInQueues(uiInviteeID, s_piInviteDisallowStates, 0))
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to invite [%d] to a private queue instance [%d] of type [%s], but the invitee is active in an instance.\n",
							iEntID,
							uiInviteeID,
							uiInstanceID,
							pchQueueName);
		queue_SendResultMessage(iEntID, uiInviteeID, pchQueueName, QUEUE_INVITEE_ACTIVE_MESG);
		return;
	}
	// Check to see if there is a valid group for the invited member (this is private queue management)
	queue_GroupCache(pInstance, pDef, NULL, &QMatch);
	if (queue_GetBestGroupIndexForPlayer(pchAffiliation, uiTeamID, -1, false, pDef, &QMatch) < 0)
	{
		bMatchFailed = true;
	}
	StructDeInit(parse_QueueMatch, &QMatch);
	
	if (bMatchFailed)
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to invite [%d] to a private queue instance [%d] of type [%s], but there was no valid group for the player.\n",
							iEntID,
							uiInviteeID,
							uiInstanceID,
							pchQueueName);
		queue_SendResultMessage(iEntID, uiInviteeID, pchQueueName, QUEUE_INVALID_GROUP_MESG);
		return;
	}

	{
		QueueCBStruct *pCBData = StructCreate(parse_QueueCBStruct);
		TransactionReturnVal *pReturn = objCreateManagedReturnVal(aslQueue_QueueInvite_CB, pCBData);
		
		pCBData->iEntID = iEntID;
		pCBData->iQueueID = pQueue->iContainerID;
		pCBData->iInstanceID = pInstance->uiID;
		pCBData->iSubjectID1 = uiInviteeID;

		AutoTrans_aslQueue_tr_QueueInvite(pReturn, GetAppGlobalType(), GLOBALTYPE_QUEUEINFO, pQueue->iContainerID, uiInstanceID, iEntID, uiInviteeID, uiTeamID, iLevel, iRank, pchAffiliation);
	}
}

static void aslQueue_Leave_HandleChatChannels(QueueInfo *pQueue, ContainerID uInstanceID, ContainerID uEntID)
{
	QueueInstance *pInstance = eaIndexedGetUsingInt(&pQueue->eaInstances, uInstanceID);
	U32 uOrigOwnerID = queue_GetOriginalOwnerID(pInstance);

	if (uOrigOwnerID > 0)
	{
		RemoteCommand_gslQueue_LeavePrivateChatChannel(GLOBALTYPE_ENTITYPLAYER, uEntID, uEntID, uOrigOwnerID, uInstanceID);
	}
}

static void aslQueue_Leave_HandleChatMessages(QueueInfo* pQueue, 
											  TransactionReturnVal* pReturn, 
											  QueueCBStruct* pCBData)
{
	QueueInstance* pInstance = eaIndexedGetUsingInt(&pQueue->eaInstances, pCBData->iInstanceID);
	QueueMap* pMap = pInstance ? eaGet(&pInstance->eaMaps, 0) : NULL;
	U32 uNewOwnerID = SAFE_MEMBER2(pInstance, pParams, uiOwnerID);
	U32 uOrigOwnerID = queue_GetOriginalOwnerID(pInstance);
	U32 uEntID = pCBData->iSubjectID1 ? pCBData->iSubjectID1 : pCBData->iEntID;
	char* pchResult = objAutoTransactionGetResult(pReturn);

	if (uOrigOwnerID && (!pMap || pMap->eMapState != kQueueMapState_Active))
	{
		// Send a message informing other players that the kicked member has left the game
		aslQueue_SendPrivateChatMessageToMembers(pQueue, 
												 pCBData->iInstanceID, 
												 QUEUE_PLAYER_LEFT_PRIVATE_GAME_MESG,
												 uEntID, 
												 uEntID,
												 -1);
	}
	if (uOrigOwnerID && pchResult)
	{
		ANALYSIS_ASSUME(pchResult != NULL);
		if ((strlen(pchResult) > 4) && stricmp(pchResult+4, QUEUE_CANCEL_LAUNCH_STRING)==0)
		{
			// Send a message to the private channel informing players that the map launch was cancelled
			aslQueue_SendPrivateChatMessageToMembers(pQueue, 
													 pCBData->iInstanceID, 
													 QUEUE_CANCEL_MAP_LAUNCH_MESG,
													 0, 
													 0,
													 -1);
		}
	}
	if (uNewOwnerID && pCBData->iOwnerID && uNewOwnerID != pCBData->iOwnerID)
	{
		// Send a message to the private channel informing players that the host has been changed
		aslQueue_SendPrivateChatMessageToMembers(pQueue, 
												 pCBData->iInstanceID, 
												 QUEUE_MIGRATE_PRIVATE_OWNER_MESG,
												 uNewOwnerID, 
												 0,
												 -1);
	}
}

static void aslQueue_QueueKick_CB(TransactionReturnVal *pReturn, void *pData)
{
	QueueCBStruct *pCBData = (QueueCBStruct*)pData;
	QueueInfo *pQueue = aslQueue_GetQueueContainer(pCBData->iQueueID);

	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		if(pQueue)
		{
			aslQueue_Leave_HandleChatMessages(pQueue, pReturn, pCBData);
			aslQueue_Leave_HandleChatChannels(pQueue, pCBData->iInstanceID, pCBData->iSubjectID1);
		}
		if (pQueue && pCBData->iEntID)
		{
			log_printf(LOG_QUEUE,"Player [%d] kicked [%d] from queue instance [%s][%d].\n", pCBData->iEntID, pCBData->iSubjectID1, pQueue->pchName, pCBData->iInstanceID);
			queue_SendResultMessage(pCBData->iEntID, pCBData->iSubjectID1, pQueue->pchName, QUEUE_KICKED_MESG);
		}
	}
	else
	{
		if(pQueue && pCBData->iEntID)
		{
			log_printf(LOG_QUEUE,"Player [%d] failed to kick [%d] from queue instance [%s][%d].\n", pCBData->iEntID, pCBData->iSubjectID1, pQueue->pchName, pCBData->iInstanceID);
			queue_SendResultMessage(pCBData->iEntID, pCBData->iSubjectID1, pQueue->pchName, QUEUE_KICK_FAILED_MESG);
		}
	}

	StructDestroy(parse_QueueCBStruct, pCBData);
}

AUTO_TRANSACTION
ATR_LOCKS(pQueueContainer, "eaInstances[], .Hdef");
enumTransactionOutcome aslQueue_tr_QueueKick(ATR_ARGS, NOCONST(QueueInfo) *pQueueContainer, U32 uiInstanceID, U32 uiEntID, U32 uiMemberID)
{
	if(aslQueue_trh_Kick(ATR_PASS_ARGS, pQueueContainer, uiInstanceID, uiEntID, uiMemberID))
	{	
		return(TRANSACTION_OUTCOME_SUCCESS);
	}
	else
	{
		return(TRANSACTION_OUTCOME_FAILURE);
	}
}

// If iEntID is 0 and uiMapID is valid, then this function assumes this is a vote-kick
AUTO_COMMAND_REMOTE;
void aslQueue_Kick(ContainerID iEntID, const char* pchQueueName, U32 uiInstanceID, S64 iMapKey, U32 uiMemberID)
{
	QueueInfo* pQueue;
	QueueDef* pDef;
	QueueInstance* pInstance;

	devassert((!iEntID && iMapKey) || (iEntID && uiInstanceID));

	if (!(pQueue = aslQueue_FindQueueByNameLocal(pchQueueName)) || !(pDef = GET_REF(pQueue->hDef)))
	{
		if (iEntID)
		{
			log_printf(LOG_QUEUE, "EntID [%d] tried to kick [%d] from a private queue of type [%s], but it wasn't present.\n",
								iEntID,
								uiMemberID,
								pchQueueName);
			queue_SendResultMessage(iEntID, uiMemberID, pchQueueName, QUEUE_NOT_FOUND_MESG);
		}
		return;
	}

	if (iEntID)
	{
		if (!(pInstance = eaIndexedGetUsingInt(&pQueue->eaInstances, uiInstanceID)) || !pInstance->pParams || pInstance->pParams->uiOwnerID != iEntID)
		{
			log_printf(LOG_QUEUE, "EntID [%d] tried to kick [%d] from a private queue of type [%s], but the instance [%d] was not found.\n",
								iEntID,
								uiMemberID,
								pchQueueName,
								uiInstanceID);
			queue_SendResultMessage(iEntID, uiMemberID, pchQueueName, QUEUE_NOT_FOUND_MESG);
			return;
		}
	}
	else if (iMapKey)
	{
		QueueMap* pMap = aslQueue_FindMap(pQueue,iMapKey,&pInstance);
		if (!pMap)
		{
			return;
		}
	}
	else
	{
		return;
	}

	if (iEntID && (eaSize(&pInstance->eaNewMaps) > 0 || aslQueue_SomeMapCouldAcceptMembers(pInstance, pDef)))
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to kick member [%d] in queue [%s][%d], but there was already an active map.\n",
							iEntID,
							uiMemberID,
							pchQueueName,
							uiInstanceID);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_MAP_ACTIVE_MESG);
		return;
	}

	if (iEntID == uiMemberID || (iEntID && !queue_FindPlayerInInstance(pInstance, iEntID)) || !queue_FindPlayerInInstance(pInstance, uiMemberID))
	{
		if (iEntID)
		{
			log_printf(LOG_QUEUE, "EntID [%d] tried to kick [%d] (an invalid user) from a private queue instance [%s][%d].\n",
								iEntID,
								uiMemberID,
								pchQueueName,
								uiInstanceID);
			queue_SendResultMessage(iEntID, uiMemberID, pchQueueName, QUEUE_NOT_MEMBER_MESG);
		}
		return;
	}

	{
		QueueCBStruct *pCBData = StructCreate(parse_QueueCBStruct);
		TransactionReturnVal *pReturn = objCreateManagedReturnVal(aslQueue_QueueKick_CB, pCBData);
		
		pCBData->iEntID = iEntID;
		pCBData->iQueueID = pQueue->iContainerID;
		pCBData->iInstanceID = pInstance->uiID;
		pCBData->iSubjectID1 = uiMemberID;
		pCBData->iMapKey = iMapKey;

		AutoTrans_aslQueue_tr_QueueKick(pReturn, GetAppGlobalType(), GLOBALTYPE_QUEUEINFO, pQueue->iContainerID, uiInstanceID, iEntID, uiMemberID);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// Change groups in a private queue
///////////////////////////////////////////////////////////////////////////////////////////

static void aslQueue_ChangeGroup_CB(TransactionReturnVal *pReturn, void *pData)
{
	QueueCBStruct *pCBData = (QueueCBStruct*)pData;
	QueueInfo *pQueue = aslQueue_GetQueueContainer(pCBData->iQueueID);

	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		if (pQueue)
		{
			log_printf(LOG_QUEUE,"Player [%d] changed group in queue instance [%s][%d].\n", pCBData->iEntID, pQueue->pchName, pCBData->iInstanceID);
			queue_SendResultMessage(pCBData->iEntID, 0, pQueue->pchName, QUEUE_CHANGED_GROUP_MESG);
		}
	}
	else
	{
		if (pQueue)
		{
			log_printf(LOG_QUEUE,"Player [%d] failed to change group in queue instance [%s][%d].\n", pCBData->iEntID, pQueue->pchName, pCBData->iInstanceID);
			queue_SendResultMessage(pCBData->iEntID, 0, pQueue->pchName, QUEUE_CHANGE_GROUP_FAILED_MESG);
		}
	}

	StructDestroy(parse_QueueCBStruct, pCBData);
}

AUTO_TRANSACTION
ATR_LOCKS(pQueueContainer, ".Hdef, eaInstances[]");
enumTransactionOutcome aslQueue_tr_ChangeGroup(ATR_ARGS, NOCONST(QueueInfo) *pQueueContainer, U32 uiInstanceID, U32 uiEntID, S32 iGroupID)
{
	if (aslQueue_trh_ChangeGroup(ATR_PASS_ARGS, pQueueContainer, uiInstanceID, uiEntID, iGroupID))
	{	
		return TRANSACTION_OUTCOME_SUCCESS;
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_COMMAND_REMOTE;
void aslQueue_ChangeGroup(ContainerID iEntID, ContainerID iSubjectID, const char* pchQueueName, U32 uiInstanceID, S32 iGroupID)
{
	QueueInfo* pQueue;
	QueueDef* pDef;
	QueueInstance* pInstance;
	QueueMember* pSubject;
	QueueMember* pMember;
	QueueMatch QMatch = {0};
	bool bMatchFailed = false;

	if (!(pQueue = aslQueue_FindQueueByNameLocal(pchQueueName)) || !(pDef = GET_REF(pQueue->hDef)))
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to change group of [%d] to [%d] in a private queue of type [%s], but it wasn't present.\n",
							iEntID,
							iSubjectID,
							iGroupID,
							pchQueueName);
		queue_SendResultMessage(iEntID, iSubjectID, pchQueueName, QUEUE_NOT_FOUND_MESG);
		return;
	}
	if (!(pInstance = eaIndexedGetUsingInt(&pQueue->eaInstances, uiInstanceID)) || !pInstance->pParams || pInstance->pParams->uiOwnerID == 0)
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to change group of [%d] to [%d] in a private queue of type [%s], but the instance [%d] was not found.\n",
							iEntID,
							iSubjectID,
							iGroupID,
							pchQueueName,
							uiInstanceID);
		queue_SendResultMessage(iEntID, iSubjectID, pchQueueName, QUEUE_NOT_FOUND_MESG);
		return;
	}
	if (eaSize(&pInstance->eaNewMaps) > 0 || aslQueue_SomeMapCouldAcceptMembers(pInstance, pDef))
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to change groups in queue [%s][%d], but there was already an active map.\n",
							iEntID,
							pchQueueName,
							uiInstanceID);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_MAP_ACTIVE_MESG);
		return;
	}

	if (	!(pMember = queue_FindPlayerInInstance(pInstance,iEntID))
		||	!(pSubject = queue_FindPlayerInInstance(pInstance,iSubjectID))
		|| (iEntID != iSubjectID && pInstance->pParams->uiOwnerID != iEntID))
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to change group of [%d] to [%d] in a private queue [%s][%d], but is not a member.\n",
							iEntID,
							iSubjectID,
							iGroupID,
							pchQueueName,
							uiInstanceID);
		queue_SendResultMessage(iEntID, iSubjectID, pchQueueName, QUEUE_NOT_MEMBER_MESG);
		return;
	}

	//Check to see if the subject can change to the desired group
	queue_GroupCache(pInstance, pDef, NULL, &QMatch);
	if (	pSubject->iGroupIndex == iGroupID 
		|| !queue_IsValidGroupIndexForPlayer(iGroupID, pSubject->pchAffiliation, false, pDef, &QMatch))
	{
		bMatchFailed = true;
	}

	if (bMatchFailed)
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to change group of [%d] to [%d] in a private queue [%s][%d], but the group was not valid.\n",
							iEntID,
							iSubjectID,
							iGroupID,
							pchQueueName,
							uiInstanceID);
		queue_SendResultMessage(iEntID, iSubjectID, pchQueueName, QUEUE_INVALID_GROUP_MESG);
		return;
	}

	{
		QueueCBStruct *pCBData = StructCreate(parse_QueueCBStruct);
		TransactionReturnVal *pReturn = objCreateManagedReturnVal(aslQueue_ChangeGroup_CB, pCBData);
		
		pCBData->iEntID = iEntID;
		pCBData->iQueueID = pQueue->iContainerID;
		pCBData->iInstanceID = pInstance->uiID;
		pCBData->iSubjectID1 = iEntID;
		AutoTrans_aslQueue_tr_ChangeGroup(pReturn, GetAppGlobalType(), GLOBALTYPE_QUEUEINFO, pQueue->iContainerID, uiInstanceID, iSubjectID, iGroupID);
	}
}

static void aslQueue_SwapGroups_CB(TransactionReturnVal *pReturn, void *pData)
{
	QueueCBStruct *pCBData = (QueueCBStruct*)pData;
	QueueInfo *pQueue = aslQueue_GetQueueContainer(pCBData->iQueueID);

	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		if (pQueue)
		{
			log_printf(LOG_QUEUE,"Player [%d] swapped groups of members [%d] and [%d] in queue instance [%s][%d].\n", pCBData->iEntID, pCBData->iSubjectID1, pCBData->iSubjectID2, pQueue->pchName, pCBData->iInstanceID);
			queue_SendResultMessage(pCBData->iEntID, 0, pQueue->pchName, QUEUE_CHANGED_GROUP_MESG);
		}
	}
	else
	{
		if (pQueue)
		{
			log_printf(LOG_QUEUE,"Player [%d] failed to swap groups of members [%d] and [%d] in queue instance [%s][%d].\n", pCBData->iEntID, pCBData->iSubjectID1, pCBData->iSubjectID2, pQueue->pchName, pCBData->iInstanceID);
			queue_SendResultMessage(pCBData->iEntID, 0, pQueue->pchName, QUEUE_CHANGE_GROUP_FAILED_MESG);
		}
	}

	StructDestroy(parse_QueueCBStruct, pCBData);
}

AUTO_TRANSACTION
ATR_LOCKS(pQueueContainer, ".Hdef, eaInstances[]");
enumTransactionOutcome aslQueue_tr_SwapGroups(ATR_ARGS, NOCONST(QueueInfo) *pQueueContainer, U32 uiInstanceID, U32 uiEntID, U32 uiMemberID1, U32 uiMemberID2)
{
	NOCONST(QueueInstance)* pInstance = eaIndexedGetUsingInt(&pQueueContainer->eaInstances, uiInstanceID);
	NOCONST(QueueMember)* pMember1 = queue_trh_FindPlayerInInstance(pInstance, uiMemberID1);
	NOCONST(QueueMember)* pMember2 = queue_trh_FindPlayerInInstance(pInstance, uiMemberID2);

	if (	NONNULL(pInstance) && NONNULL(pInstance->pParams) && pInstance->pParams->uiOwnerID == uiEntID
		&&	NONNULL(pMember1) && NONNULL(pMember2) && pMember1->iGroupIndex != pMember2->iGroupIndex
		&&	aslQueue_trh_ChangeGroup(ATR_PASS_ARGS, pQueueContainer, uiInstanceID, uiMemberID1, pMember2->iGroupIndex)
		&&	aslQueue_trh_ChangeGroup(ATR_PASS_ARGS, pQueueContainer, uiInstanceID, uiMemberID2, pMember1->iGroupIndex))
	{
		return TRANSACTION_OUTCOME_SUCCESS;
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_COMMAND_REMOTE;
void aslQueue_SwapGroups(ContainerID iEntID, const char* pchQueueName, U32 uiInstanceID, U32 uiMemberID1, U32 uiMemberID2)
{
	QueueInfo* pQueue;
	QueueDef* pDef;
	QueueInstance* pInstance;
	QueueMember* pMember1;
	QueueMember* pMember2;
	QueueMatch QMatch = {0};
	bool bMatchFailed = false;

	if (!(pQueue = aslQueue_FindQueueByNameLocal(pchQueueName)) || !(pDef = GET_REF(pQueue->hDef)))
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to swap group of members [%d] and [%d] in a private queue of type [%s], but it wasn't present.\n",
							iEntID,
							uiMemberID1,
							uiMemberID2,
							pchQueueName);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_FOUND_MESG);
		return;
	}
	if (!(pInstance = eaIndexedGetUsingInt(&pQueue->eaInstances, uiInstanceID)) || !pInstance->pParams || pInstance->pParams->uiOwnerID != iEntID)
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to swap group of members [%d] and [%d] in a private queue of type [%s], but the instance [%d] was not found.\n",
							iEntID,
							uiMemberID1,
							uiMemberID2,
							pchQueueName,
							uiInstanceID);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_FOUND_MESG);
		return;
	}
	if (	!(pMember1 = queue_FindPlayerInInstance(pInstance,uiMemberID1)) 
		||	!(pMember2 = queue_FindPlayerInInstance(pInstance,uiMemberID2)) 
		||	pMember1->iGroupIndex == pMember2->iGroupIndex)
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to swap group of members [%d] and [%d] in a private queue [%s][%d], but one or both of the members were invalid.\n",
							iEntID,
							uiMemberID1,
							uiMemberID2,
							pchQueueName,
							uiInstanceID);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_MEMBER_MESG);
		return;
	}

	if (eaSize(&pInstance->eaNewMaps) > 0 || aslQueue_SomeMapCouldAcceptMembers(pInstance, pDef))
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to swap groups in queue [%s][%d], but there was already an active map.\n",
							iEntID,
							pchQueueName,
							uiInstanceID);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_MAP_ACTIVE_MESG);
		return;
	}

	//Check to see if the two members satisfy requirements for the groups they are trying to move to
	queue_GroupCache(pInstance, pDef, NULL, &QMatch);
	if (	!queue_IsValidGroupIndexForPlayer(pMember2->iGroupIndex, pMember1->pchAffiliation, false, pDef, &QMatch)
		||	!queue_IsValidGroupIndexForPlayer(pMember1->iGroupIndex, pMember2->pchAffiliation, false, pDef, &QMatch))
	{
		bMatchFailed = true;
	}
	StructDeInit(parse_QueueMatch, &QMatch);

	if (bMatchFailed)
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to swap group of members [%d] and [%d] in a private queue [%s][%d], but groups weren't valid for the members.\n",
							iEntID,
							uiMemberID1,
							uiMemberID2,
							pchQueueName,
							uiInstanceID);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_INVALID_GROUP_MESG);
		return;
	}

	{
		QueueCBStruct *pCBData = StructCreate(parse_QueueCBStruct);
		TransactionReturnVal *pReturn = objCreateManagedReturnVal(aslQueue_SwapGroups_CB, pCBData);
		
		pCBData->iEntID = iEntID;
		pCBData->iQueueID = pQueue->iContainerID;
		pCBData->iInstanceID = pInstance->uiID;
		pCBData->iSubjectID1 = uiMemberID1;
		pCBData->iSubjectID2 = uiMemberID2;
		AutoTrans_aslQueue_tr_SwapGroups(pReturn, GetAppGlobalType(), GLOBALTYPE_QUEUEINFO, pQueue->iContainerID, uiInstanceID, iEntID, uiMemberID1, uiMemberID2);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// Change a map for a private game
///////////////////////////////////////////////////////////////////////////////////////////

static void aslQueue_ChangeMap_CB(TransactionReturnVal *pReturn, void *pData)
{
	QueueCBStruct *pCBData = (QueueCBStruct*)pData;
	QueueInfo *pQueue = aslQueue_GetQueueContainer(pCBData->iQueueID);

	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		if(pQueue)
		{
			log_printf(LOG_QUEUE,"Player [%d] changed map to [%s] in queue instance [%s][%d].\n", pCBData->iEntID, pCBData->pchMapName, pQueue->pchName, pCBData->iInstanceID);
			queue_SendResultMessage(pCBData->iEntID, 0, pQueue->pchName, QUEUE_MAP_CHANGED_MESG);
		}
	}
	else
	{
		if(pQueue)
		{
			log_printf(LOG_QUEUE,"Player [%d] failed to change map to [%s] in queue instance [%s][%d].\n", pCBData->iEntID, pCBData->pchMapName, pQueue->pchName, pCBData->iInstanceID);
			queue_SendResultMessage(pCBData->iEntID, 0, pQueue->pchName, QUEUE_MAP_CHANGE_FAILED_MESG);
		}
	}
	StructDestroy(parse_QueueCBStruct, pCBData);
}

AUTO_TRANSACTION
ATR_LOCKS(pQueueContainer, "eaInstances[]");
enumTransactionOutcome aslQueue_tr_ChangeMap(ATR_ARGS, NOCONST(QueueInfo) *pQueueContainer, U32 uiInstanceID, U32 uiEntID, const char* pchMapName)
{
	NOCONST(QueueInstance)* pInstance = eaIndexedGetUsingInt(&pQueueContainer->eaInstances, uiInstanceID);
	NOCONST(QueueMember)* pMember = queue_trh_FindPlayerInInstance(pInstance, uiEntID);

	if (ISNULL(pMember) || ISNULL(pInstance->pParams) || uiEntID != pInstance->pParams->uiOwnerID)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	pInstance->pParams->pchMapName = allocAddString(pchMapName);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_COMMAND_REMOTE;
void aslQueue_ChangeMap(ContainerID iEntID, const char* pchQueueName, U32 uiInstanceID, const char* pchMapName)
{
	QueueInfo* pQueue;
	QueueDef* pDef;
	QueueInstance* pInstance;

	if (!(pQueue = aslQueue_FindQueueByNameLocal(pchQueueName)) || !(pDef = GET_REF(pQueue->hDef)))
	{
		log_printf(LOG_QUEUE, 
			"EntID [%d] tried to change the map in queue [%s], but it wasn't present.\n",
			iEntID,
			pchQueueName);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_FOUND_MESG);
		return;
	}
	if (!(pInstance = eaIndexedGetUsingInt(&pQueue->eaInstances, uiInstanceID)) || 
		!pInstance->pParams || pInstance->pParams->uiOwnerID != iEntID)
	{
		log_printf(LOG_QUEUE, 
			"EntID [%d] tried to change the map in queue [%s], but the instance [%d] was not found.\n",
			iEntID,
			pchQueueName,
			uiInstanceID);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_FOUND_MESG);
		return;
	}
	// Fail if the instance's map is already the desired map
	if (stricmp(pInstance->pParams->pchMapName, pchMapName) == 0)
	{
		return;
	}
	if (eaSize(&pInstance->eaNewMaps) > 0 || aslQueue_SomeMapCouldAcceptMembers(pInstance, pDef))
	{
		log_printf(LOG_QUEUE, 
			"EntID [%d] tried to change the map in queue [%s][%d], but there was already an active map.\n",
			iEntID,
			pchQueueName,
			uiInstanceID);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_MAP_ACTIVE_MESG);
		return;
	}

	{
		QueueCBStruct *pCBData = StructCreate(parse_QueueCBStruct);
		TransactionReturnVal *pReturn = objCreateManagedReturnVal(aslQueue_ChangeMap_CB, pCBData);

		pCBData->iEntID = iEntID;
		pCBData->iQueueID = pQueue->iContainerID;
		pCBData->iInstanceID = pInstance->uiID;
		pCBData->pchMapName = allocAddString(pchMapName);

		AutoTrans_aslQueue_tr_ChangeMap(pReturn, GetAppGlobalType(), GLOBALTYPE_QUEUEINFO, pQueue->iContainerID, uiInstanceID, iEntID, pchMapName);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// Change a private game setting
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_TRANS_HELPER
ATR_LOCKS(pInstance, ".Easettings");
NOCONST(QueueGameSetting)* aslQueue_trh_FindSetting(ATH_ARG NOCONST(QueueInstance)* pInstance, 
													const char* pchQueueVar)
{
	S32 iSettingIdx;
	for (iSettingIdx = eaSize(&pInstance->eaSettings)-1; iSettingIdx >= 0; iSettingIdx--)
	{
		NOCONST(QueueGameSetting)* pSetting = pInstance->eaSettings[iSettingIdx];
		if (stricmp(pSetting->pchQueueVarName, pchQueueVar)==0)
		{
			return pSetting;
		}
	}
	return NULL;
}

static void aslQueue_ChangeSetting_CB(TransactionReturnVal* pReturn, void *pData)
{
	QueueCBStruct *pCBData = (QueueCBStruct*)pData;
	QueueInfo *pQueue = aslQueue_GetQueueContainer(pCBData->iQueueID);

	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		if(pQueue)
		{
			log_printf(LOG_QUEUE,"Player [%d] changed setting (%s) to [%d] in queue instance [%s][%d].\n", 
				pCBData->iEntID, pCBData->pchQueueVar, pCBData->iQueueVarVal, pQueue->pchName, pCBData->iInstanceID);
			queue_SendResultMessage(pCBData->iEntID, 0, pQueue->pchName, QUEUE_MAP_SETTING_CHANGED_MESG);
		}
	}
	else
	{
		if(pQueue)
		{
			log_printf(LOG_QUEUE,"Player [%d] failed to change setting (%s) to [%d] in queue instance [%s][%d].\n", 
				pCBData->iEntID, pCBData->pchQueueVar, pCBData->iQueueVarVal, pQueue->pchName, pCBData->iInstanceID);
			queue_SendResultMessage(pCBData->iEntID, 0, pQueue->pchName, QUEUE_MAP_SETTING_CHANGE_FAILED_MESG);
		}
	}
	StructDestroy(parse_QueueCBStruct, pCBData);
}

AUTO_TRANSACTION
ATR_LOCKS(pQueueContainer, "eaInstances[]");
enumTransactionOutcome aslQueue_tr_ChangeSetting(ATR_ARGS, 
												 NOCONST(QueueInfo)* pQueueContainer, 
												 U32 uiInstanceID, 
												 U32 uiEntID, 
												 const char* pchQueueVar,
												 S32 iQueueVarValue)
{
	NOCONST(QueueInstance)* pInstance = eaIndexedGetUsingInt(&pQueueContainer->eaInstances, uiInstanceID);
	NOCONST(QueueMember)* pMember = queue_trh_FindPlayerInInstance(pInstance, uiEntID);
	NOCONST(QueueGameSetting)* pSetting;

	if (ISNULL(pMember) || ISNULL(pInstance->pParams) || uiEntID != pInstance->pParams->uiOwnerID)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}
	pSetting = aslQueue_trh_FindSetting(pInstance, pchQueueVar);

	if (NONNULL(pSetting) && pSetting->iValue == iQueueVarValue)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}
	if (ISNULL(pSetting))
	{
		pSetting = StructCreateNoConst(parse_QueueGameSetting);
		eaPush(&pInstance->eaSettings, pSetting);
	}
	pSetting->pchQueueVarName = StructAllocString(pchQueueVar);
	pSetting->iValue = iQueueVarValue;
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_COMMAND_REMOTE;
void aslQueue_ChangeSetting(ContainerID iEntID, const char* pchQueueName, U32 uiInstanceID,
							const char* pchQueueVar, S32 iValue)
{
	QueueInfo* pQueue;
	QueueDef* pDef;
	QueueInstance* pInstance;
	QueueGameSetting* pSetting;

	if (!(pQueue = aslQueue_FindQueueByNameLocal(pchQueueName)) || !(pDef = GET_REF(pQueue->hDef)))
	{
		log_printf(LOG_QUEUE, 
			"EntID [%d] tried to change setting [%s] to [%d] in queue [%s], but it wasn't present.\n",
			iEntID,
			pchQueueVar,
			iValue,
			pchQueueName);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_FOUND_MESG);
		return;
	}
	if (!(pInstance = eaIndexedGetUsingInt(&pQueue->eaInstances, uiInstanceID)) || 
		!pInstance->pParams || pInstance->pParams->uiOwnerID != iEntID)
	{
		log_printf(LOG_QUEUE, 
			"EntID [%d] tried to change setting [%s] to [%d] in queue [%s], but the instance [%d] was not found.\n",
			iEntID,
			pchQueueVar,
			iValue,
			pchQueueName,
			uiInstanceID);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_FOUND_MESG);
		return;
	}
	if (eaSize(&pInstance->eaNewMaps) > 0 || aslQueue_SomeMapCouldAcceptMembers(pInstance, pDef))
	{
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_CHANGE_SETTING_MAP_ERROR_MESG);
		return;
	}

	if(g_QueueConfig.bEnablePrivateQueueLevelLimit && pchQueueVar && strstri(pchQueueVar, "maplevel"))
	{
		// prevent setting the map lower than allowed
		S32 i;
		for(i = 0; i < eaSize(&pInstance->eaUnorderedMembers); ++i)
		{
			if(pInstance->eaUnorderedMembers[i]->iLevel > iValue + g_QueueConfig.iPrivateQueueLevelLimit)
			{
				queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_CHANGE_SETTING_LEVEL_ERROR_MESG);
				return;
			}
		}
	}

	if ((pSetting = aslQueue_FindSetting(pInstance, pchQueueVar)) && pSetting->iValue == iValue)
	{
		return;
	}

	{
		QueueCBStruct *pCBData = StructCreate(parse_QueueCBStruct);
		TransactionReturnVal *pReturn = objCreateManagedReturnVal(aslQueue_ChangeSetting_CB, pCBData);

		pCBData->iEntID = iEntID;
		pCBData->iQueueID = pQueue->iContainerID;
		pCBData->iInstanceID = pInstance->uiID;
		pCBData->pchQueueVar = StructAllocString(pchQueueVar);
		pCBData->iQueueVarVal = iValue;

		AutoTrans_aslQueue_tr_ChangeSetting(pReturn, GetAppGlobalType(), GLOBALTYPE_QUEUEINFO, pQueue->iContainerID, uiInstanceID, iEntID, pchQueueVar, iValue);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// Change a private game password
///////////////////////////////////////////////////////////////////////////////////////////

static void aslQueue_ChangePassword_CB(TransactionReturnVal* pReturn, void *pData)
{
	QueueCBStruct *pCBData = (QueueCBStruct*)pData;
	QueueInfo *pQueue = aslQueue_GetQueueContainer(pCBData->iQueueID);

	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		if(pQueue)
		{
			log_printf(LOG_QUEUE,"Player [%d] changed password in queue instance [%s][%d].\n", 
				pCBData->iEntID, pQueue->pchName, pCBData->iInstanceID);
			queue_SendResultMessage(pCBData->iEntID, 0, pQueue->pchName, QUEUE_MAP_PASSWORD_CHANGED_MESG);
		}
	}
	else
	{
		if(pQueue)
		{
			log_printf(LOG_QUEUE,"Player [%d] failed to change password in queue instance [%s][%d].\n", 
				pCBData->iEntID, pQueue->pchName, pCBData->iInstanceID);
			queue_SendResultMessage(pCBData->iEntID, 0, pQueue->pchName, QUEUE_MAP_PASSWORD_CHANGE_FAILED_MESG);
		}
	}
	StructDestroy(parse_QueueCBStruct, pCBData);
}

AUTO_TRANSACTION
ATR_LOCKS(pQueueContainer, "eaInstances[]");
enumTransactionOutcome aslQueue_tr_ChangePassword(ATR_ARGS, 
												  NOCONST(QueueInfo)* pQueueContainer, 
												  U32 uiInstanceID, 
												  U32 uiEntID,
												  const char* pchPassword)
{
	NOCONST(QueueInstance)* pInstance = eaIndexedGetUsingInt(&pQueueContainer->eaInstances, uiInstanceID);
	NOCONST(QueueMember)* pMember = queue_trh_FindPlayerInInstance(pInstance, uiEntID);

	if (ISNULL(pMember) || ISNULL(pInstance->pParams) || uiEntID != pInstance->pParams->uiOwnerID)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}
	StructCopyString(&pInstance->pParams->pchPassword, EMPTY_TO_NULL(pchPassword));
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_COMMAND_REMOTE;
void aslQueue_ChangePassword(ContainerID iEntID, const char* pchQueueName, U32 uiInstanceID, const char* pchPassword)
{
	QueueInfo* pQueue;
	QueueDef* pDef;
	QueueInstance* pInstance;

	if (!(pQueue = aslQueue_FindQueueByNameLocal(pchQueueName)) || !(pDef = GET_REF(pQueue->hDef)))
	{
		log_printf(LOG_QUEUE, 
			"EntID [%d] tried to change password in queue [%s], but it wasn't present.\n",
			iEntID,
			pchQueueName);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_FOUND_MESG);
		return;
	}
	if (!(pInstance = eaIndexedGetUsingInt(&pQueue->eaInstances, uiInstanceID)) || 
		!pInstance->pParams || pInstance->pParams->uiOwnerID != iEntID)
	{
		log_printf(LOG_QUEUE, 
			"EntID [%d] tried to change password in queue [%s], but the instance [%d] was not found.\n",
			iEntID,
			pchQueueName,
			uiInstanceID);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_FOUND_MESG);
		return;
	}
	// Password is already the same, do nothing
	if (strcmp_safe(pInstance->pParams->pchPassword, EMPTY_TO_NULL(pchPassword))==0)
	{
		return;
	}
	// Check password length
	if (pchPassword)
	{
		U32 uLen = (U32)strlen(pchPassword);
		if (uLen > g_QueueConfig.uMaxPasswordLength)
		{
			log_printf(LOG_QUEUE, 
				"EntID [%d] tried to change password in queue [%s][%d], but the password was too long.\n",
				iEntID,
				pchQueueName,
				uiInstanceID);
			queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_PASSWORD_INVALID_MESG);
			return;
		}
	}

	{
		QueueCBStruct *pCBData = StructCreate(parse_QueueCBStruct);
		TransactionReturnVal *pReturn = objCreateManagedReturnVal(aslQueue_ChangePassword_CB, pCBData);

		pCBData->iEntID = iEntID;
		pCBData->iQueueID = pQueue->iContainerID;
		pCBData->iInstanceID = pInstance->uiID;

		AutoTrans_aslQueue_tr_ChangePassword(pReturn, GetAppGlobalType(), GLOBALTYPE_QUEUEINFO, pQueue->iContainerID, uiInstanceID, iEntID, pchPassword);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// Start a private game
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE;
void aslQueue_StartGame(ContainerID iEntID, const char* pchQueueName, U32 uiInstanceID)
{
	QueueInfo* pQueue;
	QueueDef* pDef;
	QueueInstance* pInstance;
	QueueMatch QMatch = {0};
	bool bValidMatch = false;

	if (!(pQueue = aslQueue_FindQueueByNameLocal(pchQueueName)) || !(pDef = GET_REF(pQueue->hDef)))
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to start a private game of type [%s], but it wasn't present.\n",
							iEntID,
							pchQueueName);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_FOUND_MESG);
		return;
	}
	if (!(pInstance = eaIndexedGetUsingInt(&pQueue->eaInstances, uiInstanceID)) || 
		!pInstance->pParams || pInstance->pParams->uiOwnerID != iEntID)
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to start a private game of type [%s], but the instance [%d] was not found.\n",
							iEntID,
							pchQueueName,
							uiInstanceID);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_FOUND_MESG);
		return;
	}
	if (eaSize(&pInstance->eaNewMaps) > 0 || aslQueue_SomeMapCouldAcceptMembers(pInstance, pDef))
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to start a private queue game [%s][%d], but a map is already started.\n",
							iEntID,
							pchQueueName,
							uiInstanceID);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_MAP_ACTIVE_MESG);
		return;
	}
	if (!queue_Instance_AllMembersInState(pInstance, PlayerQueueState_InQueue))
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to start a private queue game [%s][%d], but not all players were ready to start.\n",
							iEntID,
							pchQueueName,
							uiInstanceID);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_PLAYERS_NOT_READY_MESG);
		return;
	}

	queue_GroupCache(pInstance, pDef, NULL, &QMatch);
	if (queue_Match_Validate(&QMatch, pDef, pInstance->pParams, pInstance->bOvertime, false, false))
	{
		bValidMatch = true;
	}
	StructDeInit(parse_QueueMatch, &QMatch);
	if (!bValidMatch)
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to start a private queue game [%s][%d], but not there weren't enough players to start.\n",
							iEntID,
							pchQueueName,
							uiInstanceID);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_PLAYERS_NOT_ENOUGH_PLAYERS_MESG);
		return;
	}

	// Create the new map
	aslQueue_CreateNewMap(pQueue, pInstance, pDef, true, NULL);
	
}

//////////////////////////////////////////////////////////////
// Leave the Queue
//////////////////////////////////////////////////////////////

AUTO_TRANS_HELPER
ATR_LOCKS(pInstance, ".Pparams.Uiownerid");
bool aslQueue_trh_IsOwner(ATH_ARG NOCONST(QueueInstance)* pInstance, U32 uMemberID)
{
	if (NONNULL(pInstance) && NONNULL(pInstance->pParams))
	{
		if (pInstance->pParams->uiOwnerID == uMemberID)
		{
			return true;
		}
	}
	return false;
}

AUTO_TRANSACTION
ATR_LOCKS(pQueueContainer, ".Pchname, .Eainstances, .Hdef");
enumTransactionOutcome aslQueue_tr_TeamLeave(ATR_ARGS, NOCONST(QueueInfo) *pQueueContainer, U32 iTeamID, U32 uiInstanceID)
{
	NOCONST(QueueInstance) *pInstance = NULL;
	NOCONST(QueueMember)* pMember = NULL;
	QueueDef* pDef = GET_REF(pQueueContainer->hDef);
	S32 iLaunchingMapIdx = -1;

	if(uiInstanceID > 0)
	{
		pInstance = eaIndexedGetUsingInt(&pQueueContainer->eaInstances, uiInstanceID);
	}
	else
	{
		int i;

		for(i=0;i<eaSize(&pQueueContainer->eaInstances);i++)
		{
			int t;
			for(t=0;t<eaSize(&pQueueContainer->eaInstances[i]->eaUnorderedMembers);t++)
			{
				if(pQueueContainer->eaInstances[i]->eaUnorderedMembers[t]->iTeamID == iTeamID)
				{
					pInstance = pQueueContainer->eaInstances[i];
					break;
				}
			}

			if(NONNULL(pInstance))
				break;
		}
	}

	if (NONNULL(pInstance))
	{
		int iMemberIdx;
		int iRemoved = 0;

		for(iMemberIdx=eaSize(&pInstance->eaUnorderedMembers)-1;iMemberIdx>=0;iMemberIdx--)
		{
			pMember = pInstance->eaUnorderedMembers[iMemberIdx];

			if(pMember->iTeamID == iTeamID)
			{
				if ((NONNULL(pInstance) && !queue_trh_CanLeaveQueue(pInstance->pParams, pMember->eState)) ||
					(ISNULL(pInstance) && !queue_trh_CanLeaveQueue(NULL, pMember->eState)))
				{
					log_printf(LOG_QUEUE, "EntID [%d] tried to leave queue [%s] but he/she is actively in that queue and cannot leave it.\n",
						pMember->iEntID,
						pQueueContainer->pchName);
					return TRANSACTION_OUTCOME_FAILURE;
				}

				// If this is a private match, check to see if the member is leaving while a map is launching
				if (pMember->iMapKey && NONNULL(pInstance) && queue_trh_IsPrivateMatch(pInstance->pParams))
				{
					for (iLaunchingMapIdx = eaSize(&pInstance->eaMaps)-1; iLaunchingMapIdx >= 0; iLaunchingMapIdx--)
					{
						NOCONST(QueueMap)* pMap = pInstance->eaMaps[iLaunchingMapIdx];
						if (pMember->iMapKey == pMap->iMapKey &&
							(pMap->eMapState == kQueueMapState_Open ||
							pMap->eMapState == kQueueMapState_LaunchCountdown))
						{
							break;
						}
					}
				}
				// Don't remove the member if it's the owner and the map is trying to launch
				if ((iLaunchingMapIdx < 0 || !aslQueue_trh_IsOwner(pInstance, pMember->iEntID)) &&
					((NONNULL(pInstance) && !aslQueue_trh_RemovePlayerFromInstance(pInstance, pDef, pMember->iEntID, 0)) ||	
					(ISNULL(pInstance) && !aslQueue_trh_RemovePlayer(ATR_PASS_ARGS, pQueueContainer, pMember->iEntID, 0))))
				{
					log_printf(LOG_QUEUE, "EntID [%d] tried to leave queue [%s] but the member could not be found.\n",
						pMember->iEntID,
						pQueueContainer->pchName);
					return TRANSACTION_OUTCOME_FAILURE;
				}

				iRemoved++;
			}
		}

		// If a member leaves the match during a map countdown, cancel the launch
		if (iRemoved > 0 && iLaunchingMapIdx >= 0)
		{
			NOCONST(QueueMap)* pLaunchingMap = pInstance->eaMaps[iLaunchingMapIdx];
			for (iMemberIdx = eaSize(&pInstance->eaUnorderedMembers)-1; iMemberIdx >= 0; iMemberIdx--)
			{
				NOCONST(QueueMember)* pFindMember = pInstance->eaUnorderedMembers[iMemberIdx];
				if (pFindMember->iMapKey == pLaunchingMap->iMapKey)
				{
					aslQueue_trh_MemberChangeState(ATR_PASS_ARGS, pFindMember, PlayerQueueState_InQueue, true);
				}
			}
			StructDestroyNoConst(parse_QueueMap, eaRemove(&pInstance->eaMaps, iLaunchingMapIdx));
//			TRANSACTION_APPEND_LOG_SUCCESS("%s", QUEUE_CANCEL_LAUNCH_STRING);
			// WOLF[16Jan13] I am being lazy and do not feeling like modifying all the objCreateManagedReturnVals just now.
			//  They need to become LoggedTransactions_CreateManagedReturnVal if we ever decide it is worth it.
			log_printf(LOG_QUEUE, "EntID [%d] left queue [%s] during countdown. Launch Cancelled.\n",
				pMember->iEntID,
				pQueueContainer->pchName);
		}

		return TRANSACTION_OUTCOME_SUCCESS;
	}

	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANSACTION
ATR_LOCKS(pQueueContainer, ".Pchname, .Eainstances, .Hdef");
enumTransactionOutcome aslQueue_tr_QueueLeave(ATR_ARGS, NOCONST(QueueInfo) *pQueueContainer, ContainerID iEntID, U32 uiInstanceID, U32 bForceLeave)
{
	NOCONST(QueueInstance)* pInstance = NULL;
	NOCONST(QueueMember)* pMember = NULL;
	QueueDef* pDef = GET_REF(pQueueContainer->hDef);
	S32 iLaunchingMapIdx = -1;

	if (uiInstanceID > 0)
	{
		pInstance = eaIndexedGetUsingInt(&pQueueContainer->eaInstances, uiInstanceID);
		if (NONNULL(pInstance))
		{
			pMember = queue_trh_FindPlayerInInstance(pInstance, iEntID);
		}
	}
	else
	{
		pMember = queue_trh_FindPlayer(pQueueContainer, iEntID, NULL);
	}

	if (ISNULL(pMember))
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to leave queue [%s] but he/she wasn't in that queue.\n",
			iEntID,
			pQueueContainer->pchName);
		return TRANSACTION_OUTCOME_FAILURE;
	}
	if (!bForceLeave && 
		((NONNULL(pInstance) && !queue_trh_CanLeaveQueue(pInstance->pParams, pMember->eState)) ||
		(ISNULL(pInstance) && !queue_trh_CanLeaveQueue(NULL, pMember->eState))))
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to leave queue [%s] but he/she is actively in that queue and cannot leave it.\n",
			iEntID,
			pQueueContainer->pchName);
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// If this is a private match, check to see if the member is leaving while a map is launching
	if (pMember->iMapKey && NONNULL(pInstance) && queue_trh_IsPrivateMatch(pInstance->pParams))
	{
		for (iLaunchingMapIdx = eaSize(&pInstance->eaMaps)-1; iLaunchingMapIdx >= 0; iLaunchingMapIdx--)
		{
			NOCONST(QueueMap)* pMap = pInstance->eaMaps[iLaunchingMapIdx];
			if (pMember->iMapKey == pMap->iMapKey &&
				(pMap->eMapState == kQueueMapState_Open ||
				 pMap->eMapState == kQueueMapState_LaunchCountdown))
			{
				break;
			}
		}
	}
	// Don't remove the member if it's the owner and the map is trying to launch
	if ((iLaunchingMapIdx < 0 || !aslQueue_trh_IsOwner(pInstance, pMember->iEntID)) &&
		((NONNULL(pInstance) && !aslQueue_trh_RemovePlayerFromInstance(pInstance, pDef, iEntID, 0)) ||	
		(ISNULL(pInstance) && !aslQueue_trh_RemovePlayer(ATR_PASS_ARGS, pQueueContainer, iEntID, 0))))
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to leave queue [%s] but the member could not be found.\n",
			iEntID,
			pQueueContainer->pchName);
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// If a member leaves the match during a map countdown, cancel the launch
	if (iLaunchingMapIdx >= 0)
	{
		NOCONST(QueueMap)* pLaunchingMap = pInstance->eaMaps[iLaunchingMapIdx];
		S32 iMemberIdx;
		for (iMemberIdx = eaSize(&pInstance->eaUnorderedMembers)-1; iMemberIdx >= 0; iMemberIdx--)
		{
			NOCONST(QueueMember)* pFindMember = pInstance->eaUnorderedMembers[iMemberIdx];
			if (pFindMember->iMapKey == pLaunchingMap->iMapKey)
			{
				aslQueue_trh_MemberChangeState(ATR_PASS_ARGS, pFindMember, PlayerQueueState_InQueue, true);
			}
		}
		StructDestroyNoConst(parse_QueueMap, eaRemove(&pInstance->eaMaps, iLaunchingMapIdx));
//		TRANSACTION_APPEND_LOG_SUCCESS("%s", QUEUE_CANCEL_LAUNCH_STRING);
		// WOLF[16Jan13] I am being lazy and do not feeling like modifying all the objCreateManagedReturnVals just now.
		//  They need to become LoggedTransactions_CreateManagedReturnVal if we ever decide it is worth it.
		log_printf(LOG_QUEUE, "EntID [%d] left queue [%s] during countdown. Launch Cancelled.\n",
			iEntID,
			pQueueContainer->pchName);
		
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

void queue_SendTeamResultMessageEx(CachedTeamStruct *pTeam,
									U32 iTargetEntID,
									const char *pcQueueName,
									const char *pcMessageKey,
									const char *pchCallerFunction)
{
	if (pTeam)
	{
		int i;
		for(i=0;i<eaiSize(&pTeam->eaiEntityIds);i++)
		{
			queue_SendResultMessageEx(pTeam->eaiEntityIds[i],iTargetEntID,pcQueueName,pcMessageKey,pchCallerFunction);
		}
	}
}


static void aslQueue_QueueTeamLeave_CB(TransactionReturnVal *pVal, void *pData)
{
	QueueCBStruct *pCBData = (QueueCBStruct*)pData;
	QueueInfo *pQueue = aslQueue_GetQueueContainer(pCBData->iQueueID);
	CachedTeamStruct *pTeam = eaIndexedGetUsingInt(&s_eaCachedTeams, pCBData->iTeamID);

	if(pVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		if (pQueue)
		{
			aslQueue_Leave_HandleChatMessages(pQueue, pVal, pCBData);
			aslQueue_Leave_HandleChatChannels(pQueue, pCBData->iInstanceID, pCBData->iEntID);

			if(pCBData->iMapKey)
			{
				U32 uMapID = queue_GetMapIDFromMapKey(pCBData->iMapKey);
				U32 uPartitionID = queue_GetPartitionIDFromMapKey(pCBData->iMapKey);
				RemoteCommand_gslQueue_RemoveMemberFromMatch(GLOBALTYPE_GAMESERVER, uMapID, uPartitionID, pCBData->iEntID);
			}
			log_printf(LOG_QUEUE, "TeamID [%d] left the [%s] queue.\n",
				pCBData->iTeamID,
				pQueue->pchName);
			queue_SendTeamResultMessage(pTeam, 0, pQueue->pchName, QUEUE_LEFT_MESG);
		}
	}
	else
	{
		if (pQueue)
		{
			queue_SendTeamResultMessage(pTeam, 0, pQueue->pchName, QUEUE_NOT_MEMBER_MESG);
		}
	}
	aslQueue_CleanupPreventMapLaunch(pCBData);
	StructDestroy(parse_QueueCBStruct, pCBData);
}

static void aslQueue_QueueLeave_CB(TransactionReturnVal *pVal, void *pData)
{
	QueueCBStruct *pCBData = (QueueCBStruct*)pData;
	QueueInfo *pQueue = aslQueue_GetQueueContainer(pCBData->iQueueID);

	if(pVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		if (pQueue)
		{
			aslQueue_Leave_HandleChatMessages(pQueue, pVal, pCBData);
			aslQueue_Leave_HandleChatChannels(pQueue, pCBData->iInstanceID, pCBData->iEntID);

			if(pCBData->iMapKey)
			{
				U32 uMapID = queue_GetMapIDFromMapKey(pCBData->iMapKey);
				U32 uPartitionID = queue_GetPartitionIDFromMapKey(pCBData->iMapKey);
				RemoteCommand_gslQueue_RemoveMemberFromMatch(GLOBALTYPE_GAMESERVER, uMapID, uPartitionID, pCBData->iEntID);
			}
			log_printf(LOG_QUEUE, "EntID [%d] left the [%s] queue.\n",
								pCBData->iEntID,
								pQueue->pchName);
			queue_SendResultMessage(pCBData->iEntID, 0, pQueue->pchName, QUEUE_LEFT_MESG);
		}
	}
	else
	{
		if (pQueue)
		{
			queue_SendResultMessage(pCBData->iEntID, 0, pQueue->pchName, QUEUE_NOT_MEMBER_MESG);
		}
	}
	aslQueue_CleanupPreventMapLaunch(pCBData);
	StructDestroy(parse_QueueCBStruct, pCBData);
}

AUTO_COMMAND_REMOTE;
void aslQueue_TeamLeave(ContainerID iEntID, const char *pchQueueName, U32 uiInstanceID)
{
	QueueInfo* pQueue = aslQueue_FindQueueByNameLocal(pchQueueName);
	QueueInstance* pInstance = pQueue && uiInstanceID > 0 ? eaIndexedGetUsingInt(&pQueue->eaInstances, uiInstanceID) : NULL;
	QueueMember* pMember = pInstance ? queue_FindPlayerInInstance(pInstance, iEntID) : queue_FindPlayerAndInstance(pQueue,iEntID,&pInstance);
	QueueDef *pDef;

	log_printf(LOG_QUEUE,"Player [%d] requested to team leave queue [%s].\n",iEntID,pQueue ? pchQueueName : "Unknown Queue");

	if(!pQueue || !(pDef = GET_REF(pQueue->hDef)))
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to leave a queue [%s] but it wasn't present.\n",
			iEntID,
			pchQueueName);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_FOUND_MESG);
		return;
	}

	if(pMember && !pMember->iTeamID)
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to team leave queue [%s] but is not part of a team.\n",
			iEntID,
			pchQueueName);
		queue_SendResultMessage(iEntID,0,pchQueueName, QUEUE_NOT_FOUND_MESG);
	}

	if(pQueue && pMember)
	{
		U32 iTeamID = pMember->iTeamID;
		QueueCBStruct *pCBData = StructCreate(parse_QueueCBStruct);
		TransactionReturnVal *pReturn = objCreateManagedReturnVal(aslQueue_QueueTeamLeave_CB, pCBData);
		pCBData->iTeamID = iTeamID;
		pCBData->iMapKey = pMember->iMapKey;
		pCBData->iQueueID = pQueue->iContainerID;
		pCBData->iInstanceID = uiInstanceID;
		pCBData->iOwnerID = SAFE_MEMBER2(pInstance, pParams, uiOwnerID);
		
		// Prevent the map from launching if this is a private match
		if (SAFE_MEMBER2(pInstance, pParams, uiOwnerID) > 0)
		{
			pInstance->uiPlayersPreventingMapLaunch++;
			pCBData->bPreventMapLaunch = true;
		}
		AutoTrans_aslQueue_tr_TeamLeave(pReturn, GetAppGlobalType(), GLOBALTYPE_QUEUEINFO, pQueue->iContainerID, iTeamID, pInstance->uiID);
	}
}

AUTO_COMMAND_REMOTE;
void aslQueue_Leave(ContainerID iEntID, const char* pchQueueName, U32 uiInstanceID)
{
	QueueInfo* pQueue = aslQueue_FindQueueByNameLocal(pchQueueName);
	QueueInstance* pInstance = pQueue && uiInstanceID > 0 ? eaIndexedGetUsingInt(&pQueue->eaInstances, uiInstanceID) : NULL;
	QueueMember* pMember = pInstance ? queue_FindPlayerInInstance(pInstance, iEntID) : queue_FindPlayerAndInstance(pQueue, iEntID, &pInstance);
	QueueDef* pDef;

	log_printf(LOG_QUEUE,"Player [%d] requested to leave queue [%s].\n", iEntID, pQueue ? pchQueueName : "Unknown Queue");

	if(!pQueue || !(pDef = GET_REF(pQueue->hDef)))
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to leave a queue [%s] but it wasn't present.\n",
							iEntID,
							pchQueueName);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_FOUND_MESG);
		return;
	}

	if((g_QueueConfig.bEnableStrictTeamRules || pDef->bEnableStrictTeamRules) && pMember && pMember->iTeamID)
	{
		CachedTeamStruct *pTeam = eaIndexedGetUsingInt(&s_eaCachedTeams, pMember->iTeamID);

		log_printf(LOG_QUEUE, "EntID [%d] tried to leave queue [%s] but is part of a team.\n",
					iEntID,
					pchQueueName);
		
		// send a notification to all members that this guy decided to leave the queue
		if (pTeam)
		{
			queue_SendTeamResultMessage(pTeam, pMember->iEntID, pchQueueName, QUEUE_TEAM_MEMBER_DECLINED_MESG);
		}
		
		// force all the other team members to leave the queue as well
		aslQueue_TeamLeave(iEntID, pchQueueName, uiInstanceID);
		return;
	}

	if(pQueue && pMember)
	{
		QueueCBStruct *pCBData = StructCreate(parse_QueueCBStruct);
		TransactionReturnVal *pReturn = objCreateManagedReturnVal(aslQueue_QueueLeave_CB, pCBData);
		pCBData->iEntID = iEntID;
		pCBData->iMapKey = pMember->iMapKey;
		pCBData->iQueueID = pQueue->iContainerID;
		pCBData->iInstanceID = uiInstanceID;
		pCBData->iOwnerID = SAFE_MEMBER2(pInstance, pParams, uiOwnerID);
		
		// Prevent the map from launching if this is a private match
		if (SAFE_MEMBER2(pInstance, pParams, uiOwnerID) > 0)
		{
			pInstance->uiPlayersPreventingMapLaunch++;
			pCBData->bPreventMapLaunch = true;
		}
		AutoTrans_aslQueue_tr_QueueLeave(pReturn, GetAppGlobalType(), GLOBALTYPE_QUEUEINFO, pQueue->iContainerID, iEntID, pInstance->uiID,false);
	}
}

//Called from the server to force remove a player from a queue (if they out level it for instance)
AUTO_COMMAND_REMOTE;
void aslQueue_LeaveForced(ContainerID iEntID, const char* pchQueueName, U32 uiInstanceID)
{
	QueueInfo* pQueue = aslQueue_FindQueueByNameLocal(pchQueueName);
	QueueInstance* pInstance = NULL;
	QueueMember* pMember = NULL;

	if (pQueue)
	{
		pInstance = eaIndexedGetUsingInt(&pQueue->eaInstances, uiInstanceID);
		if (pInstance)
		{
			pMember = queue_FindPlayerInInstance(pInstance, iEntID);
		}
		else
		{
			pMember = queue_FindPlayerAndInstance(pQueue, iEntID, &pInstance);
		}
	}

	if(pMember && 
		(pMember->eState == PlayerQueueState_InQueue ||
		 pMember->eState == PlayerQueueState_Invited ||
		 pMember->eState == PlayerQueueState_Offered ||
		 pMember->eState == PlayerQueueState_Countdown ||
		 pMember->eState == PlayerQueueState_Delaying ||
		 pMember->eState == PlayerQueueState_Limbo))
	{
		QueueCBStruct *pCBData = StructCreate(parse_QueueCBStruct);
		TransactionReturnVal *pReturn = objCreateManagedReturnVal(aslQueue_ForcedLeave_CB, pCBData);

		pCBData->iEntID = iEntID;
		pCBData->iMapKey = pMember->iMapKey;
		pCBData->iQueueID = pQueue->iContainerID;
		
		// Prevent the map from launching if this is a private match
		if (SAFE_MEMBER2(pInstance, pParams, uiOwnerID) > 0)
		{
			pInstance->uiPlayersPreventingMapLaunch++;
			pCBData->bPreventMapLaunch = true;
		}
		AutoTrans_aslQueue_tr_QueueLeave(pReturn, GetAppGlobalType(), GLOBALTYPE_QUEUEINFO, pQueue->iContainerID, iEntID, pInstance->uiID, true);

		log_printf(LOG_QUEUE,"Player [%d] was forced to leave queue [%s].\n", iEntID, pQueue ? pQueue->pchName : "Unknown Queue");
	}
}

AUTO_COMMAND_REMOTE;
void aslQueue_LeaveQueuesWithStrictTeamRules(ContainerID iEntID, bool bLeaveIfInMap)
{
	ContainerIterator queueIter;
	QueueInfo* pQueue = NULL;

	objInitContainerIteratorFromType(GLOBALTYPE_QUEUEINFO, &queueIter);
	while (pQueue = objGetNextObjectFromIterator(&queueIter))
	{
		QueueInstance* pInstance = NULL;
		QueueMember* pMember = queue_FindPlayerAndInstance(pQueue, iEntID, &pInstance);
		QueueDef *pQueueDef = GET_REF(pQueue->hDef);

		if(!pQueueDef->bEnableStrictTeamRules && !g_QueueConfig.bEnableStrictTeamRules)
			continue;

		if(pMember && 
			(pMember->eState == PlayerQueueState_InQueue ||
			pMember->eState == PlayerQueueState_Invited ||
			pMember->eState == PlayerQueueState_Offered ||
			pMember->eState == PlayerQueueState_Countdown ||
			pMember->eState == PlayerQueueState_Delaying ||
			pMember->eState == PlayerQueueState_Limbo ||
			(pMember->eState == PlayerQueueState_InMap && bLeaveIfInMap)))
		{
			QueueCBStruct *pCBData = StructCreate(parse_QueueCBStruct);
			TransactionReturnVal *pReturn = objCreateManagedReturnVal(aslQueue_ForcedLeave_CB, pCBData);

			// Prevent the map from launching if this is a private match
			if (SAFE_MEMBER2(pInstance, pParams, uiOwnerID) > 0)
			{
				pInstance->uiPlayersPreventingMapLaunch++;
				pCBData->bPreventMapLaunch = true;
			}

			pCBData->iEntID = iEntID;
			pCBData->iMapKey = pMember->iMapKey;
			pCBData->iQueueID = pQueue->iContainerID;
			AutoTrans_aslQueue_tr_QueueLeave(pReturn, GetAppGlobalType(), GLOBALTYPE_QUEUEINFO, pQueue->iContainerID, iEntID, 0,true);
		}
	}
	objClearContainerIterator(&queueIter);
}

AUTO_COMMAND_REMOTE;
void aslQueue_ForceLeaveActiveInstance(ContainerID iEntID)
{
	ContainerIterator queueIter;
	QueueInfo* pQueue = NULL;

	objInitContainerIteratorFromType(GLOBALTYPE_QUEUEINFO, &queueIter);
	while (pQueue = objGetNextObjectFromIterator(&queueIter))
	{
		QueueInstance* pInstance = NULL;
		QueueMember* pMember = queue_FindPlayerAndInstance(pQueue, iEntID, &pInstance);

		if (pMember && pMember->iMapKey && 
			(pMember->eState == PlayerQueueState_Accepted ||
			 pMember->eState == PlayerQueueState_Countdown ||
			 pMember->eState == PlayerQueueState_Limbo ||
			 pMember->eState == PlayerQueueState_InMap))
		{
			QueueCBStruct *pCBData = StructCreate(parse_QueueCBStruct);
			TransactionReturnVal *pReturn = objCreateManagedReturnVal(aslQueue_ForcedLeave_CB, pCBData);

			pCBData->iEntID = iEntID;
			pCBData->iMapKey = pMember->iMapKey;
			pCBData->iQueueID = pQueue->iContainerID;

			AutoTrans_aslQueue_tr_QueueLeave(pReturn, GetAppGlobalType(), GLOBALTYPE_QUEUEINFO, pQueue->iContainerID, iEntID, pInstance->uiID, true);
			break;
		}
	}
	objClearContainerIterator(&queueIter);
}

AUTO_COMMAND_REMOTE;
void aslQueue_LeaveAll(ContainerID iEntID, bool bLeaveIfInMap)
{
	ContainerIterator queueIter;
	QueueInfo* pQueue = NULL;

	objInitContainerIteratorFromType(GLOBALTYPE_QUEUEINFO, &queueIter);
	while (pQueue = objGetNextObjectFromIterator(&queueIter))
	{
		QueueInstance* pInstance = NULL;
		QueueMember* pMember = queue_FindPlayerAndInstance(pQueue, iEntID, &pInstance);

		if(pMember && 
			(pMember->eState == PlayerQueueState_InQueue ||
			pMember->eState == PlayerQueueState_Invited ||
			pMember->eState == PlayerQueueState_Offered ||
			pMember->eState == PlayerQueueState_Countdown ||
			pMember->eState == PlayerQueueState_Delaying ||
			(pMember->eState == PlayerQueueState_Limbo && (!pMember->iMapKey || bLeaveIfInMap)) ||
			(pMember->eState == PlayerQueueState_InMap && bLeaveIfInMap)))
		{
			QueueCBStruct *pCBData = StructCreate(parse_QueueCBStruct);
			TransactionReturnVal *pReturn = objCreateManagedReturnVal(aslQueue_ForcedLeave_CB, pCBData);

			pCBData->iEntID = iEntID;
			pCBData->iMapKey = pMember->iMapKey;
			pCBData->iQueueID = pQueue->iContainerID;
			
			// Prevent the map from launching if this is a private match
			if (SAFE_MEMBER2(pInstance, pParams, uiOwnerID) > 0)
			{
				pInstance->uiPlayersPreventingMapLaunch++;
				pCBData->bPreventMapLaunch = true;
			}
			AutoTrans_aslQueue_tr_QueueLeave(pReturn, GetAppGlobalType(), GLOBALTYPE_QUEUEINFO, pQueue->iContainerID, iEntID, pInstance->uiID, bLeaveIfInMap);
		}
	}
	objClearContainerIterator(&queueIter);
}

// special version of aslQueue_LeaveAll used for strict teaming that will not leave a queue instance that you are a part of
static void aslQueue_LeaveAllStrictTeaming(ContainerID iEntID)
{
	ContainerIterator queueIter;
	QueueInfo* pQueue = NULL;

	objInitContainerIteratorFromType(GLOBALTYPE_QUEUEINFO, &queueIter);
	while (pQueue = objGetNextObjectFromIterator(&queueIter))
	{
		QueueInstance* pInstance = NULL;
		QueueMember* pMember = queue_FindPlayerAndInstance(pQueue, iEntID, &pInstance);

		if (!pMember)
			continue;
		
		if ( pMember->iMapKey &&
				(pMember->eState == PlayerQueueState_Countdown ||
				 pMember->eState == PlayerQueueState_InMap ||
				 pMember->eState == PlayerQueueState_Limbo))
		{	// we are on our way to or in a map, don't leave
			continue;
		}
		
		// leave this queue
		{
			QueueCBStruct *pCBData = StructCreate(parse_QueueCBStruct);
			TransactionReturnVal *pReturn = objCreateManagedReturnVal(aslQueue_ForcedLeave_CB, pCBData);

			pCBData->iEntID = iEntID;
			pCBData->iMapKey = pMember->iMapKey;
			pCBData->iQueueID = pQueue->iContainerID;
			
			// Prevent the map from launching if this is a private match
			if (SAFE_MEMBER2(pInstance, pParams, uiOwnerID) > 0)
			{
				pInstance->uiPlayersPreventingMapLaunch++;
				pCBData->bPreventMapLaunch = true;
			}
			AutoTrans_aslQueue_tr_QueueLeave(pReturn, GetAppGlobalType(), GLOBALTYPE_QUEUEINFO, pQueue->iContainerID, iEntID, 0,true);
		}
	}
	objClearContainerIterator(&queueIter);
}

typedef struct RequeueInstanceInfo
{
	const char *pchAffiliation;
	U32 instanceID;
	const char *pchQueueName;
	QueueInstanceParams *pParams;
} RequeueInstanceInfo;

static void aslQueue_GetRequeueListForMember(U32 entityID, RequeueInstanceInfo ***peaRequeueListOut)
{
	ContainerIterator queueIter;
	QueueInfo *pQueue = NULL;

	objInitContainerIteratorFromType(GLOBALTYPE_QUEUEINFO, &queueIter);
	while (pQueue = objGetNextObjectFromIterator(&queueIter))
	{
		S32 i;
		for (i = eaSize(&pQueue->eaInstances)-1; i >= 0; i--)
		{
			QueueInstance* pInstance = pQueue->eaInstances[i];
			QueueMember* pMember = queue_FindPlayerInInstance(pInstance, entityID);
			if (pMember && (pMember->eState == PlayerQueueState_InQueue))
			{	
				RequeueInstanceInfo *pRequeueInfo = malloc(sizeof(RequeueInstanceInfo));

				pRequeueInfo->instanceID = pInstance->uiID;
				pRequeueInfo->pchAffiliation = pMember->pchAffiliation;
				pRequeueInfo->pchQueueName = pQueue->pchName;
				pRequeueInfo->pParams = pInstance->pParams;

				eaPush(peaRequeueListOut, pRequeueInfo);
			}
		}		
	}
	objClearContainerIterator(&queueIter);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
void aslQueue_TeamUpdate(ContainerID iTeamID, QueueIntArray *pMemberList)
{
	int i;
	if(iTeamID)
	{
		CachedTeamStruct *pTeam = eaIndexedGetUsingInt(&s_eaCachedTeams, iTeamID);
		RequeueInstanceInfo **eaRequeueInstanceInfo = NULL;
		U32 *eaiNewMembers = NULL;

		if(!pTeam)
		{
			pTeam = StructCreate(parse_CachedTeamStruct);
			pTeam->iTeamID = iTeamID;
			eaPush(&s_eaCachedTeams, pTeam);
			eaIndexedEnable(&s_eaCachedTeams, parse_CachedTeamStruct);
		}
		
		// Find members leaving and remove them from all queues
		for(i=0;i<eaiSize(&pTeam->eaiEntityIds);i++)
		{
			if(eaiFind(&pMemberList->piArray,pTeam->eaiEntityIds[i])==-1)
			{
				if(g_QueueConfig.bEnableStrictTeamRules)
					aslQueue_LeaveAllStrictTeaming(pTeam->eaiEntityIds[i]);
				else
					aslQueue_LeaveQueuesWithStrictTeamRules(pTeam->eaiEntityIds[i],false);
			}
		}
			
		// only process joining members if there is more than one person on the team
		if (eaiSize(&pMemberList->piArray) > 1)
		{	
			// Find members joining
			for(i=0;i<eaiSize(&pMemberList->piArray);i++)
			{
				if(eaiFind(&pTeam->eaiEntityIds,pMemberList->piArray[i])==-1)
				{
					if(eaiFind(&pTeam->eaiEntityIds,pMemberList->piArray[i])==-1)
					{
						// remove them from all queues
						aslQueue_LeaveAllStrictTeaming(pMemberList->piArray[i]);
						eaiPush(&eaiNewMembers, pMemberList->piArray[i]);
						break;
					}
					eaiPush(&eaiNewMembers, pMemberList->piArray[i]);
					break;
				}
			}
		}

		if (eaiSize(&eaiNewMembers))
		{	// get a list of all the queues that we will want to add players to with the necessary info to re-queue
				
			aslQueue_GetRequeueListForMember(pMemberList->piArray[0], &eaRequeueInstanceInfo);
		}


		eaiClear(&pTeam->eaiEntityIds);
		eaiCopy(&pTeam->eaiEntityIds, &pMemberList->piArray);

		// New members found, join queues with all members
		if(g_QueueConfig.bEnableStrictTeamRules && eaiSize(&eaiNewMembers))
		{
			QueueIntArray teamMemberList = {0};

			teamMemberList.piArray = pMemberList->piArray;
			
			for (i = 0; i < eaiSize(&eaiNewMembers); ++i)
			{
				FOR_EACH_IN_EARRAY(eaRequeueInstanceInfo, RequeueInstanceInfo, pQueueInfo)
				{
					RemoteCommand_gslQueue_rcmd_JoinParams(GLOBALTYPE_ENTITYPLAYER, 
															eaiNewMembers[i], 
															eaiNewMembers[i],
															pQueueInfo->pchQueueName,
															pQueueInfo->instanceID,
															0,
															false,
															false, 
															true, 
															pQueueInfo->pParams);
				}
				FOR_EACH_END
			}
			

			eaDestroyEx(&eaRequeueInstanceInfo, NULL);
			eaiDestroy(&eaiNewMembers);
		}
	}
}

AUTO_COMMAND_REMOTE;
void aslQueue_TeamRemove(ContainerID iTeamID)
{
	S32 iIdx = eaIndexedFindUsingInt(&s_eaCachedTeams, iTeamID);
	if(iIdx >= 0)
	{
		int i;

		for(i=0;i<eaiSize(&s_eaCachedTeams[iIdx]->eaiEntityIds);i++)
		{
			if(g_QueueConfig.bEnableStrictTeamRules)
				aslQueue_LeaveAllStrictTeaming(s_eaCachedTeams[iIdx]->eaiEntityIds[i]);
			else
				aslQueue_LeaveQueuesWithStrictTeamRules(s_eaCachedTeams[iIdx]->eaiEntityIds[i],false);
		}
		StructDestroy(parse_CachedTeamStruct, eaRemove(&s_eaCachedTeams, iIdx));
	}
}

//------------------------------------------------
// Accepting or delaying offers from the queue server
//------------------------------------------------

static void AcceptOffer_CB(TransactionReturnVal *pReturn, void *pData)
{
	QueueCBStruct *pCBStruct = (QueueCBStruct*)pData;

	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		QueueInstantiationInfo* pInstantiationInfo = NULL;
		U32 uInstanceID = pCBStruct->iInstanceID;
		QueueInfo* pQueue = aslQueue_GetQueueContainer(pCBStruct->iQueueID);
		QueueDef* pDef = pQueue ? GET_REF(pQueue->hDef) : NULL;
		QueueInstance* pInstance = pQueue ? eaIndexedGetUsingInt(&pQueue->eaInstances, uInstanceID) : NULL;

		if (pDef && pInstance && pInstance->pParams)
		{
			if (!pInstance->pParams->uiOwnerID && !queue_InstanceShouldCheckOffers(pDef, pInstance->pParams))
			{
				QueueMember* pMember = queue_FindPlayerInInstance(pInstance, pCBStruct->iEntID);
				QueueMap* pMap = pMember ? eaIndexedGetUsingInt(&pInstance->eaMaps, pMember->iMapKey) : NULL;
				QueueGroupDef* pGroupDef = pMember ? eaGet(&pDef->eaGroupDefs, pMember->iGroupIndex) : NULL;

				if (pMap && pGroupDef)
				{
					pInstantiationInfo = StructCreate(parse_QueueInstantiationInfo);
					pInstantiationInfo->pchQueueDef = allocAddString(pDef->pchName);
					pInstantiationInfo->pchMapName = allocAddString(pMap->pchMapName);
					pInstantiationInfo->pchSpawnName = allocAddString(pGroupDef->pchSpawnTargetName);
					pInstantiationInfo->iMapKey = pMap->iMapKey;
					pInstantiationInfo->uInstanceID = pInstance->uiID;
					pInstantiationInfo->iGroupIndex = pMember->iGroupIndex;
				}
			}
		}
		if (pQueue)
		{
			log_printf(LOG_QUEUE,"Player [%d] accepted the offer from queue instance [%s][%d].\n", pCBStruct->iEntID, pQueue->pchName, pCBStruct->iInstanceID);

			RemoteCommand_gslQueue_AcceptOffer(GLOBALTYPE_ENTITYPLAYER, pCBStruct->iEntID, pCBStruct->iEntID, pQueue->pchName, pInstantiationInfo);
		}
		if (pInstantiationInfo)
		{
			StructDestroy(parse_QueueInstantiationInfo, pInstantiationInfo);
		}
	}
	StructDestroy(parse_QueueCBStruct, pCBStruct);
}

AUTO_COMMAND_REMOTE;
void aslQueue_AcceptOffer(ContainerID iEntID, const char* pchQueueName, U32 uiInstanceID)
{
	QueueInfo* pQueue = aslQueue_FindQueueByNameLocal(pchQueueName);
	QueueInstance* pInstance = pQueue ? eaIndexedGetUsingInt(&pQueue->eaInstances, uiInstanceID) : NULL;
	QueueMember* pMember = pInstance ? queue_FindPlayerInInstance(pInstance, iEntID) : NULL;

	if(!pInstance)
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to accept a queue offer [%s] but the instance wasn't present.\n",
							iEntID,
							pchQueueName);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_FOUND_MESG);
		return;
	}
	if(!pMember)
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to accept a queue offer [%s] but he/she wasn't found in that queue.\n",
							iEntID,
							pchQueueName);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_MEMBER_MESG);
		return;
	}

	if(pMember->eState != PlayerQueueState_Offered
		&& pMember->eState != PlayerQueueState_InMap)
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to accept a queue offer [%s] but he/she didn't have an offer.\n",
							iEntID,
							pchQueueName);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_OFFERED_MESG);
		return;
	}

	{
		QueueCBStruct *pCBData = StructCreate(parse_QueueCBStruct);
		QMemberUpdateList *pList = StructCreate(parse_QMemberUpdateList);
		TransactionReturnVal *pReturn = objCreateManagedReturnVal(AcceptOffer_CB, pCBData);
		
		aslQueue_AddMemberStateChange(&pList->eaUpdates, pInstance, pMember, PlayerQueueState_Accepted);

		pCBData->iEntID = pMember->iEntID;
		pCBData->iQueueID = pQueue->iContainerID;
		pCBData->iInstanceID = pInstance->uiID;

		AutoTrans_aslQueue_tr_MembersStateUpdate(pReturn, GLOBALTYPE_QUEUESERVER, GLOBALTYPE_QUEUEINFO, pQueue->iContainerID, pList);

		StructDestroySafe(parse_QMemberUpdateList, &pList);
	}
}

static void AcceptInvite_CB(TransactionReturnVal *pReturn, void *pData)
{
	QueueCBStruct *pCBStruct = (QueueCBStruct*)pData;

	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		QueueInfo *pQueue = aslQueue_GetQueueContainer(pCBStruct->iQueueID);

		if (pQueue)
		{
			aslQueue_Join_HandleChatMessages(pQueue, pCBStruct);
			
			// Don't have the correct params here because the accept invite command doesn't need them, it cannot have an invalid instance ID
			aslQueue_Join_HandleChatChannels(pQueue, pCBStruct->iInstanceID, pCBStruct->iEntID, NULL);

			log_printf(LOG_QUEUE,"Player [%d] accepted the invite from queue instance [%s][%d].\n", pCBStruct->iEntID, pQueue->pchName, pCBStruct->iInstanceID);

			queue_SendResultMessage(pCBStruct->iEntID, 0, pQueue->pchName, QUEUE_JOINED_MESG);
		}
	}
	StructDestroy(parse_QueueCBStruct, pCBStruct);
}

AUTO_COMMAND_REMOTE;
void aslQueue_AcceptInvite(ContainerID iEntID, const char* pchQueueName, U32 uiInstanceID)
{
	QueueInfo* pQueue = aslQueue_FindQueueByNameLocal(pchQueueName);
	QueueInstance* pInstance = pQueue ? eaIndexedGetUsingInt(&pQueue->eaInstances, uiInstanceID) : NULL;
	QueueMember* pMember = pInstance ? queue_FindPlayerInInstance(pInstance, iEntID) : NULL;

	if(!pInstance)
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to accept a queue invite [%s][%d] but the instance wasn't present.\n",
							iEntID,
							pchQueueName,
							uiInstanceID);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_FOUND_MESG);
		return;
	}
	if(!pMember)
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to accept a queue invite [%s][%d] but he/she wasn't found in that instance.\n",
							iEntID,
							pchQueueName,
							uiInstanceID);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_MEMBER_MESG);
		return;
	}

	if(pMember->eState != PlayerQueueState_Invited)
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to accept a queue invite [%s][%d] but he/she didn't have an invite.\n",
							iEntID,
							pchQueueName,
							uiInstanceID);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_INVITED_MESG);
		return;
	}

	{
		QueueCBStruct *pCBData = StructCreate(parse_QueueCBStruct);
		QMemberUpdateList *pList = StructCreate(parse_QMemberUpdateList);
		TransactionReturnVal *pReturn = objCreateManagedReturnVal(AcceptInvite_CB, pCBData);
		
		aslQueue_AddMemberStateChange(&pList->eaUpdates, pInstance, pMember, PlayerQueueState_InQueue);
		
		pCBData->iEntID = pMember->iEntID;
		pCBData->iQueueID = pQueue->iContainerID;
		pCBData->iInstanceID = uiInstanceID;
		
		AutoTrans_aslQueue_tr_MembersStateUpdate(pReturn, GLOBALTYPE_QUEUESERVER, GLOBALTYPE_QUEUEINFO, pQueue->iContainerID, pList);

		StructDestroySafe(parse_QMemberUpdateList, &pList);
	}
}

static void JoinActiveMap_CB(TransactionReturnVal *pReturn, void *pData)
{
	QueueCBStruct *pCBStruct = (QueueCBStruct*)pData;

	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		QueueInfo *pQueue = aslQueue_GetQueueContainer(pCBStruct->iQueueID);

		log_printf(LOG_QUEUE,"Player [%d] joined the currently active map in queue [%s].\n", pCBStruct->iEntID, pQueue->pchName);

		queue_SendResultMessage(pCBStruct->iEntID, 0, pQueue->pchName, QUEUE_JOINED_ACTIVE_MAP_MESG);
	}
	StructDestroy(parse_QueueCBStruct, pCBStruct);
}

AUTO_COMMAND_REMOTE;
void aslQueue_JoinActiveMap(ContainerID iEntID, const char* pchQueueName, U32 uiInstanceID)
{
	QueueInfo* pQueue = aslQueue_FindQueueByNameLocal(pchQueueName);
	QueueDef* pDef = SAFE_GET_REF(pQueue, hDef);
	QueueInstance* pInstance = pQueue ? eaIndexedGetUsingInt(&pQueue->eaInstances, uiInstanceID) : NULL;
	QueueMember* pMember = pInstance ? queue_FindPlayerInInstance(pInstance, iEntID) : NULL;
	QueueMap* pMap;

	if(!pInstance)
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to join the active map in queue [%s][%d] but the instance wasn't present.\n",
							iEntID,
							pchQueueName,
							uiInstanceID);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_FOUND_MESG);
		return;
	}
	if(!pMember)
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to join the active map in queue [%s][%d] but he/she wasn't found in that instance.\n",
							iEntID,
							pchQueueName,
							uiInstanceID);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_MEMBER_MESG);
		return;
	}
	
	pMap = queue_FindActiveMapForPrivateInstance(pInstance, pDef);
	if (pMap && pMap->iMapKey != pMember->iMapKey)
	{
		QueueCBStruct *pCBData = StructCreate(parse_QueueCBStruct);
		QMemberUpdateList *pList = StructCreate(parse_QMemberUpdateList);
		TransactionReturnVal *pReturn = objCreateManagedReturnVal(JoinActiveMap_CB, pCBData);
		
		aslQueue_AddMemberStateChangeEx(&pList->eaUpdates,pInstance,pMember,PlayerQueueState_InQueue,0,false,0,false,pMap->iMapKey,true);
		
		pCBData->iEntID = pMember->iEntID;
		pCBData->iQueueID = pQueue->iContainerID;
		pCBData->iInstanceID = uiInstanceID;
		
		AutoTrans_aslQueue_tr_MembersStateUpdate(pReturn, GLOBALTYPE_QUEUESERVER, GLOBALTYPE_QUEUEINFO, pQueue->iContainerID, pList);

		StructDestroySafe(parse_QMemberUpdateList, &pList);
	}
}

static void DelayOffer_CB(TransactionReturnVal *pReturn, void *pData)
{
	QueueCBStruct *pCBStruct = (QueueCBStruct*)pData;

	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		QueueInfo *pQueue = aslQueue_GetQueueContainer(pCBStruct->iQueueID);

		log_printf(LOG_QUEUE,"Player [%d] delayed the offer from queue [%s].\n", pCBStruct->iEntID, pQueue->pchName);

		queue_SendResultMessage(pCBStruct->iEntID, 0, pQueue->pchName, QUEUE_DELAYED_MESG);
	}
	StructDestroy(parse_QueueCBStruct, pCBStruct);
}

AUTO_COMMAND_REMOTE;
void aslQueue_DelayOffer(ContainerID iEntID, const char* pchQueueName, U32 uiInstanceID)
{
	QueueInfo* pQueue = aslQueue_FindQueueByNameLocal(pchQueueName);
	QueueInstance* pInstance = pQueue && uiInstanceID > 0 ? eaIndexedGetUsingInt(&pQueue->eaInstances, uiInstanceID) : NULL;
	QueueMember* pMember = pInstance ? queue_FindPlayerInInstance(pInstance, iEntID) : NULL;

	if(!pQueue)
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to delay a queue offer [%s] but it wasn't present.\n",
							iEntID,
							pchQueueName);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_FOUND_MESG);
		return;
	}
	if(!pMember)
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to delay a queue offer [%s][%d] but he/she wasn't found in that instance.\n",
							iEntID,
							pchQueueName,
							uiInstanceID);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_MEMBER_MESG);
		return;
	}
	if(pMember->eState != PlayerQueueState_Offered)
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to delay a queue offer [%s][%d] but he/she didn't have an offer.\n",
							iEntID,
							pchQueueName,
							uiInstanceID);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_OFFERED_MESG);
		return;
	}

	{
		QueueCBStruct *pCBData = StructCreate(parse_QueueCBStruct);
		QMemberUpdateList *pList = StructCreate(parse_QMemberUpdateList);

		TransactionReturnVal *pReturn = objCreateManagedReturnVal(DelayOffer_CB, pCBData);
		
		aslQueue_AddMemberStateChange(&pList->eaUpdates, pInstance, pMember, PlayerQueueState_Delaying);
		
		pCBData->iEntID = pMember->iEntID;
		pCBData->iQueueID = pQueue->iContainerID;
		pCBData->iInstanceID = pInstance->uiID;
		
		AutoTrans_aslQueue_tr_MembersStateUpdate(pReturn, GLOBALTYPE_QUEUESERVER, GLOBALTYPE_QUEUEINFO, pQueue->iContainerID, pList);

		StructDestroySafe(parse_QMemberUpdateList, &pList);
	}
}

static void Resume_CB(TransactionReturnVal *pReturn, void *pData)
{
	QueueCBStruct *pCBStruct = (QueueCBStruct*)pData;

	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		QueueInfo *pQueue = aslQueue_GetQueueContainer(pCBStruct->iQueueID);

		if (pQueue)
		{
			log_printf(LOG_QUEUE,"Player [%d] resumed queueing for [%s].\n", pCBStruct->iEntID, pQueue->pchName);

			queue_SendResultMessage(pCBStruct->iEntID, 0, pQueue->pchName, QUEUE_RESUMED_MESG);
		}
	}
	StructDestroy(parse_QueueCBStruct, pCBStruct);
}

AUTO_COMMAND_REMOTE;
void aslQueue_Resume(ContainerID iEntID, const char* pchQueueName, U32 uiInstanceID)
{
	QueueInfo* pQueue = aslQueue_FindQueueByNameLocal(pchQueueName);
	QueueInstance* pInstance = pQueue && uiInstanceID > 0 ? eaIndexedGetUsingInt(&pQueue->eaInstances, uiInstanceID) : NULL;
	QueueMember* pMember = pInstance ? queue_FindPlayerInInstance(pInstance, iEntID) : NULL;

	if(!pQueue)
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to resume a delay [%s] but it wasn't present.\n",
							iEntID,
							pchQueueName);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_FOUND_MESG);
		return;
	}
	if(!pMember)
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to resume a delay [%s][%d] but the player wasn't found in that instance.\n",
							iEntID,
							pchQueueName,
							uiInstanceID);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_MEMBER_MESG);
		return;
	}
	if(pMember->eState != PlayerQueueState_Delaying)
	{
		log_printf(LOG_QUEUE, "EntID [%d] tried to resume a delay [%s][%d] but the player wasn't delayed.\n",
							iEntID,
							pchQueueName,
							uiInstanceID);
		queue_SendResultMessage(iEntID, 0, pchQueueName, QUEUE_NOT_OFFERED_MESG);
		return;
	}

	{
		QueueCBStruct *pCBData = StructCreate(parse_QueueCBStruct);
		TransactionReturnVal *pReturn = objCreateManagedReturnVal(Resume_CB, pCBData);
		pCBData->iQueueID = pQueue->iContainerID;
		pCBData->iEntID = iEntID;
		AutoTrans_aslQueue_tr_MemberReenterQueue(pReturn, GLOBALTYPE_QUEUESERVER, GLOBALTYPE_QUEUEINFO, pQueue->iContainerID, uiInstanceID, iEntID);
	}
}


//--------------------------------------------------
// QueueServer - Map Management
//--------------------------------------------------

// A new map has finished being created, set the state to open so queue members will start to get scheduled on the map
AUTO_TRANSACTION
ATR_LOCKS(pQueueContainer, "eaInstances[]");
enumTransactionOutcome aslQueue_tr_OpenNewMap(ATR_ARGS, NOCONST(QueueInfo) *pQueueContainer, U32 uiInstanceID, S64 iMapKey, const char* pchMapName)
{
	//TODO(BH): Maximum NumMaps verification
	NOCONST(QueueInstance)* pInstance = eaIndexedGetUsingInt(&pQueueContainer->eaInstances, uiInstanceID);

	if (NONNULL(pInstance))
	{
		NOCONST(QueueMap)* pMap = StructCreateNoConst(parse_QueueMap);
		U32 iCurrentTime = timeSecondsSince2000();

		//Record the new map key
		pMap->iMapKey = iMapKey;
		
		// Record the timestamps
		pMap->iMapCreateTime = iCurrentTime;
		pMap->iStateEnteredTime = iCurrentTime;
		
		//Set the new state
		pMap->eMapState = kQueueMapState_Open;

		//Set the map name
		pMap->pchMapName = allocAddString(pchMapName);

		//And push the map onto the array
		eaPush(&pInstance->eaMaps, pMap);

		// If this is a private match, set the map key on all members currently queued
		if (queue_trh_IsPrivateMatch(pInstance->pParams))
		{
			int iMemberIdx;
			for (iMemberIdx = eaSize(&pInstance->eaUnorderedMembers)-1; iMemberIdx >= 0; iMemberIdx--)
			{
				NOCONST(QueueMember)* pMember = pInstance->eaUnorderedMembers[iMemberIdx];
				if (pMember->eState == PlayerQueueState_InQueue)
				{
					pMember->iMapKey = pMap->iMapKey;
				}
			}
		}
		return TRANSACTION_OUTCOME_SUCCESS;
	}

	return TRANSACTION_OUTCOME_FAILURE;
}

////////////////////////////////////////////////////////////////////////
// map state change transactions
////////////////////////////////////////////////////////////////////////


static void StopTracking_CB(TransactionReturnVal *pReturn, void *pData)
{
	QueueCBStruct *pCBStruct = (QueueCBStruct*)pData;

	QueueInfo *pQueue = aslQueue_GetQueueLocal(pCBStruct->iQueueID);
	if(pQueue)
		pQueue->bDirty = false;

	StructDestroy(parse_QueueCBStruct, pCBStruct);
}


AUTO_TRANSACTION
ATR_LOCKS(pQueueContainer, "eaInstances[]");
enumTransactionOutcome aslQueue_tr_StopTrackingMap(ATR_ARGS, NOCONST(QueueInfo) *pQueueContainer, U32 uiInstanceID, S64 iMapKey)
{
	NOCONST(QueueInstance)* pInstance = eaIndexedGetUsingInt(&pQueueContainer->eaInstances, uiInstanceID);
	S32 iMemberIdx, iMapIdx = NONNULL(pInstance) ? eaIndexedFindUsingInt(&pInstance->eaMaps, iMapKey) : -1;

	//Remove the map from the list of tracked maps
	if (iMapIdx >= 0)
	{
		StructDestroyNoConst(parse_QueueMap, eaRemove(&pInstance->eaMaps,iMapIdx));
	
		//Remove all the members that were on that map from the queue system.
		for(iMemberIdx = eaSize(&pInstance->eaUnorderedMembers)-1; iMemberIdx >= 0; iMemberIdx--)
		{
			NOCONST(QueueMember) *pMember = pInstance->eaUnorderedMembers[iMemberIdx];
			devassert(pMember!=NULL);	// How could this happen? It's bad because we can't then find the corresponding EntID in the order list
			if(NONNULL(pMember) && pMember->iMapKey == iMapKey)
			{
				aslQueue_trh_InstanceRemoveMemberByMemberAndIndex(pInstance, pMember, iMemberIdx);
			}
		}
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void aslQueue_MapFinish(const char *pchQueueName, S64 iMapKey)
{
	QueueInstance* pInstance = NULL;
	QueueInfo* pQueue = aslQueue_FindQueueByNameLocal(pchQueueName);
	QueueMap* pMap = pQueue ? aslQueue_FindMap(pQueue,iMapKey,&pInstance) : NULL;

	if(pMap)
	{
		//Special handling for finished maps.
			// bMaintainQueueTrackingOnMapFinish is a horrible (or maybe wonderful)  hack for NW to get things working for the first Beta.
			//   Maybe figure out how to disentagle FinishMap from autoteaming.
		
		if (g_QueueConfig.bMaintainQueueTrackingOnMapFinish)
		{
			pMap->iLastServerUpdateTime = timeSecondsSince2000();
			pMap->bPendingFinished=true;		// Resolved in aslQueue_MapStateUpdate in aslQueueServer.c
		}
		else
		{
			QueueCBStruct *pCBData = StructCreate(parse_QueueCBStruct);
			TransactionReturnVal *pReturn = objCreateManagedReturnVal(StopTracking_CB, pCBData);
		
			pQueue->bDirty = true;

			pCBData->iQueueID = pQueue->iContainerID;
			pCBData->iMapKey = iMapKey;
		
			//TODO(BH): Update leaderboard info one final time
			//aslQueueMap_Cleanup(pMap);
			AutoTrans_aslQueue_tr_StopTrackingMap(pReturn, GLOBALTYPE_QUEUESERVER, GLOBALTYPE_QUEUEINFO, pQueue->iContainerID, pInstance->uiID, iMapKey);
 		}
	}
}

// The game server is requesting we set the map active.
AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void aslQueue_MapRequestActive(const char *pchQueueName, S64 iMapKey)
{
	QueueInstance* pInstance = NULL;
	QueueInfo* pQueue = aslQueue_FindQueueByNameLocal(pchQueueName);
	QueueMap* pMap = pQueue ? aslQueue_FindMap(pQueue,iMapKey,&pInstance) : NULL;

	if(pMap)
	{
		pMap->iLastServerUpdateTime = timeSecondsSince2000();
		pMap->bPendingActive=true;		// Resolved in aslQueue_MapStateUpdate in aslQueueServer.c once we are Launched.
										// Note that aslQueue_UpdateMapLeadeboard also does the same thing. It is used to recover from map limbo
	}
}



//-------------------------------------------------------
// Transactions to put members and maps into limbo after restarting the queue server
//-------------------------------------------------------

AUTO_TRANSACTION
ATR_LOCKS(pQueueContainer, ".Eainstances");
enumTransactionOutcome aslQueue_tr_PlaceOldMapsIntoLimbo(ATR_ARGS, NOCONST(QueueInfo) *pQueueContainer)
{
	U32 iCurrentTime = timeSecondsSince2000();
	S32 iInstanceIdx, iMapIdx;
	for(iInstanceIdx = eaSize(&pQueueContainer->eaInstances)-1; iInstanceIdx >= 0; iInstanceIdx--)
	{
		NOCONST(QueueInstance)* pInstance = pQueueContainer->eaInstances[iInstanceIdx];
		for(iMapIdx = eaSize(&pInstance->eaMaps)-1; iMapIdx >= 0; iMapIdx--)
		{	
			NOCONST(QueueMap)* pMap = pInstance->eaMaps[iMapIdx];
			if(ISNULL(pMap))
			{
				eaRemove(&pInstance->eaMaps, iMapIdx);
			}
			else
			{
				pMap->eMapState = kQueueMapState_Limbo;
				pMap->iStateEnteredTime = iCurrentTime;
			}
		}
	}
	return(TRANSACTION_OUTCOME_SUCCESS);
}

AUTO_TRANSACTION
ATR_LOCKS(pQueueContainer, ".Eainstances");
enumTransactionOutcome aslQueue_tr_PlaceOldMembersIntoLimbo(ATR_ARGS, NOCONST(QueueInfo) *pQueueContainer)
{
	U32 iCurrentTime = timeSecondsSince2000();
	S32 iInstanceIdx, iMemberIdx;
	for(iInstanceIdx = eaSize(&pQueueContainer->eaInstances)-1; iInstanceIdx >= 0; iInstanceIdx--)
	{
		NOCONST(QueueInstance)* pInstance = pQueueContainer->eaInstances[iInstanceIdx];
		for(iMemberIdx = eaSize(&pInstance->eaUnorderedMembers)-1; iMemberIdx >= 0; iMemberIdx--)
		{
			NOCONST(QueueMember) *pMember = pInstance->eaUnorderedMembers[iMemberIdx];
			devassert(pMember!=NULL);	// How could this happen? It's bad because we can't then find the corresponding EntID in the order list
			
			//Everyone who was in map or accepting the offer goes into limbo, waiting for an update from their game server
			if (pMember->eState == PlayerQueueState_Countdown ||
					pMember->eState == PlayerQueueState_Accepted ||
					pMember->eState == PlayerQueueState_InMap)
			{
				pMember->eState = PlayerQueueState_Limbo;
				pMember->iStateEnteredTime = iCurrentTime;
			}
			//Else they gotta go
			else
			{
				//TODO(BH): Figure out if all these entities are online and instead re-integrate them
				aslQueue_trh_InstanceRemoveMemberByMemberAndIndex(pInstance, pMember, iMemberIdx);
}

		}
	}
	return(TRANSACTION_OUTCOME_SUCCESS);
}

//////////////////////////////////////////////////////////////////
//	Leaderboard commands
//////////////////////////////////////////////////////////////////

// This function is a ping from the Queue Server periodically and is essentially what holds the map
//   open. If the server stops sending info, the map and/or player will be put into a Limbo state because iLastServerUpdateTime will lapse
// Note that we allow off-map players and assume they still exist. There is hopefully code elsewhere in the team shutdown that disbands the team
// (and in turn the instance) if all the members go offline or something.


AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void aslQueue_UpdateMapLeaderboard(const char *pchQueueName, MapLeaderboard *pLeaderboard)
{
	QueueInfo* pQueue = aslQueue_FindQueueByNameLocal(pchQueueName);
	QueueInstance* pInstance = NULL;
	QueueMap* pMap = pQueue ? aslQueue_FindMap(pQueue,pLeaderboard->iMapKey,&pInstance) : NULL;
	U32 iCurrentTime = timeSecondsSince2000();

	if(pMap)
	{
		pMap->iLastServerUpdateTime = iCurrentTime;

		// Special Map Limbo recovery mode. If we were limboed due to a queueServer restart, we need to recognize this ping as a request active.
		if (pMap->eMapState == kQueueMapState_Limbo)
		{
			pMap->bPendingActive=true;		// Resolved in aslQueue_MapStateUpdate in aslQueueServer.c.
											// Note that aslQueue_MapRequestActive also does the same thing. It is used at map launch
		}
	}

	// Now check all the members of the leaderboard to see if they still 'exist' (we are getting a ping from their server)
	// We have to fudge if we allow players off map since they will not be OnMap in the score state. If all the players
	// leave the map or disconnect, a different system will need to deal with shutting things down as all the players will
	// remain 'in map'.
	if(pInstance)
	{
		S32 iMemberIdx;
		bool bAMemberWasReallyOnMap=false; // use to track if someone is Actually on the map and responding.

		for (iMemberIdx = 0; iMemberIdx < eaSize(&pLeaderboard->eaEntities); iMemberIdx++)
		{
			PlayerLeaderboard *pScore = eaGet(&pLeaderboard->eaEntities, iMemberIdx);
			if (pScore!=NULL && pScore->bOnMap)
			{
				bAMemberWasReallyOnMap = true;
				break;
			}
		}

		if (bAMemberWasReallyOnMap)
		{
			// This is used to track if someone is Actually on the map and responding. If we don't do a check like this, if we allow bCanBeOffMap
			//  all the members could walk out the front door and log off, and we would not be able to detect it. This may be a bit
			//  aggressive since if all the players walk out, they only have X time before the map shuts down. Really we would like some
			//  sort of query if the Entities were actually on line, regardless of GameServer.

			QMemberUpdateList *pList = StructCreate(parse_QMemberUpdateList);
		
			for (iMemberIdx = 0; iMemberIdx < eaSize(&pLeaderboard->eaEntities); iMemberIdx++)
			{
				PlayerLeaderboard *pScore = eaGet(&pLeaderboard->eaEntities, iMemberIdx);
				QueueDef *pQueueDef = GET_REF(pQueue->hDef);
				bool bCanBeOffMap = false;
				bool bConsideredOnMap = false;
				QueueMember *pMember = NULL;

				if (pQueueDef!=NULL)
				{
					bCanBeOffMap = (pQueueDef->Settings.bStayInQueueOnMapLeave || g_QueueConfig.bStayInQueueOnMapLeave);
					bConsideredOnMap = bCanBeOffMap;
	
					// if bCanBeOffMap then we're allowed to be off the instance map. We need to make the
					//    PlayerState _InMap to guarantee we transfer out of any possible Limbo state.
					// Note that we eventually consider of ANY members are on the map, as we don't want to get
					//    stuck with the map open and nobody on it.
				}
				if (pScore!=NULL)
				{
					pMember = queue_FindPlayerInInstance(pInstance, pScore->iEntID);
					if (pScore->bOnMap)
					{
						bConsideredOnMap=true;
					}
				}
				
				if (pMember!=NULL && bConsideredOnMap)
				{
					pMember->iLastMapUpdate = iCurrentTime;
			
					if(pMember->eState != PlayerQueueState_InMap)
					{
						aslQueue_AddMemberStateChange(&pList->eaUpdates, pInstance, pMember, PlayerQueueState_InMap);
					}
				}
			}
		
			if(eaSize(&pList->eaUpdates))
			{
				AutoTrans_aslQueue_tr_MembersStateUpdate(NULL, GLOBALTYPE_QUEUESERVER, GLOBALTYPE_QUEUEINFO, pQueue->iContainerID, pList); 
			}

			StructDestroySafe(parse_QMemberUpdateList, &pList);
		}
	}
}

//////////////////////////////////////////////////////////////////
//	CSR Transactions
//////////////////////////////////////////////////////////////////

static void ResetQueue_CB(TransactionReturnVal *pReturn, QueueCBStruct *pData)
{
	QueueInfo *pQueue = pData ? aslQueue_GetQueueLocal(pData->iQueueID) : NULL;
	if(pData && pQueue)
	{
		//Un-dirty the queue
		pQueue->bDirty = false;

		if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
		{
			pQueue->iNextFrameTime = timeSecondsSince2000() + 15;

			log_printf(LOG_QUEUE, "Queue [%s] successfully reset.  Will not update for 15 seconds.", pQueue->pchName);
		}
	}

	SAFE_FREE(pData);
}

AUTO_TRANSACTION
ATR_LOCKS(pQueue, ".Eainstances");
enumTransactionOutcome aslQueue_tr_ResetQueue(ATR_ARGS, NOCONST(QueueInfo) *pQueue)
{
	U32 iCurrentTime = timeSecondsSince2000();
	S32 iInstanceIdx;

	for(iInstanceIdx = eaSize(&pQueue->eaInstances)-1; iInstanceIdx >= 0; iInstanceIdx--)
	{
		NOCONST(QueueInstance)* pInstance = pQueue->eaInstances[iInstanceIdx];
		S32 iMapIdx, iMemberIdx;
		U32 *piMembersFound = NULL;

		for(iMapIdx = eaSize(&pInstance->eaMaps)-1; iMapIdx >= 0; iMapIdx--)
		{
			NOCONST(QueueMap) *pMap = eaGet(&pInstance->eaMaps, iMapIdx);
			if(ISNULL(pMap))
			{
				eaRemove(&pInstance->eaMaps, iMapIdx);
			}
			else
			{
				pMap->eMapState = kQueueMapState_Limbo;
				pMap->iStateEnteredTime = iCurrentTime;
			}
		}

		for(iMemberIdx = eaSize(&pInstance->eaUnorderedMembers)-1; iMemberIdx >= 0; iMemberIdx--)
		{
			NOCONST(QueueMember) *pMember = pInstance->eaUnorderedMembers[iMemberIdx];
			devassert(pMember!=NULL);	// How could this happen? It's bad because we can't then find the corresponding EntID in the order list

			//Everyone who was in map or accepting the offer goes into limbo, waiting for an update from their game server
			if(eaiFind(&piMembersFound, pMember->iEntID) <= 0 &&
				(pMember->eState == PlayerQueueState_Accepted ||
				 pMember->eState == PlayerQueueState_Countdown ||
				 pMember->eState == PlayerQueueState_InMap))
			{
				eaiPushUnique(&piMembersFound, pMember->iEntID);
				pMember->eState = PlayerQueueState_Limbo;
				pMember->iStateEnteredTime = iCurrentTime;
			}
			//Else they gotta go
			else
			{
				//TODO(BH): Figure out if all these entities are online and instead re-integrate them (WOLF: Probably never)
				aslQueue_trh_InstanceRemoveMemberByMemberAndIndex(pInstance, pMember, iMemberIdx);
			}
		}

		eaiDestroy(&piMembersFound);
	}

	return TRANSACTION_OUTCOME_SUCCESS;

}

AUTO_COMMAND_REMOTE;
void aslQueueCsrCmd_ResetQueue(const char *pchQueueName)
{
	QueueInfo *pQueue = aslQueue_FindQueueByNameLocal(pchQueueName);
	if(pQueue)
	{
		QueueCBStruct *pCBStruct = calloc(1,sizeof(QueueCBStruct));
		TransactionReturnVal *pReturn = objCreateManagedReturnVal(ResetQueue_CB, pCBStruct);

		pQueue->bDirty = true;
		pCBStruct->iQueueID = pQueue->iContainerID;

		log_printf(LOG_QUEUE, "CSR: Requesting queue [%s] to reset.", pQueue->pchName);

		AutoTrans_aslQueue_tr_ResetQueue(pReturn, GLOBALTYPE_QUEUESERVER, GLOBALTYPE_QUEUEINFO, pQueue->iContainerID);
	}
}

static void FixStalledQueue(QueueInfo *pQueue)
{
	int iInstanceIdx;

	for(iInstanceIdx = eaSize(&pQueue->eaInstances)-1; iInstanceIdx >= 0; iInstanceIdx--)
	{
		QueueInstance *pInstance = eaGet(&pQueue->eaInstances, iInstanceIdx);

		if(!pInstance || !pInstance->uiFailedMapLaunchCount)
			continue;

		log_printf(LOG_QUEUE, "Fixing queue [%s] instance [%d].  It had failed to launch a map for [%d] times\n",
			pQueue->pchName,
			pInstance->uiID,
			pInstance->uiFailedMapLaunchCount);

		eaDestroyStruct(&pInstance->eaNewMaps, parse_QueueMap);
		pInstance->uiFailedMapLaunchCount = 0;
		pInstance->uiNextMapLaunchTime = 0;
	}

	pQueue->bDirty = false;
}

AUTO_COMMAND_REMOTE;
void aslQueueCsrCmd_FixStalledQueue(const char *pchQueueName)
{
	QueueInfo *pQueue = aslQueue_FindQueueByNameLocal(pchQueueName);
	if(pQueue)
	{
		log_printf(LOG_QUEUE, "CSR: Requesting fix for stalled queue [%s].\n", pQueue->pchName);
		FixStalledQueue(pQueue);
	}
}

AUTO_COMMAND_REMOTE;
void aslQueueCsrCmd_FixAllStalledQueues(void)
{
	ContainerIterator queueIter;
	QueueInfo *pQueue = NULL;

	log_printf(LOG_QUEUE, "CSR: Requesting fix for ALL stalled queues.\n");

	objInitContainerIteratorFromType(GLOBALTYPE_QUEUEINFO, &queueIter);
	while (pQueue = objGetNextObjectFromIterator(&queueIter))
	{
		FixStalledQueue(pQueue);
	}
	objClearContainerIterator(&queueIter);
}

AUTO_COMMAND_REMOTE;
void aslQueueCmd_AutoFillQueues(const char* pchQueueName)
{
	QueueInfo *pQueue = aslQueue_FindQueueByNameLocal(pchQueueName);
	if (pQueue)
	{
		log_printf(LOG_QUEUE, "Debug: Queue auto-fill has been enabled for %s.\n", pchQueueName);
		pQueue->bAutoFill = true;
	}
}

AUTO_COMMAND_REMOTE;
void aslQueueCmd_NeverLaunchMaps(const char* pchQueueName, bool bEnabled)
{
	QueueInfo *pQueue = aslQueue_FindQueueByNameLocal(pchQueueName);
	if (pQueue)
	{
		if (bEnabled)
		{
			log_printf(LOG_QUEUE, "Debug: Queue (%s) will never launch maps.\n", pchQueueName);
		}
		else
		{
			log_printf(LOG_QUEUE, "Debug: Queue (%s) will begin launching maps.\n", pchQueueName);
		}
		pQueue->bNeverLaunchMaps = bEnabled;
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void aslQueue_LogMapKickedMember(const char* pchQueueName, ContainerID uMemberID, S64 iMapKey)
{
	QueueInfo* pQueue = aslQueue_FindQueueByNameLocal(pchQueueName);
	QueueDef* pDef = pQueue ? GET_REF(pQueue->hDef) : NULL;
	QueueMember* pMember = pQueue ? queue_FindPlayer(pQueue, uMemberID) : NULL;
	QueueInstance* pInstance = NULL;

	if (pDef && pMember && pMember->iMapKey == iMapKey)
	{
		char pchString[256];
		U32 uMapID = queue_GetMapIDFromMapKey(iMapKey);
		U32 uPartitionID = queue_GetPartitionIDFromMapKey(iMapKey);
		sprintf(pchString, "Map is kicking member [%d] from [MapID=%d,PartitionID=%d]", uMemberID, uMapID, uPartitionID);
		aslQueue_Log(pDef, NULL, pchString);

		ANALYSIS_ASSUME(pMember != NULL);

		if (aslQueue_FindMap(pQueue, iMapKey, &pInstance))
		{
			QMemberUpdateList *pList = StructCreate(parse_QMemberUpdateList);
			aslQueue_AddMemberStateChange(&pList->eaUpdates, pInstance, pMember, PlayerQueueState_InQueue);
			AutoTrans_aslQueue_tr_MembersStateUpdate(NULL, GLOBALTYPE_QUEUESERVER, GLOBALTYPE_QUEUEINFO, pQueue->iContainerID, pList);
			StructDestroy(parse_QMemberUpdateList, pList);
		}
	}
}

//REMOVE PLAYER FROM QUEUE?
//

///// CRAZY TEST FUNCTIONS
/*
#include "rand.h"

void aslQueue_Validate(QueueInfo *pQueue)
{
	U32 *piMembers = NULL;
	S32 iIdx = 0;
	U32 iCurrentTime = timeSecondsSince2000();

	for(iIdx = eaSize(&pQueue->eaOrderedMembers)-1; iIdx >= 0; iIdx--)
	{
		if(eaiFind(&piMembers, pQueue->eaOrderedMembers[iIdx]->iEntID) < 0)
		{
			//pQueue->eaOrderedMembers[iIdx]->iLastMapUpdate = iCurrentTime;
			eaiPush(&piMembers, pQueue->eaOrderedMembers[iIdx]->iEntID);
		}
		else
		{
			printf("[%d] found more than once\n", pQueue->eaOrderedMembers[iIdx]->iEntID);
		}
	}
}
*/
/*
AUTO_COMMAND_REMOTE;
void aslQueueCmd_AddMembers(const char *pchQueueName, int iMembers)
{
	static ContainerID iId = 1;
	S32 iIdx = 0;

	QueueInfo *pQueue = aslQueue_FindQueueByNameLocal(pchQueueName);

	if(pQueue)
	{
		QueueInstanceParams DefaultParams = {0};
		
		for(iIdx = 0; iIdx < iMembers; iIdx++)
		{
			AutoTrans_aslQueue_tr_QueueJoin(NULL, GLOBALTYPE_QUEUESERVER, GLOBALTYPE_QUEUEINFO, 
				pQueue->iContainerID, iId++, 0, 0, 0, NULL, NULL, 0, 0, 0, &DefaultParams, NULL, 0);
		}
		printf("Queue [%s] %d added\n", pchQueueName, iMembers);
	}
	else
	{
		printf("Queue [%s] not found\n", pchQueueName);
	}
}
*/
/*
AUTO_COMMAND_REMOTE;
void aslQueueCmd_RemoveFive(const char *pchQueueName)
{
	S32 iIdx = 0;

	QueueInfo *pQueue = aslQueue_FindQueueByNameLocal(pchQueueName);

	if(pQueue && eaSize(&pQueue->eaOrderedMembers))
	{
		S32 iRandomIdx = randomIntRange(0, eaSize(&pQueue->eaOrderedMembers)-1);
		QMStateUpdateList *pList = StructCreate(parse_QMStateUpdateList);
		for(iIdx = 0; iIdx < 5; iIdx++)
		{
			QueueMember *pMember = eaGet(&pQueue->eaOrderedMembers, iIdx + iRandomIdx);
			if(pMember)
			{
				aslQueue_AddMemberStateChange(pList, pMember, PlayerQueueState_Exiting);
			}		
		}
		AutoTrans_aslQueue_tr_MembersStateUpdate(NULL, GLOBALTYPE_QUEUESERVER, GLOBALTYPE_QUEUEINFO, pQueue->iContainerID, pList);
		printf("Queue [%s] 5 removed\n", pchQueueName);

		StructDestroySafe(parse_QMStateUpdateList, &pList);
	}
	else
	{
		printf("Queue [%s] not found\n", pchQueueName);
	}
}

AUTO_COMMAND_REMOTE;
void aslQueueCmd_RandomDelayFive(const char *pchQueueName)
{
	S32 iIdx = 0;

	QueueInfo *pQueue = aslQueue_FindQueueByNameLocal(pchQueueName);

	if(pQueue && eaSize(&pQueue->eaOrderedMembers))
	{
		QMStateUpdateList *pList = StructCreate(parse_QMStateUpdateList);

		for(iIdx = 0; iIdx < 5; iIdx++)
		{
			S32 iRandomIdx = randomIntRange(0, eaSize(&pQueue->eaOrderedMembers)-1);
			QueueMember *pMember = eaGet(&pQueue->eaOrderedMembers, iRandomIdx);
			if(pMember)
			{
				aslQueue_AddMemberStateChange(pList, pMember, PlayerQueueState_Delaying);
			}	

		}

		AutoTrans_aslQueue_tr_MembersStateUpdate(NULL, GLOBALTYPE_QUEUESERVER, GLOBALTYPE_QUEUEINFO, pQueue->iContainerID, pList);
		printf("Queue [%s] 5 removed\n", pchQueueName);

		StructDestroySafe(parse_QMStateUpdateList, &pList);
	}
}
*/

AUTO_TRANS_HELPER
ATR_LOCKS(pQueue, ".Eainstances");
bool aslQueue_trh_UpdateInstance(ATH_ARG NOCONST(QueueInfo)* pQueue, QInstanceUpdate* pUpdate)
{
	S32 iInstanceIdx = eaIndexedFindUsingInt(&pQueue->eaInstances, pUpdate->uiInstanceID);
	NOCONST(QueueInstance)* pInstance = eaGet(&pQueue->eaInstances, iInstanceIdx);
	
	if (NONNULL(pInstance))
	{
		if (pUpdate->bRemove)
		{
			StructDestroyNoConst(parse_QueueInstance, eaRemove(&pQueue->eaInstances,iInstanceIdx));
		}
		else
		{
			pInstance->bOvertime = pUpdate->bOvertime;
			pInstance->bNewMap = pUpdate->bNewMap;
		}
	}
	return true;
}

AUTO_TRANSACTION
ATR_LOCKS(pQueue, ".Eainstances");
enumTransactionOutcome aslQueue_tr_UpdateInstance(ATR_ARGS, NOCONST(QueueInfo) *pQueue, 
												  QInstanceUpdate* pUpdate)
{
	if (!aslQueue_trh_UpdateInstance(pQueue, pUpdate))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pQueue, ".Eainstances");
bool aslQueue_trh_UpdateMap(ATH_ARG NOCONST(QueueInfo)* pQueue, QMapUpdate* pUpdate)
{
	U32 iCurrentTime = timeSecondsSince2000();
	S32 iInstanceIdx = eaIndexedFindUsingInt(&pQueue->eaInstances, pUpdate->uiInstanceID);
	NOCONST(QueueInstance)* pInstance = eaGet(&pQueue->eaInstances, iInstanceIdx);

	if (NONNULL(pInstance))
	{
		S32 iMapIdx = eaIndexedFindUsingInt(&pInstance->eaMaps, pUpdate->iMapKey);
		NOCONST(QueueMap)* pMap = eaGet(&pInstance->eaMaps, iMapIdx);
		if (NONNULL(pMap))
		{
			if (pUpdate->eState == kQueueMapState_Destroy)
			{
				S64 iMapKey = 0;
				int i;

				iMapKey = pInstance->eaMaps[iMapIdx]->iMapKey;

				StructDestroyNoConst(parse_QueueMap, eaRemove(&pInstance->eaMaps, iMapIdx));

				// Clean up the member list
				for(i=eaSize(&pInstance->eaUnorderedMembers)-1;i>=0;i--)
				{
					NOCONST(QueueMember)* pMember = pInstance->eaUnorderedMembers[i];
					if(pMember->iMapKey == iMapKey)
					{
						if (pMember->eState == PlayerQueueState_Offered ||
							pMember->eState == PlayerQueueState_Accepted)
						{
							// If the member is in the offered or accepted state, place them back into the queue 
							bool bIsPrivate = queue_trh_IsPrivateMatch(pInstance->pParams);
							aslQueue_trh_MemberChangeState(ATR_EMPTY_ARGS, pMember, PlayerQueueState_InQueue, bIsPrivate);
						}
						else
						{
							// Remove player from the instance
							aslQueue_trh_InstanceRemoveMemberByMemberAndIndex(pInstance, pMember, i);
						}
					}
				}
				return true;
			}
			else if (pUpdate->eState != kQueueMapState_None)
			{
				//update the map state
				pMap->eMapState = pUpdate->eState;
				pMap->iStateEnteredTime = iCurrentTime;
			}
			if (pUpdate->eState == kQueueMapState_Launched)
			{
				pMap->iMapLaunchTime = iCurrentTime;
			}
			//Set the average wait time
			if (pUpdate->uAverageWaitTime > 0)
			{
				pInstance->iAverageWaitTime = pUpdate->uAverageWaitTime;
			}
			return true;
		}
	}
	return false;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pQueue, ".Eainstances, .Hdef");
bool aslQueue_trh_UpdateMember(ATH_ARG NOCONST(QueueInfo)* pQueue, QMemberUpdate* pUpdate)
{
	S32 iInstanceIdx = eaIndexedFindUsingInt(&pQueue->eaInstances, pUpdate->uiInstanceID);
	NOCONST(QueueInstance)* pInstance = eaGet(&pQueue->eaInstances, iInstanceIdx);
	QueueDef* pDef = GET_REF(pQueue->hDef);

	if (ISNULL(pInstance))
	{
		return false;
	}
	else
	{
		if (pUpdate->eState == PlayerQueueState_Exiting)
		{
			if (!aslQueue_trh_RemovePlayerFromInstance(pInstance, pDef, pUpdate->uiMemberID, PlayerQueueState_None))
			{
				return false;
			}
		}
		else
		{
			NOCONST(QueueMember) *pMember = queue_trh_FindPlayerInInstance(pInstance, pUpdate->uiMemberID);
			if (NONNULL(pMember))
			{
				if (pUpdate->bUpdateTeam)
				{
					pMember->iTeamID = pUpdate->uiTeamID;
				}
				if (pUpdate->bUpdateGroup)
				{
					pMember->iGroupIndex = pUpdate->iGroupIndex;
				}
				if (pUpdate->bUpdateMap)
				{
					pMember->iMapKey = pUpdate->iMapKey;
				}
				if (pMember->eState == PlayerQueueState_Delaying && pUpdate->eState == PlayerQueueState_InQueue)
				{
					if (!aslQueue_trh_MemberReenterQueue(ATR_EMPTY_ARGS, pInstance, GET_REF(pQueue->hDef), pUpdate->uiMemberID))
					{
						return false;
					}
				}
				else if (pUpdate->eState != PlayerQueueState_None)
				{
					bool bIsPrivate = queue_trh_IsPrivateMatch(pInstance->pParams);
					aslQueue_trh_MemberChangeState(ATR_EMPTY_ARGS, pMember, pUpdate->eState, bIsPrivate);

					// Set the map time as each member is invited. This prevents queue from timing out as members leave and join
					if(pUpdate->eState == PlayerQueueState_Offered)
					{
						// reset map timer
						S32 iMapIdx;
						for(iMapIdx = 0; iMapIdx < eaSize(&pInstance->eaMaps); ++iMapIdx)
						{
							NOCONST(QueueMap) *pMap = pInstance->eaMaps[iMapIdx];
							if(pMap->iMapKey == pUpdate->iMapKey)
							{
								pMap->iStateEnteredTime = timeSecondsSince2000();
							}
						}
					}
				}
			}
		}
	}
	return true;
}

AUTO_TRANSACTION
ATR_LOCKS(pQueue, ".Eainstances, .Hdef");
enumTransactionOutcome aslQueue_tr_MembersStateUpdate(ATR_ARGS, NOCONST(QueueInfo)* pQueue, QMemberUpdateList* pList)
{
	S32 iUpdateIdx, iNumUpdates = eaSize(&pList->eaUpdates);
	for(iUpdateIdx = 0; iUpdateIdx < iNumUpdates; iUpdateIdx++)
	{
		QMemberUpdate* pUpdate = pList->eaUpdates[iUpdateIdx];
		if (!aslQueue_trh_UpdateMember(pQueue, pUpdate))
		{
			return TRANSACTION_OUTCOME_FAILURE;
		}
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pQueue, ".Eainstances, .Hdef");
enumTransactionOutcome aslQueue_tr_PerformUpdate(ATR_ARGS, NOCONST(QueueInfo)* pQueue, QUpdateData* pData)
{
	S32 i;
	for (i = 0; i < eaSize(&pData->eaList); i++)
	{
		QGeneralUpdate* pUpdate = pData->eaList[i];
		if (pUpdate->pInstanceUpdate)
		{
			if (!aslQueue_trh_UpdateInstance(pQueue, pUpdate->pInstanceUpdate))
			{
				return TRANSACTION_OUTCOME_FAILURE;
			}
		}
		else if (pUpdate->pMapUpdate)
		{
			if (!aslQueue_trh_UpdateMap(pQueue, pUpdate->pMapUpdate))
			{
				return TRANSACTION_OUTCOME_FAILURE;
			}
		}
		else if (pUpdate->pMemberUpdate)
		{
			if (!aslQueue_trh_UpdateMember(pQueue, pUpdate->pMemberUpdate))
			{
				return TRANSACTION_OUTCOME_FAILURE;
			}
		}
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  We use this to send certain queue data that used to be sent via MapVar. The map/partition has started
// send the GameInfo it may need. If we don't have an instance representing that map, we are a non-queue-started
// queue map so we should return NULL. This is expected if someone is testing and mapmoves to a queue map,
// or for front-door-entered queue maps.

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
QueueGameInfo* aslQueue_RequestQueueGameInfo(S64 iMapKey)
{
    QueueGameInfo *pQueueGameInfo = NULL;
	ContainerIterator queueIter;
	QueueInfo *pQueue = NULL;

	objInitContainerIteratorFromType(GLOBALTYPE_QUEUEINFO, &queueIter);
	while (pQueue = objGetNextObjectFromIterator(&queueIter))
	{
		QueueDef* pQueueDef = GET_REF(pQueue->hDef);
		QueueInstance* pInstance;
		QueueMap* pMap = aslQueue_FindMap(pQueue, iMapKey, &pInstance);

		// I don't think pInstance can be NULL if pMap is not. But...
		if (pMap!=NULL && pInstance!=NULL && pQueueDef!=NULL)
		{
		    pQueueGameInfo = StructCreate(parse_QueueGameInfo);
			queue_FillGameInfo(pQueueGameInfo, pQueueDef, pInstance, pMap);
		}
	}

	objClearContainerIterator(&queueIter);
	
	// Can be NULL
    return pQueueGameInfo;
}	


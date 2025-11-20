/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GameServerLib.h"


#include "ActivityCommon.h"
#include "alerts.h"
#include "Character.h"
#include "chatCommon.h"
#include "chatCommonStructs.h"
#include "cmdServerCombat.h"
#include "CombatConfig.h"
#include "Entity.h"
#include "EntityIterator.h"
#include "EntityLib.h"
#include "GameServerLib.h"
#include "gslChat.h"
#include "gslDoorTransition.h"
#include "gslMapState.h"
#include "gslMapTransfer.h"
#include "gslMapVariable.h"
#include "gslQueue.h"
#include "gslQueue_h_ast.h"
#include "gslTeam.h"
#include "gslTransactions.h"
#include "file.h"
#include "GameStringFormat.h"
#include "Guild.h"
#include "NotifyCommon.h"
#include "OfficerCommon.h"
#include "Player.h"
#include "queue_common.h"
#include "queue_common_h_ast.h"
#include "queue_common_structs.h"
#include "queue_common_structs_h_ast.h"
#include "RegionRules.h"
#include "rewardCommon.h"
#include "StringCache.h"
#include "Team.h"
#include "WorldGrid.h"
#include "AutoTransDefs.h"
#include "gslPartition.h"
#include "ChoiceTable.h"
#include "ServerLib.h"

#include "Entity_h_ast.h"
#include "Player_h_ast.h"
#include "queue_common_h_ast.h"
#include "Team_h_ast.h"

#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "Autogen/ChatServer_autogen_remotefuncs.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/ServerLib_autogen_remotefuncs.h"

#define QUEUE_NOT_FOUND_MESG "QueueServer_QueueNotFound"
#define QUEUE_RESTRICTIONS_MESG "QueueServer_DoesNotMeetRestrictions"
#define QUEUE_INVITE_FAILED_MESG "QueueServer_QueueInviteFailed"
#define QUEUE_PLAYER_NOT_FOUND_MESG "QueueServer_PlayerNotFound"
#define QUEUE_PLAYERS_NOT_READY_MESG "QueueServer_PlayersNotReady"
#define QUEUE_INVALID_PARAMS_MESG "QueueServer_InvalidParams"
#define QUEUE_INVALID_GROUP_MESG "QueueServer_InvalidGroup"
#define QUEUE_TRANSACTION_FAILURE_MESG "QueueServer_TransactionFailed"
#define QUEUE_NOT_OFFERED_MESG "QueueServer_NotOffered"
#define QUEUE_NOT_DELAYED_MESG "QueueServer_NotDelayed"
#define QUEUE_NOT_MEMBER_MESG "QueueServer_NotAMember"
#define QUEUE_NOT_TEAMMEMBER_MESG "QueueServer_Team_NotOnTeam"
#define QUEUE_NOT_TEAMLEADER_MESG "QueueServer_Team_NotTeamLeader"
#define QUEUE_IN_TEAM "QueueServer_Team_InTeam"
#define QUEUE_INVALID_MAP_MESG "QueueServer_PvPMap"
#define QUEUE_ACCEPTED_MESG "QueueServer_Accepted"
#define QUEUE_POHO_MESG "QueueServer_PowerhouseError"
#define QUEUE_BEYOND_MAX_JOIN_TIME_MESG "QueueServer_BeyondJoinTimeLimit"
#define QUEUE_MAP_FULL_MESG "QueueServer_MapFull"
#define QUEUE_MAP_NOT_FOUND_MESG "QueueServer_MapNotFound"
#define QUEUE_MAP_SETTING_INVALID_MESG "QueueServer_MapSettingInvalid"
#define QUEUE_CANNOT_CONCEDE_MESG "QueueServer_CannotConcede"
#define QUEUE_CANNOT_VOTE_KICK_MESG "QueueServer_CannotVoteKick"
#define QUEUE_INVITED_PLAYER_CANNOT_JOIN_MESG "QueueServer_InvitedPlayerCannotJoin"
#define QUEUE_PLAYER_IGNORING_INVITES_MESG "QueueServer_PlayerIgnoringInvites"
#define QUEUE_PRIVATE_NAME_PROFANITY "QueueServer_PrivateNameContainsProfanity"
#define QUEUE_PRIVATE_NAME_MINLENGTH "QueueServer_PrivateNameTooShort"
#define QUEUE_PRIVATE_NAME_MAXLENGTH "QueueServer_PrivateNameTooLong"
#define QUEUE_UGC_CHARACTER_NOT_ALLOWED "QueueServer_UGCCharacterNotAllowed"
#define QUEUE_PRIVATE_QUEUES_DISABLED "QueueServer_PrivateDisabled"
#define QUEUE_PRIVATE_LEVEL_OUT_OF_RANGE "QueueServer_LevelOutOfRange"
#define QUEUE_PRIVATE_LEVEL "QueueServer_LevelDifference"
#define QUEUE_ENT_PRIVATE_LEVEL_OUT_OF_RANGE "QueueServer_EntLevelOutOfRange"

typedef struct GSLQueueCBData
{
	const char* pchQueueName; AST( POOL_STRING )
	U32 uiInstanceID;
	U32 uiSubjectID;
	GlobalType eLocationType;
	ContainerID uLocationID;
	char pcErrorName[256];
	void *pUserData;
} GSLQueueCBData;


extern int g_iShowDebugQueues;
extern int g_iDebugQueueServer;
extern U32 s_uiQueueListNextRequestTime;
extern U32 s_uiQueueNewDataFromQueueServer;  	// Tells ProcessPlayerQueues that we got new data so it can update any pending players

AUTO_CMD_INT(g_iShowDebugQueues, EnableDebuggingQueues) ACMD_ACCESSLEVEL(9) ACMD_HIDE;
AUTO_CMD_INT(g_iDebugQueueServer, DebugQueueServer) ACMD_ACCESSLEVEL(9);

static bool gslQueue_DisableJoinOrCreateOnCurrentMap(Entity* pEnt)
{
	if (gslQueue_IsQueueMap())
	{
		ZoneMapType eMapType = zmapInfoGetMapType(NULL);
		if (eaiFind(&g_QueueConfig.peAllowQueuingOnQueueMaps, eMapType) < 0)
		{
			return true;
		}
	}
	else if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pPlayerQueueInfo)
	{
		PlayerQueueInfo* pInfo = pEnt->pPlayer->pPlayerQueueInfo;
		if (pInfo && pInfo->pQueueInstantiationInfo)
		{
			const char* pchMapName = pInfo->pQueueInstantiationInfo->pchMapName;
			ZoneMapInfo* pZoneMapInfo = zmapInfoGetByPublicName(pchMapName);
			if (!pZoneMapInfo)
			{
				return true;
			}
			else
			{
				ZoneMapType eMapType = zmapInfoGetMapType(pZoneMapInfo);
				if (eaiFind(&g_QueueConfig.peAllowQueuingOnQueueMaps, eMapType) < 0)
				{
					return true;
				}
			}
		}
	}
	return false;
}

GSLQueueCBData *gslQueue_MakeCBData(const char* pchQueueName, U32 uiInstanceID, const char *pcErrorName, void *pData)
{
	GSLQueueCBData *pCBData = calloc(1, sizeof(GSLQueueCBData));
	pCBData->pchQueueName = allocAddString(pchQueueName);
	pCBData->uiInstanceID = uiInstanceID;
	if (pcErrorName) {
		strcpy_s(pCBData->pcErrorName, 256, pcErrorName);
	}
	pCBData->pUserData = pData;
	return pCBData;
}

static QueueJoinFlags gslQueue_GetJoinFlags(QueueDef* pDef, U32 bJoinNewMap, U32 bAutoAcceptOffer, U32 bTeamJoin)
{
	QueueJoinFlags eJoinFlags = kQueueJoinFlags_None;
	if (bJoinNewMap)
		eJoinFlags |= kQueueJoinFlags_JoinNewMap;
	if (bAutoAcceptOffer)
		eJoinFlags |= kQueueJoinFlags_AutoAcceptOffers;
	if (bTeamJoin && pDef->Settings.bIgnoreLevelBandsForTeams)
		eJoinFlags |= kQueueJoinFlags_IgnoreLevelRestrictions;

	return eJoinFlags;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Error reporting callback
///////////////////////////////////////////////////////////////////////////////////////////

const char* gslQueue_GetResultMessageEntityName(Entity* pMessageEnt, S32 eTargetType, U32 iTargetEntID)
{
	Entity *pTargetEnt = iTargetEntID ? entFromContainerIDAnyPartition(eTargetType, iTargetEntID) : NULL;
	if (pTargetEnt!=NULL)
	{
		return(entGetPersistedName(pTargetEnt));
	}
	if (iTargetEntID!=0 && eTargetType==GLOBALTYPE_ENTITYPLAYER)
	{
		// We didn't have the entity on this server. Hmm. Let's try the team. That's the most likely time we'd get such a thing
		Team* pTeam = team_GetTeam(pMessageEnt);
		if (pTeam!=NULL)
		{
			TeamMember *pTeamMember = team_FindMemberID(pTeam, iTargetEntID);
			if (pTeamMember!=NULL)
			{
				return(pTeamMember->pcName);
			}
		}
	}
	return(NULL);
}


void gslQueue_ResultMessageEx(U32 iEntID, 
							  S32 eSubjectType, U32 iSubjectEntID, 
							  S32 eTargetType, U32 iTargetEntID,
							  const char *pcQueueName, const char *pcMessageKey,
							  NotifyType eNotifyType)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iEntID);
	QueueDef *pDef = queue_DefFromName(pcQueueName);

	// Yuck. Private Queues use the Subject. All other messages use the Target.
	// We're also out of luck if the player is not on the local GameServer. Do some team fanciness.

	const char* pcTargetName=gslQueue_GetResultMessageEntityName(pEnt, eTargetType, iTargetEntID);
	const char* pcSubjectName=gslQueue_GetResultMessageEntityName(pEnt, eSubjectType, iSubjectEntID);

	if (pEnt) {
		char *estrBuffer = NULL;
		estrStackCreate(&estrBuffer);

		if (pDef)
		{
			EventDef *pEventDef = EventDef_Find(pDef->Requirements.pchRequiredEvent);
			if (pEventDef!=NULL)
			{
				entFormatGameMessageKey(pEnt, &estrBuffer, pcMessageKey,
					STRFMT_DISPLAYMESSAGE("Queue", pDef->displayNameMesg),
					STRFMT_DISPLAYMESSAGE("Event", pEventDef->msgDisplayName),
					STRFMT_STRING("Subject", NULL_TO_EMPTY(pcSubjectName)),
					STRFMT_STRING("Target", NULL_TO_EMPTY(pcTargetName)),
					STRFMT_END);
			}
			else
			{
				entFormatGameMessageKey(pEnt, &estrBuffer, pcMessageKey,
					STRFMT_DISPLAYMESSAGE("Queue", pDef->displayNameMesg),
					STRFMT_STRING("Event", ""),
					STRFMT_STRING("Subject", NULL_TO_EMPTY(pcSubjectName)),
					STRFMT_STRING("Target", NULL_TO_EMPTY(pcTargetName)),
					STRFMT_END);
			}
		}
		else
		{
			entFormatGameMessageKey(pEnt, &estrBuffer, pcMessageKey,
				STRFMT_STRING("Queue", NULL_TO_EMPTY(pcQueueName)),
				STRFMT_STRING("Event", ""),
				STRFMT_STRING("Subject", NULL_TO_EMPTY(pcSubjectName)),
				STRFMT_STRING("Target", NULL_TO_EMPTY(pcTargetName)),
				STRFMT_END);
		}
		// send the pcMessageKey as the logical string so the client can use it to filter the notification
		notify_NotifySend(pEnt, eNotifyType, estrBuffer, pcMessageKey, NULL);
		estrDestroy(&estrBuffer);
	}
}

AUTO_COMMAND_REMOTE;
void gslQueue_SendWarning(U32 iEntID, U32 iTargetEntID, const char *pcQueueName, const char *pcMessageKey)
{
	gslQueue_ResultMessageEx(iEntID, 0, 0, GLOBALTYPE_ENTITYPLAYER, iTargetEntID, pcQueueName, pcMessageKey, kNotifyType_PvPWarning);
}

AUTO_COMMAND_REMOTE;
void gslQueue_ResultMessage(U32 iEntID, U32 iTargetEntID, const char *pcQueueName, const char *pcMessageKey)
{
	gslQueue_ResultMessageEx(iEntID, 0, 0, GLOBALTYPE_ENTITYPLAYER, iTargetEntID, pcQueueName, pcMessageKey, kNotifyType_PvPGeneral);
}



///////////////////////////////////////////////////////////////////////////////////////////
// Validate that a challenge can be issued
///////////////////////////////////////////////////////////////////////////////////////////

static bool gslQueue_CanChallenge(QueueDef* pDef, const char* pchAffiliationA, const char* pchAffiliationB)
{
	S32 i, j;
	for (i = eaSize(&pDef->eaGroupDefs)-1; i >= 0; i--)
	{
		QueueGroupDef* pGroupDefA = pDef->eaGroupDefs[i];
	
		if (pGroupDefA->pchAffiliation && stricmp(pGroupDefA->pchAffiliation, pchAffiliationA)!=0)
		{
			continue;
		}
		for (j = eaSize(&pDef->eaGroupDefs)-1; j >= 0; j--)
		{
			QueueGroupDef* pGroupDefB = pDef->eaGroupDefs[j];

			if (pGroupDefA == pGroupDefB)
			{
				continue;
			}
			if (pGroupDefB->pchAffiliation && stricmp(pGroupDefB->pchAffiliation, pchAffiliationB)!=0)
			{
				continue;
			}
			return true;
		}
	}
	return false;
}

AUTO_COMMAND_REMOTE;
void gslQueue_SendMessageToPrivateChatChannel(U32 uEntID, 
											  U32 uOrigOwnerID, 
											  U32 uInstanceID, 
											  const char* pchMessageKey,
											  U32 uSubjectID)
{
	Entity* pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, uEntID);
	if (pEntity && pEntity->pPlayer)
	{
		char* estrChannelName = NULL;
		ANALYSIS_ASSUME(pEntity != NULL);
		estrStackCreate(&estrChannelName);
		queue_GetPrivateChatChannelName(&estrChannelName, uOrigOwnerID, uInstanceID);
		gslQueue_SendSystemMessageToChannel(pEntity, estrChannelName, pchMessageKey, uSubjectID, true, false);
		estrDestroy(&estrChannelName);
	}
}

AUTO_COMMAND_REMOTE;
void gslQueue_CreateOrJoinPrivateChatChannel(U32 uEntID, U32 uOwnerID, U32 uInstanceID)
{
	Entity* pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, uEntID);
	if (pEntity && pEntity->pPlayer)
	{
		char* estrChannelName = NULL;
		estrStackCreate(&estrChannelName);
		queue_GetPrivateChatChannelName(&estrChannelName, uOwnerID, uInstanceID);
		RemoteCommand_ChatServerJoinOrCreateChannel_Special(GLOBALTYPE_CHATSERVER, 0, 
			pEntity->pPlayer->accountID, pEntity->myContainerID, estrChannelName, CHANNEL_SPECIAL_PVP);
		estrDestroy(&estrChannelName);
	}
}

AUTO_COMMAND_REMOTE;
void gslQueue_LeavePrivateChatChannel(U32 uEntID, U32 uOwnerID, U32 uInstanceID)
{
	Entity* pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, uEntID);
	if (pEntity)
	{
		char* estrChannelName = NULL;
		estrStackCreate(&estrChannelName);
		queue_GetPrivateChatChannelName(&estrChannelName, uOwnerID, uInstanceID);
		RemoteCommand_ChatServerLeaveChannel_Special(GLOBALTYPE_CHATSERVER, 0, 
			pEntity->pPlayer->accountID, pEntity->myContainerID, estrChannelName);
		estrDestroy(&estrChannelName);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// Refresh queue list on client
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_NAME(queue_refreshQueues) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueueRefresh(Entity *pEnt)
{
	PERFINFO_AUTO_START_FUNC();

	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	if(entity_IsUGCCharacter(pEnt))
	{
		// do not refresh queues for UGC characters
		PERFINFO_AUTO_STOP();
		return;
	}

	if (pEnt->pPlayer)
	{
		gslQueue_RequestPlayerQueuesProcess(pEnt);
		
	}

	PERFINFO_AUTO_STOP();
}


///////////////////////////////////////////////////////////////////////////////////////////
// Invite to a private queue
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void gslQueue_SendInvite(U32 uiInviteeID, U32 uiInviterID, U32 uiInviterAccountID,
						 const char* pchQueueName, U32 uiInstanceID, QueueInstanceParams* pParams,
						 QueueInviteInfo* pCreateData)
{
	Entity* pInvitee = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, uiInviteeID);
	QueueDef* pDef = queue_DefFromName(pchQueueName);

	if (!pInvitee || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pInvitee)))
	{
		return;
	}

	if (pInvitee->pPlayer && pDef)
	{
		const char* pchAffiliation = queue_EntGetQueueAffiliation(pInvitee);
		U32 uiInviteeTeamID = team_IsMember(pInvitee) ? pInvitee->pTeam->iTeamID : 0;
		S32 iLevel = entity_GetSavedExpLevel(pInvitee);
		S32 iRank = Officer_GetRank(pInvitee);
		
		// Check to see if the invited player can join the match
		if (gslQueue_IsLeaverPenaltyEnabled(entGetPartitionIdx(pInvitee)) ||
			gslQueue_DisableJoinOrCreateOnCurrentMap(pInvitee) ||
			gslEntCannotUseQueueInstanceMsgKey(pInvitee, pParams, pDef, false, false))
		{
			queue_SendResultMessage(uiInviterID, uiInviteeID, pchQueueName, QUEUE_INVITED_PLAYER_CANNOT_JOIN_MESG);
			return;
		}
		// Fail the invite if the invitee has whitelisting enabled
		if (!entIsWhitelistedEx(pInvitee, uiInviterID, uiInviterAccountID, kPlayerWhitelistFlags_PvPInvites))
		{
			queue_SendResultMessage(uiInviterID, uiInviteeID, pchQueueName, QUEUE_PLAYER_IGNORING_INVITES_MESG);
			return;
		}
		// Verify that the challenge is valid
		if (pCreateData && !gslQueue_CanChallenge(pDef, pCreateData->pchAffiliation, pchAffiliation))
		{
			queue_SendResultMessage(uiInviterID, uiInviteeID, pchQueueName, QUEUE_INVITE_FAILED_MESG);
			return;
		}

		if(entity_IsUGCCharacter(pInvitee))
		{
			gslQueue_ResultMessage(pInvitee->myContainerID, 0, pchQueueName, QUEUE_UGC_CHARACTER_NOT_ALLOWED);
			return;
		}

		gslQueueRefresh(pInvitee);
		VerifyServerTypeExistsInShard(GLOBALTYPE_QUEUESERVER);

		if (pCreateData)
		{
			QueueInviteInfo* pInviteData = StructCreate(parse_QueueInviteInfo);
			pInviteData->uEntID = uiInviteeID;
			pInviteData->iLevel = iLevel;
			pInviteData->iRank = iRank;
			pInviteData->uTeamID = uiInviteeTeamID;
			pInviteData->pchAffiliation = StructAllocString(pchAffiliation);
			RemoteCommand_aslQueue_Create(GLOBALTYPE_QUEUESERVER, 0, uiInviterID, uiInviterAccountID, pCreateData->uTeamID, pCreateData->iLevel, pCreateData->iRank, pCreateData->pchAffiliation, pchQueueName, pParams, pInviteData, NULL, NULL); 
			StructDestroy(parse_QueueInviteInfo, pInviteData);
		}
		else
		{
			RemoteCommand_aslQueue_Invite(GLOBALTYPE_QUEUESERVER, 0, uiInviterID, pchQueueName, uiInstanceID, uiInviteeID, uiInviteeTeamID, iLevel, iRank, pchAffiliation);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// Automatically fill in default instance parameters for an entity
///////////////////////////////////////////////////////////////////////////////////////////

static bool gslQueue_GetValidInstanceParams(Entity* pEnt, QueueDef* pDef, NOCONST(QueueInstanceParams)* pParams)
{
	if (!gslEntCannotUseQueueMsgKey(pEnt, pDef, false, false))
	{
		S32 i, iEntLevel = entity_GetSavedExpLevel(pEnt);

		if (pDef->MapRules.bChallengeMatch)
		{
			pParams->uiOwnerID = entGetContainerID(pEnt);
		}
		else
		{
			pParams->uiOwnerID = 0;
		}
		if (pDef->Requirements.bRequireSameGuild)
		{
			pParams->uiGuildID = guild_GetGuildID(pEnt);
		}
		if (!pDef->Settings.bRandomMap)
		{
			ChoiceTable *pChoiceTable = GET_REF(pDef->QueueMaps.hMapChoiceTable);
			const char* pchMapName = NULL;
			if (pChoiceTable)
			{
				WorldVariable* pVar = choice_ChooseValue(pChoiceTable, pDef->QueueMaps.pchTableEntry, 0, 0, timeSecondsSince2000());
				if(pVar->eType == WVAR_MAP_POINT)
				{
					pchMapName = pVar->pcZoneMap;
				}
			}
			else
			{
				pchMapName = queue_GetMapNameByIndex(pDef, 0);
			}
			if (pchMapName)
			{
				pParams->pchMapName = allocAddString(pchMapName);
			}
			if(eaSize(&pDef->QueueMaps.eaCustomMapTypes))
			{
				pParams->eGameType = ea32Size(&pDef->QueueMaps.eaCustomMapTypes[0]->puiPVPGameModes) ? pDef->QueueMaps.eaCustomMapTypes[0]->puiPVPGameModes[0] : kPVPGameType_None;
			}
		}
		for (i = eaSize(&pDef->eaLevelBands)-1; i >= 0; i--)
		{
			QueueLevelBand* pLevelBand = pDef->eaLevelBands[i];
			if (	(pLevelBand->iMinLevel == 0 || pLevelBand->iMinLevel <= iEntLevel)
				&&	(pLevelBand->iMaxLevel == 0 || pLevelBand->iMaxLevel >= iEntLevel))
			{
				pParams->iLevelBandIndex = i;
				return true;
			}
		}
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Validate Game Settings
///////////////////////////////////////////////////////////////////////////////////////////

static bool gslQueue_ValidateSetting(QueueDef* pDef, S32 iLevelBandIndex, const char* pchQueueVarName, S32 iValue)
{
	QueueVariableData* pQueueVarData = queue_FindVariableData(pDef, iLevelBandIndex, pchQueueVarName);
	
	if (!pQueueVarData || !pQueueVarData->pSettingData)
	{
		return false;
	}
	if (iValue < pQueueVarData->pSettingData->iMinValue)
	{
		return false;
	}
	if (pQueueVarData->pSettingData->iMaxValue && iValue > pQueueVarData->pSettingData->iMaxValue)
	{
		return false;
	}
	return true;
}

static bool gslQueue_ValidateSettings(QueueDef* pDef, S32 iLevelBandIndex, QueueGameSettings* pSettings)
{
	if (pSettings)
	{
		S32 i;
		for (i = eaSize(&pSettings->eaSettings)-1; i >= 0; i--)
		{
			QueueGameSetting* pSetting = pSettings->eaSettings[i];
			if (!gslQueue_ValidateSetting(pDef, iLevelBandIndex, pSetting->pchQueueVarName, pSetting->iValue))
			{
				return false;
			}
		}
	}
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Create a queue
///////////////////////////////////////////////////////////////////////////////////////////

static void gslQueue_Create(Entity *pEnt,
							const char* pchQueueName,
							ContainerID iTeamID,
							QueueInstanceParams* pParams,
							QueueIntArray* pInviteList,
							QueueGameSettings* pSettings)
{
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	VerifyServerTypeExistsInShard(GLOBALTYPE_QUEUESERVER);

	if(g_QueueConfig.bDisablePrivateQueues && pParams && pParams->uiOwnerID != 0)
	{
		gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_PRIVATE_QUEUES_DISABLED);
		return;
	}

	if (pEnt->pPlayer)
	{
		QueuePrivateNameInvalidReason eInvalidNameReason;
		QueueDef* pDef;
		const char *pchMsg;
		const char* pchAffiliation = queue_EntGetQueueAffiliation(pEnt);
		S32 iLevel = entity_GetSavedExpLevel(pEnt);
		S32 iRank = Officer_GetRank(pEnt);
		
		// Validation
		if (gslQueue_DisableJoinOrCreateOnCurrentMap(pEnt))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_INVALID_MAP_MESG);
			return;
		}
		if (gslQueue_IsLeaverPenaltyEnabled(entGetPartitionIdx(pEnt)))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_INVALID_MAP_MESG);
			return;
		}
		if (!(pDef = queue_DefFromName(pchQueueName)))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_NOT_FOUND_MESG);
			return;
		}

		eInvalidNameReason = queue_GetPrivateNameInvalidReason(pParams->pchPrivateName);
		switch (eInvalidNameReason)
		{
			xcase kQueuePrivateNameInvalidReason_MinLength:
			{
				gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_PRIVATE_NAME_MINLENGTH);
				return;
			}
			xcase kQueuePrivateNameInvalidReason_MaxLength:
			{
				gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_PRIVATE_NAME_MAXLENGTH);
				return;
			}
			xcase kQueuePrivateNameInvalidReason_Profanity:
			{
				gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_PRIVATE_NAME_PROFANITY);
				return;
			}
		}
		if (!queue_CheckInstanceParamsValid(pEnt, pDef, pParams))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_INVALID_PARAMS_MESG);
			return;
		}
		if (pchMsg = gslEntCannotUseQueueInstanceMsgKey(pEnt, pParams, pDef, false, false))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, pchMsg);
			return;
		}
		if (!pParams->uiOwnerID && 
			((pInviteList && ea32Size(&pInviteList->piArray)) || 
			 (pSettings && eaSize(&pSettings->eaSettings))))
		{
			return;
		}
		if (!gslQueue_ValidateSettings(pDef, pParams->iLevelBandIndex, pSettings))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_MAP_SETTING_INVALID_MESG);
			return;
		}

		// Challenge matches only support one invite for now
		if (pDef->MapRules.bChallengeMatch && pInviteList && ea32Get(&pInviteList->piArray, 0) > 0)
		{
			ContainerID iInviteEntID = pInviteList->piArray[0];
			Entity* pInvitee = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iInviteEntID);
			QueueInviteInfo* pData = StructCreate(parse_QueueInviteInfo);
			pData->uEntID = entGetContainerID(pEnt);
			pData->iLevel = iLevel;
			pData->iRank = iRank;
			pData->uTeamID = iTeamID;
			pData->pchAffiliation = StructAllocString(pchAffiliation);
			
			// Check to see if the player can be invited. If the invite fails then don't create the game.
			if (pInvitee)
			{
				gslQueue_SendInvite(iInviteEntID, pData->uEntID, pEnt->pPlayer->accountID, pchQueueName, 0, pParams, pData);
			}
			else
			{
				RemoteCommand_gslQueue_SendInvite(GLOBALTYPE_ENTITYPLAYER, iInviteEntID, iInviteEntID, entGetContainerID(pEnt), pEnt->pPlayer->accountID, pchQueueName, 0, pParams, pData);
			}
			StructDestroy(parse_QueueInviteInfo, pData);
		}
		else
		{
			RemoteCommand_aslQueue_Create(GLOBALTYPE_QUEUESERVER, 0, entGetContainerID(pEnt), entGetAccountID(pEnt), iTeamID, iLevel, iRank, pchAffiliation, pchQueueName, pParams, NULL, pInviteList, pSettings);
		}
	}
}



AUTO_COMMAND ACMD_NAME(queue_create) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_PRIVATE;
void gslQueue_CreateCmd(Entity *pEnt, 
						const char* pchQueueName, 
						QueueInstanceParams* pParams,
						QueueIntArray* pInviteList,
						QueueGameSettings* pSettings)
{
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	if (pchQueueName && *pchQueueName && pParams)
	{
		gslQueue_Create(pEnt, pchQueueName, 0, pParams, pInviteList, pSettings);
	}
}

AUTO_COMMAND ACMD_NAME(queue_challenge) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueue_Challenge(Entity *pEnt, const char* pchQueueName, U32 uInviteEntID)
{
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	if (pchQueueName && pchQueueName[0] && uInviteEntID > 0)
	{
		QueueDef* pDef = queue_DefFromName(pchQueueName);
		if (pDef && pDef->MapRules.bChallengeMatch)
		{
			NOCONST(QueueInstanceParams) QParams = {0};
			QueueIntArray* pInviteList = StructCreate(parse_QueueIntArray);
			gslQueue_GetValidInstanceParams(pEnt, pDef, &QParams); 
			ea32Push(&pInviteList->piArray, uInviteEntID);
			gslQueue_Create(pEnt, pchQueueName, 0, (QueueInstanceParams*)&QParams, pInviteList, NULL);
			StructDeInitNoConst(parse_QueueInstanceParams, &QParams);
			StructDestroy(parse_QueueIntArray, pInviteList);
		}
	}
}

static void gslQueueChallengeName_CB(Entity *pEnt, U32 uiInviteeID, U32 uiAccountID, U32 uiLoginServerID, const char *pchQueueName)
{
	QueueDef* pDef = queue_DefFromName(pchQueueName);
	NOCONST(QueueInstanceParams) QParams = {0};
	QueueIntArray* pInviteList = StructCreate(parse_QueueIntArray);
	gslQueue_GetValidInstanceParams(pEnt, pDef, &QParams); 
	ea32Push(&pInviteList->piArray, uiInviteeID);
	gslQueue_Create(pEnt, pchQueueName, 0, (QueueInstanceParams*)&QParams, pInviteList, NULL);
	StructDeInitNoConst(parse_QueueInstanceParams, &QParams);
	StructDestroy(parse_QueueIntArray, pInviteList);
}

static void gslQueueChallengeNameFailure_CB(Entity *pEnt, U32 uiInviteeID, U32 uiAccountID, U32 uiLoginServerID, const char *pchQueueName)
{
	if (pEnt)
	{
		gslQueue_ResultMessage(pEnt->myContainerID, uiInviteeID, pchQueueName, QUEUE_PLAYER_NOT_FOUND_MESG);
	}
}

AUTO_COMMAND ACMD_NAME(queue_challenge_name) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueue_ChallengeName(Entity *pEnt, const char* pchQueueName, const char* pchChallengeeName)
{
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	if (pchQueueName && pchQueueName[0] && pchChallengeeName && pchChallengeeName[0])
	{
		QueueDef* pDef = queue_DefFromName(pchQueueName);
		if (pDef && pDef->MapRules.bChallengeMatch)
		{
			gslPlayerResolveHandle(pEnt, pchChallengeeName, gslQueueChallengeName_CB, gslQueueChallengeNameFailure_CB, (void *)pDef->pchName);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// Join a queue
///////////////////////////////////////////////////////////////////////////////////////////


	////////////////////////////////////////////////////////////////////////
	// Team Validation
	////////////////////////////////////////////////////////////////////////

		typedef struct QueueTeamValidationCBStruct
		{
			U32 uTeamID;
			U32 uEntityID;
			const char* pchQueueName; AST( POOL_STRING )
			U32 uLeaderEntityID;
			U32 uiInstanceID;
			S64 iMapKey;
			U32 bJoinNewMap;
			const char* pchAffiliation; AST( POOL_STRING )
			QueueInstanceParams* pParams;
		} QueueTeamValidationCBStruct;


		// Receive validation info from the individual team members. When we get all the info, report errors, or send off the join
		void gslQueue_ValidateForTeamJoin_CB (TransactionReturnVal *pReturn, QueueTeamValidationCBStruct *pData)
		{
			// We should be back on the original calling server. Get the caller entity
			Entity *pCallerEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pData->uLeaderEntityID);
			Team* pTeam = team_GetTeam(pCallerEnt); // This works with NULL Ent

			if (pCallerEnt!=NULL && pTeam!=NULL)
			{
				// Otherwise our team/leader went away somehow
				S32 i;
				bool bStillWaiting=false;
				bool bRequestingMismatch=false;
				bool bAllMembersGood=true;
					
				TeamMember *pTeamMember = team_FindMemberID(pTeam, pData->uEntityID);
	
				if (pTeamMember!=NULL)
				{
					// Otherwise the member left in the interim. Or something

					int iCannotUseReason;	
					enumTransactionOutcome eOutcome = RemoteCommandCheck_gslQueue_rcmd_ValidateForTeamJoin(pReturn, &iCannotUseReason);
					
					if (eOutcome == TRANSACTION_OUTCOME_SUCCESS)
					{
						if (iCannotUseReason!=QueueCannotUseReason_None)
						{
							// Send to the leader entity. Use the erroring entity as target (will name work?)
							gslQueue_ResultMessage(pData->uLeaderEntityID, pData->uEntityID, pData->pchQueueName, gslGetTeamCannotUseQueueReasonMsgKey(iCannotUseReason));
							pTeamMember->bQueueValidationWasOkay=0;
							// Don't return or anything here. We need to process that we received a respons and check if everything is done.
						}
						else
						{
							pTeamMember->bQueueValidationWasOkay=1;
						}
					}
					else
					{
						// We couldn't contact the member server. Tell the leader.
						gslQueue_ResultMessage(pData->uLeaderEntityID, 0, pData->pchQueueName, "QueueServer_TeamJoin_MemberError");
						pTeamMember->bQueueValidationWasOkay=0;
					}
					
					// Clean up regardless of status
					pTeamMember->bQueueValidationReceived=1;
				}
	
				// Check for waiting, etc.
				for(i = 0; i < eaSize(&pTeam->eaMembers); i++)
				{
					if (pTeam->eaMembers[i]->bQueueValidationRequested)
					{
						if (!pTeam->eaMembers[i]->bQueueValidationReceived)
						{
							bStillWaiting=true;
						}
	
						if (!pTeam->eaMembers[i]->bQueueValidationWasOkay)
						{
							bAllMembersGood=false;
						}
					}
					else
					{
						// We have a member who did not request validation. They must have arrived while validating. Don't join
						bRequestingMismatch=true;
					}
				}
					
				// If all received, do whatever we would have done
				if (!bStillWaiting)
				{
					// Make sure things didn't go astray
					if (bRequestingMismatch)
					{
						// Mismatch error. Someone on the team who wasn't there when we started validation
						gslQueue_ResultMessage(pData->uLeaderEntityID, 0, pData->pchQueueName, "QueueServer_TeamJoin_MemberError");
					}
					else if (!bAllMembersGood)
					{
						// Someone was not valid
						gslQueue_ResultMessage(pData->uLeaderEntityID, 0, pData->pchQueueName, "QueueServer_TeamJoin_MemberInvalid");
					}
					else
					{
						// GO!!!
						S32 j;
						QueueIntArray *pMemberList = StructCreate(parse_QueueIntArray);
						//Clone the team members
						for(j = 0; j < eaSize(&pTeam->eaMembers); j++)
						{
							eaiPush(&pMemberList->piArray, pTeam->eaMembers[j]->iEntID);
						}
						RemoteCommand_aslQueue_TeamJoin(GLOBALTYPE_QUEUESERVER, 0,
														pData->uLeaderEntityID,
														pData->uTeamID,
														pData->pchAffiliation,
														pMemberList,
														pData->pchQueueName,
														pData->uiInstanceID,
														pData->iMapKey,
														pData->bJoinNewMap,
														pData->pParams);
	
						StructDestroy(parse_QueueIntArray, pMemberList);
					}
				}
			}
			if (pData->pParams!=NULL)
			{
				StructDestroy(parse_QueueInstanceParams, pData->pParams);
				pData->pParams=NULL;
			}
			SAFE_FREE(pData);
		}

		AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
		int gslQueue_rcmd_ValidateForTeamJoin(U32 uTeamID, U32 iEntID, const char* pchQueueName, QueueInstanceParams* pParams, const char* pchLeaderAffiliation)
		{
			// Pass in the queue name instead of the def so we can get the one loaded on the server rather than the one constructed by the rcmd parser.
			//  The parsed one will not have any expressions generated.
			
			Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iEntID);
			QueueDef* pQueueDef = queue_DefFromName(pchQueueName);

			if (pEnt!=NULL && pQueueDef!=NULL)
			{
				Team* pTeam = team_GetTeam(pEnt);

				if (pTeam!=NULL && pTeam->iContainerID==uTeamID)
				{
					bool bNeedsMatchedAffiliation=false;
					int i;
					for (i = 0; i < eaSize(&pQueueDef->eaGroupDefs); i++)
					{
						if (pQueueDef->eaGroupDefs[i]->pchAffiliation!=NULL && pQueueDef->eaGroupDefs[i]->pchAffiliation[0]!=0)
						{
							bNeedsMatchedAffiliation=true;
						}
					}
					if (bNeedsMatchedAffiliation)
					{
						const char* pchAffiliation = queue_EntGetQueueAffiliation(pEnt);
						if (pchLeaderAffiliation!=NULL && pchLeaderAffiliation[0]!=0)
						{
							if (stricmp(pchAffiliation,pchLeaderAffiliation)!=0)
							{
								return(QueueCannotUseReason_MixedTeamAffiliation);
							}
						}
					}

					// Check for CannotUse
					{
						bool bPreValidated = false;
						bool bIgnoreLevelRestrictions = false;
						bool bCheckMissionReqs = true;
						bool bCheckCooldown = true;
						bool bInActiveQueue = true;
						QueueCannotUseReason eReason = gslEntCannotUseQueue(pEnt, pQueueDef, bPreValidated, bIgnoreLevelRestrictions, bInActiveQueue);
						if (eReason==QueueCannotUseReason_None)
						{
							eReason = gslEntCannotUseQueueInstance(pEnt, pParams, pQueueDef,
																				bPreValidated,bIgnoreLevelRestrictions,bCheckMissionReqs,bCheckCooldown);
						}

						return(eReason);
					}
				}
			}
			return(QueueCannotUseReason_Other);
		}

		// Set us up to validate the other members. Call onto those members' servers and validate. Wait 'til we get replies before doing anything.
		void gslTeam_ValidateTeamJoin(Team *pTeam, QueueDef* pQueueDef, U32 iLeadEntID, const char* pchAffiliation, U32 uiInstanceID, S64 iMapKey, U32 bJoinNewMap,
										QueueInstanceParams* pParams)
		{
			S32 i;

			// Check for disconnecteds
			if (eaSize(&pTeam->eaDisconnecteds) > 0)
			{
				gslQueue_ResultMessage(iLeadEntID, 0, pQueueDef->pchName, "QueueServer_TeamJoin_DisconnectedMembers");
				return;
			}
			
			for(i = 0; i < eaSize(&pTeam->eaMembers); i++)
			{
				QueueTeamValidationCBStruct* pData = NULL;
				TransactionReturnVal* pReturn;

				// Clear the validatation flag.
				pTeam->eaMembers[i]->bQueueValidationReceived=0;
				pTeam->eaMembers[i]->bQueueValidationRequested=1;
				
				pData = malloc(sizeof(QueueTeamValidationCBStruct));
				pData->uTeamID = pTeam->iContainerID;
				pData->uEntityID = pTeam->eaMembers[i]->iEntID;

				// We need a lot of this to pass in to the eventual join in the callback
				pData->pchQueueName = allocAddString(pQueueDef->pchName);
				pData->uLeaderEntityID = iLeadEntID;
				pData->uiInstanceID = uiInstanceID;
				pData->iMapKey = iMapKey;
				pData->bJoinNewMap = bJoinNewMap;
				pData->pchAffiliation = allocAddString(pchAffiliation);
				if (pParams!=NULL)
				{
					// We need to clone the params since the data will need to last until the Callback happens.
					//   Freed in gslQueue_ValidateForTeamJoin_CB.
					pData->pParams = StructClone(parse_QueueInstanceParams,pParams);
				}
				
				pReturn = objCreateManagedReturnVal(gslQueue_ValidateForTeamJoin_CB, pData);
				RemoteCommand_gslQueue_rcmd_ValidateForTeamJoin(pReturn, GLOBALTYPE_ENTITYPLAYER, pData->uEntityID,
																pData->uTeamID, pData->uEntityID, pData->pchQueueName, pParams, pData->pchAffiliation);
			}
		}


	////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////

static PlayerQueueInstance* gslQueue_EntFindValidInstance(Entity* pEnt, PlayerQueue* pQueue, QueueDef* pDef)
{
	S32 i;
	if (pEnt && pQueue && !pQueue->eCannotUseReason && pDef)
	{
		for (i = eaSize(&pQueue->eaInstances)-1; i >= 0; i--)
		{
			PlayerQueueInstance* pInstance = pQueue->eaInstances[i];
			if (pDef->Requirements.bRequireSameGuild && (!pInstance->pParams || !pInstance->pParams->uiGuildID))
			{
				continue;
			}
			if (!gslEntCannotUseQueueInstanceMsgKey(pEnt, pInstance->pParams, pDef, false, false))
			{
				return pInstance;
			}
		}
	}
	return NULL;
}

static bool gslQueue_PlayerMapJoinTimeLimitIsOkay(SA_PARAM_NN_VALID PlayerQueueMap* pMap, SA_PARAM_NN_VALID QueueDef* pDef)
{
	U32 uCurrentTime = timeSecondsSince2000();
	int iJoinTimeLimit = queue_GetJoinTimeLimit(pDef);
		
	if (iJoinTimeLimit<0 ||
		pMap->uMapLaunchTime == 0 ||
		uCurrentTime < pMap->uMapLaunchTime + iJoinTimeLimit)
	{
		return true;
	}
	return false;
}

void gslQueue_Join(Entity *pEnt,
				   const char* pchQueueName,
				   const char* pchPassword,
				   U32 uiTeamID,
				   U32 uiInstanceID,
				   S64 iMapKey,
				   U32 bJoinNewMap,
				   U32 bAutoAcceptOffer,
				   QueueMemberPrefs *pPrefs)
{
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	VerifyServerTypeExistsInShard(GLOBALTYPE_QUEUESERVER);

	if (pEnt->pPlayer)
	{
		NOCONST(QueueInstanceParams) DefaultParams = {0};
		const char *pchMesg;
		QueueDef* pDef = queue_DefFromName(pchQueueName);
		PlayerQueueInfo* pQueueInfo = pEnt->pPlayer->pPlayerQueueInfo;
		PlayerQueue* pQueue = pQueueInfo ? eaIndexedGetUsingString(&pQueueInfo->eaQueues, pchQueueName) : NULL;
		PlayerQueueInstance* pInstance = NULL;
		QueueInstanceParams* pParams;
		PlayerQueueMap* pMap = NULL;
		QueueJoinFlags eJoinFlags;

		// Member join criteria is stuff we need to determine groups/balancing, etc.
		QueueMemberJoinCriteria* pMemberJoinCriteria = StructCreate(parse_QueueMemberJoinCriteria);
		queue_EntFillJoinCriteria(pEnt,pMemberJoinCriteria);

		// Re-validate the queue usability. The PlayerQueue info may be stale. Espcially per events changing, levels changing, etc. etc.
		if (pQueue!=NULL)
		{
			pQueue->eCannotUseReason = gslEntCannotUseQueue(pEnt, pDef, false, false, false);
			// And set up other info based on this.
			pInstance = !pQueue->eCannotUseReason ? eaIndexedGetUsingInt(&pQueue->eaInstances, uiInstanceID) : NULL;
			pMap = pInstance && iMapKey ? eaIndexedGetUsingInt(&pInstance->eaPlayerQueueMaps, iMapKey) : NULL;
		}

		// Validation
		if (gslQueue_DisableJoinOrCreateOnCurrentMap(pEnt))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_INVALID_MAP_MESG);
			return;
		}
		if (gslQueue_IsLeaverPenaltyEnabled(entGetPartitionIdx(pEnt)))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_INVALID_MAP_MESG);
			return;
		}
		if(entity_IsUGCCharacter(pEnt))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_UGC_CHARACTER_NOT_ALLOWED);
			return;
		}

		if (pQueue && pQueue->eCannotUseReason != QueueCannotUseReason_None)
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, gslGetCannotUseQueueReasonMsgKey(pQueue->eCannotUseReason));
//			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_RESTRICTIONS_MESG);
			return;
		}
		if (pDef && !pQueue)
		{
			if (gslQueue_NotifyPlayerCannotUseReason(pEnt, pDef, QueueCannotUseReason_None))
			{
				return;
			}
		}
		if (!pDef || !pQueue || (uiInstanceID > 0 && !pInstance) || (iMapKey && !pMap))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_NOT_FOUND_MESG);
			return;
		}
		if(!gslQueue_IsRandomQueueActive(pDef))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_NOT_OFFERED_MESG);
			return;
		}
		if (pMap && !gslQueue_PlayerMapJoinTimeLimitIsOkay(pMap, pDef))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_BEYOND_MAX_JOIN_TIME_MESG);
			return;
		}
		if (pMap && queue_IsPlayerQueueMapFull(pMap, pDef, 1, pMemberJoinCriteria->pchAffiliation, pInstance->bOvertime))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_MAP_FULL_MESG);
			return;
		}
		if((g_QueueConfig.bEnableStrictTeamRules || pDef->bEnableStrictTeamRules) && team_IsMember(pEnt))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_IN_TEAM);
			return;
		}

		if (uiInstanceID == 0)
		{
			pInstance = gslQueue_EntFindValidInstance(pEnt, pQueue, pDef);
			if (pInstance)
			{
				uiInstanceID = pInstance->uiID;
			}
		}
		if (pInstance)
		{
			pParams = pInstance->pParams;
		}
		else
		{
			if (gslQueue_GetValidInstanceParams(pEnt, pDef, &DefaultParams))
			{
				pParams = (QueueInstanceParams*)&DefaultParams;
			}
			else
			{
				pParams = NULL;
			}
		}
		if(pchMesg = gslEntCannotUseQueueInstanceMsgKey(pEnt, pParams, pDef, false, false))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, pchMesg);
			return;
		}

		if(pchMesg = gslEntCannotUseQueuePrefs(pEnt, pPrefs, pDef))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, pchMesg);
			return;
		}

		// check private queue level
		if(pInstance && pInstance->uiOrigOwnerID != 0 && !queue_EntPrivateQueueLevelCheck(pEnt, pInstance))
		{
			char *estrBuffer = NULL;

			gslQueue_ResultMessage(pEnt->myContainerID, 0, SAFE_MEMBER(pQueue,pchQueueName), QUEUE_ENT_PRIVATE_LEVEL_OUT_OF_RANGE);
			entFormatGameMessageKey(pEnt, &estrBuffer, QUEUE_PRIVATE_LEVEL,
				STRFMT_INT("Level", g_QueueConfig.iPrivateQueueLevelLimit),
				STRFMT_END);
			notify_NotifySend(pEnt, kNotifyType_PvPGeneral, estrBuffer, NULL, NULL);

			estrDestroy(&estrBuffer);
			return;
		}


		gslQueueRefresh(pEnt);

		eJoinFlags = gslQueue_GetJoinFlags(pDef, bJoinNewMap, bAutoAcceptOffer, false);

		RemoteCommand_aslQueue_Join(GLOBALTYPE_QUEUESERVER, 0, 
									entGetContainerID(pEnt), 
									uiTeamID,
									pMemberJoinCriteria,
									pchQueueName, 
									pchPassword, 
									uiInstanceID, 
									iMapKey, 
									eJoinFlags,
									pParams,
									pPrefs);

		StructDestroy(parse_QueueMemberJoinCriteria, pMemberJoinCriteria);
	}
}

AUTO_COMMAND ACMD_NAME(Queue_JoinWithPrefs) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_SERVERCMD ACMD_CATEGORY(Queue);
void gslQueue_JoinWithPrefsCmd(Entity *pEnt, const char *pchQueueName, QueueMemberPrefs *pMemberPrefs)
{
	if(!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	if(pchQueueName && *pchQueueName)
	{
		gslQueue_Join(pEnt, pchQueueName, NULL, 0, 0, 0, false, false, pMemberPrefs);
	}
}

AUTO_COMMAND ACMD_NAME(Queue_Join) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueue_JoinCmd(Entity *pEnt, const char* pchQueueName)
{
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}
		
	if (pchQueueName && *pchQueueName)
	{
		gslQueue_Join(pEnt, pchQueueName, NULL, 0, 0, 0, false, false, NULL);
	}
}

AUTO_COMMAND ACMD_NAME(Queue_JoinInstance) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueue_JoinInstanceCmd(Entity *pEnt, const char* pchQueueName, U32 uiInstanceID, S64 iMapKey)
{
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	if (pchQueueName && *pchQueueName)
	{
		gslQueue_Join(pEnt, pchQueueName, NULL, 0, uiInstanceID, iMapKey, false, false, NULL);
	}
}

AUTO_COMMAND ACMD_NAME(Queue_JoinNextInstance) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueue_JoinNextInstanceCmd(Entity *pEnt, const char* pchQueueName, U32 uiInstanceID, S64 iMapKey)
{
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	if (pchQueueName && *pchQueueName)
	{
		gslQueue_Join(pEnt, pchQueueName, NULL, 0, uiInstanceID, iMapKey, true, false, NULL);
	}
}

AUTO_COMMAND ACMD_NAME(Queue_JoinWithPassword) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueue_JoinWithPassword(Entity *pEnt, 
							   const char* pchQueueName, 
							   const char* pchPassword,
							   U32 uiInstanceID)
{
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	if (pchQueueName && *pchQueueName)
	{
		gslQueue_Join(pEnt, pchQueueName, pchPassword, 0, uiInstanceID, 0, false, false, NULL);
	}
}

// Joins a map with a guildmate, teammate, or friend - if possible
AUTO_COMMAND ACMD_NAME(Queue_JoinBestMap) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueue_JoinBestMap(Entity *pEnt, const char* pchQueueName, U32 uiInstanceID)
{
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	if (pchQueueName && *pchQueueName)
	{
		S64 iMapKey = gslQueue_FindBestMapToJoin(pEnt, pchQueueName, &uiInstanceID);
		gslQueue_Join(pEnt, pchQueueName, NULL, 0, uiInstanceID, iMapKey, false, false, NULL);
	}
}

static bool gslQueue_JoinParams(Entity *pEnt, const char* pchQueueName, S32 iTeamID, 
								U32 uiInstanceID, S64 iMapKey, 
								U32 bJoinNewMap, U32 bAutoAcceptOffer, U32 bTeamJoin,
								QueueInstanceParams* pParams)
{
	QueueDef* pDef = queue_DefFromName(pchQueueName);
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return false;
	}

	if (pEnt->pPlayer && pDef)
	{
		QueueJoinFlags eJoinFlags = gslQueue_GetJoinFlags(pDef, bJoinNewMap, bAutoAcceptOffer, bTeamJoin);
		const char *pchMesg;

		// Member join criteria is stuff we need to determine groups/balancing, etc.
		QueueMemberJoinCriteria* pMemberJoinCriteria = StructCreate(parse_QueueMemberJoinCriteria);
		queue_EntFillJoinCriteria(pEnt,pMemberJoinCriteria);

		if (gslQueue_DisableJoinOrCreateOnCurrentMap(pEnt))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_INVALID_MAP_MESG);
			return false;
		}
		if (gslQueue_IsLeaverPenaltyEnabled(entGetPartitionIdx(pEnt)))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_INVALID_MAP_MESG);
			return false;
		}
		if(entity_IsUGCCharacter(pEnt))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_UGC_CHARACTER_NOT_ALLOWED);
			return false;
		}
		if(pchMesg = gslEntCannotUseQueueInstanceMsgKey(pEnt, pParams, pDef, true, bTeamJoin))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, pchMesg);
			return false;
		}


		gslQueueRefresh(pEnt);

		RemoteCommand_aslQueue_Join(GLOBALTYPE_QUEUESERVER, 0, 
								    entGetContainerID(pEnt), 
									iTeamID,
									pMemberJoinCriteria,
									pchQueueName,
									NULL, 
									uiInstanceID, 
									iMapKey, 
									eJoinFlags,
									pParams,
									NULL);
		
		StructDestroy(parse_QueueMemberJoinCriteria, pMemberJoinCriteria);
		
		return true;
	}
	return false;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER) ACMD_IFDEF(GAMESERVER);
void gslQueue_rcmd_JoinParams(ContainerID iEntID, const char* pchQueueName, 
							  U32 uiInstanceID, S64 iMapKey, U32 bJoinNewMap, U32 bAutoAcceptOffer, U32 bTeamJoin,
							  QueueInstanceParams* pParams)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iEntID);
	U32 uTeamID;

	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	uTeamID = team_GetTeamID(pEnt);
	gslQueue_JoinParams(pEnt, 
						pchQueueName, 
						uTeamID, 
						uiInstanceID, 
						iMapKey, 
						bJoinNewMap, 
						bAutoAcceptOffer, 
						bTeamJoin, 
						pParams);
}

void gslQueue_TeamJoin(Entity *pEnt, const char* pchQueueName, U32 uiInstanceID, S64 iMapKey, U32 bJoinNewMap)
{
	Team *pTeam;
	VerifyServerTypeExistsInShard(GLOBALTYPE_QUEUESERVER);
	
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	if(entity_IsUGCCharacter(pEnt))
	{
		gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_UGC_CHARACTER_NOT_ALLOWED);
		return;
	}

	pTeam = team_GetTeam(pEnt);
	if (pTeam && pchQueueName && *pchQueueName)
	{
		NOCONST(QueueInstanceParams) DefaultParams = {0};
		const char *pchMesg;
		QueueDef* pDef = queue_DefFromName(pchQueueName);
		PlayerQueueInfo* pQueueInfo = pEnt->pPlayer->pPlayerQueueInfo;
		PlayerQueue* pQueue = pQueueInfo ? eaIndexedGetUsingString(&pQueueInfo->eaQueues, pchQueueName) : NULL;
		PlayerQueueInstance* pInstance = NULL;
		QueueInstanceParams* pParams;
		PlayerQueueMap* pMap = NULL;
		const char* pchAffiliation = queue_EntGetQueueAffiliation(pEnt);

		// Re-validate the queue usability. The PlayerQueue info may be stale. Espcially per events changing, levels changing, etc. etc.
		if (pQueue!=NULL)
		{
			pQueue->eCannotUseReason = gslEntCannotUseQueue(pEnt, pDef, false, false, false);
			// And set up other info based on this.
			pInstance = !pQueue->eCannotUseReason ? eaIndexedGetUsingInt(&pQueue->eaInstances, uiInstanceID) : NULL;
			pMap = pInstance && iMapKey ? eaIndexedGetUsingInt(&pInstance->eaPlayerQueueMaps, iMapKey) : NULL;
		}
		
		// Validation
		if (gslQueue_DisableJoinOrCreateOnCurrentMap(pEnt))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_INVALID_MAP_MESG);
			return;
		}
		// This just means we cannot queue for something if we are on a map that has a leaver penalty. 
		if (gslQueue_IsLeaverPenaltyEnabled(entGetPartitionIdx(pEnt)))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_INVALID_MAP_MESG);
			return;
		}
		if (pQueue && pQueue->eCannotUseReason != QueueCannotUseReason_None)
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, gslGetCannotUseQueueReasonMsgKey(pQueue->eCannotUseReason));
//			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_RESTRICTIONS_MESG);
			return;
		}
		if (pDef && !pQueue)
		{
			if (gslQueue_NotifyPlayerCannotUseReason(pEnt, pDef, QueueCannotUseReason_None))
			{
				return;
			}
		}
		if (!pDef || (uiInstanceID > 0 && !pInstance) || (iMapKey && !pMap))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_NOT_FOUND_MESG);
			return;
		}
		if (pMap && !gslQueue_PlayerMapJoinTimeLimitIsOkay(pMap, pDef))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_BEYOND_MAX_JOIN_TIME_MESG);
			return;
		}
		if (pMap && queue_IsPlayerQueueMapFull(pMap, pDef, team_NumTotalMembers(pTeam), pchAffiliation, pInstance->bOvertime))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_MAP_FULL_MESG);
			return;
		}
		if (!team_IsMember(pEnt))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_NOT_TEAMMEMBER_MESG);
			return;
		}
		if ((g_QueueConfig.bEnableStrictTeamRules || pDef->bEnableStrictTeamRules) && !team_IsTeamLeader(pEnt))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_NOT_TEAMLEADER_MESG);
			return;
		}
		
		if (uiInstanceID == 0)
		{
			pInstance = gslQueue_EntFindValidInstance(pEnt, pQueue, pDef);
			if (pInstance)
			{
				uiInstanceID = pInstance->uiID;
			}
		}
		if (pInstance)
		{
			pParams = pInstance->pParams;
		}
		else
		{
			if (gslQueue_GetValidInstanceParams(pEnt, pDef, &DefaultParams))
			{
				pParams = (QueueInstanceParams*)&DefaultParams;
			}
			else
			{
				pParams = NULL;
			}
		}
		if(pchMesg = gslEntCannotUseQueueInstanceMsgKey(pEnt, pParams, pDef, false, false))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, pchMesg);
			return;
		}

		gslTeam_ValidateTeamJoin(pTeam, pDef, entGetContainerID(pEnt), pchAffiliation, uiInstanceID, iMapKey, bJoinNewMap, pParams);
	}
}

// Entire team join a queue
AUTO_COMMAND ACMD_NAME(Queue_TeamJoin) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueueTeamJoin(Entity *pEnt, const char* pchQueueName)
{
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	gslQueue_TeamJoin(pEnt, pchQueueName, 0, 0, false);
}

AUTO_COMMAND ACMD_NAME(Queue_TeamJoinInstance) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueueTeamJoinInstance(Entity *pEnt, const char* pchQueueName, U32 uiInstanceID, S64 iMapKey)
{
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	gslQueue_TeamJoin(pEnt, pchQueueName, uiInstanceID, iMapKey, false);
}

AUTO_COMMAND ACMD_NAME(Queue_TeamJoinNextInstance) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueueTeamJoinNextInstance(Entity *pEnt, const char* pchQueueName, U32 uiInstanceID, S64 iMapKey)
{
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	gslQueue_TeamJoin(pEnt, pchQueueName, uiInstanceID, iMapKey, true);
}

// Attempts to put you and your team on a map with another guildmate, teammate, or friend
AUTO_COMMAND ACMD_NAME(Queue_TeamJoinBestMap) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueue_TeamJoinBestMap(Entity *pEnt, const char* pchQueueName, U32 uiInstanceID)
{
	S64 iMapKey;
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	iMapKey = gslQueue_FindBestMapToJoin(pEnt, pchQueueName, &uiInstanceID);
	gslQueue_TeamJoin(pEnt, pchQueueName, uiInstanceID, iMapKey, false);
}


///////////////////////////////////////////////////////////////////////////////////////////
// Resolving player handles
///////////////////////////////////////////////////////////////////////////////////////////

static void gslQueue_ResolveHandleFailure_CB(Entity *pEnt, ContainerID uiPlayerID, U32 uiAccountID, U32 uiLoginServerID, GSLQueueCBData *pData)
{
	if (pEnt)
	{
		gslQueue_ResultMessage(pEnt->myContainerID, uiPlayerID, pData->pchQueueName, QUEUE_PLAYER_NOT_FOUND_MESG);
	}
	if (pData->pUserData)
	{
		StructDestroy(parse_QueueInstanceParams, pData->pUserData);
	}
	SAFE_FREE(pData);
}

static void gslQueueInvite_Internal(Entity* pEnt, const char* pchQueueName, U32 uiInstanceID, U32 uiInviteeID)
{
	Entity* pInvitee = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, uiInviteeID);
	QueueDef* pDef = queue_DefFromName(pchQueueName);
	PlayerQueueInfo* pQueueInfo = pDef && pEnt && pEnt->pPlayer ? pEnt->pPlayer->pPlayerQueueInfo : NULL;
	PlayerQueueInstance* pInstance = queue_FindPlayerQueueInstanceByID(pQueueInfo, pchQueueName, uiInstanceID, true);

	if (!pEnt || !pEnt->pPlayer)
		return;

	if (mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	// Validation
	if (!pDef || !pInstance || !pInstance->pParams || pInstance->pParams->uiOwnerID == 0)
	{
		gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_NOT_FOUND_MESG);
		return;
	}

	if(entity_IsUGCCharacter(pEnt))
	{
		gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_UGC_CHARACTER_NOT_ALLOWED);
		return;
	}
	
	if (pInvitee)
	{
		gslQueue_SendInvite(entGetContainerID(pInvitee), entGetContainerID(pEnt), pEnt->pPlayer->accountID, pchQueueName, pInstance->uiID, pInstance->pParams, NULL);
	}
	else
	{
		RemoteCommand_gslQueue_SendInvite(GLOBALTYPE_ENTITYPLAYER, uiInviteeID, uiInviteeID, entGetContainerID(pEnt), pEnt->pPlayer->accountID, pchQueueName, pInstance->uiID, pInstance->pParams, NULL);
	}
}

static void gslQueueInvite_CB(Entity* pEnt, U32 uiInviteeID, U32 uiAccountID, U32 uiLoginServerID, GSLQueueCBData* pData)
{
	const char* pchQueueName = pData->pchQueueName;
	U32 uiInstanceID = pData->uiInstanceID;
	if (uiInviteeID)
	{
		gslQueueInvite_Internal(pEnt, pchQueueName, uiInstanceID, uiInviteeID);
	}
	SAFE_FREE(pData);
}

AUTO_COMMAND ACMD_NAME(queue_inviteinstance) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueueInviteToInstance(Entity *pEnt, const char* pchQueueName, U32 uiInstanceID, const char* pchInviteeName)
{
	GSLQueueCBData *pData = gslQueue_MakeCBData(pchQueueName, uiInstanceID, NULL, NULL);
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	gslPlayerResolveHandle(pEnt, pchInviteeName, gslQueueInvite_CB, gslQueue_ResolveHandleFailure_CB, pData);
}

AUTO_COMMAND ACMD_NAME(queue_invite) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueueInvite(Entity *pEnt, const char* pchInviteeName)
{
	PlayerQueue* pQueue = NULL;
	PlayerQueueInstance* pInstance = gslQueue_FindPrivateInstance(pEnt, &pQueue);
	
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	if (pInstance)
	{
		gslQueueInviteToInstance(pEnt, pQueue->pchQueueName, pInstance->uiID, pchInviteeName);
	}
}

static void gslQueueInviteTeam_Internal(SA_PARAM_NN_VALID Entity* pInvitee,
										U32 uiInviterID,
										const char* pchQueueName, 
										U32 uiInstanceID,
										QueueInstanceParams* pParams,
										U32 bInviteSelf)
{
	U32 uiInviteeID = entGetContainerID(pInvitee);
	Team* pTeam = team_GetTeam(pInvitee);
	if (pTeam)
	{
		S32 i;
		for (i = eaSize(&pTeam->eaMembers)-1; i >= 0; i--)
		{
			TeamMember* pMember = pTeam->eaMembers[i];
			if (!bInviteSelf && pMember->iEntID == uiInviteeID)
			{
				continue;
			}
			RemoteCommand_gslQueue_SendInvite(GLOBALTYPE_ENTITYPLAYER, pMember->iEntID, 
				pMember->iEntID, uiInviterID, 0, pchQueueName, uiInstanceID, pParams, NULL);
		}
	}
	else if (bInviteSelf)
	{
		gslQueue_SendInvite(uiInviteeID, uiInviterID, 0, pchQueueName, uiInstanceID, pParams, NULL);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void gslQueue_SendInviteTeam(U32 uiInviteeID, 
							 U32 uiInviterID, 
							 const char* pchQueueName, 
							 U32 uiInstanceID,
							 QueueInstanceParams* pParams,
							 U32 bInviteSelf)
{
	Entity* pInvitee = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, uiInviteeID);
	if (pInvitee)
	{
		gslQueueInviteTeam_Internal(pInvitee, uiInviterID, pchQueueName, uiInstanceID, pParams, bInviteSelf);
	}
}

static void gslQueueInviteTeamToInstance(Entity *pEnt, 
										 const char* pchQueueName, 
										 U32 uiInstanceID, 
										 U32 uiInviteeID, 
										 bool bInviteSelf)
{
	QueueDef* pDef = queue_DefFromName(pchQueueName);
	PlayerQueueInfo* pQueueInfo = pDef && pEnt && pEnt->pPlayer ? pEnt->pPlayer->pPlayerQueueInfo : NULL;
	PlayerQueueInstance* pInstance = queue_FindPlayerQueueInstanceByID(pQueueInfo, pchQueueName, uiInstanceID, true);

	if (pInstance && pInstance->pParams && pInstance->pParams->uiOwnerID > 0)
	{
		Entity* pInvitee = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, uiInviteeID);
		if (pInvitee)
		{
			gslQueueInviteTeam_Internal(pInvitee, entGetContainerID(pEnt), pchQueueName, pInstance->uiID, pInstance->pParams, bInviteSelf);
		}
		else
		{
			RemoteCommand_gslQueue_SendInviteTeam(GLOBALTYPE_ENTITYPLAYER, uiInviteeID, 
				uiInviteeID, entGetContainerID(pEnt), pchQueueName, pInstance->uiID, pInstance->pParams, bInviteSelf);
		}
	}
	else if (pEnt)
	{
		gslQueue_ResultMessage(entGetContainerID(pEnt), 0, pchQueueName, QUEUE_NOT_FOUND_MESG);
	}
}

static void gslQueueInviteTeam_CB(Entity* pEnt, U32 uiInviteeID, U32 uiAccountID, U32 uiLoginServerID, GSLQueueCBData* pData)
{
	const char* pchQueueName = pData->pchQueueName;
	U32 uiInstanceID = pData->uiInstanceID;
	bool bInviteSelf = (entGetContainerID(pEnt) != uiInviteeID);
	
	gslQueueInviteTeamToInstance(pEnt, pchQueueName, uiInstanceID, uiInviteeID, bInviteSelf);
	SAFE_FREE(pData);
}

AUTO_COMMAND ACMD_NAME(queue_inviteteam) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueueInviteTeam(Entity *pEnt, const char* pchInviteeName)
{
	GSLQueueCBData* pData;
	PlayerQueue* pQueue = NULL;
	PlayerQueueInstance* pInstance;

	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}
	if (!(pInstance = gslQueue_FindPrivateInstance(pEnt, &pQueue)))
	{
		return;
	}
	pData = gslQueue_MakeCBData(pQueue->pchQueueName, pInstance->uiID, NULL, NULL);
	gslPlayerResolveHandle(pEnt, pchInviteeName, gslQueueInviteTeam_CB, gslQueue_ResolveHandleFailure_CB, pData);
}


AUTO_COMMAND ACMD_NAME(queue_invitelist) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_PRIVATE;
void gslQueueInviteList(Entity *pEnt, QueueIntArray* pList, U32 bInviteTeam)
{
	PlayerQueue* pQueue = NULL;
	PlayerQueueInstance* pInstance = gslQueue_FindPrivateInstance(pEnt, &pQueue);
	
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}
		
	if (pList && pInstance)
	{
		S32 i;
		for (i = ea32Size(&pList->piArray)-1; i >= 0; i--)
		{
			U32 uiInviteeID = pList->piArray[i];

			if (!uiInviteeID)
			{
				continue;
			}
			if (bInviteTeam)
			{
				bool bInviteSelf = (entGetContainerID(pEnt) != uiInviteeID);
				gslQueueInviteTeamToInstance(pEnt, pQueue->pchQueueName, pInstance->uiID, uiInviteeID, bInviteSelf);
			}
			else
			{
				gslQueueInvite_Internal(pEnt, pQueue->pchQueueName, pInstance->uiID, uiInviteeID);
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// Kick from a private queue
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_NAME(queue_kickinstance) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueueKickFromInstance(Entity *pEnt, const char* pchQueueName, U32 uiInstanceID, U32 uiMemberID)
{
	QueueDef* pDef = queue_DefFromName(pchQueueName);
	PlayerQueueInfo* pQueueInfo = pDef && pEnt && pEnt->pPlayer ? pEnt->pPlayer->pPlayerQueueInfo : NULL;
	PlayerQueueInstance* pInstance = queue_FindPlayerQueueInstanceByID(pQueueInfo, pchQueueName, uiInstanceID, true);

	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	// Validation
	if (!pDef || !pInstance || !pInstance->pParams || pInstance->pParams->uiOwnerID != entGetContainerID(pEnt))
	{
		gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_NOT_FOUND_MESG);
		return;
	}

	VerifyServerTypeExistsInShard(GLOBALTYPE_QUEUESERVER);
	RemoteCommand_aslQueue_Kick(GLOBALTYPE_QUEUESERVER, 0, entGetContainerID(pEnt), pchQueueName, uiInstanceID, 0, uiMemberID);
}

static void gslQueueKick_CB(Entity* pEnt, U32 uiMemberID, U32 uiAccountID, U32 uiLoginServerID, GSLQueueCBData* pData)
{
	const char* pchQueueName = pData->pchQueueName;
	U32 uiInstanceID = pData->uiInstanceID;
	gslQueueKickFromInstance(pEnt, pchQueueName, uiInstanceID, uiMemberID);
	SAFE_FREE(pData);
}

AUTO_COMMAND ACMD_NAME(queue_kick) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueueKick(Entity *pEnt, const char* pchMemberName)
{
	PlayerQueue* pQueue = NULL;
	PlayerQueueInstance* pInstance = gslQueue_FindPrivateInstance(pEnt, &pQueue);

	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	if (pInstance)
	{
		GSLQueueCBData *pData = gslQueue_MakeCBData(pQueue->pchQueueName, pInstance->uiID, NULL, NULL);
		gslPlayerResolveHandle(pEnt, pchMemberName, gslQueueKick_CB, gslQueue_ResolveHandleFailure_CB, pData);
	}
}

AUTO_COMMAND ACMD_NAME(queue_kickid) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueueKickID(Entity *pEnt, U32 uiMemberID)
{
	PlayerQueue* pQueue = NULL;
	PlayerQueueInstance* pInstance = gslQueue_FindPrivateInstance(pEnt, &pQueue);

	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	if (pInstance)
	{
		gslQueueKickFromInstance(pEnt, pQueue->pchQueueName, pInstance->uiID, uiMemberID);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// Change groups in a private queue
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_NAME(queue_changegroup) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueueChangeGroup(Entity *pEnt, U32 uiSubjectID, S32 iGroupID)
{
	PlayerQueue* pQueue = NULL;
	PlayerQueueInstance* pInstance = gslQueue_FindPrivateInstance(pEnt, &pQueue);
	QueueDef* pDef = pQueue ? GET_REF(pQueue->hDef) : NULL;

	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	// Validation
	if (!pDef || !pInstance || !pInstance->pParams || pInstance->pParams->uiOwnerID == 0)
	{
		gslQueue_ResultMessage(pEnt->myContainerID, 0, SAFE_MEMBER(pQueue, pchQueueName), QUEUE_NOT_FOUND_MESG);
		return;
	}
	if (iGroupID < 0 || iGroupID >= eaSize(&pDef->eaGroupDefs))
	{
		gslQueue_ResultMessage(pEnt->myContainerID, 0, SAFE_MEMBER(pQueue, pchQueueName), QUEUE_INVALID_GROUP_MESG);
		return;
	}

	VerifyServerTypeExistsInShard(GLOBALTYPE_QUEUESERVER);
	RemoteCommand_aslQueue_ChangeGroup(GLOBALTYPE_QUEUESERVER, 0, entGetContainerID(pEnt), uiSubjectID, pQueue->pchQueueName, pInstance->uiID, iGroupID);
}

AUTO_COMMAND ACMD_NAME(queue_swapgroups) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueueSwapGroups(Entity *pEnt, U32 uiMemberID1, U32 uiMemberID2)
{
	PlayerQueue* pQueue = NULL;
	PlayerQueueInstance* pInstance = gslQueue_FindPrivateInstance(pEnt, &pQueue);
	QueueDef* pDef = pQueue ? GET_REF(pQueue->hDef) : NULL;

	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	// Validation
	if (!pDef || !pInstance || !pInstance->pParams || pInstance->pParams->uiOwnerID != entGetContainerID(pEnt))
	{
		gslQueue_ResultMessage(pEnt->myContainerID, 0, SAFE_MEMBER(pQueue, pchQueueName), QUEUE_NOT_FOUND_MESG);
		return;
	}

	VerifyServerTypeExistsInShard(GLOBALTYPE_QUEUESERVER);
	RemoteCommand_aslQueue_SwapGroups(GLOBALTYPE_QUEUESERVER, 0, entGetContainerID(pEnt), pQueue->pchQueueName, pInstance->uiID, uiMemberID1, uiMemberID2);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Change private game settings
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_NAME(queue_changemap) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueue_ChangeMap(Entity *pEnt, const char* pchMapName)
{
	PlayerQueue* pQueue = NULL;
	PlayerQueueInstance* pInstance = gslQueue_FindPrivateInstance(pEnt, &pQueue);	
	QueueDef* pDef = pQueue ? GET_REF(pQueue->hDef) : NULL;

	if (!pEnt)
	{
		return;
	}
	if (!pDef || !pInstance || SAFE_MEMBER2(pInstance, pParams, uiOwnerID) != entGetContainerID(pEnt))
	{
		gslQueue_ResultMessage(pEnt->myContainerID, 0, SAFE_MEMBER(pQueue,pchQueueName), QUEUE_MAP_NOT_FOUND_MESG);
		return;
	}
	if (queue_GetMapIndexByName(pDef, pchMapName) < 0)
	{
		gslQueue_ResultMessage(pEnt->myContainerID, 0, SAFE_MEMBER(pQueue, pchQueueName), QUEUE_MAP_NOT_FOUND_MESG);
		return;
	}

	VerifyServerTypeExistsInShard(GLOBALTYPE_QUEUESERVER);
	RemoteCommand_aslQueue_ChangeMap(GLOBALTYPE_QUEUESERVER, 0, entGetContainerID(pEnt), pQueue->pchQueueName, pInstance->uiID, pchMapName);
}

AUTO_COMMAND ACMD_NAME(queue_ChangeSetting) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueue_ChangeSetting(Entity *pEnt, const char* pchQueueVarName, S32 iValue)
{
	PlayerQueue* pQueue = NULL;
	PlayerQueueInstance* pInstance = gslQueue_FindPrivateInstance(pEnt, &pQueue);	
	QueueDef* pDef = pQueue ? GET_REF(pQueue->hDef) : NULL;
	S32 iLevelBandIndex = SAFE_MEMBER2(pInstance, pParams, iLevelBandIndex);

	if (!pEnt)
	{
		return;
	}
	if (!pDef || !pInstance || SAFE_MEMBER2(pInstance, pParams, uiOwnerID) != entGetContainerID(pEnt))
	{
		gslQueue_ResultMessage(pEnt->myContainerID, 0, SAFE_MEMBER(pQueue,pchQueueName), QUEUE_MAP_NOT_FOUND_MESG);
		return;
	}

	if (!gslQueue_ValidateSetting(pDef, iLevelBandIndex, pchQueueVarName, iValue))
	{
		gslQueue_ResultMessage(pEnt->myContainerID, 0, SAFE_MEMBER(pQueue,pchQueueName), QUEUE_MAP_SETTING_INVALID_MESG);
		return;
	}

	VerifyServerTypeExistsInShard(GLOBALTYPE_QUEUESERVER);
	RemoteCommand_aslQueue_ChangeSetting(GLOBALTYPE_QUEUESERVER, 0, entGetContainerID(pEnt), pQueue->pchQueueName, pInstance->uiID, pchQueueVarName, iValue);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Change private game password
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_NAME(queue_ChangePassword) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueue_ChangePassword(Entity *pEnt, const char* pchPassword)
{
	PlayerQueue* pQueue = NULL;
	PlayerQueueInstance* pInstance = gslQueue_FindPrivateInstance(pEnt, &pQueue);	
	QueueDef* pDef = pQueue ? GET_REF(pQueue->hDef) : NULL;

	if (!pDef || !pInstance || SAFE_MEMBER2(pInstance, pParams, uiOwnerID) != entGetContainerID(pEnt))
	{
		gslQueue_ResultMessage(pEnt->myContainerID, 0, SAFE_MEMBER(pQueue,pchQueueName), QUEUE_MAP_NOT_FOUND_MESG);
		return;
	}

	VerifyServerTypeExistsInShard(GLOBALTYPE_QUEUESERVER);
	RemoteCommand_aslQueue_ChangePassword(GLOBALTYPE_QUEUESERVER, 0, entGetContainerID(pEnt), pQueue->pchQueueName, pInstance->uiID, pchPassword);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Start a private game 
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_NAME(queue_startgame) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueue_StartGame(Entity *pEnt)
{
	PlayerQueue* pQueue = NULL;
	PlayerQueueInstance* pInstance = gslQueue_FindPrivateInstance(pEnt, &pQueue);
	QueueDef* pDef = pQueue ? GET_REF(pQueue->hDef) : NULL;
	
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	if (gslQueue_DisableJoinOrCreateOnCurrentMap(pEnt))
	{
		gslQueue_ResultMessage(pEnt->myContainerID, 0, SAFE_MEMBER(pQueue, pchQueueName), QUEUE_INVALID_MAP_MESG);
		return;
	}
	if (gslQueue_IsLeaverPenaltyEnabled(entGetPartitionIdx(pEnt)))
	{
		gslQueue_ResultMessage(pEnt->myContainerID, 0, SAFE_MEMBER(pQueue, pchQueueName), QUEUE_INVALID_MAP_MESG);
		return;
	}
	if (!pDef || !pInstance || !pInstance->pParams || pInstance->pParams->uiOwnerID != entGetContainerID(pEnt))
	{
		gslQueue_ResultMessage(pEnt->myContainerID, 0, SAFE_MEMBER(pQueue,pchQueueName), QUEUE_NOT_FOUND_MESG);
		return;
	}
	if (!queue_PlayerInstance_AllMembersInState(pInstance, PlayerQueueState_InQueue))
	{
		gslQueue_ResultMessage(pEnt->myContainerID, 0, SAFE_MEMBER(pQueue,pchQueueName), QUEUE_PLAYERS_NOT_READY_MESG);
		return;
	}

	// check private queue level
	if(!queue_PrivateQueueLevelCheck(pEnt, pInstance))
	{
		char *estrBuffer = NULL;

		gslQueue_ResultMessage(pEnt->myContainerID, 0, SAFE_MEMBER(pQueue,pchQueueName), QUEUE_PRIVATE_LEVEL_OUT_OF_RANGE);
		entFormatGameMessageKey(pEnt, &estrBuffer, QUEUE_PRIVATE_LEVEL,
			STRFMT_INT("Level", g_QueueConfig.iPrivateQueueLevelLimit),
			STRFMT_END);
		notify_NotifySend(pEnt, kNotifyType_PvPGeneral, estrBuffer, NULL, NULL);

		estrDestroy(&estrBuffer);
		return;
	}


	VerifyServerTypeExistsInShard(GLOBALTYPE_QUEUESERVER);
	RemoteCommand_aslQueue_StartGame(GLOBALTYPE_QUEUESERVER, 0, entGetContainerID(pEnt), pQueue->pchQueueName, pInstance->uiID);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Leave a queue
///////////////////////////////////////////////////////////////////////////////////////////


// The ent is NULL, being destroyed or queues are disabled
static bool queue_EntDestroyedOrQueueDisabled(Entity *pEnt)
{
	int iPartitionIdx;

	if(!pEnt)
	{
		return true;
	}

	iPartitionIdx = entGetPartitionIdx(pEnt);

	if(iPartitionIdx == PARTITION_ENT_BEING_DESTROYED || mapState_ArePVPQueuesDisabled(mapState_FromPartitionIdx(iPartitionIdx)))
	{
		return true;
	}

	return false;
}

void queue_LeaveAllQueues(Entity* pEnt)
{
	// changed to more robust call as a map transfer fail can get this called as the entity logs out
	if (queue_EntDestroyedOrQueueDisabled(pEnt))
	{
		return;
	}

	if (pEnt->pPlayer)
	{
		RemoteCommand_aslQueue_LeaveAll(GLOBALTYPE_QUEUESERVER, 0, entGetContainerID(pEnt), false);
	}
}

static void gslQueueLeave_Internal(Entity *pEnt, const char* pchQueueName, U32 uiInstanceID)
{
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	if (pEnt->pPlayer)
	{
		QueueDef* pDef = queue_DefFromName(pchQueueName);
		if (pDef)
		{
			RemoteCommand_aslQueue_Leave(GLOBALTYPE_QUEUESERVER, 0, entGetContainerID(pEnt), pchQueueName, uiInstanceID);
		}
		else
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_NOT_FOUND_MESG);
		}
	}
}

AUTO_COMMAND ACMD_NAME(queue_leaveall) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueueLeaveAll(Entity *pEnt)
{
	queue_LeaveAllQueues(pEnt);
}

// Leave all instances of a queue
AUTO_COMMAND ACMD_NAME(queue_leave) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueueLeave(Entity *pEnt, const char* pchQueueName)
{
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	gslQueueLeave_Internal(pEnt, pchQueueName, 0);
}

AUTO_COMMAND ACMD_NAME(queue_teamleave) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueueTeamLeave(Entity *pEnt, const char *pchQueueName)
{
	QueueDef *pDef = queue_DefFromName(pchQueueName);
	if(!pDef || !pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return ;
	}

	if(team_IsMember(pEnt))
	{
		if(!g_QueueConfig.bEnableStrictTeamRules || !pDef->bEnableStrictTeamRules || team_IsTeamLeader(pEnt))
		{
			RemoteCommand_aslQueue_TeamLeave(GLOBALTYPE_QUEUESERVER,0,entGetContainerID(pEnt),pchQueueName,0);
		} else {
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_NOT_TEAMLEADER_MESG);
		}
	} else if (pEnt) {
		gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_NOT_TEAMMEMBER_MESG);
	}
}

// Leave a particular instance of a queue
AUTO_COMMAND ACMD_NAME(queue_leaveinstance) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueueLeaveInstance(Entity *pEnt, const char* pchQueueName, U32 uiInstanceID)
{
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	gslQueueLeave_Internal(pEnt, pchQueueName, uiInstanceID);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Transfer to a queue map
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
void gslQueue_MapTransferMember(U32 iEntID, QueueInstantiationInfo* pInstantiationInfo)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iEntID);
	if (pEnt && pInstantiationInfo)
	{
		QueueDef* pDef = queue_DefFromName(pInstantiationInfo->pchQueueDef);
		const char* pchMapName = pInstantiationInfo->pchMapName;
		const char* pchSpawnName = pInstantiationInfo->pchSpawnName;
		const char* pchCannotUseMsgKey;
		S64 iMapKey = pInstantiationInfo->iMapKey;
		U32 uMapID = queue_GetMapIDFromMapKey(iMapKey);
		U32 uPartitionID = queue_GetPartitionIDFromMapKey(iMapKey);
		ChoiceTable *pChoiceTable = SAFE_GET_REF(pDef, QueueMaps.hMapChoiceTable);
		RegionRules *pCurrRules = getRegionRulesFromEnt(pEnt);
		ZoneMapInfo* pNextZoneMap = worldGetZoneMapByPublicName(pchMapName);
		RegionRules *pNextRules = pNextZoneMap ? getRegionRulesFromZoneMap(pNextZoneMap) : NULL;

		// Final check to see if the player is allowed to be in this queue
		// If the player cannot use the queue at this point, 
		// then they have likely tried bypass the restrictions on the queue at the last second
		if (pchCannotUseMsgKey = gslEntCannotUseQueueMsgKey(pEnt, pDef, false, true))
		{
			gslQueue_ResultMessage(iEntID, 0, pInstantiationInfo->pchQueueDef, pchCannotUseMsgKey);
			RemoteCommand_aslQueue_LeaveForced(GLOBALTYPE_QUEUESERVER, 0, iEntID, pInstantiationInfo->pchQueueDef, 0);
			return;
		}

		// Don't bother setting up the QueueInstantiationInfo because we'll lose it during the transfer.

		if (g_CombatConfig.bForceRespawnOnMapLeave)
		{
			gslPlayerRespawn(pEnt, true, false);
		}

		if (!uMapID || !uPartitionID)
		{
			ErrorOrAlert("NO_ID_BEFORE_MAP_SEARCH", "gslQueue_MapTransferMember trying to do SPECIFiC_CONTAINER_AND_PARTITION move with container ID %u, partition ID %u. Both must be set",
				uMapID, uPartitionID);
		}
		if (pChoiceTable)
		{
			WorldVariable* pVar = choice_ChooseValue(pChoiceTable, pDef->QueueMaps.pchTableEntry, 0, 0, timeSecondsSince2000());
			if(pVar->eType == WVAR_MAP_POINT)
			{
				pchSpawnName = pVar->pcStringVal;
			}
		}
		gslEntityPlayTransitionSequenceThenMapMoveEx(pEnt,
													 pchMapName, 
													 ZMTYPE_UNSPECIFIED, 
													 pchSpawnName, 
													 0, 
													 uMapID, 
													 uPartitionID,
													 0, 
													 0, 
													 NULL, 
													 pCurrRules, 
													 pNextRules,
													 NULL,
													 TRANSFERFLAG_SPECIFIC_MAP_ONLY|TRANSFERFLAG_IGNORE_ENCOUNTER_SPAWN_LOGIC);
	}
}

AUTO_COMMAND ACMD_SERVERCMD;
void Queue_TestTimer(int time)
{
	static ChoiceTable* pChoiceTable = NULL;
	WorldVariable* pVar = NULL;
	if (!pChoiceTable)
		pChoiceTable = RefSystem_ReferentFromString(g_hChoiceTableDict, "Random_Alerts_Master");

	pVar = choice_ChooseValue(pChoiceTable, "map_name", 0, 0, time);
	printf("%d: %s:%s\n", time, pVar->pcZoneMap, pVar->pcStringVal);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
void gslQueue_AcceptOffer(U32 iEntID, const char *pchQueueName, QueueInstantiationInfo* pInstantiationInfo)
{
	QueueDef* pDef = queue_DefFromName(pchQueueName);
	
	if (pDef)
	{
		gslQueue_ResultMessage(iEntID, 0, pDef->pchName, QUEUE_ACCEPTED_MESG);

		if (pInstantiationInfo && pInstantiationInfo->pchMapName && pInstantiationInfo->pchMapName[0])
		{
			gslQueue_MapTransferMember(iEntID, pInstantiationInfo);
		}
	}
}

// Similar to LeaveMap. Abandon any currently attached queues and maps. Really only useful post
//  queue pop when the player is either on the queue map, or has stepped out and has a "return to map" button
//  (only when StayInQueueOnMapLeave is true)
AUTO_COMMAND ACMD_NAME(queue_abandonqueue) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueueAbandon(Entity *pEnt)
{
	if (pEnt!=NULL)
	{
		bool bLeaveTeam = true;
		gslQueue_HandleAbandonMap(pEnt, pEnt->myContainerID, bLeaveTeam);
	}
}

//  Pass through remote command for gslQueue_HandleAbandonMap. Used when we do a team release queue since not all the entities may be on the same
//  Game server. We rely on Entity magic to call this in the right place
AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);																													 
void gslQueue_rcmd_DoAbandonMap(ContainerID uEntID, bool bLeaveTeam)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, uEntID);
	if (pEnt!=NULL)
	{
		gslQueue_HandleAbandonMap(pEnt, pEnt->myContainerID, bLeaveTeam);
	}
}

//  Pass through remote command for gslQueue_EntAbandonThisMapAndQueue when the ent is on a different game server than the queue map.
// The actual abandon has to be done on the queue map so we get the correct penalty data, etc. The Entity Server calls this
// server (Which is the map server). The actual Entity may not be valid at this stage even though we have the ID
AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);																													 
void gslQueue_DoEntAbandonThisMapAndQueue(ContainerID uEntID, ContainerID uMapID, U32 uPartitionID)
{
	bool bDoMapLeaveIfOnMap=true; // Always do this
	gslQueue_EntAbandonThisMapAndQueue(uEntID, uMapID, uPartitionID, bDoMapLeaveIfOnMap);
}


// Return to the queue map that the player is supposed to be on
AUTO_COMMAND ACMD_NAME(queue_return) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueue_ReturnToQueueMap(Entity *pEnt)
{
	PlayerQueueInfo* pInfo;
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}
	if (pInfo = SAFE_MEMBER2(pEnt, pPlayer, pPlayerQueueInfo))
	{
		if (pInfo->pQueueInstantiationInfo && !pInfo->pQueueInstantiationInfo->bOnCorrectMapPartition)
		{
			gslQueue_MapTransferMember(entGetContainerID(pEnt), pInfo->pQueueInstantiationInfo);
		}
	}
}


static void gslQueueAccept_Internal(Entity *pEnt, const char* pchQueueName, U32 uiInstanceID)
{
	VerifyServerTypeExistsInShard(GLOBALTYPE_QUEUESERVER);
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}
	if(pEnt->pPlayer && pEnt->pPlayer->pPlayerQueueInfo)
	{
		const char *pchMesg;
		PlayerQueueInstance* pInstance;
		QueueInstanceParams* pParams;
		QueueDef* pDef = queue_DefFromName(pchQueueName);
		const char *pcCurrentMap = zmapInfoGetPublicName(NULL);
		ZoneMapInfo* pCurrZoneMap = worldGetZoneMapByPublicName(pcCurrentMap);
		bool bIgnoreLevelRestrictions;

		if (uiInstanceID > 0)
		{
			pInstance = queue_FindPlayerQueueInstanceByID(pEnt->pPlayer->pPlayerQueueInfo, pchQueueName, uiInstanceID, true);
		}
		else
		{
			PlayerQueue* pQueue = eaIndexedGetUsingString(&pEnt->pPlayer->pPlayerQueueInfo->eaQueues, pchQueueName);
			pInstance = pQueue && !pQueue->eCannotUseReason ? eaGet(&pQueue->eaInstances,0) : NULL;
		}

		pParams = pInstance ? pInstance->pParams : NULL;

		//You can't transfer from a zone map that requires confirmation
		if (pCurrZoneMap && zmapInfoConfirmPurchasesOnExit(pCurrZoneMap))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_POHO_MESG);
			return;
		}
		// Validation - can't be done on a pvp map or a map with a leaver penalty
		if (gslQueue_DisableJoinOrCreateOnCurrentMap(pEnt))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_INVALID_MAP_MESG);
			return;
		}
		if (gslQueue_IsLeaverPenaltyEnabled(entGetPartitionIdx(pEnt)))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_INVALID_MAP_MESG);
			return;
		}

		bIgnoreLevelRestrictions = SAFE_MEMBER(pInstance, bIgnoreLevelRestrictions);
		
		if (pchMesg = gslEntCannotUseQueueInstanceMsgKey(pEnt, pParams, pDef, true, bIgnoreLevelRestrictions))
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, pchMesg);
			gslQueueLeave(pEnt, pchQueueName);	// Remove the player from the queue (they shouldn't be in this queue)
			return;
		}

		if (pDef && pInstance)
		{
			switch (pInstance->eQueueState)
			{
				xcase PlayerQueueState_Offered:
				case PlayerQueueState_InMap:
				{
					RemoteCommand_aslQueue_AcceptOffer(GLOBALTYPE_QUEUESERVER, 0, entGetContainerID(pEnt), pchQueueName, pInstance->uiID);
				}
				xcase PlayerQueueState_Invited:
				{
					RemoteCommand_aslQueue_AcceptInvite(GLOBALTYPE_QUEUESERVER, 0, entGetContainerID(pEnt), pchQueueName, pInstance->uiID);
				}
				xdefault:
				{
					gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_NOT_MEMBER_MESG);
				}
			}
		}
	}
}

AUTO_COMMAND ACMD_NAME(Queue_JoinActiveMap) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueue_JoinActiveMap(Entity *pEnt)
{
	PlayerQueueInfo *pPlayerQueueInfo = SAFE_MEMBER2(pEnt, pPlayer, pPlayerQueueInfo);
	
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	if(entity_IsUGCCharacter(pEnt))
	{
		return;
	}

	if (pPlayerQueueInfo)
	{
		int iQueueIdx, iInstanceIdx;
		for (iQueueIdx = eaSize(&pPlayerQueueInfo->eaQueues)-1; iQueueIdx >= 0; iQueueIdx--)
		{
			PlayerQueue* pQueue = pPlayerQueueInfo->eaQueues[iQueueIdx];
			QueueDef* pDef = GET_REF(pQueue->hDef);
			
			if (!pDef || pQueue->eCannotUseReason != QueueCannotUseReason_None) {
				continue;
			}
			for (iInstanceIdx = eaSize(&pQueue->eaInstances)-1; iInstanceIdx >= 0; iInstanceIdx--)
			{
				PlayerQueueInstance* pInstance = pQueue->eaInstances[iInstanceIdx];
				if (pInstance->eQueueState == PlayerQueueState_InQueue && queue_IsPrivateMatch(pInstance->pParams))
				{
					PlayerQueueMap* pMap = queue_FindActiveMapForPrivatePlayerInstance(pInstance, pDef);

					if (pMap && pMap->iKey != pInstance->iOfferedMapKey)
					{
						RemoteCommand_aslQueue_JoinActiveMap(GLOBALTYPE_QUEUESERVER, 0, entGetContainerID(pEnt), pQueue->pchQueueName, pInstance->uiID);
					}
					return;
				}
			}
		}
	}
}


// Transfer to a queue map
AUTO_COMMAND ACMD_NAME(queue_accept) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue, Interface) ACMD_HIDE;
void gslQueueAccept(Entity *pEnt, const char* pchQueueName)
{
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}
	gslQueueAccept_Internal(pEnt, pchQueueName, 0);
}

// Transfer to a queue map
AUTO_COMMAND ACMD_NAME(queue_acceptinstance) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue, Interface) ACMD_HIDE;
void gslQueueAcceptInstance(Entity *pEnt, const char* pchQueueName, U32 uiInstanceID)
{
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	gslQueueAccept_Internal(pEnt, pchQueueName, uiInstanceID);
}

static void gslQueueDelay_Internal(Entity *pEnt, const char* pchQueueName, U32 uiInstanceID)
{
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pPlayerQueueInfo)
	{
		PlayerQueueInstance* pInstance;

		if (uiInstanceID > 0)
		{
			pInstance = queue_FindPlayerQueueInstanceByID(pEnt->pPlayer->pPlayerQueueInfo, pchQueueName, uiInstanceID, true);
		}
		else
		{
			PlayerQueue* pQueue = eaIndexedGetUsingString(&pEnt->pPlayer->pPlayerQueueInfo->eaQueues, pchQueueName);
			pInstance = pQueue && !pQueue->eCannotUseReason ? eaGet(&pQueue->eaInstances,0) : NULL;
		}

		if(pInstance && pInstance->pParams && pInstance->pParams->uiOwnerID == 0) //Can't delay private queue instances
		{
			if(pInstance->eQueueState == PlayerQueueState_Offered)
				RemoteCommand_aslQueue_DelayOffer(GLOBALTYPE_QUEUESERVER, 0, entGetContainerID(pEnt), pchQueueName, pInstance->uiID);
			else
				gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_NOT_OFFERED_MESG);
		}
		else
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_NOT_MEMBER_MESG);
		}
	}
}

// Delay participating in a full queue
AUTO_COMMAND ACMD_NAME(queue_delay) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueueDelay(Entity *pEnt, const char* pchQueueName)
{
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	gslQueueDelay_Internal(pEnt, pchQueueName, 0);
}

// Delay participating in a full queue instance
AUTO_COMMAND ACMD_NAME(queue_delayinstance) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueueDelayInstance(Entity *pEnt, const char* pchQueueName, U32 uiInstanceID)
{
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	gslQueueDelay_Internal(pEnt, pchQueueName, uiInstanceID);
}

static void gslQueueResume_Internal(Entity *pEnt, const char* pchQueueName, U32 uiInstanceID)
{
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pPlayerQueueInfo)
	{
		PlayerQueueInstance* pInstance;

		if (uiInstanceID > 0)
		{
			pInstance = queue_FindPlayerQueueInstanceByID(pEnt->pPlayer->pPlayerQueueInfo, pchQueueName, uiInstanceID, true);
		}
		else
		{
			PlayerQueue* pQueue = eaIndexedGetUsingString(&pEnt->pPlayer->pPlayerQueueInfo->eaQueues, pchQueueName);
			pInstance = pQueue && !pQueue->eCannotUseReason ? eaGet(&pQueue->eaInstances,0) : NULL;
		}

		if(pInstance && pInstance->pParams)
		{
			if(pInstance->eQueueState == PlayerQueueState_Delaying)
				RemoteCommand_aslQueue_Resume(GLOBALTYPE_QUEUESERVER, 0, entGetContainerID(pEnt), pchQueueName, pInstance->uiID);
			else
				gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_NOT_DELAYED_MESG);
		}
		else
		{
			gslQueue_ResultMessage(pEnt->myContainerID, 0, pchQueueName, QUEUE_NOT_MEMBER_MESG);
		}
	}
}

// Re-enter a queue after delaying
AUTO_COMMAND ACMD_NAME(queue_resumeinstance) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueueResumeInstance(Entity *pEnt, const char* pchQueueName, U32 uiInstanceID)
{
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	gslQueueResume_Internal(pEnt, pchQueueName, uiInstanceID);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Voting commands and helper functions
///////////////////////////////////////////////////////////////////////////////////////////

// Attempt to concede the match
AUTO_COMMAND ACMD_NAME(queue_concede) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueueConcede(Entity *pEnt)
{
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}
	if (!gslQueue_IsQueueMap())
	{
		return;
	}
	if (!gslQueue_ConcedeVote(pEnt))
	{
		gslQueue_ResultMessage(pEnt->myContainerID, 0, NULL, QUEUE_CANNOT_CONCEDE_MESG);
	}
}

AUTO_COMMAND ACMD_NAME(queue_votekick) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Queue) ACMD_HIDE;
void gslQueueVoteKick(Entity *pEnt, U32 uKickPlayerID)
{
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}
	if (!gslQueue_IsQueueMap())
	{
		return;
	}
	if (!gslQueue_VoteKick(pEnt, uKickPlayerID))
	{
		gslQueue_ResultMessage(pEnt->myContainerID, 0, NULL, QUEUE_CANNOT_VOTE_KICK_MESG);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// QueueServer updates
///////////////////////////////////////////////////////////////////////////////////////////

//bfoxworthy  10/28/2009 4:59:54 PM
//[CO-70804] Added a new method to spawn multiple Nemeses on a map.
//  - Added a new Map Variable type, "PLAYER".  This can't be set from data, only code.
//  - Maps created by Queues now have PLAYER Map Variables set up for each player on the map.  These are in the form "TeamXPlayerN".
//  - Encounters can specify a PLAYER Map Variable to use as the encounter's "owner".  The encounter's owner is used when spawning Nemesis critters.\
// WOLF[10Jan13] Re-extracted this code and made it behave with multiple map updates if the team changes. 

void gslQueue_UpdateMatchNemesisMapVars(QueueMatch *pOldMatchMembers, QueueMatch *pNewMatchMembers,	int iPartitionIdx)
{
	char *estrBuffer = NULL;
	int i, j;
	
	estrStackCreate(&estrBuffer);

	// Set up Player map variables
	if (pOldMatchMembers)
	{
		for (i = 0; i < eaSize(&pOldMatchMembers->eaGroups); i++) {
			QueueGroup *pGroup = pOldMatchMembers->eaGroups[i];
			for (j = 0; j < eaSize(&pGroup->eaMembers); j++) {
				MapVariable *pMapVar = NULL;
				estrPrintf(&estrBuffer, "Team%dPlayer%d", pGroup->iGroupIndex+1, j+1);
				pMapVar = mapvariable_GetByName(iPartitionIdx, estrBuffer);

				if (pMapVar && pMapVar->pVariable && pMapVar->pVariable->eType == WVAR_PLAYER) {
					// UNSET because we may be getting rid of them. Hopefully this will not break stuff. Nemesis aren't being used now anyway?
					pMapVar->pVariable->uContainerID = 0;
				}
			}
		}
	}
	
	// Set up Player map variables
	if (pNewMatchMembers)
	{
		for (i = 0; i < eaSize(&pNewMatchMembers->eaGroups); i++) {
			QueueGroup *pGroup = pNewMatchMembers->eaGroups[i];
			for (j = 0; j < eaSize(&pGroup->eaMembers); j++) {
				MapVariable *pMapVar = NULL;
				estrPrintf(&estrBuffer, "Team%dPlayer%d", pGroup->iGroupIndex+1, j+1);
				pMapVar = mapvariable_GetByName(iPartitionIdx, estrBuffer);

				if (pMapVar && pMapVar->pVariable && pMapVar->pVariable->eType == WVAR_PLAYER) {
					pMapVar->pVariable->uContainerID = pGroup->eaMembers[j]->uEntID;
				}
			}
		}
	}
	estrDestroy(&estrBuffer);
}


void gslQueue_UpdateQueueMatchDebug(QueueMatch *pOldMatchMembers, QueueMatch *pNewMatchMembers)
{
	int i, j;
	
	if (g_iDebugQueueServer)
	{
		// Go through the new matches and see what's added
		if (pNewMatchMembers)
		{
			for (i = 0; i < eaSize(&pNewMatchMembers->eaGroups); i++)
			{
				QueueGroup* pNewGroup = pNewMatchMembers->eaGroups[i];
				S32 iExistingGroupIdx = queue_Match_FindGroupByIndex(pOldMatchMembers, pNewGroup->iGroupIndex);
				QueueGroup* pExistingGroup = pOldMatchMembers ? eaGet(&pOldMatchMembers->eaGroups, iExistingGroupIdx) : NULL;
					
				for (j = 0; j < eaSize(&pNewGroup->eaMembers); j++)
				{
					QueueMatchMember* pMember = pNewGroup->eaMembers[j];
					if (queue_Match_FindMemberInGroup(pExistingGroup, pMember->uEntID) < 0)
					{
						// Was not there, or it's a new group
						qDebugPrintf("Entity %d added to match (group %d)\n",pMember->uEntID,pNewGroup->iGroupIndex);
					}
				}
			}
		}
		
		// Go through the old matches and see what's no longer there
		if (pOldMatchMembers)
		{
			for (i = 0; i < eaSize(&pOldMatchMembers->eaGroups); i++)
			{
				QueueGroup* pOldGroup = pOldMatchMembers->eaGroups[i];
				S32 iNewGroupIdx = queue_Match_FindGroupByIndex(pNewMatchMembers, pOldGroup->iGroupIndex);
				QueueGroup* pNewGroup = pNewMatchMembers ? eaGet(&pNewMatchMembers->eaGroups, iNewGroupIdx) : NULL;
					
				for (j = 0; j < eaSize(&pOldGroup->eaMembers); j++)
				{
					QueueMatchMember* pMember = pOldGroup->eaMembers[j];
					if (queue_Match_FindMemberInGroup(pNewGroup, pMember->uEntID) < 0)
					{
						// Was not there, or it's a new group
						qDebugPrintf("Entity %d removed from match (group %d)\n",pMember->uEntID,pOldGroup->iGroupIndex);
					}
				}
			}
		}
	}
}


AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
void gslQueue_UpdateQueueMatch(U32 uPartitionID, QueueMatch *pNewMatchMembers)
{
	int iPartitionIdx = partition_IdxFromID(uPartitionID);
	QueuePartitionInfo *pInfo = gslQueuePartitionInfoFromIdx(iPartitionIdx);

	if (!pInfo)
	{
		gslQueue_AddPendingMatchUpdate(uPartitionID, pNewMatchMembers);
		return;
	}

	qDebugPrintf("Update Queue Match---------\n");

	if (!pInfo->pMatch)
	{
		pInfo->pMatch = StructCreate(parse_QueueMatch);
	}
	
	if(pNewMatchMembers && pNewMatchMembers->uUpdateID > pInfo->pMatch->uUpdateID)
	{
		gslQueue_UpdateMatchNemesisMapVars(pInfo->pMatch, pNewMatchMembers, iPartitionIdx);

		// Do some Debuggy stuff for qDebug

		if (g_iDebugQueueServer)
		{
			gslQueue_UpdateQueueMatchDebug(pInfo->pMatch, pNewMatchMembers);
		}

		// This will deal with resizing eArrays, etc. etc.
		StructCopyAll(parse_QueueMatch, pNewMatchMembers, pInfo->pMatch);

		pInfo->pMatch->bHasNewGroupData=true;
	}

	if(g_iDebugQueueServer)
	{
		gslQueuePartition_PrintMatchList(pInfo);
	}

	// Update the local teams
	gslQueue_LocalTeamUpdate(pInfo);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
void gslQueue_RemoveMemberFromMatch(U32 uPartitionID, ContainerID iEntID)
{
	S32 iPartitionIdx = partition_IdxFromID(uPartitionID);
	QueuePartitionInfo *pInfo = gslQueuePartitionInfoFromIdx(iPartitionIdx);
	if (pInfo && pInfo->pMatch)
	{
		S32 iGroupIndex;
		for (iGroupIndex = eaSize(&pInfo->pMatch->eaGroups)-1; iGroupIndex >= 0; iGroupIndex--)
		{
			QueueGroup *pGroup = eaGet(&pInfo->pMatch->eaGroups, iGroupIndex);
			if (pGroup)
			{
				S32 iMemberIdx = queue_Match_FindMemberInGroup(pGroup, iEntID);
				if (iMemberIdx >= 0)
				{
					StructDestroy(parse_QueueMatchMember, eaRemove(&pGroup->eaMembers, iMemberIdx));
				}
				pGroup->iGroupSize = eaSize(&pGroup->eaMembers);

				qDebugPrintf("Entity %d removed from match\n",iEntID);
			}
		}

		pInfo->pMatch->bHasNewGroupData=true;
		
		// Update the local teams
		gslQueue_LocalTeamUpdate(pInfo);
	}
}



////////////////////////////////////////////////////////////////////////
// CSR Commands
////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_NAME(queue_Csr_ResetQueue) ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(CSR, QueueAdmin);
void gslQueueCsrCmd_ResetQueue(Entity *pEnt, ACMD_NAMELIST("QueueDef", REFDICTIONARY) char *pchQueueName)
{
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	RemoteCommand_aslQueueCsrCmd_ResetQueue(GLOBALTYPE_QUEUESERVER, 0, pchQueueName);
}

AUTO_COMMAND ACMD_NAME(queue_Csr_FixStalledQueue) ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(CSR, QueueAdmin);
void gslQueueCsrCmd_FixStalledQueue(Entity *pEnt, ACMD_NAMELIST("QueueDef", REFDICTIONARY) char *pchQueueName)
{
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	RemoteCommand_aslQueueCsrCmd_FixStalledQueue(GLOBALTYPE_QUEUESERVER, 0, pchQueueName);
}

AUTO_COMMAND ACMD_NAME(queue_Csr_FixAllStalledQueues) ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(CSR, QueueAdmin);
void gslQueueCsrCmd_FixAllStalledQueues(Entity *pEnt)
{
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	RemoteCommand_aslQueueCsrCmd_FixAllStalledQueues(GLOBALTYPE_QUEUESERVER, 0);
}

// Removes all penalties given to the entity specified by the container ID (uPlayerID)
AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(QueueRemoveAllPenaltiesByEntID) ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(CSR, QueueAdmin);
void gslQueue_RemoveAllPenaltiesByEntID(U32 uPlayerID)
{
	RemoteCommand_aslQueue_RemoveAllPenalties(GLOBALTYPE_QUEUESERVER, 0, uPlayerID);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Debugging Commands
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_NAME(queue_AutoFill) ACMD_ACCESSLEVEL(7) ACMD_SERVERCMD ACMD_CATEGORY(Queue);
void gslDebug_AutoFill(Entity *pEnt, ACMD_NAMELIST("QueueDef", REFDICTIONARY) char *pchQueueName)
{
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	RemoteCommand_aslQueueCmd_AutoFillQueues(GLOBALTYPE_QUEUESERVER, 0, pchQueueName);
}

AUTO_COMMAND ACMD_NAME(queue_AutoFillMyQueues) ACMD_ACCESSLEVEL(7) ACMD_SERVERCMD ACMD_CATEGORY(Queue);
void gslDebug_QueueAutoFillMine(Entity *pEnt)
{
	int i;

	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	if(! SAFE_MEMBER3(pEnt,pPlayer,pPlayerQueueInfo,eaQueues))
		return;

	for(i=0;i<eaSize(&pEnt->pPlayer->pPlayerQueueInfo->eaQueues);i++)
	{
		RemoteCommand_aslQueueCmd_AutoFillQueues(GLOBALTYPE_QUEUESERVER, 0, pEnt->pPlayer->pPlayerQueueInfo->eaQueues[i]->pchQueueName);
	}
}

static bool gslDebug_inQueue(Entity *pEntity, QueueInfo *pQueueInfo)
{
	S32 j;

	for(j = 0; j < eaSize(&pQueueInfo->eaInstances); ++j)
	{
		if (eaIndexedGetUsingInt(&pQueueInfo->eaInstances[j]->eaUnorderedMembers, pEntity->myContainerID)!=NULL)
		{
			return true;
		}
	}

	return false;
}

// This auto-command gives more detailed info on the queue(s) the player is joined to
// Hidden as it designed to be used by developers although would be safe to use by anyone
AUTO_COMMAND ACMD_NAME(queue_ShowMyInfo) ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(Queue);
void gslDebug_QueueShowMyInfo(Entity *pEnt)
{
	S32 i;

	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	if(! SAFE_MEMBER3(pEnt,pPlayer,pPlayerQueueInfo,eaQueues))
		return;

	if(!g_pQueueList)
	{
		return;
	}

	for(i=0;i<eaSize(&pEnt->pPlayer->pPlayerQueueInfo->eaQueues);i++)
	{
		S32 iQueueRefIdx, iNumQueueRefs = eaSize(&g_pQueueList->eaQueueRefs);
		for (iQueueRefIdx = 0; iQueueRefIdx < iNumQueueRefs; ++iQueueRefIdx)
		{
			QueueInfo *pQueueInfo = GET_REF(g_pQueueList->eaQueueRefs[iQueueRefIdx]->hDef);
			if(pQueueInfo && stricmp(pQueueInfo->pchName, pEnt->pPlayer->pPlayerQueueInfo->eaQueues[i]->pchQueueName) == 0 && gslDebug_inQueue(pEnt, pQueueInfo))
			{
				char * esInfo = NULL;
				S32 j, iCount = 0, iOnMap = 0, iInQueue = 0, iOffered = 0;
				
				estrPrintf(&esInfo, "%s", pQueueInfo->pchName);
				for(j = 0; j < eaSize(&pQueueInfo->eaInstances); ++j)
				{
					S32 k;
					estrConcatf(&esInfo, " ID(%d)", pQueueInfo->eaInstances[j]->uiID);
					for(k = 0; k < eaSize(&pQueueInfo->eaInstances[j]->eaUnorderedMembers); ++k)
					{
						if(pQueueInfo->eaInstances[j]->eaUnorderedMembers[k]->eState == PlayerQueueState_InQueue)
						{
							++iInQueue;
						}
						else if(pQueueInfo->eaInstances[j]->eaUnorderedMembers[k]->eState == PlayerQueueState_Offered)
						{
							++iOffered;
						}
						else if
						(	
							pQueueInfo->eaInstances[j]->eaUnorderedMembers[k]->eState == PlayerQueueState_Accepted ||
							pQueueInfo->eaInstances[j]->eaUnorderedMembers[k]->eState == PlayerQueueState_Countdown	
						)
						{
							++iCount;
						}
						else if
						(
							pQueueInfo->eaInstances[j]->eaUnorderedMembers[k]->eState == PlayerQueueState_InMap ||
							pQueueInfo->eaInstances[j]->eaUnorderedMembers[k]->eState == PlayerQueueState_Limbo
						)
						{
							++iOnMap;
						}
					}
				}

				if(iInQueue > 0)
				{
					estrConcatf(&esInfo, " In Queue %d", iInQueue);
				}
				if(iOffered > 0)
				{
					estrConcatf(&esInfo, " Offered %d", iOffered);
				}
				if(iCount > 0)
				{
					estrConcatf(&esInfo, " Accepted %d", iCount);
				}
				if(iOnMap > 0)
				{
					estrConcatf(&esInfo, " In maps %d", iOnMap);
				}

				notify_NotifySend(pEnt, kNotifyType_GameplayAnnounce, esInfo, NULL, NULL);
				estrDestroy(&esInfo);

			}
		}
	}
}

static void gslQueueDumpInstanceInfoCB(TransactionReturnVal *pReturnStruct, void *pData)
{
	QueueInstanceWrapper *pInstance;
	switch(RemoteCommandCheck_aslQueue_DbgGetInstanceInfo(pReturnStruct, &pInstance))	
	{
	case TRANSACTION_OUTCOME_FAILURE:
		{
			break;
		}
	case TRANSACTION_OUTCOME_SUCCESS:
		{
			if(pInstance)
			{
				Entity *pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pInstance->uEntID);
				if(pEntity)
				{
					ClientCmd_gclDumpQueueInstance(pEntity, pInstance->pQueueInstance);
				}
				StructDestroySafe(parse_QueueInstanceWrapper, &pInstance);
			}
			break;
		}
	}
}

// get this queueinstance and send to client where it is written out.
AUTO_COMMAND ACMD_NAME(queue_DumpInstanceInfo) ACMD_ACCESSLEVEL(7) ACMD_SERVERCMD ACMD_CATEGORY(Queue);
void gslDebug_DumpInstanceInfo(Entity *pEnt, const char *pcQueueName, S32 iID)
{
	if(pcQueueName && pEnt)
	{
		// Setup remote auto-command 
		TransactionReturnVal *pReturnVal;
		pReturnVal = objCreateManagedReturnVal(gslQueueDumpInstanceInfoCB, NULL);
		RemoteCommand_aslQueue_DbgGetInstanceInfo(pReturnVal, GLOBALTYPE_QUEUESERVER, 0, pcQueueName, iID, pEnt->myContainerID);
	}
}

static void gslQueueDumpQueueInfoCB(TransactionReturnVal *pReturnStruct, void *pData)
{
	QueueInfoWrapper *pInfo;
	switch(RemoteCommandCheck_aslQueue_DbgGetQueueInfo(pReturnStruct, &pInfo))	
	{
	case TRANSACTION_OUTCOME_FAILURE:
		{
			break;
		}
	case TRANSACTION_OUTCOME_SUCCESS:
		{
			if(pInfo)
			{
				Entity *pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pInfo->uEntID);
				if(pEntity)
				{
					ClientCmd_gclDumpQueueInfo(pEntity, pInfo->pQueueInfo);
				}
				StructDestroySafe(parse_QueueInfoWrapper, &pInfo);
			}
			break;
		}
	}
}

// get this queueinstance and send to client where it is written out.
AUTO_COMMAND ACMD_NAME(queue_DumpQueueInfo) ACMD_ACCESSLEVEL(7) ACMD_SERVERCMD ACMD_CATEGORY(Queue);
void gslDebug_DumpQueueInfo(Entity *pEnt, ACMD_NAMELIST("Queuedef", REFDICTIONARY) const char *pcQueueName)
{
	if(pcQueueName && pEnt)
	{
		// Setup remote auto-command 
		TransactionReturnVal *pReturnVal;
		pReturnVal = objCreateManagedReturnVal(gslQueueDumpQueueInfoCB, NULL);
		RemoteCommand_aslQueue_DbgGetQueueInfo(pReturnVal, GLOBALTYPE_QUEUESERVER, 0, pcQueueName, pEnt->myContainerID);
	}
}


// get all player queueinstance and send to client where it is written out.
AUTO_COMMAND ACMD_NAME(queue_DumpMyInstanceInfo) ACMD_ACCESSLEVEL(7) ACMD_SERVERCMD ACMD_CATEGORY(Queue);
void gslDebug_DumpMyInstanceInfo(Entity *pEnt)
{
	S32 i;
	bool bSuccess = false;

	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		notify_NotifySend(pEnt, kNotifyType_GameplayAnnounce, "No ent or queues are disabled", NULL, NULL);
		return;
	}

	if(! SAFE_MEMBER3(pEnt,pPlayer,pPlayerQueueInfo,eaQueues))
	{
		notify_NotifySend(pEnt, kNotifyType_GameplayAnnounce, "No queue info on player", NULL, NULL);
		return;
	}

	if(!g_pQueueList)
	{
		notify_NotifySend(pEnt, kNotifyType_GameplayAnnounce, "No queue info on game server", NULL, NULL);
		return;
	}

	for(i=0;i<eaSize(&pEnt->pPlayer->pPlayerQueueInfo->eaQueues);i++)
	{
		S32 iQueueRefIdx, iNumQueueRefs = eaSize(&g_pQueueList->eaQueueRefs);
		for (iQueueRefIdx = 0; iQueueRefIdx < iNumQueueRefs; ++iQueueRefIdx)
		{
			QueueInfo *pQueueInfo = GET_REF(g_pQueueList->eaQueueRefs[iQueueRefIdx]->hDef);
			if(pQueueInfo && stricmp(pQueueInfo->pchName, pEnt->pPlayer->pPlayerQueueInfo->eaQueues[i]->pchQueueName) == 0 && gslDebug_inQueue(pEnt, pQueueInfo))
			{
				S32 j;
				for(j = 0; j < eaSize(&pQueueInfo->eaInstances); ++j)
				{
					gslDebug_DumpInstanceInfo(pEnt, pQueueInfo->pchName, pQueueInfo->eaInstances[j]->uiID);
					bSuccess = true;
				}
			}
		}
	}

	if(!bSuccess)
	{
		notify_NotifySend(pEnt, kNotifyType_GameplayAnnounce, "This character is not in any queues.", NULL, NULL);
	}
}

AUTO_COMMAND ACMD_NAME(queue_NeverLaunchMaps) ACMD_ACCESSLEVEL(7) ACMD_SERVERCMD ACMD_CATEGORY(Queue);
void gslDebug_NeverLaunchMaps(Entity *pEnt, ACMD_NAMELIST("QueueDef", REFDICTIONARY) char *pchQueueName, bool bEnabled)
{
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	RemoteCommand_aslQueueCmd_NeverLaunchMaps(GLOBALTYPE_QUEUESERVER, 0, pchQueueName, bEnabled);
}

static void gslQueueMovePlayer_CB(TransactionReturnVal* pReturn, GSLQueueCBData* pData)
{
	QueueInstanceParams* pParams = (QueueInstanceParams*)pData->pUserData;
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		Entity* pJoinPlayer = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pData->uiSubjectID);
		if (pJoinPlayer)
		{
			const char* pchQueueName = pData->pchQueueName;
			U32 uInstanceID = pData->uiInstanceID;
			U32 uTeamID = team_GetTeamID(pJoinPlayer);
			gslQueue_JoinParams(pJoinPlayer, pchQueueName, uTeamID, uInstanceID, 0, false, true, false, pParams);
		}
	}
	pData->pUserData = NULL;
	StructDestroySafe(parse_QueueInstanceParams, &pParams);
	SAFE_FREE(pData);
}

static void gslQueuePlayerGetOwner_CB(TransactionReturnVal* pReturn, GSLQueueCBData* pData)
{
	ContainerRef *pLocation = NULL;
	QueueInstanceParams* pParams = (QueueInstanceParams*)pData->pUserData;
	switch (RemoteCommandCheck_ContainerGetOwner(pReturn, &pLocation))
	{
		xcase TRANSACTION_OUTCOME_SUCCESS:
		{
			if (pLocation->containerType == objServerType() && pLocation->containerID == objServerID())
			{
				RemoteCommand_gslQueue_rcmd_JoinParams(GLOBALTYPE_ENTITYPLAYER, 
													   pData->uiSubjectID,
													   pData->uiSubjectID,
													   pData->pchQueueName,
													   pData->uiInstanceID,
													   0,
													   false,
													   true,
													   false,
													   pParams);									   
			}
			else
			{
				TransactionReturnVal* pNewReturn = objCreateManagedReturnVal(gslQueueMovePlayer_CB, pData);
				pData->eLocationType = pLocation->containerType;
				pData->uLocationID = pLocation->containerID;
				objRequestContainerMove(pNewReturn,
										GLOBALTYPE_ENTITYPLAYER, 
										pData->uiSubjectID, 
										GLOBALTYPE_OBJECTDB, 
										0, 
										objServerType(), 
										objServerID());
				return;
			}
		}
	}
	pData->pUserData = NULL;
	StructDestroySafe(parse_QueueInstanceParams, &pParams);
	SAFE_FREE(pData);
}


static void gslQueueJoinPlayerByName_CB(Entity* pEnt, U32 uJoinPlayerID, U32 uiAccountID, U32 uiLoginServerID, GSLQueueCBData* pData)
{
	Entity* pJoinPlayer = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, uJoinPlayerID);
	QueueInstanceParams* pParams = (QueueInstanceParams*)pData->pUserData;

	if (pEnt)
	{
		if (pJoinPlayer)
		{
			const char* pchQueueName = pData->pchQueueName;
			U32 uInstanceID = pData->uiInstanceID;
			U32 uTeamID = team_GetTeamID(pJoinPlayer);
			gslQueue_JoinParams(pJoinPlayer, pchQueueName, uTeamID, uInstanceID, 0, false, true, false, pParams);
		}
		else
		{
			TransactionReturnVal* pReturn = objCreateManagedReturnVal(gslQueuePlayerGetOwner_CB, pData);
			pData->uiSubjectID = uJoinPlayerID;
			RemoteCommand_ContainerGetOwner(pReturn, GLOBALTYPE_ENTITYPLAYER, uJoinPlayerID);
			return;
		}
	}
	pData->pUserData = NULL;
	StructDestroySafe(parse_QueueInstanceParams, &pParams);
	SAFE_FREE(pData);
}

AUTO_COMMAND ACMD_NAME(queue_InstanceJoinPlayerByName) ACMD_ACCESSLEVEL(9) ACMD_SERVERCMD ACMD_CATEGORY(Queue);
void gslDebug_InstanceJoinPlayerByName(Entity *pEnt, 
									   ACMD_NAMELIST("QueueDef", REFDICTIONARY) char* pchQueueName, 
									   U32 uInstanceID, 
									   const char* pchPlayerName)
{
	PlayerQueueInfo* pQueueInfo;
	PlayerQueueInstance* pInstance;
	
	if (!pEnt || mapState_ArePVPQueuesDisabled(mapState_FromEnt(pEnt)))
	{
		return;
	}

	pQueueInfo = SAFE_MEMBER2(pEnt, pPlayer, pPlayerQueueInfo);
	pInstance = queue_FindPlayerQueueInstanceByID(pQueueInfo, pchQueueName, uInstanceID, false);

	if (pInstance)
	{
		QueueInstanceParams* pParams = StructClone(parse_QueueInstanceParams, pInstance->pParams);
		GSLQueueCBData* pData = gslQueue_MakeCBData(pchQueueName, uInstanceID, NULL, pParams);
		gslPlayerResolveHandle(pEnt, pchPlayerName, gslQueueJoinPlayerByName_CB, gslQueue_ResolveHandleFailure_CB, pData);
	}
}


static U32 gslQueue_JoinPlayerByName_GetBestInstance(PlayerQueueInfo* pQueueInfo, const char* pchQueueName)
{
	if (pQueueInfo)
	{
		S32 iQueueIdx, iInstanceIdx;
		for (iQueueIdx = eaSize(&pQueueInfo->eaQueues)-1; iQueueIdx >= 0; iQueueIdx--)
		{
			PlayerQueue* pQueue = pQueueInfo->eaQueues[iQueueIdx];
			if (pQueue->eCannotUseReason != QueueCannotUseReason_None || stricmp(pQueue->pchQueueName, pchQueueName) != 0)
			{
				continue;
			}
			for (iInstanceIdx = eaSize(&pQueue->eaInstances)-1; iInstanceIdx >= 0; iInstanceIdx--)
			{
				PlayerQueueInstance* pInstance = pQueue->eaInstances[iInstanceIdx];

				switch (pInstance->eQueueState)
				{
					xcase PlayerQueueState_InQueue:
					acase PlayerQueueState_Offered:
					acase PlayerQueueState_Accepted:
					{
						return pInstance->uiID;
					}
				}
			}
		}
	}
	return 0;
}

AUTO_COMMAND ACMD_NAME(queue_JoinPlayerByName) ACMD_ACCESSLEVEL(9) ACMD_SERVERCMD ACMD_CATEGORY(Queue);
void gslDebug_JoinPlayerByName(Entity *pEnt, 
							   ACMD_NAMELIST("QueueDef", REFDICTIONARY) char* pchQueueName, 
							   const char* pchPlayerName)
{
	PlayerQueueInfo* pQueueInfo = SAFE_MEMBER2(pEnt, pPlayer, pPlayerQueueInfo);
	U32 uInstanceID = gslQueue_JoinPlayerByName_GetBestInstance(pQueueInfo, pchQueueName);
		
	gslDebug_InstanceJoinPlayerByName(pEnt, pchQueueName, uInstanceID, pchPlayerName);
}

AUTO_COMMAND ACMD_NAME(queue_SendMessageToChannel) ACMD_ACCESSLEVEL(9) ACMD_SERVERCMD ACMD_CATEGORY(Queue);
void gslQueue_cmd_SendMessageToPrivateChatChannel(Entity* pEnt, U32 uSubjectID, const char* pchMessageKey)
{
	PlayerQueueInstance* pInstance = gslQueue_FindPrivateInstance(pEnt, NULL);
	if (pInstance && pInstance->pParams)
	{
		ContainerID uEntID = entGetContainerID(pEnt);
		ContainerID uOwnerID = pInstance->uiOrigOwnerID;
		gslQueue_SendMessageToPrivateChatChannel(uEntID, uOwnerID, pInstance->uiID, pchMessageKey, uSubjectID);
	}
}

/*
// Throw errorfs to the client reporting on the current state of queues on the server
AUTO_COMMAND ACMD_SERVERCMD ACMD_CATEGORY(Queue);
void gslDebug_NumQueuesOnServer(Entity *pEnt)
{
	VerifyServerTypeExistsInShard(GLOBALTYPE_QUEUESERVER);

	if(s_QueueList && pEnt && pEnt->pChar)
	{
		QueueList* clientQueueList = StructCreate(parse_QueueList);
		int i, n = eaSize(&s_QueueList->eaQueues);
		S32 iEntLevel = pEnt->pChar->iLevelCombat;
		int numAllowedQueues = 0;

		// Count the number of queues the client can see
		for(i=0; i<n; i++)
		{
			QueueDesc* pDesc = s_QueueList->eaQueues[i];

			if(gslEntCanUseQueue(pEnt, pDesc))
			{
				numAllowedQueues++;
			}
		}

		Errorf("There are %d queues on the server, and you can see %d of them.\n", n, numAllowedQueues);
	}
	else if(s_QueueList)
		Errorf("There are %d queues on the server, but you don't exist so you can't see them.", eaSize(&s_QueueList->eaQueues));
	else
		Errorf("The server has no queue list.\n");
}
*/

/////////////////////////////////////////////////////////////////////////////////////////////////
// Debug Commands
/////////////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_NAME(PlaceInGroup) ACMD_ACCESSLEVEL(9) ACMD_SERVERCMD ACMD_CATEGORY(QueueDebug);
void gslQueue_cmd_EntityPlaceInGroup(Entity *pEnt, int iTeamIndex)
{
	QueuePartitionInfo *pInfo = gslQueuePartitionInfoFromIdx(entGetPartitionIdx(pEnt));

	if(pInfo && pInfo->pMatch)
	{
		if(iTeamIndex < eaSize(&pInfo->pMatch->eaGroups) && !queue_Match_FindMemberInGroup(pInfo->pMatch->eaGroups[iTeamIndex],entGetContainerID(pEnt)) >= 0)
		{
			gslQueue_RemoveFromGroup(pEnt,pInfo->pMatch);
			queue_Match_AddMember(pInfo->pMatch,iTeamIndex,entGetContainerID(pEnt),pEnt->pTeam && pEnt->pTeam->eState == TeamState_Member ? pEnt->pTeam->iTeamID : 0);	
		}
		
		pInfo->pMatch->bHasNewGroupData=true;
		gslQueue_LocalTeamUpdate(pInfo);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////

// Fill in the eArray of references to the queues
AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
void gslQueue_GetQueueList(QueueList *pList)
{
	//Clear the old list
	if (g_pQueueList)
	{
		gslQueue_ClearQueueList();
	}

	if (pList)
	{
		char pcIDBuff[32];
		S32 iQueueIdx, iNumQueues = eaSize(&pList->eaQueueRefs);
		
		//Make the new list
		g_pQueueList = StructCreate(parse_QueueList);
		
		//Fill it in with all the queues
		for (iQueueIdx = 0; iQueueIdx < iNumQueues; iQueueIdx++)
		{
			if (pList->eaQueueRefs[iQueueIdx])
			{
				QueueRef *pRef = StructClone(parse_QueueRef, pList->eaQueueRefs[iQueueIdx]);
				if (pRef)
				{
					//Setup container subscription for the queues
					sprintf(pcIDBuff, "%d", pRef->iContID);
					SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_QUEUEINFO), pcIDBuff, pRef->hDef);
					eaPush(&g_pQueueList->eaQueueRefs, pRef);
				}
			}
		}
	}

  	// Tell ProcessPlayerQueues that we got new data so it can update any pending players
	s_uiQueueNewDataFromQueueServer=1;
	
	//Reset the static flag
	s_uiQueueListNextRequestTime = 0;
}

// This is run when an entity logs onto a Game server.
// We use it to set up any queue-specific data that should/needs to be on the player.
// --QueueInstantiation data so the QueueUI knows stuff
// --Apply the queue penalty (if the entity had been logged out when the penalty was applied, for instance)
// --If we're on a the wrong map, put the player back where they belong.
AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
void gslQueue_ReceiveQueueState(ContainerID uiPlayerID, U32 bIsQueued, PlayerQueuePenaltyData* pPenaltyData, QueueInstantiationInfo* pQueueInstantiationInfo)
{
	Entity* pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, uiPlayerID);

	if(pEnt && pEnt->pPlayer)
	{
		bool bDirty = false;

		if (!pEnt->pPlayer->pPlayerQueueInfo)
		{
			pEnt->pPlayer->pPlayerQueueInfo = StructCreate(parse_PlayerQueueInfo);
			bDirty = true;
		}
		if (StructCompare(parse_PlayerQueuePenaltyData, pEnt->pPlayer->pPlayerQueueInfo->pPenaltyData, pPenaltyData, 0, 0, 0))
		{
			if (!pEnt->pPlayer->pPlayerQueueInfo->pPenaltyData)
				pEnt->pPlayer->pPlayerQueueInfo->pPenaltyData = StructCreate(parse_PlayerQueuePenaltyData);
			StructCopyAll(parse_PlayerQueuePenaltyData, pPenaltyData, pEnt->pPlayer->pPlayerQueueInfo->pPenaltyData);
			bDirty = true;
		}
		if (bIsQueued)
		{
			gslQueue_RequestPlayerQueuesProcess(pEnt);
		}
		// Always set up the instantiation data now.
		if (pQueueInstantiationInfo && pQueueInstantiationInfo->iMapKey)
		{
			QueueDef* pDef = queue_DefFromName(pQueueInstantiationInfo->pchQueueDef);
			if (!pEnt->pPlayer->pPlayerQueueInfo->pQueueInstantiationInfo)
			{
				pEnt->pPlayer->pPlayerQueueInfo->pQueueInstantiationInfo = StructCreate(parse_QueueInstantiationInfo);
			}
			StructCopyAll(parse_QueueInstantiationInfo, pQueueInstantiationInfo, pEnt->pPlayer->pPlayerQueueInfo->pQueueInstantiationInfo);

			// Set if we are on the correct map for this queue instantiation
			if (gGSLState.gameServerDescription.baseMapDescription.containerID==queue_GetMapIDFromMapKey(pEnt->pPlayer->pPlayerQueueInfo->pQueueInstantiationInfo->iMapKey) &&
				entGetPartitionIdx(pEnt)==partition_IdxFromID(queue_GetPartitionIDFromMapKey(pEnt->pPlayer->pPlayerQueueInfo->pQueueInstantiationInfo->iMapKey)))
			{
				pEnt->pPlayer->pPlayerQueueInfo->pQueueInstantiationInfo->bOnCorrectMapPartition=true;
			}
			
			bDirty = true;
			
			if (!(g_QueueConfig.bStayInQueueOnMapLeave || (pDef && pDef->Settings.bStayInQueueOnMapLeave)))
			{
				// No transfer back to the map
				pEnt->pPlayer->pPlayerQueueInfo->pQueueInstantiationInfo->iMapKey = 0;

				if (!gslQueue_IsQueueMap())
				{
					// Automatically transfer the member back to the queue map if they somehow get to a non-queue map
					if(entCheckFlag(pEnt, ENTITYFLAG_IGNORE) != 0)
					{
						Errorf("gslQueue_ReceiveQueueState trying to do a map transfer while entity is in IGNORE state.");
						return;
					}
					else
					{
						gslQueue_MapTransferMember(uiPlayerID, pQueueInstantiationInfo);
					}
				}
			}
		}
		if (bDirty)
		{
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		}
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
void gslQueue_AttemptToPenalizePlayer(U32 uPartitionID, U32 uPlayerID, const char* pchQueueName)
{
	int iPartitionIdx = partition_IdxFromID(uPartitionID);
	if (gslQueue_IsLeaverPenaltyEnabled(iPartitionIdx))
	{
		RemoteCommand_aslQueue_PenalizePlayer(GLOBALTYPE_QUEUESERVER, 0, uPlayerID, pchQueueName);
	}
}

// Removes all penalties given to the entity
AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(QueueRemoveAllPenalties) ACMD_ACCESSLEVEL(7) ACMD_CATEGORY(QueueDebug);
void gslQueue_RemoveAllPenalties(Entity* pEnt)
{
	RemoteCommand_aslQueue_RemoveAllPenalties(GLOBALTYPE_QUEUESERVER, 0, entGetContainerID(pEnt));
}

// Resets all cooldowns on the current entity
AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(QueueResetAllCooldowns) ACMD_ACCESSLEVEL(7) ACMD_CATEGORY(QueueDebug);
void gslQueue_ResetAllCooldowns(Entity* pEnt)
{
	if (pEnt->pPlayer && eaSize(&pEnt->pPlayer->eaQueueCooldowns))
	{
		eaDestroyStruct(&pEnt->pPlayer->eaQueueCooldowns, parse_PlayerQueueCooldown);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME("QueueStopTrackingMapOnQueueServer") ACMD_ACCESSLEVEL(9);
void gslQueueCmd_StopTrackingMapOnQueueServer(Entity* pEntPlayer)
{
	gslQueue_StopTrackingMapOnQueueServer(entGetPartitionIdx(pEntPlayer));
}


void gslQueuePartition_PrintMatchList(QueuePartitionInfo *pInfo)
{
	int iGroup, iMemeber;

	if(!pInfo->pMatch)
	{
		printf("No Match set up for partition %d\n",pInfo->iPartitionIdx);
		return;
	}

	printf("Member List for partition %d------------\n",pInfo->iPartitionIdx);

	for(iGroup=0;iGroup<eaSize(&pInfo->pMatch->eaGroups);iGroup++)
	{
		QueueGroup *pGroup = pInfo->pMatch->eaGroups[iGroup];

		printf("\tGroup %d\n",iGroup);

		for(iMemeber=0;iMemeber<eaSize(&pGroup->eaMembers);iMemeber++)
		{
			printf("\t\tMember %d: Entity ID %d Team ID %d\n",iMemeber,pGroup->eaMembers[iMemeber]->uEntID,pGroup->eaMembers[iMemeber]->uTeamID);
		}
	}

	printf("End Member list------------------------\n");
}

AUTO_COMMAND ACMD_NAME(PrintMatchList) ACMD_CATEGORY(Queue);
void gslQueue_PrintMatchList(Entity *pEntPlayer)
{
	QueuePartitionInfo *pInfo = gslQueuePartitionInfoFromIdx(entGetPartitionIdx(pEntPlayer));

	if(pInfo)
	{
		gslQueuePartition_PrintMatchList(pInfo);
	}
	else
	{
		printf("No Queue partition Info for this player!");
	}
}


AUTO_COMMAND ACMD_NAME(PrintMatchListAll) ACMD_CATEGORY(Queue);
void gslQueue_PrintMatchListAll()
{
	gslQueuePartition_ForEachPartition(gslQueuePartition_PrintMatchList); 
}



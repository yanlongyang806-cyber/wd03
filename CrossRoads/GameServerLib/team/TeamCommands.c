/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "TeamCommands.h"
#include "AutoTransDefs.h"
#include "Character.h"
#include "CharacterClass.h"
#include "Character_target.h"
#include "chatCommon.h"
#include "chatCommonStructs.h"
#include "EntDebugMenu.h"
#include "entCritter.h"
#include "Entity.h"
#include "EntityIterator.h"
#include "EntityLib.h"
#include "EntitySavedData.h"
#include "file.h"
#include "GameAccountDataCommon.h"
#include "GameServerLib.h"
#include "GameStringFormat.h"
#include "Guild.h"
#include "gslChat.h"
#include "gslDoorTransition.h"
#include "gslLogSettings.h"
#include "LoggedTransactions.h"
#include "gslMapState.h"
#include "gslMapTransfer.h"
#include "gslMapVariable.h"
#include "gslMechanics.h"
#include "gslPartition.h"
#include "gslPowerTransactions.h"
#include "gslSavedPet.h"
#include "gslSpawnPoint.h"
#include "gslQueue.h"
#include "gslTransactions.h"
#include "rand.h"
#include "logging.h"
#include "mechanics_common.h"
#include "gslMission.h"
#include "mission_common.h"
#include "gslMission_transact.h"
#include "NotifyCommon.h"
#include "OfficerCommon.h"
#include "Player.h"
#include "PlayerDifficultyCommon.h"
#include "queue_common.h"
#include "RegionRules.h"
#include "SavedPetCommon.h"
#include "StringCache.h"
#include "StringFormat.h"
#include "Team.h"
#include "PowersAutoDesc.h"
#include "TeamPetsCommonStructs.h"
#include "WorldGrid.h"
#include "GameAccountDataCommon.h"
#include "gslChatConfig.h"
#include "GamePermissionsCommon.h"
#include "qsortG.h"
#include "progression_common.h"
#include "serverlib.h"
#include "gslTeamCorral.h"

#include "AutoGen/chatCommon_h_ast.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "Autogen/ChatServer_autogen_remotefuncs.h"
#include "entity_h_ast.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"
#include "AutoGen/team_h_ast.h"
#include "AutoGen/TeamPetsCommonStructs_h_ast.h"
#include "AutoGen/TeamCommands_c_ast.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"


// The command was successful if the string is empty (or doesn't exist).
#define IS_SUCCESS(pch) (!SAFE_DEREF(pch))

AUTO_STRUCT;
typedef struct MemberMissionMapTransfer
{
	EntityRef	iEntRef;
	bool		bOptIn;
	bool		bUpdatedOnce;
} MemberMissionMapTransfer;

// Team mission map transfer data
AUTO_STRUCT;
typedef struct TeamMissionMapTransfer
{
	const char*					pcMap;						AST(POOL_STRING)
	const char*					pcSpawn;					AST(POOL_STRING)
	const char*					pchMapVars;					AST(POOL_STRING)
	GlobalType 					eOwnerType;
	ContainerID					uOwner;
	DoorTransitionSequenceDef*	pTransOverride;				NO_AST

	int							iPartitionIdx;
	U32							iMapTransferResponseTimer;
	ZoneMapType					eSrcZoneMapType;
	ZoneMapType					eDstZoneMapType;
	WorldRegionType				eSrcRegionType;
	WorldRegionType				eDstRegionType;
	S32							iSourceAllowedPetsPerPlayer;
	S32							iAllowedPetsPerPlayer;
	MemberMissionMapTransfer**	eaMapTransferTeammates;
	AwayTeamMembers*			pAwayTeamMembers;
	Team*						pTeam;	AST(UNOWNED)
	U32							uiSinglePlayerID;
	//Vec3						v3GatherPartyCenter;
	bool						bCancelTeamTransfer;
	bool						bAllMembersReady;
	const char*					pcSrcMap;					AST(POOL_STRING)
} TeamMissionMapTransfer;

static TeamMissionMapTransfer**	s_eaTeamMapTransfers;

typedef enum GSLTeamCBResult
{
	GSLTeamCBResult_Success,
	GSLTeamCBResult_Error_Generic,
	GSLTeamCBResult_Error_TargetLevelBad, 
	GSLTeamCBResult_Error_Blocked,
	GSLTeamCBResult_Error_VirtualShardMismatch,
	GSLTeamCBResult_Error_OpposingFaction,
} GSLTeamCBResult;


typedef struct GSLTeamCBData
{
	EntityRef iRef;
	ContainerID iSubjectID;
	char pcErrorName[256];
} GSLTeamCBData;

static GSLTeamCBData *gslTeam_MakeCBData(EntityRef iRef, const char *pcErrorName)
{
	GSLTeamCBData *pData = malloc(sizeof(GSLTeamCBData));
	pData->iRef = iRef;
	if (pcErrorName) {
		strcpy_s(pData->pcErrorName, 256, pcErrorName);
	}
	return pData;
}

void gslTeam_ServerDown(void *pData, void *pData2)
{
	Entity *pEnt = pData ? entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, (ContainerID)((intptr_t)(pData))) : NULL;
	
	if (pEnt)
	{
		char *pcErrorMsg = NULL;
		entFormatGameMessageKey(pEnt, &pcErrorMsg, "TeamServer_Offline", STRFMT_END);
		ClientCmd_NotifySend(pEnt, kNotifyType_TeamError, pcErrorMsg, NULL, NULL);
		estrDestroy(&pcErrorMsg);
	}
}

#define TEAM_SERVERDOWNHANDLER(pEnt) gslTeam_ServerDown, (void*)((intptr_t)(pEnt ? pEnt->myContainerID: 0)), NULL

///////////////////////////////////////////////////////////////////////////////////////////
// User output callbacks
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_STRUCT;
typedef struct GSLTeamMessageCBData
{
	U32 iEntID;
	ContainerID iObjectID;
	ContainerID iSubjectID;
	char pcErrorName[64];
	char pcMessageKey[64];
	const char *pcSubjectName;
	const char *pcObjectName;
	bool bError;
	S32 eMode;
	S32 eLootMode;
	const char* eLootQuality;	AST(POOL_STRING)
	int iDifficulty;
	char *pchStatusMessage;		AST(ESTRING)
} GSLTeamMessageCBData;

static void gslTeam_SendResult(U32 iEntID, const char *pcObjectName, const char *pcSubjectName, const char *pcErrorName, const char *pcMessageKey, bool bError, S32 eMode, S32 eLootMode, const char* eLootQuality, int iDifficulty, const char *pchStatusMessage)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iEntID);
	if (pEnt) {
		PlayerDifficulty *pDifficulty = pd_GetDifficulty(iDifficulty);
		char *estrBuffer = NULL;
		estrStackCreate(&estrBuffer);
		langFormatMessageKey(entGetLanguage(pEnt), &estrBuffer, pcMessageKey,
			STRFMT_STRING("ErrorType", langTranslateMessageKey(entGetLanguage(pEnt), pcErrorName)),
			STRFMT_STRING("Subject", pcSubjectName),
			STRFMT_STRING("Object", pcObjectName),
			STRFMT_STRING("TeamMode", entTranslateMessage(pEnt, StaticDefineGetMessage(TeamModeEnum, eMode))),
			STRFMT_STRING("Difficulty", pDifficulty ? entTranslateMessageRef(pEnt, pDifficulty->hName) : ""),
			STRFMT_STRING("LootMode", entTranslateMessage(pEnt, StaticDefineGetMessage(LootModeEnum, eLootMode))),
			STRFMT_STRING("LootThreshold", entTranslateMessage(pEnt, StaticDefineGetMessage(ItemQualityEnum, StaticDefineIntGetInt(ItemQualityEnum, eLootQuality)))),
			STRFMT_STRING("StatusMessage", pchStatusMessage),
			STRFMT_END);
		ClientCmd_NotifySend(pEnt, bError ? kNotifyType_TeamError : kNotifyType_TeamFeedback, estrBuffer, NULL, NULL);
		estrDestroy(&estrBuffer);
	}
}

void gslTeam_ResultMessage_GetObjectName_CB(TransactionReturnVal *pReturn, GSLTeamMessageCBData *pData);

static void gslTeam_ResultMessage_GetSubjectName_CB(TransactionReturnVal *pReturn, GSLTeamMessageCBData *pData)
{
	char *estrName = NULL;
	enumTransactionOutcome eOutcome = RemoteCommandCheck_dbNameFromID(pReturn, &estrName);
	if (pData->pcObjectName) {
		if (eOutcome == TRANSACTION_OUTCOME_SUCCESS) {
			gslTeam_SendResult(pData->iEntID, pData->pcObjectName, estrName, pData->pcErrorName, pData->pcMessageKey, pData->bError, pData->eMode, pData->eLootMode, pData->eLootQuality, pData->iDifficulty, pData->pchStatusMessage);
		} else {
			gslTeam_SendResult(pData->iEntID, pData->pcObjectName, "???", pData->pcErrorName, pData->pcMessageKey, pData->bError, pData->eMode, pData->eLootMode, pData->eLootQuality, pData->iDifficulty, pData->pchStatusMessage);
		}
		StructDestroy(parse_GSLTeamMessageCBData, pData);
	} else {
		TransactionReturnVal *pNewReturn;
		if (eOutcome == TRANSACTION_OUTCOME_SUCCESS) {
			pData->pcSubjectName = StructAllocString(estrName);
		} else {
			pData->pcSubjectName = StructAllocString("???");
		}
		pNewReturn = objCreateManagedReturnVal(gslTeam_ResultMessage_GetObjectName_CB, pData);
		RemoteCommand_dbNameFromID(pNewReturn, GLOBALTYPE_OBJECTDB, 0, GLOBALTYPE_ENTITYPLAYER, pData->iObjectID);
	}

	estrDestroy(&estrName);
}

void gslTeam_ResultMessage_GetObjectName_CB(TransactionReturnVal *pReturn, GSLTeamMessageCBData *pData)
{
	char *estrName = NULL;
	enumTransactionOutcome eOutcome = RemoteCommandCheck_dbNameFromID(pReturn, &estrName);
	if (pData->pcSubjectName) {
		if (eOutcome == TRANSACTION_OUTCOME_SUCCESS) {
			gslTeam_SendResult(pData->iEntID, estrName, pData->pcSubjectName, pData->pcErrorName, pData->pcMessageKey, pData->bError, pData->eMode, pData->eLootMode, pData->eLootQuality, pData->iDifficulty, pData->pchStatusMessage);
		} else {
			gslTeam_SendResult(pData->iEntID, "???", pData->pcSubjectName, pData->pcErrorName, pData->pcMessageKey, pData->bError, pData->eMode, pData->eLootMode, pData->eLootQuality, pData->iDifficulty, pData->pchStatusMessage);
		}
		StructDestroy(parse_GSLTeamMessageCBData, pData);
	} else {
		TransactionReturnVal *pNewReturn;
		if (eOutcome == TRANSACTION_OUTCOME_SUCCESS) {
			pData->pcObjectName = StructAllocString(estrName);
		} else {
			pData->pcObjectName = StructAllocString("???");
		}
		pNewReturn = objCreateManagedReturnVal(gslTeam_ResultMessage_GetSubjectName_CB, pData);
		RemoteCommand_dbNameFromID(pNewReturn, GLOBALTYPE_OBJECTDB, 0, GLOBALTYPE_ENTITYPLAYER, pData->iSubjectID);
	}
	estrDestroy(&estrName);
}

AUTO_COMMAND_REMOTE;
void gslTeam_ResultMessage(U32 iEntID, U32 iObjectID, U32 iSubjectID, const char *pcErrorName, const char *pcMessageKey, bool bError, S32 eMode, S32 eLootMode, const char* eLootQuality, int iDifficulty, const char *pchStatusMessage)
{
	Entity *pObject = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iObjectID);
	Entity *pSubject = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iSubjectID);
	if ((pSubject || !iSubjectID) && (pObject || !iObjectID)) {
		gslTeam_SendResult(iEntID, iObjectID ? entGetPersistedName(pObject) : "???", iSubjectID ? entGetPersistedName(pSubject) : "???", pcErrorName, pcMessageKey, bError, eMode, eLootMode, eLootQuality, iDifficulty, pchStatusMessage);
	} else {
		GSLTeamMessageCBData *pData;
		TransactionReturnVal *pReturn;
		
		pData = StructCreate(parse_GSLTeamMessageCBData);
		pData->iEntID = iEntID;
		pData->iObjectID = iObjectID;
		pData->iSubjectID = iSubjectID;
		pData->bError = bError;
		pData->eMode = eMode;
		pData->eLootMode = eLootMode;
		pData->eLootQuality = allocAddString(eLootQuality);
		pData->iDifficulty = iDifficulty;
		estrCopy2(&pData->pchStatusMessage, pchStatusMessage);
		if (pcErrorName) {
			strcpy(pData->pcErrorName, pcErrorName);
		}
		if (pcMessageKey) {
			strcpy(pData->pcMessageKey, pcMessageKey);
		}
		if (pObject) {
			pData->pcObjectName = StructAllocString(entGetPersistedName(pObject));
		} else if (!iObjectID) {
			pData->pcObjectName = StructAllocString("???");
		}
		if (pSubject) {
			pData->pcSubjectName = StructAllocString(entGetPersistedName(pSubject));
		} else if (!iSubjectID) {
			pData->pcSubjectName = StructAllocString("???");
		}
		if (!pData->pcObjectName) {
			pReturn = objCreateManagedReturnVal(gslTeam_ResultMessage_GetObjectName_CB, pData);
			RemoteCommand_dbNameFromID(pReturn, GLOBALTYPE_OBJECTDB, 0, GLOBALTYPE_ENTITYPLAYER, iObjectID);
		} else {
			pReturn = objCreateManagedReturnVal(gslTeam_ResultMessage_GetSubjectName_CB, pData);
			RemoteCommand_dbNameFromID(pReturn, GLOBALTYPE_OBJECTDB, 0, GLOBALTYPE_ENTITYPLAYER, iSubjectID);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// Resolving player handles
///////////////////////////////////////////////////////////////////////////////////////////

static void gslTeam_ResolveHandleFailure_CB(Entity *pEnt, ContainerID uiPlayerID, U32 uiAccountID, U32 uiLoginServerID, GSLTeamCBData *pData)
{
	if (pEnt)
	{
		gslTeam_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, pData->pcErrorName, "TeamServer_Error_PlayerNotFound", true, 0, 0, 0, 0, NULL);
	}
	SAFE_FREE(pData);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Change map ownership
///////////////////////////////////////////////////////////////////////////////////////////

//iRefPlayerID tells us what entity we're "talking about", which lets us get the partition to change
//the ownership of
AUTO_COMMAND_REMOTE;
void gslTeam_ClaimMap(ContainerID iRefPlayerID, U32 eOldOwnerType, U32 iOldOwnerID, U32 eNewOwnerType, U32 iNewOwnerID)
{
	MapDescription *pMapDesc = &gGSLState.gameServerDescription.baseMapDescription;
		// Don't change the map ownership to team if this is a mission map that does not require teaming for multiple players
		//  to join.  This is currently used for starship bridges.
	bool teamOwnershipOK = !zmapInfoGetTeamNotRequired(pMapDesc->pZoneMapInfo);

	Entity *pEnt;
	int iPartitionIdx;
	GlobalType eActualOldOwnerType;
	ContainerID iActualOldOwnerID;

	if (pMapDesc->eMapType != ZMTYPE_MISSION && pMapDesc->eMapType != ZMTYPE_QUEUED_PVE)
	{
		// We allow queued pve to have ownership, but only if we were not created by the queue server. We can determinine this based on the queuePartitionInfo
		//    on the partition (see below). This is a similar check as for map booting in mechanics_LogoutTimersProcess in gslMechanics.c
		return;
	}

	if (!teamOwnershipOK)
	{
		return;
	}

	pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iRefPlayerID);
	if (!pEnt)
	{
		ErrorOrAlert("UNKNOWN_PLAYER_FOR_CLAIMMAP", "gslTeam_ClaimMap trying to change the ownership for player %u from %s[%u] to %s[%u], but player %u is unknown",
			iRefPlayerID, GlobalTypeToName(eOldOwnerType), iOldOwnerID, GlobalTypeToName(eNewOwnerType), iNewOwnerID, iRefPlayerID);
		return;
	}

	iPartitionIdx = entGetPartitionIdx(pEnt);

	// Check for QueuePartition Info for Queued_Pve maps.
	if (pMapDesc->eMapType == ZMTYPE_QUEUED_PVE)
	{
		QueuePartitionInfo *pInfo = gslQueuePartitionInfoFromIdx(iPartitionIdx);
		if (pInfo!=NULL && !pInfo->bAllowNonGroupedEntities)
		{
			// We allow queued pve to have ownership, but only if we were not created by the queue server. We can determinine this based on the queuePartitionInfo
			//    on the partition. This is a similar check as for map booting in mechanics_LogoutTimersProcess in gslMechanics.c
			return;
		}
	}

	eActualOldOwnerType = partition_OwnerTypeFromIdx(iPartitionIdx);
	iActualOldOwnerID = partition_OwnerIDFromIdx(iPartitionIdx);

	if ((GlobalType)eOldOwnerType == eActualOldOwnerType && iOldOwnerID == iActualOldOwnerID)
	{
		partition_SetOwnerTypeAndIDFromIdx(iPartitionIdx, eNewOwnerType, iNewOwnerID);
	}

	
}

// A console command for checking map ownership
AUTO_COMMAND ACMD_NAME(CheckMapOwnership);
void gslTeam_cmd_CheckMapOwnership(Entity *pEnt)
{
	static char pcBuffer[256];
	int iPartitionIdx = entGetPartitionIdx(pEnt);
	sprintf(pcBuffer, "Map owned by %s[%d]", StaticDefineIntRevLookup(GlobalTypeEnum, partition_OwnerTypeFromIdx(iPartitionIdx)), partition_OwnerIDFromIdx(iPartitionIdx));
	ServerChat_SendChatMessage(pEnt, kChatLogEntryType_System, pcBuffer, NULL);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Invite to team
///////////////////////////////////////////////////////////////////////////////////////////

static void gslTeam_rcmd_CanInviteToTeam_CB_Internal(GSLTeamCBData *pData, enumTransactionOutcome eOutcome, U32 iCanInviteStatus)
{
	static char pcErrorName[] = "TeamServer_ErrorType_Invite";
	Vec3 vPos;
	ContainerID iInviteeID = pData->iSubjectID;
	
	Entity *pEnt = entFromEntityRefAnyPartition(pData->iRef);

	SAFE_FREE(pData);

	if (!pEnt) {
		return;
	}
	if (eOutcome != TRANSACTION_OUTCOME_SUCCESS) {
		gslTeam_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, iInviteeID, pcErrorName, "TeamServer_Error_Failed", true, 0, 0, 0, 0, NULL);
		return;
	}

	if (iCanInviteStatus != GSLTeamCBResult_Success)
	{
		const char* pcErrorTag="";
		switch (iCanInviteStatus)
		{
			case GSLTeamCBResult_Error_Generic: pcErrorTag = "TeamServer_Error_Failed"; break;
			case GSLTeamCBResult_Error_TargetLevelBad: pcErrorTag = "TeamServer_Error_TargetLevelTooLow"; break;
			case GSLTeamCBResult_Error_Blocked: pcErrorTag = "TeamServer_Error_NotAcceptingInvites"; break;
			case GSLTeamCBResult_Error_VirtualShardMismatch: pcErrorTag = "TeamServer_Error_CantInvite"; break;
			case GSLTeamCBResult_Error_OpposingFaction: pcErrorTag = "TeamServer_Error_Hostile"; break;
			default: ;
		}
		gslTeam_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, iInviteeID, pcErrorName, pcErrorTag, true, 0, 0, 0, 0, NULL);
		
		return;
	}

	// Check this Ent's level. (Invitee is done on other server in remote command) 
	if (pEnt->pChar->iLevelCombat < gConf.iMinimumTeamLevel)
	{
		gslTeam_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, iInviteeID, pcErrorName, "TeamServer_Error_SelfLevelTooLow", true, 0, 0, 0, 0, NULL);
		return;
	}

	// See if we're on a local team. Can't invite to a local team
	if(team_IsLocal(pEnt)) {
		gslTeam_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, iInviteeID, pcErrorName, "TeamServer_Error_CannotInviteToLocalTeam", true, 0, 0, 0, 0, NULL);
		return;
	}

	// Other error checking done in aslTeam_Invite, etc.
	
	entGetPos(pEnt, vPos);
	if (gbEnableGamePlayDataLogging) {
		entLog(LOG_TEAM, pEnt, "TeamInvite", "Location <%d;%d;%d> Name %s", (int)vPos[0], (int)vPos[1], (int)vPos[2], pEnt->debugName);
	}
	RemoteCommand_aslTeam_Invite(GLOBALTYPE_TEAMSERVER, 0, team_GetTeamID(pEnt), pEnt->myContainerID, iInviteeID, REF_STRING_FROM_HANDLE(pEnt->hAllegiance), REF_STRING_FROM_HANDLE(pEnt->hSubAllegiance), TEAM_SERVERDOWNHANDLER(pEnt));
}

void gslTeam_rcmd_CanInviteToTeam_CB(TransactionReturnVal *pReturn, GSLTeamCBData *pData)
{
	U32 iCanInvite;
	enumTransactionOutcome eOutcome = RemoteCommandCheck_gslTeam_rcmd_CanInviteToTeam(pReturn, &iCanInvite);

	gslTeam_rcmd_CanInviteToTeam_CB_Internal(pData, eOutcome, iCanInvite);
}

static void gslTeam_cmd_Invite_CB(Entity *pEnt, ContainerID iSubjectID, U32 uiAccountID, U32 uiLoginServerID, GSLTeamCBData *pData)
{
	static char pcErrorName[] = "TeamServer_ErrorType_Invite";

	if (!pEnt || !pEnt->pPlayer) {
		SAFE_FREE(pData);
		return;
	}
	
	pData->iSubjectID = iSubjectID;
	VerifyServerTypeExistsInShard(GLOBALTYPE_TEAMSERVER);
    devassertmsg(uiLoginServerID == 0, "team invite attempt from loginserver");
    {
		const char* pchFactionHandle = REF_STRING_FROM_HANDLE(pEnt->hFactionOverride);
		if (pchFactionHandle==NULL) pchFactionHandle = REF_STRING_FROM_HANDLE(pEnt->hPowerFactionOverride);
		if (pchFactionHandle==NULL) pchFactionHandle = REF_STRING_FROM_HANDLE(pEnt->hFaction);
		
		RemoteCommand_gslTeam_rcmd_CanInviteToTeam(objCreateManagedReturnVal(gslTeam_rcmd_CanInviteToTeam_CB, pData), GLOBALTYPE_ENTITYPLAYER, iSubjectID,
												   iSubjectID, pEnt->myContainerID,
												   entGetVirtualShardID(pEnt), pEnt->pPlayer->accountID,
												   pchFactionHandle);
	}
}

// Invite another player to your team.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_Invite,Invite);
void gslTeam_cmd_Invite(SA_PARAM_NN_VALID Entity *pEnt, ACMD_SENTENCE pcOtherPlayer)
{
	GSLTeamCBData *pData;
	if (gamePermission_Enabled() && !GamePermission_EntHasToken(pEnt, GAME_PERMISSION_CAN_INVITE_INTO_TEAM)) {
		gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", "TeamServer_ErrorType_Invite", "TeamServer_Error_NoTrialInvites", true, 0, 0, 0, 0, NULL);
		return;
	}
	pData = gslTeam_MakeCBData(entGetRef(pEnt), "TeamServer_ErrorType_Invite");
	gslPlayerResolveHandle(pEnt, pcOtherPlayer, gslTeam_cmd_Invite_CB, gslTeam_ResolveHandleFailure_CB, pData);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_InviteByID) ACMD_PRIVATE;
void gslTeam_cmd_InviteByID(SA_PARAM_NN_VALID Entity *pEnt, U32 iInviteID)
{
	gslTeam_cmd_Invite_CB(pEnt, iInviteID, 0, 0, NULL);
}

static void gslTeam_AcceptInvite(SA_PARAM_NN_VALID Entity *pEnt, bool bAutoSidekick)
{
	static char pcErrorName[] = "TeamServer_ErrorType_AcceptInvite";
	
	if (!team_IsInvitee(pEnt)) {
		gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_SelfNotInvited", true, 0, 0, 0, 0, NULL);
		return;
	}
	
	VerifyServerTypeExistsInShard(GLOBALTYPE_TEAMSERVER);
	RemoteCommand_aslTeam_AcceptInvite(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, entGetContainerID(pEnt), bAutoSidekick, TEAM_SERVERDOWNHANDLER(pEnt));
}

///////////////////////////////////////////////////////////////////////////////////////////
// Accept invite to team
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_AcceptInvite);
void gslTeam_cmd_AcceptInvite(SA_PARAM_NN_VALID Entity *pEnt)
{
	gslTeam_AcceptInvite(pEnt, false);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Accept invite to team - auto-sidekick 
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_AcceptInviteSidekick) ACMD_HIDE;
void gslTeam_cmd_AcceptInviteSidekick(Entity *pEnt)
{
	gslTeam_AcceptInvite(pEnt, true);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Decline invite to team
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_DeclineInvite);
void gslTeam_cmd_DeclineInvite(SA_PARAM_NN_VALID Entity *pEnt)
{
	static char pcErrorName[] = "TeamServer_ErrorType_DeclineInvite";
	
	if (!pEnt->pTeam || !pEnt->pTeam->iTeamID || pEnt->pTeam->eState != TeamState_Invitee) {
		gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_SelfNotInvited", true, 0, 0, 0, 0, NULL);
		return;
	}
	
	VerifyServerTypeExistsInShard(GLOBALTYPE_TEAMSERVER);
	RemoteCommand_aslTeam_DeclineInvite(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, entGetContainerID(pEnt), TEAM_SERVERDOWNHANDLER(pEnt));
}

///////////////////////////////////////////////////////////////////////////////////////////
// Request to join team
///////////////////////////////////////////////////////////////////////////////////////////

void gslTeam_rcmd_CanRequestToJoinTeam_CB(TransactionReturnVal *pReturn, void *pFromGameServer)
{
	TeamRequestCheckResult *pResult = NULL;
	enumTransactionOutcome eOutcome;

	devassertmsg(1 == (intptr_t)pFromGameServer, "Request to join team from login server");
	eOutcome = RemoteCommandCheck_gslTeam_rcmd_CanRequestToJoinTeam(pReturn, &pResult);

	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pResult->uiRequesterID);

		if (pEnt)
		{
			const char * pcErrorName = "GuildServer_ErrorType_Request";
			const char *pchErrorMessageKey = NULL;

			switch(pResult->eStatus)
			{
			case TeamRequestCheckStatus_Success:
				{
					VerifyServerTypeExistsInShard(GLOBALTYPE_TEAMSERVER);
					RemoteCommand_aslTeam_Request(GLOBALTYPE_TEAMSERVER, 0, pResult->uiTeamID, pEnt->myContainerID, TEAM_SERVERDOWNHANDLER(pEnt));
				}
				break;
			case TeamRequestCheckStatus_NotOnMap:
				pchErrorMessageKey = "TeamServer_Error_NotOnMap";
				break;
			case TeamRequestCheckStatus_NotOnTeam:
				pchErrorMessageKey = "TeamServer_Error_NotOnTeam";
				break;
			case TeamRequestCheckStatus_PlayerHostile:
				pchErrorMessageKey = "TeamServer_Error_PlayerHostile";
				break;
			case TeamRequestCheckStatus_TeamIsLocal:
				pchErrorMessageKey = "TeamServer_Error_TeamIsLocal";
				break;
			}

			if (pchErrorMessageKey)
			{
				gslTeam_ResultMessage(pEnt->myContainerID, pResult->uiRequesterID, pResult->uiRequestedID, pcErrorName, pchErrorMessageKey, true, 0, 0, 0, 0, NULL);
			}
		}
	}

	StructDestroySafe(parse_TeamRequestCheckResult, &pResult);
}

static void gslTeam_cmd_Request_CB(Entity *pEnt, ContainerID iSubjectID, U32 uiAccountID, U32 uiLoginServerID, GSLTeamCBData *pData)
{
	const char * pcErrorName = "GuildServer_ErrorType_Request";
	
	SAFE_FREE(pData);

	if (!pEnt) {
		return;
	}

    devassertmsg(uiLoginServerID == 0, "Request to join team from login server.");
	{
		const char* pchFactionHandle = REF_STRING_FROM_HANDLE(pEnt->hFactionOverride);
		if (pchFactionHandle==NULL) pchFactionHandle = REF_STRING_FROM_HANDLE(pEnt->hPowerFactionOverride);
		if (pchFactionHandle==NULL) pchFactionHandle = REF_STRING_FROM_HANDLE(pEnt->hFaction);
		
		RemoteCommand_gslTeam_rcmd_CanRequestToJoinTeam(objCreateManagedReturnVal(gslTeam_rcmd_CanRequestToJoinTeam_CB, (void *)(intptr_t)1),
														GLOBALTYPE_ENTITYPLAYER, iSubjectID, iSubjectID,
														pEnt->myContainerID, pchFactionHandle);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_Request,Request);
void gslTeam_cmd_Request(SA_PARAM_NN_VALID Entity *pEnt, ACMD_SENTENCE pcOtherPlayer)
{
	GSLTeamCBData *pData = gslTeam_MakeCBData(entGetRef(pEnt), "TeamServer_ErrorType_Request");
	gslPlayerResolveHandle(pEnt, pcOtherPlayer, gslTeam_cmd_Request_CB, gslTeam_ResolveHandleFailure_CB, pData);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_RequestByID) ACMD_PRIVATE;
void gslTeam_cmd_RequestByID(SA_PARAM_NN_VALID Entity *pEnt, U32 iRequestID)
{
	gslTeam_cmd_Request_CB(pEnt, iRequestID, 0, 0, NULL);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_RequestByMap) ACMD_PRIVATE;
void gslTeam_cmd_RequestByMap(SA_PARAM_NN_VALID Entity *pEnt, const char* pchMapName, const char* pchMapVars)
{
	static const char* pcErrorName = "TeamServer_ErrorType_Open_Instance";
	MapSummary* pMapSummary;
	const char* pchFindName = pchMapName && pchMapName[0] ? allocAddString(pchMapName) : NULL;
	const char* pchFindVars = pchMapVars && pchMapVars[0] ? allocAddString(pchMapVars) : NULL;
	S32 iDifficultySearchRange;

	switch (pEnt->pPlayer->eLFGDifficultyMode)
	{
		xcase LFGDifficultyMode_Player:
			iDifficultySearchRange = 0;
		xdefault:
			iDifficultySearchRange = -1;
	}

	if ( pchFindName == NULL )
		return;

	if(		pchFindName == gGSLState.gameServerDescription.baseMapDescription.mapDescription
		&&	pchFindVars == partition_MapVariablesFromIdx(entGetPartitionIdx(pEnt)) )
	{
		gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_Failed", true, 0, 0, 0, 0, NULL);
		return;
	}
	if ( team_GetTeam(pEnt) )
	{
		gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_SelfAlreadyOnTeam", true, 0, 0, 0, 0, NULL);
		return;
	}
	if ( !(pMapSummary = mechanics_FindMapSummaryFromMapInfo( pchFindName, pchFindVars )) )
	{
		gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_Failed", true, 0, 0, 0, 0, NULL);
		return;
	}
	if ( pMapSummary->iNumEnabledOpenInstancing == 0 )
	{
		gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_Failed", true, 0, 0, 0, 0, NULL);
		return;
	}

	if (pEnt && pEnt->pTeam)
	{
		pEnt->pTeam->iNumMatchTries++;
	}

	VerifyServerTypeExistsInShard(GLOBALTYPE_TEAMSERVER);
	RemoteCommand_aslMapManagerJoinOpenTeamByMap(GLOBALTYPE_MAPMANAGER, 0, 
			gGSLState.gameServerDescription.baseMapDescription.containerID, 
			pchFindName, pchFindVars, entGetContainerID(pEnt), REF_STRING_FROM_HANDLE(pEnt->hAllegiance),
			REF_STRING_FROM_HANDLE(pEnt->hSubAllegiance), pEnt->pPlayer->iDifficulty, iDifficultySearchRange);
}

static void gslTeam_OpenInstancingMapMove(Entity* pEnt, const char* pchEOIMapRequest, const char* pchEOIMapVars, ContainerID uMapContainerID, U32 uPartitionID)
{
	RegionRules *pCurrRules = getRegionRulesFromEnt( pEnt );
	RegionRules *pNextRules = MapTransferGetRegionRulesFromMapName( pchEOIMapRequest );
	gslEntityPlayTransitionSequenceThenMapMoveEx(pEnt, 
												 pchEOIMapRequest, 
												 ZMTYPE_UNSPECIFIED, 
												 NULL, 
												 0, 
												 uMapContainerID,
												 uPartitionID,
												 0, 
												 0, 
												 pchEOIMapVars, 
												 pCurrRules, 
												 pNextRules, 
												 NULL, 
												 0); //TODO(MK): this needs transition override
}

AUTO_COMMAND_REMOTE;
void gslTeam_OpenInstancingJoinTeamAtMap(U32 iEOIRequestEntID, const char* pchEOIMapRequest, const char* pchEOIMapVars, U32 uMapContainerID, U32 uPartitionID)
{
	if (pchEOIMapRequest && pchEOIMapRequest[0])
	{
		Entity* pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iEOIRequestEntID);
		if (pEnt && pEnt->pTeam)
		{
			pEnt->pTeam->iNumMatchTries = 0;
			gslTeam_OpenInstancingMapMove(pEnt, pchEOIMapRequest, pchEOIMapVars, uMapContainerID, uPartitionID);
		}
	}
}

#define MAX_OPENINSTANCE_TRIES 2

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
void gslTeam_JoinOpenTeamByMap_Error( ContainerID uiEntID, const char* pchEOIMapRequest, const char* pchEOIMapVars )
{
	Entity* pEnt = entFromContainerIDAnyPartition( GLOBALTYPE_ENTITYPLAYER, uiEntID );
	if ( pEnt && pEnt->pPlayer && pEnt->pTeam)
	{
		if (pEnt->pTeam->iNumMatchTries >= MAX_OPENINSTANCE_TRIES)
		{
			gslTeam_SendResult(uiEntID, entGetPersistedName(pEnt), "", "TeamServer_ErrorType_Open_Instance", "TeamServer_Error_Failed", true, 0, 0, 0, 0, NULL);
			gslTeam_OpenInstancingMapMove(pEnt, pchEOIMapRequest, pchEOIMapVars, 0, 0);
			pEnt->pTeam->iNumMatchTries = 0;
		}		
		else
		{
			gslTeam_cmd_RequestByMap(pEnt, pchEOIMapRequest, pchEOIMapVars);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// Accept request to join team
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_AcceptRequest);
void gslTeam_cmd_AcceptRequest(SA_PARAM_NN_VALID Entity *pEnt, U32 iEntID)
{
	static char pcErrorName[] = "TeamServer_ErrorType_AcceptRequest";
	
	if (!team_IsMember(pEnt)) {
		gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_NotOnTeam", true, 0, 0, 0, 0, NULL);
		return;
	}
	
	VerifyServerTypeExistsInShard(GLOBALTYPE_TEAMSERVER);
	RemoteCommand_aslTeam_AcceptRequest(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, pEnt->myContainerID, iEntID, TEAM_SERVERDOWNHANDLER(pEnt));
}

///////////////////////////////////////////////////////////////////////////////////////////
// Decline request to join team
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_DeclineRequest);
void gslTeam_cmd_DeclineRequest(SA_PARAM_NN_VALID Entity *pEnt, U32 iEntID)
{
	static char pcErrorName[] = "TeamServer_ErrorType_DeclineRequest";
	
	if (!team_IsMember(pEnt)) {
		gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_NotOnTeam", true, 0, 0, 0, 0, NULL);
		return;
	}
	
	VerifyServerTypeExistsInShard(GLOBALTYPE_TEAMSERVER);
	RemoteCommand_aslTeam_DeclineRequest(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, pEnt->myContainerID, iEntID, TEAM_SERVERDOWNHANDLER(pEnt));
}

///////////////////////////////////////////////////////////////////////////////////////////
// Cancel request to join team
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_CancelRequest);
void gslTeam_cmd_CancelRequest(SA_PARAM_NN_VALID Entity *pEnt)
{
	VerifyServerTypeExistsInShard(GLOBALTYPE_TEAMSERVER);
	
	if (!team_IsRequester(pEnt)) {
		gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", "CancelRequest", "TeamServer_Error_NotRequesting", true, 0, 0, 0, 0, NULL);
		return;
	}
	
	RemoteCommand_aslTeam_CancelRequest(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, pEnt->myContainerID, TEAM_SERVERDOWNHANDLER(pEnt));
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set team spokesman
///////////////////////////////////////////////////////////////////////////////////////////

static void gslTeam_cmd_SetSpokesman_CB(Entity *pEnt, ContainerID iSubjectID, U32 uiAccountID, U32 uiLoginServerID, GSLTeamCBData *pData)
{
	static char pcErrorName[] = "GuildServer_ErrorType_SetSpokesman";

	SAFE_FREE(pData);

	if (!pEnt) {
		return;
	}
	if (!team_IsMember(pEnt)) {
		gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_NotOnTeam", true, 0, 0, 0, 0, NULL);
		return;
	}

	VerifyServerTypeExistsInShard(GLOBALTYPE_TEAMSERVER);
	RemoteCommand_aslTeam_SetSpokesman(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, pEnt->myContainerID, iSubjectID, TEAM_SERVERDOWNHANDLER(pEnt));
}

// Set team spokesman
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_SetSpokesman);
void gslTeam_cmd_SetSpokesman(SA_PARAM_NN_VALID Entity *pEnt, ACMD_SENTENCE pcOtherPlayer)
{
	GSLTeamCBData *pData = gslTeam_MakeCBData(entGetRef(pEnt), "TeamServer_ErrorType_SetSpokesman");
	gslPlayerResolveHandle(pEnt, pcOtherPlayer, gslTeam_cmd_SetSpokesman_CB, gslTeam_ResolveHandleFailure_CB, pData);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Promote team leader
///////////////////////////////////////////////////////////////////////////////////////////

static void gslTeam_cmd_Promote_CB(Entity *pEnt, ContainerID iSubjectID, U32 uiAccountID, U32 uiLoginServerID, GSLTeamCBData *pData)
{
	static char pcErrorName[] = "GuildServer_ErrorType_Promote";
	
	SAFE_FREE(pData);

	if (!pEnt) {
		return;
	}
	if (!team_IsMember(pEnt)) {
		gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_NotOnTeam", true, 0, 0, 0, 0, NULL);
		return;
	}
	
	VerifyServerTypeExistsInShard(GLOBALTYPE_TEAMSERVER);
	RemoteCommand_aslTeam_Promote(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, pEnt->myContainerID, iSubjectID, TEAM_SERVERDOWNHANDLER(pEnt));
}

// Promote team leader
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_Promote,Promote);
void gslTeam_cmd_Promote(SA_PARAM_NN_VALID Entity *pEnt, ACMD_SENTENCE pcOtherPlayer)
{
	GSLTeamCBData *pData = gslTeam_MakeCBData(entGetRef(pEnt), "TeamServer_ErrorType_Promote");
	gslPlayerResolveHandle(pEnt, pcOtherPlayer, gslTeam_cmd_Promote_CB, gslTeam_ResolveHandleFailure_CB, pData);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_PromoteByID) ACMD_PRIVATE;
void gslTeam_cmd_PromoteByID(SA_PARAM_NN_VALID Entity *pEnt, U32 iPromoteID)
{
	gslTeam_cmd_Promote_CB(pEnt, iPromoteID, 0, 0, NULL);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set team champion
///////////////////////////////////////////////////////////////////////////////////////////

static void gslTeam_cmd_SetChampion_CB(Entity *pEnt, ContainerID iSubjectID, U32 uiAccountID, U32 uiLoginServerID, GSLTeamCBData *pData)
{
	static char pcErrorName[] = "TeamServer_ErrorType_SetChampion";
	
	SAFE_FREE(pData);

	if (!pEnt) {
		return;
	}
	if (!team_IsMember(pEnt)) {
		gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_NotOnTeam", true, 0, 0, 0, 0, NULL);
		return;
	}
	
	VerifyServerTypeExistsInShard(GLOBALTYPE_TEAMSERVER);
	RemoteCommand_aslTeam_SetChampion(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, pEnt->myContainerID, iSubjectID, TEAM_SERVERDOWNHANDLER(pEnt));
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_SetChampion,Champion) ACMD_PRODUCTS(StarTrek, FightClub);
void gslTeam_cmd_SetChampion(SA_PARAM_NN_VALID Entity *pEnt, ACMD_SENTENCE pcOtherPlayer)
{
	GSLTeamCBData *pData = gslTeam_MakeCBData(entGetRef(pEnt), "TeamServer_ErrorType_SetChampion");
	gslPlayerResolveHandle(pEnt, pcOtherPlayer, gslTeam_cmd_SetChampion_CB, gslTeam_ResolveHandleFailure_CB, pData);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_SetChampionByID) ACMD_PRIVATE;
void gslTeam_cmd_SetChampionByID(SA_PARAM_NN_VALID Entity *pEnt, U32 iChampionID)
{
	gslTeam_cmd_SetChampion_CB(pEnt, iChampionID, 0, 0, NULL);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set whether a player is sidekicking
///////////////////////////////////////////////////////////////////////////////////////////

static bool gslTeam_CanSidekick(Entity *pEnt)
{
	bool bAllowed = true;

	if(!pEnt || !pEnt->pChar || 
		(pEnt->pChar->pLevelCombatControl &&
		 pEnt->pChar->pLevelCombatControl->iLevelForce) ||
		!gslQueue_CanSidekick() ||
		!mapState_CanSidekick(entGetPartitionIdx(pEnt)))
		bAllowed = false;

	return(bAllowed);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_Sidekicking);
void gslTeam_cmd_SetSidekicking(Entity *pEnt, bool bSidekicking)
{
	static char pcErrorName[] = "TeamServer_ErrorType_SetSidekicking";
	
	if (!team_IsMember(pEnt)) {
		if (pEnt) {
			gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_NotOnTeam", true, 0, 0, 0, 0, NULL);
		}
		return;
	}

	if( !bSidekicking || gslTeam_CanSidekick(pEnt) )
	{	
		VerifyServerTypeExistsInShard(GLOBALTYPE_TEAMSERVER);
		RemoteCommand_aslTeam_SetSidekicking(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, pEnt->myContainerID, bSidekicking, TEAM_SERVERDOWNHANDLER(pEnt));
	}
	else
	{
		if (pEnt) {
			gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_CannotSidekick", true, 0, 0, 0, 0, NULL);
		}
	}
}


///////////////////////////////////////////////////////////////////////////////////////////
// Release Owned Team
///////////////////////////////////////////////////////////////////////////////////////////

// This serves as a utility/test function for TeamRelease. See full description in TeamTransactions.c
//   on aslTeam_ReleaseTeam

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_ReleaseOwned);
void gslTeam_cmd_EntReleaseOwned(SA_PARAM_NN_VALID Entity *pEnt)
{
	static char pcErrorName[] = "TeamServer_ErrorType_ReleaseOwned";

	Team* pTeam = team_GetTeam(pEnt);
	
	if (pTeam==NULL)
	{
		gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_NotOnTeam", true, 0, 0, 0, 0, NULL);
		return;
	}

	// Only can release if it's owned
	if (pTeam->iGameServerOwnerID != 0)
	{
		// Release the team ID
		RemoteCommand_aslTeam_ReleaseOwned(GLOBALTYPE_TEAMSERVER, 0, pTeam->iContainerID);

		// Inside the callback to this, we send each enitity on the team the command to abandon the map.
		// We need to make sure the release transaction happens first so that we stop accepting OwnedTeamUpdates
	}
	// Don't worry if the team is not local. It is already considered 'released'.
}



///////////////////////////////////////////////////////////////////////////////////////////
// Leave a team
///////////////////////////////////////////////////////////////////////////////////////////

// This is an internal version of leave which doesn't send any feedback to the user
void gslTeam_LeaveSansFeedback(Entity* pEnt)
{
	if(team_IsMember(pEnt)) {
		RemoteCommand_aslTeam_Leave(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, pEnt->myContainerID, pEnt->myContainerID, false, NULL, NULL, NULL);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_Leave);
void gslTeam_cmd_Leave(SA_PARAM_NN_VALID Entity *pEnt)
{
	static char pcErrorName[] = "TeamServer_ErrorType_Leave";
	
	if (!team_IsMember(pEnt)) {
		gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_NotOnTeam", true, 0, 0, 0, 0, NULL);
		return;
	}
	
//	if(team_IsLocal(pEnt) && zmapInfoGetMapType(NULL) != ZMTYPE_STATIC)
	if(team_IsLocal(pEnt))		// We can be on a static map in a local team now. This may cause problems where one gets stuck on a static map in a local team,
								//  but the various pieces of cleanup team code should boot the player automatically
	{
		// Try abandoning queue/map
		bool bDoLeaveMap = true;
		if (!gslQueue_HandleAbandonMap(pEnt, pEnt->myContainerID, bDoLeaveMap))
		{
			gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_TeamIsLocal", true, 0, 0, 0, 0, NULL);
		}
		return;
	}
	VerifyServerTypeExistsInShard(GLOBALTYPE_TEAMSERVER);
	RemoteCommand_aslTeam_Leave(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, pEnt->myContainerID, pEnt->myContainerID, true, TEAM_SERVERDOWNHANDLER(pEnt));
}

///////////////////////////////////////////////////////////////////////////////////////////
// Kick a player off your team
///////////////////////////////////////////////////////////////////////////////////////////

static void gslTeam_cmd_Kick_CB(Entity *pEnt, ContainerID iSubjectID, U32 uiAccountID, U32 uiLoginServerID, GSLTeamCBData *pData)
{
	static char pcErrorName[] = "TeamServer_ErrorType_Kick";
	Team* pTeam;
	
	SAFE_FREE(pData);

	if (!pEnt) {
		return;
	}

	pTeam = team_GetTeam(pEnt);

	
	if (pTeam==NULL || (team_FindMemberID(pTeam, iSubjectID)==NULL && team_FindDisconnectedStubMemberID(pTeam, iSubjectID)==NULL) )
	{
		gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_NotOnTeam", true, 0, 0, 0, 0, NULL);
		return;
	}

	if (pTeam->pLeader->iEntID != pEnt->myContainerID)
	{
		gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_NotTeamLeader", true, 0, 0, 0, 0, NULL);
		return;
	}

	if (team_FindMemberID(pTeam, iSubjectID) || team_FindDisconnectedStubMemberID(pTeam, iSubjectID))
	{
		// The subject is actually on the team. Do local check

		if (team_IsLocal(pEnt))
		{
			// The Team is a local team. We should try abandoning, and if we fail, it's because we're not allowed to leave.
			bool bDoLeaveMap = true;
			if (!gslQueue_HandleAbandonMap(pEnt, iSubjectID, bDoLeaveMap))
			{
				gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_TeamIsLocal", true, 0, 0, 0, 0, NULL);
			}
			return;
		}
	}

	// Actually kick them. If they are a member they will be removed. If they are in Disconnecteds they will be removed from that too.
	VerifyServerTypeExistsInShard(GLOBALTYPE_TEAMSERVER);
	RemoteCommand_aslTeam_Leave(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, pEnt->myContainerID, iSubjectID, true, TEAM_SERVERDOWNHANDLER(pEnt));
}

// Kick a player off your team
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_Kick);
void gslTeam_cmd_Kick(SA_PARAM_NN_VALID Entity *pEnt, ACMD_SENTENCE pcOtherPlayer)
{
	GSLTeamCBData *pData = gslTeam_MakeCBData(entGetRef(pEnt), "TeamServer_ErrorType_Kick");
	gslPlayerResolveHandle(pEnt, pcOtherPlayer, gslTeam_cmd_Kick_CB, gslTeam_ResolveHandleFailure_CB, pData);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_KickByID) ACMD_PRIVATE;
void gslTeam_cmd_KickByID(SA_PARAM_NN_VALID Entity *pEnt, U32 iKickID)
{
	gslTeam_cmd_Kick_CB(pEnt, iKickID, 0, 0, NULL);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set the team difficulty
///////////////////////////////////////////////////////////////////////////////////////////
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_ChangeDifficulty) ACMD_HIDE;
void gslTeam_cmd_ChangeDifficulty(Entity *pEnt, int iDifficulty)
{
	static char pcErrorName[] = "TeamServer_ErrorType_ChangeDifficulty";

	// make sure entity is part of a team
	if (!team_IsMember(pEnt)) {
		if (pEnt) {
			gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_NotOnTeam", true, 0, 0, 0, 0, NULL);
		}
		return;
	}

	if(team_IsLocal(pEnt)) {
		gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_TeamIsLocal", true, 0, 0, 0, 0, NULL);
		return;
	}

	// make sure difficulty target exists
	if (!pd_GetDifficulty(iDifficulty)) {
		gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_InvalidDifficulty", true, 0, 0, 0, 0, NULL);
		return;
	}

	// make sure difficulty is not being changed on an instance map
	if (pd_MapDifficultyApplied()) {
		gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_DifficultyLocked", true, 0, 0, 0, 0, NULL);
		return;
	}

	VerifyServerTypeExistsInShard(GLOBALTYPE_TEAMSERVER);
	RemoteCommand_aslTeam_ChangeDifficulty(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, pEnt->myContainerID, iDifficulty);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Change the team mode
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_Mode);
void gslTeam_cmd_ChangeMode(Entity *pEnt, ACMD_NAMELIST(TeamModeEnum, STATICDEFINE) char *pcMode)
{
	static char pcErrorName[] = "TeamServer_ErrorType_ChangeMode";
	TeamMode eMode;
	
	if (!team_IsMember(pEnt)) {
		if (pEnt) {
			gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_NotOnTeam", true, 0, 0, 0, 0, NULL);
		}
		return;
	}
	
	if(team_IsLocal(pEnt)) {
		gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_TeamIsLocal", true, 0, 0, 0, 0, NULL);
		return;
	}

	eMode = StaticDefineIntGetInt(TeamModeEnum, pcMode);
	if (eMode < 0) {
		gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_InvalidMode", true, 0, 0, 0, 0, NULL);
		return;
	}
	
	VerifyServerTypeExistsInShard(GLOBALTYPE_TEAMSERVER);
	RemoteCommand_aslTeam_ChangeMode(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, pEnt->myContainerID, eMode, TEAM_SERVERDOWNHANDLER(pEnt));
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_DefaultMode);
void gslTeam_cmd_ChangeDefaultMode(Entity *pEnt, ACMD_NAMELIST(TeamModeEnum, STATICDEFINE) char *pcMode)
{
	static char pcErrorName[] = "TeamServer_ErrorType_ChangeDefaultMode";
	TeamMode eMode = StaticDefineIntGetInt(TeamModeEnum, pcMode);
	if (eMode < 0) {
		gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_InvalidMode", true, 0, 0, 0, 0, NULL);
		return;
	}
	
	VerifyServerTypeExistsInShard(GLOBALTYPE_TEAMSERVER);
	RemoteCommand_aslTeam_ChangeDefaultMode(GLOBALTYPE_TEAMSERVER, 0, pEnt->myContainerID, eMode, TEAM_SERVERDOWNHANDLER(pEnt));
}

///////////////////////////////////////////////////////////////////////////////////////////
// Team status message
///////////////////////////////////////////////////////////////////////////////////////////

// Sets the team status message
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_SetStatusMessage);
void gslTeam_cmd_SetStatusMessage(Entity *pEnt, const char *pchStatusMessage)
{
	static char pcErrorName[] = "TeamServer_ErrorType_ChangeStatusMessage";

	if (!team_IsMember(pEnt)) {
		if (pEnt) {
			gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_NotOnTeam", true, 0, 0, 0, 0, NULL);
		}
		return;
	}

	if(team_IsLocal(pEnt)) {
		gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_TeamIsLocal", true, 0, 0, 0, 0, NULL);
		return;
	}

	VerifyServerTypeExistsInShard(GLOBALTYPE_TEAMSERVER);
	RemoteCommand_aslTeam_ChangeStatusMessage(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, pEnt->myContainerID, pchStatusMessage, TEAM_SERVERDOWNHANDLER(pEnt));
}

///////////////////////////////////////////////////////////////////////////////////////////
// Team Loot mode
///////////////////////////////////////////////////////////////////////////////////////////

// Sets the team loot mode
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_SetLootMode);
void gslTeam_cmd_SetLootMode(Entity *pEnt, ACMD_NAMELIST(LootModeEnum, STATICDEFINE) char *pcMode)
{
    static char pcErrorName[] = "TeamServer_ErrorType_ChangeLootMode";
    LootMode mode;
    
	if (!team_IsMember(pEnt)) {
		if (pEnt) {
			gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_NotOnTeam", true, 0, 0, 0, 0, NULL);
		}
		return;
	}

	if(team_IsLocal(pEnt)) {
		gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_TeamIsLocal", true, 0, 0, 0, 0, NULL);
		return;
	}


    mode = StaticDefineIntGetInt(LootModeEnum, pcMode);
    if (mode < 0) {
		gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_InvalidMode", true, 0, 0, 0, 0, NULL);
		return;
	}
    
	VerifyServerTypeExistsInShard(GLOBALTYPE_TEAMSERVER);
	RemoteCommand_aslTeam_ChangeLootMode(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, pEnt->myContainerID, mode, TEAM_SERVERDOWNHANDLER(pEnt));
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_SetDefaultLootMode);
void gslTeam_cmd_SetDefaultLootMode(Entity *pEnt, ACMD_NAMELIST(LootModeEnum, STATICDEFINE) char *pcMode)
{
    static char pcErrorName[] = "TeamServer_ErrorType_ChangeDefaultLootMode";
    LootMode eMode = StaticDefineIntGetInt(LootModeEnum, pcMode);
    if (eMode < 0) {
		gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_InvalidMode", true, 0, 0, 0, 0, NULL);
		return;
	}
    
	VerifyServerTypeExistsInShard(GLOBALTYPE_TEAMSERVER);
	RemoteCommand_aslTeam_ChangeDefaultLootMode(GLOBALTYPE_TEAMSERVER, 0, pEnt->myContainerID, eMode, TEAM_SERVERDOWNHANDLER(pEnt));
}

// Sets the minimum quality for team looting
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_SetLootModeQuality);
void gslTeam_cmd_SetLootModeQuality(Entity *pEnt, ACMD_NAMELIST(ItemQualityEnum, STATICDEFINE) char *pcQuality)
{
	static char pcErrorName[] = "TeamServer_ErrorType_SetQuality";
	ItemQuality quality;
    
   if (!team_IsMember(pEnt)) {
		if (pEnt) {
			gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_NotOnTeam", true, 0, 0, 0, 0, NULL);
		}
		return;
	}


   if(team_IsLocal(pEnt)) {
	   gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_TeamIsLocal", true, 0, 0, 0, 0, NULL);
	   return;
   }

    quality = StaticDefineIntGetInt(ItemQualityEnum, pcQuality);
    if (quality < 0) {
		gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_InvalidQuality", true, 0, 0, 0, 0, NULL);
		return;
	}
    
	VerifyServerTypeExistsInShard(GLOBALTYPE_TEAMSERVER);
	RemoteCommand_aslTeam_ChangeLootModeQuality(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, pEnt->myContainerID, pcQuality, TEAM_SERVERDOWNHANDLER(pEnt));
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_SetDefaultLootModeQuality);
void gslTeam_cmd_SetDefaultLootModeQuality(Entity *pEnt, ACMD_NAMELIST(ItemQualityEnum, STATICDEFINE) char *pcQuality)
{
	static char pcErrorName[] = "TeamServer_ErrorType_SetDefaultQuality";
	ItemQuality eQuality = StaticDefineIntGetInt(ItemQualityEnum, pcQuality);

    if (eQuality < 0) {
		gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", pcErrorName, "TeamServer_Error_InvalidQuality", true, 0, 0, 0, 0, NULL);
		return;
	}

	VerifyServerTypeExistsInShard(GLOBALTYPE_TEAMSERVER);
	RemoteCommand_aslTeam_ChangeDefaultLootModeQuality(GLOBALTYPE_TEAMSERVER, 0, pEnt->myContainerID, pcQuality, TEAM_SERVERDOWNHANDLER(pEnt));
}

///////////////////////////////////////////////////////////////////////////////////////////
// Other functionality
///////////////////////////////////////////////////////////////////////////////////////////

static bool gslTeamIsEntityOpposedToFaction(Entity *pEnt, const char *pchFactionName)
{
	CritterFaction *pSourceFaction;
	CritterFaction *pTargetFaction;
		
	pSourceFaction = pchFactionName ? RefSystem_ReferentFromString("CritterDef", pchFactionName) : NULL;
	pTargetFaction = entGetFaction(pEnt);

	if (pSourceFaction && pTargetFaction && (faction_GetRelation(pSourceFaction,pTargetFaction)==kEntityRelation_Foe))
	{
		return(true);
	}
	return(false);
}


AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
TeamRequestCheckResult * gslTeam_rcmd_CanRequestToJoinTeam(U32 iRequestedID, U32 iRequesterID, const char *pchRequesterFactionName)
{
	TeamRequestCheckResult *pResult = StructCreate(parse_TeamRequestCheckResult);
	Entity *pRequestedEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iRequestedID);

	pResult->uiRequesterID = iRequesterID;
	pResult->uiRequestedID = iRequestedID;

	if (!pRequestedEnt) 
	{
		pResult->eStatus = TeamRequestCheckStatus_NotOnMap;
		return pResult;
	}

	// Set the team ID
	pResult->uiTeamID = team_GetTeamID(pRequestedEnt);

	if (pResult->uiTeamID==0) 
	{
		pResult->eStatus = TeamRequestCheckStatus_NotOnTeam;
		return pResult;
	}

	// Cannot request to join a team that is local
	if (team_IsLocal(pRequestedEnt)) 
	{
		pResult->eStatus = TeamRequestCheckStatus_TeamIsLocal;
		return pResult;
	}

	// Check Faction. 
	if (gslTeamIsEntityOpposedToFaction(pRequestedEnt, pchRequesterFactionName))
	{
		pResult->eStatus = TeamRequestCheckStatus_PlayerHostile;
		return pResult;
	}

	pResult->eStatus = TeamRequestCheckStatus_Success;
	return pResult;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
U32 gslTeam_rcmd_CanInviteToTeam(U32 iInviteeID, U32 iInviterID, ContainerID iInviterVirtualShardID, U32 iInviterAccountID, const char *pchInviterFactionName)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iInviteeID);
	// Entity *pInviterEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iInviterID); // Was only used for faction checking.
	S32 i;
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEnt);

	// This is for checking conditions on the Invitee that have to be done on the Invitee's server. There are eventually additional
	//   checks (which return appropriate errors) on the TeamServer once the request gets there. Don't duplicate if you
	//   don't have to.
	
	if (!pEnt || !pEnt->pPlayer || !pConfig) {
		return GSLTeamCBResult_Error_Generic;
	}

	if (entGetVirtualShardID(pEnt) != iInviterVirtualShardID) 
	{
		return GSLTeamCBResult_Error_VirtualShardMismatch;
	}

	// We used to check team_IsLocal. The eventual tean invite will check this

	// Check this Ent's level. (Inviter is done on other server in Callback)
	if (pEnt->pChar->iLevelCombat < gConf.iMinimumTeamLevel)
	{
		return GSLTeamCBResult_Error_TargetLevelBad;
	}

	// Check Faction. 
	if (gslTeamIsEntityOpposedToFaction(pEnt, pchInviterFactionName))
	{
		return GSLTeamCBResult_Error_OpposingFaction;
	}


	// Refuse invites from everybody, period
	if (pEnt->pPlayer->eLFGMode == TeamMode_Closed) {
		return GSLTeamCBResult_Error_Blocked;
	}

	if(!entIsWhitelistedEx(pEnt, iInviterID, iInviterAccountID, kPlayerWhitelistFlags_Invites)) {
		return GSLTeamCBResult_Error_Blocked;
	}

	// If the player is hidden, ignore team invites
	if (pConfig->status & USERSTATUS_HIDDEN) {
		return GSLTeamCBResult_Error_Blocked;
	}

	// Accept invites only from friends and guildmates
	if (pConfig->status & USERSTATUS_FRIENDSONLY) {
		bool bIsOkay=false;
		Guild *pGuild = guild_GetGuild(pEnt);

		// Check the friend list, if it exists
		if (pEnt->pPlayer && pEnt->pPlayer->pUI && pEnt->pPlayer->pUI->pChatState) {
			for (i = eaSize(&pEnt->pPlayer->pUI->pChatState->eaFriends)-1; i >= 0; i--) {
				ChatPlayerStruct* pFriend = pEnt->pPlayer->pUI->pChatState->eaFriends[i];
				if (!ChatFlagIsFriend(pFriend->flags))
					continue;
				if (pFriend->accountID == iInviterAccountID) {
					bIsOkay=true;
					break;
				}
			}
		}

		// Check the guild list, if they're in a guild
		if (pGuild && guild_IsMember(pEnt)) {
			for (i = eaSize(&pGuild->eaMembers)-1; i >= 0; i--) {
				if (pGuild->eaMembers[i]->iEntID == iInviterID) {
					bIsOkay=true;
					break;
				}
			}
		}
		if (!bIsOkay)
		{
			return GSLTeamCBResult_Error_Blocked;
		}
	}

	// Accept invites from everybody not on the ignore list
	
	// If there is no ignore list, accept the invite
	if (!(!pEnt->pPlayer->pUI || !pEnt->pPlayer->pUI->pChatState || !pEnt->pPlayer->pUI->pChatState->eaIgnores))
	{
		// Check the ignore list
		for (i = eaSize(&pEnt->pPlayer->pUI->pChatState->eaIgnores)-1; i >= 0; i--) {
			if (pEnt->pPlayer->pUI->pChatState->eaIgnores[i]->accountID == iInviterAccountID) {
				return GSLTeamCBResult_Error_Blocked;
			}
		}
	}

	return(GSLTeamCBResult_Success);
}

static const char *gslTeam_GetClassName(Entity *pEnt)
{
	if (pEnt->pChar)
	{
		CharacterClass* pClass = GET_REF(pEnt->pChar->hClass);
		if (pClass && pClass->eType == StaticDefineIntGetInt(CharClassTypesEnum, "Space"))
		{
			int i;
			int iClass = StaticDefineIntGetInt(CharClassTypesEnum, "Ground");
			Entity* pGroundEnt = NULL;
			for ( i = eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppets)-1; i >= 0; --i )
			{
				pGroundEnt = GET_REF(pEnt->pSaved->pPuppetMaster->ppPuppets[i]->hEntityRef);
				if ( (!pGroundEnt) || (!pGroundEnt->pChar) ) continue;
				pClass = GET_REF(pGroundEnt->pChar->hClass);
				if ( !pClass ) continue;
				if ( pClass->eType != iClass ) continue;
				if ( pEnt->pSaved->pPuppetMaster->ppPuppets[i]->eState != PUPPETSTATE_ACTIVE ) continue;
				break;
			}
			if (pGroundEnt && pGroundEnt->pChar && i >= 0)
			{
				return allocAddString(REF_STRING_FROM_HANDLE(pGroundEnt->pChar->hClass));
			}
		}
		return allocAddString(REF_STRING_FROM_HANDLE(pEnt->pChar->hClass));
	}

	return NULL;
}

// This function is called once per frame for each entity that has a team structure, to update
// and validate all their team info.
void gslTeam_EntityUpdate(Entity *pEnt)
{
	char pcNameBuff[512]; 
	Team *pTeam = NULL;
	TeamMember *pMember = NULL;
	U32 iCurTime;
	int i;
	ContainerID iLeaderID = 0;
	
	iCurTime = timeSecondsSince2000();

	// If the user logged out improperly, try to rejoin their previous team
	if (pEnt->pTeam->eState == TeamState_LoggedOut && pEnt->pTeam->iRejoinID != 0) {
		PERFINFO_AUTO_START("LoggedOutTeamState", 1);

		if (!pEnt->pTeam->bTriedRejoining && ((iCurTime - pEnt->pTeam->iLogoutTime < TEAM_REJOIN_TIMEOUT) || gConf.bAlwaysTryToRejoinTeamAfterLogout))
		{
			// Attempt to rejoin the team
			bool bRejoin=true;
			RemoteCommand_aslTeam_JoinWithoutInvite(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iRejoinID, pEnt->myContainerID, NULL, NULL, 0, 0, bRejoin);
			pEnt->pTeam->bTriedRejoining = true;
		}
		
		PERFINFO_AUTO_STOP();
		return;
	}

	PERFINFO_AUTO_START("CheckTeamHandle", 1);

	if (!team_IsMember(pEnt))
	{
		pEnt->pTeam->iLastTeamIDForInitialMeeting = 0;
		entity_SetDirtyBit(pEnt, parse_PlayerTeam, pEnt->pTeam, true);
	}

	//Make sure the TeamID and the hTeam are correctly matched
	if (IS_HANDLE_ACTIVE(pEnt->pTeam->hTeam) && StringToContainerID(REF_STRING_FROM_HANDLE(pEnt->pTeam->hTeam)) != pEnt->pTeam->iTeamID)
	{
		//We must have changed the team for the player in one frame; Remove the handle so it gets updated to the correct team handle
		REMOVE_HANDLE(pEnt->pTeam->hTeam);
	}

	PERFINFO_AUTO_STOP(); // CheckTeamHandle

	PERFINFO_AUTO_START("CheckRecruitingPowers", 1);

	//Update the team powers (currently used only for recruiting powers)
	if(pEnt->pChar && pEnt->pTeam->bUpdateTeamPowers && !gConf.bDisableRecruitUpdates)
	{
		//If I'm on a valid team, or not on a team, run the update.  Otherwise wait for another tick
		if( (pEnt->pTeam->iTeamID && pTeam) || (!pEnt->pTeam->iTeamID) )
		{
			character_RecruitingUpdatePowers(pEnt->pChar);
			pEnt->pTeam->bUpdateTeamPowers = false;
		}
	}

	PERFINFO_AUTO_STOP(); // CheckRecruitingPowers
	
	// Check if the player is on a team at all, and if not, make sure their team data is cleared
	if (!pEnt->pTeam->iTeamID) {
		// The player isn't actually on a team
		
		PERFINFO_AUTO_START("PlayerNotOnTeam", 1);

		// If their team handle is still active, clear it
		REMOVE_HANDLE(pEnt->pTeam->hTeam);
		entity_SetDirtyBit(pEnt, parse_PlayerTeam, pEnt->pTeam, true);
		
		// If they're still in team chat, remove them
		if (pEnt->pPlayer && pEnt->pTeam->iInChat) {
			team_MakeTeamChannelNameFromID(SAFESTR(pcNameBuff), pEnt->pTeam->iInChat);
			RemoteCommand_ChatServerLeaveChannel_Special(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, pEnt->myContainerID, pcNameBuff);
		}

		if (pEnt->pTeam->bTeamMissionMapTransfer)
		{
			gslTeam_AwayTeamMemberRemove(pEnt,true);
		}
		
		// If they're sidekicked, remove the level link
		if (pEnt->pChar && pEnt->pChar->pLevelCombatControl && pEnt->pChar->pLevelCombatControl->bLinkRequiresTeam && gslTeam_CanSidekick(pEnt)) {
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
			character_LevelCombatNatural(entGetPartitionIdx(pEnt), pEnt->pChar, pExtract);
		}
		
		// Clear their team version
		pEnt->pTeam->iVersion = 0;
		
		if(pEnt->pTeam->bMapLocal) {
			pEnt->pTeam->bMapLocal = false;
			entity_SetDirtyBit(pEnt, parse_PlayerTeam, pEnt->pTeam, true);
		}

		PERFINFO_AUTO_STOP();
		return;
	}
	
	// If the team handle isn't yet active, we need to make it active before we can do anything else
	if (!IS_HANDLE_ACTIVE(pEnt->pTeam->hTeam)) {
		PERFINFO_AUTO_START("FixTeamHandle", 1);

		sprintf(pcNameBuff, "%d", pEnt->pTeam->iTeamID);
		SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_TEAM), pcNameBuff, pEnt->pTeam->hTeam);
		pEnt->pTeam->iTimeSinceHandleInit = timeSecondsSince2000();
		entity_SetDirtyBit(pEnt, parse_PlayerTeam, pEnt->pTeam, true);

		PERFINFO_AUTO_STOP();
	}

	// Okay, we either had a handle before now, or we just got a new one.
	// Do the GET_REF here. If we do it before the previous chunk of code we may not be looking at the right handle
	//  (it could have been changed in the TeamID check higher above). 
	
	pTeam = GET_REF(pEnt->pTeam->hTeam);
	
	if (!pTeam) {
		PERFINFO_AUTO_START("TeamNotPresent", 1);

		// The team data hasn't been downloaded yet so no further updating or validation is possible
		if ((iCurTime - pEnt->pTeam->iTimeSinceHandleInit) > TEAM_TIME_OUT_INTERVAL) {
			// Loading the team data has timed out, meaning the team probably doesn't exist,
			// so we need to validate that the team actually exists, and clear the player's
			// team data if it doesn't. These remote commands handle both.
			switch (pEnt->pTeam->eState) {
				case TeamState_Member:
					RemoteCommand_aslTeam_ValidateMember(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, pEnt->myContainerID);
					break;
				case TeamState_Invitee:
					RemoteCommand_aslTeam_ValidateInvite(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, pEnt->myContainerID);
					break;
				case TeamState_Requester:
					RemoteCommand_aslTeam_ValidateRequest(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, pEnt->myContainerID);
					break;
			}
			pEnt->pTeam->iTimeSinceHandleInit = iCurTime;
		}

		PERFINFO_AUTO_STOP();
		return;
	} else {
		//Set the local flag
		U32 bLocal = (pTeam->iGameServerOwnerID != 0);
		if(pEnt->pTeam->bMapLocal != bLocal) {
			pEnt->pTeam->bMapLocal = bLocal;
			entity_SetDirtyBit(pEnt, parse_PlayerTeam, pEnt->pTeam, true);
		}
	}

	if(pTeam->pLeader)
	{
		iLeaderID = pTeam->pLeader->iEntID;
	}

	// Regular updates to do on team members even when the version hasn't been updated
	if (team_IsMember(pEnt)) {
		const char *pcMapName;
		const char *pcMapVars;
		U32 iInstanceNum;
		U32 iMapContainerID;
		const char *pcStatus;
		Entity *pLeader;
		S32 iExpLevel;

		pMember = team_FindMember(pTeam, pEnt);
		pLeader = entFromContainerID(entGetPartitionIdx(pEnt), GLOBALTYPE_ENTITYPLAYER, iLeaderID);
		
		if (pLeader && (critter_IsFactionKOS(entGetPartitionIdx(pEnt), pLeader, pEnt) || critter_IsFactionKOS(entGetPartitionIdx(pEnt), pEnt, pLeader))) {
			RemoteCommand_aslTeam_Leave(GLOBALTYPE_TEAMSERVER, 0, pTeam->iContainerID, pEnt->myContainerID, pEnt->myContainerID, false, NULL, NULL, NULL);
			gslTeam_SendResult(pEnt->myContainerID, entGetPersistedName(pEnt), "", "", "TeamServer_Special_OpposedFaction", true, 0, 0, 0, 0, NULL);
			return;
		}
		
		PERFINFO_AUTO_START("CheckTeamChat", 1);

		// Make sure they're in team chat
		if (pEnt->pPlayer && !pEnt->pTeam->iInChat) {
			team_MakeTeamChannelNameFromID(SAFESTR(pcNameBuff), pTeam->iContainerID);
			RemoteCommand_ChatServerJoinOrCreateChannel_Special(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, 
				pEnt->myContainerID, pcNameBuff, CHANNEL_SPECIAL_TEAM);
		}

		PERFINFO_AUTO_STOP(); // CheckTeamChat
		
		PERFINFO_AUTO_START("CheckTeamDataCorrect", 1);

		pcMapName = allocAddString(zmapInfoGetPublicName(NULL));
		pcMapVars = allocAddString(partition_MapVariablesFromIdx(entGetPartitionIdx(pEnt)));
		iInstanceNum = 
			(gGSLState.gameServerDescription.baseMapDescription.eMapType == ZMTYPE_STATIC || 
			gGSLState.gameServerDescription.baseMapDescription.eMapType == ZMTYPE_SHARED ||
			gGSLState.gameServerDescription.baseMapDescription.eMapType == ZMTYPE_MISSION) ? partition_PublicInstanceIndexFromIdx(entGetPartitionIdx(pEnt)) : 0;
		iMapContainerID = gGSLState.gameServerDescription.baseMapDescription.containerID;
		pcStatus = SAFE_MEMBER4(pEnt, pPlayer, pUI, pChatConfig, pchStatusMessage);
		iExpLevel = entity_GetSavedExpLevel(pEnt);		

		// Make sure the location info is up to date
		if (pMember && iCurTime - pEnt->pTeam->iTimeSinceLastUpdate > TEAM_TIME_OUT_INTERVAL &&
			(iInstanceNum != pMember->iMapInstanceNumber ||
			iExpLevel != pMember->iExpLevel ||
			strcmp(NULL_TO_EMPTY(pMember->pcMapName), NULL_TO_EMPTY(pcMapName)) ||
			strcmp(NULL_TO_EMPTY(pMember->pcStatus), NULL_TO_EMPTY(pcStatus)))) 
		{
			S32 iOfficerRank = Officer_GetRank(pEnt);
			const char *pcMapMsgKey = zmapInfoGetDisplayNameMsgKey(NULL);
			RemoteCommand_aslTeam_UpdateInfo(GLOBALTYPE_TEAMSERVER, 0, pTeam->iContainerID, pEnt->myContainerID, pcMapName, pcMapMsgKey, pcMapVars, iInstanceNum, pcStatus, gslTeam_GetClassName(pEnt), iExpLevel, iOfficerRank, partition_IDFromIdx(entGetPartitionIdx(pEnt)), iMapContainerID);
			pEnt->pTeam->iTimeSinceLastUpdate = iCurTime;
		}

		PERFINFO_AUTO_STOP(); // CheckTeamDataCorrect

		PERFINFO_AUTO_START("CheckSidekickLevel", 1);
		
		// Make sure the player's combat level link matches their team sidekick status
		if (pMember && gslTeam_CanSidekick(pEnt)) {
			if(pMember->bSidekicked) {
				LevelCombatControl *pLevelCombatControl = pEnt->pChar->pLevelCombatControl;
				Entity *pChampionEnt = NULL;
				if (pTeam->pChampion) {
					if (!pLevelCombatControl || pLevelCombatControl->cidLinkPlayer != pTeam->pChampion->iEntID) {
						GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
						int iPartitionIdx = entGetPartitionIdx(pEnt);
						pChampionEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pTeam->pChampion->iEntID);
						if (pChampionEnt && pChampionEnt->myContainerID != pEnt->myContainerID) {
							F32 fMaxDistance = 300.0f;
							RegionRules* pRules = getRegionRulesFromEnt(pEnt);
							if (pRules) {
								fMaxDistance = pRules->fSendDistanceMin;
							}
							character_LevelCombatLink(iPartitionIdx, pEnt->pChar, entGetRef(pChampionEnt), true, fMaxDistance, pExtract);
						} else {
							character_LevelCombatNatural(iPartitionIdx, pEnt->pChar, pExtract);
						}
					}
				} else {
					if (!pLevelCombatControl || pLevelCombatControl->cidLinkPlayer != iLeaderID) {
						GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
						int iPartitionIdx = entGetPartitionIdx(pEnt);
						pChampionEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, iLeaderID);
						if (pChampionEnt && pChampionEnt->myContainerID != pEnt->myContainerID) {
							F32 fMaxDistance = 300.0f;
							RegionRules* pRules = getRegionRulesFromEnt(pEnt);
							if (pRules) {
								fMaxDistance = pRules->fSendDistanceMin;
							}
							character_LevelCombatLink(iPartitionIdx, pEnt->pChar, entGetRef(pChampionEnt), true, fMaxDistance, pExtract);
						} else {
							character_LevelCombatNatural(iPartitionIdx, pEnt->pChar, pExtract);
						}
					}
				}
			} else {
				LevelCombatControl *pLevelCombatControl = pEnt->pChar->pLevelCombatControl;
				if (pLevelCombatControl) {
					GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
					character_LevelCombatNatural(entGetPartitionIdx(pEnt), pEnt->pChar, pExtract);
				}
			}
		}
		PERFINFO_AUTO_STOP(); // CheckSidekickLevel
	}
	
	if (pTeam->iVersion == pEnt->pTeam->iVersion) {
		// Nothing has been changed, so no further updating or validation is necessary
		return;
	}
	
	// Check the team state to figure out whether the entity thinks it's a member of the team, invited to
	// join the team, or requesting to join the team, and do the appropriate validation and updating.
	switch (pEnt->pTeam->eState) {
		case TeamState_Member:
			PERFINFO_AUTO_START("MemberUpdate", 1);

			
			// Make sure the entity is really on the team
			// If the team container and the entity are in conflict, the team container wins
			if (!pMember) {
				pMember = team_FindMember(pTeam, pEnt);
				if (!pMember) {
					// The entity isn't actually on the team, so clear their team data
					RemoteCommand_aslTeam_ValidateMember(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, entGetContainerID(pEnt));
					PERFINFO_AUTO_STOP(); // MemberUpdate
					return;
				}
			}

			//If the team has a game server ID and we're not on that server and we're supposed to be on that server... leave the team
			//  (This is determined by gConf or queue setting at Team Creation Time)
			//If we are a owner server situation, make sure we don't do the usual NumPresentMembers check in any case.
			if(pTeam->iGameServerOwnerID)
			{
				if (pTeam->bTeamMembersMustBeOnOwnerGameServer && (objServerID() != pTeam->iGameServerOwnerID))
 				{
					RemoteCommand_aslTeam_Leave(GLOBALTYPE_TEAMSERVER, 0, pTeam->iContainerID, entGetContainerID(pEnt), entGetContainerID(pEnt), false, NULL, NULL, NULL);
					PERFINFO_AUTO_STOP(); // MemberUpdate
					return;
				}
			}
			// else, Check if the entity is on a team all by themselves, and if so, leave the team. But only if we are set to do so.
			//  Make sure we are not just forming a team via request or invite as there will temporarily be only one member at that point
			else if (
						(!gConf.bManageTeamDisconnecteds && team_NumPresentMembers(pTeam) <= 1) &&
						eaSize(&pTeam->eaInvites) < 1 &&
						eaSize(&pTeam->eaRequests) < 1 &&
						timeSecondsSince2000() > pTeam->uBadLogoutTime + TEAM_BAD_LOGOUT_SECONDS
					 ) 
			{
				RemoteCommand_aslTeam_Leave(GLOBALTYPE_TEAMSERVER, 0, pTeam->iContainerID, entGetContainerID(pEnt), entGetContainerID(pEnt), false, NULL, NULL, NULL);
				PERFINFO_AUTO_STOP(); // MemberUpdate
				return;
			}

			// Don't clear the LFG flag, because it's handled on the search end now		

			//check to see if the team's pets are valid
			gslTeam_CheckPetCount( entGetPartitionIdx(pEnt), pTeam, pEnt );
			
			pEnt->pTeam->iVersion = pTeam->iVersion;
			
			PERFINFO_AUTO_STOP(); // MemberUpdate
			break;
		case TeamState_Invitee:
			PERFINFO_AUTO_START("InviteeUpdate", 1);

			// Make sure the entity is really invited to the team
			// If the team container and the entity are in conflict, the team container wins
			for (i = eaSize(&pTeam->eaInvites)-1; i >= 0; i--) {
				if (pTeam->eaInvites[i]->iEntID == pEnt->myContainerID) {
					break;
				}
			}
			if (i < 0) {
				// The entity isn't actually invited to the team, so clear their team data
				RemoteCommand_aslTeam_ValidateInvite(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, entGetContainerID(pEnt));
			}
			
			PERFINFO_AUTO_STOP();
			break;
		case TeamState_Requester:
			PERFINFO_AUTO_START("RequesterUpdate", 1);

			// Make sure the entity is really requesting to join the team
			// If the team container and the entity are in conflict, the team container wins
			for (i = eaSize(&pTeam->eaRequests)-1; i >= 0; i--) {
				if (pTeam->eaRequests[i]->iEntID == pEnt->myContainerID) {
					break;
				}
			}
			if (i < 0) {
				// The entity isn't actually requesting to join the team, so clear their team data
				RemoteCommand_aslTeam_ValidateRequest(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, entGetContainerID(pEnt));
			}
			
			PERFINFO_AUTO_STOP();
			break;
	}
}

void gslTeam_HandlePlayerTeamChange(Entity *pEnt) {
	entity_SetDirtyBit(pEnt, parse_PlayerTeam, pEnt->pTeam, true);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Saved Pet team management
///////////////////////////////////////////////////////////////////////////////////////////

//****AWAY TEAM MAP TRANSFER****
// This code deals with the organization of away teams when moving to mission maps.
// It handles teams of players as well as just a single player (on a team or not),
// so therefore uses the concept of teams loosely and might be better suited in a different file?


TeamMissionMapTransfer* gslTeam_FindAwayTeamTransfer(Entity* pEntity)
{
	S32 i;
	Team* pTeam = team_GetTeam( pEntity );

	if ( pEntity==NULL )
		return NULL;

	for ( i = eaSize( &s_eaTeamMapTransfers )-1; i >= 0; i-- )
	{
		TeamMissionMapTransfer* pData = s_eaTeamMapTransfers[i];

		if (	(pData->uiSinglePlayerID == entGetContainerID( pEntity ))
			||	(pTeam && pData->pTeam && pTeam->iContainerID == pData->pTeam->iContainerID) )
		{
			return pData;
		}
	}
	return NULL;
}

static bool gslTeam_DestroyAwayTeamTransfer( TeamMissionMapTransfer* pTeamMapTransfer )
{
	if ( pTeamMapTransfer )
	{
		TeamCorralInfo *pCorral = (pTeamMapTransfer->pTeam ? gslTeam_FindTeamCorralInfoForTeam(pTeamMapTransfer->pTeam) : NULL);
		S32 i;

		if(pCorral)
		{
			gslTeam_DestroyTeamCorral(pCorral);
		}

		for ( i = eaSize( &s_eaTeamMapTransfers ) - 1; i >= 0; i-- )
		{
			if ( s_eaTeamMapTransfers[i] == pTeamMapTransfer )
			{		
				StructDestroy( parse_TeamMissionMapTransfer, eaRemoveFast(&s_eaTeamMapTransfers, i) );
				return true;
			}
		}

		//somehow we didn't find the pointer in the list, destroy it anyway
		StructDestroy( parse_TeamMissionMapTransfer, pTeamMapTransfer );
		return true;
	}

	return false;
}

bool gslTeam_IsEntReadyForMapTransfer(Entity *pEnt)
{
	TeamMissionMapTransfer *pTransfer = gslTeam_FindAwayTeamTransfer(pEnt);
	bool bIsReady = false;

	if(pTransfer)
	{
		int i;
		for(i = 0; i < eaSize(&pTransfer->eaMapTransferTeammates); ++i)
		{
			MemberMissionMapTransfer *pMember = pTransfer->eaMapTransferTeammates[i];

			if(pMember)
			{
				Entity *pTempEnt = entFromEntityRefAnyPartition(pMember->iEntRef);
				if (pTempEnt == pEnt)
				{
					return pMember->bOptIn;
					break;
				}
			}
		}
	}

	return bIsReady;
}

TeamMapTransferResult gslTeam_IsMapTransferChoiceTakingPlace( Entity* pEntity, const char* pchMap, const char* pchSpawn )
{
	TeamMissionMapTransfer* pTeamMapTransfer = gslTeam_FindAwayTeamTransfer( pEntity );

	if ( pTeamMapTransfer && eaSize( &pTeamMapTransfer->eaMapTransferTeammates ) > 0 )
	{
		if (	stricmp( pTeamMapTransfer->pcMap, pchMap ) == 0 
			&&	stricmp( pTeamMapTransfer->pcSpawn, pchSpawn ) == 0  )
		{
			return TeamMapTransferResult_SameMap;
		}
		return TeamMapTransferResult_DifferentMap;
	}
	return TeamMapTransferResult_None;
}

static void gslTeam_ClearAwayTeamMemberData( SA_PARAM_NN_VALID Entity* pEntity )
{
	if ( pEntity->pTeam )
	{
		pEntity->pTeam->bTeamMissionMapTransfer = false;
	}
}

static void gslTeam_RemoveAwayTeamMemberInternal( TeamMissionMapTransfer* pTeamMapTransfer, S32 i )
{
	Entity* pEntity = entFromEntityRefAnyPartition(pTeamMapTransfer->eaMapTransferTeammates[i]->iEntRef);
	
	if ( pEntity )
	{
		gslTeam_ClearAwayTeamMemberData( pEntity );
	}

	StructDestroy( parse_MemberMissionMapTransfer, eaRemove( &pTeamMapTransfer->eaMapTransferTeammates, i ) );
}

static void gslTeam_RemoveAllAwayTeamMembersInternal( SA_PARAM_NN_VALID TeamMissionMapTransfer* pTeamMapTransfer )
{
	S32 i;
	bool bUseNNOMapTransfer = gConf.bEnableNNOTeamWarp;

	if(bUseNNOMapTransfer)
	{
		Team *pTeam = NULL;//pTeamMapTransfer->pTeam;
		for(i = 0; i < eaSize(&pTeamMapTransfer->eaMapTransferTeammates); ++i)
		{
			MemberMissionMapTransfer *pMember = pTeamMapTransfer->eaMapTransferTeammates[i];
			if(pMember)
			{
				Entity *pEnt = entFromEntityRefAnyPartition(pMember->iEntRef);
				
				if(pEnt)
				{
					pTeam = team_GetTeam(pEnt);
					if(pTeam)
						break;
				}
			}
		}
		if(pTeam && pTeam->eaMembers)
		{
			for(i = 0; i < eaSize(&pTeam->eaMembers); ++i)
			{
				Entity *pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pTeam->eaMembers[i]->iEntID);
				if(pEntity)
				{
					gslTeam_ClearAwayTeamMemberData( pEntity );
					ClientCmd_teamHideMapTransferChoice(pEntity);
				}
				else
				{
					RemoteCommand_gslTeam_HideMapTransferAlertOnOtherServer(GLOBALTYPE_GAMESERVER, pTeam->eaMembers[i]->iMapContainerID, pTeam->eaMembers[i]->iEntID);
				}
			}
		}
		else
		{
			bUseNNOMapTransfer = false;
		}
		
	}

	if(!bUseNNOMapTransfer)
	{
		for ( i = 0; i < eaSize( &pTeamMapTransfer->eaMapTransferTeammates ); i++ )
		{
			Entity* pEntity = entFromEntityRefAnyPartition(pTeamMapTransfer->eaMapTransferTeammates[i]->iEntRef);

			if ( pEntity )
			{
				gslTeam_ClearAwayTeamMemberData( pEntity );
			}
		}
	}

	//no need for the map transfer info anymore, destroy it
	gslTeam_DestroyAwayTeamTransfer( pTeamMapTransfer );
}

bool gslTeam_IsValidAwayTeamMapTransfer(ZoneMapInfo* pCurrZoneMap, 
										ZoneMapInfo* pNextZoneMap, 
										RegionRules* pCurrRules, 
										RegionRules* pNextRules )
{
	S32 iCurrZoneMapType = zmapInfoGetMapType( pCurrZoneMap );
	S32 iNextZoneMapType = zmapInfoGetMapType( pNextZoneMap );
	S32 iCurrRegionType = pCurrRules->eRegionType;
	S32 iNextRegionType = pNextRules->eRegionType;

	if ( iCurrZoneMapType == ZMTYPE_MISSION && iNextZoneMapType == ZMTYPE_MISSION )
		return true;

	if ( (iCurrRegionType == WRT_Space || iCurrRegionType == WRT_SectorSpace) && (iNextRegionType == WRT_Ground))
		return true;

	if (	iCurrZoneMapType == ZMTYPE_STATIC && iNextZoneMapType == ZMTYPE_MISSION 
		&&	(iCurrRegionType == WRT_Ground)
		&&	(iNextRegionType == WRT_Ground) )
	{
		return true;
	}

	if (	iCurrRegionType == WRT_Ground
		&&	iNextRegionType == WRT_Ground
		&&	iNextZoneMapType == ZMTYPE_STATIC
		&&	pNextRules->iAllowedPetsPerPlayer > 0
		&&	pCurrRules->iAllowedPetsPerPlayer != pNextRules->iAllowedPetsPerPlayer )
	{
		return true;
	}

	return false;
}

bool gslTeam_IsValidTeamMapTransferForNNO( ZoneMapInfo* pCurrZoneMap, ZoneMapInfo* pNextZoneMap, 
	RegionRules* pCurrRules, RegionRules* pNextRules )
{
	S32 iCurrZoneMapType = zmapInfoGetMapType( pCurrZoneMap );
	S32 iNextZoneMapType = zmapInfoGetMapType( pNextZoneMap );
	S32 iCurrRegionType = pCurrRules->eRegionType;
	S32 iNextRegionType = pNextRules->eRegionType;

	if(!gConf.bEnableNNOTeamWarp)
		return false;

	if(iCurrZoneMapType == ZMTYPE_STATIC && iNextZoneMapType == ZMTYPE_STATIC)
	{
		return false;
	}

	return true;
}


static bool gslTeam_ShouldShowAwayTeamChooser( TeamMissionMapTransfer* pTeamMapTransfer )
{
	if ( pTeamMapTransfer && gConf.bUseAwayTeams)
	{
		WorldRegionType eSrcRegionType = pTeamMapTransfer->eSrcRegionType;
		WorldRegionType eDstRegionType = pTeamMapTransfer->eDstRegionType;

		if ( eSrcRegionType == WRT_Space || eSrcRegionType == WRT_SectorSpace )
		{
			if ( eDstRegionType == WRT_Ground )
			{	
				return true;
			}
		}
		else if ( eSrcRegionType == WRT_Ground )
		{
			ZoneMapType eSrcZoneMapType = pTeamMapTransfer->eSrcZoneMapType;
			ZoneMapType eDstZoneMapType = pTeamMapTransfer->eDstZoneMapType;
			
			if ( eSrcZoneMapType == ZMTYPE_STATIC && eDstZoneMapType == ZMTYPE_MISSION )
			{
				return true;
			}
			else if ( eDstRegionType == WRT_Ground && eDstZoneMapType == ZMTYPE_STATIC &&
				pTeamMapTransfer->iAllowedPetsPerPlayer > 0	&&
				pTeamMapTransfer->iSourceAllowedPetsPerPlayer != pTeamMapTransfer->iAllowedPetsPerPlayer )
			{
				return true;
			}
		}
	}

	return false;
}

static void gslTeam_MapTransferAwayTeam( SA_PARAM_NN_VALID TeamMissionMapTransfer* pTeamMapTransfer )
{
	int i, iSize = eaSize(&pTeamMapTransfer->eaMapTransferTeammates);

	bool bForceTransfer = false;

	if (gConf.bEnableNNOTeamWarp && (timeSecondsSince2000() >= pTeamMapTransfer->iMapTransferResponseTimer || 1))
	{
		bForceTransfer = true;
	}

	for ( i = 0; i < iSize; i++ )
	{
		Entity* pEntity = entFromEntityRef(	pTeamMapTransfer->iPartitionIdx, pTeamMapTransfer->eaMapTransferTeammates[i]->iEntRef );

		if ( ( bForceTransfer || pTeamMapTransfer->eaMapTransferTeammates[i]->bOptIn ) && pEntity && pEntity->pPlayer )
		{
			//make sure the UI is hidden
			ClientCmd_teamHideMapTransferChoice( pEntity );

			// Transact to save away team choices
			Entity_SaveAwayTeamPets(pEntity,pTeamMapTransfer->pAwayTeamMembers);
			Entity_SaveAwayTeamCritterPets(pEntity,pTeamMapTransfer->pAwayTeamMembers);

			if ( pTeamMapTransfer->pcMap )
			{
				//Do map transfer on this entity
				gslEntityPlayTransitionSequenceThenMapMoveEx(
					 pEntity, 
					 pTeamMapTransfer->pcMap, 
					 pTeamMapTransfer->eDstZoneMapType,
					 pTeamMapTransfer->pcSpawn,
					 0,
					 0,
					 0,
					 pTeamMapTransfer->eOwnerType, 
					 pTeamMapTransfer->uOwner, 
					 pTeamMapTransfer->pchMapVars, 
					 getRegionRulesFromRegionType(pTeamMapTransfer->eSrcRegionType), 
					 getRegionRulesFromRegionType(pTeamMapTransfer->eDstRegionType),
					 pTeamMapTransfer->pTransOverride,
					 0);
			}
		}
	}

	if ( pTeamMapTransfer->pTeam && iSize > 1 )
	{
		RemoteCommand_aslTeam_SaveAwayTeamPreferences( GLOBALTYPE_TEAMSERVER, 0, pTeamMapTransfer->pTeam->iContainerID, pTeamMapTransfer->pAwayTeamMembers );
	}

	//need to call this to restore the select/target state on all away team members
	gslTeam_RemoveAllAwayTeamMembersInternal( pTeamMapTransfer );
}

static bool gslTeam_AwayTeamMemberGrantPet( SA_PARAM_NN_VALID TeamMissionMapTransfer* pTeamMapTransfer,
											Entity* pEntity, S32 iPetIndex )
{
	Entity* pBestPet = NULL;
	Entity* pLastResortPet = NULL;
	S32 i, j, iBestPreferredIndex = -1;
	S32 iPetArraySize = eaSize(&pEntity->pSaved->ppOwnedContainers);
	S32 iAwayTeamSize = eaSize( &pTeamMapTransfer->pAwayTeamMembers->eaMembers );
	S32 iTeamSize =	eaSize( &pTeamMapTransfer->eaMapTransferTeammates );

	for ( i = 0; i < iPetArraySize; i++ )
	{
		S32 iPreferredIndex;
		bool bFoundPetInAwayTeam = false;
		PetRelationship* pPet = pEntity->pSaved->ppOwnedContainers[i];
		Entity* pPetEnt = GET_REF(pPet->hPetRef);

		if ( pPetEnt==NULL || pPetEnt->pChar==NULL || SavedPet_IsPetAPuppet(pEntity,pPet) )
			continue;

		//make sure this pet isn't already on the team
		for ( j = iAwayTeamSize - 1; j >= iTeamSize; j-- )
		{
			if ( pPetEnt->myContainerID == pTeamMapTransfer->pAwayTeamMembers->eaMembers[j]->iEntID )
			{
				bFoundPetInAwayTeam = true;
				break;
			}
		}

		if ( bFoundPetInAwayTeam )
			continue;

		if ( eaSize(&pPetEnt->pChar->ppTraining) > 0 )
			continue;

		iPreferredIndex = ea32Find(&pEntity->pSaved->ppPreferredPetIDs, entGetContainerID(pPetEnt));

		if ( iPreferredIndex >= 0 && (iBestPreferredIndex < 0 || iPreferredIndex < iBestPreferredIndex) )
		{
			pBestPet = pPetEnt;
			iBestPreferredIndex = iPreferredIndex;
			if ( iPreferredIndex == 0 )
			{
				break;
			}
			continue;
		}

		if ( ( pPet->bTeamRequest ) == 0 )
		{
			if ( pLastResortPet==NULL )
				pLastResortPet = pPetEnt;

			continue;
		}

		if ( iBestPreferredIndex < 0 )
		{
			pBestPet = pPetEnt;
		}
	}

	if ( iPetIndex < 0 ) //if the index is < 0, then find the best place for the pet
	{
		for ( i = 0; i < iAwayTeamSize; i++ )
		{
			if ( pTeamMapTransfer->pAwayTeamMembers->eaMembers[i]->iEntID == 0 )
			{	
				iPetIndex = i;
				break;
			}
		}
	}

	if ( iPetIndex < 0 || iPetIndex >= iAwayTeamSize )
		return false;

	if ( pBestPet )
	{
		pTeamMapTransfer->pAwayTeamMembers->eaMembers[iPetIndex]->eEntType = entGetType(pBestPet);
		pTeamMapTransfer->pAwayTeamMembers->eaMembers[iPetIndex]->iEntID = entGetContainerID(pBestPet);
		pTeamMapTransfer->pAwayTeamMembers->eaMembers[iPetIndex]->uiCritterPetID = 0;
	}
	else if ( pLastResortPet )
	{
		pTeamMapTransfer->pAwayTeamMembers->eaMembers[iPetIndex]->eEntType = entGetType(pLastResortPet);
		pTeamMapTransfer->pAwayTeamMembers->eaMembers[iPetIndex]->iEntID = entGetContainerID(pLastResortPet);
		pTeamMapTransfer->pAwayTeamMembers->eaMembers[iPetIndex]->uiCritterPetID = 0;
	}
	else
	{
		if(eaSize(&pEntity->pSaved->ppAllowedCritterPets) > 0)
		{
			PetDef *pPetDef = GET_REF(pEntity->pSaved->ppAllowedCritterPets[0]->hPet);
			pTeamMapTransfer->pAwayTeamMembers->eaMembers[iPetIndex]->eEntType = GLOBALTYPE_ENTITYCRITTER;
			pTeamMapTransfer->pAwayTeamMembers->eaMembers[iPetIndex]->iEntID = entGetContainerID(pEntity);
			pTeamMapTransfer->pAwayTeamMembers->eaMembers[iPetIndex]->uiCritterPetID = pEntity->pSaved->ppAllowedCritterPets[0]->uiPetID;
		}else{
			Errorf("Away Team Map Transfer: Could not add pet for entity ID: %i", pEntity->myContainerID);
			return false;
		}
		
	}

	return true;
}

static bool gslTeam_AwayTeamMemberGrantPetByIndex(	int iPartitionIdx,
													SA_PARAM_NN_VALID TeamMissionMapTransfer* pTeamMapTransfer, 
													S32 iTeammateIndex, S32 iPetIndex )
{
	Entity* pEntity = entFromContainerID(	iPartitionIdx,
											pTeamMapTransfer->pAwayTeamMembers->eaMembers[iTeammateIndex]->eEntType,
											pTeamMapTransfer->pAwayTeamMembers->eaMembers[iTeammateIndex]->iEntID );
	
	if ( pEntity==NULL )
		return false;

	return gslTeam_AwayTeamMemberGrantPet(pTeamMapTransfer, pEntity, iPetIndex);
}

static void gslTeam_CreateAwayTeamMemberList( int iPartitionIdx, SA_PARAM_NN_VALID TeamMissionMapTransfer* pTeamMapTransfer )
{
	S32 iTeamSize = eaSize(&pTeamMapTransfer->eaMapTransferTeammates);
	S32 iTeamMaxSize = iTeamSize * (pTeamMapTransfer->iAllowedPetsPerPlayer + 1); // Assume each player may bring all their pets
	S32 iMinPetsPerPlayer = 0;
	S32 i, j;

	if (!pTeamMapTransfer->pAwayTeamMembers)
		pTeamMapTransfer->pAwayTeamMembers = StructCreate(parse_AwayTeamMembers);
	if (!pTeamMapTransfer->pAwayTeamMembers)
		return;

	MIN1(iTeamMaxSize, TEAM_MAX_SIZE); // First clip theoretical max to actual team size limits
	MAX1(iTeamMaxSize, iTeamSize); // Ensure that there is enough space for all the players
	eaSetSizeStruct(&pTeamMapTransfer->pAwayTeamMembers->eaMembers, parse_AwayTeamMember, iTeamMaxSize);
	pTeamMapTransfer->pAwayTeamMembers->iMaxTeamSize = iTeamMaxSize;

	if (iTeamMaxSize <= 0)
		return;

	// Fill away team data with the current players
	for (i = 0; i < iTeamSize; i++)
	{
		Entity* pEntity = entFromEntityRef(iPartitionIdx, pTeamMapTransfer->eaMapTransferTeammates[i]->iEntRef);
		pTeamMapTransfer->pAwayTeamMembers->eaMembers[i]->eEntType = pEntity ? entGetType(pEntity) : GLOBALTYPE_NONE;
		pTeamMapTransfer->pAwayTeamMembers->eaMembers[i]->iEntID = pEntity ? entGetContainerID(pEntity) : 0;
		pTeamMapTransfer->pAwayTeamMembers->eaMembers[i]->uiCritterPetID = 0;
		pTeamMapTransfer->pAwayTeamMembers->eaMembers[i]->bIsReady = pEntity && pTeamMapTransfer->eaMapTransferTeammates[i]->bOptIn;
	}

	// Determine how many pets each player may place
	if (iTeamSize > 0)
		iMinPetsPerPlayer = MIN((iTeamMaxSize - iTeamSize) / iTeamSize, pTeamMapTransfer->iAllowedPetsPerPlayer);

	// Give each player iMinPetsPerPlayer pets
	for (i = 0; i < iMinPetsPerPlayer; i++)
	{
		for (j = 0; j < iTeamSize; j++)
			gslTeam_AwayTeamMemberGrantPetByIndex(iPartitionIdx, pTeamMapTransfer, j, -1);
	}

	// Have each player roll for the remaining slots
	for (i = iTeamSize * (iMinPetsPerPlayer + 1); i < iTeamMaxSize; i++)
	{
		S32 iBestIndex = -1;
		S32 iBestRoll = -1;

		for (j = 0; j < iTeamSize; j++)
		{
			S32 iRoll = randomIntRange(0, 1000);
			if (iBestRoll < iRoll)
			{
				iBestIndex = j;
				iBestRoll = iRoll;
			}
		}

		if (iBestIndex >= 0)
			gslTeam_AwayTeamMemberGrantPetByIndex(iPartitionIdx, pTeamMapTransfer, iBestIndex, i);
	}
}

// This is a terribly named function, and it only exists because a fix needs to go live asap. 
static void gslTeam_UpdateAwayTeamMemberListEx( int iPartitionIdx, SA_PARAM_NN_VALID TeamMissionMapTransfer* pTeamMapTransfer )
{
	S32 iTeamSize = eaSize(&pTeamMapTransfer->eaMapTransferTeammates);
	S32 iTeamMaxSize = iTeamSize * (pTeamMapTransfer->iAllowedPetsPerPlayer + 1); // Assume each player may bring all their pets
	S32 iMinPetsPerPlayer = 0;
	S32 i;

	if (!pTeamMapTransfer->pAwayTeamMembers)
		pTeamMapTransfer->pAwayTeamMembers = StructCreate(parse_AwayTeamMembers);
	if (!pTeamMapTransfer->pAwayTeamMembers)
		return;

	MIN1(iTeamMaxSize, TEAM_MAX_SIZE); // First clip theoretical max to actual team size limits
	MAX1(iTeamMaxSize, iTeamSize); // Ensure that there is enough space for all the players
	eaSetSizeStruct(&pTeamMapTransfer->pAwayTeamMembers->eaMembers, parse_AwayTeamMember, iTeamMaxSize);
	pTeamMapTransfer->pAwayTeamMembers->iMaxTeamSize = iTeamMaxSize;

	if (iTeamMaxSize <= 0)
		return;

	// Fill away team data with the current players
	for (i = 0; i < iTeamSize; i++)
	{
		Entity* pEntity = entFromEntityRef(iPartitionIdx, pTeamMapTransfer->eaMapTransferTeammates[i]->iEntRef);
		pTeamMapTransfer->pAwayTeamMembers->eaMembers[i]->eEntType = pEntity ? entGetType(pEntity) : GLOBALTYPE_NONE;
		pTeamMapTransfer->pAwayTeamMembers->eaMembers[i]->iEntID = pEntity ? entGetContainerID(pEntity) : 0;
		pTeamMapTransfer->pAwayTeamMembers->eaMembers[i]->uiCritterPetID = 0;
		pTeamMapTransfer->pAwayTeamMembers->eaMembers[i]->bIsReady = pEntity && pTeamMapTransfer->eaMapTransferTeammates[i]->bOptIn;
	}

	// Figure out which pets belong in the new list.
	//for (i = iTeamSize; i < iTeamMaxSize; i++)
	//{
	//	int j = -1;
	//	AwayTeamMember *pExistingMember = pTeamMapTransfer->pAwayTeamMembers->eaMembers[i];
	//	for (j = 0; j < iTeamSize; j++)
	//	{
	//		if (pExistingMember->iEntID == pTeamMapTransfer->pAwayTeamMembers->eaMembers[j]->iEntID)
	//		{
	//			break;
	//		}
	//	}
	//	gslTeam_AwayTeamMemberGrantPetByIndex(iPartitionIdx, pTeamMapTransfer, j, i);
	//}
}

static void gslTeam_CopyAwayTeamMemberListFromPrefs(SA_PARAM_NN_VALID TeamMissionMapTransfer* pTeamMapTransfer,
													AwayTeamPrefs* pPrefs )
{
	S32 i, iTeamSize = eaSize(&pPrefs->eaTeamMembers);

	if (!pTeamMapTransfer->pAwayTeamMembers)
		pTeamMapTransfer->pAwayTeamMembers = StructCreate(parse_AwayTeamMembers);
	if (!pTeamMapTransfer->pAwayTeamMembers)
		return;

	eaSetSizeStruct(&pTeamMapTransfer->pAwayTeamMembers->eaMembers, parse_AwayTeamMember, iTeamSize);
	pTeamMapTransfer->pAwayTeamMembers->iMaxTeamSize = iTeamSize;

	for (i = 0; i < iTeamSize; i++)
	{
		pTeamMapTransfer->pAwayTeamMembers->eaMembers[i]->eEntType = pPrefs->eaTeamMembers[i]->eEntType;
		pTeamMapTransfer->pAwayTeamMembers->eaMembers[i]->iEntID = pPrefs->eaTeamMembers[i]->iEntID;
		pTeamMapTransfer->pAwayTeamMembers->eaMembers[i]->uiCritterPetID = pPrefs->eaTeamMembers[i]->uiCritterPetID;
	}

	//this should only be done when the map transfer data is initially set up, but just in case - update "bIsReady"
	for (i = eaSize(&pTeamMapTransfer->eaMapTransferTeammates) - 1; i >= 0; i--)
	{
		Entity* pEntity = entFromEntityRef(pTeamMapTransfer->iPartitionIdx, pTeamMapTransfer->eaMapTransferTeammates[i]->iEntRef);
		pTeamMapTransfer->pAwayTeamMembers->eaMembers[i]->bIsReady = pEntity && pTeamMapTransfer->eaMapTransferTeammates[i]->bOptIn;
	}
}

static bool gslTeam_CurrentTeamMatchesAwayTeamPreference( SA_PARAM_NN_VALID TeamMissionMapTransfer* pTeamMapTransfer,
														  AwayTeamPrefs* pPrefs )
{
	S32 i, j;
	S32 iTeamSize = (S32)eaSize( &pTeamMapTransfer->eaMapTransferTeammates );
	S32 iTeamMaxSize = iTeamSize * (pTeamMapTransfer->iAllowedPetsPerPlayer + 1);
	S32 iPrefsTeamSize = eaSize( &pPrefs->eaTeamMembers );
	S32 iPrefsPlayerCount = 0;

	if ( iPrefsTeamSize != MIN( iTeamMaxSize, TEAM_MAX_SIZE ) )
		return false;

	for ( i = 0; i < iPrefsTeamSize; i++ )
	{
		if ( pPrefs->eaTeamMembers[i]->eEntType == GLOBALTYPE_ENTITYPLAYER )
			iPrefsPlayerCount++;
	}

	if ( iTeamSize != iPrefsPlayerCount && iPrefsTeamSize )
		return false;

	for ( i = 0; i < iTeamSize; i++ )
	{
		bool bFound = false;

		for ( j = 0; j < iPrefsTeamSize; j++ )
		{
			Entity* pEntity;
			
			if ( pPrefs->eaTeamMembers[j]->eEntType != GLOBALTYPE_ENTITYPLAYER )
				continue;

			pEntity = entFromEntityRef( pTeamMapTransfer->iPartitionIdx, pTeamMapTransfer->eaMapTransferTeammates[i]->iEntRef );

			if ( pEntity && pEntity->myContainerID == pPrefs->eaTeamMembers[j]->iEntID )
			{
				bFound = true;
				break;
			}
		}

		if ( bFound == false )
			return false;
	}

	return true;
}

static void gslTeam_UpdateAwayTeamMemberList( int iPartitionIdx, SA_PARAM_NN_VALID TeamMissionMapTransfer* pTeamMapTransfer, bool bTryToLoadPrefs )
{
	if ( bTryToLoadPrefs )
	{
		if ( pTeamMapTransfer->pTeam )
		{
			//check to see if any of the away team prefs match our current away team
			S32 i, iPrefSize = eaSize( &pTeamMapTransfer->pTeam->eaAwayTeamPrefs );

			for ( i = 0; i < iPrefSize; i++ )
			{
				AwayTeamPrefs* pPrefs = pTeamMapTransfer->pTeam->eaAwayTeamPrefs[i];
				if ( gslTeam_CurrentTeamMatchesAwayTeamPreference( pTeamMapTransfer, pPrefs ) )
				{
					//if one matches an away team pref, then copy that
					gslTeam_CopyAwayTeamMemberListFromPrefs( pTeamMapTransfer, pPrefs );
					return;
				}
			}
		}

		//if none of the teams match or we don't have a team, make a new list
		gslTeam_CreateAwayTeamMemberList( iPartitionIdx, pTeamMapTransfer );
	}
	else
	{
		gslTeam_UpdateAwayTeamMemberListEx( iPartitionIdx, pTeamMapTransfer );
	}
}

static bool gslTeam_IsTeamInteractingWithContact(SA_PARAM_NN_VALID TeamMissionMapTransfer* pTeamMapTransfer)
{		
	S32 iTeamSize = eaSize( &pTeamMapTransfer->eaMapTransferTeammates );
	int i;
	for ( i = 0; i < iTeamSize; i++ )
	{
		Entity* pEntity = entFromEntityRef(	pTeamMapTransfer->iPartitionIdx, pTeamMapTransfer->eaMapTransferTeammates[i]->iEntRef );
		InteractInfo* pInfo = pEntity ? SAFE_MEMBER2(pEntity,pPlayer,pInteractInfo) : NULL;

		if ( pEntity==NULL || pInfo == NULL)
			continue;

		if ( pInfo->pContactDialog )
			return true;
	}
	return false;
}

#define TEAM_MAP_TRANSFER_BUFFER_TIME 2
static int s_iTeamMapTransferWaitTimeShort = 15;
static int s_iTeamMapTransferWaitTime = 30;
static int s_iTeamMapTransferWaitTimeLong = 60;  // Used if a player on the team is interacting with a contact
static F32 s_fPartyCircleRadius = 10;
static F32 s_fPartyCircleRadiusSquared = 100;
static const char *s_pchPartyDepartureCircleFx = NULL;
AUTO_CMD_INT(s_iTeamMapTransferWaitTimeShort, AwayTeam_MapTransferShortWaitTime) ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(9);
AUTO_CMD_INT(s_iTeamMapTransferWaitTime, AwayTeam_MapTransferWaitTime) ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(9);
AUTO_CMD_INT(s_iTeamMapTransferWaitTimeLong, AwayTeam_MapTransferLongWaitTime) ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(9);

AUTO_STRUCT;
typedef struct TeamTransferConfig
{
	S32 iTeamMapTransferWaitTimeShort;
	S32 iTeamMapTransferWaitTime;
	S32 iTeamMapTransferWaitTimeLong;
	const char *pchPartyDepartureCircleFx; AST(POOL_STRING)
	F32 fPartyCircleRadius;
} TeamTransferConfig;

// Config
static TeamTransferConfig s_TeamTransferConfig = { 0 };

AUTO_STARTUP(TeamTransferConfig);
void gslTeam_LoadTeamTransferConfig(void)
{
	StructInit(parse_TeamTransferConfig, &s_TeamTransferConfig);

	ParserLoadFiles(NULL, "defs/config/TeamTransferConfig.def", "TeamTransferConfig.bin", PARSER_OPTIONALFLAG, parse_TeamTransferConfig, &s_TeamTransferConfig);

	if (s_TeamTransferConfig.iTeamMapTransferWaitTime > 0)
	{
		s_iTeamMapTransferWaitTime = s_TeamTransferConfig.iTeamMapTransferWaitTime;
	}

	if (s_TeamTransferConfig.iTeamMapTransferWaitTimeShort > 0)
	{
		s_iTeamMapTransferWaitTimeShort = s_TeamTransferConfig.iTeamMapTransferWaitTimeShort;
	}

	if (s_TeamTransferConfig.iTeamMapTransferWaitTimeLong > 0)
	{
		s_iTeamMapTransferWaitTimeLong = s_TeamTransferConfig.iTeamMapTransferWaitTimeLong;
	}

	if (s_TeamTransferConfig.pchPartyDepartureCircleFx && s_TeamTransferConfig.pchPartyDepartureCircleFx[0])
	{
		s_pchPartyDepartureCircleFx = s_TeamTransferConfig.pchPartyDepartureCircleFx;
	}

	if (s_TeamTransferConfig.fPartyCircleRadius > 0)
	{
		s_fPartyCircleRadius = s_TeamTransferConfig.fPartyCircleRadius;
		s_fPartyCircleRadiusSquared = s_fPartyCircleRadius * s_fPartyCircleRadius;
	}
}

static bool gslTeam_SendMapTransferInfoToClientInternal(SA_PARAM_NN_VALID TeamMissionMapTransfer* pTeamMapTransfer, 
														Entity* pEntity, 
														bool bUpdateAwayTeamList, bool bResetTimer, 
														bool bTeamIsInteracting, bool bIsInitialSend,
														bool bIncludeTeammates)
{
	S32 iTeamSize = eaSize( &pTeamMapTransfer->eaMapTransferTeammates );
	WorldRegionType eSrcZoneType = pTeamMapTransfer->eSrcRegionType;
	WorldRegionType eDstZoneType = pTeamMapTransfer->eDstRegionType;
	bool bContactDialog = false;
	InteractInfo* pInfo = pEntity ? SAFE_MEMBER2(pEntity, pPlayer, pInteractInfo) : NULL;
	int iPartitionIdx = pEntity ? entGetPartitionIdx(pEntity) : PARTITION_UNINITIALIZED;
	Team *pTeam = team_GetTeam(pEntity);

	bContactDialog = (pInfo && pInfo->pContactDialog);

	if (iTeamSize > 1 || (gConf.bEnableNNOTeamWarp && team_NumTotalMembers(pTeam) > 1))		// Now allow single player to transfer if they have disconnecteds with them
	{
		U32 iTimerValue;
		bool bShowChooser = gslTeam_ShouldShowAwayTeamChooser( pTeamMapTransfer );
		S32 iWaitTime = bShowChooser ? s_iTeamMapTransferWaitTime : s_iTeamMapTransferWaitTimeShort;
		S32 iTimeLeft = (int)(pTeamMapTransfer->iMapTransferResponseTimer - timeSecondsSince2000());
		S32 iTransferBuffer = gConf.bEnableNNOTeamWarp ? 0 : TEAM_MAP_TRANSFER_BUFFER_TIME;
		TeamCorralInfo *pCorral = gslTeam_FindTeamCorralInfoForTeam(pTeam);

		if ( (bResetTimer || (!bTeamIsInteracting && iTimeLeft > iWaitTime)) && (!pTeamMapTransfer->bAllMembersReady || !gConf.bEnableNNOTeamWarp) )
		{
			if(bTeamIsInteracting)
				iTimerValue = s_iTeamMapTransferWaitTimeLong;
			else
				iTimerValue = iWaitTime;
			pTeamMapTransfer->iMapTransferResponseTimer = timeSecondsSince2000() + iTimerValue + iTransferBuffer;
			if (pCorral)
			{
				pCorral->uiCountdownTimer = pTeamMapTransfer->iMapTransferResponseTimer;
			}
		}
		else
		{
			iTimerValue = iTimeLeft - iTransferBuffer; //subtract some time due to network delay
		}

		if ( bShowChooser )
		{
			if ( bUpdateAwayTeamList )
				gslTeam_UpdateAwayTeamMemberList( iPartitionIdx, pTeamMapTransfer, bIsInitialSend );

			//show/update away team picker
			ClientCmd_teamShowAwayTeamPicker( pEntity, iTimerValue, pTeamMapTransfer->pAwayTeamMembers, pTeamMapTransfer->bAllMembersReady );
		}
		else
		{
			//just provide transfer choice: opt in or out
			ClientCmd_teamShowMapTransferChoice( pEntity, iTimerValue, pTeamMapTransfer->pAwayTeamMembers, pTeamMapTransfer->bAllMembersReady );
		}
		if (bContactDialog && pInfo) {
			pInfo->bAwaitingMapTransfer = true;
		}
	}
	else if (iTeamSize == 1 || bIncludeTeammates)
	{
		//no timer for 1-player
		pTeamMapTransfer->iMapTransferResponseTimer = 0;

		//1-player map transfer
		if ( gslTeam_ShouldShowAwayTeamChooser( pTeamMapTransfer ) && pTeamMapTransfer->iAllowedPetsPerPlayer != 0 )
		{
			if ( bUpdateAwayTeamList )
				gslTeam_UpdateAwayTeamMemberList( iPartitionIdx, pTeamMapTransfer, bIsInitialSend );

			//show the 1-player away team picker
			ClientCmd_teamShowAwayTeamPicker( pEntity, -1, pTeamMapTransfer->pAwayTeamMembers, pTeamMapTransfer->bAllMembersReady );
		}
		else
		{
			if (bIsInitialSend)
			{
				if (pTeamMapTransfer->eaMapTransferTeammates && pTeamMapTransfer->eaMapTransferTeammates[0])
				{
					pTeamMapTransfer->eaMapTransferTeammates[0]->bOptIn = true;
				}				

				//just do the transfer right away
				gslTeam_MapTransferAwayTeam( pTeamMapTransfer );
				return false;
			}
			else
			{
				// Since this function is called due to a state from the team, we still want to keep the map transfer choice up.
				ClientCmd_teamShowMapTransferChoice( pEntity, -1, NULL, true );
			}
		}
		if (bContactDialog && !bIsInitialSend && pInfo) {
			pInfo->bAwaitingMapTransfer = true;
		}
	}
	else
	{
		//no teammates for some reason, just clean-up/destroy the map transfer structure
		gslTeam_RemoveAllAwayTeamMembersInternal( pTeamMapTransfer );
		return false;
	}
	return true;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void gslTeam_ShowMapTransferAlertOnOtherServer(ContainerID id, TeamMissionMapTransfer* pTeamMapTransfer )
{
	Entity *pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, id);
	
	if(pEntity)
	{
		ClientCmd_teamShowMapTransferChoice(pEntity, -1, pTeamMapTransfer->pAwayTeamMembers, false);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void gslTeam_HideMapTransferAlertOnOtherServer(ContainerID id)
{
	Entity *pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, id);

	if(pEntity)
	{
		ClientCmd_teamHideMapTransferChoice(pEntity);
	}
}

F32 gslTeam_GetTeamCorralCircleRadius()
{
	return s_fPartyCircleRadius;
}

F32 gslTeam_GetTeamCorralCircleRadiusSquared()
{
	return s_fPartyCircleRadiusSquared;
}

S32 gslTeam_GetTeamTransferTimeDefault()
{
	return s_iTeamMapTransferWaitTime;
}

//For NNO check all members of a team for whether they are on the same map, and if not, alert them
static bool gslTeam_checkAllMembersForTransfer( Team * pTeam, SA_PARAM_NN_VALID TeamMissionMapTransfer* pTeamMapTransfer) 
{
	Entity *pEntity = NULL;
	bool bIsTeamOnOneMap = true;
	int i;

	if(pTeam && eaSize(&pTeam->eaMembers) != eaSize(&pTeamMapTransfer->eaMapTransferTeammates) ) //If these are different sizes, there are players on different maps
	{
		for(i = 0; i < eaSize(&pTeam->eaMembers); ++i)
		{
			TeamMember *pMember = pTeam->eaMembers[i];
			if(pMember)
			{
				if (!pMember->pcMapName || !pMember->pcMapName[0])
				{
					bIsTeamOnOneMap = false;
				}
				else if(pMember->pcMapName != pTeamMapTransfer->pcSrcMap)
				{
					if (pMember->iMapContainerID)
					{
						RemoteCommand_gslTeam_ShowMapTransferAlertOnOtherServer(GLOBALTYPE_GAMESERVER, pMember->iMapContainerID, pMember->iEntID, pTeamMapTransfer);
					}
					bIsTeamOnOneMap = false;
				}
				else
				{
					pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pMember->iEntID);
					if(pEntity)
					{
						// Check to see if the entity is already in the transfer
						bool bInTransfer = false;
						int j;
						for ( j = 0; j < eaSize( &pTeamMapTransfer->eaMapTransferTeammates); ++j )
						{
							if(pTeamMapTransfer->eaMapTransferTeammates[j]->iEntRef == pEntity->myRef)
								bInTransfer = true;
						}

						// If the entity isn't in the transfer, add them
						gslTeam_AddAwayTeamMemberToMapTransfer(pEntity, true);
					}
				}
			}
		}
	}

	if (pTeam && team_NumTotalMembers(pTeam) != eaSize(&pTeam->eaMembers))
	{
		bIsTeamOnOneMap = false;
	}

	return bIsTeamOnOneMap;
}

static bool gslTeam_IsNNOTeamTransferReady( SA_PARAM_NN_VALID TeamMissionMapTransfer* pTeamMapTransfer, U32 iCurrentTime ) 
{
	Entity *pEntity = NULL;
	Team *pTeam = NULL;
	bool bLocalTeamReady = true; // Are all the players in the local map ready
	bool bIsTeamOnOneMap = true; // Are all members of the team on the same map
	TeamCorralInfo *pCorral = NULL;
	int i;

	// Find the team
	for ( i = 0; i < eaSize( &pTeamMapTransfer->eaMapTransferTeammates); ++i )
	{
		pEntity = entFromEntityRef(pTeamMapTransfer->iPartitionIdx, pTeamMapTransfer->eaMapTransferTeammates[i]->iEntRef);
		if(pEntity)
		{
			pTeam = team_GetTeam(pEntity);
			if (pTeam)
				break;
		}
	}

	bIsTeamOnOneMap = gslTeam_checkAllMembersForTransfer(pTeam, pTeamMapTransfer);

	pCorral = gslTeam_FindTeamCorralInfoForTeam(pTeam);

	if (pCorral)
	{
		bLocalTeamReady = pCorral->bIsTeamReady;
	}
	else
	{
		pTeamMapTransfer->bCancelTeamTransfer = true;
	}

	pTeamMapTransfer->bAllMembersReady = bLocalTeamReady && bIsTeamOnOneMap;

	return pTeamMapTransfer->bAllMembersReady && iCurrentTime >= pTeamMapTransfer->iMapTransferResponseTimer;
}

static bool gslTeam_IsAwayTeamReady( SA_PARAM_NN_VALID TeamMissionMapTransfer* pTeamMapTransfer )
{
	S32 i;
	U32 iCurrentTime = timeSecondsSince2000();
	
	// Players are transferred regardless of their response once the timer runs out in NNO.
	if (gConf.bEnableNNOTeamWarp)
	{
		return gslTeam_IsNNOTeamTransferReady(pTeamMapTransfer, iCurrentTime);
	}

	for ( i = eaSize( &pTeamMapTransfer->eaMapTransferTeammates )-1; i >= 0; i-- )
	{
		if ( !entFromEntityRef(pTeamMapTransfer->iPartitionIdx, pTeamMapTransfer->eaMapTransferTeammates[i]->iEntRef) )
			continue;

		if ( pTeamMapTransfer->eaMapTransferTeammates[i]->bOptIn == false )
			return false;
	}

	return true;
}

static void gslTeam_AwayTeamUpdatePrimaryMemberData(	SA_PARAM_NN_VALID TeamMissionMapTransfer* pTeamMapTransfer,
														SA_PARAM_NN_VALID Entity* pEntity )
{
	pTeamMapTransfer->pTeam = team_GetTeam( pEntity );
	//always set the single player id just in case the team is disbanded
	pTeamMapTransfer->uiSinglePlayerID = entGetContainerID( pEntity );
}

static void gslTeam_AwayTeamMemberStateChangedUpdateInternal(	SA_PARAM_NN_VALID TeamMissionMapTransfer* pTeamMapTransfer,
																Entity* pInstigator, bool bResetTimer )
{
	S32 i, iTeamSize = eaSize( &pTeamMapTransfer->eaMapTransferTeammates );
	bool bUpdate = true;
	bool bContactDialog = gslTeam_IsTeamInteractingWithContact(pTeamMapTransfer);

	if ( iTeamSize > 0 )
	{
		Entity* pEntity = entFromEntityRef(pTeamMapTransfer->iPartitionIdx, pTeamMapTransfer->eaMapTransferTeammates[0]->iEntRef);
		
		if ( pEntity )
		{
			gslTeam_AwayTeamUpdatePrimaryMemberData( pTeamMapTransfer, pEntity );
		}
	}

	if ( gslTeam_IsAwayTeamReady( pTeamMapTransfer ) )
	{
		gslTeam_MapTransferAwayTeam( pTeamMapTransfer );
		return;
	}

	if(pTeamMapTransfer->bCancelTeamTransfer)
	{
		gslTeam_RemoveAllAwayTeamMembersInternal(pTeamMapTransfer);
		return;
	}
	
	if ( pInstigator )
	{
		for ( i = iTeamSize-1; i >= 0; i-- )
		{
			if ( pTeamMapTransfer->eaMapTransferTeammates[i]->iEntRef == pInstigator->myRef )
			{
				pTeamMapTransfer->eaMapTransferTeammates[i]->bOptIn = false;
				break;
			}
		}
	}

	for ( i = iTeamSize-1; i >= 0; i-- )
	{
		Entity* pEntity = entFromEntityRef(	pTeamMapTransfer->iPartitionIdx, pTeamMapTransfer->eaMapTransferTeammates[i]->iEntRef );

		if ( pEntity==NULL )
		{
			gslTeam_RemoveAwayTeamMemberInternal(pTeamMapTransfer, i);
			continue;
		}

		if (!gslTeam_SendMapTransferInfoToClientInternal(pTeamMapTransfer,pEntity,bUpdate,bResetTimer,bContactDialog,false, false))
		{
			break;
		}
		bResetTimer = false;
		bUpdate = false;
	}
}

void gslTeam_AwayTeamMemberStateChangedUpdate( Entity* pInstigator )
{
	TeamMissionMapTransfer* pTeamMapTransfer = gslTeam_FindAwayTeamTransfer( pInstigator );

	if ( pTeamMapTransfer==NULL )
		return;

	gslTeam_AwayTeamMemberStateChangedUpdateInternal( pTeamMapTransfer, pInstigator, true );
}

static void gslTeam_AwayTeamHandleOptOut( SA_PARAM_NN_VALID TeamMissionMapTransfer* pTeamMapTransfer )
{
	//only do this for the away team chooser as it is the only window that requires updates
	if ( gslTeam_ShouldShowAwayTeamChooser( pTeamMapTransfer ) )
	{
		S32 i;
		for ( i = eaSize( &pTeamMapTransfer->eaMapTransferTeammates )-1; i >= 0; i-- )
		{
			pTeamMapTransfer->eaMapTransferTeammates[i]->bOptIn = false;
		}
	}

	gslTeam_AwayTeamMemberStateChangedUpdateInternal( pTeamMapTransfer, NULL, true );
}

void gslTeam_ShowPartyCircleOnClient(Entity *pEntity, Vec3 v3Center)
{
	ClientCmd_team_showPartyCircle(pEntity, v3Center, s_fPartyCircleRadius, s_pchPartyDepartureCircleFx);
}

void gslTeam_HidePartyCircleOnClient(Entity *pEntity)
{
	ClientCmd_team_hidePartyCircle(pEntity);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_PRIVATE;
void gslTeam_cmd_MapTransferOptIn(SA_PARAM_NN_VALID Entity *pPlayer)
{
	TeamMissionMapTransfer* pTeamMapTransfer = gslTeam_FindAwayTeamTransfer( pPlayer );

	if ( pTeamMapTransfer )
	{
		S32 iTeamSize = eaSize( &pTeamMapTransfer->eaMapTransferTeammates );
		
		if ( iTeamSize > 0 )
		{
			S32 i;

			for ( i = iTeamSize - 1; i >= 0; --i )
			{
				EntityRef erRef = pTeamMapTransfer->eaMapTransferTeammates[i]->iEntRef;
					
				if ( erRef == entGetRef(pPlayer) )
				{
					pTeamMapTransfer->eaMapTransferTeammates[i]->bOptIn = true;
					break;
				}
			}

			gslTeam_AwayTeamMemberStateChangedUpdateInternal( pTeamMapTransfer, NULL, false );
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_PRIVATE;
void gslTeam_cmd_MapTransferOptOut(SA_PARAM_NN_VALID Entity *pPlayer)
{
	// Players cannot opt out of team transfer in NNO
	if (!gConf.bEnableNNOTeamWarp)
	{
		TeamMissionMapTransfer* pTeamMapTransfer = gslTeam_FindAwayTeamTransfer( pPlayer );

		if ( pTeamMapTransfer )
		{
			S32 iTeamSize = eaSize( &pTeamMapTransfer->eaMapTransferTeammates );

			if ( iTeamSize > 0 )
			{
				S32 i;

				for ( i = iTeamSize - 1; i >= 0; --i )
				{
					EntityRef erRef = pTeamMapTransfer->eaMapTransferTeammates[i]->iEntRef;

					if ( erRef == entGetRef(pPlayer) )
					{
						gslTeam_RemoveAwayTeamMemberInternal( pTeamMapTransfer, i );
						break;
					}
				}

				if ( i >= 0 )
				{
					gslTeam_AwayTeamHandleOptOut( pTeamMapTransfer );
				}
			}
		}
	}
}


AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_PRIVATE;
void gslTeam_ChangeAwayTeamPet( Entity* pEntity,
								S32 iNewPetType, U32 iNewPetID,
								U32 uiNewCritterID,
								int iSlot)
{
	TeamMissionMapTransfer* pTeamMapTransfer = gslTeam_FindAwayTeamTransfer( pEntity );

	if ( pTeamMapTransfer && pTeamMapTransfer->pAwayTeamMembers )
	{
		Entity* pNewPetEnt = NULL;
		int iPartitionIdx = entGetPartitionIdx(pEntity);
		S32 i, iAwayTeamSize = eaSize( &pTeamMapTransfer->pAwayTeamMembers->eaMembers );
		AwayTeamMember *pMember = iSlot < iAwayTeamSize ? 
			pTeamMapTransfer->pAwayTeamMembers->eaMembers[iSlot] : NULL;

		if ( pMember==NULL )
			return;

		if (pMember->eEntType == GLOBALTYPE_ENTITYSAVEDPET && !entity_GetSubEntity(iPartitionIdx,pEntity,pMember->eEntType,pMember->iEntID))
			return;

		if (pMember->eEntType == GLOBALTYPE_ENTITYCRITTER && pMember->iEntID != entGetContainerID(pEntity))
			return;

		if ( iNewPetType == GLOBALTYPE_ENTITYSAVEDPET )
			pNewPetEnt = entity_GetSubEntity( iPartitionIdx, pEntity, iNewPetType, iNewPetID );
		else if ( iNewPetType == GLOBALTYPE_ENTITYCRITTER && iNewPetID == entGetContainerID(pEntity) )
			pNewPetEnt = pEntity;

		if ( pNewPetEnt )
		{
			if (pMember && (pMember->eEntType == iNewPetType) && (pMember->iEntID == iNewPetID)
				&& (pMember->uiCritterPetID == uiNewCritterID)) 
			{
				// No work since setting to same as current
				return;
			}

			if (iNewPetType != GLOBALTYPE_ENTITYCRITTER)
			{
				for ( i = 0; i < iAwayTeamSize; i++ )
				{
					AwayTeamMember *pOtherMember = pTeamMapTransfer->pAwayTeamMembers->eaMembers[i];
					if (pOtherMember->eEntType == iNewPetType && pOtherMember->iEntID == iNewPetID)
					{
						// Found this pet in a different slot, so swap slots
						pOtherMember->eEntType = pMember->eEntType;
						pOtherMember->iEntID = pMember->iEntID;
						pOtherMember->uiCritterPetID = pMember->uiCritterPetID;
						break;
					}
				}
			}

			pMember->eEntType = iNewPetType;
			pMember->iEntID = iNewPetID;
			pMember->uiCritterPetID = uiNewCritterID;
			gslTeam_AwayTeamMemberStateChangedUpdateInternal( pTeamMapTransfer, pEntity, false );
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_PRIVATE;
void gslTeam_AddAwayTeamMember( Entity* pEntity )
{
	TeamMissionMapTransfer* pTeamMapTransfer = gslTeam_FindAwayTeamTransfer( pEntity );

	if ( pTeamMapTransfer && pTeamMapTransfer->pAwayTeamMembers )
	{
		S32 iTeamSize = eaSize( &pTeamMapTransfer->eaMapTransferTeammates );
		S32 iAwayTeamSize = eaSize(&pTeamMapTransfer->pAwayTeamMembers->eaMembers);	
		Team* pTeam = team_GetTeam( pEntity );
		
		if ((iTeamSize == 1 || (pTeam && pTeam == pTeamMapTransfer->pTeam && team_IsAwayTeamLeader(pEntity,pTeamMapTransfer->pAwayTeamMembers))))
		{
			if ( iAwayTeamSize < TEAM_MAX_SIZE )
			{
				if ( team_IsMember(pEntity) )
				{
					pEntity->pTeam->bTeamMissionMapTransfer = true;
				}

				eaPush( &pTeamMapTransfer->pAwayTeamMembers->eaMembers, StructCreate( parse_AwayTeamMember ) );

				gslTeam_AwayTeamMemberStateChangedUpdateInternal( pTeamMapTransfer, pEntity, true );
			}
		}
	}
}


AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_PRIVATE;
void gslTeam_RemoveAwayTeamMember( Entity* pEntity, S32 iEntityType, U32 iContainerID )
{
	TeamMissionMapTransfer* pTeamMapTransfer = gslTeam_FindAwayTeamTransfer( pEntity );

	//not possible to remove yourself
	if ( pEntity==NULL || pEntity->myContainerID == iContainerID )
		return;

	if ( iEntityType == GLOBALTYPE_ENTITYPLAYER ) //cannot remove players
		return;

	if ( pTeamMapTransfer && pTeamMapTransfer->pAwayTeamMembers )
	{
		S32 i, j;
		S32 iAwayTeamSize = eaSize( &pTeamMapTransfer->pAwayTeamMembers->eaMembers );	
		S32 iTeamSize = eaSize( &pTeamMapTransfer->eaMapTransferTeammates );
		int iPartitionIdx = entGetPartitionIdx(pEntity);
		Team* pTeam = team_GetTeam( pEntity );
		
		if (iAwayTeamSize > 1 && (iTeamSize == 1 || (pTeam && pTeam == pTeamMapTransfer->pTeam && team_IsAwayTeamLeader(pEntity,pTeamMapTransfer->pAwayTeamMembers))))
		{
			for ( i = iAwayTeamSize-1; i >= 0; i-- )
			{
				if (	pTeamMapTransfer->pAwayTeamMembers->eaMembers[i]->eEntType == iEntityType 
					&&	pTeamMapTransfer->pAwayTeamMembers->eaMembers[i]->iEntID == iContainerID )
				{
					if ( pTeamMapTransfer->pAwayTeamMembers->eaMembers[i]->eEntType == GLOBALTYPE_ENTITYPLAYER )
					{
						Entity* pRemovedEntity = 
							entFromContainerID(	iPartitionIdx,
												pTeamMapTransfer->pAwayTeamMembers->eaMembers[i]->eEntType,
												pTeamMapTransfer->pAwayTeamMembers->eaMembers[i]->iEntID );
						if ( pRemovedEntity )
						{
							ClientCmd_teamHideMapTransferChoice( pRemovedEntity );

							for ( j = iTeamSize - 1; j >= 0; j-- )
							{
								if ( pTeamMapTransfer->eaMapTransferTeammates[j]->iEntRef == pRemovedEntity->myRef )
								{
									gslTeam_RemoveAwayTeamMemberInternal( pTeamMapTransfer, j );
									break;
								}
							}
						}
					}
					
					StructDestroy( parse_AwayTeamMember, eaRemove( &pTeamMapTransfer->pAwayTeamMembers->eaMembers, i ) );
						
					gslTeam_AwayTeamMemberStateChangedUpdateInternal( pTeamMapTransfer, pEntity, true );

					break;
				}
			}
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_PRIVATE;
void gslTeam_ChangeAwayTeamPetOwnership( Entity* pEntity, S32 iOwnerType, U32 iOwnerID, S32 iSlot )
{
	TeamMissionMapTransfer* pTeamMapTransfer = gslTeam_FindAwayTeamTransfer( pEntity );

	if ( pTeamMapTransfer && pTeamMapTransfer->pAwayTeamMembers )
	{
		Entity* pOwner = entFromContainerID( entGetPartitionIdx(pEntity), iOwnerType, iOwnerID );
		Team* pTeam = team_GetTeam( pEntity );

		if ( pOwner && pTeam && pTeam == pTeamMapTransfer->pTeam && team_IsAwayTeamLeader(pEntity,pTeamMapTransfer->pAwayTeamMembers) )
		{
			S32 iAwayTeamSize = eaSize(&pTeamMapTransfer->pAwayTeamMembers->eaMembers);

			//make sure the slot has a valid index
			if ( iSlot < 0 || iSlot >= iAwayTeamSize )
			{
				return;
			}

			//if the entity residing in this slot is a player, don't do anything
			if ( pTeamMapTransfer->pAwayTeamMembers->eaMembers[iSlot]->eEntType == GLOBALTYPE_ENTITYPLAYER )
			{
				return;
			}

			//if this is the owner of this slot, don't do anything
			if ( entity_GetSubEntity(	entGetPartitionIdx(pOwner), pOwner,	
										pTeamMapTransfer->pAwayTeamMembers->eaMembers[iSlot]->eEntType, 
										pTeamMapTransfer->pAwayTeamMembers->eaMembers[iSlot]->iEntID ) )
			{
				return;
			}

			pTeamMapTransfer->pAwayTeamMembers->eaMembers[iSlot]->eEntType = 0;
			pTeamMapTransfer->pAwayTeamMembers->eaMembers[iSlot]->iEntID = 0;
			pTeamMapTransfer->pAwayTeamMembers->eaMembers[iSlot]->uiCritterPetID = 0;

			gslTeam_AwayTeamMemberGrantPet( pTeamMapTransfer, pOwner, iSlot );

			gslTeam_AwayTeamMemberStateChangedUpdateInternal( pTeamMapTransfer, pEntity, true );
		}
	}
}

bool gslTeam_CreateAwayTeamMapTransferData( Entity* pEntity,
											const char* pcMap, const char* pcSpawn, GlobalType eOwnerType, 
											ContainerID uOwner, const char* pchMapVars, 
											ZoneMapInfo* pCurrZoneMap, ZoneMapInfo* pNextZoneMap,
											RegionRules* pSrcRules, RegionRules* pDstRules,
											DoorTransitionSequenceDef* pTransOverride )
{
	TeamMissionMapTransfer* pTeamMapTransfer = gslTeam_FindAwayTeamTransfer( pEntity );

	if ( pEntity==NULL || pSrcRules==NULL || pDstRules==NULL || pCurrZoneMap==NULL || pNextZoneMap==NULL )
		return false;
	
	if ( pTeamMapTransfer ) //this shouldn't happen, but just in case...
	{
		gslTeam_RemoveAllAwayTeamMembersInternal( pTeamMapTransfer );
	}

	pTeamMapTransfer = StructCreate( parse_TeamMissionMapTransfer );
		
	pTeamMapTransfer->pcMap = allocAddString(pcMap);
	pTeamMapTransfer->pcSpawn = allocAddString(pcSpawn);
	pTeamMapTransfer->eOwnerType = eOwnerType;
	pTeamMapTransfer->uOwner = uOwner;
	pTeamMapTransfer->eSrcZoneMapType = zmapInfoGetMapType( pCurrZoneMap );
	pTeamMapTransfer->eDstZoneMapType = zmapInfoGetMapType( pNextZoneMap );
	pTeamMapTransfer->eSrcRegionType = pSrcRules->eRegionType;
	pTeamMapTransfer->eDstRegionType = pDstRules->eRegionType;
	gslTeam_AwayTeamUpdatePrimaryMemberData( pTeamMapTransfer, pEntity );
	//set the max pets per player from the source region rules. Used for comparison purposes
	pTeamMapTransfer->iSourceAllowedPetsPerPlayer = pSrcRules->iAllowedPetsPerPlayer;
	//set the max pets per player from the destination region rules
	pTeamMapTransfer->iAllowedPetsPerPlayer = pDstRules->iAllowedPetsPerPlayer;
	//grab the door exit sequence override
	pTeamMapTransfer->pTransOverride = pTransOverride;
	pTeamMapTransfer->pchMapVars = allocAddString(pchMapVars);
	pTeamMapTransfer->iPartitionIdx = entGetPartitionIdx(pEntity);
	pTeamMapTransfer->pcSrcMap = zmapInfoGetCurrentName(pCurrZoneMap);

	if (gConf.bEnableNNOTeamWarp && team_GetTeam(pEntity))
	{
		gslTeam_CreateTeamCorral(pEntity, false);
	}

	eaPush( &s_eaTeamMapTransfers, pTeamMapTransfer );
	
	return true;
}

bool gslTeam_AddAwayTeamMemberToMapTransfer( Entity* pEntity, bool bIncludeTeammates )
{
	MemberMissionMapTransfer* pMember;
	TeamMissionMapTransfer* pTeamMapTransfer = gslTeam_FindAwayTeamTransfer( pEntity );

	if ( pTeamMapTransfer )
	{
		S32 i;

		for ( i = eaSize( &pTeamMapTransfer->eaMapTransferTeammates )-1; i >= 0; i-- )
		{
			if ( pTeamMapTransfer->eaMapTransferTeammates[i]->iEntRef == entGetRef(pEntity) )
			{
				return false;
			}
		}

		pMember = StructCreate( parse_MemberMissionMapTransfer );
		pMember->iEntRef = entGetRef( pEntity );
		pMember->bOptIn = false;
		eaPush( &pTeamMapTransfer->eaMapTransferTeammates, pMember );

		if ( team_IsMember(pEntity) )
		{
			pEntity->pTeam->bTeamMissionMapTransfer = true;
		}

		return true;
	}

	return false;
}

void gslTeam_SendMapTransferInfoToClients( Entity* pEntity, bool bIncludeTeammates )
{
	S32 i;
	bool bIsTeamInteracting, bUpdate = true;
	TeamMissionMapTransfer* pTeamMapTransfer = gslTeam_FindAwayTeamTransfer( pEntity );

	if ( pTeamMapTransfer==NULL )
	{
		return;
	}

	bIsTeamInteracting = gslTeam_IsTeamInteractingWithContact(pTeamMapTransfer);

	for ( i = eaSize( &pTeamMapTransfer->eaMapTransferTeammates )-1; i >= 0; i-- )
	{
		Entity* pCurrEnt = entFromEntityRef( pTeamMapTransfer->iPartitionIdx, pTeamMapTransfer->eaMapTransferTeammates[i]->iEntRef );

		if ( pCurrEnt==NULL )
		{
			gslTeam_RemoveAwayTeamMemberInternal(pTeamMapTransfer, i);
			continue;
		}

		if (!gslTeam_SendMapTransferInfoToClientInternal(pTeamMapTransfer,pCurrEnt,bUpdate,bUpdate,bIsTeamInteracting,true, bIncludeTeammates))
		{
			pTeamMapTransfer = NULL;
			break;
		}
		bUpdate = false;
	}

	// It's possible that this function removed all the members of the away team
	if ( pTeamMapTransfer && eaSize( &pTeamMapTransfer->eaMapTransferTeammates ) == 0 )
	{
		// If so, destroy pTeamMapTransfer
		gslTeam_DestroyAwayTeamTransfer( pTeamMapTransfer );
	}
}

static void gslTeam_MapTransferNotifyAutoOptOut( TeamMissionMapTransfer* pTeamMapTransfer )
{
	if ( pTeamMapTransfer )
	{
		bool bRemoved = false;
		int i;

		for ( i = eaSize(&pTeamMapTransfer->eaMapTransferTeammates) - 1; i >= 0; i-- )
		{
			if ( !pTeamMapTransfer->eaMapTransferTeammates[i]->bOptIn )
			{	
				Entity* pEntity = entFromEntityRef(	pTeamMapTransfer->iPartitionIdx, pTeamMapTransfer->eaMapTransferTeammates[i]->iEntRef );

				if ( pEntity )
				{
					//make sure the window is closed
					ClientCmd_teamHideMapTransferChoice( pEntity );
				}

				gslTeam_RemoveAwayTeamMemberInternal( pTeamMapTransfer, i );
				bRemoved = true;
			}
		}

		if ( bRemoved )
		{
			gslTeam_AwayTeamHandleOptOut( pTeamMapTransfer );
		}
	}
}

static void gslTeam_AwayTeamMemberRemoveInternal(Entity* pEntity, TeamMissionMapTransfer* pTeamMapTransfer, 
												 bool bKeepIfMapOwner)
{
	
	GlobalType eOwnerType;
	ContainerID iOwnerID;

	eOwnerType = partition_OwnerTypeFromIdx(entGetPartitionIdx(pEntity));
	iOwnerID = partition_OwnerIDFromIdx(entGetPartitionIdx(pEntity));



	if (	!bKeepIfMapOwner 
		||	(eOwnerType != entGetType(pEntity) || iOwnerID != entGetContainerID(pEntity)) )
	{
		int i;
		for ( i = eaSize(&pTeamMapTransfer->eaMapTransferTeammates) - 1; i >= 0; i-- )
		{
			if ( pEntity->myRef == pTeamMapTransfer->eaMapTransferTeammates[i]->iEntRef )
			{
				gslTeam_RemoveAwayTeamMemberInternal( pTeamMapTransfer, i );
				break;
			}
		}

		//make sure the window is closed
		ClientCmd_teamHideMapTransferChoice( pEntity );
	}
	else if ( pEntity->pTeam && !team_IsMember( pEntity ) )
	{
		pEntity->pTeam->bTeamMissionMapTransfer = false;
	}

	if ( eaSize(&pTeamMapTransfer->eaMapTransferTeammates) == 0 )
	{
		gslTeam_RemoveAllAwayTeamMembersInternal( pTeamMapTransfer );
	}
	else
	{
		gslTeam_AwayTeamHandleOptOut( pTeamMapTransfer );
	}
}

void gslTeam_AwayTeamMemberRemove( Entity* pEntity, bool bKeepIfMapOwner )
{
	TeamMissionMapTransfer* pTeamMapTransfer = gslTeam_FindAwayTeamTransfer( pEntity );

	if ( pTeamMapTransfer==NULL && pEntity && pEntity->pTeam && pEntity->pTeam->bTeamMissionMapTransfer )
	{
		S32 i, j;
		for ( i = eaSize( &s_eaTeamMapTransfers )-1; i >= 0; i-- )
		{
			TeamMissionMapTransfer* pData = s_eaTeamMapTransfers[i];
			if ( pData->pTeam )
			{
				for ( j = eaSize(&pData->eaMapTransferTeammates)-1; j >= 0; j-- )
				{
					if ( pData->eaMapTransferTeammates[j]->iEntRef == entGetRef( pEntity ) )
					{
						pTeamMapTransfer = pData;
						break;
					}
				}
				if ( pTeamMapTransfer )
					break;
			}
		}	
	}

	if ( pTeamMapTransfer )
	{
		gslTeam_AwayTeamMemberRemoveInternal( pEntity, pTeamMapTransfer, bKeepIfMapOwner );
	}
}

void gslTeam_Tick( void )
{
	static U32 s_uiLastUpdateTime = 0;
	U32 uiCurrentTime = timeSecondsSince2000();

	if ( uiCurrentTime != s_uiLastUpdateTime )
	{
		S32 i;

		gslTeam_UpdateAllTeamCorrals();

		for ( i = eaSize(&s_eaTeamMapTransfers) - 1; i >= 0; i-- )
		{
			TeamMissionMapTransfer* pTeamMapTransfer = s_eaTeamMapTransfers[i];

			if ( pTeamMapTransfer->iMapTransferResponseTimer <= 0 )
				continue;

			if ( uiCurrentTime >= pTeamMapTransfer->iMapTransferResponseTimer  || gConf.bEnableNNOTeamWarp)
			{
				// Players do not automatically opt out of team transfer in NNO. Instead they are forced to transfer once the timer runs out.
				if (gConf.bEnableNNOTeamWarp)
				{
					gslTeam_AwayTeamMemberStateChangedUpdateInternal(pTeamMapTransfer, NULL, true);
				}
				else
				{
					gslTeam_MapTransferNotifyAutoOptOut(pTeamMapTransfer);
				}
			}
		}

		s_uiLastUpdateTime = uiCurrentTime;
	}
}

EntityIterator *teamRaiderIterator;
bool teamRaiderRunning = false;
int teamRaidsPerFrame = 30;
int teamRaiderDisband = 1;
int teamRaiderRequest = 1;
int teamRaiderRepeat = 1;
AUTO_CMD_INT(teamRaidsPerFrame, TeamRaidsPerFrame);
AUTO_CMD_INT(teamRaiderDisband, TeamRaiderDisband);
AUTO_CMD_INT(teamRaiderRequest, TeamRaiderRequest);
AUTO_CMD_INT(teamRaiderRepeat, TeamRaiderRepeat);

AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void TeamRaiderStop(void)
{
	EntityIteratorRelease(teamRaiderIterator);
	teamRaiderRunning = false;
}

static void TeamRaiderDoStuff_CB(TimedCallback *pCallback, F32 timeSinceLastCallback, void *userData)
{
	int i = 0;
	int j = 0;
	Entity *pMember1 = NULL, *pMember2 = NULL, *pMember3 = NULL;
	Entity *pLeader = NULL;
	Team *pTeam = NULL;

	if(!teamRaiderRunning)
	{
		return;
	}

	while((pMember1 = EntityIteratorGetNext(teamRaiderIterator)))
	{
		pTeam = team_GetTeam(pMember1);
		pLeader = pTeam ? entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pTeam->pLeader ? pTeam->pLeader->iEntID: 0) : NULL;
		pMember2 = EntityIteratorGetNext(teamRaiderIterator);
		if(pMember2) pMember3 = EntityIteratorGetNext(teamRaiderIterator);

		if(!pTeam || !pLeader) continue;

		j = randomIntRange(0, 4);
		
		switch(j)
		{
		case 0:
			RemoteCommand_aslTeam_ChangeLootMode(GLOBALTYPE_TEAMSERVER, 0, pTeam->iContainerID, entGetContainerID(pLeader), LootMode_RoundRobin, TEAM_SERVERDOWNHANDLER(pLeader));
			break;
		case 1:
			RemoteCommand_aslTeam_SetChampion(GLOBALTYPE_TEAMSERVER, 0, pTeam->iContainerID, entGetContainerID(pLeader), entGetContainerID(pMember1), TEAM_SERVERDOWNHANDLER(pLeader));
			break;
		case 2:
			RemoteCommand_aslTeam_SetSidekicking(GLOBALTYPE_TEAMSERVER, 0, pTeam->iContainerID, entGetContainerID(pLeader), true, TEAM_SERVERDOWNHANDLER(pLeader));
			break;
		case 3:
			RemoteCommand_aslTeam_Promote(GLOBALTYPE_TEAMSERVER, 0, pTeam->iContainerID, entGetContainerID(pLeader), entGetContainerID(pMember1), TEAM_SERVERDOWNHANDLER(pLeader));
			break;
		case 4:
			RemoteCommand_aslTeam_ChangeMode(GLOBALTYPE_TEAMSERVER, 0, pTeam->iContainerID, entGetContainerID(pLeader), TeamMode_Open, TEAM_SERVERDOWNHANDLER(pLeader));
			break;
		}

		if(!pMember2 || !pMember3)
		{
			break;
		}

		if(++i >= teamRaidsPerFrame)
		{
			TimedCallback_Run(TeamRaiderDoStuff_CB, NULL, 0.0f);
			return;
		}
	}

	if(teamRaiderRepeat)
	{
		EntityIteratorRelease(teamRaiderIterator);
		teamRaiderIterator = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
		TimedCallback_Run(TeamRaiderDoStuff_CB, NULL, 0.0f);
	}
	else
	{
		EntityIteratorRelease(teamRaiderIterator);
		teamRaiderRunning = false;
	}
}

static void TeamRaiderInvite_CB(TimedCallback *pCallback, F32 timeSinceLastCallback, void *userData);

static void TeamRaiderLeave_CB(TimedCallback *pCallback, F32 timeSinceLastCallback, void *userData)
{
	int i = 0;
	Entity *pMember1 = NULL, *pMember2 = NULL, *pMember3 = NULL;

	if(!teamRaiderRunning)
	{
		return;
	}

	while((pMember1 = EntityIteratorGetNext(teamRaiderIterator)))
	{
		pMember2 = EntityIteratorGetNext(teamRaiderIterator);
		if(pMember2) pMember3 = EntityIteratorGetNext(teamRaiderIterator);
		
		RemoteCommand_aslTeam_Leave(GLOBALTYPE_TEAMSERVER, 0, team_GetTeamID(pMember1), entGetContainerID(pMember1), entGetContainerID(pMember1), true, TEAM_SERVERDOWNHANDLER(pMember1));
		if(pMember2) RemoteCommand_aslTeam_Leave(GLOBALTYPE_TEAMSERVER, 0, team_GetTeamID(pMember2), entGetContainerID(pMember2), entGetContainerID(pMember2), true, TEAM_SERVERDOWNHANDLER(pMember2));
		else break;
		if(pMember3) RemoteCommand_aslTeam_Leave(GLOBALTYPE_TEAMSERVER, 0, team_GetTeamID(pMember3), entGetContainerID(pMember3), entGetContainerID(pMember3), true, TEAM_SERVERDOWNHANDLER(pMember3));
		else break;

		if(++i >= teamRaidsPerFrame)
		{
			TimedCallback_Run(TeamRaiderLeave_CB, NULL, 0.0f);
			return;
		}
	}

	if(teamRaiderRepeat)
	{
		EntityIteratorRelease(teamRaiderIterator);
		teamRaiderIterator = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
		TimedCallback_Run(TeamRaiderInvite_CB, NULL, 0.0f);
	}
	else
	{
		EntityIteratorRelease(teamRaiderIterator);
		teamRaiderRunning = false;
	}
}

static void TeamRaiderAccept2_CB(TimedCallback *pCallback, F32 timeSinceLastCallback, void *userData)
{
	int i = 0;
	Entity *pLeader = NULL;
	Entity *pInvitee = NULL;

	if(!teamRaiderRunning)
	{
		return;
	}

	while((pLeader = EntityIteratorGetNext(teamRaiderIterator)) && EntityIteratorGetNext(teamRaiderIterator) && (pInvitee = EntityIteratorGetNext(teamRaiderIterator)))
	{
		ANALYSIS_ASSUME(pLeader != NULL && pInvitee != NULL);
		RemoteCommand_aslTeam_AcceptRequest(GLOBALTYPE_TEAMSERVER, 0, team_GetTeamID(pLeader), entGetContainerID(pLeader), entGetContainerID(pInvitee), TEAM_SERVERDOWNHANDLER(pLeader));

		if(++i >= teamRaidsPerFrame)
		{
			TimedCallback_Run(TeamRaiderAccept2_CB, NULL, 0.0f);
			return;
		}
	}

	EntityIteratorRelease(teamRaiderIterator);
	teamRaiderIterator = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);

	if(teamRaiderDisband)
	{
		TimedCallback_Run(TeamRaiderLeave_CB, NULL, 0.0f);
	}
	else
	{
		TimedCallback_Run(TeamRaiderDoStuff_CB, NULL, 0.0f);
	}
}

static void TeamRaiderRequest_CB(TimedCallback *pCallback, F32 timeSinceLastCallback, void *userData)
{
	int i = 0;
	Entity *pLeader = NULL;
	Entity *pRequest = NULL;

	if(!teamRaiderRunning)
	{
		return;
	}

	while((pLeader = EntityIteratorGetNext(teamRaiderIterator)) && EntityIteratorGetNext(teamRaiderIterator) && (pRequest = EntityIteratorGetNext(teamRaiderIterator)))
	{
		ANALYSIS_ASSUME(pRequest != NULL);
		RemoteCommand_aslTeam_Request(GLOBALTYPE_TEAMSERVER, 0, team_GetTeamID(pLeader), entGetContainerID(pRequest), TEAM_SERVERDOWNHANDLER(pRequest));
		
		if(++i >= teamRaidsPerFrame)
		{
			TimedCallback_Run(TeamRaiderRequest_CB, NULL, 0.0f);
			return;
		}
	}

	EntityIteratorRelease(teamRaiderIterator);
	teamRaiderIterator = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
	TimedCallback_Run(TeamRaiderAccept2_CB, NULL, 0.0f);
}

static void TeamRaiderAccept_CB(TimedCallback *pCallback, F32 timeSinceLastCallback, void *userData)
{
	int i = 0;
	Entity *pLeader = NULL;
	Entity *pInvitee = NULL;

	if(!teamRaiderRunning)
	{
		return;
	}

	while((pLeader = EntityIteratorGetNext(teamRaiderIterator)) && (pInvitee = EntityIteratorGetNext(teamRaiderIterator)))
	{
		ANALYSIS_ASSUME(pLeader != NULL && pInvitee != NULL);
		RemoteCommand_aslTeam_ChangeMode(GLOBALTYPE_TEAMSERVER, 0, team_GetTeamID(pLeader), entGetContainerID(pLeader), TeamMode_RequestOnly, TEAM_SERVERDOWNHANDLER(pLeader));
		RemoteCommand_aslTeam_AcceptInvite(GLOBALTYPE_TEAMSERVER, 0, pInvitee->pTeam->iTeamID, entGetContainerID(pInvitee), false, TEAM_SERVERDOWNHANDLER(pInvitee));

		if(!EntityIteratorGetNext(teamRaiderIterator))
		{
			break;
		}

		if(++i >= teamRaidsPerFrame)
		{
			TimedCallback_Run(TeamRaiderAccept_CB, NULL, 0.0f);
			return;
		}
	}

	EntityIteratorRelease(teamRaiderIterator);
	teamRaiderIterator = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);

	if(teamRaiderRequest)
	{
		TimedCallback_Run(TeamRaiderRequest_CB, NULL, 0.0f);
	}
	else if(teamRaiderDisband)
	{
		TimedCallback_Run(TeamRaiderLeave_CB, NULL, 0.0f);
	}
	else
	{
		TimedCallback_Run(TeamRaiderDoStuff_CB, NULL, 0.0f);
	}
}

static void TeamRaiderInvite_CB(TimedCallback *pCallback, F32 timeSinceLastCallback, void *userData)
{
	int i = 0;
	Entity *pLeader = NULL;
	Entity *pInvitee = NULL;

	if(!teamRaiderRunning)
	{
		return;
	}

	while((pLeader = EntityIteratorGetNext(teamRaiderIterator)) && (pInvitee = EntityIteratorGetNext(teamRaiderIterator)))
	{
		ANALYSIS_ASSUME(pLeader != NULL && pInvitee != NULL);
		RemoteCommand_aslTeam_Invite(GLOBALTYPE_TEAMSERVER, 0, team_GetTeamID(pLeader), entGetContainerID(pLeader), entGetContainerID(pInvitee), REF_STRING_FROM_HANDLE(pLeader->hAllegiance), REF_STRING_FROM_HANDLE(pLeader->hSubAllegiance), TEAM_SERVERDOWNHANDLER(pLeader));

		if(!EntityIteratorGetNext(teamRaiderIterator))
		{
			break;
		}

		if(++i >= teamRaidsPerFrame)
		{
			TimedCallback_Run(TeamRaiderInvite_CB, NULL, 0.0f);
			return;
		}
	}

	EntityIteratorRelease(teamRaiderIterator);
	teamRaiderIterator = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
	TimedCallback_Run(TeamRaiderAccept_CB, NULL, 0.0f);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void TeamRaider(void)
{
	if(teamRaiderRunning)
	{
		return;
	}

	teamRaiderRunning = true;
	teamRaiderIterator = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
	TimedCallback_Run(TeamRaiderInvite_CB, NULL, 0.0f);
}

///////////////////////////////////////////////////////////////////////////////////////////
// XBOX/Voice Chat Related
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_JoinVoiceChat);
void gslTeam_cmd_JoinVoiceChat(SA_PARAM_NN_VALID Entity *pEnt)
{
	if (team_IsMember(pEnt)) 
	{
		RemoteCommand_aslTeam_JoinVoiceChat(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, pEnt->myContainerID, NULL, NULL, NULL);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_LeaveVoiceChat);
void gslTeam_cmd_LeaveVoiceChat(SA_PARAM_NN_VALID Entity *pEnt)
{
	if (team_IsMember(pEnt)) 
	{
		RemoteCommand_aslTeam_LeaveVoiceChat(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, pEnt->myContainerID, NULL, NULL, NULL);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_CATEGORY(Team) ACMD_NAME(Team_SetXSessionInfo);
void gslTeam_cmd_SetXSessionInfo(SA_PARAM_NN_VALID Entity *pEnt, CrypticXSessionInfo *pXSessionInfo)
{
	if (team_IsMember(pEnt)) 
	{
		RemoteCommand_aslTeam_SetXBoxOnlineSessionId(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, pEnt->myContainerID, pXSessionInfo, NULL, NULL, NULL);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
void gslTeam_cmd_xBoxSessionCreate(ContainerID iTeamId, ContainerID iEntId)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iEntId);

	// Call the command on the client
	if (pEnt != NULL)
	{
		ClientCmd_xBoxSessionCreate(pEnt, iTeamId);
	}	
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
void gslTeam_cmd_SendStatusUpdate(ContainerID iEntId)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iEntId);

	// We need to resend our status so the team search works
	if (pEnt != NULL)
	{
		ServerChat_PlayerUpdate(pEnt, CHATUSER_UPDATE_SHARD);
	}	
}

static TeamInfoPlayer *gslTeam_GetTeamRequestInfoFromEnt(Entity *pEntity, TeamInfoRequest *pTeamInfoRequest)
{
	int i, j, k;
	TeamInfoPlayer *tip = StructCreate(parse_TeamInfoPlayer);
	tip->containerID = entGetContainerID(pEntity);
	if (pEntity->pSaved)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
		int iPartitionIdx = entGetPartitionIdx(pEntity);

		for (i = eaSize(&pEntity->pSaved->ppOwnedContainers)-1; i >= 0; --i)
		{
			TeamInfoPet *tip2;
			PetRelationship* pPet = pEntity->pSaved->ppOwnedContainers[i];
			Entity* pPetEnt = pPet ? GET_REF(pPet->hPetRef) : NULL;
			if (!pPetEnt) continue;
			if (SavedPet_IsPetAPuppet(pEntity, pPet)) continue;
			tip2 = StructCreate(parse_TeamInfoPet);
			tip2->containerID = entGetContainerID(pPetEnt);

			if (pPetEnt->pChar)
			{
				Power **ppPowersTemp = NULL;
				character_FindAllPowersInPowerTrees(pPetEnt->pChar, &ppPowersTemp);

				for ( j = 0; j < eaSize(&ppPowersTemp); ++j )
				{
					TeamInfoPowerElem *tipe;
					Power *pPow = ppPowersTemp[j];
					PowerDef *pPowDef = pPow ? GET_REF(pPow->hDef) : NULL;
					int iCategory = -1;

					for ( k = eaSize(&pTeamInfoRequest->eaPowersCategories)-1; k >= 0 ; --k)
					{
						iCategory = StaticDefineIntGetInt(PowerCategoriesEnum,pTeamInfoRequest->eaPowersCategories[k]);
						if(iCategory != -1)
						{
							if (eaiFind(&pPowDef->piCategories,iCategory) >= 0)
								break;
						}
					}
					if (k < 0) continue;

					tipe = StructCreate(parse_TeamInfoPowerElem);
					tipe->pchIcon = StructAllocString(pPowDef->pchIconName);
					tipe->iePurpose = (int)pPowDef->ePurpose;
					tipe->uiID = pPow->uiID;
					COPY_HANDLE(tipe->hRef, pPow->hDef);
					if(pEntity->pChar)
					{
						//TODO(jransom): Set the language to ClientEntity (Not Entity passed in to this function)
						if(!pEntity->pChar->iLevelCombat)
							pEntity->pChar->iLevelCombat = entity_GetSavedExpLevel(pEntity);
						MAX1(pEntity->pChar->iLevelCombat,1);
						power_AutoDesc(iPartitionIdx,pPow,pEntity->pChar,&tipe->pchToolTip,NULL,"<br>","<bsp><bsp>","- ",false,0,entGetPowerAutoDescDetail(pEntity,true),pExtract,NULL);
					}

					eaPush(&tip2->eaPowerElem, tipe);
				}
			}

			eaPush(&tip->eaPetList, tip2);
		}
	}
	return tip;
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(RequestTeamInfo) ACMD_ACCESSLEVEL(0) ACMD_SERVERONLY ACMD_PRIVATE;
void gslTeam_cmd_RequestTeamInfo(Entity *pClientEntity, TeamInfoRequest *pTeamInfoRequest)
{
	int i, j, k;
	TeamInfoFromServer teamInfo;
	Team *pTeam = team_GetTeam(pClientEntity);

	teamInfo.eaPlayerList = NULL;
	if (pTeam)
	{
		int iPartitionIdx = entGetPartitionIdx(pClientEntity);

		for (i = eaSize(&pTeam->eaMembers)-1; i >= 0; --i)
		{
			TeamInfoPlayer *tip;
			TeamMember *m = pTeam->eaMembers[i];
			Entity *e = m ? GET_REF(m->hEnt) : NULL;
			if (!e) continue;
			if (entGetType(e) != GLOBALTYPE_ENTITYPLAYER) continue;
			e = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, m->iEntID);
			if (!e) continue;
			tip = gslTeam_GetTeamRequestInfoFromEnt(e, pTeamInfoRequest);
			eaPush(&teamInfo.eaPlayerList, tip);
		}
	}
	else
	{
		TeamInfoPlayer *tip = gslTeam_GetTeamRequestInfoFromEnt(pClientEntity, pTeamInfoRequest);
		eaPush(&teamInfo.eaPlayerList, tip);
	}

	ClientCmd_RecieveTeamInfo(pClientEntity, &teamInfo);

	for (i=eaSize(&teamInfo.eaPlayerList)-1;i>=0;--i)
	{
		for (j=eaSize(&teamInfo.eaPlayerList[i]->eaPetList)-1;j>=0;--j)
		{
			for (k=eaSize(&teamInfo.eaPlayerList[i]->eaPetList[j]->eaPowerElem)-1;k>=0;--k)
			{
				TeamInfoPowerElem *tipe = teamInfo.eaPlayerList[i]->eaPetList[j]->eaPowerElem[k];
				if (tipe->pchToolTip) estrDestroy(&tipe->pchToolTip);
			}
		}
	}
	eaDestroyStruct(&teamInfo.eaPlayerList, parse_TeamInfoPlayer);
}


// Player/Mission expression to determine if the player is on a team and a team member is on the same map but a different partition
AUTO_EXPR_FUNC(Player, Mission) ACMD_NAME(team_TeammateIsInDifferentPartition);
bool gslTeam_expr_TeammateIsInDifferntParitition(ExprContext* context)
{
	Entity* pEntity = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);

	if (pEntity!=NULL)
	{
		Team *pTeam = team_GetTeam(pEntity);

		if (pTeam!=NULL)
		{
			int i;
			const char *pcMapName;
			U32 iInstanceNum;

			// These are similar to what is in gslTeam_EntityUpdate, the data from which is passed to aslTeam_UpdateInfo
			//   and is what creates the TeamMember data.
			
			pcMapName = allocAddString(zmapInfoGetPublicName(NULL));
			
			iInstanceNum = 
				(gGSLState.gameServerDescription.baseMapDescription.eMapType == ZMTYPE_STATIC || 
				gGSLState.gameServerDescription.baseMapDescription.eMapType == ZMTYPE_SHARED ||
				gGSLState.gameServerDescription.baseMapDescription.eMapType == ZMTYPE_MISSION) ? partition_PublicInstanceIndexFromIdx(entGetPartitionIdx(pEntity)) : 0;
			
			for (i = 0; i < eaSize(&pTeam->eaMembers); i++)
			{
				if (pTeam->eaMembers[i]->pcMapName==pcMapName && pTeam->eaMembers[i]->iMapInstanceNumber!=iInstanceNum)
				{
					return(true);
				}
			}
		}
	}
	return(false);
}


#include "AutoGen/TeamCommands_c_ast.c"

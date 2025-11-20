/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "logging.h"
#include "TextParser.h"
#include "StringCache.h"
#include "StringUtil.h"

#include "Entity.h"
#include "EntityLib.h"
#include "EntitySavedData.h"
#include "EntitySavedData_h_ast.h"
#include "Player.h"

#include "Team.h"
#include "XBoxStructs.h"
#include "entCritter.h"
#include "aslTeamServer.h"
#include "GameSession.h"
#include "AutoGen/GameSession_h_ast.h"

#include "Character.h"
#include "mission_common.h"
#include "progression_common.h"
#include "progression_transact.h"
#include "AutoGen/progression_common_h_ast.h"
#include "AutoGen/TeamTransactions_c_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/Team_h_ast.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/AppServerLib_autogen_SlowFuncs.h"
#include "AutoGen/AppServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/ChatServer_autogen_RemoteFuncs.h"
#include "AutoGen/ServerLib_autogen_remotefuncs.h"
#include "AutoGen/TeamPetsCommonStructs_h_ast.h"
#include "Player_h_ast.h"
#include "TeamPetsCommonStructs.h"
#include "qsortG.h"
#include "UGCProjectCommon.h"
#include "XBoxStructs_h_ast.h"
#include "queue_common.h"

extern TimingHistory *gCreationHistory;
extern TimingHistory *gActionHistory;

// This macro should be placed at the start of every team transaction. It declares the
// structure used to store feedback and logging.
#define TEAM_TRANSACTION_INIT ASLTeamTransactionReturn *ATR_TEAM_RETURN = StructCreate(parse_ASLTeamTransactionReturn)

// These macros should be called to return from every team transaction. It compiles the
// return structure into a string and stores it in the transaction output.
#define TEAM_TRANSACTION_RETURN_SUCCESS { \
	ParserWriteText(ATR_RESULT_SUCCESS, parse_ASLTeamTransactionReturn, ATR_TEAM_RETURN, 0, 0, 0); \
	StructDestroy(parse_ASLTeamTransactionReturn, ATR_TEAM_RETURN); \
	return TRANSACTION_OUTCOME_SUCCESS; \
}
#define TEAM_TRANSACTION_RETURN_FAILURE { \
	ParserWriteText(ATR_RESULT_FAIL, parse_ASLTeamTransactionReturn, ATR_TEAM_RETURN, 0, 0, 0); \
	StructDestroy(parse_ASLTeamTransactionReturn, ATR_TEAM_RETURN); \
	return TRANSACTION_OUTCOME_FAILURE; \
}

// Add this as the first argument to all team transaction helper function definitions
#define TEAM_TRH_ARGS ATR_ARGS, ASLTeamTransactionReturn *ATR_TEAM_RETURN

// Add this as the first argument to all team transaction helper function calls
#define TEAM_TRH_PASS_ARGS ATR_PASS_ARGS, ATR_TEAM_RETURN

///////////////////////////////////////////////////////////////////////////////////////////
// User Feedback and Logging
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_STRUCT;
typedef struct ASLTeamFeedbackMessage
{
	ContainerID iDestPlayerID;
	ContainerID iObjectID;
	ContainerID iSubjectID;
	char pcMessageKey[64];
	bool bError;
	TeamMode eMode;
	LootMode eLootMode;
	const char* eLootQuality; AST(POOL_STRING)
	int iDifficulty;
	char *pchStatusMessage; AST(ESTRING)
} ASLTeamFeedbackMessage;

AUTO_STRUCT;
typedef struct ASLTeamLogMessage
{
	char *pcEntName;
	ContainerID iTeamID;
	
	char *pcLogMessage; AST(ESTRING)
} ASLTeamLogMessage;

AUTO_ENUM;
typedef enum ASLTeamTransactionReturnActionType
{
	TEAM_TRANSACTION_RETURN_ACTION_TYPE_NONE,
	TEAM_TRANSACTION_RETURN_ACTION_TYPE_BECOME_XBOX_GAMING_SESSION_HOST,
	TEAM_TRANSACTION_RETURN_ACTION_TYPE_SEND_STATUS_UPDATE,
	TEAM_TRANSACTION_RETURN_ACTION_TYPE_UPDATE_GAME_SESSION,
	TEAM_TRANSACTION_RETURN_ACTION_TYPE_JOIN_TEAM_CHAT,
	TEAM_TRANSACTION_RETURN_ACTION_TYPE_LEAVE_TEAM_CHAT,
} ASLTeamTransactionReturnActionType;

AUTO_STRUCT;
typedef struct ASLTeamTransactionReturnAction
{
	// The type of action
	ASLTeamTransactionReturnActionType iActionType;

	// The team ID associated with this action
	ContainerID iTeamId;

	// The entity ID associated with this action
	ContainerID iEntId;

	// The account ID associated with this action
	U32 iAccountID;

} ASLTeamTransactionReturnAction;


AUTO_STRUCT;
typedef struct ASLTeamTransactionReturn
{
	EARRAY_OF(ASLTeamFeedbackMessage) eaSuccessFeedbackMessages;
	EARRAY_OF(ASLTeamFeedbackMessage) eaFailureFeedbackMessages;
	EARRAY_OF(ASLTeamLogMessage) eaSuccessLogMessages;
	EARRAY_OF(ASLTeamLogMessage) eaFailureLogMessages;
	EARRAY_OF(ASLTeamTransactionReturnAction) eaReturnActions;
} ASLTeamTransactionReturn;

AUTO_STRUCT;
typedef struct ASLTeamCBData
{
	char *pcActionType;
	bool bFeedback;
	const char* pchEOIMapRequest;
	const char* pchEOIMapVars;
	U32 uMapContainerID;
	U32 uMapPartitionID;
	const char *pcAllegiance;
	const char *pcSubAllegiance;
	SlowRemoteCommandID iCmdID;
	
	ContainerID iEntID;
	ContainerID iSubjectID;
	ContainerID iTeamID;
	ContainerID iOwningServerID;	//The game server that owns this team (only used for OwnedTeam Create CB)
	bool bTeamMembersMustBeOnOwningGameServer; // Whether we enforce that team members must be on the owning server.
	int  iWasDisconnected; // The EntID was disconnected. Used for ForceJoin CB
		
	U32 iPartitionID;
		//The partition that owns this team

	GameContentNodeRef *pGameContentNodeRef;
} ASLTeamCBData;

static ASLTeamFeedbackMessage *aslTeam_CreateFeedback(U32 iDestPlayerID, U32 iObjectID, U32 iSubjectID, const char *pcMessageKey, bool bError, TeamMode eMode, LootMode eLootMode, const char* eLootQuality, int iDifficulty, const char *pchStatusMessage)
{
	ASLTeamFeedbackMessage *pMessage = StructCreate(parse_ASLTeamFeedbackMessage);
	
	pMessage->iDestPlayerID = iDestPlayerID;
	pMessage->iObjectID = iObjectID;
	pMessage->iSubjectID = iSubjectID;
	strcpy_s(pMessage->pcMessageKey, 64, pcMessageKey);
	pMessage->bError = bError;
	pMessage->eMode = eMode;
	pMessage->eLootMode = eLootMode;
	pMessage->eLootQuality = eLootQuality;
	pMessage->iDifficulty = iDifficulty;
	estrCopy2(&pMessage->pchStatusMessage, pchStatusMessage);
	
	return pMessage;
}

static void aslTeam_AddReturnActionEx(ASLTeamTransactionReturn *pReturn, ASLTeamTransactionReturnActionType iActionType, ContainerID iTeamId, ContainerID iEntId, U32 iAccountID)
{
	// The return action
	ASLTeamTransactionReturnAction *pReturnAction = NULL;

	if (pReturn == NULL)
	{
		return;
	}

	// Create a return action
	pReturnAction = StructCreate(parse_ASLTeamTransactionReturnAction);

	pReturnAction->iActionType = iActionType;
	pReturnAction->iTeamId = iTeamId;
	pReturnAction->iEntId = iEntId;
	pReturnAction->iAccountID = iAccountID;

	// Add to the list
	eaPush(&pReturn->eaReturnActions, pReturnAction);
}

static void aslTeam_AddReturnAction(ASLTeamTransactionReturn *pReturn, ASLTeamTransactionReturnActionType iActionType, ContainerID iTeamId, ContainerID iEntId)
{
	aslTeam_AddReturnActionEx(pReturn, iActionType, iTeamId, iEntId, 0);
}

static void aslTeam_AddSuccessFeedback(ASLTeamTransactionReturn *pReturn, U32 iDestPlayerID, U32 iObjectID, U32 iSubjectID, const char *pcMessageKey, bool bError, TeamMode eMode, LootMode eLootMode, const char* eLootQuality, int iDifficulty, const char *pchStatusMessage)
{
	ASLTeamFeedbackMessage *pMessage = aslTeam_CreateFeedback(iDestPlayerID, iObjectID, iSubjectID, pcMessageKey, bError, eMode, eLootMode, eLootQuality, iDifficulty, pchStatusMessage);
	eaPush(&pReturn->eaSuccessFeedbackMessages, pMessage);
}

static void aslTeam_HandleOpenInstancingError(ASLTeamCBData* pData)
{
	const char* pchEOIMapRequest = pData->pchEOIMapRequest;

	if (pchEOIMapRequest && pchEOIMapRequest[0])
	{
		RemoteCommand_gslTeam_JoinOpenTeamByMap_Error(GLOBALTYPE_ENTITYPLAYER, pData->iSubjectID, pData->iSubjectID, pData->pchEOIMapRequest, pData->pchEOIMapVars);
	}
}

AUTO_TRANS_HELPER;
void aslTeam_trh_AddFeedbackAll(ASLTeamTransactionReturn *pReturn, ATH_ARG NOCONST(Team) *pTeamContainer, U32 iSpecialID, U32 iObjectID, U32 iSubjectID, const char *pcMessageKey, const char *pcSpecialMessageKey)
{
	S32 i;
	for (i = eaSize(&pTeamContainer->eaMembers)-1; i >= 0; i--) {
		if (pTeamContainer->eaMembers[i]->iEntID != iSpecialID) {
			aslTeam_AddSuccessFeedback(pReturn, pTeamContainer->eaMembers[i]->iEntID, iObjectID, iSubjectID, pcMessageKey, false, pTeamContainer->eMode, pTeamContainer->loot_mode, pTeamContainer->loot_mode_quality, pTeamContainer->iDifficulty, pTeamContainer->pchStatusMessage);
		}
	}
	if (iSpecialID) {
		aslTeam_AddSuccessFeedback(pReturn, iSpecialID, iObjectID, iSubjectID, pcSpecialMessageKey, false, pTeamContainer->eMode, pTeamContainer->loot_mode, pTeamContainer->loot_mode_quality, pTeamContainer->iDifficulty, pTeamContainer->pchStatusMessage);
	}
}

static void aslTeam_AddFailureFeedback(ASLTeamTransactionReturn *pReturn, U32 iDestPlayerID, U32 iObjectID, U32 iSubjectID, const char *pcMessageKey, bool bError, TeamMode eMode, LootMode eLootMode, const char* eLootQuality, int iDifficulty, const char *pchStatusMessage)
{
	ASLTeamFeedbackMessage *pMessage = aslTeam_CreateFeedback(iDestPlayerID, iObjectID, iSubjectID, pcMessageKey, bError, eMode, eLootMode, eLootQuality, iDifficulty, pchStatusMessage);
	eaPush(&pReturn->eaFailureFeedbackMessages, pMessage);
}

static void aslTeam_SendFeedbackStruct(const char *pcActionType, ASLTeamFeedbackMessage *pMessage)
{
	char *estrBuffer = NULL;
	estrStackCreate(&estrBuffer);
	estrConcatf(&estrBuffer, "TeamServer_ErrorType_%s", pcActionType);
	RemoteCommand_gslTeam_ResultMessage(GLOBALTYPE_ENTITYPLAYER, pMessage->iDestPlayerID, pMessage->iDestPlayerID, pMessage->iObjectID, pMessage->iSubjectID, estrBuffer, pMessage->pcMessageKey, pMessage->bError, pMessage->eMode, pMessage->eLootMode, pMessage->eLootQuality, pMessage->iDifficulty, pMessage->pchStatusMessage);
	estrDestroy(&estrBuffer);
}

static void aslTeam_SendError(U32 iDestPlayerID, U32 iSubjectID, SA_PARAM_NN_VALID const char *pcActionType, SA_PARAM_NN_VALID const char *pcMessageKey)
{
	if (iDestPlayerID) {
		char *estrBuffer = NULL;
		estrStackCreate(&estrBuffer);
		estrConcatf(&estrBuffer, "TeamServer_ErrorType_%s", pcActionType);
		RemoteCommand_gslTeam_ResultMessage(GLOBALTYPE_ENTITYPLAYER, iDestPlayerID, iDestPlayerID, iDestPlayerID, iSubjectID, estrBuffer, pcMessageKey, true, 0, 0, 0, 0, NULL);
		estrDestroy(&estrBuffer);
	}
}

static ASLTeamLogMessage *aslTeam_CreateLogMessage(const char *pcEntName, U32 iTeamID, const char *pcFormat, va_list vaArgs)
{
	ASLTeamLogMessage *pMessage = StructCreate(parse_ASLTeamLogMessage);
	
	pMessage->iTeamID = iTeamID;
	if (pcEntName && pcEntName[0]) {
		pMessage->pcEntName = StructAllocString(pcEntName);
	}
	estrConcatfv(&pMessage->pcLogMessage, pcFormat, vaArgs);
	
	return pMessage;
}

static void aslTeam_AddSuccessLogMessage(ASLTeamTransactionReturn *pReturn, const char *pcEntName, U32 iTeamID, const char *pcFormat, ...)
{
	ASLTeamLogMessage *pcMessage;
	
	VA_START(vaArgs, pcFormat);
	pcMessage = aslTeam_CreateLogMessage(pcEntName, iTeamID, pcFormat, vaArgs);
	VA_END();
	
	eaPush(&pReturn->eaSuccessLogMessages, pcMessage);
}

static void aslTeam_AddFailureLogMessage(ASLTeamTransactionReturn *pReturn, const char *pcEntName, U32 iTeamID, const char *pcFormat, ...)
{
	ASLTeamLogMessage *pcMessage;
	
	VA_START(vaArgs, pcFormat);
	pcMessage = aslTeam_CreateLogMessage(pcEntName, iTeamID, pcFormat, vaArgs);
	VA_END();
	
	eaPush(&pReturn->eaFailureLogMessages, pcMessage);
}

static void aslTeam_SaveLogMessageStruct(const char *pcActionType, ASLTeamLogMessage *pMessage)
{
	objLog(LOG_TEAM, GLOBALTYPE_TEAM, pMessage->iTeamID, 0, NULL, NULL, NULL, pcActionType, NULL, " %s: %s ", pMessage->pcEntName, pMessage->pcLogMessage);
}

static void aslTeam_SaveLogMessage(const char *pcActionType, const char *pcEntName, U32 iTeamID, const char *pcFormat, ...)
{
	char *estrBuffer = NULL;
	estrStackCreate(&estrBuffer);
	
	VA_START(vaArgs, pcFormat);
	estrConcatfv(&estrBuffer, pcFormat, vaArgs);
	VA_END();
	if (pcEntName && pcEntName[0]) {
		objLog(LOG_TEAM, GLOBALTYPE_TEAM, iTeamID, 0, NULL, NULL, NULL, pcActionType, NULL, " %s: %s ", pcEntName, estrBuffer);
	} else {
		objLog(LOG_TEAM, GLOBALTYPE_TEAM, iTeamID, 0, NULL, NULL, NULL, pcActionType, NULL, " %s ", estrBuffer);
	}
	estrDestroy(&estrBuffer);
}

static ASLTeamCBData *aslTeam_MakeCBData(char *pcActionType)
{
	ASLTeamCBData *pData = StructCreate(parse_ASLTeamCBData);
	if (pcActionType && pcActionType[0]) {
		pData->pcActionType = StructAllocString(pcActionType);
	}
	pData->bFeedback = true;
	return pData;
}

static void aslTeam_MemberJoinTeamChat(U32 uTeamID, U32 uAccountID, U32 uEntID)
{
	char pchTeamChannelName[512];
	team_MakeTeamChannelNameFromID(SAFESTR(pchTeamChannelName), uTeamID);
	RemoteCommand_ChatServerJoinOrCreateChannel_Special(GLOBALTYPE_CHATSERVER, 0, uAccountID, uEntID, 
		pchTeamChannelName, CHANNEL_SPECIAL_TEAM);
}

static void aslTeam_MemberRemoveFromTeamChat(U32 uTeamID, U32 uAccountID, U32 uEntID)
{
	char pchTeamChannelName[512];
	team_MakeTeamChannelNameFromID(SAFESTR(pchTeamChannelName), uTeamID);
	RemoteCommand_ChatServerLeaveChannel_Special(GLOBALTYPE_CHATSERVER, 0, uAccountID, uEntID, pchTeamChannelName);
}

static void aslTeam_RemoteCommand_CB(TransactionReturnVal *pReturn, ASLTeamCBData *pData)
{
	Team *pTeam = NULL;
	bool bUpdatedGameSession = false;

	if (pData->bFeedback) 
	{
		S32 i;
		ASLTeamTransactionReturn *pTeamReturn = StructCreate(parse_ASLTeamTransactionReturn);
		char *pcTeamReturnParsed = objAutoTransactionGetResult(pReturn);
		ASLTeamFeedbackMessage **eaFeedbackMessages;
		ASLTeamLogMessage **eaLogMessages;
		
		ParserReadText(pcTeamReturnParsed, parse_ASLTeamTransactionReturn, pTeamReturn, 0);
		if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS) {
			eaFeedbackMessages = pTeamReturn->eaSuccessFeedbackMessages;
			eaLogMessages = pTeamReturn->eaSuccessLogMessages;
		} else {
			eaFeedbackMessages = pTeamReturn->eaFailureFeedbackMessages;
			eaLogMessages = pTeamReturn->eaFailureLogMessages;
		}
		
		for (i = 0; i < eaSize(&eaFeedbackMessages); i++) {
			aslTeam_SendFeedbackStruct(pData->pcActionType, eaFeedbackMessages[i]);
		}
		for (i = 0; i < eaSize(&eaLogMessages); i++) {
			aslTeam_SaveLogMessageStruct(pData->pcActionType, eaLogMessages[i]);
		}

		// Process all return actions
		for (i = 0; i <eaSize(&pTeamReturn->eaReturnActions); i++)
		{
			if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS &&
				pTeamReturn->eaReturnActions[i]->iActionType == TEAM_TRANSACTION_RETURN_ACTION_TYPE_BECOME_XBOX_GAMING_SESSION_HOST)
			{
				pTeam = aslTeam_GetTeam(pTeamReturn->eaReturnActions[i]->iTeamId);

				// Call the remote command on the game server to make the team leader the XBOX gaming session host
				RemoteCommand_gslTeam_cmd_xBoxSessionCreate(GLOBALTYPE_ENTITYPLAYER, 
					pTeamReturn->eaReturnActions[i]->iEntId, 
					pTeamReturn->eaReturnActions[i]->iTeamId, 
					pTeamReturn->eaReturnActions[i]->iEntId);
			}
			else if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS &&
				pTeamReturn->eaReturnActions[i]->iActionType == TEAM_TRANSACTION_RETURN_ACTION_TYPE_SEND_STATUS_UPDATE)
			{
				// Call the remote command on the game server to cause it to update the team status on the chatserver
				RemoteCommand_gslTeam_cmd_SendStatusUpdate(GLOBALTYPE_ENTITYPLAYER, 
					pTeamReturn->eaReturnActions[i]->iEntId,
					pTeamReturn->eaReturnActions[i]->iEntId);
			}
		}
		
		StructDestroy(parse_ASLTeamTransactionReturn, pTeamReturn);
	}
	
	// Return actions which need to happen regardless of feedback flag
	{
		S32 i;
		ASLTeamTransactionReturn *pTeamReturn = StructCreate(parse_ASLTeamTransactionReturn);
		char *pcTeamReturnParsed = objAutoTransactionGetResult(pReturn);

		ParserReadText(pcTeamReturnParsed, parse_ASLTeamTransactionReturn, pTeamReturn, 0);		

		// Process all return actions
		for (i = 0; i <eaSize(&pTeamReturn->eaReturnActions); i++)
		{
			if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS &&
				pTeamReturn->eaReturnActions[i]->iActionType == TEAM_TRANSACTION_RETURN_ACTION_TYPE_JOIN_TEAM_CHAT)
			{
				aslTeam_MemberJoinTeamChat(pTeamReturn->eaReturnActions[i]->iTeamId,
										   pTeamReturn->eaReturnActions[i]->iAccountID,
										   pTeamReturn->eaReturnActions[i]->iEntId);
			}
			else if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS &&
				pTeamReturn->eaReturnActions[i]->iActionType == TEAM_TRANSACTION_RETURN_ACTION_TYPE_LEAVE_TEAM_CHAT)
			{
				aslTeam_MemberRemoveFromTeamChat(pTeamReturn->eaReturnActions[i]->iTeamId,
												 pTeamReturn->eaReturnActions[i]->iAccountID,
												 pTeamReturn->eaReturnActions[i]->iEntId);
			}
		}

		StructDestroy(parse_ASLTeamTransactionReturn, pTeamReturn);
	}
	
	StructDestroy(parse_ASLTeamCBData, pData);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Transaction Helpers
///////////////////////////////////////////////////////////////////////////////////////////
// All commonly repeated checks, and all changes, are stuck in these helper functions.
// The ones that make changes contain no validation. It is the job of the main transaction
// functions to do the appropriate validation (usually by calling the right check
// functions) before calling the helpers that make the changes.
///////////////////////////////////////////////////////////////////////////////////////////

// Get the entity's display name
AUTO_TRANS_HELPER;
const char *aslTeam_trh_GetDisplayName(TEAM_TRH_ARGS, ATH_ARG NOCONST(Entity) *pEnt)
{
	if (pEnt->pSaved->savedName && pEnt->pSaved->savedName[0]) {
		return pEnt->pSaved->savedName;
	}
	return pEnt->debugName;
}

// Make a TeamMember struct from an entity
AUTO_TRANS_HELPER;
NOCONST(TeamMember) *aslTeam_trh_MakeTeamMember(TEAM_TRH_ARGS, ATH_ARG NOCONST(Entity) *pEnt)
{
	char idBuf[128];
	NOCONST(TeamMember) *pMember = StructCreateNoConst(parse_TeamMember);
	pMember->iEntID = pEnt->myContainerID;
	SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYPLAYER), ContainerIDToString(pEnt->myContainerID, idBuf), pMember->hEnt);
	pMember->iJoinTime = timeSecondsSince2000();
	pMember->pcName = StructAllocString(aslTeam_trh_GetDisplayName(TEAM_TRH_PASS_ARGS, pEnt));
	pMember->pcAccountHandle = StructAllocString(pEnt->pPlayer->publicAccountName);
	pMember->pcLogName = StructAllocString(pEnt->debugName);
	return pMember;
}

// Find a given team member and return the TeamMember index
AUTO_TRANS_HELPER;
S32 aslTeam_trh_FindMemberIdx(TEAM_TRH_ARGS, U32 iEntID, ATH_ARG NOCONST(Team) *pTeamContainer)
{
	int i;
	for (i = eaSize(&pTeamContainer->eaMembers)-1; i >= 0; i--) {
		if (pTeamContainer->eaMembers[i]->iEntID == iEntID) {
			return i;
		}
	}
	return -1;
}

// Find a given team member and return the TeamMember struct
AUTO_TRANS_HELPER;
NOCONST(TeamMember) *aslTeam_trh_FindMember(TEAM_TRH_ARGS, U32 iEntID, ATH_ARG NOCONST(Team) *pTeamContainer)
{
	return eaGet(&pTeamContainer->eaMembers, aslTeam_trh_FindMemberIdx(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer));
}

// Find a given team index and return the TeamMember struct
AUTO_TRANS_HELPER;
S32 aslTeam_trh_FindInviteIdx(TEAM_TRH_ARGS, U32 iEntID, ATH_ARG NOCONST(Team) *pTeamContainer)
{
	int i;
	for (i = eaSize(&pTeamContainer->eaInvites)-1; i >= 0; i--) {
		if (pTeamContainer->eaInvites[i]->iEntID == iEntID) {
			return i;
		}
	}
	return -1;
}

// Find a given team index and return the TeamMember struct
AUTO_TRANS_HELPER;
NOCONST(TeamMember) *aslTeam_trh_FindInvite(TEAM_TRH_ARGS, U32 iEntID, ATH_ARG NOCONST(Team) *pTeamContainer)
{
	return eaGet(&pTeamContainer->eaInvites, aslTeam_trh_FindInviteIdx(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer));
}

// Find a given team request and return the TeamMember index
AUTO_TRANS_HELPER;
S32 aslTeam_trh_FindRequestIdx(TEAM_TRH_ARGS, U32 iEntID, ATH_ARG NOCONST(Team) *pTeamContainer)
{
	int i;
	for (i = eaSize(&pTeamContainer->eaRequests)-1; i >= 0; i--) {
		if (pTeamContainer->eaRequests[i]->iEntID == iEntID) {
			return i;
		}
	}
	return -1;
}

// Find a given team request and return the TeamMember struct
AUTO_TRANS_HELPER;
NOCONST(TeamMember) *aslTeam_trh_FindRequest(TEAM_TRH_ARGS, U32 iEntID, ATH_ARG NOCONST(Team) *pTeamContainer)
{
	return eaGet(&pTeamContainer->eaRequests, aslTeam_trh_FindRequestIdx(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer));
}

// Check if there is a leader, and if the leader is still on the team
// If this check fails, set the first member of the team to the leader
AUTO_TRANS_HELPER;
void aslTeam_trh_UpdateLeader(TEAM_TRH_ARGS, ATH_ARG NOCONST(Team) *pTeamContainer, bool bSilent)
{
	S32 i;
	if (eaSize(&pTeamContainer->eaMembers) > 0) {
		if (ISNULL(pTeamContainer->pLeader)) {
			pTeamContainer->pLeader = StructCreateNoConst(parse_TeamMember);
		}
		if (!aslTeam_trh_FindMember(TEAM_TRH_PASS_ARGS, pTeamContainer->pLeader->iEntID, pTeamContainer)) {
			pTeamContainer->pLeader->iEntID = pTeamContainer->eaMembers[0]->iEntID;
			pTeamContainer->pLeader->iJoinTime = timeSecondsSince2000();
			pTeamContainer->iVersion++;
			
			// Can't use aslTeam_trh_AddFeedbackAll because we aren't using entities
			if (!bSilent && eaSize(&pTeamContainer->eaMembers) > 1) {
				for (i = eaSize(&pTeamContainer->eaMembers)-1; i >= 0; i--) {
					if (pTeamContainer->eaMembers[i]->iEntID != pTeamContainer->pLeader->iEntID) {
						aslTeam_AddSuccessFeedback(ATR_TEAM_RETURN, pTeamContainer->eaMembers[i]->iEntID, 0, pTeamContainer->pLeader->iEntID, "TeamServer_Team_Promoted", false, 0, 0, 0, 0, NULL);
					}
				}
				aslTeam_AddSuccessFeedback(ATR_TEAM_RETURN, pTeamContainer->pLeader->iEntID, 0, 0, "TeamServer_Subject_Promoted", false, 0, 0, 0, 0, NULL);
			}
			aslTeam_AddSuccessLogMessage(ATR_TEAM_RETURN, pTeamContainer->eaMembers[0]->pcLogName, pTeamContainer->iContainerID, "Automatically promoted to team leader.");

			// Set the session info to NULL if necessary
			if (pTeamContainer->pXSessionInfo != NULL)
			{
				StructDestroyNoConst(parse_CrypticXSessionInfo, pTeamContainer->pXSessionInfo);
				pTeamContainer->pXSessionInfo = NULL;
			}
			
			aslTeam_AddReturnAction(ATR_TEAM_RETURN, TEAM_TRANSACTION_RETURN_ACTION_TYPE_BECOME_XBOX_GAMING_SESSION_HOST, 
				pTeamContainer->iContainerID, pTeamContainer->pLeader->iEntID);
		}
	}
}

AUTO_TRANS_HELPER;
int aslTeam_trh_NumTotalMembers(TEAM_TRH_ARGS, ATH_ARG NOCONST(Team) *pTeamContainer)
{
	return(eaSize(&pTeamContainer->eaMembers) + eaSize(&pTeamContainer->eaDisconnecteds));
}

AUTO_TRANS_HELPER;
int aslTeam_trh_NumPresentMembers(TEAM_TRH_ARGS, ATH_ARG NOCONST(Team) *pTeamContainer)
{
	return(eaSize(&pTeamContainer->eaMembers));

}

// 'Move' a team member to the disconnected list. (Create a stubMember that represents it). 
AUTO_TRANS_HELPER;
void aslTeam_trh_CopyToDisconnecteds(TEAM_TRH_ARGS, ATH_ARG NOCONST(Team) *pTeamContainer, ATH_ARG NOCONST(TeamMember) *pTeamMember)
{
	// Create Stub Member

	NOCONST(StubTeamMember) *pStubMember = StructCreateNoConst(parse_StubTeamMember);
	pStubMember->iEntID = pTeamMember->iEntID;
	pStubMember->iStubTime = timeSecondsSince2000();
	pStubMember->pcName = StructAllocString(pTeamMember->pcName);
	pStubMember->pcAccountHandle = StructAllocString(pTeamMember->pcAccountHandle);
	pStubMember->pchClassName = pTeamMember->pchClassName;

	// Put on the disconnecteds list
	
	eaPush(&pTeamContainer->eaDisconnecteds, pStubMember);
	pTeamContainer->iVersion++;
}

// Remove a Stub Member from the disconnecteds if it happens to match.
AUTO_TRANS_HELPER;
bool aslTeam_trh_RemoveFromDisconnecteds(TEAM_TRH_ARGS, ATH_ARG NOCONST(Team) *pTeamContainer, U32 uEntID)
{
	int i;
	
	// Look for the stub that has matching ID

	for (i = eaSize(&pTeamContainer->eaDisconnecteds)-1; i >= 0; i--)
	{
		if (pTeamContainer->eaDisconnecteds[i]->iEntID==uEntID)
		{
			// Got it. Just remove it.
			
			NOCONST(StubTeamMember) *pStubMember = eaRemove(&pTeamContainer->eaDisconnecteds, i);
			pTeamContainer->iVersion++;
			StructDestroyNoConst(parse_StubTeamMember, pStubMember);
			return(true);
		}
	}
	// Not there. Don't worry about it.
	return(false);
}

// Adjusts a team's allegiance requirements for an added team member.
AUTO_TRANS_HELPER;
void aslTeam_trh_AdjustAllegiance(TEAM_TRH_ARGS, ATH_ARG NOCONST(Team) *pTeamContainer, ATH_ARG NOCONST(Entity) *pEnt)
{
	const char *playerAllegianceString = REF_STRING_FROM_HANDLE(pEnt->hAllegiance);
	const char *playerSubAllegianceString = REF_STRING_FROM_HANDLE(pEnt->hSubAllegiance);
	bool bSubAllegiance = false;

	if (pTeamContainer->iGameServerOwnerID)
	{
		// If this is a local team, then don't adjust allegiance
		return;
	}

	if (!pTeamContainer->pcAllegiance || *pTeamContainer->pcAllegiance == '\0' || !playerAllegianceString)
	{
		// Either the team has no allegiance, no suballegiance, or the player has no allegiance, so don't adjust team allegiance
		return;
	}

	bSubAllegiance = (pTeamContainer->pcSubAllegiance && *pTeamContainer->pcSubAllegiance != '\0');

	// If the player's allegiance matches the team's allegiance, then the team cannot require a sub allegiance match for teaming
	if (!stricmp(pTeamContainer->pcAllegiance, playerAllegianceString))
	{
		// If the player's sub allegiance does not match the team's sub allegiance, then the team can no longer have a sub allegiance
		if (bSubAllegiance && (!playerSubAllegianceString || stricmp(pTeamContainer->pcSubAllegiance, playerSubAllegianceString)))
		{
			pTeamContainer->pcSubAllegiance = NULL;
		}
	}
	// Else, if the player's allegiance does not match the team's allegiance, but it does match the team's sub allegiance,
	// then the team will require a sub allegiance match for teaming
	else if (bSubAllegiance && stricmp(pTeamContainer->pcSubAllegiance, playerAllegianceString))
	{
		pTeamContainer->bRequireSubAllegianceMatch = true;
	}
	// Else if team has no suballegiance, and the player's sub allegiance matches the team's allegiance,
	// then the team's sub allegiance needs to be set to its current allegiance, and it's new main
	// allegiance needs to be set to this player's allegiance
	else if (!bSubAllegiance && playerSubAllegianceString && !stricmp(pTeamContainer->pcAllegiance, playerSubAllegianceString))
	{
		pTeamContainer->pcSubAllegiance = allocAddString(pTeamContainer->pcAllegiance);
		pTeamContainer->pcAllegiance = allocAddString(playerAllegianceString);
	}
}

// Resets a team's allegiance, used to relax allegiance restrictions as much as possible when a team member leaves
AUTO_TRANS_HELPER;
void aslTeam_trh_ResetAllegiance(TEAM_TRH_ARGS, ATH_ARG NOCONST(Team) *pTeamContainer)
{
	int i;
	bool bAllegianceSet = false;

	for (i = eaSize(&pTeamContainer->eaMembers)-1; i >= 0; i--)
	{
		Entity *pTeamMemberEntity = GET_REF(pTeamContainer->eaMembers[i]->hEnt);
		if (pTeamMemberEntity)
		{
			// If this is not the first valid member of the team, adjust the team's allegiance based on the same rules we use for team invites
			if (bAllegianceSet)
			{
				aslTeam_trh_AdjustAllegiance(TEAM_TRH_PASS_ARGS, pTeamContainer, CONTAINER_NOCONST(Entity, pTeamMemberEntity));
			}
			// Else this is the first valid member of the team, so set the team's allegiance and sub-allegiance to this team member's
			else
			{
				const char *playerAllegianceString = REF_STRING_FROM_HANDLE(pTeamMemberEntity->hAllegiance);
				const char *playerSubAllegianceString = REF_STRING_FROM_HANDLE(pTeamMemberEntity->hSubAllegiance);

				pTeamContainer->pcAllegiance = playerAllegianceString && playerAllegianceString[0] ? allocAddString(playerAllegianceString) : NULL;
				pTeamContainer->pcSubAllegiance = playerSubAllegianceString && playerSubAllegianceString[0] ? allocAddString(playerSubAllegianceString) : NULL;

				bAllegianceSet = true;
			}
		}
	}
}

// Add an entity to the team
AUTO_TRANS_HELPER;
void aslTeam_trh_AddMember(TEAM_TRH_ARGS, ATH_ARG NOCONST(Team) *pTeamContainer, ATH_ARG NOCONST(Entity) *pSubject, bool bEOIRequest)
{
	NOCONST(TeamMember) *pTeamMember = aslTeam_trh_MakeTeamMember(TEAM_TRH_PASS_ARGS, pSubject);

	// Set the joined voice chat flag
 	if (NONNULL(pSubject) && NONNULL(pSubject->pPlayer))
 	{
 		pTeamMember->bJoinedVoiceChat = pSubject->pPlayer->bAutoJoinTeamVoiceChat;		
 	}

	eaPush(&pTeamContainer->eaMembers, pTeamMember);
	pTeamContainer->iVersion++;
	pTeamContainer->uBadLogoutTime = 0;	// someone has joined the team, clear the bad logout time.

	if (gConf.bAllowSuballegianceTeaming)
	{
		aslTeam_trh_AdjustAllegiance(TEAM_TRH_PASS_ARGS, pTeamContainer, pSubject);
	}

	if (gConf.bManageTeamDisconnecteds)
	{
		// Move person from disconnected to connected
		aslTeam_trh_RemoveFromDisconnecteds(TEAM_TRH_PASS_ARGS, pTeamContainer, pTeamMember->iEntID);
	}
	
	if (ISNULL(pSubject->pTeam)) {
		pSubject->pTeam = StructCreateNoConst(parse_PlayerTeam);
	}

	pSubject->pTeam->iTeamID = pTeamContainer->iContainerID;
	pSubject->pTeam->eState = TeamState_Member;
	pSubject->pTeam->iRejoinID = 0;
	
	
	if (bEOIRequest)
		aslTeam_trh_AddFeedbackAll(ATR_TEAM_RETURN, pTeamContainer, pSubject->myContainerID, pSubject->myContainerID, pSubject->myContainerID, "TeamServer_Team_Joined_EOI", "TeamServer_Subject_Joined_EOI");
	else
		aslTeam_trh_AddFeedbackAll(ATR_TEAM_RETURN, pTeamContainer, pSubject->myContainerID, pSubject->myContainerID, pSubject->myContainerID, "TeamServer_Team_Joined", "TeamServer_Subject_Joined");
	aslTeam_AddSuccessFeedback(ATR_TEAM_RETURN, pSubject->myContainerID, pSubject->myContainerID, pSubject->myContainerID, "TeamServer_Team_LootModeChanged", false, pTeamContainer->eMode, pTeamContainer->loot_mode, pTeamContainer->loot_mode_quality, pTeamContainer->iDifficulty, pTeamContainer->pchStatusMessage);
	aslTeam_AddSuccessFeedback(ATR_TEAM_RETURN, pSubject->myContainerID, pSubject->myContainerID, pSubject->myContainerID, "TeamServer_Team_LootModeQualityChanged", false, pTeamContainer->eMode, pTeamContainer->loot_mode, pTeamContainer->loot_mode_quality, pTeamContainer->iDifficulty, pTeamContainer->pchStatusMessage);
	aslTeam_AddSuccessLogMessage(ATR_TEAM_RETURN, pSubject->debugName, pTeamContainer->iContainerID, "Joined team. Team size now %d.", aslTeam_trh_NumTotalMembers(TEAM_TRH_PASS_ARGS, pTeamContainer));

	aslTeam_AddReturnAction(ATR_TEAM_RETURN, TEAM_TRANSACTION_RETURN_ACTION_TYPE_UPDATE_GAME_SESSION, 
		pTeamContainer->iContainerID, pSubject->myContainerID);

	if (NONNULL(pSubject->pPlayer))
	{
		aslTeam_AddReturnActionEx(ATR_TEAM_RETURN, TEAM_TRANSACTION_RETURN_ACTION_TYPE_JOIN_TEAM_CHAT, 
			pTeamContainer->iContainerID, pSubject->myContainerID, pSubject->pPlayer->accountID);
	}

}

// Add an entity to the invite list
AUTO_TRANS_HELPER;
void aslTeam_trh_AddInvite(TEAM_TRH_ARGS, U32 iEntID, ATH_ARG NOCONST(Team) *pTeamContainer, ATH_ARG NOCONST(Entity) *pSubject)
{
	NOCONST(TeamMember) *pMember = aslTeam_trh_FindMember(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer);
	if (pMember) {
		eaPush(&pTeamContainer->eaInvites, aslTeam_trh_MakeTeamMember(TEAM_TRH_PASS_ARGS, pSubject));
		pTeamContainer->iVersion++;
		
		if (ISNULL(pSubject->pTeam)) {
			pSubject->pTeam = StructCreateNoConst(parse_PlayerTeam);
		}
		pSubject->pTeam->iTeamID = pTeamContainer->iContainerID;
		pSubject->pTeam->eState = TeamState_Invitee;
		pSubject->pTeam->iRejoinID = 0;
		pSubject->pTeam->pcInviterHandle = StructAllocString(pMember->pcAccountHandle);
		pSubject->pTeam->pcInviterName = StructAllocString(pMember->pcName);

		aslTeam_AddSuccessFeedback(ATR_TEAM_RETURN, iEntID, iEntID, pSubject->myContainerID, "TeamServer_Success_Invited", false, 0, 0, 0, 0, NULL);
		aslTeam_AddSuccessFeedback(ATR_TEAM_RETURN, pSubject->myContainerID, iEntID, pSubject->myContainerID, "TeamServer_Subject_Invited", false, 0, 0, 0, 0, NULL);
		aslTeam_AddSuccessLogMessage(ATR_TEAM_RETURN, pMember->pcLogName, pTeamContainer->iContainerID, "Invited %s to team.", pSubject->debugName);
	}
}

// Add an entity to the request list
AUTO_TRANS_HELPER;
void aslTeam_trh_AddRequest(TEAM_TRH_ARGS, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(Team) *pTeamContainer)
{
	eaPush(&pTeamContainer->eaRequests, aslTeam_trh_MakeTeamMember(TEAM_TRH_PASS_ARGS, pEnt));
	pTeamContainer->iVersion++;
	
	if (ISNULL(pEnt->pTeam)) {
		pEnt->pTeam = StructCreateNoConst(parse_PlayerTeam);
	}
	pEnt->pTeam->iTeamID = pTeamContainer->iContainerID;
	pEnt->pTeam->eState = TeamState_Requester;
	pEnt->pTeam->iRejoinID = 0;
	
	aslTeam_AddSuccessFeedback(ATR_TEAM_RETURN, pEnt->myContainerID, pEnt->myContainerID, 0, "TeamServer_Success_Requested", false, 0, 0, 0, 0, NULL);
	aslTeam_AddSuccessLogMessage(ATR_TEAM_RETURN, pEnt->debugName, pTeamContainer->iContainerID, "Requested to join team.");

	aslTeam_AddReturnAction(ATR_TEAM_RETURN, TEAM_TRANSACTION_RETURN_ACTION_TYPE_UPDATE_GAME_SESSION, 
		pTeamContainer->iContainerID, pEnt->myContainerID);
}

// Remove an entity from the team
// Return a boolean representing success
AUTO_TRANS_HELPER;
bool aslTeam_trh_RemoveMember(TEAM_TRH_ARGS, U32 iEntID, ATH_ARG NOCONST(Team) *pTeamContainer, ATH_ARG NOCONST(Entity) *pSubject)
{
	int iSubjectID = NONNULL(pSubject) ? pSubject->myContainerID : 0;
	NOCONST(TeamMember) *pMember = aslTeam_trh_FindMember(TEAM_TRH_PASS_ARGS, iSubjectID, pTeamContainer);
	S32 iSubjectMemberIdx = aslTeam_trh_FindMemberIdx(TEAM_TRH_PASS_ARGS, iSubjectID, pTeamContainer);
	if (iSubjectMemberIdx >= 0) {
		NOCONST(TeamMember) *pSubjectMember = eaRemove(&pTeamContainer->eaMembers, iSubjectMemberIdx);
		pTeamContainer->iVersion++;
		if (NONNULL(pSubject) && (pSubject->pTeam->iTeamID == pTeamContainer->iContainerID) && (pSubject->pTeam->eState == TeamState_Member)) {
			pSubject->pTeam->iTeamID = 0;
		}
		if (pTeamContainer->pChampion && pTeamContainer->pChampion->iEntID == (ContainerID)iSubjectID) {
			StructDestroyNoConst(parse_TeamMember, pTeamContainer->pChampion);
			pTeamContainer->pChampion = NULL;
		}
		
		if (iEntID != (U32)iSubjectID) {
			char *estrNameBuff = NULL;
			aslTeam_trh_AddFeedbackAll(ATR_TEAM_RETURN, pTeamContainer, iSubjectID, iEntID, iSubjectID, "TeamServer_Team_Kicked", "TeamServer_Subject_Kicked");
			aslTeam_AddSuccessLogMessage(ATR_TEAM_RETURN, pMember->pcLogName, pTeamContainer->iContainerID, "Kicked %s from team. Team size now %d.", NONNULL(pSubject) ? pSubject->debugName : "0", aslTeam_trh_NumTotalMembers(TEAM_TRH_PASS_ARGS, pTeamContainer));
		} else {
			// Disband if we have zero present members. Don't keep teams that are only disconnecteds.
			if (aslTeam_trh_NumPresentMembers(TEAM_TRH_PASS_ARGS, pTeamContainer) == 0) {
				aslTeam_AddSuccessFeedback(ATR_TEAM_RETURN, iSubjectID, iEntID, 0, "TeamServer_Subject_Disbanded", false, 0, 0, 0, 0, NULL);
			} else {
				aslTeam_trh_AddFeedbackAll(ATR_TEAM_RETURN, pTeamContainer, iSubjectID, iEntID, 0, "TeamServer_Team_Left", "TeamServer_Subject_Left");
			}
			aslTeam_AddSuccessLogMessage(ATR_TEAM_RETURN, pMember->pcLogName, pTeamContainer->iContainerID, "Left team. Team size now %d.", aslTeam_trh_NumTotalMembers(TEAM_TRH_PASS_ARGS, pTeamContainer));
		}

		aslTeam_AddReturnAction(ATR_TEAM_RETURN, TEAM_TRANSACTION_RETURN_ACTION_TYPE_UPDATE_GAME_SESSION, 
			pTeamContainer->iContainerID, 0);
		
		if (NONNULL(pSubject->pPlayer))
		{
			aslTeam_AddReturnActionEx(ATR_TEAM_RETURN, TEAM_TRANSACTION_RETURN_ACTION_TYPE_LEAVE_TEAM_CHAT, 
				pTeamContainer->iContainerID, pSubject->myContainerID, pSubject->pPlayer->accountID);
		}

		aslTeam_trh_UpdateLeader(TEAM_TRH_PASS_ARGS, pTeamContainer, false);

		if (gConf.bAllowSuballegianceTeaming)
		{
			aslTeam_trh_ResetAllegiance(TEAM_TRH_PASS_ARGS, pTeamContainer);
		}

		StructDestroyNoConst(parse_TeamMember, pSubjectMember);
		return true;
	}
	return false;
}

// Remove an entity from the invite list
// Return a boolean representing success
AUTO_TRANS_HELPER;
bool aslTeam_trh_RemoveInvite(TEAM_TRH_ARGS, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(Team) *pTeamContainer)
{
	S32 iMemberIdx = aslTeam_trh_FindInviteIdx(TEAM_TRH_PASS_ARGS, pEnt->myContainerID, pTeamContainer);
	if (iMemberIdx >= 0) {
		StructDestroyNoConst(parse_TeamMember, pTeamContainer->eaInvites[iMemberIdx]);
		eaRemove(&pTeamContainer->eaInvites, iMemberIdx);
		pTeamContainer->iVersion++;
		if (pEnt->pTeam->iTeamID == pTeamContainer->iContainerID && pEnt->pTeam->eState == TeamState_Invitee) {
			pEnt->pTeam->iTeamID = 0;
			if (pEnt->pTeam->pcInviterName) {
				StructFreeStringSafe(&pEnt->pTeam->pcInviterName);
			}
		}
		return true;
	}
	return false;
}

// Remove an entity from the request list
// Return a boolean representing success
AUTO_TRANS_HELPER;
bool aslTeam_trh_RemoveRequest(TEAM_TRH_ARGS, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(Team) *pTeamContainer)
{
	S32 iMemberIdx = aslTeam_trh_FindRequestIdx(TEAM_TRH_PASS_ARGS, pEnt->myContainerID, pTeamContainer);
	if (iMemberIdx >= 0) {
		StructDestroyNoConst(parse_TeamMember, pTeamContainer->eaRequests[iMemberIdx]);
		eaRemove(&pTeamContainer->eaRequests, iMemberIdx);
		pTeamContainer->iVersion++;
		if (pEnt->pTeam->iTeamID == pTeamContainer->iContainerID && pEnt->pTeam->eState == TeamState_Requester) {
			pEnt->pTeam->iTeamID = 0;
		}

		aslTeam_AddReturnAction(ATR_TEAM_RETURN, TEAM_TRANSACTION_RETURN_ACTION_TYPE_UPDATE_GAME_SESSION, 
			pTeamContainer->iContainerID, pEnt->myContainerID);

		return true;
	}
	return false;
}


// Set a team member's state to be logged out
AUTO_TRANS_HELPER;
void aslTeam_trh_SetAsLoggedOut(TEAM_TRH_ARGS, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(Team) *pTeamContainer, U32 uBadLogout)
{
	if (gConf.bManageTeamDisconnecteds)
	{
		//  'Move' to disconnecteds
		NOCONST(TeamMember) *pTeamMember = aslTeam_trh_FindMember(TEAM_TRH_PASS_ARGS, pEnt->myContainerID, pTeamContainer);
		aslTeam_trh_CopyToDisconnecteds(TEAM_TRH_PASS_ARGS, pTeamContainer, pTeamMember);
	}

	aslTeam_trh_RemoveMember(TEAM_TRH_PASS_ARGS, pEnt->myContainerID, pTeamContainer, pEnt);
	pEnt->pTeam->eState = TeamState_LoggedOut;
	pEnt->pTeam->iTeamID = 0;
	pEnt->pTeam->iRejoinID = pTeamContainer->iContainerID;
	pEnt->pTeam->iLogoutTime = timeSecondsSince2000();
	if (uBadLogout)
	{
		pTeamContainer->uBadLogoutTime = timeSecondsSince2000();
		aslTeam_AddSuccessLogMessage(ATR_TEAM_RETURN, pEnt->debugName, pTeamContainer->iContainerID, "Logged out improperly.");
	}
	else
	{
		pTeamContainer->uBadLogoutTime = 0;
		aslTeam_AddSuccessLogMessage(ATR_TEAM_RETURN, pEnt->debugName, pTeamContainer->iContainerID, "Logged out.");
	}
}


AUTO_TRANS_HELPER;
bool aslTeam_trh_IsAnyMemberSidekicked(TEAM_TRH_ARGS, ATH_ARG NOCONST(Team) *pTeamContainer)
{
	S32 i;
	for (i = eaSize(&pTeamContainer->eaMembers)-1; i >= 0; i--)
	{
		if (pTeamContainer->eaMembers[i]->bSidekicked)
		{
			return true;
		}
	}
	return false;
}

// Set an entity as the leader
AUTO_TRANS_HELPER;
void aslTeam_trh_SetLeader(TEAM_TRH_ARGS, U32 iEntID, ATH_ARG NOCONST(Team) *pTeamContainer, U32 iSubjectID)
{
	NOCONST(TeamMember) *pMember = aslTeam_trh_FindMember(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer);
	NOCONST(TeamMember) *pSubjectMember = aslTeam_trh_FindMember(TEAM_TRH_PASS_ARGS, iSubjectID, pTeamContainer);
	if (pMember && pSubjectMember) {
		if (ISNULL(pTeamContainer->pLeader)) {
			pTeamContainer->pLeader = StructCreateNoConst(parse_TeamMember);
		}
		else if (ISNULL(pTeamContainer->pChampion)) {
			if (aslTeam_trh_IsAnyMemberSidekicked(TEAM_TRH_PASS_ARGS, pTeamContainer)) {
				// If anyone was sidekicking, set the champion as the old leader
				pTeamContainer->pChampion = StructCreateNoConst(parse_TeamMember);
				pTeamContainer->pChampion->iEntID = pTeamContainer->pLeader->iEntID;
				pTeamContainer->pChampion->iJoinTime = pTeamContainer->pLeader->iJoinTime;
			}
		}

		pTeamContainer->pLeader->iEntID = iSubjectID;
		pTeamContainer->pLeader->iJoinTime = timeSecondsSince2000();
		
		aslTeam_trh_AddFeedbackAll(ATR_TEAM_RETURN, pTeamContainer, iSubjectID, iEntID, iSubjectID, "TeamServer_Team_Promoted", "TeamServer_Subject_Promoted");
		aslTeam_AddSuccessLogMessage(ATR_TEAM_RETURN, pMember->pcLogName, pTeamContainer->iContainerID, "Promoted %s to team leader.", pSubjectMember->pcLogName);

		// Set the session info to NULL if necessary
		if (pTeamContainer->pXSessionInfo != NULL)
		{
			StructDestroyNoConst(parse_CrypticXSessionInfo, pTeamContainer->pXSessionInfo);
			pTeamContainer->pXSessionInfo = NULL;
		}

		aslTeam_AddReturnAction(ATR_TEAM_RETURN, TEAM_TRANSACTION_RETURN_ACTION_TYPE_UPDATE_GAME_SESSION, 
			pTeamContainer->iContainerID, iSubjectID);

		aslTeam_AddReturnAction(ATR_TEAM_RETURN, TEAM_TRANSACTION_RETURN_ACTION_TYPE_BECOME_XBOX_GAMING_SESSION_HOST, 
			pTeamContainer->iContainerID, iSubjectID);
	}
}

// Check if an entity is the leader
AUTO_TRANS_HELPER;
bool aslTeam_trh_IsLeader(TEAM_TRH_ARGS, U32 iEntID, ATH_ARG NOCONST(Team) *pTeamContainer)
{
	if (NONNULL(pTeamContainer->pLeader)) {
		return pTeamContainer->pLeader->iEntID == iEntID;
	}
	return false;
}

// Set an entity as the champion
AUTO_TRANS_HELPER;
void aslTeam_trh_SetChampion(TEAM_TRH_ARGS, U32 iEntID, ATH_ARG NOCONST(Team) *pTeamContainer, U32 iSubjectID)
{
	NOCONST(TeamMember) *pMember = aslTeam_trh_FindMember(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer);
	NOCONST(TeamMember) *pSubjectMember = aslTeam_trh_FindMember(TEAM_TRH_PASS_ARGS, iSubjectID, pTeamContainer);
	if (pMember && pSubjectMember) {
		if (ISNULL(pTeamContainer->pChampion)) {
			pTeamContainer->pChampion = StructCreateNoConst(parse_TeamMember);
		}
		pTeamContainer->pChampion->iEntID = iSubjectID;
		pTeamContainer->pChampion->iJoinTime = timeSecondsSince2000();
		
		aslTeam_trh_AddFeedbackAll(ATR_TEAM_RETURN, pTeamContainer, iSubjectID, iEntID, iSubjectID, "TeamServer_Team_SetChampion", "TeamServer_Subject_SetChampion");
		aslTeam_AddSuccessLogMessage(ATR_TEAM_RETURN, pMember->pcLogName, pTeamContainer->iContainerID, "Set %s to team champion.", pSubjectMember->pcLogName);
	}
}

// Set whether a team member is sidekicking
AUTO_TRANS_HELPER;
void aslTeam_trh_SetSidekicking(TEAM_TRH_ARGS, U32 iEntID, ATH_ARG NOCONST(Team) *pTeamContainer, U8 bSidekicking)
{
	NOCONST(TeamMember) *pMember = aslTeam_trh_FindMember(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer);
	if (pMember) {
		pMember->bSidekicked = (bool)bSidekicking;
		aslTeam_AddSuccessFeedback(ATR_TEAM_RETURN, iEntID, 0, iEntID, bSidekicking ? "TeamServer_Subject_EnableSidekicking" : "TeamServer_Subject_DisableSidekicking", false, 0, 0, 0, 0, NULL);
		aslTeam_AddSuccessLogMessage(ATR_TEAM_RETURN, pMember->pcLogName, pTeamContainer->iContainerID, bSidekicking ? "Sidekicking turned on." : "Sidekicking turned off.");
	}
}

// Set the team difficulty level
AUTO_TRANS_HELPER;
void aslTeam_trh_ChangeDifficulty(TEAM_TRH_ARGS, U32 iEntID, ATH_ARG NOCONST(Team) *pTeamContainer, int iDifficulty)
{
	NOCONST(TeamMember) *pMember = aslTeam_trh_FindMember(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer);
	if (NONNULL(pMember)) {
		pTeamContainer->iDifficulty = iDifficulty;
		aslTeam_trh_AddFeedbackAll(ATR_TEAM_RETURN, pTeamContainer, 0, iEntID, 0, "TeamServer_Team_DifficultyChanged", NULL);
		aslTeam_AddSuccessLogMessage(ATR_TEAM_RETURN, pMember->pcLogName, pTeamContainer->iContainerID, "Team difficulty changed to %i.", iDifficulty);		
	}
}

// Set the team mode
AUTO_TRANS_HELPER;
void aslTeam_trh_SetMode(TEAM_TRH_ARGS, U32 iEntID, ATH_ARG NOCONST(Team) *pTeamContainer, TeamMode eMode)
{
	NOCONST(TeamMember) *pMember = aslTeam_trh_FindMember(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer);
	if (NONNULL(pMember)) {
		pTeamContainer->eMode = eMode;
		aslTeam_trh_AddFeedbackAll(ATR_TEAM_RETURN, pTeamContainer, 0, iEntID, 0, "TeamServer_Team_ModeChanged", NULL);
		aslTeam_AddSuccessLogMessage(ATR_TEAM_RETURN, pMember->pcLogName, pTeamContainer->iContainerID, "Team mode changed to %s.", StaticDefineIntRevLookup(TeamModeEnum, eMode));		

		aslTeam_AddReturnAction(ATR_TEAM_RETURN, TEAM_TRANSACTION_RETURN_ACTION_TYPE_UPDATE_GAME_SESSION, 
			pTeamContainer->iContainerID, iEntID);
	}
}

// Set the team status message
AUTO_TRANS_HELPER;
void aslTeam_trh_SetStatusMessage(TEAM_TRH_ARGS, U32 iEntID, ATH_ARG NOCONST(Team) *pTeamContainer, const char *pchStatusMessage)
{
	NOCONST(TeamMember) *pMember = aslTeam_trh_FindMember(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer);
	if (NONNULL(pMember)) 
	{
		if (pchStatusMessage && pchStatusMessage[0])
		{
			estrCopy2(&pTeamContainer->pchStatusMessage, pchStatusMessage);
		}
		else
		{
			estrClear(&pTeamContainer->pchStatusMessage);
		}

		aslTeam_trh_AddFeedbackAll(ATR_TEAM_RETURN, pTeamContainer, 0, iEntID, 0, "TeamServer_Team_StatusMessageChanged", NULL);
		aslTeam_AddSuccessLogMessage(ATR_TEAM_RETURN, pMember->pcLogName, pTeamContainer->iContainerID, "Team status message is changed to %s.", NULL_TO_EMPTY(pchStatusMessage));

		aslTeam_AddReturnAction(ATR_TEAM_RETURN, TEAM_TRANSACTION_RETURN_ACTION_TYPE_UPDATE_GAME_SESSION, 
			pTeamContainer->iContainerID, iEntID);
	}
}

// Set the loot mode
AUTO_TRANS_HELPER;
void aslTeam_trh_SetLootMode(TEAM_TRH_ARGS, U32 iEntID, ATH_ARG NOCONST(Team) *pTeamContainer, LootMode eMode)
{
	NOCONST(TeamMember) *pMember = aslTeam_trh_FindMember(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer);
	if (pMember) {
		pTeamContainer->loot_mode = eMode;
		aslTeam_trh_AddFeedbackAll(ATR_TEAM_RETURN, pTeamContainer, 0, iEntID, 0, "TeamServer_Team_LootModeChanged", NULL);
		aslTeam_AddSuccessLogMessage(ATR_TEAM_RETURN, pMember->pcLogName, pTeamContainer->iContainerID, "Loot mode changed to %s.", StaticDefineIntRevLookup(LootModeEnum, eMode));

		aslTeam_AddReturnAction(ATR_TEAM_RETURN, TEAM_TRANSACTION_RETURN_ACTION_TYPE_UPDATE_GAME_SESSION, 
			pTeamContainer->iContainerID, iEntID);
	}
}

// Set the loot mode quality
AUTO_TRANS_HELPER;
void aslTeam_trh_SetLootModeQuality(TEAM_TRH_ARGS, U32 iEntID, ATH_ARG NOCONST(Team) *pTeamContainer, const char* pcQuality)
{
	NOCONST(TeamMember) *pMember = aslTeam_trh_FindMember(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer);
	if (pMember) {
		pTeamContainer->loot_mode_quality = allocAddString(pcQuality);
		aslTeam_trh_AddFeedbackAll(ATR_TEAM_RETURN, pTeamContainer, 0, iEntID, 0, "TeamServer_Team_LootModeQualityChanged", NULL);
		aslTeam_AddSuccessLogMessage(ATR_TEAM_RETURN, pMember->pcLogName, pTeamContainer->iContainerID, "Loot mode quality changed to %s.", pcQuality);

		aslTeam_AddReturnAction(ATR_TEAM_RETURN, TEAM_TRANSACTION_RETURN_ACTION_TYPE_UPDATE_GAME_SESSION, 
			pTeamContainer->iContainerID, iEntID);
	}
}

// Set the team member's stored location
AUTO_TRANS_HELPER;
void aslTeam_trh_SetInfo(TEAM_TRH_ARGS, ATH_ARG NOCONST(Team) *pTeamContainer, U32 iEntID, const char *pcMapName, const char *pcMapMsgKey, const char *pcMapVars, U32 iMapInstanceNumber, const char *pcStatus, const char *pchClassName, S32 iLevel, S32 iOfficerRank, U32 uPartitionID, U32 iMapContainerID)
{
	NOCONST(TeamMember) *pMember = aslTeam_trh_FindMember(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer);
	if (pMember) {
		pMember->pcMapName = allocAddString(pcMapName);
		pMember->pcMapVars = allocAddString(pcMapVars);
		pMember->pcMapMsgKey = StructAllocString(pcMapMsgKey);
		pMember->iMapInstanceNumber = iMapInstanceNumber;
		pMember->iMapContainerID = iMapContainerID;
		if (pMember->pcStatus) {
			StructFreeString(pMember->pcStatus);
		}
		pMember->pcStatus = StructAllocString(pcStatus);
		pMember->pchClassName = allocAddString(pchClassName);
		pMember->iExpLevel = iLevel;
		pMember->iOfficerRank = iOfficerRank;		
		pMember->uPartitionID = uPartitionID;

		aslTeam_AddReturnAction(ATR_TEAM_RETURN, TEAM_TRANSACTION_RETURN_ACTION_TYPE_UPDATE_GAME_SESSION, 
			pTeamContainer->iContainerID, iEntID);
	}
}

// Clear all team data from the entity
AUTO_TRANS_HELPER;
void aslTeam_trh_ClearPlayerTeam(TEAM_TRH_ARGS, ATH_ARG NOCONST(Entity) *pEnt)
{
	if (NONNULL(pEnt->pPlayer) && pEnt->pTeam->eState == TeamState_Member)
	{
		aslTeam_AddReturnActionEx(ATR_TEAM_RETURN, TEAM_TRANSACTION_RETURN_ACTION_TYPE_LEAVE_TEAM_CHAT,
			pEnt->pTeam->iTeamID, pEnt->myContainerID, pEnt->pPlayer->accountID);
	}
	aslTeam_AddSuccessLogMessage(ATR_TEAM_RETURN, pEnt->debugName, 0, "Team data cleared.");
	pEnt->pTeam->iTeamID = 0;
	StructFreeString(pEnt->pTeam->pcInviterName);
	pEnt->pTeam->pcInviterName = NULL;
}

AUTO_TRANS_HELPER;
bool aslTeam_trh_CheckAllegiance(ATH_ARG NOCONST(Team) *pTeamContainer, ATH_ARG NOCONST(Entity) *pEnt)
{
	const char *playerAllegianceString = REF_STRING_FROM_HANDLE(pEnt->hAllegiance);

	if (pTeamContainer->iGameServerOwnerID)
	{
		// If this is a local team, allow the member to be any allegiance
		return true;
	}

	if (!pTeamContainer->pcAllegiance || *pTeamContainer->pcAllegiance == '\0')
	{
		// no team allegiance, so always accept
		return true;
	}

	if (!playerAllegianceString)
	{
		// No player allegiance but there is a team allegiance, so never accept
		return false;
	}

	// If the player's allegiance matches the team's suballegiance, then they are always allowed into the team
	if (gConf.bAllowSuballegianceTeaming && pTeamContainer->pcSubAllegiance && (*pTeamContainer->pcSubAllegiance != '\0') &&
		!stricmp(playerAllegianceString, pTeamContainer->pcSubAllegiance))
	{
		return true;
	}
	// Else if the team requires a sub-allegiance match, check that both the player's allegiance and suballegiance matches the team
	else if (gConf.bAllowSuballegianceTeaming && pTeamContainer->pcSubAllegiance &&	(*pTeamContainer->pcSubAllegiance != '\0') &&
		pTeamContainer->bRequireSubAllegianceMatch)
	{
		const char *playerSubAllegianceString = REF_STRING_FROM_HANDLE(pEnt->hSubAllegiance);
		if (playerSubAllegianceString && !stricmp(playerAllegianceString, pTeamContainer->pcAllegiance) &&
			!stricmp(playerSubAllegianceString, pTeamContainer->pcSubAllegiance))
		{
			return true;
		}
	}
	// Else the player's allegiance must match the team's allegiance
	else if (!stricmp(playerAllegianceString, pTeamContainer->pcAllegiance))
	{
		return true;
	}

	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Non-Transaction Helpers
///////////////////////////////////////////////////////////////////////////////////////////
// This represents a lot of validation code that is repeated among the many remote
// commands below.

// Return whether the entity is a member of the team
static bool aslTeam_IsMember(Team *pTeam, U32 iEntID) {
	S32 i;
	if(!pTeam)
	{
		return false;
	}
	for (i = eaSize(&pTeam->eaMembers)-1; i >= 0; i--) {
		if (pTeam->eaMembers[i]->iEntID == iEntID) {
			return true;
		}
	}
	return false;
}

// Return whether the entity is invited to the team
static bool aslTeam_IsInvite(Team *pTeam, U32 iEntID) {
	S32 i;
	if(!pTeam)
	{
		return false;
	}
	for (i = eaSize(&pTeam->eaInvites)-1; i >= 0; i--) {
		if (pTeam->eaInvites[i]->iEntID == iEntID) {
			return true;
		}
 	}
	return false;
}

// Return whether the entity is requesting the team
static bool aslTeam_IsRequest(Team *pTeam, U32 iEntID) {
	S32 i;
	if(!pTeam)
	{
		return false;
	}
	for (i = eaSize(&pTeam->eaRequests)-1; i >= 0; i--) {
		if (pTeam->eaRequests[i]->iEntID == iEntID) {
			return true;
		}
	}
	return false;
}

// Return whether the entity is in the disconnecteds
static bool aslTeam_IsOnDisconnecteds(Team *pTeam, U32 iEntID)
{
	S32 i;
	if(!pTeam)
	{
		return false;
	}
	for (i = eaSize(&pTeam->eaDisconnecteds)-1; i >= 0; i--) {
		if (pTeam->eaDisconnecteds[i]->iEntID == iEntID) {
			return true;
		}
	}
	return false;
}


// Check if the entity is on the team, and call the appropriate validation if not
static bool aslTeam_CheckIsMember(Team *pTeam, U32 iEntID, const char *pcActionType) {
	if (!aslTeam_IsMember(pTeam, iEntID)) {
		aslTeam_SendError(iEntID, 0, pcActionType, "TeamServer_Error_SelfNotOnTeam");
		RemoteCommand_aslTeam_ValidateMember(GetAppGlobalType(), 0, pTeam->iContainerID, iEntID);
		return false;
	}
	return true;
}

// Check if the entity is not on the team, and send the appropriate feedback if so
static bool aslTeam_CheckIsNotMember(Team *pTeam, U32 iEntID, const char *pcActionType) {
	if (aslTeam_IsMember(pTeam, iEntID)) {
		aslTeam_SendError(iEntID, 0, pcActionType, "TeamServer_Error_SelfAlreadyOnTeam");
		return false;
	}
	return true;
}

// Check if the subject is on the team, and call the appropriate validation if not
static bool aslTeam_CheckSubjectIsMember(Team *pTeam, U32 iEntID, U32 iSubjectID, const char *pcActionType) {
	if (!aslTeam_IsMember(pTeam, iSubjectID)) {
		aslTeam_SendError(iEntID, iSubjectID, pcActionType, "TeamServer_Error_NotOnTeam");
		RemoteCommand_aslTeam_ValidateMember(GetAppGlobalType(), 0, pTeam->iContainerID, iSubjectID);
		return false;
	}
	return true;
}

// Check if the subject is not on the team, and send the appropriate feedback if so
static bool aslTeam_CheckSubjectIsNotMember(Team *pTeam, U32 iEntID, U32 iSubjectID, const char *pcActionType) {
	if (aslTeam_IsMember(pTeam, iSubjectID)) {
		aslTeam_SendError(iEntID, iSubjectID, pcActionType, "TeamServer_Error_AlreadyOnTeam");
		return false;
	}
	return true;
}

// Check if the subject is on the team, and call the appropriate validation if not
static bool aslTeam_CheckSubjectIsMemberOrDisconnecteds(Team *pTeam, U32 iEntID, U32 iSubjectID, const char *pcActionType) {
	if (!(aslTeam_IsMember(pTeam, iSubjectID) || aslTeam_IsOnDisconnecteds(pTeam, iSubjectID))) {
		aslTeam_SendError(iEntID, iSubjectID, pcActionType, "TeamServer_Error_NotOnTeam");
		RemoteCommand_aslTeam_ValidateMember(GetAppGlobalType(), 0, pTeam->iContainerID, iSubjectID);
		return false;
	}
	return true;
}



// Check if the entity is invited to the team, and call the appropriate validation if not
static bool aslTeam_CheckIsInvite(Team *pTeam, U32 iEntID, const char *pcActionType) {
	if (!aslTeam_IsInvite(pTeam, iEntID)) {
		aslTeam_SendError(iEntID, 0, pcActionType, "TeamServer_Error_SelfNotInvited");
		RemoteCommand_aslTeam_ValidateInvite(GetAppGlobalType(), 0, pTeam->iContainerID, iEntID);
		return false;
	}
	return true;
}

// Check if the entity is not invited to the team, and send the appropriate feedback if so
static bool aslTeam_CheckIsNotInvite(Team *pTeam, U32 iEntID, const char *pcActionType) {
	if (aslTeam_IsInvite(pTeam, iEntID)) {
		aslTeam_SendError(iEntID, 0, pcActionType, "TeamServer_Error_SelfAlreadyInvited");
		return false;
	}
	return true;
}

// Check if the subject is invited to the team, and call the appropriate validation if not
static bool aslTeam_CheckSubjectIsInvite(Team *pTeam, U32 iEntID, U32 iSubjectID, const char *pcActionType) {
	if (!aslTeam_IsInvite(pTeam, iSubjectID)) {
		aslTeam_SendError(iEntID, iSubjectID, pcActionType, "TeamServer_Error_NotInvited");
		RemoteCommand_aslTeam_ValidateInvite(GetAppGlobalType(), 0, pTeam->iContainerID, iSubjectID);
		return false;
	}
	return true;
}

// Check if the subject is not invited to the team, and send the appropriate feedback if so
static bool aslTeam_CheckSubjectIsNotInvite(Team *pTeam, U32 iEntID, U32 iSubjectID, const char *pcActionType) {
	if (aslTeam_IsInvite(pTeam, iSubjectID)) {
		aslTeam_SendError(iEntID, iSubjectID, pcActionType, "TeamServer_Error_AlreadyInvited");
		return false;
	}
	return true;
}

// Check if the entity is requesting the team, and call the appropriate validation if not
static bool aslTeam_CheckIsRequest(Team *pTeam, U32 iEntID, const char *pcActionType) {
	if (!aslTeam_IsRequest(pTeam, iEntID)) {
		aslTeam_SendError(iEntID, 0, pcActionType, "TeamServer_Error_SelfNotRequesting");
		RemoteCommand_aslTeam_ValidateRequest(GetAppGlobalType(), 0, pTeam->iContainerID, iEntID);
		return false;
	}
	return true;
}

// Check if the entity is not requesting the team, and send the appropriate feedback if so
static bool aslTeam_CheckIsNotRequest(Team *pTeam, U32 iEntID, const char *pcActionType) {
	if (aslTeam_IsRequest(pTeam, iEntID)) {
		aslTeam_SendError(iEntID, 0, pcActionType, "TeamServer_Error_SelfAlreadyRequesting");
		return false;
	}
	return true;
}

// Check if the subject is requesting the team, and call the appropriate validation if not
static bool aslTeam_CheckSubjectIsRequest(Team *pTeam, U32 iEntID, U32 iSubjectID, const char *pcActionType) {
	if (!aslTeam_IsRequest(pTeam, iSubjectID)) {
		aslTeam_SendError(iEntID, iSubjectID, pcActionType, "TeamServer_Error_NotRequesting");
		RemoteCommand_aslTeam_ValidateRequest(GetAppGlobalType(), 0, pTeam->iContainerID, iSubjectID);
		return false;
	}
	return true;
}

// Check if the subject is not requesting the team, and send the appropriate feedback if so
static bool aslTeam_CheckSubjectIsNotRequest(Team *pTeam, U32 iEntID, U32 iSubjectID, const char *pcActionType) {
	if (aslTeam_IsRequest(pTeam, iSubjectID)) {
		aslTeam_SendError(iEntID, iSubjectID, pcActionType, "TeamServer_Error_AlreadyRequesting");
		return false;
	}
	return true;
}

// Check if the team exists, and call the appropriate validation if not
static bool aslTeam_CheckExists(Team *pTeam, U32 iTeamID, U32 iEntID, const char *pcActionType) {
	if (!pTeam) {
		aslTeam_SendError(iEntID, 0, pcActionType, "TeamServer_Error_TeamNotFound");
		RemoteCommand_aslTeam_ValidateExists(GetAppGlobalType(), 0, iTeamID, iEntID);
		return false;
	}
	return true;
}

// Check if the team exists, and send the appropriate feedback if not
static bool aslTeam_CheckExistsNoValidate(Team *pTeam, U32 iEntID, const char *pcActionType) {
	if (!pTeam) {
		aslTeam_SendError(iEntID, 0, pcActionType, "TeamServer_Error_TeamNotFound");
		return false;
	}
	return true;
}

// Check if the team is full, and send the appropriate feedback if not
static bool aslTeam_CheckIsNotFull(Team *pTeam, U32 iEntID, const char *pcActionType) {
	if (team_NumTotalMembers(pTeam) >= TEAM_MAX_SIZE) {
		aslTeam_SendError(iEntID, 0, pcActionType, "TeamServer_Error_TeamFull");
		return false;
	}
	return true;
}

// Check if the player is the team leader, and send the appropriate feedback if not
static bool aslTeam_CheckIsLeader(Team *pTeam, U32 iEntID, const char *pcActionType) {
	if (pTeam->pLeader->iEntID != iEntID) {
		aslTeam_SendError(iEntID, 0, pcActionType, "TeamServer_Error_NotTeamLeader");
		return false;
	}
	return true;
}

// Check if the team is closed, and send the appropriate feedback if so
static bool aslTeam_CheckIsNotClosed(Team *pTeam, U32 iEntID, const char *pcActionType) {
	if (pTeam->eMode == TeamMode_Closed || pTeam->eMode == TeamMode_Prompt) {
		aslTeam_SendError(iEntID, 0, pcActionType, "TeamServer_Error_TeamClosed");
		return false;
	}
	return true;
}

// Check if the player is attempting to do something to themselves
static bool aslTeam_CheckSubjectIsNotSelf(Team *pTeam, U32 iEntID, U32 iSubjectID, const char *pcActionType) {
	if (iEntID == iSubjectID) {
		aslTeam_SendError(iEntID, 0, pcActionType, "TeamServer_Error_SubjectIsSelf");
		return false;
	}
	return true;
}

// Check if the team is local.  If it is then send the appropriate feedback.
static bool aslTeam_CheckIsNotLocal(Team *pTeam, U32 iEntID, const char *pcActionType) {
	if (pTeam->iGameServerOwnerID) {
		aslTeam_SendError(iEntID, 0, pcActionType, "TeamServer_Error_TeamIsLocal");
		return false;
	}
	return true;
}

// Check if the team is local.  If it is then send the appropriate feedback.
static bool aslTeam_CheckSubjectIsNotLocal(Team *pTeam, U32 iEntID,  U32 iSubjectID, const char *pcActionType) {
	if (pTeam->iGameServerOwnerID) {
		aslTeam_SendError(iEntID, iSubjectID, pcActionType, "TeamServer_Error_SubjectTeamIsLocal");
		return false;
	}
	return true;
}


///////////////////////////////////////////////////////////////////////////////////////////
// Destroy Team
///////////////////////////////////////////////////////////////////////////////////////////

typedef struct TeamDestroyData
{
	U32 iTeamID;
	U32 iGameServerOwnerID;
} TeamDestroyData;

static void aslTeam_Destroy_CB(TransactionReturnVal *pReturn, TeamDestroyData *pData)
{
	if (pReturn->eOutcome != TRANSACTION_OUTCOME_SUCCESS) {
		Errorf("Attempt to destroy GLOBALTYPE_TEAM[%d] failed: %s", pData->iTeamID, GetTransactionFailureString(pReturn));
	} else if (pData) {
		// Notify the owning game server that this team was destroyed
		if (pData->iGameServerOwnerID)
		{
			RemoteCommand_gslTeam_NotifyOwnedTeamDestroyed(GLOBALTYPE_GAMESERVER, pData->iGameServerOwnerID, pData->iTeamID);
		}
	}
	SAFE_FREE(pData);
}

AUTO_COMMAND_REMOTE;
void aslTeam_DestroyWithReason(U32 iTeamID, const char* pchReason)
{
	Team *pTeam = aslTeam_GetTeam(iTeamID);
	TeamDestroyData *pCBData;
	int i;

	if (!pTeam) {
		return;
	}

	for (i = eaSize(&pTeam->eaMembers)-1; i >= 0; i--) {
		TransactionReturnVal *pReturn = NULL;
		TeamMember *pMember = pTeam->eaMembers[i];
		ASLTeamCBData *pData = aslTeam_MakeCBData(NULL);
		pData->iEntID = pMember->iEntID;
		pData->iSubjectID = pMember->iEntID;
		pData->iTeamID = pTeam->iContainerID;
		pReturn = objCreateManagedReturnVal(aslTeam_RemoteCommand_CB, pData);
		AutoTrans_aslTeam_tr_ClearPlayerTeam(pReturn, GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, pMember->iEntID);
	}
	for (i = eaSize(&pTeam->eaInvites)-1; i >= 0; i--) {
		AutoTrans_aslTeam_tr_ClearPlayerTeam(NULL, GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, pTeam->eaInvites[i]->iEntID);
	}
	for (i = eaSize(&pTeam->eaRequests)-1; i >= 0; i--) {
		AutoTrans_aslTeam_tr_ClearPlayerTeam(NULL, GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, pTeam->eaRequests[i]->iEntID);
	}

	// Disconnecteds are just data so we shouldn't need clean up on them

	pCBData = calloc(1, sizeof(TeamDestroyData));
	pCBData->iTeamID = pTeam->iContainerID;
	pCBData->iGameServerOwnerID = pTeam->iGameServerOwnerID;

	objRequestContainerDestroy(objCreateManagedReturnVal(aslTeam_Destroy_CB, pCBData), GLOBALTYPE_TEAM, pTeam->iContainerID, objServerType(), objServerID());
	aslTeam_SaveLogMessage("Destroy", 0, iTeamID, "Team destroyed. Reason: %s", NULL_TO_EMPTY(pchReason));
}

AUTO_COMMAND_REMOTE;
void aslTeam_Destroy(U32 iTeamID)
{
	aslTeam_DestroyWithReason(iTeamID, "No Reason Given");
}


///////////////////////////////////////////////////////////////////////////////////////////
// Team Join Without Invite
///////////////////////////////////////////////////////////////////////////////////////////

void aslTeam_JoinWithoutInvite_CB(TransactionReturnVal *pReturn, ASLTeamCBData *pData)
{
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS) {
		RemoteCommand_gslTeam_ClaimMap(GLOBALTYPE_ENTITYPLAYER, pData->iEntID, pData->iEntID, GLOBALTYPE_ENTITYPLAYER, pData->iEntID, GLOBALTYPE_TEAM, pData->iTeamID);
		if (pData->pchEOIMapRequest && pData->pchEOIMapRequest[0]) {
			RemoteCommand_gslTeam_OpenInstancingJoinTeamAtMap(GLOBALTYPE_ENTITYPLAYER, pData->iSubjectID, pData->iSubjectID, pData->pchEOIMapRequest, pData->pchEOIMapVars, pData->uMapContainerID, pData->uMapPartitionID);
		}
	}
	else {
		aslTeam_HandleOpenInstancingError(pData);
	}
	aslTeam_RemoteCommand_CB(pReturn, pData);
}

AUTO_COMMAND_REMOTE;
void aslTeam_JoinWithoutInvite(U32 iTeamID, U32 iEntID, const char* pchEOIMapRequest, const char* pchEOIMapVars, U32 uMapContainerID, U32 uMapPartitionID, bool bIsRejoin)
{
	static char pcActionType[] = "Join";
	ASLTeamCBData *pData = NULL;
	TransactionReturnVal *pReturn = NULL;
	Team *pTeam = aslTeam_GetTeam(iTeamID);

	bool bCanJoin=false;

	if (pTeam)
	{
		if (bIsRejoin && gConf.bManageTeamDisconnecteds)
		{
			// Attempting to rejoin and we are managing disconnecteds. We need to check if it's okay.
			if (aslTeam_IsOnDisconnecteds(pTeam, iEntID) && aslTeam_CheckIsNotMember(pTeam, iEntID, pcActionType))
			{
				// We were on the disconnecteds list and not in the members list. So far so good.

				// Safety check.
				if (team_NumPresentMembers(pTeam) >= TEAM_MAX_SIZE)
				{
					//  If somehow we were to get here with the PresentMembers being full. Something got out of synch somewhere.
					//    This really should not happen, but return an appropriate error if it does.
					aslTeam_SendError(iEntID, 0, pcActionType, "TeamServer_Error_TeamFull");
				}
				else
				{
					bCanJoin=true;
				}
			}
			// Fail silently if we were not on the list. We may have been uninvited since we were last logged in
		}
		else
		{
			// Not a rejoin situation. Either it's a force join, or we're not managing disconnecteds
			
			// Make sure we are not already a full member of the team
			// Make sure that the team didn't fill up
			
			if (aslTeam_CheckIsNotMember(pTeam, iEntID, pcActionType) &&
				aslTeam_CheckIsNotFull(pTeam, iEntID, pcActionType))
			{
				bCanJoin=true;
			}
		}
	}

	if (bCanJoin)
	{
		pData = aslTeam_MakeCBData(pcActionType);
		pData->iEntID = iEntID;
		pData->iSubjectID = iEntID;
		pData->iTeamID = iTeamID;
		pData->pchEOIMapRequest = pchEOIMapRequest && pchEOIMapRequest[0] ? StructAllocString(pchEOIMapRequest) : NULL;
		pData->pchEOIMapVars = pchEOIMapVars && pchEOIMapVars[0] ? StructAllocString(pchEOIMapVars) : NULL;
		pData->uMapContainerID = uMapContainerID;
		pData->uMapPartitionID = uMapPartitionID;
		pReturn = objCreateManagedReturnVal(aslTeam_JoinWithoutInvite_CB, pData);
		AutoTrans_aslTeam_tr_JoinWithoutInvite(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, iTeamID, GLOBALTYPE_ENTITYPLAYER, iEntID, pchEOIMapRequest && pchEOIMapRequest[0]);
	}
	else if (pchEOIMapRequest && pchEOIMapRequest[0])
	{
		RemoteCommand_gslTeam_JoinOpenTeamByMap_Error(GLOBALTYPE_ENTITYPLAYER, iEntID, iEntID, pchEOIMapRequest, pchEOIMapVars);
	}
}

void aslTeam_JoinGameSession_CB(TransactionReturnVal *pReturn, ASLTeamCBData *pData)
{
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS) 
	{
		Team *pTeam = aslTeam_GetTeam(pData->iTeamID);

		TeamMember *pTeamMember = pTeam ? team_FindMemberID(pTeam, pData->iEntID) : NULL;

		if (pTeamMember)
		{
			// Set the lobby join time
			pTeamMember->uiLobbyJoinTime = timeSecondsSince2000();
		}		

		// Team join succeeded
		SlowRemoteCommandReturn_aslTeam_JoinGameSession(pData->iCmdID, 1);
	}
	else 
	{
		// Team join failed
		SlowRemoteCommandReturn_aslTeam_JoinGameSession(pData->iCmdID, 0);
	}
	aslTeam_RemoteCommand_CB(pReturn, pData);
}

void aslTeam_RequestToJoinGameSession_CB(TransactionReturnVal *pReturn, ASLTeamCBData *pData)
{
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS) 
	{
		// Request sent
		SlowRemoteCommandReturn_aslTeam_JoinGameSession(pData->iCmdID, 2);
	}
	else
	{
		// Team join failed
		SlowRemoteCommandReturn_aslTeam_JoinGameSession(pData->iCmdID, 0);
	}

	aslTeam_RemoteCommand_CB(pReturn, pData);
}

// Valid return values:
// 0 = Join failed
// 1 = Join succeeded
// 2 = Request sent
AUTO_COMMAND_REMOTE_SLOW(S32);
void aslTeam_JoinGameSession(U32 iTeamID, U32 iEntID, SlowRemoteCommandID iCmdID)
{
	static char pcActionType[] = "JoinGameSession";
	ASLTeamCBData *pData = NULL;
	TransactionReturnVal *pReturn = NULL;
	Team *pTeam = aslTeam_GetTeam(iTeamID);

	if (pTeam && aslTeam_IsMember(pTeam, iEntID))
	{
		// Already in the team
		SlowRemoteCommandReturn_aslTeam_JoinGameSession(iCmdID, 1);
		return;
	}

	if (aslTeam_CheckExists(pTeam, iTeamID, iEntID, pcActionType) &&
		aslTeam_CheckIsNotMember(pTeam, iEntID, pcActionType) &&
		aslTeam_CheckIsNotInvite(pTeam, iEntID, pcActionType) &&
		aslTeam_CheckIsNotRequest(pTeam, iEntID, pcActionType) &&
		aslTeam_CheckIsNotFull(pTeam, iEntID, pcActionType) &&
		aslTeam_CheckIsNotClosed(pTeam, iEntID, pcActionType) &&
		aslTeam_CheckIsNotLocal(pTeam, iEntID, pcActionType)) 
	{
		pData = aslTeam_MakeCBData(pcActionType);
		pData->iCmdID = iCmdID;
		pData->iEntID = iEntID;
		pData->iSubjectID = iEntID;
		pData->iTeamID = iTeamID;

		if (pTeam->eMode == TeamMode_Open) 
		{
			// The team is open, so just add them directly to the team
			pReturn = objCreateManagedReturnVal(aslTeam_JoinGameSession_CB, pData);
			AutoTrans_aslTeam_tr_JoinWithoutInvite(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, iTeamID, GLOBALTYPE_ENTITYPLAYER, iEntID, false);
		} 
		else 
		{
			// The team isn't open, so add them to the request list
			pReturn = objCreateManagedReturnVal(aslTeam_RequestToJoinGameSession_CB, pData);
			AutoTrans_aslTeam_tr_Request(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, iTeamID, GLOBALTYPE_ENTITYPLAYER, iEntID);
		}
	}
	else
	{
		// Team join failed
		SlowRemoteCommandReturn_aslTeam_JoinGameSession(iCmdID, 0);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".Icontainerid, .Igameserverownerid, .Pcallegiance, .Pcsuballegiance, .Brequiresuballegiancematch, .Iversion, .Eamembers, .Eadisconnecteds, .Emode, .Loot_Mode, .Loot_Mode_Quality, .Idifficulty, .Pleader, .Pxsessioninfo, .pchStatusMessage, .uBadLogoutTime")
ATR_LOCKS(pEnt, ".Hallegiance, .Hsuballegiance, .Pplayer.Bautojointeamvoicechat, .Psaved.Savedname, .Pteam, .Pplayer.Accountid, .Pplayer.Publicaccountname");
enumTransactionOutcome aslTeam_tr_JoinWithoutInvite(ATR_ARGS, NOCONST(Team) *pTeamContainer, NOCONST(Entity) *pEnt, U8 bEOIRequest)
{
	TEAM_TRANSACTION_INIT;
	
	// Make sure the player isn't already on a team
	if (NONNULL(pEnt->pTeam) && pEnt->pTeam->iTeamID) {
		if (pEnt->pTeam->eState == TeamState_Member) {
			aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, pEnt->myContainerID, 0, pEnt->myContainerID, "TeamServer_Error_AlreadyOnTeam", true, 0, 0, 0, 0, NULL);
			TEAM_TRANSACTION_RETURN_FAILURE;
		}
		if (pEnt->pTeam->eState == TeamState_Invitee) {
			aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, pEnt->myContainerID, 0, pEnt->myContainerID, "TeamServer_Error_AlreadyInvited", true, 0, 0, 0, 0, NULL);
			TEAM_TRANSACTION_RETURN_FAILURE;
		}
		if (pEnt->pTeam->eState == TeamState_Requester) {
			aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, pEnt->myContainerID, 0, pEnt->myContainerID, "TeamServer_Error_AlreadyRequesting", true, 0, 0, 0, 0, NULL);
			TEAM_TRANSACTION_RETURN_FAILURE;
		}
	}
	// Make sure player's allegiance matches team's
	if (!aslTeam_trh_CheckAllegiance(pTeamContainer, pEnt))
	{
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, pEnt->myContainerID, 0, pEnt->myContainerID, "TeamServer_Error_AllegianceMismatch", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}

	// Add the member
	aslTeam_trh_AddMember(TEAM_TRH_PASS_ARGS, pTeamContainer, pEnt, bEOIRequest);
	
	// Make sure there are not too many members now. Do this after the add instead of before so we can check AFTER disconnects/rejoins have done their thing
	if (aslTeam_trh_NumTotalMembers(TEAM_TRH_PASS_ARGS, pTeamContainer) > TEAM_MAX_SIZE) {
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, pEnt->myContainerID, 0, pEnt->myContainerID, "TeamServer_Error_TeamFull", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}
	
	aslTeam_trh_UpdateLeader(TEAM_TRH_PASS_ARGS, pTeamContainer, false);

	TEAM_TRANSACTION_RETURN_SUCCESS;
}


// After a forceJoin, run an onlineValidate in case they've gone offline at some point in the process.
void aslTeam_ForceJoin_CB(TransactionReturnVal *pReturn, ASLTeamCBData *pData)
{
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		if (pData->iWasDisconnected==0)
		{
			RemoteCommand_aslTeam_ValidateOnline(GLOBALTYPE_TEAMSERVER, 0, pData->iTeamID, pData->iEntID);
		}
	}
	StructDestroy(parse_ASLTeamCBData, pData);
}

AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".uiLocalTeamSyncRequestID, .Icontainerid, .Igameserverownerid, .Pcallegiance, .Pcsuballegiance, .Brequiresuballegiancematch, .Iversion, .Eamembers, .Eadisconnecteds, .Emode, .Loot_Mode, .Loot_Mode_Quality, .Idifficulty, .Pleader, .Pxsessioninfo, .pchStatusMessage, .uBadLogoutTime, .pChampion")
ATR_LOCKS(pPreviousTeam, ".Eamembers, .Eadisconnecteds[AO], .Iversion, .Icontainerid, .Pchampion, .Emode, .Loot_Mode, .Loot_Mode_Quality, .Idifficulty, .Pleader, .Pxsessioninfo, .pchStatusMessage, .Pcallegiance, .Pcsuballegiance, .Igameserverownerid, .Brequiresuballegiancematch")
ATR_LOCKS(pEnt, ".Pteam, .Hallegiance, .Hsuballegiance, .Psaved.Savedname, .Pplayer.Bautojointeamvoicechat, .Pplayer.Accountid, .Pplayer.Publicaccountname");
enumTransactionOutcome aslTeam_tr_ForceJoin(ATR_ARGS, NOCONST(Team) *pTeamContainer, NOCONST(Team) *pPreviousTeam, NOCONST(Entity) *pEnt, int iWasDisconnected, U32 uiRequestID)
{
	TEAM_TRANSACTION_INIT;

	U32 uPrevTeamID = NONNULL(pPreviousTeam) ? pPreviousTeam->iContainerID : 0;
	NOCONST(TeamMember) *pMember = aslTeam_trh_FindMember(TEAM_TRH_PASS_ARGS, pEnt->myContainerID, pTeamContainer);

	// The player cannot already be a member of this team
	if (NONNULL(pMember))
	{
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, pEnt->myContainerID, 0, pEnt->myContainerID, "TeamServer_Error_AlreadyOnTeam", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}

	// The player's previous team ID must match the passed in container ID
	// Note that we don't care about this check if the player is no longer on a team
	if (NONNULL(pEnt->pTeam) && pEnt->pTeam->iTeamID && pEnt->pTeam->iTeamID != uPrevTeamID)
	{
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, pEnt->myContainerID, 0, pEnt->myContainerID, "TeamServer_Error_Failed", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}

	// Make sure player's allegiance matches team's
	if (!aslTeam_trh_CheckAllegiance(pTeamContainer, pEnt))
	{
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, pEnt->myContainerID, 0, pEnt->myContainerID, "TeamServer_Error_AllegianceMismatch", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}

	// Make sure there is room on the team
	if (aslTeam_trh_NumTotalMembers(TEAM_TRH_PASS_ARGS, pTeamContainer) >= TEAM_MAX_SIZE) {
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, pEnt->myContainerID, 0, pEnt->myContainerID, "TeamServer_Error_TeamFull", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}

	// Remove the player from their old team
	if (NONNULL(pPreviousTeam)) {
		aslTeam_trh_RemoveMember(TEAM_TRH_PASS_ARGS, pEnt->myContainerID, pPreviousTeam, pEnt);
	}

	aslTeam_trh_AddMember(TEAM_TRH_PASS_ARGS, pTeamContainer, pEnt, false);
	aslTeam_trh_UpdateLeader(TEAM_TRH_PASS_ARGS, pTeamContainer, false);

	if (iWasDisconnected!=0)
	{
		aslTeam_trh_SetAsLoggedOut(TEAM_TRH_PASS_ARGS, pEnt, pTeamContainer, false);
	}

	if (uiRequestID > pTeamContainer->uiLocalTeamSyncRequestID)
		pTeamContainer->uiLocalTeamSyncRequestID = uiRequestID;

	TEAM_TRANSACTION_RETURN_SUCCESS;
}


///////////////////////////////////////////////////////////////////////////////////////////
// Invite to Team
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslTeam_Invite(U32 iTeamID, U32 iEntID, U32 iSubjectID, const char *pchCreateAllegiance, const char *pchCreateSubAllegiance)
{
	static char pcActionType[] = "Invite";
	TransactionReturnVal *pReturn = NULL;
	Team *pTeam = aslTeam_GetTeam(iTeamID);

	if (aslTeam_IsRequest(pTeam, iEntID))
	{
		RemoteCommand_aslTeam_CancelAndCreate(GetAppGlobalType(), 0, iTeamID, iEntID, iSubjectID, NULL, NULL, 0, 0, pchCreateAllegiance, pchCreateSubAllegiance);
		return;
	}

	// Check if they're passing in a team ID of zero: If so, create a new team instead
	if (!iTeamID) {
		RemoteCommand_aslTeam_Create(GetAppGlobalType(), 0, iEntID, iSubjectID, NULL, NULL, 0, 0, pchCreateAllegiance, pchCreateSubAllegiance);
		return;
	}
	
	if (aslTeam_CheckExists(pTeam, iTeamID, iEntID, pcActionType) &&
		aslTeam_CheckIsMember(pTeam, iEntID, pcActionType) &&
		aslTeam_CheckIsLeader(pTeam, iEntID, pcActionType) &&
		aslTeam_CheckSubjectIsNotMember(pTeam, iEntID, iSubjectID, pcActionType) &&
		aslTeam_CheckSubjectIsNotInvite(pTeam, iEntID, iSubjectID, pcActionType) &&
		aslTeam_CheckSubjectIsNotRequest(pTeam, iEntID, iSubjectID, pcActionType) &&
		aslTeam_CheckIsNotFull(pTeam, iEntID, pcActionType) &&
		aslTeam_CheckSubjectIsNotSelf(pTeam, iEntID, iSubjectID, pcActionType) && 
		aslTeam_CheckSubjectIsNotLocal(pTeam, iEntID, iSubjectID, pcActionType)) {
		
		pReturn = objCreateManagedReturnVal(aslTeam_RemoteCommand_CB, aslTeam_MakeCBData(pcActionType));
		AutoTrans_aslTeam_tr_Invite(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, iTeamID, iEntID, GLOBALTYPE_ENTITYPLAYER, iSubjectID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".Eainvites, .Pleader.Ientid, .Pcallegiance, .Pcsuballegiance, .Brequiresuballegiancematch, .Iversion, .Icontainerid, .Igameserverownerid, .Eamembers, .Eadisconnecteds[AO]")
ATR_LOCKS(pSubject, ".Psaved.Savedname, .Hallegiance, .Hsuballegiance, .Pteam, .Pplayer.Publicaccountname");
enumTransactionOutcome aslTeam_tr_Invite(ATR_ARGS, NOCONST(Team) *pTeamContainer, U32 iEntID, NOCONST(Entity) *pSubject)
{
	TEAM_TRANSACTION_INIT;
	
	// Make sure that the invite was issued by the team leader
	if (!aslTeam_trh_IsLeader(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer)) {
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iEntID, iEntID, pSubject->myContainerID, "TeamServer_Error_NotTeamLeader", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}
	// Make sure that the player isn't already on a team
	if (NONNULL(pSubject->pTeam) && pSubject->pTeam->iTeamID) {
		if (pSubject->pTeam->eState == TeamState_Member) {
			aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iEntID, iEntID, pSubject->myContainerID, "TeamServer_Error_AlreadyOnTeam", true, 0, 0, 0, 0, NULL);
			TEAM_TRANSACTION_RETURN_FAILURE;
		}
		if (pSubject->pTeam->eState == TeamState_Invitee) {
			aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iEntID, iEntID, pSubject->myContainerID, "TeamServer_Error_AlreadyInvited", true, 0, 0, 0, 0, NULL);
			TEAM_TRANSACTION_RETURN_FAILURE;
		}
		if (pSubject->pTeam->eState == TeamState_Requester) {
			aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iEntID, iEntID, pSubject->myContainerID, "TeamServer_Error_AlreadyRequesting", true, 0, 0, 0, 0, NULL);
			TEAM_TRANSACTION_RETURN_FAILURE;
		}
	}
	// Make sure player's allegiance matches team's
	if (!aslTeam_trh_CheckAllegiance(pTeamContainer, pSubject))
	{
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iEntID, iEntID, pSubject->myContainerID, "TeamServer_Error_AllegianceMismatch", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}

	// Make sure that the team isn't already full
	if (aslTeam_trh_NumTotalMembers(TEAM_TRH_PASS_ARGS, pTeamContainer) >= TEAM_MAX_SIZE) {
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iEntID, iEntID, pSubject->myContainerID, "TeamServer_Error_TeamFull", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}
	
	aslTeam_trh_AddInvite(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer, pSubject);
	
	TEAM_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Accept Invite to Team
///////////////////////////////////////////////////////////////////////////////////////////

void aslTeam_AcceptInvite_CB(TransactionReturnVal *pReturn, ASLTeamCBData *pData)
{
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS) {
		RemoteCommand_gslTeam_ClaimMap(GLOBALTYPE_ENTITYPLAYER, pData->iEntID, pData->iEntID, GLOBALTYPE_ENTITYPLAYER, pData->iEntID, GLOBALTYPE_TEAM, pData->iTeamID);
	}
	aslTeam_RemoteCommand_CB(pReturn, pData);
}

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslTeam_AcceptInvite(U32 iTeamID, U32 iEntID, bool bAutoSidekick)
{
	static char pcActionType[] = "AcceptInvite";
	ASLTeamCBData *pData = NULL;
	TransactionReturnVal *pReturn = NULL;
	Team *pTeam = aslTeam_GetTeam(iTeamID);
	
	if (aslTeam_CheckExists(pTeam, iTeamID, iEntID, pcActionType) &&
		aslTeam_CheckIsInvite(pTeam, iEntID, pcActionType) &&
		aslTeam_CheckIsNotFull(pTeam, iEntID, pcActionType)) {
		
		pData = aslTeam_MakeCBData(pcActionType);
		pData->iEntID = iEntID;
		pData->iTeamID = iTeamID;
		pReturn = objCreateManagedReturnVal(aslTeam_AcceptInvite_CB, pData);
		AutoTrans_aslTeam_tr_AcceptInvite(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, iTeamID, GLOBALTYPE_ENTITYPLAYER, iEntID, bAutoSidekick);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".Pcallegiance, .Pcsuballegiance, .Brequiresuballegiancematch, .Binviteaccepted, .Icontainerid, .Igameserverownerid, .Eainvites, .Iversion, .Eamembers, .Eadisconnecteds, .Emode, .Loot_Mode, .Loot_Mode_Quality, .Idifficulty, .pchStatusMessage, .Pleader, .Pxsessioninfo, .uBadLogoutTime")
ATR_LOCKS(pEnt, ".Pteam, .Psaved.Savedname, .Pplayer.Bautojointeamvoicechat, .Hallegiance, .Hsuballegiance, .Pplayer.Accountid, .Pplayer.Publicaccountname");
enumTransactionOutcome aslTeam_tr_AcceptInvite(ATR_ARGS, NOCONST(Team) *pTeamContainer, NOCONST(Entity) *pEnt, U8 bAutoSidekick)
{
	TEAM_TRANSACTION_INIT;
	
	// Make sure there is room on the team
	if (aslTeam_trh_NumTotalMembers(TEAM_TRH_PASS_ARGS, pTeamContainer) >= TEAM_MAX_SIZE) {
		aslTeam_trh_RemoveInvite(TEAM_TRH_PASS_ARGS, pEnt, pTeamContainer);
		aslTeam_AddSuccessFeedback(ATR_TEAM_RETURN, pEnt->myContainerID, pEnt->myContainerID, 0, "TeamServer_Error_TeamFull", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_SUCCESS;
	}
	// Make sure the player was actually invited, and remove them from the invite list if they were
	if (!aslTeam_trh_RemoveInvite(TEAM_TRH_PASS_ARGS, pEnt, pTeamContainer)) {
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, pEnt->myContainerID, pEnt->myContainerID, 0, "TeamServer_Error_SelfNotInvited", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}

	// Make sure player's allegiance matches team's
	if (!aslTeam_trh_CheckAllegiance(pTeamContainer, pEnt))
	{
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, pEnt->myContainerID, pEnt->myContainerID, 0, "TeamServer_Error_AllegianceMismatch", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}
	
	aslTeam_AddSuccessLogMessage(ATR_TEAM_RETURN, pEnt->debugName, pTeamContainer->iContainerID, "Accepted team invite.");
	aslTeam_trh_AddMember(TEAM_TRH_PASS_ARGS, pTeamContainer, pEnt, false);
	pTeamContainer->bInviteAccepted = true;

	// Update the leader in case they (the leader) went away before we accepted and we need to be the new leader. [NNO-14456]
	aslTeam_trh_UpdateLeader(TEAM_TRH_PASS_ARGS, pTeamContainer, true);

	if (bAutoSidekick)
	{
		aslTeam_trh_SetSidekicking(TEAM_TRH_PASS_ARGS, pEnt->myContainerID, pTeamContainer, true);
	}
	
	TEAM_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Team Decline Invite
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslTeam_DeclineInvite(U32 iTeamID, U32 iEntID)
{
	static char pcActionType[] = "DeclineInvite";
	TransactionReturnVal *pReturn = NULL;
	Team *pTeam = aslTeam_GetTeam(iTeamID);
	
	if (aslTeam_CheckExists(pTeam, iTeamID, iEntID, pcActionType) &&
		aslTeam_CheckIsInvite(pTeam, iEntID, pcActionType)) {
		
		pReturn = objCreateManagedReturnVal(aslTeam_RemoteCommand_CB, aslTeam_MakeCBData(pcActionType));
		AutoTrans_aslTeam_tr_DeclineInvite(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, iTeamID, GLOBALTYPE_ENTITYPLAYER, iEntID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".Icontainerid, .Pleader.Ientid, .Eainvites, .Iversion")
ATR_LOCKS(pEnt, ".Pteam.Estate, .Pteam.Pcinvitername, .Pteam.Iteamid");
enumTransactionOutcome aslTeam_tr_DeclineInvite(ATR_ARGS, NOCONST(Team) *pTeamContainer, NOCONST(Entity) *pEnt)
{
	TEAM_TRANSACTION_INIT;
	
	if (!aslTeam_trh_RemoveInvite(TEAM_TRH_PASS_ARGS, pEnt, pTeamContainer)) {
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, pEnt->myContainerID, pEnt->myContainerID, 0, "TeamServer_Error_SelfNotInvited", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}
	
	aslTeam_AddSuccessLogMessage(ATR_TEAM_RETURN, pEnt->debugName, pTeamContainer->iContainerID, "Declined team invite.");
	aslTeam_AddSuccessFeedback(ATR_TEAM_RETURN, pTeamContainer->pLeader->iEntID, pEnt->myContainerID, 0, "TeamServer_Subject_DeclineInvite", false, 0, 0, 0, 0, NULL);
	TEAM_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Request to Join Team
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslTeam_Request(U32 iTeamID, U32 iEntID)
{
	static char pcActionType[] = "Request";
	TransactionReturnVal *pReturn = NULL;
	Team *pTeam = aslTeam_GetTeam(iTeamID);
	bool bJoinWithoutInvite = false;
	
	if (aslTeam_CheckExists(pTeam, iTeamID, iEntID, pcActionType) &&
		aslTeam_CheckIsNotMember(pTeam, iEntID, pcActionType) &&
		aslTeam_CheckIsNotInvite(pTeam, iEntID, pcActionType) &&
		aslTeam_CheckIsNotRequest(pTeam, iEntID, pcActionType) &&
		aslTeam_CheckIsNotFull(pTeam, iEntID, pcActionType) &&
		aslTeam_CheckIsNotClosed(pTeam, iEntID, pcActionType) &&
		aslTeam_CheckIsNotLocal(pTeam, iEntID, pcActionType)) {
		
		if ( pTeam->eMode == TeamMode_Open ) {
			// The team is open, so just add them directly to the team
			ASLTeamCBData *pData = aslTeam_MakeCBData(pcActionType);
			pData->iEntID = iEntID;
			pData->iSubjectID = iEntID;
			pData->iTeamID = iTeamID;
			pReturn = objCreateManagedReturnVal(aslTeam_JoinWithoutInvite_CB, pData);
			AutoTrans_aslTeam_tr_JoinWithoutInvite(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, iTeamID, GLOBALTYPE_ENTITYPLAYER, iEntID, false);
		} else {
			// The team isn't open, so add them to the request list
			pReturn = objCreateManagedReturnVal(aslTeam_RemoteCommand_CB, aslTeam_MakeCBData(pcActionType));
			AutoTrans_aslTeam_tr_Request(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, iTeamID, GLOBALTYPE_ENTITYPLAYER, iEntID);
		}
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".Eamembers[AO], .Eadisconnecteds[AO], .Pcallegiance, .Pcsuballegiance, .Iversion, .Icontainerid, .Igameserverownerid, .Earequests, .Brequiresuballegiancematch")
ATR_LOCKS(pEnt, ".Psaved.Savedname, .Hallegiance, .Hsuballegiance, .Pteam, .Pplayer.Publicaccountname");
enumTransactionOutcome aslTeam_tr_Request(ATR_ARGS, NOCONST(Team) *pTeamContainer, NOCONST(Entity) *pEnt)
{
	TEAM_TRANSACTION_INIT;
	
	// Make sure that the player isn't already on a team
	if (NONNULL(pEnt->pTeam) && pEnt->pTeam->iTeamID) {
		if (pEnt->pTeam->eState == TeamState_Member) {
			aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, pEnt->myContainerID, pEnt->myContainerID, 0, "TeamServer_Error_SelfAlreadyOnTeam", true, 0, 0, 0, 0, NULL);
			TEAM_TRANSACTION_RETURN_FAILURE;
		}
		if (pEnt->pTeam->eState == TeamState_Invitee) {
			aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, pEnt->myContainerID, pEnt->myContainerID, 0, "TeamServer_Error_SelfAlreadyInvited", true, 0, 0, 0, 0, NULL);
			TEAM_TRANSACTION_RETURN_FAILURE;
		}
		if (pEnt->pTeam->eState == TeamState_Requester) {
			aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, pEnt->myContainerID, pEnt->myContainerID, 0, "TeamServer_Error_SelfAlreadyRequesting", true, 0, 0, 0, 0, NULL);
			TEAM_TRANSACTION_RETURN_FAILURE;
		}
	}

	// Make sure player's allegiance matches team's
	if (!aslTeam_trh_CheckAllegiance(pTeamContainer, pEnt))
	{
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, pEnt->myContainerID, pEnt->myContainerID, 0, "TeamServer_Error_AllegianceMismatch", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}

	// Make sure that the team isn't already full
	if (aslTeam_trh_NumTotalMembers(TEAM_TRH_PASS_ARGS, pTeamContainer) >= TEAM_MAX_SIZE) {
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, pEnt->myContainerID, pEnt->myContainerID, 0, "TeamServer_Error_TeamFull", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}
	
	aslTeam_trh_AddRequest(TEAM_TRH_PASS_ARGS, pEnt, pTeamContainer);
	
	TEAM_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Accept Request to Join Team
///////////////////////////////////////////////////////////////////////////////////////////

void aslTeam_AcceptRequest_CB(TransactionReturnVal *pReturn, ASLTeamCBData *pData)
{
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS) {
		RemoteCommand_gslTeam_ClaimMap(GLOBALTYPE_ENTITYPLAYER, pData->iEntID, pData->iEntID, GLOBALTYPE_ENTITYPLAYER, pData->iEntID, GLOBALTYPE_TEAM, pData->iTeamID);
	}
	aslTeam_RemoteCommand_CB(pReturn, pData);
}

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslTeam_AcceptRequest(U32 iTeamID, U32 iEntID, U32 iSubjectID)
{
	static char pcActionType[] = "AcceptRequest";
	ASLTeamCBData *pData = NULL;
	TransactionReturnVal *pReturn = NULL;
	Team *pTeam = aslTeam_GetTeam(iTeamID);
	
	if (aslTeam_CheckExists(pTeam, iTeamID, iEntID, pcActionType) &&
		aslTeam_CheckIsMember(pTeam, iEntID, pcActionType) &&
		aslTeam_CheckIsLeader(pTeam, iEntID, pcActionType) &&
		aslTeam_CheckSubjectIsRequest(pTeam, iEntID, iSubjectID, pcActionType)) {
		
		pData = aslTeam_MakeCBData(pcActionType);
		pData->iEntID = iSubjectID;
		pData->iTeamID = iTeamID;
		pReturn = objCreateManagedReturnVal(aslTeam_AcceptRequest_CB, pData);
		AutoTrans_aslTeam_tr_AcceptRequest(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, iTeamID, iEntID, GLOBALTYPE_ENTITYPLAYER, iSubjectID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".Icontainerid, .Igameserverownerid, .Eamembers, .Eadisconnecteds, .Pleader.Ientid, .Earequests, .Iversion, .Pcallegiance, .Pcsuballegiance, .Brequiresuballegiancematch, .Emode, .Loot_Mode, .Loot_Mode_Quality, .Idifficulty, .pchStatusMessage, .uBadLogoutTime")
ATR_LOCKS(pSubject, ".Pteam, .Psaved.Savedname, .Hallegiance, .Hsuballegiance, .Pplayer.Bautojointeamvoicechat, .Pplayer.Accountid, .Pplayer.Publicaccountname");
enumTransactionOutcome aslTeam_tr_AcceptRequest(ATR_ARGS, NOCONST(Team) *pTeamContainer, U32 iEntID, NOCONST(Entity) *pSubject)
{
	TEAM_TRANSACTION_INIT;
	NOCONST(TeamMember) *pMember = aslTeam_trh_FindMember(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer);
	
	// Make sure that the request was accepted by somebody on the team
	if (ISNULL(pMember)) {
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iEntID, iEntID, pSubject->myContainerID, "TeamServer_Error_SelfNotOnTeam", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}
	// Make sure that the request was accepted by the team leader
	if (!aslTeam_trh_IsLeader(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer)) {
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iEntID, iEntID, pSubject->myContainerID, "TeamServer_Error_NotTeamLeader", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}
	// Make sure there is room on the team
	if (aslTeam_trh_NumTotalMembers(TEAM_TRH_PASS_ARGS, pTeamContainer) >= TEAM_MAX_SIZE) {
		aslTeam_trh_RemoveRequest(TEAM_TRH_PASS_ARGS, pSubject, pTeamContainer);
		aslTeam_AddSuccessFeedback(ATR_TEAM_RETURN, iEntID, iEntID, pSubject->myContainerID, "TeamServer_Error_TeamFull", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_SUCCESS;
	}
	// Make sure the player was actually requesting, and remove them from the request list if they were
	if (!aslTeam_trh_RemoveRequest(TEAM_TRH_PASS_ARGS, pSubject, pTeamContainer)) {
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iEntID, iEntID, pSubject->myContainerID, "TeamServer_Error_NotRequesting", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}
	
	// Make sure player's allegiance matches team's
	if (!aslTeam_trh_CheckAllegiance(pTeamContainer, pSubject))
	{
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iEntID, iEntID, pSubject->myContainerID, "TeamServer_Error_AllegianceMismatch", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}

	aslTeam_trh_AddMember(TEAM_TRH_PASS_ARGS, pTeamContainer, pSubject, false);
	
	aslTeam_AddSuccessFeedback(ATR_TEAM_RETURN, iEntID, iEntID, pSubject->myContainerID, "TeamServer_Success_RequestAccepted", false, 0, 0, 0, 0, NULL);
	aslTeam_AddSuccessFeedback(ATR_TEAM_RETURN, pSubject->myContainerID, iEntID, pSubject->myContainerID, "TeamServer_Subject_RequestAccepted", false, 0, 0, 0, 0, NULL);
	aslTeam_AddSuccessLogMessage(ATR_TEAM_RETURN, pMember->pcLogName, pTeamContainer->iContainerID, "Accepted request to team from %s", pSubject->debugName);
	TEAM_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Decline Request to Join Team
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslTeam_DeclineRequest(U32 iTeamID, U32 iEntID, U32 iSubjectID)
{
	static char pcActionType[] = "DeclineRequest";
	TransactionReturnVal *pReturn = NULL;
	Team *pTeam = aslTeam_GetTeam(iTeamID);
	
	if (aslTeam_CheckExists(pTeam, iTeamID, iEntID, pcActionType) &&
		aslTeam_CheckIsMember(pTeam, iEntID, pcActionType) &&
		aslTeam_CheckIsLeader(pTeam, iEntID, pcActionType) &&
		aslTeam_CheckSubjectIsRequest(pTeam, iEntID, iSubjectID, pcActionType)) {
		
		pReturn = objCreateManagedReturnVal(aslTeam_RemoteCommand_CB, aslTeam_MakeCBData(pcActionType));
		AutoTrans_aslTeam_tr_DeclineRequest(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, iTeamID, iEntID, GLOBALTYPE_ENTITYPLAYER, iSubjectID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".Icontainerid, .Eamembers, .Pleader.Ientid, .Earequests, .Iversion")
ATR_LOCKS(pSubject, ".Pteam.Iteamid, .Pteam.Estate");
enumTransactionOutcome aslTeam_tr_DeclineRequest(ATR_ARGS, NOCONST(Team) *pTeamContainer, U32 iEntID, NOCONST(Entity) *pSubject)
{
	TEAM_TRANSACTION_INIT;
	NOCONST(TeamMember) *pMember = aslTeam_trh_FindMember(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer);
	
	// Make sure that the request was declined by the team leader
	if (!pMember || !aslTeam_trh_IsLeader(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer)) {
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iEntID, iEntID, pSubject->myContainerID, "TeamServer_Error_NotTeamLeader", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}
	// Make sure the player was actually requesting, and remove them from the request list if they were
	if (!aslTeam_trh_RemoveRequest(TEAM_TRH_PASS_ARGS, pSubject, pTeamContainer)) {
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iEntID, iEntID, pSubject->myContainerID, "TeamServer_Error_NotRequesting", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}
	
	aslTeam_AddSuccessFeedback(ATR_TEAM_RETURN, iEntID, iEntID, pSubject->myContainerID, "TeamServer_Success_RequestDeclined", false, 0, 0, 0, 0, NULL);
	aslTeam_AddSuccessFeedback(ATR_TEAM_RETURN, pSubject->myContainerID, iEntID, pSubject->myContainerID, "TeamServer_Subject_RequestDeclined", false, 0, 0, 0, 0, NULL);
	aslTeam_AddSuccessLogMessage(ATR_TEAM_RETURN, pMember->pcLogName, pTeamContainer->iContainerID, "Declined request to team from %s", pSubject->debugName);
	TEAM_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Cancel Request to Join Team
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslTeam_CancelRequest(U32 iTeamID, U32 iEntID)
{
	static char pcActionType[] = "CancelRequest";
	TransactionReturnVal *pReturn = NULL;
	Team *pTeam = aslTeam_GetTeam(iTeamID);
	
	if (aslTeam_CheckExists(pTeam, iTeamID, iEntID, pcActionType) &&
		aslTeam_CheckIsRequest(pTeam, iEntID, pcActionType)) {
		
		pReturn = objCreateManagedReturnVal(aslTeam_RemoteCommand_CB, aslTeam_MakeCBData(pcActionType));
		AutoTrans_aslTeam_tr_CancelRequest(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, iTeamID, GLOBALTYPE_ENTITYPLAYER, iEntID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".Icontainerid, .Earequests, .Iversion")
ATR_LOCKS(pEnt, ".Pteam.Iteamid, .Pteam.Estate");
enumTransactionOutcome aslTeam_tr_CancelRequest(ATR_ARGS, NOCONST(Team) *pTeamContainer, NOCONST(Entity) *pEnt)
{
	TEAM_TRANSACTION_INIT;
	
	// Make sure the player was actually requesting, and remove them from the request list if they were
	if (!aslTeam_trh_RemoveRequest(TEAM_TRH_PASS_ARGS, pEnt, pTeamContainer)) {
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, pEnt->myContainerID, pEnt->myContainerID, 0, "TeamServer_Error_NotRequesting", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}
	
	aslTeam_AddSuccessFeedback(ATR_TEAM_RETURN, pEnt->myContainerID, pEnt->myContainerID, 0, "TeamServer_Success_RequestCancelled", false, 0, 0, 0, 0, NULL);
	aslTeam_AddSuccessLogMessage(ATR_TEAM_RETURN, pEnt->debugName, pTeamContainer->iContainerID, "Cancelled request to team");
	TEAM_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Team Leave
///////////////////////////////////////////////////////////////////////////////////////////

void aslTeam_Leaving_CB(TransactionReturnVal *pReturn, ASLTeamCBData *pData)
{
	bool bDoExtraLeave=false;
	TransactionReturnVal *pExtraLeaveReturn = NULL;
	ASLTeamCBData *pExtraLeaveData = NULL;
		
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		Team *pTeam = aslTeam_GetTeam(pData->iTeamID);

		int iNumPresentMembers = team_NumPresentMembers(pTeam);		// Members who are logged in and full entities
		int iNumTotalMembers = team_NumTotalMembers(pTeam);			// Both connected and disconnected members

		// We were the last present member. Claim the map
		if (pTeam==NULL || iNumPresentMembers==0)
		{
			RemoteCommand_gslTeam_ClaimMap(GLOBALTYPE_ENTITYPLAYER, pData->iEntID, pData->iEntID,
													GLOBALTYPE_TEAM, pData->iTeamID,
													GLOBALTYPE_ENTITYPLAYER, pData->iEntID);
		}
		else
		{
			// Check for singleton member and shut the team down
			if (pTeam->iGameServerOwnerID==0 &&
				// We are associated with a GameServer/Local Team, don't do the normal deletion checks.
				// We would like the team to stay around in case someone gets added to the local team
				iNumTotalMembers==1 &&
				// Don't disband if there was a bad logout when disconnecteds are not managed
				 (gConf.bManageTeamDisconnecteds || timeSecondsSince2000() > pTeam->uBadLogoutTime + TEAM_BAD_LOGOUT_SECONDS) &&
				// Or if we have pending requests or invites
				(eaSize(&pTeam->eaRequests) == 0 && eaSize(&pTeam->eaInvites) == 0))
			{
				// Get everything we need out of pData before it's destroyed
				pExtraLeaveData = aslTeam_MakeCBData("LastMemberAutoLeave");
				pExtraLeaveData->iEntID = pTeam->eaMembers[0]->iEntID;
				pExtraLeaveData->iTeamID = pData->iTeamID;
				pExtraLeaveData->bFeedback = pData->bFeedback;
				pExtraLeaveReturn = objCreateManagedReturnVal(aslTeam_Leaving_CB, pExtraLeaveData);
				
				bDoExtraLeave=true;
			}
		}
	}

	// Complete the command before we do the last member leave. (Which destroys pData)
	aslTeam_RemoteCommand_CB(pReturn, pData);

	// Remove the last member if we need to
	if (bDoExtraLeave)
	{
		//   This is recursive. We must always be removing members if we get back here so it must terminate the recursion
		AutoTrans_aslTeam_tr_Leave(pExtraLeaveReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, pExtraLeaveData->iTeamID, GLOBALTYPE_ENTITYPLAYER, pExtraLeaveData->iEntID, 0);
	}
}


AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslTeam_Leave(U32 iTeamID, U32 iEntID, U32 iSubjectID, bool bFeedback)
{
	static char pcActionTypeLeave[] = "Leave";
	static char pcActionTypeKick[] = "Kick";
	char *pcActionType = iSubjectID == iEntID ? pcActionTypeLeave : pcActionTypeKick;
	Team *pTeam = aslTeam_GetTeam(iTeamID);
	bool bDoKick=false;
	bool bDoLeave=false;
	
	if (aslTeam_CheckExists(pTeam, iTeamID, iEntID, pcActionType) &&
		aslTeam_CheckIsMember(pTeam, iEntID, pcActionType))
	{
		if (iEntID == iSubjectID)
		{
			bDoLeave=true;
		}
		else if (aslTeam_CheckSubjectIsMemberOrDisconnecteds(pTeam, iEntID, iSubjectID, pcActionType) &&
				 aslTeam_CheckIsLeader(pTeam, iEntID, pcActionType))
		{
			// We used to check if the team was not local. This prevented disconnected members from being kicked
			//  when in a queue instance. All the calls to this function protect it by checks of allow queue abandonment, so we should
			//  be okay not checking here. WOLF[30Jan13]
			bDoKick=true;
		}
	}

	if (bDoKick || bDoLeave)
	{
		TransactionReturnVal *pReturn = NULL;
		ASLTeamCBData *pData = NULL;
		
		pData = aslTeam_MakeCBData(pcActionType);
		pData->iEntID = iEntID;
		pData->iTeamID = iTeamID;
		pData->bFeedback = bFeedback;
		pReturn = objCreateManagedReturnVal(aslTeam_Leaving_CB, pData);
		if (bDoKick)
		{
			AutoTrans_aslTeam_tr_Kick(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, iTeamID, iEntID, GLOBALTYPE_ENTITYPLAYER, iSubjectID);
		}
		else // DoLeave
		{
			AutoTrans_aslTeam_tr_Leave(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, iTeamID, GLOBALTYPE_ENTITYPLAYER, iEntID, 0);
		}
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".uiLocalTeamSyncRequestID, .Eamembers, .Eadisconnecteds[AO], .Iversion, .Icontainerid, .Pchampion, .Emode, .Loot_Mode, .Loot_Mode_Quality, .Idifficulty, .Pleader, .Pxsessioninfo, .pchStatusMessage, .Pcallegiance, .Pcsuballegiance, .Igameserverownerid, .Brequiresuballegiancematch")
ATR_LOCKS(pEnt, ".Pteam.Estate, .Pteam.Iteamid, .Pplayer.Accountid");
enumTransactionOutcome aslTeam_tr_Leave(ATR_ARGS, NOCONST(Team) *pTeamContainer, NOCONST(Entity) *pEnt, U32 uiRequestID)
{
	int iContainerID = NONNULL(pEnt) ? pEnt->myContainerID : 0;
	TEAM_TRANSACTION_INIT;
	
	if (!aslTeam_trh_RemoveMember(TEAM_TRH_PASS_ARGS, iContainerID, pTeamContainer, pEnt)) {
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iContainerID, iContainerID, 0, "TeamServer_Error_NotOnTeam", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}

	if (uiRequestID > pTeamContainer->uiLocalTeamSyncRequestID)
		pTeamContainer->uiLocalTeamSyncRequestID = uiRequestID;
	
	TEAM_TRANSACTION_RETURN_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".Idifficulty, .Eamembers, .Eadisconnecteds, .Iversion, .Icontainerid, .Pchampion, .Emode, .Loot_Mode, .Loot_Mode_Quality, .Pleader, .Pxsessioninfo, .pchStatusMessage, .Pcallegiance, .Pcsuballegiance, .Igameserverownerid, .Brequiresuballegiancematch")
ATR_LOCKS(pSubject, ".Pteam.Estate, .Pteam.Iteamid, .Pplayer.Accountid");
enumTransactionOutcome aslTeam_tr_Kick(ATR_ARGS, NOCONST(Team) *pTeamContainer, U32 iEntID, NOCONST(Entity) *pSubject)
{
	TEAM_TRANSACTION_INIT;

	bool bDidSomething=false;
	
	// Make sure the initiator is the leader
	if (!aslTeam_trh_IsLeader(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer)) {
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iEntID, iEntID, 0, "TeamServer_Error_NotTeamLeader", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}

	// Try removing it from both disconnecteds and the regular member list. We hope that pSubject came through the autoTrans system even if they are disconnected
	if (gConf.bManageTeamDisconnecteds)
	{
		if (aslTeam_trh_RemoveFromDisconnecteds(TEAM_TRH_PASS_ARGS, pTeamContainer, pSubject->myContainerID))
		{
			bDidSomething=true;
		}
	}
	
	if (aslTeam_trh_RemoveMember(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer, pSubject))
	{
		bDidSomething=true;
	}
		
	if (!bDidSomething)
	{
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iEntID, iEntID, 0, "TeamServer_Error_NotOnTeam", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}
	
	TEAM_TRANSACTION_RETURN_SUCCESS;
}


///////////////////////////////////////////////////////////////////////////////////////////
// Team Spokesman
///////////////////////////////////////////////////////////////////////////////////////////

// Set an entity as the team spokesman
AUTO_TRANS_HELPER;
void aslTeam_trh_SetSpokesman(TEAM_TRH_ARGS, U32 iEntID, ATH_ARG NOCONST(Team) *pTeamContainer, U32 iSubjectID)
{
	if (NONNULL(pTeamContainer)) 
	{
		// Set the spokesman
		pTeamContainer->iTeamSpokesmanEntID = iSubjectID;

		aslTeam_trh_AddFeedbackAll(ATR_TEAM_RETURN, pTeamContainer, iSubjectID, iEntID, iSubjectID, "TeamServer_Team_SetSpokesman", "TeamServer_Subject_SetSpokesman");
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".Pleader.Ientid, .Eamembers, .Iteamspokesmanentid, .Emode, .Loot_Mode, .Loot_Mode_Quality, .Idifficulty, .pchStatusMessage");
enumTransactionOutcome aslTeam_tr_SetSpokesman(ATR_ARGS, NOCONST(Team) *pTeamContainer, U32 iEntID, U32 iSubjectID)
{
	TEAM_TRANSACTION_INIT;

	// Make sure the initiator is the old leader
	if (!aslTeam_trh_IsLeader(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer)) {
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iEntID, iEntID, iSubjectID, "TeamServer_Error_NotTeamLeader", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}
	// Make sure the spokesman is on the team
	if (!aslTeam_trh_FindMember(TEAM_TRH_PASS_ARGS, iSubjectID, pTeamContainer)) {
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iEntID, iEntID, iSubjectID, "TeamServer_Error_NotOnTeam", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}
	aslTeam_trh_SetSpokesman(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer, iSubjectID);

	timingHistoryPush(gActionHistory);

	TEAM_TRANSACTION_RETURN_SUCCESS;
}

// Check if given entity is the team spokesman
static bool aslTeam_CheckIsTeamSpokesman(Team *pTeam, U32 iEntID) 
{
	if (pTeam)
	{
		if (pTeam->iTeamSpokesmanEntID == 0)
			return iEntID == pTeam->pLeader->iEntID;
		else
			return iEntID == pTeam->iTeamSpokesmanEntID;
	}
	return false;
}

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslTeam_SetSpokesman(U32 iTeamID, U32 iEntID, U32 iSubjectID)
{
	static char pcActionType[] = "SetSpokesman";
	TransactionReturnVal *pReturn = NULL;
	Team *pTeam = aslTeam_GetTeam(iTeamID);

	if (aslTeam_CheckExists(pTeam, iTeamID, iEntID, pcActionType) && // Team exists
		aslTeam_CheckIsMember(pTeam, iEntID, pcActionType) && // Requesting entity is a member of the team
		aslTeam_CheckIsLeader(pTeam, iEntID, pcActionType) && // Requesting entity is the team leader
		aslTeam_CheckSubjectIsMember(pTeam, iEntID, iSubjectID, pcActionType) && // Subject is a member of the same team
		!aslTeam_CheckIsTeamSpokesman(pTeam, iSubjectID) && // Subject is not already the spokesman 
		aslTeam_CheckIsNotLocal(pTeam, iEntID, pcActionType))
	{
			pReturn = objCreateManagedReturnVal(aslTeam_RemoteCommand_CB, aslTeam_MakeCBData(pcActionType));
			AutoTrans_aslTeam_tr_SetSpokesman(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, iTeamID, iEntID, iSubjectID);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// Team Promote
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslTeam_Promote(U32 iTeamID, U32 iEntID, U32 iSubjectID)
{
	static char pcActionType[] = "Promote";
	TransactionReturnVal *pReturn = NULL;
	Team *pTeam = aslTeam_GetTeam(iTeamID);
	
	if (aslTeam_CheckExists(pTeam, iTeamID, iEntID, pcActionType) &&
		aslTeam_CheckIsMember(pTeam, iEntID, pcActionType) &&
		aslTeam_CheckIsLeader(pTeam, iEntID, pcActionType) &&
		aslTeam_CheckSubjectIsMember(pTeam, iEntID, iSubjectID, pcActionType) &&
		aslTeam_CheckSubjectIsNotSelf(pTeam, iEntID, iSubjectID, pcActionType)) {
		
		pReturn = objCreateManagedReturnVal(aslTeam_RemoteCommand_CB, aslTeam_MakeCBData(pcActionType));
		AutoTrans_aslTeam_tr_Promote(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, iTeamID, iEntID, iSubjectID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".Eamembers, .Pleader, .Pchampion, .Pxsessioninfo, .Icontainerid, .Emode, .Loot_Mode, .Loot_Mode_Quality, .Idifficulty, .pchStatusMessage");
enumTransactionOutcome aslTeam_tr_Promote(ATR_ARGS, NOCONST(Team) *pTeamContainer, U32 iEntID, U32 iSubjectID)
{
	TEAM_TRANSACTION_INIT;
	
	// Make sure the initiator is the old leader
	if (!aslTeam_trh_IsLeader(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer)) {
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iEntID, iEntID, iSubjectID, "TeamServer_Error_NotTeamLeader", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}
	// Make sure the new leader is on the team
	if (!aslTeam_trh_FindMember(TEAM_TRH_PASS_ARGS, iSubjectID, pTeamContainer)) {
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iEntID, iEntID, iSubjectID, "TeamServer_Error_NotOnTeam", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}
	aslTeam_trh_SetLeader(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer, iSubjectID);

	timingHistoryPush(gActionHistory);
	
	TEAM_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set Team Champion
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslTeam_SetChampion(U32 iTeamID, U32 iEntID, U32 iSubjectID)
{
	static char pcActionType[] = "SetChampion";
	TransactionReturnVal *pReturn = NULL;
	Team *pTeam = aslTeam_GetTeam(iTeamID);
	
	if (aslTeam_CheckExists(pTeam, iTeamID, iEntID, pcActionType) &&
		aslTeam_CheckIsMember(pTeam, iEntID, pcActionType) &&
		aslTeam_CheckIsLeader(pTeam, iEntID, pcActionType) &&
		aslTeam_CheckSubjectIsMember(pTeam, iEntID, iSubjectID, pcActionType)) {
		
		pReturn = objCreateManagedReturnVal(aslTeam_RemoteCommand_CB, aslTeam_MakeCBData(pcActionType));
		AutoTrans_aslTeam_tr_SetChampion(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, iTeamID, iEntID, iSubjectID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".Pleader.Ientid, .Eamembers, .Pchampion, .Icontainerid, .Emode, .Loot_Mode, .Loot_Mode_Quality, .Idifficulty, .pchStatusMessage");
enumTransactionOutcome aslTeam_tr_SetChampion(ATR_ARGS, NOCONST(Team) *pTeamContainer, U32 iEntID, U32 iSubjectID)
{
	TEAM_TRANSACTION_INIT;
	
	// Make sure the initiator is the leader
	if (!aslTeam_trh_IsLeader(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer)) {
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iEntID, iEntID, iSubjectID, "TeamServer_Error_NotTeamLeader", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}
	// Make sure the new champion is on the team
	if (!aslTeam_trh_FindMember(TEAM_TRH_PASS_ARGS, iSubjectID, pTeamContainer)) {
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iEntID, iEntID, iSubjectID, "TeamServer_Error_NotOnTeam", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}
	aslTeam_trh_SetChampion(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer, iSubjectID);
	
	timingHistoryPush(gActionHistory);
	
	TEAM_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set Sidekicking
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslTeam_SetSidekicking(U32 iTeamID, U32 iEntID, bool bSidekicking)
{
	static char pcActionType[] = "SetSidekicking";
	TransactionReturnVal *pReturn = NULL;
	Team *pTeam = aslTeam_GetTeam(iTeamID);
	
	if (aslTeam_CheckExists(pTeam, iTeamID, iEntID, pcActionType) &&
		aslTeam_CheckIsMember(pTeam, iEntID, pcActionType)) {
		
		pReturn = objCreateManagedReturnVal(aslTeam_RemoteCommand_CB, aslTeam_MakeCBData(pcActionType));
		AutoTrans_aslTeam_tr_SetSidekicking(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, pTeam->iContainerID, iEntID, bSidekicking);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".Eamembers, .Icontainerid");
enumTransactionOutcome aslTeam_tr_SetSidekicking(ATR_ARGS, NOCONST(Team)* pTeamContainer, U32 iEntID, U8 bSidekicking)
{
	TEAM_TRANSACTION_INIT;
	
	// Make sure the player is on the team
	if (!aslTeam_trh_FindMember(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer)) {
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iEntID, iEntID, 0, "TeamServer_Error_SelfNotOnTeam", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}
	aslTeam_trh_SetSidekicking(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer, bSidekicking);
	
	timingHistoryPush(gActionHistory);
	
	TEAM_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Change Team Difficulty Level
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE;
void aslTeam_ChangeDifficulty(U32 iTeamID, U32 iEntID, int iDifficulty)
{
	static char pcActionType[] = "ChangeDifficulty";
	TransactionReturnVal *pReturn = NULL;
	Team *pTeam = aslTeam_GetTeam(iTeamID);

	if (aslTeam_CheckExists(pTeam, iTeamID, iEntID, pcActionType) &&
		aslTeam_CheckIsMember(pTeam, iEntID, pcActionType) &&
		aslTeam_CheckIsLeader(pTeam, iEntID, pcActionType) &&
		aslTeam_CheckIsNotLocal(pTeam, iEntID, pcActionType))
	{
		pReturn = objCreateManagedReturnVal(aslTeam_RemoteCommand_CB, aslTeam_MakeCBData(pcActionType));
		AutoTrans_aslTeam_tr_ChangeDifficulty(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, pTeam->iContainerID, iEntID, iDifficulty);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".Pleader.Ientid, .Idifficulty, .Icontainerid, .Eamembers, .Emode, .Loot_Mode, .Loot_Mode_Quality, .pchStatusMessage");
enumTransactionOutcome aslTeam_tr_ChangeDifficulty(ATR_ARGS, NOCONST(Team) *pTeamContainer, int iEntID, int iDifficulty)
{
	TEAM_TRANSACTION_INIT;

	// Make sure the initiator is the leader
	if (!aslTeam_trh_IsLeader(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer)) {
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iEntID, iEntID, 0, "TeamServer_Error_NotTeamLeader", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}

	aslTeam_trh_ChangeDifficulty(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer, iDifficulty);

	timingHistoryPush(gActionHistory);

	TEAM_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Join Whole Team to Guild
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE;
void aslTeam_JoinGuild(U32 iTeamID, U32 iEntID, U32 iGuildID)
{
	static char pcActionType[] = "Invite";
	TransactionReturnVal *pReturn = NULL;
	Team *pTeam = aslTeam_GetTeam(iTeamID);
	S32 i;
	
	if (aslTeam_CheckExists(pTeam, iTeamID, iEntID, pcActionType) &&
		aslTeam_CheckIsMember(pTeam, iEntID, pcActionType)) {
		
		for (i = eaSize(&pTeam->eaMembers)-1; i >= 0; i--) {
			if (pTeam->eaMembers[i]->iEntID != iEntID) {
				RemoteCommand_aslGuild_JoinWithoutInvite(GLOBALTYPE_GUILDSERVER, 0, iGuildID, pTeam->eaMembers[i]->iEntID, pTeam->eaMembers[i]->pchClassName);
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// Update a team member's data
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE;
void aslTeam_UpdateInfo(U32 iTeamID, U32 iEntID, const char *pcMapName, const char *pcMapMsgKey, const char *pcMapVars, U32 iMapInstanceNumber, const char *pcStatus, const char *pchClassName, S32 iLevel, S32 iOfficerRank, U32 uPartitionID, U32 iMapContainerID)
{
	static char pcActionType[] = "UpdateInfo";
	TransactionReturnVal *pReturn = objCreateManagedReturnVal(aslTeam_RemoteCommand_CB, aslTeam_MakeCBData(pcActionType));

	AutoTrans_aslTeam_tr_UpdateInfo(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, iTeamID, iEntID, pcMapName, pcMapMsgKey, pcMapVars, iMapInstanceNumber, pcStatus, pchClassName, iLevel, iOfficerRank, uPartitionID, iMapContainerID);
}

AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".Icontainerid, .Eamembers");
enumTransactionOutcome aslTeam_tr_UpdateInfo(ATR_ARGS, NOCONST(Team) *pTeamContainer, U32 iEntID, const char *pcMapName, const char *pcMapMsgKey, const char *pcMapVars, U32 iMapInstanceNumber, const char *pcStatus, const char *pchClassName, S32 iLevel, S32 iOfficerRank, U32 uPartitionID, U32 iMapContainerID)
{
	TEAM_TRANSACTION_INIT;
	
	if (!aslTeam_trh_FindMember(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer)) {
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iEntID, iEntID, 0, "TeamServer_Error_SelfNotOnTeam", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}
	
	if ((pcMapVars && pcMapVars[0]) || (pcMapName && pcMapName[0])) {
		aslTeam_trh_SetInfo(TEAM_TRH_PASS_ARGS, pTeamContainer, iEntID, pcMapName, pcMapMsgKey, pcMapVars, iMapInstanceNumber, pcStatus, pchClassName, iLevel, iOfficerRank, uPartitionID, iMapContainerID);
	}
	TEAM_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Team Mode
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslTeam_ChangeMode(U32 iTeamID, U32 iEntID, TeamMode eMode)
{
	static char pcActionType[] = "ChangeMode";
	TransactionReturnVal *pReturn = NULL;
	Team *pTeam = aslTeam_GetTeam(iTeamID);
	
	if (aslTeam_CheckExists(pTeam, iTeamID, iEntID, pcActionType) &&
		aslTeam_CheckIsMember(pTeam, iEntID, pcActionType) &&
		aslTeam_CheckIsLeader(pTeam, iEntID, pcActionType) &&
		aslTeam_CheckIsNotLocal(pTeam, iEntID, pcActionType)) {
		
		pReturn = objCreateManagedReturnVal(aslTeam_RemoteCommand_CB, aslTeam_MakeCBData(pcActionType));
		AutoTrans_aslTeam_tr_ChangeMode(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, pTeam->iContainerID, iEntID, eMode);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".Pleader.Ientid, .Emode, .Icontainerid, .Eamembers, .Loot_Mode, .Loot_Mode_Quality, .Idifficulty, .pchStatusMessage");
enumTransactionOutcome aslTeam_tr_ChangeMode(ATR_ARGS, NOCONST(Team)* pTeamContainer, U32 iEntID, U32 eMode)
{
	TEAM_TRANSACTION_INIT;
	
	// Make sure the initiator is the leader
	if (!aslTeam_trh_IsLeader(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer)) {
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iEntID, iEntID, 0, "TeamServer_Error_NotTeamLeader", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}
	aslTeam_trh_SetMode(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer, eMode);
	
	timingHistoryPush(gActionHistory);
	
	TEAM_TRANSACTION_RETURN_SUCCESS;
}

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslTeam_ChangeStatusMessage(U32 iTeamID, U32 iEntID, const char *pchStatusMessage)
{
	static char pcActionType[] = "ChangeStatusMessage";
	TransactionReturnVal *pReturn = NULL;
	Team *pTeam = aslTeam_GetTeam(iTeamID);

	if (aslTeam_CheckExists(pTeam, iTeamID, iEntID, pcActionType) &&
		aslTeam_CheckIsMember(pTeam, iEntID, pcActionType) &&
		aslTeam_CheckIsLeader(pTeam, iEntID, pcActionType) &&
		aslTeam_CheckIsNotLocal(pTeam, iEntID, pcActionType)) 
	{
		pReturn = objCreateManagedReturnVal(aslTeam_RemoteCommand_CB, aslTeam_MakeCBData(pcActionType));
		AutoTrans_aslTeam_tr_ChangeStatusMessage(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, pTeam->iContainerID, iEntID, pchStatusMessage);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".Pleader.Ientid, .Emode, .Icontainerid, .Eamembers, .Loot_Mode, .Loot_Mode_Quality, .Idifficulty, .pchStatusMessage");
enumTransactionOutcome aslTeam_tr_ChangeStatusMessage(ATR_ARGS, NOCONST(Team)* pTeamContainer, U32 iEntID, const char *pchStatusMessage)
{
	TEAM_TRANSACTION_INIT;

	// Make sure the initiator is the leader
	if (!aslTeam_trh_IsLeader(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer)) {
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iEntID, iEntID, 0, "TeamServer_Error_NotTeamLeader", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}
	aslTeam_trh_SetStatusMessage(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer, pchStatusMessage);

	timingHistoryPush(gActionHistory);

	TEAM_TRANSACTION_RETURN_SUCCESS;
}

// Sets the progression override
AUTO_TRANS_HELPER;
void aslTeam_trh_SetCurrentProgressionOverride(TEAM_TRH_ARGS, ATH_ARG NOCONST(Team) *pTeamContainer, ATH_ARG NOCONST(Entity) *pLeader, const GameContentNodeRef* pProgression)
{
	pTeamContainer->iCurrentUGCProjectID = 0;
	if(IS_HANDLE_ACTIVE(pProgression->hNode))
	{
		GameProgressionNodeDef* pNodeDef = GET_REF(pProgression->hNode);
		GameProgressionNodeDef* pBranchNodeDef = progression_GetStoryBranchNode(pNodeDef);
		if (pBranchNodeDef &&
			NONNULL(pLeader->pPlayer) &&
			NONNULL(pLeader->pPlayer->pProgressionInfo))
		{
			if (ISNULL(pLeader->pPlayer->pProgressionInfo->pTeamData))
			{
				pLeader->pPlayer->pProgressionInfo->pTeamData = StructCreateNoConst(parse_TeamProgressionData);
			}
			SET_HANDLE_FROM_REFERENT("GameProgressionNodeDef", pBranchNodeDef, pLeader->pPlayer->pProgressionInfo->pTeamData->hStoryArcNode);
			COPY_HANDLE(pLeader->pPlayer->pProgressionInfo->pTeamData->hNode, pProgression->hNode);
			pLeader->pPlayer->pProgressionInfo->pTeamData->iLastUpdated = timeSecondsSince2000();
		}
	}
	else if (pProgression->iUGCProjectID)
	{
		pTeamContainer->iCurrentUGCProjectID = pProgression->iUGCProjectID;
		if (NONNULL(pLeader->pPlayer) &&
			NONNULL(pLeader->pPlayer->pProgressionInfo) &&
			NONNULL(pLeader->pPlayer->pProgressionInfo->pTeamData))
		{
			StructDestroyNoConstSafe(parse_TeamProgressionData, &pLeader->pPlayer->pProgressionInfo->pTeamData);
		}
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".Pleader.Ientid, .Emode, .Icontainerid, .Eamembers, .Loot_Mode, .Loot_Mode_Quality, .Idifficulty, .iCurrentUGCProjectID, .pchStatusMessage")
ATR_LOCKS(pLeader, ".Pplayer.pProgressionInfo.pTeamData");
enumTransactionOutcome aslTeam_tr_SaveFromLobby(ATR_ARGS, NOCONST(Team)* pTeamContainer, NOCONST(Entity) *pLeader, S32 eTeamMode, S32 eLootMode, const char * eItemQuality, const GameContentNodeRef* pProgressionOverride, const char *pchStatusMessage)
{
	TEAM_TRANSACTION_INIT;

	// Make sure the initiator is the leader
	if (ISNULL(pLeader) || !aslTeam_trh_IsLeader(TEAM_TRH_PASS_ARGS, pLeader->myContainerID, pTeamContainer)) 
	{
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, pLeader->myContainerID, pLeader->myContainerID, 0, "TeamServer_Error_NotTeamLeader", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}

	aslTeam_trh_SetMode(TEAM_TRH_PASS_ARGS, pLeader->myContainerID, pTeamContainer, eTeamMode);
	aslTeam_trh_SetLootMode(TEAM_TRH_PASS_ARGS, pLeader->myContainerID, pTeamContainer, eLootMode);
	aslTeam_trh_SetLootModeQuality(TEAM_TRH_PASS_ARGS, pLeader->myContainerID, pTeamContainer, eItemQuality);	
	aslTeam_trh_SetCurrentProgressionOverride(TEAM_TRH_PASS_ARGS, pTeamContainer, pLeader, pProgressionOverride);
	aslTeam_trh_SetStatusMessage(TEAM_TRH_PASS_ARGS, pLeader->myContainerID, pTeamContainer, pchStatusMessage);

	timingHistoryPush(gActionHistory);

	TEAM_TRANSACTION_RETURN_SUCCESS;
}

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslTeam_ChangeDefaultMode(U32 iEntID, TeamMode eMode)
{
	static char pcActionType[] = "ChangeDefaultMode";
	TransactionReturnVal *pReturn = NULL;

	pReturn = objCreateManagedReturnVal(aslTeam_RemoteCommand_CB, aslTeam_MakeCBData(pcActionType));
	AutoTrans_aslTeam_tr_ChangeDefaultMode(pReturn, GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, iEntID, eMode);
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pteam");
enumTransactionOutcome aslTeam_tr_ChangeDefaultMode(ATR_ARGS, NOCONST(Entity)* pEnt, U32 eMode)
{
	TEAM_TRANSACTION_INIT;
	
	if (ISNULL(pEnt->pTeam)) {
		pEnt->pTeam = StructCreateNoConst(parse_PlayerTeam);
	}
	pEnt->pTeam->eMode = eMode;
	aslTeam_AddSuccessFeedback(ATR_TEAM_RETURN, pEnt->myContainerID, pEnt->myContainerID, 0, "TeamServer_Success_ChangeDefaultMode", false, 0, 0, 0, 0, NULL);
	
	TEAM_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Loot Mode
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslTeam_ChangeLootMode(U32 iTeamID, U32 iEntID, LootMode eMode)
{
	static char pcActionType[] = "ChangeLootMode";
	TransactionReturnVal *pReturn = NULL;
	Team *pTeam = aslTeam_GetTeam(iTeamID);
	
	if (aslTeam_CheckExists(pTeam, iTeamID, iEntID, pcActionType) &&
		aslTeam_CheckIsMember(pTeam, iEntID, pcActionType) &&
		aslTeam_CheckIsLeader(pTeam, iEntID, pcActionType) &&
		aslTeam_CheckIsNotLocal(pTeam, iEntID, pcActionType)) {
		
		pReturn = objCreateManagedReturnVal(aslTeam_RemoteCommand_CB, aslTeam_MakeCBData(pcActionType));
		AutoTrans_aslTeam_tr_ChangeLootMode(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, pTeam->iContainerID, iEntID, eMode);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".Pleader.Ientid, .Loot_Mode, .Icontainerid, .Eamembers, .Emode, .Loot_Mode_Quality, .Idifficulty, .pchStatusMessage");
enumTransactionOutcome aslTeam_tr_ChangeLootMode(ATR_ARGS, NOCONST(Team)* pTeamContainer, U32 iEntID, U32 eMode)
{
	TEAM_TRANSACTION_INIT;
	// Make sure the initiator is the leader
	if (!aslTeam_trh_IsLeader(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer)) {
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iEntID, iEntID, 0, "TeamServer_Error_NotTeamLeader", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}
	aslTeam_trh_SetLootMode(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer, eMode);
	
	timingHistoryPush(gActionHistory);
	
	TEAM_TRANSACTION_RETURN_SUCCESS;
}

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslTeam_ChangeDefaultLootMode(U32 iEntID, LootMode eMode)
{
	static char pcActionType[] = "ChangeDefaultLootMode";
	TransactionReturnVal *pReturn = NULL;
	
	pReturn = objCreateManagedReturnVal(aslTeam_RemoteCommand_CB, aslTeam_MakeCBData(pcActionType));
	AutoTrans_aslTeam_tr_ChangeDefaultLootMode(pReturn, GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, iEntID, eMode);
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pteam");
enumTransactionOutcome aslTeam_tr_ChangeDefaultLootMode(ATR_ARGS, NOCONST(Entity)* pEnt, U32 eMode)
{
	TEAM_TRANSACTION_INIT;
	
	if (ISNULL(pEnt->pTeam)) {
		pEnt->pTeam = StructCreateNoConst(parse_PlayerTeam);
	}
	pEnt->pTeam->eLootMode = eMode;
	aslTeam_AddSuccessFeedback(ATR_TEAM_RETURN, pEnt->myContainerID, pEnt->myContainerID, 0, "TeamServer_Success_ChangeDefaultLootMode", false, 0, 0, 0, 0, NULL);
	
	TEAM_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Loot Mode Quality
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslTeam_ChangeLootModeQuality(U32 iTeamID, U32 iEntID, const char* eQuality)
{
	static char pcActionType[] = "ChangeLootModeQuality";
	TransactionReturnVal *pReturn = NULL;
	Team *pTeam = aslTeam_GetTeam(iTeamID);

	if (aslTeam_CheckExists(pTeam, iTeamID, iEntID, pcActionType) &&
		aslTeam_CheckIsMember(pTeam, iEntID, pcActionType) &&
		aslTeam_CheckIsLeader(pTeam, iEntID, pcActionType)  &&
		aslTeam_CheckIsNotLocal(pTeam, iEntID, pcActionType)) {
		
		pReturn = objCreateManagedReturnVal(aslTeam_RemoteCommand_CB, aslTeam_MakeCBData(pcActionType));
		AutoTrans_aslTeam_tr_ChangeLootModeQuality(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, pTeam->iContainerID, iEntID, eQuality);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".Pleader.Ientid, .Loot_Mode_Quality, .Icontainerid, .Eamembers, .Emode, .Loot_Mode, .Idifficulty, .pchStatusMessage");
enumTransactionOutcome aslTeam_tr_ChangeLootModeQuality(ATR_ARGS, NOCONST(Team)* pTeamContainer, U32 iEntID, const char* eQuality)
{
	TEAM_TRANSACTION_INIT;
	// Make sure the initiator is the leader
	if (!aslTeam_trh_IsLeader(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer)) {
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iEntID, iEntID, 0, "TeamServer_Error_NotTeamLeader", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}
	aslTeam_trh_SetLootModeQuality(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer, eQuality);
	
	timingHistoryPush(gActionHistory);
	
	TEAM_TRANSACTION_RETURN_SUCCESS;
}

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslTeam_ChangeDefaultLootModeQuality(U32 iEntID, const char* pcQuality)
{
	static char pcActionType[] = "ChangeDefaultLootModeQuality";
	TransactionReturnVal *pReturn = NULL;
	
	pReturn = objCreateManagedReturnVal(aslTeam_RemoteCommand_CB, aslTeam_MakeCBData(pcActionType));
	AutoTrans_aslTeam_tr_ChangeDefaultLootModeQuality(pReturn, GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, iEntID, pcQuality);
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pteam");
enumTransactionOutcome aslTeam_tr_ChangeDefaultLootModeQuality(ATR_ARGS, NOCONST(Entity)* pEnt, const char* pcQuality)
{
	TEAM_TRANSACTION_INIT;
	
	if (ISNULL(pEnt->pTeam)) {
		pEnt->pTeam = StructCreateNoConst(parse_PlayerTeam);
	}
	pEnt->pTeam->eLootQuality = allocAddString(pcQuality);
	aslTeam_AddSuccessFeedback(ATR_TEAM_RETURN, pEnt->myContainerID, pEnt->myContainerID, 0, "TeamServer_Success_ChangeDefaultLootModeQuality", false, 0, 0, 0, 0, NULL);
	
	TEAM_TRANSACTION_RETURN_SUCCESS;
}



///////////////////////////////////////////////////////////////////////////////////////////
// Release Owned Team
///////////////////////////////////////////////////////////////////////////////////////////

// Release owned team is a very specific operation that is rather delicate. The point is, for
// the given team which is being GameServerOwned (is being controlled by an AutoTeam), to release the
// team from that binding and make it a normal team preserving the current members. This is tricky
// since currently the QueueServer tells the map GameServer who should be on the team. This updates
// a LocalTeam which in turn is sent to the TeamServer to add or drop members. 
//
// This is rather complicated. If we don't do stuff in the right order, we run into problems with the
// QueueServer continuing to tell the instance map GameServer to continue updating the team and we
// will have inconsistent behaviour including some members being booted.
//
// For now, tell the team server to remove the owner status. And when that is complete, have it
// tell each member that they should AbandonMap and not remove themselves from the team. AbandonMap
// will teleport the member off the instance map if they are still there, remove them from the
// queue system, and clear their Instantiation data (which is the "Return To Instance" button).
// By virtue of us waiting for the owned status to be cleared before tampering with the queue membership,
// we can intercept aslTeam_OwnedTeamUpdate from making any more changes to the released team.


void aslTeam_ReleaseTeam_CB(TransactionReturnVal *pReturn, ASLTeamCBData *pData)
{
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		Team *pTeam = aslTeam_GetTeam(pData->iTeamID);
		
		// Abandon the map, but don't leave the team.
		if(pTeam && pTeam->eaMembers)		// This better be true!
		{
			int i;
			bool bDoLeaveTeam=false;
				
			// NOTE!!  We do not handle disconnected players. They will need to
			//  rely on validating the map when they log back in. :-(
			for(i = 0; i < eaSize(&pTeam->eaMembers); ++i)
			{
				RemoteCommand_gslQueue_rcmd_DoAbandonMap(GLOBALTYPE_ENTITYPLAYER,  pTeam->eaMembers[i]->iEntID, pTeam->eaMembers[i]->iEntID, bDoLeaveTeam);
			}
		}
	}
	aslTeam_RemoteCommand_CB(pReturn, pData);
}


AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".Igameserverownerid, .Eamembers, .Emode, .Loot_Mode, .Loot_Mode_Quality, .Idifficulty, .pchStatusMessage");
enumTransactionOutcome aslTeam_tr_ReleaseOwned(ATR_ARGS, NOCONST(Team) *pTeamContainer)
{
	bool bWasOK=true;
	TEAM_TRANSACTION_INIT;

	if (NONNULL(pTeamContainer)) 
	{
		if (pTeamContainer->iGameServerOwnerID != 0)
		{
			pTeamContainer->iGameServerOwnerID = 0;
			aslTeam_trh_AddFeedbackAll(ATR_TEAM_RETURN, pTeamContainer, 0, 0, 0, "TeamServer_Team_ReleaseOwned", "TeamServer_Subject_ReleaseOwned");
		}
		else
		{
			bWasOK=false;
		}
	}

	timingHistoryPush(gActionHistory);
	
	if (bWasOK)
	{
		TEAM_TRANSACTION_RETURN_SUCCESS;
	}
	else
	{
		// Was already unowned
		TEAM_TRANSACTION_RETURN_FAILURE;
	}
}

AUTO_COMMAND_REMOTE;
void aslTeam_ReleaseOwned(U32 iTeamID)
{
	static char pcActionType[] = "ReleaseOwned";
	TransactionReturnVal *pReturn = NULL;
	ASLTeamCBData *pData = NULL;
	Team *pTeam = aslTeam_GetTeam(iTeamID);

	pData = aslTeam_MakeCBData(pcActionType);
	pData->iTeamID = iTeamID;

	pReturn = objCreateManagedReturnVal(aslTeam_ReleaseTeam_CB, pData);
	AutoTrans_aslTeam_tr_ReleaseOwned(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, iTeamID);
}


///////////////////////////////////////////////////////////////////////////////////////////
// Create Team
///////////////////////////////////////////////////////////////////////////////////////////

void aslTeam_AddInitialMembers_CB(TransactionReturnVal *pReturn, ASLTeamCBData *pData)
{
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS) {
		timingHistoryPush(gCreationHistory);
		RemoteCommand_gslTeam_ClaimMap(GLOBALTYPE_ENTITYPLAYER, pData->iEntID, pData->iEntID, GLOBALTYPE_ENTITYPLAYER, pData->iEntID, GLOBALTYPE_TEAM, pData->iTeamID);

		if (pData->pchEOIMapRequest && pData->pchEOIMapRequest[0]) {
			RemoteCommand_gslTeam_OpenInstancingJoinTeamAtMap(GLOBALTYPE_ENTITYPLAYER, pData->iSubjectID, pData->iSubjectID, pData->pchEOIMapRequest, pData->pchEOIMapVars, pData->uMapContainerID, pData->uMapPartitionID);
		}
	} else {
		aslTeam_HandleOpenInstancingError(pData);
		RemoteCommand_aslTeam_DestroyWithReason(GetAppGlobalType(), 0, pData->iTeamID, "Failed to add initial members");
	}
	aslTeam_RemoteCommand_CB(pReturn, pData);
}

void aslTeam_Create_CB(TransactionReturnVal *pReturn, ASLTeamCBData *pData)
{
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		bool bEOIRequest = pData->pchEOIMapRequest && pData->pchEOIMapRequest[0];
		TransactionReturnVal *pNextReturn;
		pData->iTeamID = atoi(pReturn->pBaseReturnVals[0].returnString);

		if (!pData->iEntID || !pData->iSubjectID) {
			aslTeam_SaveLogMessage("Create", "", pData->iTeamID, "ZERO ID ERROR: Attempt to call aslTeam_tr_AddInitialMembers with ENTITYPLAYER[%d] and ENTITYPLAYER[%d]", pData->iEntID, pData->iSubjectID);
			RemoteCommand_aslTeam_Destroy(GetAppGlobalType(), 0, pData->iTeamID);
			aslTeam_HandleOpenInstancingError(pData);
			SAFE_FREE(pData);
			return;
		}
		pNextReturn = objCreateManagedReturnVal(aslTeam_AddInitialMembers_CB, pData);
		AutoTrans_aslTeam_tr_AddInitialMembers(pNextReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, pData->iTeamID, GLOBALTYPE_ENTITYPLAYER, pData->iEntID, GLOBALTYPE_ENTITYPLAYER, pData->iSubjectID, bEOIRequest);
	}
	else 
	{
		aslTeam_HandleOpenInstancingError(pData);
		aslTeam_SendError(pData->iEntID, 0, pData->pcActionType, "TeamServer_Error_Failed");			
		SAFE_FREE(pData);
	}
}

static void aslTeam_Create_Internal(TransactionReturnVal *pReturn, ASLTeamCBData *pData)
{
	NOCONST(Team) *pNewTeam = StructCreateNoConst(parse_Team);
	TransactionReturnVal *pLocalReturn = pReturn;

	pNewTeam->iCreatedOn = timeSecondsSince2000();
	pNewTeam->eMode = TeamMode_Closed;
	pNewTeam->pcAllegiance = pData->pcAllegiance && pData->pcAllegiance[0] ? allocAddString(pData->pcAllegiance) : NULL;
	pNewTeam->pcSubAllegiance = pData->pcSubAllegiance && pData->pcSubAllegiance[0] ? allocAddString(pData->pcSubAllegiance) : NULL;
	pNewTeam->iGameServerOwnerID = pData->iOwningServerID;	// Should only be being used by owned teams from aslTeam_OwnedTeamCreate.
	pNewTeam->bTeamMembersMustBeOnOwnerGameServer = pData->bTeamMembersMustBeOnOwningGameServer; // Whether we enforce that team members must be on the owning server. 
	
	pNewTeam->loot_mode = gConf.pcDefaultLootMode ? StaticDefineIntGetInt(LootModeEnum, gConf.pcDefaultLootMode) : LootMode_NeedOrGreed;
	pNewTeam->loot_mode_quality = allocAddString(gConf.pcNeedBeforeGreedThreshold ? gConf.pcNeedBeforeGreedThreshold : FALLBACK_NEEDORGREED_THRESHOLD);

	if(!pLocalReturn)
		pLocalReturn = objCreateManagedReturnVal(aslTeam_Create_CB, pData);

	objRequestContainerCreate(pLocalReturn, GLOBALTYPE_TEAM, pNewTeam, objServerType(), objServerID());

	StructDestroyNoConst(parse_Team, pNewTeam);
}

// Use this when you need to call aslTeam_Create_Internal() as a transaction callback.  This ensures that
//  the same TransactionReturnVal doesn't get used twice.
void aslTeam_Create_Internal_CB(TransactionReturnVal *pReturn, ASLTeamCBData *pData)
{
    aslTeam_Create_Internal(NULL, pData);
}

AUTO_COMMAND_REMOTE;
void aslTeam_Create(U32 iLeaderID, U32 iInviteID, const char* pchEOIMapRequest, const char* pchEOIMapVars, U32 uMapContainerID, U32 uMapPartitionID, const char *pcAllegiance, const char *pcSubAllegiance)
{
	ASLTeamCBData *pData = NULL;

	if (aslTeam_CheckSubjectIsNotSelf(NULL, iLeaderID, iInviteID, "Create")) {
		pData = aslTeam_MakeCBData("Create");
		pData->iEntID = iLeaderID;
		pData->iSubjectID = iInviteID;
		pData->pchEOIMapRequest = pchEOIMapRequest && pchEOIMapRequest[0] ? StructAllocString(pchEOIMapRequest) : NULL;
		pData->pchEOIMapVars = pchEOIMapVars && pchEOIMapVars[0] ? StructAllocString(pchEOIMapVars) : NULL;
		pData->uMapContainerID = uMapContainerID;
		pData->uMapPartitionID = uMapPartitionID;
		pData->pcAllegiance = pcAllegiance && pcAllegiance[0] ? StructAllocString(pcAllegiance) : NULL;
		pData->pcSubAllegiance = pcSubAllegiance && pcSubAllegiance[0] ? StructAllocString(pcSubAllegiance) : NULL;
		pData->bTeamMembersMustBeOnOwningGameServer = true; // Whether we enforce that team members must be on the owning server. 
		
		aslTeam_Create_Internal(NULL, pData);
	}
	else if (pchEOIMapRequest && pchEOIMapRequest[0])
	{
		RemoteCommand_gslTeam_JoinOpenTeamByMap_Error(GLOBALTYPE_ENTITYPLAYER, iInviteID, iInviteID, pchEOIMapRequest, pchEOIMapVars);
	}
}

static void CreateOwned_CB(TransactionReturnVal *pVal, ASLTeamCBData *pData)
{
	if(pData) {
		U32 iTeamID = atoi(pVal->pBaseReturnVals[0].returnString);
		RemoteCommand_gslTeam_CreateOwnedCallback(GLOBALTYPE_GAMESERVER, pData->iOwningServerID, pData->iPartitionID, iTeamID, pData->iSubjectID);
		StructDestroy(parse_ASLTeamCBData, pData);
	}
}

// attempts to convert the given team to an owned team, otherwise just creates a new team 
AUTO_COMMAND_REMOTE;
void aslTeam_OwnedTeamCreate(U32 iServerID, U32 iReturnID, U32 iPartitionID, bool bTeamMembersMustBeOnOwningGameServer)
{
	ASLTeamCBData *pData = aslTeam_MakeCBData("CreateOwned");
	TransactionReturnVal *pReturnVal = objCreateManagedReturnVal(CreateOwned_CB, pData);
	pData->bFeedback = false;
	pData->iSubjectID = iReturnID;
	pData->iPartitionID = iPartitionID;
	pData->iOwningServerID = iServerID;
	pData->bTeamMembersMustBeOnOwningGameServer = bTeamMembersMustBeOnOwningGameServer;  // Whether we enforce that team members must be on the owning server. 
	
	aslTeam_Create_Internal(pReturnVal, pData);
}



AUTO_COMMAND_REMOTE;
void aslTeam_OwnedTeamUpdate(U32 iTeamID, IntEarrayWrapper *pList, U32 uiRequestID)
{
	S32 i, j;
	Team *pTeam = aslTeam_GetTeam(iTeamID);
	if(!pTeam)
	{
		return;
	}

	if (pTeam->iGameServerOwnerID==0)
	{
		// We are not really owned now. Likely the team was released from an ownership situation and the
		//  fact has not propagated to all appropriate gameServers, etc. Don't do any changes to the team.
		return;
	}
	
	// Remove members that aren't supposed to be on the team
	for (i=0;i<eaSize(&pTeam->eaMembers);i++)
	{
		bool bFound=false;
		ContainerID iMemberID = pTeam->eaMembers[i]->iEntID;
		for (j=0;j<eaiSize(&pList->eaInts);j++)
		{
			ContainerID iNewMemberID = (ContainerID)pList->eaInts[j];
			if (iMemberID == iNewMemberID)
			{
				bFound=true;
				break;
			}
		}
		if (!bFound)
		{
			AutoTrans_aslTeam_tr_Leave(NULL, GetAppGlobalType(),
										GLOBALTYPE_TEAM, iTeamID,
										GLOBALTYPE_ENTITYPLAYER, iMemberID, uiRequestID);
		}
	}

	// Add new members to the team
	for (j=0;j<eaiSize(&pList->eaInts);j++)
	{
		ContainerID iNewMemberID = (ContainerID)pList->eaInts[j];
		if(!aslTeam_IsMember(pTeam, iNewMemberID)) {
			if ( aslTeam_CheckIsNotMember(pTeam, iNewMemberID, "Join") &&
				 aslTeam_CheckIsNotFull(pTeam, iNewMemberID, "Join"))
			{
				int iWasDisconnected=0;
				int iOldTeam=0;
				TransactionReturnVal *pReturn = NULL;
				ASLTeamCBData *pData = aslTeam_MakeCBData("");	// We do not run this through the normal Remote CB. No actions are processed
				
				iOldTeam=aslTeam_GetStashedMemberTeamID(iNewMemberID);
				if (iOldTeam==0)
				{
					iOldTeam=aslTeam_GetStashedDisconnectedTeamID(iNewMemberID);
					if (iOldTeam!=0)
					{
						iWasDisconnected=1;
					}
				}
				if (aslTeam_GetTeam(iOldTeam)==NULL)
				{
					// Safety check. Theoretically this should never happen, but just in case
					//  our stashes are stale, we can't try to pass in the OldTeam.
					iOldTeam=0;
					iWasDisconnected=0;
				}

				pData->iEntID = iNewMemberID;
				pData->iTeamID = iTeamID;
				pData->iWasDisconnected=iWasDisconnected;
				pReturn = objCreateManagedReturnVal(aslTeam_ForceJoin_CB, pData);

				AutoTrans_aslTeam_tr_ForceJoin(pReturn, GetAppGlobalType(),
										GLOBALTYPE_TEAM, iTeamID,
										GLOBALTYPE_TEAM, iOldTeam,
									    GLOBALTYPE_ENTITYPLAYER, iNewMemberID,
										iWasDisconnected, uiRequestID);
			}
		}
	}
}

AUTO_COMMAND_REMOTE;
void aslTeam_OwnedTeamAcknowledge(U32 iTeamID, bool bFound)
{
	Team *pTeam = aslTeam_GetTeam(iTeamID);
	if(pTeam) {
		if(bFound) {
			pTeam->iOwnedLastUpdate = timeSecondsSince2000();
		} else {
			aslTeam_DestroyWithReason(iTeamID, "Couldn't find local team");
		}
	}
}

AUTO_COMMAND_REMOTE;
void aslTeam_CancelAndCreate(U32 iTeamID, U32 iLeaderID, U32 iInviteID, const char* pchEOIMapRequest, const char* pchEOIMapVars, U32 uMapContainerID, U32 uMapPartitionID, const char *pcAllegiance, const char *pcSubAllegiance)
{
	ASLTeamCBData *pData = NULL;

	if (aslTeam_CheckSubjectIsNotSelf(NULL, iLeaderID, iInviteID, "Create")) {
		Team *pTeam = aslTeam_GetTeam(iTeamID);

		pData = aslTeam_MakeCBData("Create");
		pData->iEntID = iLeaderID;
		pData->iSubjectID = iInviteID;
		pData->pchEOIMapRequest = pchEOIMapRequest && pchEOIMapRequest[0] ? StructAllocString(pchEOIMapRequest) : NULL;
		pData->pchEOIMapVars = pchEOIMapVars && pchEOIMapVars[0] ? StructAllocString(pchEOIMapVars) : NULL;
		pData->uMapContainerID = uMapContainerID;
		pData->uMapPartitionID = uMapPartitionID;
		pData->pcAllegiance = pcAllegiance && pcAllegiance[0] ? StructAllocString(pcAllegiance) : NULL;
		pData->pcSubAllegiance = pcSubAllegiance && pcSubAllegiance[0] ? StructAllocString(pcSubAllegiance) : NULL;
		pData->bTeamMembersMustBeOnOwningGameServer = true; // Whether we enforce that team members must be on the owning server. 

		if (pTeam && aslTeam_IsRequest(pTeam, iLeaderID))
		{
			TransactionReturnVal *pReturn = objCreateManagedReturnVal(aslTeam_Create_Internal_CB, pData);
			AutoTrans_aslTeam_tr_CancelRequest(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, iTeamID, GLOBALTYPE_ENTITYPLAYER, iLeaderID);
		}
		else
		{
			aslTeam_Create_Internal(NULL, pData);
		}
	}
}


AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".Emode, .Loot_Mode, .Loot_Mode_Quality, .Idifficulty, .Pcallegiance, .Pcsuballegiance, .Brequiresuballegiancematch, .Iversion, .Icontainerid, .Igameserverownerid, .Eamembers, .Eadisconnecteds, .Eainvites, .Pleader, .Pxsessioninfo, .pchStatusMessage, .uBadLogoutTime")
ATR_LOCKS(pLeader, ".Pplayer.Bautojointeamvoicechat, .Pplayer.Idifficulty, .Psaved.Savedname, .Hallegiance, .Hsuballegiance, .Pteam, .Pplayer.Accountid, .Pplayer.Publicaccountname")
ATR_LOCKS(pInvite, ".Hallegiance, .Hsuballegiance, .Pplayer.Bautojointeamvoicechat, .Psaved.Savedname, .Pteam, .Pplayer.Accountid, .Pplayer.Publicaccountname");
enumTransactionOutcome aslTeam_tr_AddInitialMembers(ATR_ARGS, NOCONST(Team) *pTeamContainer, NOCONST(Entity) *pLeader, NOCONST(Entity) *pInvite, U8 bEOIRequest)
{
	TEAM_TRANSACTION_INIT;
	
	// Make sure that the leader isn't already on a team
	if (NONNULL(pLeader->pTeam) && pLeader->pTeam->iTeamID) {
		if (pLeader->pTeam->eState == TeamState_Member) {
			aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, pLeader->myContainerID, pLeader->myContainerID, pInvite->myContainerID, "TeamServer_Error_SelfAlreadyOnTeam", true, 0, 0, 0, 0, NULL);
			TEAM_TRANSACTION_RETURN_FAILURE;
		}
		if (pLeader->pTeam->eState == TeamState_Invitee) {
			aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, pLeader->myContainerID, pLeader->myContainerID, pInvite->myContainerID, "TeamServer_Error_SelfAlreadyInvited", true, 0, 0, 0, 0, NULL);
			TEAM_TRANSACTION_RETURN_FAILURE;
		}
		if (pLeader->pTeam->eState == TeamState_Requester) {
			aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, pLeader->myContainerID, pLeader->myContainerID, pInvite->myContainerID, "TeamServer_Error_SelfAlreadyRequesting", true, 0, 0, 0, 0, NULL);
			TEAM_TRANSACTION_RETURN_FAILURE;
		}
	}
	// Make sure that the invite isn't already on a team
	if (NONNULL(pInvite->pTeam) && pInvite->pTeam->iTeamID) {
		if (pInvite->pTeam->eState == TeamState_Member) {
			aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, pLeader->myContainerID, pLeader->myContainerID, pInvite->myContainerID, "TeamServer_Error_AlreadyOnTeam", true, 0, 0, 0, 0, NULL);
			TEAM_TRANSACTION_RETURN_FAILURE;
		}
		if (pInvite->pTeam->eState == TeamState_Invitee) {
			aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, pLeader->myContainerID, pLeader->myContainerID, pInvite->myContainerID, "TeamServer_Error_AlreadyInvited", true, 0, 0, 0, 0, NULL);
			TEAM_TRANSACTION_RETURN_FAILURE;
		}
		if (pInvite->pTeam->eState == TeamState_Requester) {
			aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, pLeader->myContainerID, pLeader->myContainerID, pInvite->myContainerID, "TeamServer_Error_AlreadyRequesting", true, 0, 0, 0, 0, NULL);
			TEAM_TRANSACTION_RETURN_FAILURE;
		}
	}
	// Make sure leader's allegiance matches team's
	if (!aslTeam_trh_CheckAllegiance(pTeamContainer, pLeader))
	{
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, pLeader->myContainerID, pLeader->myContainerID, pInvite->myContainerID, "TeamServer_Error_AllegianceMismatch", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}

	// Make sure invitee's allegiance matches team's
	if (!aslTeam_trh_CheckAllegiance(pTeamContainer, pInvite))
	{
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, pLeader->myContainerID, pLeader->myContainerID, pInvite->myContainerID, "TeamServer_Error_AllegianceMismatch", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}

	if (NONNULL(pLeader->pTeam)) {
		pTeamContainer->eMode = pLeader->pTeam->eMode;
		pTeamContainer->loot_mode = pLeader->pTeam->eLootMode;
		pTeamContainer->loot_mode_quality = allocAddString(pLeader->pTeam->eLootQuality);
	}
	if (NONNULL(pLeader->pPlayer)) {
		pTeamContainer->iDifficulty = pLeader->pPlayer->iDifficulty;
	}
	aslTeam_trh_AddMember(TEAM_TRH_PASS_ARGS, pTeamContainer, pLeader, bEOIRequest);

	if (bEOIRequest && (pTeamContainer->eMode == TeamMode_Closed || pTeamContainer->eMode == TeamMode_RequestOnly))
	{
		// If the team mode has changed, just fail it now
		TEAM_TRANSACTION_RETURN_FAILURE;
	}

	if (bEOIRequest) {
		aslTeam_trh_AddMember(TEAM_TRH_PASS_ARGS, pTeamContainer, pInvite, bEOIRequest);
	} else {
		aslTeam_trh_AddInvite(TEAM_TRH_PASS_ARGS, pLeader->myContainerID, pTeamContainer, pInvite);
	}
	aslTeam_trh_UpdateLeader(TEAM_TRH_PASS_ARGS, pTeamContainer, true);
	
	TEAM_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Put Entity in Logged Out State
///////////////////////////////////////////////////////////////////////////////////////////
// This is used to denote a player that logged out improperly, so that when they log back
// in they will automatically attempt to rejoin their team.

AUTO_COMMAND_REMOTE;
void aslTeam_DoLogout(U32 iTeamID, GlobalType eEntType, U32 iEntID, bool bBadLogout)
{
	static char pcActionTypeBad[] = "BadLogout";
	static char pcActionTypeGood[] = "Logout";
	char *pcActionType = bBadLogout ? pcActionTypeBad : pcActionTypeGood;
	
	TransactionReturnVal *pReturn = NULL;
	Team *pTeam = aslTeam_GetTeam(iTeamID);

	if (!(bBadLogout || gConf.bKeepOnTeamOnLogout))
	{
		// Old style. Treat it as a leave
			
		aslTeam_Leave(iTeamID, iEntID, iEntID, false);
		return;
	}

	// Either bad or good logout
	
	if (aslTeam_CheckExists(pTeam, iTeamID, iEntID, pcActionType) &&
		aslTeam_CheckIsMember(pTeam, iEntID, pcActionType)) {
		
		pReturn = objCreateManagedReturnVal(aslTeam_RemoteCommand_CB, aslTeam_MakeCBData(pcActionType));
		AutoTrans_aslTeam_tr_DoLogout(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, iTeamID, GLOBALTYPE_ENTITYPLAYER, iEntID, bBadLogout);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".Eamembers, .Eadisconnecteds, .Icontainerid, .Iversion, .Pchampion, .Emode, .Loot_Mode, .Loot_Mode_Quality, .Idifficulty, .Pleader, .Pxsessioninfo, .pchStatusMessage, .uBadLogoutTime, .Pcallegiance, .Pcsuballegiance, .Igameserverownerid, .Brequiresuballegiancematch")
ATR_LOCKS(pEnt, ".Pteam.Irejoinid, .Pteam.Iteamid, .Pteam.Pcinvitername, .Pteam.Ilogouttime, .Pteam.Estate, .Pplayer.Accountid");
enumTransactionOutcome aslTeam_tr_DoLogout(ATR_ARGS, NOCONST(Team) *pTeamContainer, NOCONST(Entity) *pEnt, U32 uBadLogout)
{
	TEAM_TRANSACTION_INIT;
	
	// Make sure the player was on the team
	if (!aslTeam_trh_FindMember(TEAM_TRH_PASS_ARGS, pEnt->myContainerID, pTeamContainer)) {
		aslTeam_trh_ClearPlayerTeam(TEAM_TRH_PASS_ARGS, pEnt);
		TEAM_TRANSACTION_RETURN_SUCCESS;
	}
	
	aslTeam_trh_SetAsLoggedOut(TEAM_TRH_PASS_ARGS, pEnt, pTeamContainer, uBadLogout);
	
	TEAM_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Clear Player Team Data
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pteam.Iteamid, .Pteam.Pcinvitername, .Pteam.Estate, .Pplayer.Accountid");
enumTransactionOutcome aslTeam_tr_ClearPlayerTeam(ATR_ARGS, NOCONST(Entity) *pEnt)
{
	TEAM_TRANSACTION_INIT;
	aslTeam_trh_ClearPlayerTeam(TEAM_TRH_PASS_ARGS, pEnt);
	TEAM_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Validation Commands
///////////////////////////////////////////////////////////////////////////////////////////
// These commands each validate a particular aspect of the team, to be called when it's
// suspected that this may be incorrect. Each of these commands will correct any
// inconsistencies is finds.
//
// ValidateOnline - Validates that the player is online
// ValidateExists - Validates that the team exists
// ValidateMember - Validates that the player and team agree about the player's membership
// ValidateInvite - Validates that the player and team agree about the player's invitation
// ValidateRequest - Validates that the player and team agree about the player's request

void aslTeam_ValidateOnline_CB(TransactionReturnVal *pReturn, ASLTeamCBData *pData)
{
	ContainerRef *pContainerRef = NULL;
	TransactionReturnVal *pNewReturn = NULL;
	
	if (RemoteCommandCheck_ContainerGetOwner(pReturn, &pContainerRef) == TRANSACTION_OUTCOME_SUCCESS) {
		if (pContainerRef->containerType == GLOBALTYPE_OBJECTDB) {
			pReturn = objCreateManagedReturnVal(aslTeam_RemoteCommand_CB, pData);
			// WOLF[30Jan13] We used to _Leave. Now run through DoLogout so we take advantage of Disconnected management if it is enabled
			AutoTrans_aslTeam_tr_DoLogout(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, pData->iTeamID, GLOBALTYPE_ENTITYPLAYER, pData->iEntID, false);
//			AutoTrans_aslTeam_tr_Leave(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, pData->iTeamID, GLOBALTYPE_ENTITYPLAYER, pData->iEntID);
			return;
		}
	}
	
	StructDestroy(parse_ASLTeamCBData, pData);
}

AUTO_COMMAND_REMOTE;
void aslTeam_ValidateOnline(U32 iTeamID, U32 iEntID)
{
	ASLTeamCBData *pData = NULL;
	TransactionReturnVal *pReturn = NULL;
	Team *pTeam = aslTeam_GetTeam(iTeamID);
	
	if (aslTeam_CheckExists(pTeam, iTeamID, iEntID, "ValidateOnline")) {
		pData = aslTeam_MakeCBData(NULL);
		pData->iTeamID = iTeamID;
		pData->iEntID = iEntID;
		pReturn = objCreateManagedReturnVal(aslTeam_ValidateOnline_CB, pData);
		RemoteCommand_ContainerGetOwner(pReturn, GLOBALTYPE_ENTITYPLAYER, iEntID);
	}
}

void aslTeam_ValidateExists_CB(TransactionReturnVal *pReturn, ASLTeamCBData *pData)
{
	ContainerRef *pContainerRef = NULL;
	TransactionReturnVal *pNewReturn = NULL;
	
	if (RemoteCommandCheck_ContainerGetOwner(pReturn, &pContainerRef) == TRANSACTION_OUTCOME_FAILURE) {
		char pcName[32];
		sprintf(pcName, "?@?[%d]", pData->iEntID);
		pReturn = objCreateManagedReturnVal(aslTeam_RemoteCommand_CB, pData);
		AutoTrans_aslTeam_tr_ClearPlayerTeam(pReturn, GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, pData->iEntID);
		aslTeam_SaveLogMessage("ValidateExists", pcName, pData->iTeamID, "OUT OF SYNC ERROR: Player on a team that doesn't exist. Fixing.");
	} else {
		StructDestroy(parse_ASLTeamCBData, pData);
	}
}

AUTO_COMMAND_REMOTE;
void aslTeam_ValidateExists(U32 iTeamID, U32 iEntID)
{
	ASLTeamCBData *pData = NULL;
	TransactionReturnVal *pReturn = NULL;
	Team *pTeam = aslTeam_GetTeam(iTeamID);
	
	if (pTeam) {
		return;
	}
	
	pData = aslTeam_MakeCBData(NULL);
	pData->iTeamID = iTeamID;
	pData->iEntID = iEntID;
	pReturn = objCreateManagedReturnVal(aslTeam_ValidateExists_CB, pData);
	RemoteCommand_ContainerGetOwner(pReturn, GLOBALTYPE_TEAM, iTeamID);
}

AUTO_COMMAND_REMOTE;
void aslTeam_ValidateMember(U32 iTeamID, U32 iEntID)
{
	TransactionReturnVal *pReturn = NULL;
	Team *pTeam = aslTeam_GetTeam(iTeamID);
	
	if (!iEntID) {
		return;
	}
	
	if (aslTeam_CheckExists(pTeam, iTeamID, iEntID, "ValidateMember")) {
		pReturn = objCreateManagedReturnVal(aslTeam_RemoteCommand_CB, aslTeam_MakeCBData("ValidateMember"));
		AutoTrans_aslTeam_tr_ValidateMember(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, iTeamID, GLOBALTYPE_ENTITYPLAYER, iEntID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".Eamembers, .Icontainerid, .Iversion, .Pleader, .Pxsessioninfo")
ATR_LOCKS(pEnt, ".Pteam.Pcinvitername, .Pteam.Iteamid, .Pteam.Estate, .Pplayer.Accountid");
enumTransactionOutcome aslTeam_tr_ValidateMember(ATR_ARGS, NOCONST(Team) *pTeamContainer, NOCONST(Entity) *pEnt) {
	TEAM_TRANSACTION_INIT;
	int i;
	
	for (i = eaSize(&pTeamContainer->eaMembers)-1; i >= 0; i--) {
		if (pTeamContainer->eaMembers[i]->iEntID == pEnt->myContainerID) {
			if (ISNULL(pEnt->pTeam) || pEnt->pTeam->iTeamID != pTeamContainer->iContainerID || pEnt->pTeam->eState != TeamState_Member) {
				StructDestroyNoConst(parse_TeamMember, pTeamContainer->eaMembers[i]);
				eaRemove(&pTeamContainer->eaMembers, i);
				pTeamContainer->iVersion++;

				if (NONNULL(pEnt->pPlayer))
				{
					aslTeam_AddReturnActionEx(ATR_TEAM_RETURN, TEAM_TRANSACTION_RETURN_ACTION_TYPE_LEAVE_TEAM_CHAT, 
						pTeamContainer->iContainerID, pEnt->myContainerID, pEnt->pPlayer->accountID);
				}
				aslTeam_trh_UpdateLeader(TEAM_TRH_PASS_ARGS, pTeamContainer, false);
				aslTeam_AddSuccessLogMessage(ATR_TEAM_RETURN, pEnt->debugName, pTeamContainer->iContainerID, "OUT OF SYNC ERROR: Player is not tagged as a member of this team. Fixing.");
				TEAM_TRANSACTION_RETURN_SUCCESS;
			}
			break;
		}
	}
	
	if (i < 0 && NONNULL(pEnt->pTeam) && pEnt->pTeam->iTeamID == pTeamContainer->iContainerID && pEnt->pTeam->eState == TeamState_Member) {
		aslTeam_AddSuccessLogMessage(ATR_TEAM_RETURN, pEnt->debugName, pTeamContainer->iContainerID, "OUT OF SYNC ERROR: Team does not have this player as a member. Fixing.");
		aslTeam_trh_ClearPlayerTeam(TEAM_TRH_PASS_ARGS, pEnt);
	}
	
	TEAM_TRANSACTION_RETURN_SUCCESS;
}

AUTO_COMMAND_REMOTE;
void aslTeam_ValidateInvite(U32 iTeamID, U32 iEntID)
{
	TransactionReturnVal *pReturn = NULL;
	Team *pTeam = aslTeam_GetTeam(iTeamID);
	
	if (!iEntID) {
		return;
	}
	
	if (aslTeam_CheckExists(pTeam, iTeamID, iEntID, "ValidateInvite")) {
		pReturn = objCreateManagedReturnVal(aslTeam_RemoteCommand_CB, aslTeam_MakeCBData("ValidateInvite"));
		AutoTrans_aslTeam_tr_ValidateInvite(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, iTeamID, GLOBALTYPE_ENTITYPLAYER, iEntID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".Eainvites, .Icontainerid, .Iversion")
ATR_LOCKS(pEnt, ".Pteam.Pcinvitername, .Pteam.Iteamid, .Pteam.Estate, .Pplayer.Accountid");
enumTransactionOutcome aslTeam_tr_ValidateInvite(ATR_ARGS, NOCONST(Team) *pTeamContainer, NOCONST(Entity) *pEnt) {
	TEAM_TRANSACTION_INIT;
	int i;
	
	for (i = eaSize(&pTeamContainer->eaInvites)-1; i >= 0; i--) {
		if (pTeamContainer->eaInvites[i]->iEntID == pEnt->myContainerID) {
			if (ISNULL(pEnt->pTeam) || pEnt->pTeam->iTeamID != pTeamContainer->iContainerID || pEnt->pTeam->eState != TeamState_Invitee) {
				StructDestroyNoConst(parse_TeamMember, pTeamContainer->eaInvites[i]);
				eaRemove(&pTeamContainer->eaInvites, i);
				pTeamContainer->iVersion++;
				aslTeam_AddSuccessLogMessage(ATR_TEAM_RETURN, pEnt->debugName, pTeamContainer->iContainerID, "OUT OF SYNC ERROR: Player is not tagged as an invite to this team. Fixing.");
				TEAM_TRANSACTION_RETURN_SUCCESS;
			}
			break;
		}
	}
	
	if (i < 0 && NONNULL(pEnt->pTeam) && pEnt->pTeam->iTeamID == pTeamContainer->iContainerID && pEnt->pTeam->eState == TeamState_Invitee) {
		aslTeam_AddSuccessLogMessage(ATR_TEAM_RETURN, pEnt->debugName, pTeamContainer->iContainerID, "OUT OF SYNC ERROR: Team does not have this player as an invite. Fixing.");
		aslTeam_trh_ClearPlayerTeam(TEAM_TRH_PASS_ARGS, pEnt);
	}
	
	TEAM_TRANSACTION_RETURN_SUCCESS;
}

AUTO_COMMAND_REMOTE;
void aslTeam_ValidateRequest(U32 iTeamID, U32 iEntID)
{
	TransactionReturnVal *pReturn = NULL;
	Team *pTeam = aslTeam_GetTeam(iTeamID);
	
	if (!iEntID) {
		return;
	}
	
	if (aslTeam_CheckExists(pTeam, iTeamID, iEntID, "ValidateRequest")) {
		pReturn = objCreateManagedReturnVal(aslTeam_RemoteCommand_CB, aslTeam_MakeCBData("ValidateRequest"));
		AutoTrans_aslTeam_tr_ValidateRequest(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, iTeamID, GLOBALTYPE_ENTITYPLAYER, iEntID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".Earequests, .Icontainerid, .Iversion")
ATR_LOCKS(pEnt, ".Pteam.Pcinvitername, .Pteam.Iteamid, .Pteam.Estate, .Pplayer.Accountid");
enumTransactionOutcome aslTeam_tr_ValidateRequest(ATR_ARGS, NOCONST(Team) *pTeamContainer, NOCONST(Entity) *pEnt) {
	TEAM_TRANSACTION_INIT;
	int i;
	
	for (i = eaSize(&pTeamContainer->eaRequests)-1; i >= 0; i--) {
		if (pTeamContainer->eaRequests[i]->iEntID == pEnt->myContainerID) {
			if (ISNULL(pEnt->pTeam) || pEnt->pTeam->iTeamID != pTeamContainer->iContainerID || pEnt->pTeam->eState != TeamState_Requester) {
				StructDestroyNoConst(parse_TeamMember, pTeamContainer->eaRequests[i]);
				eaRemove(&pTeamContainer->eaRequests, i);
				pTeamContainer->iVersion++;
				aslTeam_AddSuccessLogMessage(ATR_TEAM_RETURN, pEnt->debugName, pTeamContainer->iContainerID, "OUT OF SYNC ERROR: Player is not tagged as requesting this team. Fixing.");
				TEAM_TRANSACTION_RETURN_SUCCESS;
			}
			break;
		}
	}
	
	if (i < 0 && NONNULL(pEnt->pTeam) && pEnt->pTeam->iTeamID == pTeamContainer->iContainerID && pEnt->pTeam->eState == TeamState_Requester) {
		aslTeam_AddSuccessLogMessage(ATR_TEAM_RETURN, pEnt->debugName, pTeamContainer->iContainerID, "OUT OF SYNC ERROR: Team does not have this player as a request. Fixing.");
		aslTeam_trh_ClearPlayerTeam(TEAM_TRH_PASS_ARGS, pEnt);
	}
	
	TEAM_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Saved Pet team management
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_TRANSACTION
ATR_LOCKS(pTeam, ".Eaawayteamprefs");
enumTransactionOutcome aslTeam_tr_SaveAwayTeamPreferences(ATR_ARGS, 
														  NOCONST(Team)* pTeam, 
														  AwayTeamMembers* pAwayTeamMembers)
{
	TEAM_TRANSACTION_INIT;

	if (NONNULL(pTeam) && NONNULL(pAwayTeamMembers))
	{
		S32 i, j, k;
		S32 iAwayTeamSize = eaSize(&pAwayTeamMembers->eaMembers);
		S32 iAwayTeamPlayerCount = 0;

		if (iAwayTeamSize > 0)
		{
			for (i = 0; i < iAwayTeamSize; i++)
			{
				if (pAwayTeamMembers->eaMembers[i]->eEntType == GLOBALTYPE_ENTITYPLAYER)
					iAwayTeamPlayerCount++;
			}

			for (i = eaSize(&pTeam->eaAwayTeamPrefs)-1; i >= 0; i--)
			{
				NOCONST(AwayTeamPrefs)* pAwayTeamPrefs = pTeam->eaAwayTeamPrefs[i];
				S32 iPrefsMembersSize = eaSize(&pAwayTeamPrefs->eaTeamMembers);
				S32 iPrefsPlayerCount = 0;
				S32 iPrefsMatchCount = 0;

				for (j = 0; j < iPrefsMembersSize; j++)
				{
					if (pAwayTeamPrefs->eaTeamMembers[j]->eEntType == GLOBALTYPE_ENTITYPLAYER)
						iPrefsPlayerCount++;
				}

				if (iPrefsPlayerCount == iAwayTeamPlayerCount)
				{
					for (j = 0; j < iPrefsMembersSize; j++)
					{
						NOCONST(AwayTeamMemberPref)* pMemberPref = pAwayTeamPrefs->eaTeamMembers[j];

						if (pMemberPref->eEntType != GLOBALTYPE_ENTITYPLAYER)
							continue;

						for (k = 0; k < iAwayTeamSize; k++)
						{
							AwayTeamMember* pAwayTeamMember = pAwayTeamMembers->eaMembers[k];

							if (pAwayTeamMember->eEntType != GLOBALTYPE_ENTITYPLAYER)
								continue;

							if (pMemberPref->iEntID == pAwayTeamMember->iEntID)
							{
								iPrefsMatchCount++;
								break;
							}
						}

						if (k == iAwayTeamSize)
							break;
					}
				}

				if (iPrefsPlayerCount == iAwayTeamPlayerCount && 
					iPrefsMatchCount == iPrefsPlayerCount)
				{
					TEAM_TRANSACTION_RETURN_SUCCESS("TeamServer_Success_SavedAwayTeamPreferences");
				}
				else if (iPrefsMatchCount > 0)
				{
					StructDestroyNoConst(parse_AwayTeamPrefs, eaRemoveFast(&pTeam->eaAwayTeamPrefs, i));
				}
			}
			{
				NOCONST(AwayTeamPrefs)* pPrefs = StructCreateNoConst(parse_AwayTeamPrefs);

				for (i = 0; i < iAwayTeamSize; i++)
				{
					AwayTeamMember* pAwayTeamMember = pAwayTeamMembers->eaMembers[i];
					NOCONST(AwayTeamMemberPref)* pMember = StructCreateNoConst(parse_AwayTeamMemberPref);

					pMember->eEntType = pAwayTeamMember->eEntType;
					pMember->iEntID = pAwayTeamMember->iEntID;
					pMember->uiCritterPetID = pAwayTeamMember->uiCritterPetID;
					
					eaPush(&pPrefs->eaTeamMembers, pMember);
				}
				eaPush(&pTeam->eaAwayTeamPrefs, pPrefs);
			}
		}
		TEAM_TRANSACTION_RETURN_SUCCESS("TeamServer_Success_SavedAwayTeamPreferences");
	}
	TEAM_TRANSACTION_RETURN_FAILURE("TeamServer_Failure_NoAwayTeamFound");
}


AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void aslTeam_SaveAwayTeamPreferences( U32 iTeamID, AwayTeamMembers* pAwayTeamMembers )
{
	ASLTeamCBData *pData = NULL;
	TransactionReturnVal *pReturn = NULL;

	Team *pTeam = aslTeam_GetTeam(iTeamID);

	if ( pTeam==NULL )
		return;

	pData = aslTeam_MakeCBData(NULL);
	pReturn = objCreateManagedReturnVal(aslTeam_RemoteCommand_CB, pData);

	AutoTrans_aslTeam_tr_SaveAwayTeamPreferences( pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, iTeamID, pAwayTeamMembers );
}

AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".Pchprimarymission");
enumTransactionOutcome aslTeam_tr_SetPrimaryMission(ATR_ARGS, NOCONST(Team) *pTeamContainer, const char *pcPrimaryMission)
{
	TEAM_TRANSACTION_INIT;
	
	if (pcPrimaryMission && pcPrimaryMission[0]){
		pTeamContainer->pchPrimaryMission = allocAddString(pcPrimaryMission);
	} else {
		pTeamContainer->pchPrimaryMission = NULL;
	}

	TEAM_TRANSACTION_RETURN_SUCCESS;
}

AUTO_COMMAND_REMOTE;
void aslTeam_SetPrimaryMission(U32 iTeamID, const char *pcPrimaryMission)
{
	Team *pTeam = aslTeam_GetTeam(iTeamID);

	if(pTeam)
	{
		AutoTrans_aslTeam_tr_SetPrimaryMission(NULL, GetAppGlobalType(), GLOBALTYPE_TEAM, iTeamID, pcPrimaryMission);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// XBOX/Voice Chat Related
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslTeam_JoinVoiceChat(U32 iTeamID, U32 iEntID)
{
	ASLTeamCBData *pData = NULL;
	TransactionReturnVal *pReturn = NULL;

	// Get the team and validate
	Team *pTeam = aslTeam_GetTeam(iTeamID);
	if (pTeam == NULL || !aslTeam_IsMember(pTeam, iEntID))
	{
		return;
	}

	pData = aslTeam_MakeCBData(NULL);
	pReturn = objCreateManagedReturnVal(aslTeam_RemoteCommand_CB, pData);

	AutoTrans_aslTeam_tr_SetVoiceChatFlag(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, iTeamID, iEntID, (U8)true);
}

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslTeam_LeaveVoiceChat(U32 iTeamID, U32 iEntID)
{
	ASLTeamCBData *pData = NULL;
	TransactionReturnVal *pReturn = NULL;

	// Get the team and validate
	Team *pTeam = aslTeam_GetTeam(iTeamID);

	if (pTeam == NULL || !aslTeam_IsMember(pTeam, iEntID))
	{
		return;
	}

	pData = aslTeam_MakeCBData(NULL);
	pReturn = objCreateManagedReturnVal(aslTeam_RemoteCommand_CB, pData);

	AutoTrans_aslTeam_tr_SetVoiceChatFlag(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, iTeamID, iEntID, (U8)false);
}

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK ACMD_IFDEF(GAMESERVER);
void aslTeam_SetXBoxOnlineSessionId(U32 iTeamID, U32 iEntID, CrypticXSessionInfo *pXSessionInfo)
{
	// Get the team and validate
	Team *pTeam = aslTeam_GetTeam(iTeamID);
	if (pTeam == NULL || !aslTeam_IsMember(pTeam, iEntID))
	{
		return;
	}
	AutoTrans_aslTeam_tr_SetXBoxOnlineSessionId(NULL, GetAppGlobalType(), GLOBALTYPE_TEAM, iTeamID, iEntID, pXSessionInfo);
}

AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".Eamembers");
enumTransactionOutcome aslTeam_tr_SetVoiceChatFlag(ATR_ARGS, NOCONST(Team) *pTeamContainer, U32 iEntID, U8 bJoined)
{
	bool bJoinedVoiceChat = bJoined == 0 ? false : true;
	int i;

	TEAM_TRANSACTION_INIT;

	// Check for the validity of the team
	if (ISNULL(pTeamContainer) || aslTeam_trh_NumPresentMembers(TEAM_TRH_PASS_ARGS, pTeamContainer) <= 0)
	{
		if (bJoined)
			aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iEntID, iEntID, 0, "TeamServer_Error_CouldNotJoinVoiceChat", true, 0, 0, 0, 0, NULL);
		else
			aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iEntID, iEntID, 0, "TeamServer_Error_CouldNotLeaveVoiceChat", true, 0, 0, 0, 0, NULL);
		TEAM_TRANSACTION_RETURN_FAILURE;
	}

	// Find the correct member and set the flag
	for (i = 0; i < eaSize(&pTeamContainer->eaMembers); i++)
	{
		if (pTeamContainer->eaMembers[i]->iEntID == iEntID)
		{
			pTeamContainer->eaMembers[i]->bJoinedVoiceChat = bJoinedVoiceChat;

			if (bJoined)
				aslTeam_AddSuccessFeedback(ATR_TEAM_RETURN, iEntID, iEntID, 0, "TeamServer_Success_JoinedVoiceChat", false, 0, 0, 0, 0, NULL);
			else
				aslTeam_AddSuccessFeedback(ATR_TEAM_RETURN, iEntID, iEntID, 0, "TeamServer_Success_Error_LeftVoiceChat", false, 0, 0, 0, 0, NULL);

			TEAM_TRANSACTION_RETURN_SUCCESS;
		}
	}

	if (bJoined)
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iEntID, iEntID, 0, "TeamServer_Error_CouldNotJoinVoiceChat", true, 0, 0, 0, 0, NULL);
	else
		aslTeam_AddFailureFeedback(ATR_TEAM_RETURN, iEntID, iEntID, 0, "TeamServer_Error_CouldNotLeaveVoiceChat", true, 0, 0, 0, 0, NULL);
	TEAM_TRANSACTION_RETURN_FAILURE;
}

AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".Pxsessioninfo");
enumTransactionOutcome aslTeam_tr_SetXBoxOnlineSessionId(ATR_ARGS, NOCONST(Team) *pTeamContainer, U32 iEntID, NON_CONTAINER CrypticXSessionInfo *pXSessionInfo)
{
	TEAM_TRANSACTION_INIT;

	// Check for the validity of the team
	if (ISNULL(pTeamContainer))
	{
		TEAM_TRANSACTION_RETURN_FAILURE;
	}
	
	if (pTeamContainer->pXSessionInfo == NULL)
	{
		pTeamContainer->pXSessionInfo = StructCloneDeConst(parse_CrypticXSessionInfo, pXSessionInfo);
	}
	else
	{
		StructCopyDeConst(parse_CrypticXSessionInfo, pXSessionInfo, pTeamContainer->pXSessionInfo, 0, 0, 0);
	}

	TEAM_TRANSACTION_RETURN_SUCCESS
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
GameSessionQueryResult * aslTeam_CmdGetGameSessions(GameSessionQuery *pQuery)
{
	GameSessionQueryResult *pResult = StructCreate(parse_GameSessionQueryResult);

	pQuery->iNumRecords = CLAMP(pQuery->iNumRecords, GAME_SESSION_MIN_COUNT_PER_GROUP, GAME_SESSION_MAX_COUNT_PER_GROUP);

	pResult->iVersion = pQuery->iVersion;

	aslTeam_GetGameSessions(pQuery, pResult);

	return pResult;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
GameContentNodePartyCountResult * aslTeam_CmdGetGameSessionCountByGroup(const char *pchGroupName)
{
	return aslTeam_GetGameSessionCountByGroup(pchGroupName);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
GameSession * aslTeam_CmdGetGameSession(U32 uiTeamID, U32 uiCurrentVersion)
{
	GameSession *pSession = aslTeam_GetGameSessionByID(uiTeamID, uiCurrentVersion);
	GameSession *pSessionCopy;

	if (pSession)
	{
		pSessionCopy = StructClone(parse_GameSession, pSession);
	}
	else
	{
		pSessionCopy = StructCreate(parse_GameSession);
	}
	
	return pSessionCopy;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
void aslTeam_CmdSetReadyFlagInGameSession(U32 uiTeamID, U32 uiEntID, bool bReady)
{
	aslTeam_SetReadyFlagInGameSession(uiTeamID, uiEntID, bReady);
}

void aslTeam_ReplaceMember_CB(TransactionReturnVal *pReturn, ASLTeamCBData *pData)
{
	SlowRemoteCommandReturn_aslTeam_ReplaceMember(pData->iCmdID, pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS);

	aslTeam_RemoteCommand_CB(pReturn, pData);
}

AUTO_COMMAND_REMOTE_SLOW(bool);
void aslTeam_ReplaceMember(U32 iTeamID, U32 iCurrentEntID, U32 iNewEntID, SlowRemoteCommandID iCmdID)
{
	static char pcActionType[] = "ReplaceMember";
	ASLTeamCBData *pData = NULL;
	TransactionReturnVal *pReturn = NULL;
	Team *pTeam = aslTeam_GetTeam(iTeamID);

	if (pTeam &&
		aslTeam_CheckIsMember(pTeam, iCurrentEntID, pcActionType) &&
		aslTeam_CheckIsNotMember(pTeam, iNewEntID, pcActionType)) 
	{
		pData = aslTeam_MakeCBData(pcActionType);
		pData->iCmdID = iCmdID;
		pData->iEntID = iCurrentEntID;
		pData->iSubjectID = iNewEntID;
		pData->iTeamID = iTeamID;
		pReturn = objCreateManagedReturnVal(aslTeam_ReplaceMember_CB, pData);
		AutoTrans_aslTeam_tr_ReplaceMember(pReturn, GetAppGlobalType(), GLOBALTYPE_TEAM, iTeamID, GLOBALTYPE_ENTITYPLAYER, iCurrentEntID, GLOBALTYPE_ENTITYPLAYER, iNewEntID);
	}
	else
	{
		// Team join failed
		SlowRemoteCommandReturn_aslTeam_ReplaceMember(iCmdID, false);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".Pleader, .Eamembers, .Eadisconnecteds, .Iversion, .Icontainerid, .Pchampion, .Emode, .Loot_Mode, .Loot_Mode_Quality, .Idifficulty, .Pxsessioninfo, .pchStatusMessage, .uBadLogoutTime, .Pcallegiance, .Pcsuballegiance, .Igameserverownerid, .Brequiresuballegiancematch")
ATR_LOCKS(pCurrentEnt, ".Pteam.Estate, .Pteam.Iteamid, .Pplayer.Accountid, .Pplayer.Pprogressioninfo.Pteamdata")
ATR_LOCKS(pNewEnt, ".Psaved.Savedname, .Pplayer.Bautojointeamvoicechat, .Pteam, .Pplayer.Accountid, .Pplayer.Pprogressioninfo.Pteamdata, .Pplayer.Publicaccountname, .Hallegiance, .Hsuballegiance");
enumTransactionOutcome aslTeam_tr_ReplaceMember(ATR_ARGS, NOCONST(Team) *pTeamContainer, NOCONST(Entity) *pCurrentEnt, NOCONST(Entity) *pNewEnt)
{
	TEAM_TRANSACTION_INIT;

	ContainerID iCurrentEntContainerID = NONNULL(pCurrentEnt) ? pCurrentEnt->myContainerID : 0;
	ContainerID iNewEntContainerID = NONNULL(pNewEnt) ? pNewEnt->myContainerID : 0;
	NOCONST(TeamProgressionData) *pTeamData = NULL;
	bool bLeader;

	// Make sure the current entity is in this team
	NOCONST(TeamMember) *pMember = aslTeam_trh_FindMember(TEAM_TRH_PASS_ARGS, iCurrentEntContainerID, pTeamContainer);
	if (ISNULL(pMember))
	{
		TEAM_TRANSACTION_RETURN_FAILURE;
	}

	// Make sure the new entity is not already on a team
	if (NONNULL(pNewEnt->pTeam) && pNewEnt->pTeam->iTeamID)
	{
		TEAM_TRANSACTION_RETURN_FAILURE;
	}

	// Get the leadership status before we remove the current entity from the team
	bLeader = aslTeam_trh_IsLeader(TEAM_TRH_PASS_ARGS, iCurrentEntContainerID, pTeamContainer);

	// Get the story tracking data override
	if (NONNULL(pCurrentEnt->pPlayer) && 
		NONNULL(pCurrentEnt->pPlayer->pProgressionInfo) && 
		NONNULL(pCurrentEnt->pPlayer->pProgressionInfo->pTeamData))
	{
		pTeamData = pCurrentEnt->pPlayer->pProgressionInfo->pTeamData;
		pCurrentEnt->pPlayer->pProgressionInfo->pTeamData = NULL;
	}

	// Remove the current member
	if (!aslTeam_trh_RemoveMember(TEAM_TRH_PASS_ARGS, iCurrentEntContainerID, pTeamContainer, pCurrentEnt))
	{
		TEAM_TRANSACTION_RETURN_FAILURE;
	}

	// Add the new member
	aslTeam_trh_AddMember(TEAM_TRH_PASS_ARGS, pTeamContainer, pNewEnt, false);

	// Pass the story tracking data override to the new entity
	if (NONNULL(pNewEnt->pPlayer) && NONNULL(pNewEnt->pPlayer->pProgressionInfo) && pTeamData)
	{
		if (pNewEnt->pPlayer->pProgressionInfo->pTeamData)
		{
			StructCopyAllNoConst(parse_TeamProgressionData, pTeamData, pNewEnt->pPlayer->pProgressionInfo->pTeamData);
			StructDestroyNoConst(parse_TeamProgressionData, pTeamData);
		}
		else
		{
			pNewEnt->pPlayer->pProgressionInfo->pTeamData = pTeamData;
		}
	}
	else if (pTeamData)
	{
		StructDestroyNoConst(parse_TeamProgressionData, pTeamData);
	}

	if (bLeader)
	{
		// Set the new leader
		if (ISNULL(pTeamContainer->pLeader)) 
		{
			pTeamContainer->pLeader = StructCreateNoConst(parse_TeamMember);
		}
		pTeamContainer->pLeader->iEntID = iNewEntContainerID;
		pTeamContainer->pLeader->iJoinTime = timeSecondsSince2000();
	}

	TEAM_TRANSACTION_RETURN_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pTeamContainer, ".Icontainerid, .Pleader.Ientid,");
enumTransactionOutcome aslTeam_tr_SetTeamReadyForTransfer(ATR_ARGS, NOCONST(Team) *pTeamContainer, U32 iEntID)
{
	TEAM_TRANSACTION_INIT;

	GameSession *pGameSession = aslTeam_GetGameSessionByID(pTeamContainer->iContainerID, 0);

	// Make sure the entity is the actual leader of the team, the team is still in the lobby and everyone in the team is ready
	if (pGameSession)
	{
		ANALYSIS_ASSUME(pGameSession != NULL);
		if (aslTeam_IsEveryoneInSessionReady(pGameSession) &&
			!aslTeam_trh_IsLeader(TEAM_TRH_PASS_ARGS, iEntID, pTeamContainer))
		{
			TEAM_TRANSACTION_RETURN_FAILURE;
		}
	}

	aslTeam_AddReturnAction(ATR_TEAM_RETURN, TEAM_TRANSACTION_RETURN_ACTION_TYPE_UPDATE_GAME_SESSION, 
		pTeamContainer->iContainerID, 0);

	TEAM_TRANSACTION_RETURN_SUCCESS;
}

#include "AutoGen/TeamTransactions_c_ast.c"

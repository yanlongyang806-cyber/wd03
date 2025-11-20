/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/


#include "StringCache.h"
#include "logging.h"
#include "AutoTransDefs.h"

#include "GameAccountDataCommon.h"
#include "Guild.h"
#include "Team.h"
#include "Entity.h"
#include "EntityLib.h"
#include "EntitySavedData.h"
#include "entCritter.h"
#include "aslGuildServer.h"
#include "inventoryCommon.h"
#include "Player.h"
#include "TextFilter.h"
#include "SocialCommon.h"
#include "ResourceInfo.h"
#include "PowerTree.h"
#include "ActivityLogCommon.h"
#include "chatCommonStructs.h"
#include "guildCommonStructs.h"
#include "ticketnet.h"
#include "StringFormat.h"
#include "file.h"
#include "ShardCommon.h"

#include "Character.h"

#include "AutoGen/Character_h_ast.h"
#include "AutoGen/itemEnums_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/EntitySavedData_h_ast.h"
#include "AutoGen/Guild_h_ast.h"
#include "AutoGen/GuildTransactions_c_ast.h"
#include "AutoGen/aslGuildServer_h_ast.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/AppServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/ObjectDB_autogen_remotefuncs.h"
#include "AutoGen/ChatServer_autogen_RemoteFuncs.h"
#include "AutoGen/serverlib_autogen_RemoteFuncs.h"
#include "Player_h_ast.h"

extern ParseTable parse_TicketData[];
#define TYPE_parse_TicketData TicketData

extern DictionaryHandle g_hDefaultInventoryDict;

extern TimingHistory *gCreationHistory;
extern TimingHistory *gActionHistory;

// This macro should be placed at the start of every guild transaction. It declares the
// structure used to store feedback and logging.
#define GUILD_TRANSACTION_INIT ASLGuildTransactionReturn *ATR_GUILD_RETURN = StructCreate(parse_ASLGuildTransactionReturn)

// These macros should be called to return from every guild transaction. It compiles the
// return structure into a string and stores it in the transaction output.
#define GUILD_TRANSACTION_RETURN_SUCCESS { \
	ParserWriteText(ATR_RESULT_SUCCESS, parse_ASLGuildTransactionReturn, ATR_GUILD_RETURN, 0, 0, 0); \
	StructDestroy(parse_ASLGuildTransactionReturn, ATR_GUILD_RETURN); \
	return TRANSACTION_OUTCOME_SUCCESS; \
}
#define GUILD_TRANSACTION_RETURN_FAILURE { \
	ParserWriteText(ATR_RESULT_FAIL, parse_ASLGuildTransactionReturn, ATR_GUILD_RETURN, 0, 0, 0); \
	StructDestroy(parse_ASLGuildTransactionReturn, ATR_GUILD_RETURN); \
	return TRANSACTION_OUTCOME_FAILURE; \
}

// Add this as the first argument to all guild transaction helper function definitions
#define GUILD_TRH_ARGS ATR_ARGS, ASLGuildTransactionReturn *ATR_GUILD_RETURN

// Add this as the first argument to all guild transaction helper function calls
#define GUILD_TRH_PASS_ARGS ATR_PASS_ARGS, ATR_GUILD_RETURN

///////////////////////////////////////////////////////////////////////////////////////////
// User Feedback and Logging
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_STRUCT;
typedef struct ASLGuildFeedbackMessage
{
	ContainerID iDestPlayerID;
	ContainerID iObjectID;
	ContainerID iSubjectID;
	const char *pcGuildName;
	char pcMessageKey[64];
	bool bError;
} ASLGuildFeedbackMessage;

AUTO_STRUCT;
typedef struct ASLGuildLogMessage
{
	char *pcEntName;
	ContainerID iGuildID;
	char *pcGuildName;
	
	char *pcLogMessage; AST(ESTRING)
} ASLGuildLogMessage;

AUTO_STRUCT;
typedef struct ASLGuildActivity
{
	ContainerID iEntID;
	ContainerID iGuildID;
	U32 kType; // Don't store this as an ActivityType because the trick with the duplicate names will break stuff
	char *pcGuildName;
	char *pcPlayerName; // Only set when leaving the guild
	char *pcRankName; // Only set when changing rank
} ASLGuildActivity;

AUTO_STRUCT;
typedef struct ASLGuildTransactionReturn
{
	EARRAY_OF(ASLGuildFeedbackMessage) eaSuccessFeedbackMessages;
	EARRAY_OF(ASLGuildFeedbackMessage) eaFailureFeedbackMessages;
	EARRAY_OF(ASLGuildLogMessage) eaSuccessLogMessages;
	EARRAY_OF(ASLGuildLogMessage) eaFailureLogMessages;
	EARRAY_OF(ASLGuildActivity) eaActivity;
} ASLGuildTransactionReturn;

#define aslGuild_GetName(pGuild) ((pGuild) && (pGuild)->pcName ? (pGuild)->pcName : "")

static ASLGuildFeedbackMessage *aslGuild_CreateFeedback(U32 iDestPlayerID, U32 iObjectID, U32 iSubjectID, const char *pcGuildName, const char *pcMessageKey, bool bError)
{
	ASLGuildFeedbackMessage *pMessage = StructCreate(parse_ASLGuildFeedbackMessage);
	
	pMessage->iDestPlayerID = iDestPlayerID;
	pMessage->iObjectID = iObjectID;
	pMessage->iSubjectID = iSubjectID;
	pMessage->pcGuildName = StructAllocString(pcGuildName);
	strcpy_s(pMessage->pcMessageKey, 64, pcMessageKey);
	pMessage->bError = bError;
	
	return pMessage;
}

AUTO_TRANS_HELPER_SIMPLE;
static void aslGuild_AddSuccessFeedback(ASLGuildTransactionReturn *pReturn, U32 iDestPlayerID, U32 iObjectID, U32 iSubjectID, const char *pcGuildName, const char *pcMessageKey, bool bError)
{
	ASLGuildFeedbackMessage *pMessage = aslGuild_CreateFeedback(iDestPlayerID, iObjectID, iSubjectID, pcGuildName, pcMessageKey, bError);
	eaPush(&pReturn->eaSuccessFeedbackMessages, pMessage);
}

AUTO_TRANS_HELPER;
void aslGuild_trh_AddSuccessFeedback(ASLGuildTransactionReturn *pReturn, ATH_ARG NOCONST(Guild) *pGuild, U32 iDestPlayerID, U32 iObjectID, U32 iSubjectID, const char *pcMessageKey, bool bError)
{
	aslGuild_AddSuccessFeedback(pReturn, iDestPlayerID, iObjectID, iSubjectID, NONNULL(pGuild) && NONNULL(pGuild->pcName) ? pGuild->pcName : "", pcMessageKey, bError);
}

AUTO_TRANS_HELPER;
void aslGuild_trh_AddFeedbackAll(ASLGuildTransactionReturn *pReturn, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iSpecialID, U32 iObjectID, U32 iSubjectID, const char *pcMessageKey, const char *pcSpecialMessageKey)
{
	S32 i;
	for (i = eaSize(&pGuildContainer->eaMembers)-1; i >= 0; i--) {
		if (pGuildContainer->eaMembers[i]->iEntID != iSpecialID) {
			aslGuild_AddSuccessFeedback(pReturn, pGuildContainer->eaMembers[i]->iEntID, iObjectID, iSubjectID, NONNULL(pGuildContainer) && NONNULL(pGuildContainer->pcName) ? pGuildContainer->pcName : "", pcMessageKey, false);
		}
	}
	if (iSpecialID) {
		aslGuild_trh_AddSuccessFeedback(pReturn, pGuildContainer, iSpecialID, iObjectID, iSubjectID, pcSpecialMessageKey, false);
	}
}

AUTO_TRANS_HELPER_SIMPLE;
static void aslGuild_AddFailureFeedback(ASLGuildTransactionReturn *pReturn, U32 iDestPlayerID, U32 iObjectID, U32 iSubjectID, const char *pcGuildName, const char *pcMessageKey, bool bError)
{
	ASLGuildFeedbackMessage *pMessage = aslGuild_CreateFeedback(iDestPlayerID, iObjectID, iSubjectID, pcGuildName, pcMessageKey, bError);
	eaPush(&pReturn->eaFailureFeedbackMessages, pMessage);
}

AUTO_TRANS_HELPER;
void aslGuild_trh_AddFailureFeedback(ASLGuildTransactionReturn *pReturn, ATH_ARG NOCONST(Guild) *pGuild, U32 iDestPlayerID, U32 iObjectID, U32 iSubjectID, const char *pcMessageKey, bool bError)
{
	aslGuild_AddFailureFeedback(pReturn, iDestPlayerID, iObjectID, iSubjectID, NONNULL(pGuild) && NONNULL(pGuild->pcName) ? pGuild->pcName : "", pcMessageKey, bError);
}

static void aslGuild_AddActivity(ASLGuildTransactionReturn *pReturn, ContainerID iEntID, ContainerID iGuildID, ActivityType kType, const char *pcGuildName, const char *pcPlayerName, const char *pcRankName)
{
	ASLGuildActivity *pActivity = StructCreate(parse_ASLGuildActivity);
	pActivity->iEntID = iEntID;
	pActivity->iGuildID = iGuildID;
	pActivity->kType = kType;
	pActivity->pcGuildName = StructAllocString(pcGuildName);
	pActivity->pcPlayerName = StructAllocString(pcPlayerName);
	pActivity->pcRankName = StructAllocString(pcRankName);
	eaPush(&pReturn->eaActivity, pActivity);
}

AUTO_TRANS_HELPER;
void aslGuild_trh_AddActivity(ASLGuildTransactionReturn *pReturn, ATH_ARG NOCONST(Guild) *pGuild, ContainerID iEntID, ActivityType kType, const char *pcPlayerName, S32 iRank)
{
	if ( ISNULL(pGuild) )
	{
		aslGuild_AddActivity(pReturn, iEntID,  0, kType,  "", NONNULL(pcPlayerName) ? pcPlayerName : "", "");
	}
	else
	{
		char achRankNum[64];
		// HACK(jm): Currently, the GuildServer does not have access to the default rank
		// messages, so instead using an empty string for unnamed guild ranks, track the
		// rank number in the rank string instead. The ESC character is the marker used
		// to indicate that the rank name is actually the rank number.
		//    When displayed for the guild activity log, the rank number is resolved on 
		// the client (using the client's current locale instead of the GuildServer's).
		// When the rank name is used for the player's activity log, the GameServer
		// resolves the rank name before adding to the player's log using the player's
		// last known locale.
		//    See STO-28060
		achRankNum[0] = '\33';
		_itoa_s(iRank, achRankNum+1, ARRAY_SIZE(achRankNum)-1, 10);
		aslGuild_AddActivity(pReturn, iEntID, pGuild->iContainerID, kType, NONNULL(pGuild->pcName) ? pGuild->pcName : "", NONNULL(pcPlayerName) ? pcPlayerName : "", achRankNum);
	}
}


static void aslGuild_SendFeedbackStruct(const char *pcActionType, ASLGuildFeedbackMessage *pMessage)
{
	char *estrBuffer = NULL;
	estrStackCreate(&estrBuffer);
	estrConcatf(&estrBuffer, "GuildServer_ErrorType_%s", pcActionType);
	RemoteCommand_gslGuild_ResultMessage(GLOBALTYPE_ENTITYPLAYER, pMessage->iDestPlayerID, pMessage->iDestPlayerID, pMessage->iObjectID, pMessage->iSubjectID, pMessage->pcGuildName, estrBuffer, pMessage->pcMessageKey, pMessage->bError);
	estrDestroy(&estrBuffer);
}

static void aslGuild_SendFeedback(U32 iDestPlayerID, U32 iObjectID, U32 iSubjectID, const char *pcGuildName, SA_PARAM_NN_VALID const char *pcActionType, SA_PARAM_NN_VALID const char *pcMessageKey, bool bDialog)
{
	if (iDestPlayerID) {
		char *estrBuffer = NULL;
		estrStackCreate(&estrBuffer);
		estrConcatf(&estrBuffer, "GuildServer_ErrorType_%s", pcActionType);
		RemoteCommand_gslGuild_ResultMessage(GLOBALTYPE_ENTITYPLAYER, iDestPlayerID, iDestPlayerID, iObjectID, iSubjectID, pcGuildName, estrBuffer, pcMessageKey, bDialog);
		estrDestroy(&estrBuffer);
	}
}

static ASLGuildLogMessage *aslGuild_CreateLogMessage(const char *pcEntName, U32 iGuildID, const char *pcGuildName, const char *pcFormat, va_list vaArgs)
{
	ASLGuildLogMessage *pMessage = StructCreate(parse_ASLGuildLogMessage);
	
	pMessage->iGuildID = iGuildID;
	if (pcGuildName && pcGuildName[0]) {
		pMessage->pcGuildName = StructAllocString(pcGuildName);
	}
	if (pcEntName && pcEntName[0]) {
		pMessage->pcEntName = StructAllocString(pcEntName);
	}
	estrConcatfv(&pMessage->pcLogMessage, pcFormat, vaArgs);
	
	return pMessage;
}

AUTO_TRANS_HELPER_SIMPLE;
static void aslGuild_AddSuccessLogMessage(ASLGuildTransactionReturn *pReturn, const char *pcEntName, U32 iGuildID, const char *pcGuildName, const char *pcFormat, ...)
{
	ASLGuildLogMessage *pcMessage;
	
	VA_START(vaArgs, pcFormat);
	pcMessage = aslGuild_CreateLogMessage(pcEntName, iGuildID, pcGuildName, pcFormat, vaArgs);
	VA_END();
	
	eaPush(&pReturn->eaSuccessLogMessages, pcMessage);
}

static void aslGuild_AddFailureLogMessage(ASLGuildTransactionReturn *pReturn, const char *pcEntName, U32 iGuildID, const char *pcGuildName, const char *pcFormat, ...)
{
	ASLGuildLogMessage *pcMessage;
	
	VA_START(vaArgs, pcFormat);
	pcMessage = aslGuild_CreateLogMessage(pcEntName, iGuildID, pcGuildName, pcFormat, vaArgs);
	VA_END();
	
	eaPush(&pReturn->eaFailureLogMessages, pcMessage);
}

static void aslGuild_SaveLogMessageStruct(const char *pcActionType, ASLGuildLogMessage *pMessage)
{
	objLog(LOG_GUILD, GLOBALTYPE_GUILD, pMessage->iGuildID, 0, pMessage->pcGuildName, NULL, NULL, pcActionType, NULL, " %s: %s ", pMessage->pcEntName, pMessage->pcLogMessage);
}

static void aslGuild_SaveLogMessage(const char *pcActionType, const char *pcEntName, U32 iGuildID, const char *pcGuildName, const char *pcFormat, ...)
{
	char *estrBuffer = NULL;
	estrStackCreate(&estrBuffer);
	
	VA_START(vaArgs, pcFormat);
	estrConcatfv(&estrBuffer, pcFormat, vaArgs);
	VA_END();
	objLog(LOG_GUILD, GLOBALTYPE_GUILD, iGuildID, 0, pcGuildName, NULL, NULL, pcActionType, NULL, " %s: %s ", pcEntName, estrBuffer);
	
	estrDestroy(&estrBuffer);
}

static ASLGuildCBData *aslGuild_MakeCBData(const char *pcActionType)
{
	ASLGuildCBData *pData = StructCreate(parse_ASLGuildCBData);
	if (pcActionType && pcActionType[0]) {
		pData->pcActionType = StructAllocString(pcActionType);
	}
	return pData;
}

static void LogGuildActivity_CB(TransactionReturnVal *pReturn, void *cbData)
{

}

static void aslGuild_LogGuildActivity(ASLGuildActivity *guildActivity)
{
	ActivityLogEntryType entryType;
	ActivityLogEntryTypeConfig *entryTypeConfig;
	U32 time;
	const char *argString;
	ContainerID subjectID;

	if ( guildActivity->iGuildID == 0 )
	{
		return;
	}

	switch (guildActivity->kType)
	{
	case kActivityType_GuildJoin:
		entryType = ActivityLogEntryType_GuildJoin;
		break;
	case kActivityType_GuildLeave:
		entryType = ActivityLogEntryType_GuildLeave;
		break;
	case kActivityType_GuildCreate:
		entryType = ActivityLogEntryType_GuildCreate;
		break;
	case kActivityType_GuildRankChange:
		entryType = ActivityLogEntryType_GuildRankChange;
		break;
	default:
		return;
	}

	time = timeSecondsSince2000();

	entryTypeConfig = ActivityLog_GetTypeConfig(entryType);

	if ( entryTypeConfig != NULL )
	{
		if ( entryTypeConfig->addToPersonalLog )
		{
			TransactionReturnVal *pReturnEntity;

			pReturnEntity = objCreateManagedReturnVal(LogGuildActivity_CB, NULL);

			// Rank change gets the rank number as its argString.  Other types get guild name
			if ( entryType == ActivityLogEntryType_GuildRankChange )
			{
				argString = guildActivity->pcRankName;
				AutoTrans_ActivityLog_tr_AddEntityGuildLogEntry(pReturnEntity, GLOBALTYPE_GAMESERVER, GLOBALTYPE_ENTITYPLAYER, guildActivity->iEntID, GLOBALTYPE_GUILD, guildActivity->iGuildID, entryType, argString, time, 0.0f);
			}
			else
			{
				argString = guildActivity->pcGuildName;
				AutoTrans_ActivityLog_tr_AddEntityLogEntry(pReturnEntity, GLOBALTYPE_GAMESERVER, GLOBALTYPE_ENTITYPLAYER, guildActivity->iEntID, entryType, argString, time, 0.0f);
			}
		}
		if ( entryTypeConfig->addToGuildLog )
		{
			TransactionReturnVal *pReturnGuild;

			if ( entryType == ActivityLogEntryType_GuildLeave )
			{
				argString = guildActivity->pcPlayerName;
				subjectID = 0;
			}
			else if ( entryType == ActivityLogEntryType_GuildRankChange )
			{
				argString = guildActivity->pcRankName;
				subjectID = guildActivity->iEntID;
			}
			else
			{
				argString = guildActivity->pcGuildName;
				subjectID = guildActivity->iEntID;
			}
			pReturnGuild = objCreateManagedReturnVal(LogGuildActivity_CB, NULL);

			AutoTrans_ActivityLog_tr_AddGuildLogEntry(pReturnGuild, GLOBALTYPE_GUILDSERVER, GLOBALTYPE_GUILD, guildActivity->iGuildID, entryType, argString, time, subjectID);
		}
	}
}

static void aslGuild_RemoteCommand_CB(TransactionReturnVal *pReturn, ASLGuildCBData *pData)
{
	S32 i;
	ASLGuildTransactionReturn *pGuildReturn = StructCreate(parse_ASLGuildTransactionReturn);
	char *pcGuildReturnParsed = objAutoTransactionGetResult(pReturn);
	ASLGuildFeedbackMessage **eaFeedbackMessages;
	ASLGuildLogMessage **eaLogMessages;
	
	ParserReadText(pcGuildReturnParsed, parse_ASLGuildTransactionReturn, pGuildReturn, 0);
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS) {
		eaFeedbackMessages = pGuildReturn->eaSuccessFeedbackMessages;
		eaLogMessages = pGuildReturn->eaSuccessLogMessages;
		for (i = 0; i < eaSize(&pGuildReturn->eaActivity); i++) {
			RemoteCommand_gslSocialCmdActivity(GLOBALTYPE_ENTITYPLAYER, pGuildReturn->eaActivity[i]->iEntID, pGuildReturn->eaActivity[i]->iEntID, pGuildReturn->eaActivity[i]->kType, pGuildReturn->eaActivity[i]->pcGuildName);
			aslGuild_LogGuildActivity(pGuildReturn->eaActivity[i]);
		}
	} else {
		eaFeedbackMessages = pGuildReturn->eaFailureFeedbackMessages;
		eaLogMessages = pGuildReturn->eaFailureLogMessages;
	}
	
	for (i = 0; i < eaSize(&eaFeedbackMessages); i++) {
		aslGuild_SendFeedbackStruct(pData->pcActionType, eaFeedbackMessages[i]);
	}
	for (i = 0; i < eaSize(&eaLogMessages); i++) {
		aslGuild_SaveLogMessageStruct(pData->pcActionType, eaLogMessages[i]);
	}
	
	StructDestroy(parse_ASLGuildCBData, pData);
	StructDestroy(parse_ASLGuildTransactionReturn, pGuildReturn);
}

static void aslGuild_RecurGuildEvent_CB(TransactionReturnVal *pReturn, ASLGuildCBData *pData)
{
	Guild *pGuild = aslGuild_GetGuild(pData->iGuildID);
	GuildEvent *pGuildEvent = pGuild ? eaIndexedGetUsingInt(&pGuild->eaEvents, pData->iGuildEventID) : NULL;

	if (pGuildEvent)
	{
		pGuildEvent->bRecurInTransaction = false;
	}

	aslGuild_RemoteCommand_CB(pReturn, pData);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Transaction Helpers
///////////////////////////////////////////////////////////////////////////////////////////
// All commonly repeated checks, and all changes, are stuck in these helper functions.
// The ones that make changes contain no validation. It is the job of the main transaction
// functions to do the appropriate validation (usually by calling the right check
// functions) before calling the helpers that make the changes.
///////////////////////////////////////////////////////////////////////////////////////////

// Get the player's display name
AUTO_TRANS_HELPER;
const char *aslGuild_trh_GetDisplayName(GUILD_TRH_ARGS, ATH_ARG NOCONST(Entity) *pEnt)
{
	if (pEnt->pSaved->savedName && pEnt->pSaved->savedName[0]) {
		return pEnt->pSaved->savedName;
	}
	return pEnt->debugName;
}

// Find the guild member that corresponds with an entity, if it exists
AUTO_TRANS_HELPER;
NOCONST(GuildMember) *aslGuild_trh_FindMember(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID)
{
	return eaIndexedGetUsingInt(&pGuildContainer->eaMembers, iEntID);
}

// Find the guild member that corresponds with an entity, if it exists
AUTO_TRANS_HELPER;
S32 aslGuild_trh_FindMemberIdx(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID)
{
	return eaIndexedFindUsingInt(&pGuildContainer->eaMembers, iEntID);
}

// Find the guild invite that corresponds with an entity, if it exists
AUTO_TRANS_HELPER;
NOCONST(GuildMember) *aslGuild_trh_FindInvite(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID)
{
	return eaIndexedGetUsingInt(&pGuildContainer->eaInvites, iEntID);
}

// Find the guild invite that corresponds with an entity, if it exists
AUTO_TRANS_HELPER;
S32 aslGuild_trh_FindInviteIdx(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID)
{
	return eaIndexedFindUsingInt(&pGuildContainer->eaInvites, iEntID);
}

// Check if a given rank in this guild has a particular permission
AUTO_TRANS_HELPER;
bool aslGuild_trh_HasPermission(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, S32 iRank, GuildRankPermissions ePerms)
{
	if (iRank >= 0 && iRank < eaSize(&pGuildContainer->eaRanks)) {
		return (pGuildContainer->eaRanks[iRank]->ePerms & ePerms) != 0;
	}
	return false;
}

// Make a GuildMember struct from an entity
AUTO_TRANS_HELPER;
NOCONST(GuildMember) *aslGuild_trh_MakeGuildMember(GUILD_TRH_ARGS, ATH_ARG NOCONST(Entity) *pEnt, S32 iRank, const char *pchClassName)
{
	static char pcAccountName[MAX_NAME_LEN+1];
	NOCONST(GuildMember) *pMember = StructCreateNoConst(parse_GuildMember);
	pMember->iEntID = pEnt->myContainerID;
	pMember->iJoinTime = timeSecondsSince2000();
	pMember->pcName = StructAllocString(aslGuild_trh_GetDisplayName(GUILD_TRH_PASS_ARGS, pEnt));
	sprintf(pcAccountName, "@%s", pEnt->pPlayer->publicAccountName);
	pMember->pcAccount = StructAllocString(pcAccountName);
	pMember->pcLogName = StructAllocString(pEnt->debugName);
	pMember->iAccountID = pEnt->pPlayer->accountID;
	if (pchClassName && *pchClassName) pMember->pchClassName = allocAddString(pchClassName);
	pMember->iRank = iRank;
	return pMember;
}

// Add a player to the invite list
AUTO_TRANS_HELPER;
void aslGuild_trh_AddInvite(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(Entity) *pSubject)
{
	eaPush(&pGuildContainer->eaInvites, aslGuild_trh_MakeGuildMember(GUILD_TRH_PASS_ARGS, pSubject, 0, NULL));
	pGuildContainer->iVersion++;
	
	if (!pSubject->pPlayer->pGuild) {
		pSubject->pPlayer->pGuild = StructCreateNoConst(parse_PlayerGuild);
	}
	pSubject->pPlayer->pGuild->iGuildID = pGuildContainer->iContainerID;
	pSubject->pPlayer->pGuild->eState = GuildState_Invitee;
	pSubject->pPlayer->pGuild->pcInviterName = StructAllocString(aslGuild_trh_GetDisplayName(GUILD_TRH_PASS_ARGS, pEnt));
	pSubject->pPlayer->pGuild->pcInviterHandle = StructAllocString(pEnt->pPlayer->publicAccountName);

	aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, pEnt->myContainerID, pEnt->myContainerID, pSubject->myContainerID, "GuildServer_Invited", false);
	aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, pSubject->myContainerID, pEnt->myContainerID, pSubject->myContainerID, "GuildServer_Subject_Invited", false);
	aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pEnt->debugName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Invited %s to guild.", pSubject->debugName);
}

// Remove a player from the invite list
// Return a boolean representing success
AUTO_TRANS_HELPER;
bool aslGuild_trh_RemoveInvite(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, ATH_ARG NOCONST(Entity) *pEnt)
{
	int iMemberIdx = aslGuild_trh_FindInviteIdx(GUILD_TRH_PASS_ARGS, pGuildContainer, pEnt->myContainerID);
	if (iMemberIdx >= 0) {
		StructDestroyNoConst(parse_GuildMember, pGuildContainer->eaInvites[iMemberIdx]);
		eaRemove(&pGuildContainer->eaInvites, iMemberIdx);
		if (NONNULL(pEnt->pPlayer->pGuild)) {
			pEnt->pPlayer->pGuild->iGuildID = 0;
			if (pEnt->pPlayer->pGuild->pcInviterName) {
				StructFreeString(pEnt->pPlayer->pGuild->pcInviterName);
				pEnt->pPlayer->pGuild->pcInviterName = NULL;
			}
		}
		return true;
	}
	return false;
}

// Add a player to the member list
AUTO_TRANS_HELPER;
void aslGuild_trh_AddMember(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, ATH_ARG NOCONST(Entity) *pEnt, S32 iRank, const char *pchClassName)
{
	static char pcAccountName[MAX_NAME_LEN+1];
	
	eaPush(&pGuildContainer->eaMembers, aslGuild_trh_MakeGuildMember(GUILD_TRH_PASS_ARGS, pEnt, iRank, pchClassName));
	pGuildContainer->iVersion++;

	if (pGuildContainer->pGuildStatsInfo == NULL)
	{
		pGuildContainer->pGuildStatsInfo = StructCreateNoConst(parse_GuildStatsInfo);
	}
	// Increment the version of the guild stats so the game server reevaluates the temporary powers for the entity.
	pGuildContainer->pGuildStatsInfo->uiVersion++;
	
	if (!pEnt->pPlayer->pGuild) {
		pEnt->pPlayer->pGuild = StructCreateNoConst(parse_PlayerGuild);
	}
	pEnt->pPlayer->pGuild->iGuildID = pGuildContainer->iContainerID;
	pEnt->pPlayer->pGuild->eState = GuildState_Member;
	
	aslGuild_trh_AddFeedbackAll(ATR_GUILD_RETURN, pGuildContainer, pEnt->myContainerID, pEnt->myContainerID, 0, "GuildServer_Guild_Joined", "GuildServer_Joined");
	aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pEnt->debugName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Joined guild. Guild size is now %d.", eaSize(&pGuildContainer->eaMembers));
	aslGuild_PushMemberAddToChatServer(pGuildContainer->iContainerID, pGuildContainer->pcName, pEnt->pPlayer->accountID, iRank,
		(const GuildCustomRank *const *)pGuildContainer->eaRanks);
}

// Remove a player from the member list
// Return a boolean representing success
AUTO_TRANS_HELPER;
bool aslGuild_trh_RemoveMember(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, ATH_ARG NOCONST(Entity) *pSubject, U32 iSubjectID)
{
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	NOCONST(GuildMember) *pSubjectMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iSubjectID);
	int iSubjectMemberIdx = aslGuild_trh_FindMemberIdx(GUILD_TRH_PASS_ARGS, pGuildContainer, iSubjectID);
	if (NONNULL(pMember) && iSubjectMemberIdx >= 0) {
		char *pcLogName = StructAllocString(pMember->pcLogName);
		char *pcSubjectLogName = StructAllocString(pSubjectMember->pcLogName);
		char *pcSubjectName = StructAllocString(pSubjectMember->pcName);
		U32 iSubjectAccountID = pSubjectMember->iAccountID;
		StructDestroyNoConst(parse_GuildMember, pGuildContainer->eaMembers[iSubjectMemberIdx]);
		eaRemove(&pGuildContainer->eaMembers, iSubjectMemberIdx);

		if (pGuildContainer->pGuildStatsInfo == NULL)
		{
			pGuildContainer->pGuildStatsInfo = StructCreateNoConst(parse_GuildStatsInfo);
		}
		// Increment the version of the guild stats so the game server reevaluates the temporary powers for the entity.
		pGuildContainer->pGuildStatsInfo->uiVersion++;

		if (NONNULL(pSubject) && NONNULL(pSubject->pPlayer) && NONNULL(pSubject->pPlayer->pGuild)) {
			pSubject->pPlayer->pGuild->iGuildID = 0;
		}
		
		aslGuild_trh_AddActivity(ATR_GUILD_RETURN, pGuildContainer, iSubjectID, kActivityType_GuildLeave, pcSubjectName, 0);

		if (iEntID != iSubjectID) {
			aslGuild_trh_AddFeedbackAll(ATR_GUILD_RETURN, pGuildContainer, iSubjectID, iEntID, iSubjectID, "GuildServer_Guild_Kicked", "GuildServer_Subject_Kicked");
			aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Kicked %s from guild. Guild size is now %d.", pcSubjectLogName, eaSize(&pGuildContainer->eaMembers));
		} else {
			if (eaSize(&pGuildContainer->eaMembers) == 0) {
				aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iSubjectID, iEntID, 0, "GuildServer_Disbanded", false);
			} else {
				aslGuild_trh_AddFeedbackAll(ATR_GUILD_RETURN, pGuildContainer, iSubjectID, iEntID, 0, "GuildServer_Guild_Left", "GuildServer_Left");
			}
			aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Left guild. Guild size is now %d.", eaSize(&pGuildContainer->eaMembers));
		}
		aslGuild_PushMemberRemoveToChatServer(pGuildContainer->iContainerID, pGuildContainer->pcName, iSubjectAccountID);
		StructFreeString(pcSubjectLogName);
		StructFreeString(pcSubjectName);
		StructFreeString(pcLogName);
		return true;
	}
	return false;
}

// Promote a member
AUTO_TRANS_HELPER;
void aslGuild_trh_Promote(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, U32 iSubjectID)
{
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	NOCONST(GuildMember) *pSubjectMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iSubjectID);
	if (NONNULL(pMember) && NONNULL(pSubjectMember)) {
		pSubjectMember->iRank++;

		aslGuild_trh_AddActivity(ATR_GUILD_RETURN, pGuildContainer, iSubjectID, kActivityType_GuildRankChange, NULL, pSubjectMember->iRank);

		aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iSubjectID, "GuildServer_Promoted", false);
		aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iSubjectID, iEntID, iSubjectID, "GuildServer_Subject_Promoted", false);
		aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Promoted %s to rank %d.", pSubjectMember->pcLogName, pSubjectMember->iRank+1);
		aslGuild_PushMemberAddToChatServer(pGuildContainer->iContainerID, pGuildContainer->pcName, pSubjectMember->iAccountID,
			pSubjectMember->iRank, (const GuildCustomRank *const *)pGuildContainer->eaRanks);
		return;
	}

}

// Demote a member
AUTO_TRANS_HELPER;
void aslGuild_trh_Demote(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, U32 iSubjectID)
{
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	NOCONST(GuildMember) *pSubjectMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iSubjectID);
	if (NONNULL(pMember) && NONNULL(pSubjectMember)) {
		pSubjectMember->iRank--;

		aslGuild_trh_AddActivity(ATR_GUILD_RETURN, pGuildContainer, iSubjectID, kActivityType_GuildRankChange, NULL, pSubjectMember->iRank);

		aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iSubjectID, "GuildServer_Demoted", false);
		aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iSubjectID, iEntID, iSubjectID, "GuildServer_Subject_Demoted", false);
		aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Demoted %s to rank %d.", pSubjectMember->pcLogName, pSubjectMember->iRank+1);
		aslGuild_PushMemberAddToChatServer(pGuildContainer->iContainerID, pGuildContainer->pcName, pMember->iAccountID,
			pSubjectMember->iRank, (const GuildCustomRank *const *)pGuildContainer->eaRanks);
		return;
	}
}

// Rename the guild
AUTO_TRANS_HELPER;
void aslGuild_trh_SetName(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, const char *pcNewName)
{
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	if (ISNULL(pMember)) {
		return;
	}
	if (pGuildContainer->pcName) {
		StructFreeString(pGuildContainer->pcName);
	}
	pGuildContainer->pcName = StructAllocString(pcNewName);
	aslGuild_trh_AddFeedbackAll(ATR_GUILD_RETURN, pGuildContainer, 0, iEntID, 0, "GuildServer_Guild_Renamed", NULL);
	aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Set guild name to %s.", pcNewName);
	pGuildContainer->iVersion++;
}

// Rename a guild rank
AUTO_TRANS_HELPER;
void aslGuild_trh_SetRankName(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, S32 iRank, char *pcNewName)
{
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	if (NONNULL(pMember) && iRank >= 0 && iRank < eaSize(&pGuildContainer->eaRanks)) {
		if (pGuildContainer->eaRanks[iRank]->pcDisplayName) {
			StructFreeString(pGuildContainer->eaRanks[iRank]->pcDisplayName);
		}
		pGuildContainer->eaRanks[iRank]->pcDisplayName = StructAllocString(pcNewName);
		aslGuild_trh_AddFeedbackAll(ATR_GUILD_RETURN, pGuildContainer, 0, iEntID, 0, "GuildServer_Guild_RankRenamed", NULL);
		aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Set guild rank %d name to %s.", iRank+1, pcNewName);
	}
}

// Rename a guild bank tab
AUTO_TRANS_HELPER;
void aslGuild_trh_SetBankTabName(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, ATH_ARG NOCONST(InventoryBag) *pBag, char *pcNewName)
{
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	if (NONNULL(pMember) && NONNULL(pBag->pGuildBankInfo)) {
		pBag->pGuildBankInfo->pcName = StructAllocString(pcNewName);
		aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_RenamedBankTab", false);
		aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Guild bank tab %d renamed to %s.", pBag->BagID-InvBagIDs_Bank1+1, pcNewName);
	}
}

// Rename a guild bank tab
AUTO_TRANS_HELPER;
void aslGuild_trh_SetBankTabNameLite(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, ATH_ARG NOCONST(InventoryBagLite) *pBag, char *pcNewName)
{
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	if (NONNULL(pMember) && NONNULL(pBag->pGuildBankInfo)) {
		pBag->pGuildBankInfo->pcName = StructAllocString(pcNewName);
		aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_RenamedBankTab", false);
		aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Guild bank tab %d renamed to %s.", pBag->BagID-InvBagIDs_Bank1+1, pcNewName);
	}
}

// Enable guild rank permissions
AUTO_TRANS_HELPER;
void aslGuild_trh_EnablePermission(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, S32 iRank, GuildRankPermissions ePerms)
{
	if (iRank >= 0 && iRank < eaSize(&pGuildContainer->eaRanks)) {
		NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
		pGuildContainer->eaRanks[iRank]->ePerms |= ePerms;
		if (pMember) {
			aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_PermissionSet", false);
			aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Permission %s enabled on rank %d.", StaticDefineIntRevLookup(GuildRankPermissionsEnum, ePerms), iRank+1);
		}
	}
}

// Disable guild rank permissions
AUTO_TRANS_HELPER;
void aslGuild_trh_DisablePermission(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, S32 iRank, GuildRankPermissions ePerms)
{
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	if (NONNULL(pMember) && iRank >= 0 && iRank < eaSize(&pGuildContainer->eaRanks)) {
		pGuildContainer->eaRanks[iRank]->ePerms &= ~ePerms;
		aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_PermissionRevoked", false);
		aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Permission %s disabled on rank %d.", StaticDefineIntRevLookup(GuildRankPermissionsEnum, ePerms), iRank+1);
	}
}

// Enable guild bank permissions
AUTO_TRANS_HELPER;
void aslGuild_trh_EnableBankPermission(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, ATH_ARG NOCONST(InventoryBag) *pBag, S32 iRank, GuildRankPermissions ePerms)
{
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	if (NONNULL(pMember) && iRank >= 0 && NONNULL(pBag->pGuildBankInfo) && iRank < eaSize(&pBag->pGuildBankInfo->eaPermissions)) {
		pBag->pGuildBankInfo->eaPermissions[iRank]->ePerms |= ePerms;
		aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_BankPermissionSet", false);
		aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Permission %s enabled on bank tab %d for rank %d.", StaticDefineIntRevLookup(GuildRankPermissionsEnum, ePerms), pBag->BagID-InvBagIDs_Bank1+1, iRank+1);
	}
}

// Enable guild bank permissions
AUTO_TRANS_HELPER;
void aslGuild_trh_EnableBankPermissionLite(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, ATH_ARG NOCONST(InventoryBagLite) *pBag, S32 iRank, GuildRankPermissions ePerms)
{
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	if (NONNULL(pMember) && iRank >= 0 && NONNULL(pBag->pGuildBankInfo) && iRank < eaSize(&pBag->pGuildBankInfo->eaPermissions)) {
		pBag->pGuildBankInfo->eaPermissions[iRank]->ePerms |= ePerms;
		aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_BankPermissionSet", false);
		aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Permission %s enabled on bank tab %d for rank %d.", StaticDefineIntRevLookup(GuildRankPermissionsEnum, ePerms), pBag->BagID-InvBagIDs_Bank1+1, iRank+1);
	}
}

// Disable guild bank permissions
AUTO_TRANS_HELPER;
void aslGuild_trh_DisableBankPermission(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, ATH_ARG NOCONST(InventoryBag) *pBag, S32 iRank, GuildRankPermissions ePerms)
{
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	if (NONNULL(pMember) && iRank >= 0 && NONNULL(pBag->pGuildBankInfo) && iRank < eaSize(&pBag->pGuildBankInfo->eaPermissions)) {
		pBag->pGuildBankInfo->eaPermissions[iRank]->ePerms &= ~ePerms;
		aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_BankPermissionRevoked", false);
		aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Permission %s disabled on bank tab %d for rank %d.", StaticDefineIntRevLookup(GuildRankPermissionsEnum, ePerms), pBag->BagID-InvBagIDs_Bank1+1, iRank+1);
	}
}

// Disable guild bank permissions
AUTO_TRANS_HELPER;
void aslGuild_trh_DisableBankPermissionLite(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, ATH_ARG NOCONST(InventoryBagLite) *pBag, S32 iRank, GuildRankPermissions ePerms)
{
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	if (NONNULL(pMember) && iRank >= 0 && NONNULL(pBag->pGuildBankInfo) && iRank < eaSize(&pBag->pGuildBankInfo->eaPermissions)) {
		pBag->pGuildBankInfo->eaPermissions[iRank]->ePerms &= ~ePerms;
		aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_BankPermissionRevoked", false);
		aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Permission %s disabled on bank tab %d for rank %d.", StaticDefineIntRevLookup(GuildRankPermissionsEnum, ePerms), pBag->BagID-InvBagIDs_Bank1+1, iRank+1);
	}
}

// Set guild bank withdraw limit
AUTO_TRANS_HELPER;
void aslGuild_trh_SetBankWithdrawLimit(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, NOCONST(GuildBankTabInfo) *pGuildBankInfo, S32 iRank, S32 iLimit)
{
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	if (NONNULL(pMember) && iRank >= 0 && NONNULL(pGuildBankInfo) && iRank < eaSize(&pGuildBankInfo->eaPermissions)) {
		pGuildBankInfo->eaPermissions[iRank]->iWithdrawLimit = iLimit;
		aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_BankWithdrawLimitSet", false);
		aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Set withdraw limit on bank tab %s for rank %d to %d", pGuildBankInfo->pcName, iRank+1, iLimit);
	}
}

// Set guild bank item withdraw limit
AUTO_TRANS_HELPER;
void aslGuild_trh_SetBankItemWithdrawLimit(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, NOCONST(GuildBankTabInfo) *pGuildBankInfo, S32 iRank, S32 iCount)
{
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	if (pMember && iRank >= 0 && NONNULL(pGuildBankInfo) && iRank < eaSize(&pGuildBankInfo->eaPermissions)) {
		pGuildBankInfo->eaPermissions[iRank]->iWithdrawItemCountLimit = iCount;
		aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_BankWithdrawLimitSet", false);
		aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Set withdraw item limit on bank tab %s for rank %d to %d", pGuildBankInfo->pcName, iRank+1, iCount);
	}
}

// Set the guild's MotD
AUTO_TRANS_HELPER;
void aslGuild_trh_SetMotD(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, char *pcMotD)
{
	if(pGuildContainer->bIsOwnedBySystem)
	{
		// already checked at a higher level
		pGuildContainer->pcMotD = StructAllocString(pcMotD);
		pGuildContainer->iMotDUpdated = timeSecondsSince2000();
	}
	else
	{
		NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
		if (NONNULL(pMember)) {
			pGuildContainer->pcMotD = StructAllocString(pcMotD);
			pGuildContainer->iMotDUpdated = timeSecondsSince2000();
			aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_MotDSet", false);
			aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Set MotD to: %s", pcMotD);
		}
	}
}

// Set the guild's Description
AUTO_TRANS_HELPER;
void aslGuild_trh_SetDescription(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, char *pcDescription)
{
	if(pGuildContainer->bIsOwnedBySystem)
	{
		// already checked at a higher level
		pGuildContainer->pcDescription = StructAllocString(pcDescription);
	}
	else
	{
		NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
		if (NONNULL(pMember)) {
			pGuildContainer->pcDescription = StructAllocString(pcDescription);
			aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_DescriptionSet", false);
			aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Set Description to: %s", pcDescription);
		}
	}
}

// Set the guild's Recruit Message
AUTO_TRANS_HELPER;
void aslGuild_trh_SetRecruitMessage(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, char *pcRecruitMessage)
{
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	if (NONNULL(pMember)) {
		pGuildContainer->pcRecruitMessage = StructAllocString(pcRecruitMessage);
		aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_RecruitMessageSet", false);
		aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Set Recruit Message to: %s", pcRecruitMessage);
	}
}

// Set the guild's Web Site
AUTO_TRANS_HELPER;
void aslGuild_trh_SetWebSite(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, char *pcWebSite)
{
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	if (NONNULL(pMember)) {
		pGuildContainer->pcWebSite = StructAllocString(pcWebSite);
		aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_WebSiteSet", false);
		aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Set Web Site to: %s", pcWebSite);
	}
}

// Set a guild's recruit category
AUTO_TRANS_HELPER;
void aslGuild_trh_SetRecruitCat(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, char *pcRecruitCat, int bSet)
{
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	if (NONNULL(pMember)) {
		int i;
		const char *temp = allocAddString(pcRecruitCat);
		for (i = eaSize(&pGuildContainer->eaRecruitCat)-1; i >= 0; --i)
		{
			if (pGuildContainer->eaRecruitCat[i] && pGuildContainer->eaRecruitCat[i]->pcName && temp == pGuildContainer->eaRecruitCat[i]->pcName)
			{
				break;
			}
		}
		if (bSet && i < 0)
		{
			NOCONST(GuildRecruitCat) *grc = StructCreateNoConst(parse_GuildRecruitCat);
			grc->pcName = temp;
			eaPush(&pGuildContainer->eaRecruitCat, grc);
			aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_RecruitCatSet", false);
			aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Set Recruit Category '%s' to: True", pcRecruitCat);
		}
		else if (i >= 0 && !bSet)
		{
			StructDestroyNoConst(parse_GuildRecruitCat, pGuildContainer->eaRecruitCat[i]);
			eaRemove(&pGuildContainer->eaRecruitCat, i);
			aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_RecruitCatSet", false);
			aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Set Recruit Category '%s' to: False", pcRecruitCat);
		}
	}
}

// Set the guild's Web Site
AUTO_TRANS_HELPER;
void aslGuild_trh_SetMinLevelRecruit(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, int iMin)
{
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	if (NONNULL(pMember)) {
		pGuildContainer->iMinLevelRecruit = iMin;
		aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_MinLevelRecruitSet", false);
		aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Set Min Recruit Level to: %d", iMin);
	}
}

// Set the guild's Web Site
AUTO_TRANS_HELPER;
void aslGuild_trh_SetRecruitVisibility(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, int bShow)
{
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	if (NONNULL(pMember)) {
		pGuildContainer->bHideRecruitMessage = bShow ? false : true;
		aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_RecruitVisibilitySet", false);
		aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Set Recruit Visibility to: %s", bShow ? "Show" : "Hide");
	}
}

// Set the guild's Web Site
AUTO_TRANS_HELPER;
void aslGuild_trh_SetRecruitMemberVisibility(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, int bShow)
{
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	if (NONNULL(pMember)) {
		pGuildContainer->bHideMembers = bShow ? false : true;
		aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_RecruitVisibilitySet", false);
		aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Set Recruit Member Visibility to: %s", bShow ? "Show" : "Hide");
	}
}

// Set the guild's emblem
AUTO_TRANS_HELPER;
void aslGuild_trh_SetEmblem(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, char *pcEmblem)
{
	pGuildContainer->pcEmblem = allocAddString(pcEmblem);
	if(!pGuildContainer->bIsOwnedBySystem)
	{
		NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
		if (NONNULL(pMember)) {
			aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_EmblemSet", false);
			aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Set emblem to %s", pcEmblem);
		}
	}
}

// Set the guild's emblem
AUTO_TRANS_HELPER;
void aslGuild_trh_SetAdvancedEmblem(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, char *pcEmblem, U32 iEmblemColor0, U32 iEmblemColor1, F32 fEmblemRotation, bool bHidePlayerFeedback)
{

	if (pcEmblem)
	{
		pGuildContainer->pcEmblem = allocAddString(pcEmblem); 
	}
	else
	{
		pGuildContainer->pcEmblem = NULL;
	}

	pGuildContainer->iEmblemColor0 = iEmblemColor0;
	pGuildContainer->iEmblemColor1 = iEmblemColor1;
	pGuildContainer->fEmblemRotation = fEmblemRotation;

	if(!pGuildContainer->bIsOwnedBySystem)
	{
		NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
		if (NONNULL(pMember))
		{
			if (!bHidePlayerFeedback) aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_EmblemSet", false);
			aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Set emblem to %s, colors[0x%08X][0x%08X] rotation[%f deg]", pcEmblem, iEmblemColor0, iEmblemColor1, DEG(fEmblemRotation));
		}
	}
}

// Set the guild's emblem
AUTO_TRANS_HELPER;
void aslGuild_trh_SetAdvancedEmblem2(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, char *pcEmblem2, U32 iEmblem2Color0, U32 iEmblem2Color1, F32 fEmblem2Rotation, F32 fEmblem2X, F32 fEmblem2Y, F32 fEmblem2ScaleX, F32 fEmblem2ScaleY)
{
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	if (NONNULL(pMember)) {
		if (pcEmblem2) pGuildContainer->pcEmblem2 = allocAddString(pcEmblem2); else pGuildContainer->pcEmblem2 = NULL;
		pGuildContainer->iEmblem2Color0 = iEmblem2Color0;
		pGuildContainer->iEmblem2Color1 = iEmblem2Color1;
		pGuildContainer->fEmblem2Rotation = fEmblem2Rotation;
		pGuildContainer->fEmblem2X = fEmblem2X;
		pGuildContainer->fEmblem2Y = fEmblem2Y;
		pGuildContainer->fEmblem2ScaleX = fEmblem2ScaleX;
		pGuildContainer->fEmblem2ScaleY = fEmblem2ScaleY;
		aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_EmblemSet", false);
		aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Set emblem#2 to %s, colors[0x%08X][0x%08X] rotation[%f deg] X[%f] Y[%f] ScaleX[%f] ScaleY[%f]", pcEmblem2, iEmblem2Color0, iEmblem2Color1, DEG(fEmblem2Rotation), fEmblem2X, fEmblem2Y, fEmblem2ScaleX, fEmblem2ScaleY);
	}
}

// Set the guild's emblem
AUTO_TRANS_HELPER;
void aslGuild_trh_SetAdvancedEmblem3(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, char *pcEmblem3, bool bHidePlayerFeedback)
{
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	if (NONNULL(pMember)) {
		if (pcEmblem3) pGuildContainer->pcEmblem3 = allocAddString(pcEmblem3); else pGuildContainer->pcEmblem3 = NULL;
		if (!bHidePlayerFeedback) aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_EmblemSet", false);
		aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Set emblem#3 to %s", pcEmblem3);
	}
}

// Set the guild's first color
AUTO_TRANS_HELPER;
void aslGuild_trh_SetColors(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, U32 iColor1, U32 iColor2)
{
	pGuildContainer->iColor1 = iColor1;
	pGuildContainer->iColor2 = iColor2;
	if(!pGuildContainer->bIsOwnedBySystem)
	{
		NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
		if (NONNULL(pMember)) {
			aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_ColorsSet", false);
			aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Set colors to %d and %d", iColor1, iColor2);
		}
	}
}

// Set the guild member's stored level
AUTO_TRANS_HELPER;
void aslGuild_trh_SetLevel(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, U32 iLevel)
{
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	if (NONNULL(pMember)) {
		pMember->iLevel = iLevel;
		pGuildContainer->iVersion++;
	}
}

// Set the guild member's stored location
AUTO_TRANS_HELPER;
void aslGuild_trh_SetLocation(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, const char *pcMapName, const char *pcMapMsgKey, const char *pcMapVars, U32 iInstanceNumber)
{
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	if (NONNULL(pMember)) {
		pMember->pcMapName = allocAddString(pcMapName);
		pMember->pcMapVars = allocAddString(pcMapVars);
		pMember->pcMapMsgKey = StructAllocString(pcMapMsgKey);
		pMember->iMapInstanceNumber = iInstanceNumber;
		pGuildContainer->iVersion++;
	}
}

// Set the guild member's LFG status
AUTO_TRANS_HELPER;
void aslGuild_trh_SetLFGStatus(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, U32 eLFGMode)
{
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	if (NONNULL(pMember)) {
		pMember->eLFGMode = eLFGMode;
		pGuildContainer->iVersion++;
	}
}

// Set the guild member's stored status
AUTO_TRANS_HELPER;
void aslGuild_trh_SetStatus(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, const char *pcStatus)
{
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	if (NONNULL(pMember)) {
		pMember->pcStatus = StructAllocString(pcStatus);
		pGuildContainer->iVersion++;
	}
}

// Set the guild member's online status
AUTO_TRANS_HELPER;
void aslGuild_trh_SetOnline(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, U32 iEntID, bool bOnline)
{
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	if (NONNULL(pMember) && pMember->bOnline != bOnline) {
		pMember->iLogoutTime = timeSecondsSince2000(); //We need to record logout time even at login otherwise if the server crashes the logout time wont be recorded.
		pMember->bOnline = bOnline;
		pGuildContainer->iVersion++;
	}
}

// Set the guild member's name and account name
AUTO_TRANS_HELPER;
void aslGuild_trh_SetMemberName(GUILD_TRH_ARGS, ATH_ARG NOCONST(Guild) *pGuildContainer, ATH_ARG NOCONST(Entity) *pEnt)
{
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, pEnt->myContainerID);
	if (NONNULL(pMember)) {
		static char pcAccountName[MAX_NAME_LEN+1];
		if (pMember->pcName) {
			StructFreeString(pMember->pcName);
		}
		pMember->pcName = StructAllocString(aslGuild_trh_GetDisplayName(GUILD_TRH_PASS_ARGS, pEnt));
		if (pMember->pcAccount) {
			StructFreeString(pMember->pcAccount);
		}
		sprintf(pcAccountName, "@%s", pEnt->pPlayer->publicAccountName);
		pMember->pcAccount = StructAllocString(pcAccountName);
		if (pMember->pcLogName) {
			StructFreeString(pMember->pcLogName);
		}
		pMember->pcLogName = StructAllocString(pEnt->debugName);
		pGuildContainer->iVersion++;
	}
}

// Clear all guild data from the entity
AUTO_TRANS_HELPER;
void aslGuild_trh_ClearPlayerGuild(GUILD_TRH_ARGS, ATH_ARG NOCONST(Entity) *pEnt)
{
	if (NONNULL(pEnt->pPlayer->pGuild)) {
		pEnt->pPlayer->pGuild->iGuildID = 0;
		if (pEnt->pPlayer->pGuild->pcInviterName) {
			StructFreeString(pEnt->pPlayer->pGuild->pcInviterName);
			pEnt->pPlayer->pGuild->pcInviterName = NULL;
			aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pEnt->debugName, 0, NULL, "Cleared guild info");
		}
	}
}

AUTO_TRANS_HELPER;
bool aslGuild_trh_CheckInviteeAllegiance(ATH_ARG NOCONST(Guild) *pGuildContainer, ATH_ARG NOCONST(Entity) *pInviteeEnt)
{
	if (!gConf.bEnforceGuildGuildMemberAllegianceMatch)
	{
		return true;
	}
	
	if ( ( pGuildContainer->pcAllegiance != NULL ) && ( *pGuildContainer->pcAllegiance != '\0' ) )
	{
		const char *playerAllegianceString = REF_STRING_FROM_HANDLE(pInviteeEnt->hAllegiance);
		if (playerAllegianceString != NULL)
		{
			if ( stricmp(playerAllegianceString, pGuildContainer->pcAllegiance) == 0 )
			{
				// player has allegiance and it matches guild
				return true;
			}
		}

		// player doesn't have allegiance or it doesn't match guild
		return false;
	}

	// no guild allegiance, so always accept
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Non-Transaction Helpers
///////////////////////////////////////////////////////////////////////////////////////////
// This represents a lot of validation code that is repeated amung the many remote
// commands below.

// Return whether the entity is a member of the guild
bool aslGuild_IsMember(Guild *pGuild, U32 iEntID) {
	if (eaIndexedGetUsingInt(&pGuild->eaMembers, iEntID)) {
		return true;
	}
	return false;
}

// Return whether the entity is invited to the guild
bool aslGuild_IsInvite(Guild *pGuild, U32 iEntID) {
	if (eaIndexedGetUsingInt(&pGuild->eaInvites, iEntID)) {
		return true;
	}
	return false;
}

// Check if the entity is in the guild, and call the appropriate validation if not
bool aslGuild_CheckIsMember(Guild *pGuild, U32 iEntID, const char *pcActionType) {
	if (!aslGuild_IsMember(pGuild, iEntID)) {
		aslGuild_SendFeedback(iEntID, iEntID, 0, aslGuild_GetName(pGuild), pcActionType, "GuildServer_SelfNotInGuild", false);
		RemoteCommand_aslGuild_ValidateMember(GetAppGlobalType(), 0, pGuild->iContainerID, iEntID);
		return false;
	}
	return true;
}

// Check if the entity is not in the guild, and send the appropriate feedback if so
bool aslGuild_CheckIsNotMember(Guild *pGuild, U32 iEntID, const char *pcActionType) {
	if (aslGuild_IsMember(pGuild, iEntID)) {
		aslGuild_SendFeedback(iEntID, iEntID, 0, aslGuild_GetName(pGuild), pcActionType, "GuildServer_SelfAlreadyInGuild", false);
		RemoteCommand_aslGuild_ValidateMember(GetAppGlobalType(), 0, pGuild->iContainerID, iEntID);
		return false;
	}
	return true;
}

// Check if the subject is in the guild, and call the appropriate validation if not
bool aslGuild_CheckSubjectIsMember(Guild *pGuild, U32 iEntID, U32 iSubjectID, const char *pcActionType) {
	if (!aslGuild_IsMember(pGuild, iSubjectID)) {
		aslGuild_SendFeedback(iEntID, iEntID, iSubjectID, aslGuild_GetName(pGuild), pcActionType, "GuildServer_NotInGuild", false);
		RemoteCommand_aslGuild_ValidateMember(GetAppGlobalType(), 0, pGuild->iContainerID, iSubjectID);
		return false;
	}
	return true;
}

// Check if the subject is not in the guild, and send the appropriate feedback if so
bool aslGuild_CheckSubjectIsNotMember(Guild *pGuild, U32 iEntID, U32 iSubjectID, const char *pcActionType) {
	if (aslGuild_IsMember(pGuild, iSubjectID)) {
		aslGuild_SendFeedback(iEntID, iEntID, iSubjectID, aslGuild_GetName(pGuild), pcActionType, "GuildServer_AlreadyInGuild", false);
		RemoteCommand_aslGuild_ValidateMember(GetAppGlobalType(), 0, pGuild->iContainerID, iSubjectID);
		return false;
	}
	return true;
}

// Check if the entity is invited to the guild, and call the appropriate validation if not
bool aslGuild_CheckIsInvite(Guild *pGuild, U32 iEntID, const char *pcActionType) {
	if (!aslGuild_IsInvite(pGuild, iEntID)) {
		aslGuild_SendFeedback(iEntID, iEntID, 0, aslGuild_GetName(pGuild), pcActionType, "GuildServer_SelfNotInvited", false);
		RemoteCommand_aslGuild_ValidateInvite(GetAppGlobalType(), 0, pGuild->iContainerID, iEntID);
		return false;
	}
	return true;
}

// Check if the entity is not invited to the guild, and send the appropriate feedback if so
bool aslGuild_CheckIsNotInvite(Guild *pGuild, U32 iEntID, const char *pcActionType) {
	if (aslGuild_IsInvite(pGuild, iEntID)) {
		aslGuild_SendFeedback(iEntID, iEntID, 0, aslGuild_GetName(pGuild), pcActionType, "GuildServer_SelfAlreadyInvited", false);
		RemoteCommand_aslGuild_ValidateInvite(GetAppGlobalType(), 0, pGuild->iContainerID, iEntID);
		return false;
	}
	return true;
}

// Check if the subject is invited to the guild, and call the appropriate validation if not
bool aslGuild_CheckSubjectIsInvite(Guild *pGuild, U32 iEntID, U32 iSubjectID, const char *pcActionType) {
	if (!aslGuild_IsInvite(pGuild, iSubjectID)) {
		aslGuild_SendFeedback(iEntID, iEntID, iSubjectID, aslGuild_GetName(pGuild), pcActionType, "GuildServer_NotInvited", false);
		RemoteCommand_aslGuild_ValidateInvite(GetAppGlobalType(), 0, pGuild->iContainerID, iSubjectID);
		return false;
	}
	return true;
}

// Check if the subject is not invited to the guild, and send the appropriate feedback if so
bool aslGuild_CheckSubjectIsNotInvite(Guild *pGuild, U32 iEntID, U32 iSubjectID, const char *pcActionType) {
	if (aslGuild_IsInvite(pGuild, iSubjectID)) {
		aslGuild_SendFeedback(iEntID, iEntID, iSubjectID, aslGuild_GetName(pGuild), pcActionType, "GuildServer_AlreadyInvited", false);
		RemoteCommand_aslGuild_ValidateInvite(GetAppGlobalType(), 0, pGuild->iContainerID, iSubjectID);
		return false;
	}
	return true;
}

// Check if the guild exists, and call the appropriate validation if not
bool aslGuild_CheckExists(Guild *pGuild, U32 iGuildID, U32 iEntID, const char *pcActionType) {
	if (!pGuild) {
		if (iGuildID) {
			aslGuild_SendFeedback(iEntID, iEntID, 0, "", pcActionType, "GuildServer_GuildNotFound", false);
			RemoteCommand_aslGuild_ValidateExists(GetAppGlobalType(), 0, iGuildID, iEntID);
		} else {
			aslGuild_SendFeedback(iEntID, iEntID, 0, "", pcActionType, "GuildServer_SelfNotInGuild", false);
		}
		return false;
	}
	return true;
}

// Check if the guild bank exists, and call the appropriate validation if not
bool aslGuild_CheckBankExists(Entity *pGuildBank, U32 iGuildID, U32 iEntID, const char *pcActionType) {
	if (!pGuildBank) {
		if (iGuildID) {
			aslGuild_SendFeedback(iEntID, iEntID, 0, "", pcActionType, "GuildServer_GuildBankNotFound", false);
			RemoteCommand_aslGuild_ValidateBankExists(GetAppGlobalType(), 0, iGuildID);
		} else {
			aslGuild_SendFeedback(iEntID, iEntID, 0, "", pcActionType, "GuildServer_SelfNotInGuild", false);
		}
		return false;
	}
	return true;
}

// Check if the guild exists, and send the appropriate feedback if not
bool aslGuild_CheckExistsNoValidate(Guild *pGuild, U32 iEntID, const char *pcActionType) {
	if (!pGuild) {
		aslGuild_SendFeedback(iEntID, iEntID, 0, aslGuild_GetName(pGuild), pcActionType, "GuildServer_GuildNotFound", false);
		return false;
	}
	return true;
}

// Check if the guild is full, and send the appropriate feedback if not
bool aslGuild_CheckIsNotFull(Guild *pGuild, U32 iEntID, const char *pcActionType) {
	if (eaSize(&pGuild->eaMembers) >= GUILD_MAX_SIZE) {
		aslGuild_SendFeedback(iEntID, iEntID, 0, aslGuild_GetName(pGuild), pcActionType, "GuildServer_GuildFull", false);
		return false;
	}
	return true;
}

// Check if this is a valid guild name, and send the appropriate feedback if not
bool aslGuild_CheckName(Guild *pGuild, U32 iEntID, const char *pcName, const char *pcActionType, ContainerID iVirtualShardID) {
	Guild *pExistingGuild;

	if (strlen(pcName) > MAX_NAME_LEN) {
		aslGuild_SendFeedback(iEntID, iEntID, 0, aslGuild_GetName(pGuild), pcActionType, "GuildServer_NameTooLong", true);
		return false;
	}
	if (strlen(pcName) < 3) {
		aslGuild_SendFeedback(iEntID, iEntID, 0, aslGuild_GetName(pGuild), pcActionType, "GuildServer_NameTooShort", true);
		return false;
	}
	pExistingGuild = aslGuild_GetGuildByName(pcName, iVirtualShardID);
	if (pExistingGuild && ((pGuild == NULL) || (pExistingGuild->iContainerID != pGuild->iContainerID))) {
		aslGuild_SendFeedback(iEntID, iEntID, 0, aslGuild_GetName(pGuild), pcActionType, "GuildServer_NameTaken", true);
		return false;
	}
	return true;
}

// Check if this is a valid guild rank name, and send the appropriate feedback if not
bool aslGuild_CheckRankName(Guild *pGuild, U32 iEntID, const char *pcName, const char *pcActionType) {
	if (strlen(pcName) > MAX_NAME_LEN) {
		aslGuild_SendFeedback(iEntID, iEntID, 0, aslGuild_GetName(pGuild), pcActionType, "GuildServer_NameTooLong", false);
		return false;
	}
	if (strlen(pcName) < 3) {
		aslGuild_SendFeedback(iEntID, iEntID, 0, aslGuild_GetName(pGuild), pcActionType, "GuildServer_NameTooShort", false);
		return false;
	}
	return true;
}

// Check if this is a valid guild bank tab name, and send the appropriate feedback if not
bool aslGuild_CheckBankTabName(Guild *pGuild, U32 iEntID, const char *pcName, const char *pcActionType) {
	if (strlen(pcName) > MAX_NAME_LEN) {
		aslGuild_SendFeedback(iEntID, iEntID, 0, aslGuild_GetName(pGuild), pcActionType, "GuildServer_NameTooLong", false);
		return false;
	}
	if (strlen(pcName) < 3) {
		aslGuild_SendFeedback(iEntID, iEntID, 0, aslGuild_GetName(pGuild), pcActionType, "GuildServer_NameTooShort", false);
		return false;
	}
	return true;
}

// Check if this is a valid guild bank tab name, and send the appropriate feedback if not
bool aslGuild_CheckProfanity(Guild *pGuild, U32 iEntID, const char *pcName, const char *pcActionType) {
	if (IsAnyProfane(pcName)) {
		aslGuild_SendFeedback(iEntID, iEntID, 0, aslGuild_GetName(pGuild), pcActionType, "GuildServer_ContainsProfanity", false);
		return false;
	}
	return true;
}

// Check if this is a valid guild emblem, and send the appropriate feedback if not
bool aslGuild_CheckEmblem(Guild *pGuild, U32 iEntID, const char *pcEmblem, const char *pcActionType) {
	S32 i;
	for (i = eaSize(&g_GuildEmblems.eaEmblems)-1; i >= 0; i--) {
		const char* pchTextureName = REF_STRING_FROM_HANDLE(g_GuildEmblems.eaEmblems[i]->hTexture);
		if (stricmp(pchTextureName, pcEmblem) == 0) {
			return true;
		}
	}
	aslGuild_SendFeedback(iEntID, iEntID, 0, aslGuild_GetName(pGuild), pcActionType, "GuildServer_InvalidEmblem", false);
	return false;
}

// Check if this is a valid guild emblem, and send the appropriate feedback if not
bool aslGuild_CheckEmblemType(Guild *pGuild, U32 iEntID, const char *pcEmblem, bool bBackground, bool bDetail, const char *pcActionType) {
	S32 i;
	if (!pcEmblem) return true;
	if (!*pcEmblem) return true;
	for (i = eaSize(&g_GuildEmblems.eaEmblems)-1; i >= 0; i--) {
		const char* pchTextureName = REF_STRING_FROM_HANDLE(g_GuildEmblems.eaEmblems[i]->hTexture);
		if (stricmp(pchTextureName, pcEmblem) == 0) {
			if (bBackground != (bool)g_GuildEmblems.eaEmblems[i]->bBackground && !bDetail) break;
			if (bDetail != (bool)g_GuildEmblems.eaEmblems[i]->bDetail) break;
			return true;
		}
	}
	if(iEntID)
	{
		aslGuild_SendFeedback(iEntID, iEntID, 0, aslGuild_GetName(pGuild), pcActionType, "GuildServer_InvalidEmblem", false);
	}
	return false;
}

// Check if the player has this permission, and send the appropriate feedback if not
bool aslGuild_CheckPermission(Guild *pGuild, U32 iEntID, GuildRankPermissions ePerm, bool bSettingThis, const char *pcActionType) {
	GuildMember *pMember = eaIndexedGetUsingInt(&pGuild->eaMembers, iEntID);
	static char pcMessageName[128];
	
	if (pMember) {
		if (!guild_HasPermission(pMember->iRank, pGuild, ePerm)) {
			if (bSettingThis) {
				aslGuild_SendFeedback(iEntID, iEntID, 0, aslGuild_GetName(pGuild), pcActionType, "GuildServer_GivingUnownedPermission", false);
			} else {
				sprintf(pcMessageName, "GuildServer_NoPermission_%s", StaticDefineIntRevLookup(GuildRankPermissionsEnum, ePerm));
				aslGuild_SendFeedback(iEntID, iEntID, 0, aslGuild_GetName(pGuild), pcActionType, pcMessageName, false);
			}
			return false;
		}
		return true;
	}
	return false;
}

// Check if the player has this bank permission, and send the appropriate feedback if not
bool aslGuild_CheckBankPermission(Guild *pGuild, Entity *pGuildBank, U32 iEntID, U32 iBagID, GuildBankPermissions ePerm, bool bSettingThis, const char *pcActionType) {
	GuildMember *pMember = eaIndexedGetUsingInt(&pGuild->eaMembers, iEntID);
	InventoryBag *pBag = inv_guildbank_GetBag(pGuildBank, iBagID);
	InventoryBagLite *pBagLite = !pBag ? inv_guildbank_GetLiteBag(pGuildBank, iBagID) : NULL;
	static char pcMessageName[128];
	
	if (pMember && pBag && pBag->pGuildBankInfo && pMember->iRank < eaSize(&pBag->pGuildBankInfo->eaPermissions)) {
		if (!(pBag->pGuildBankInfo->eaPermissions[pMember->iRank]->ePerms & ePerm)) {
			if (bSettingThis) {
				aslGuild_SendFeedback(iEntID, iEntID, 0, aslGuild_GetName(pGuild), pcActionType, "GuildServer_GivingUnownedPermission", false);
			} else {
				sprintf(pcMessageName, "GuildServer_NoBankPermission_%s", StaticDefineIntRevLookup(GuildRankPermissionsEnum, ePerm));
				aslGuild_SendFeedback(iEntID, iEntID, 0, aslGuild_GetName(pGuild), pcActionType, pcMessageName, false);
			}
			return false;
		}
		return true;
	}
	if (pMember && pBagLite && pBagLite->pGuildBankInfo && pMember->iRank < eaSize(&pBagLite->pGuildBankInfo->eaPermissions)) {
		if (!(pBagLite->pGuildBankInfo->eaPermissions[pMember->iRank]->ePerms & ePerm)) {
			if (bSettingThis) {
				aslGuild_SendFeedback(iEntID, iEntID, 0, aslGuild_GetName(pGuild), pcActionType, "GuildServer_GivingUnownedPermission", false);
			} else {
				sprintf(pcMessageName, "GuildServer_NoBankPermission_%s", StaticDefineIntRevLookup(GuildRankPermissionsEnum, ePerm));
				aslGuild_SendFeedback(iEntID, iEntID, 0, aslGuild_GetName(pGuild), pcActionType, pcMessageName, false);
			}
			return false;
		}
		return true;
	}
	return false;
}

// Check if the player can promote the subject, and send the appropriate feedback if not
bool aslGuild_CheckCanPromote(Guild *pGuild, U32 iEntID, U32 iSubjectID, const char *pcActionType) {
	GuildMember *pMember = eaIndexedGetUsingInt(&pGuild->eaMembers, iEntID);
	GuildMember *pSubjectMember = eaIndexedGetUsingInt(&pGuild->eaMembers, iSubjectID);
	if (pMember && pSubjectMember) {
		if (pMember == pSubjectMember) {
			aslGuild_SendFeedback(iEntID, iEntID, iSubjectID, aslGuild_GetName(pGuild), pcActionType, "GuildServer_CantPromoteSelf", false);
			return false;
		}
		
		if (pMember->iRank > (pSubjectMember->iRank+1)) {
			if (!(guild_HasPermission(pMember->iRank, pGuild, GuildPermission_PromoteBelowRank) || guild_HasPermission(pMember->iRank, pGuild, GuildPermission_PromoteToRank))) {
				aslGuild_SendFeedback(iEntID, iEntID, iSubjectID, aslGuild_GetName(pGuild), pcActionType, "GuildServer_NoPermission_PromoteBelowRank", false);
				return false;
			}
		} else if (pMember->iRank == (pSubjectMember->iRank+1)) {
			if (!guild_HasPermission(pMember->iRank, pGuild, GuildPermission_PromoteToRank)) {
				aslGuild_SendFeedback(iEntID, iEntID, iSubjectID, aslGuild_GetName(pGuild), pcActionType, "GuildServer_NoPermission_PromoteToRank", false);
				return false;
			}
		} else {
			aslGuild_SendFeedback(iEntID, iEntID, iSubjectID, aslGuild_GetName(pGuild), pcActionType, "GuildServer_HigherRank", false);
			return false;
		}
		
		if (pSubjectMember->iRank >= eaSize(&pGuild->eaRanks)-1) {
			aslGuild_SendFeedback(iEntID, iEntID, iSubjectID, aslGuild_GetName(pGuild), pcActionType, "GuildServer_AlreadyHighestRank", false);
			return false;
		}
		
		return true;
	}
	return false;
}

// Check if the player can demote the subject, and send the appropriate feedback if not
bool aslGuild_CheckCanDemote(Guild *pGuild, U32 iEntID, U32 iSubjectID, const char *pcActionType) {
	GuildMember *pMember = eaIndexedGetUsingInt(&pGuild->eaMembers, iEntID);
	GuildMember *pSubjectMember = eaIndexedGetUsingInt(&pGuild->eaMembers, iSubjectID);
	if (pMember && pSubjectMember) {
		if (pMember->iRank > pSubjectMember->iRank) {
			if (!(guild_HasPermission(pMember->iRank, pGuild, GuildPermission_DemoteBelowRank) || guild_HasPermission(pMember->iRank, pGuild, GuildPermission_DemoteAtRank))) {
				aslGuild_SendFeedback(iEntID, iEntID, iSubjectID, aslGuild_GetName(pGuild), pcActionType, "GuildServer_NoPermission_DemoteBelowRank", false);
				return false;
			}
		} else if (pMember->iRank == pSubjectMember->iRank) {
			if (!guild_HasPermission(pMember->iRank, pGuild, GuildPermission_DemoteAtRank)) {
				aslGuild_SendFeedback(iEntID, iEntID, iSubjectID, aslGuild_GetName(pGuild), pcActionType, "GuildServer_NoPermission_DemoteAtRank", false);
				return false;
			}
		} else {
			aslGuild_SendFeedback(iEntID, iEntID, iSubjectID, aslGuild_GetName(pGuild), pcActionType, "GuildServer_HigherRank", false);
			return false;
		}
		
		if (pSubjectMember->iRank <= 0) {
			aslGuild_SendFeedback(iEntID, iEntID, iSubjectID, aslGuild_GetName(pGuild), pcActionType, "GuildServer_AlreadyLowestRank", false);
			return false;
		}
		
		return true;
	}
	return false;
}

// Check if the player can edit this rank, and send the appropriate feedback if not
bool aslGuild_CheckCanEditRank(Guild *pGuild, U32 iEntID, S32 iRank, const char *pcActionType) {
	GuildMember *pMember = eaIndexedGetUsingInt(&pGuild->eaMembers, iEntID);
	if (pMember) {
		if (pMember->iRank < iRank) {
			aslGuild_SendFeedback(iEntID, iEntID, 0, aslGuild_GetName(pGuild), pcActionType, "GuildServer_EditRankTooHigh", false);
			return false;
		}
		if (iRank >= eaSize(&pGuild->eaRanks)-1) {
			aslGuild_SendFeedback(iEntID, iEntID, 0, aslGuild_GetName(pGuild), pcActionType, "GuildServer_CantRemoveLeaderPerms", false);
			return false;
		}
		return true;
	}
	return iEntID == 0;
}

// Check if the player can set this withdraw limit, and send the appropriate feedback if not
bool aslGuild_CheckCanSetWithdrawLimit(Guild *pGuild, Entity *pGuildBank, U32 iEntID, S32 iRank, U32 iBagID, U32 iLimit, const char *pcActionType) {
	GuildMember *pMember = eaIndexedGetUsingInt(&pGuild->eaMembers, iEntID);
	static char pcMessageName[128];
	NOCONST(GuildBankTabInfo) *pGuildBankInfo = inv_GuildbankGetBankTabInfo(pGuildBank, iBagID);
	
	if (pMember && pGuildBankInfo && pMember->iRank < eaSize(&pGuildBankInfo->eaPermissions)) {
		U32 iPlayerLimit = pGuildBankInfo->eaPermissions[pMember->iRank]->iWithdrawLimit;
		if (iRank > pMember->iRank) {
			aslGuild_SendFeedback(iEntID, iEntID, 0, aslGuild_GetName(pGuild), pcActionType, "GuildServer_EditRankTooHigh", false);
			return false;
		}
		if (iPlayerLimit != 0 && (iLimit == 0 || iLimit > iPlayerLimit)) {
			aslGuild_SendFeedback(iEntID, iEntID, 0, aslGuild_GetName(pGuild), pcActionType, "GuildServer_WithdrawLimitHigherThanOwn", false);
			return false;
		}
		return true;
	}
	return false;
}

// Check if the player can set this item withdraw limit, and send the appropriate feedback if not
bool aslGuild_CheckCanSetItemWithdrawLimit(Guild *pGuild, Entity *pGuildBank, U32 iEntID, S32 iRank, U32 iBagID, U32 iCount, const char *pcActionType) {
	GuildMember *pMember = eaIndexedGetUsingInt(&pGuild->eaMembers, iEntID);
	static char pcMessageName[128];
	NOCONST(GuildBankTabInfo) *pGuildBankInfo = inv_GuildbankGetBankTabInfo(pGuildBank, iBagID);

	if (pMember && pGuildBankInfo && pMember->iRank < eaSize(&pGuildBankInfo->eaPermissions)) {
		U32 iPlayerLimit = pGuildBankInfo->eaPermissions[pMember->iRank]->iWithdrawItemCountLimit;
		if (iRank > pMember->iRank) {
			aslGuild_SendFeedback(iEntID, iEntID, 0, aslGuild_GetName(pGuild), pcActionType, "GuildServer_EditRankTooHigh", false);
			return false;
		}
		if (iPlayerLimit != 0 && (iCount == 0 || iCount > iPlayerLimit)) {
			aslGuild_SendFeedback(iEntID, iEntID, 0, aslGuild_GetName(pGuild), pcActionType, "GuildServer_WithdrawLimitHigherThanOwn", false);
			return false;
		}
		return true;
	}
	return false;
}

// Check if this rank is valid, and send the appropriate feedback if not
bool aslGuild_CheckRank(Guild *pGuild, U32 iEntID, S32 iRank, const char *pcActionType) {
	if (iRank < 0 || iRank >= eaSize(&pGuild->eaRanks)) {
		aslGuild_SendFeedback(iEntID, iEntID, 0, aslGuild_GetName(pGuild), pcActionType, "GuildServer_InvalidRank", false);
		return false;
	}
	return true;
}

// Check if this bank tag is valid, and send the appropriate feedback if not
bool aslGuild_CheckBankTab(Guild *pGuild, Entity *pGuildBank, U32 iEntID, U32 iBagID, const char *pcActionType) {
	InventoryBag *pBag = inv_guildbank_GetBag(pGuildBank, iBagID);
	if (!pBag || !(invbag_flags(pBag) & InvBagFlag_GuildBankBag)) {
		InventoryBagLite *pBagLite = inv_guildbank_GetLiteBag(pGuildBank, iBagID);
		if (!pBagLite || !(invbaglite_flags(pBagLite) & InvBagFlag_GuildBankBag)) {
			aslGuild_SendFeedback(iEntID, iEntID, 0, aslGuild_GetName(pGuild), pcActionType, "GuildServer_InvalidBankTab", false);
			return false;
		}
	}
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Create Guild
///////////////////////////////////////////////////////////////////////////////////////////

static void CreateGuildBank_CB(TransactionReturnVal *returnVal, ASLGuildCBData *cbData)
{
	switch(returnVal->eOutcome)
	{
	case TRANSACTION_OUTCOME_FAILURE:
		{
			free(cbData);
			return;
		}
	case TRANSACTION_OUTCOME_SUCCESS:
		{
			ASLGuildCBData *pData = StructClone(parse_ASLGuildCBData, cbData);
			TransactionReturnVal *pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, pData);
			AutoTrans_aslGuild_tr_AddInventory(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, cbData->iGuildID, GLOBALTYPE_ENTITYGUILDBANK, cbData->iGuildID);

			free(cbData);
			return;
		}
	}

}

static void aslGuildBank_Create_Internal(U32 guildID)
{
	if(!objGetContainer(GLOBALTYPE_ENTITYGUILDBANK, guildID))
	{
		ASLGuildCBData *cbData;

		cbData = StructCreate(parse_ASLGuildCBData);
		cbData->iGuildID = guildID;

		objRequestContainerVerifyAndMove(objCreateManagedReturnVal(CreateGuildBank_CB, cbData), GLOBALTYPE_ENTITYGUILDBANK, cbData->iGuildID, GetAppGlobalType(), GetAppGlobalID());
	}
}

void aslGuild_tr_AddInitialMember_CB(TransactionReturnVal *pReturn, ASLGuildCBData *pData)
{
	if (pReturn->eOutcome != TRANSACTION_OUTCOME_SUCCESS) {
		RemoteCommand_aslGuild_Destroy(GetAppGlobalType(), 0, pData->iGuildID, pData->iEntID);
	} else if (pData->iTeamID) {
		RemoteCommand_aslTeam_JoinGuild(GLOBALTYPE_TEAMSERVER, 0, pData->iTeamID, pData->iEntID, pData->iGuildID);
	}
	aslGuild_RemoteCommand_CB(pReturn, pData);
}

void aslGuild_Create_CB(TransactionReturnVal *pReturn, ASLGuildCBData *pData)
{
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS) {
		U32 iGuildID = atoi(pReturn->pBaseReturnVals[0].returnString);
		Guild *pGuild = aslGuild_GetGuild(iGuildID);
		pData->iGuildID = iGuildID;
		
		if(pGuild && !pGuild->bIsOwnedBySystem)
		{
			pReturn = objCreateManagedReturnVal(aslGuild_tr_AddInitialMember_CB, pData);
			AutoTrans_aslGuild_tr_AddInitialMember(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, GLOBALTYPE_ENTITYPLAYER, pData->iEntID, pData->pchClassName);
		}
		else
		{
			StructDestroySafe(parse_ASLGuildCBData, &pData);
		}

		if(!guild_LazyCreateBank())
			aslGuildBank_Create_Internal(iGuildID);

		if (pGuild) {
			RemoteCommand_ChatServerUpdateGuild(NULL, GLOBALTYPE_CHATSERVER, 0, pGuild->iContainerID, pGuild->pcName);
		}
	}
	else
	{
		U32 iGuildID = atoi(pReturn->pBaseReturnVals[0].returnString);
		Guild *pGuild = aslGuild_GetGuild(iGuildID);

		if(pGuild && !pGuild->bIsOwnedBySystem)
		{
			aslGuild_SendFeedback(pData->iEntID, pData->iEntID, 0, "", pData->pcActionType, objAutoTransactionGetResult(pReturn), true);
		}
		StructDestroySafe(parse_ASLGuildCBData, &pData);
	}
}

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuildBank_Create(U32 guildID)
{
	aslGuildBank_Create_Internal(guildID);
}

typedef struct GuildNameCheckData
{
	U32 iGuildID;
	U32 iEntID;
	U32 iTeamID;
	char *pcName;
	char *pcAllegiance;
	U32 iColor1;
	U32 iColor2;
	char *pcEmblem;
	char *pcDescription;
	char *pchClassName;
	ContainerID iVirtualShardID;
	const char *pchThemeName;
	bool bSystemControlled;
} GuildNameCheckData;

static void aslGuild_CreateGuild(U32 iEntID, U32 iTeamID, char *pcName, char *pcAllegiance, U32 iColor1, U32 iColor2, char *pcEmblem, char *pcDescription, char *pchClassName, ContainerID iVirtualShardID, const char *pchThemeName, bool bSystemControlled)
{
	char pcActionType[] = "Create";
	ASLGuildCBData *pData;
	NOCONST(Guild) *pNewGuild;
	TransactionReturnVal *pReturn;
	S32 i;

	pNewGuild = StructCreateNoConst(parse_Guild);
	pNewGuild->iCreatedOn = timeSecondsSince2000();
	pNewGuild->pcName = StructAllocString(pcName);
	pNewGuild->pcAllegiance = allocAddString(pcAllegiance);
	pNewGuild->iColor1 = iColor1;
	pNewGuild->iColor2 = iColor2;

	// system guilds use a random emblem
	if(bSystemControlled)
	{
		S32 iNumEmblem = eaSize(&g_GuildEmblems.eaEmblems);
		iNumEmblem = randInt(iNumEmblem);
		pNewGuild->pcEmblem = allocAddString(REF_STRING_FROM_HANDLE(g_GuildEmblems.eaEmblems[iNumEmblem]->hTexture));

	}
	else
	{
		pNewGuild->pcEmblem = allocAddString(pcEmblem);
	}

	pNewGuild->iVirtualShardID = iVirtualShardID;

	if(bSystemControlled)
	{
		// system type guild as there is no owning character
		pNewGuild->bIsOwnedBySystem = true;
	}
	if (pchThemeName && pchThemeName[0] && RefSystem_ReferentFromString(g_GuildThemeDictionary, pchThemeName))
	{
		SET_HANDLE_FROM_STRING(g_GuildThemeDictionary, pchThemeName, pNewGuild->hTheme);
	}
	else if (gConf.pchDefaultGuildThemeName && gConf.pchDefaultGuildThemeName[0] && RefSystem_ReferentFromString(g_GuildThemeDictionary, gConf.pchDefaultGuildThemeName))
	{
		SET_HANDLE_FROM_STRING(g_GuildThemeDictionary, gConf.pchDefaultGuildThemeName, pNewGuild->hTheme);
	}
	if (pcDescription && *pcDescription) pNewGuild->pcDescription = StructAllocString(pcDescription);
	for (i = 0; i < eaSize(&g_GuildRanks.eaRanks); i++) {
		NOCONST(GuildCustomRank) *pRank = StructCreateNoConst(parse_GuildCustomRank);
		pRank->pcName = g_GuildRanks.eaRanks[i]->pcName;
		pRank->ePerms = g_GuildRanks.eaRanks[i]->ePerms;
		pRank->pcDefaultNameMsg = g_GuildRanks.eaRanks[i]->pcDisplayNameMsg;
		eaPush(&pNewGuild->eaRanks, pRank);
	}

	pNewGuild->iFixupVersion = CURRENT_GUILD_FIXUP_VERSION;

	pData = aslGuild_MakeCBData(pcActionType);
	pData->iEntID = iEntID;
	pData->iTeamID = iTeamID;
	pData->pchClassName = allocAddString(pchClassName);
		
	pReturn = objCreateManagedReturnVal(aslGuild_Create_CB, pData);
	objRequestContainerCreate(pReturn, GLOBALTYPE_GUILD, pNewGuild, objServerType(), objServerID());
		
	StructDestroyNoConst(parse_Guild, pNewGuild);
}

static void aslGuild_CreateCheckNameCB(TransactionReturnVal *returnVal, GuildNameCheckData *guildData)
{
	char pcActionType[] = "Create";
	enumTransactionOutcome eOutcome;
	int iResult;
		
	eOutcome = RemoteCommandCheck_ChatServerReserveGuildName(returnVal, &iResult);
	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		
		switch (iResult)
		{
		case 0:
			aslGuild_SendFeedback(guildData->iEntID, guildData->iEntID, 0, guildData->pcName, pcActionType, "GuildServer_NameTaken", true);
			break;
		case -1:
			aslGuild_SendFeedback(guildData->iEntID, guildData->iEntID, 0, guildData->pcName, pcActionType, "GuildServer_CannotValidateGuildName", true);
			break;
		default:
			aslGuild_CreateGuild(guildData->iEntID, guildData->iTeamID, 
				guildData->pcName, guildData->pcAllegiance, 
				guildData->iColor1, guildData->iColor2, guildData->pcEmblem,
				guildData->pcDescription, guildData->pchClassName, guildData->iVirtualShardID,
				guildData->pchThemeName, guildData->bSystemControlled);
		}
	}
	else
	{
		aslGuild_SendFeedback(guildData->iEntID, guildData->iEntID, 0, guildData->pcName, pcActionType, "GuildServer_CannotValidateGuildName", true);
	}
	SAFE_FREE(guildData->pcName);
	SAFE_FREE(guildData->pcAllegiance);
	SAFE_FREE(guildData->pcEmblem);
	SAFE_FREE(guildData->pcDescription);
	SAFE_FREE(guildData->pchClassName);
	SAFE_FREE(guildData->pchThemeName);
	free(guildData);
}

static void aslGuild_CreateCheckNameWithChatServer(U32 iEntID, U32 iTeamID, char *pcName, char *pcAllegiance, U32 iColor1, U32 iColor2, char *pcEmblem, char *pcDescription, char *pchClassName, ContainerID iVirtualShardID, const char *pchThemeName, bool bSystemControlled)
{
	GuildNameCheckData *data = malloc(sizeof(GuildNameCheckData));
	data->iEntID = iEntID;
	data->iTeamID = iTeamID;
	data->pcName = StructAllocString(pcName);
	data->pcAllegiance = StructAllocString(pcAllegiance);
	data->iColor1 = iColor1;
	data->iColor2 = iColor2;
	data->pcEmblem = StructAllocString(pcEmblem);
	data->pcDescription = StructAllocString(pcDescription);
	data->pchClassName = StructAllocString(pchClassName);
	data->iVirtualShardID = iVirtualShardID;
	data->pchThemeName = StructAllocString(pchThemeName);
	data->bSystemControlled = bSystemControlled;

	RemoteCommand_ChatServerReserveGuildName(objCreateManagedReturnVal(aslGuild_CreateCheckNameCB, data),
		GLOBALTYPE_CHATSERVER, 0, data->pcName);
}

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_Create(U32 iEntID, U32 iTeamID, char *pcName, char *pcAllegiance, U32 iColor1, U32 iColor2, char *pcEmblem, char *pcDescription, char *pchClassName, ContainerID iVirtualShardID, const char *pchThemeName, bool bSystemControlled)
{
	char pcActionType[] = "Create";
	if (!aslGuild_CheckName(NULL, iEntID, pcName, pcActionType, iVirtualShardID) || 
		!(bSystemControlled || aslGuild_CheckEmblem(NULL, iEntID, pcEmblem, pcActionType)))
		return;

	if (ShardCommon_GetClusterName())
	{
		aslGuild_CreateCheckNameWithChatServer(iEntID, iTeamID, pcName, pcAllegiance, iColor1, iColor2, pcEmblem, pcDescription, 
			pchClassName, iVirtualShardID, pchThemeName, bSystemControlled);
	}
	else
	{
		aslGuild_CreateGuild(iEntID, iTeamID, pcName, pcAllegiance, iColor1, iColor2, pcEmblem, pcDescription, 
			pchClassName, iVirtualShardID, pchThemeName, bSystemControlled);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Pguildstatsinfo, .Earanks, .Ivirtualshardid, .Icontainerid, .Pcname, .Iversion, .Eamembers")
ATR_LOCKS(pEnt, ".Psaved.Savedname, .Pplayer.Publicaccountname, .Pplayer.Ivirtualshardid, .Pplayer.Pguild, .Pplayer.Accountid");
enumTransactionOutcome aslGuild_tr_AddInitialMember(ATR_ARGS, NOCONST(Guild) *pGuildContainer, NOCONST(Entity) *pEnt, const char *pchClassName)
{
	GUILD_TRANSACTION_INIT;
	U32 iMaxRank = eaSize(&pGuildContainer->eaRanks)-1;
	
	// Make sure that the player isn't already in a guild
	if (NONNULL(pEnt->pPlayer->pGuild) && pEnt->pPlayer->pGuild->iGuildID) {
		if (pEnt->pPlayer->pGuild->eState == GuildState_Member) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, pEnt->myContainerID, pEnt->myContainerID, 0, "GuildServer_SelfAlreadyInGuild", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
		if (pEnt->pPlayer->pGuild->eState == GuildState_Invitee) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, pEnt->myContainerID, pEnt->myContainerID, 0, "GuildServer_SelfAlreadyInvited", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
	}
	// Make sure that the guild isn't already full
	if (eaSize(&pGuildContainer->eaMembers) >= GUILD_MAX_SIZE) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, pEnt->myContainerID, pEnt->myContainerID, 0, "GuildServer_GuildFull", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	
	if ((ISNULL(pEnt->pPlayer) ? 0: pEnt->pPlayer->iVirtualShardID) != pGuildContainer->iVirtualShardID)
	{
		aslGuild_AddFailureLogMessage(ATR_GUILD_RETURN, pEnt->debugName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Error: Wrong virtual shard.");
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, pEnt->myContainerID, pEnt->myContainerID, 0, "GuildServer_WrongVirtualShard", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	aslGuild_trh_AddMember(GUILD_TRH_PASS_ARGS, pGuildContainer, pEnt, iMaxRank, pchClassName);
	aslGuild_trh_AddActivity(ATR_GUILD_RETURN, pGuildContainer, pEnt->myContainerID, kActivityType_GuildCreate, NULL, 0);

	timingHistoryPush(gCreationHistory);
	
	GUILD_TRANSACTION_RETURN_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Earanks[AO]")
ATR_LOCKS(pGuildBank, ".Pinventoryv2, pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
enumTransactionOutcome aslGuild_tr_AddInventory(ATR_ARGS, NOCONST(Guild) *pGuildContainer, NOCONST(Entity) *pGuildBank)
{
	if (inv_guildbank_trh_InitializeInventory(ATR_PASS_ARGS, pGuildContainer, pGuildBank, (DefaultInventory*)RefSystem_ReferentFromString(g_hDefaultInventoryDict, "GuildDefault"))) {
		return TRANSACTION_OUTCOME_SUCCESS;
	} else {
		return TRANSACTION_OUTCOME_FAILURE;
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuild, ".iContainerID, .pInventoryDeprecated, .iFixupVersion, .Earanks[AO]")
ATR_LOCKS(pGuildBank, ".pInventoryv2, .Pinventoryv1_Deprecated, .pchar.ilevelexp, pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
enumTransactionOutcome aslGuild_tr_MoveGuildInventoryToGuildBankContainer(ATR_ARGS, NOCONST(Guild) *pGuild, NOCONST(Entity) *pGuildBank)
{
	if(NONNULL(pGuild))
	{
		if(NONNULL(pGuild->pInventoryDeprecated))
		{
			if(NONNULL(pGuildBank))
			{
				pGuildBank->pInventoryV1_Deprecated = pGuild->pInventoryDeprecated;
				pGuild->pInventoryDeprecated = NULL;
				pGuildBank->myContainerID = pGuild->iContainerID;
				pGuildBank->myEntityType = GLOBALTYPE_ENTITYGUILDBANK;

				//go straight into v1->v2 fixup
				inv_guildbank_trh_InitializeInventory(ATR_PASS_ARGS, pGuild, pGuildBank, (DefaultInventory*)RefSystem_ReferentFromString(g_hDefaultInventoryDict, "GuildDefault"));
				inv_trh_ent_MigrateInventoryV1ToV2(ATR_PASS_ARGS, pGuildBank);
				pGuild->iFixupVersion = 1;

				return TRANSACTION_OUTCOME_SUCCESS;
			}
			else
			{
				return TRANSACTION_OUTCOME_FAILURE;
			}
		}
		else
		{
			return TRANSACTION_OUTCOME_SUCCESS;
		}
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANSACTION
	ATR_LOCKS(pGuild, ".iFixupVersion, .Earanks[AO]")
	ATR_LOCKS(pGuildBank, ".pInventoryv2, .Pinventoryv1_Deprecated, .pchar.ilevelexp, pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
enumTransactionOutcome aslGuild_tr_FixupGuildV0ToV1(ATR_ARGS, NOCONST(Guild) *pGuild, NOCONST(Entity) *pGuildBank)
{
	if(NONNULL(pGuildBank))
	{
		inv_guildbank_trh_InitializeInventory(ATR_PASS_ARGS, pGuild, pGuildBank, (DefaultInventory*)RefSystem_ReferentFromString(g_hDefaultInventoryDict, "GuildDefault"));
		inv_trh_ent_MigrateInventoryV1ToV2(ATR_PASS_ARGS, pGuildBank);
        StructDestroyNoConstSafe(parse_InventoryV1, &pGuildBank->pInventoryV1_Deprecated);
		pGuild->iFixupVersion = 1;
		return TRANSACTION_OUTCOME_SUCCESS;
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

// there is an issue with guild bank
AUTO_TRANSACTION
ATR_LOCKS(pGuild, "eaRanks[AO]")
ATR_LOCKS(pGuildBank, "pInventoryV2.ppInventoryBags");
enumTransactionOutcome aslGuild_tr_FixGuildBankInfo(ATR_ARGS, NOCONST(Guild) *pGuild, NOCONST(Entity) *pGuildBank)
{
	if(NONNULL(pGuild))
	{
		if(NONNULL(pGuildBank))
		{
			if(NONNULL(pGuildBank->pInventoryV2))
			{
				S32 iMaxRank = eaSize(&pGuild->eaRanks);	// total number of ranks
				S32 i;
			
				for(i = 0; i < eaSize(&pGuildBank->pInventoryV2->ppInventoryBags); ++i)
				{
					if(eaSize(&pGuildBank->pInventoryV2->ppInventoryBags[i]->pGuildBankInfo->eaPermissions) < iMaxRank)
					{
						// fix the guild ranks in, need to add more
						S32 j;
						S32 iAddAmount = iMaxRank - eaSize(&pGuildBank->pInventoryV2->ppInventoryBags[i]->pGuildBankInfo->eaPermissions);
					
						for(j = 0; j < iAddAmount; ++j)
						{
							S32 iInsertIdx = max(0,eaSize(&pGuildBank->pInventoryV2->ppInventoryBags[i]->pGuildBankInfo->eaPermissions)-1);
							NOCONST(GuildBankTabPermission) *pGuildTab = StructCreateNoConst(parse_GuildBankTabPermission);					
							eaInsert(&pGuildBank->pInventoryV2->ppInventoryBags[i]->pGuildBankInfo->eaPermissions, pGuildTab, iInsertIdx);
						}
					}
					else if(eaSize(&pGuildBank->pInventoryV2->ppInventoryBags[i]->pGuildBankInfo->eaPermissions) > iMaxRank && iMaxRank > 1)
					{
						// fix the guild ranks in, remove some
						S32 iDeleteAmount = eaSize(&pGuildBank->pInventoryV2->ppInventoryBags[i]->pGuildBankInfo->eaPermissions) - iMaxRank;
						S32 j;
					
						for(j= 0; j < iDeleteAmount; ++j)
						{
							StructDestroyNoConst(parse_GuildBankTabPermission, eaRemove(&pGuildBank->pInventoryV2->ppInventoryBags[i]->pGuildBankInfo->eaPermissions, 0));
						}
					}
				}
			}
			return TRANSACTION_OUTCOME_SUCCESS;
	
		}
	}	
	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANSACTION
ATR_LOCKS(pGuild, ".Eaevents, .Ieventupdated, .Inextguildeventindex, .Ifixupversion, .Eamembers, .Earanks[AO]");
enumTransactionOutcome aslGuild_tr_FixupGuildV1ToV2(ATR_ARGS, NOCONST(Guild) *pGuild)
{
	int i, j;
	NOCONST(GuildEvent) **ppEvents = NULL;
	NOCONST(GuildEvent) *pGuildEvent = NULL;
	NOCONST(GuildMember) *pMember = NULL;
	NOCONST(GuildEventReply) **ppReplies = NULL;

	if (ISNULL(pGuild))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	eaCopy(&ppEvents, &pGuild->eaEvents);
	eaDestroy(&pGuild->eaEvents);

	if (!eaSize(&pGuild->eaEvents))
	{
		eaIndexedEnableNoConst(&pGuild->eaEvents, parse_GuildEvent);
	}

	pGuild->iNextGuildEventIndex = 0;
	pGuild->iEventUpdated = timeSecondsSince2000();

	for (i = eaSize(&ppEvents) - 1; i >= 0; i--)
	{
		// If we cleared out this event already, don't process it
		if (!ppEvents[i])
		{
			continue;
		}

		pGuildEvent = ppEvents[i];
		pGuildEvent->uiID = pGuild->iNextGuildEventIndex;
		pGuild->iNextGuildEventIndex++;

		pGuildEvent->iDuration = pGuildEvent->iEndTimeTime - pGuildEvent->iStartTimeTime;
		pGuildEvent->eRecurType = pGuildEvent->iDaysRecurring / DAYS(1);
		pGuildEvent->iRecurrenceCount = -1;
		pGuildEvent->iEventUpdated = pGuild->iEventUpdated;

		pMember = eaIndexedGetUsingInt(&pGuild->eaMembers, pGuildEvent->iEventOwnerID);
		if (pMember)
		{
			pGuildEvent->iMinGuildEditRank = pMember->iRank;
		}
		else
		{
			pGuildEvent->iMinGuildEditRank = eaSize(&pGuild->eaRanks) - 1;
		}

		// Process the rest of the event list in case we find other recurrences of this event.
		// Add any replies to these recurrences to the original event, update the start time
		// of the original event if necessary, and remove the recurrence
		for (j = i - 1; j >= 0; j--)
		{
			// If we cleared out this event already, don't process it
			if (!ppEvents[j])
			{
				continue;
			}

			if (!stricmp(ppEvents[j]->pcTitle, pGuildEvent->pcTitle))
			{
				pGuildEvent->iStartTimeTime = MIN(ppEvents[j]->iStartTimeTime, pGuildEvent->iStartTimeTime);
				eaCopy(&ppReplies, &ppEvents[j]->eaReplies);
				eaPushArray(&pGuildEvent->eaReplies, ppReplies, eaSize(&ppReplies));
				StructDestroyNoConst(parse_GuildEvent, ppEvents[j]);
				ppEvents[j] = NULL;
			}
		}

		eaIndexedAdd(&pGuild->eaEvents, pGuildEvent);
	}

	eaClear(&ppEvents);
	pGuild->iFixupVersion = 2;

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pGuild, ".Eaevents, .Ifixupversion");
enumTransactionOutcome aslGuild_tr_FixupGuildV2ToV3(ATR_ARGS, NOCONST(Guild) *pGuild)
{
	NOCONST(GuildEvent) *pGuildEvent = NULL;
	NOCONST(GuildEventReply) **ppReplies = NULL;
	NOCONST(GuildEventReply) *pReply = NULL;
	int i, j;

	if (ISNULL(pGuild))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	for (i = eaSize(&pGuild->eaEvents) - 1; i >= 0; i--)
	{
		pGuildEvent = pGuild->eaEvents[i];

		for (j = eaSize(&pGuildEvent->eaReplies) - 1; j >= 0; j--)
		{
			pReply = pGuildEvent->eaReplies[j];
			eaRemove(&pGuildEvent->eaReplies, j);
			pReply->uiKey = guildevent_GetReplyKey(pReply->iMemberID, pReply->iStartTime);
			eaPush(&ppReplies, pReply);
		}

		eaClear(&pGuildEvent->eaReplies);
		eaIndexedEnableNoConst(&pGuildEvent->eaReplies, parse_GuildEventReply);

		for (j = eaSize(&ppReplies) - 1; j >= 0; j--)
		{
			pReply = ppReplies[j];
			eaIndexedAdd(&pGuildEvent->eaReplies, pReply);
		}

		eaClear(&ppReplies);
		ppReplies = NULL;
	}

	pGuild->iFixupVersion = 3;

	return TRANSACTION_OUTCOME_SUCCESS;
}

//ATR_LOCKS(pGuild, ".iFixupVersion, .Eaevents, .Ieventupdated, .Inextguildeventindex, .Ifixupversion, .Eamembers, .Earanks[AO]")

AUTO_TRANSACTION
ATR_LOCKS(pGuild, ".Ifixupversion, .Earanks[AO], .Inextguildeventindex, .Ieventupdated, .Eaevents, .Eamembers")
ATR_LOCKS(pGuildBank, ".Pchar.Ilevelexp, .Pinventoryv2, .Pinventoryv1_Deprecated, pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
enumTransactionOutcome aslGuild_tr_FixupGuild(ATR_ARGS, NOCONST(Guild) *pGuild, NOCONST(Entity) *pGuildBank)
{
	if (NONNULL(pGuild) && NONNULL(pGuildBank))
	{
		if (pGuild->iFixupVersion < 1)
		{
			aslGuild_tr_FixupGuildV0ToV1(ATR_PASS_ARGS, pGuild, pGuildBank);
		}
		if (pGuild->iFixupVersion < 2)
		{
			aslGuild_tr_FixupGuildV1ToV2(ATR_PASS_ARGS, pGuild);
		}
		if (pGuild->iFixupVersion < 3)
		{
			aslGuild_tr_FixupGuildV2ToV3(ATR_PASS_ARGS, pGuild);
		}
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Destroy Guild
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE;
void aslGuild_Destroy(U32 iGuildID, U32 iEntID)
{
	TransactionReturnVal *pReturn = NULL;
	char pcActionType[] = "Destroy";
	Guild *pGuild = aslGuild_GetGuild(iGuildID);
	int i;
	
	if (aslGuild_CheckExistsNoValidate(pGuild, iEntID, pcActionType)) {
		// Clear all the members and invites
		for (i = eaSize(&pGuild->eaMembers)-1; i >= 0; i--) {
			AutoTrans_aslGuild_tr_ClearPlayerGuild(NULL, GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, pGuild->eaMembers[i]->iEntID);
		}
		for (i = eaSize(&pGuild->eaInvites)-1; i >= 0; i--) {
			AutoTrans_aslGuild_tr_ClearPlayerGuild(NULL, GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, pGuild->eaInvites[i]->iEntID);
		}
		
		pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
		objRequestContainerDestroy(pReturn, GLOBALTYPE_GUILD, pGuild->iContainerID, objServerType(), objServerID());
		pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
		objRequestContainerDestroy(pReturn, GLOBALTYPE_ENTITYGUILDBANK, pGuild->iContainerID, objServerType(), objServerID());
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// Update Guild Ranks
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE;
void aslGuild_UpdateRanks(U32 iGuildID, U32 iEntID)
{
	TransactionReturnVal *pReturn = NULL;
	char pcActionType[] = "UpdateRanks";
	Guild *pGuild = aslGuild_GetGuild(iGuildID);

	if (aslGuild_CheckExistsNoValidate(pGuild, iEntID, pcActionType))
	{
		pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
		AutoTrans_aslGuild_tr_UpdateRanks(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Earanks");
enumTransactionOutcome aslGuild_tr_UpdateRanks(ATR_ARGS, NOCONST(Guild) *pGuildContainer)
{
	GUILD_TRANSACTION_INIT;

	while (eaSize(&pGuildContainer->eaRanks) < eaSize(&g_GuildRanks.eaRanks))
	{
		NOCONST(GuildCustomRank) *pRank = StructCreateNoConst(parse_GuildCustomRank);
		int i = eaSize(&pGuildContainer->eaRanks);
		pRank->pcName = g_GuildRanks.eaRanks[i]->pcName;
		pRank->ePerms = g_GuildRanks.eaRanks[i]->ePerms;
		pRank->pcDefaultNameMsg = g_GuildRanks.eaRanks[i]->pcDisplayNameMsg;
		eaPush(&pGuildContainer->eaRanks, pRank);
	}

	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Join Guild without Invite
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE;
void aslGuild_JoinWithoutInvite(U32 iGuildID, U32 iEntID, const char *pchClassName)
{
	char pcActionType[] = "Join";
	TransactionReturnVal *pReturn = NULL;
	Guild *pGuild = aslGuild_GetGuild(iGuildID);
	
	if (aslGuild_CheckExistsNoValidate(pGuild, iEntID, pcActionType) &&
		aslGuild_CheckIsNotMember(pGuild, iEntID, pcActionType) &&
		aslGuild_CheckIsNotFull(pGuild, iEntID, pcActionType)) {
		
		pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
		AutoTrans_aslGuild_tr_JoinWithoutInvite(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, GLOBALTYPE_ENTITYPLAYER, iEntID, pchClassName);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Earanks, .Ivirtualshardid, .Icontainerid, .Pcname, .Iversion, .Pguildstatsinfo, .Eamembers")
ATR_LOCKS(pEnt, ".Psaved.Savedname, .Pplayer.Publicaccountname, .Pplayer.Ivirtualshardid, .Pplayer.Pguild, .Pplayer.Accountid");
enumTransactionOutcome aslGuild_tr_JoinWithoutInvite(ATR_ARGS, NOCONST(Guild) *pGuildContainer, NOCONST(Entity) *pEnt, const char *pchClassName)
{
	GUILD_TRANSACTION_INIT;
	
	// Make sure that the player isn't already in a guild
	if (NONNULL(pEnt->pPlayer->pGuild) && pEnt->pPlayer->pGuild->iGuildID) {
		if (pEnt->pPlayer->pGuild->eState == GuildState_Member) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, pEnt->myContainerID, pEnt->myContainerID, 0, "GuildServer_AlreadyInGuild", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
		if (pEnt->pPlayer->pGuild->eState == GuildState_Invitee) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, pEnt->myContainerID, pEnt->myContainerID, 0, "GuildServer_AlreadyInvited", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
	}
	// Make sure that the guild isn't already full
	if (eaSize(&pGuildContainer->eaMembers) >= GUILD_MAX_SIZE) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, pEnt->myContainerID, pEnt->myContainerID, 0, "GuildServer_GuildFull", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	
	if ((ISNULL(pEnt->pPlayer) ? 0: pEnt->pPlayer->iVirtualShardID) != pGuildContainer->iVirtualShardID)
	{
		aslGuild_AddFailureLogMessage(ATR_GUILD_RETURN, pEnt->debugName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Error: Wrong virtual shard.");
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, pEnt->myContainerID, pEnt->myContainerID, 0, "GuildServer_WrongVirtualShard", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	aslGuild_trh_AddMember(GUILD_TRH_PASS_ARGS, pGuildContainer, pEnt, 0, pchClassName);
	
	timingHistoryPush(gActionHistory);
	
	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Invite to Guild
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_Invite(U32 iGuildID, U32 iEntID, U32 iSubjectID)
{
	char pcActionType[] = "Invite";
	TransactionReturnVal *pReturn = NULL;
	Guild *pGuild = aslGuild_GetGuild(iGuildID);
	
	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsMember(pGuild, iEntID, pcActionType) &&
		aslGuild_CheckPermission(pGuild, iEntID, GuildPermission_Invite, false, pcActionType) &&
		aslGuild_CheckSubjectIsNotMember(pGuild, iEntID, iSubjectID, pcActionType) &&
		aslGuild_CheckSubjectIsNotInvite(pGuild, iEntID, iSubjectID, pcActionType) &&
		aslGuild_CheckIsNotFull(pGuild, iEntID, pcActionType)) {
		
		pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
		AutoTrans_aslGuild_tr_Invite(pReturn, GetAppGlobalType(), 
			GLOBALTYPE_GUILD, iGuildID, 
			GLOBALTYPE_ENTITYPLAYER, iEntID, 
			GLOBALTYPE_ENTITYPLAYER, iSubjectID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Eamembers, .Pcname, .Earanks, .Pcallegiance, .Iversion, .Icontainerid, .Eainvites")
ATR_LOCKS(pEnt, ".Psaved.Savedname, .Pplayer.Publicaccountname")
ATR_LOCKS(pSubject, ".Pplayer.Publicaccountname, .Hallegiance, .Pplayer.Pguild, .Psaved.Savedname, .Pplayer.Accountid");
enumTransactionOutcome aslGuild_tr_Invite(ATR_ARGS, NOCONST(Guild) *pGuildContainer, NOCONST(Entity) *pEnt, NOCONST(Entity) *pSubject)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, pEnt->myContainerID);
	
	// Make sure that the inviter is actually in the guild
	if (ISNULL(pMember)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, pEnt->myContainerID, pEnt->myContainerID, pSubject->myContainerID, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	// Make sure that the inviter has permission to invite
	if (!aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_Invite)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, pEnt->myContainerID, pEnt->myContainerID, pSubject->myContainerID, "GuildServer_NoInvitePermission", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	// Make sure that the player isn't already in a guild
	if (NONNULL(pSubject->pPlayer->pGuild) && pSubject->pPlayer->pGuild->iGuildID) {
		if (pSubject->pPlayer->pGuild->eState == GuildState_Member) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, pEnt->myContainerID, pEnt->myContainerID, pSubject->myContainerID, "GuildServer_AlreadyInGuild", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
		if (pSubject->pPlayer->pGuild->eState == GuildState_Invitee) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, pEnt->myContainerID, pEnt->myContainerID, pSubject->myContainerID, "GuildServer_AlreadyInvited", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
	}
	// Make sure that the guild isn't already full
	if (eaSize(&pGuildContainer->eaMembers) >= GUILD_MAX_SIZE) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, pEnt->myContainerID, pEnt->myContainerID, pSubject->myContainerID, "GuildServer_GuildFull", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	// Make sure the allegiance of the invitee matches that of the guild
	if (!aslGuild_trh_CheckInviteeAllegiance(pGuildContainer, pSubject))
	{
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, pEnt->myContainerID, pEnt->myContainerID, pSubject->myContainerID, "GuildServer_PlayerNotAllegiance", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	// TODO: Make sure that the invitee can socialize
	// Note: Used to call "entity_CanSocialize" but that doesn't work since that entity's game account data 
	//       isn't available.  We need to get the subject entity's game account data passed into this 
	//       transaction, or otherwise test this somewhere when the information is available.

	aslGuild_trh_AddInvite(GUILD_TRH_PASS_ARGS, pGuildContainer, pEnt, pSubject);

	timingHistoryPush(gActionHistory);
	
	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Accept Invite to Guild
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_AcceptInvite(U32 iGuildID, U32 iEntID, const char *pchClassName)
{
	TransactionReturnVal *pReturn = NULL;
	char pcActionType[] = "AcceptInvite";
	Guild *pGuild = aslGuild_GetGuild(iGuildID);
	
	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsInvite(pGuild, iEntID, pcActionType) &&
		aslGuild_CheckIsNotFull(pGuild, iEntID, pcActionType)) {
		
		pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
		AutoTrans_aslGuild_tr_AcceptInvite(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, GLOBALTYPE_ENTITYPLAYER, iEntID, pchClassName);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Earanks, .Ivirtualshardid, .Icontainerid, .Pcname, .Eainvites, .Iversion, .Pguildstatsinfo, .Eamembers")
ATR_LOCKS(pEnt, ".Psaved.Savedname, .Pplayer.Publicaccountname, .Pplayer.Ivirtualshardid, .Pplayer.Pguild, .Pplayer.Accountid");
enumTransactionOutcome aslGuild_tr_AcceptInvite(ATR_ARGS, NOCONST(Guild) *pGuildContainer, NOCONST(Entity) *pEnt, const char *pchClassName)
{
	GUILD_TRANSACTION_INIT;
	// Make sure there is room in the guild
	if (eaSize(&pGuildContainer->eaMembers) >= GUILD_MAX_SIZE) {
		aslGuild_trh_RemoveInvite(GUILD_TRH_PASS_ARGS, pGuildContainer, pEnt);
		aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, pEnt->myContainerID, pEnt->myContainerID, 0, "GuildServer_GuildFull", true);
		aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pEnt->debugName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Error: Guild full.");
		GUILD_TRANSACTION_RETURN_SUCCESS;
	}
	// Make sure the player was actually invited, and remove them from the invite list if they were
	if (!aslGuild_trh_RemoveInvite(GUILD_TRH_PASS_ARGS, pGuildContainer, pEnt)) {
		if (pEnt->pPlayer->pGuild->eState == GuildState_Invitee) {
			aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pEnt->debugName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Error: Not invited to guild.");
			aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, pEnt->myContainerID, pEnt->myContainerID, 0, "GuildServer_NotInvited", true);
			aslGuild_trh_ClearPlayerGuild(GUILD_TRH_PASS_ARGS, pEnt);
			GUILD_TRANSACTION_RETURN_SUCCESS;
		} else {
			aslGuild_AddFailureLogMessage(ATR_GUILD_RETURN, pEnt->debugName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Error: Not invited to guild.");
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, pEnt->myContainerID, pEnt->myContainerID, 0, "GuildServer_NotInvited", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
	}
	
	if ((ISNULL(pEnt->pPlayer) ? 0: pEnt->pPlayer->iVirtualShardID) != pGuildContainer->iVirtualShardID)
	{
		aslGuild_AddFailureLogMessage(ATR_GUILD_RETURN, pEnt->debugName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Error: Wrong virtual shard.");
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, pEnt->myContainerID, pEnt->myContainerID, 0, "GuildServer_WrongVirtualShard", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	
	aslGuild_trh_AddMember(GUILD_TRH_PASS_ARGS, pGuildContainer, pEnt, 0, pchClassName);
	aslGuild_trh_AddActivity(ATR_GUILD_RETURN, pGuildContainer, pEnt->myContainerID, kActivityType_GuildJoin, NULL, 0);

	timingHistoryPush(gActionHistory);
	
	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Decline Invite to Guild
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_DeclineInvite(U32 iGuildID, U32 iEntID)
{
	TransactionReturnVal *pReturn = NULL;
	char pcActionType[] = "DeclineInvite";
	Guild *pGuild = aslGuild_GetGuild(iGuildID);
	
	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsInvite(pGuild, iEntID, pcActionType)) {
		
		pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
		AutoTrans_aslGuild_tr_DeclineInvite(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, GLOBALTYPE_ENTITYPLAYER, iEntID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Icontainerid, .Pcname, .Eainvites")
ATR_LOCKS(pEnt, ".Pplayer.Pguild.Estate, .Pplayer.Pguild.Pcinvitername, .Pplayer.Pguild.Iguildid");
enumTransactionOutcome aslGuild_tr_DeclineInvite(ATR_ARGS, NOCONST(Guild) *pGuildContainer, NOCONST(Entity) *pEnt)
{
	GUILD_TRANSACTION_INIT;
	if (!aslGuild_trh_RemoveInvite(GUILD_TRH_PASS_ARGS, pGuildContainer, pEnt)) {
		if (pEnt->pPlayer->pGuild->eState == GuildState_Invitee) {
			aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pEnt->debugName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Error: Not invited to guild.");
			aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, pEnt->myContainerID, pEnt->myContainerID, 0, "GuildServer_NotInvited", true);
			aslGuild_trh_ClearPlayerGuild(GUILD_TRH_PASS_ARGS, pEnt);
			GUILD_TRANSACTION_RETURN_SUCCESS;
		} else {
			aslGuild_AddFailureLogMessage(ATR_GUILD_RETURN, pEnt->debugName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Error: Not invited to guild.");
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, pEnt->myContainerID, pEnt->myContainerID, 0, "GuildServer_NotInvited", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
	}
	
	aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pEnt->debugName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Guild invite declined.");

	timingHistoryPush(gActionHistory);

	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Leave Guild
///////////////////////////////////////////////////////////////////////////////////////////

static void GuildKickExistenceCheck_CB(TransactionReturnVal *returnVal, ASLGuildCBData *cbData)
{
	enumTransactionOutcome eOutcome;
	int iResult;
	TransactionReturnVal *pReturn;

	eOutcome = RemoteCommandCheck_DBCheckSingleContainerExists(returnVal, &iResult);

	switch (eOutcome)
	{
	case TRANSACTION_OUTCOME_SUCCESS:
		pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, cbData);
		if (iResult == 0)
		{
			AutoTrans_aslGuild_tr_Kick_NonExistent(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, cbData->iGuildID, cbData->iEntID, cbData->iSubjectID);
		}
		else
		{
			AutoTrans_aslGuild_tr_Kick(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, cbData->iGuildID, cbData->iEntID, GLOBALTYPE_ENTITYPLAYER, cbData->iSubjectID);
		}
		break;
	case TRANSACTION_OUTCOME_FAILURE:
		StructDestroy(parse_ASLGuildCBData, cbData);
	}
}

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_Leave(U32 iGuildID, U32 iEntID, U32 iSubjectID)
{
	static char pcKickActionType[] = "Kick";
	static char pcLeaveActionType[] = "Leave";
	TransactionReturnVal *pReturn = NULL;
	char *pcActionType = iSubjectID == iEntID ? pcLeaveActionType : pcKickActionType;
	Guild *pGuild = aslGuild_GetGuild(iGuildID);
	
	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsMember(pGuild, iEntID, pcActionType) &&
		(iEntID == iSubjectID || aslGuild_CheckPermission(pGuild, iEntID, GuildPermission_Remove, false, pcActionType))) {
		
		ASLGuildCBData *cbData = aslGuild_MakeCBData(pcActionType);
		if (iSubjectID == iEntID) {
			pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, cbData);
			AutoTrans_aslGuild_tr_Leave(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, GLOBALTYPE_ENTITYPLAYER, iEntID);
		} else {
			cbData->iEntID = iEntID;
			cbData->iSubjectID = iSubjectID;
			cbData->iGuildID = iGuildID;
			RemoteCommand_DBCheckSingleContainerExists(objCreateManagedReturnVal(GuildKickExistenceCheck_CB, cbData), GLOBALTYPE_OBJECTDB, 0, GLOBALTYPE_ENTITYPLAYER, iSubjectID);
		}
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Eamembers, .Pguildstatsinfo, .Icontainerid, .Pcname")
ATR_LOCKS(pEnt, ".Pplayer.Pguild.Iguildid");
enumTransactionOutcome aslGuild_tr_Leave(ATR_ARGS, NOCONST(Guild) *pGuildContainer, NOCONST(Entity) *pEnt)
{
	GUILD_TRANSACTION_INIT;
	
	// Make sure the player is in the guild
	if (!aslGuild_trh_RemoveMember(GUILD_TRH_PASS_ARGS, pGuildContainer, pEnt->myContainerID, pEnt, pEnt->myContainerID)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, pEnt->myContainerID, pEnt->myContainerID, 0, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	
	timingHistoryPush(gActionHistory);
	
	GUILD_TRANSACTION_RETURN_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Eamembers, .Pcname, .Earanks, .Pguildstatsinfo, .Icontainerid")
ATR_LOCKS(pSubject, ".Pplayer.Pguild.Iguildid");
enumTransactionOutcome aslGuild_tr_Kick(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID, NOCONST(Entity) *pSubject)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	NOCONST(GuildMember) *pSubjectMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, pSubject->myContainerID);
	
	// Make sure both players are in the guild
	if (!pMember) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, pSubject->myContainerID, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (!pSubjectMember) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, pSubject->myContainerID, "GuildServer_NotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	// Make sure the initiator is allowed to remove this player
	if (!aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_Remove) || pSubjectMember->iRank >= pMember->iRank) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, pSubject->myContainerID, "GuildServer_NoKickPermission", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (!aslGuild_trh_RemoveMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, pSubject, pSubject->myContainerID)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, pSubject->myContainerID, "GuildServer_NotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	GUILD_TRANSACTION_RETURN_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Pcname, .Earanks, .Eamembers, .Pguildstatsinfo, .Icontainerid");
enumTransactionOutcome aslGuild_tr_Kick_NonExistent(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID, U32 iSubjectID)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	NOCONST(GuildMember) *pSubjectMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iSubjectID);
	
	// Make sure both players are in the guild
	if (!pMember) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iSubjectID, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (!pSubjectMember) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iSubjectID, "GuildServer_NotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	// Make sure the initiator is allowed to remove this player
	if (!aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_Remove) || pSubjectMember->iRank >= pMember->iRank) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iSubjectID, "GuildServer_NoKickPermission", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (!aslGuild_trh_RemoveMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, NULL, iSubjectID)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iSubjectID, "GuildServer_NotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Promote Guildmate
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_Promote(U32 iGuildID, U32 iEntID, U32 iSubjectID)
{
	TransactionReturnVal *pReturn = NULL;
	char pcActionType[] = "Promote";
	Guild *pGuild = aslGuild_GetGuild(iGuildID);
	
	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsMember(pGuild, iEntID, pcActionType) &&
		aslGuild_CheckSubjectIsMember(pGuild, iEntID, iSubjectID, pcActionType) &&
		aslGuild_CheckCanPromote(pGuild, iEntID, iSubjectID, pcActionType)) {
		
		pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
		AutoTrans_aslGuild_tr_Promote(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, iEntID, iSubjectID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Pcname, .Iocversion, .Earanks, .Icontainerid, eaMembers[], eaMembers[]");
enumTransactionOutcome aslGuild_tr_Promote(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID, U32 iSubjectID)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	NOCONST(GuildMember) *pSubjectMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iSubjectID);
	
	// Make sure both players are in the guild
	if (ISNULL(pMember)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iSubjectID, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (ISNULL(pSubjectMember)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iSubjectID, "GuildServer_NotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	// Make sure the initiator is allowed to promote this player
	if (pMember->iRank > pSubjectMember->iRank+1) {
		if (!(aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_PromoteBelowRank) ||
			aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_PromoteToRank))) {
			
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iSubjectID, "GuildServer_NoPromotePermission", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
	} else if (pMember->iRank == pSubjectMember->iRank+1) {
		if (!aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_PromoteToRank)) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iSubjectID, "GuildServer_NoPromotePermission", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
	} else {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iSubjectID, "GuildServer_HigherRank", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	// Check if the member is already the highest rank
	if (pSubjectMember->iRank >= eaSize(&pGuildContainer->eaRanks)-1) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iSubjectID, "GuildServer_AlreadyHighestRank", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	aslGuild_trh_Promote(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, iSubjectID);
	pGuildContainer->iOCVersion++;

	timingHistoryPush(gActionHistory);
	
	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Demote Guildmate
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_Demote(U32 iGuildID, U32 iEntID, U32 iSubjectID)
{
	TransactionReturnVal *pReturn = NULL;
	char pcActionType[] = "Demote";
	Guild *pGuild = aslGuild_GetGuild(iGuildID);
	
	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsMember(pGuild, iEntID, pcActionType) &&
		aslGuild_CheckSubjectIsMember(pGuild, iEntID, iSubjectID, pcActionType) &&
		aslGuild_CheckCanDemote(pGuild, iEntID, iSubjectID, pcActionType)) {
		
		pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
		AutoTrans_aslGuild_tr_Demote(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, iEntID, iSubjectID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Iocversion, .Pcname, .Earanks, .Icontainerid, eaMembers[], eaMembers[]");
enumTransactionOutcome aslGuild_tr_Demote(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID, U32 iSubjectID)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	NOCONST(GuildMember) *pSubjectMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iSubjectID);
	
	// Make sure both players are in the guild
	if (ISNULL(pMember)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iSubjectID, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (ISNULL(pSubjectMember)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iSubjectID, "GuildServer_NotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	// Make sure the initiator is allowed to demote this player
	if (pMember->iRank > pSubjectMember->iRank) {
		if (!(aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_DemoteBelowRank) ||
			aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_DemoteAtRank))) {
			
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iSubjectID, "GuildServer_NoDemotePermission", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
	} else if (pMember->iRank == pSubjectMember->iRank) {
		if (!aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_DemoteAtRank)) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iSubjectID, "GuildServer_NoDemotePermission", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
	} else {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iSubjectID, "GuildServer_HigherRank", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (pSubjectMember->iRank <= 0) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iSubjectID, "GuildServer_AlreadyLowestRank", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	aslGuild_trh_Demote(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, iSubjectID);
	pGuildContainer->iOCVersion++;

	timingHistoryPush(gActionHistory);
	
	GUILD_TRANSACTION_RETURN_SUCCESS("GuildServer_Demoted", "Demoted ENTITYPLAYER[%u]", iSubjectID);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Change Guild Name
///////////////////////////////////////////////////////////////////////////////////////////

static void aslGuild_Rename_CB(TransactionReturnVal *pReturn, ASLGuildCBData *pData) {
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS) {
		Guild *pGuild = aslGuild_GetGuild(pData->iGuildID);
		
		if (pGuild) {
			RemoteCommand_ChatServerUpdateGuild(NULL, GLOBALTYPE_CHATSERVER, 0, pGuild->iContainerID, pGuild->pcName);
		}
	}
	aslGuild_RemoteCommand_CB(pReturn, pData);
}

static void aslGuild_RenameCheckNameCB(TransactionReturnVal *returnVal, GuildNameCheckData *guildData)
{
	char pcActionType[] = "Rename";
	enumTransactionOutcome eOutcome;
	int iResult;
		
	eOutcome = RemoteCommandCheck_ChatServerReserveGuildName(returnVal, &iResult);
	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		switch (iResult)
		{
		case 0:
			aslGuild_SendFeedback(guildData->iEntID, guildData->iEntID, 0, guildData->pcName, pcActionType, "GuildServer_NameTaken", true);
			break;
		case -1:
			aslGuild_SendFeedback(guildData->iEntID, guildData->iEntID, 0, guildData->pcName, pcActionType, "GuildServer_CannotValidateGuildName", true);
			break;
		default:
			{
				TransactionReturnVal *pReturn = objCreateManagedReturnVal(aslGuild_Rename_CB, aslGuild_MakeCBData(pcActionType));
				AutoTrans_aslGuild_tr_Rename(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, guildData->iGuildID, guildData->iEntID, guildData->pcName);
			}
		}
	}
	else
	{
		aslGuild_SendFeedback(guildData->iEntID, guildData->iEntID, 0, guildData->pcName, pcActionType, "GuildServer_CannotValidateGuildName", true);
	}
	SAFE_FREE(guildData->pcName);
	free(guildData);
}

static void aslGuild_RenameCheckNameWithChatServer(U32 iGuildID, U32 iEntID, const char *pcNewName)
{
	GuildNameCheckData *data = malloc(sizeof(GuildNameCheckData));
	data->iGuildID = iGuildID;
	data->iEntID = iEntID;
	data->pcName = StructAllocString(pcNewName);

	RemoteCommand_ChatServerReserveGuildName(objCreateManagedReturnVal(aslGuild_RenameCheckNameCB, data),
		GLOBALTYPE_CHATSERVER, 0, data->pcName);
}

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_Rename(U32 iGuildID, U32 iEntID, char *pcNewName)
{
	char pcActionType[] = "Rename";
	Guild *pGuild = aslGuild_GetGuild(iGuildID);
	
	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsMember(pGuild, iEntID, pcActionType) &&
		aslGuild_CheckPermission(pGuild, iEntID, GuildPermission_Rename, false, pcActionType) &&
		aslGuild_CheckName(pGuild, iEntID, pcNewName, pcActionType, pGuild->iVirtualShardID) &&
		aslGuild_CheckProfanity(pGuild, iEntID, pcNewName, pcActionType)) {
		if (ShardCommon_GetClusterName())
			aslGuild_RenameCheckNameWithChatServer(iGuildID, iEntID, pcNewName);
		else
		{
			TransactionReturnVal *pReturn = objCreateManagedReturnVal(aslGuild_Rename_CB, aslGuild_MakeCBData(pcActionType));
			AutoTrans_aslGuild_tr_Rename(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, iEntID, pcNewName);
		}
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Pcname, .Earanks, .Iversion, .Icontainerid, .Eamembers");
enumTransactionOutcome aslGuild_tr_Rename(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID, char *pcNewName)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	
	if (ISNULL(pMember)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (!aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_Rename)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_NoRenamePermission", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (strlen(pcNewName) > MAX_NAME_LEN) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_NameTooLong", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	
	aslGuild_trh_SetName(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, pcNewName);
	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Change guild rank name
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_RenameRank(U32 iGuildID, U32 iEntID, S32 iRank, char *pcNewName)
{
	TransactionReturnVal *pReturn = NULL;
	char pcActionType[] = "RenameRank";
	Guild *pGuild = aslGuild_GetGuild(iGuildID);
	
	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsMember(pGuild, iEntID, pcActionType) &&
		aslGuild_CheckPermission(pGuild, iEntID, GuildPermission_RenameRank, false, pcActionType) &&
		aslGuild_CheckRank(pGuild, iEntID, iRank, pcActionType) &&
		aslGuild_CheckRankName(pGuild, iEntID, pcNewName, pcActionType) &&
		aslGuild_CheckProfanity(pGuild, iEntID, pcNewName, pcActionType)) {
		
		pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
		AutoTrans_aslGuild_tr_RenameRank(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, iEntID, iRank, pcNewName);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Pcname, .Earanks, .Icontainerid, .Eamembers");
enumTransactionOutcome aslGuild_tr_RenameRank(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID, S32 iRank, char *pcNewName)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	
	if (ISNULL(pMember)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (!aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_RenameRank)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_NoRenameRankPermission", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (iRank < 0 || iRank >= eaSize(&pGuildContainer->eaRanks)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_InvalidRank", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (strlen(pcNewName) > MAX_NAME_LEN) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_NameTooLong", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	
	aslGuild_trh_SetRankName(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, iRank, pcNewName);
	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Change guild bank tab name
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_RenameBankTab(U32 iGuildID, U32 iEntID, S32 iBagID, char *pcNewName)
{
	TransactionReturnVal *pReturn = NULL;
	char pcActionType[] = "RenameBankTab";
	Guild *pGuild = aslGuild_GetGuild(iGuildID);
	Entity *pGuildBank = aslGuild_GetGuildBank(iGuildID);
	
	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckBankExists(pGuildBank, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsMember(pGuild, iEntID, pcActionType) &&
		aslGuild_CheckPermission(pGuild, iEntID, GuildPermission_RenameBankTab, false, pcActionType) &&
		aslGuild_CheckBankTab(pGuild, pGuildBank, iEntID, iBagID, pcActionType) &&
		aslGuild_CheckBankTabName(pGuild, iEntID, pcNewName, pcActionType) &&
		aslGuild_CheckProfanity(pGuild, iEntID, pcNewName, pcActionType)) {
		
		pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
		AutoTrans_aslGuild_tr_RenameBankTab(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, GLOBALTYPE_ENTITYGUILDBANK, iGuildID, iEntID, iBagID, pcNewName);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Pcname, .Earanks, .Icontainerid, eaMembers[]")
ATR_LOCKS(pGuildBank, ".pInventoryV2.Ppinventorybags[], .pInventoryV2.Pplitebags[]");
enumTransactionOutcome aslGuild_tr_RenameBankTab(ATR_ARGS, NOCONST(Guild) *pGuildContainer, NOCONST(Entity) *pGuildBank, U32 iEntID, S32 iBagID, char *pcNewName)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	NOCONST(InventoryBag) *pBag;
	
	if (ISNULL(pMember)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (!aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_RenameBankTab)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_NoRenameBankTabPermission", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	pBag = inv_guildbank_trh_GetBag(ATR_PASS_ARGS, pGuildBank, iBagID);
	if (NONNULL(pBag))
	{
		if (ISNULL(pBag->pGuildBankInfo) || !(invbag_trh_flags(pBag) & InvBagFlag_GuildBankBag)) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_InvalidBankTab", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
		if (strlen(pcNewName) > MAX_NAME_LEN) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_NameTooLong", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}

		aslGuild_trh_SetBankTabName(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, pBag, pcNewName);
		GUILD_TRANSACTION_RETURN_SUCCESS;
	}
	else
	{
		NOCONST(InventoryBagLite) *pBagLite = inv_guildbank_trh_GetLiteBag(ATR_PASS_ARGS, pGuildBank, iBagID);
		if (ISNULL(pBagLite) || ISNULL(pBagLite->pGuildBankInfo) || !(invbaglite_trh_flags(pBagLite) & InvBagFlag_GuildBankBag)) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_InvalidBankTab", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
		if (strlen(pcNewName) > MAX_NAME_LEN) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_NameTooLong", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}

		aslGuild_trh_SetBankTabNameLite(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, pBagLite, pcNewName);
		GUILD_TRANSACTION_RETURN_SUCCESS;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set a permission for a particular rank
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_SetPermission(U32 iGuildID, U32 iEntID, S32 iRank, GuildRankPermissions ePerm, bool bOn)
{
	TransactionReturnVal *pReturn = NULL;
	char pcActionType[] = "SetPermission";
	Guild *pGuild = aslGuild_GetGuild(iGuildID);
	
	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		(iEntID == 0 ||
		 (aslGuild_CheckIsMember(pGuild, iEntID, pcActionType) &&
		  aslGuild_CheckPermission(pGuild, iEntID, GuildPermission_SetPermission, false, pcActionType) &&
		  aslGuild_CheckPermission(pGuild, iEntID, ePerm, true, pcActionType) &&
		  aslGuild_CheckRank(pGuild, iEntID, iRank, pcActionType) &&
		  aslGuild_CheckCanEditRank(pGuild, iEntID, iRank, pcActionType)))) {
		
		pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
		AutoTrans_aslGuild_tr_SetPermission(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, iEntID, iRank, ePerm, bOn);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Pcname, .Iocversion, .Earanks, .Icontainerid, eaMembers[]");
enumTransactionOutcome aslGuild_tr_SetPermission(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID, S32 iRank, U32 ePerms, U8 bOn)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	
	if (iEntID) {
		if (ISNULL(pMember)) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_SelfNotInGuild", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
		if (!aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_SetPermission)) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_NoSetPermissionPermission", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
		if (iRank >= pMember->iRank) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_EditRankTooHigh", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
		if (!aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, ePerms)) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_GivingUnownedPermission", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
	}
	if (iRank < 0 || iRank >= eaSize(&pGuildContainer->eaRanks)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_InvalidRank", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	
	if (bOn) {
		aslGuild_trh_EnablePermission(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, iRank, ePerms);
		if (ePerms == GuildPermission_SeeOfficerComment || ePerms == GuildPermission_ChangeOfficerComment) pGuildContainer->iOCVersion++;
		GUILD_TRANSACTION_RETURN_SUCCESS;
	} else {
		aslGuild_trh_DisablePermission(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, iRank, ePerms);
		if (ePerms == GuildPermission_SeeOfficerComment || ePerms == GuildPermission_ChangeOfficerComment) pGuildContainer->iOCVersion++;
		GUILD_TRANSACTION_RETURN_SUCCESS;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set a permission for a particular uniform rank
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_SetUniformPermission(U32 iGuildID, U32 iEntID, U32 iPermissionIndex, U32 iUniformIndex, const char *pcCategory, bool bOn)
{
	TransactionReturnVal *pReturn = NULL;
	char pcActionType[] = "SetPermission";
	Guild *pGuild = aslGuild_GetGuild(iGuildID);
	PlayerCostume *pCostume = NULL;
	GuildCostume *pUniform = NULL;
	int i;

	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsMember(pGuild, iEntID, pcActionType) &&
		aslGuild_CheckPermission(pGuild, iEntID, GuildPermission_SetPermission, false, pcActionType) &&
		aslGuild_CheckPermission(pGuild, iEntID, GuildPermission_SetLook, false, pcActionType))
	{
		if (iUniformIndex >= eaUSize(&pGuild->eaUniforms)) return;
		pUniform = pGuild->eaUniforms[iUniformIndex];
		if (!pUniform) return;

		//if (iPermissionIndex == 0)
		//{
		//	//SpeciesNotAllowed
		//	SpeciesDef *pSpecies = RefSystem_ReferentFromString("SpeciesDef", pcCategory);
		//	if (!pSpecies) return;
		//	for (i = eaSize(&pUniform->eaSpeciesNotAllowed)-1; i >= 0; --i)
		//	{
		//		if (pSpecies == GET_REF(pUniform->eaSpeciesNotAllowed[i]->hSpeciesRef))
		//		{
		//			if (!bOn) return; //Already Off
		//			break;
		//		}
		//	}
		//	if (i < 0)
		//	{
		//		if (bOn) return; //Already On
		//	}
		//}
		//else
		if (iPermissionIndex == 1)
		{
			//RanksNotAllowed
			char *e = (char *)pcCategory;
			int cat = strtol(pcCategory, &e, 10);
			if (e == pcCategory || cat < 0 || cat >= eaSize(&g_GuildRanks.eaRanks)) {
				return;
			}
			for (i = ea32Size(&pUniform->eaRanksNotAllowed)-1; i >= 0; --i)
			{
				if (cat == pUniform->eaRanksNotAllowed[i])
				{
					if (!bOn) return; //Already Off
					break;
				}
			}
			if (i < 0)
			{
				if (bOn) return; //Already On
			}
		}
		//else if (iPermissionIndex == 2)
		//{
		//	//ClassNotAllowed
		//	PTTypeDef *pType = RefSystem_ReferentFromString("PowerTreeType","Primarytree");
		//	PowerTreeDef *pPowerTree = RefSystem_ReferentFromString("PowerTreeDef", pcCategory);
		//	if (!pPowerTree) return;
		//	if (!pType) return;
		//	if (GET_REF(pPowerTree->hTreeType) != pType) return;
		//	for (i = eaSize(&pUniform->eaClassNotAllowed)-1; i >= 0; --i)
		//	{
		//		if (!stricmp(pcCategory,pUniform->eaClassNotAllowed[i]->pchClass))
		//		{
		//			if (!bOn) return; //Already Off
		//			break;
		//		}
		//	}
		//	if (i < 0)
		//	{
		//		if (bOn) return; //Already On
		//	}
		//}
		else
		{
			return;
		}

		pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
		AutoTrans_aslGuild_tr_SetUniformPermission(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, iEntID, iPermissionIndex, iUniformIndex, pcCategory, bOn);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Eauniforms, .Pcname, .Earanks, eaMembers[]");
enumTransactionOutcome aslGuild_tr_SetUniformPermission(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID, U32 iPermissionIndex, U32 iUniformIndex, const char *pcCategory, U8 bOn)
{
	GUILD_TRANSACTION_INIT;
	int i;
	NOCONST(GuildCostume) *pGuildCostume = NULL;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);

	if (ISNULL(pMember)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (!aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_SetPermission)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_NoSetPermissionPermission", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (!aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_SetLook)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_GivingUnownedPermission", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	if (iUniformIndex >= eaUSize(&pGuildContainer->eaUniforms))
	{
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	pGuildCostume = pGuildContainer->eaUniforms[iUniformIndex];

	//if (iPermissionIndex == 0)
	//{
	//	//SpeciesNotAllowed
	//	SpeciesDef *pSpecies = RefSystem_ReferentFromString("SpeciesDef", pcCategory);
	//	if (!pSpecies)
	//	{
	//		GUILD_TRANSACTION_RETURN_FAILURE;
	//	}
	//	for (i = eaSize(&pGuildCostume->eaSpeciesNotAllowed)-1; i >= 0; --i)
	//	{
	//		if (pSpecies == GET_REF(pGuildCostume->eaSpeciesNotAllowed[i]->hSpeciesRef))
	//		{
	//			break;
	//		}
	//	}
	//	if (bOn)
	//	{
	//		if (i >= 0)
	//		{
	//			StructDestroy(parse_GuildSpeciesPerm, pGuildCostume->eaSpeciesNotAllowed[i]);
	//			eaRemove(&pGuildCostume->eaSpeciesNotAllowed, i);
	//		}
	//	}
	//	else
	//	{
	//		if (i < 0)
	//		{
	//			NOCONST(GuildSpeciesPerm) *gsp = StructCreate(parse_GuildSpeciesPerm);
	//			SET_HANDLE_FROM_REFERENT("SpeciesDef", pSpecies, gsp->hSpeciesRef);
	//			eaPush(&pGuildCostume->eaSpeciesNotAllowed, gsp);
	//		}
	//	}
	//	GUILD_TRANSACTION_RETURN_SUCCESS;
	//}
	//else
	if (iPermissionIndex == 1)
	{
		//RanksNotAllowed
		char *e = (char *)pcCategory;
		int cat = strtol(pcCategory, &e, 10);
		if (e == pcCategory || cat >= eaSize(&pGuildContainer->eaRanks)) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_InvalidRank", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}

		for (i = ea32Size(&pGuildCostume->eaRanksNotAllowed)-1; i >= 0; --i)
		{
			if (pGuildCostume->eaRanksNotAllowed[i] == cat)
			{
				break;
			}
		}
		if (bOn)
		{
			if (i >= 0)
			{
				ea32Remove(&pGuildCostume->eaRanksNotAllowed, i);
			}
		}
		else
		{
			if (i < 0)
			{
				ea32Push(&pGuildCostume->eaRanksNotAllowed, cat);
			}
		}
		GUILD_TRANSACTION_RETURN_SUCCESS;
	}
	//else if (iPermissionIndex == 2)
	//{
	//	//ClassNotAllowed
	//	PTTypeDef *pType = RefSystem_ReferentFromString("PowerTreeType","Primarytree");
	//	PowerTreeDef *pPowerTree = RefSystem_ReferentFromString("PowerTreeDef", pcCategory);
	//	if ((!pPowerTree) || (!pType) || GET_REF(pPowerTree->hTreeType) != pType)
	//	{
	//		GUILD_TRANSACTION_RETURN_FAILURE;
	//	}
	//	for (i = eaSize(&pGuildCostume->eaClassNotAllowed)-1; i >= 0; --i)
	//	{
	//		if (!stricmp(pPowerTree->pchName,pGuildCostume->eaClassNotAllowed[i]->pchClass))
	//		{
	//			break;
	//		}
	//	}
	//	if (bOn)
	//	{
	//		if (i >= 0)
	//		{
	//			StructDestroy(parse_GuildClassPerm, pGuildCostume->eaClassNotAllowed[i]);
	//			eaRemove(&pGuildCostume->eaClassNotAllowed, i);
	//		}
	//	}
	//	else
	//	{
	//		if (i < 0)
	//		{
	//			NOCONST(GuildClassPerm) *gcp = StructCreate(parse_GuildClassPerm);
	//			gcp->pchClass = allocAddString(pPowerTree->pchName);
	//			eaPush(&pGuildCostume->eaClassNotAllowed, gcp);
	//		}
	//	}
	//	GUILD_TRANSACTION_RETURN_SUCCESS;
	//}

	GUILD_TRANSACTION_RETURN_FAILURE;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set a permission for a bank tab at a particular rank
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_SetBankPermission(U32 iGuildID, U32 iEntID, S32 iBagID, S32 iRank, GuildBankPermissions ePerm, bool bOn)
{
	TransactionReturnVal *pReturn = NULL;
	char pcActionType[] = "SetBankPermission";
	Guild *pGuild = aslGuild_GetGuild(iGuildID);
	Entity *pGuildBank = aslGuild_GetGuildBank(iGuildID);

	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckBankExists(pGuildBank, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsMember(pGuild, iEntID, pcActionType) &&
		aslGuild_CheckPermission(pGuild, iEntID, GuildPermission_SetBankPermission, false, pcActionType) &&
		aslGuild_CheckBankTab(pGuild, pGuildBank, iEntID, iBagID, pcActionType) &&
		aslGuild_CheckBankPermission(pGuild, pGuildBank, iEntID, iBagID, ePerm, true, pcActionType) &&
		aslGuild_CheckRank(pGuild, iEntID, iRank, pcActionType) &&
		aslGuild_CheckCanEditRank(pGuild, iEntID, iRank, pcActionType)) {
		
		pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
		AutoTrans_aslGuild_tr_SetBankPermission(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, GLOBALTYPE_ENTITYGUILDBANK, iGuildID, iEntID, iBagID, iRank, ePerm, bOn);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Pcname, .Earanks, .Icontainerid, eaMembers[]")
ATR_LOCKS(pGuildBank, ".pInventoryV2.Ppinventorybags[], .pInventoryV2.Pplitebags[]");
enumTransactionOutcome aslGuild_tr_SetBankPermission(ATR_ARGS, NOCONST(Guild) *pGuildContainer, NOCONST(Entity) *pGuildBank, U32 iEntID, S32 iBagID, S32 iRank, U32 ePerm, U8 bOn)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	NOCONST(InventoryBag) *pBag;
	
	if (ISNULL(pMember)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (!aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_SetBankPermission)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_NoSetBankPermissionPermission", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (iRank < 0 || iRank >= eaSize(&pGuildContainer->eaRanks)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_InvalidRank", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (iRank >= pMember->iRank) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_EditRankTooHigh", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	pBag = inv_guildbank_trh_GetBag(ATR_PASS_ARGS, pGuildBank, iBagID);
	if (NONNULL(pBag)) {
		if (ISNULL(pBag->pGuildBankInfo) || !(invbag_trh_flags(pBag) & InvBagFlag_GuildBankBag)) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_InvalidBankTab", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
		if (pMember->iRank < 0 || pMember->iRank >= eaSize(&pBag->pGuildBankInfo->eaPermissions) ||
			!(pBag->pGuildBankInfo->eaPermissions[pMember->iRank]->ePerms & ePerm)) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_GivingUnownedPermission", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
	
		if (bOn) {
			aslGuild_trh_EnableBankPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, pBag, iRank, ePerm);
			GUILD_TRANSACTION_RETURN_SUCCESS;
		} else {
			aslGuild_trh_DisableBankPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, pBag, iRank, ePerm);
			GUILD_TRANSACTION_RETURN_SUCCESS;
		}
	} else {
		NOCONST(InventoryBagLite) *pBagLite = inv_guildbank_trh_GetLiteBag(ATR_PASS_ARGS, pGuildBank, iBagID);
		if (ISNULL(pBagLite) || ISNULL(pBagLite->pGuildBankInfo) || !(invbaglite_trh_flags(pBagLite) & InvBagFlag_GuildBankBag)) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_InvalidBankTab", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
		if (pMember->iRank < 0 || pMember->iRank >= eaSize(&pBagLite->pGuildBankInfo->eaPermissions) ||
			!(pBagLite->pGuildBankInfo->eaPermissions[pMember->iRank]->ePerms & ePerm)) {
				aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_GivingUnownedPermission", true);
				GUILD_TRANSACTION_RETURN_FAILURE;
		}

		if (bOn) {
			aslGuild_trh_EnableBankPermissionLite(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, pBagLite, iRank, ePerm);
			GUILD_TRANSACTION_RETURN_SUCCESS;
		} else {
			aslGuild_trh_DisableBankPermissionLite(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, pBagLite, iRank, ePerm);
			GUILD_TRANSACTION_RETURN_SUCCESS;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set a withdraw limit for a bank tab at a particular rank
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_SetBankWithdrawLimit(U32 iGuildID, U32 iEntID, S32 iBagID, S32 iRank, S32 iLimit)
{
	TransactionReturnVal *pReturn = NULL;
	char pcActionType[] = "SetBankWithdrawLimit";
	Guild *pGuild = aslGuild_GetGuild(iGuildID);
	Entity *pGuildBank = aslGuild_GetGuildBank(iGuildID);

	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckBankExists(pGuildBank, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsMember(pGuild, iEntID, pcActionType) &&
		aslGuild_CheckPermission(pGuild, iEntID, GuildPermission_SetBankPermission, false, pcActionType) &&
		aslGuild_CheckBankTab(pGuild, pGuildBank, iEntID, iBagID, pcActionType) &&
		aslGuild_CheckRank(pGuild, iEntID, iRank, pcActionType) &&
		aslGuild_CheckCanSetWithdrawLimit(pGuild, pGuildBank, iEntID, iRank, iBagID, iLimit, pcActionType) &&
		aslGuild_CheckCanEditRank(pGuild, iEntID, iRank, pcActionType)) {
		
		pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
		AutoTrans_aslGuild_tr_SetBankWithdrawLimit(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, GLOBALTYPE_ENTITYGUILDBANK, iGuildID, iEntID, iBagID, iRank, iLimit);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Pcname, .Earanks, .Icontainerid, eaMembers[]")
ATR_LOCKS(pGuildBank, ".pInventoryV2.Ppinventorybags[], .pInventoryV2.Pplitebags[]");
enumTransactionOutcome aslGuild_tr_SetBankWithdrawLimit(ATR_ARGS, NOCONST(Guild) *pGuildContainer, NOCONST(Entity) *pGuildBank, U32 iEntID, S32 iBagID, S32 iRank, S32 iLimit)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	S32 iPlayersLimit;
	NOCONST(GuildBankTabInfo) *pGuildBankInfo = NULL;
	
	if (ISNULL(pMember)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (!aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_SetBankPermission)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_NoSetBankPermissionPermission", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (iRank < 0 || iRank >= eaSize(&pGuildContainer->eaRanks)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_InvalidRank", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (iRank >= pMember->iRank) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_EditRankTooHigh", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	pGuildBankInfo = inv_guildbank_trh_GetBankTabInfo(pGuildBank, iBagID);

	if (ISNULL(pGuildBankInfo))
	{
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_InvalidBankTab", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if(pMember->iRank >= 0 && pMember->iRank < eaSize(&pGuildBankInfo->eaPermissions))
	{
		iPlayersLimit = pGuildBankInfo->eaPermissions[pMember->iRank]->iWithdrawLimit;
	}
	else
	{
		iPlayersLimit = 0;
	}
	
	if (iPlayersLimit > 0 && (iLimit > iPlayersLimit || iLimit <= 0)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_WithdrawLimitHigherThanOwn", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	
	aslGuild_trh_SetBankWithdrawLimit(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, pGuildBankInfo, iRank, iLimit);
	GUILD_TRANSACTION_RETURN_SUCCESS;
}

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_SetBankItemWithdrawLimit(U32 iGuildID, U32 iEntID, S32 iBagID, S32 iRank, S32 iCount)
{
	TransactionReturnVal *pReturn = NULL;
	char pcActionType[] = "SetBankItemWithdrawLimit";
	Guild *pGuild = aslGuild_GetGuild(iGuildID);
	Entity *pGuildBank = aslGuild_GetGuildBank(iGuildID);

	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsMember(pGuild, iEntID, pcActionType) &&
		aslGuild_CheckPermission(pGuild, iEntID, GuildPermission_SetBankPermission, false, pcActionType) &&
		aslGuild_CheckBankTab(pGuild, pGuildBank, iEntID, iBagID, pcActionType) &&
		aslGuild_CheckRank(pGuild, iEntID, iRank, pcActionType) &&
		aslGuild_CheckCanSetItemWithdrawLimit(pGuild, pGuildBank, iEntID, iRank, iBagID, iCount, pcActionType) &&
		aslGuild_CheckCanEditRank(pGuild, iEntID, iRank, pcActionType)) {

			pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
			AutoTrans_aslGuild_tr_SetBankItemWithdrawLimit(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, GLOBALTYPE_ENTITYGUILDBANK, iGuildID, iEntID, iBagID, iRank, iCount);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Pcname, .Earanks, .Icontainerid, eaMembers[]")
ATR_LOCKS(pGuildBank, ".pInventoryV2.Ppinventorybags[], pInventoryV2.ppLiteBags[]");
enumTransactionOutcome aslGuild_tr_SetBankItemWithdrawLimit(ATR_ARGS, NOCONST(Guild) *pGuildContainer, NOCONST(Entity) *pGuildBank, U32 iEntID, S32 iBagID, S32 iRank, S32 iCount)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	NOCONST(GuildBankTabInfo) *pGuildBankInfo = NULL;
	S32 iPlayersLimit;

	if (!pMember) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (!aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_SetBankPermission)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_NoSetBankPermissionPermission", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (iRank < 0 || iRank >= eaSize(&pGuildContainer->eaRanks)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_InvalidRank", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (iRank >= pMember->iRank) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_EditRankTooHigh", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	pGuildBankInfo = inv_guildbank_trh_GetBankTabInfo(pGuildBank, iBagID);

	if (ISNULL(pGuildBankInfo))
	{
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_InvalidBankTab", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if(pMember->iRank >= 0 && pMember->iRank < eaSize(&pGuildBankInfo->eaPermissions))
	{
		iPlayersLimit = pGuildBankInfo->eaPermissions[pMember->iRank]->iWithdrawItemCountLimit;
	}
	else
	{
		iPlayersLimit = 0;
	}
	if (iPlayersLimit > 0 && (iCount > iPlayersLimit || iCount <= 0)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_WithdrawLimitHigherThanOwn", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	if(NONNULL(pGuildBankInfo))
	{
		aslGuild_trh_SetBankItemWithdrawLimit(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, pGuildBankInfo, iRank, iCount);
	}

	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set the Guild MotD
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_SetMotD(U32 iGuildID, U32 iEntID, char *pcMotD, bool bSystemGuild)
{
	char pcActionType[] = "SetMotD";
	TransactionReturnVal *pReturn = NULL;
	Guild *pGuild = aslGuild_GetGuild(iGuildID);

	if(!pGuild || pGuild->bIsOwnedBySystem != bSystemGuild)
	{
		// something wrong here
		return;
	}
	
	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		(bSystemGuild || 
		(aslGuild_CheckIsMember(pGuild, iEntID, pcActionType) &&
		aslGuild_CheckPermission(pGuild, iEntID, GuildPermission_SetMotD, false, pcActionType)))) {
		
		pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
		AutoTrans_aslGuild_tr_SetMotD(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, iEntID, pcMotD);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Pcname, .Earanks, .Pcmotd, .Imotdupdated, .Icontainerid, eaMembers[], .Bisownedbysystem");
enumTransactionOutcome aslGuild_tr_SetMotD(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID, char *pcMotD)
{
	GUILD_TRANSACTION_INIT;
	if(!pGuildContainer->bIsOwnedBySystem)
	{
		NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);

		if (ISNULL(pMember)) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_SelfNotInGuild", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
		if (!aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_SetMotD)) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_NoSetMotDPermission", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
	}
	
	aslGuild_trh_SetMotD(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, pcMotD);

	timingHistoryPush(gActionHistory);

	GUILD_TRANSACTION_RETURN_SUCCESS;
}
///////////////////////////////////////////////////////////////////////////////////////////
// Set personal public comment
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_SetPublicComment(U32 iGuildID, U32 iEntID, char *pcComment)
{
	char pcActionType[] = "SetPublicComment";
	TransactionReturnVal *pReturn = NULL;
	Guild *pGuild = aslGuild_GetGuild(iGuildID);

	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsMember(pGuild, iEntID, pcActionType)) {
			pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
			AutoTrans_aslGuild_tr_SetPublicComment(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, iEntID, pcComment);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Icontainerid, .Pcname, eaMembers[]");
enumTransactionOutcome aslGuild_tr_SetPublicComment(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID, char *pcComment)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);

	if (ISNULL(pMember)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	if (pMember->pcPublicComment) StructFreeStringSafe(&pMember->pcPublicComment);
	if (pcComment && *pcComment) pMember->pcPublicComment = StructAllocString(pcComment);
	pMember->iPublicCommentTime = timeSecondsSince2000();
	aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Set Guild Public Comment to: %s", pcComment ? pcComment : "");

	timingHistoryPush(gActionHistory);

	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set officer comment
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_SetOfficerComment(U32 iGuildID, U32 iEntID, U32 iSubjectID, char *pcComment)
{
	char pcActionType[] = "SetOfficerComment";
	TransactionReturnVal *pReturn = NULL;
	Guild *pGuild = aslGuild_GetGuild(iGuildID);
	GuildMember *pMember = pGuild ? eaIndexedGetUsingInt(&pGuild->eaMembers, iEntID) : NULL;
	GuildMember *pSubjectMember = pGuild ? eaIndexedGetUsingInt(&pGuild->eaMembers, iSubjectID) : NULL;

	if (pMember && pSubjectMember &&
		aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsMember(pGuild, iEntID, pcActionType) &&
		aslGuild_CheckPermission(pGuild, iEntID, GuildPermission_ChangeOfficerComment, false, pcActionType) &&
		aslGuild_CheckPermission(pGuild, iEntID, GuildPermission_SeeOfficerComment, false, pcActionType) &&
		pMember->iRank > pSubjectMember->iRank) {
			pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
			AutoTrans_aslGuild_tr_SetOfficerComment(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, iEntID, iSubjectID, pcComment);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Iocversion, .Icontainerid, .Pcname, eaMembers[], eaMembers[]");
enumTransactionOutcome aslGuild_tr_SetOfficerComment(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID, U32 iSubjectID, char *pcComment)
{
	GUILD_TRANSACTION_INIT;
	char temp[128];
	NOCONST(GuildMember) *pOfficerMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iSubjectID);

	if (ISNULL(pMember)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iSubjectID, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (ISNULL(pOfficerMember)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iSubjectID, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	if (pMember->pcOfficerComment) StructFreeStringSafe(&pMember->pcOfficerComment);
	if (pMember->pcWhoOfficerComment) StructFreeStringSafe(&pMember->pcWhoOfficerComment);
	if (pcComment && *pcComment) pMember->pcOfficerComment = StructAllocString(pcComment);
	*temp = '\0';
	strcat(temp, pOfficerMember->pcName);
	strcat(temp, pOfficerMember->pcAccount);
	pMember->pcWhoOfficerComment = StructAllocString(temp);
	pMember->iOfficerCommentTime = timeSecondsSince2000();
	pGuildContainer->iOCVersion++;
	aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pOfficerMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Set Guild Officer Comment for '%s' to: %s", pMember->pcLogName, pcComment ? pcComment : "");

	timingHistoryPush(gActionHistory);

	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Guild Event Remove
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_RemoveGuildEvent(U32 iGuildID, U32 uiID)
{
	char pcActionType[] = "RemoveGuildEvent";
	Guild *pGuild = aslGuild_GetGuild(iGuildID);

	if (pGuild)
	{
		TransactionReturnVal *pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
		AutoTrans_aslGuild_tr_RemoveGuildEvent(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, uiID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Eaevents, .Ieventupdated, .Icontainerid, .Pcname");
enumTransactionOutcome aslGuild_tr_RemoveGuildEvent(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 uiID)
{
	int index;
	GUILD_TRANSACTION_INIT;

	index = eaIndexedFindUsingInt(&pGuildContainer->eaEvents, uiID);
	if (index < 0)
	{
		aslGuild_AddFailureLogMessage(ATR_GUILD_RETURN, "", pGuildContainer->iContainerID, pGuildContainer->pcName, "Failed to Remove Guild Event");
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	StructDestroyNoConst(parse_GuildEvent, pGuildContainer->eaEvents[index]);
	eaRemove(&pGuildContainer->eaEvents, index);
	pGuildContainer->iEventUpdated = timeSecondsSince2000();

	aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, "", pGuildContainer->iContainerID, pGuildContainer->pcName, "Removed Guild Event");
	timingHistoryPush(gActionHistory);
	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Guild Event Recurrence
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_RecurGuildEvent(U32 iGuildID, U32 uiID)
{
	char pcActionType[] = "RecurGuildEvent";
	Guild *pGuild = aslGuild_GetGuild(iGuildID);
	GuildEvent *pGuildEvent = pGuild ? eaIndexedGetUsingInt(&pGuild->eaEvents, uiID) : NULL;

	if (pGuild && pGuildEvent && !pGuildEvent->bRecurInTransaction)
	{
		ASLGuildCBData *pData = aslGuild_MakeCBData(pcActionType);
		TransactionReturnVal *pReturn;

		pData->iGuildID = iGuildID;
		pData->iGuildEventID = uiID;
		pReturn = objCreateManagedReturnVal(aslGuild_RecurGuildEvent_CB, pData);

		pGuildEvent->bRecurInTransaction = true;
		AutoTrans_aslGuild_tr_RecurGuildEvent(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, uiID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Icontainerid, .Pcname, .Eaevents[], .Eamembers");
enumTransactionOutcome aslGuild_tr_RecurGuildEvent(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 uiID)
{
	int i;
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildEvent) *pGuildEvent = eaIndexedGetUsingInt(&pGuildContainer->eaEvents, uiID);
	S32 iRecurDifference;
	U32 iCurrentTime = timeSecondsSince2000();

	if (ISNULL(pGuildEvent))
	{
		aslGuild_AddFailureLogMessage(ATR_GUILD_RETURN, "", pGuildContainer->iContainerID, pGuildContainer->pcName, "Failed to Recur Guild Event %d", uiID);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	// Since RecurGuildEvent is called asynchronously, we need to make sure that we actually need to recur this event
	if (pGuildEvent->iStartTimeTime + pGuildEvent->iDuration + MIN_GUILD_EVENT_TIME_PAST_REMOVE >= iCurrentTime)
	{
		aslGuild_AddFailureLogMessage(ATR_GUILD_RETURN, "", pGuildContainer->iContainerID, pGuildContainer->pcName,
			"Failed to Recur Guild Event %d because it was already recurred in a previous transaction", uiID);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	// Make sure the event actually needs to be recurred. Protects against both bad input and a divide by zero error
	if (pGuildEvent->eRecurType == 0)
	{
		aslGuild_AddFailureLogMessage(ATR_GUILD_RETURN, "", pGuildContainer->iContainerID, pGuildContainer->pcName,
			"Failed to Recur Guild Event %d because it was set to not recur, and should have been removed instead", uiID);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	iRecurDifference = ((iCurrentTime - pGuildEvent->iStartTimeTime) / DAYS(pGuildEvent->eRecurType)) + 1;
	iRecurDifference = (pGuildEvent->iRecurrenceCount > 0) ? MIN(iRecurDifference, pGuildEvent->iRecurrenceCount) : iRecurDifference;

	pGuildEvent->iStartTimeTime += DAYS(pGuildEvent->eRecurType) * iRecurDifference;
	pGuildEvent->iEventUpdated = timeSecondsSince2000();
	if (pGuildEvent->iRecurrenceCount > 0)
	{
		pGuildEvent->iRecurrenceCount = pGuildEvent->iRecurrenceCount - iRecurDifference;
	}

	for (i = eaSize(&pGuildEvent->eaReplies) - 1; i >= 0; i--)
	{
		NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, pGuildEvent->eaReplies[i]->iMemberID);
		if (!pMember || pGuildEvent->eaReplies[i]->iStartTime < pGuildEvent->iStartTimeTime)
		{
			StructDestroyNoConst(parse_GuildEventReply, pGuildEvent->eaReplies[i]);
			eaRemove(&pGuildEvent->eaReplies, i);
		}
	}

	aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, "", pGuildContainer->iContainerID, pGuildContainer->pcName, "Recurred Guild Event %d", uiID);
	timingHistoryPush(gActionHistory);
	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// New Guild Event
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_NewGuildEvent(U32 iGuildID, U32 iEntID, GuildEventData *pGuildEventData)
{
	char pcActionType[] = "NewGuildEvent";
	Guild *pGuild = aslGuild_GetGuild(iGuildID);

	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsMember(pGuild, iEntID, pcActionType) &&
		aslGuild_CheckPermission(pGuild, iEntID, GuildPermission_PostEvent, false, pcActionType))
	{
		TransactionReturnVal *pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
		AutoTrans_aslGuild_tr_NewGuildEvent(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, iEntID, pGuildEventData);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Eaevents, .Ieventupdated, .Icontainerid, .Inextguildeventindex, .Pcname, eaMembers[]");
enumTransactionOutcome aslGuild_tr_NewGuildEvent(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID, GuildEventData *pGuildEventData)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	NOCONST(GuildEvent) *pGuildEvent;

	if (ISNULL(pMember))
	{
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iEntID, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	pGuildEvent = StructCreateNoConst(parse_GuildEvent);

	if (pGuildEventData->pcTitle)
	{
		pGuildEvent->pcTitle = StructAllocString(pGuildEventData->pcTitle);
	}

	if (pGuildEventData->pcDescription)
	{
		pGuildEvent->pcDescription = StructAllocString(pGuildEventData->pcDescription);
	}

	pGuildEvent->uiID = pGuildContainer->iNextGuildEventIndex;
	pGuildEvent->iStartTimeTime = pGuildEventData->iStartTimeTime;
	pGuildEvent->iDuration = pGuildEventData->iDuration;
	pGuildEvent->eRecurType = pGuildEventData->eRecurType;
	pGuildEvent->iRecurrenceCount = pGuildEventData->iRecurrenceCount;
	pGuildEvent->iMinGuildRank = pGuildEventData->iMinGuildRank;
	pGuildEvent->iMinGuildEditRank = pGuildEventData->iMinGuildEditRank;
	pGuildEvent->iMinLevel = pGuildEventData->iMinLevel;
	pGuildEvent->iMaxLevel = pGuildEventData->iMaxLevel;
	pGuildEvent->iMinAccepts = pGuildEventData->iMinAccepts;
	pGuildEvent->iMaxAccepts = pGuildEventData->iMaxAccepts;

	pGuildContainer->iNextGuildEventIndex++;
	pGuildContainer->iEventUpdated = timeSecondsSince2000();

	if (!eaSize(&pGuildContainer->eaEvents))
	{
		eaIndexedEnableNoConst(&pGuildContainer->eaEvents, parse_GuildEvent);
	}
	eaIndexedAdd(&pGuildContainer->eaEvents, pGuildEvent);

	aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Added Guild Event: %s", pGuildEvent->pcTitle);
	timingHistoryPush(gActionHistory);
	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Edit Guild Event
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_EditGuildEvent(U32 iGuildID, U32 iEntID, GuildEventData *pGuildEventData)
{
	char pcActionType[] = "EditGuildEvent";
	Guild *pGuild = aslGuild_GetGuild(iGuildID);
	NOCONST(GuildMember) *pMember = pGuild ? aslGuild_trh_FindMember(ATR_EMPTY_ARGS, NULL, CONTAINER_NOCONST(Guild, pGuild), iEntID) : NULL;
	NOCONST(GuildEvent) *pGuildEvent;

	if (!pGuildEventData || !pGuild || !pMember)
	{
		return;
	}

	pGuildEvent = eaIndexedGetUsingInt(&pGuild->eaEvents, pGuildEventData->uiID);
	if (!pGuildEvent)
	{
		return;
	}

	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsMember(pGuild, iEntID, pcActionType) &&
		pMember->iRank >= pGuildEvent->iMinGuildEditRank)
	{
		TransactionReturnVal *pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
		AutoTrans_aslGuild_tr_EditGuildEvent(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, iEntID, pGuildEventData);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Ieventupdated, .Icontainerid, .Pcname, .Eaevents, eaMembers[]");
enumTransactionOutcome aslGuild_tr_EditGuildEvent(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID, GuildEventData *pGuildEventData)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildEvent) *pGuildEvent;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);

	if (ISNULL(pMember))
	{
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iEntID, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	pGuildEvent = eaIndexedGetUsingInt(&pGuildContainer->eaEvents, pGuildEventData->uiID);
	if (ISNULL(pGuildEvent))
	{
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iEntID, "GuildServer_NoTitleFound", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	StructFreeStringSafe(&pGuildEvent->pcTitle);
	if (pGuildEventData->pcTitle)
	{
		pGuildEvent->pcTitle = StructAllocString(pGuildEventData->pcTitle);
	}

	StructFreeStringSafe(&pGuildEvent->pcDescription);
	if (pGuildEventData->pcDescription)
	{
		pGuildEvent->pcDescription = StructAllocString(pGuildEventData->pcDescription);
	}

	pGuildEvent->iStartTimeTime = pGuildEventData->iStartTimeTime;
	pGuildEvent->iDuration = pGuildEventData->iDuration;
	pGuildEvent->eRecurType = pGuildEventData->eRecurType;
	pGuildEvent->iRecurrenceCount = pGuildEventData->iRecurrenceCount;
	pGuildEvent->iMinGuildRank = pGuildEventData->iMinGuildRank;
	pGuildEvent->iMinGuildEditRank = pGuildEventData->iMinGuildEditRank;
	pGuildEvent->iMinLevel = pGuildEventData->iMinLevel;
	pGuildEvent->iMaxLevel = pGuildEventData->iMaxLevel;
	pGuildEvent->iMinAccepts = pGuildEventData->iMinAccepts;
	pGuildEvent->iMaxAccepts = pGuildEventData->iMaxAccepts;
	pGuildEvent->bCanceled = false;
	pGuildContainer->iEventUpdated = timeSecondsSince2000();

	aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Edited Guild Event: %s", pGuildEvent->pcTitle);
	timingHistoryPush(gActionHistory);
	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Cancel Guild Event
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_CancelGuildEvent(U32 iGuildID, U32 iEntID, U32 uiID)
{
	char pcActionType[] = "CancelGuildEvent";
	Guild *pGuild = aslGuild_GetGuild(iGuildID);
	GuildMember *pMember = pGuild ? (GuildMember*)aslGuild_trh_FindMember(ATR_EMPTY_ARGS, NULL, CONTAINER_NOCONST(Guild, pGuild), iEntID) : NULL;
	GuildEvent *pGuildEvent;

	if (!pGuild || !pMember)
	{
		return;
	}

	pGuildEvent = eaIndexedGetUsingInt(&pGuild->eaEvents, uiID);
	if (!pGuildEvent || pGuildEvent->bCanceled)
	{
		return;
	}

	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsMember(pGuild, iEntID, pcActionType) &&
		pMember->iRank >= pGuildEvent->iMinGuildEditRank)
	{
		TransactionReturnVal *pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
		AutoTrans_aslGuild_tr_CancelGuildEvent(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, iEntID, uiID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Ieventupdated, .Icontainerid, .Pcname, .Eaevents[], eaMembers[]");
enumTransactionOutcome aslGuild_tr_CancelGuildEvent(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID, U32 uiID)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildEvent) *pGuildEvent;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);

	if (ISNULL(pMember))
	{
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iEntID, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	pGuildEvent = eaIndexedGetUsingInt(&pGuildContainer->eaEvents, uiID);
	if (!pGuildEvent)
	{
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iEntID, "GuildServer_NoTitleFound", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	pGuildContainer->iEventUpdated = timeSecondsSince2000();
	pGuildEvent->bCanceled = true;

	aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Canceled Guild Event: %s", pGuildEvent->pcTitle);
	timingHistoryPush(gActionHistory);
	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Reply Guild Event
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_ReplyGuildEvent(U32 iGuildID, U32 iEntID, U32 uiID, U32 iStartTime, /*GuildEventReplyType*/ int eReplyType, char *pcMessage)
{
	char pcActionType[] = "ReplyGuildEvent";
	Guild *pGuild = aslGuild_GetGuild(iGuildID);

	if (!pGuild)
	{
		return;
	}

	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsMember(pGuild, iEntID, pcActionType))
	{
		TransactionReturnVal *pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
		AutoTrans_aslGuild_tr_ReplyGuildEvent(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, iEntID, uiID, iStartTime, eReplyType, pcMessage);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Pcname, .Eaevents[], eaMembers[]");
enumTransactionOutcome aslGuild_tr_ReplyGuildEvent(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID, U32 uiID, U32 iStartTime, /*GuildEventReplyType*/ int eReplyType, char *pcMessage)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildEvent) *pGuildEvent;
	NOCONST(GuildEventReply) *pReply = NULL;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	int i;
	U32 iCount = 0;
	U64 iKey;

	if (ISNULL(pMember))
	{
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iEntID, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	pGuildEvent = eaIndexedGetUsingInt(&pGuildContainer->eaEvents, uiID);
	if (ISNULL(pGuildEvent))
	{
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iEntID, "GuildServer_NoTitleFound", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	if (pGuildEvent->bCanceled)
	{
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iEntID, "GuildServer_EventCanceled", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	if (pMember->iRank < pGuildEvent->iMinGuildRank)
	{
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iEntID, "GuildServer_EventLowRank", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	if (pMember->iLevel < pGuildEvent->iMinLevel || pMember->iLevel > pGuildEvent->iMaxLevel)
	{
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iEntID, "GuildServer_EventWrongLevel", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	if (iStartTime < timeSecondsSince2000())
	{
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iEntID, "GuildServer_IncorrectTime", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	if (pGuildEvent->eRecurType != 0)
	{
		if ((iStartTime - pGuildEvent->iStartTimeTime) % DAYS(pGuildEvent->eRecurType) != 0)
		{
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iEntID, "GuildServer_IncorrectTime", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
	}
	else if (iStartTime != pGuildEvent->iStartTimeTime)
	{
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iEntID, "GuildServer_IncorrectTime", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	for (i = eaSize(&pGuildEvent->eaReplies)-1; i >= 0; --i)
	{
		if ((pGuildEvent->eaReplies[i]->iStartTime == iStartTime) &&
			(pGuildEvent->eaReplies[i]->eGuildEventReplyType == GuildEventReplyType_Accept))
		{
			iCount++;
		}
	}

	if (eReplyType == GuildEventReplyType_Accept && pGuildEvent->iMaxAccepts && pGuildEvent->iMaxAccepts <= iCount)
	{
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iEntID, "GuildServer_EventFilled", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	iKey = guildevent_GetReplyKey(iEntID, iStartTime);
	pReply = eaIndexedGetUsingInt(&pGuildEvent->eaReplies, iKey);

	if (!pReply)
	{
		pReply = StructCreateNoConst(parse_GuildEventReply);
		pReply->iMemberID = iEntID;
		pReply->iStartTime = iStartTime;
		pReply->uiKey = guildevent_GetReplyKey(iEntID, iStartTime);

		if (!eaSize(&pGuildEvent->eaReplies))
		{
			eaIndexedEnableNoConst(&pGuildEvent->eaReplies, parse_GuildEventReply);
		}
		eaIndexedAdd(&pGuildEvent->eaReplies, pReply);
	}

	pReply->eGuildEventReplyType = eReplyType;
	if (pReply->pcReplyMessage)
	{
		StructFreeStringSafe(&pReply->pcReplyMessage);
	}

	if (pcMessage)
	{
		pReply->pcReplyMessage = StructAllocString(pcMessage);
	}

	timingHistoryPush(gActionHistory);
	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set the Guild Description
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_SetDescription(U32 iGuildID, U32 iEntID, char *pcDescription, bool bSystemGuild)
{
	char pcActionType[] = "SetDescription";
	TransactionReturnVal *pReturn = NULL;
	Guild *pGuild = aslGuild_GetGuild(iGuildID);

	if(!pGuild || pGuild->bIsOwnedBySystem != bSystemGuild)
	{
		// something wrong here
		return;
	}

	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		(bSystemGuild || 
		(aslGuild_CheckIsMember(pGuild, iEntID, pcActionType) &&
		aslGuild_CheckPermission(pGuild, iEntID, GuildPermission_Rename, false, pcActionType)))) {

			pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
			AutoTrans_aslGuild_tr_SetDescription(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, iEntID, pcDescription);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Pcname, .Earanks, .Pcdescription, .Icontainerid, eaMembers[], .Bisownedbysystem");
enumTransactionOutcome aslGuild_tr_SetDescription(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID, char *pcDescription)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);

	if(!pGuildContainer->bIsOwnedBySystem || iEntID)
	{
		if (ISNULL(pMember)) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_SelfNotInGuild", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
		if (!aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_Rename)) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_NoRenamePermission", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
	}

	aslGuild_trh_SetDescription(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, pcDescription);

	timingHistoryPush(gActionHistory);

	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set the Guild emblem
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_SetEmblem(U32 iGuildID, U32 iEntID, char *pcEmblem, bool bSystemGuild)
{
	char pcActionType[] = "SetEmblem";
	ASLGuildCBData *pData = NULL;
	TransactionReturnVal *pReturn = NULL;
	Guild *pGuild = aslGuild_GetGuild(iGuildID);

	if(!pGuild || pGuild->bIsOwnedBySystem != bSystemGuild)
	{
		// something wrong here
		return;
	}
	
	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		(
			bSystemGuild ||
			(aslGuild_CheckIsMember(pGuild, iEntID, pcActionType) &&
			aslGuild_CheckPermission(pGuild, iEntID, GuildPermission_SetLook, false, pcActionType))
		) &&
		pcEmblem && aslGuild_CheckEmblemType(pGuild, iEntID, pcEmblem, true, false, pcActionType)) {
		
		pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
		AutoTrans_aslGuild_tr_SetEmblem(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, iEntID, pcEmblem);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Pcname, .Earanks, .Pcemblem, .Icontainerid, eaMembers[], .Bisownedbysystem");
enumTransactionOutcome aslGuild_tr_SetEmblem(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID, char *pcEmblem)
{
	GUILD_TRANSACTION_INIT;
	if(!pGuildContainer->bIsOwnedBySystem)
	{
		NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);

		if (ISNULL(pMember)) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_SelfNotInGuild", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
		if (!aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_SetLook)) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_NoEmblemPermission", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
	}
	
	aslGuild_trh_SetEmblem(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, pcEmblem);
	GUILD_TRANSACTION_RETURN_SUCCESS;
}

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_SetAdvancedEmblem(U32 iGuildID, U32 iEntID, char *pcEmblem, U32 iEmblemColor0, U32 iEmblemColor1, F32 fEmblemRotation, bool bHidePlayerFeedback, bool bSystemGuild)
{
	char pcActionType[] = "SetEmblem";
	ASLGuildCBData *pData = NULL;
	TransactionReturnVal *pReturn = NULL;
	Guild *pGuild = aslGuild_GetGuild(iGuildID);

	if(!pGuild || pGuild->bIsOwnedBySystem != bSystemGuild)
	{
		// something wrong here
		return;
	}

	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		(
			bSystemGuild ||
			(aslGuild_CheckIsMember(pGuild, iEntID, pcActionType) &&
			aslGuild_CheckPermission(pGuild, iEntID, GuildPermission_SetLook, false, pcActionType))
		) &&
		(gConf.bAllowGuildstoHaveNoEmblems || pcEmblem) && aslGuild_CheckEmblemType(pGuild, iEntID, pcEmblem, true, false, pcActionType)) {

			pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
			AutoTrans_aslGuild_tr_SetAdvancedEmblem(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, iEntID, pcEmblem, iEmblemColor0, iEmblemColor1, fEmblemRotation, bHidePlayerFeedback);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Pcname, .Earanks, .Pcemblem, .Iemblemcolor0, .Iemblemcolor1, .Femblemrotation, .Icontainerid, eaMembers[], .Bisownedbysystem");
enumTransactionOutcome aslGuild_tr_SetAdvancedEmblem(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID, char *pcEmblem, U32 iEmblemColor0, U32 iEmblemColor1, F32 fEmblemRotation, int bHidePlayerFeedback)
{
	GUILD_TRANSACTION_INIT;
	if(!pGuildContainer->bIsOwnedBySystem)
	{
		NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);

		if (ISNULL(pMember)) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_SelfNotInGuild", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
		if (!aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_SetLook)) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_NoEmblemPermission", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
	}

	aslGuild_trh_SetAdvancedEmblem(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, pcEmblem, iEmblemColor0, iEmblemColor1, fEmblemRotation, bHidePlayerFeedback);
	GUILD_TRANSACTION_RETURN_SUCCESS;
}

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_SetAdvancedEmblem2(U32 iGuildID, U32 iEntID, char *pcEmblem2, U32 iEmblem2Color0, U32 iEmblem2Color1, F32 fEmblem2Rotation, F32 fEmblem2X, F32 fEmblem2Y, F32 fEmblem2ScaleX, F32 fEmblem2ScaleY)
{
	char pcActionType[] = "SetEmblem";
	ASLGuildCBData *pData = NULL;
	TransactionReturnVal *pReturn = NULL;
	Guild *pGuild = aslGuild_GetGuild(iGuildID);

	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsMember(pGuild, iEntID, pcActionType) &&
		aslGuild_CheckPermission(pGuild, iEntID, GuildPermission_SetLook, false, pcActionType) &&
		aslGuild_CheckEmblemType(pGuild, iEntID, pcEmblem2, false, false, pcActionType)) {

			pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
			AutoTrans_aslGuild_tr_SetAdvancedEmblem2(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, iEntID, pcEmblem2, iEmblem2Color0, iEmblem2Color1, fEmblem2Rotation, fEmblem2X, fEmblem2Y, fEmblem2ScaleX, fEmblem2ScaleY);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Pcname, .Earanks, .Pcemblem2, .Iemblem2color0, .Iemblem2color1, .Femblem2rotation, .Femblem2x, .Femblem2y, .Femblem2scalex, .Femblem2scaley, .Icontainerid, eaMembers[]");
enumTransactionOutcome aslGuild_tr_SetAdvancedEmblem2(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID, char *pcEmblem2, U32 iEmblem2Color0, U32 iEmblem2Color1, F32 fEmblem2Rotation, F32 fEmblem2X, F32 fEmblem2Y, F32 fEmblem2ScaleX, F32 fEmblem2ScaleY)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);

	if (ISNULL(pMember)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (!aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_SetLook)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_NoEmblemPermission", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	aslGuild_trh_SetAdvancedEmblem2(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, pcEmblem2, iEmblem2Color0, iEmblem2Color1, fEmblem2Rotation, fEmblem2X, fEmblem2Y, fEmblem2ScaleX, fEmblem2ScaleY);
	GUILD_TRANSACTION_RETURN_SUCCESS;
}

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_SetAdvancedEmblem3(U32 iGuildID, U32 iEntID, char *pcEmblem3, bool bHidePlayerFeedback)
{
	char pcActionType[] = "SetEmblem";
	ASLGuildCBData *pData = NULL;
	TransactionReturnVal *pReturn = NULL;
	Guild *pGuild = aslGuild_GetGuild(iGuildID);

	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsMember(pGuild, iEntID, pcActionType) &&
		aslGuild_CheckPermission(pGuild, iEntID, GuildPermission_SetLook, false, pcActionType) &&
		aslGuild_CheckEmblemType(pGuild, iEntID, pcEmblem3, true, true, pcActionType)) {

			pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
			AutoTrans_aslGuild_tr_SetAdvancedEmblem3(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, iEntID, pcEmblem3, bHidePlayerFeedback);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Pcname, .Earanks, .Pcemblem3, .Icontainerid, eaMembers[]");
enumTransactionOutcome aslGuild_tr_SetAdvancedEmblem3(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID, char *pcEmblem3, int bHidePlayerFeedback)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);

	if (ISNULL(pMember)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (!aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_SetLook)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_NoEmblemPermission", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	aslGuild_trh_SetAdvancedEmblem3(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, pcEmblem3, bHidePlayerFeedback);
	GUILD_TRANSACTION_RETURN_SUCCESS;
}

AUTO_COMMAND_REMOTE;
void aslGuild_SetBadEmblem(U32 iGuildID, U32 iEntID)
{
	char pcActionType[] = "SetBadEmblem";
	ASLGuildCBData *pData = NULL;
	TransactionReturnVal *pReturn = NULL;
	Guild *pGuild = aslGuild_GetGuild(iGuildID);

	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsMember(pGuild, iEntID, pcActionType)) {
			pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
			AutoTrans_aslGuild_tr_SetBadEmblem(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, iEntID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Pguildbademblem, .Pcemblem, .Iemblemcolor0, .Iemblemcolor1, .Femblemrotation, .Pcemblem2, .Iemblem2color0, .Iemblem2color1, .Femblem2rotation, .Femblem2x, .Femblem2y, .Femblem2scalex, .Femblem2scaley, .Pcemblem3, .Icontainerid, .Pcname, .Earanks, eaMembers[]");
enumTransactionOutcome aslGuild_tr_SetBadEmblem(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID)
{
	GUILD_TRANSACTION_INIT;
	int i;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);

	if (ISNULL(pMember)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (!aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_SetLook)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_NoEmblemPermission", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	if (ISNULL(pGuildContainer->pGuildBadEmblem)) pGuildContainer->pGuildBadEmblem = StructCreateNoConst(parse_GuildBadEmblem);
	pGuildContainer->pGuildBadEmblem->pcEmblem = pGuildContainer->pcEmblem;
	pGuildContainer->pGuildBadEmblem->iEmblemColor0 = pGuildContainer->iEmblemColor0;
	pGuildContainer->pGuildBadEmblem->iEmblemColor1 = pGuildContainer->iEmblemColor1;
	pGuildContainer->pGuildBadEmblem->fEmblemRotation = pGuildContainer->fEmblemRotation;
	pGuildContainer->pGuildBadEmblem->pcEmblem2 = pGuildContainer->pcEmblem2;
	pGuildContainer->pGuildBadEmblem->iEmblem2Color0 = pGuildContainer->iEmblem2Color0;
	pGuildContainer->pGuildBadEmblem->iEmblem2Color1 = pGuildContainer->iEmblem2Color1;
	pGuildContainer->pGuildBadEmblem->fEmblem2Rotation = pGuildContainer->fEmblem2Rotation;
	pGuildContainer->pGuildBadEmblem->fEmblem2X = pGuildContainer->fEmblem2X;
	pGuildContainer->pGuildBadEmblem->fEmblem2Y = pGuildContainer->fEmblem2Y;
	pGuildContainer->pGuildBadEmblem->fEmblem2ScaleX = pGuildContainer->fEmblem2ScaleX;
	pGuildContainer->pGuildBadEmblem->fEmblem2ScaleY = pGuildContainer->fEmblem2ScaleY;
	pGuildContainer->pGuildBadEmblem->pcEmblem3 = pGuildContainer->pcEmblem3;

	pGuildContainer->pcEmblem = NULL;
	for (i = eaSize(&g_GuildEmblems.eaEmblems)-1; i >= 0; i--) {
		if (g_GuildEmblems.eaEmblems[i]->bFalse)
		{
			pGuildContainer->pcEmblem = REF_STRING_FROM_HANDLE(g_GuildEmblems.eaEmblems[i]->hTexture);
			break;
		}
	}
	if ((!pGuildContainer->pcEmblem) && (!gConf.bAllowGuildstoHaveNoEmblems) && eaSize(&g_GuildEmblems.eaEmblems)) pGuildContainer->pcEmblem = REF_STRING_FROM_HANDLE(g_GuildEmblems.eaEmblems[0]->hTexture);
	pGuildContainer->iEmblemColor0 = 0;
	pGuildContainer->iEmblemColor1 = 0;
	pGuildContainer->fEmblemRotation = 0;
	pGuildContainer->pcEmblem2 = NULL;
	pGuildContainer->iEmblem2Color0 = 0;
	pGuildContainer->iEmblem2Color1 = 0;
	pGuildContainer->fEmblem2Rotation = 0;
	pGuildContainer->fEmblem2X = 0;
	pGuildContainer->fEmblem2Y = 0;
	pGuildContainer->fEmblem2ScaleX = 1;
	pGuildContainer->fEmblem2ScaleY = 1;
	pGuildContainer->pcEmblem3 = NULL;

	aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "BadGuildEmblem Applied");

	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set the Guild colors
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_SetColors(U32 iGuildID, U32 iEntID, U32 iColor, bool bIsColor1, bool bSystemControlled)
{
	char pcActionType[] = "SetColors";
	TransactionReturnVal *pReturn = NULL;
	Guild *pGuild = aslGuild_GetGuild(iGuildID);
	
	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		(bSystemControlled || 
		(aslGuild_CheckIsMember(pGuild, iEntID, pcActionType) &&
		aslGuild_CheckPermission(pGuild, iEntID, GuildPermission_SetLook, false, pcActionType)))) {
		
		pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
		if (bIsColor1) {
			AutoTrans_aslGuild_tr_SetColors(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, iEntID, iColor, pGuild->iColor2);
		} else {
			AutoTrans_aslGuild_tr_SetColors(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, iEntID, pGuild->iColor1, iColor);
		}
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Pcname, .Earanks, .Icolor1, .Icolor2, .Icontainerid, eaMembers[], .Bisownedbysystem");
enumTransactionOutcome aslGuild_tr_SetColors(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID, U32 iColor1, U32 iColor2)
{
	GUILD_TRANSACTION_INIT;
	if(!pGuildContainer->bIsOwnedBySystem)
	{
		NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);

		if (ISNULL(pMember)) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_SelfNotInGuild", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
		if (!aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_SetLook)) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_NoColorPermission", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
	}
	
	aslGuild_trh_SetColors(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, iColor1, iColor2);
	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Add Costume to the Guild
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK ACMD_IFDEF(GAMESERVER);
void aslGuild_AddCostume(U32 iGuildID, U32 iEntID, PlayerCostume *pCostume)
{
	char pcActionType[] = "AddCostume";
	TransactionReturnVal *pReturn = NULL;
	Guild *pGuild = aslGuild_GetGuild(iGuildID);

	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsMember(pGuild, iEntID, pcActionType) &&
		aslGuild_CheckPermission(pGuild, iEntID, GuildPermission_SetLook, false, pcActionType)) {

			pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
			AutoTrans_aslGuild_tr_AddCostume(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, iEntID, pCostume);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Eauniforms, .Icontainerid, .Pcname, .Earanks, eaMembers[]");
enumTransactionOutcome aslGuild_tr_AddCostume(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID, NON_CONTAINER PlayerCostume *pCostume)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	NOCONST(GuildCostume) *pUniform;

	if (ISNULL(pMember)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (!aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_SetLook)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_NoAddCostumePermission", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	pUniform = StructCreateNoConst(parse_GuildCostume);
	pUniform->pCostume = StructCloneDeConst(parse_PlayerCostume, pCostume);
	eaPush(&pGuildContainer->eaUniforms,pUniform);

	aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_UniformAdded", false);
	aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Uniform Added");

	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Remove Costume from the Guild
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_DeleteCostume(U32 iGuildID, U32 iEntID, int iIndex)
{
	char pcActionType[] = "DeleteCostume";
	TransactionReturnVal *pReturn = NULL;
	Guild *pGuild = aslGuild_GetGuild(iGuildID);

	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsMember(pGuild, iEntID, pcActionType) &&
		aslGuild_CheckPermission(pGuild, iEntID, GuildPermission_SetLook, false, pcActionType)) {

			pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
			AutoTrans_aslGuild_tr_DeleteCostume(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, iEntID, iIndex);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Eauniforms, .Icontainerid, .Pcname, .Earanks, eaMembers[]");
enumTransactionOutcome aslGuild_tr_DeleteCostume(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID, int iIndex)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);

	if (ISNULL(pMember)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (!aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_SetLook)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_NoAddCostumePermission", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	if (iIndex >= 0 && iIndex < eaSize(&pGuildContainer->eaUniforms))
	{
		StructDestroyNoConst(parse_GuildCostume, pGuildContainer->eaUniforms[iIndex]);
		eaRemove(&pGuildContainer->eaUniforms, iIndex);
	}
	else
	{
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_UniformDeleted", false);
	aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Uniform Deleted");

	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set a player to the maximum rank
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE;
void aslGuild_MakeLeader(U32 iGuildID, U32 iEntID)
{
	char pcActionType[] = "Promote";
	TransactionReturnVal *pReturn = NULL;
	Guild *pGuild = aslGuild_GetGuild(iGuildID);
	
	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsMember(pGuild, iEntID, pcActionType)) {
		
		pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
		AutoTrans_aslGuild_tr_MakeLeader(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, iEntID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Pcname, .Earanks, .Icontainerid, eaMembers[]");
enumTransactionOutcome aslGuild_tr_MakeLeader(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	
	if (ISNULL(pMember)) {
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	
	pMember->iRank = eaSize(&pGuildContainer->eaRanks)-1;

	aslGuild_trh_AddActivity(ATR_GUILD_RETURN, pGuildContainer, iEntID, kActivityType_GuildRankChange, NULL, pMember->iRank);

	aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iEntID, "GuildServer_Promoted", false);
	aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, iEntID, "GuildServer_Subject_Promoted", false);
	aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pMember->pcLogName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Promoted %s to rank %d.", pMember->pcLogName, pMember->iRank);
	aslGuild_PushMemberAddToChatServer(pGuildContainer->iContainerID, pGuildContainer->pcName, pMember->iAccountID,
		pMember->iRank, (const GuildCustomRank *const *)pGuildContainer->eaRanks);

	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Update a guild member's data
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE;
void aslGuild_UpdateInfo(U32 iGuildID, U32 iEntID, U32 iLevel, S32 iOfficerRank, const char *pcMapName, const char* pcMapMsgKey, const char *pcMapVars, U32 iInstanceNumber, int eLFGMode, const char *pcStatus, const char *pchClassName, bool bOnline)
{
	char pcActionType[] = "Update";
	Guild *pGuild = aslGuild_GetGuild(iGuildID);
	
	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsMember(pGuild, iEntID, pcActionType)) {

		AutoTrans_aslGuild_tr_UpdateInfo(NULL, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, iEntID, iLevel, iOfficerRank, pcMapName, pcMapMsgKey, pcMapVars, iInstanceNumber, eLFGMode, pcStatus, pchClassName, bOnline);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Earanks, .Pcname, .Iversion, eaMembers[]");
enumTransactionOutcome aslGuild_tr_UpdateInfo(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID, U32 iLevel, S32 iOfficerRank, const char *pcMapName, const char *pcMapMsgKey, const char *pcMapVars, U32 iInstanceNumber, int eLFGMode, const char *pcStatus, const char *pchClassName, U8 bOnline)
{
	GUILD_TRANSACTION_INIT;
	int i, j;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);
	
	if (ISNULL(pMember)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	
	if (iLevel > 0) {
		aslGuild_trh_SetLevel(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, iLevel);
	}
	if ((pcMapVars && pcMapVars[0]) || (pcMapName && pcMapName[0]) || iInstanceNumber) {
		aslGuild_trh_SetLocation(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, pcMapName, pcMapMsgKey, pcMapVars, iInstanceNumber);
	}
	if (eLFGMode != -1)
	{
		aslGuild_trh_SetLFGStatus(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, (U32)eLFGMode);
	}
	if (pcStatus) {
		aslGuild_trh_SetStatus(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, pcStatus);
	}
	aslGuild_trh_SetOnline(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, bOnline);
	if (pchClassName && *pchClassName) pMember->pchClassName = allocAddString(pchClassName);

	pMember->iOfficerRank = iOfficerRank;
	i = eaSize(&pGuildContainer->eaRanks);
	j = eaSize(&g_GuildRanks.eaRanks);
	if (i && j && i == j && pMember->iRank == i - 1)
	{
		// Make sure guild leader rank always has all permissions
		GuildRank *pRank = g_GuildRanks.eaRanks[i - 1];
		NOCONST(GuildCustomRank) *pGuildRank = pGuildContainer->eaRanks[i - 1];
		pGuildRank->ePerms = pRank->ePerms;
	}
	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Update a guild member's name and account
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE;
void aslGuild_UpdateName(U32 iGuildID, U32 iEntID)
{
	char pcActionType[] = "Update";
	Guild *pGuild = aslGuild_GetGuild(iGuildID);
	
	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsMember(pGuild, iEntID, pcActionType)) {
		
 		AutoTrans_aslGuild_tr_UpdateName(NULL, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, GLOBALTYPE_ENTITYPLAYER, iEntID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Eamembers, .Pcname, .Iversion")
ATR_LOCKS(pEnt, ".Psaved.Savedname, .Pplayer.Publicaccountname");
enumTransactionOutcome aslGuild_tr_UpdateName(ATR_ARGS, NOCONST(Guild) *pGuildContainer, NOCONST(Entity) *pEnt)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, pEnt->myContainerID);
	
	if (ISNULL(pMember)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, pEnt->myContainerID, pEnt->myContainerID, 0, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	
	aslGuild_trh_SetMemberName(GUILD_TRH_PASS_ARGS, pGuildContainer, pEnt);
	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// CSR - Set the guild name
///////////////////////////////////////////////////////////////////////////////////////////

static void aslGuild_SetNameCheckNameCB(TransactionReturnVal *returnVal, GuildNameCheckData *guildData)
{
	char pcActionType[] = "Rename";
	enumTransactionOutcome eOutcome;
	int iResult;
		
	eOutcome = RemoteCommandCheck_ChatServerReserveGuildName(returnVal, &iResult);
	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		switch (iResult)
		{
		case 0:
			aslGuild_SendFeedback(guildData->iEntID, guildData->iEntID, 0, guildData->pcName, pcActionType, "GuildServer_NameTaken", true);
			break;
		case -1:
			aslGuild_SendFeedback(guildData->iEntID, guildData->iEntID, 0, guildData->pcName, pcActionType, "GuildServer_CannotValidateGuildName", true);
			break;
		default:
			{
				TransactionReturnVal *pReturn = objCreateManagedReturnVal(aslGuild_Rename_CB, aslGuild_MakeCBData(pcActionType));
				AutoTrans_aslGuild_tr_SetName(pReturn, GetAppGlobalType(), guildData->iEntID, GLOBALTYPE_GUILD, guildData->iGuildID, guildData->pcName);
			}
		}
	}
	else
	{
		aslGuild_SendFeedback(guildData->iEntID, guildData->iEntID, 0, guildData->pcName, pcActionType, "GuildServer_CannotValidateGuildName", true);
	}
	SAFE_FREE(guildData->pcName);
	free(guildData);
}

static void aslGuild_SetNameInternal(SA_PARAM_NN_VALID Guild *pGuild, U32 iEntID, const char *pcNewName, ContainerID iVirtualShardID)
{
	char pcActionType[] = "Rename";

	if (aslGuild_CheckExistsNoValidate(pGuild, iEntID, pcActionType) &&
		aslGuild_CheckName(pGuild, iEntID, pcNewName, pcActionType, iVirtualShardID)) {
		
		if (ShardCommon_GetClusterName())
		{
			GuildNameCheckData *data = malloc(sizeof(GuildNameCheckData));
			data->iGuildID = pGuild->iContainerID;
			data->iEntID = iEntID;
			data->pcName = StructAllocString(pcNewName);

			RemoteCommand_ChatServerReserveGuildName(objCreateManagedReturnVal(aslGuild_SetNameCheckNameCB, data),
				GLOBALTYPE_CHATSERVER, 0, data->pcName);
		}
		else
 			AutoTrans_aslGuild_tr_SetName(NULL, GetAppGlobalType(), iEntID, GLOBALTYPE_GUILD, pGuild->iContainerID, (char*)pcNewName);
	}
}

AUTO_COMMAND_REMOTE;
void aslGuild_SetName(U32 iEntID, const char *pcOldName, const char *pcNewName, ContainerID iVirtualShardID)
{
	Guild *pGuild = aslGuild_GetGuildByName(pcOldName, iVirtualShardID);
	if (pGuild)
		aslGuild_SetNameInternal(pGuild, iEntID, pcNewName, iVirtualShardID);
}

AUTO_COMMAND_REMOTE;
void aslGuild_SetNameByID(U32 iEntID, U32 iGuildID, const char *pcNewName, ContainerID iVirtualShardID)
{
	Guild *pGuild = aslGuild_GetGuild(iGuildID);
	if (pGuild)
		aslGuild_SetNameInternal(pGuild, iEntID, pcNewName, iVirtualShardID);
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Pcname, .Iversion, .Icontainerid, .Eamembers");
enumTransactionOutcome aslGuild_tr_SetName(ATR_ARGS, U32 iEntID, NOCONST(Guild) *pGuildContainer, const char *pcNewName)
{
	GUILD_TRANSACTION_INIT;
	if (pGuildContainer->pcName) {
		StructFreeString(pGuildContainer->pcName);
	}
	pGuildContainer->pcName = StructAllocString(pcNewName);
	aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_Renamed", false);
	aslGuild_trh_AddFeedbackAll(ATR_GUILD_RETURN, pGuildContainer, 0, iEntID, 0, "GuildServer_Guild_Renamed", NULL);
	aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, NULL, pGuildContainer->iContainerID, pGuildContainer->pcName, "CSR: Changed name to %s.", pcNewName);
	pGuildContainer->iVersion++;
	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// CSR - Purge the guild bank
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE;
void aslGuild_PurgeBank(U32 iEntID, const char *pcGuildName, ContainerID iVirtualShardID)
{
	char pcActionType[] = "PurgeBank";
	Guild *pGuild = aslGuild_GetGuildByName(pcGuildName, iVirtualShardID);
	
	if (aslGuild_CheckExistsNoValidate(pGuild, iEntID, pcActionType)) {
 		AutoTrans_aslGuild_tr_PurgeBank(NULL, GetAppGlobalType(), iEntID, GLOBALTYPE_GUILD, pGuild->iContainerID, GLOBALTYPE_ENTITYGUILDBANK, pGuild->iContainerID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Icontainerid, .Pcname")
ATR_LOCKS(pGuildBank, ".pInventoryV2.Ppinventorybags, .pInventoryV2.Pplitebags");
enumTransactionOutcome aslGuild_tr_PurgeBank(ATR_ARGS, U32 iEntID, NOCONST(Guild) *pGuildContainer, NOCONST(Entity) *pGuildBank)
{
	GUILD_TRANSACTION_INIT;
	if (inv_guildbank_trh_ClearAllBags(ATR_PASS_ARGS, pGuildBank)) {
		aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_BankPurged", false);
		GUILD_TRANSACTION_RETURN_SUCCESS;
	} else {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_BankPurgeFailed", true);
		aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, NULL, pGuildContainer->iContainerID, pGuildContainer->pcName, "CSR: Purged bank.");
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// CSR - Set the guild leader
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE;
void aslGuild_SetLeader(U32 iEntID, const char *pcGuildName, U32 iSubjectID, ContainerID iVirtualShardID)
{
	char pcActionType[] = "SetLeader";
	Guild *pGuild = aslGuild_GetGuildByName(pcGuildName, iVirtualShardID);
	
	if (aslGuild_CheckExistsNoValidate(pGuild, iEntID, pcActionType)) {
 		AutoTrans_aslGuild_tr_SetLeader(NULL, GetAppGlobalType(), iEntID, GLOBALTYPE_GUILD, pGuild->iContainerID, iSubjectID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Pcname, .Icontainerid, .Earanks[AO], eaMembers[]");
enumTransactionOutcome aslGuild_tr_SetLeader(ATR_ARGS, U32 iEntID, NOCONST(Guild) *pGuildContainer, U32 iSubjectID)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iSubjectID);
	
	if (ISNULL(pMember)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_NotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	
	pMember->iRank = eaSize(&pGuildContainer->eaRanks)-1;

	aslGuild_trh_AddActivity(ATR_GUILD_RETURN, pGuildContainer, iEntID, kActivityType_GuildRankChange, NULL, pMember->iRank);

	aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_SetLeader", false);
	aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, NULL, pGuildContainer->iContainerID, pGuildContainer->pcName, "CSR: Made %d leader.", pMember->pcLogName);
	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// CSR - Reset the guild ranks
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE;
void aslGuild_ResetRanks(U32 iEntID, const char *pcGuildName, U32 iSubjectID, ContainerID iVirtualShardID)
{
	char pcActionType[] = "ResetRanks";
	Guild *pGuild = aslGuild_GetGuildByName(pcGuildName, iVirtualShardID);
	
	if (aslGuild_CheckExistsNoValidate(pGuild, iEntID, pcActionType)) {
 		AutoTrans_aslGuild_tr_ResetRanks(NULL, GetAppGlobalType(), iEntID, GLOBALTYPE_GUILD, pGuild->iContainerID, iSubjectID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Eamembers, .Earanks, .Icontainerid, .Pcname");
enumTransactionOutcome aslGuild_tr_ResetRanks(ATR_ARGS, U32 iEntID, NOCONST(Guild) *pGuildContainer, U32 iSubjectID)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iSubjectID);
	S32 i;
	
	if (ISNULL(pMember)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_NotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	
	for (i = eaSize(&pGuildContainer->eaMembers)-1; i >= 0; i--) {
		pGuildContainer->eaMembers[i]->iRank = 0;
	}
	pMember->iRank = eaSize(&pGuildContainer->eaRanks)-1;
	
	for (i = 0; i < eaSize(&pGuildContainer->eaRanks); i++) {
		pGuildContainer->eaRanks[i]->pcDisplayName = NULL;
		pGuildContainer->eaRanks[i]->ePerms = g_GuildRanks.eaRanks[i]->ePerms;
	}
	
	aslGuild_trh_AddSuccessFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_SetLeader", false);
	aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, NULL, pGuildContainer->iContainerID, pGuildContainer->pcName, "CSR: Reset ranks. Made %s leader.", pMember->pcLogName);
	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// CSR - Get the guild member list
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE;
void aslGuild_Who(U32 iEntID, const char *pcGuildName, ContainerID iVirtualShardID)
{
	char pcActionType[] = "Who";
	Guild *pGuild = aslGuild_GetGuildByName(pcGuildName, iVirtualShardID);
	
	if (aslGuild_CheckExistsNoValidate(pGuild, iEntID, pcActionType)) {
		S32 i;
		char *estrMemberList = NULL;
		estrConcatf(&estrMemberList, "Member list of %s:\n", pGuild->pcName);
		for (i = eaSize(&pGuild->eaMembers)-1; i >= 0; i--) {
			GuildMember *pMember = pGuild->eaMembers[i];
			if (pMember->bOnline) {
				estrConcatf(&estrMemberList, "%s%s (Level %d, Rank %d)\n", pMember->pcName, pMember->pcAccount, pMember->iLevel, pMember->iRank);
			}
		}
		RemoteCommand_gslGuild_PrintInfo(GLOBALTYPE_ENTITYPLAYER, iEntID, iEntID, estrMemberList);
		estrDestroy(&estrMemberList);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// CSR - Get the guild member list
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE;
void aslGuild_Info(U32 iEntID, const char *pcGuildName, ContainerID iVirtualShardID)
{
	char pcActionType[] = "Info";
	Guild *pGuild = aslGuild_GetGuildByName(pcGuildName, iVirtualShardID);
	
	if (aslGuild_CheckExistsNoValidate(pGuild, iEntID, pcActionType)) {
		S32 i;
		char *estrMemberList = NULL;
		estrConcatf(&estrMemberList, "Member list of %s:\n", pGuild->pcName);
		for (i = eaSize(&pGuild->eaMembers)-1; i >= 0; i--) {
			GuildMember *pMember = pGuild->eaMembers[i];
			estrConcatf(&estrMemberList, "%s%s (%d-%s, joined %d)\n", pMember->pcName, pMember->pcAccount, pMember->iRank, pGuild->eaRanks[pMember->iRank]->pcName, pMember->iJoinTime);
		}
		RemoteCommand_gslGuild_PrintInfo(GLOBALTYPE_ENTITYPLAYER, iEntID, iEntID, estrMemberList);
		estrDestroy(&estrMemberList);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// CSR - Forcibly join a guild
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE;
void aslGuild_ForceJoin(U32 iEntID, const char *pcGuildName, ContainerID iVirtualShardID, const char *pchClassName)
{
	char pcActionType[] = "Join";
	TransactionReturnVal *pReturn = NULL;
	Guild *pGuild = aslGuild_GetGuildByName(pcGuildName, iVirtualShardID);

	if (aslGuild_CheckExistsNoValidate(pGuild, iEntID, pcActionType) &&
		aslGuild_CheckIsNotMember(pGuild, iEntID, pcActionType)) {

		pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
		AutoTrans_aslGuild_tr_ForceJoin(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, pGuild->iContainerID, GLOBALTYPE_ENTITYPLAYER, iEntID, pchClassName);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Earanks, .Ivirtualshardid, .Icontainerid, .Pcname, .Iversion, .Pguildstatsinfo, .Eamembers")
ATR_LOCKS(pEnt, ".Psaved.Savedname, .Pplayer.Publicaccountname, .Pplayer.Ivirtualshardid, .Pplayer.Pguild, .Pplayer.Accountid");
enumTransactionOutcome aslGuild_tr_ForceJoin(ATR_ARGS, NOCONST(Guild) *pGuildContainer, NOCONST(Entity) *pEnt, const char *pchClassName)
{
	GUILD_TRANSACTION_INIT;
	
	// Make sure that the player isn't already in a guild
	if (NONNULL(pEnt->pPlayer->pGuild) && pEnt->pPlayer->pGuild->iGuildID) {
		if (pEnt->pPlayer->pGuild->eState == GuildState_Member) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, pEnt->myContainerID, pEnt->myContainerID, 0, "GuildServer_AlreadyInGuild", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
		if (pEnt->pPlayer->pGuild->eState == GuildState_Invitee) {
			aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, pEnt->myContainerID, pEnt->myContainerID, 0, "GuildServer_AlreadyInvited", true);
			GUILD_TRANSACTION_RETURN_FAILURE;
		}
	}

	if ((ISNULL(pEnt->pPlayer) ? 0: pEnt->pPlayer->iVirtualShardID) != pGuildContainer->iVirtualShardID)
	{
		aslGuild_AddFailureLogMessage(ATR_GUILD_RETURN, pEnt->debugName, pGuildContainer->iContainerID, pGuildContainer->pcName, "Error: Wrong virtual shard.");
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, pEnt->myContainerID, pEnt->myContainerID, 0, "GuildServer_WrongVirtualShard", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	aslGuild_trh_AddMember(GUILD_TRH_PASS_ARGS, pGuildContainer, pEnt, 0, pchClassName);
	
	timingHistoryPush(gActionHistory);
	
	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Clear Player Guild Data
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Pguild.Iguildid, .Pplayer.Pguild.Pcinvitername");
enumTransactionOutcome aslGuild_tr_ClearPlayerGuild(ATR_ARGS, NOCONST(Entity) *pEnt)
{
	GUILD_TRANSACTION_INIT;
	aslGuild_trh_ClearPlayerGuild(GUILD_TRH_PASS_ARGS, pEnt);
	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Validation Commands
///////////////////////////////////////////////////////////////////////////////////////////
// These commands each validate a particular aspect of the guild, to be called when it's
// suspected that this may be incorrect. Each of these commands will correct any
// inconsistencies is finds.
//
// ValidateExists - Validates that the guild exists
// ValidateBankExists - Validates that the guild exists
// ValidateMember - Validates that the player and guild agree about the player's membership
// ValidateInvite - Validates that the player and guild agree about the player's invitation

void aslGuild_ValidateExists_CB(TransactionReturnVal *pReturn, ASLGuildCBData *pData)
{
	ContainerRef *pContainerRef = NULL;
	TransactionReturnVal *pNewReturn = NULL;
	
	if (RemoteCommandCheck_ContainerGetOwner(pReturn, &pContainerRef) == TRANSACTION_OUTCOME_FAILURE) {
		char pcName[32];
		sprintf(pcName, "?@?[%d]", pData->iEntID);
		pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, pData);
		AutoTrans_aslGuild_tr_ClearPlayerGuild(pReturn, GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, pData->iEntID);
		aslGuild_SaveLogMessage("ValidateExists", pcName, pData->iGuildID, "", "OUT OF SYNC ERROR: Player in a guild that doesn't exist. Fixing.");
	} else {
		StructDestroy(parse_ASLGuildCBData, pData);
	}
}

AUTO_COMMAND_REMOTE;
void aslGuild_ValidateExists(U32 iGuildID, U32 iEntID)
{
	ASLGuildCBData *pData = NULL;
	TransactionReturnVal *pReturn = NULL;
	Guild *pGuild = aslGuild_GetGuild(iGuildID);
	
	if (pGuild) {
		return;
	}
	
	pData = aslGuild_MakeCBData(NULL);
	pData->iGuildID = iGuildID;
	pData->iEntID = iEntID;
	pReturn = objCreateManagedReturnVal(aslGuild_ValidateExists_CB, pData);
	RemoteCommand_ContainerGetOwner(pReturn, GLOBALTYPE_GUILD, iGuildID);
}

void aslGuild_ValidateBankExists_CB(TransactionReturnVal *pReturn, ASLGuildCBData *pData)
{
	ContainerRef *pContainerRef = NULL;
	TransactionReturnVal *pNewReturn = NULL;
	
	if (RemoteCommandCheck_ContainerGetOwner(pReturn, &pContainerRef) == TRANSACTION_OUTCOME_FAILURE) {
		aslGuildBank_Create_Internal(pData->iGuildID);
	} else {
		StructDestroy(parse_ASLGuildCBData, pData);
	}
}

AUTO_COMMAND_REMOTE;
void aslGuild_ValidateBankExists(U32 iGuildID)
{
	ASLGuildCBData *pData = NULL;
	TransactionReturnVal *pReturn = NULL;
	Entity *pGuildBank = aslGuild_GetGuildBank(iGuildID);
	
	if (pGuildBank) {
		return;
	}
	
	pData = aslGuild_MakeCBData(NULL);
	pData->iGuildID = iGuildID;
	pReturn = objCreateManagedReturnVal(aslGuild_ValidateBankExists_CB, pData);
	RemoteCommand_ContainerGetOwner(pReturn, GLOBALTYPE_ENTITYGUILDBANK, iGuildID);
}

AUTO_COMMAND_REMOTE;
void aslGuild_ValidateMember(U32 iGuildID, U32 iEntID)
{
	ASLGuildCBData *pData = NULL;
	TransactionReturnVal *pReturn = NULL;
	Guild *pGuild = aslGuild_GetGuild(iGuildID);
	
	if (!iEntID) {
		return;
	}
	
	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, "ValidateMember")) {
		pData = aslGuild_MakeCBData("ValidateMember");
		pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, pData);
		AutoTrans_aslGuild_tr_ValidateMember(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, GLOBALTYPE_ENTITYPLAYER, iEntID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Icontainerid, .Pcname, .Iversion, .Eamembers")
ATR_LOCKS(pEnt, ".Pplayer.Pguild.Iguildid, .Pplayer.Pguild.Estate, .Pplayer.Pguild.Pcinvitername");
enumTransactionOutcome aslGuild_tr_ValidateMember(ATR_ARGS, NOCONST(Guild) *pGuildContainer, NOCONST(Entity) *pEnt) {
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, pEnt->myContainerID);
	
	// added check as there has been a case of null player
	if(ISNULL(pEnt) || ISNULL(pEnt->pPlayer))
	{
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	
	if (NONNULL(pMember)) {
		if (ISNULL(pEnt->pPlayer->pGuild) || pEnt->pPlayer->pGuild->iGuildID != pGuildContainer->iContainerID || pEnt->pPlayer->pGuild->eState != GuildState_Member) {
			eaFindAndRemove(&pGuildContainer->eaMembers, pMember);
			StructDestroyNoConst(parse_GuildMember, pMember);
			pGuildContainer->iVersion++;
			aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pEnt->debugName, pGuildContainer->iContainerID, pGuildContainer->pcName, "OUT OF SYNC ERROR: Player is not tagged as a member of this guild. Fixing.");
			GUILD_TRANSACTION_RETURN_SUCCESS;
		}
	} else if (NONNULL(pEnt->pPlayer->pGuild) && pEnt->pPlayer->pGuild->iGuildID == pGuildContainer->iContainerID && pEnt->pPlayer->pGuild->eState == GuildState_Member) {
		aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pEnt->debugName, pGuildContainer->iContainerID, pGuildContainer->pcName, "OUT OF SYNC ERROR: Guild does not have this player as a member. Fixing.");
		aslGuild_trh_ClearPlayerGuild(GUILD_TRH_PASS_ARGS, pEnt);
	}
	
	GUILD_TRANSACTION_RETURN_SUCCESS;
}

AUTO_COMMAND_REMOTE;
void aslGuild_ValidateInvite(U32 iGuildID, U32 iEntID)
{
	ASLGuildCBData *pData = NULL;
	TransactionReturnVal *pReturn = NULL;
	Guild *pGuild = aslGuild_GetGuild(iGuildID);
	
	if (!iEntID) {
		return;
	}
	
	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, "ValidateInvite")) {
		pData = aslGuild_MakeCBData("ValidateInvite");
		pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, pData);
		AutoTrans_aslGuild_tr_ValidateInvite(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, GLOBALTYPE_ENTITYPLAYER, iEntID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Icontainerid, .Pcname, .Iversion, .Eainvites")
ATR_LOCKS(pEnt, ".Pplayer.Pguild.Iguildid, .Pplayer.Pguild.Estate, .Pplayer.Pguild.Pcinvitername");
enumTransactionOutcome aslGuild_tr_ValidateInvite(ATR_ARGS, NOCONST(Guild) *pGuildContainer, NOCONST(Entity) *pEnt) {
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindInvite(GUILD_TRH_PASS_ARGS, pGuildContainer, pEnt->myContainerID);
	
	if (NONNULL(pMember)) {
		if (ISNULL(pEnt->pPlayer->pGuild) || pEnt->pPlayer->pGuild->iGuildID != pGuildContainer->iContainerID || pEnt->pPlayer->pGuild->eState != GuildState_Member) {
			eaFindAndRemove(&pGuildContainer->eaInvites, pMember);
			StructDestroyNoConst(parse_GuildMember, pMember);
			pGuildContainer->iVersion++;
			aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pEnt->debugName, pGuildContainer->iContainerID, pGuildContainer->pcName, "OUT OF SYNC ERROR: Player is not tagged as an invite to this guild. Fixing.");
			GUILD_TRANSACTION_RETURN_SUCCESS;
		}
	} else if (NONNULL(pEnt->pPlayer->pGuild) && pEnt->pPlayer->pGuild->iGuildID == pGuildContainer->iContainerID && pEnt->pPlayer->pGuild->eState == GuildState_Invitee) {
		aslGuild_AddSuccessLogMessage(ATR_GUILD_RETURN, pEnt->debugName, pGuildContainer->iContainerID, pGuildContainer->pcName, "OUT OF SYNC ERROR: Guild does not have this player as an invite. Fixing.");
		aslGuild_trh_ClearPlayerGuild(GUILD_TRH_PASS_ARGS, pEnt);
	}
	
	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Remove Deleted Player
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE;
void aslGuild_RemoveDeletedPlayer(U32 iGuildID, U32 iEntID)
{
	AutoTrans_aslGuild_tr_RemoveDeletedPlayer(NULL, GLOBALTYPE_GUILDSERVER, GLOBALTYPE_GUILD, iGuildID, iEntID);
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Eamembers");
enumTransactionOutcome aslGuild_tr_RemoveDeletedPlayer(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID)
{
	int i;
	for (i = eaSize(&pGuildContainer->eaMembers)-1; i >= 0; i--) {
		if (pGuildContainer->eaMembers[i]->iEntID == iEntID) {
			StructDestroyNoConst(parse_GuildMember, pGuildContainer->eaMembers[i]);
			eaRemove(&pGuildContainer->eaMembers, i);
			return TRANSACTION_OUTCOME_SUCCESS;
		}
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Get a guild ID by name
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE;
U32 aslGuild_GetIDByName(char *pcName, ContainerID iVirtualShardID)
{
	Guild *pGuild = aslGuild_GetGuildByName(pcName, iVirtualShardID);
	if (pGuild) {
		return pGuild->iContainerID;
	} else {
		return 0;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// Search guild request info
///////////////////////////////////////////////////////////////////////////////////////////

// Returns list of guilds that satisfy search request
AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
GuildRecruitInfoList* Guild_GetGuilds(GuildRecruitSearchRequest *pRequest)
{
	GuildRecruitInfoList *pFinalGuildList = StructCreate(parse_GuildRecruitInfoList);
	GuildRecruitParam *pParams = Guild_GetGuildRecruitParams();
	Guild *pGuild = NULL;
	ContainerStore *pStore = objFindContainerStoreFromType(GLOBALTYPE_GUILD);
	int iCurContainer, iStartContainer;
	GuildRecruitInfo *pGuildInfo;
	U32 i = 0, traverse = 0;
	int j, k;
	const char *pcName;

	if (!pStore) return NULL;
	objLockContainerStore_ReadOnly(pStore); // If we turn on locking for the guild server, we may want to see if we can rewrite this to lock less of the time.
	if (pStore->totalContainers<=0) 
	{
		objUnlockContainerStore_ReadOnly(pStore);
		return NULL;
	}

	iStartContainer = randInt(pStore->totalContainers);
	iCurContainer = iStartContainer;
	
	PERFINFO_AUTO_START_FUNC();

	for (i = 0; i < pParams->iMaxNumSearchResults; ++i)
	{
		// If we've seen at least one container and we're back to our start, then we processed everything
		if (traverse > 0 && iCurContainer==iStartContainer)
		{
			break;
		}

		// Check to see if we reached our traverse limit
		traverse++;
		if (traverse > pParams->iMaxNumSearchTraverse)
		{
			break;
		}

		// Get the guild info
		pGuildInfo = NULL;
		pGuild = (Guild *)pStore->containers[iCurContainer]->containerData;
		
		// Update curContainer for the next loop
		iCurContainer++;
		if (iCurContainer >= pStore->totalContainers)
		{
			iCurContainer = 0;
		}
		
		// Now start filtering the actual guild
		if ((!pGuild->pcName) ||
			pGuild->bHideRecruitMessage ||
			pRequest->iRequesterLevel < pGuild->iMinLevelRecruit ||
			pGuild->iVirtualShardID != pRequest->iRequesterVirtualShardID)
		{
			--i;
			continue;
		}

		// Find match
		if (eaSize(&pRequest->eaGuildIncludeSearchCat))
		{
			for (k = eaSize(&pRequest->eaGuildIncludeSearchCat)-1; k >= 0; --k)
			{
				pcName = pRequest->eaGuildIncludeSearchCat[k]->pcName;
				for (j = eaSize(&pGuild->eaRecruitCat)-1; j >= 0; --j)
				{
					if (pcName == pGuild->eaRecruitCat[j]->pcName)
					{
						break;
					}
				}
				if (j < 0) break;
			}
			if (k >= 0)
			{
				--i;
				continue;
			}
		}
		if (eaSize(&pRequest->eaGuildExcludeSearchCat))
		{
			for (k = eaSize(&pRequest->eaGuildExcludeSearchCat)-1; k >= 0; --k)
			{
				pcName = pRequest->eaGuildExcludeSearchCat[k]->pcName;
				for (j = eaSize(&pGuild->eaRecruitCat)-1; j >= 0; --j)
				{
					if (pcName == pGuild->eaRecruitCat[j]->pcName)
					{
						break;
					}
				}
				if (j >= 0) break;
			}
			if (k >= 0)
			{
				--i;
				continue;
			}
		}
		if (pRequest->stringSearch && *pRequest->stringSearch)
		{
			if (!strstri(pGuild->pcName,pRequest->stringSearch))
			{
				if ((!pGuild->pcRecruitMessage) || !strstri(pGuild->pcRecruitMessage,pRequest->stringSearch))
				{
					if ((!pGuild->pcWebSite) || !strstri(pGuild->pcWebSite,pRequest->stringSearch))
					{
						--i;
						continue;
					}
				}
			}
		}

		// Find members who are online and have invite permission
		{
			for (j = eaSize(&pGuild->eaMembers)-1; j >= 0; --j)
			{
				GuildRecruitMember *grm;
				GuildMember *gm = pGuild->eaMembers[j];
				if (!gm->bOnline) continue;
				if (!guild_HasPermission(gm->iRank, pGuild, GuildPermission_Invite)) continue;
				if (!pGuildInfo) pGuildInfo = StructCreate(parse_GuildRecruitInfo);
				if (!pGuild->bHideMembers)
				{
					grm = StructCreate(parse_GuildRecruitMember);
					grm->iLevel = gm->iLevel;
					grm->iOfficerRank = gm->iOfficerRank;
					grm->pcName = StructAllocString(gm->pcName);
					grm->pcAccount = StructAllocString(gm->pcAccount);
					grm->iRank = gm->iRank;
					if (gm->iRank >= 0 && gm->iRank < eaSize(&pGuild->eaRanks))
					{
						if (pGuild->eaRanks[gm->iRank]->pcDisplayName)
						{
							grm->pcRankName = StructAllocString(pGuild->eaRanks[gm->iRank]->pcDisplayName);
						}
						else
						{
							grm->pcRankName = StructAllocString(TranslateMessageKey(pGuild->eaRanks[gm->iRank]->pcDefaultNameMsg));
						}
					}
					eaPush(&pGuildInfo->eaMembers, grm);
				}
			}
		}

		if (!pGuildInfo)
		{
			--i;
			continue;
		}

		// Copy info
		pGuildInfo->iContainerID = pGuild->iContainerID;
		pGuildInfo->pcName = StructAllocString(pGuild->pcName);
		if (pGuild->pcRecruitMessage) pGuildInfo->pcRecruitMessage = StructAllocString(pGuild->pcRecruitMessage);
		if (pGuild->pcWebSite) pGuildInfo->pcWebSite = StructAllocString(pGuild->pcWebSite);
		if (pGuild->pcEmblem) pGuildInfo->pcEmblem = allocAddString(pGuild->pcEmblem);
		pGuildInfo->iEmblemColor0 = pGuild->iEmblemColor0;
		pGuildInfo->iEmblemColor1 = pGuild->iEmblemColor1;
		pGuildInfo->fEmblemRotation = pGuild->fEmblemRotation;
		if (pGuild->pcEmblem2) pGuildInfo->pcEmblem2 = allocAddString(pGuild->pcEmblem2);
		pGuildInfo->iEmblem2Color0 = pGuild->iEmblem2Color0;
		pGuildInfo->iEmblem2Color1 = pGuild->iEmblem2Color1;
		pGuildInfo->fEmblem2Rotation = pGuild->fEmblem2Rotation;
		pGuildInfo->fEmblem2X = pGuild->fEmblem2X;
		pGuildInfo->fEmblem2Y = pGuild->fEmblem2Y;
		pGuildInfo->fEmblem2ScaleX = pGuild->fEmblem2ScaleX;
		pGuildInfo->fEmblem2ScaleY = pGuild->fEmblem2ScaleY;
		if (pGuild->pcEmblem3) pGuildInfo->pcEmblem3 = allocAddString(pGuild->pcEmblem3);
		pGuildInfo->iColor1 = pGuild->iColor1;
		pGuildInfo->iColor2 = pGuild->iColor2;
		pGuildInfo->iMinLevelRecruit = pGuild->iMinLevelRecruit;
		for (j = eaSize(&pGuild->eaRecruitCat)-1; j >= 0; --j)
		{
			GuildRecruitSearchCat *grsc = StructCreate(parse_GuildRecruitSearchCat);
			if (!pGuild->eaRecruitCat[j]) continue;
			if (!pGuild->eaRecruitCat[j]->pcName) continue;
			if (!*pGuild->eaRecruitCat[j]->pcName) continue;
			grsc->pcName = allocAddString(pGuild->eaRecruitCat[j]->pcName);
			eaPush(&pGuildInfo->eaRecruitCat, grsc);
		}
		if (pGuild->pcAllegiance) pGuildInfo->pcGuildAllegiance = allocAddString(pGuild->pcAllegiance);

		eaPush(&pFinalGuildList->eaGuilds, pGuildInfo);
	}

	objUnlockContainerStore_ReadOnly(pStore);
	PERFINFO_AUTO_STOP();

	return pFinalGuildList;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set the Guild Recruit Message
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_SetRecruitMessage(U32 iGuildID, U32 iEntID, char *pcRecruitMessage)
{
	char pcActionType[] = "SetRecruitMessage";
	TransactionReturnVal *pReturn = NULL;
	Guild *pGuild = aslGuild_GetGuild(iGuildID);

	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsMember(pGuild, iEntID, pcActionType) &&
		aslGuild_CheckPermission(pGuild, iEntID, GuildPermission_SetRecruitInfo, false, pcActionType)) {

			pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
			AutoTrans_aslGuild_tr_SetRecruitMessage(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, iEntID, pcRecruitMessage);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Pcname, .Earanks, .Pcrecruitmessage, .Icontainerid, eaMembers[]");
enumTransactionOutcome aslGuild_tr_SetRecruitMessage(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID, char *pcRecruitMessage)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);

	if (ISNULL(pMember)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (!aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_SetRecruitInfo)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_NoRenamePermission", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	aslGuild_trh_SetRecruitMessage(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, pcRecruitMessage);

	timingHistoryPush(gActionHistory);

	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set the Guild Web Site
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_SetWebSite(U32 iGuildID, U32 iEntID, char *pcWebSite)
{
	char pcActionType[] = "SetWebSite";
	TransactionReturnVal *pReturn = NULL;
	Guild *pGuild = aslGuild_GetGuild(iGuildID);

	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsMember(pGuild, iEntID, pcActionType) &&
		aslGuild_CheckPermission(pGuild, iEntID, GuildPermission_SetRecruitInfo, false, pcActionType)) {

			pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
			AutoTrans_aslGuild_tr_SetWebSite(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, iEntID, pcWebSite);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Pcname, .Earanks, .Pcwebsite, .Icontainerid, eaMembers[]");
enumTransactionOutcome aslGuild_tr_SetWebSite(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID, char *pcWebSite)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);

	if (ISNULL(pMember)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (!aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_SetRecruitInfo)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_NoRenamePermission", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	aslGuild_trh_SetWebSite(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, pcWebSite);

	timingHistoryPush(gActionHistory);

	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set a Guild category
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_SetRecruitCat(U32 iGuildID, U32 iEntID, char *pcRecruitCat, int bSet)
{
	char pcActionType[] = "SetRecruitCat";
	TransactionReturnVal *pReturn = NULL;
	Guild *pGuild = aslGuild_GetGuild(iGuildID);

	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsMember(pGuild, iEntID, pcActionType) &&
		aslGuild_CheckPermission(pGuild, iEntID, GuildPermission_SetRecruitInfo, false, pcActionType)) {

			pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
			AutoTrans_aslGuild_tr_SetRecruitCat(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, iEntID, pcRecruitCat, bSet);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Pcname, .Earanks, .Earecruitcat, .Icontainerid, eaMembers[]");
enumTransactionOutcome aslGuild_tr_SetRecruitCat(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID, char *pcRecruitCat, int bSet)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);

	if (ISNULL(pMember)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (!aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_SetRecruitInfo)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_NoRenamePermission", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	aslGuild_trh_SetRecruitCat(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, pcRecruitCat, bSet);

	timingHistoryPush(gActionHistory);

	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set a Guild min recruit level
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_SetMinLevelRecruit(U32 iGuildID, U32 iEntID, int iMin)
{
	char pcActionType[] = "SetMinLevelRecruit";
	TransactionReturnVal *pReturn = NULL;
	Guild *pGuild = aslGuild_GetGuild(iGuildID);

	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsMember(pGuild, iEntID, pcActionType) &&
		aslGuild_CheckPermission(pGuild, iEntID, GuildPermission_SetRecruitInfo, false, pcActionType)) {

			pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
			AutoTrans_aslGuild_tr_SetMinLevelRecruit(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, iEntID, iMin);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Pcname, .Earanks, .Iminlevelrecruit, .Icontainerid, eaMembers[]");
enumTransactionOutcome aslGuild_tr_SetMinLevelRecruit(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID, int iMin)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);

	if (ISNULL(pMember)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (!aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_SetRecruitInfo)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_NoRenamePermission", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	aslGuild_trh_SetMinLevelRecruit(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, iMin);

	timingHistoryPush(gActionHistory);

	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set a Guild recruit visibility
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_SetRecruitVisibility(U32 iGuildID, U32 iEntID, int bShow)
{
	char pcActionType[] = "SetRecruitVisibility";
	TransactionReturnVal *pReturn = NULL;
	Guild *pGuild = aslGuild_GetGuild(iGuildID);

	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsMember(pGuild, iEntID, pcActionType) &&
		aslGuild_CheckPermission(pGuild, iEntID, GuildPermission_SetRecruitInfo, false, pcActionType)) {

			pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
			AutoTrans_aslGuild_tr_SetRecruitVisibility(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, iEntID, bShow);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Pcname, .Earanks, .Bhiderecruitmessage, .Icontainerid, eaMembers[]");
enumTransactionOutcome aslGuild_tr_SetRecruitVisibility(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID, int bShow)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);

	if (ISNULL(pMember)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (!aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_SetRecruitInfo)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_NoRenamePermission", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	aslGuild_trh_SetRecruitVisibility(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, bShow);

	timingHistoryPush(gActionHistory);

	GUILD_TRANSACTION_RETURN_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set a Guild recruit member visibility
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslGuild_SetRecruitMemberVisibility(U32 iGuildID, U32 iEntID, int bShow)
{
	char pcActionType[] = "SetRecruitMemberVisibility";
	TransactionReturnVal *pReturn = NULL;
	Guild *pGuild = aslGuild_GetGuild(iGuildID);

	if (aslGuild_CheckExists(pGuild, iGuildID, iEntID, pcActionType) &&
		aslGuild_CheckIsMember(pGuild, iEntID, pcActionType) &&
		aslGuild_CheckPermission(pGuild, iEntID, GuildPermission_SetRecruitInfo, false, pcActionType)) {

			pReturn = objCreateManagedReturnVal(aslGuild_RemoteCommand_CB, aslGuild_MakeCBData(pcActionType));
			AutoTrans_aslGuild_tr_SetRecruitMemberVisibility(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, iEntID, bShow);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Pcname, .Earanks, .Bhidemembers, .Icontainerid, eaMembers[]");
enumTransactionOutcome aslGuild_tr_SetRecruitMemberVisibility(ATR_ARGS, NOCONST(Guild) *pGuildContainer, U32 iEntID, int bShow)
{
	GUILD_TRANSACTION_INIT;
	NOCONST(GuildMember) *pMember = aslGuild_trh_FindMember(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID);

	if (ISNULL(pMember)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_SelfNotInGuild", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}
	if (!aslGuild_trh_HasPermission(GUILD_TRH_PASS_ARGS, pGuildContainer, pMember->iRank, GuildPermission_SetRecruitInfo)) {
		aslGuild_trh_AddFailureFeedback(ATR_GUILD_RETURN, pGuildContainer, iEntID, iEntID, 0, "GuildServer_NoRenamePermission", true);
		GUILD_TRANSACTION_RETURN_FAILURE;
	}

	aslGuild_trh_SetRecruitMemberVisibility(GUILD_TRH_PASS_ARGS, pGuildContainer, iEntID, bShow);

	timingHistoryPush(gActionHistory);

	GUILD_TRANSACTION_RETURN_SUCCESS;
}



//#include "AutoGen/EntitySavedData_h_ast.c"
#include "AutoGen/GuildTransactions_c_ast.c"

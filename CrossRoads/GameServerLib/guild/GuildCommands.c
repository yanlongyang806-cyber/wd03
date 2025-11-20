/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "StringCache.h"

#include "allegiance.h"
#include "Entity.h"
#include "entity_h_ast.h"
#include "gslChatConfig.h"
#include "gslEntity.h"
#include "gslMapTransfer.h"
#include "gslMapVariable.h"
#include "gslMechanics.h"
#include "gslPartition.h"
#include "gslTransactions.h"
#include "Guild.h"
#include "Team.h"
#include "GameServerLib.h"
#include "file.h"
#include "AutoTransDefs.h"
#include "WorldGrid.h"
#include "rand.h"
#include "logging.h"
#include "NotifyCommon.h"
#include "itemTransaction.h"
#include "GameAccountDataCommon.h"
#include "ServerLib.h"
#include "LoggedTransactions.h"

#include "Entity.h"
#include "EntityLib.h"
#include "EntitySavedData.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonTailor.h"
#include "CombatEnums.h"
#include "CharacterClass.h"
#include "SavedPetCommon.h"
#include "gslCostume.h"
#include "EntDebugMenu.h"
#include "OfficerCommon.h"
#include "Player.h"
#include "Player_h_ast.h"
#include "cmdServerCharacter.h"
#include "species_common.h"

#include "chatCommon.h"
#include "chatCommonStructs.h"
#include "gslChat.h"
#include "gslSendToClient.h"
#include "guildCommonStructs.h"

#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "Autogen/ChatServer_autogen_remotefuncs.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"
#include "AutoGen/guild_h_ast.h"
#include "AutoGen/chatCommon_h_ast.h"
#include "AutoGen/chatCommonStructs_h_ast.h"
#include "AutoGen/CostumeCommon_h_ast.h"

#include "EntityIterator.h"
#include "StringUtil.h"

#include "rewardCommon.h"
#include "itemCommon.h"
#include "StringFormat.h"
#include "GameStringFormat.h"
#include "GameServerLib.h"
#include "PowerTree.h"
#include "Character.h"
#include "CostumeCommonLoad.h"
#include "GamePermissionsCommon.h"

#define GUILD_MAP_KICK_LOGOUT_TIMER 10

GuildRankList g_GuildRanks;
GuildEmblemList g_GuildEmblems;
GuildConfig gGuildConfig;

typedef struct GSLGuildCBData
{
	EntityRef iRef;
	ContainerID iSubjectID;
	char pcErrorName[256];
	char pchString[256];
} GSLGuildCBData;

GSLGuildCBData *gslGuild_MakeCBData(EntityRef iRef, const char *pcErrorName, const char* pchString)
{
	GSLGuildCBData *pCBData = calloc(1, sizeof(GSLGuildCBData));
	pCBData->iRef = iRef;
	if (pcErrorName) {
		strcpy_s(pCBData->pcErrorName, 256, pcErrorName);
	}
	if (pchString) {
		S32 iBufferSize = ARRAY_SIZE(pCBData->pchString);
		strncpy(pCBData->pchString, pchString, iBufferSize-1);
		pCBData->pchString[iBufferSize-1] = '\0';
	}
	return pCBData;
}

void gslGuild_ServerDown(void *pData, void *pData2)
{
	Entity *pEnt = pData ? entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, (ContainerID)((intptr_t)(pData))) : NULL;
	
	if (pEnt)
	{
		char *pcErrorMsg = NULL;
		entFormatGameMessageKey(pEnt, &pcErrorMsg, "GuildServer_Offline", STRFMT_END);
		ClientCmd_NotifySend(pEnt, kNotifyType_GuildError, pcErrorMsg, NULL, NULL);
		estrDestroy(&pcErrorMsg);
	}
}

bool gslGuild_CheckRank(Entity *pEnt, S32 iRank)
{
	if ( guild_IsMember(pEnt) )
	{
		Guild *pGuild = GET_REF(pEnt->pPlayer->pGuild->hGuild);
		if ( pGuild != NULL )
		{
			if ( ( iRank >= 0 ) && ( iRank < eaSize(&pGuild->eaRanks) ) )
			{
				return true;
			}
		}
	}
	return false;
}

#define GUILD_SERVERDOWNHANDLER(pEnt) gslGuild_ServerDown, (void*)((intptr_t)(pEnt ? pEnt->myContainerID: 0)), NULL

AUTO_STARTUP(Guilds);
void gslGuild_LoadData(void)
{
	loadstart_printf("Loading Guild data...");
	ParserLoadFiles(NULL, "defs/config/GuildRanks.def", "GuildRanks.bin", PARSER_OPTIONALFLAG, parse_GuildRankList, &g_GuildRanks);
	ParserLoadFiles(NULL, "defs/config/GuildEmblems.def", "GuildEmblems.bin", PARSER_OPTIONALFLAG, parse_GuildEmblemList, &g_GuildEmblems);
	ParserLoadFiles(NULL, "defs/config/GuildConfig.def", "GuildConfig.bin", PARSER_OPTIONALFLAG, parse_GuildConfig, &gGuildConfig);
	guild_FixupRanks(&g_GuildRanks);
	loadend_printf(" done.");
}

///////////////////////////////////////////////////////////////////////////////////////////
// User output callbacks
///////////////////////////////////////////////////////////////////////////////////////////

typedef struct GSLGuildMessageCBData
{
	U32 iEntID;
	U32 iObjectID;
	U32 iSubjectID;
	char pcObjectName[MAX_NAME_LEN];
	char pcObjHandleName[MAX_NAME_LEN];
	char pcSubjectName[MAX_NAME_LEN];
	char pcGuildName[MAX_NAME_LEN];
	char pcErrorName[64];
	char pcMessageKey[64];
	bool bError;
} GSLGuildMessageCBData;

void gslGuild_SendResult(U32 iEntID, const char *pcObjectName, const char *pcObjHandleName, const char *pcSubjectName, const char *pcGuildName, const char *pcErrorName, const char *pcMessageKey, bool bError)
{
	static char pcErrorMessage[512];
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iEntID);
	if (pEnt && pcMessageKey && pcMessageKey[0]) {
		char *estrBuffer = NULL;
		char *estrErrorTypeBuffer = NULL;

		if ( ( pEnt->pPlayer != NULL ) && ( pEnt->pPlayer->pUI != NULL ) && 
			( pEnt->pPlayer->pUI->pChatState != NULL ) && ( pEnt->pPlayer->pUI->pChatState->eaIgnores != NULL )	)
		{
			int i;
			int n;
			n = eaSize(&pEnt->pPlayer->pUI->pChatState->eaIgnores);
			for ( i = 0; i < n; i++ )
			{
				if ( !stricmp(pEnt->pPlayer->pUI->pChatState->eaIgnores[i]->chatHandle, pcObjHandleName) )
				{
					return;
				}
			}
		}

		estrStackCreate(&estrErrorTypeBuffer);
		entFormatGameMessageKey(pEnt, &estrErrorTypeBuffer, pcErrorName, STRFMT_END);
		
		estrStackCreate(&estrBuffer);
		entFormatGameMessageKey(pEnt, &estrBuffer, pcMessageKey,
			STRFMT_STRING("ErrorType", estrErrorTypeBuffer),
			STRFMT_STRING("Subject", pcSubjectName),
			STRFMT_STRING("Object", pcObjectName),
			STRFMT_STRING("Guild", pcGuildName),
			STRFMT_END);
		
		ClientCmd_NotifySend(pEnt, bError ? kNotifyType_GuildError : kNotifyType_GuildFeedback, estrBuffer, NULL, NULL);
		
		estrDestroy(&estrErrorTypeBuffer);
		estrDestroy(&estrBuffer);
	}
}

void gslGuild_SendResultFromData(GSLGuildMessageCBData *pData) {
	gslGuild_SendResult(pData->iEntID, pData->pcObjectName, pData->pcObjHandleName, pData->pcSubjectName, pData->pcGuildName, pData->pcErrorName, pData->pcMessageKey, pData->bError);
	SAFE_FREE(pData);
}

static void gslGuild_ResultMessage_GetObjHandleName_CB(TransactionReturnVal *pReturn, GSLGuildMessageCBData *pData)
{
	char *pcName = NULL;
	enumTransactionOutcome eOutcome = RemoteCommandCheck_dbNameAndPublicAccountFromID(pReturn, &pcName);
	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS && pcName) {
		while (*pcName && *pcName != '@') ++pcName;
		if (*pcName)
		{
			++pcName;
			strcpy(pData->pcObjHandleName, pcName);
		}
	}
	if (!pData->pcObjHandleName[0]) {
		strcpy(pData->pcObjHandleName, "???");
	}
	gslGuild_SendResultFromData(pData);
}

static void gslGuild_ResultMessage_GetObjectName_CB(TransactionReturnVal *pReturn, GSLGuildMessageCBData *pData)
{
	char *pcName = NULL;
	TransactionReturnVal *pNewReturn;
	enumTransactionOutcome eOutcome = RemoteCommandCheck_dbNameFromID(pReturn, &pcName);
	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS) {
		strcpy(pData->pcObjectName, pcName);
	} else {
		strcpy(pData->pcObjectName, "???");
	}
	pNewReturn = objCreateManagedReturnVal(gslGuild_ResultMessage_GetObjHandleName_CB, pData);
	RemoteCommand_dbNameAndPublicAccountFromID(pNewReturn, GLOBALTYPE_OBJECTDB, 0, GLOBALTYPE_ENTITYPLAYER, pData->iObjectID);
}

static void gslGuild_ResultMessage_GetSubjectName_CB(TransactionReturnVal *pReturn, GSLGuildMessageCBData *pData)
{
	char *pcName = NULL;
	TransactionReturnVal *pNewReturn;
	enumTransactionOutcome eOutcome = RemoteCommandCheck_dbNameFromID(pReturn, &pcName);
	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS) {
		strcpy(pData->pcSubjectName, pcName);
	} else {
		strcpy(pData->pcSubjectName, "???");
	}
	pNewReturn = objCreateManagedReturnVal(gslGuild_ResultMessage_GetObjectName_CB, pData);
	RemoteCommand_dbNameFromID(pNewReturn, GLOBALTYPE_OBJECTDB, 0, GLOBALTYPE_ENTITYPLAYER, pData->iObjectID);
}

AUTO_COMMAND_REMOTE;
void gslGuild_ResultMessage(U32 iEntID, U32 iObjectID, U32 iSubjectID, const char *pcGuildName, const char *pcErrorName, const char *pcMessageKey, bool bError)
{
	Entity *pSubject = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iSubjectID);
	Entity *pObject = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iObjectID);
	if ((!iSubjectID || (iSubjectID && pSubject)) && (!iObjectID || (iObjectID && pObject))) {
		gslGuild_SendResult(iEntID, pObject ? entGetPersistedName(pObject) : "(null)", pObject ? entGetAccountOrLocalName(pObject) : "(null)", pSubject ? entGetPersistedName(pSubject) : "(null)", pcGuildName, pcErrorName, pcMessageKey, bError);
	} else {
		GSLGuildMessageCBData *pData;
		TransactionReturnVal *pReturn;
		
		pData = malloc(sizeof(GSLGuildMessageCBData));
		pData->iEntID = iEntID;
		pData->iObjectID = iObjectID;
		pData->iSubjectID = iSubjectID;
		if (pcGuildName) {
			strcpy(pData->pcGuildName, pcGuildName);
		} else {
			pData->pcGuildName[0] = 0;
		}
		if (pcErrorName) {
			strcpy(pData->pcErrorName, pcErrorName);
		} else {
			pData->pcErrorName[0] = 0;
		}
		if (pcMessageKey) {
			strcpy(pData->pcMessageKey, pcMessageKey);
		} else {
			pData->pcMessageKey[0] = 0;
		}
		pData->bError = bError;
		pData->pcSubjectName[0] = 0;
		pData->pcObjectName[0] = 0;
		pData->pcObjHandleName[0] = 0;
		if (!pSubject) {
			pReturn = objCreateManagedReturnVal(gslGuild_ResultMessage_GetSubjectName_CB, pData);
			RemoteCommand_dbNameFromID(pReturn, GLOBALTYPE_OBJECTDB, 0, GLOBALTYPE_ENTITYPLAYER, iSubjectID);
			return;
		}
		strcpy(pData->pcSubjectName, entGetPersistedName(pSubject));
		pReturn = objCreateManagedReturnVal(gslGuild_ResultMessage_GetObjectName_CB, pData);
		RemoteCommand_dbNameFromID(pReturn, GLOBALTYPE_OBJECTDB, 0, GLOBALTYPE_ENTITYPLAYER, iObjectID);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// Resolving player handles
///////////////////////////////////////////////////////////////////////////////////////////

static void gslGuild_ResolveHandleFailure_CB(Entity *pEnt, ContainerID uiPlayerID, U32 uiAccountID, U32 uiLoginServerID, GSLGuildCBData *pData)
{
	if (pEnt)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, "", pData->pcErrorName, "GuildServer_PlayerNotFound", true);
	}
	SAFE_FREE(pData);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Create guild
///////////////////////////////////////////////////////////////////////////////////////////

static const char *gslGuild_GetClassName(Entity *pEnt)
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

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_SERVERCMD ACMD_CATEGORY(Guild) ACMD_NAME(Guild_UpdateGuildStat);
void gslGuild_cmd_UpdateGuildStat(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_STR const char *pchStatName, GuildStatUpdateOperation eOperation, S32 iValue)
{
	if (pEnt &&
		guild_WithGuild(pEnt) &&
		pEnt->pPlayer->pGuild->eState == GuildState_Member)
	{
		AutoTrans_gslGuild_tr_UpdateGuildStat(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GUILD, pEnt->pPlayer->pGuild->iGuildID, pchStatName, eOperation, iValue);
	}	
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_SERVERCMD ACMD_CATEGORY(Guild) ACMD_NAME(Guild_ResetGuildStats);
void gslGuild_cmd_ResetGuildStats(SA_PARAM_NN_VALID Entity *pEnt)
{
	if (pEnt &&
		guild_WithGuild(pEnt) &&
		pEnt->pPlayer->pGuild->eState == GuildState_Member)
	{
		AutoTrans_gslGuild_tr_ResetGuildStats(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GUILD, pEnt->pPlayer->pGuild->iGuildID);
	}	
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_SERVERCMD ACMD_CATEGORY(Guild) ACMD_NAME(Guild_CreateTest);
void gslGuild_cmd_CreateTest(SA_PARAM_NN_VALID Entity *pEnt, U32 iColor1, U32 iColor2, char *pcEmblem, char *pcDescription, ACMD_SENTENCE pcName)
{
	if (guild_IsMember(pEnt)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_Create", "GuildServer_SelfAlreadyInGuild", true);
		return;
	}
	
	if (StringIsInvalidGuildName(pcName)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_Create", "GuildServer_InvalidGuildName", true);
		return;
	}

	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_Create(GLOBALTYPE_GUILDSERVER, 0, pEnt->myContainerID, team_GetTeamID(pEnt), pcName, REF_STRING_FROM_HANDLE(pEnt->hAllegiance), iColor1, iColor2, pcEmblem, pcDescription, gslGuild_GetClassName(pEnt), entGetVirtualShardID(pEnt), NULL, false, GUILD_SERVERDOWNHANDLER(pEnt));
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Guild) ACMD_NAME(Guild_CreateEx) ACMD_HIDE;
void gslGuild_cmd_CreateEx(SA_PARAM_NN_VALID Entity *pEnt, U32 iColor1, U32 iColor2, const char * pcEmblem, const char * pcDescription, const char * pchThemeName, ACMD_SENTENCE pcName)
{
	Team *pTeam;
	AllegianceDef *pGuildAllegiance = NULL;
	S32 i;

	if (guild_IsMember(pEnt)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_Create", "GuildServer_SelfAlreadyInGuild", true);
		return;
	}

	if (StringIsInvalidGuildName(pcName)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_Create", "GuildServer_InvalidGuildName", true);
		return;
	}

	pTeam = team_GetTeam(pEnt);
	if (entGetAccessLevel(pEnt) < ACCESS_DEBUG)
	{
		if (!pTeam) {
			gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_Create", "GuildServer_NotInTeam", true);
			return;
		}

		if (team_NumPresentMembers(pTeam) < TEAM_MAX_SIZE)
		{
			// Don't allow a guild unless the team has a full set of members who are actually present and logged in. Don't count
			//  disconnecteds for this.
			gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_Create", "GuildServer_TeamNotFull", true);
			return;
		}
	}

	if (pcDescription && strlen(pcDescription) > 2000)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_Create", "GuildServer_DescriptionTooLong", true);
		return;
	}

	// Validate the name passed to the function
	if (pchThemeName && pchThemeName[0] && RefSystem_ReferentFromString(g_GuildThemeDictionary, pchThemeName) == NULL)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_Create", "GuildServer_InvalidThemeName", true);
		return;
	}

	// check for permission for the creating ent
	if(pEnt)
	{
		if(gamePermission_Enabled() && !GamePermission_EntHasToken(pEnt, GAME_PERMISSION_CAN_CREATE_GUILD) )
		{
			gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_Create", "GuildServer_NoTrialInvites", true);
			return;
		}

		pGuildAllegiance = GET_REF(pEnt->hAllegiance);
		if (pGuildAllegiance && pGuildAllegiance->bCanBeSubAllegiance)
		{
			gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_Create", "GuildServer_InvalidAllegiance", true);
			return;
		}
	}

	if (pTeam)
	{
		for (i = eaSize(&pTeam->eaMembers)-1; i >= 0; i--) {
			if (pTeam->eaMembers[i]->iEntID != pEnt->myContainerID) {
				Entity *pMemberEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pTeam->eaMembers[i]->iEntID);
				if (!pMemberEnt) {
					gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_Create", "GuildServer_TeamMemberNotLocal", true);
					return;
				}
				if (guild_WithGuild(pMemberEnt)) {
					gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_Create", "GuildServer_TeamMemberWithGuild", true);
					return;
				}
				
				if (gConf.bEnforceGuildGuildMemberAllegianceMatch)
				{
					if (pGuildAllegiance) {
						AllegianceDef *pAllegiance = GET_REF(pMemberEnt->hAllegiance);
						if (pGuildAllegiance != pAllegiance) {
							gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_Create", "GuildServer_PlayerNotAllegiance", true);
							return;						
						}
					}
				}
				if(gamePermission_Enabled() && !GamePermission_EntHasToken(pMemberEnt, GAME_PERMISSION_CAN_CREATE_GUILD))
				{
					gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_Create", "GuildServer_NoTrialInvites", true);
					return;
				}
			}
		}
	}

	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_Create(GLOBALTYPE_GUILDSERVER, 0, pEnt->myContainerID, pTeam ? pTeam->iContainerID : 0, pcName, REF_STRING_FROM_HANDLE(pEnt->hAllegiance), iColor1, iColor2, pcEmblem, pcDescription, gslGuild_GetClassName(pEnt), entGetVirtualShardID(pEnt), pchThemeName, false, GUILD_SERVERDOWNHANDLER(pEnt));
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Guild) ACMD_NAME(Guild_Create) ACMD_HIDE;
void gslGuild_cmd_Create(SA_PARAM_NN_VALID Entity *pEnt, U32 iColor1, U32 iColor2, char *pcEmblem, char *pcDescription, ACMD_SENTENCE pcName)
{
	gslGuild_cmd_CreateEx(pEnt, iColor1, iColor2, pcEmblem, pcDescription, NULL, pcName);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Guild) ACMD_NAME(GuildJoinAuto) ACMD_HIDE;
void gslGuild_cmd_JoinAuto(SA_PARAM_NN_VALID Entity *pEntity)
{
	if(pEntity && pEntity->pPlayer && pEntity->pPlayer->iGuildID == 0 && gGuildConfig.pcAutoName && gGuildConfig.pcAutoName[0])
	{
		RemoteCommand_aslGuild_AddToAutoGuild(GLOBALTYPE_GUILDSERVER, 0, pEntity->myContainerID);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_SERVERCMD ACMD_CATEGORY(Guild) ACMD_NAME(GuildDestroy);
void gslGuild_cmd_Destroy(ContainerID iGuildID)
{
	RemoteCommand_aslGuild_Destroy(GLOBALTYPE_GUILDSERVER, 0, iGuildID, 0);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Invite to guild
///////////////////////////////////////////////////////////////////////////////////////////

static const char* gslGuild_GetInviteErrorString(Entity* pInvite, 
												 U32 iInviterID, 
												 U32 iInviterAccountID, 
												 ContainerID iInviterVirtualShardID,
												 const char* pchGuildAllegiance)
{
	NOCONST(ChatConfig) *pConfig;
	int i;

	if (!pInvite) {
		return "GuildServer_PlayerNotFound";
	}
	if(!entIsWhitelistedEx(pInvite, iInviterID, iInviterAccountID, kPlayerWhitelistFlags_Invites)) {
		return "GuildServer_PlayerIgnoring";
	}
	if (gamePermission_Enabled() && !GamePermission_EntHasToken(pInvite, GAME_PERMISSION_CAN_JOIN_GUILD)) {
		return "GuildServer_NoTrialInvites";
	}

	if (entGetVirtualShardID(pInvite) != iInviterVirtualShardID)
	{
		return "GuildServer_WrongVirtualShardID";
	}

	if (SAFE_MEMBER3(pInvite, pPlayer, pUI, bDisallowGuildInvites)) {
		return "GuildServer_PlayerIgnoring";
	}

	pConfig = ServerChatConfig_GetChatConfig(pInvite);
	if (pConfig) {
		// If the player is hidden, ignore guild invites
		if (pConfig->status & USERSTATUS_HIDDEN) {
			return "GuildServer_PlayerNotFound";
		}
		if (pConfig->status & USERSTATUS_FRIENDSONLY) {
			Team *pTeam = team_GetTeam(pInvite);
			bool bValidInvite = false;
			
			// Check the friend list, if it exists
			if (pInvite->pPlayer && pInvite->pPlayer->pUI && pInvite->pPlayer->pUI->pChatState) {
				for (i = eaSize(&pInvite->pPlayer->pUI->pChatState->eaFriends)-1; i >= 0; i--) {
					ChatPlayerStruct* pFriend = pInvite->pPlayer->pUI->pChatState->eaFriends[i];
					if (!ChatFlagIsFriend(pFriend->flags))
						continue;
					if (pFriend->accountID == iInviterAccountID) {
						bValidInvite = true;
						break;
					}
				}
			}

			// Check the team list, if they're in a team
			if (!bValidInvite && pTeam) {
				for (i = eaSize(&pTeam->eaMembers)-1; i >= 0; i--) {
					if (pTeam->eaMembers[i]->iEntID == iInviterID) {
						bValidInvite = true;
					}
				}
			}

			if (!bValidInvite) {
				return "GuildServer_PlayerIgnoring";
			}
		}
	}

	if (pInvite->pPlayer && pInvite->pPlayer->pUI->pChatState->eaIgnores)
	{
		for (i = eaSize(&pInvite->pPlayer->pUI->pChatState->eaIgnores)-1; i >= 0; i--)
		{
			if (pInvite->pPlayer->pUI->pChatState->eaIgnores[i]->accountID == iInviterAccountID)
			{
				return "GuildServer_PlayerIgnoring";
			}
		}
	}

	if (gConf.bEnforceGuildGuildMemberAllegianceMatch)
	{
		if (pchGuildAllegiance && pchGuildAllegiance[0])
		{
			const char* pchInviteeAllegiance = REF_STRING_FROM_HANDLE(pInvite->hAllegiance);
			if (!pchInviteeAllegiance || stricmp(pchInviteeAllegiance, pchGuildAllegiance)!=0)
			{
				return "GuildServer_PlayerNotAllegiance";
			}
		}
	}
	return NULL;
}

static void gslGuild_HandleInvite(SA_PARAM_NN_VALID Entity* pEnt, 
								  U32 iSubjectID, 
								  const char* pchErrorName, 
								  const char* pchErrStr)
{
	if (pchErrStr && pchErrStr[0] && stricmp("(null)", pchErrStr))
	{
		gslGuild_ResultMessage(pEnt->myContainerID,pEnt->myContainerID,0,"",pchErrorName,pchErrStr,true);
		return;
	}
	
	RemoteCommand_aslGuild_Invite(GLOBALTYPE_GUILDSERVER, 0, 
			guild_GetGuildID(pEnt), pEnt->myContainerID, 
			iSubjectID, GUILD_SERVERDOWNHANDLER(pEnt));
}

static void gslGuild_CanInvite_CB(TransactionReturnVal *pReturn, GSLGuildCBData *pData)
{
	char* pchErrStr = NULL;
	const char* pchErrorName = pData->pcErrorName;
	Entity *pEnt = entFromEntityRefAnyPartition(pData->iRef);
	ContainerID iSubjectID = pData->iSubjectID;
	enumTransactionOutcome eOutcome = RemoteCommandCheck_gslGuild_rcmd_CanInvite(pReturn, &pchErrStr);

	if (!pEnt)
	{
		SAFE_FREE(pData);
		return;
	}
	if (eOutcome != TRANSACTION_OUTCOME_SUCCESS)
	{
		//TODO: If the remote command fails, then the entity is offline. 
		// When this happens, fetch the ignore list from the chat server
		// and check to see if the inviter is on the invitee's ignore list
		//return; //fall through for now
	}
	gslGuild_HandleInvite(pEnt, iSubjectID, pchErrorName, pchErrStr); 
	SAFE_FREE(pData);
}

AUTO_COMMAND_REMOTE;
const char* gslGuild_rcmd_CanInvite(U32 iInviteeID, 
									U32 iInviterID, 
									U32 iInviterAccountID, 
									U32 iInviterVirtualShardID,
									const char* pchGuildAllegiance)
{
	Entity* pInvite = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iInviteeID);
	
	return gslGuild_GetInviteErrorString(pInvite, iInviterID, iInviterAccountID, iInviterVirtualShardID, pchGuildAllegiance);
}

static void gslGuild_cmd_Invite_CB(Entity *pEnt, ContainerID iSubjectID, U32 uiAccountID, U32 uiLoginServerID, GSLGuildCBData *pData)
{
	if (!pEnt || !pEnt->pPlayer) {
		SAFE_FREE(pData);
		return;
	}

	pData->iSubjectID = iSubjectID;

	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_gslGuild_rcmd_CanInvite(
		objCreateManagedReturnVal(gslGuild_CanInvite_CB, pData), 
		GLOBALTYPE_ENTITYPLAYER, iSubjectID, iSubjectID, 
		pEnt->myContainerID, pEnt->pPlayer->accountID, entGetVirtualShardID(pEnt), pData->pchString);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_Invite);
void gslGuild_cmd_Invite(SA_PARAM_NN_VALID Entity *pEnt, ACMD_SENTENCE pcOtherPlayer)
{
	static char pcErrorName[] = "GuildServer_ErrorType_Invite";
	Guild* pGuild;
	GSLGuildCBData *pData;
	if (gamePermission_Enabled() && !GamePermission_EntHasToken(pEnt, GAME_PERMISSION_CAN_INVITE_INTO_GUILD)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, "", pcErrorName, "GuildServer_NoTrialInvites", true);
		return;
	}
	pGuild = guild_GetGuild(pEnt);
	if (!pGuild)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, "", pcErrorName, "GuildServer_SelfNotInGuild", true);
		return;
	}
	pData = gslGuild_MakeCBData(entGetRef(pEnt), "GuildServer_ErrorType_Invite", pGuild->pcAllegiance);
	gslPlayerResolveHandle(pEnt, pcOtherPlayer, gslGuild_cmd_Invite_CB, gslGuild_ResolveHandleFailure_CB, pData);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Accept invite to guild
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_AcceptInvite);
void gslGuild_cmd_AcceptInvite(SA_PARAM_NN_VALID Entity *pEnt)
{
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_AcceptInvite(GLOBALTYPE_GUILDSERVER, 0, guild_GetGuildID(pEnt), pEnt->myContainerID, gslGuild_GetClassName(pEnt), GUILD_SERVERDOWNHANDLER(pEnt));
}

///////////////////////////////////////////////////////////////////////////////////////////
// Decline invite to guild
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_DeclineInvite);
void gslGuild_cmd_DeclineInvite(SA_PARAM_NN_VALID Entity *pEnt)
{
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_DeclineInvite(GLOBALTYPE_GUILDSERVER, 0, guild_GetGuildID(pEnt), pEnt->myContainerID, GUILD_SERVERDOWNHANDLER(pEnt));
}

///////////////////////////////////////////////////////////////////////////////////////////
// Promote guildmate
///////////////////////////////////////////////////////////////////////////////////////////

static void gslGuild_cmd_Promote_CB(Entity *pEnt, ContainerID iSubjectID, U32 uiAccountID, U32 uiLoginServerID, GSLGuildCBData *pData)
{
	static char pcErrorName[] = "GuildServer_ErrorType_Promote";
	ContainerID iGuildID;

	SAFE_FREE(pData);
	
	if (!pEnt) {
		return;
	}
	iGuildID = guild_GetGuildID(pEnt);
	if (!iGuildID) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, "", pcErrorName, "GuildServer_SelfNotInGuild", true);
		return;
	}
	
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_Promote(GLOBALTYPE_GUILDSERVER, 0, iGuildID, pEnt->myContainerID, iSubjectID, GUILD_SERVERDOWNHANDLER(pEnt));
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_Promote) ACMD_HIDE;
void gslGuild_cmd_Promote(SA_PARAM_NN_VALID Entity *pEnt, ACMD_SENTENCE pcOtherPlayer)
{
	GSLGuildCBData *pData = gslGuild_MakeCBData(entGetRef(pEnt), "GuildServer_ErrorType_Promote", NULL);
	gslPlayerResolveHandle(pEnt, pcOtherPlayer, gslGuild_cmd_Promote_CB, gslGuild_ResolveHandleFailure_CB, pData);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Demote guildmate
///////////////////////////////////////////////////////////////////////////////////////////

static void gslGuild_cmd_Demote_CB(Entity *pEnt, ContainerID iSubjectID, U32 uiAccountID, U32 uiLoginServerID, GSLGuildCBData *pData)
{
	static char pcErrorName[] = "GuildServer_ErrorType_Demote";
	ContainerID iGuildID;
	
	SAFE_FREE(pData);

	if (!pEnt) {
		return;
	}
	iGuildID = guild_GetGuildID(pEnt);
	if (!iGuildID) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, "", pcErrorName, "GuildServer_SelfNotInGuild", true);
		return;
	}
	
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_Demote(GLOBALTYPE_GUILDSERVER, 0, iGuildID, pEnt->myContainerID, iSubjectID, GUILD_SERVERDOWNHANDLER(pEnt));
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_Demote) ACMD_HIDE;
void gslGuild_cmd_Demote(SA_PARAM_NN_VALID Entity *pEnt, ACMD_SENTENCE pcOtherPlayer)
{
	GSLGuildCBData *pData = gslGuild_MakeCBData(entGetRef(pEnt), "GuildServer_ErrorType_Demote", NULL);
	gslPlayerResolveHandle(pEnt, pcOtherPlayer, gslGuild_cmd_Demote_CB, gslGuild_ResolveHandleFailure_CB, pData);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Leave a guild
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_Leave);
void gslGuild_cmd_Leave(SA_PARAM_NN_VALID Entity *pEnt)
{
	if (!guild_IsMember(pEnt)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_Leave", "GuildServer_SelfNotInGuild", true);
		return;
	}
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_Leave(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, pEnt->myContainerID, pEnt->myContainerID, GUILD_SERVERDOWNHANDLER(pEnt));
}

///////////////////////////////////////////////////////////////////////////////////////////
// Kick a player out of a guild
///////////////////////////////////////////////////////////////////////////////////////////

static void gslGuild_cmd_Kick_CB(Entity *pEnt, ContainerID iSubjectID, U32 uiAccountID, U32 uiLoginServerID, GSLGuildCBData *pData)
{
	static char pcErrorName[] = "GuildServer_ErrorType_Kick";
	ContainerID iGuildID;

	SAFE_FREE(pData);
	
	if (!pEnt) {
		return;
	}
	iGuildID = guild_GetGuildID(pEnt);
	if (!iGuildID) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, "", pcErrorName, "GuildServer_SelfNotInGuild", true);
		return;
	}
	
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_Leave(GLOBALTYPE_GUILDSERVER, 0, iGuildID, pEnt->myContainerID, iSubjectID, GUILD_SERVERDOWNHANDLER(pEnt));
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_Kick);
void gslGuild_cmd_Kick(SA_PARAM_NN_VALID Entity *pEnt, ACMD_SENTENCE pcOtherPlayer)
{
	GSLGuildCBData *pData = gslGuild_MakeCBData(entGetRef(pEnt), "GuildServer_ErrorType_Kick", NULL);
	gslPlayerResolveHandle(pEnt, pcOtherPlayer, gslGuild_cmd_Kick_CB, gslGuild_ResolveHandleFailure_CB, pData);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Rename a guild
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_Rename) ACMD_HIDE;
void gslGuild_cmd_Rename(SA_PARAM_NN_VALID Entity *pEnt, ACMD_SENTENCE pcNewName)
{
	if (StringIsInvalidGuildName(pcNewName)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_Rename", "GuildServer_InvalidGuildName", true);
		return;
	}
	
	if (!guild_IsMember(pEnt)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_Rename", "GuildServer_SelfNotInGuild", true);
		return;
	}
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_Rename(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, pEnt->myContainerID, pcNewName, GUILD_SERVERDOWNHANDLER(pEnt));
}

///////////////////////////////////////////////////////////////////////////////////////////
// Rename a guild rank
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_RenameRank) ACMD_HIDE;
void gslGuild_cmd_RenameRank(SA_PARAM_NN_VALID Entity *pEnt, S32 iRank, ACMD_SENTENCE pcNewName)
{
	if (StringIsInvalidGuildRankName(pcNewName)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_RenameRank", "GuildServer_InvalidRankName", true);
		return;
	}
	
	if (!guild_IsMember(pEnt)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_RenameRank", "GuildServer_SelfNotInGuild", true);
		return;
	}
	
	iRank--;
	if (!gslGuild_CheckRank(pEnt, iRank)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetPermission", "GuildServer_InvalidRank", true);
		return;
	}
	
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_RenameRank(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, pEnt->myContainerID, iRank, pcNewName, GUILD_SERVERDOWNHANDLER(pEnt));
}

///////////////////////////////////////////////////////////////////////////////////////////
// Rename a guild bank tab
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_RenameBankTab) ACMD_HIDE;
void gslGuild_cmd_RenameBankTab(SA_PARAM_NN_VALID Entity *pEnt, ACMD_NAMELIST(InvBagIDsEnum, STATICDEFINE) const char *pcBagName, ACMD_SENTENCE pcNewName)
{
	S32 iBagID = StaticDefineIntGetInt(InvBagIDsEnum, pcBagName);
	
	if (StringIsInvalidGuildBankTabName(pcNewName)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_RenameBankTab", "GuildServer_InvalidBankTabName", true);
		return;
	}
	
	if (!guild_IsMember(pEnt)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_RenameBankTab", "GuildServer_SelfNotInGuild", true);
		return;
	}
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_RenameBankTab(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, pEnt->myContainerID, iBagID, pcNewName, GUILD_SERVERDOWNHANDLER(pEnt));
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set a permission
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_SetPermission) ACMD_HIDE;
void gslGuild_cmd_SetPermission(SA_PARAM_NN_VALID Entity *pEnt, S32 iRank, ACMD_NAMELIST(GuildRankPermissionsEnum, STATICDEFINE) const char *pcPermission, bool bOn)
{
	GuildRankPermissions ePerms = StaticDefineIntGetInt(GuildRankPermissionsEnum, pcPermission);
	if (!ePerms) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetPermission", "GuildServer_InvalidPermission", true);
		return;
	}
	if (!guild_IsMember(pEnt)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetPermission", "GuildServer_SelfNotInGuild", true);
		return;
	}
	iRank--;
	if (!gslGuild_CheckRank(pEnt, iRank)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetPermission", "GuildServer_InvalidRank", true);
		return;
	}
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_SetPermission(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, pEnt->myContainerID, iRank, ePerms, bOn, GUILD_SERVERDOWNHANDLER(pEnt));
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set an Uniform permission
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_SetUniformPermission) ACMD_HIDE ACMD_PRODUCTS(StarTrek);
void gslGuild_cmd_SetUniformPermission(SA_PARAM_NN_VALID Entity *pEnt, const char *pcPermission, const char* pcUniform, const char* pcCategory, bool bOn)
{
	int i, iP = -1, iU = -1;
	PlayerCostume *pCostume = NULL;
	GuildCostume *pUniform = NULL;
	Guild *pGuild = guild_GetGuild(pEnt);

	if (!guild_IsMember(pEnt)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetPermission", "GuildServer_SelfNotInGuild", true);
		return;
	}

	for (i = eaSize(&pGuild->eaUniforms)-1; i >= 0; --i)
	{
		pCostume = pGuild->eaUniforms[i]->pCostume;
		if (!pCostume) continue;
		if (!pCostume->pcName) continue;
		if (!stricmp(pCostume->pcName, pcUniform))
		{
			pUniform = pGuild->eaUniforms[i];
			iU = i;
			break;
		}
	}
	if (!pUniform) return;

	//if (!stricmp("SpeciesNotAllowed",pcPermission))
	//{
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
	//	iP = 0;
	//}
	//else
	if (!stricmp("RanksNotAllowed",pcPermission))
	{
		char *e = (char *)pcCategory;
		int cat = strtol(pcCategory, &e, 10);
		iP = 1;
		if ( ( e == pcCategory ) || ( !gslGuild_CheckRank(pEnt, cat) ) ) {
			gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetPermission", "GuildServer_InvalidRank", true);
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
		iP = 1;
	}
	//else if (!stricmp("ClassNotAllowed",pcPermission))
	//{
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
	//	iP = 2;
	//}
	else
	{
		return;
	}

	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_SetUniformPermission(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, pEnt->myContainerID, iP, iU, pcCategory, bOn, GUILD_SERVERDOWNHANDLER(pEnt));
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set a bank permission
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_SetBankPermission) ACMD_HIDE;
void gslGuild_cmd_SetBankPermission(SA_PARAM_NN_VALID Entity *pEnt, ACMD_NAMELIST(InvBagIDsEnum, STATICDEFINE) const char *pcBagName, S32 iRank, ACMD_NAMELIST(GuildBankPermissionsEnum, STATICDEFINE) const char *pcPermission, bool bOn)
{
	S32 iBagID = StaticDefineIntGetInt(InvBagIDsEnum, pcBagName);
	GuildRankPermissions ePerms = StaticDefineIntGetInt(GuildBankPermissionsEnum, pcPermission);
	if (!ePerms) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetBankPermission", "GuildServer_InvalidPermission", true);
		return;
	}
	if (!guild_IsMember(pEnt)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetBankPermission", "GuildServer_SelfNotInGuild", true);
		return;
	}
	iRank--;
	if (!gslGuild_CheckRank(pEnt, iRank)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetPermission", "GuildServer_InvalidRank", true);
		return;
	}
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_SetBankPermission(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, pEnt->myContainerID, iBagID, iRank, ePerms, bOn, GUILD_SERVERDOWNHANDLER(pEnt));
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set a bank withdraw limit
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_SetBankWithdrawLimit) ACMD_HIDE;
void gslGuild_cmd_SetBankWithdrawLimit(SA_PARAM_NN_VALID Entity *pEnt, ACMD_NAMELIST(InvBagIDsEnum, STATICDEFINE) const char *pcBagName, S32 iRank, S32 iLimit)
{
	S32 iBagID = StaticDefineIntGetInt(InvBagIDsEnum, pcBagName);
	if (!guild_IsMember(pEnt)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetBankPermission", "GuildServer_SelfNotInGuild", true);
		return;
	}
	iRank--;
	if (!gslGuild_CheckRank(pEnt, iRank)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetPermission", "GuildServer_InvalidRank", true);
		return;
	}
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_SetBankWithdrawLimit(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, pEnt->myContainerID, iBagID, iRank, iLimit, GUILD_SERVERDOWNHANDLER(pEnt));
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_SetBankItemWithdrawLimit) ACMD_HIDE;
void gslGuild_cmd_SetBankItemWithdrawLimit(SA_PARAM_NN_VALID Entity *pEnt, ACMD_NAMELIST(InvBagIDsEnum, STATICDEFINE) const char *pcBagName, S32 iRank, S32 iCount)
{
	S32 iBagID = StaticDefineIntGetInt(InvBagIDsEnum, pcBagName);
	if (!guild_IsMember(pEnt)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetBankPermission", "GuildServer_SelfNotInGuild", true);
		return;
	}
	iRank--;
	if (!gslGuild_CheckRank(pEnt, iRank)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetPermission", "GuildServer_InvalidRank", true);
		return;
	}
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_SetBankItemWithdrawLimit(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, pEnt->myContainerID, iBagID, iRank, iCount, GUILD_SERVERDOWNHANDLER(pEnt));
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set guild MotD
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_SetMotD);
void gslGuild_cmd_SetMotD(SA_PARAM_NN_VALID Entity *pEnt, ACMD_SENTENCE pcMotD)
{
	if (!guild_IsMember(pEnt)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetMotD", "GuildServer_SelfNotInGuild", true);
		return;
	}
	if (pcMotD && strlen(pcMotD) > 1024)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetMotD", "GuildServer_MotDTooLong", true);
		return;
	}
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_SetMotD(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, pEnt->myContainerID, pcMotD, false, GUILD_SERVERDOWNHANDLER(pEnt));
}

///////////////////////////////////////////////////////////////////////////////////////////
// Show guild MotD
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Guild) ACMD_NAME(Guild_MotD);
void gslGuild_cmd_MotD(SA_PARAM_NN_VALID Entity *pEnt)
{
	char pcErrorName[] = "GuildServer_ErrorType_ShowMotD";
	Guild *pGuild = NULL;
	if (!guild_IsMember(pEnt)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, pcErrorName, "GuildServer_SelfNotInGuild", true);
		return;
	}
	pGuild = GET_REF(pEnt->pPlayer->pGuild->hGuild);
	if (!pGuild) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, pcErrorName, "GuildServer_NotLoaded", true);
		return;
	}
	
	if (pEnt) {
		ClientCmd_NotifySend(pEnt, kNotifyType_GuildMotD, pGuild->pcMotD, NULL, NULL);
	}
}

void gslGuild_SendEventReminder(Entity *pEnt, GuildEvent *pGuildEvent, char *messageKey)
{
	ClientCmd_gclGuild_SendEventReminder(pEnt, messageKey, pGuildEvent->pcTitle, pGuildEvent->iStartTimeTime);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set guild Description
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_SetDescription) ACMD_HIDE;
void gslGuild_cmd_SetDescription(SA_PARAM_NN_VALID Entity *pEnt, ACMD_SENTENCE pcDescription)
{
	if (!guild_IsMember(pEnt)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetDescription", "GuildServer_SelfNotInGuild", true);
		return;
	}
	if (pcDescription && strlen(pcDescription) > 2000)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetDescription", "GuildServer_DescriptionTooLong", true);
		return;
	}
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_SetDescription(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, pEnt->myContainerID, pcDescription, false, GUILD_SERVERDOWNHANDLER(pEnt));
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set personal public comment
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_SetPublicComment) ACMD_HIDE;
void gslGuild_cmd_SetPublicComment(SA_PARAM_NN_VALID Entity *pEnt, ACMD_SENTENCE pcComment)
{
	if (!guild_IsMember(pEnt)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetComment", "GuildServer_SelfNotInGuild", true);
		return;
	}
	if (pcComment && strlen(pcComment) > 256)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetComment", "GuildServer_CommentTooLong", true);
		return;
	}
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_SetPublicComment(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, pEnt->myContainerID, pcComment, GUILD_SERVERDOWNHANDLER(pEnt));
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set officer comment
///////////////////////////////////////////////////////////////////////////////////////////

static void gslGuild_cmd_SetOfficerComment_CB(Entity *pEnt, ContainerID iSubjectID, U32 uiAccountID, U32 uiLoginServerID, GSLGuildCBData *pData)
{
	static char pcErrorName[] = "GuildServer_ErrorType_SetComment";
	ContainerID iGuildID;

	if (!pEnt) {
		SAFE_FREE(pData);
		return;
	}
	iGuildID = guild_GetGuildID(pEnt);
	if (!iGuildID) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, "", pcErrorName, "GuildServer_SelfNotInGuild", true);
		SAFE_FREE(pData);
		return;
	}
	{
		Guild *pGuild = guild_GetGuild(pEnt);
		GuildMember *m = pGuild ? eaIndexedGetUsingInt(&pGuild->eaMembers, iSubjectID) : NULL;
		if (!m)
		{
			gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, "", pcErrorName, "GuildServer_PlayerNotFound", true);
			SAFE_FREE(pData);
			return;
		}
	}

	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_SetOfficerComment(GLOBALTYPE_GUILDSERVER, 0, iGuildID, pEnt->myContainerID, iSubjectID, pData->pchString, GUILD_SERVERDOWNHANDLER(pEnt));

	SAFE_FREE(pData);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_SetOfficerComment) ACMD_HIDE;
void gslGuild_cmd_SetOfficerComment(SA_PARAM_NN_VALID Entity *pEnt, char *pcOtherPlayer, ACMD_SENTENCE pcComment)
{
	if (!guild_IsMember(pEnt)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetComment", "GuildServer_SelfNotInGuild", true);
		return;
	}
	if (pcComment && strlen(pcComment) > 256)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetComment", "GuildServer_CommentTooLong", true);
		return;
	}
	{
		GSLGuildCBData *pData = gslGuild_MakeCBData(entGetRef(pEnt), "GuildServer_ErrorType_SetComment", pcComment);
		gslPlayerResolveHandle(pEnt, pcOtherPlayer, gslGuild_cmd_SetOfficerComment_CB, gslGuild_ResolveHandleFailure_CB, pData);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// Guild Event
///////////////////////////////////////////////////////////////////////////////////////////

bool gslGuild_ValidateGuildEventData(SA_PARAM_NN_VALID Entity *pEnt, GuildEventData *pGuildEventData)
{
	Guild *pGuild = guild_GetGuild(pEnt);
	GuildMember *pMember = guild_FindMember(pEnt);
	U32 iTimestamp = timeSecondsSince2000();

	if ((!pGuild) || (!pMember))
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_GuildEvent", "GuildServer_SelfNotInGuild", true);
		return false;
	}

	if (!pGuildEventData->pcTitle)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_GuildEvent", "GuildServer_NoTitle", true);
		return false;
	}

	if (strlen(pGuildEventData->pcTitle) > MAX_GUILD_EVENT_TITLE_LEN)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_GuildEvent", "GuildServer_TitleTooLong", true);
		return false;
	}

	if (pGuildEventData->pcDescription && strlen(pGuildEventData->pcDescription) > MAX_GUILD_EVENT_DESC_LEN)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_GuildEvent", "GuildServer_DescriptionTooLong", true);
		return false;
	}

	if (pGuildEventData->iStartTimeTime <= iTimestamp)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_GuildEvent", "GuildServer_StartTimeInPast", true);
		return false;
	}

	if (pGuildEventData->iDuration < 0 || pGuildEventData->iDuration > MAX_GUILD_EVENT_LEN)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_GuildEvent", "GuildServer_InvalidDuration", true);
		return false;
	}

	if (pGuildEventData->iStartTimeTime - iTimestamp > MAX_GUILD_EVENT_FUTURE_START || pGuildEventData->iRecurrenceCount > MAX_GUILD_EVENT_RECURR)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_GuildEvent", "GuildServer_InvalidTimes", true);
		return false;
	}

	if (pMember->iRank < pGuildEventData->iMinGuildRank || pMember->iRank < pGuildEventData->iMinGuildEditRank)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_GuildEvent", "GuildServer_InvalidRankRange", true);
		return false;
	}

	if (pGuildEventData->iMaxLevel < pGuildEventData->iMinLevel)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_GuildEvent", "GuildServer_InvalidLevelRange", true);
		return false;
	}

	if (eaSize(&pGuild->eaEvents) >= MAX_GUILD_EVENTS)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_GuildEvent", "GuildServer_TooManyEvents", true);
		return false;
	}

	return true;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_NewEvent) ACMD_PRIVATE;
void gslGuild_cmd_NewEvent(SA_PARAM_NN_VALID Entity *pEnt, GuildEventData *pGuildEventData)
{
	if (!gslGuild_ValidateGuildEventData(pEnt, pGuildEventData))
	{
		return;
	}

	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_NewGuildEvent(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, entGetContainerID(pEnt), pGuildEventData, GUILD_SERVERDOWNHANDLER(pEnt));
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_EditEvent) ACMD_PRIVATE;
void gslGuild_cmd_EditEvent(SA_PARAM_NN_VALID Entity *pEnt, GuildEventData *pGuildEventData)
{
	ContainerID iID = entGetContainerID(pEnt);
	Guild *pGuild = guild_GetGuild(pEnt);
	GuildMember *pMember = guild_FindMember(pEnt);
	GuildEvent *pGuildEvent = NULL;
	int index;

	if (!gslGuild_ValidateGuildEventData(pEnt, pGuildEventData))
	{
		return;
	}

	index = eaIndexedFindUsingInt(&pGuild->eaEvents, pGuildEventData->uiID);
	pGuildEvent = pGuild->eaEvents[index];

	if (!pGuildEvent)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_GuildEvent", "GuildServer_EventDoesNotExist", true);
		return;
	}

	if (pMember->iRank < pGuildEvent->iMinGuildEditRank)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_GuildEvent", "GuildServer_CantEdit", true);
		return;
	}

	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_EditGuildEvent(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, iID, pGuildEventData, GUILD_SERVERDOWNHANDLER(pEnt));
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_CancelEvent) ACMD_PRIVATE;
void gslGuild_cmd_CancelEvent(SA_PARAM_NN_VALID Entity *pEnt, U32 uiID)
{
	GuildMember *pMember = guild_FindMember(pEnt);
	GuildEvent *pGuildEvent = NULL;
	ContainerID iID = entGetContainerID(pEnt);
	Guild *pGuild = guild_GetGuild(pEnt);
	int index;

	if ((!pGuild) || (!pMember))
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_GuildEvent", "GuildServer_SelfNotInGuild", true);
		return;
	}

	index = eaIndexedFindUsingInt(&pGuild->eaEvents, uiID);
	pGuildEvent = pGuild->eaEvents[index];

	if (!pGuildEvent)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_GuildEvent", "GuildServer_NoTitleFound", true);
		return;
	}

	if (pMember->iRank < pGuildEvent->iMinGuildEditRank)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_GuildEvent", "GuildServer_CantEdit", true);
		return;
	}

	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_CancelGuildEvent(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, iID, uiID, GUILD_SERVERDOWNHANDLER(pEnt));
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_RemoveEvent) ACMD_PRIVATE;
void gslGuild_cmd_RemoveEvent(SA_PARAM_NN_VALID Entity *pEnt, U32 uiID)
{
	GuildMember *pMember = guild_FindMember(pEnt);
	GuildEvent *pGuildEvent = NULL;
	ContainerID iID = entGetContainerID(pEnt);
	Guild *pGuild = guild_GetGuild(pEnt);
	int index;

	if ((!pGuild) || (!pMember))
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_GuildEvent", "GuildServer_SelfNotInGuild", true);
		return;
	}

	index = eaIndexedFindUsingInt(&pGuild->eaEvents, uiID);
	pGuildEvent = pGuild->eaEvents[index];

	if (!pGuildEvent)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_GuildEvent", "GuildServer_NoTitleFound", true);
		return;
	}

	if (pMember->iRank < pGuildEvent->iMinGuildEditRank)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_GuildEvent", "GuildServer_CantEdit", true);
		return;
	}

	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_RemoveGuildEvent(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, uiID, GUILD_SERVERDOWNHANDLER(pEnt));
}

///////////////////////////////////////////////////////////////////////////////////////////
// Guild Event Reply
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_ReplyEvent) ACMD_PRIVATE;
void gslGuild_cmd_ReplyEvent(SA_PARAM_NN_VALID Entity *pEnt, U32 uiID, U32 iStartTime, /*GuildEventReplyType*/ int eReplyType, char *pcMessage)
{
	GuildMember *pOwner = NULL, *pMember = guild_FindMember(pEnt);
	GuildEvent *pGuildEvent = NULL;
	int index;
	ContainerID iID = entGetContainerID(pEnt);
	Guild *pGuild = guild_GetGuild(pEnt);

	if (!pGuild || !pMember)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_GuildEvent", "GuildServer_SelfNotInGuild", true);
		return;
	}

	if (eReplyType != GuildEventReplyType_Accept && eReplyType != GuildEventReplyType_Maybe && eReplyType != GuildEventReplyType_Refuse)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_GuildEvent", "GuildServer_InvalidEventReply", true);
		return;
	}

	if (strlen(pcMessage) > MAX_GUILD_EVENT_MESSAGE_LEN)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_GuildEvent", "GuildServer_EventMessageTooLong", true);
		return;
	}

	index = eaIndexedFindUsingInt(&pGuild->eaEvents, uiID);
	if (index < 0)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_GuildEvent", "GuildServer_NoTitleFound", true);
		return;
	}

	pGuildEvent = pGuild->eaEvents[index];

	if (pMember->iRank < pGuildEvent->iMinGuildRank)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_GuildEvent", "GuildServer_EventLowRank", true);
		return;
	}

	if (pMember->iLevel < pGuildEvent->iMinLevel || pMember->iLevel > pGuildEvent->iMaxLevel)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_GuildEvent", "GuildServer_EventWrongLevel", true);
		return;
	}

	if (iStartTime < timeSecondsSince2000())
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_GuildEvent", "GuildServer_IncorrectTime", true);
		return;
	}

	if (pGuildEvent->eRecurType != 0)
	{
		if ((iStartTime - pGuildEvent->iStartTimeTime) % DAYS(pGuildEvent->eRecurType) != 0)
		{
			gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_GuildEvent", "GuildServer_IncorrectTime", true);
			return;
		}
	}
	else if (iStartTime != pGuildEvent->iStartTimeTime)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_GuildEvent", "GuildServer_IncorrectTime", true);
		return;
	}

	if (eReplyType == GuildEventReplyType_Accept && pGuildEvent->iMaxAccepts && pGuildEvent->iMaxAccepts <= guildevent_GetReplyCount(pGuild, pGuildEvent, iStartTime, GuildEventReplyType_Accept))
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_GuildEvent", "GuildServer_EventFilled", true);
		return;
	}

	pEnt->pPlayer->pGuild->uiDisplayedEvent = timeSecondsSince2000();

	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_ReplyGuildEvent(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, pEnt->myContainerID, uiID, iStartTime, eReplyType, pcMessage, GUILD_SERVERDOWNHANDLER(pEnt));
}


///////////////////////////////////////////////////////////////////////////////////////////
// Invite to Guild Map
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_AcceptGuildMapInvite) ACMD_HIDE; 
void gslGuild_cmd_AcceptGuildMapInvite(Entity* pEnt, ContainerID iGuildID)
{
	if (pEnt->pPlayer) {
		int i;
		for (i = eaSize(&pEnt->pPlayer->eaGuildMapInvites)-1; i >= 0; i--) {
			GuildMapInvite* pInvite = pEnt->pPlayer->eaGuildMapInvites[i];
			if (pInvite->uGuildID == iGuildID) {
				MapMoveStaticEx(pEnt, NULL, NULL, 0, pInvite->uMapID, pInvite->uPartitionID, 0, 0, NULL, 
					0, MAPSEARCHTYPE_SPECIFIC_CONTAINER_AND_PARTITION_ID_ONLY,
					STACK_SPRINTF("AcceptGuildMapInvite entering map [MapID=%d,PartitionID=%d]",pInvite->uMapID,pInvite->uPartitionID), 
					NULL);
				break;
			}
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_DeclineGuildMapInvite) ACMD_HIDE;
void gslGuild_cmd_DeclineGuildMapInvite(Entity *pEnt, ContainerID iGuildID)
{
	if (pEnt->pPlayer) {
		int i;
		for (i = eaSize(&pEnt->pPlayer->eaGuildMapInvites)-1; i >= 0; i--) {
			if (pEnt->pPlayer->eaGuildMapInvites[i]->uGuildID == iGuildID) {
				break;
			}
		}
		if (i >= 0) {
			StructDestroy(parse_GuildMapInvite, eaRemove(&pEnt->pPlayer->eaGuildMapInvites, i));
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		}
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void gslGuild_rcmd_InviteToGuildMapError(ContainerID iEntID, const char* pchErrorKey)
{
	Entity* pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iEntID);
	if (pEnt)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, "", "GuildServer_ErrorType_GuildMapCommand", pchErrorKey, true);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void gslGuild_rcmd_InviteToGuildMap(ContainerID iSubjectID, ContainerID iInviterID, ContainerID iGuildID, ContainerID iMapID, U32 uPartitionID, const char* pchGuildName, const char* pchInviterAllegiance)
{
	// Note we do NOT check	(gConf.bEnforceGuildGuildMemberAllegianceMatch) here. This checks player/player allegiance matching
	Entity *pSubject = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iSubjectID);
	if (pSubject && pSubject->pPlayer) {
		const char* pchInviteeAllegiance = REF_STRING_FROM_HANDLE(pSubject->hAllegiance);
		if (!pchInviteeAllegiance || stricmp(pchInviteeAllegiance, pchInviterAllegiance) != 0) {
			RemoteCommand_gslGuild_rcmd_InviteToGuildMapError(GLOBALTYPE_ENTITYPLAYER, iInviterID, iInviterID, "GuildServer_PlayerNotAllegiance");
		} else if (allegiance_CanPlayerUseWarp(pSubject)) {
			GuildMapInvite* pInvite = NULL;
			int i;
			for (i = eaSize(&pSubject->pPlayer->eaGuildMapInvites)-1; i >= 0; i--) {
				pInvite = pSubject->pPlayer->eaGuildMapInvites[i];
				if (pInvite->uGuildID == iGuildID) {
					break;
				}
			}
			if (i < 0) {
				pInvite = StructCreate(parse_GuildMapInvite);
				pInvite->uGuildID = iGuildID;
				StructCopyString(&pInvite->pchGuildName, pchGuildName);
				eaPush(&pSubject->pPlayer->eaGuildMapInvites, pInvite);
				entity_SetDirtyBit(pSubject, parse_Player, pSubject->pPlayer, false);
			}
			pInvite->uMapID = iMapID;
			pInvite->uPartitionID = uPartitionID;
		} else {
			RemoteCommand_gslGuild_rcmd_InviteToGuildMapError(GLOBALTYPE_ENTITYPLAYER, iInviterID, iInviterID, "GuildServer_PlayerCannotReceiveGuildInvites");
		}
	}
}

static void gslGuild_cmd_InviteToGuildMap_CB(Entity *pEnt, ContainerID iSubjectID, U32 uiAccountID, U32 uiLoginServerID, GSLGuildCBData *pData)
{
	Entity *pSubject;
	ContainerID iGuildID;
	ContainerID iInviterID;
	GuildMember* pMember;
	Guild* pGuild;
	int iPartitionIdx;
	const char* pchAllegiance;

	SAFE_FREE(pData);
	
	if (!pEnt) {
		return;
	}
	iPartitionIdx = entGetPartitionIdx(pEnt);
	iInviterID = entGetContainerID(pEnt);
	iGuildID = guild_IsMember(pEnt) ? guild_GetGuildID(pEnt) : 0;
	if (!iGuildID) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, "", "GuildServer_ErrorType_GuildMapCommand", "GuildServer_SelfNotInGuild", true);
		return;
	}
	if (partition_OwnerTypeFromIdx(iPartitionIdx) != GLOBALTYPE_GUILD || partition_OwnerIDFromIdx(iPartitionIdx) != iGuildID) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, "", "GuildServer_ErrorType_GuildMapCommand", "GuildServer_NotOnGuildMap", true);
		return;
	}
	if (!zmapInfoGetGuildNotRequired(NULL)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, "", "GuildServer_ErrorType_GuildMapCommand", "GuildServer_RequireMembersOnGuildMap", true);
		return;
	}

	pGuild = guild_GetGuild(pEnt);
	pMember = guild_FindMemberInGuild(pEnt, pGuild);

	if (!pMember || !guild_HasPermission(pMember->iRank, pGuild, GuildPermission_GuildMapInvites)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, "", "GuildServer_ErrorType_GuildMapCommand", "GuildServer_NoPermission_GuildMapInvites", true);
		return;
	}

	// Note we do NOT check	(gConf.bEnforceGuildGuildMemberAllegianceMatch) here. This checks player/player allegiance matching
	pchAllegiance = REF_STRING_FROM_HANDLE(pEnt->hAllegiance);
	if (!pchAllegiance) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, "", "GuildServer_ErrorType_GuildMapCommand", "GuildServer_PlayerNotAllegiance", true);
		return;
	}

	pSubject = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, iSubjectID);
	if (!pSubject) {
		ContainerID iMapID = gGSLState.gameServerDescription.baseMapDescription.containerID;
		U32 iPartitionID = partition_IDFromIdx(iPartitionIdx);
		RemoteCommand_gslGuild_rcmd_InviteToGuildMap(GLOBALTYPE_ENTITYPLAYER, iSubjectID, iSubjectID, iInviterID, iGuildID, iMapID, iPartitionID, pGuild->pcName, pchAllegiance);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_InviteToGuildMap) ACMD_HIDE;
void gslGuild_cmd_InviteToGuildMap(SA_PARAM_NN_VALID Entity *pEnt, ACMD_SENTENCE pcOtherPlayer)
{
	GSLGuildCBData *pData = gslGuild_MakeCBData(entGetRef(pEnt), "GuildServer_ErrorType_GuildMapCommand", NULL);
	gslPlayerResolveHandle(pEnt, pcOtherPlayer, gslGuild_cmd_InviteToGuildMap_CB, gslGuild_ResolveHandleFailure_CB, pData);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Kick from Guild Map
///////////////////////////////////////////////////////////////////////////////////////////

static void gslGuild_cmd_KickFromGuildMap_CB(Entity *pEnt, ContainerID iSubjectID, U32 uiAccountID, U32 uiLoginServerID, GSLGuildCBData *pData)
{
	Entity *pSubject;
	ContainerID iGuildID;
	GuildMember* pMember;
	Guild* pGuild;
	int iPartitionIdx;

	SAFE_FREE(pData);
	
	if (!pEnt) {
		return;
	}
	iPartitionIdx = entGetPartitionIdx(pEnt);
	iGuildID = guild_IsMember(pEnt) ? guild_GetGuildID(pEnt) : 0;
	if (!iGuildID) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, "", "GuildServer_ErrorType_GuildMapCommand", "GuildServer_SelfNotInGuild", true);
		return;
	}
	if (partition_OwnerTypeFromIdx(iPartitionIdx) != GLOBALTYPE_GUILD || partition_OwnerIDFromIdx(iPartitionIdx) != iGuildID) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, "", "GuildServer_ErrorType_GuildMapCommand", "GuildServer_NotOnGuildMap", true);
		return;
	}

	pGuild = guild_GetGuild(pEnt);
	pMember = guild_FindMemberInGuild(pEnt, pGuild);

	if (!pMember || !guild_HasPermission(pMember->iRank, pGuild, GuildPermission_GuildMapInvites)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, "", "GuildServer_ErrorType_GuildMapCommand", "GuildServer_NoPermission_GuildMapInvites", true);
		return;
	}

	pSubject = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, iSubjectID);

	if (!pSubject) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, "", "GuildServer_ErrorType_GuildMapCommand", "GuildServer_PlayerNotFound", true);
		return;
	}
	
	if (guild_IsMember(pSubject) && guild_GetGuildID(pSubject) == iGuildID) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, "", "GuildServer_ErrorType_GuildMapCommand", "GuildServer_CannotKickMemberFromGuildMap", true);
		return;
	}

	mechanics_LogoutTimerStartEx(pSubject, LogoutTimerType_MissionReturn, GUILD_MAP_KICK_LOGOUT_TIMER);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_KickFromGuildMap) ACMD_HIDE;
void gslGuild_cmd_KickFromGuildMap(SA_PARAM_NN_VALID Entity *pEnt, ACMD_SENTENCE pcOtherPlayer)
{
	GSLGuildCBData *pData = gslGuild_MakeCBData(entGetRef(pEnt), "GuildServer_ErrorType_GuildMapCommand", NULL);
	gslPlayerResolveHandle(pEnt, pcOtherPlayer, gslGuild_cmd_KickFromGuildMap_CB, gslGuild_ResolveHandleFailure_CB, pData);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_KickFromGuildMapByID) ACMD_HIDE;
void gslGuild_cmd_KickFromGuildMapByID(SA_PARAM_NN_VALID Entity *pEnt, U32 uOtherPlayerID)
{
	gslGuild_cmd_KickFromGuildMap_CB(pEnt, uOtherPlayerID, 0, 0, NULL);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set guild emblem
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_SetEmblem) ACMD_HIDE ACMD_PRODUCTS(StarTrek);
void gslGuild_cmd_SetEmblem(SA_PARAM_NN_VALID Entity *pEnt, ACMD_SENTENCE pcEmblem)
{
	if (!guild_IsMember(pEnt)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetEmblem", "GuildServer_SelfNotInGuild", true);
		return;
	}
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_SetEmblem(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, pEnt->myContainerID, pcEmblem, false, GUILD_SERVERDOWNHANDLER(pEnt));
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_SetAdvancedEmblem) ACMD_PRIVATE;
void gslGuild_cmd_SetAdvancedEmblem(SA_PARAM_NN_VALID Entity *pEnt, char *pcEmblem, U32 iEmblemColor0, U32 iEmblemColor1, F32 fEmblemRotation, bool bHidePlayerFeedback)
{
	if (!guild_IsMember(pEnt)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetEmblem", "GuildServer_SelfNotInGuild", true);
		return;
	}
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_SetAdvancedEmblem(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, pEnt->myContainerID, pcEmblem, iEmblemColor0, iEmblemColor1, fEmblemRotation, bHidePlayerFeedback, false, GUILD_SERVERDOWNHANDLER(pEnt));
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_SetAdvancedEmblem2) ACMD_PRIVATE;
void gslGuild_cmd_SetAdvancedEmblem2(SA_PARAM_NN_VALID Entity *pEnt, char *pcEmblem2, U32 iEmblem2Color0, U32 iEmblem2Color1, F32 fEmblem2Rotation, F32 fEmblem2X, F32 fEmblem2Y, F32 fEmblem2ScaleX, F32 fEmblem2ScaleY)
{
	if (!guild_IsMember(pEnt)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetEmblem", "GuildServer_SelfNotInGuild", true);
		return;
	}

	//Check values against limits
	if (fEmblem2X > 100) fEmblem2X = 100;
	if (fEmblem2X < -100) fEmblem2X = -100;
	if (fEmblem2Y > 100) fEmblem2Y = 100;
	if (fEmblem2Y < -100) fEmblem2Y = -100;
	if (fEmblem2ScaleX > 100) fEmblem2ScaleX = 100;
	if (fEmblem2ScaleX < 0) fEmblem2ScaleX = 0;
	if (fEmblem2ScaleY > 100) fEmblem2ScaleY = 100;
	if (fEmblem2ScaleY < 0) fEmblem2ScaleY = 0;

	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_SetAdvancedEmblem2(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, pEnt->myContainerID, pcEmblem2, iEmblem2Color0, iEmblem2Color1, fEmblem2Rotation, fEmblem2X, fEmblem2Y, fEmblem2ScaleX, fEmblem2ScaleY, GUILD_SERVERDOWNHANDLER(pEnt));
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_SetAdvancedEmblem3) ACMD_PRIVATE;
void gslGuild_cmd_SetAdvancedEmblem3(SA_PARAM_NN_VALID Entity *pEnt, char *pcEmblem3, bool bHidePlayerFeedback)
{
	if (!guild_IsMember(pEnt)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetEmblem", "GuildServer_SelfNotInGuild", true);
		return;
	}
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_SetAdvancedEmblem3(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, pEnt->myContainerID, pcEmblem3, bHidePlayerFeedback, GUILD_SERVERDOWNHANDLER(pEnt));
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set guild color 1
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_SetColor1) ACMD_PRIVATE;
void gslGuild_cmd_SetColor1(SA_PARAM_NN_VALID Entity *pEnt, U32 iColor)
{
	if (!guild_IsMember(pEnt)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetColors", "GuildServer_SelfNotInGuild", true);
		return;
	}
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_SetColors(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, pEnt->myContainerID, iColor, true, false, GUILD_SERVERDOWNHANDLER(pEnt));
}

///////////////////////////////////////////////////////////////////////////////////////////
// Set guild color 2
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_SetColor2) ACMD_PRIVATE;
void gslGuild_cmd_SetColor2(SA_PARAM_NN_VALID Entity *pEnt, U32 iColor)
{
	if (!guild_IsMember(pEnt)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetColors", "GuildServer_SelfNotInGuild", true);
		return;
	}
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_SetColors(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, pEnt->myContainerID, iColor, false, false, GUILD_SERVERDOWNHANDLER(pEnt));
}

///////////////////////////////////////////////////////////////////////////////////////////
// Add Guild Costume
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_AddCostume) ACMD_PRIVATE;
void gslGuild_cmd_AddCostume(SA_PARAM_NN_VALID Entity *pEnt, const char *pchName)
{
	int i;
	Guild *pGuild;
	PlayerCostume *pCostume = NULL;
	NOCONST(PlayerCostume) *pOverlay = NULL;

	if (!guild_IsMember(pEnt)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_AddCostume", "GuildServer_SelfNotInGuild", true);
		return;
	}

	pGuild = guild_GetGuild(pEnt);
	if (!pGuild) return;

	//Make sure we don't have too many costumes
	if (eaSize(&pGuild->eaUniforms) >= GUILD_UNIFORM_MAX_COUNT)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_AddCostume", "GuildServer_MaxCostumes", true);
		return;
	}

	//Make sure name is valid
	if (!pchName) return;
	if (StringIsInvalidCostumeName(pchName)) return;

	//Make sure name is not duplicated
	for (i = eaSize(&pGuild->eaUniforms)-1; i >= 0; --i)
	{
		if (!pGuild->eaUniforms[i]) continue;
		if (!pGuild->eaUniforms[i]->pCostume) continue;
		if (!pGuild->eaUniforms[i]->pCostume->pcName) continue;
		if (!stricmp(pchName,pGuild->eaUniforms[i]->pCostume->pcName))
		{
			gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_AddCostume", "GuildServer_DupCostumeName", true);
			return;
		}
	}

	if (pEnt && pEnt->pSaved){
		pCostume = costumeEntity_GetActiveSavedCostume(pEnt);
	}

	if (!pCostume) return; //Data error

	//Make sure Overlay is valid
	if (costumeTailor_DoesCostumeHaveUnlockables((PlayerCostume*)pCostume))
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_AddCostume", "GuildServer_CostumeHasUnlockables", true);
		return;
	}

	{
		NOCONST(PlayerCostume) *pClone = StructCloneDeConst(parse_PlayerCostume, pCostume);

		//Make it valid
		costumeTailor_MakeCostumeValid(pClone, GET_REF(pEnt->pChar->hSpecies), NULL, costumeLoad_GetSlotType("Uniform"), false, false, false, pGuild, false, NULL, false, NULL);

		//Acquire Overlay
		pOverlay = CONTAINER_NOCONST(PlayerCostume, costumeTailor_MakeCostumeOverlay(CONTAINER_RECONST(PlayerCostume, pClone), "Uniforms", false, false));
		StructDestroyNoConst(parse_PlayerCostume, pClone);
		if (!pOverlay) return;
	}

	pOverlay->pcName = allocAddString(pchName);

	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_AddCostume(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, pEnt->myContainerID, (PlayerCostume*)pOverlay, GUILD_SERVERDOWNHANDLER(pEnt));

	StructDestroyNoConst(parse_PlayerCostume, pOverlay);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_AddLoadedCostume) ACMD_PRIVATE;
void gslGuild_cmd_AddLoadedCostume(SA_PARAM_NN_VALID Entity *pEnt, PlayerCostume *pCostume, const char *pchName)
{
	int i;
	Guild *pGuild;
	NOCONST(PlayerCostume) *pOverlay = NULL;

	if (!pCostume) return;

	if (!guild_IsMember(pEnt)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_AddCostume", "GuildServer_SelfNotInGuild", true);
		return;
	}

	pGuild = guild_GetGuild(pEnt);
	if (!pGuild) return;

	//Make sure we don't have too many costumes
	if (eaSize(&pGuild->eaUniforms) >= GUILD_UNIFORM_MAX_COUNT)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_AddCostume", "GuildServer_MaxCostumes", true);
		return;
	}

	//Make sure name is valid
	if (!pchName) return;
	if (StringIsInvalidCostumeName(pchName)) return;

	//Make sure name is not duplicated
	for (i = eaSize(&pGuild->eaUniforms)-1; i >= 0; --i)
	{
		if (!pGuild->eaUniforms[i]) continue;
		if (!pGuild->eaUniforms[i]->pCostume) continue;
		if (!pGuild->eaUniforms[i]->pCostume->pcName) continue;
		if (!stricmp(pchName,pGuild->eaUniforms[i]->pCostume->pcName))
		{
			gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_AddCostume", "GuildServer_DupCostumeName", true);
			return;
		}
	}

	//Make sure Overlay is valid
	if (costumeTailor_DoesCostumeHaveUnlockables((PlayerCostume*)pCostume))
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_AddCostume", "GuildServer_CostumeHasUnlockables", true);
		return;
	}

	{
		NOCONST(PlayerCostume) *pClone = StructCloneDeConst(parse_PlayerCostume, pCostume);

		//Make it valid
		costumeTailor_MakeCostumeValid(pClone, GET_REF(pCostume->hSpecies), NULL, costumeLoad_GetSlotType("Uniform"), false, true, false, pGuild, false, NULL, false, NULL);

		//Acquire Overlay
		pOverlay = CONTAINER_NOCONST(PlayerCostume, costumeTailor_MakeCostumeOverlay(CONTAINER_RECONST(PlayerCostume, pClone), "Uniforms", false, false));
		StructDestroyNoConst(parse_PlayerCostume, pClone);
		if (!pOverlay) return;
	}

	pOverlay->pcName = allocAddString(pchName);

	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_AddCostume(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, pEnt->myContainerID, (PlayerCostume*)pOverlay, GUILD_SERVERDOWNHANDLER(pEnt));

	StructDestroyNoConst(parse_PlayerCostume, pOverlay);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Delete Guild Costume
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_DeleteCostume) ACMD_HIDE ACMD_PRODUCTS(StarTrek);
void gslGuild_cmd_DeleteCostume(SA_PARAM_NN_VALID Entity *pEnt, const char *pchName)
{
	int i;
	Guild *pGuild;

	if (!guild_IsMember(pEnt)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_AddCostume", "GuildServer_SelfNotInGuild", true);
		return;
	}

	pGuild = guild_GetGuild(pEnt);
	if (!pGuild) return;
	if (!pchName) return;

	for (i = eaSize(&pGuild->eaUniforms)-1; i >= 0; --i)
	{
		if (!pGuild->eaUniforms[i]) continue;
		if (!pGuild->eaUniforms[i]->pCostume) continue;
		if (!pGuild->eaUniforms[i]->pCostume->pcName) continue;
		if (!stricmp(pchName,pGuild->eaUniforms[i]->pCostume->pcName))
		{
			break;
		}
	}
	if (i < 0) return; //Costume by that name is not found

	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_DeleteCostume(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, pEnt->myContainerID, i, GUILD_SERVERDOWNHANDLER(pEnt));
}

///////////////////////////////////////////////////////////////////////////////////////////
// Request a portion of the bank log
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Guild) ACMD_NAME(Guild_ClearLog) ACMD_HIDE ACMD_PRIVATE;
void gslGuild_cmd_ClearLog(SA_PARAM_NN_VALID Entity *pEnt)
{
	if (!pEnt->pPlayer || !pEnt->pPlayer->pGuild) {
		return;
	}
	eaClearStruct(&pEnt->pPlayer->pGuild->eaBankLog, parse_PlayerGuildLog);

	entity_SetDirtyBit(pEnt, parse_PlayerGuild, pEnt->pPlayer->pGuild, true);
	entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Guild) ACMD_NAME(Guild_RequestLog) ACMD_HIDE ACMD_PRIVATE;
void gslGuild_cmd_RequestLog(SA_PARAM_NN_VALID Entity *pEnt, U32 iStart, U32 iNumEntries)
{
	S32 i, iFirstIdx;
	U32 count = 0;
	Guild *pGuild = guild_GetGuild(pEnt);
	Entity *pGuildBank = guild_GetGuildBank(pEnt);
	if (!pGuild) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_RequestLog", "GuildServer_SelfNotInGuild", true);
		return;
	}

	if(!pGuildBank)
	{
		if(!IS_HANDLE_ACTIVE(pEnt->pPlayer->pGuild->hGuildBank))
		{
			char idBuf[128];
			SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYGUILDBANK), ContainerIDToString(pGuild->iContainerID, idBuf), pEnt->pPlayer->pGuild->hGuildBank);
		}

		return;
	}

	if (!eaSize(&pGuild->eaBankLog))
	{
		gslGuild_cmd_ClearLog(pEnt);
		return;
	}

	if ((S32)iStart >= eaSize(&pGuild->eaBankLog)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_RequestLog", "GuildServer_OutsideLogSize", true);
		return;
	}

	iFirstIdx = pGuild->uOldBankLogIdx - 1;
	if(iFirstIdx < 0)
	{
		iFirstIdx = eaSize(&pGuild->eaBankLog) - 1;
		if(iFirstIdx < 0)
		{
			// no entries
			return;
		}
	}

	gslGuild_cmd_ClearLog(pEnt);
	i = iFirstIdx - iStart;
	while(i < 0)
	{
		i += eaSize(&pGuild->eaBankLog);
	}
	while(count < iNumEntries && i < eaSize(&pGuild->eaBankLog))
	{
		GuildBankLogEntry *pLogEntry = pGuild->eaBankLog[i];
		PlayerGuildLog *pPlayerLogEntry;
		InventoryBag *pBag = inv_guildbank_GetBag(pGuildBank, pLogEntry->eBag);
		const char *pcMessageKey;
		const char *pcItemName;
		const char *pcTabName;
		char *estrBuffer = NULL;
		char *estrNameBuf = NULL;
		const char *pcEntAccount = NULL;
		ItemDef *pItemDef = NULL;

		if(pLogEntry->pcItemDef)
		{
			pItemDef = RefSystem_ReferentFromString(g_hItemDict, pLogEntry->pcItemDef);
		}

		if (pLogEntry->iNumberMoved > 0) {
			if (pLogEntry->eBag == InvBagIDs_Numeric) {
				pcMessageKey = allocAddString("GuildServer_Log_DepositNumeric");
			} else {
				pcMessageKey = allocAddString("GuildServer_Log_Deposit");
			}
		} else {
			if (pLogEntry->eBag == InvBagIDs_Numeric) {
				pcMessageKey = allocAddString("GuildServer_Log_WithdrawNumeric");
			} else {
				pcMessageKey = allocAddString("GuildServer_Log_Withdraw");
			}
		}

		if(eaSize(&pLogEntry->ppItemNames) > 0)
		{
			S32 iNameIdx;
			char **ppNames = NULL;

			for(iNameIdx = 0; iNameIdx < eaSize(&pLogEntry->ppItemNames); ++iNameIdx)
			{
				eaPush(&ppNames, (char *)CONTAINER_NOCONST(GuildBankLogEntry, pLogEntry)->ppItemNames[iNameIdx]->pcItemName);
			}

			item_GetNameFromUntranslatedStrings(entGetLanguage(pEnt), true, ppNames, &estrNameBuf);
			pcItemName = estrNameBuf;
			eaClear(&ppNames);
			eaDestroy(&ppNames);
		}
		else if(pItemDef)
		{
			pcItemName = item_GetDefLocalName(pItemDef, entGetLanguage(pEnt));
		}
		else
		{
			pcItemName = entTranslateMessageKey(pEnt, "GuildServer_Log_UnknownItem");
		}

		if (pLogEntry->eBag == InvBagIDs_Numeric) {
			pcTabName = "";
		} if (pBag) {
			pcTabName = guild_GetBankTabName(pEnt, pLogEntry->eBag);
		} else {
			pcTabName = entTranslateMessageKey(pEnt, "GuildServer_Log_UnknownBag");
		}

		pcEntAccount = pLogEntry->pcEntAccount;
		if ((!pcEntAccount) || (!*pcEntAccount))
		{
			GuildMember *m = eaIndexedGetUsingInt(&pGuild->eaMembers, pLogEntry->iEntID);
			if (m)
			{
				pcEntAccount = m->pcAccount;
			}
			else
			{
				pcEntAccount = "";
			}
		}

		estrStackCreate(&estrBuffer);
		entFormatGameMessageKey(pEnt, &estrBuffer, pcMessageKey,
			STRFMT_DATETIME("Time", pLogEntry->iTimestamp),
			STRFMT_STRING("Player", pLogEntry->pcEntName),
			STRFMT_STRING("Account", pcEntAccount),
			STRFMT_STRING("ItemName", pcItemName),
			STRFMT_INT("Amount", abs(pLogEntry->iNumberMoved)),
			STRFMT_STRING("Tab", pcTabName),
			STRFMT_END);
		pPlayerLogEntry = StructCreate(parse_PlayerGuildLog);
		pPlayerLogEntry->pcLogEntry = StructAllocString(estrBuffer);
		eaPush(&pEnt->pPlayer->pGuild->eaBankLog, pPlayerLogEntry);
		estrDestroy(&estrBuffer);
		estrDestroy(&estrNameBuf);
		pPlayerLogEntry->time = pLogEntry->iTimestamp;

		++count;
		--i;
		if(i < 0)
		{
			i = eaSize(&pGuild->eaBankLog) -1;
		}

		if(i == iFirstIdx)
		{
			// wrapped around
			break;
		}
	}
	pEnt->pPlayer->pGuild->iBankLogSize = eaSize(&pGuild->eaBankLog);
	++pEnt->pPlayer->pGuild->iUpdated;

	entity_SetDirtyBit(pEnt, parse_PlayerGuild, pEnt->pPlayer->pGuild, true);
	entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Request valid guild uniforms for the player
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Guild) ACMD_NAME(Guild_ClearUniforms) ACMD_PRIVATE;
void gslGuild_cmd_ClearUniforms(SA_PARAM_NN_VALID Entity *pEnt)
{
	if (!pEnt->pPlayer || !pEnt->pPlayer->pGuild) {
		return;
	}
	eaClearStruct(&pEnt->pPlayer->pGuild->eaGuildCostumes, parse_PlayerCostumeHolder);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Guild) ACMD_NAME(Guild_RequestUniforms) ACMD_PRIVATE;
void gslGuild_cmd_RequestUniforms(SA_PARAM_NN_VALID Entity *pEnt)
{
	S32 i, j;
	Guild *pGuild = guild_GetGuild(pEnt);
	GuildMember* pGuildMember = NULL;
	GuildCostume *pGuildCostume = NULL;
	SpeciesDef *pSpecies = NULL;
	PowerTree *pTree = NULL;
	PowerTreeDef *pDef = NULL;
	PlayerCostumeHolder *pPC;

	if (!pEnt->pChar) return;
	pSpecies = GET_REF(pEnt->pChar->hSpecies);
	if (!pSpecies) return;

	if (!pGuild) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_RequestUniforms", "GuildServer_SelfNotInGuild", true);
		return;
	}
	pGuildMember = guild_FindMember(pEnt);
	if (!pGuildMember) return;

	pTree = eaGet(&pEnt->pChar->ppPowerTrees, 0);
	if (!pTree) return;
	pDef = GET_REF(pTree->hDef);
	if (!pDef) return;

	gslGuild_cmd_ClearUniforms(pEnt);

	for (i = eaSize(&pGuild->eaUniforms)-1; i >= 0; --i)
	{
		pGuildCostume = pGuild->eaUniforms[i];

		//See if Species is valid
		for (j = eaSize(&pGuildCostume->eaSpeciesNotAllowed)-1; j >= 0; --j)
		{
			if (GET_REF(pGuildCostume->eaSpeciesNotAllowed[j]->hSpeciesRef) == pSpecies)
			{
				break;
			}
		}
		if (j >= 0) continue;

		//See if Rank is valid
		for (j = ea32Size(&pGuildCostume->eaRanksNotAllowed)-1; j >= 0; --j)
		{
			if (pGuildCostume->eaRanksNotAllowed[j] == pGuildMember->iRank)
			{
				break;
			}
		}
		if (j >= 0) continue;

		//See if Class is valid
		for (j = eaSize(&pGuildCostume->eaClassNotAllowed)-1; j >= 0; --j)
		{
			if (!stricmp(pGuildCostume->eaClassNotAllowed[j]->pchClass,pDef->pchName))
			{
				break;
			}
		}
		if (j >= 0) continue;

		pPC = StructCreate(parse_PlayerCostumeHolder);
		pPC->pCostume = StructClone(parse_PlayerCostume, pGuildCostume->pCostume);
		eaPush(&pEnt->pPlayer->pGuild->eaGuildCostumes, pPC);
	}

	entity_SetDirtyBit(pEnt, parse_PlayerGuild, pEnt->pPlayer->pGuild, true);
	entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Check if a guild name is valid
///////////////////////////////////////////////////////////////////////////////////////////

static void gslGuild_NameValid_CB(TransactionReturnVal *pReturn, GSLGuildCBData *pData) {
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS) {
		Entity *pEnt = entFromEntityRefAnyPartition(pData->iRef);
		U32 iGuildID;
		RemoteCommandCheck_aslGuild_GetIDByName(pReturn, &iGuildID);
		if (iGuildID) {
			ClientCmd_gclGuild_ccmd_NameTakenReturn(pEnt, true);
		} else {
			ClientCmd_gclGuild_ccmd_NameTakenReturn(pEnt, false);
		}
	}
	SAFE_FREE(pData);
}

// This can't be used as a direct console command because a variable needs to be
// set on the client each time it's called.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Guild) ACMD_PRIVATE;
void gslGuild_NameValid(Entity *pEnt, char *pcName)
{
	if (StringIsInvalidGuildName(pcName)) {
		ClientCmd_gclGuild_ccmd_NameTakenReturn(pEnt, false);
	} else {
		GSLGuildCBData *pData = gslGuild_MakeCBData(pEnt->myRef, NULL, NULL);
		TransactionReturnVal *pReturn = objCreateManagedReturnVal(gslGuild_NameValid_CB, pData);
		VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
		RemoteCommand_aslGuild_GetIDByName(pReturn, GLOBALTYPE_GUILDSERVER, 0, pcName, entGetVirtualShardID(pEnt));
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// Get the guild info
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(Guild) ACMD_NAME(Guild_Info);
void gslGuild_cmd_Info(SA_PARAM_NN_VALID Entity *pEnt)
{
	Guild *pGuild = guild_GetGuild(pEnt);
	char *estrMemberList = NULL;
	char *estrFinal = NULL;
	char *estrTemp = NULL;
	S32 i;
	
	if (!pGuild) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_Info", "GuildServer_SelfNotInGuild", true);
		return;
	}
	
	for (i = eaSize(&pGuild->eaMembers)-1; i >= 0; i--) {
		entFormatGameMessageKey(pEnt, &estrTemp, "GuildServer_Info_Member", STRFMT_GUILDMEMBER_KEY("member", pGuild->eaMembers[i]), STRFMT_END);
		estrConcatf(&estrMemberList, "\n%s", estrTemp);
		estrClear(&estrTemp);
	}
	entFormatGameMessageKey(pEnt, &estrFinal, "GuildServer_Info_Final",
		STRFMT_GUILD_KEY("guild", pGuild),
		STRFMT_STRING("memberlist", estrMemberList),
		STRFMT_STRING("nl", "\n"),
		STRFMT_END);
	ClientCmd_NotifySend(pEnt, kNotifyType_GuildInfo, estrFinal, NULL, NULL);
	
	estrDestroy(&estrTemp);
	estrDestroy(&estrMemberList);
	estrDestroy(&estrFinal);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Get the guild online members
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(Guild) ACMD_NAME(Guild_Who);
void gslGuild_cmd_Who(SA_PARAM_NN_VALID Entity *pEnt)
{
	Guild *pGuild = guild_GetGuild(pEnt);
	char *estrMemberList = NULL;
	char *estrFinal = NULL;
	char *estrTemp = NULL;
	S32 i;
	
	if (!pGuild) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_Who", "GuildServer_SelfNotInGuild", true);
		return;
	}
	
	for (i = eaSize(&pGuild->eaMembers)-1; i >= 0; i--) {
		if (pGuild->eaMembers[i]->bOnline) {
			entFormatGameMessageKey(pEnt, &estrTemp, "GuildServer_Who_Member", STRFMT_GUILDMEMBER_KEY("member", pGuild->eaMembers[i]), STRFMT_END);
			estrConcatf(&estrMemberList, "\n%s", estrTemp);
			estrClear(&estrTemp);
		}
	}
	entFormatGameMessageKey(pEnt, &estrFinal, "GuildServer_Who_Final",
		STRFMT_GUILD_KEY("guild", pGuild),
		STRFMT_STRING("memberlist", estrMemberList),
		STRFMT_END);
	ClientCmd_NotifySend(pEnt, kNotifyType_GuildInfo, estrFinal, NULL, NULL);
	
	estrDestroy(&estrTemp);
	estrDestroy(&estrMemberList);
	estrDestroy(&estrFinal);
}

///////////////////////////////////////////////////////////////////////////////////////////
// CSR Commands
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(Guild, csr) ACMD_NAME(GuildCSR_SetName);
void gslGuild_csrcmd_SetName(Entity *pEnt, const char *pcOldName, const char *pcNewName)
{
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_SetName(GLOBALTYPE_GUILDSERVER, 0, pEnt->myContainerID, pcOldName, pcNewName, entGetVirtualShardID(pEnt));
}

AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(Guild, csr) ACMD_NAME(GuildCSR_SetNameByID);
void gslGuild_csrcmd_SetNameByID(Entity *pEnt, U32 iGuildID, const char *pcNewName)
{
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_SetNameByID(GLOBALTYPE_GUILDSERVER, 0, pEnt->myContainerID, iGuildID, pcNewName, entGetVirtualShardID(pEnt));
}

AUTO_COMMAND ACMD_ACCESSLEVEL(7) ACMD_SERVERCMD ACMD_CATEGORY(Guild, csr) ACMD_NAME(GuildCSR_PurgeBank);
void gslGuild_csrcmd_PurgeBank(Entity *pEnt, const char *pcGuildName)
{
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_PurgeBank(GLOBALTYPE_GUILDSERVER, 0, pEnt->myContainerID, pcGuildName, entGetVirtualShardID(pEnt));
}

static void gslGuild_cmd_SetLeader_CB(Entity *pEnt, ContainerID iSubjectID, U32 uiAccountID, U32 uiLoginServerID, GSLGuildCBData *pData)
{
	static char pcErrorName[] = "GuildServer_ErrorType_SetLeader";
	
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_SetLeader(GLOBALTYPE_GUILDSERVER, 0, pEnt->myContainerID, pData->pchString, iSubjectID,
		entGetVirtualShardID(pEnt));
	SAFE_FREE(pData);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(Guild, csr) ACMD_NAME(GuildCSR_SetLeader);
void gslGuild_csrcmd_SetLeader(Entity *pEnt, const char *pcGuildName, const char *pcHandle)
{
	GSLGuildCBData *pData;
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	pData = gslGuild_MakeCBData(entGetRef(pEnt), "GuildServer_ErrorType_SetLeader", pcGuildName);
	gslPlayerResolveHandle(pEnt, pcHandle, gslGuild_cmd_SetLeader_CB, gslGuild_ResolveHandleFailure_CB, pData);
}

static void gslGuild_cmd_ResetRanks_CB(Entity *pEnt, ContainerID iSubjectID, U32 uiAccountID, U32 uiLoginServerID, GSLGuildCBData *pData)
{
	static char pcErrorName[] = "GuildServer_ErrorType_ResetRanks";
	
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_ResetRanks(GLOBALTYPE_GUILDSERVER, 0, pEnt->myContainerID, pData->pchString, iSubjectID,
		entGetVirtualShardID(pEnt));
	SAFE_FREE(pData);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(7) ACMD_SERVERCMD ACMD_CATEGORY(Guild, csr) ACMD_NAME(GuildCSR_ResetRanks);
void gslGuild_csrcmd_ResetRanks(Entity *pEnt, const char *pcGuildName, const char *pcHandle)
{
	GSLGuildCBData *pData;
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	pData = gslGuild_MakeCBData(entGetRef(pEnt), "GuildServer_ErrorType_ResetRanks", pcGuildName);
	gslPlayerResolveHandle(pEnt, pcHandle, gslGuild_cmd_ResetRanks_CB, gslGuild_ResolveHandleFailure_CB, pData);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(Guild, csr) ACMD_NAME(GuildCSR_Info);
void gslGuild_csrcmd_Info(Entity *pEnt, const char *pcGuildName)
{
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_Info(GLOBALTYPE_GUILDSERVER, 0, pEnt->myContainerID, pcGuildName, entGetVirtualShardID(pEnt));
}

AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(Guild, csr) ACMD_NAME(GuildCSR_Who);
void gslGuild_csrcmd_Who(Entity *pEnt, const char *pcGuildName)
{
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_Who(GLOBALTYPE_GUILDSERVER, 0, pEnt->myContainerID, pcGuildName, entGetVirtualShardID(pEnt));
}

AUTO_COMMAND ACMD_ACCESSLEVEL(7) ACMD_SERVERCMD ACMD_CATEGORY(Guild, csr) ACMD_NAME(GuildCSR_ForceJoin);
void gslGuild_csrcmd_ForceJoin(Entity *pEnt, const char *pcGuildName) {
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_ForceJoin(GLOBALTYPE_GUILDSERVER, 0, pEnt->myContainerID, pcGuildName, entGetVirtualShardID(pEnt), gslGuild_GetClassName(pEnt));
}

AUTO_COMMAND ACMD_ACCESSLEVEL(7) ACMD_SERVERCMD ACMD_CATEGORY(Guild) ACMD_NAME(GuildCSR_MakeMeLeader);
void gslGuild_cmd_MakeMeLeader(Entity *pEnt)
{
	GuildMember *pGuildMember = guild_FindMember(pEnt);
	Guild *pGuild = guild_GetGuild(pEnt);

	if (!pGuild || !pGuildMember)
		return;

	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_MakeLeader(GLOBALTYPE_GUILDSERVER, 0, pGuild->iContainerID, pGuildMember->iEntID);
}

AUTO_COMMAND_REMOTE;
void gslGuild_PrintInfo(U32 iEntID, const char *pcGuildInfo) {
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iEntID);
	if (pEnt) {
		gslSendPrintf(pEnt, "%s", pcGuildInfo);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// Guild Recruit and Search
///////////////////////////////////////////////////////////////////////////////////////////

static void gslGuild_GetGuildsSearch_CB(TransactionReturnVal *pReturn, void *pData)
{
	GuildRecruitInfoList *pList;
	Entity *pEnt = (Entity*)entFromEntityRefAnyPartition((EntityRef)((intptr_t)pData));

	if (RemoteCommandCheck_Guild_GetGuilds(pReturn, &pList) == TRANSACTION_OUTCOME_SUCCESS) 
	{
		if (!pList)
		{
			//NotifySend(pEnt, kNotifyType_AuctionFailed, entTranslateMessageKey(pEnt, "Auction.SearchFailed"), NULL, NULL);
			ClientCmd_gclGuildSetRecruitInfoList(pEnt, NULL);
			return;
		}

		pList->cooldownEnd = timeSecondsSince2000();

		ClientCmd_gclGuildSetRecruitInfoList(pEnt, pList);

		StructDestroy(parse_GuildRecruitInfoList, pList);
	}
	else
	{
		if(pEnt)
		{
			//NotifySend(pEnt, kNotifyType_AuctionFailed, entTranslateMessageKey(pEnt, "Auction.SearchFailed"), NULL, NULL);
			ClientCmd_gclGuildSetRecruitInfoList(pEnt, NULL);
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_SERVERCMD ACMD_CATEGORY(Guild);
void gslGuild_GetGuildsForSearch(Entity *pEnt, GuildRecruitSearchRequest *pRequest)
{
	{
		int i, j, k;
		GuildRecruitParam *grp = Guild_GetGuildRecruitParams();

		// Make sure nothing is marked as both include and exclude; also check for valid categories
		for (i = eaSize(&pRequest->eaGuildIncludeSearchCat)-1; i >= 0; --i)
		{
			GuildRecruitSearchCat *grsc = pRequest->eaGuildIncludeSearchCat[i];
			if (!grsc) return;
			if (!grsc->pcName) return;
			for (j = eaSize(&grp->eaGuildRecruitCatDef)-1; j >= 0; --j)
			{
				GuildRecruitCatDef *grcd = grp->eaGuildRecruitCatDef[j];
				if (!grcd) return;
				if (!grcd->pcName) return;
				for (k = eaSize(&grcd->eaGuildRecruitTagDef)-1; k >= 0; --k)
				{
					GuildRecruitTagDef *grtd = grcd->eaGuildRecruitTagDef[k];
					if (!grtd) return;
					if (!grtd->pcName) return;
					if (grsc->pcName == grtd->pcName) break;
				}
				if (k >= 0) break;
			}
			if (j < 0) return;
			for (j = eaSize(&pRequest->eaGuildExcludeSearchCat)-1; j >= 0; --j)
			{
				GuildRecruitSearchCat *grsc2 = pRequest->eaGuildExcludeSearchCat[j];
				if (!grsc2) return;
				if (!grsc2->pcName) return;
				if (grsc->pcName == grsc2->pcName) return;
			}
		}
		for (i = eaSize(&pRequest->eaGuildExcludeSearchCat)-1; i >= 0; --i)
		{
			GuildRecruitSearchCat *grsc = pRequest->eaGuildExcludeSearchCat[i];
			if (!grsc) return;
			if (!grsc->pcName) return;
			for (j = eaSize(&grp->eaGuildRecruitCatDef)-1; j >= 0; --j)
			{
				GuildRecruitCatDef *grcd = grp->eaGuildRecruitCatDef[j];
				if (!grcd) return;
				if (!grcd->pcName) return;
				for (k = eaSize(&grcd->eaGuildRecruitTagDef)-1; k >= 0; --k)
				{
					GuildRecruitTagDef *grtd = grcd->eaGuildRecruitTagDef[k];
					if (!grtd) return;
					if (!grtd->pcName) return;
					if (grsc->pcName == grtd->pcName) break;
				}
				if (k >= 0) break;
			}
			if (j < 0) return;
		}

		pRequest->iRequesterLevel = entity_GetSavedExpLevel(pEnt);
		pRequest->iRequesterAccountID = entGetAccountID(pEnt);
		pRequest->iRequesterEntID = entGetContainerID(pEnt);
		pRequest->iRequesterVirtualShardID = entGetVirtualShardID(pEnt);

		RemoteCommand_Guild_GetGuilds(objCreateManagedReturnVal(gslGuild_GetGuildsSearch_CB, (void *)((intptr_t)entGetRef(pEnt))),GLOBALTYPE_GUILDSERVER, 0, pRequest);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_SetRecruitMessage) ACMD_HIDE;
void gslGuild_cmd_SetRecruitMessage(SA_PARAM_NN_VALID Entity *pEnt, ACMD_SENTENCE pcRecruitMessage)
{
	if (!guild_IsMember(pEnt)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetRecruitMessage", "GuildServer_SelfNotInGuild", true);
		return;
	}
	if (pcRecruitMessage && strlen(pcRecruitMessage) > 500)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetRecruitMessage", "GuildServer_RecruitMessageTooLong", true);
		return;
	}
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_SetRecruitMessage(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, pEnt->myContainerID, pcRecruitMessage, GUILD_SERVERDOWNHANDLER(pEnt));
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_SetWebSite) ACMD_HIDE;
void gslGuild_cmd_SetWebSite(SA_PARAM_NN_VALID Entity *pEnt, ACMD_SENTENCE pcWebSite)
{
	if (!guild_IsMember(pEnt)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetWebSite", "GuildServer_SelfNotInGuild", true);
		return;
	}
	if (pcWebSite && strlen(pcWebSite) > 200)
	{
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetWebSite", "GuildServer_WebSiteTooLong", true);
		return;
	}
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_SetWebSite(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, pEnt->myContainerID, pcWebSite, GUILD_SERVERDOWNHANDLER(pEnt));
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_SetRecruitCat) ACMD_HIDE;
void gslGuild_cmd_SetRecruitCat(SA_PARAM_NN_VALID Entity *pEnt, char *pcRecruitCat, int bSet)
{
	int i, j;

	if ((!pcRecruitCat) || (!*pcRecruitCat)) return;
	if (!guild_IsMember(pEnt)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetRecruitCat", "GuildServer_SelfNotInGuild", true);
		return;
	}
	if (bSet)
	{
		GuildRecruitParam *grp = Guild_GetGuildRecruitParams();
		for (i = eaSize(&grp->eaGuildRecruitCatDef)-1; i >= 0; --i)
		{
			GuildRecruitCatDef *grcd = grp->eaGuildRecruitCatDef[i];
			if (!grcd) continue;
			if (!grcd->pcName) continue;
			for (j = eaSize(&grcd->eaGuildRecruitTagDef)-1; j >= 0; --j)
			{
				GuildRecruitTagDef *grtd = grcd->eaGuildRecruitTagDef[j];
				if (grtd && grtd->pcName && !stricmp(grtd->pcName, pcRecruitCat))
				{
					break;
				}
			}
			if (j >= 0) break;
		}
		if (i < 0)
		{
			gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetRecruitCat", "GuildServer_InvalidCategoryName", true);
			return;
		}
	}
	else
	{
		Guild *pGuild = guild_GetGuild(pEnt);
		for (i = eaSize(&pGuild->eaRecruitCat)-1; i >= 0; --i)
		{
			if (pGuild->eaRecruitCat[i] && pGuild->eaRecruitCat[i]->pcName && !stricmp(pGuild->eaRecruitCat[i]->pcName, pcRecruitCat))
			{
				break;
			}
		}
		if (i < 0)
		{
			gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetRecruitCat", "GuildServer_InvalidCategoryName2", true);
			return;
		}
	}
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_SetRecruitCat(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, pEnt->myContainerID, pcRecruitCat, bSet, GUILD_SERVERDOWNHANDLER(pEnt));
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_SetMinLevelRecruit) ACMD_HIDE;
void gslGuild_cmd_SetMinLevelRecruit(SA_PARAM_NN_VALID Entity *pEnt, int iMin)
{
	if (!guild_IsMember(pEnt)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetMinLevelRecruit", "GuildServer_SelfNotInGuild", true);
		return;
	}
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_SetMinLevelRecruit(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, pEnt->myContainerID, iMin, GUILD_SERVERDOWNHANDLER(pEnt));
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_SetRecruitVisibility) ACMD_HIDE;
void gslGuild_cmd_SetRecruitVisibility(SA_PARAM_NN_VALID Entity *pEnt, int bShow)
{
	if (!guild_IsMember(pEnt)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetRecruitVisibility", "GuildServer_SelfNotInGuild", true);
		return;
	}
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_SetRecruitVisibility(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, pEnt->myContainerID, bShow, GUILD_SERVERDOWNHANDLER(pEnt));
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(XMLRPC, Guild) ACMD_NAME(Guild_SetRecruitMemberVisibility) ACMD_HIDE;
void gslGuild_cmd_SetRecruitMemberVisibility(SA_PARAM_NN_VALID Entity *pEnt, int bShow)
{
	if (!guild_IsMember(pEnt)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_SetRecruitVisibility", "GuildServer_SelfNotInGuild", true);
		return;
	}
	VerifyServerTypeExistsInShard(GLOBALTYPE_GUILDSERVER);
	RemoteCommand_aslGuild_SetRecruitMemberVisibility(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, pEnt->myContainerID, bShow, GUILD_SERVERDOWNHANDLER(pEnt));
}

///////////////////////////////////////////////////////////////////////////////////////////
// Other Functionality
///////////////////////////////////////////////////////////////////////////////////////////

static void gslGuild_AddCostumeSlot_CB(TransactionReturnVal *pReturn, GSLGuildCBData *pData)
{
	Entity *pEnt = entFromEntityRefAnyPartition(pData->iRef);
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild && pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS) {
		pEnt->pPlayer->pGuild->bJoinedGuild = true;
	}
	SAFE_FREE(pData);
}

static void gslGuild_ProcessEvents(Entity *pEnt, Guild *pGuild, GuildMember *pMember)
{
	int i, j;
	ContainerID iEntID = entGetContainerID(pEnt);
	bool bNotifyEventChange = false;
	U32 iCurrentTime = timeSecondsSince2000();

	// First, remove expired events and reschedule recurring events that need rescheduling
	for (i = eaSize(&pGuild->eaEvents)-1; i >= 0; i--)
	{
		GuildEventReply *pGuildEventReply = NULL;
		GuildEventReplyType eReplyType = 0;
		U32 iAcceptedCount = 0;
		GuildEvent *pGuildEvent = pGuild->eaEvents[i];

		// If the entity is not eligible for this event, skip it
		if (pMember->iRank < pGuildEvent->iMinGuildRank ||
			pMember->iLevel < pGuildEvent->iMinLevel || pMember->iLevel > pGuildEvent->iMaxLevel)
		{
			continue;
		}

		// Process event replies
		for (j = eaSize(&pGuildEvent->eaReplies)-1; j >= 0; j--)
		{
			if (pGuildEvent->eaReplies[j]->iStartTime != pGuildEvent->iStartTimeTime)
				continue;

			if (pGuildEvent->eaReplies[j]->eGuildEventReplyType == GuildEventReplyType_Accept ||
				pGuildEvent->eaReplies[j]->eGuildEventReplyType == GuildEventReplyType_Maybe)
			{
				GuildMember *pReplyMember = eaIndexedGetUsingInt(&pGuild->eaMembers, pGuildEvent->eaReplies[j]->iMemberID);
				if (pReplyMember && pReplyMember->iRank >= pGuildEvent->iMinGuildRank &&
					pReplyMember->iLevel >= pGuildEvent->iMinLevel && pReplyMember->iLevel <= pGuildEvent->iMaxLevel)
				{
					iAcceptedCount++;
				}
			}
		}

		pGuildEventReply = eaIndexedGetUsingInt(&pGuildEvent->eaReplies, guildevent_GetReplyKey(iEntID, pGuildEvent->iStartTimeTime));
		if (!pGuildEventReply)
		{
			continue;
		}

		// If the event was not refused, and the event has changed, send an event change notification
		if (pEnt->pPlayer->pGuild->uiDisplayedEvent < pGuildEvent->iEventUpdated &&
			pGuildEventReply->eGuildEventReplyType != GuildEventReplyType_Refuse)
		{
			bNotifyEventChange = true;
		}

		// Send out notifications for the event's start if the player accepted the event invitation
		if (!pGuildEvent->bCanceled &&
			iAcceptedCount >= pGuildEvent->iMinAccepts &&
			(pGuildEventReply->eGuildEventReplyType == GuildEventReplyType_Maybe ||
			 pGuildEventReply->eGuildEventReplyType == GuildEventReplyType_Accept))
		{
			if (pGuildEvent->iStartTimeTime - iCurrentTime <= 0 && pGuildEventReply->iLastRemindedTime < pGuildEvent->iStartTimeTime)
			{
				gslGuild_SendEventReminder(pEnt, pGuildEvent, "GuildServer_Event_Started");
			}
			else if (pGuildEvent->iStartTimeTime - iCurrentTime <= MINUTES(15) && pGuildEventReply->iLastRemindedTime < pGuildEvent->iStartTimeTime - MINUTES(15))
			{
				gslGuild_SendEventReminder(pEnt, pGuildEvent, "GuildServer_Event_FifteenMin");
			}
			else if (pGuildEvent->iStartTimeTime - iCurrentTime <= MINUTES(60) && pGuildEventReply->iLastRemindedTime < pGuildEvent->iStartTimeTime - MINUTES(60))
			{
				gslGuild_SendEventReminder(pEnt, pGuildEvent, "GuildServer_Event_OneHour");
			}

			pGuildEventReply->iLastRemindedTime = iCurrentTime;
		}
	}

	if (bNotifyEventChange)
	{
		char *estrTemp = NULL;
		entFormatGameMessageKey(pEnt, &estrTemp, "GuildServer_Event_Change", STRFMT_END);
		ClientCmd_NotifySend(pEnt, kNotifyType_GuildMotD, estrTemp, NULL, NULL);
		pEnt->pPlayer->pGuild->uiDisplayedEvent = iCurrentTime;
		estrDestroy(&estrTemp);
	}
}

// Removes a guilds emblem
AUTO_COMMAND ACMD_NAME(BadEmblem) ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(csr);
void costume_BadEmblemCmd(Entity *pEnt)
{
	if (entGetType(pEnt) != GLOBALTYPE_ENTITYPLAYER)
	{
		pEnt = entGetOwner(pEnt);
	}
	if (guild_IsMember(pEnt))
	{
		RemoteCommand_aslGuild_SetBadEmblem(GLOBALTYPE_GUILDSERVER, 0, guild_GetGuildID(pEnt), entGetContainerID(pEnt));
	}
}

static void gslGuild_FixupCostume(Entity *pPlayerEnt, Entity *pEnt, Guild *pGuild)
{
	int i;
	if (pEnt->pSaved && pEnt->pChar && pEnt->pSaved->costumeData.iActiveCostume >= 0 && pEnt->pSaved->costumeData.iActiveCostume < eaSize(&pEnt->pSaved->costumeData.eaCostumeSlots))
	{
		PlayerCostume *pPC = pEnt->pSaved->costumeData.eaCostumeSlots[pEnt->pSaved->costumeData.iActiveCostume]->pCostume;
		if (pPC)
		{
			for (i = eaSize(&pPC->eaParts)-1; i >= 0; --i)
			{
				PCPart *pPart = pPC->eaParts[i];
				PCBoneDef *pBone = GET_REF(pPart->hBoneDef);
				if (!pBone) continue;
				if (!pBone->bIsGuildEmblemBone) continue;
				if (costumeTailor_PartHasBadGuildEmblem(CONTAINER_NOCONST(PCPart, pPart), pGuild))
				{
					SpeciesDef *pSpecies = GET_REF(pEnt->pChar->hSpecies);
					NOCONST(PlayerCostume) *pPCCopy = StructCloneDeConst(parse_PlayerCostume, pPC);
					NOCONST(PCPart) *pPart2 = NULL;
					PCSlotType *pSlotType = costumeEntity_GetSlotType(pEnt, pEnt->pSaved->costumeData.iActiveCostume, false, NULL);

					StructDestroyNoConst(parse_PCPart, pPCCopy->eaParts[i]);
					eaRemove(&pPCCopy->eaParts, i);

					if (costumeTailor_IsBoneRequired(pPCCopy, pBone, pSpecies))
					{
						pPart2 = StructCreateNoConst(parse_PCPart);
						SET_HANDLE_FROM_REFERENT("CostumeBone", pBone, pPart2->hBoneDef);
						eaPush(&pPCCopy->eaParts, pPart2);
						costumeTailor_PickValidPartValues(pPCCopy, pPart2, pSpecies, pSlotType, NULL, false, false, true, false, pGuild);
					}

					// Store new costume
					if (pEnt && pEnt != pPlayerEnt)
					{
						costumetransaction_StorePlayerCostume(pPlayerEnt, pEnt, kPCCostumeStorageType_Pet, pEnt->pSaved->costumeData.iActiveCostume, (PlayerCostume*)pPCCopy, SAFE_MEMBER(pSlotType, pcName), 0, kPCPay_Default);
					}
					else
					{
						PCCostumeStorageType eCostumeType;
						CharClassTypes Class = GetCharacterClassEnum( pEnt );
						if ( Class == StaticDefineIntGetInt(CharClassTypesEnum, "Space") )
						{
							eCostumeType = kPCCostumeStorageType_SpacePet;
						}
						else
						{
							eCostumeType = kPCCostumeStorageType_Primary;
						}
						costumetransaction_StorePlayerCostume(pEnt, NULL, eCostumeType, pEnt->pSaved->costumeData.iActiveCostume, (PlayerCostume*)pPCCopy, SAFE_MEMBER(pSlotType, pcName), 0, kPCPay_Default);
					}

					StructDestroyNoConst(parse_PlayerCostume, pPCCopy);
				}
			}
		}
	}
}

// This function is called periodically for each entity that has a guild structure, to update
// and validate all their guild info.
void gslGuild_EntityUpdate(Entity *pEnt)
{
	GSLGuildCBData *pData = NULL;
	char pcNameBuff[512];
	Guild *pGuild;
	int iNumMembers;
	GuildMember *pMember = NULL;
	U32 iCurTime;
	int i;
	
	// Check if the player is in a guild at all, and if not, make sure their guild data is cleared
	if (!pEnt->pPlayer->pGuild->iGuildID) {
		bool bDirty = false;

		PERFINFO_AUTO_START("PlayerNotInGuild",1);
		
		// If their guild handle is still active, clear it
		if (IS_HANDLE_ACTIVE(pEnt->pPlayer->pGuild->hGuild))
		{
			REMOVE_HANDLE(pEnt->pPlayer->pGuild->hGuild);
			if(IS_HANDLE_ACTIVE(pEnt->pPlayer->pGuild->hGuildBank))
				REMOVE_HANDLE(pEnt->pPlayer->pGuild->hGuildBank);

			// Clear their guild version
			pEnt->pPlayer->pGuild->iVersion = 0;
			// Clear the MotD timestamp
			pEnt->pPlayer->pGuild->uiDisplayedMotD = 0;

			bDirty = true;
		}
	
		// Remove them from guild chat and officer chat, if they're in them
		if (pEnt->pPlayer->pGuild->iGuildChat) {
			guild_GetGuildChannelNameFromID(pcNameBuff, sizeof(pcNameBuff), pEnt->pPlayer->pGuild->iGuildChat, GAMESERVER_VSHARD_ID);
			RemoteCommand_ChatServerLeaveChannel_Special(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, pEnt->myContainerID, pcNameBuff);
			pEnt->pPlayer->pGuild->iGuildChat = 0;
			bDirty = true;
		}
		if (pEnt->pPlayer->pGuild->iOfficerChat) {
			guild_GetOfficerChannelNameFromID(pcNameBuff, sizeof(pcNameBuff), pEnt->pPlayer->pGuild->iOfficerChat, GAMESERVER_VSHARD_ID);
			RemoteCommand_ChatServerLeaveChannel_Special(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, pEnt->myContainerID, pcNameBuff);
			pEnt->pPlayer->pGuild->iOfficerChat = 0;
			bDirty = true;
		}
		
		// Clear their guild name field
		if (pEnt->pPlayer->pcGuildName) {
			StructFreeString(pEnt->pPlayer->pcGuildName);
			pEnt->pPlayer->pcGuildName = NULL;
			pEnt->pPlayer->iGuildID = 0;
			bDirty = true;
		}
		
		// If they still have a local guild bank log, clear it
		if (eaSize(&pEnt->pPlayer->pGuild->eaBankLog)) {
			gslGuild_cmd_ClearLog(pEnt);
			bDirty = true;
		}

		if (bDirty)
		{
			entity_SetDirtyBit(pEnt, parse_PlayerGuild, pEnt->pPlayer->pGuild, true);
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		}
		
		PERFINFO_AUTO_STOP();
		return;
	}

	PERFINFO_AUTO_START("CheckGuildHandle", 1);
	
	// If the guild handle isn't yet active, we need to make it active before we can do anything else
	if (!IS_HANDLE_ACTIVE(pEnt->pPlayer->pGuild->hGuild)) {
		sprintf(pcNameBuff, "%d", pEnt->pPlayer->pGuild->iGuildID);
		SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GUILD), pcNameBuff, pEnt->pPlayer->pGuild->hGuild);

		pEnt->pPlayer->pGuild->iTimeSinceHandleInit = timeSecondsSince2000();
		entity_SetDirtyBit(pEnt, parse_PlayerGuild, pEnt->pPlayer->pGuild, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
	
	// If the guild handle isn't yet active, we need to make it active before we can do anything else
	if (!IS_HANDLE_ACTIVE(pEnt->pPlayer->pGuild->hGuildBank) && !guild_LazySubscribeToBank()) {
		sprintf(pcNameBuff, "%d", pEnt->pPlayer->pGuild->iGuildID);
		SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYGUILDBANK), pcNameBuff, pEnt->pPlayer->pGuild->hGuildBank);

		pEnt->pPlayer->pGuild->iTimeSinceHandleInit = timeSecondsSince2000();
		entity_SetDirtyBit(pEnt, parse_PlayerGuild, pEnt->pPlayer->pGuild, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}

	iCurTime = timeSecondsSince2000();

	pGuild = GET_REF(pEnt->pPlayer->pGuild->hGuild);
	if (!pGuild) {
		// The guild data hasn't been downloaded yet so no further updating or validation is possible
		if(pEnt->pPlayer->pGuild->iTimeSinceHandleInit == 0)
		{
			// first time wait for a minute, then check as subscription could be late
			// This could delay later checks using time but those aren't that important
			pEnt->pPlayer->pGuild->iTimeSinceHandleInit = iCurTime + GUILD_VALIDATE_MEMBER_TIME;
		}
		else if(iCurTime > pEnt->pPlayer->pGuild->iTimeSinceHandleInit)
		{
			// Loading the guild data has timed out, meaning the guild probably doesn't exist,
			// so we need to validate that the guild actually exists, and clear the player's
			// guild data if it doesn't. This remote command handles both.
			RemoteCommand_aslGuild_ValidateMember(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, pEnt->myContainerID);
			pEnt->pPlayer->pGuild->iTimeSinceHandleInit = GUILD_VALIDATE_MEMBER_TIME_FAIL;	// don't check again, if later checks are done this will cause them to underflow and check right away which is correct
		}
		PERFINFO_AUTO_STOP();
		return;
	}

	PERFINFO_AUTO_STOP(); // CheckGuildHandle
	
	// Regular updates to do on guild members even when the version hasn't been updated
	if (guild_IsMember(pEnt)) {
		U32 iLevel;
		int eLFGMode;
		const char *pcMapName;
		const char *pcMapVars;
		U32 iInstanceNum;
		const char *pcStatus;

		PERFINFO_AUTO_START("MakeSureReallyAMember", 1);
		// If pMember is NULL, they may not really be in the guild
		pMember = guild_FindMember(pEnt);
		if (!pMember) {
			RemoteCommand_aslGuild_ValidateMember(GLOBALTYPE_GUILDSERVER, 0, pGuild->iContainerID, pEnt->myContainerID);
			PERFINFO_AUTO_STOP();
			return;
		}
		PERFINFO_AUTO_STOP(); // MakeSureReallyAMember

		PERFINFO_AUTO_START("CheckGuildChatState", 1);
		
		// Make sure they're in guild chat if they have guild chat permissions
		if (!pEnt->pPlayer->pGuild->iGuildChat && guild_HasPermission(pMember->iRank, pGuild, GuildPermission_Chat)) {
			guild_GetGuildChannelNameFromID(pcNameBuff, sizeof(pcNameBuff), pGuild->iContainerID, GAMESERVER_VSHARD_ID);
			RemoteCommand_ChatServerJoinOrCreateChannel_Special(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, 
				pEnt->myContainerID, pcNameBuff, CHANNEL_SPECIAL_GUILD);
		}
		
		// Make sure they're not in guild chat if they don't have the guild chat permission
		if (pEnt->pPlayer->pGuild->iGuildChat && !guild_HasPermission(pMember->iRank, pGuild, GuildPermission_Chat)) {
			guild_GetGuildChannelNameFromID(pcNameBuff, sizeof(pcNameBuff), pEnt->pPlayer->pGuild->iGuildChat, GAMESERVER_VSHARD_ID);
			RemoteCommand_ChatServerLeaveChannel_Special(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, pEnt->myContainerID, pcNameBuff);
		}
		
		// Make sure they're in officer chat, if they have officer chat permissions
		if (!pEnt->pPlayer->pGuild->iOfficerChat && guild_HasPermission(pMember->iRank, pGuild, GuildPermission_OfficerChat)) {
			guild_GetOfficerChannelNameFromID(pcNameBuff, sizeof(pcNameBuff), pGuild->iContainerID, GAMESERVER_VSHARD_ID);
			RemoteCommand_ChatServerJoinOrCreateChannel_Special(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, 
				pEnt->myContainerID, pcNameBuff, CHANNEL_SPECIAL_OFFICER);
		}
		
		// Make sure they're not in officer chat if they don't have the officer chat permission
		if (pEnt->pPlayer->pGuild->iOfficerChat && !guild_HasPermission(pMember->iRank, pGuild, GuildPermission_OfficerChat)) {
			guild_GetOfficerChannelNameFromID(pcNameBuff, sizeof(pcNameBuff), pEnt->pPlayer->pGuild->iGuildChat, GAMESERVER_VSHARD_ID);
			RemoteCommand_ChatServerLeaveChannel_Special(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, pEnt->myContainerID, pcNameBuff);
		}

		PERFINFO_AUTO_STOP(); // CheckGuildChatState
		
		PERFINFO_AUTO_START("CheckMotD", 1);
		// If the MotD has changed since they last saw it, or they haven't seen it for
		// over a half a day, show it again.
		if (pEnt->pPlayer->pGuild->uiDisplayedMotD < pGuild->iMotDUpdated || pEnt->pPlayer->pGuild->uiDisplayedMotD < iCurTime - HOURS(12)) {
			gslGuild_cmd_MotD(pEnt);
			pEnt->pPlayer->pGuild->uiDisplayedMotD = iCurTime;
		}
		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("gslGuild_ProcessEvents", 1);
		gslGuild_ProcessEvents(pEnt, pGuild, pMember);
		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("CheckDataCachedOnGuild", 1);

		if (team_GetTeamID(pEnt))
		{
			Team *t = team_GetTeam(pEnt);

			if (t && team_IsTeamLeader(pEnt))
			{
				if (team_NumTotalMembers(t) >= TEAM_MAX_SIZE)
					eLFGMode = TeamMode_Closed; // Team is full
				else
					eLFGMode = t->eMode;
			}
			else
			{
				eLFGMode = TeamMode_Closed;
			}
		}
		else
		{		
			eLFGMode = pEnt->pPlayer->eLFGMode;
		}

		// Make sure the level, location and status info is up to date
		iLevel = entity_GetSavedExpLevel(pEnt);
		pcMapName = zmapInfoGetPublicName(NULL);
		pcMapVars = partition_MapVariablesFromIdx(entGetPartitionIdx(pEnt));
		iInstanceNum = (gGSLState.gameServerDescription.baseMapDescription.eMapType == ZMTYPE_STATIC || gGSLState.gameServerDescription.baseMapDescription.eMapType == ZMTYPE_SHARED) ? partition_PublicInstanceIndexFromIdx(entGetPartitionIdx(pEnt)) : 0;
		pcStatus = pEnt->pPlayer->pUI->pChatConfig ? pEnt->pPlayer->pUI->pChatConfig->pchStatusMessage : NULL;

		if (iCurTime - pEnt->pPlayer->pGuild->iTimeSinceLastUpdate > GUILD_TIME_OUT_INTERVAL &&
			(pMember->iLevel != iLevel || iInstanceNum != pMember->iMapInstanceNumber || !pMember->bOnline || pMember->eLFGMode != (U32)eLFGMode ||
			 strcmp(NULL_TO_EMPTY(pMember->pcMapName), NULL_TO_EMPTY(pcMapName)) ||
			 strcmp(NULL_TO_EMPTY(pMember->pcMapVars), NULL_TO_EMPTY(pcMapVars)) ||
			 strcmp(NULL_TO_EMPTY(pMember->pcStatus), NULL_TO_EMPTY(pcStatus)))) 
		{
			S32 iOfficerRank = Officer_GetRank(pEnt);
			const char *pcMapMsgKey = zmapInfoGetDisplayNameMsgKey(NULL);
			RemoteCommand_aslGuild_UpdateInfo(GLOBALTYPE_GUILDSERVER, 0, pGuild->iContainerID, pEnt->myContainerID, iLevel, iOfficerRank, pcMapName, pcMapMsgKey, pcMapVars, iInstanceNum, eLFGMode, pcStatus, gslGuild_GetClassName(pEnt), true);
			pEnt->pPlayer->pGuild->iTimeSinceLastUpdate = iCurTime;
		}
		
		// Make sure that the player name and account name info is up to date
		if (iCurTime - pEnt->pPlayer->pGuild->iTimeSinceLastUpdate > GUILD_TIME_OUT_INTERVAL && (!pMember->pcLogName || strcmp(pMember->pcLogName, pEnt->debugName))) {
			RemoteCommand_aslGuild_UpdateName(GLOBALTYPE_GUILDSERVER, 0, pGuild->iContainerID, pEnt->myContainerID);
			pEnt->pPlayer->pGuild->iTimeSinceLastUpdate = iCurTime;
		}

		PERFINFO_AUTO_STOP(); // CheckDataCachedOnGuild

		if (pEnt->pSaved)
		{
			int iPartitionIdx;

			PERFINFO_AUTO_START("CheckGuildCostume", 1);
			
			iPartitionIdx = entGetPartitionIdx(pEnt);
			gslGuild_FixupCostume(pEnt, pEnt, pGuild);
			for ( i = eaSize(&pEnt->pSaved->ppOwnedContainers)-1; i >= 0; --i )
			{
				PetRelationship* pPet = pEnt->pSaved->ppOwnedContainers[i];
				Entity* pPetEnt = SavedPet_GetEntity(iPartitionIdx, pPet);
				if ( pPetEnt==NULL ) continue;
				if ( pPetEnt->myEntityType != GLOBALTYPE_ENTITYSAVEDPET ) continue;
				gslGuild_FixupCostume(pEnt, pPetEnt, pGuild);
			}

			PERFINFO_AUTO_STOP(); // CheckGuildCostume
		}
	}

	if ((pGuild->iOCVersion != pEnt->pPlayer->pGuild->iOCVersion || !pEnt->pPlayer->pGuild->bUpdatedOfficerComments) &&
		pEnt->pPlayer->pGuild->eState == GuildState_Member)
	{
		PERFINFO_AUTO_START("UpdateOfficerComments", 1);

		eaClearStruct(&pEnt->pPlayer->pGuild->eaOfficerComments, parse_PlayerGuildOfficerComments);
		if (!pMember) {
			pMember = guild_FindMember(pEnt);
		}
		if (pMember && guild_HasPermission(pMember->iRank, pGuild, GuildPermission_SeeOfficerComment))
		{
			for (i = eaSize(&pGuild->eaMembers)-1; i >= 0; --i)
			{
				GuildMember *pGuildMember = pGuild->eaMembers[i];
				if (pGuildMember->pcWhoOfficerComment && *pGuildMember->pcWhoOfficerComment &&
					pMember->iRank > pGuildMember->iRank)
				{
					PlayerGuildOfficerComments *pPGOC = StructCreate(parse_PlayerGuildOfficerComments);
					pPGOC->iEntID = pGuildMember->iEntID;
					if (pGuildMember->pcOfficerComment && *pGuildMember->pcOfficerComment)
					{
						pPGOC->pcOfficerComment = StructAllocString(pGuildMember->pcOfficerComment);
					}
					pPGOC->pcWhoOfficerComment = StructAllocString(pGuildMember->pcWhoOfficerComment);
					pPGOC->iOfficerCommentTime = pGuildMember->iOfficerCommentTime;
					eaPush(&pEnt->pPlayer->pGuild->eaOfficerComments, pPGOC);
				}
			}
		}
		pEnt->pPlayer->pGuild->bUpdatedOfficerComments = true;
		pEnt->pPlayer->pGuild->iOCVersion = pGuild->iOCVersion;
		entity_SetDirtyBit(pEnt, parse_PlayerGuild, pEnt->pPlayer->pGuild, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);

		PERFINFO_AUTO_STOP();
	}

	if (pGuild->iVersion == pEnt->pPlayer->pGuild->iVersion) {
		// Nothing has been changed, so no further updating or validation is necessary
		return;
	}
	
	// Check the guild state to figure out whether the entity thinks it's a member of the guild, or invited to
	// join the guild, and do the appropriate validation and updating.
	switch (pEnt->pPlayer->pGuild->eState) {
		case GuildState_Member:
			PERFINFO_AUTO_START("MemberUpdate",1);

			iNumMembers = eaSize(&pGuild->eaMembers);
			
			// Make sure the player is really in the guild
			// If the guild container and the entity are in conflict, the guild container wins
			if (!pMember) {
				pMember = guild_FindMember(pEnt);
			}
			if (!pMember) {
				// The entity may not actually be in the guild
				RemoteCommand_aslGuild_ValidateMember(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, pEnt->myContainerID);
			}
			
			// Make sure that the guild name stored on the player is up to date
			if (pEnt->pPlayer->pcGuildName) {
				StructFreeString(pEnt->pPlayer->pcGuildName);
			}
			pEnt->pPlayer->pcGuildName = StructAllocString(pGuild->pcName);
			pEnt->pPlayer->iGuildID = pGuild->iContainerID;
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
			
			if (!pEnt->pPlayer->pGuild->bJoinedGuild) {
				pEnt->pPlayer->pGuild->bJoinedGuild = true;
				if (!gConf.bDisableGuildFreeCostumeViaNumeric) {
					ItemChangeReason reason = {0};
					pData = gslGuild_MakeCBData(pEnt->myRef, NULL, NULL);
					inv_FillItemChangeReason(&reason, pEnt, "Guild:GrantFreeCostumeOnGuildJoin", pGuild->pcName);
					itemtransaction_AddNumeric(pEnt, "primarycostume", 1, &reason, gslGuild_AddCostumeSlot_CB, pData);
				}
			}
			
			pEnt->pPlayer->pGuild->iVersion = pGuild->iVersion;
			
			PERFINFO_AUTO_STOP();
			break;

		case GuildState_Invitee:
			PERFINFO_AUTO_START("InviteeUpdate", 1);

			// Make sure the entity is really invited to the guild
			// If the guild container and the entity are in conflict, the guild container wins
			for (i = eaSize(&pGuild->eaInvites)-1; i >= 0; i--) {
				if (pGuild->eaInvites[i]->iEntID == pEnt->myContainerID) {
					break;
				}
			}
			if (i < 0) {
				// The entity may not be actually invited to the guild
				RemoteCommand_aslGuild_ValidateInvite(GLOBALTYPE_GUILDSERVER, 0, pEnt->pPlayer->pGuild->iGuildID, pEnt->myContainerID);
			}

			if ( ( pEnt->pPlayer != NULL ) && ( pEnt->pPlayer->pUI != NULL ) && 
				( pEnt->pPlayer->pUI->pChatState != NULL ) && ( pEnt->pPlayer->pUI->pChatState->eaIgnores != NULL ) )
			{
				int n;
				n = eaSize(&pEnt->pPlayer->pUI->pChatState->eaIgnores);
				for ( i = 0; i < n; i++ )
				{
					if ( pEnt->pPlayer->pUI->pChatState->eaIgnores[i]->chatHandle && pEnt->pPlayer->pGuild->pcInviterHandle && !stricmp(pEnt->pPlayer->pUI->pChatState->eaIgnores[i]->chatHandle, pEnt->pPlayer->pGuild->pcInviterHandle) )
					{
						//Reject Invite
						gslGuild_cmd_DeclineInvite(pEnt);
						break;
					}
				}
			}

			pEnt->pPlayer->pGuild->iVersion = pGuild->iVersion;
			
			PERFINFO_AUTO_STOP();
			break;
	}
}

void gslGuild_HandlePlayerGuildChange(Entity *pEnt) {
	entity_SetDirtyBit(pEnt, parse_PlayerGuild, pEnt->pPlayer->pGuild, true);
	entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
}

///////////////////////////////////////////////////////////////////////////////////////////
// GuildRaider
///////////////////////////////////////////////////////////////////////////////////////////

typedef struct GuildRaiderBaton
{
	bool bCreate;
	bool bDisband;
	int iDiv;
	int iBatches;
	int iFirstBatch;
	int iCurBatch;
	int iGuildSize;
	int iRequests;
	int iResponses;
	ContainerList *pList;
	EntityIterator *pIter;
	int iCallerID;
	int iNumChanges;
	int iMaxChanges;
} GuildRaiderBaton;

int guildRaidsPerFrame = 10;
AUTO_CMD_INT(guildRaidsPerFrame, GuildRaidsPerFrame);

void GuildRaider_Dispatch(GuildRaiderBaton *pBaton);

static void GuildRaider_ReturnContainers_CB(TimedCallback *pCallback, F32 timeSinceLastCallback, GuildRaiderBaton *pBaton)
{
	int i = 0;
	Entity *pEnt = NULL;

	while((pEnt = EntityIteratorGetNext(pBaton->pIter)))
	{
		objRequestContainerMove(NULL, GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEnt), GetAppGlobalType(), GetAppGlobalID(), GLOBALTYPE_OBJECTDB, 0);
		++i;

		if(i >= 256)
		{
			TimedCallback_Run(GuildRaider_ReturnContainers_CB, pBaton, 0.1f);
			return;
		}
	}

	EntityIteratorRelease(pBaton->pIter);
	pBaton->pIter = NULL;
	GuildRaider_Dispatch(pBaton);
}

static void GuildRaider_FillGuilds2_CB(TimedCallback *pCallback, F32 timeSinceLastCallback, GuildRaiderBaton *pBaton)
{
	int i = 0;
	Entity *pLeader = NULL;
	Entity *pInvitee = NULL;

	while((pLeader = EntityIteratorGetNext(pBaton->pIter)))
	{
		int j = 0;

		if(!guild_WithGuild(pLeader))
		{
			break;
		}

		for(j = 1; j < pBaton->iGuildSize; ++j)
		{
			pInvitee = EntityIteratorGetNext(pBaton->pIter);

			RemoteCommand_aslGuild_AcceptInvite(GLOBALTYPE_GUILDSERVER, 0, guild_GetGuildID(pInvitee), entGetContainerID(pInvitee), gslGuild_GetClassName(pInvitee), GUILD_SERVERDOWNHANDLER(pInvitee));
		}

		++i;

		if(i >= guildRaidsPerFrame)
		{
			TimedCallback_Run(GuildRaider_FillGuilds2_CB, pBaton, 0.0f);
			return;
		}
	}

	EntityIteratorRelease(pBaton->pIter);
	pBaton->pIter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
	TimedCallback_Run(GuildRaider_ReturnContainers_CB, pBaton, 0.0f);
}

static void GuildRaider_FillGuilds_CB(TimedCallback *pCallback, F32 timeSinceLastCallback, GuildRaiderBaton *pBaton)
{
	int i = 0;
	Entity *pCreator = NULL;
	Entity *pInvitee = NULL;

	while((pCreator = EntityIteratorGetNext(pBaton->pIter)))
	{
		int j;

		if(!guild_WithGuild(pCreator))
		{
			break;
		}

		for(j = 1; j < pBaton->iGuildSize; ++j)
		{
			pInvitee = EntityIteratorGetNext(pBaton->pIter);

			RemoteCommand_aslGuild_JoinWithoutInvite(GLOBALTYPE_GUILDSERVER, 0, guild_GetGuildID(pCreator), entGetContainerID(pInvitee), gslGuild_GetClassName(pInvitee));
		}

		++i;
		
		if(i >= guildRaidsPerFrame)
		{
			TimedCallback_Run(GuildRaider_FillGuilds_CB, pBaton, 0.0f);
			return;
		}
	}

	EntityIteratorRelease(pBaton->pIter);
	pBaton->pIter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
	TimedCallback_Run(GuildRaider_ReturnContainers_CB, pBaton, 0.0f);
	//TimedCallback_Run(GuildRaider_FillGuilds2_CB, pBaton, 0.0f);
}

static void GuildRaider_MakeGuilds_CB(TimedCallback *pCallback, F32 timeSinceLastCallback, GuildRaiderBaton *pBaton)
{
	int i = 0;
	Entity *pCreator = NULL;

	if(!pBaton->pIter)
	{
		pBaton->pIter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
	}

	while((pCreator = EntityIteratorGetNext(pBaton->pIter)))
	{
		int j;

		for(j = 1; j < pBaton->iGuildSize; ++j)
		{
			if(!EntityIteratorGetNext(pBaton->pIter))
			{
				j = 0;
				break;
			}
		}

		if(!j)
		{
			break;
		}

		RemoteCommand_aslGuild_Create(GLOBALTYPE_GUILDSERVER, 0, entGetContainerID(pCreator), 0, 
			STACK_SPRINTF("League of Awesomeness %d", entGetContainerID(pCreator)), 
			REF_STRING_FROM_HANDLE(pCreator->hAllegiance), 1, 2, "C_Emblemf_Tight_Fire_01_Mm", "", 
			gslGuild_GetClassName(pCreator), entGetVirtualShardID(pCreator), NULL, false, GUILD_SERVERDOWNHANDLER(pCreator));
		++i;

		if(i >= guildRaidsPerFrame)
		{
			TimedCallback_Run(GuildRaider_MakeGuilds_CB, pBaton, 0.0f);
			return;
		}
	}

	EntityIteratorRelease(pBaton->pIter);
	pBaton->pIter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
	TimedCallback_Run(GuildRaider_FillGuilds_CB, pBaton, 5.0f);
}

static void GuildRaider_LeaveGuilds_CB(TimedCallback *pCallback, F32 timeSinceLastCallback, GuildRaiderBaton *pBaton)
{
	int i = 0;
	Entity *pMember = NULL;

	if(!pBaton->pIter)
	{
		pBaton->pIter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
	}

	while((pMember = EntityIteratorGetNext(pBaton->pIter)))
	{
		RemoteCommand_aslGuild_Leave(GLOBALTYPE_GUILDSERVER, 0, guild_GetGuildID(pMember), entGetContainerID(pMember), entGetContainerID(pMember), GUILD_SERVERDOWNHANDLER(pMember));
		++i;

		if(i >= guildRaidsPerFrame)
		{
			TimedCallback_Run(GuildRaider_LeaveGuilds_CB, pBaton, 0.0f);
			return;
		}
	}

	EntityIteratorRelease(pBaton->pIter);
	pBaton->pIter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
	TimedCallback_Run(GuildRaider_ReturnContainers_CB, pBaton, 0.0f);
}

static void GuildRaider_UpdateValues_CB(TimedCallback *pCallback, F32 timeSinceLastCallback, GuildRaiderBaton *pBaton)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pBaton->iCallerID);
	Guild *pGuild = guild_GetGuild(pEnt);
	
	if (pGuild) {
		int i = 0;
		while (i < guildRaidsPerFrame && pBaton->iNumChanges < pBaton->iMaxChanges) {
			//S32 iMemberNum = randomIntRange(0, eaSize(&pGuild->eaMembers)-1);
			S32 iMemberNum = pBaton->iNumChanges % eaSize(&pGuild->eaMembers);
			GuildMember *pMember = pGuild->eaMembers[iMemberNum];
			char pcRandomStatus[128];
			S32 j;
			S32 iLength = randomIntRange(8, 127);
			for (j = 0; j < iLength; j++) {
				pcRandomStatus[j] = randomIntRange('a', 'z');
			}
			pcRandomStatus[j] = '\0';
			RemoteCommand_aslGuild_UpdateInfo(GLOBALTYPE_GUILDSERVER, 0, pGuild->iContainerID, pMember->iEntID, randomIntRange(1,40), -1, NULL, NULL, NULL, randomIntRange(1,40), 0, pcRandomStatus, gslGuild_GetClassName(pEnt), true);
			pBaton->iNumChanges++;
			i++;
		}
		if (pBaton->iNumChanges < pBaton->iMaxChanges) {
			TimedCallback_Run(GuildRaider_UpdateValues_CB, pBaton, 0.0f);
		} else {
			printf("GuildRaider done.\n");
			free(pBaton);
		}
	}
}

static void GuildRaider_DoStuff_CB(TimedCallback *pCallback, F32 timeSinceLastCallback, GuildRaiderBaton *pBaton)
{
	int i = 0;
	Entity *pMember = NULL;
	Entity *pLastLeader = NULL;
	Guild *pGuild;
	GuildMember *pGuildMember;
	GuildMember *pGuildInvitee;
	bool bLeader;

	while((pMember = EntityIteratorGetNext(pBaton->pIter)))
	{
		int j = 0;

		if(i >= guildRaidsPerFrame)
		{
			TimedCallback_Run(GuildRaider_DoStuff_CB, pBaton, 0.0f);
			return;
		}

		++i;

		pGuild = guild_GetGuild(pMember);
		pGuildMember = guild_FindMember(pMember);
		pGuildInvitee = pGuildMember ? NULL : guild_FindInvite(pMember);
		bLeader = pGuildMember ? guild_HasPermission(pGuildMember->iRank, pGuild, GuildPermission_PromoteToRank) : false;

		if(!pGuild)
		{
			if(pLastLeader)
			{
				RemoteCommand_aslGuild_Invite(GLOBALTYPE_GUILDSERVER, 0, guild_GetGuildID(pLastLeader), entGetContainerID(pLastLeader), entGetContainerID(pMember), GUILD_SERVERDOWNHANDLER(pLastLeader));
			}

			continue;
		}
		else if(pGuildInvitee)
		{
			j = randomIntRange(0, 1);

			if(j)
			{
				RemoteCommand_aslGuild_AcceptInvite(GLOBALTYPE_GUILDSERVER, 0, pGuild->iContainerID, entGetContainerID(pMember), gslGuild_GetClassName(pMember), GUILD_SERVERDOWNHANDLER(pMember));
			}
			else
			{
				RemoteCommand_aslGuild_DeclineInvite(GLOBALTYPE_GUILDSERVER, 0, pGuild->iContainerID, entGetContainerID(pMember), GUILD_SERVERDOWNHANDLER(pMember));
			}

			continue;
		}
		
		if(bLeader)
		{
			pLastLeader = pMember;
			RemoteCommand_aslGuild_SetMotD(GLOBALTYPE_GUILDSERVER, 0, pGuild->iContainerID, entGetContainerID(pMember), "Hey look I'm setting the MotD!", false, GUILD_SERVERDOWNHANDLER(pMember));
			continue;
		}

		j = randomIntRange(0,1);

		if(j)
		{
			RemoteCommand_aslGuild_Leave(GLOBALTYPE_GUILDSERVER, 0, pGuild->iContainerID, entGetContainerID(pMember), entGetContainerID(pMember), GUILD_SERVERDOWNHANDLER(pMember));
		}
		else
		{
			if(!pLastLeader || !guild_FindMemberInGuild(pLastLeader, pGuild))
			{
				continue;
			}

			if(pGuildMember->iRank > 0)
			{
				RemoteCommand_aslGuild_Demote(GLOBALTYPE_GUILDSERVER, 0, pGuild->iContainerID, entGetContainerID(pLastLeader), entGetContainerID(pMember), GUILD_SERVERDOWNHANDLER(pLastLeader));
			}
			else
			{
				RemoteCommand_aslGuild_Promote(GLOBALTYPE_GUILDSERVER, 0, pGuild->iContainerID, entGetContainerID(pLastLeader), entGetContainerID(pMember), GUILD_SERVERDOWNHANDLER(pLastLeader));
			}
		}
	}

	EntityIteratorRelease(pBaton->pIter);
	pBaton->pIter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
	TimedCallback_Run(GuildRaider_ReturnContainers_CB, pBaton, 0.0f);
}

static void GuildRaider_EntityUpdate_CB(TimedCallback *pCallback, F32 timeSinceLastCallback, GuildRaiderBaton *pBaton)
{
	int i = 0;
	Entity *pEnt = NULL;

	if(!pBaton->pIter)
	{
		pBaton->pIter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
	}

	while((pEnt = EntityIteratorGetNext(pBaton->pIter)))
	{
		if(pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild)
		{
			gslGuild_EntityUpdate(pEnt);
			++i;
		}

		if(i >= guildRaidsPerFrame)
		{
			TimedCallback_Run(GuildRaider_EntityUpdate_CB, pBaton, 0.0f);
			return;
		}
	}

	EntityIteratorRelease(pBaton->pIter);
	pBaton->pIter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
	TimedCallback_Run(GuildRaider_DoStuff_CB, pBaton, 0.0f);
}

// void GuildRaider_ContainerMove_CB(TransactionReturnVal *pReturnVal, GuildRaiderBaton *pBaton)
// {
// 	++pBaton->iResponses;
// 
// 	if(pBaton->iRequests == pBaton->iResponses && !eaiSize(&pBaton->pList->eaiContainers))
// 	{
// 		StructDestroy(parse_ContainerList, pBaton->pList);
// 		pBaton->pIter = entGetIteratorSingleType(0, 0, GLOBALTYPE_ENTITYPLAYER);
// 
// 		if(pBaton->bCreate)
// 		{
// 			TimedCallback_Run(GuildRaider_MakeGuilds_CB, pBaton, 0.0f);
// 		}
// 		else if(pBaton->bDisband)
// 		{
// 			TimedCallback_Run(GuildRaider_LeaveGuilds_CB, pBaton, 0.0f);
// 		}
// 		else
// 		{
// 			TimedCallback_Run(GuildRaider_EntityUpdate_CB, pBaton, 0.0f);
// 		}
// 	}
// }
// 
// void GuildRaider_ContainerRequest_CB(TimedCallback *pCallback, F32 timeSinceLastCallback, GuildRaiderBaton *pBaton)
// {
// 	int i = 0;
// 
// 	while(eaiSize(&pBaton->pList->eaiContainers) > 0)
// 	{
// 		U32 iContainerID = eaiPop(&pBaton->pList->eaiContainers);
// 
// 		objRequestContainerMove(objCreateManagedReturnVal(GuildRaider_ContainerMove_CB, pBaton), pBaton->pList->type, iContainerID, GLOBALTYPE_OBJECTDB, 0, GetAppGlobalType(), GetAppGlobalID());
// 		++i;
// 		++pBaton->iRequests;
// 
// 		if(i >= 256)
// 		{
// 			TimedCallback_Run(GuildRaider_ContainerRequest_CB, pBaton, 0.1f);
// 			break;
// 		}
// 	}
// 
// 	printf("Acquiring ownership of %d %s containers from the Object DB\n", i, GlobalTypeToName(pBaton->pList->type));
// }
// 
// void GuildRaider_ContainerRaid_CB(TransactionReturnVal *pReturnVal, GuildRaiderBaton *pBaton)
// {
// 	if(RemoteCommandCheck_dbRaidContainers(pReturnVal, &pBaton->pList) == TRANSACTION_OUTCOME_SUCCESS)
// 	{
// 		TimedCallback_Run(GuildRaider_ContainerRequest_CB, pBaton, 0.0f);
// 	}
// 	else
// 	{
// 		printf("Failed to raid containers for GuildRaider.\n");
// 		free(pBaton);
// 	}
// }

void GuildRaider_Dispatch(GuildRaiderBaton *pBaton)
{
	if(!pBaton->iDiv || pBaton->iCurBatch >= pBaton->iBatches)
	{
		printf("GuildRaider done.\n");
		free(pBaton);
		return;
	}

	if(pBaton->bCreate)
	{
		gsl_GenericContainerRaider(GLOBALTYPE_ENTITYPLAYER, pBaton->iDiv, pBaton->iCurBatch + pBaton->iFirstBatch, GuildRaider_MakeGuilds_CB, pBaton);
	}
	else if(pBaton->bDisband)
	{
		gsl_GenericContainerRaider(GLOBALTYPE_ENTITYPLAYER, pBaton->iDiv, pBaton->iCurBatch + pBaton->iFirstBatch, GuildRaider_LeaveGuilds_CB, pBaton);
	}
	else
	{
		gsl_GenericContainerRaider(GLOBALTYPE_ENTITYPLAYER, pBaton->iDiv, pBaton->iCurBatch + pBaton->iFirstBatch, GuildRaider_EntityUpdate_CB, pBaton);
	}

	++pBaton->iCurBatch;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void GuildRaiderDoStuff(int iDiv, int iBatches, int iFirstBatch)
{
	GuildRaiderBaton *pBaton = calloc(1, sizeof(GuildRaiderBaton));

	pBaton->bCreate = false;
	pBaton->bDisband = false;
	pBaton->iDiv = iDiv;
	pBaton->iBatches = iBatches;
	pBaton->iFirstBatch = iFirstBatch;
	pBaton->iCurBatch = 0;
	pBaton->iRequests = 0;
	pBaton->iResponses = 0;
	pBaton->pList = NULL;

	GuildRaider_Dispatch(pBaton);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void GuildRaiderDisband(int iDiv, int iBatches, int iFirstBatch)
{
	GuildRaiderBaton *pBaton = calloc(1, sizeof(GuildRaiderBaton));

	pBaton->bCreate = false;
	pBaton->bDisband = true;
	pBaton->iDiv = iDiv;
	pBaton->iBatches = iBatches;
	pBaton->iFirstBatch = iFirstBatch;
	pBaton->iCurBatch = 0;
	pBaton->iRequests = 0;
	pBaton->iResponses = 0;
	pBaton->pList = NULL;

	GuildRaider_Dispatch(pBaton);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void GuildRaider(int iDiv, int iBatches, int iFirstBatch, int iGuildSize)
{
	GuildRaiderBaton *pBaton = calloc(1, sizeof(GuildRaiderBaton));

	pBaton->bCreate = true;
	pBaton->bDisband = false;
	pBaton->iDiv = iDiv;
	pBaton->iBatches = iBatches;
	pBaton->iFirstBatch = iFirstBatch;
	pBaton->iCurBatch = 0;
	pBaton->iGuildSize = iGuildSize;
	pBaton->iRequests = 0;
	pBaton->iResponses = 0;
	pBaton->pList = NULL;

	GuildRaider_Dispatch(pBaton);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void GuildRaiderUpdateMemberList(Entity *pEnt, int iMaxChanges)
{
	GuildRaiderBaton *pBaton = calloc(1, sizeof(GuildRaiderBaton));
	
	pBaton->iCallerID = entGetContainerID(pEnt);
	pBaton->iNumChanges = 0;
	pBaton->iMaxChanges = iMaxChanges;
	
	TimedCallback_Run(GuildRaider_UpdateValues_CB, pBaton, 0.0f);
}

// Make sure container exists, if not create at guild id
bool gslGuildBankLoadOrCreate(Entity *pEnt)
{
	NOCONST(Guild) *pGuildNoconst = NULL;
	NOCONST(Entity) *pEntNoconst = NULL;
	Guild *pGuild;

	// not a player character
	if(!pEnt->pPlayer)
	{
		return false;
	}

	pGuild = guild_GetGuild(pEnt);

	if(!pGuild)
	{
		return false;
	}

	// subscribe the bank
	pGuildNoconst = CONTAINER_NOCONST(Guild, pGuild);
	pEntNoconst = CONTAINER_NOCONST(Entity, pEnt);
	if(pEnt->pPlayer && pEnt->pPlayer->pGuild)
	{
		char idBuf[128];
		SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYGUILDBANK), ContainerIDToString(pGuild->iContainerID, idBuf), pEntNoconst->pPlayer->pGuild->hGuildBank);
	}
	entity_SetDirtyBit(pEnt, parse_PlayerGuild, pEnt->pPlayer->pGuild, true);
	entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);

	RemoteCommand_aslGuildBank_Create(GLOBALTYPE_GUILDSERVER, 0, guild_GetGuildID(pEnt), GUILD_SERVERDOWNHANDLER(pEnt));

	return true;
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void GuildBankLoadOrCreate(Entity *pEntity)
{
	gslGuildBankLoadOrCreate(pEntity);
}


// Set the ref to the container, do a guild bank create
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void GuildBankInit(Entity *pEnt, bool bDoCreate) 
{
	if(pEnt && pEnt->pPlayer)
	{
		Guild *pGuild = guild_GetGuild(pEnt);
		
		if(pGuild)
		{
			NOCONST(Guild) *pGuildNoconst = CONTAINER_NOCONST(Guild, pGuild);
			NOCONST(Entity) *pEntNoconst = CONTAINER_NOCONST(Entity, pEnt);
			if(bDoCreate)
			{
				// create and subscribe the bank container
				// note that even if this is called with the container already created the following call is safe.
				GuildBankLoadOrCreate(pEnt);
			}
			else
			{
				// subscribe the bank container
				if(!IS_HANDLE_ACTIVE(pEntNoconst->pPlayer->pGuild->hGuildBank))
				{
					char idBuf[128];
					SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYGUILDBANK), ContainerIDToString(pGuild->iContainerID, idBuf), pEntNoconst->pPlayer->pGuild->hGuildBank);
				}
				entity_SetDirtyBit(pEnt, parse_PlayerGuild, pEnt->pPlayer->pGuild, true);
				entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
			}
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE ACMD_NAME(RequestGuildMapOwner);
void gslGuild_cmd_RequestGuildMapOwner(Entity *pEnt)
{
	int iPartitionIdx = entGetPartitionIdx(pEnt);
	if (partition_OwnerTypeFromIdx(iPartitionIdx) == GLOBALTYPE_GUILD)
	{
		ClientCmd_ReceiveGuildMapOwner(pEnt, partition_OwnerIDFromIdx(iPartitionIdx));
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Guild) ACMD_NAME(Guild_ClaimLeadership) ACMD_HIDE;
void gslGuild_cmd_ClaimLeadership(Entity *pEnt)
{
	GuildMember *pGuildMember = guild_FindMember(pEnt);
	Guild *pGuild = guild_GetGuild(pEnt);

	if (!guild_CanClaimLeadership(pGuildMember, pGuild))
	{
		return;
	}

	RemoteCommand_aslGuild_MakeLeader(GLOBALTYPE_GUILDSERVER, 0, pGuild->iContainerID, pGuildMember->iEntID);
}








///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Allegiance Changing. The transactions have to happen on the GameServer because the GuildServer knows nothing
//   about numeric items


///////////////////////////////////////////////////////////////////////////////////////////
// Set a Guild's Allegiance. This should be used with caution as it can become a different allegiance
//   from the players in it.
///////////////////////////////////////////////////////////////////////////////////////////


AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".pcAllegiance, .IVersion, .Earanks, .Eamembers")
ATR_LOCKS(pEnt, ".Pinventoryv2.Ppinventorybags[], .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.Pugckillcreditlimit, .Pchar.Ilevelexp, .Psaved.Pscpdata.Isummonedscp, .Psaved.Pscpdata.Fcachedpetbonusxppct, .Psaved.Pscpdata.Erscp, .Hallegiance, .Hsuballegiance, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome gslGuild_tr_SetAllegiance(ATR_ARGS, NOCONST(Guild) *pGuildContainer,	NOCONST(Entity)* pEnt, const char* pcNewAllegiance, const ItemChangeReason *pReason)
{
	U32 iEntID = pEnt->myContainerID;
	NOCONST(GuildMember) *pMember = eaIndexedGetUsingInt(&pGuildContainer->eaMembers, iEntID);

	if (ISNULL(pMember))
	{
		return(TRANSACTION_OUTCOME_FAILURE);		
	}

	// Check Permission
	if ((pMember->iRank < 0 || pMember->iRank >= eaSize(&pGuildContainer->eaRanks)) ||
		((pGuildContainer->eaRanks[pMember->iRank]->ePerms & GuildPermission_ChangeAllegiance) == 0))
	{
		// We don't have permission (or the rank is bogus)
		return(TRANSACTION_OUTCOME_FAILURE);		
	}

	if (pGuildContainer->pcAllegiance!=NULL && pGuildContainer->pcAllegiance[0]!=0 && stricmp(pGuildContainer->pcAllegiance,pcNewAllegiance)!=0)
	{
		// If we already have an allegiance, we need to check the cost
		if(gGuildConfig.iAllegianceChangeCost!=0)
		{
			if (!inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pEnt, false, gGuildConfig.pchAllegianceChangeNumeric, -gGuildConfig.iAllegianceChangeCost, pReason))
			{
				return(TRANSACTION_OUTCOME_FAILURE);		
			}
		}
	}

	pGuildContainer->pcAllegiance = allocAddString(pcNewAllegiance);	
	pGuildContainer->iVersion++;

	return(TRANSACTION_OUTCOME_SUCCESS);		
}



AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Guild) ACMD_NAME(Guild_SetAllegiance) ACMD_HIDE;
void gslGuild_cmd_SetAllegiance(Entity *pEnt, const char* pcNewAllegiance)
{
	GuildMember *pGuildMember = guild_FindMember(pEnt);
	Guild *pGuild = guild_GetGuild(pEnt);

	if (!gConf.bAllowGuildAllegianceChanges)
	{
		// No error. This command will do nothing for games that do not support it
		return;
	}

	if (!pGuildMember || !guild_HasPermission(pGuildMember->iRank, pGuild, GuildPermission_ChangeAllegiance)) {
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, "", "GuildServer_ErrorType_Allegiance", "GuildServer_NoPermission_SetAllegiance", true);
		return;
	}

	if (pcNewAllegiance!=NULL && pcNewAllegiance[0]!=0 && RefSystem_ReferentFromString("Allegiance", pcNewAllegiance)==NULL)
	{
		// Not a valid allegiance.
		gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_Allegiance", "GuildServer_InvalidAllegiance", true);
		return;
	}

	{
	}
	if (pGuild->pcAllegiance!=NULL && pGuild->pcAllegiance[0]!=0)
	{
		if (stricmp(pGuild->pcAllegiance,pcNewAllegiance)==0)
		{
			// We're already that allegiance
			gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_Allegiance", "GuildServer_AlreadyThatAllegiance", true);
			return;
		}
		else if (gGuildConfig.iAllegianceChangeCost!=0)
		{
			// If we already have an allegiance, we need to check the cost
			S32 iNumericValue;

			iNumericValue = inv_GetNumericItemValue(pEnt, gGuildConfig.pchAllegianceChangeNumeric);
			if (iNumericValue < gGuildConfig.iAllegianceChangeCost)
			{
				gslGuild_ResultMessage(pEnt->myContainerID, pEnt->myContainerID, 0, NULL, "GuildServer_ErrorType_Allegiance", "GuildServer_CantAffordAllegianceChange", true);
				return;
			}
		}
	}

	{
		ItemChangeReason reason = {0};
		TransactionReturnVal *pReturn;
		
		inv_FillItemChangeReason(&reason, pEnt, "Guild:ChangeAllegiance", pGuild->pcName);  // We need a reason in case we have a cost and need to change the numeric
		
//		pReturn = objCreateManagedReturnVal(gslGuild_SetAllegiance_CB,NULL);
		pReturn = LoggedTransactions_MakeReturnVal("GLDALG_");
		AutoTrans_gslGuild_tr_SetAllegiance(pReturn, GetAppGlobalType(), GLOBALTYPE_GUILD, pGuild->iContainerID,
															GLOBALTYPE_ENTITYPLAYER, pEnt->myContainerID, pcNewAllegiance, &reason);
		
	}
}

// Remote command pass through to do the setting since we can't call the transaction from inside the action expression directly
AUTO_COMMAND_REMOTE;
void gslGuild_rcmd_SetAllegiance(ContainerID iEntID, const char* pcNewAllegiance)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iEntID);
	
	gslGuild_cmd_SetAllegiance(pEnt, pcNewAllegiance);
}

// encounter_action command expression to change the allegiance. Can be hooked up to Expression Actions in contacts
AUTO_EXPR_FUNC(encounter_action) ACMD_NAME(Ent_SetGuildAllegiance);
ExprFuncReturnVal gslGuildExprSetGuildAllegiance(ACMD_EXPR_PARTITION iPartitionIdx, 
										SA_PARAM_NN_VALID Entity *pEntity, 
										SA_PARAM_NN_STR const char *pcNewAllegiance)
{
	if (!pEntity)
		return ExprFuncReturnError;

	RemoteCommand_gslGuild_rcmd_SetAllegiance(GLOBALTYPE_ENTITYPLAYER, pEntity->myContainerID, pEntity->myContainerID, pcNewAllegiance);
	return ExprFuncReturnFinished;
}

// Player/Mission expression to determine if the player can afford the allegiance change
AUTO_EXPR_FUNC(Player, Mission) ACMD_NAME(CanPlayerAffordGuildAllegianceChange);
bool gslGuild_CanPlayerAffordGuildAllegianceChange(ExprContext* context)
{
	Entity* pEnt = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);

	if (gGuildConfig.iAllegianceChangeCost==0)
	{
		return(true);
	}
	
	if (pEnt!=NULL)
	{
		int iNumericValue = inv_GetNumericItemValue(pEnt, gGuildConfig.pchAllegianceChangeNumeric);
		if (iNumericValue < gGuildConfig.iAllegianceChangeCost)
		{
			return(false);
		}
		return(true);
	}
	return(false);
}


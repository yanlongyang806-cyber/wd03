#include "userPermissions.h"

#include "chatCommandStrings.h"
#include "chatdb.h"
#include "chatGlobal.h"
#include "chatGlobalConfig.h"
#include "ChatServer.h"
#include "ChatServer/chatBlacklist.h"
#include "ChatServer/chatShared.h"
#include "crypt.h"
#include "file.h"
#include "GameStringFormat.h"
#include "net.h"
#include "objTransactions.h"
#include "StringFormat.h"
#include "StringUtil.h"
#include "timing.h"
#include "users.h"

#include "ticketnet.h"
#include "ticketenums.h"
#include "AutoGen/chatBlacklist_h_ast.h"
#include "AutoGen/chatdb_h_ast.h"
#include "AutoGen/GlobalChatServer_autotransactions_autogen_wrappers.h"
#include "AutoGen/userPermissions_c_ast.h"

extern ParseTable parse_TicketData[];
#define TYPE_parse_TicketData TicketData
extern bool gbGlobalChatResponse;
extern bool gbChatVerbose;
extern GlobalChatServerConfig gGlobalChatServerConfig;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
int chatServer_UserIsBanned(const char *accountName)
{
	ChatUser *user = userFindByAccountName(accountName);
	if (user)
		return userIsBanned(user);
	Errorf("Could not find user with account name '%s'", accountName);
	return -1;
}

bool OVERRIDE_LATELINK_userIsSilenced(ChatUser *user)
{
	if (UserIsAdmin(user))
		return false;
	if (userIsBanned(user))
		return true;
	else if (!user->silenced)
		return false;
	else
	{
		U32 uTime = timeSecondsSince2000();
		if (user->silenced > uTime)
			return true;
		userUnsilence(user, false);
		return false;
	}
}

static TicketData *chatCreateTicket(ChatUser *user, U32 uChatServerID, const char *pSummary, const char *pDescription)
{
	TicketData *pTicketData = StructCreate(parse_TicketData);
	// Look up Shard information
	if (uChatServerID)
	{
		GlobalChatLinkStruct *shardData = GlobalChatGetShardData(uChatServerID);
		PlayerInfoStruct *player = findPlayerInfoByLocalChatServerID(user, uChatServerID);
		if (shardData)
		{
			char *tempShardInfo = NULL;
			estrStackCreate(&tempShardInfo);
			estrPrintf(&tempShardInfo, "%s shard, name (%s), category (%s)", shardData->pProductName,
				shardData->pShardName, shardData->pShardCategoryName);
			pTicketData->pProductName = StructAllocString(shardData->pProductName);
			pTicketData->pShardInfoString = StructAllocString(tempShardInfo);
			estrDestroy(&tempShardInfo);
		}
		if (player && player->onlinePlayerName)
			pTicketData->pCharacterName = StructAllocString(player->onlinePlayerName);
	}
	if (!pTicketData->pProductName)
		pTicketData->pProductName = strdup(GetProductName()); // just so something is here
	pTicketData->eVisibility = TICKETVISIBLE_HIDDEN;
	pTicketData->pPlatformName = strdup(PLATFORM_NAME);
	pTicketData->pSummary = StructAllocString(pSummary);
	pTicketData->pUserDescription = StructAllocString(pDescription);
	pTicketData->iProductionMode = isProductionMode();
	pTicketData->iMergeID = 0;
	pTicketData->eLanguage = userGetLastLanguage(user);
	pTicketData->uIsInternal = true;
	pTicketData->pAccountName = StructAllocString(user->accountName);
	pTicketData->pDisplayName = StructAllocString(user->handle);
	estrCopy2(&pTicketData->pMainCategory, "CBug.CategoryMain.GM");
	estrCopy2(&pTicketData->pCategory, "CBug.Category.GM.Behavior");
	return pTicketData;
}

static void userBan(SA_PARAM_NN_VALID ChatUser *user)
{
	if ((user->uFlags & CHATUSER_FLAG_BANNED) == 0)
	{
		objRequestTransactionSimplef(NULL, GLOBALTYPE_CHATUSER, user->id, "userBan", "set uFlags = %d", user->uFlags | CHATUSER_FLAG_BANNED);
		sendUserUpdate(user);
	}
}

static void userUnban(SA_PARAM_NN_VALID ChatUser *user)
{
	if (user->uFlags & CHATUSER_FLAG_BANNED)
	{
		objRequestTransactionSimplef(NULL, GLOBALTYPE_CHATUSER, user->id, "userBan", 
			"set uFlags = %d", ~CHATUSER_FLAG_BANNED & user->uFlags);
		sendUserUpdate(user);
	}
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
int chatServer_UserBan(U32 uID)
{
	ChatUser *user = userFindByContainerId(uID);
	if (!user)
	{
		Errorf("user_dne");
		return -1;
	}
	userBan(user);
	return 0;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
int chatServer_UserUnban(U32 uID)
{
	ChatUser *user = userFindByContainerId(uID);
	if (!user)
	{
		Errorf("user_dne");
		return -1;
	}
	userUnban(user);
	return 0;
}

AUTO_COMMAND_REMOTE;
void userBanByHandle(CmdContext *context, U32 uID, ACMD_SENTENCE displayName)
{
	ChatUser *user, *target;
	if (!gbGlobalChatResponse)
	{
		if (globalChatAddCommandWaitQueueByLink(chatServerGetCommandLink(), displayName, context->commandString))
			return;
	}
	user = userFindByContainerId(uID);
	target = userFindByHandle(displayName);
	if (!target)
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, NULL, "ChatServer_UserDNE", STRFMT_STRING("User", displayName), STRFMT_END);
		return;
	}
	userBan(target);
	sendTranslatedMessageToUser(user, kChatLogEntryType_System, NULL, "ChatServer_BanConfirm", STRFMT_STRING("User", displayName), STRFMT_END);
}

AUTO_COMMAND_REMOTE;
void userUnbanByHandle(CmdContext *context, U32 uID, ACMD_SENTENCE displayName)
{
	ChatUser *user, *target;
	if (!gbGlobalChatResponse)
	{
		if (globalChatAddCommandWaitQueueByLink(chatServerGetCommandLink(), displayName, context->commandString))
			return;
	}
	user = userFindByContainerId(uID);
	target = userFindByHandle(displayName);
	if (!target)
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, NULL, "ChatServer_UserDNE", STRFMT_STRING("User", displayName), STRFMT_END);
		return;
	}
	userUnban(target);
	sendTranslatedMessageToUser(user, kChatLogEntryType_System, NULL, "ChatServer_UnbanConfirm", STRFMT_STRING("User", displayName), STRFMT_END);
}


AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
int chatServer_UserSilence(U32 uID, U32 uDuration)
{
	ChatUser *user = userFindByContainerId(uID);
	if (!user)
	{
		Errorf("user_dne");
		return -1;
	}
	userSilence(user, uDuration);
	return 0;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
int chatServer_UserUnsilence(U32 uID)
{
	ChatUser *user = userFindByContainerId(uID);
	if (!user)
	{
		Errorf("user_dne");
		return -1;
	}
	userUnsilence(user, true);
	return 0;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
int userSilencedEndTime(const char *accountName)
{
	ChatUser *user = userFindByAccountName(accountName);

	if (user)
	{
		if (userIsSilenced(user))
			return user->silenced;
		return 0;
	}
	Errorf("Could not find user with account name '%s'", accountName);
	return -1;
}

// Silence user for [duration] seconds
void userSilence(ChatUser *user, U32 duration)
{
	if (user && duration)
	{
		objRequestTransactionSimplef(NULL, GLOBALTYPE_CHATUSER, user->id, "UserSilence", "set silenced = %d", timeSecondsSince2000() + duration);
		sendUserUpdate(user);
	}
}

void userUnsilence(ChatUser *user, bool bManualCall)
{
	if (user && user->silenced)
	{
		objRequestTransactionSimplef(NULL, GLOBALTYPE_CHATUSER, user->id, "UserUnsilence", "set silenced = %d", 0);
		if (bManualCall && user->naughty)
			objRequestTransactionSimplef(NULL, GLOBALTYPE_CHATUSER, user->id, "userClearNaught", "set naughty = 0");
		sendUserUpdate(user);
	}
}

AUTO_COMMAND_REMOTE;
void userSilenceByAccountName (U32 uUserID, const char *accountName, U32 uDuration)
{
	ChatUser *user = userFindByContainerId(uUserID);
	if (user && UserIsAdmin(user))
	{
		ChatUser *target = userFindByAccountName(accountName);
		if (target)
		{
			userSilence(target, uDuration);
			// TODO send response
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, NULL, "ChatServer_SilenceConfirm", 
				STRFMT_STRING("User", target->handle), STRFMT_TIMER("Time", uDuration), STRFMT_END);
		}
		else
		{
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, NULL, "ChatServer_UserDNE", STRFMT_STRING("User", accountName), STRFMT_END);
		}
	}
}

AUTO_COMMAND_REMOTE;
void userSilenceByDisplayName(U32 uUserID, const char *displayName, U32 uDuration)
{
	ChatUser *user = userFindByContainerId(uUserID);
	if (user && UserIsAdmin(user))
	{
		ChatUser *target = userFindByHandle(displayName);
		if (target)
		{
			userSilence(target, uDuration);
			// TODO Fix STRFMT_TIMER to NOT crash the GCS when translations are being done on it
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, NULL, "ChatServer_SilenceConfirm", 
				STRFMT_STRING("User", target->handle), STRFMT_TIMER("Time", uDuration), STRFMT_END);
		}
		else
		{
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, NULL, "ChatServer_UserDNE", STRFMT_STRING("User", displayName), STRFMT_END);
		}
	}
}

AUTO_COMMAND_REMOTE;
void userUnsilenceByDisplayName(CmdContext *context, U32 uUserID, const char *displayName)
{
	ChatUser *user = userFindByContainerId(uUserID);
	if (user && UserIsAdmin(user))
	{
		ChatUser *target = userFindByHandle(displayName);
		if (target)
		{
			userUnsilence(target, true);
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, NULL, "ChatServer_UnsilenceConfirm", STRFMT_STRING("User", target->handle), STRFMT_END);
		}
		else
		{
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, NULL, "ChatServer_UserDNE", STRFMT_STRING("User", displayName), STRFMT_END);
		}
	}
}

// These are deprecated calls that can be accessed by GM commands from older shards
AUTO_COMMAND ACMD_NAME(userSilenceByDisplayName);
void userSilenceByDisplayName_DEPRECATED(const char *displayName, U32 uDuration)
{
	// placeholder to prevent alerts
	ChatUser *target = userFindByHandle(displayName);
	if (target)
		userSilence(target, uDuration);
}
AUTO_COMMAND ACMD_NAME(userUnsilenceByDisplayName);
void userUnsilenceByDisplayName_DEPRECATED(const char *displayName)
{
	// placeholder to prevent alerts
	ChatUser *target = userFindByHandle(displayName);
	if (target)
		userUnsilence(target, true);
}

// This does not change the last naughty value time
AUTO_COMMAND ACMD_CATEGORY(ChatDebug);
void userSetNaughtyValue (U32 uID, int iNaughty)
{
	ChatUser *user = userFindByContainerId(uID);
	if (user)
	{
		objRequestTransactionSimplef(NULL, GLOBALTYPE_CHATUSER, user->id, "userClearNaughty", "set .naughty = 0");
		userIncrementNaughtyValue(user, iNaughty, "Manual Debug Set");
	}
}

AUTO_TRANSACTION ATR_LOCKS(user, ".naughty, .uLastNaughtyTime");
enumTransactionOutcome trChangeNaughtyValue(ATR_ARGS, NOCONST(ChatUser) *user, int iNewNaughty, U32 uTimeSet)
{
	user->naughty = iNewNaughty;
	user->uLastNaughtyTime = uTimeSet;
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Returns the value of the naughty after the decay is factored in
int userGetNaughtyDecayPoints(ChatUser *user, U32 uCurrentTime)
{
	// number of hours, rounded down; comparisons are [>=] so that 6 hours means 6 and not 7 hours
	U32 uTimeDiff = (uCurrentTime - user->uLastNaughtyTime) / SECONDS_PER_HOUR;
	if (user->uLastNaughtyTime == 0)
		return user->naughty;
	if (gGlobalChatServerConfig.iNaughtyFullDecay && 
		uTimeDiff >= gGlobalChatServerConfig.iNaughtyFullDecay)
	{
		// destroy the people-who-have-reported-as-spammer list after full decay time hours
		eaiDestroy(&user->eaiSpamIgnorers);
		if (gbChatVerbose)
			printf("Full decay for @%s\n", user->accountName);
		return 0;
	}
	else if (gGlobalChatServerConfig.iNaughtyHalfDecay && 
			uTimeDiff >= gGlobalChatServerConfig.iNaughtyHalfDecay)
	{
		if (gbChatVerbose)
			printf("1/2 decay for @%s\n", user->accountName);
		return (user->naughty + 1) / 2; // +1 so it rounds up;
	}
	else if (gGlobalChatServerConfig.iNaughtyQuarterDecay && 
		uTimeDiff >= gGlobalChatServerConfig.iNaughtyQuarterDecay)
	{
		if (gbChatVerbose)
			printf("1/4 decay for @%s\n", user->accountName);
		return (user->naughty + 3) / 4; // +3 so it rounds up;
	}
	return user->naughty;
}

AUTO_TRANSACTION ATR_LOCKS(user, ".naughty, .uLastNaughtyTime, .eaSpamMessages");
enumTransactionOutcome trUserAddSpamMessage(ATR_ARGS, NOCONST(ChatUser) *user, int iNewNaughty, U32 uTimeSet, 
	const char *spamText, const char *hash)
{
	NOCONST(SpamMessage) *msg = NULL;
	user->naughty = iNewNaughty;
	user->uLastNaughtyTime = uTimeSet;
	EARRAY_CONST_FOREACH_BEGIN(user->eaSpamMessages, i, s);
	{
		if (stricmp(user->eaSpamMessages[i]->hash, hash) == 0)
		{
			msg = eaRemove(&user->eaSpamMessages, i);
			break;
		}
	}
	EARRAY_FOREACH_END;
	if (!msg)
	{
		msg = StructCreateNoConst(parse_SpamMessage);
		msg->hash = StructAllocString(hash);
		msg->message = StructAllocString(spamText);
	}
	msg->lastReportTime = uTimeSet;
	while (eaSize(&user->eaSpamMessages) >= gGlobalChatServerConfig.iSpamMessageSaveCount)
	{
		NOCONST(SpamMessage) *oldestMsg = eaRemove(&user->eaSpamMessages, 0);
		StructDestroyNoConst(parse_SpamMessage, oldestMsg);
	}
	eaPush(&user->eaSpamMessages, msg);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(user, ".eaSpamMessages");
enumTransactionOutcome trUserClearSpamMessages(ATR_ARGS, NOCONST(ChatUser) *user)
{
	eaDestroyStructNoConst(&user->eaSpamMessages, parse_SpamMessage);
	return TRANSACTION_OUTCOME_SUCCESS;
}

static int getNaughtyValueChange(ChatUser *user, int iAddNaughty, U32 uCurTime, const char *reason)
{
	int iCurrentNaughty = user->naughty;
	int iDecayValue = userGetNaughtyDecayPoints(user, uCurTime);
	int iNewNaughty = iDecayValue + iAddNaughty;

	chatServerLogUserCommandf(LOG_CHATPERMISSIONS, "NaughtyIncrement", NULL, user, NULL, "Add %d, Decay %d, End %d, Reason %s", 
		iAddNaughty, iCurrentNaughty - iDecayValue, iNewNaughty, reason);
	return iNewNaughty;
}

static void checkNaughtyBan(ChatUser *user, U32 uCurTime, const char *reason)
{
	//If this increment pushed user over the max naughty value, ban them.
	if (user->naughty > gGlobalChatServerConfig.iMaxNaughty)
	{
		int iSilencedDuration = user->silenced ? user->silenced - uCurTime : 0;
		// Only re-ban if duration is half or less than iBanDuration so that it doesn't spam tickets
		if (iSilencedDuration < gGlobalChatServerConfig.iBanDuration * SECONDS_PER_HOUR / 2)
			banSpammer(user, reason);
	}
}

void userIncrementNaughtyValueWithSpam (ChatUser *user, int iAddNaughty, const char *reason, const char *spamMsg)
{
	static char hash[34];
	U32 uCurTime = timeSecondsSince2000();
	int iNewNaughty = getNaughtyValueChange(user, iAddNaughty, uCurTime, reason);

	cryptMD5Hex(spamMsg, (int) strlen(spamMsg), hash, ARRAY_SIZE_CHECKED(hash));

	// Change the naughty value and time set
	AutoTrans_trUserAddSpamMessage(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id, iNewNaughty, uCurTime, 
		spamMsg, hash);
	
	checkNaughtyBan(user, uCurTime, reason);
}

void userIncrementNaughtyValue (ChatUser *user, int iAddNaughty, const char *reason)
{
	U32 uCurTime = timeSecondsSince2000();
	int iNewNaughty = getNaughtyValueChange(user, iAddNaughty, uCurTime, reason);

	// Change the naughty value and time set
	AutoTrans_trChangeNaughtyValue(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id, iNewNaughty, uCurTime);

	checkNaughtyBan(user, uCurTime, reason);
}

// Checks if the user is online on any gold subscriptions or XMPP
static bool userIsSilverUser(ChatUser *user)
{
	EARRAY_CONST_FOREACH_BEGIN(user->ppPlayerInfo, i, s);
	{
		if (user->ppPlayerInfo[i]->bGoldSubscriber)
			return false;
		if (user->ppPlayerInfo[i]->uChatServerID == XMPP_CHAT_ID)
			return false;
	}
	EARRAY_FOREACH_END;
	return true;
}

void userSpamSilence(SA_PARAM_NN_VALID ChatUser *user, U32 curTime)
{
	if (gGlobalChatServerConfig.iSilverSpamDuration && userIsSilverUser(user))
		userSilence(user, gGlobalChatServerConfig.iSilverSpamDuration + max(0, (S32)user->silenced-(S32)curTime));
	else
		userSilence(user, gGlobalChatServerConfig.iSpamSilenceDuration + max(0, (S32)user->silenced-(S32)curTime));
}

// If naughty value of a user has exceeded the allowed threshold, silence the user and submit a CSR ticket
// Naughty value is also reset to gGlobalChatServerConfig.iNaughtyReset
void banSpammer(ChatUser* user, const char *reason)
{
	U32 curTime = timeSecondsSince2000();
	U32 duration;
	char* estrBuffer = NULL;
	const char *playerName = NULL;
	TicketData *pTicketData;
	U32 uChatServerID;

	if (UserIsAdmin(user)) // non-access level 0 accounts do not get auto-banned
		return;
	//Check to make sure gGlobalChatServerConfig was initialized
	if(!gGlobalChatServerConfig.iBanDuration)
		return;
	if (userIsBanned(user))
		return;

	//Silence the user
	userSilence(user, gGlobalChatServerConfig.iBanDuration * SECONDS_PER_HOUR + max(0, (S32)user->silenced-(S32)curTime));
	duration = user->silenced - curTime;
	objRequestTransactionSimplef(NULL, GLOBALTYPE_CHATUSER, user->id, "userResetNaughty", "set .naughty = %d", 
		gGlobalChatServerConfig.iNaughtyReset);

	//Send message to the user
	estrStackCreate(&estrBuffer);
	sendTranslatedMessageToUser(user, kChatLogEntryType_System, PRIVATE_CHANNEL_NAME, "ChatServer_BanSpammer", 
		STRFMT_INT("H", duration/(SECONDS_PER_HOUR)), STRFMT_INT("M", (duration/60)%60), STRFMT_INT("S", duration%60), STRFMT_END);

	/*if(user->pPlayerInfo) {
	U32 entID = user->pPlayerInfo->onlineCharacterID;
	RemoteCommand_ServerChat_NotifyUser(GLOBALTYPE_ENTITYPLAYER, entID, entID, kNotifyType_ChatAdmin, estrBuffer, NULL, NULL);
	}*/ // TODO notification
	chatServerLogUserCommandf(LOG_CHATPERMISSIONS, "banUser", NULL, user, NULL, "Seconds %d, Reset %d, Trigger %s", 
		duration, gGlobalChatServerConfig.iNaughtyReset, reason);
	estrClear(&estrBuffer);

	uChatServerID = GetLocalChatLinkID(chatServerGetCommandLink());
	if (uChatServerID)
	{
		PlayerInfoStruct *player = findPlayerInfoByLocalChatServerID(user, uChatServerID);
		if (player && player->onlinePlayerName)
			playerName = player->onlinePlayerName;
	}
	
	// Construct summary; no translation for this since it's a internal ticket
	estrPrintf(&estrBuffer, "This report was automatically generated by the ignore count/chat rate spam filter!\n"
		"The number of times the player has been ignored or has been silenced due to excessive chat rate has exceeded the allowed limit.\n"
		"Silenced for %d hours.\n\n"
		"Account Name: %s\n"
		"Character Name: %s\n"
		"Display Name: %s\n"
		"Last action before ban: %s\n", 
		duration/SECONDS_PER_HOUR, user->accountName, playerName ? playerName : "[Unknown]", user->handle, reason);
	// Include the last X messages sent by user that were reported as spam
	if (user->eaSpamMessages)
	{
		char timeBuffer[32];
		estrConcatf(&estrBuffer, "Spam Messages:\n");
		EARRAY_CONST_FOREACH_REVERSE_BEGIN(user->eaSpamMessages, i, s);
		{
			SpamMessage *msg = user->eaSpamMessages[i];
			timeMakeLocalIso8601StringFromSecondsSince2000(timeBuffer, msg->lastReportTime);
			estrConcatf(&estrBuffer, "[%s] %s\n", timeBuffer, msg->message);
		}
		EARRAY_FOREACH_END;
		AutoTrans_trUserClearSpamMessages(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id);
	}

	pTicketData = chatCreateTicket(user, uChatServerID, "Auto-report for spamming", estrBuffer);

	//Send CSR ticket
	ticketTrackerSendTicket(pTicketData);
	StructDestroy(parse_TicketData, pTicketData);
	estrDestroy(&estrBuffer);
}

AUTO_COMMAND;
void userSetAsBot(U32 uID, bool bBot)
{
	ChatUser *user = userFindByContainerId(uID);
	if (!user)
		return;
	if (bBot)
	{
		if ((user->uFlags & CHATUSER_FLAG_BOT) == 0)
			objRequestTransactionSimplef(NULL, GLOBALTYPE_CHATUSER, user->id, "userBot", "set uFlags = %d", 
				user->uFlags | CHATUSER_FLAG_BOT);		
	}
	else
	{
		if ((user->uFlags & CHATUSER_FLAG_BOT) != 0)
			objRequestTransactionSimplef(NULL, GLOBALTYPE_CHATUSER, user->id, "userUnbot", 
				"set uFlags = %d", ~CHATUSER_FLAG_BOT & user->uFlags);
	}
}

AUTO_COMMAND ACMD_CATEGORY(ChatDebug);
void clearSpamIgnorers(U32 id)
{
	ChatUser *user = userFindByContainerId(id);
	if (user)
		eaiDestroy(&user->eaiSpamIgnorers);
}

//////////////////////////////////
// Chat Blacklist Management - GCS Specific 

// Sends Blacklist Update to specified link, or to all shards if link == NULL
static void blacklist_SendToShard(NetLink *link, const ChatBlacklist *blacklist, ChatBlacklistUpdate eUpdateType)
{
	char *commandString = NULL;
	char *structString = NULL;

	estrStackCreate(&commandString);
	estrStackCreate(&structString);
	ParserWriteTextEscaped(&structString, parse_ChatBlacklist, blacklist, 0, 0, TOK_NO_NETSEND);
	estrPrintf(&commandString, "blacklist_ShardUpdate %s %d", structString, eUpdateType);
	if (link)
		sendCommandToLink(link, commandString);
	else
		sendCommandToAllLocal(commandString, false);
	estrDestroy(&commandString);
	estrDestroy(&structString);
}

ChatBlacklistString *blacklist_AddString_Internal(const char *string, const char *reason)
{
	ChatBlacklistString *blString;
	if (nullStr(string))
		return NULL;
	blString = blacklist_LookupString(string);
	if (blString)
		return NULL;
	blString = StructCreate(parse_ChatBlacklistString);
	blString->string = StructAllocString(string);
	blString->reason = StructAllocString(reason);
	blString->uTimeAdded = timeSecondsSince2000();
	blacklist_AddStringStruct(blString);
	return blString;
}

static char *blacklist_TrimLine(char *line)
{
	char *endOfString;
	while (*line && IS_WHITESPACE(*line))
		line++;
	endOfString = line+strlen(line)-1;
	while (endOfString > line && IS_WHITESPACE(*endOfString))
	{
		*endOfString = '\0';
		endOfString--;
	}
	return line;
}

// File must have blacklist strings and reasons in a new line immediately following it; the reason line is REQUIRED (but can be blank)
AUTO_COMMAND ACMD_CATEGORY(ChatAdmin);
void blacklist_AddFile(const char *filename)
{
	char linebuffer[256];
	char blacklistBuffer[256];
	char reasonBuffer[256];
	FileWrapper * file;
	ChatBlacklist addedList = {0};
	ChatBlacklistString *blString;

	if (!fileExists(filename))
	{
		Errorf("Blacklist File not found: %s", filename);
		return;
	}
	linebuffer[0] = 0;
	file = fopen(filename, "rt");
	while (fgets(linebuffer, ARRAY_SIZE_CHECKED(linebuffer), file))
	{
		// Remove leading and trailing whitespace
		strcpy(blacklistBuffer, blacklist_TrimLine(&linebuffer[0]));
		if (strlen(blacklistBuffer))
		{
			if (fgets(linebuffer, ARRAY_SIZE_CHECKED(linebuffer), file))
				strcpy(reasonBuffer, blacklist_TrimLine(&linebuffer[0]));
			else
				reasonBuffer[0] = 0;
			blString = blacklist_AddString_Internal(blacklistBuffer, reasonBuffer);
			if (blString)
				eaPush(&addedList.eaStrings, blString);
		}
	}
	fclose(file);
	saveBlacklistFile();
	blacklist_SendToShard(NULL, &addedList, CHATBLACKLIST_ADD);
	eaDestroy(&addedList.eaStrings);
}

void blacklist_InitShardChatServer(NetLink *link)
{
	blacklist_SendToShard(link, blacklist_GetList(), CHATBLACKLIST_REPLACE);
}

#define BLACKLIST_SUCCESS "success"
#define BLACKLIST_FAILURE "failure"
#define BLACKLIST_ALLOWED "allowed"
#define BLACKLIST_BLOCKED "blocked"

AUTO_STRUCT;
typedef struct BlacklistXMLRPCResponse
{
	char *status;
} BlacklistXMLRPCResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC, ChatAdmin) ACMD_NAME(AddToChatBlacklist);
BlacklistXMLRPCResponse *blacklist_AddString(const char *string, const char *reason)
{
	ChatBlacklistString *blString = blacklist_AddString_Internal(string, reason);
	BlacklistXMLRPCResponse *response = StructCreate(parse_BlacklistXMLRPCResponse);
	if (blString)
	{
		ChatBlacklist bl = {0};
		saveBlacklistFile();
		eaPush(&bl.eaStrings, blString);
		blacklist_SendToShard(NULL, &bl, CHATBLACKLIST_ADD);
		eaDestroy(&bl.eaStrings);
		response->status = strdup(BLACKLIST_SUCCESS);
	}
	else
		response->status = strdup(BLACKLIST_FAILURE);
	return response;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC, ChatAdmin) ACMD_NAME(RemoveFromChatBlacklist);
BlacklistXMLRPCResponse *blacklist_RemoveString(char *string)
{
	ChatBlacklistString *blString = blacklist_RemoveString_Internal(string, false);
	BlacklistXMLRPCResponse *response = StructCreate(parse_BlacklistXMLRPCResponse);
	if (blString)
	{
		ChatBlacklist bl = {0};
		saveBlacklistFile();
		eaPush(&bl.eaStrings, blString);
		blacklist_SendToShard(NULL, &bl, CHATBLACKLIST_REMOVE);
		eaDestroy(&bl.eaStrings);
		StructDestroy(parse_ChatBlacklistString, blString);
		response->status = strdup(BLACKLIST_SUCCESS);
	}
	else
		response->status = strdup(BLACKLIST_FAILURE);
	return response;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME(FetchChatBlacklist);
ACMD_STATIC_RETURN const ChatBlacklist *BlacklistGet(void)
{
	return blacklist_GetList();
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC, ChatAdmin) ACMD_NAME(TestAgainstNewString);
BlacklistXMLRPCResponse *blacklist_TestString(const char *string)
{
	BlacklistXMLRPCResponse *response = StructCreate(parse_BlacklistXMLRPCResponse);
	if (blacklist_CheckForViolations(string, NULL))
		response->status = strdup(BLACKLIST_SUCCESS);
	else
		response->status = strdup(BLACKLIST_FAILURE);
	return response;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC, ChatAdmin) ACMD_NAME(TestAgainstBlacklist);
BlacklistXMLRPCResponse *blacklist_TestStringAgainstNew(const char *string, const char *testBlacklist)
{
	BlacklistXMLRPCResponse *response = StructCreate(parse_BlacklistXMLRPCResponse);
	if (strstri(string, testBlacklist) != NULL)
		response->status = strdup(BLACKLIST_SUCCESS);
	else
		response->status = strdup(BLACKLIST_FAILURE);
	return response;
}

AUTO_COMMAND_REMOTE;
void blacklist_ViolationBan(U32 uAccountID, const char *fullMessageText)
{
	ChatUser *user = userFindByContainerId(uAccountID);
	U32 uChatServerID = GetLocalChatLinkID(chatServerGetCommandLink());
	char *pDescription = NULL;
	const char *playerName = NULL;
	TicketData *pTicketData;
	if (!user || userIsBanned(user))
		return;

	userBan(user);
	estrStackCreate(&pDescription);	
	if (uChatServerID)
	{
		PlayerInfoStruct *player = findPlayerInfoByLocalChatServerID(user, uChatServerID);
		if (player && player->onlinePlayerName)
			playerName = player->onlinePlayerName;
	}

	// Construct summary; no translation for this since it's a internal ticket
	estrPrintf(&pDescription, "This report was automatically generated by excessive blacklist violations!\n"
		"The user has been permanently banned from the game, as well as from using any chat features in all Cryptic products.\n"
		"Account Name: %s\n"
		"Character Name: %s\n"
		"Display Name: %s\n"
		"Last message before ban: %s\n", 
		user->accountName, playerName ? playerName : "[Unknown]", user->handle, fullMessageText);

	pTicketData = chatCreateTicket(user, uChatServerID, "Blacklist Violation Auto-Ban", pDescription);
	ticketTrackerSendTicket(pTicketData);
	StructDestroy(parse_TicketData, pTicketData);
	estrDestroy(&pDescription);
}

#include "AutoGen/userPermissions_c_ast.c"
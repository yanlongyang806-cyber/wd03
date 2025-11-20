/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gclChatLog.h"

#include "chatCommon.h"
#include "chatCommonStructs.h"
#include "chatData.h"
#include "cmdparse.h"
#include "EntityLib.h"
#include "GameClientLib.h"
#include "gclChatConfig.h"
#include "gclClientChat.h"
#include "gclEntity.h"
#include "gclScript.h"
#include "GfxConsole.h"
#include "MemoryPool.h"
#include "NotifyCommon.h"
#include "StringUtil.h"
#include "UIGen.h"
#include "GameStringFormat.h"
#include "logging.h"
#include "gclFriendsIgnore.h"
#include "StringCache.h"
#include "itemCommon.h"
#include "AutoGen/chatCommonStructs_h_ast.h"
#include "AutoGen/chatData_h_ast.h"
#include "AutoGen/gclChatlog_h_ast.h"

#define MAX_RECENT_CHAT_USERS 20

extern bool gbNoGraphics;

static CommandHistoryEntry **s_eaCommandHistory = NULL;
//Keeps track of how large the command history is and where you're at when indexing through it
static int s_iCommandHistoryIdx = 0;

static char *s_pchLastTellFromNameAndHandle = NULL;
static ChatUserInfo **s_eaRecentChatSenders = NULL;
static ChatUserInfo **s_eaRecentChatReceivers = NULL;
static ChatUserInfo **s_eaRecentTellCorrespondents = NULL;

static U32 s_lifetimeChatLogEntryCount = 0;
// Chat log entries are pushed into s_eaPendingChatLog as they are added, then
// pushed as a batch onto g_ChatLog once per frame. This ensures anything in g_ChatLog
// is valid for the entirety of the frame. Likewise, clearing should not happen except
// at that one point in the frame.
static ChatLogEntry **s_eaPendingChatLog;
static bool s_bClearChatLog;
ChatLogEntry **g_ChatLog = NULL;

static S32 s_ChatLogFormatVersion = -1;
S32 g_ChatLogVersion = 1;

// Earray holding all private chat windows. This is not indexed and searches are linear.
// The idea is that we're not gonna have more than 50 or so windows.
static PrivateChatWindow **s_eaPrivateChatWindows;

// The idea is that the tab states are synchronized to the ChatTabConfig's. New messages
// are then filtered through the various tab states.
static ChatTabState **s_eaTabStates;

static bool s_bChatLog;
AUTO_CMD_INT(s_bChatLog, ChatLog) ACMD_HIDE ACMD_ACCESSLEVEL(0);

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););
MP_DEFINE(ChatLogEntry);
MP_DEFINE(CommandHistoryEntry);

extern void gclChatLogFormat(ChatConfig *pConfig, ChatLogEntry *pEntry);

// Private chat window function forward declarations

// Updates the private chat window info for the given handle after a new private message is received
static void ClientChat_UpdatePrivateChatWindow(SA_PARAM_NN_STR const char * pchHandle);

// Once per frame processing for private chat windows
static void ClientChat_PrivateChatWindowsOncePerFrame(void);

static bool ChatUser_PassesNameFilter (ChatUserInfo *userInfo, ChatLogFilter *pFilter)
{
	int i, size;
	size = eaSize(&pFilter->ppNameFilter);
	if (size == 0)
		return true; // always return true if there are no filters
	for (i=0; i<size; i++)
	{
		ChatUserInfo *userFilter = pFilter->ppNameFilter[i];
		if (userFilter->nonPlayerEntityRef) // NPC match
		{
			if (userInfo->nonPlayerEntityRef == pFilter->ppNameFilter[i]->nonPlayerEntityRef)
				return pFilter->eNameInclusion == CHATLOGFILTER_INCLUDE; //Inclusion = return true, exclusion = false
		}
		else // Player match
		{
			bool bMatchedCharacterName = true;
			bool bMatchedDisplayName = true;
			if (userFilter->pchName) // ignore character name if NULL
				bMatchedCharacterName = userInfo->pchName ? stricmp(userFilter->pchName, userInfo->pchName) == 0 : false;
			if (userFilter->pchHandle) // ignore account name if NULL
				bMatchedDisplayName = userInfo->pchHandle ? stricmp(userFilter->pchHandle, userInfo->pchHandle) == 0 : false;
			if (bMatchedCharacterName && bMatchedDisplayName)
				return pFilter->eNameInclusion == CHATLOGFILTER_INCLUDE; //Inclusion = return true, exclusion = false
		}
	}
	return pFilter->eNameInclusion == CHATLOGFILTER_EXCLUDE; // TRUE if passed all exclude filters
}

static bool ChatUser_PassesTypeFilter (ChatLogEntryType eType, ChatLogFilter *pFilter)
{
	int i, size;
	size = eaiSize(&pFilter->pTypeFilter); // Check inclusions first
	if (size == 0)
		return true; // always return true if there are no filters
	for (i=0; i<size; i++)
	{
		if (pFilter->pTypeFilter[i] == eType)
			return pFilter->eTypeInclusion == CHATLOGFILTER_INCLUDE;
	}
	return pFilter->eTypeInclusion == CHATLOGFILTER_EXCLUDE; // TRUE if passed all exclude filters
}

static bool ChatLogEntry_MatchesFilter (ChatLogEntry *pChatEntry, ChatLogFilter *pFilter)
{
	if (!pChatEntry->pMsg)
		return false;
	if (pFilter->uStartTime && pChatEntry->iTimestamp < pFilter->uStartTime)
		return false;
	if (pFilter->uEndTime && pChatEntry->iTimestamp > pFilter->uEndTime)
		return false;
	if (!ChatUser_PassesTypeFilter(pChatEntry->pMsg->eType, pFilter))
		return false;
	if (!ChatUser_PassesNameFilter(pChatEntry->pMsg->pFrom, pFilter) && !ChatUser_PassesNameFilter(pChatEntry->pMsg->pTo, pFilter))
		return false;
	return true; 
}

void ChatLog_FilterMessages(ChatLogEntry ***eaChatMessages, ChatLogFilter *pFilter)
{
	// TODO add filter
	int i, size;
	size = eaSize(&g_ChatLog);
	for (i=0; i<size; i++)
	{
		if (ChatLogEntry_MatchesFilter(g_ChatLog[i], pFilter))
			eaPush(eaChatMessages, g_ChatLog[i]);
	}
}

// Called from Chat_Initialize AUTO_RUN
void ChatLog_Initialize(void)
{
	MP_CREATE(ChatLogEntry, MAX_CHAT_LOG_ENTRIES + 20);
	MP_CREATE(CommandHistoryEntry, MAX_COMMAND_HISTORY + 1);
}

static S32 ClientChat_FindChatUserInfoIndex(const ChatUserInfo ***peaChatUsers, const ChatUserInfo *pInfo) {
	int i;

	if (pInfo->accountID == 0) {
		return -1;
	}

	for (i=0; i < eaSize(peaChatUsers); i++) {
		if (pInfo->accountID == (*peaChatUsers)[i]->accountID) {
			return i;
		}
	}

	return -1;
}

static bool ClientChat_ChatUserInfoMatch(const ChatUserInfo *pInfo1, const ChatUserInfo *pInfo2) {
	if (pInfo1 == pInfo2) {
		return true;
	}

	if (!pInfo1 || !pInfo2) {
		return false;
	}

	if (pInfo1->accountID != pInfo2->accountID) {
		return false;
	}

	if (pInfo1->playerID != pInfo2->playerID) {
		return false;
	}

	if (stricmp_safe(pInfo1->pchHandle, pInfo2->pchHandle) != 0) {
		return false;
	}

	if (stricmp_safe(pInfo1->pchName, pInfo2->pchName) != 0) {
		return false;
	}

	return true;
}

static void ClientChat_UpdateRecentUser(ChatUserInfo ***peaRecentUsers, ChatUserInfo *pUser, U32 uAccountID)
{
	S32 iIndex;
	// Make sure the user is another player (i.e. not self and not an NPC)
	if (!pUser || pUser->accountID == 0 || pUser->accountID == uAccountID)
		return;
	iIndex = ClientChat_FindChatUserInfoIndex(peaRecentUsers, pUser);

	// If the user already exists in the recent correspondents list, move them
	// to the top of the list.
	if (iIndex >= 0)
	{
		if (ClientChat_ChatUserInfoMatch(pUser, (*peaRecentUsers)[iIndex]))
		{
			if (iIndex != 0)
			{
				// Nothing changed, so just move the user info to the top of the list
				eaMove(peaRecentUsers, 0, iIndex);
			}
		}
		else
		{
			ChatUserInfo *pCopy = StructClone(parse_ChatUserInfo, pUser);
			StructDestroy(parse_ChatUserInfo, eaRemove(peaRecentUsers, iIndex));
			eaInsert(peaRecentUsers, pCopy, 0);
		}
	}
	else
	{
		// The user doesn't exist in the list, to insert them at the top
		// and trim the last entry if we've hit MAX_RECENT_CHAT_USERS.
		ChatUserInfo *pCopy = StructClone(parse_ChatUserInfo, pUser);
		eaInsert(peaRecentUsers, pCopy, 0);

		if (eaSize(peaRecentUsers) > MAX_RECENT_CHAT_USERS)
			StructDestroy(parse_ChatUserInfo, eaPop(peaRecentUsers));
	}
}

static void ClientChat_UpdateRecentSenderReceivers(const ChatMessage *pMsg, U32 uAccountID)
{
	if (pMsg && uAccountID)
	{
		// Update recent senders
		ClientChat_UpdateRecentUser(&s_eaRecentChatSenders, pMsg->pFrom, uAccountID);

		// Update recent receivers
		ClientChat_UpdateRecentUser(&s_eaRecentChatReceivers, pMsg->pTo, uAccountID);

		// Update recent tell correspondents
		if (pMsg->eType == kChatLogEntryType_Private || pMsg->eType == kChatLogEntryType_Private_Sent)
		{
			ChatUserInfo *pUser = pMsg->eType == kChatLogEntryType_Private ? pMsg->pFrom : pMsg->pTo;
			ClientChat_UpdateRecentUser(&s_eaRecentTellCorrespondents, pUser, uAccountID);
		}
	}
}

// ------------------------------------------------------------
// Command History Functions

static void CommandHistoryFree( CommandHistoryEntry *cmd ) {
	StructDestroy(parse_CommandHistoryEntry, cmd);
}

//Removes the last item in the history array if it was a temporary command (tstamp of 0)
static bool CommandHistoryRemoveCurrent(void) {
	if (eaSize(&s_eaCommandHistory) > 0) {
		CommandHistoryEntry* pLatestCmd = eaGet(&s_eaCommandHistory, eaSize(&s_eaCommandHistory)-1);
		if (!pLatestCmd->nonTemporary) {
			CommandHistoryFree(eaRemove(&s_eaCommandHistory, eaSize(&s_eaCommandHistory)-1));
			return true;
		}
	}

	return false;
}

static CommandHistoryEntry *CommandHistoryNew(const char *command, bool nonTemporary, SA_PARAM_OP_VALID const ChatData *pData) {
	CommandHistoryEntry *pNewCmdHistory = StructCreate(parse_CommandHistoryEntry);
	if (eaSize(&s_eaCommandHistory) > 0 && nonTemporary) {
		while(eaSize(&s_eaCommandHistory) >= MAX_COMMAND_HISTORY) {
			CommandHistoryFree(eaRemove(&s_eaCommandHistory, 0));
		}
	}

	pNewCmdHistory->nonTemporary = nonTemporary;
	pNewCmdHistory->command = StructAllocString(command);
	pNewCmdHistory->pData = pData ? StructClone(parse_ChatData, pData) : NULL;
	eaPush(&s_eaCommandHistory, pNewCmdHistory);

	return pNewCmdHistory;
}

// Returns NULL if the index is not found
static CommandHistoryEntry* CommandHistoryGet(int history_idx) {
	return eaGet(&s_eaCommandHistory, history_idx);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_Escape);
void ClientChat_Escape(void) {
	CommandHistoryRemoveCurrent();
	s_iCommandHistoryIdx = eaSize(&s_eaCommandHistory);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_HistoryUpData);
const char* ClientChat_HistoryUp(const char *current_text, SA_PARAM_OP_VALID const ChatData *pData) {
	CommandHistoryEntry *pEntryReturn;

	//If you're at the end of the history and hit "up", save the current text
	if(s_iCommandHistoryIdx >= eaSize(&s_eaCommandHistory) )
	{
		if(current_text && *current_text)
		{
			//If there was a current, remove it
			CommandHistoryRemoveCurrent();
			//Then save the new current
			CommandHistoryNew(current_text, 0, pData);

			s_iCommandHistoryIdx = eaSize(&s_eaCommandHistory)-1;
		}
	}
	else
	{
		//And you edited the command
		CommandHistoryEntry *pEntry = CommandHistoryGet(s_iCommandHistoryIdx);
		if (pEntry && stricmp_safe(pEntry->command, current_text) != 0) {
			//Remove any current
			CommandHistoryRemoveCurrent();

			//Save your edited command
			if(current_text && *current_text) {
				CommandHistoryNew(current_text, 0, pData);
				s_iCommandHistoryIdx = eaSize(&s_eaCommandHistory)-1;
			} else {
				s_iCommandHistoryIdx = eaSize(&s_eaCommandHistory);
			}
		}
	}

	if(s_iCommandHistoryIdx > 0) {
		s_iCommandHistoryIdx--;
	}

	pEntryReturn = CommandHistoryGet(s_iCommandHistoryIdx);
	return pEntryReturn ? pEntryReturn->command : "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_HistoryDownData);
const char* ClientChat_HistoryDown(const char* current_text, SA_PARAM_OP_VALID const ChatData *pData) {
	//If you're in the command history
	if( s_iCommandHistoryIdx < eaSize(&s_eaCommandHistory) )
	{
		//And you edited the command
		CommandHistoryEntry *pEntry = CommandHistoryGet(s_iCommandHistoryIdx);
		CommandHistoryEntry *pEntryReturn;

		if (pEntry && stricmp_safe(pEntry->command, current_text) != 0) {
			//Remove any current
			CommandHistoryRemoveCurrent();

			//Save your edited command
			if(current_text && *current_text) {
				CommandHistoryNew(current_text, 0, NULL);
			}

			s_iCommandHistoryIdx = eaSize(&s_eaCommandHistory)-1;
		}
		else if(s_iCommandHistoryIdx != eaSize(&s_eaCommandHistory)-1)
			++s_iCommandHistoryIdx;

		pEntryReturn = CommandHistoryGet( s_iCommandHistoryIdx );
		return pEntryReturn ? pEntryReturn->command : "";
	}

	return(current_text);
}

static ChatData* ClientChat_GetCurrentHistoryData(void) {
	CommandHistoryEntry *pEntry = CommandHistoryGet(s_iCommandHistoryIdx);
	ChatData *pData = pEntry ? pEntry->pData : NULL;
	return pData;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_GetEncodedCurrentHistoryData);
const char* ClientChat_GetEncodedCurrentHistoryData(void) {
	static char *s_encodedChatData = NULL;
	ChatData *pData = ClientChat_GetCurrentHistoryData();

	if (pData) {
		ChatData_Encode(&s_encodedChatData, pData);
		return s_encodedChatData;
	}

	return "";
}

// Links have the form: 
//		<link game="xx" type="xx" key="xx" text="xx"/>
/*
static void ChatLog_ParseEntryLinks(ChatLogEntry *pEntry) {
	ChatData_DecodeLinks(&pEntry->msg, &pEntry->eaLinkInfos, pEntry->rawMsg);
}
*/

void CommandHistory_AddNewMessage(const char *msg, ChatData *pData)
{
	//If there was a current, remove it
	CommandHistoryRemoveCurrent();

	if (*msg) {
		if(s_iCommandHistoryIdx < eaSize(&s_eaCommandHistory)) {
			CommandHistoryEntry *pEntry = CommandHistoryGet(s_iCommandHistoryIdx);
			if (pEntry && stricmp_safe(pEntry->command, msg) == 0) {
				CommandHistoryFree(eaRemove(&s_eaCommandHistory, s_iCommandHistoryIdx));
			}
		}

		//Store the command into the history
		CommandHistoryNew(msg, true, pData);
	}

	s_iCommandHistoryIdx = eaSize(&s_eaCommandHistory);
}

// ------------------------------------------------------------
// Chat Log Functions

// Sets the text in the chat text entry gen with /tell reply text
AUTO_COMMAND ACMD_NAME("Chat_SetReplyText") ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_CATEGORY(Interface);
bool ClientChat_SetReplyText(const char *pchTextEntryGen) {
	if (pchTextEntryGen && *pchTextEntryGen) {
		char *pchCommand = NULL;
		int iResult;

		// Dev note: we build up a command instead of finding the gen and setting the text
		// directly on it because we don't have access to the text entry gen from this
		// project.
		if (s_pchLastTellFromNameAndHandle) {
			estrPrintf(&pchCommand, "GenSetText %s /tell %s, ", pchTextEntryGen, s_pchLastTellFromNameAndHandle);
		} else {
			estrPrintf(&pchCommand, "GenSetText %s /tell ", pchTextEntryGen);
		}
		
		iResult = globCmdParse(pchCommand);
		estrDestroy(&pchCommand);

		return (bool) iResult;
	}

	return false;
}

// Sets the text in the chat text entry gen with /tell reply text
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenChatSetReplyText");
bool ClientChat_GenSetReplyText(SA_PARAM_NN_VALID UIGen *pGen) {
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextEntry)) {
		char *pchCommand = NULL;
		bool bResult;
		if (s_pchLastTellFromNameAndHandle) {
			estrPrintf(&pchCommand, "/tell %s, ", s_pchLastTellFromNameAndHandle);
		} else {
			estrPrintf(&pchCommand, "/tell ");
		}
		bResult = ui_GenTextEntrySetText(pGen, pchCommand);
		estrDestroy(&pchCommand);
		return bResult;
	}
	return false;
}

AUTO_COMMAND ACMD_NAME("Chat_SetReplyLastText") ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_CATEGORY(Interface);
bool ClientChat_SetReplyLastText(const char *pchTextEntryGen) {
	if (pchTextEntryGen && *pchTextEntryGen) {
		char *pchCommand = NULL;
		int iResult;

		// Dev note: we build up a command instead of finding the gen and setting the text
		// directly on it because we don't have access to the text entry gen from this
		// project.
		if (ClientChat_LastTellCorrespondent()) {
			estrPrintf(&pchCommand, "GenSetText %s /tell %s, ", pchTextEntryGen, ClientChat_LastTellCorrespondent());
		} else {
			estrPrintf(&pchCommand, "GenSetText %s /tell ", pchTextEntryGen);
		}
		
		iResult = globCmdParse(pchCommand);
		estrDestroy(&pchCommand);

		return (bool) iResult;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenChatSetReplyLastText");
bool ClientChat_GenSetReplyLastText(SA_PARAM_NN_VALID UIGen *pGen) {
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextEntry)) {
		char *pchCommand = NULL;
		bool bResult;
		if (ClientChat_LastTellCorrespondent()) {
			estrPrintf(&pchCommand, "/tell %s, ", ClientChat_LastTellCorrespondent());
		} else {
			estrPrintf(&pchCommand, "/tell ");
		}
		bResult = ui_GenTextEntrySetText(pGen, pchCommand);
		estrDestroy(&pchCommand);
		return bResult;
	}
	return false;
}

static void ChatLog_ShowBubbleMessage(SA_PARAM_NN_VALID const ChatMessage *pMsg) {
	// NOTE: NPC chat bubbles are currently handled in EntSayMsgWithBubble...
	//       We should probably move that here, but we also need to seed the 
	//       bubble style into the ChatMessage.

	if (pMsg->pData && pMsg->pData->bEmote)
		return;

	if(gbNoGraphics)
		return;

	switch (pMsg->eType) {
		case kChatLogEntryType_Local:
		//case kChatLogEntryType_Help:
		//case kChatLogEntryType_Lifetime:
		case kChatLogEntryType_NPC:
		case kChatLogEntryType_Team:
		case kChatLogEntryType_Zone: {
			if (pMsg->pFrom) {
				EntityRef entRef = pMsg->pFrom->nonPlayerEntityRef;
				if (!entRef) {
					Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pMsg->pFrom->playerID);
					if (pEnt) {
						entRef = entGetRef(pEnt);
					}
				}

				if (entRef) {
					F32 fDuration = pMsg->pData && pMsg->pData->pBubbleData
						? pMsg->pData->pBubbleData->fDuration 
						: 3.0f;
					const char *pchBubbleStyle = pMsg->pData && pMsg->pData->pBubbleData
						? pMsg->pData->pBubbleData->pchBubbleStyle 
						: NULL;
					ChatBubble_Say(entRef, pMsg->pchText, fDuration, pchBubbleStyle);
				}
			}

			break;
		}
	}
}

static void ChatLog_SendMsgToTestClient(SA_PARAM_NN_VALID const ChatMessage *pMsg)
{
	if(!gclGetLinkToTestClient()) return;

	if (pMsg->pFrom && pMsg->pFrom->pchName && *pMsg->pFrom->pchName && pMsg->pchText && *pMsg->pchText)
	{
		char *pcHandle = NULL;
		estrPrintf(&pcHandle, "%s@%s", pMsg->pFrom->pchName, pMsg->pFrom->pchHandle);

		if(!pMsg->pchChannel || !*pMsg->pchChannel)
		{
			gclScript_QueueChat(LOCAL_CHANNEL_NAME, pcHandle, pMsg->pchText);
			SendCommandStringToTestClientf("PushChat Local %u \"%s\" \"%s\"", pMsg->pFrom->playerID, pcHandle, pMsg->pchText);
		}
		else if(!strStartsWith(pMsg->pchChannel, PRIVATE_CHANNEL_SENT_NAME))
		{
			gclScript_QueueChat(pMsg->pchChannel, pcHandle, pMsg->pchText);
			SendCommandStringToTestClientf("PushChat \"%s\" %u \"%s\" \"%s\"", pMsg->pchChannel, pMsg->pFrom->playerID, pcHandle, pMsg->pchText);
		}

		estrDestroy(&pcHandle);
	}
}

SA_ORET_OP_VALID static ChatLogEntry *ChatLog_AddMessageInternal(SA_PARAM_OP_VALID ChatMessage *pMsg, bool bSendToTestClient)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(entActivePlayerPtr());
	ChatLogEntry *pEntry;
	Entity *pEnt = entActivePlayerPtr();
	S32 i, j;

	if (!pMsg || !pMsg->pchText || !*pMsg->pchText) {
		return NULL;
	}

	ChatLog_ShowBubbleMessage(pMsg);

	// Now that the Chat Bubble has processed the full chat string (with SMF), we'll strip the <sound> SMF and store the shortened string in the message for display to the chat log.
	// Whitespace should be stripped by now.
	if( !strnicmp(pMsg->pchText, "<sound", strlen("<sound")) )
	{
		char* pBegin = strchr(pMsg->pchText,'>');
		if( pBegin )
		{
			++pBegin;
			while(isspace((unsigned char)*pBegin))
				++pBegin;

			if(!*pBegin)
				return NULL; //After removing the "<sound *>" and all the whitespace, the message is empty.

			estrCopy2(&pMsg->pchText, pBegin);
		}
	}

	if (!*pMsg->pchText) {
		return NULL;
	}

	pEntry = StructCreate(parse_ChatLogEntry);
	pEntry->id = s_lifetimeChatLogEntryCount++;
	pEntry->pMsg = StructClone(parse_ChatMessage, pMsg);
	pEntry->iTimestamp = timeSecondsSince2000();

	//ChatLogEntry_ParseLinks(pEntry);

	if (s_bChatLog)
	{
		static char *s_pchChatLogLine;
		FormatGameMessageKey(&s_pchChatLogLine, "ChatLineEntry_FileLog",
			STRFMT_DATETIME("Time", pEntry->iTimestamp),
			STRFMT_UGLYINT("TimeRaw", pEntry->iTimestamp),
			STRFMT_UGLYINT("Instance", SAFE_MEMBER2(pEntry, pMsg, iInstanceIndex)),
			STRFMT_INT("InstanceFormatted", SAFE_MEMBER2(pEntry, pMsg, iInstanceIndex)),
			STRFMT_STRING("FromName", SAFE_MEMBER3(pEntry, pMsg, pFrom, pchName)),
			STRFMT_STRING("FromHandle", SAFE_MEMBER3(pEntry, pMsg, pFrom, pchHandle)),
			STRFMT_STRING("ToName", SAFE_MEMBER3(pEntry, pMsg, pTo, pchName)),
			STRFMT_STRING("ToHandle", SAFE_MEMBER3(pEntry, pMsg, pTo, pchHandle)),
			STRFMT_STRING("ChannelInternal", SAFE_MEMBER2(pEntry, pMsg, pchChannel)),
			STRFMT_STRING("ChannelDisplay", SAFE_MEMBER2(pEntry, pMsg, pchChannelDisplay)),
			STRFMT_STRING("Type", StaticDefineIntRevLookup(ChatLogEntryTypeEnum, SAFE_MEMBER2(pEntry, pMsg, eType))),
			STRFMT_STRING("Id", SAFE_MEMBER2(pEntry, pMsg, pchId)),
			STRFMT_STRING("Subject", SAFE_MEMBER2(pEntry, pMsg, pchSubject)),
			STRFMT_STRING("Thread", SAFE_MEMBER2(pEntry, pMsg, pchThread)),
			STRFMT_STRING("Text", SAFE_MEMBER2(pEntry, pMsg, pchText)),
			STRFMT_END);
		if (s_pchChatLogLine && *s_pchChatLogLine)
		{
			logDirectWrite("chat.log", s_pchChatLogLine);
		}
		estrClear(&s_pchChatLogLine);
	}

	gclChatLogFormat(pConfig, pEntry);

	eaPush(&s_eaPendingChatLog, pEntry);
	ClientChat_UpdateRecentSenderReceivers(pMsg, gclClientChat_GetAccountID());

	// Update the new indicators
	for (i = 0; i < eaSize(&s_eaTabStates) && pConfig; i++)
	{
		ChatTabConfig *pTabConfig = NULL;
		for (j = eaSize(&pConfig->eaChatTabGroupConfigs) - 1; j >= 0 && !pTabConfig; j--)
		{
			ChatTabGroupConfig *pTabGroupConfig = pConfig->eaChatTabGroupConfigs[j];
			S32 iPos = ChatCommon_FindTabConfigIndex(pTabGroupConfig, s_eaTabStates[i]->pchTabName);
			pTabConfig = eaGet(&pTabGroupConfig->eaChatTabConfigs, iPos);
		}
		if (pTabConfig && ChatCommon_IsLogEntryVisibleInTab(pTabConfig, pEntry->pMsg->eType, pEntry->pMsg->pchChannel))
		{
			s_eaTabStates[i]->uLastID = pEntry->id;
			s_eaTabStates[i]->uLastTimestamp = pEntry->iTimestamp;
			s_eaTabStates[i]->bNew = true;
		}
	}

	// If the message is a tell, then keep track of who sent it so we can reply later
	if (pMsg->eType == kChatLogEntryType_Private && pMsg->pFrom)
	{
		char *pchName = pMsg->pFrom->pchName ? pMsg->pFrom->pchName : "";
		char chAt = pMsg->pFrom->pchHandle && *pMsg->pFrom->pchHandle ? '@' : '\0';
		char *pchHandle = pMsg->pFrom->pchHandle ? pMsg->pFrom->pchHandle : "";

		if (pMsg->pFrom->pchHandle && pMsg->pFrom->pchHandle[0])
		{
			ClientChat_UpdatePrivateChatWindow(pMsg->pFrom->pchHandle);
		}

		estrPrintf(&s_pchLastTellFromNameAndHandle, "%s%c%s", pchName, chAt, pchHandle);
		notify_NotifySend(pEnt, kNotifyType_ChatTellReceived, pMsg->pchText, SAFE_MEMBER2(pMsg, pFrom, pchHandle), NULL);
	}

	if (pMsg->eType == kChatLogEntryType_Team && SAFE_MEMBER(pEnt, myContainerID) && (SAFE_MEMBER(pEnt, myContainerID) != SAFE_MEMBER2(pMsg, pFrom, playerID)))
	{
		notify_NotifySend(pEnt, kNotifyType_ChatTeamMessageReceived, pMsg->pchText, SAFE_MEMBER2(pMsg, pFrom, pchHandle), NULL);
	}

	if (bSendToTestClient) {
		ChatLog_SendMsgToTestClient(pMsg);
	}

	return pEntry;
}

AUTO_COMMAND ACMD_NAME(Clear) ACMD_ACCESSLEVEL(0);
void ChatLog_Clear(void)
{
	s_bClearChatLog = true;
}

static StashTable s_stEchoErrorFileShow = NULL;
void ChatLog_Echo(const char *pchFilename, const char *pchText)
{
	if (!pchFilename) {
		pchFilename = "Unknown Location";
	}

	if (!s_stEchoErrorFileShow) {
		s_stEchoErrorFileShow = stashTableCreateWithStringKeys(11, StashDefault);
	}

	if (!stashGetKey(s_stEchoErrorFileShow, pchFilename, NULL)) {
		Errorf("%s: Echo should not be used in production code.  It is only meant to be used for debugging.  You will only get this error once per file per session.", pchFilename);
		stashAddPointer(s_stEchoErrorFileShow, pchFilename, pchFilename, false);
	}

	ChatLog_AddSystemMessage(pchText, "Echo");
}

// Takes the number of lines you want, and an earray.  Strings are allocated using StructAllocString().
void ChatLog_GetLastLines(S32 iLineCount, char ***pppchLines)
{
	int i, s;

	if(!g_ChatLog)
		return;

	s = eaSize(&g_ChatLog);
	for (i = s - 1; i >= 0 && s-i < iLineCount; i--)
	{
		ChatLogEntry *pEntry = g_ChatLog[i];
		eaPush(pppchLines,StructAllocString(pEntry->pchFormattedText));
	}
}

void ChatLog_AddSystemMessage(SA_PARAM_OP_VALID const char *pchText, SA_PARAM_OP_VALID const char *pchFromTag)
{
	ChatLog_AddSystemMessageWithData(pchText, pchFromTag, NULL);
}

void ChatLog_AddSystemMessageWithData(SA_PARAM_OP_VALID const char *pchText, SA_PARAM_OP_VALID const char *pchFromTag, const ChatData *pData)
{
	// See Core/data/messages/ChatMessageTags.ms
	ChatMessage *pMsg = NULL;
	ChatUserInfo *pFrom = NULL;

	if (!pchText || !pchText[0]) {
		return;
	}

	if (pchFromTag && *pchFromTag) {
		pFrom = ChatCommon_CreateUserInfoFromNameOrHandle(pchFromTag);
	}

	pMsg = ChatCommon_CreateMsg(pFrom, NULL, kChatLogEntryType_System, NULL, pchText, pData);
	ChatLog_AddMessageInternal(pMsg, false);
	StructDestroy(parse_ChatMessage, pMsg);
	StructDestroy(parse_ChatUserInfo, pFrom);
}

static void ChatLog_AddSystemMessagePrintCallback(char *pchText)
{
	ChatLog_AddSystemMessage(pchText, NULL);
}

void ChatLog_OncePerFrame(void)
{
	PERFINFO_AUTO_START_FUNC();
	{
		ChatConfig *pConfig = ClientChatConfig_GetChatConfig(entActivePlayerPtr());

		if (!conGetPrintCallback())
			conSetPrintCallback(ChatLog_AddSystemMessagePrintCallback);

		eaPushEArray(&g_ChatLog, &s_eaPendingChatLog);
		eaClearFast(&s_eaPendingChatLog);
		if (s_bClearChatLog)
		{
			eaClearStruct(&g_ChatLog, parse_ChatLogEntry);
			s_bClearChatLog = false;
		}
		while (eaSize(&g_ChatLog) > MAX_CHAT_LOG_ENTRIES)
			StructDestroy(parse_ChatLogEntry, eaRemove(&g_ChatLog, 0));

		if (pConfig && s_ChatLogFormatVersion != pConfig->iVersion)
		{
			S32 i, j;

			// Check for formatting change, this shouldn't happen very often.
			// So just process them all at once. Also, there's a small cap
			// on the number entries.
			for (i = eaSize(&g_ChatLog) - 1; i >= 0; --i)
				gclChatLogFormat(pConfig, g_ChatLog[i]);

			// Synchronize tab states
			if (!s_eaTabStates)
				eaIndexedEnable(&s_eaTabStates, parse_ChatTabState);
			for (i = eaSize(&pConfig->eaChatTabGroupConfigs) - 1; i >= 0; --i)
			{
				ChatTabGroupConfig *pTabGroupConfig = pConfig->eaChatTabGroupConfigs[i];
				for (j = eaSize(&pTabGroupConfig->eaChatTabConfigs) - 1; j >= 0; --j)
				{
					ChatTabConfig *pTabConfig = pTabGroupConfig->eaChatTabConfigs[j];
					ChatTabState *pTabState = eaIndexedGetUsingString(&s_eaTabStates, pTabConfig->pchTitle);
					if (!pTabState)
					{
						pTabState = StructCreate(parse_ChatTabState);
						pTabState->pchTabName = StructAllocString(pTabConfig->pchTitle);
						eaPush(&s_eaTabStates, pTabState);
					}
					pTabState->bUpdated = true;
				}
			}
			for (i = eaSize(&s_eaTabStates) - 1; i >= 0; --i)
			{
				if (!s_eaTabStates[i]->bUpdated)
					StructDestroy(parse_ChatTabState, eaRemove(&s_eaTabStates, i));
				else
					s_eaTabStates[i]->bUpdated = false;
			}

			s_ChatLogFormatVersion = pConfig->iVersion;
			g_ChatLogVersion++;
		}

		ClientChat_PrivateChatWindowsOncePerFrame();
	}
	PERFINFO_AUTO_STOP_FUNC();
}

void ChatLog_AddChatMessage(ChatLogEntryType eType, SA_PARAM_OP_VALID const char *pchText, SA_PARAM_OP_VALID const ChatData *pData)
{
	ChatMessage *pMsg = ChatCommon_CreateMsg(NULL, NULL, eType, NULL, pchText, pData);
	ChatLog_AddMessageInternal(pMsg, false);
	StructDestroy(parse_ChatMessage, pMsg);
}

void ChatLog_AddEntityMessage(ChatUserInfo *pFrom, ChatLogEntryType eType, SA_PARAM_OP_VALID const char *pchText, SA_PARAM_OP_VALID const ChatData *pData)
{
	ChatMessage *pMsg = ChatCommon_CreateMsg(pFrom, NULL, eType, NULL, pchText, pData);
	ChatLog_AddMessageInternal(pMsg, false);
	StructDestroy(parse_ChatMessage, pMsg);
}

ChatUserInfo ***ChatLog_GetRecentChatSenders(void) {
	return &s_eaRecentChatSenders;
}

ChatUserInfo ***ChatLog_GetRecentChatReceivers(void) {
	return &s_eaRecentChatReceivers;
}

ChatUserInfo ***ChatLog_GetRecentTellCorrespondents(void) {
	return &s_eaRecentTellCorrespondents;
}

void ClientChat_DumpChatTypes(void) {
	Entity *pEntity = entActivePlayerPtr();
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	S32 i;
	char *pchMessage = NULL;

	for (i=1; ChatLogEntryTypeEnum[i].key; i++) {
		ChatLogEntryType eType = ChatLogEntryTypeEnum[i].value;
		U32 iColor = 0;
		if (pConfig) {
			iColor = ChatCommon_GetChatColor(pConfig, eType, NULL, ChatCommon_GetChatConfigSourceForEntity(pEntity));
		}
		estrPrintf(&pchMessage, "This is a '%s' message.  Color is: 0x%x", ChatLogEntryTypeEnum[i].key, iColor);
		ChatLog_AddChatMessage(eType, pchMessage, NULL);
	}

	estrDestroy(&pchMessage);
}

// For debug use only - Fill the chat log with interesting stuff
void ClientChat_FillChatLog(void) {
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(entActivePlayerPtr());
	char *pchMessage = NULL;
	
	// Dump one line to use as a marker
	estrPrintf(&pchMessage, "Entry # %d", s_lifetimeChatLogEntryCount);
	ChatLog_AddChatMessage(kChatLogEntryType_System, pchMessage, NULL);

	while (eaSize(&s_eaPendingChatLog) < MAX_CHAT_LOG_ENTRIES) {
		// Dump a bunch of lines to add variety
		ClientChat_DumpChatTypes();

		// Dump one line to use as a marker
		estrPrintf(&pchMessage, "Entry # %d", s_lifetimeChatLogEntryCount);
		ChatLog_AddChatMessage(kChatLogEntryType_System, pchMessage, NULL);
	}

	estrDestroy(&pchMessage);
}

AUTO_COMMAND ACMD_GENERICCLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void ChatLog_AddMessage(SA_PARAM_NN_VALID ChatMessage *pMsg, bool bSendToTestClient)
{
	ChatLog_AddMessageInternal(pMsg, bSendToTestClient);
}

const char * ClientChat_LastTellSender(void)
{
	return s_pchLastTellFromNameAndHandle;
}


const char * ClientChat_LastTellCorrespondent(void)
{
	static char *s_estrLastTellCorrespondent = NULL;
	ChatUserInfo ***peaUserInfos = ChatLog_GetRecentTellCorrespondents();
	if( peaUserInfos && eaSize(peaUserInfos) > 0 )
	{
		ChatUserInfo *pInfo = (*peaUserInfos)[0];
		char *pchName = pInfo->pchName ? pInfo->pchName : "";
		char chAt = pInfo->pchHandle && *pInfo->pchHandle ? '@' : '\0';
		char *pchHandle = pInfo->pchHandle ? pInfo->pchHandle : "";
		estrPrintf(&s_estrLastTellCorrespondent, "%s%c%s", pchName, chAt, pchHandle);
	}
	else
	{
		estrClear(&s_estrLastTellCorrespondent);
	}
	return s_estrLastTellCorrespondent;
}

// Private chat window functions

// Once per frame processing for private chat windows
static void ClientChat_PrivateChatWindowsOncePerFrame(void)
{
	if (s_eaPrivateChatWindows)
	{
		const char * pchOfflineText = TranslateMessageKey("Player.Offline");

		S32 i;

		for (i = eaSize(&s_eaPrivateChatWindows) - 1; i >= 0; i--)
		{
			PrivateChatWindow *pWindow = s_eaPrivateChatWindows[i];
			if (pWindow->bDeleteOnNextFrame)
			{
				eaRemove(&s_eaPrivateChatWindows, i);
				StructDestroy(parse_PrivateChatWindow, pWindow);
			}
			else
			{
				ChatPlayerStruct *pChatPlayer = FindFriendByHandle(pWindow->pchHandle);

				if (pChatPlayer)
				{
					pWindow->bOnline = !(pChatPlayer->online_status == USERSTATUS_OFFLINE || pChatPlayer->online_status & USERSTATUS_HIDDEN);

					if (!pWindow->bOnline)
					{
						if (strcmp_safe(pWindow->pchName, pchOfflineText) != 0)
						{
							estrCopy2(&pWindow->pchName, pchOfflineText);
						}	
					}
					else if (strcmp_safe(pWindow->pchName, pChatPlayer->pPlayerInfo.onlinePlayerName) != 0)
					{
						estrCopy2(&pWindow->pchName, pChatPlayer->pPlayerInfo.onlinePlayerName);
					}

					pWindow->accountID = pChatPlayer->accountID;
					pWindow->onlineCharacterID = pChatPlayer->pPlayerInfo.onlineCharacterID;
					pWindow->pchClassName = allocAddString(pChatPlayer->pPlayerInfo.pchClassName);
					pWindow->pchPathName = allocAddString(pChatPlayer->pPlayerInfo.pchPathName);
					if (strcmp_safe(pWindow->pchLocationMessageKey, pChatPlayer->pPlayerInfo.pLocationMessageKey) != 0)
					{
						estrCopy2(&pWindow->pchLocationMessageKey, pChatPlayer->pPlayerInfo.pLocationMessageKey);
					}
					pWindow->iPlayerLevel = pChatPlayer->pPlayerInfo.iPlayerLevel;
					pWindow->uLoginServerID = pChatPlayer->pPlayerInfo.uLoginServerID;
					pWindow->iPlayerTeam = pChatPlayer->pPlayerInfo.iPlayerTeam;
				}
				else
				{
					pWindow->bOnline = false;
					pWindow->accountID = 0;
					pWindow->onlineCharacterID = 0;
					pWindow->pchClassName = NULL;
					pWindow->pchPathName = NULL;
					estrClear(&pWindow->pchLocationMessageKey);
					pWindow->iPlayerLevel = 0;
					pWindow->uLoginServerID = 0;
					pWindow->iPlayerTeam = 0;

					if (strcmp_safe(pWindow->pchName, pchOfflineText) != 0)
					{
						estrCopy2(&pWindow->pchName, pchOfflineText);
					}	
				}
			}
		}
	}
}

static bool ClientChat_PrivateChatWindowsEnabled(void)
{
	ChatConfigDefaults *pConfig = ChatCommon_GetConfigDefaults(CHAT_CONFIG_SOURCE_PC);
	return pConfig ? pConfig->bEnablePrivateChatWindowSystem : false;
}

static void ClientChat_RemoveAllPrivateChatWindows(void)
{
	eaClearStruct(&s_eaPrivateChatWindows, parse_PrivateChatWindow);
}

static void ClientChat_AddPrivateChatWindowToList(SA_PARAM_NN_VALID PrivateChatWindow *pWindow)
{
	eaPush(&s_eaPrivateChatWindows, pWindow);
}

S32 ClientChat_FindPrivateChatWindowCmp(const PrivateChatWindow *pWindow, const char ** ppchHandle)
{
	return pWindow && ppchHandle ? stricmp_safe(pWindow->pchHandle, *ppchHandle) == 0 : 0;
}

static bool ClientChat_RemovePrivateChatWindowFromList(SA_PARAM_OP_STR const char * pchHandle)
{
	PERFINFO_AUTO_START_FUNC();

	if (s_eaPrivateChatWindows && pchHandle && pchHandle[0])
	{
		S32 iIndex = eaFindCmp(&s_eaPrivateChatWindows, &pchHandle, ClientChat_FindPrivateChatWindowCmp);

		if (iIndex >= 0)
		{
			s_eaPrivateChatWindows[iIndex]->bDeleteOnNextFrame = true;

			PERFINFO_AUTO_STOP_FUNC();

			return true;
		}
	}

	PERFINFO_AUTO_STOP_FUNC();

	return false;
}

static PrivateChatWindow * ClientChat_GetPrivateChatWindowFromList(SA_PARAM_OP_STR const char *pchHandle)
{
	PERFINFO_AUTO_START_FUNC();

	if (s_eaPrivateChatWindows && pchHandle && pchHandle[0])
	{		
		S32 iIndex = eaFindCmp(&s_eaPrivateChatWindows, &pchHandle, ClientChat_FindPrivateChatWindowCmp);
		if (iIndex >= 0)
		{
			PERFINFO_AUTO_STOP_FUNC();

			return s_eaPrivateChatWindows[iIndex];
		}
	}

	PERFINFO_AUTO_STOP_FUNC();

	return NULL;
}

static PrivateChatWindow * ClientChat_GetNewPrivateChatWindow(SA_PARAM_NN_STR const char * pchHandle, bool bMinimized)
{
	PrivateChatWindow *pWindow = StructCreate(parse_PrivateChatWindow);

	// Set the handle name
	estrCopy2(&pWindow->pchHandle, pchHandle);

	// Set the minimized state
	pWindow->bMinimized = bMinimized;

	// Set the window creation time
	pWindow->iTimeCreated = timeSecondsSince2000();

	return pWindow;
}

// Set the minimized state of a private chat window
static bool ClientChat_SetPrivateChatWindowMinimizedState(SA_PARAM_OP_STR const char * pchHandle, bool bMinimized)
{
	PrivateChatWindow *pWindow = ClientChat_GetPrivateChatWindowFromList(pchHandle);

	if (pWindow)
	{
		pWindow->bMinimized = bMinimized;
		return true;
	}

	return false;
}

// Clears the unread message flag for the private chat window of the given handle
static bool ClientChat_ClearPrivateChatWindowUnreadMessageFlag(SA_PARAM_OP_STR const char * pchHandle)
{
	PrivateChatWindow *pWindow = ClientChat_GetPrivateChatWindowFromList(pchHandle);

	if (pWindow)
	{
		pWindow->bHasUnreadMessage = false;
		return true;
	}

	return false;
}

// Opens a new private chat window for the given handle if one already does not exist
static void ClientChat_OpenPrivateChatWindow(SA_PARAM_NN_STR const char * pchHandle, bool bMinimized)
{
	PrivateChatWindow *pWindow = ClientChat_GetPrivateChatWindowFromList(pchHandle);
	if (pWindow)
	{
		pWindow->bMinimized = bMinimized;
		pWindow->bDeleteOnNextFrame = false;
	}
	else
	{
		pWindow = ClientChat_GetNewPrivateChatWindow(pchHandle, bMinimized);

		// Add to the list
		ClientChat_AddPrivateChatWindowToList(pWindow);
	}
}

// Closes the private chat window for the given handle if one already exists
static bool ClientChat_ClosePrivateChatWindow(SA_PARAM_NN_STR const char * pchHandle)
{
	return ClientChat_RemovePrivateChatWindowFromList(pchHandle);
}

// Updates the private chat window info for the given handle after a new private message is received
static void ClientChat_UpdatePrivateChatWindow(SA_PARAM_NN_STR const char * pchHandle)
{
	PrivateChatWindow *pWindow;

	if (!ClientChat_PrivateChatWindowsEnabled())
	{
		return;
	}

	if ((pWindow = ClientChat_GetPrivateChatWindowFromList(pchHandle)) == NULL)
	{
		pWindow = ClientChat_GetNewPrivateChatWindow(pchHandle, true);

		// Add to the list
		ClientChat_AddPrivateChatWindowToList(pWindow);
	}

	pWindow->bDeleteOnNextFrame = false;

	if (pWindow->bMinimized)
	{
		pWindow->bHasUnreadMessage = true;
	}
}

static const char * ClientChat_StripAtFromHandle(SA_PARAM_OP_STR const char *pchHandle)
{
	if (pchHandle[0] == '@' && pchHandle[0] != '\0')
	{
		static char *estrHandle = NULL;
		estrCopy2(&estrHandle, pchHandle + 1);
		return estrHandle;
	}

	return pchHandle;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PrivateChatWindowOpen);
void exprPrivateChatWindowOpen(SA_PARAM_NN_STR const char *pchHandle, bool bMinimized) 
{
	if (ClientChat_PrivateChatWindowsEnabled())
	{
		ClientChat_OpenPrivateChatWindow(ClientChat_StripAtFromHandle(pchHandle), bMinimized);
	}	
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PrivateChatWindowClose);
bool exprPrivateChatWindowClose(SA_PARAM_NN_STR const char *pchHandle) 
{
	if (!ClientChat_PrivateChatWindowsEnabled())
	{
		return false;
	}
	return ClientChat_ClosePrivateChatWindow(ClientChat_StripAtFromHandle(pchHandle));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PrivateChatWindowSetMinimized);
bool exprPrivateChatWindowSetMinimized(SA_PARAM_OP_STR const char * pchHandle, bool bMinimized)
{
	if (!ClientChat_PrivateChatWindowsEnabled())
	{
		return false;
	}
	return ClientChat_SetPrivateChatWindowMinimizedState(ClientChat_StripAtFromHandle(pchHandle), bMinimized);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PrivateChatWindowClearUnreadMessageFlag);
bool exprPrivateChatWindowClearUnreadMessageFlag(SA_PARAM_OP_STR const char * pchHandle)
{
	if (!ClientChat_PrivateChatWindowsEnabled())
	{
		return false;
	}
	return ClientChat_ClearPrivateChatWindowUnreadMessageFlag(ClientChat_StripAtFromHandle(pchHandle));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PrivateChatWindowGetByHandle);
SA_RET_OP_VALID PrivateChatWindow * exprPrivateChatWindowGetByHandle(SA_PARAM_NN_STR const char *pchHandle) 
{
	return ClientChat_GetPrivateChatWindowFromList(ClientChat_StripAtFromHandle(pchHandle));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PrivateChatWindowGetAll);
void exprPrivateChatWindowGetAll(SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenSetList(pGen, &s_eaPrivateChatWindows, parse_PrivateChatWindow);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PrivateChatWindowCloseAll);
void exprPrivateChatWindowCloseAll(void)
{
	ClientChat_RemoveAllPrivateChatWindows();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ChatTabHasReceivedNewMessage");
bool ChatLog_HasReceivedNewMessage(const char *pchTabName)
{
	ChatTabState *pTabState = eaIndexedGetUsingString(&s_eaTabStates, pchTabName);
	return pTabState && pTabState->bNew;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ChatTabGetLastMessageTime");
S32 ChatLog_GetLastMessageTime(const char *pchTabName)
{
	ChatTabState *pTabState = eaIndexedGetUsingString(&s_eaTabStates, pchTabName);
	return pTabState ? pTabState->uLastTimestamp : 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ChatTabClearReceivedNewMessage");
void ChatLog_ClearReceivedNewMessage(const char *pchTabName)
{
	ChatTabState *pTabState = eaIndexedGetUsingString(&s_eaTabStates, pchTabName);
	if (pTabState)
		pTabState->bNew = false;
}

#include "AutoGen/gclChatlog_h_ast.c"

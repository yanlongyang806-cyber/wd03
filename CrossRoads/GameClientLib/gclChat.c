/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "color.h"
#include "cmdparse.h"
#include "earray.h"
#include "StashTable.h"
#include "StringUtil.h"
#include "StringFormat.h"
#include "MemoryPool.h"

#include "GfxConsole.h"
#include "UIGen.h"
#include "gclDialogBox.h"
#include "gclEntity.h"
#include "EntityLib.h"

#include "chatCommon.h"
#include "chatCommonStructs.h"
#include "ChatData.h"
#include "gclChat.h"
#include "chat/gclChatLog.h"
#include "chat/gclClientChat.h"
#include "gclChatConfig.h"
#include "gclChatChannelUI.h"
#include "GameClientLib.h"
#include "gclUIGen.h"

#include "Character.h"
#include "itemCommon.h"
#include "itemCommon_h_ast.h"
#include "inventoryCommon.h"
#include "Player.h"
#include "Powers.h"
#include "SimpleParser.h"
#include "FCInventoryUI.h"
#include "ui/UIGenChatLog.h"
#include "gclUIGenMapExpr.h"
#include "sndVoice.h"
#include "soundLib.h"

#include "Autogen/chatCommon_h_ast.h"
#include "Autogen/chatCommonStructs_h_ast.h"
#include "Autogen/ChatData_h_ast.h"
#include "Autogen/gclChatConfig_h_ast.h"
#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "Autogen/ChatRelay_autogen_GenericServerCmdWrappers.h"
#include "Autogen/UIGen_h_ast.h"

static ChatData *s_pCurrentChatData = NULL;

// Cached channel information
static char *s_pchJoinChannelRequest = NULL;
static ChatChannelInfo *s_pJoinChannelDetail = NULL;

static ChatConfigInputChannel **s_eaSubscribedChannels = NULL; // List of channels for the dropdown

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("chatCommon", BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("chatCommonStructs", BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("gameserverlib_autogen_servercmdwrappers", BUDGET_GameSystems););

AUTO_RUN;
void Chat_Initialize(void)
{
	ChatLog_Initialize();
	//ui_GenAddExprFuncs("Chat");
	g_UIGenFilterProfanityForPlayerCB = ClientChat_FilterProfanityForPlayer;
	ui_GenInitStaticDefineVars(ChatAccessEnum, "ChatAccess");
}

ChatChannelMember *GetChannelMember(SA_PARAM_OP_VALID const ChatChannelInfo *pInfo, SA_PARAM_OP_STR const char *pchHandle) {
	if (pInfo && pInfo->ppMembers && pchHandle && *pchHandle) {
		int i;
		for (i=0; i < eaSize(&pInfo->ppMembers); i++) {
			ChatChannelMember *pMember = eaGet(&pInfo->ppMembers, i);
			if (stricmp_safe(pchHandle, pMember->handle) == 0) {
				return pMember;
			}
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_GetNamePart);
const char* gclChat_GetNamePart(SA_PARAM_OP_VALID const char *pchNameAndHandle) {
	static char *pchName = NULL;
	const char *pchIndex = pchNameAndHandle ? strchr(pchNameAndHandle, '@') : NULL;
	S32 iLength = pchIndex ? pchIndex - pchNameAndHandle : 0;

	estrClear(&pchName);
	if (pchIndex) {
		estrConcat(&pchName, pchNameAndHandle, iLength);
	} else if (pchNameAndHandle) {
		estrCopy2(&pchName, pchNameAndHandle);
	}

	return pchName;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_GetHandlePart);
const char* gclChat_GetHandlePart(SA_PARAM_OP_VALID const char *pchNameAndHandle) {
	const char *pchHandle = pchNameAndHandle ? strchr(pchNameAndHandle, '@') : NULL;
	if (!pchHandle) {
		pchHandle = "";
	}

	return pchHandle;
}

// ------------------------------------------------------------
// Channel Chatting


const char ***ClientChat_GetSubscribedCustomChannels(void) {
	static const char **s_eaCustomChannels = NULL;

	eaClear(&s_eaCustomChannels);
	if (s_eaSubscribedChannels) {
		int i;

		for (i=0; i < eaSize(&s_eaSubscribedChannels); i++) {
			ChatConfigInputChannel *pInputChannel = eaGet(&s_eaSubscribedChannels, i);
			if (!ChatCommon_IsBuiltInChannel(pInputChannel->pchSystemName)) {
				eaPush(&s_eaCustomChannels, pInputChannel->pchSystemName);
			}
		}
	}

	return &s_eaCustomChannels;
}

// Attempt to get the system name of a subscribed channel from the 
// given name which may either be the channel or message type name.
// If the given channel is not currently subscribed to, then NULL is
// returned.
const char *ClientChat_GetSubscribedChannelSystemName(const char *pchChannel) {
	const char *pchSystemName = NULL;
	const char *pchChannelCheck = pchChannel;
	char *pchEstrChannel = NULL;
	int i;

	//Custom code for the team and team up channels
	if(strStartsWith(pchChannel,"Team"))
	{
		estrStackCreate(&pchEstrChannel);
		estrPrintf(&pchEstrChannel,"%sID",pchChannel);
		pchChannelCheck = pchEstrChannel;
	}

	// Special handling for local channel
	if (!pchChannel || !*pchChannel || !stricmp_safe(pchChannel, LOCAL_CHANNEL_NAME)) {
		return LOCAL_CHANNEL_NAME;
	}

	if (s_eaSubscribedChannels) {
		// Scan the list of known subscribed channels for the given channel name
		for (i=0; i < eaSize(&s_eaSubscribedChannels); i++) {
			ChatConfigInputChannel *pInputChannel = eaGet(&s_eaSubscribedChannels, i);
			const char *pchKnownChannel = pInputChannel->pchSystemName;

			// If we have an exact match, then use that
			if (!stricmp_safe(pchKnownChannel, pchChannel)) {
				pchSystemName = pchKnownChannel;
				break;
			}

			
			// Otherwise test to see if the known channel starts with the desired one
			// or whether the channel matches the display name of a subscribed channel.
			// If so, then check to see if the known channel is a built-in channel
			// If so, then use that known channel.
			if (strStartsWith(pchKnownChannel, pchChannelCheck) || !stricmp_safe(pInputChannel->pchDisplayName, pchChannelCheck)) {
				if (ChatCommon_IsBuiltInChannel(pchKnownChannel)) {
					pchSystemName = pchKnownChannel;
					break;
				}
			}
		}
	}

	if(pchEstrChannel)
		estrDestroy(&pchEstrChannel);

	return pchSystemName;
}

// Test whether or not the given channel is currently subscribed to.
// This will only return true to volatile subscriptions, such as "team" and "guild" 
// when the player is currently a member.  Using channel==NULL or "Local" will always
// return true.
bool ClientChat_IsCurrentlySubscribedChannel(const char *pchChannel) {
	return !!ClientChat_GetSubscribedChannelSystemName(pchChannel);
}

void ClientChat_FillInputChannelInput(ChatConfigInputChannel *pInputChannel, const char *pchSystemName, bool bForceSubscribed)
{
	Entity *pEnt = entActivePlayerPtr();
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEnt);
	bool bBuiltIn = ChatCommon_IsBuiltInChannel(pchSystemName);
	ChatChannelInfo *info = ClientChat_GetCachedChannelInfo(pchSystemName);
	
	if (!pchSystemName || !stricmp_safe(pchSystemName, LOCAL_CHANNEL_NAME))
	{
		estrCopy2(&pInputChannel->pchSystemName, LOCAL_CHANNEL_NAME);
		ClientChat_GetMessageTypeDisplayNameByType(&pInputChannel->pchDisplayName, kChatLogEntryType_Local);
		pInputChannel->bIsSubscribed = true;
		pInputChannel->iColor = ChatCommon_GetChatColor(pConfig, kChatLogEntryType_Local, LOCAL_CHANNEL_NAME, ChatCommon_GetChatConfigSourceForEntity(pEnt));
	}
	else if (bBuiltIn)
	{
		ChatLogEntryType kType = ChatCommon_GetChannelMessageType(pchSystemName);
		const char *pchMessageTypeName = ChatCommon_GetMessageTypeName(kType);

		estrCopy2(&pInputChannel->pchSystemName, pchSystemName);
		ClientChat_GetMessageTypeDisplayNameByType(&pInputChannel->pchDisplayName, kType);
		pInputChannel->bIsSubscribed = 
			bForceSubscribed || ClientChat_IsCurrentlySubscribedChannel(pchSystemName);
		if (pInputChannel->bIsSubscribed)
			pInputChannel->iColor = ChatCommon_GetChatColor(pConfig, kType, pchMessageTypeName, ChatCommon_GetChatConfigSourceForEntity(pEnt));
		else
			pInputChannel->iColor = ChatCommon_GetUnsubscribedChatColor(ChatCommon_GetChatConfigSourceForEntity(pEnt));
	}
	else
	{
		estrCopy2(&pInputChannel->pchSystemName, pchSystemName);
		if (info && info->pDisplayName && *info->pDisplayName)
			estrCopy2(&pInputChannel->pchDisplayName, info->pDisplayName);
		else
			estrCopy2(&pInputChannel->pchDisplayName, pchSystemName);
		pInputChannel->bIsSubscribed = true;
		pInputChannel->iColor = ChatCommon_GetChatColor(pConfig, kChatLogEntryType_Channel, pchSystemName, ChatCommon_GetChatConfigSourceForEntity(pEnt));
	}

	pInputChannel->bIsBuiltIn = bBuiltIn;
	pInputChannel->uLastUpdateTime = gGCLState.totalElapsedTimeMs;
	sprintf(pInputChannel->pchColorHex, "%x", pInputChannel->iColor);
}

static S32 ClientChat_GetGoodLinkInsertionPoint(SA_PARAM_NN_VALID ChatData *pData, S32 iStartPoint, S32 iLength) {
	// This assumes the links in ChatData are ordered by start position
	int i;

	for (i=0; i < eaSize(&pData->eaLinkInfos); i++) {
		ChatLinkInfo *pInfo = pData->eaLinkInfos[i];
		if (pInfo->iStart > iStartPoint) {
			// The link's start point is greater than the desired start
			// point, so we no longer need to iterate, but we do need
			// to check whether or not the desired start point & length
			// will fit before this link's start point.
			if (iStartPoint + iLength < pInfo->iStart) {
				return i;
			} else {
				return -1;
			}
		}

		// See if the current link span contains the desired start point start.
		// We already know that pInfo->iStart is <= to iStartPoint, so no need
		// to test that.
		if (pInfo->iStart+pInfo->iLength > iStartPoint) {
			return -1;
		}
	}

	// We either have no links or no conflicting links...we're good!
	return eaSize(&pData->eaLinkInfos);
}

// Realign the chat data links to match the text.  Remove chat links if the
// text for them no longer exists.
//
// Note: This heuristic is not perfect.  It will not catch duplicates that
//       occur via copy/paste, nor will it recognize the difference between
//       links that have the same display text (i.e. an item & power with the same
//       display text).  It will, however, preserve the count and type of links 
//       that have the same display text.
//
// Note: We assume that the links are ordered by start position.
//       That order will be maintained after fix up.
//
// Note: This is kind of ugly that we need to do this in the first place. 
//       A more ideal solution would be to create a new version of TextBuffer
//       that can handle textual spans and editing around them.  That or just
//       expose a link syntax to end users.  We're currently not doing that to
//       avoid letting player data mine to see what kinds of items/powers might
//       exist.
static void ClientChat_FixupChatData(ChatData *pData, SA_PARAM_NN_VALID const char *pchText) {
	if (pData && eaSize(&pData->eaLinkInfos)) {
		ChatLinkInfo **eaOrphanedLinks = NULL;
		int i;

		// First iterate over existing links to determine which are good and
		// which are orphaned.
		// We do this in reverse order so that we can delete orphans while we
		// iterate.
		for (i=eaSize(&pData->eaLinkInfos)-1; i >= 0; i--) {
			ChatLinkInfo *pInfo = pData->eaLinkInfos[i];
			if ((S32) strlen(pchText) < pInfo->iStart + pInfo->iLength) {
				// Text is too short, this must be an orphan
				eaPush(&eaOrphanedLinks, pInfo);
				eaRemove(&pData->eaLinkInfos, i);
			} else if (strncmp(pchText + pInfo->iStart, pInfo->pLink->pchText, pInfo->iLength) != 0) {
				// Didn't find an exact match, this must be an orphan
				eaPush(&eaOrphanedLinks, pInfo);
				eaRemove(&pData->eaLinkInfos, i);
			} else {
				// Found an exact match, this must be a good link
			}
		}

		// What's left in pData->eaLinkInfos are all good links.  Now
		// we need to analyze the orphans to see if we can find a new 
		// home for them.  If not, we'll need to free them (run Forest, run!)
		if (eaSize(&eaOrphanedLinks)) {
			while (eaSize(&eaOrphanedLinks)) {
				ChatLinkInfo *pInfo = eaPop(&eaOrphanedLinks);
				const char *pchIndex = strstr(pchText, pInfo->pLink->pchText);

				while (pchIndex) {
					//TODO check whether location is good										
					S32 iInsertAt = ClientChat_GetGoodLinkInsertionPoint(pData, pchIndex-pchText, (U32) pInfo->iLength);
					if (iInsertAt >= 0) {
						// We found a home!  Reinsert the orphan and fix up its
						// start location.
						pInfo->iStart = pchIndex - pchText;
						eaInsert(&pData->eaLinkInfos, pInfo, iInsertAt);
						break;
					}
					pchIndex = strstr(pchIndex + pInfo->iLength, pInfo->pLink->pchText);
				}

				
				if (!pchIndex) {
					// We didn't find a good position, so delete the orphan
					StructDestroy(parse_ChatLinkInfo, pInfo); // I cast thee into the dark pits of Doom!
				}
			}
		}
		
		eaDestroy(&eaOrphanedLinks);
	}
}

static void ClientChat_SetCurrentChannelByType(ChatLogEntryType eType)
{
	switch (eType)
	{
	case kChatLogEntryType_Zone:
		ClientChat_SetCurrentChannelByName(CHAT_ZONE_SHORTCUT);
	xcase kChatLogEntryType_Guild:
		ClientChat_SetCurrentChannelByName(CHAT_GUILD_SHORTCUT);
	xcase kChatLogEntryType_Officer:
		ClientChat_SetCurrentChannelByName(CHAT_GUILD_OFFICER_SHORTCUT);
	xcase kChatLogEntryType_Local:
		ClientChat_SetCurrentChannelByName(LOCAL_CHANNEL_NAME);
	xcase kChatLogEntryType_Team:
		ClientChat_SetCurrentChannelByName(CHAT_TEAM_SHORTCUT);
	xdefault:
		// nothing else does anything
		break;
	}
}

static void ClientChat_SendMessageInternal(SA_PARAM_OP_VALID const ChatUserInfo *pTo, ChatLogEntryType eType, SA_PARAM_OP_VALID const char *pchChannel, SA_PARAM_OP_VALID const char *pchText)
{
	ChatMessage *pMsg;

	if (!pchText || !*pchText) {
		return;
	}

	ClientChat_FixupChatData(s_pCurrentChatData, pchText);
	pMsg = ChatCommon_CreateMsg(NULL, pTo, eType, pchChannel, pchText, s_pCurrentChatData);
	if (pMsg)
	{
		Entity *pEnt = entActiveOrSelectedPlayer();
		bool bChangeCurrentChannel = false;
		if (pEnt)
		{
			const char *pchPlayerName = entGetLangName(pEnt, entGetLanguage(pEnt));
			Entity *pTarget =  entity_GetTarget(pEnt);

			if (pchPlayerName)
			{
				estrReplaceOccurrences_CaseInsensitive(&pMsg->pchText, "$playername", pchPlayerName);
				estrReplaceOccurrences_CaseInsensitive(&pMsg->pchText, "$player", pchPlayerName);
			}
			if (pTarget)
			{
				const char *pchTargetName = entGetLangName(pTarget, entGetLanguage(pEnt));
				if (pchTargetName)
					estrReplaceOccurrences_CaseInsensitive(&pMsg->pchText, "$target", pchTargetName);
			}
		}
#ifdef USE_CHATRELAY
		switch (pMsg->eType)
		{
		case kChatLogEntryType_Zone:
		case kChatLogEntryType_Guild:
		case kChatLogEntryType_Officer:
		case kChatLogEntryType_Match:
		case kChatLogEntryType_Local:
			// These also go through the ChatRelay for preliminary validation before being sent to the GameServer
			GServerCmd_crSendMessage(GLOBALTYPE_CHATRELAY, pMsg);
			bChangeCurrentChannel = true;
		xcase kChatLogEntryType_Global:
		xcase kChatLogEntryType_Channel:
		case kChatLogEntryType_Private:
		case kChatLogEntryType_Team:
		case kChatLogEntryType_TeamUp:
			GServerCmd_crSendMessage(GLOBALTYPE_CHATRELAY, pMsg);
			bChangeCurrentChannel = true;
		xdefault:
			devassert(0);
		}

		if (bChangeCurrentChannel)
		{
			if (pMsg->eType == kChatLogEntryType_Channel)
				ClientChat_SetCurrentChannelByName(pMsg->pchChannel);
			else
				ClientChat_SetCurrentChannelByType(pMsg->eType);
		}
#else
		ServerCmd_cmdServerChat_SendMessage(pMsg);
#endif
	}

	StructDestroy(parse_ChatMessage, pMsg);
}

static void ClientChat_SendMessageChannel(ChatConfigInputChannel *pInputChannel, SA_PARAM_OP_VALID const char *msg, SA_PARAM_OP_VALID ChatData *pData)
{
	if (!msg) {
		return;
	}

	CommandHistory_AddNewMessage(msg, pData);
	
	// The following is an ugly hack to pass data through to client chat commands.
	// It assumes that locally executed chat commands executed via the call to globCmdParse()
	// will be in the same stack as this function() and that commands are 
	// running single-threaded.
	s_pCurrentChatData = pData; 

	if (msg[0] == '/') {
		globCmdParseAndStripWhitespace(CMD_CONTEXT_HOWCALLED_CHATWINDOW, msg + 1);
	} else if (msg[0] == ';') {
		// Regardless of what the /em command does, ; should do exactly the same thing, so
		// rather than call a server command just call the correct alias.
		globCmdParsefAndStripWhitespace(CMD_CONTEXT_HOWCALLED_CHATWINDOW, "em %s", msg + 1);
	} else {
		if (!pInputChannel)
			pInputChannel = ClientChatConfig_GetCurrentInputChannel();
		if (stricmp_safe(pInputChannel->pchSystemName, LOCAL_CHANNEL_NAME)) {
			ClientChat_SendChannelChat(NULL, pInputChannel->pchSystemName, msg);
		} else {
			ClientChat_SendLocalChat(NULL, msg);
		}
	}

	// Clear the current chat data
	s_pCurrentChatData = NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_Send);
void ClientChat_SendMessage(SA_PARAM_OP_VALID const char *msg, SA_PARAM_OP_VALID ChatData *pData)
{
	ClientChat_SendMessageChannel(NULL, msg, pData);
}


void ClientChat_SendPrivateMessage(SA_PARAM_OP_VALID const char *pchText, SA_PARAM_NN_STR const char *handle)
{
	ChatUserInfo to = { 0 };
	to.pchHandle = (char*)handle;
	ClientChat_SendMessageInternal( &to, kChatLogEntryType_Private, PRIVATE_CHANNEL_NAME, pchText );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_SendFromLobbyTeamChat);
void ClientChat_SendMessageFromLobbyTeamChat(SA_PARAM_OP_VALID const char *msg, SA_PARAM_OP_VALID ChatData *pData)
{
	if (msg == NULL)
	{
		return;
	}

	CommandHistory_AddNewMessage(msg, pData);

	// The following is an ugly hack to pass data through to client chat commands.
	// It assumes that locally executed chat commands executed via the call to globCmdParse()
	// will be in the same stack as this function() and that commands are 
	// running single-threaded.
	s_pCurrentChatData = pData; 

	if (msg[0] == '/') 
	{
		globCmdParseAndStripWhitespace(CMD_CONTEXT_HOWCALLED_CHATWINDOW, msg + 1);
	} 
	else if (msg[0] == ';') 
	{
		// Regardless of what the /em command does, ; should do exactly the same thing, so
		// rather than call a server command just call the correct alias.
		globCmdParsefAndStripWhitespace(CMD_CONTEXT_HOWCALLED_CHATWINDOW, "em %s", msg + 1);
	}
	else 
	{
		ClientChat_SendMessageInternal(NULL, kChatLogEntryType_Team, NULL, msg);
	}

	// Clear the current chat data
	s_pCurrentChatData = NULL;	
}

bool ClientChat_AttemptChannelMessage(const char *cmdString, char **estrCmdName)
{
	static const char *sDelims = " \n,\t";
	char *channelName = (char*) cmdString;
	char *channelNameEnd, *text;
	char temp;

	// Get the channel name, make sure there's stuff in it (and after it)
	while (*channelName && strchr(sDelims, *channelName))
	{
		channelName++;
	}
	if (!*channelName)
		return false;
	channelNameEnd = channelName;
	while (*channelNameEnd && !strchr(sDelims, *channelNameEnd))
	{
		channelNameEnd++;
	}
	temp = *channelNameEnd;
	*channelNameEnd = 0;
	estrCopy2(estrCmdName, channelName);
	if (!temp)
		return false;

	// Get the message, make sure it's not just whitespace
	text = channelNameEnd+1;
	while (*text && strchr(sDelims, *text))
	{
		text++;
	}
	if (!*text || !ClientChat_GetCachedChannelInfo(channelName))
	{
		*channelNameEnd = temp;
		return false;
	}

	ClientChat_SendChannelChat(entActivePlayerPtr(), channelName, text);
	*channelNameEnd = temp;
	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_SendChannel);
void ClientChat_SendMessageZone(SA_PARAM_OP_VALID const char *pchTitle, SA_PARAM_OP_VALID const char *msg, SA_PARAM_OP_VALID ChatData *pData)
{
	if (pchTitle)
	{
		ChatConfigInputChannel CurrentInputChannel = {0};
		StructInit(parse_ChatConfigInputChannel, &CurrentInputChannel);
		ClientChat_FillInputChannelInput(&CurrentInputChannel, pchTitle, true);
		ClientChat_SendMessageChannel(&CurrentInputChannel, msg, pData);
		StructDeInit(parse_ChatConfigInputChannel, &CurrentInputChannel);
	}
}

// Send a chat message using the channel information from the given window
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatWindowSend);
void exprClientChatSendMessage(SA_PARAM_OP_VALID ChatTabGroupConfig *pWindowSettings, const char *pchTitle, const char *msg, SA_PARAM_OP_VALID ChatData *pData)
{
	S32 i = ChatCommon_FindTabConfigIndex(pWindowSettings, pchTitle);
	ChatTabConfig *pTab = (i >= 0 && pWindowSettings ? eaGet(&pWindowSettings->eaChatTabConfigs, i) : NULL);
	if (pTab)
	{
		ChatConfigInputChannel CurrentInputChannel = {0};
		const char *pchChannel = pTab->pchDefaultChannel;
		if (!pchChannel || !*pchChannel) {
			pchChannel = LOCAL_CHANNEL_NAME;
		}

		StructInit(parse_ChatConfigInputChannel, &CurrentInputChannel);
		ClientChat_FillInputChannelInput(&CurrentInputChannel, pchChannel, true);
		ClientChat_SendMessageChannel(&CurrentInputChannel, msg, pData);
		StructDeInit(parse_ChatConfigInputChannel, &CurrentInputChannel);
	}
}

void ClientChat_SendLocalChat(Entity *pEnt, const char *pchText) {
	ClientChat_SendMessageInternal(NULL, kChatLogEntryType_Local, LOCAL_CHANNEL_NAME, pchText);
}

void ClientChat_SendChannelChat(Entity *pEnt, const char *channel_name, const char *pchText) {
	ChatLogEntryType eType = ChatCommon_GetChannelMessageType(channel_name);
	ClientChat_SendMessageInternal(NULL, eType, channel_name, pchText);
}

void ClientChat_SendZoneChat(Entity *pEnt, const char *pchText) {
	// Channel will be filled in by the server
	ClientChat_SendMessageInternal(NULL, kChatLogEntryType_Zone, NULL, pchText);
}

void ClientChat_SendGuildChat(Entity *pEnt, const char *pchText) {
	// Channel will be filled in by the server
	ClientChat_SendMessageInternal(NULL, kChatLogEntryType_Guild, NULL, pchText);
}

void ClientChat_SendOfficerChat(Entity *pEnt, const char *pchText) {
	// Channel will be filled in by the server
	ClientChat_SendMessageInternal(NULL, kChatLogEntryType_Officer, NULL, pchText);
}

void ClientChat_SendTeamChat(Entity *pEnt, const char *pchText) {
	// Channel will be filled in by the server
	ClientChat_SendMessageInternal(NULL, kChatLogEntryType_Team, NULL, pchText);
}

//extern void ClientChat_SendHelpChat(Entity *pEnt, const char *pchText) {
//	// Channel will be filled in by the server
//	ClientChat_SendMessageInternal(NULL, kChatLogEntryType_Help, NULL, pchText);
//}
//
//extern void ClientChat_SendLifetimeChat(Entity *pEnt, const char *pchText) {
//	// Channel will be filled in by the server
//	ClientChat_SendMessageInternal(NULL, kChatLogEntryType_Lifetime, NULL, pchText);
//}

static const char* correctTellUsage(void) {
	return TranslateMessageKeyDefault(
		"Chat_CorrectTellUsage", "[UNTRANSLATED] Usage:\n /tell [or /t] Player Name, message");
}

void ClientChat_SendTellChatErrorFunc(CmdContext *pCmdContext) {
	estrConcatf(pCmdContext->output_msg, "%s", correctTellUsage());
}

void ClientChat_ParseTellCommand(char* esFullCmd, OUT char **ppchSendName, OUT char **ppchSendMessage)
{
	char *pchSendName = esFullCmd;
	char *pchSendMessage = NULL;

	if( !esFullCmd )
		return;

	if (esFullCmd[0] == '"') {
		pchSendName = esFullCmd+1;
		if (pchSendName[0]) {
			pchSendMessage = strchr(pchSendName, '"');

			//If I found the closing " before the end...
			if(pchSendMessage != NULL) {
				//Terminate the name
				*pchSendMessage++ = 0;

				if(pchSendMessage[0] == ',') {
					pchSendMessage++;
				}
			}
		} else {
			pchSendMessage = NULL;
		}
	} else {
		//Search through the message for a , in the first MAX_NAME_LEN characters
		pchSendMessage = strchr(esFullCmd, ',');

		//If there is no comma, then just use the first word
		if (pchSendMessage == NULL) {
			pchSendMessage = strchr(esFullCmd, ' ');
		}

		if (pchSendMessage != NULL) {
			//terminate the "name"
			*pchSendMessage++ = 0;
		}
	}

	if (pchSendName) {
		pchSendName = (char *)removeLeadingWhiteSpaces(pchSendName);
		removeTrailingWhiteSpaces(pchSendName);
	}

	if (pchSendMessage) {
		pchSendMessage = (char *)removeLeadingWhiteSpaces(pchSendMessage);
		removeTrailingWhiteSpaces(pchSendMessage);
	}

	// Copy to the return pointers
	if( ppchSendName )
		*ppchSendName = pchSendName;

	if( ppchSendMessage )
		*ppchSendMessage = pchSendMessage;
}

void ClientChat_SendTellChat(CmdContext *pCmdContext, Entity *pEnt, const char *pchFullText) {
	char *esFullCmd = StructAllocString(pchFullText);
	char *pchSendName = NULL;
	char *pchSendMessage = NULL;
	ClientChat_ParseTellCommand(esFullCmd, &pchSendName, &pchSendMessage);

	if (pchSendName && *pchSendName && pchSendMessage && *pchSendMessage) {
		ChatUserInfo *pTo = ChatCommon_CreateUserInfoFromNameOrHandle(pchSendName);
		ClientChat_SendMessageInternal(pTo, kChatLogEntryType_Private, PRIVATE_CHANNEL_NAME, pchSendMessage);
		StructDestroy(parse_ChatUserInfo, pTo);
	} else {
		if(!estrLength(pCmdContext->output_msg)) {
			estrPrintf(pCmdContext->output_msg, "%s\n",
				TranslateMessageKeyDefault("Chat_TellError", "[UNTRANSLATED]Tell is formatted incorrectly"));
		}
		estrConcatf(pCmdContext->output_msg, "%s", correctTellUsage());
	}

	free(esFullCmd);
}

void ClientChat_SendReplyChat(CmdContext *pCmdContext, Entity *pEnt, const char *pchFullText)
{
	char *pchCommand = NULL;
	const char *pchLastTellSender = ClientChat_LastTellSender();

	if (pchLastTellSender) {
		estrPrintf(&pchCommand, "%s, %s", pchLastTellSender, pchFullText);
		ClientChat_SendTellChat(pCmdContext, pEnt, pchCommand);
	} else {
		estrPrintf(pCmdContext->output_msg, "%s\n",
			TranslateMessageKeyDefault("Chat_ReplyError", "[UNTRANSLATED]No Tells received"));
	}

	estrDestroy(&pchCommand);
}

void ClientChat_SendReplyLastChat(CmdContext *pCmdContext, Entity *pEnt, const char *pchFullText)
{
	char *pchCommand = NULL;
	const char *pchLastTellSender = ClientChat_LastTellCorrespondent();

	if (pchLastTellSender) {
		estrPrintf(&pchCommand, "%s, %s", pchLastTellSender, pchFullText);
		ClientChat_SendTellChat(pCmdContext, pEnt, pchCommand);
	} else {
		estrPrintf(pCmdContext->output_msg, "%s\n",
			TranslateMessageKeyDefault("Chat_ReplyLastError", "[UNTRANSLATED]No Tells sent or received"));
	}

	estrDestroy(&pchCommand);
}

// Get the display name of the current input channel.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_GetCurrentChannel);
const char *ClientChat_GetCurrentChannel(void)
{
	ChatConfigInputChannel *pInputChannel = ClientChatConfig_GetCurrentInputChannel();
	return pInputChannel->pchDisplayName;
}

// Get the system name of the current input channel.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_GetCurrentChannelSystemName);
const char *ClientChat_GetCurrentChannelSystemName(void)
{
	ChatConfigInputChannel *pInputChannel = ClientChatConfig_GetCurrentInputChannel();
	return pInputChannel->pchSystemName;
}

// Set the color of the specified channel.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_SetChannelColor);
void ClientChat_SetChannelColor(const char *pchSystemName, bool bIsChannel, U32 iColor)
{
	if (ClientChatConfig_UsingEntityConfig())
		ServerCmd_cmdServerChatConfig_SetChannelColor(pchSystemName, bIsChannel, iColor);
	else
		GServerCmd_crChatConfig_SetChannelColor(GLOBALTYPE_CHATRELAY, pchSystemName, bIsChannel, iColor);
}

// Get the color of the specified input channel.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_GetChannelColor);
U32 ClientChat_GetChannelColor(const char* pchSystemName, const char *pchChannel)
{
	Entity *pEnt = entActivePlayerPtr();
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEnt);
	ChatLogEntryType eType = StaticDefineIntGetInt(ChatLogEntryTypeEnum, pchSystemName);
	return ChatCommon_GetChatColor(pConfig, eType, pchChannel, ChatCommon_GetChatConfigSourceForEntity(pEnt));
}

// Get the color of the current input channel.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_GetCurrentChannelColor);
U32 ClientChat_GetCurrentChannelColor(void)
{
	ChatConfigInputChannel *pInputChannel = ClientChatConfig_GetCurrentInputChannel();
	return pInputChannel->iColor;
}

// Get the color of the current input channel in stringified hex.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_GetCurrentChannelColorHex);
const char *ClientChat_GetCurrentChannelColorHex(void)
{
	ChatConfigInputChannel *pInputChannel = ClientChatConfig_GetCurrentInputChannel();
	return pInputChannel->pchColorHex;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_SetCurrentChannelByName);
void ClientChat_SetCurrentChannelByName(const char *pchName)
{
	const char *pchSystemName = ClientChat_GetSubscribedChannelSystemName(pchName);
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(entActivePlayerPtr());
	ChatTabGroupConfig *pTabGroup = NULL;
	if (pConfig)
		pTabGroup = ChatCommon_GetTabGroupConfig(pConfig, pConfig->pchCurrentTabGroup);

	if (!pchSystemName || !pTabGroup || pTabGroup->iCurrentTab < 0)
		return;	// Couldn't find it.
	ClientChatConfig_SetCurrentChannelForTab(pTabGroup->pchName, pTabGroup->iCurrentTab, pchSystemName);
}

// ------------------------------------------------------------
// Channel Creation, Joining and Leaving

void ClientChat_AddChannelSummary(ChatChannelInfo *pInfo, bool *bRefreshedJoinDetail, bool *bRefreshedAdminDetail)
{
	ChatChannelInfo *pCopy = StructClone(parse_ChatChannelInfo, pInfo);
	if (pCopy)
	{
		FillChannelStatus(pCopy, false);
		FillAccessLevel(&pCopy->pchUserLevel, pCopy->eUserLevel);
		FillChannelMemberStatuses(pCopy);
		eaQSort(pCopy->ppMembers, ChannelMemberComparator); // Sort the members, if present

		if (s_pJoinChannelDetail && stricmp_safe(s_pJoinChannelDetail->pName, pCopy->pName) == 0) {
			ClientChat_RefreshJoinChannelDetail(pCopy->pName);
			if (bRefreshedJoinDetail)
				*bRefreshedJoinDetail = true;
		}
		else
			StructDestroy(parse_ChatChannelInfo, pCopy);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Channel_Join);
void ClientChat_JoinChannel(const char *channel_name)
{
	//todo: consider pushing the conditional logic to the chat server
	const char *pchSystemName = ClientChat_GetSubscribedChannelSystemName(channel_name);
	if (!pchSystemName || !ChatCommon_IsBuiltInChannel(pchSystemName))
	{
#ifdef USE_CHATRELAY
		GServerCmd_crJoinChannel(GLOBALTYPE_CHATRELAY, channel_name);
#else
		ServerCmd_cmdServerChat_JoinChannel(channel_name);
#endif
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Channel_JoinOrCreate);
void ClientChat_JoinOrCreateChannel(const char *pchChannel) {
	//todo: consider pushing the conditional logic to the chat server
	const char *pchSystemName = ClientChat_GetSubscribedChannelSystemName(pchChannel);
	if (!pchSystemName || !ChatCommon_IsBuiltInChannel(pchSystemName))
	{
#ifdef USE_CHATRELAY
		GServerCmd_crJoinOrCreateChannel(GLOBALTYPE_CHATRELAY, pchChannel);
#else
		ServerCmd_cmdServerChat_JoinOrCreateChannel(pchChannel);
#endif
		ClientChat_RefreshJoinChannelDetail(pchChannel);
	}
}

void ClientChat_AddChannelToDropdown(ChatChannelInfo *channelInfo, bool bSetAsCurrentChannel)
{
	bool bFoundChannel = false;
	int i;

	if (s_eaSubscribedChannels) {
		// Check to see if we're already tracking this channel
		for (i=0; i < eaSize(&s_eaSubscribedChannels); i++) {
			ChatConfigInputChannel *pSubscribedChannel = eaGet(&s_eaSubscribedChannels, i);
			if (!stricmp_safe(pSubscribedChannel->pchSystemName, channelInfo->pName)) {
				bFoundChannel = true;
				break;
			}
		}
	}

	if (!bFoundChannel) {
		ChatConfigInputChannel *pSubscribedChannel = StructCreate(parse_ChatConfigInputChannel);
		estrCreate(&pSubscribedChannel->pchDisplayName);
		estrCreate(&pSubscribedChannel->pchSystemName);
		ClientChat_FillInputChannelInput(pSubscribedChannel, channelInfo->pName, true);
		eaPush(&s_eaSubscribedChannels, pSubscribedChannel);
	}

	if (bSetAsCurrentChannel) {
		ClientChat_SetCurrentChannelByName(channelInfo->pName);
	}
}

void ClientChat_RemoveChannelFromDropdown(const char *pchSystemName)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(entActivePlayerPtr());
	int i;

	if (s_eaSubscribedChannels) {
		for (i=0; i < eaSize(&s_eaSubscribedChannels); i++) {
			ChatConfigInputChannel *pSubscribedChannel = eaGet(&s_eaSubscribedChannels, i);
			if (!stricmp_safe(pSubscribedChannel->pchSystemName, pchSystemName)) {
				StructDestroy(parse_ChatConfigInputChannel, eaRemove(&s_eaSubscribedChannels, i));
				break; // Given that we can only subscribe unique channels, 
				// we don't need to continue looking after we've found the right one to remove
			}
		}
	}
	if (s_pJoinChannelDetail && stricmp_safe(s_pJoinChannelDetail->pName, pchSystemName) == 0)
	{
		StructDestroy(parse_ChatChannelInfo, s_pJoinChannelDetail);
		s_pJoinChannelDetail = NULL;
	}
	if (pConfig && !stricmp_safe(pConfig->pchCurrentInputChannel, pchSystemName)) {
		ClientChat_SetCurrentChannelByName(LOCAL_CHANNEL_NAME);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(channel_leave);
void ClientChat_LeaveChannel(const char *channel_name)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(entActivePlayerPtr());
	const char *pchSystemName = ClientChat_GetSubscribedChannelSystemName(channel_name);

#ifdef USE_CHATRELAY
	GServerCmd_crLeaveChannel(GLOBALTYPE_CHATRELAY, pchSystemName ? pchSystemName : channel_name);
#else
	ServerCmd_cmdServerChat_LeaveChannel(pchSystemName ? pchSystemName : channel_name);
#endif

	// If the subscribed to channel is the current input channel, then reset it
	if (pConfig && !stricmp_safe(pConfig->pchCurrentInputChannel, pchSystemName)) {
		ClientChat_SetCurrentChannelByName(LOCAL_CHANNEL_NAME);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(channel_destroy);
void ClientChat_DestroyChannel(const char *pchChannel) {
#ifdef USE_CHATRELAY
	gclClientChat_DestroyChannel((char*) pchChannel);
#else
	ServerCmd_channel_destroy(pchChannel);
#endif
}

AUTO_EXPR_FUNC(UIGen)  ACMD_NAME(channel_description);
void ClientChat_ChannelDescription(const char *pchChannel, const char *pchDescription) {
#ifdef USE_CHATRELAY
	gclClientChat_SetChannelDescription((char*) pchChannel, (char*) pchDescription);
#else
	ServerCmd_channel_description(pchChannel, pchDescription);
#endif
}

AUTO_EXPR_FUNC(UIGen)  ACMD_NAME(channel_motd);
void ClientChat_ChannelMotd(const char *pchChannel, const char *pchMotd) {
#ifdef USE_CHATRELAY
	gclClientChat_SetChannelMotd((char*) pchChannel, (char*) pchMotd);
#else
	ServerCmd_motd(pchChannel, pchMotd);
#endif
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(channel_invite);
void ClientChat_ChannelInvite(const char *pchChannel, const char *pchUser) {
#ifdef USE_CHATRELAY
	gclClientChat_ChannelInvite((char*) pchChannel, (char*) pchUser);
#else
	ServerCmd_cinvite(pchChannel, pchUser);
#endif
}

void ClientChat_GetChannelMembers(const char *pchChannel)
{
#ifdef USE_CHATRELAY
	const char *pchSystemName = ClientChat_GetSubscribedChannelSystemName(pchChannel);
	if (pchSystemName && !ChatCommon_IsBuiltInChannel(pchSystemName))
	{
		GServerCmd_crRequestChannelMemberList(GLOBALTYPE_CHATRELAY, pchChannel);
	}
#endif
}

//////////////////////////
// Channels

// Create a Link from an Item pointer.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_CreateItemLinkEx);
const char *ClientChat_CreateItemLink(SA_PARAM_OP_VALID Item *pItem)
{
	static char *s_pchEncodedLink = NULL;
	Entity *pEntity = entActivePlayerPtr();
	ItemDef *pItemDef;
	ChatLink *pLink = NULL;

	estrClear(&s_pchEncodedLink);

	if (!pItem) {
		return s_pchEncodedLink;
	}
	
	// Dev Note: It turns out if we only send the ItemDef, we really
	// don't get all the data we want on the receiving end, so we
	// almost always want to send the entire item.  We specifically don't
	// get the power descriptions of the item when just sending the item
	// def.  (this might be a champions' specific behavior based on how
	// items are defined).
	if (true) { // pItem->bAlgo) {
		const char *pchTranslatedName = item_GetName(pItem,pEntity);
		char *pchEncodedItem = NULL;
		// If the translated name is NULL, then we probably don't have the ItemDef yet. Exit early.
		if (!pchTranslatedName) {
			return s_pchEncodedLink;
		}
		// Encode the entire item (fugly!)
		ParserWriteText(&pchEncodedItem, parse_Item, pItem, 
			WRITETEXTFLAG_USEHTMLACCESSLEVEL |WRITETEXTFLAG_DONTWRITEENDTOKENIFNOTHINGELSEWRITTEN, 
			0, 0);
		
		pLink = ChatData_CreateLink(kChatLinkType_Item, pchEncodedItem, pchTranslatedName);
	} else {
		const char *pchTranslatedName = NULL;

		// Encode the Def of the item
		pItemDef = GET_REF(pItem->hItem);
		if (!pItemDef) {
			return s_pchEncodedLink;
		}

		// Note: Display text for links is sent using locale of sender
		//       This should mostly be OK since players will likely 
		//       mostly communicate with people that speak the same
		//       language.  Regardless, when the user uses a link
		//       (either via pop up or dialog), localization of the
		//       linked thing should be able to happen at that time.
		pchTranslatedName = TranslateDisplayMessage(pItemDef->displayNameMsg);

		pLink = ChatData_CreateLink(kChatLinkType_ItemDef, pItemDef->pchName, pchTranslatedName);
	}

	if (pLink) {
		ChatData_EncodeLink(&s_pchEncodedLink, pLink);
		StructDestroy(parse_ChatLink, pLink);
	}

	return s_pchEncodedLink;
}

// Create a Link from an inventory slot key.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_CreateItemLink, Chat_CreateInventorySlotLink);
const char *ClientChat_CreateInventorySlotLink(SA_PARAM_OP_VALID const char *pchInventorySlotKey)
{
	UIInventoryKey Key = {0};
	if (gclInventoryParseKey(pchInventorySlotKey, &Key) && Key.pSlot)
		return ClientChat_CreateItemLink(Key.pSlot->pItem);
	return "";
}

const char *CreatePowerDefLinkHelper(SA_PARAM_OP_VALID PowerDef *pPowerDef)
{
	static char *s_pchEncodedLink = NULL;
	const char *pchTranslatedMsg = NULL;
	ChatLink *pLink = NULL;

	estrClear(&s_pchEncodedLink);

	if (!pPowerDef) {
		return s_pchEncodedLink;	
	}

	// Note: Display text for links is sent using locale of sender
	//       This should mostly be OK since players will likely 
	//       mostly communicate with people that speak the same
	//       language.  Regardless, when the user uses a link
	//       (either via pop up or dialog), localization of the
	//       linked thing should be able to happen at that time.
	pchTranslatedMsg = TranslateDisplayMessage(pPowerDef->msgDisplayName);
	if (!pchTranslatedMsg) {
		pchTranslatedMsg = "";
	}
	pLink = ChatData_CreateLink(kChatLinkType_PowerDef, pPowerDef->pchName, pchTranslatedMsg);
	if (pLink) {
		ChatData_EncodeLink(&s_pchEncodedLink, pLink);
		StructDestroy(parse_ChatLink, pLink);
	}

	return s_pchEncodedLink;
}

// Create a Link from a PowerDef
extern DictionaryHandle g_hPowerDefDict;
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_CreatePowerDefLinkFromPowerDef);
const char *ClientChat_CreatePowerDefLinkFromPowerDef(SA_PARAM_OP_STR const char* pchPowerDefName)
{
	PowerDef *pPowerDef = RefSystem_ReferentFromString(g_hPowerDefDict, pchPowerDefName);
	return CreatePowerDefLinkHelper(pPowerDef);
}


// Create a Link from a Power ID
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_CreatePowerDefLink, Chat_CreatePowerDefLinkFromPowerID);
const char *ClientChat_CreatePowerDefLink(S32 iPowerId)
{
	Entity *pEntity = entActivePlayerPtr();
	Power *pPower = NULL;
	PowerDef *pPowerDef = NULL;

	if (pEntity) {
		// TODO(Theo) Figure out how to get powerdef without entity
		pPower = character_FindPowerByID(pEntity->pChar, iPowerId);
	}

	if (pPower) {
		pPowerDef = GET_REF(pPower->hDef);
	}

	return CreatePowerDefLinkHelper(pPowerDef);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_GetItemFromLink);
SA_RET_OP_VALID const Item *ClientChat_GetItemFromLink(ChatLink *pLink) {
	static Item item = {0};

	if (pLink && pLink->eType == kChatLinkType_Item) {
		StructReset(parse_Item, &item);
		if (ParserReadText(pLink->pchKey, parse_Item, &item, 0)) {
			return &item;
		}
	}

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_GetItemDefFromLink);
SA_RET_OP_VALID const ItemDef *ClientChat_GetItemDefFromLink(SA_PARAM_OP_VALID ChatLink *pLink) {
	static Item item = {0};

	if (pLink) {
		if (pLink->eType == kChatLinkType_Item) {
			StructReset(parse_Item, &item);
			if (ParserReadText(pLink->pchKey, parse_Item, &item, 0)) {
				return GET_REF(item.hItem);
			}
		} else if (pLink->eType == kChatLinkType_ItemDef) {
			return item_DefFromName(pLink->pchKey);
		}
	}

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_GetPowerDefFromLink);
SA_RET_OP_VALID const PowerDef *ClientChat_GetPowerDefFromLink(SA_PARAM_OP_VALID ChatLink *pLink) {
	if (pLink && pLink->eType == kChatLinkType_PowerDef) {
		return powerdef_Find(pLink->pchKey);
	}

	return NULL;
}

U32 ChatClient_GetChatLogItemLinkColor(SA_PARAM_OP_VALID ChatLink *pRenderingLink, SA_PARAM_OP_VALID Item **ppItemCache) {
	if (pRenderingLink) {
		const char *pchItemColor = NULL;
		
		if (pRenderingLink->eType == kChatLinkType_Item) {
			if (ppItemCache) {
				if (!*ppItemCache) {
					*ppItemCache = StructCreate(parse_Item);
					ParserReadText(pRenderingLink->pchKey, parse_Item, *ppItemCache, 0);
				}
				if (*ppItemCache) {
					pchItemColor = item_GetChatLinkColorNameByQuality(item_GetQuality(*ppItemCache));
				}
			} else {
				Item item;
				StructInit(parse_Item, &item);
				if (ParserReadText(pRenderingLink->pchKey, parse_Item, &item, 0)) {
					pchItemColor = item_GetChatLinkColorNameByQuality(item_GetQuality(&item));
				}
				StructDeInit(parse_Item, &item);
			}
		} else if (pRenderingLink->eType == kChatLinkType_ItemDef) {
			ItemDef *pItemDef = item_DefFromName(pRenderingLink->pchKey);
			if (pItemDef) {
				pchItemColor = item_GetChatLinkColorNameByQuality(pItemDef->Quality);
			}
		}

		if (pchItemColor) {
			U32 iColor = ui_StyleColorPaletteIndex(ColorRGBAFromName(pchItemColor));
			if (iColor) {
				return iColor;
			}
		}
	}

	return 0xffffffff; // Return white by default
}

U32 ChatClient_GetChatLogPlayerHandleLinkColor(SA_PARAM_OP_VALID ChatConfig *pConfig, ChatLogEntryType eType, const char *pchChannel) {

	if (pConfig && pchChannel) {
		return ChatCommon_GetChatColor(pConfig, eType, pchChannel, 
			ChatCommon_GetChatConfigSourceForEntity(entActivePlayerPtr()));
	}

	return 0xffffffff; // Return white by default
}

void ClientChat_ReceiveJoinChannelInfo(ChatChannelInfo *pInfo)
{
	if (s_pchJoinChannelRequest && pInfo && pInfo->pName && *pInfo->pName) {
		if (stricmp_safe(pInfo->pName, s_pchJoinChannelRequest) == 0) {
			if (s_pJoinChannelDetail)
				StructDestroy(parse_ChatChannelInfo, s_pJoinChannelDetail);
			s_pJoinChannelDetail = StructClone(parse_ChatChannelInfo, pInfo);
			//FillChannelStatus(s_pJoinChannelDetail, true);
			//FillChannelMemberStatuses(s_pJoinChannelDetail);
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_FindChatChannelInfoByName);
SA_ORET_OP_VALID ChatChannelInfo* ClientChat_FindChatChannelInfoByName(const char* pchChannel)
{
	ChatState *pChatState = ClientChat_GetChatState();
	ChatChannelInfo *pInfo = NULL;

	if (s_pJoinChannelDetail && stricmp(s_pJoinChannelDetail->pName, pchChannel) == 0)
	{
		pInfo = s_pJoinChannelDetail;
	}
	else if (pChatState && pChatState->eaChannels)
	{
		ChatChannelInfo **eaChannels = pChatState->eaChannels;
		EARRAY_FOREACH_BEGIN(eaChannels, i);
		{
			if (!stricmp_safe(eaChannels[i]->pName, pchChannel))
			{
				pInfo = eaChannels[i];
				break;
			}
		}
		EARRAY_FOREACH_END;
	}
	return pInfo;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_IsChannelJoinable);
bool ClientChat_IsChannelJoinable(const char *pchChannel) {
	ChatChannelInfo* pInfo = ClientChat_FindChatChannelInfoByName(pchChannel);
	if (pInfo)
	{
		// Check to see if player is already a member
		if (pInfo->bUserSubscribed)
			return false;
		// Check to see if it is reserved
		if (pInfo->eChannelAccess & CHATACCESS_RESERVED)
			return false;
		// Check whether or not anyone may join the channel (public)
		if (pInfo->eChannelAccess & CHATACCESS_JOIN)
			return true;
		// Check whether the player is invited (for private channels)
		if (pInfo->bUserInvited)
			return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Channel_RefreshSummary);
void ClientChat_RefreshChannelSummary(bool bSubscribed, bool bInvited, bool bReserved) {
	U32 iflags = 0;

	// We can't use the flags because the function on the other end (ClientChat_ReceiveUserChannelList)
	//  assumes that it receives all channels and has no means to know we aren't requesting all
	/*
	iflags |= bSubscribed ? USER_CHANNEL_SUBSCRIBED : 0;
	iflags |= bInvited ? USER_CHANNEL_INVITED : 0;
	iflags |= bReserved ? USER_CHANNEL_RESERVED : 0;
	*/
	iflags = USER_CHANNEL_SUBSCRIBED | USER_CHANNEL_INVITED | USER_CHANNEL_RESERVED;
#ifdef USE_CHATRELAY
	GServerCmd_crRequestUserChannelList(GLOBALTYPE_CHATRELAY, iflags);
#else
	ServerCmd_cmdServerChat_RequestUserChannelList(iflags);
#endif
}

static void RefreshChannelDetail(const char *pchChannel, char **ppchCurrentRequest, ChatChannelInfo **ppCurrentDetail) {
	if (stricmp_safe(*ppchCurrentRequest, pchChannel)) {
		if (*ppchCurrentRequest) {
			free(*ppchCurrentRequest);
		}

		*ppchCurrentRequest = strdup(pchChannel);

		// We only clear out the detail if a different channel is requested
		// Otherwise, we'll get flicker in the channel admin UI
		if (*ppCurrentDetail) {
			StructDestroy(parse_ChatChannelInfo, *ppCurrentDetail);
			*ppCurrentDetail = NULL;
		}
	}
#ifdef USE_CHATRELAY
	GServerCmd_crRequestJoinChannelInfo(GLOBALTYPE_CHATRELAY, pchChannel);
#else
	ServerCmd_cmdServerChat_RequestJoinChannelInfo(pchChannel);
#endif
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Channel_RefreshJoinDetail);
void ClientChat_RefreshJoinChannelDetail(const char *pchChannel)
{
	if (pchChannel && *pchChannel) // Don't request for empty channel names
		RefreshChannelDetail(pchChannel, &s_pchJoinChannelRequest, &s_pJoinChannelDetail);
	else if (s_pJoinChannelDetail)
	{
		StructDestroy(parse_ChatChannelInfo, s_pJoinChannelDetail);
		s_pJoinChannelDetail = NULL;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_FillChannelSummary);
void ClientChat_FillChannelSummary(UIGen *pGen)
{
	ChatState *pChatState = ClientChat_GetChatState();
	if (pChatState && pChatState->eaChannels)
		ui_GenSetList(pGen, &pChatState->eaChannels, parse_ChatChannelInfo);
	else
		ui_GenSetList(pGen, NULL, parse_ChatChannelInfo);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_GetJoinChannelDetail);
SA_RET_OP_VALID ChatChannelInfo *ClientChat_GetJoinChannelDetail(void) {
	return s_pJoinChannelDetail;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_SetChannelAccess);
void ClientChat_SetChannelAccess(const char* pchChannel, const char *pchAccess, bool bValue) {
	char *pchAccessString = NULL;

	estrPrintf(&pchAccessString, "%c%s", bValue ? '+' : '-', pchAccess);
#ifdef USE_CHATRELAY
	gclClientChat_SetChannelAccess(pchChannel, pchAccessString);
#else
	ServerCmd_caccess(pchChannel, pchAccessString);
#endif
	estrDestroy(&pchAccessString);
}

bool ClientChat_FilterProfanityForPlayer(void) {
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(entActivePlayerPtr());
	if (pConfig) {
		return pConfig->bProfanityFilter;
	}
	// We don't know the player's preference, so we have to assume they don't want profanity
	return true;
}

bool ClientChat_ShowChannelNamesForPlayer(void) {
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(entActivePlayerPtr());
	if (pConfig) {
		return pConfig->bShowChannelNames;
	}
	// We don't know the player's preference, so we have to assume they want channel names
	return true;
}

void ClientChat_GetMessageTypeDisplayNameByType(char **ppchDisplayName, ChatLogEntryType kType) {
	const char *pchKey = ChatCommon_GetMessageTypeDisplayNameKeyByType(kType);
	estrClear(ppchDisplayName);
	langFormatMessageKey(locGetLanguage(getCurrentLocale()), ppchDisplayName, pchKey, STRFMT_END);
}

void ClientChat_GetMessageTypeDisplayNameByMessage(char **ppchDisplayName, const ChatMessage *msg)
{
	const char *pchKey = ChatCommon_GetMessageTypeDisplayNameKeyByType(msg->eType);

	estrClear(ppchDisplayName);
	langFormatMessageKey(locGetLanguage(getCurrentLocale()), ppchDisplayName, pchKey, STRFMT_END);
	if (msg->eType == kChatLogEntryType_Zone && msg->iInstanceIndex)
	{
		int iInstanceNumber = gclGetCurrentInstanceIndex();
		if (msg->iInstanceIndex != iInstanceNumber )
		{
			if( !gConf.bHideZoneInstanceNumberInChat )
			{
				estrConcatf(ppchDisplayName, " #%d", msg->iInstanceIndex);
			}
		}
	}
}

///////////////////////////
// Utility Functions 

void FillAccessLevel(const char **ppchAccessLevel, ChannelUserLevel eUserLevel) {
	static const char **s_eaAccessLevels = NULL;
	int i;

	if (!s_eaAccessLevels) {
		char *pchMessageKey = NULL;
		const char *pchMessage;

		for (i=0; i < CHANUSER_COUNT; i++) {
			estrClear(&pchMessageKey);
			estrPrintf(&pchMessageKey, "ChatConfig_Channel_Status_User_%s", ChannelUserLevelEnum[i+1].key);
			pchMessage = strdup(TranslateMessageKeyDefault(pchMessageKey, pchMessageKey));
			eaPush(&s_eaAccessLevels, pchMessage);
		}
		estrClear(&pchMessageKey);
		estrPrintf(&pchMessageKey, "ChatConfig_Channel_Status_User_%s", ChannelUserLevelEnum[CHANUSER_GM+1].key);
		pchMessage = strdup(TranslateMessageKeyDefault(pchMessageKey, pchMessageKey));
		eaPush(&s_eaAccessLevels, pchMessage);

		eaPush(&s_eaAccessLevels, TranslateMessageKey("ChatConfig_Channel_Status_User_Unknown"));

		estrDestroy(&pchMessageKey);
	}

	if (eUserLevel >= 0 && eUserLevel < CHANUSER_COUNT) {
		*ppchAccessLevel = eaGet(&s_eaAccessLevels, eUserLevel);
	} else {
		*ppchAccessLevel = eaGet(&s_eaAccessLevels, CHANUSER_COUNT);
	}
}

void AppendStatus(char **ppchStatus, const char *state) {
	if (!state || !*state) {
		return;
	}

	if (ppchStatus && *ppchStatus && **ppchStatus) {
		estrAppend2(ppchStatus, TranslateMessageKey("ChatConfig_Channel_Status_Separator"));
	}

	if (ppchStatus) {
		estrAppend2(ppchStatus, state);
	}
}

void FillChannelMemberStatus(SA_PARAM_NN_VALID ChatChannelMember *pMember) {
	estrClear(&pMember->pchStatus);
	if (!pMember->pchStatus) {
		estrCopy2(&pMember->pchStatus, "");
	}

	/*
	if (pMember->eUserLevel != CHANUSER_USER) {
		if (pMember->eUserLevel == CHANUSER_OPERATOR) {
			AppendStatus(&pMember->pchStatus, TranslateMessageKey("ChatConfig_Channel_Status_User_Operator"));	
		} else if (pMember->eUserLevel == CHANUSER_ADMIN) {
			AppendStatus(&pMember->pchStatus, TranslateMessageKey("ChatConfig_Channel_Status_User_Admin"));	
		} else if (pMember->eUserLevel == CHANUSER_OWNER) {
			AppendStatus(&pMember->pchStatus, TranslateMessageKey("ChatConfig_Channel_Status_User_Owner"));	
		}
	}
	*/

	//if (pMember->bBanned) {
	//	AppendStatus(&pMember->pchStatus, TranslateMessageKey("ChatConfig_Channel_Status_User_Banned"));
	//}

	if (pMember->bInvited) {
		AppendStatus(&pMember->pchStatus, TranslateMessageKey("ChatConfig_Channel_Status_User_Invited"));
	}

	if (pMember->bSilenced) {
		AppendStatus(&pMember->pchStatus, TranslateMessageKey("ChatConfig_Channel_Status_User_Muted"));	
	}
}

void FillChannelMemberStatuses(ChatChannelInfo *pInfo) {
	if (pInfo && pInfo->ppMembers) {
		int i;

		// Fill in member info
		for (i = 0; i < eaSize(&pInfo->ppMembers); i++) {
			ChatChannelMember *pMember = eaGet(&pInfo->ppMembers, i);
			FillChannelMemberStatus(pMember);
			FillAccessLevel(&pMember->pchUserLevel, pMember->eUserLevel);
		}
	}
}

void FillChannelStatus(SA_PARAM_NN_VALID ChatChannelInfo *pInfo, bool bShowSubscribed) {
	estrClear(&pInfo->pchStatus);
	if (!pInfo->pchStatus) {
		estrCopy2(&pInfo->pchStatus, "");
	}

	if (pInfo->eUserLevel != CHANUSER_USER) {
		char *pchAccessLevel; // Points to pooled string - no need to free
		FillAccessLevel(&pchAccessLevel, pInfo->eUserLevel);
		AppendStatus(&pInfo->pchStatus, pchAccessLevel);
	}

	if (bShowSubscribed && pInfo->bUserSubscribed) {
		AppendStatus(&pInfo->pchStatus, TranslateMessageKey("ChatConfig_Channel_Status_User_Subscribed"));
	}

	if (pInfo->eChannelAccess & CHATACCESS_RESERVED) {
		AppendStatus(&pInfo->pchStatus, TranslateMessageKey("ChatConfig_Channel_Status_Channel_Reserved"));
	}

	if (pInfo->bUserInvited) {
		AppendStatus(&pInfo->pchStatus, TranslateMessageKey("ChatConfig_Channel_Status_User_Invited"));
	}

	if (!(pInfo->eChannelAccess & CHATACCESS_JOIN)) {
		AppendStatus(&pInfo->pchStatus, TranslateMessageKey("ChatConfig_Channel_Status_Channel_Private"));	
	}

	if (pInfo->bSilenced || !(pInfo->eChannelAccess & CHATACCESS_SEND)) {
		// Only show one of user/channel muted
		if (!(pInfo->eChannelAccess & CHATACCESS_SEND)) {
			AppendStatus(&pInfo->pchStatus, TranslateMessageKey("ChatConfig_Channel_Status_Channel_Muted"));
		} else if (pInfo->bUserInvited || pInfo->bUserSubscribed) {
			// Only show 'user muted' status if the user is a member of or invited to the channel
			AppendStatus(&pInfo->pchStatus, TranslateMessageKey("ChatConfig_Channel_Status_User_Muted"));
		}
	}
}

int ChannelMemberComparator(const ChatChannelMember **m1, const ChatChannelMember **m2) {
	if (m1 == m2) {
		return 0;
	}

	if (!m1 || !m2) {
		return m1 ? 1 : -1;
	}

	if (*m1 == *m2) {
		return 0;
	}

	if (!*m1 || !*m2) {
		return *m1 ? 1 : -1;
	}

	return stricmp_safe((*m1)->handle, (*m2)->handle);
}

int ChannelComparator(const ChatChannelInfo **c1, const ChatChannelInfo **c2) {
	// Null checks.  These shouldn't normally happen, but NULLs come after NON-NULLs
	if (c1 == c2) {
		return 0;
	}

	if (!c1 || !c2) {
		return c1 ? 1 : -1;
	}

	if (*c1 == *c2) {
		return 0;
	}

	if (!*c1 || !*c2) {
		return *c1 ? 1 : -1;
	}

	// Default sort order = Channel Invites, Subscribed Channels, Reserved Channels
	if ((*c1)->bUserInvited != (*c2)->bUserInvited) {
		return (*c1)->bUserInvited ? -1 : 1;
	}

	if ((*c1)->bUserSubscribed != (*c2)->bUserSubscribed) {
		return (*c1)->bUserSubscribed ? -1 : 1;
	}

	if (((*c1)->eChannelAccess & CHATACCESS_RESERVED) != ((*c2)->eChannelAccess & CHATACCESS_RESERVED)) {
		return ((*c1)->eChannelAccess & CHATACCESS_RESERVED) ? -1 : 1;
	}

	// All else being equal, just compare the names
	return stricmp_safe((*c1)->pName, (*c2)->pName);
}

void ClientChat_ResetSubscribedChannels(bool bBuiltIn)
{
	S32 i;
	for (i = eaSize(&s_eaSubscribedChannels) - 1; i >= 0; i--)
	{
		if (s_eaSubscribedChannels[i]->bIsBuiltIn && bBuiltIn || !s_eaSubscribedChannels[i]->bIsBuiltIn && !bBuiltIn)
			StructDestroy(parse_ChatConfigInputChannel, eaRemove(&s_eaSubscribedChannels, i));
	}
}

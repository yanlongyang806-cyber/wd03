/***************************************************************************
*     Copyright (c) 2005-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gclChat.h"
#include "chat/gclChatLog.h"
#include "gclChatConfig.h"
#include "gclChatAutoComplete.h"
#include "gclFriendsIgnore.h"
#include "Expression.h"
#include "chatCommonStructs.h"
#include "ChatData.h"
#include "Entity.h"
#include "Player.h"
#include "UIGen.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

SA_RET_NN_VALID UIGen *ui_GenExprFind(ExprContext *pContext, ACMD_EXPR_DICT("UIGen") const char *pchName);

static const char *s_DefaultChatTextEntryWindow = NULL;
static const char *s_CurrentChatTextEntryWindow = NULL;

//
// Chat Error Handlers
//

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(tell);
void errClientChat_SendTellChatErrorFunc(CmdContext *pCmdContext) {
	ClientChat_SendTellChatErrorFunc(pCmdContext);
}

//
// Chat Commands
//

AUTO_COMMAND ACMD_PRIVATE ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD;
void cmdChatLog_AddMessage(SA_PARAM_NN_VALID ChatMessage *pMsg) {
	ChatLog_AddMessage(pMsg, false);
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void cmdClientChat_MessageReceive(SA_PARAM_NN_VALID ChatMessage *pMsg) {
	ChatLog_AddMessage(pMsg, true);
}

AUTO_COMMAND ACMD_NAME(channel_join) ACMD_ACCESSLEVEL(0);
void cmdClientChat_JoinChannel(ACMD_SENTENCE channel_name) {
	ClientChat_JoinChannel(channel_name);
}

// Create and join a new channel
AUTO_COMMAND ACMD_NAME(create,channel_create) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Social);
void cmdClientChat_CreateChannel(ACMD_SENTENCE channel_name) {
	ClientChat_JoinOrCreateChannel(channel_name);
}

AUTO_COMMAND ACMD_NAME(channel_info) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Social);
void cmdClientChat_GetChannelInfo(ACMD_SENTENCE channel_name)
{
	ClientChat_GetChannelMembers(channel_name);
}

AUTO_COMMAND ACMD_NAME(channel_setcurrent) ACMD_ACCESSLEVEL(0);
void cmdClientChat_SetCurrentChannelByName(const char *pchName) {
	ClientChat_SetCurrentChannelByName(pchName);
}

AUTO_COMMAND ACMD_NAME(channel_leave) ACMD_ACCESSLEVEL(0);
void cmdClientChat_LeaveChannel(ACMD_SENTENCE channel_name) {
	ClientChat_LeaveChannel(channel_name);
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_IFDEF(GAMESERVER);
void cmdClientChat_ReceiveFriendRequest(ChatPlayerStruct *friendStruct) {
	ClientChat_ReceiveFriendRequest(friendStruct);
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_GENERICCLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0) ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(CHATRELAY);
void cmdClientChat_ReceiveJoinChannelInfo(ChatChannelInfo *pInfo) {
	ClientChat_ReceiveJoinChannelInfo(pInfo);
}

// Send chat to a channel
AUTO_COMMAND ACMD_NAME(ChannelSend) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat, Social);
void cmdClientChat_SendChannelChat(Entity *pEnt, const char *pchChannel, const ACMD_SENTENCE pchText) {
	ClientChat_SendChannelChat(pEnt, pchChannel, pchText);
}

// Private tell. Chat handles should be prefixed with an '@' character.
AUTO_COMMAND ACMD_NAME(tell) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat, Social);
void cmdClientChat_SendTellChat(CmdContext *pCmdContext, Entity *pEnt, const ACMD_SENTENCE full_cmd) {
	ClientChat_SendTellChat(pCmdContext, pEnt, full_cmd);
}

AUTO_COMMAND ACMD_NAME(reply) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat, Social);
void cmdClientChat_SendReplyChat(CmdContext *pCmdContext, Entity *pEnt, const ACMD_SENTENCE full_cmd) {
	ClientChat_SendReplyChat(pCmdContext, pEnt, full_cmd);
}

AUTO_COMMAND ACMD_NAME(replyLast) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat, Social);
void cmdClientChat_SendReplyLastChat(CmdContext *pCmdContext, Entity *pEnt, const ACMD_SENTENCE full_cmd) {
	ClientChat_SendReplyLastChat(pCmdContext, pEnt, full_cmd);
}

// Send chat to other players in your vicinity
AUTO_COMMAND ACMD_NAME(local) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat, Social);
void cmdClientChat_SendLocalChat(Entity *pEnt, const ACMD_SENTENCE msg) {
	ClientChat_SendLocalChat(pEnt, msg);
}

// Send chat to other players in the same zone.
AUTO_COMMAND ACMD_NAME(zone) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat, Social);
void cmdClientChat_SendZoneChat(Entity *pEnt, const ACMD_SENTENCE msg) {
	ClientChat_SendZoneChat(pEnt, msg);
}

// Send chat to other players in your guild.
AUTO_COMMAND ACMD_NAME(guild) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat, Guild, Social);
void cmdClientChat_SendGuildChat(Entity *pEnt, const ACMD_SENTENCE msg) {
	ClientChat_SendGuildChat(pEnt, msg);
}

// Send chat to other players in your guild.
AUTO_COMMAND ACMD_NAME(officer) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat, Guild, Social);
void cmdClientChat_SendOfficerChat(Entity *pEnt, const ACMD_SENTENCE msg) {
	ClientChat_SendOfficerChat(pEnt, msg);
}

AUTO_COMMAND  ACMD_NAME(team) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat, Team, Social);
void cmdClientChat_SendTeamChat(Entity *pEnt, ACMD_SENTENCE msg)
{
	ClientChat_SendTeamChat(pEnt, msg);
}

AUTO_COMMAND ACMD_NAME(Channel_RefreshSummary) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat, Social);
void cmdClientChat_RefreshChannelSummary(bool bSubscribed, bool bInvited, bool bReserved)
{
	ClientChat_RefreshChannelSummary(bSubscribed, bInvited, bReserved);
}

AUTO_COMMAND ACMD_NAME(Channel_RefreshJoinDetail) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat, Social);
void cmdClientChat_RefreshJoinChannelDetail(const char *pchChannel)
{
	ClientChat_RefreshJoinChannelDetail(pchChannel);
}

AUTO_COMMAND ACMD_NAME(Channel_RefreshAdminDetail) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat, Social);
void cmdClientChat_RefreshAdminChannelDetail(const char *pchChannel)
{
	ClientChat_RefreshJoinChannelDetail(pchChannel);
}

AUTO_COMMAND ACMD_NAME(who) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Social);
void gclChat_FindPlayersSimpleCmd(ACMD_SENTENCE pchFilter) {
	gclChat_FindPlayersSimple(NULL, pchFilter);
}

AUTO_COMMAND ACMD_NAME(findteams) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Social);
void gclChat_FindTeamsCmd(bool bFindTeams, bool bFindSolos) {
	gclChat_FindTeams(NULL, bFindTeams, bFindSolos);
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(who);
void gclChat_FindPlayersSimpleCmd_ErrorFunc(void) {
	gclChat_FindPlayersSimple_ErrorFunc();
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void gclChat_UpdateFoundPlayersCmd(bool bFromSimple, ChatPlayerList *pList) {
	gclChat_UpdateFoundPlayers(bFromSimple, pList);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void gclChat_UpdateFoundTeamsCmd(ChatTeamToJoinList *pList) {
	gclChat_UpdateFoundTeams(pList);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_GENERICCLIENTCMD ACMD_PRIVATE;
void gclChat_UpdateMapsCmd(ChatMapList *pList) {
	gclChat_UpdateMaps(pList);
}

// List all commands available for auto complete.
AUTO_COMMAND ACMD_NAME(DumpCommandsForAutoComplete) ACMD_CATEGORY(Debug);
void gclChat_DumpCommandListForAutoCompleteCmd() {
	gclChat_DumpCommandListForAutoComplete();
}

// List all available chat entry types.
AUTO_COMMAND ACMD_NAME(DumpChatTypes) ACMD_CATEGORY(Debug);
void ClientChat_DumpChatTypesCmd() {
	ClientChat_DumpChatTypes();
}

// Fill the chat log with debugging text of various types.
AUTO_COMMAND ACMD_NAME(FillChatLog) ACMD_CATEGORY(Debug);
void ClientChat_FillChatLogCmd() {
	ClientChat_FillChatLog();
}

// Echo debugging text to the chat log.
AUTO_COMMAND ACMD_NAME(Echo);
void ChatLog_EchoCmd(CmdContext *pContext, const ACMD_SENTENCE pchText) {
	ChatLog_Echo(NULL, pchText);
}

// Add Channel Summary Info to UI data
AUTO_COMMAND ACMD_CLIENTCMD ACMD_IFDEF(GAMESERVER) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void ClientChat_AddChannelInfo (ChatChannelInfo *pInfo, bool bSetAsCurrentChannel)
{
	ClientChat_AddChannelSummary(pInfo, NULL, NULL);
}

//
// Chat Expressions
//

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Chat_HistoryDown");
const char* exprClientChatHistoryDown(const char* current_text) {
	return ClientChat_HistoryDown(current_text, NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Chat_HistoryUp");
const char* exprClientChat_HistoryUp(const char *current_text) {
	return ClientChat_HistoryUp(current_text, NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_IsLookingForGroup);
bool gclChat_IsLookingForGroupExpr(SA_PARAM_OP_VALID Entity *pEnt)
{
	TeamMode mode = SAFE_MEMBER2(pEnt, pPlayer, eLFGMode);
	return mode == TeamMode_Open || mode == TeamMode_RequestOnly;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_LookingForGroupMode);
U32 gclChat_LookingForGroupMode(SA_PARAM_OP_VALID Entity *pEnt)
{
	return SAFE_MEMBER2(pEnt, pPlayer, eLFGMode);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_LookingForGroupDifficultyMode);
U32 gclChat_LookingForGroupDifficultyMode(SA_PARAM_OP_VALID Entity *pEnt)
{
	return SAFE_MEMBER2(pEnt, pPlayer, eLFGDifficultyMode);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_IsAway);
bool gclChat_IsAway(SA_PARAM_OP_VALID Entity *pEnt)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEnt);	
	if (pConfig && ((pConfig->status & USERSTATUS_AFK) || (pConfig->status & USERSTATUS_AUTOAFK)))
		return true;
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_IsDND);
bool gclChat_IsDND(SA_PARAM_OP_VALID Entity *pEnt)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEnt);	
	if (pConfig && (pConfig->status & USERSTATUS_DND))
		return true;
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_IsVisible);
bool gclChat_IsVisible(SA_PARAM_OP_VALID Entity *pEnt)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEnt);	
	if (pConfig && !(pConfig->status & USERSTATUS_HIDDEN) && !(pConfig->status & USERSTATUS_FRIENDSONLY))
		return true;
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_IsFriendsOnly);
bool gclChat_IsFriendsOnly(SA_PARAM_OP_VALID Entity *pEnt)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEnt);	
	if (pConfig && (pConfig->status & USERSTATUS_FRIENDSONLY))
		return true;
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_IsHidden);
bool gclChat_IsHidden(SA_PARAM_OP_VALID Entity *pEnt)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEnt);	
	if (pConfig && (pConfig->status & USERSTATUS_HIDDEN))
		return true;
	return false;
}

static bool s_bWhitelistChatEnabled = false;
static bool s_bWhitelistTellsEnabled = false;
static bool s_bWhitelistEmailEnabled = false;

AUTO_COMMAND ACMD_CLIENTCMD ACMD_GENERICCLIENTCMD ACMD_IFDEF(CHATRELAY) ACMD_IFDEF(GAMESERVER) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void cmdClientChat_ReceiveWhitelistInfo(bool bChatEnable, bool bTellsEnable, bool bEmailEnable) 
{
	s_bWhitelistChatEnabled = bChatEnable;
	s_bWhitelistTellsEnabled = bTellsEnable;
	s_bWhitelistEmailEnabled = bEmailEnable;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_IsWhitelistEnabledChat);
bool gclChat_IsWhitelistEnabledChat(void)
{
	return s_bWhitelistChatEnabled;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_IsWhitelistEnabledTells);
bool gclChat_IsWhitelistEnabledTells(void)
{
	return s_bWhitelistTellsEnabled;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_IsWhitelistEnabledEmail);
bool gclChat_IsWhitelistEnabledEmail(void)
{
	return s_bWhitelistEmailEnabled;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_IsWhitelistEnabledInvites);
bool gclChat_IsWhitelistEnabledInvites(SA_PARAM_OP_VALID Entity *pEnt)
{
	if (pEnt && pEnt->pPlayer)
	{
		return (pEnt->pPlayer->eWhitelistFlags & kPlayerWhitelistFlags_Invites) != 0;
	}
	return false;
}
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_IsWhitelistEnabledTrades);
bool gclChat_IsWhitelistEnabledTrades(SA_PARAM_OP_VALID Entity *pEnt)
{
	if (pEnt && pEnt->pPlayer)
	{
		return (pEnt->pPlayer->eWhitelistFlags & kPlayerWhitelistFlags_Trades) != 0;
	}
	return false;
}
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_IsWhitelistEnabledDuels, social_IsWhitelistEnabledPvPInvites);
bool gclChat_IsWhitelistEnabledPvPInvites(SA_PARAM_OP_VALID Entity *pEnt)
{
	if (pEnt && pEnt->pPlayer)
	{
		return (pEnt->pPlayer->eWhitelistFlags & kPlayerWhitelistFlags_PvPInvites) != 0;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen)  ACMD_NAME(Echo);
void ChatLog_EchoExpr(ExprContext *pContext, const char *pchText) {
	ChatLog_Echo(exprContextGetBlameFile(pContext), pchText);
}

AUTO_RUN;
void chat_InitUI(void)
{
	ui_GenInitStaticDefineVars(LFGDifficultyModeEnum, "LFGDifficultyMode_");
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(SetDefaultChatTextEntryWindow);
void gclChat_SetDefaultChatTextEntryWindow(SA_PARAM_NN_VALID UIGen *pUIGen)
{
	if (pUIGen)
	{
		s_DefaultChatTextEntryWindow = pUIGen->pchName;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(SetActiveChatTextEntryWindow);
void gclChat_SetActiveChatTextEntryWindow(SA_PARAM_NN_VALID UIGen *pUIGen)
{
	if (pUIGen)
	{
		s_CurrentChatTextEntryWindow = pUIGen->pchName;
	}
	else
	{
		s_CurrentChatTextEntryWindow = NULL;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ClearActiveChatTextEntryWindow);
void gclChat_ClearActiveChatTextEntryWindow()
{
	s_CurrentChatTextEntryWindow = NULL;
}

AUTO_COMMAND ACMD_NAME(SetFocusToCurrentChatTextEntryWindow) ACMD_ACCESSLEVEL(0);
void gclChat_SetFocusToCurrentChatTextEntryWindow()
{
	UIGen *pGen = NULL;

	if (s_CurrentChatTextEntryWindow)
	{
		pGen = ui_GenExprFind(NULL, s_CurrentChatTextEntryWindow);
	}
	else if (s_DefaultChatTextEntryWindow)
	{
		pGen = ui_GenExprFind(NULL, s_DefaultChatTextEntryWindow);
	}
	if (pGen)
		ui_GenSetFocus(pGen);
}

//
// Chat Config
//

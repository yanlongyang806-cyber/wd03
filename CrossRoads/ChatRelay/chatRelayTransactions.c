#include "chatRelayTransactions.h"
#include "Alerts.h"
#include "chatRelay.h"
#include "chatCommon.h"
#include "earray.h"
#include "estring.h"
#include "GameAccountData\GameAccountData.h"
#include "GameAccountDataCommon.h"
#include "NotifyEnum.h"
#include "objTransactions.h"
#include "StringUtil.h"
#include "textparser.h"

#include "AutoGen/chatCommon_h_ast.h"
#include "AutoGen/ChatServer_autogen_RemoteFuncs.h"
#include "AutoGen/GameAccountData_h_ast.h"
#include "AutoGen/GameClientLib_autogen_GenericClientCmdWrappers.h"

AUTO_COMMAND_REMOTE ACMD_IFDEF(OBJECTDB);
void crReceiveGameAccountData(const char *pGADString)
{
	GameAccountData gad = {0};
	StructInit(parse_GameAccountData, &gad);
	if (ParserReadText(pGADString, parse_GameAccountData, &gad, 0))
		GClientCmd_gclClientChat_UpdateGADChatConfig(gad.iAccountID, &gad.chatConfig);
	StructDeInit(parse_GameAccountData, &gad);
}

AUTO_TRANSACTION ATR_LOCKS(pData, ".chatConfig");
enumTransactionOutcome tr_crSetAccountChatConfig(ATR_ARGS, NOCONST(GameAccountData) *pData, NON_CONTAINER ChatConfig *pConfig)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	StructResetNoConst(parse_ChatConfig, &pData->chatConfig);
	StructCopyDeConst(parse_ChatConfig, pConfig, &pData->chatConfig, 0, 0, 0);
	return TRANSACTION_OUTCOME_SUCCESS;
}

////////////////////////////////
// Color Settings

AUTO_TRANSACTION ATR_LOCKS(pData, ".chatConfig.eaChannelColorDefs");
enumTransactionOutcome tr_crChatConfig_SetChannelColor(ATR_ARGS, NOCONST(GameAccountData) *pData, const char *pchChannelName, U32 iColor, U32 iDefaultColor)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	ChatConfigCommon_SetChannelColor(&pData->chatConfig, pchChannelName, iColor, iDefaultColor);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(pData, ".chatConfig.eaMessageTypeColorDefs");
enumTransactionOutcome tr_crChatConfig_SetMessageColor(ATR_ARGS, NOCONST(GameAccountData) *pData, const char *pchMessageName, U32 iColor, U32 iDefaultColor)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	ChatConfigCommon_SetMessageColor(&pData->chatConfig, pchMessageName, iColor, iDefaultColor);
	return TRANSACTION_OUTCOME_SUCCESS;
}

////////////////////////////////
// Tab Group Transactions

// Reset a chat tab group
AUTO_TRANSACTION ATR_LOCKS(pData, ".chatConfig.eaChatTabGroupConfigs[], .iAccountID");
enumTransactionOutcome tr_crChatConfig_ResetTabGroup(ATR_ARGS, NOCONST(GameAccountData) *pData, char *pchTabGroup)
{
	NOCONST(ChatTabGroupConfig) *pTabGroup;
	ChatRelayUser *relayUser;

	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	pTabGroup = eaIndexedGetUsingString(&pData->chatConfig.eaChatTabGroupConfigs, pchTabGroup);
	relayUser = chatRelayGetUser(pData->iAccountID);
	if (!pTabGroup || !relayUser)
		return TRANSACTION_OUTCOME_FAILURE;

	ChatConfigCommon_ResetTabGroup(&pData->chatConfig, pchTabGroup, relayUser->eLanguage, relayUser->uSource);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Create a new chat tab group
AUTO_TRANSACTION ATR_LOCKS(pData, ".chatConfig.eaChatTabGroupConfigs, .iAccountID");
enumTransactionOutcome tr_crChatConfig_CreateTabGroup(ATR_ARGS, NOCONST(GameAccountData) *pData, char *pchTabGroup, int bInitTabs)
{
	ChatRelayUser *relayUser = chatRelayGetUser(pData->iAccountID);
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	if (!relayUser)
		return TRANSACTION_OUTCOME_FAILURE;
	ChatConfigCommon_CreateTabGroup(&pData->chatConfig, pchTabGroup, bInitTabs, relayUser->eLanguage, relayUser->uSource);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Delete a chat tab group
AUTO_TRANSACTION ATR_LOCKS(pData, ".chatConfig.eaChatTabGroupConfigs");
enumTransactionOutcome tr_crConfig_DeleteTabGroup(ATR_ARGS, NOCONST(GameAccountData) *pData, const char *pchTabGroup)
{
	U32 idx;
	NOCONST(ChatTabGroupConfig) *pTabGroup;

	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	idx = eaIndexedFindUsingString(&pData->chatConfig.eaChatTabGroupConfigs, pchTabGroup);
	if (idx == -1)
		return TRANSACTION_OUTCOME_SUCCESS;
	pTabGroup = pData->chatConfig.eaChatTabGroupConfigs[idx];

	eaRemove(&pData->chatConfig.eaChatTabGroupConfigs, idx);
	StructDestroyNoConst(parse_ChatTabGroupConfig, pTabGroup);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Create a new chat tab
AUTO_TRANSACTION ATR_LOCKS(pData, ".chatConfig.eaChatTabGroupConfigs, .iAccountID");
enumTransactionOutcome tr_crChatConfig_CreateTab(ATR_ARGS, NOCONST(GameAccountData) *pData, const char *pchTabGroup)
{
	ChatRelayUser *relayUser;
	int idx;

	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	relayUser = chatRelayGetUser(pData->iAccountID);
	if (!relayUser)
		return TRANSACTION_OUTCOME_FAILURE;

	idx = ChatConfigCommon_CreateTab(&pData->chatConfig, pchTabGroup, relayUser->eLanguage, relayUser->uSource);
	if (idx == -1)
		return TRANSACTION_OUTCOME_FAILURE;
	GClientCmd_ChatConfig_NotifyTabIndexUpdated(relayUser->uAccountID, idx);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Copy a chat tab
AUTO_TRANSACTION ATR_LOCKS(pData, ".chatConfig.eaChatTabGroupConfigs, .iAccountID");
enumTransactionOutcome tr_crChatConfig_CopyTab(ATR_ARGS, NOCONST(GameAccountData) *pData, const char *pchDstTabGroup, const char *pchDstName, const char *pchSrcTabGroup, int iSrcTab)
{
	ChatRelayUser *relayUser;
	int idx;

	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	relayUser = chatRelayGetUser(pData->iAccountID);
	if (!relayUser)
		return TRANSACTION_OUTCOME_FAILURE;

	idx = ChatConfigCommon_CopyTab(&pData->chatConfig, pchDstTabGroup, pchDstName, pchSrcTabGroup, iSrcTab);

	if (idx == -1)
		return TRANSACTION_OUTCOME_FAILURE;
	GClientCmd_ChatConfig_NotifyTabIndexUpdated(relayUser->uAccountID, idx);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Delete a chat tab
AUTO_TRANSACTION ATR_LOCKS(pData, ".chatConfig.eaChatTabGroupConfigs[], .iAccountID");
enumTransactionOutcome tr_crChatConfig_DeleteTab(ATR_ARGS, NOCONST(GameAccountData) *pData, char *pchTabGroup, int iTab)
{
	ChatRelayUser *relayUser;
	int idx;

	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	relayUser = chatRelayGetUser(pData->iAccountID);
	if (!relayUser)
		return TRANSACTION_OUTCOME_FAILURE;

	idx = ChatConfigCommon_DeleteTab(&pData->chatConfig, pchTabGroup, iTab);
	if (idx == -1)
		return TRANSACTION_OUTCOME_FAILURE;
	GClientCmd_ChatConfig_NotifyTabIndexUpdated(relayUser->uAccountID, idx);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Rename a tab
AUTO_TRANSACTION ATR_LOCKS(pData, ".chatConfig.eaChatTabGroupConfigs");
enumTransactionOutcome tr_crChatConfig_SetTabName(ATR_ARGS, NOCONST(GameAccountData) *pData, char *pchTabGroup, int iTab, char *pchName)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	if (ChatConfigCommon_SetTabName(&pData->chatConfig, pchTabGroup, iTab, pchName))
		return TRANSACTION_OUTCOME_SUCCESS;
	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANSACTION ATR_LOCKS(pData, ".chatConfig.eaChatTabGroupConfigs[], .iAccountID");
enumTransactionOutcome tr_crChatConfig_MoveTab(ATR_ARGS, NOCONST(GameAccountData) *pData, const char *pchFromGroupName, S32 iFromIndex, const char *pchToGroupName, S32 iToIndex)
{
	ChatRelayUser *relayUser;
	int idx;
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	relayUser = chatRelayGetUser(pData->iAccountID);
	if (!relayUser)
		return TRANSACTION_OUTCOME_FAILURE;

	idx = ChatConfigCommon_MoveTab(&pData->chatConfig, pchFromGroupName, iFromIndex, pchToGroupName, iToIndex);
	if (idx == -1)
		return TRANSACTION_OUTCOME_FAILURE;
	//TODO: We should change notify tab index updated to include the tab group name!!!
	GClientCmd_ChatConfig_NotifyTabIndexUpdated(relayUser->uAccountID, idx);
	return TRANSACTION_OUTCOME_SUCCESS;
}

////////////////////////////////
// Filter Transactions

// Set the filtering of a message type for the given tab group tab
AUTO_TRANSACTION ATR_LOCKS(pData, ".chatConfig.eaChatTabGroupConfigs");
enumTransactionOutcome tr_crChatConfig_SetMessageTypeFilter(ATR_ARGS, NOCONST(GameAccountData) *pData, const char *pchTabGroup, int iTab, const char *pchFilterName, int bFiltered)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	ChatConfigCommon_SetMessageTypeFilter(&pData->chatConfig, pchTabGroup, iTab, pchFilterName, bFiltered);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Set the filtering of a channel for the given tab group tab
AUTO_TRANSACTION ATR_LOCKS(pData, ".chatConfig.eaChatTabGroupConfigs");
enumTransactionOutcome tr_crChatConfig_SetChannelFilter(ATR_ARGS, NOCONST(GameAccountData) *pData, const char *pchTabGroup, int iTab, const char *pchFilterName, int bFiltered)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	ChatConfigCommon_SetChannelFilter(&pData->chatConfig, pchTabGroup, iTab, pchFilterName, bFiltered);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(pData, ".chatConfig.eaChatTabGroupConfigs, .iAccountID");
enumTransactionOutcome tr_crChatConfig_SetTabFilterMode(ATR_ARGS, NOCONST(GameAccountData) *pData, const char *pchTabGroup, int iTab, int bExclusiveFilter)
{
	NOCONST(ChatTabConfig) *pTabConfig = ChatConfigCommon_GetTabConfig(&pData->chatConfig, pchTabGroup, iTab);
	ChatRelayUser *relayUser = chatRelayGetUser(pData->iAccountID);
	bool bChanged;

	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	if (!pTabConfig)
		return TRANSACTION_OUTCOME_FAILURE;
	bChanged = ChatConfigCommon_SetTabFilterMode(pTabConfig, relayUser ? &relayUser->eaSubscribedChannels : NULL,
		pchTabGroup, iTab, bExclusiveFilter);
	if (bChanged)
		ChatConfigCommon_UpdateCommonTabs(&pData->chatConfig, pTabConfig);
	return TRANSACTION_OUTCOME_SUCCESS;
}


AUTO_TRANSACTION ATR_LOCKS(pData, ".chatConfig.eaChatTabGroupConfigs, .iAccountID");
enumTransactionOutcome tr_crChatConfig_SetAllFilters(ATR_ARGS, NOCONST(GameAccountData) *pData, const char *pchTabGroup, int iTab, int bFiltered)
{
	ChatRelayUser *relayUser = chatRelayGetUser(pData->iAccountID);
	NOCONST(ChatTabConfig) *pTabConfig = ChatConfigCommon_GetTabConfig(&pData->chatConfig, pchTabGroup, iTab);
	bool bListAllMessageTypes;
	bool bListAllChannels;

	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	if (!pTabConfig)
		return TRANSACTION_OUTCOME_FAILURE;

	bListAllMessageTypes = pTabConfig->eMessageTypeFilterMode == kChatTabFilterMode_Inclusive ? bFiltered : !bFiltered;
	bListAllChannels = pTabConfig->eChannelFilterMode == kChatTabFilterMode_Inclusive ? bFiltered : !bFiltered;
	ChatConfigCommon_ResetMessageTypeFilters(pTabConfig, bListAllMessageTypes);
	ChatConfigCommon_ResetChannelFilters(pTabConfig, &relayUser->eaSubscribedChannels, bListAllChannels);
	ChatConfigCommon_UpdateCommonTabs(&pData->chatConfig, pTabConfig);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Input Channel
AUTO_TRANSACTION ATR_LOCKS(pData, ".chatConfig.eaChatTabGroupConfigs[]");
enumTransactionOutcome tr_crChatConfig_SetDefaultInputChannel(ATR_ARGS, NOCONST(GameAccountData) *pData, const char *pchTabGroup, S32 iTab, const char *pchChannel)
{
	NOCONST(ChatTabConfig) *pTabConfig = ChatConfigCommon_GetTabConfig(&pData->chatConfig, pchTabGroup, iTab);
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	if (!pTabConfig)
		return TRANSACTION_OUTCOME_FAILURE;

	estrCopy2(&pTabConfig->pchDefaultChannel, pchChannel);
	//ChatConfigCommon_UpdateCommonTabs(&pData->chatConfig, pTabConfig);
	return TRANSACTION_OUTCOME_SUCCESS;
}

////////////////////////////////
// Other Settings

// Sets whether chat log entries should include dates
AUTO_TRANSACTION ATR_LOCKS(pData, ".chatConfig.bShowDate, .chatConfig.iVersion");
enumTransactionOutcome tr_crChatConfig_SetShowDate(ATR_ARGS, NOCONST(GameAccountData) *pData, int bShowDate)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	pData->chatConfig.bShowDate = bShowDate;
	pData->chatConfig.iVersion++;
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Sets whether chat log entries should include timestamps
AUTO_TRANSACTION ATR_LOCKS(pData, ".chatConfig.bShowTime, .chatConfig.iVersion");
enumTransactionOutcome tr_crChatConfig_SetShowTime(ATR_ARGS, NOCONST(GameAccountData) *pData, int bShowTime)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	pData->chatConfig.bShowTime = bShowTime;
	pData->chatConfig.iVersion++;
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Sets whether chat log entries should always show message types
AUTO_TRANSACTION ATR_LOCKS(pData, ".chatConfig.bShowMessageTypeNames, .chatConfig.iVersion");
enumTransactionOutcome tr_crChatConfig_SetShowMessageTypeNames(ATR_ARGS, NOCONST(GameAccountData) *pData, int bShowMessageTypeNames)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	pData->chatConfig.bShowMessageTypeNames = bShowMessageTypeNames;
	pData->chatConfig.iVersion++;
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Sets whether chat links should display full names or just the character name (if provided)
AUTO_TRANSACTION ATR_LOCKS(pData, ".chatConfig.bHideAccountNames, .chatConfig.iVersion");
enumTransactionOutcome tr_crChatConfig_SetShowFullNames(ATR_ARGS, NOCONST(GameAccountData) *pData, int bShowFullNames)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	pData->chatConfig.bHideAccountNames = !bShowFullNames;
	pData->chatConfig.iVersion++;
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Sets whether chat log entries should be filtered for profanity
AUTO_TRANSACTION ATR_LOCKS(pData, ".chatConfig.bProfanityFilter, .chatConfig.iVersion");
enumTransactionOutcome tr_crChatConfig_SetProfanityFilter(ATR_ARGS, NOCONST(GameAccountData) *pData, int bProfanityFilter)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	pData->chatConfig.bProfanityFilter = bProfanityFilter;
	pData->chatConfig.iVersion++;
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Sets whether chat log entries should show channel names
AUTO_TRANSACTION ATR_LOCKS(pData, ".chatConfig.bShowChannelNames, .chatConfig.iVersion");
enumTransactionOutcome tr_crChatConfig_SetShowChannelNames(ATR_ARGS, NOCONST(GameAccountData) *pData, int bShowChannelNames)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	pData->chatConfig.bShowChannelNames = bShowChannelNames;
	pData->chatConfig.iVersion++;
	return TRANSACTION_OUTCOME_SUCCESS;
}



// Sets whether the auto-complete suggestion list shows the source of it entries
AUTO_TRANSACTION ATR_LOCKS(pData, ".chatConfig.bAnnotateAutoComplete");
enumTransactionOutcome tr_crChatConfig_SetAnnotateAutoComplete(ATR_ARGS, NOCONST(GameAccountData) *pData, int bAnnotateAutoComplete)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	pData->chatConfig.bAnnotateAutoComplete = bAnnotateAutoComplete;
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Sets font size used by the chat window
AUTO_TRANSACTION ATR_LOCKS(pData, ".chatConfig.fFontScale");
enumTransactionOutcome tr_crChatConfig_SetFontScale(ATR_ARGS, NOCONST(GameAccountData) *pData, F32 fFontScale)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	pData->chatConfig.fFontScale = fFontScale;
	// Changing the scale does *not* increment the version, since scale does
	// not require reformatting the text, just laying it out again.
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Sets chat window alpha to use when active
AUTO_TRANSACTION ATR_LOCKS(pData, ".chatConfig.fActiveWindowAlpha");
enumTransactionOutcome tr_crChatConfig_SetActiveWindowAlpha(ATR_ARGS, NOCONST(GameAccountData) *pData, F32 fAlpha)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	pData->chatConfig.fActiveWindowAlpha = fAlpha;
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Sets chat window alpha to use when inactive
AUTO_TRANSACTION ATR_LOCKS(pData, ".chatConfig.fInactiveWindowAlpha");
enumTransactionOutcome tr_crChatConfig_SetInactiveWindowAlpha(ATR_ARGS, NOCONST(GameAccountData) *pData, F32 fAlpha)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	pData->chatConfig.fInactiveWindowAlpha = fAlpha;
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Sets the fade away duration for chat entries
AUTO_TRANSACTION ATR_LOCKS(pData, ".chatConfig.siFadeAwayDuration");
enumTransactionOutcome tr_crChatConfig_SetFadeAwayDuration(ATR_ARGS, NOCONST(GameAccountData) *pData, F32 fFadeAwayDuration)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	ChatConfigCommon_SetFadeAwayDuration(&pData->chatConfig, fFadeAwayDuration);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Store the visibility of the chat window
AUTO_TRANSACTION ATR_LOCKS(pData, ".chatConfig.bChatHidden");
enumTransactionOutcome tr_crChatConfig_SetChatVisible(ATR_ARGS, NOCONST(GameAccountData) *pData, int bVisible)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	pData->chatConfig.bChatHidden = !bVisible;
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Sets the time required for chat entries to start fading away
AUTO_TRANSACTION ATR_LOCKS(pData, ".chatConfig.siTimeRequiredToStartFading");
enumTransactionOutcome tr_crChatConfig_SetTimeRequiredToStartFading(ATR_ARGS, NOCONST(GameAccountData) *pData, F32 fTimeRequiredToStartFading)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	ChatConfigCommon_SetTimeRequiredToStartFading(&pData->chatConfig, fTimeRequiredToStartFading);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(pData, ".iAccountID, .chatConfig.pchStatusMessage");
enumTransactionOutcome tr_crChatConfig_SetStatusMessage(ATR_ARGS, NOCONST(GameAccountData) *pData, const char *pchMessage)
{
	ChatRelayUser *relayUser = chatRelayGetUser(pData->iAccountID);

	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	if (!relayUser)
		return TRANSACTION_OUTCOME_FAILURE;
	switch (ChatConfigCommon_SetStatusMessage(&pData->chatConfig, pchMessage))
	{
	case CHATCONFIG_STATUS_CHANGED:
		RemoteCommand_UserSetStatus(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, pchMessage);
	xcase CHATCONFIG_STATUS_PROFANE:
		{
			const char *pchError = langTranslateMessageKey(relayUser->eLanguage, "Chat_Status_ContainsProfanity");
			crNotifySend(relayUser, kNotifyType_Failed, pchError);
		}
	xcase CHATCONFIG_STATUS_UNCHANGED:
	default: // does nothing
		break;
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}
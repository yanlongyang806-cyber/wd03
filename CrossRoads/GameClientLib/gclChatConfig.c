/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "cmdparse.h"
#include "chatCommon.h"
#include "chatCommonStructs.h"
#include "Expression.h"
#include "GameAccountData/GameAccountData.h"
#include "GameStringFormat.h"
#include "gclChat.h"
#include "gclChatConfig.h"
#include "gclEntity.h"
#include "gclSendToServer.h"
#include "LoginCommon.h"
#include "NotifyCommon.h"
#include "StringUtil.h"
#include "TextFilter.h"
#include "UIGen.h"
#include "UIGenColorChooser.h"
#include "Player.h"
#include "GameClientLib.h"
#include "gclLogin.h"
#include "Login2Common.h"

#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "Autogen/chatCommon_h_ast.h"
#include "Autogen/chatCommonStructs_h_ast.h"
#include "Autogen/gclChatConfig_h_ast.c"
#include "AutoGen/ChatRelay_autogen_GenericServerCmdWrappers.h"
#include "AutoGen/GameAccountData_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static S32 s_iNewTabIndex = -1;
extern ChatAuthData *g_pChatAuthData;

//
// Private/static utility method
//

bool ClientChatConfig_UsingEntityConfig(void)
{
	//return entActivePlayerPtr() != NULL;
	// Removed non-entity configs due to Lobby removal
	return true;
}

static ChatConfig * ClientChatConfig_GetAccountChatConfig(void)
{
#ifdef USE_CHATRELAY
	if (g_pChatAuthData && g_pChatAuthData->uAccountID)
	{
        if ( g_characterSelectionData )
        {
            GameAccountData *gameAccountData = GET_REF(g_characterSelectionData->hGameAccountData);
            if ( gameAccountData )
            {
			    return (ChatConfig*) &gameAccountData->chatConfig;
            }
        }
	}
#endif
	return NULL;
}

// Receive the updated GAD Chat Config through the Chat Relay
AUTO_COMMAND ACMD_GENERICCLIENTCMD ACMD_IFDEF(CHATRELAY) ACMD_PRIVATE ACMD_HIDE ACMD_ACCESSLEVEL(0);
void gclClientChat_UpdateGADChatConfig(ChatConfig *pChatConfig)
{
    if (g_characterSelectionData )
    {
        GameAccountData *gameAccountData = GET_REF(g_characterSelectionData->hGameAccountData);
        if ( gameAccountData )
        {
            StructCopy(parse_ChatConfig, pChatConfig, (ChatConfig*) &gameAccountData->chatConfig, 0, 0, TOK_NO_TRANSACT);
        }
    }
}

SA_RET_OP_VALID extern ChatConfig *ClientChatConfig_GetChatConfig(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (ClientChatConfig_UsingEntityConfig())
	{
		ChatConfig *pChatConfig = ChatCommon_GetChatConfig(pEntity);
		if (pChatConfig)
		{
			// Make sure there's a tab group with at least one tab
			if (eaSize(&pChatConfig->eaChatTabGroupConfigs) > 0)
			{
				ChatTabGroupConfig *pTabGroup = eaGet(&pChatConfig->eaChatTabGroupConfigs, 0);
				if (eaSize(&pTabGroup->eaChatTabConfigs) > 0)
					return pChatConfig;
			}
			// Somehow all of the tabs disappeared.  Reset everything so that the user isn't screwed
			ServerCmd_cmdServerChatConfig_Reset();

		}
		if (pEntity)
		{
			// It's not initialized...so poke the game server to initialize it
			ServerCmd_cmdServerChatConfig_PlayerUpdate();
		}
		// No Entity here is a... weird case
	}
	else if (gclChatIsConnected())
	{
		static U32 suLastResetTime = 0;
		U32 uCurTime;
		// No entity or don't want to use it - use GAD Chat Config
		ChatConfig *pChatConfig = ClientChatConfig_GetAccountChatConfig();
		if (pChatConfig && 
			eaSize(&pChatConfig->eaChatTabGroupConfigs) > 0 && 
			eaSize(&pChatConfig->eaChatTabGroupConfigs[0]->eaChatTabConfigs) > 0)
			return pChatConfig;

		// Otherwise something isn't right about the config - reset/init it
		uCurTime = timeSecondsSince2000();
		if (uCurTime - suLastResetTime > CHAT_CONFIG_RESET_TIME)
		{
			GServerCmd_crChatConfig_Reset(GLOBALTYPE_CHATRELAY);
			suLastResetTime = uCurTime;
		}
	}
	return NULL;
}

static NOCONST(ChatConfig) *ClientChatConfig_GetEditableConfig(void)
{
	Entity *pEnt = entActivePlayerPtr();
	return CONTAINER_NOCONST(ChatConfig, ClientChatConfig_GetChatConfig(pEnt));
}

SA_RET_NN_VALID extern ChatConfigInputChannel *ClientChatConfig_GetCurrentInputChannel(void)
{
	static ChatConfigInputChannel *s_pCurrentInputChannel = NULL;
	Entity *pEnt = entActivePlayerPtr();
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEnt);

	if (s_pCurrentInputChannel == NULL)
	{
		char *pchChannel = pConfig ? pConfig->pchCurrentInputChannel : LOCAL_CHANNEL_NAME;
		if (!pchChannel || !*pchChannel)
			pchChannel = LOCAL_CHANNEL_NAME;

		s_pCurrentInputChannel = StructCreate(parse_ChatConfigInputChannel);
		ClientChat_FillInputChannelInput(s_pCurrentInputChannel, pchChannel, true);
	}
	
	// If the cached input channel differs from the persisted one, then refill it
	if (pConfig && stricmp_safe(pConfig->pchCurrentInputChannel, s_pCurrentInputChannel->pchSystemName))
	{
		ClientChat_FillInputChannelInput(s_pCurrentInputChannel, pConfig->pchCurrentInputChannel, true);
	}
	return s_pCurrentInputChannel;
}

//
// Public UI interfaces
//

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_GetCurrentTabGroupName);
const char* ClientChatConfig_GetCurrentTabGroupName() {
	Entity *pEntity = entActivePlayerPtr();
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	if (pConfig && pConfig->pchCurrentTabGroup && *pConfig->pchCurrentTabGroup) {
		return pConfig->pchCurrentTabGroup;
	}

	return ChatCommon_GetConfigDefaults(ChatCommon_GetChatConfigSourceForEntity(pEntity))->pchDefaultTabGroupName;
}

AUTO_COMMAND ACMD_NAME(ChatConfig_NotifyTabIndexUpdated) ACMD_PRIVATE ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_GENERICCLIENTCMD;
void cmdClientChatConfigNotifyTabIndexUpdated(S32 iNewTabIndex)
{
	s_iNewTabIndex = iNewTabIndex;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_GetCurrentTabIndex);
S32 ClientChatConfig_GetCurrentTabIndex(const char *pchTabGroup) {
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(entActivePlayerPtr());
	ChatTabGroupConfig *pTabGroup = ChatCommon_GetTabGroupConfig(pConfig, pchTabGroup);

	if (pTabGroup) {
		return pTabGroup->iCurrentTab;
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_SetCurrentTab);
void exprClientChatConfigSetCurrentTab(ExprContext *pContext, const char *pchCurrentTabGroup, S32 iTabIndex) {
	Entity *pEntity = entActivePlayerPtr();
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);

	if (!pchCurrentTabGroup || !*pchCurrentTabGroup || iTabIndex < 0) {
		return;
	}

	if (pConfig && pConfig->pchCurrentTabGroup && *pConfig->pchCurrentTabGroup) {
		if (!stricmp(pchCurrentTabGroup, pConfig->pchCurrentTabGroup)) {
			NOCONST(ChatTabGroupConfig) *pTabGroupConfig = CONTAINER_NOCONST(ChatTabGroupConfig, ChatCommon_GetCurrentTabGroupConfig(pConfig, ChatCommon_GetChatConfigSourceForEntity(pEntity)));
			if (pTabGroupConfig) {
				if (pTabGroupConfig->iCurrentTab == iTabIndex) {
					// No change, no reason to call the server...
					return;
				}

				// Note: This is technically unsafe because we're modifying
				// the entity, but we can't wait for the Entity round trip
				// (client->server->client) update to occur...
				// The reason we do this is to keep the tab selection
				// in the chat window in sync with the Tabs tab of the 
				// chat config.  If we don't do this, then the chat config
				// always shows the previously selected tab.  If there's a
				// better way to do this, then feel free to fix.
				ChatConfigCommon_SetCurrentTab(pConfig, pchCurrentTabGroup, iTabIndex);
			}
		}
	}
	s_iNewTabIndex = -1;
	if (ClientChatConfig_UsingEntityConfig())
		ServerCmd_cmdServerChatConfig_SetCurrentTab(pchCurrentTabGroup, iTabIndex);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_GetNewTabIndex);
S32 exprClientChatConfigGetNewTabIndex(ExprContext *pContext) {
	return s_iNewTabIndex;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_GetTabIndex);
S32 exprClientChatGetTabIndex(ExprContext *pContext, const char *pchTabGroup, const char *pchTab) {
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(entActivePlayerPtr());
	ChatTabGroupConfig *pTabGroup = ChatCommon_GetTabGroupConfig(pConfig, pchTabGroup);
	return ChatCommon_FindTabConfigIndex(pTabGroup, pchTab);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_SetNewTabIndexToCurrent);
void exprClientChatConfigSetNewTabIndexToCurrent(ExprContext *pContext) {
	Entity *pEntity = entActivePlayerPtr();
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	ChatTabGroupConfig *pTabGroup = ChatCommon_GetCurrentTabGroupConfig(pConfig, ChatCommon_GetChatConfigSourceForEntity(pEntity));

	if (pTabGroup) {
		s_iNewTabIndex = pTabGroup->iCurrentTab;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_ResetNewTabIndex);
void exprClientChatConfigResetNewTabIndex(ExprContext *pContext) {
	s_iNewTabIndex = -1;
}

// Get the name of the current tab
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_GetCurrentTabName);
const char *exprClientChatConfigGetTabName(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	ChatTabConfig *pTabConfig = ChatCommon_GetCurrentTabConfig(pConfig, ChatCommon_GetChatConfigSourceForEntity(pEntity));
	if (pTabConfig) {
		return pTabConfig->pchTitle;
	}

	return NULL;
}

// Set the specified tab's name
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_SetCurrentTabNameForTabIndex);
void exprClientChatConfigSetTabNameForTabIndex(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, const char *pchTabName, S32 iTab)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	ChatTabGroupConfig *pTabGroup = ChatCommon_GetCurrentTabGroupConfig(pConfig, ChatCommon_GetChatConfigSourceForEntity(pEntity));
	if (pTabGroup)
	{
		if (ClientChatConfig_UsingEntityConfig())
			ServerCmd_cmdServerChatConfig_SetTabName(pTabGroup->pchName, iTab, pchTabName);
		else
		{
			ChatConfigCommon_SetTabName(CONTAINER_NOCONST(ChatConfig, pConfig), pTabGroup->pchName, iTab, pchTabName);
			GServerCmd_crChatConfig_SetTabName(GLOBALTYPE_CHATRELAY, pTabGroup->pchName, iTab, pchTabName);
		}
	}
}

// Set the current tab's name
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_SetCurrentTabName);
void exprClientChatConfigSetTabName(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, const char *pchTabName)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	ChatTabGroupConfig *pTabGroup = ChatCommon_GetCurrentTabGroupConfig(pConfig, ChatCommon_GetChatConfigSourceForEntity(pEntity));
	exprClientChatConfigSetTabNameForTabIndex(pContext, pEntity, pchTabName, SAFE_MEMBER(pTabGroup,iCurrentTab));
}

// Get the filter mode of the current tab.  True == Exclusive.  False = Inclusive.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_IsCurrentTabFilterModeExclusive);
bool exprClientChatIsCurrentTabFilterModeExclusive(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	ChatTabConfig *pTabConfig = ChatCommon_GetCurrentTabConfig(pConfig, ChatCommon_GetChatConfigSourceForEntity(pEntity));
	if (pTabConfig) {
		devassertmsg(pTabConfig->eMessageTypeFilterMode == pTabConfig->eChannelFilterMode, "Message & Channel filter modes are out of sync.  This isn't tested/supported.  Your mileage may vary.");
		return pTabConfig->eMessageTypeFilterMode == kChatTabFilterMode_Exclusive;
	}

	return false;
}

// Set the filter mode of the current tab.  True == Exclusive. False = Inclusive.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_SetCurrentTabFilterModeExclusive);
void exprClientChatConfigSetCurrentTabFilterModeExclusive(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, bool bExclusive)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	ChatTabGroupConfig *pTabGroup = ChatCommon_GetCurrentTabGroupConfig(pConfig, ChatCommon_GetChatConfigSourceForEntity(pEntity));
	if (pTabGroup)
	{
		if (ClientChatConfig_UsingEntityConfig())
			ServerCmd_cmdServerChatConfig_SetTabFilterMode(pTabGroup->pchName, pTabGroup->iCurrentTab, bExclusive);
		else
			GServerCmd_crChatConfig_SetTabFilterMode(GLOBALTYPE_CHATRELAY, pTabGroup->pchName, pTabGroup->iCurrentTab, bExclusive);
	}
}

static void ClientChatConfig_MoveTab(ChatConfig *pConfig, const char *pchFromGroup, S32 iFromTabIndex, const char *pchToGroup, S32 iToTabIndex)
{
	if (ClientChatConfig_UsingEntityConfig())
		ServerCmd_cmdServerChatConfig_MoveTab(pchFromGroup, iFromTabIndex, pchToGroup, iToTabIndex);
	else
	{
		int idx = ChatConfigCommon_MoveTab(CONTAINER_NOCONST(ChatConfig, pConfig), pchFromGroup, iFromTabIndex, pchToGroup, iToTabIndex);
		if (idx != -1)
			s_iNewTabIndex = idx;
		GServerCmd_crChatConfig_MoveTab(GLOBALTYPE_CHATRELAY, pchFromGroup, iFromTabIndex, pchToGroup, iToTabIndex);
	}
}

// Move a tab
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_MoveTab);
void exprClientChatConfigMoveTab(ExprContext *pContext, const char *pchFromGroup, S32 iFromTabIndex, const char *pchToGroup, const char *pchToTab, bool bInsertAfter)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(entActivePlayerPtr());
	ChatTabGroupConfig *pFromGroup = ChatCommon_GetTabGroupConfig(pConfig, pchFromGroup);
	ChatTabGroupConfig *pToGroup = ChatCommon_GetTabGroupConfig(pConfig, pchToGroup);
	S32 iToTabIndex = ChatCommon_FindTabConfigIndex(pToGroup, pchToTab);

	// If we have a bad from tab group or tab names, then abort.  It's OK to have bad/NULL to tab names 
	// because we will infer that to mean we want to create a new tab group to house the "from tab".
	if (!pFromGroup || iFromTabIndex < 0) {
		return;
	}

	// If the from & to indices are the same, then there's no change
	if (pFromGroup == pToGroup && iFromTabIndex == iToTabIndex) {
		return;
	}

	// Increment the index if we're inserting after the "to" tab.
	// Note: we want to do that AFTER checking that that the indices are not the same.
	if (bInsertAfter) {
		iToTabIndex++;
	}

	ClientChatConfig_MoveTab(pConfig, pchFromGroup, iFromTabIndex, pchToGroup, iToTabIndex);
}

// Fill the gen's chat tab configs, starting at a certain index, from the named tab group
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_FillTabConfigsFromGroupStartingFrom);
void exprClientChatConfigFillTabConfigsFromGroupStartingFrom(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity, int iStartIndex, const char* pchTabGroupName)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	ChatTabConfig ***peaTabConfigs = ui_GenGetManagedListSafe(pGen, ChatTabConfig);
	int i;

	if (pConfig && pConfig->eaChatTabGroupConfigs) {
		ChatTabGroupConfig *pTabGroup = ChatCommon_GetTabGroupConfig(pConfig, pchTabGroupName);
		if (pTabGroup) {
			int numEntries = eaSize(&pTabGroup->eaChatTabConfigs);

			// Make sure space is allocated for all rows
			eaSetSizeStruct(peaTabConfigs, parse_ChatTabConfig, numEntries);

			// From iStartIndex to the end of the array
			for (i=0; i < numEntries; ++i) {
				ChatTabConfig *pTabConfigDest = eaGet(peaTabConfigs, i);
				ChatTabConfig *pTabConfigSrc = eaGet(&pTabGroup->eaChatTabConfigs, (i+iStartIndex+numEntries) % numEntries);
				estrCopy2(&CONTAINER_NOCONST(ChatTabConfig, pTabConfigDest)->pchTitle, pTabConfigSrc->pchTitle);
			}
		}
	}

	if (pGen) {
		ui_GenSetManagedListSafe(pGen, peaTabConfigs, ChatTabConfig, true);
	}
}


// Fill the gen's chat tab configs, starting at a certain index
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_FillTabConfigsStartingFrom);
void exprClientChatConfigFillTabConfigsStartingFrom(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity, int iStartIndex)
{
	exprClientChatConfigFillTabConfigsFromGroupStartingFrom(pContext, pGen, pEntity, iStartIndex, pGen->pchName);
}

// Fill the gen's chat tab configs
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_FillTabConfigs);
void exprClientChatConfigFillTabConfigs(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity)
{
	exprClientChatConfigFillTabConfigsStartingFrom(pContext, pGen, pEntity, 0);
}

// Fill the gen's tab filter defs for the specified tab
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_FillTabMessageTypeFilterDefsForTabIndex);
void exprChatConfig_FillTabMessageTypeFilterDefsForTabIndex(ExprContext *pContext, 
									  SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity, S32 iTab)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	ChatTabGroupConfig *pTabGroup = ChatCommon_GetCurrentTabGroupConfig(pConfig, ChatCommon_GetChatConfigSourceForEntity(pEntity));
	ChatTabConfig *pTabConfig = pTabGroup ? eaGet(&pTabGroup->eaChatTabConfigs, iTab) : NULL;
	ChatLogEntryType **peaiMessageTypes = ChatCommon_GetFilterableMessageTypes();
	static ChatConfigTabFilterDef **eaFilterDefs = NULL;

	if (pTabConfig) {
		int numMessageTypes = eaiSize(peaiMessageTypes);
		int i;

		// Make sure space is allocated for all rows
		eaSetSizeStruct(&eaFilterDefs, parse_ChatConfigTabFilterDef, numMessageTypes);

		// Fill in message type filter defs
		for (i = 0; i < numMessageTypes; i++) {
			ChatConfigTabFilterDef *pFilterDef = eaFilterDefs[i];
			ChatLogEntryType kMessageType = (*peaiMessageTypes)[i];
			const char *pchMessageTypeName = ChatLogEntryTypeEnum[1+kMessageType].key;

			estrCopy2(&pFilterDef->pchChatTabGroupName, pConfig->pchCurrentTabGroup);
			estrCopy2(&pFilterDef->pchFilterSystemName, pchMessageTypeName);
			ClientChat_GetMessageTypeDisplayNameByType(&pFilterDef->pchFilterDisplayName, kMessageType);

			pFilterDef->iTab = pTabGroup->iCurrentTab;
			pFilterDef->kMessageType = kMessageType;
			pFilterDef->bIsChannel = false;
			pFilterDef->bMayUnsubscribe = false;
			pFilterDef->bFiltered = 
				eaFindString(&pTabConfig->eaMessageTypes, pchMessageTypeName) >= 0;
			if (pTabConfig->eMessageTypeFilterMode == kChatTabFilterMode_Exclusive) {
				pFilterDef->bFiltered = !pFilterDef->bFiltered;
			}
			pFilterDef->iColor = ChatCommon_GetChatColor(pConfig, kMessageType, pchMessageTypeName, ChatCommon_GetChatConfigSourceForEntity(pEntity));
			ChatCommon_SetVectorFromColor(pFilterDef->vColor, pFilterDef->iColor);
		}
	}

	if (pGen) {
		eaSortUsingKey(&eaFilterDefs, parse_ChatConfigTabFilterDef);
		ui_GenSetManagedListSafe(pGen, &eaFilterDefs, ChatConfigTabFilterDef, false);
	}
}

// Fill the gen's tab filter defs for the current tab
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_FillTabMessageTypeFilterDefs);
void exprChatConfig_FillTabMessageTypeFilterDefs(ExprContext *pContext, 
									  SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	ChatTabGroupConfig *pTabGroup = ChatCommon_GetCurrentTabGroupConfig(pConfig, ChatCommon_GetChatConfigSourceForEntity(pEntity));
	exprChatConfig_FillTabMessageTypeFilterDefsForTabIndex(pContext, pGen, pEntity, SAFE_MEMBER(pTabGroup,iCurrentTab) );
}

// Fill the gen's tab filter defs for the specified tab
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_FillTabChannelFilterDefsForTabIndex);
void exprClientChatConfig_FillTabChannelFilterDefsForTabIndex(ExprContext *pContext, 
	SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity, S32 iTab)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	ChatTabGroupConfig *pTabGroup = ChatCommon_GetCurrentTabGroupConfig(pConfig, ChatCommon_GetChatConfigSourceForEntity(pEntity));
	ChatTabConfig* pTabConfig = pTabGroup ? eaGet(&pTabGroup->eaChatTabConfigs, iTab) : NULL;
	const char ***peaSubscribedChannels = ClientChat_GetSubscribedCustomChannels();
	static ChatConfigTabFilterDef **eaFilterDefs = NULL;

	if (pTabConfig) {
		int numSubscribedChannels = eaSize(peaSubscribedChannels);
		int numEntries = numSubscribedChannels;
		int i;

		eaSetSizeStruct(&eaFilterDefs, parse_ChatConfigTabFilterDef, numSubscribedChannels);

		// Fill in subscription filter defs
		for (i = 0; i < numSubscribedChannels; i++) {
			ChatConfigTabFilterDef *pFilterDef = eaFilterDefs[i];
			const char *pchChannel = (*peaSubscribedChannels)[i];

			estrCopy2(&pFilterDef->pchChatTabGroupName, pConfig->pchCurrentTabGroup);
			estrCopy2(&pFilterDef->pchFilterSystemName, pchChannel);
			estrCopy2(&pFilterDef->pchFilterDisplayName, pchChannel); // No translation needed

			pFilterDef->iTab = pTabGroup->iCurrentTab;
			pFilterDef->bIsChannel = true;
			pFilterDef->bMayUnsubscribe = true;
			pFilterDef->bFiltered = 
				eaFindString(&pTabConfig->eaChannels, pchChannel) >= 0;
			if (pTabConfig->eChannelFilterMode == kChatTabFilterMode_Exclusive) {
				pFilterDef->bFiltered = !pFilterDef->bFiltered;
			}
			pFilterDef->iColor = ChatCommon_GetChatColor(pConfig, kChatLogEntryType_Channel, pchChannel, ChatCommon_GetChatConfigSourceForEntity(pEntity));
			ChatCommon_SetVectorFromColor(pFilterDef->vColor, pFilterDef->iColor);
		}
	}

	if (pGen) {
		eaSortUsingKey(&eaFilterDefs, parse_ChatConfigTabFilterDef);
		ui_GenSetManagedListSafe(pGen, &eaFilterDefs, ChatConfigTabFilterDef, false);
	}
}

// Fill the gen's tab filter defs for the current tab
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_FillTabChannelFilterDefs);
void exprClientChatConfig_FillTabChannelFilterDefs(ExprContext *pContext, 
									  SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	ChatTabGroupConfig *pTabGroup = ChatCommon_GetCurrentTabGroupConfig(pConfig, ChatCommon_GetChatConfigSourceForEntity(pEntity));
	exprClientChatConfig_FillTabChannelFilterDefsForTabIndex(pContext, pGen, pEntity, SAFE_MEMBER(pTabGroup,iCurrentTab) );

}

static int ChatConfig_InputChannelComparator(const ChatConfigInputChannel **ppc1, const ChatConfigInputChannel **ppc2) {
	const ChatConfigInputChannel *pc1;
	const ChatConfigInputChannel *pc2;
	bool bC1IsLocal;
	bool bC2IsLocal;

	if (ppc1 == ppc2) {
		return 0;
	}

	if (ppc1 == NULL) {
		return -1;
	}

	if (ppc2 == NULL) {
		return 1;
	}

	pc1 = *ppc1;
	pc2 = *ppc2;

	if (pc1 == pc2) {
		return 0;
	}

	if (pc1 == NULL) {
		return -1;
	}

	if (pc2 == NULL) {
		return 1;
	}

	// We have 2 non-null input channels.  The order should be:
	//   Local
	//   All built-ins, sorted by display name
	//   All custom channels, sorted by display name

	bC1IsLocal = !stricmp_safe(pc1->pchDisplayName, "Local");
	bC2IsLocal = !stricmp_safe(pc2->pchDisplayName, "Local");

	// Test whether one of the channels is local
	if (bC1IsLocal || bC2IsLocal) {
		if (bC1IsLocal == bC2IsLocal) {
			return 0;
		}

		if (bC1IsLocal) {
			return -1;
		} else {
			return 1;
		}
	}

	// Test whether one of the channels is build in
	if (pc1->bIsBuiltIn || pc2->bIsBuiltIn) {
		if (pc1->bIsBuiltIn == pc2->bIsBuiltIn) {
			return stricmp_safe(pc1->pchDisplayName, pc2->pchDisplayName);
		}

		if (pc1->bIsBuiltIn) {
			return -1;
		} else {
			return 1;
		}

	}

	// At this point, both channels are custom, so just compare display names
	return stricmp_safe(pc1->pchDisplayName, pc2->pchDisplayName);
}

// Fill the gen's tab filter defs
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_FillPossibleInputChannels);
void exprClientChatConfig_FillPossibleInputChannels(ExprContext *pContext, 
									  SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity, bool bHideUnsubscribedChannels)
{
	ChatLogEntryType **peaiBuiltInChannels = ChatCommon_GetBuiltInChannelMessageTypes();
	const char ***peaCustomChannels = ClientChat_GetSubscribedCustomChannels();
	static ChatConfigInputChannel **eaChannels = NULL;
	int numBuiltIns = eaiSize(peaiBuiltInChannels);
	int numUnsubscribedBuiltIns = 0;
	int numCustoms = eaSize(peaCustomChannels);
	int numEntries;
	int iEntry = 0;
	int i;

	if (bHideUnsubscribedChannels) {
		for (i=0; i < eaiSize(peaiBuiltInChannels); i++) {
			ChatLogEntryType kMessageType = (*peaiBuiltInChannels)[i];
			const char *pchMessageTypeName = ChatCommon_GetMessageTypeName(kMessageType);
			const char *pchSystemName = ClientChat_GetSubscribedChannelSystemName(pchMessageTypeName);

			if (!pchSystemName) {
				// This built-in isn't currently subscribed to, but 
				// we want to list it, so use the message type name.
				pchSystemName = pchMessageTypeName;
			}

			if (!ClientChat_IsCurrentlySubscribedChannel(pchSystemName)) {
				numUnsubscribedBuiltIns++;
			}
		}
	}

	numEntries = numBuiltIns + numCustoms + 1 - numUnsubscribedBuiltIns;

	// Make sure space is allocated for all rows
	while (eaSize(&eaChannels) < numEntries) {
		ChatConfigInputChannel *pInputChannel = StructCreate(parse_ChatConfigInputChannel);
		eaPush(&eaChannels, pInputChannel);
	}

	assertmsg(numEntries, "There should always be at least one entry, for the local channel.");

	while (eaSize(&eaChannels) > numEntries) {
		StructDestroy(parse_ChatConfigInputChannel, eaPop(&eaChannels));
	}

	// First add local
	{
		ChatConfigInputChannel *pInputChannel = eaChannels[iEntry];
		ClientChat_FillInputChannelInput(pInputChannel, LOCAL_CHANNEL_NAME, true);
		iEntry++;
	}

	// Add all built-in channels
	for (i=0; i < eaiSize(peaiBuiltInChannels); i++) {
		ChatLogEntryType kMessageType = (*peaiBuiltInChannels)[i];
		const char *pchMessageTypeName = ChatCommon_GetMessageTypeName(kMessageType);
		const char *pchSystemName = ClientChat_GetSubscribedChannelSystemName(pchMessageTypeName);

		if (!pchSystemName) {
			// This built-in isn't currently subscribed to, but 
			// we want to list it, so use the message type name.
			pchSystemName = pchMessageTypeName;
		}

		if (!bHideUnsubscribedChannels || ClientChat_IsCurrentlySubscribedChannel(pchSystemName)) {
			ChatConfigInputChannel *pInputChannel = eaChannels[iEntry];
			ClientChat_FillInputChannelInput(pInputChannel, pchSystemName, false);
			iEntry++;
		}
	}

	// Add all custom channel subscriptions
	for (i=0; i < eaSize(peaCustomChannels); i++, iEntry++) {
		ChatConfigInputChannel *pInputChannel = eaChannels[iEntry];
		const char *pchCustomChannel = (*peaCustomChannels)[i];
		ClientChat_FillInputChannelInput(pInputChannel, pchCustomChannel, false);
	}

	if (pGen) {
		// Sort the inputs
		eaQSort(eaChannels, ChatConfig_InputChannelComparator);
		ui_GenSetManagedListSafe(pGen, &eaChannels, ChatConfigInputChannel, false);
	}
}

static void ClientChatConfig_SetFilter(ChatConfig *pConfig, const char *pchTabGroupName, S32 iTab, const char *pchName, bool bFiltered, bool bIsChannel)
{
	if (bIsChannel)
	{
		if (ClientChatConfig_UsingEntityConfig())
			ServerCmd_cmdServerChatConfig_SetChannelFilter(pchTabGroupName, iTab, pchName, bFiltered);
		else
		{
			ChatConfigCommon_SetChannelFilter(CONTAINER_NOCONST(ChatConfig, pConfig), pchTabGroupName, iTab, pchName, bFiltered);
			GServerCmd_crChatConfig_SetChannelFilter(GLOBALTYPE_CHATRELAY, pchTabGroupName, iTab, pchName, bFiltered);
		}
	}
	else
	{
		if (ClientChatConfig_UsingEntityConfig())
			ServerCmd_cmdServerChatConfig_SetMessageTypeFilter(pchTabGroupName, iTab, pchName, bFiltered);
		else
		{
			ChatConfigCommon_SetMessageTypeFilter(CONTAINER_NOCONST(ChatConfig, pConfig), pchTabGroupName, iTab, pchName, bFiltered);
			GServerCmd_crChatConfig_SetMessageTypeFilter(GLOBALTYPE_CHATRELAY, pchTabGroupName, iTab, pchName, bFiltered);
		}
	}
}

// Set the filtering of a message type for the current tab.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_SetMessageTypeFilterForTabIndex);
void exprClientChatConfigSetMessageTypeFilterForTabIndex(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, const char *pchTypeName, bool bFiltered, S32 iTab)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	ChatTabGroupConfig *pTabGroup = ChatCommon_GetCurrentTabGroupConfig(pConfig, ChatCommon_GetChatConfigSourceForEntity(pEntity));
	if (pTabGroup)
	{
		ClientChatConfig_SetFilter(pConfig, pTabGroup->pchName, iTab, pchTypeName, bFiltered, false);
	}
}


// Set the filtering of a message type for the current tab.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_SetMessageTypeFilter);
void exprClientChatConfigSetMessageTypeFilter(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, const char *pchTypeName, bool bFiltered)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	ChatTabGroupConfig *pTabGroup = ChatCommon_GetCurrentTabGroupConfig(pConfig, ChatCommon_GetChatConfigSourceForEntity(pEntity));
	if (pTabGroup)
	{
		ClientChatConfig_SetFilter(pConfig, pTabGroup->pchName, pTabGroup->iCurrentTab, pchTypeName, bFiltered, false);
	}
}

// Set the filtering of a channel for the specified tab.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_SetChannelFilterForTabIndex);
void exprClientChatConfigSetChannelFilterForTabIndex(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, const char *pchChannel, bool bFiltered, S32 iTab)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	ChatTabGroupConfig *pTabGroup = ChatCommon_GetCurrentTabGroupConfig(pConfig, ChatCommon_GetChatConfigSourceForEntity(pEntity));
	if (pTabGroup)
	{
		ClientChatConfig_SetFilter(pConfig, pTabGroup->pchName, iTab, pchChannel, bFiltered, true);
	}
}

// Set the filtering of a channel for the current tab.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_SetChannelFilter);
void exprClientChatConfigSetChannelFilter(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, const char *pchChannel, bool bFiltered)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	ChatTabGroupConfig *pTabGroup = ChatCommon_GetCurrentTabGroupConfig(pConfig, ChatCommon_GetChatConfigSourceForEntity(pEntity));
	if (pTabGroup)
	{
		ClientChatConfig_SetFilter(pConfig, pTabGroup->pchName, pTabGroup->iCurrentTab, pchChannel, bFiltered, true);
	}
}

// Set the filtering of a channel for the specified tab.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ChatConfig_SetAllFiltersForTabIndex);
void exprClientChatConfigSetAllFiltersForTabIndex(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, bool bFiltered, S32 iTab)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	ChatTabGroupConfig *pTabGroup = ChatCommon_GetCurrentTabGroupConfig(pConfig, ChatCommon_GetChatConfigSourceForEntity(pEntity));
	if (pTabGroup)
	{
		if (ClientChatConfig_UsingEntityConfig())
			ServerCmd_cmdServerChatConfig_SetAllFilters(pTabGroup->pchName, iTab, bFiltered);
		else
			GServerCmd_crChatConfig_SetAllFilters(GLOBALTYPE_CHATRELAY, pTabGroup->pchName, iTab, bFiltered);
	}
}

// Set the filtering of a channel for the current tab.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ChatConfig_SetAllFilters);
void exprClientChatConfigSetAllFilters(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, bool bFiltered)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	ChatTabGroupConfig *pTabGroup = ChatCommon_GetCurrentTabGroupConfig(pConfig, ChatCommon_GetChatConfigSourceForEntity(pEntity));
	if (pTabGroup)
	{
		if (ClientChatConfig_UsingEntityConfig())
			ServerCmd_cmdServerChatConfig_SetAllFilters(pTabGroup->pchName, pTabGroup->iCurrentTab, bFiltered);
		else
			GServerCmd_crChatConfig_SetAllFilters(GLOBALTYPE_CHATRELAY, pTabGroup->pchName, pTabGroup->iCurrentTab, bFiltered);
	}
}

// Unsubscribe from a channel
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_ChannelUnsubscribe);
void exprClientChatConfigChannelUnsubscribe(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, const char *pchName)
{
#if USE_CHATRELAY
	GServerCmd_crLeaveChannel(GLOBALTYPE_CHATRELAY, pchName);
#else
	ServerCmd_cmdServerChat_LeaveChannel(pchName);
#endif
}

// Subscribe to a channel
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_ChannelSubscribe);
void exprClientChatConfigChannelSubscribe(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, const char *pchName)
{
#ifdef USE_CHATRELAY
	GServerCmd_crJoinChannel(GLOBALTYPE_CHATRELAY, pchName);
#else
	ServerCmd_cmdServerChat_JoinChannel(pchName);
#endif
}

// Create a new tab with the default settings.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_CreateTab);
void exprClientChatConfigCreateTab(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	if (pConfig)
	{
		if (ClientChatConfig_UsingEntityConfig())
			ServerCmd_cmdServerChatConfig_CreateTab(pConfig->pchCurrentTabGroup);
		else
			GServerCmd_crChatConfig_CreateTab(GLOBALTYPE_CHATRELAY, pConfig->pchCurrentTabGroup);
	}
}

// Create a new tab with the specified tab's settings.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_CopyTabForTabIndex);
void exprClientChatConfigCopyTabForTabIndex(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, S32 iTab)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	ChatTabGroupConfig *pTabGroup = ChatCommon_GetCurrentTabGroupConfig(pConfig, ChatCommon_GetChatConfigSourceForEntity(pEntity));
	if (pTabGroup)
	{
		if (ClientChatConfig_UsingEntityConfig())
			ServerCmd_cmdServerChatConfig_CopyTabEx(pTabGroup->pchName, NULL, pTabGroup->pchName, iTab);
		else
			GServerCmd_crChatConfig_CopyTabEx(GLOBALTYPE_CHATRELAY, pTabGroup->pchName, NULL, pTabGroup->pchName, iTab);
	}
}

// Create a new tab with the current tab's settings.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_CopyTab);
void exprClientChatConfigCopyTab(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	ChatTabGroupConfig *pTabGroup = ChatCommon_GetCurrentTabGroupConfig(pConfig, ChatCommon_GetChatConfigSourceForEntity(pEntity));
	if (pTabGroup)
	{
		if (ClientChatConfig_UsingEntityConfig())
			ServerCmd_cmdServerChatConfig_CopyTabEx(pTabGroup->pchName, NULL, pTabGroup->pchName, pTabGroup->iCurrentTab);
		else
			GServerCmd_crChatConfig_CopyTabEx(GLOBALTYPE_CHATRELAY, pTabGroup->pchName, NULL, pTabGroup->pchName, pTabGroup->iCurrentTab);
	}
}

// Delete the specified tab.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_DeleteTabForTabIndex);
void exprClientChatConfigDeleteTabForTabIndex(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, S32 iTab)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	ChatTabGroupConfig *pTabGroup = ChatCommon_GetCurrentTabGroupConfig(pConfig, ChatCommon_GetChatConfigSourceForEntity(pEntity));
	if (pTabGroup)
	{
		if (ClientChatConfig_UsingEntityConfig())
			ServerCmd_cmdServerChatConfig_DeleteTab(pTabGroup->pchName, iTab);
		else
			GServerCmd_crChatConfig_DeleteTab(GLOBALTYPE_CHATRELAY, pTabGroup->pchName, iTab);
	}
}

// Delete the current tab.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_DeleteTab);
void exprClientChatConfigDeleteTab(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	ChatTabGroupConfig *pTabGroup = ChatCommon_GetCurrentTabGroupConfig(pConfig, ChatCommon_GetChatConfigSourceForEntity(pEntity));
	if (pTabGroup)
	{
		if (ClientChatConfig_UsingEntityConfig())
			ServerCmd_cmdServerChatConfig_DeleteTab(pTabGroup->pchName, pTabGroup->iCurrentTab);
		else
			GServerCmd_crChatConfig_DeleteTab(GLOBALTYPE_CHATRELAY, pTabGroup->pchName, pTabGroup->iCurrentTab);
	}
}

// Reset the player's entire chat configuration.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatConfig_Reset);
void exprClientChatConfigReset(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	if (ClientChatConfig_UsingEntityConfig())
		ServerCmd_cmdServerChatConfig_Reset();
	else
		GServerCmd_crChatConfig_Reset(GLOBALTYPE_CHATRELAY);
}

// Sets whether chat log entries should include date stamps
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("chat_SetShowDate");
void exprClientChatShowDate(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, bool bShowDate)
{
	if (ClientChatConfig_UsingEntityConfig())
		ServerCmd_Chat_ShowDate(bShowDate);
	else
	{
		NOCONST(ChatConfig) *pConfig = ClientChatConfig_GetEditableConfig();
		if (pConfig)
			pConfig->bShowDate = bShowDate;
		GServerCmd_crChatConfig_SetShowDate(GLOBALTYPE_CHATRELAY, bShowDate);
	}
}

// Sets whether chat log entries should include time stamps
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("chat_SetShowTime");
void exprClientChatShowTime(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, bool bShowTime)
{
	if (ClientChatConfig_UsingEntityConfig())
		ServerCmd_Chat_ShowTime(bShowTime);
	else
	{
		NOCONST(ChatConfig) *pConfig = ClientChatConfig_GetEditableConfig();
		if (pConfig)
			pConfig->bShowTime = bShowTime;
		GServerCmd_crChatConfig_SetShowTime(GLOBALTYPE_CHATRELAY, bShowTime);
	}
}

// Sets whether chat log entries should always show message types
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("chat_SetShowMessageTypeNames");
void exprClientChatShowMessageTypeNames(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, bool bShowMessageTypeNames)
{
	if (ClientChatConfig_UsingEntityConfig())
		ServerCmd_Chat_ShowMessageTypeNames(bShowMessageTypeNames);
	else
	{
		NOCONST(ChatConfig) *pConfig = ClientChatConfig_GetEditableConfig();
		if (pConfig)
			pConfig->bShowMessageTypeNames = bShowMessageTypeNames;
		GServerCmd_crChatConfig_SetShowMessageTypeNames(GLOBALTYPE_CHATRELAY, bShowMessageTypeNames);
	}
}

// Sets whether chat log entries should always show full names
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("chat_SetShowFullNames");
void exprClientChatShowFullNames(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, bool bShowFullNames)
{
	if (ClientChatConfig_UsingEntityConfig())
		ServerCmd_Chat_ShowFullNames(bShowFullNames);
	else
	{
		NOCONST(ChatConfig) *pConfig = ClientChatConfig_GetEditableConfig();
		if (pConfig)
			pConfig->bHideAccountNames = !bShowFullNames;
		GServerCmd_crChatConfig_SetShowFullNames(GLOBALTYPE_CHATRELAY, bShowFullNames);
	}
}

// Sets whether chat log entries should be filtered for profanity
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Chat_SetProfanityFilter");
void exprClientChatProfanityFilter(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, bool bProfanityFilter)
{
	if (ClientChatConfig_UsingEntityConfig())
		ServerCmd_Chat_ProfanityFilter(bProfanityFilter);
	else
	{
		NOCONST(ChatConfig) *pConfig = ClientChatConfig_GetEditableConfig();
		if (pConfig)
			pConfig->bProfanityFilter = bProfanityFilter;
		GServerCmd_crChatConfig_SetProfanityFilter(GLOBALTYPE_CHATRELAY, bProfanityFilter);
	}
}

// Sets whether chat log entries should be filtered for profanity
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Chat_SetShowChannelNames");
void exprClientChatShowChannelNames(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, bool bShowChannelNames)
{
	if (ClientChatConfig_UsingEntityConfig())
		ServerCmd_Chat_ShowChannelNames(bShowChannelNames);
	else
	{
		NOCONST(ChatConfig) *pConfig = ClientChatConfig_GetEditableConfig();
		if (pConfig)
			pConfig->bShowChannelNames = bShowChannelNames;
		GServerCmd_crChatConfig_SetShowChannelNames(GLOBALTYPE_CHATRELAY, bShowChannelNames);
	}
}
// Sets whether the user wants to subscribe to the Shard Global channel or not
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Chat_SetGlobalChannelSubscription");
void exprClientChatGlobalChannelSubscribe(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, bool bSubscribe)
{
	if (ClientChatConfig_UsingEntityConfig())
		ServerCmd_Chat_GlobalChannelSubscribe(bSubscribe);
}

// Sets whether chat log entries should include date stamps
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("chat_SetAnnotateAutoComplete");
void exprClientChatSetAnnotateAutoComplete(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, bool bAnnotateAutoComplete)
{
	if (ClientChatConfig_UsingEntityConfig())
		ServerCmd_Chat_SetAnnotateAutoComplete(bAnnotateAutoComplete);
	else
	{
		NOCONST(ChatConfig) *pConfig = ClientChatConfig_GetEditableConfig();
		if (pConfig)
			pConfig->bAnnotateAutoComplete = bAnnotateAutoComplete;
		GServerCmd_crChatConfig_SetAnnotateAutoComplete(GLOBALTYPE_CHATRELAY, bAnnotateAutoComplete);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(chat_getshowdate);
bool gclChatConfig_GetShowDate(SA_PARAM_OP_VALID Entity* pEntity)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	return ( pConfig && pConfig->bShowDate );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(chat_getshowtime);
bool gclChatConfig_GetShowTime(SA_PARAM_OP_VALID Entity* pEntity)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	return ( pConfig && pConfig->bShowTime );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(chat_getshowmessagenametype);
bool gclChatConfig_GetShowMessageNameType(SA_PARAM_OP_VALID Entity* pEntity)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	return ( pConfig && pConfig->bShowMessageTypeNames );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(chat_getshowfullnames);
bool gclChatConfig_GetShowAccountNames(SA_PARAM_OP_VALID Entity* pEntity)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	return ( pConfig && !pConfig->bHideAccountNames );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(chat_getprofanityfilter);
bool gclChatConfig_GetProfanityFilter(SA_PARAM_OP_VALID Entity* pEntity)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	return ( pConfig && pConfig->bProfanityFilter );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(chat_getshowchannelnames);
bool gclChatConfig_GetShowChannelNames(SA_PARAM_OP_VALID Entity* pEntity)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	return ( pConfig && pConfig->bShowChannelNames );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(FilterProfanity);
const char *gclChatConfig_FilterProfanity(SA_PARAM_OP_VALID Entity* pEntity, const char *pchText)
{
	static char *pchFilteredText = NULL;
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);

	// Filter if the player wants filtering or if we don't know whether or not to filter
	bool bFilter = !pConfig || pConfig->bProfanityFilter;

	if (bFilter && pchText && *pchText) {
		estrCopy2(&pchFilteredText, pchText);
		ReplaceAnyWordProfane(pchFilteredText);
		return pchFilteredText;
	}

	return pchText;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_GetAnnotateAutoComplete);
bool gclChatConfig_GetAnnotateAutoComplete(SA_PARAM_OP_VALID Entity* pEntity)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	return ( pConfig && pConfig->bAnnotateAutoComplete);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_SetFontScale);
void gclChatConfig_SetFontScale(SA_PARAM_OP_VALID Entity *pEntity, F32 fFontScale)
{
	if (ClientChatConfig_UsingEntityConfig())
		ServerCmd_Chat_SetFontScale(fFontScale);
	else
	{
		NOCONST(ChatConfig) *pConfig = ClientChatConfig_GetEditableConfig();
		if (pConfig)
			pConfig->fFontScale = fFontScale;
		GServerCmd_crChatConfig_SetFontScale(GLOBALTYPE_CHATRELAY, fFontScale);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_GetFontScale);
F32 gclChatConfig_GetFontScale(SA_PARAM_OP_VALID Entity *pEntity)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	return pConfig ? pConfig->fFontScale : 1.0f;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_SetActiveWindowAlpha);
void gclChatConfig_SetActiveWindowAlpha(SA_PARAM_OP_VALID Entity *pEntity, F32 fActiveWindowAlpha)
{
	if (ClientChatConfig_UsingEntityConfig())
		ServerCmd_Chat_SetActiveWindowAlpha(fActiveWindowAlpha);
	else
	{
		NOCONST(ChatConfig) *pConfig = ClientChatConfig_GetEditableConfig();
		if (pConfig)
			pConfig->fActiveWindowAlpha = fActiveWindowAlpha;
		GServerCmd_crChatConfig_SetActiveWindowAlpha(GLOBALTYPE_CHATRELAY, fActiveWindowAlpha);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_GetActiveWindowAlpha);
F32 gclChatConfig_GetActiveWindowAlpha(SA_PARAM_OP_VALID Entity *pEntity)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	return pConfig ? pConfig->fActiveWindowAlpha : 0.9f;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_SetInactiveWindowAlpha);
void gclChatConfig_SetInactiveWindowAlpha(SA_PARAM_OP_VALID Entity *pEntity, F32 fInactiveWindowAlpha)
{
	if (ClientChatConfig_UsingEntityConfig())
		ServerCmd_Chat_SetInactiveWindowAlpha(fInactiveWindowAlpha);
	else
	{
		NOCONST(ChatConfig) *pConfig = ClientChatConfig_GetEditableConfig();
		if (pConfig)
			pConfig->fInactiveWindowAlpha = fInactiveWindowAlpha;
		GServerCmd_crChatConfig_SetInactiveWindowAlpha(GLOBALTYPE_CHATRELAY, fInactiveWindowAlpha);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_GetInactiveWindowAlpha);
F32 gclChatConfig_GetInactiveWindowAlpha(SA_PARAM_OP_VALID Entity *pEntity)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	return pConfig ? pConfig->fInactiveWindowAlpha : 0.0f;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_SetFadeAwayDuration);
void gclChatConfig_SetFadeAwayDuration(SA_PARAM_OP_VALID Entity *pEntity, F32 fFadeAwayDuration)
{
	NOCONST(ChatConfig) *pChatConfig = ClientChatConfig_GetEditableConfig();
	// HACK: We need the UI to get the change immediately
	if (pChatConfig)
		ChatConfigCommon_SetFadeAwayDuration(pChatConfig, fFadeAwayDuration);

	if (ClientChatConfig_UsingEntityConfig())
		ServerCmd_Chat_SetFadeAwayDuration(fFadeAwayDuration);
	else
		GServerCmd_crChatConfig_SetFadeAwayDuration(GLOBALTYPE_CHATRELAY, fFadeAwayDuration);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_GetFadeAwayDuration);
F32 gclChatConfig_GetFadeAwayDuration(SA_PARAM_OP_VALID Entity *pEntity)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	return pConfig ? (F32)((F32)pConfig->siFadeAwayDuration / 1000.0f) : 0.0f;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_SetTimeRequiredToStartFading);
void gclChatConfig_SetTimeRequiredToStartFading(SA_PARAM_OP_VALID Entity *pEntity, F32 fTimeRequiredToStartFading)
{
	NOCONST(ChatConfig) *pChatConfig = ClientChatConfig_GetEditableConfig();
	// HACK: We need the UI to get the change immediately
	if (pChatConfig)
		ChatConfigCommon_SetTimeRequiredToStartFading(pChatConfig, fTimeRequiredToStartFading);
	if (ClientChatConfig_UsingEntityConfig())
		ServerCmd_Chat_SetTimeRequiredToStartFading(fTimeRequiredToStartFading);
	else
		GServerCmd_crChatConfig_SetTimeRequiredToStartFading(GLOBALTYPE_CHATRELAY, fTimeRequiredToStartFading);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_GetTimeRequiredToStartFading);
F32 gclChatConfig_GetTimeRequiredToStartFading(SA_PARAM_OP_VALID Entity *pEntity)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	return pConfig ? (F32)((F32)pConfig->siTimeRequiredToStartFading / 1000.0f) : 0.0f;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_GetStatus);
const char *gclChatConfig_GetStatus(SA_PARAM_OP_VALID Entity *pEntity)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	return pConfig ? pConfig->pchStatusMessage : "";
}

// Returns a color that is fPercent as bright as the given color.
// i.e. if fPercent=1, then the given color is returned, if fPercent=0
// you get black.
static Color darker(Color color, F32 fPercent)
{
	color.r = CLAMP(color.r * fPercent, 0, 255);
	color.g = CLAMP(color.g * fPercent, 0, 255);
	color.b = CLAMP(color.b * fPercent, 0, 255);
	return color;
}

// Returns a color that is fPercent brighter than the given color.
// If fPercent=1, you'll get white.  If fPercent=0, you'll get the 
// given color.
static Color lighter(Color color, F32 fPercent)
{
	color.r = CLAMP(color.r + (255-color.r) * fPercent, 0, 255);
	color.g = CLAMP(color.g + (255-color.g) * fPercent, 0, 255);
	color.b = CLAMP(color.b + (255-color.b) * fPercent, 0, 255);
	return color;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Chat_FillColorModel");
void ClientChat_FillColorModel(SA_PARAM_NN_VALID ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, U32 iGradients)
{
	static Color iaBaseColors[] = {
		{0x7f, 0x7f, 0x7f, 0xff}, // Gray
		{0xff, 0x00, 0x00, 0xff}, // Red to green 
  			{0xff, 0x7f, 0x7f, 0xff}, // lighter tone
		{0xff, 0x7f, 0x00, 0xff}, 
		{0xff, 0xff, 0x00, 0xff}, 
			{0xff, 0xff, 0x7f, 0xff}, // lighter tone
		{0x7f, 0xff, 0x00, 0xff},
		{0x00, 0xff, 0x00, 0xff}, // Green to blue 
			{0x7f, 0xff, 0x7f, 0xff}, // lighter tone
		{0x00, 0xff, 0x7f, 0xff}, 
		{0x00, 0xff, 0xff, 0xff}, 
			{0x7f, 0xff, 0xff, 0xff}, // lighter tone
		{0x00, 0x7f, 0xff, 0xff},
		{0x00, 0x00, 0xff, 0xff}, // Blue to red 
			{0x7f, 0x7f, 0xff, 0xff}, // lighter tone
		{0x7f, 0x00, 0xff, 0xff}, 
		{0xff, 0x00, 0xff, 0xff}, 
			{0xff, 0x7f, 0xff, 0xff}, // lighter tone
		{0xff, 0x00, 0x7f, 0xff},
	};
	static const S32 iBaseColorCount = sizeof(iaBaseColors)/sizeof(Color);

	int **peaiColorList = ui_GenGetColorList(pGen);
	S32 i, j;
	S32 iLightGradients = iGradients / 2;
	// We need room for the primary row, so we take one away from the dark gradients
	// if we have an even number of gradients.
	S32 iDarkGradients = (iGradients-1) / 2;

	eaiClear(peaiColorList);
	// Create gradient rows from White to base color
	for (i=iLightGradients; i > 0; i--) {
		for (j=0; j < iBaseColorCount; j++) {
			Color iBaseColor = iaBaseColors[j];
			// The +1 below forces non-perfect whiteness
			Color color = lighter(iBaseColor, (F32)i/(F32)(iLightGradients+1));
			U32 iColor = color.r << 24 | color.g << 16 | color.b << 8 | color.a;
			eaiPush(peaiColorList, iColor);
		}
	}

	// Create base color row
	for (j=0; j < iBaseColorCount; j++) {
		Color iBaseColor = iaBaseColors[j];
		U32 iColor = iBaseColor.r << 24 | iBaseColor.g << 16 | iBaseColor.b << 8 | iBaseColor.a;
		eaiPush(peaiColorList, iColor);
	}

	// Create gradients rows from base color to black
	for (i=iDarkGradients-1; i >= 0; i--) {
		for (j=0; j < iBaseColorCount; j++) {
			Color iBaseColor = iaBaseColors[j];
			// The +1's below forces non-perfect darkness
			Color color = darker(iBaseColor, (F32)(i+1)/(F32)(iDarkGradients+1)); 
			U32 iColor = color.r << 24 | color.g << 16 | color.b << 8 | color.a;
			eaiPush(peaiColorList, iColor);
		}
	}

	// Force gray scale endpoints to be perfect white & perfect black
	(*peaiColorList)[0] = 0xffffffff;
	(*peaiColorList)[(iGradients-1)*iBaseColorCount] = 0x000000ff;
}

//
// New Chat interface
//

SA_RET_OP_VALID static ChatTabConfig *ClientChatGetTab(SA_PARAM_OP_VALID ChatTabGroupConfig *pWindowSettings, const char *pchTitle)
{
	S32 i = ChatCommon_FindTabConfigIndex(pWindowSettings, pchTitle);
	if (i >= 0 && pWindowSettings)
		return pWindowSettings->eaChatTabConfigs[i];
	return NULL;
}

// Gets the list of all chat windows
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatGetWindows);
void exprClientChatGetWindows(SA_PARAM_NN_VALID UIGen *pGen)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(entActivePlayerPtr());
	ChatTabGroupConfig ***peaWindows = ui_GenGetManagedListSafe(pGen, ChatTabGroupConfig);
	if (pConfig)
		eaCopy(peaWindows, &pConfig->eaChatTabGroupConfigs);
	else
		eaSetSize(peaWindows, 0);
	ui_GenSetManagedListSafe(pGen, peaWindows, ChatTabGroupConfig, false);
}

// Deprecated, this is to allow the old chat window settings be used with the new chat window.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatGetWindowInfo);
SA_RET_OP_VALID ChatTabGroupConfig *exprClientChatGetWindowInfo(const char *pchName)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(entActivePlayerPtr());
	ChatTabGroupConfig *pTabConfig;
	pTabConfig = pConfig && pchName && *pchName ? eaIndexedGetUsingString(&pConfig->eaChatTabGroupConfigs, pchName) : NULL;
	return pTabConfig;
}

// Deprecated, this is to allow the old chat window settings be used with the new chat window.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenChatGetWindowInfo);
SA_RET_OP_VALID ChatTabGroupConfig *exprClientGenChatGetWindowInfo(UIGen *pGen, const char *pchName)
{
	ChatTabGroupConfig *pTabConfig = exprClientChatGetWindowInfo(pchName);
	ui_GenSetPointer(pGen, pTabConfig, parse_ChatTabGroupConfig);
	return pTabConfig;
}

// Get the chat window settings for the window id.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatGetWindowSettings);
SA_RET_NN_VALID ChatTabGroupConfig *exprClientChatGetWindowSettings(S32 iWindow)
{
	static U32 s_uiEntIDFakes;
	static ChatTabGroupConfig **s_eaFakes;
	Entity *pEnt = entActivePlayerPtr();
	U32 uiEntID = pEnt ? entGetContainerID(pEnt) : 0;
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEnt);
	char achName[30];
	ChatTabGroupConfig *pTabConfig;

	sprintf(achName, "%d", iWindow);	
	pTabConfig = pConfig ? eaIndexedGetUsingString(&pConfig->eaChatTabGroupConfigs, achName) : NULL;

	// If the tab config doesn't exist, create a fake on the client until one exists on the server.
	if (!pTabConfig)
	{
		if (!s_eaFakes)
		{
			eaCreate(&s_eaFakes);
			eaIndexedEnable(&s_eaFakes, parse_ChatTabGroupConfig);
		}
		else if (s_uiEntIDFakes != uiEntID)
		{
			eaClearStruct(&s_eaFakes, parse_ChatTabGroupConfig);
			s_uiEntIDFakes = uiEntID;
		}

		pTabConfig = eaIndexedGetUsingString(&s_eaFakes, achName);
		if (!pTabConfig)
		{
			pTabConfig = StructCreate(parse_ChatTabGroupConfig);
			estrCopy2(&CONTAINER_NOCONST(ChatTabGroupConfig, pTabConfig)->pchName, achName);
			eaPush(&s_eaFakes, pTabConfig);
			if (ClientChatConfig_UsingEntityConfig())
				ServerCmd_cmdServerChatConfig_CreateTabGroup(pTabConfig->pchName, false);
			else
				GServerCmd_crChatConfig_CreateTabGroup(GLOBALTYPE_CHATRELAY, pTabConfig->pchName, false);
		}
	}
	else
	{
		ChatTabGroupConfig *pFakeTabConfig;
		if ((pFakeTabConfig = eaIndexedGetUsingString(&s_eaFakes, achName)) != NULL)
		{
			eaFindAndRemove(&s_eaFakes, pFakeTabConfig);
			StructDestroy(parse_ChatTabGroupConfig, pFakeTabConfig);
		}
	}

	return pTabConfig;
}

// Like ChatGetWindowSettings, except it also sets the GenData of the Gen to the chat window settings.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenChatGetWindowSettings);
SA_RET_OP_VALID ChatTabGroupConfig *exprClientChatGenGetWindowSettings(SA_PARAM_NN_VALID UIGen *pGen, S32 iWindow)
{
	ChatTabGroupConfig *pTabConfig = exprClientChatGetWindowSettings(iWindow);
	ui_GenSetPointer(pGen, pTabConfig, parse_ChatTabGroupConfig);
	return pTabConfig;
}

// Delete the chat window.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatWindowDelete);
void exprClientChatDelete(SA_PARAM_OP_VALID ChatTabGroupConfig *pWindowSettings)
{
	if (pWindowSettings)
	{
		if (ClientChatConfig_UsingEntityConfig())
			ServerCmd_cmdServerChatConfig_DeleteTabGroup(pWindowSettings->pchName);
		else
			GServerCmd_crChatConfig_DeleteTabGroup(GLOBALTYPE_CHATRELAY, pWindowSettings->pchName);
	}
}

// Reset the chat window.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatWindowReset);
void exprClientChatReset(SA_PARAM_OP_VALID ChatTabGroupConfig *pWindowSettings)
{
	if (pWindowSettings)
	{
		if (ClientChatConfig_UsingEntityConfig())
			ServerCmd_cmdServerChatConfig_ResetTabGroup(pWindowSettings->pchName);
		else
			GServerCmd_crChatConfig_ResetTabGroup(GLOBALTYPE_CHATRELAY, pWindowSettings->pchName);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatTabGroupConfigReset);
void exprClientChatTabGroupConfigReset(SA_PARAM_OP_VALID const char * pchName)
{
	if( pchName )
	{
		if (ClientChatConfig_UsingEntityConfig())
			ServerCmd_cmdServerChatConfig_ResetTabGroup(pchName);
		else
			GServerCmd_crChatConfig_ResetTabGroup(GLOBALTYPE_CHATRELAY, pchName);
	}
}

// Count the chat tabs for the chat window settings.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatWindowCountTabs);
S32 exprClientChatCountTabs(SA_PARAM_OP_VALID ChatTabGroupConfig *pWindowSettings)
{
	return pWindowSettings ? eaSize(&pWindowSettings->eaChatTabConfigs) : 0;
}

// Get the list of tabs from the chat window settings.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatWindowGetTabs);
void exprClientChatGetTabs(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID ChatTabGroupConfig *pWindowSettings)
{
	// TODO: Use a better way to deconst the array
	ChatTabConfig ***peaChatTabConfigs = (ChatTabConfig ***)&pWindowSettings->eaChatTabConfigs;
	ui_GenSetManagedListSafe(pGen, peaChatTabConfigs, ChatTabConfig, false);
}

// Get the current speaking chat channel. If there is no speaking channel, then it will return NULL.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatWindowGetTabChannelInfo);
SA_RET_OP_VALID ChatConfigInputChannel *exprClientChatGetChannelInfo(SA_PARAM_OP_VALID ChatTabGroupConfig *pWindowSettings, const char *pchTitle)
{
	static ChatConfigInputChannel **s_eaInputChannels;
	ChatTabConfig *pTab = ClientChatGetTab(pWindowSettings, pchTitle);
	if (pTab && pTab->pchDefaultChannel)
	{
		ChatConfigInputChannel *pChannel = NULL;
		S32 i;
		for (i = eaSize(&s_eaInputChannels) - 1; i >= 0; i--)
		{
			if (!stricmp(s_eaInputChannels[i]->pchSystemName, pTab->pchDefaultChannel))
			{
				pChannel = s_eaInputChannels[i];
				break;
			}
		}
		if (!pChannel)
		{
			pChannel = StructCreate(parse_ChatConfigInputChannel);
			eaPush(&s_eaInputChannels, pChannel);
		}
		if (pChannel->uLastUpdateTime != gGCLState.totalElapsedTimeMs)
			ClientChat_FillInputChannelInput(pChannel, pTab->pchDefaultChannel, false);
		return pChannel;
	}
	return NULL;
}

// Get the display name of the current speaking chat channel. If there is no speaking channel, then it will return "".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatWindowGetTabChannel);
const char *exprClientChatGetChannel(SA_PARAM_OP_VALID ChatTabGroupConfig *pWindowSettings, const char *pchTitle)
{
	ChatConfigInputChannel *pChannel = exprClientChatGetChannelInfo(pWindowSettings, pchTitle);
	if (pChannel)
		return pChannel->pchDisplayName;
	return "";
}

// Delete the requested tab from the chat window settings
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatWindowDeleteTab);
bool exprClientChatDeleteTab(SA_PARAM_OP_VALID ChatTabGroupConfig *pWindowSettings, const char *pchTitle)
{
	S32 i = ChatCommon_FindTabConfigIndex(pWindowSettings, pchTitle);
	if (i >= 0 && pWindowSettings)
	{
		if (ClientChatConfig_UsingEntityConfig())
			ServerCmd_cmdServerChatConfig_DeleteTab(pWindowSettings->pchName, i);
		else
			GServerCmd_crChatConfig_DeleteTab(GLOBALTYPE_CHATRELAY, pWindowSettings->pchName, i);
		return true;
	}
	return false;
}

// Delete the requested tab from the chat window settings
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatWindowRenameTab);
bool exprClientChatRenameTab(SA_PARAM_OP_VALID ChatTabGroupConfig *pWindowSettings, const char *pchNewName, const char *pchOldName)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(entActivePlayerPtr());
	S32 i = ChatCommon_FindTabConfigIndex(pWindowSettings, pchOldName);
	S32 j;

	if (!pchNewName || !*pchNewName)
	{
		return false;
	}

	if (i >= 0 && pConfig && pWindowSettings)
	{
		// Cannot rename to an existing tab name
		char *estrNewName = NULL;
		S32 iNewIndex = 1;
		estrStackCreate(&estrNewName);
		do {
			// create a new name
			if (iNewIndex > 1)
				estrPrintf(&estrNewName, "%s%d", pchNewName, iNewIndex);
			else
				estrCopy2(&estrNewName, pchNewName);

			for (j = eaSize(&pConfig->eaChatTabGroupConfigs) - 1; j >= 0; j--)
			{
				ChatTabGroupConfig *pWindow = pConfig->eaChatTabGroupConfigs[j];
				if (ChatCommon_FindTabConfigIndex(pWindow, estrNewName) >= 0)
				{
					break;
				}
			}

			// Found a unique name
			if (j < 0)
				break;

			iNewIndex++;
		} while (true);
		if (ClientChatConfig_UsingEntityConfig())
			ServerCmd_cmdServerChatConfig_SetTabName(pWindowSettings->pchName, i, estrNewName);
		else
		{
			ChatConfigCommon_SetTabName(CONTAINER_NOCONST(ChatConfig, pConfig), pWindowSettings->pchName, i, estrNewName);
			GServerCmd_crChatConfig_SetTabName(GLOBALTYPE_CHATRELAY, pWindowSettings->pchName, i, estrNewName);
		}
		estrDestroy(&estrNewName);
		return true;
	}
	return false;
}

// Create a new tab with the settings from an existing tab, if the existing tab name is "" it will use the default settings
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatWindowNewTab);
bool exprClientChatNewTab(SA_PARAM_OP_VALID ChatTabGroupConfig *pWindowSettings, const char *pchNewName, const char *pchOldName)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(entActivePlayerPtr());

	if (pConfig && pWindowSettings)
	{
		ChatTabGroupConfig *pExistingWindow = NULL;
		S32 iExistingIndex = -1;
		S32 i;

		// Search for an existing tab within the same Window
		iExistingIndex = ChatCommon_FindTabConfigIndex(pWindowSettings, pchNewName);
		if (iExistingIndex >= 0)
		{
			pExistingWindow = pWindowSettings;
		}
		else
		{
			// Search for an existing tab with the same NewName
			for (i = 0; i < eaSize(&pConfig->eaChatTabGroupConfigs); i++)
			{
				ChatTabGroupConfig *pWindow = pConfig->eaChatTabGroupConfigs[i];
				iExistingIndex = ChatCommon_FindTabConfigIndex(pWindow, pchNewName);
				if (iExistingIndex >= 0)
				{
					pExistingWindow = pWindow;
					break;
				}
			}
		}

		if (pExistingWindow && iExistingIndex >= 0)
		{
			char *estrNewName = NULL;
			S32 iNewIndex = 2;

			if ((pWindowSettings != pExistingWindow) && (!*pchOldName || !stricmp(pchNewName, pchOldName)))
			{
				// The behavior is to copy the existing tab
				if (ClientChatConfig_UsingEntityConfig())
					ServerCmd_cmdServerChatConfig_CopyTabEx(pWindowSettings->pchName, pchNewName, pExistingWindow->pchName, iExistingIndex);
				else
					GServerCmd_crChatConfig_CopyTabEx(GLOBALTYPE_CHATRELAY, pWindowSettings->pchName, pchNewName, pExistingWindow->pchName, iExistingIndex);
				return true;
			}

			// Need to rename the chat tab to be unique across all windows
			estrStackCreate(&estrNewName);
			do {
				// create a new name
				estrPrintf(&estrNewName, "%s%d", pchNewName, iNewIndex);
				for (i = eaSize(&pConfig->eaChatTabGroupConfigs) - 1; i >= 0; i--)
				{
					ChatTabGroupConfig *pWindow = pConfig->eaChatTabGroupConfigs[i];
					if (ChatCommon_FindTabConfigIndex(pWindow, estrNewName) >= 0)
					{
						break;
					}
				}

				// Found a unique name
				if (i < 0)
					break;
			} while (true);
			if (ClientChatConfig_UsingEntityConfig())
				ServerCmd_cmdServerChatConfig_CopyTabEx(pWindowSettings->pchName, pchNewName, pExistingWindow->pchName, iExistingIndex);
			else
				GServerCmd_crChatConfig_CopyTabEx(GLOBALTYPE_CHATRELAY, pWindowSettings->pchName, pchNewName, pExistingWindow->pchName, iExistingIndex);
			estrDestroy(&estrNewName);
			return true;
		}
		else if (!*pchOldName)
		{
			// Create a new tab with the default settings
			if (ClientChatConfig_UsingEntityConfig())
				ServerCmd_cmdServerChatConfig_CreateTab(pWindowSettings->pchName);
			else
				GServerCmd_crChatConfig_CreateTab(GLOBALTYPE_CHATRELAY, pWindowSettings->pchName);
			return true;
		}
		// else pchOldName is set, there is no source tab to copy from
	}

	return false;
}

// Create a new tab with the settings from a source key from ChatWindowGetTabKey. If NewName isn't specified, then it will use the source tab's title.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatWindowNewTabFromKey);
bool exprClientChatNewTabFromKey(SA_PARAM_OP_VALID ChatTabGroupConfig *pWindowSettings, const char *pchNewName, const char *pchSourceKey)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(entActivePlayerPtr());

	if (pConfig && pWindowSettings)
	{
		char achSrcWindow[30];
		ChatTabGroupConfig *pExistingWindow = NULL;
		ChatTabConfig *pTab = NULL;
		S32 iExistingWindowId = -1;
		S32 iExistingIndex = -1;

		// Parse the source key
		if (sscanf(pchSourceKey, "%d,%d", &iExistingWindowId, &iExistingIndex) != 2)
			return false;
		if (iExistingWindowId < 0 || iExistingIndex < 0)
			return false;

		// Convert the window id to a ChatTabGroupConfig *
		sprintf(achSrcWindow, "%d", iExistingWindowId);
		pExistingWindow = eaIndexedGetUsingString(&pConfig->eaChatTabGroupConfigs, achSrcWindow);

		// Validate the index/source name
		if (!pExistingWindow || !(pTab = eaGet(&pExistingWindow->eaChatTabConfigs, iExistingIndex)))
			return false;

		if (ClientChatConfig_UsingEntityConfig())
			ServerCmd_cmdServerChatConfig_CopyTabEx(pWindowSettings->pchName, pchNewName && *pchNewName ? pchNewName : pTab->pchTitle, pExistingWindow->pchName, iExistingIndex);
		else
			GServerCmd_crChatConfig_CopyTabEx(GLOBALTYPE_CHATRELAY, pWindowSettings->pchName, pchNewName && *pchNewName ? pchNewName : pTab->pchTitle, pExistingWindow->pchName, iExistingIndex);
		return true;
	}

	return false;
}

// This description does not describe the function at all.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatWindowMoveTab);
bool exprClientChatMoveTab(SA_PARAM_OP_VALID ChatTabGroupConfig *pWindowSettings, const char *pchInsert, const char *pchSourceKey)
{
	char achSrcWindow[30];
	S32 iSrcWindowId = -1;
	S32 iSrcIndex = -1;
	S32 iDstIndex = ChatCommon_FindTabConfigIndex(pWindowSettings, pchInsert);

	if (!pWindowSettings)
		return false;
	if (!*pchSourceKey)
		return false;
	if (sscanf(pchSourceKey, "%d,%d", &iSrcWindowId, &iSrcIndex) != 2)
		return false;
	if (iSrcWindowId == -1 || iSrcIndex == -1)
		return false;
	if (iDstIndex < 0)
		iDstIndex = eaSize(&pWindowSettings->eaChatTabConfigs);
	else
		iDstIndex += 1;

	sprintf(achSrcWindow, "%d", iSrcWindowId);
	ClientChatConfig_MoveTab(ClientChatConfig_GetChatConfig(entActivePlayerPtr()), achSrcWindow, iSrcIndex, pWindowSettings->pchName, iDstIndex);
	return true;
}

// Move the tab to before the provided tab.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatWindowMoveTabBefore);
bool exprClientChatMoveTabBefore(SA_PARAM_OP_VALID ChatTabGroupConfig *pWindowSettings, const char *pchInsert, const char *pchSourceKey)
{
	char achSrcWindow[30];
	S32 iSrcWindowId = -1;
	S32 iSrcIndex = -1;
	S32 iDstIndex = ChatCommon_FindTabConfigIndex(pWindowSettings, pchInsert);

	if (!pWindowSettings)
		return false;
	if (!*pchSourceKey)
		return false;
	if (sscanf(pchSourceKey, "%d,%d", &iSrcWindowId, &iSrcIndex) != 2)
		return false;
	if (iSrcWindowId == -1 || iSrcIndex == -1)
		return false;
	if (iDstIndex < 0)
		iDstIndex = 0;

	sprintf(achSrcWindow, "%d", iSrcWindowId);
	ClientChatConfig_MoveTab(ClientChatConfig_GetChatConfig(entActivePlayerPtr()), achSrcWindow, iSrcIndex, pWindowSettings->pchName, iDstIndex);
	return true;
}

// Get the move key for a specific tab, used by ChatWindowMoveTab
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatWindowGetTabKey);
const char *exprClientChatGetTabKey(ExprContext *pContext, SA_PARAM_OP_VALID ChatTabGroupConfig *pWindowSettings, const char *pchTitle)
{
	S32 i = ChatCommon_FindTabConfigIndex(pWindowSettings, pchTitle);
	if (i >= 0 && pWindowSettings)
	{
		char achBuffer[200];
		sprintf(achBuffer, "%s,%d", pWindowSettings->pchName, i);
		return exprContextAllocString(pContext, achBuffer);
	}
	return "";
}

// Count the input channels for the given chat tab
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatWindowCountInputChannels);
S32 exprClientChatCountInputChannels(SA_PARAM_OP_VALID ChatTabGroupConfig *pWindowSettings, const char *pchTitle, U32 uShowFlags)
{
	ChatTabConfig *pTab = ClientChatGetTab(pWindowSettings, pchTitle);
	S32 iUsedChannels = 0;

	if (pTab)
	{
		static ChatLogEntryType *s_eaMessageTypes = NULL;
		const char ***peaCustomChannels = ClientChat_GetSubscribedCustomChannels();
		const char *pchSystemName;
		S32 i;

		if (!s_eaMessageTypes)
		{
			DefineFillAllKeysAndValues(ChatLogEntryTypeEnum, NULL, (S32 **)&s_eaMessageTypes);
		}

		// Add built in channels
		for (i = 0; i < eaiSize(&s_eaMessageTypes); i++)
		{
			const char *pchTypeName;

			// Custom channels are handled separately
			if (s_eaMessageTypes[i] == kChatLogEntryType_Channel)
				continue;

			// Is the message type visible in the tab?
			// TODO: add flag to show all channels/just local/just zone/etc
			if (!ChatCommon_IsLogEntryVisibleInTab(pTab, s_eaMessageTypes[i], NULL))
				continue;

			pchTypeName = ChatCommon_GetMessageTypeName(s_eaMessageTypes[i]);
			pchSystemName = ClientChat_GetSubscribedChannelSystemName(pchTypeName);

			if (pchSystemName)
			{
				// Add the channel
				iUsedChannels++;
			}
		}

		// Add custom channels
		for (i = 0; i < eaSize(peaCustomChannels); i++)
		{
			// Ignore built in channels. This is so that duplicate channels don't appear in the list.
			// This may be unnecessary, but I have zero confidence in the code. - JM
			if (ChatCommon_IsBuiltInChannel((*peaCustomChannels)[i]))
				continue;

			// Is the message type visible in the tab?
			// TODO: add flag to show all channels
			if (!ChatCommon_IsLogEntryVisibleInTab(pTab, kChatLogEntryType_Channel, (*peaCustomChannels)[i]))
				continue;

			pchSystemName = ClientChat_GetSubscribedChannelSystemName((*peaCustomChannels)[i]);

			if (!pchSystemName)
			{
				// Add the channel
				iUsedChannels++;
			}
		}
	}

	return iUsedChannels;
}

// Get the list of input channels for the given chat tab
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenChatWindowGetInputChannels);
void exprClientChatGetInputChannels(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID ChatTabGroupConfig *pWindowSettings, const char *pchTitle, U32 uShowFlags)
{
	ChatTabConfig *pTab = ClientChatGetTab(pWindowSettings, pchTitle);
	ChatConfigInputChannel ***peaChannels = ui_GenGetManagedListSafe(pGen, ChatConfigInputChannel);
	S32 iUsedChannels = 0;

	if (pTab)
	{
		static ChatLogEntryType *s_eaMessageTypes = NULL;
		const char ***peaCustomChannels = ClientChat_GetSubscribedCustomChannels();
		ChatConfigInputChannel *pChannel;
		const char *pchSystemName;
		S32 i;

		if (!s_eaMessageTypes)
		{
			DefineFillAllKeysAndValues(ChatLogEntryTypeEnum, NULL, (S32 **)&s_eaMessageTypes);
		}

		// Add built in channels
		for (i = 0; i < eaiSize(&s_eaMessageTypes); i++)
		{
			const char *pchTypeName;

			// Custom channels are handled separately
			if (s_eaMessageTypes[i] == kChatLogEntryType_Channel)
				continue;

			// Is the message type visible in the tab?
			// TODO: add flag to show all channels/just local/just zone/etc
			if (!ChatCommon_IsLogEntryVisibleInTab(pTab, s_eaMessageTypes[i], NULL))
				continue;

			pchTypeName = ChatCommon_GetMessageTypeName(s_eaMessageTypes[i]);
			pchSystemName = ClientChat_GetSubscribedChannelSystemName(pchTypeName);

			if (pchSystemName)
			{
				// Add the channel
				pChannel = eaGetStruct(peaChannels, parse_ChatConfigInputChannel, iUsedChannels++);
				ClientChat_FillInputChannelInput(pChannel, pchSystemName, false);
			}
		}

		// Add custom channels
		for (i = 0; i < eaSize(peaCustomChannels); i++)
		{
			// Ignore built in channels. This is so that duplicate channels don't appear in the list.
			// This may be unnecessary, but I have zero confidence in the code. - JM
			if (ChatCommon_IsBuiltInChannel((*peaCustomChannels)[i]))
				continue;

			// Is the message type visible in the tab?
			// TODO: add flag to show all channels
			if (!ChatCommon_IsLogEntryVisibleInTab(pTab, kChatLogEntryType_Channel, (*peaCustomChannels)[i]))
				continue;

			pchSystemName = ClientChat_GetSubscribedChannelSystemName((*peaCustomChannels)[i]);

			if (!pchSystemName)
			{
				// Add the channel
				pChannel = eaGetStruct(peaChannels, parse_ChatConfigInputChannel, iUsedChannels++);
				ClientChat_FillInputChannelInput(pChannel, pchSystemName, false);
			}
		}
	}

	eaSetSizeStruct(peaChannels, parse_ChatConfigInputChannel, iUsedChannels);
	eaQSort(*peaChannels, ChatConfig_InputChannelComparator);
	ui_GenSetManagedListSafe(pGen, peaChannels, ChatConfigInputChannel, true);
}

// Count the number of custom channels
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatWindowCountAllChannels);
S32 exprClientChatCountChannels(void)
{
	const char ***peaCustomChannels = ClientChat_GetSubscribedCustomChannels();
	const char *pchSystemName;
	S32 i, iCount = 0;

	for (i = 0; i < eaSize(peaCustomChannels); i++)
	{
		// Ignore built in channels. This is so that duplicate channels don't appear in the list.
		// This may be unnecessary, but I have zero confidence in the code. - JM
		if (ChatCommon_IsBuiltInChannel((*peaCustomChannels)[i]))
			continue;

		pchSystemName = ClientChat_GetSubscribedChannelSystemName((*peaCustomChannels)[i]);
		if (!pchSystemName)
			iCount++;
	}

	return iCount;
}

void ClientChatConfig_SetCurrentChannelForTab(const char *pchTabGroupName, int iTabIndex, const char *pchChannelName)
{
	if (ClientChatConfig_UsingEntityConfig())
		ServerCmd_cmdServerChatConfig_SetCurrentInputChannel(pchTabGroupName, iTabIndex, pchChannelName);
	else
	{
		ChatConfig *pConfig = ClientChatConfig_GetChatConfig(entActivePlayerPtr());
		ChatConfigCommon_SetCurrentInputChannel(pConfig, pchTabGroupName, iTabIndex, pchChannelName);
		GServerCmd_crChatConfig_SetDefaultInputChannel(GLOBALTYPE_CHATRELAY, pchTabGroupName, iTabIndex, pchChannelName);
	}
}

// Set the input channel for the tab
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatWindowSetTabChannel);
bool exprClientChatSetTabChannel(SA_PARAM_OP_VALID ChatTabGroupConfig *pWindowSettings, const char *pchTitle, const char *pchChannel)
{
	S32 i = ChatCommon_FindTabConfigIndex(pWindowSettings, pchTitle);
	if (i >= 0 && pWindowSettings)
	{
		ChatTabConfig *pTab = pWindowSettings->eaChatTabConfigs[i];
		if (!stricmp_safe(pTab->pchDefaultChannel, pchChannel))
			return false;
		ClientChatConfig_SetCurrentChannelForTab(pWindowSettings->pchName, i, pchChannel);
	}
	return false;
}

// Set the channel filtering
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatWindowSetChannelFilter);
bool exprClientChatSetChannelFilter(SA_PARAM_OP_VALID ChatTabGroupConfig *pWindowSettings, const char *pchTitle, const char *pchChannel, bool bFilter)
{
	S32 i = ChatCommon_FindTabConfigIndex(pWindowSettings, pchTitle);
	if (i >= 0 && pWindowSettings)
	{
		ClientChatConfig_SetFilter(ClientChatConfig_GetChatConfig(entActivePlayerPtr()), pWindowSettings->pchName, i, pchChannel, bFilter, true);
		return true;
	}
	return false;
}

// Get the channel filtering
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatWindowGetChannelFilter);
bool exprClientChatGetChannelFilter(SA_PARAM_OP_VALID ChatTabGroupConfig *pWindowSettings, const char *pchTitle, const char *pchChannel)
{
	S32 i = ChatCommon_FindTabConfigIndex(pWindowSettings, pchTitle);
	if (i >= 0 && pWindowSettings)
	{
		ChatTabConfig *pTab = pWindowSettings->eaChatTabConfigs[i];
		if (eaFindString(&pTab->eaChannels, pchChannel) >= 0)
			return pTab->eChannelFilterMode == kChatTabFilterMode_Inclusive;
		return pTab->eChannelFilterMode == kChatTabFilterMode_Exclusive;
	}
	return false;
}

// Set the system message filtering
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatWindowSetMessageFilter);
bool exprClientChatSetMessageFilter(SA_PARAM_OP_VALID ChatTabGroupConfig *pWindowSettings, const char *pchTitle, const char *pchMessage, bool bFilter)
{
	S32 i = ChatCommon_FindTabConfigIndex(pWindowSettings, pchTitle);
	if (i >= 0 && pWindowSettings)
	{
		ClientChatConfig_SetFilter(ClientChatConfig_GetChatConfig(entActivePlayerPtr()), pWindowSettings->pchName, i, pchMessage, bFilter, false);
		return true;
	}
	return false;
}

// Get the system message filtering
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatWindowGetMessageFilter);
bool exprClientChatGetMessageFilter(SA_PARAM_OP_VALID ChatTabGroupConfig *pWindowSettings, const char *pchTitle, const char *pchMessage)
{
	S32 i = ChatCommon_FindTabConfigIndex(pWindowSettings, pchTitle);
	if (i >= 0 && pWindowSettings)
	{
		ChatTabConfig *pTab = pWindowSettings->eaChatTabConfigs[i];
		if (eaFindString(&pTab->eaMessageTypes, pchMessage) >= 0)
			return pTab->eMessageTypeFilterMode == kChatTabFilterMode_Inclusive;
		return pTab->eMessageTypeFilterMode == kChatTabFilterMode_Exclusive;
	}
	return false;
}

// Set the filter
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatWindowSetFilter);
bool exprClientChatWindowSetFilter(SA_PARAM_OP_VALID ChatConfigTabFilterDef *pTabFilter, bool bFilter)
{
	if (pTabFilter)
	{
		ClientChatConfig_SetFilter(ClientChatConfig_GetChatConfig(entActivePlayerPtr()), pTabFilter->pchChatTabGroupName, pTabFilter->iTab, pTabFilter->pchFilterSystemName, bFilter, pTabFilter->bIsChannel);
		return true;
	}
	return false;
}

// GET _ALL_ THE FILTERS
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatWindowGetFilters);
void exprClientChatWindowGetFilters(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID ChatTabGroupConfig *pWindowSettings, const char *pchTitle, S32 iFlags)
{
	Entity *pEntity = entActivePlayerPtr();
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);
	DefaultChatConfigSource eDefaultSource = ChatCommon_GetChatConfigSourceForEntity(pEntity);
	S32 iTabIndex = ChatCommon_FindTabConfigIndex(pWindowSettings, pchTitle);
	ChatTabConfig *pTab = pWindowSettings && iTabIndex >= 0 ? pWindowSettings->eaChatTabConfigs[iTabIndex] : NULL;
	ChatConfigTabFilterDef ***peaTabFilters = ui_GenGetManagedListSafe(pGen, ChatConfigTabFilterDef);
	S32 i, j, n, iCount = 0;

	if (pWindowSettings && pTab)
	{
		// Message types
		if (iFlags & 1)
		{
			ChatLogEntryType **peaiMessageTypes = ChatCommon_GetFilterableMessageTypes();
			n = eaiSize(peaiMessageTypes);
			for (i = 0; i < n; i++)
			{
				ChatLogEntryType eType = (*peaiMessageTypes)[i];
				const char *pchTypeName = ChatLogEntryTypeEnum[1+eType].key;
				ChatConfigTabFilterDef *pFilter = NULL;

				// Keep list stable
				for (j = iCount; j < eaSize(peaTabFilters); j++)
				{
					if (!stricmp_safe((*peaTabFilters)[j]->pchFilterSystemName, pchTypeName)
						&& !(*peaTabFilters)[j]->bIsChannel)
					{
						if (j != iCount)
							eaSwap(peaTabFilters, i, j);
						pFilter = (*peaTabFilters)[iCount++];
						break;
					}
				}
				if (!pFilter)
				{
					pFilter = StructCreate(parse_ChatConfigTabFilterDef);
					eaInsert(peaTabFilters, pFilter, iCount++);
				}

				estrCopy2(&pFilter->pchChatTabGroupName, pWindowSettings->pchName);
				estrCopy2(&pFilter->pchFilterSystemName, pchTypeName);
				ClientChat_GetMessageTypeDisplayNameByType(&pFilter->pchFilterDisplayName, eType);

				pFilter->iTab = iTabIndex;
				pFilter->kMessageType = eType;
				pFilter->bIsChannel = false;
				pFilter->bMayUnsubscribe = false;
				pFilter->bFiltered = eaFindString(&pTab->eaMessageTypes, pchTypeName);
				if (pTab->eMessageTypeFilterMode == kChatTabFilterMode_Exclusive)
					pFilter->bFiltered = !pFilter->bFiltered;
				pFilter->iColor = ChatCommon_GetChatColor(pConfig, eType, pchTypeName, eDefaultSource);
				ChatCommon_SetVectorFromColor(pFilter->vColor, pFilter->iColor);
			}
		}

		// Custom channels
		if (iFlags & 2)
		{
			const char ***peaSubscribedChannels = ClientChat_GetSubscribedCustomChannels();
			n = eaSize(peaSubscribedChannels);
			for (i = 0; i < n; i++)
			{
				const char *pchChannelName = (*peaSubscribedChannels)[i];
				ChatConfigTabFilterDef *pFilter = NULL;
				const char *pchSystemName;

				// Ignore built in channels. This is so that duplicate channels don't appear in the list.
				// This may be unnecessary, but I have zero confidence in the code. - JM
				if (ChatCommon_IsBuiltInChannel((*peaSubscribedChannels)[i]))
					continue;

				pchSystemName = ClientChat_GetSubscribedChannelSystemName((*peaSubscribedChannels)[i]);
				if (pchSystemName)
					continue;

				// Keep list stable
				for (j = iCount; j < eaSize(peaTabFilters); j++)
				{
					if (!stricmp_safe((*peaTabFilters)[j]->pchFilterSystemName, pchChannelName)
						&& (*peaTabFilters)[j]->bIsChannel)
					{
						if (j != iCount)
							eaSwap(peaTabFilters, i, j);
						pFilter = (*peaTabFilters)[iCount++];
						break;
					}
				}
				if (!pFilter)
				{
					pFilter = StructCreate(parse_ChatConfigTabFilterDef);
					eaInsert(peaTabFilters, pFilter, iCount++);
				}

				estrCopy2(&pFilter->pchChatTabGroupName, pWindowSettings->pchName);
				estrCopy2(&pFilter->pchFilterSystemName, pchChannelName);
				estrCopy2(&pFilter->pchFilterDisplayName, pchChannelName);

				pFilter->iTab = iTabIndex;
				pFilter->kMessageType = kChatLogEntryType_Channel;
				pFilter->bIsChannel = true;
				pFilter->bMayUnsubscribe = true;
				pFilter->bFiltered = eaFindString(&pTab->eaChannels, pchChannelName);
				if (pTab->eChannelFilterMode == kChatTabFilterMode_Exclusive)
					pFilter->bFiltered = !pFilter->bFiltered;
				pFilter->iColor = ChatCommon_GetChatColor(pConfig, kChatLogEntryType_Channel, pchChannelName, eDefaultSource);
				ChatCommon_SetVectorFromColor(pFilter->vColor, pFilter->iColor);
			}
		}
	}

	eaSetSizeStruct(peaTabFilters, parse_ChatConfigTabFilterDef, iCount);
	eaSortUsingKey(peaTabFilters, parse_ChatConfigTabFilterDef);
	ui_GenSetManagedListSafe(pGen, peaTabFilters, ChatConfigTabFilterDef, true);
}

// Save the tab as the currently selected tab
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatWindowSelectTab);
bool exprClientChatSetSelectedTab(SA_PARAM_OP_VALID ChatTabGroupConfig *pWindowSettings, const char *pchTitle)
{
	S32 i = ChatCommon_FindTabConfigIndex(pWindowSettings, pchTitle);
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(entActivePlayerPtr());
	if (i >= 0 && pWindowSettings)
	{
		if (i == pWindowSettings->iCurrentTab && !stricmp_safe(pConfig->pchCurrentTabGroup, pWindowSettings->pchName))
			return false;
		if (ClientChatConfig_UsingEntityConfig())
			ServerCmd_cmdServerChatConfig_SetCurrentTab(pWindowSettings->pchName, i);
		else
			ChatConfigCommon_SetCurrentTab(pConfig, pWindowSettings->pchName, i);
		return true;
	}
	return false;
}

// Get the saved tab
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatWindowSelectedTab);
const char *exprClientChatGetSelectedTab(SA_PARAM_OP_VALID ChatTabGroupConfig *pWindowSettings)
{
	if (pWindowSettings)
	{
		ChatTabConfig *pTab = eaGet(&pWindowSettings->eaChatTabConfigs, pWindowSettings->iCurrentTab);
		if (pTab)
		{
			return pTab->pchTitle;
		}
	}
	return "";
}

// Get the display name of the current input channel.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_IsVisible);
bool exprClientChatIsChatVisible(void)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(entActivePlayerPtr());
	if (pConfig)
		return !pConfig->bChatHidden;
	return true;
}

// Store whether or not the chat window is currently visible
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_SetVisible);
void ClientChat_SetChatVisible(S32 bVisible);

// Store whether or not the chat window is currently visible
AUTO_COMMAND ACMD_NAME(Chat_SetVisible) ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_CATEGORY(Interface);
void ClientChat_SetChatVisible(S32 bVisible)
{
	NOCONST(ChatConfig) *pConfig = ClientChatConfig_GetEditableConfig();
	if (pConfig)
	{
		if (bVisible == !pConfig->bChatHidden)
			return;
	}
	if (ClientChatConfig_UsingEntityConfig())
		ServerCmd_cmdServerChatConfig_SetChatVisible(bVisible);
	else
	{
		if (pConfig)
			pConfig->bChatHidden = !bVisible;
		GServerCmd_crChatConfig_SetChatVisible(GLOBALTYPE_CHATRELAY, bVisible);
	}
}

AUTO_COMMAND ACMD_NAME(Chat_ForceShowAllEntries) ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_CATEGORY(Interface);
void ClientChat_ForceShowAllEntries(S32 bVisible)
{
	NOCONST(ChatConfig) *pConfig = ClientChatConfig_GetEditableConfig();
	if (pConfig)
	{
		pConfig->bForceDrawAllEntries = !!bVisible;
	}
}


// Set the chat window as the current active chat window
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatWindowSetActive);
bool exprClientChatSetActive(SA_PARAM_OP_VALID ChatTabGroupConfig *pWindowSettings)
{
	Entity *pEntity = entActivePlayerPtr();
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);

	if (pConfig && pWindowSettings)
	{
		if (stricmp_safe(pConfig->pchCurrentTabGroup, pWindowSettings->pchName))
		{
			if (ClientChatConfig_UsingEntityConfig())
				ServerCmd_cmdServerChatConfig_SetCurrentTab(pWindowSettings->pchName, pWindowSettings->iCurrentTab);
			else
				ChatConfigCommon_SetCurrentTab(pConfig, pWindowSettings->pchName, pWindowSettings->iCurrentTab);
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ChatWindowGetActive");
S32 exprClientChatGetActive(void)
{
	Entity *pEntity = entActivePlayerPtr();
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEntity);

	if (pConfig && pConfig->pchCurrentTabGroup)
	{
		if (ChatCommon_GetTabGroupConfig(pConfig, pConfig->pchCurrentTabGroup))
			return atoi(pConfig->pchCurrentTabGroup);
	}
	return 0;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(Chat_SetStatus);
void ClientChatConfig_SetStatus(ACMD_SENTENCE pchMessage)
{
	if (ClientChatConfig_UsingEntityConfig())
		ServerCmd_cmdServerChatConfig_SetStatus(pchMessage);
	else
	{
		NOCONST(ChatConfig) *pConfig = ClientChatConfig_GetEditableConfig();
		if (pConfig)
		{
			ChatConfigStatus eResult = ChatConfigCommon_SetStatusMessage(pConfig, pchMessage);
			if (eResult == CHATCONFIG_STATUS_PROFANE)
			{
				Entity *pEnt = entActivePlayerPtr();
				const char *msg = entTranslateMessageKey(pEnt, "Chat_Status_ContainsProfanity");
				notify_NotifySend(pEnt, kNotifyType_Failed, msg, NULL, NULL);
				return;
			}
		}
		GServerCmd_crChatConfig_SetStatus(GLOBALTYPE_CHATRELAY, pchMessage);
	}
}
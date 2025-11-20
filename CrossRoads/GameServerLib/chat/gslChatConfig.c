/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslChat.h"
#include "gslChatConfig.h"
#include "chatCommon.h"
#include "chatCommonStructs.h"
#include "NotifyCommon.h"
#include "UtilitiesLib.h"
#include "TextFilter.h"

//#include "team.h"
//#include "Guild.h"
#include "Entity.h"
#include "Player.h"
#include "StringUtil.h"
#include "GameStringFormat.h"
#include "GameAccountDataCommon.h"

#include "AutoGen/chatCommon_h_ast.h"
#include "AutoGen/chatCommonStructs_h_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/ChatServer_autogen_remotefuncs.h"
#include "AutoGen/Entity_h_ast.h"
#include "Player_h_ast.h"

SA_RET_OP_VALID extern NOCONST(ChatConfig) *ServerChatConfig_GetChatConfig(Entity *pEntity) {
	NOCONST(ChatConfig) *pChatConfig = CONTAINER_NOCONST(ChatConfig, ChatCommon_GetChatConfig(pEntity));

	if (pChatConfig) {
		// Fix up current tab group reference, if needed
		if (!pChatConfig->pchCurrentTabGroup || !*pChatConfig->pchCurrentTabGroup) {
			if (eaSize(&pChatConfig->eaChatTabGroupConfigs) > 0) {
				NOCONST(ChatTabGroupConfig) *pTabGroup = eaGet(&pChatConfig->eaChatTabGroupConfigs, 0);
				devassert(pTabGroup);
				if (pTabGroup->pchName && *pTabGroup->pchName) {
					pChatConfig->pchCurrentTabGroup = StructAllocString(pTabGroup->pchName);
				} else {
					pTabGroup->pchName = estrCreateFromStr(ChatCommon_GetConfigDefaults(ChatCommon_GetChatConfigSourceForEntity(pEntity))->pchDefaultTabGroupName);
					pChatConfig->pchCurrentTabGroup = StructAllocString(pTabGroup->pchName);
				}
			} else {
				// Somehow the user has no tabs - do a full reset!
				ServerChatConfig_Reset(pEntity);
				pChatConfig = CONTAINER_NOCONST(ChatConfig, ChatCommon_GetChatConfig(pEntity));
			}
		}

		return pChatConfig;
	}

	if (!pEntity) {
		return NULL;
	}

	// It's not initialized...so perform initialization
	ServerChatConfig_PlayerUpdate(pEntity);
	return CONTAINER_NOCONST(ChatConfig, ChatCommon_GetChatConfig(pEntity)); // Try again
}

static NOCONST(ChatTabGroupConfig) *ServerChatConfig_GetTabGroupConfig(NOCONST(ChatConfig) *pChatConfig, const char *pchTabGroup)
{
	return CONTAINER_NOCONST(ChatTabGroupConfig, ChatCommon_GetTabGroupConfig(CONTAINER_RECONST(ChatConfig, pChatConfig), pchTabGroup));
}

// Create a new tab config using the first default tab config
static NOCONST(ChatTabConfig) *ServerChatConfig_CreateDefaultTabConfig(Entity *pEntity)
{
	return ChatConfigCommon_CreateDefaultTabConfig(ChatCommon_GetChatConfigSourceForEntity(pEntity), 
		entGetLanguage(pEntity));
}

static NOCONST(ChatConfig) *ServerChatConfig_CreateDefaultConfig(Entity *pEntity)
{
	NOCONST(ChatConfig) *pChatConfig = StructCreateNoConst(parse_ChatConfig);
	ChatConfigCommon_InitToDefault(pChatConfig, ChatCommon_GetChatConfigSourceForEntity(pEntity), entGetLanguage(pEntity));
	return pChatConfig;
}

// Returns a static estr that contains a unique tab name
static const char **GetUniqueTabName(Entity *pEntity, char *pchName)
{
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEntity);
	return ChatConfigCommon_GetUniqueTabName(pConfig, pchName);
}

void ServerChatConfig_SetCurrentTab(Entity *pEntity, const char *pchTabGroup, S32 iTabIndex) {
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEntity);
	NOCONST(ChatTabGroupConfig) *pTabGroup = ServerChatConfig_GetTabGroupConfig(pConfig, pchTabGroup);
	if (pTabGroup && pchTabGroup && *pchTabGroup) {
		NOCONST(ChatTabConfig) *pCurrentTab = eaGet(&pTabGroup->eaChatTabConfigs, iTabIndex);
		devassert(pConfig);

		// Update the current tab group & index
		// - Only update group if it changed
		if (pConfig->pchCurrentTabGroup && stricmp(pConfig->pchCurrentTabGroup, pchTabGroup)) {
			free(pConfig->pchCurrentTabGroup);
			pConfig->pchCurrentTabGroup = StructAllocString(pchTabGroup);
		} else if (!pConfig->pchCurrentTabGroup) {
			pConfig->pchCurrentTabGroup = StructAllocString(pchTabGroup);
		}

		pTabGroup->iCurrentTab = iTabIndex;
		if (pCurrentTab && pCurrentTab->pchDefaultChannel && pCurrentTab->pchDefaultChannel[0])
		{
			if (pConfig->pchCurrentInputChannel) {
				free(pConfig->pchCurrentInputChannel);
			}
			pConfig->pchCurrentInputChannel = StructAllocString(pCurrentTab->pchDefaultChannel);
		}

		entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
	}
}

void ServerChatConfig_SetChannelColor(Entity *pEntity, const char *pchSystemName, bool bIsChannel, U32 iColor)
{
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEntity);
	ChatLogEntryType eLogType = bIsChannel ? kChatLogEntryType_Channel : StaticDefineIntGetIntDefault(ChatLogEntryTypeEnum, pchSystemName, kChatLogEntryType_Channel);
	U32 iDefaultColor = ChatCommon_GetChatColor(NULL, eLogType, pchSystemName, ChatCommon_GetChatConfigSourceForEntity(pEntity));

	if (pConfig && pchSystemName && *pchSystemName)
	{
		if (bIsChannel)
		{
			ChatConfigCommon_SetChannelColor(pConfig, pchSystemName, iColor, iDefaultColor);
		}
		else
		{
			ChatConfigCommon_SetMessageColor(pConfig, pchSystemName, iColor, iDefaultColor);
		}

		entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
	}
}

void ServerChat_JoinSpecialChannel(Entity *pEnt, const char *channel_name);
void ServerChat_JoinLifetimeChannel(Entity *pEnt);

// Initialize defaults and make sure the entity is up-to-date
void ServerChatConfig_PlayerUpdate(Entity *pEntity) {
	//char temp[512];

	// Force defaults to be created, if not already created
	ChatCommon_GetConfigDefaults(ChatCommon_GetChatConfigSourceForEntity(pEntity)); // Force defaults to be loaded if they aren't already

	// Update player entity
	if (pEntity && pEntity->pPlayer && pEntity->pPlayer->pUI && pEntity->pPlayer->pUI) {
		NOCONST(ChatConfig) *pChatConfig = CONTAINER_NOCONST(ChatConfig, pEntity->pPlayer->pUI->pChatConfig);
		if (pChatConfig == NULL) {
			pChatConfig = ServerChatConfig_CreateDefaultConfig(pEntity);
			pEntity->pPlayer->pUI->pChatConfig = (ChatConfig*) pChatConfig;
			entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
			entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
		}
		else if (pChatConfig && pChatConfig->bGlobalChannelSubscribed)
		{
			//RemoteCommand_ChatServerJoinChannel(GLOBALTYPE_CHATSERVER, 0, pEntity->pPlayer->accountID, 
			//	SHARD_GLOBAL_CHANNEL_NAME, CHANNEL_SPECIAL_SHARDGLOBAL, 0);
			//*temp = '\0';
			//strcat(temp, GetShortProductName());
			//strcat(temp, SHARD_HELP_CHANNEL_NAME);
			//ServerChat_JoinSpecialChannel(pEntity, temp);
			//if (entity_LifetimeSubscription(pEntity)) ServerChat_JoinLifetimeChannel(pEntity);
		}
		//else
		//{
		//	*temp = '\0';
		//	strcat(temp, GetShortProductName());
		//	strcat(temp, SHARD_HELP_CHANNEL_NAME);
		//	ServerChat_JoinSpecialChannel(pEntity, temp);
		//	if (entity_LifetimeSubscription(pEntity)) ServerChat_JoinLifetimeChannel(pEntity);
		//}
	}
}

// Reset the player's entire chat configuration.
void ServerChatConfig_Reset(Entity *pEntity) {
	// Update player entity
	if (pEntity && pEntity->pPlayer && pEntity->pPlayer->pUI && pEntity->pPlayer->pUI) {
		NOCONST(ChatConfig) *pChatConfig = CONTAINER_NOCONST(ChatConfig, pEntity->pPlayer->pUI->pChatConfig);
		if (pChatConfig) {
			StructDestroyNoConst(parse_ChatConfig, pChatConfig);
		}

		pChatConfig = ServerChatConfig_CreateDefaultConfig(pEntity);
		pEntity->pPlayer->pUI->pChatConfig = (ChatConfig*) pChatConfig;
		ClientCmd_ChatConfig_NotifyTabIndexUpdated(pEntity, 0);
		entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
	}
}

// Create a new chat tab group
void ServerChatConfig_CreateTabGroup(Entity *pEntity, char *pchTabGroup, bool bInitTabs) {
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEntity);
	NOCONST(ChatTabGroupConfig) *pTabGroupConfig = ServerChatConfig_GetTabGroupConfig(pConfig, pchTabGroup);
	S32 i;
	U32 uiTime = timeSecondsSince2000();

	if (!pConfig || pTabGroupConfig)
		return;

	// First try converting an existing tab group config
	if (!stricmp(pchTabGroup, "0") && eaSize(&pConfig->eaChatTabGroupConfigs) > 0)
	{
		NOCONST(ChatTabGroupConfig) **eaTabGroupConfigs = NULL;
		eaCopy(&eaTabGroupConfigs, &pConfig->eaChatTabGroupConfigs);

		// rename them by index
		// NB: the pTabGroupConfig->eaChatTabConfigs is indexed, so remove then readd the structs. 
		eaClear(&pConfig->eaChatTabGroupConfigs);
		for (i = 0; i < eaSize(&eaTabGroupConfigs); i++) {
			pTabGroupConfig = eaTabGroupConfigs[i];
			estrPrintf(&pTabGroupConfig->pchName, "%d", i);
			eaIndexedAdd(&pConfig->eaChatTabGroupConfigs, pTabGroupConfig);
			pTabGroupConfig->uiTime = uiTime;
		}
		eaDestroy(&eaTabGroupConfigs);
	}
	ChatConfigCommon_CreateTabGroup(pConfig, pchTabGroup, bInitTabs, entGetLanguage(pEntity), ChatCommon_GetChatConfigSourceForEntity(pEntity));

	entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
	entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
}

// Delete a chat tab group
void ServerChatConfig_DeleteTabGroup(Entity *pEntity, char *pchTabGroup)
{
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEntity);
	NOCONST(ChatTabGroupConfig) *pTabGroupConfig = ServerChatConfig_GetTabGroupConfig(pConfig, pchTabGroup);

	if (!pTabGroupConfig)
		return;

	eaFindAndRemove(&pConfig->eaChatTabGroupConfigs, pTabGroupConfig);
	StructDestroyNoConst(parse_ChatTabGroupConfig, pTabGroupConfig);

	entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
	entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
}

// Reset a chat tab group
void ServerChatConfig_ResetTabGroup(Entity *pEntity, char *pchTabGroup)
{
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEntity);
	NOCONST(ChatTabGroupConfig) *pTabGroupConfig = ServerChatConfig_GetTabGroupConfig(pConfig, pchTabGroup);

	if (!pTabGroupConfig)
		return;

	ChatConfigCommon_ResetTabGroup(pConfig, pchTabGroup, entGetLanguage(pEntity), ChatCommon_GetChatConfigSourceForEntity(pEntity));

	entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
	entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
}

// Create a new chat tab
void ServerChatConfig_CreateTab(Entity *pEntity, char *pchTabGroup)
{
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEntity);
	int index;

	index = ChatConfigCommon_CreateTab(pConfig, pchTabGroup, entGetLanguage(pEntity), ChatCommon_GetChatConfigSourceForEntity(pEntity));
	if (index != -1)
	{
		ServerChatConfig_SetCurrentTab(pEntity, pchTabGroup, index);
		ClientCmd_ChatConfig_NotifyTabIndexUpdated(pEntity, index);
		entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
	}
}

// Copy a chat tab
void ServerChatConfig_CopyTab(Entity *pEntity, char *pchDstTabGroup, char *pchDstName, char *pchSrcTabGroup, int iSrcTab)
{
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEntity);
	int idx = ChatConfigCommon_CopyTab(pConfig, pchDstTabGroup, pchDstName, pchSrcTabGroup, iSrcTab);

	if (idx != -1)
	{
		ServerChatConfig_SetCurrentTab(pEntity, pchDstTabGroup, idx);
		ClientCmd_ChatConfig_NotifyTabIndexUpdated(pEntity, idx);
		entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
	}
}

// Delete a chat tab
void ServerChatConfig_DeleteTab(Entity *pEntity, char *pchTabGroup, int iTab)
{
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEntity);
	int idx = ChatConfigCommon_DeleteTab(pConfig, pchTabGroup, iTab);
	if (idx != -1)
	{
		ServerChatConfig_SetCurrentTab(pEntity, pchTabGroup, idx);
		ClientCmd_ChatConfig_NotifyTabIndexUpdated(pEntity, idx);
		entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
	}
}

// Rename a tab
void ServerChatConfig_SetTabName(Entity *pEntity, char *pchTabGroup, int iTab, char *pchName)
{
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEntity);
	if (ChatConfigCommon_SetTabName(pConfig, pchTabGroup, iTab, pchName))
	{
		entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
	}
}

void ServerChatConfig_MoveTab(Entity *pEntity, const char *pchFromGroupName, S32 iFromIndex, const char *pchToGroupName, S32 iToIndex)
{
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEntity);
	int idx = ChatConfigCommon_MoveTab(pConfig, pchFromGroupName, iFromIndex, pchToGroupName, iToIndex);

	if (idx != -1)
	{
		ServerChatConfig_SetCurrentTab(pEntity, pchToGroupName, idx);
		//TODO: We should change notify tab index updated to include the tab group name!!!
		ClientCmd_ChatConfig_NotifyTabIndexUpdated(pEntity, idx);
		entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
	}
}

void ServerChatConfig_SetTabFilterMode(Entity *pEntity, char *pchTabGroup, int iTab, bool bExclusiveFilter)
{
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEntity);
	NOCONST(ChatTabConfig) *pTabConfig = ChatConfigCommon_GetTabConfig(pConfig, pchTabGroup, iTab);
	if (pTabConfig)
	{
		bool bChanged = ChatConfigCommon_SetTabFilterMode(pTabConfig, ServerChat_GetSubscribedCustomChannels(pEntity), pchTabGroup, iTab, bExclusiveFilter);
		if (bChanged)
			ChatConfigCommon_UpdateCommonTabs(pConfig, pTabConfig);
	}
	entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
	entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
}

// Set the filtering of a message type for the given tab group tab
void ServerChatConfig_SetMessageTypeFilter(Entity *pEntity, const char *pchTabGroup, int iTab, const char *pchFilterName, bool bFiltered)
{
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEntity);
	if (ChatConfigCommon_SetMessageTypeFilter(pConfig, pchTabGroup, iTab, pchFilterName, bFiltered))
	{
		entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
	}
}

// Set the filtering of a channel for the given tab group tab
void ServerChatConfig_SetChannelFilter(Entity *pEntity, const char *pchTabGroup, int iTab, const char *pchFilterName, bool bFiltered)
{
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEntity);
	if (ChatConfigCommon_SetChannelFilter(pConfig, pchTabGroup, iTab, pchFilterName, bFiltered))
	{
		entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
	}
}

// Set all channel & message type filters
void ServerChatConfig_SetAllFilters(Entity *pEntity, const char *pchTabGroup, int iTab, bool bShowAllTypes) {
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEntity);
	NOCONST(ChatTabConfig) *pTabConfig = ChatConfigCommon_GetTabConfig(pConfig, pchTabGroup, iTab);
	bool bListAllMessageTypes;
	bool bListAllChannels;

	if (!pTabConfig) {
		return;
	}

	bListAllMessageTypes = pTabConfig->eMessageTypeFilterMode == kChatTabFilterMode_Inclusive ? bShowAllTypes : !bShowAllTypes;
	bListAllChannels = pTabConfig->eChannelFilterMode == kChatTabFilterMode_Inclusive ? bShowAllTypes : !bShowAllTypes;
	ChatConfigCommon_ResetMessageTypeFilters(pTabConfig, bListAllMessageTypes);
	ChatConfigCommon_ResetChannelFilters(pTabConfig, ServerChat_GetSubscribedCustomChannels(pEntity), bListAllChannels);
	ChatConfigCommon_UpdateCommonTabs(pConfig, pTabConfig);
	entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
	entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
}

// Sets whether chat log entries should include dates
void ServerChatConfig_SetShowDate(Entity *pEntity, bool bShowDate) {
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEntity);
	if (pConfig) {
		pConfig->bShowDate = bShowDate;
		pConfig->iVersion++;
		entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
	}
}

// Sets whether chat log entries should include timestamps
void ServerChatConfig_SetShowTime(Entity *pEntity, bool bShowTime) {
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEntity);
	if (pConfig) {
		pConfig->bShowTime = bShowTime;
		pConfig->iVersion++;
		entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
	}
}

// Sets whether chat log entries should always show message types
void ServerChatConfig_SetShowMessageTypeNames(Entity *pEntity, bool bShowMessageTypeNames) {
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEntity);
	if (pConfig) {
		pConfig->bShowMessageTypeNames = bShowMessageTypeNames;
		pConfig->iVersion++;
		entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
	}
}

// Sets whether chat links should display full names or just the character name (if provided)
void ServerChatConfig_SetShowFullNames(Entity *pEntity, bool bShowFullNames) {
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEntity);
	if (pConfig) {
		pConfig->bHideAccountNames = !bShowFullNames;
		pConfig->iVersion++;
		entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
	}
}

// Sets whether chat log entries should be filtered for profanity
void ServerChatConfig_SetProfanityFilter(Entity *pEntity, bool bProfanityFilter) {
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEntity);
	if (pConfig) {
		pConfig->bProfanityFilter = bProfanityFilter;
		pConfig->iVersion++;
		entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
	}
}

// Sets whether chat log entries should show the channel names
void ServerChatConfig_SetShowChannelNames(Entity *pEntity, bool bShowChannelNames) {
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEntity);
	if (pConfig) {
		pConfig->bShowChannelNames = bShowChannelNames;
		pConfig->iVersion++;
		entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
	}
}

// Sets whether the user wants to see the Shard Global Channel
void ServerChatConfig_GlobalChannelSubscribe(Entity *pEntity, bool bSubscribe) {
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEntity);
	if (pConfig) {
		pConfig->bGlobalChannelSubscribed = bSubscribe;
		if (bSubscribe)
			RemoteCommand_ChatServerJoinChannel(GLOBALTYPE_CHATSERVER, 0, pEntity->pPlayer->accountID, 
				SHARD_GLOBAL_CHANNEL_NAME, CHANNEL_SPECIAL_SHARDGLOBAL, 0);
		else
			RemoteCommand_ChatServerLeaveChannel(GLOBALTYPE_CHATSERVER, 0, pEntity->pPlayer->accountID, 
				SHARD_GLOBAL_CHANNEL_NAME);
		entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
	}
}

// Sets whether the auto-complete suggestion list shows the source of it entries
void ServerChatConfig_SetAnnotateAutoComplete(Entity *pEntity, bool bAnnotateAutoComplete) {
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEntity);
	if (pConfig) {
		pConfig->bAnnotateAutoComplete = bAnnotateAutoComplete;
		entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
	}
}

// Sets font size used by the chat window
void ServerChatConfig_SetFontScale(SA_PARAM_OP_VALID Entity *pEntity, F32 fFontScale) {
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEntity);
	if (pConfig) {
		pConfig->fFontScale = fFontScale;
		// Changing the scale does *not* increment the version, since scale does
		// not require reformatting the text, just laying it out again.
		entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
	}
}

// Sets chat window alpha to use when active
void ServerChatConfig_SetActiveWindowAlpha(SA_PARAM_OP_VALID Entity *pEntity, F32 fAlpha) {
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEntity);
	if (pConfig) {
		pConfig->fActiveWindowAlpha = fAlpha;
		entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
	}
}

// Sets chat window alpha to use when inactive
void ServerChatConfig_SetInactiveWindowAlpha(SA_PARAM_OP_VALID Entity *pEntity, F32 fAlpha) {
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEntity);
	if (pConfig) {
		pConfig->fInactiveWindowAlpha = fAlpha;
		entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
	}
}

// Sets the fade away duration for chat entries
void ServerChatConfig_SetFadeAwayDuration(SA_PARAM_OP_VALID Entity *pEntity, F32 fFadeAwayDuration) {
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEntity);
	if (pConfig) {
		ChatConfigCommon_SetFadeAwayDuration(pConfig, fFadeAwayDuration);
		entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
	}
}

// Sets the time required for chat entries to start fading away
void ServerChatConfig_SetTimeRequiredToStartFading(SA_PARAM_OP_VALID Entity *pEntity, F32 fTimeRequiredToStartFading) {
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEntity);
	if (pConfig) {
		ChatConfigCommon_SetTimeRequiredToStartFading(pConfig, fTimeRequiredToStartFading);
		entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
	}
}

void ServerChatConfig_SetCurrentInputChannel(Entity *pEntity, const char *pchTabGroup, S32 iTab, const char *pchChannel)\
{
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEntity);
	if (ChatConfigCommon_SetCurrentInputChannel((ChatConfig*) pConfig, pchTabGroup, iTab, pchChannel))
	{
		entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
	}
}

// Store the visibility of the chat window
void ServerChatConfig_SetChatVisible(Entity *pEntity, bool bVisible) {
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEntity);
	if (pConfig) {
		pConfig->bChatHidden = !bVisible;
		entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, true);
	}
}

void ServerChatConfig_SetStatusMessage(SA_PARAM_OP_VALID Entity *pEntity, const char *pchMessage)
{
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEntity);
	if (pConfig && SAFE_MEMBER2(pEntity, pPlayer, pUI))
	{
		switch (ChatConfigCommon_SetStatusMessage(pConfig, pchMessage))
		{
		case CHATCONFIG_STATUS_CHANGED:
			entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
			entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
			ServerChat_StatusUpdate(pEntity);
		xcase CHATCONFIG_STATUS_PROFANE:
			{
				const char *pchError = entTranslateMessageKey(pEntity, "Chat_Status_ContainsProfanity");
				notify_NotifySend(pEntity, kNotifyType_Failed, pchError, NULL, NULL);
			}
		xcase CHATCONFIG_STATUS_UNCHANGED:
		default: // does nothing
			break;
		}
	}
}

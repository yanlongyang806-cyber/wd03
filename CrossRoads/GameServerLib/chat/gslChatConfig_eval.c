/***************************************************************************
*     Copyright (c) 2005-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslChatConfig.h"
#include "Entity.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

//
// Chat Config
//

// Initialize defaults and make sure the entity is up-to-date
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void cmdServerChatConfig_PlayerUpdate(Entity *pEntity) {
	ServerChatConfig_PlayerUpdate(pEntity);
}

// Reset the player's entire chat configuration.
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void cmdServerChatConfig_Reset(Entity *pEntity) {
	ServerChatConfig_Reset(pEntity);
}

// Create a new chat tab group
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void cmdServerChatConfig_CreateTabGroup(Entity *pEntity, char *pchTabGroup, bool bInitTabs) {
	ServerChatConfig_CreateTabGroup(pEntity, pchTabGroup, bInitTabs);
}

// Delete an existing chat tab group
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void cmdServerChatConfig_DeleteTabGroup(Entity *pEntity, char *pchTabGroup) {
	ServerChatConfig_DeleteTabGroup(pEntity, pchTabGroup);
}

// Reset a chat tab group (initializes the tabs to the default)
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void cmdServerChatConfig_ResetTabGroup(Entity *pEntity, char *pchTabGroup) {
	ServerChatConfig_ResetTabGroup(pEntity, pchTabGroup);
}

// Create a new chat tab
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void cmdServerChatConfig_CreateTab(Entity *pEntity, char *pchTabGroup) {
	ServerChatConfig_CreateTab(pEntity, pchTabGroup);
}

// Copy a chat tab across tab groups
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void cmdServerChatConfig_CopyTabEx(Entity *pEntity, char *pchDstTabGroup, char *pchDstName, char *pchSrcTabGroup, int iSrcTab) {
	ServerChatConfig_CopyTab(pEntity, pchDstTabGroup, pchDstName, pchSrcTabGroup, iSrcTab);
}

// Delete a chat tab
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void cmdServerChatConfig_DeleteTab(Entity *pEntity, char *pchTabGroup, int iTab) {
	ServerChatConfig_DeleteTab(pEntity, pchTabGroup, iTab);
}

// Rename a tab
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void cmdServerChatConfig_SetTabName(Entity *pEntity, char *pchTabGroup, int iTab, char *pchName) {
	ServerChatConfig_SetTabName(pEntity, pchTabGroup, iTab, pchName);
}

// Move a tab
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void cmdServerChatConfig_MoveTab(Entity *pEntity, const char *pchFromGroupName, S32 iFromIndex, const char *pchToGroupName, S32 iToIndex) {
	ServerChatConfig_MoveTab(pEntity, pchFromGroupName, iFromIndex, pchToGroupName, iToIndex);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void cmdServerChatConfig_SetTabFilterMode(Entity *pEntity, char *pchTabGroup, int iTab, bool bExclusiveFilter) {
	ServerChatConfig_SetTabFilterMode(pEntity, pchTabGroup, iTab, bExclusiveFilter);
}

// Set the filtering of a message type for the given tab group tab
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void cmdServerChatConfig_SetMessageTypeFilter(Entity *pEntity, const char *pchTabGroup, int iTab, const char *pchFilterName, bool bFiltered) {
	ServerChatConfig_SetMessageTypeFilter(pEntity, pchTabGroup, iTab, pchFilterName, bFiltered);
}

// Set the filtering of a channel for the given tab group tab
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void cmdServerChatConfig_SetChannelFilter(Entity *pEntity, const char *pchTabGroup, int iTab, const char *pchFilterName, bool bFiltered) {
	ServerChatConfig_SetChannelFilter(pEntity, pchTabGroup, iTab, pchFilterName, bFiltered);
}

// Set all channel & message type filters
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void cmdServerChatConfig_SetAllFilters(Entity *pEntity, const char *pchTabGroup, int iTab, bool bFiltered) {
	ServerChatConfig_SetAllFilters(pEntity, pchTabGroup, iTab, bFiltered);
}

// Sets whether chat log entries should include dates
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(Chat_ShowDate) ACMD_PRIVATE;
void cmdServerChatConfig_SetShowDate(Entity *pEntity, bool bShowDate) {
	ServerChatConfig_SetShowDate(pEntity, bShowDate);
}

// Sets whether chat log entries should include timestamps
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(Chat_ShowTime) ACMD_PRIVATE;
void cmdServerChatConfig_SetShowTime(Entity *pEntity, bool bShowTime) {
	ServerChatConfig_SetShowTime(pEntity, bShowTime);
}

// Sets whether chat log entries should always show message types
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(Chat_ShowMessageTypeNames) ACMD_PRIVATE;
void cmdServerChatConfig_SetShowMessageTypeNames(Entity *pEntity, bool bShowMessageTypeNames) {
	ServerChatConfig_SetShowMessageTypeNames(pEntity, bShowMessageTypeNames);
}

// Sets whether chat log entries with links to players should show @handles or not.
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(Chat_ShowFullNames) ACMD_PRIVATE;
void cmdServerChatConfig_SetShowFullNames(Entity *pEntity, bool bShowFullNames) {
	ServerChatConfig_SetShowFullNames(pEntity, bShowFullNames);
}

// Sets whether chat log entries should be filtered for profanity
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(Chat_ProfanityFilter) ACMD_PRIVATE;
void cmdServerChatConfig_SetProfanityFilter(Entity *pEntity, bool bProfanityFilter) {
	ServerChatConfig_SetProfanityFilter(pEntity, bProfanityFilter);
}

// Sets whether chat log entries should show the channel names
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(Chat_ShowChannelNames) ACMD_PRIVATE;
void cmdServerChatConfig_SetShowChannelNames(Entity *pEntity, bool bShowChannelNames) {
	ServerChatConfig_SetShowChannelNames(pEntity, bShowChannelNames);
}

// Sets whether we should be subscribed to the shard global channel
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(Chat_GlobalChannelSubscribe) ACMD_PRIVATE;
void cmdServerChatConfig_GlobalChannelSubscribe(Entity *pEntity, bool bSubscribe) {
	ServerChatConfig_GlobalChannelSubscribe(pEntity, bSubscribe);
}

// Sets whether the auto-complete suggestion list shows annotations, when available
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(Chat_SetAnnotateAutoComplete) ACMD_PRIVATE;
void cmdServerChatConfig_SetAnnotateAutoComplete(Entity *pEntity, bool bAnnotateAutoComplete) {
	ServerChatConfig_SetAnnotateAutoComplete(pEntity, bAnnotateAutoComplete);
}

// Sets font scale for chat entries
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(Chat_SetFontScale) ACMD_PRIVATE;
void cmdServerChatConfig_SetFontScale(Entity *pEntity, F32 fFontSize) {
	ServerChatConfig_SetFontScale(pEntity, fFontSize);
}

// Sets chat window alpha when active
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(Chat_SetActiveWindowAlpha) ACMD_PRIVATE;
void cmdServerChatConfig_SetActiveWindowAlpha(Entity *pEntity, F32 fFontSize) {
	ServerChatConfig_SetActiveWindowAlpha(pEntity, fFontSize);
}

// Sets font scale for chat entries
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(Chat_SetInactiveWindowAlpha) ACMD_PRIVATE;
void cmdServerChatConfig_SetInactiveWindowAlpha(Entity *pEntity, F32 fFontSize) {
	ServerChatConfig_SetInactiveWindowAlpha(pEntity, fFontSize);
}

// Sets the fade away duration for chat entries
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(Chat_SetFadeAwayDuration) ACMD_PRIVATE;
void cmdServerChatConfig_SetFadeAwayDuration(Entity *pEntity, F32 fFadeAwayDuration) {
	ServerChatConfig_SetFadeAwayDuration(pEntity, fFadeAwayDuration);
}

// Sets the time required for chat entries to start fading away
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(Chat_SetTimeRequiredToStartFading) ACMD_PRIVATE;
void cmdServerChatConfig_SetTimeRequiredToStartFading(Entity *pEntity, F32 fTimeRequiredToStartFading) {
	ServerChatConfig_SetTimeRequiredToStartFading(pEntity, fTimeRequiredToStartFading);
}

// Set the current chat input channel
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void cmdServerChatConfig_SetCurrentInputChannel(Entity *pEntity, const char *pchTabGroup, S32 iTabIndex, const char *pchChannel) {
	ServerChatConfig_SetCurrentInputChannel(pEntity, pchTabGroup, iTabIndex, pchChannel);
}

// Set the current tab group
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void cmdServerChatConfig_SetCurrentTab(Entity *pEntity, const char *pchTabGroup, S32 iTabIndex) {
	ServerChatConfig_SetCurrentTab(pEntity, pchTabGroup, iTabIndex);
}

// Set the color for a channel or message type
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_PRIVATE;
void cmdServerChatConfig_SetChannelColor(Entity *pEntity, const char *pchSystemName, bool bIsChannel, U32 iColor) {
	ServerChatConfig_SetChannelColor(pEntity, pchSystemName, bIsChannel, iColor);
}

// Store the visibility of the chat window
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void cmdServerChatConfig_SetChatVisible(Entity *pEntity, bool bVisible) {
	ServerChatConfig_SetChatVisible(pEntity, bVisible);
}

// Set the status message
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void cmdServerChatConfig_SetStatus(Entity *pEntity, const ACMD_SENTENCE pchMessage) {
	if (pEntity && pEntity->pPlayer)
	{
		ServerChatConfig_SetStatusMessage(pEntity, pchMessage);		
	}
}

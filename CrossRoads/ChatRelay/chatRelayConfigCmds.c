#include "chatCommon.h"
#include "chatCommonStructs.h"
#include "chatRelay.h"

#include "AutoGen/ChatRelay_autotransactions_autogen_wrappers.h"
#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"
#include "AutoGen/GameClientLib_autogen_GenericClientCmdWrappers.h"

#define CMDDATA_TO_RELAYUSER ChatRelayUser *relayUser = (ChatRelayUser*) pCmdData

static void crChatConfig_UpdateClientGADChatConfig (TransactionReturnVal *returnVal, void *userData)
{
	U32 uAccountID = (U32)(intptr_t) userData;
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		RemoteCommand_DBReturnGameAccount(GLOBALTYPE_OBJECTDB, 0, GetAppGlobalType(), GetAppGlobalID(), uAccountID);
	}
}
static void crChatConfig_GADChatConfigCB_NoSuccessReturn(TransactionReturnVal *returnVal, void *userData)
{
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_FAILURE)
	{
		U32 uAccountID = (U32)(intptr_t) userData;
		RemoteCommand_DBReturnGameAccount(GLOBALTYPE_OBJECTDB, 0, GetAppGlobalType(), GetAppGlobalID(), uAccountID);
	}
}

static TransactionReturnVal *crChatConfig_CreateNoSuccessReturnVal(ChatRelayUser *relayUser)
{
	return objCreateManagedReturnVal(crChatConfig_GADChatConfigCB_NoSuccessReturn, (void*)(intptr_t) relayUser->uAccountID);
}
static TransactionReturnVal *crChatConfig_CreateReturnVal(ChatRelayUser *relayUser)
{
	return objCreateManagedReturnVal(crChatConfig_UpdateClientGADChatConfig, (void*)(intptr_t) relayUser->uAccountID);
}

// Initialize/Reset defaults for GAD ChatConfig
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crChatConfig_Reset(GenericServerCmdData *pCmdData)
{
	CMDDATA_TO_RELAYUSER;
	U32 uCurTime = timeSecondsSince2000();
	if ((uCurTime - relayUser->uLastConfigInitTime) > CHAT_CONFIG_RESET_TIME)
	{
		NOCONST(ChatConfig) *pChatConfig = StructCreateNoConst(parse_ChatConfig);
		relayUser->uLastConfigInitTime = uCurTime;

		// Force defaults to be loaded if they aren't already
		ChatConfigCommon_InitToDefault(pChatConfig, relayUser->uSource, relayUser->eLanguage);
		GClientCmd_gclClientChat_UpdateGADChatConfig(relayUser->uAccountID, (ChatConfig*) pChatConfig);
		AutoTrans_tr_crSetAccountChatConfig(crChatConfig_CreateReturnVal(relayUser), GLOBALTYPE_CHATRELAY, GLOBALTYPE_GAMEACCOUNTDATA, relayUser->uAccountID, (ChatConfig*) pChatConfig);
	}
}

// Set the color for a channel
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crChatConfig_SetChannelColor(GenericServerCmdData *pCmdData, const char *pchSystemName, bool bIsChannel, U32 iColor)
{
	CMDDATA_TO_RELAYUSER;
	ChatLogEntryType eLogType = bIsChannel ? kChatLogEntryType_Channel : StaticDefineIntGetIntDefault(ChatLogEntryTypeEnum, pchSystemName, kChatLogEntryType_Channel);
	U32 iDefaultColor = ChatCommon_GetChatColor(NULL, eLogType, pchSystemName, relayUser->uSource);
	
	if (bIsChannel)
		AutoTrans_tr_crChatConfig_SetChannelColor(crChatConfig_CreateReturnVal(relayUser), GLOBALTYPE_CHATRELAY, GLOBALTYPE_GAMEACCOUNTDATA, relayUser->uAccountID, pchSystemName, iColor, iDefaultColor);
	else
		AutoTrans_tr_crChatConfig_SetMessageColor(crChatConfig_CreateReturnVal(relayUser), GLOBALTYPE_CHATRELAY, GLOBALTYPE_GAMEACCOUNTDATA, relayUser->uAccountID, pchSystemName, iColor, iDefaultColor);
}

////////////////////////////////
// Tab and Tab Group Commands

// Create a new chat tab group
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crChatConfig_CreateTabGroup(GenericServerCmdData *pCmdData, char *pchTabGroup, bool bInitTabs)
{
	CMDDATA_TO_RELAYUSER;
	AutoTrans_tr_crChatConfig_CreateTabGroup(crChatConfig_CreateReturnVal(relayUser), GLOBALTYPE_CHATRELAY, GLOBALTYPE_GAMEACCOUNTDATA, relayUser->uAccountID,
		pchTabGroup, bInitTabs);
}

// Delete an existing chat tab group
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crChatConfig_DeleteTabGroup(GenericServerCmdData *pCmdData, char *pchTabGroup)
{
	CMDDATA_TO_RELAYUSER;
	AutoTrans_tr_crConfig_DeleteTabGroup(crChatConfig_CreateReturnVal(relayUser), GLOBALTYPE_CHATRELAY, GLOBALTYPE_GAMEACCOUNTDATA, relayUser->uAccountID, pchTabGroup);
}

// Reset a chat tab group (initializes the tabs to the default)
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crChatConfig_ResetTabGroup(GenericServerCmdData *pCmdData, char *pchTabGroup)
{
	CMDDATA_TO_RELAYUSER;
	AutoTrans_tr_crChatConfig_ResetTabGroup(crChatConfig_CreateReturnVal(relayUser), GLOBALTYPE_CHATRELAY, GLOBALTYPE_GAMEACCOUNTDATA, relayUser->uAccountID, pchTabGroup);
}

// Create a new chat tab
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crChatConfig_CreateTab(GenericServerCmdData *pCmdData, char *pchTabGroup)
{
	CMDDATA_TO_RELAYUSER;
	AutoTrans_tr_crChatConfig_CreateTab(crChatConfig_CreateReturnVal(relayUser), GLOBALTYPE_CHATRELAY, GLOBALTYPE_GAMEACCOUNTDATA, relayUser->uAccountID, pchTabGroup);
}

// Copy a chat tab across tab groups
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crChatConfig_CopyTabEx(GenericServerCmdData *pCmdData, char *pchDstTabGroup, char *pchDstName, char *pchSrcTabGroup, int iSrcTab)
{
	CMDDATA_TO_RELAYUSER;
	AutoTrans_tr_crChatConfig_CopyTab(crChatConfig_CreateReturnVal(relayUser), GLOBALTYPE_CHATRELAY, GLOBALTYPE_GAMEACCOUNTDATA, relayUser->uAccountID, 
		pchDstTabGroup, pchDstName, pchSrcTabGroup, iSrcTab);
}

// Delete a chat tab
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crChatConfig_DeleteTab(GenericServerCmdData *pCmdData, char *pchTabGroup, int iTab)
{
	CMDDATA_TO_RELAYUSER;
	AutoTrans_tr_crChatConfig_DeleteTab(crChatConfig_CreateReturnVal(relayUser), GLOBALTYPE_CHATRELAY, GLOBALTYPE_GAMEACCOUNTDATA, relayUser->uAccountID, 
		pchTabGroup, iTab);
}

// Rename a tab
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crChatConfig_SetTabName(GenericServerCmdData *pCmdData, char *pchTabGroup, int iTab, char *pchName)
{
	CMDDATA_TO_RELAYUSER;
	AutoTrans_tr_crChatConfig_SetTabName(crChatConfig_CreateNoSuccessReturnVal(relayUser), GLOBALTYPE_CHATRELAY, GLOBALTYPE_GAMEACCOUNTDATA, relayUser->uAccountID, 
		pchTabGroup, iTab, pchName);
}

// Move a tab
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crChatConfig_MoveTab(GenericServerCmdData *pCmdData, const char *pchFromGroupName, S32 iFromIndex, const char *pchToGroupName, S32 iToIndex)
{
	CMDDATA_TO_RELAYUSER;
	AutoTrans_tr_crChatConfig_MoveTab(crChatConfig_CreateNoSuccessReturnVal(relayUser), GLOBALTYPE_CHATRELAY, GLOBALTYPE_GAMEACCOUNTDATA, relayUser->uAccountID, 
		pchFromGroupName, iFromIndex, pchToGroupName, iToIndex);
}


////////////////////////////////
// Filter Commands

AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crChatConfig_SetTabFilterMode(GenericServerCmdData *pCmdData, char *pchTabGroup, int iTab, bool bExclusiveFilter)
{
	CMDDATA_TO_RELAYUSER;
	AutoTrans_tr_crChatConfig_SetTabFilterMode(crChatConfig_CreateReturnVal(relayUser), GLOBALTYPE_CHATRELAY, GLOBALTYPE_GAMEACCOUNTDATA, relayUser->uAccountID, 
		pchTabGroup, iTab, bExclusiveFilter);
}

// Set the filtering of a message type for the given tab group tab
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crChatConfig_SetMessageTypeFilter(GenericServerCmdData *pCmdData, const char *pchTabGroup, int iTab, const char *pchFilterName, bool bFiltered)
{
	CMDDATA_TO_RELAYUSER;
	AutoTrans_tr_crChatConfig_SetMessageTypeFilter(crChatConfig_CreateNoSuccessReturnVal(relayUser), GLOBALTYPE_CHATRELAY, GLOBALTYPE_GAMEACCOUNTDATA, relayUser->uAccountID, 
		pchTabGroup, iTab, pchFilterName, bFiltered);
}

// Set the filtering of a channel for the given tab group tab
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crChatConfig_SetChannelFilter(GenericServerCmdData *pCmdData, const char *pchTabGroup, int iTab, const char *pchFilterName, bool bFiltered)
{
	CMDDATA_TO_RELAYUSER;
	AutoTrans_tr_crChatConfig_SetChannelFilter(crChatConfig_CreateNoSuccessReturnVal(relayUser), GLOBALTYPE_CHATRELAY, GLOBALTYPE_GAMEACCOUNTDATA, relayUser->uAccountID, 
		pchTabGroup, iTab, pchFilterName, bFiltered);
}

// Set all channel & message type filters
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crChatConfig_SetAllFilters(GenericServerCmdData *pCmdData, const char *pchTabGroup, int iTab, bool bFiltered)
{
	CMDDATA_TO_RELAYUSER;
	AutoTrans_tr_crChatConfig_SetAllFilters(crChatConfig_CreateReturnVal(relayUser), GLOBALTYPE_CHATRELAY, GLOBALTYPE_GAMEACCOUNTDATA, relayUser->uAccountID, 
		pchTabGroup, iTab, bFiltered);
}

// Set the current chat input channel
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crChatConfig_SetDefaultInputChannel(GenericServerCmdData *pCmdData, const char *pchTabGroup, S32 iTab, const char *pchChannel)
{
	CMDDATA_TO_RELAYUSER;
	AutoTrans_tr_crChatConfig_SetDefaultInputChannel(NULL, GLOBALTYPE_CHATRELAY, GLOBALTYPE_GAMEACCOUNTDATA, relayUser->uAccountID, 
		pchTabGroup, iTab, pchChannel);
}

////////////////////////////////
// Other Settings

// Sets whether chat log entries should include dates
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crChatConfig_SetShowDate(GenericServerCmdData *pCmdData, bool bShowDate)
{
	CMDDATA_TO_RELAYUSER;
	AutoTrans_tr_crChatConfig_SetShowDate(NULL, GLOBALTYPE_CHATRELAY, GLOBALTYPE_GAMEACCOUNTDATA, relayUser->uAccountID, bShowDate);
}

// Sets whether chat log entries should include timestamps
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crChatConfig_SetShowTime(GenericServerCmdData *pCmdData, bool bShowTime) 
{
	CMDDATA_TO_RELAYUSER;
	AutoTrans_tr_crChatConfig_SetShowTime(NULL, GLOBALTYPE_CHATRELAY, GLOBALTYPE_GAMEACCOUNTDATA, relayUser->uAccountID, bShowTime);
}

// Sets whether chat log entries should always show message types
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crChatConfig_SetShowMessageTypeNames(GenericServerCmdData *pCmdData, bool bShowMessageTypeNames) 
{
	CMDDATA_TO_RELAYUSER;
	AutoTrans_tr_crChatConfig_SetShowMessageTypeNames(NULL, GLOBALTYPE_CHATRELAY, GLOBALTYPE_GAMEACCOUNTDATA, relayUser->uAccountID, bShowMessageTypeNames);
}

// Sets whether chat log entries with links to players should show @handles or not.
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crChatConfig_SetShowFullNames(GenericServerCmdData *pCmdData, bool bShowFullNames) 
{
	CMDDATA_TO_RELAYUSER;
	AutoTrans_tr_crChatConfig_SetShowFullNames(NULL, GLOBALTYPE_CHATRELAY, GLOBALTYPE_GAMEACCOUNTDATA, relayUser->uAccountID, bShowFullNames);
}

// Sets whether chat log entries should be filtered for profanity
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crChatConfig_SetProfanityFilter(GenericServerCmdData *pCmdData, bool bProfanityFilter) 
{
	CMDDATA_TO_RELAYUSER;
	AutoTrans_tr_crChatConfig_SetProfanityFilter(NULL, GLOBALTYPE_CHATRELAY, GLOBALTYPE_GAMEACCOUNTDATA, relayUser->uAccountID, bProfanityFilter);
}

// Sets whether chat log entries should show the channel names
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crChatConfig_SetShowChannelNames(GenericServerCmdData *pCmdData, bool bShowChannelNames) 
{
	CMDDATA_TO_RELAYUSER;
	AutoTrans_tr_crChatConfig_SetShowChannelNames(NULL, GLOBALTYPE_CHATRELAY, GLOBALTYPE_GAMEACCOUNTDATA, relayUser->uAccountID, bShowChannelNames);
}

// Sets whether chat log entries should be filtered for profanity
/*AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crChatConfig_GlobalChannelSubscribe(GenericServerCmdData *pCmdData, bool bSubscribe) 
{
	CMDDATA_TO_RELAYUSER;
	AutoTrans_tr_crChatConfig_GlobalChannelSubscribe(crChatConfig_CreateReturnVal(relayUser), GLOBALTYPE_CHATRELAY, GLOBALTYPE_GAMEACCOUNTDATA, relayUser->uAccountID, bSubscribe);
}*/

// Sets whether the auto-complete suggestion list shows annotations, when available
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crChatConfig_SetAnnotateAutoComplete(GenericServerCmdData *pCmdData, bool bAnnotateAutoComplete) 
{
	CMDDATA_TO_RELAYUSER;
	AutoTrans_tr_crChatConfig_SetAnnotateAutoComplete(NULL, GLOBALTYPE_CHATRELAY, GLOBALTYPE_GAMEACCOUNTDATA, relayUser->uAccountID, bAnnotateAutoComplete);
}

// Sets font scale for chat entries
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crChatConfig_SetFontScale(GenericServerCmdData *pCmdData, F32 fFontSize) 
{
	CMDDATA_TO_RELAYUSER;
	AutoTrans_tr_crChatConfig_SetFontScale(NULL, GLOBALTYPE_CHATRELAY, GLOBALTYPE_GAMEACCOUNTDATA, relayUser->uAccountID, fFontSize);
}

// Sets chat window alpha when active
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crChatConfig_SetActiveWindowAlpha(GenericServerCmdData *pCmdData, F32 fFontSize) 
{
	CMDDATA_TO_RELAYUSER;
	AutoTrans_tr_crChatConfig_SetActiveWindowAlpha(NULL, GLOBALTYPE_CHATRELAY, GLOBALTYPE_GAMEACCOUNTDATA, relayUser->uAccountID, fFontSize);
}

// Sets font scale for chat entries
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crChatConfig_SetInactiveWindowAlpha(GenericServerCmdData *pCmdData, F32 fFontSize) 
{
	CMDDATA_TO_RELAYUSER;
	AutoTrans_tr_crChatConfig_SetInactiveWindowAlpha(NULL, GLOBALTYPE_CHATRELAY, GLOBALTYPE_GAMEACCOUNTDATA, relayUser->uAccountID, fFontSize);
}

// Sets the fade away duration for chat entries
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crChatConfig_SetFadeAwayDuration(GenericServerCmdData *pCmdData, F32 fFadeAwayDuration) 
{
	CMDDATA_TO_RELAYUSER;
	AutoTrans_tr_crChatConfig_SetFadeAwayDuration(NULL, GLOBALTYPE_CHATRELAY, GLOBALTYPE_GAMEACCOUNTDATA, relayUser->uAccountID, fFadeAwayDuration);
}

// Sets the time required for chat entries to start fading away
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crChatConfig_SetTimeRequiredToStartFading(GenericServerCmdData *pCmdData, F32 fTimeRequiredToStartFading) 
{
	CMDDATA_TO_RELAYUSER;
	AutoTrans_tr_crChatConfig_SetTimeRequiredToStartFading(NULL, GLOBALTYPE_CHATRELAY, GLOBALTYPE_GAMEACCOUNTDATA, relayUser->uAccountID, fTimeRequiredToStartFading);
}

// Store the visibility of the chat window
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crChatConfig_SetChatVisible(GenericServerCmdData *pCmdData, bool bVisible)
{
	CMDDATA_TO_RELAYUSER;
	AutoTrans_tr_crChatConfig_SetChatVisible(NULL, GLOBALTYPE_CHATRELAY, GLOBALTYPE_GAMEACCOUNTDATA, relayUser->uAccountID, bVisible);
}


// Set the status message
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crChatConfig_SetStatus(GenericServerCmdData *pCmdData, const ACMD_SENTENCE pchMessage)
{
	CMDDATA_TO_RELAYUSER;
	AutoTrans_tr_crChatConfig_SetStatusMessage(crChatConfig_CreateNoSuccessReturnVal(relayUser), GLOBALTYPE_CHATRELAY, GLOBALTYPE_GAMEACCOUNTDATA, relayUser->uAccountID, pchMessage);
}

/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef SERVERCHATCONFIG_H_
#define SERVERCHATCONFIG_H_

typedef enum ChatLogEntryType ChatLogEntryType;
typedef struct ChatConfig ChatConfig;
typedef struct Entity Entity;
typedef struct NOCONST(ChatConfig) NOCONST(ChatConfig);

void ServerChatConfig_CreateTabGroup(Entity *pEntity, char *pchTabGroup, bool bInitTabs);
void ServerChatConfig_DeleteTabGroup(Entity *pEntity, char *pchTabGroup);
void ServerChatConfig_ResetTabGroup(Entity *pEntity, char *pchTabGroup);
void ServerChatConfig_CreateTab(Entity *pEntity, char *pchTabGroup);
void ServerChatConfig_CopyTab(Entity *pEntity, char *pchDstTabGroup, char *pchDstName, char *pchSrcTabGroup, int iSrcTab);
void ServerChatConfig_DeleteTab(Entity *pEntity, char *pchTabGroup, int iTab);
SA_RET_OP_VALID NOCONST(ChatConfig) *ServerChatConfig_GetChatConfig(Entity *pEntity);
void ServerChatConfig_MoveTab(Entity *pEntity, const char *pchFromGroupName, S32 iFromIndex, const char *pchToGroupName, S32 iToIndex);
void ServerChatConfig_PlayerUpdate(Entity *pEntity);
void ServerChatConfig_Reset(Entity *pEntity);
void ServerChatConfig_SetActiveWindowAlpha(SA_PARAM_OP_VALID Entity *pEntity, F32 fAlpha);
void ServerChatConfig_SetAllFilters(Entity *pEntity, const char *pchTabGroup, int iTab, bool bFiltered);
void ServerChatConfig_SetAnnotateAutoComplete(Entity *pEntity, bool bAnnotateAutoComplete);
void ServerChatConfig_SetChannelColor(Entity *pEntity, const char *pchSystemName, bool bIsChannel, U32 iColor);
void ServerChatConfig_SetChannelFilter(Entity *pEntity, const char *pchTabGroup, int iTab, const char *pchFilterName, bool bFiltered);
void ServerChatConfig_SetCurrentInputChannel(Entity *pEntity, const char *pchTabGroup, S32 iTab, const char *pchChannel);
void ServerChatConfig_SetCurrentTab(Entity *pEntity, const char *pchTabGroup, S32 iTabIndex);
void ServerChatConfig_SetFontScale(SA_PARAM_OP_VALID Entity *pEntity, F32 fFontScale);
void ServerChatConfig_SetInactiveWindowAlpha(SA_PARAM_OP_VALID Entity *pEntity, F32 fAlpha);
void ServerChatConfig_SetFadeAwayDuration(SA_PARAM_OP_VALID Entity *pEntity, F32 fFadeAwayDuration);
void ServerChatConfig_SetTimeRequiredToStartFading(SA_PARAM_OP_VALID Entity *pEntity, F32 fTimeRequiredToStartFading);
void ServerChatConfig_SetMessageTypeFilter(Entity *pEntity, const char *pchTabGroup, int iTab, const char *pchFilterName, bool bFiltered);
void ServerChatConfig_SetProfanityFilter(Entity *pEntity, bool bProfanityFilter);
void ServerChatConfig_SetShowChannelNames(Entity *pEntity, bool bShowChannelNames);
void ServerChatConfig_GlobalChannelSubscribe(Entity *pEntity, bool bSubscribe);
void ServerChatConfig_SetShowDate(Entity *pEntity, bool bShowDate);
void ServerChatConfig_SetShowMessageTypeNames(Entity *pEntity, bool bShowMessageTypeNames);
void ServerChatConfig_SetShowFullNames(Entity *pEntity, bool bShowFullNames);
void ServerChatConfig_SetShowTime(Entity *pEntity, bool bShowTime);
void ServerChatConfig_SetTabFilterMode(Entity *pEntity, char *pchTabGroup, int iTab, bool bExclusiveFilter);
void ServerChatConfig_SetTabName(Entity *pEntity, char *pchTabGroup, int iTab, char *pchName);
void ServerChatConfig_SetStatusMessage(SA_PARAM_OP_VALID Entity *pEntity, const char *pchMessage);
void ServerChatConfig_SetChatVisible(Entity *pEntity, bool bVisible);

#endif // SERVERCHATCONFIG_H_

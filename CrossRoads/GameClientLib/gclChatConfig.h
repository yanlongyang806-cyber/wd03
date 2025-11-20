/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef _CLIENTCHATCONFIG_H_
#define _CLIENTCHATCONFIG_H_
GCC_SYSTEM

AUTO_STRUCT;
typedef struct ChatConfigTabFilterDef {
	char *pchChatTabGroupName;		AST(ESTRING NAME(ChatWindowName))
	S32  iTab;						AST(NAME(TabID))
	char *pchFilterDisplayName;		AST(KEY ESTRING NAME(FilterDisplayName))
	char *pchFilterSystemName;		AST(ESTRING NAME(FilterSystemName))
	ChatLogEntryType kMessageType;
	U32  iColor;
	Vec4 vColor; AST(NAME(ColorVec))
	bool bFiltered : 1;
	bool bMayUnsubscribe : 1;
	bool bIsChannel : 1;
} ChatConfigTabFilterDef;

AUTO_STRUCT;
typedef struct ChatConfigInputChannel {
	char *pchSystemName; AST(NAME(SystemName) ESTRING)
	char *pchDisplayName; AST(NAME(DisplayName) ESTRING)
	bool bIsBuiltIn : 1;
	bool bIsSubscribed : 1;
	U32 iColor;
	char pchColorHex[9]; AST(NAME(ColorHex))
	U32 uLastUpdateTime;
} ChatConfigInputChannel;

bool ClientChatConfig_UsingEntityConfig(void);
SA_RET_OP_VALID extern ChatConfig *ClientChatConfig_GetChatConfig(SA_PARAM_OP_VALID Entity *pEntity);
SA_RET_NN_VALID extern ChatConfigInputChannel *ClientChatConfig_GetCurrentInputChannel(void);
SA_RET_NN_VALID const char* ClientChatConfig_GetCurrentTabGroupName();
S32 ClientChatConfig_GetCurrentTabIndex();
const char *gclChatConfig_FilterProfanity(SA_PARAM_OP_VALID Entity* pEntity, const char *pchText);
void ClientChatConfig_SetCurrentChannelForTab(const char *pchTabGroupName, int iTabIndex, const char *pchChannelName);

#endif //_CLIENTCHATCONFIG_H_

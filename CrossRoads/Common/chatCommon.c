#include "chatCommon.h"
#include "chatCommonStructs.h"
#include "ChatData.h"
#include "entity.h"
#include "Color.h"
#include "FolderCache.h"
#include "GameStringFormat.h"
#include "Player.h"
#include "StringUtil.h"
#include "EString.h"
#include "utilitiesLib.h"
#include "GlobalTypes.h"
#include "SimpleParser.h"
#include "GlobalEnums.h"
#include "HashFunctions.h"
#include "TextFilter.h"
#if GAMESERVER
#include "GameServerLib.h"
#include "netprivate.h"
#endif

#include "Autogen/chatCommon_h_ast.h"
#include "Autogen/chatCommonStructs_h_ast.h"
#include "Autogen/ChatData_h_ast.h"
#include "AutoGen/AppLocale_h_ast.h"

#define FALLBACK_DEFAULT_CHAT_COLOR 0xffffffff

static ChatConfigDefaults s_ConfigDefaults;
static ChatLogEntryType *s_ChatTypeList = NULL;
static ChatLogEntryType *s_FilterableMessageTypes = NULL;

// Chat config defaults for GameAccountData
static ChatConfigDefaults s_ConfigAccountDefaults;
// Chat config defaults for XBOX
static ChatConfigDefaults s_ConfigDefaultsXBox;

static bool s_bDefaultConfigLoaded = false;
static ChatCommonBuiltInMessageTypeInfo **s_eaBuiltInMessageTypeInfos = NULL;

void ChatCommon_GetMessageTypeDisplayNameKey(char **ppchMessageTypeDisplayNameKey, const char *pchMessageTypeName) {
	estrPrintf(ppchMessageTypeDisplayNameKey, "%s%s", "ChatConfig_MessageType_", pchMessageTypeName);
}

static ChatCommonBuiltInMessageTypeInfo*** ChatCommon_GetBuiltInMessageTypeInfos() {
	static char pcHelpChannelName[512] = "";
	static char pcLifetimeChannelName[512] = "";
	if (s_eaBuiltInMessageTypeInfos == NULL) {
		char *pchTmp = NULL;
		int i;
		for (i=1; ChatLogEntryTypeEnum[i].key && i < kChatLogEntryType_Count + 1; i++) {
			ChatCommonBuiltInMessageTypeInfo *pInfo = StructCreate(parse_ChatCommonBuiltInMessageTypeInfo);
			pInfo->kType = ChatLogEntryTypeEnum[i].value;
			pInfo->pchTypeName = ChatLogEntryTypeEnum[i].key;

			ChatCommon_GetMessageTypeDisplayNameKey(&pchTmp, pInfo->pchTypeName);
			pInfo->pchDisplayNameKey = strdup(pchTmp);

			switch (pInfo->kType) {
				case kChatLogEntryType_Zone:
					pInfo->pchChannelPrefix = ZONE_CHANNEL_NAME;
					break;
				case kChatLogEntryType_Team:
					pInfo->pchChannelPrefix = TEAM_CHANNEL_PREFIX;
					break;
				case kChatLogEntryType_TeamUp:
					pInfo->pchChannelPrefix = TEAMUP_CHANNEL_PREFIX;
					break;
				case kChatLogEntryType_Guild:
					pInfo->pchChannelPrefix = GUILD_CHANNEL_PREFIX;
					break;
				case kChatLogEntryType_Private:
					pInfo->pchChannelPrefix = PRIVATE_CHANNEL_NAME;
					break;
				case kChatLogEntryType_Private_Sent:
					pInfo->pchChannelPrefix = PRIVATE_CHANNEL_SENT_NAME;
					break;
				case kChatLogEntryType_Officer:
					pInfo->pchChannelPrefix = OFFICER_CHANNEL_PREFIX;
					break;
				//case kChatLogEntryType_Help:
				//	if (!*pcHelpChannelName)
				//	{
				//		strcat(pcHelpChannelName, GetShortProductName());
				//		strcat(pcHelpChannelName, SHARD_HELP_CHANNEL_NAME);
				//	}
				//	pInfo->pchChannelPrefix = pcHelpChannelName;
				//	break;
				//case kChatLogEntryType_Lifetime:
				//	if (!*pcLifetimeChannelName)
				//	{
				//		strcat(pcLifetimeChannelName, GetShortProductName());
				//		strcat(pcLifetimeChannelName, SHARD_LIFETIME_CHANNEL_NAME);
				//	}
				//	pInfo->pchChannelPrefix = pcLifetimeChannelName;
				//	break;
				xcase kChatLogEntryType_Match:
					pInfo->pchChannelPrefix = QUEUE_CHANNEL_PREFIX;
				xcase kChatLogEntryType_Global:
					pInfo->pchChannelPrefix = SHARD_GLOBAL_CHANNEL_NAME;
					break;
				default:
					pInfo->pchChannelPrefix = NULL;
					break;
			}
			
			eaPush(&s_eaBuiltInMessageTypeInfos, pInfo);
		}
		estrDestroy(&pchTmp);
	}

	return &s_eaBuiltInMessageTypeInfos;
}

static ChatCommonBuiltInMessageTypeInfo *ChatCommon_GetBuiltInMessageTypeInfo(ChatLogEntryType kType) {
	ChatCommonBuiltInMessageTypeInfo ***peaInfos = ChatCommon_GetBuiltInMessageTypeInfos();
	return eaGet(peaInfos, kType);
}

const char *ChatCommon_GetMessageTypeDisplayNameKeyByType(ChatLogEntryType kType) {
	ChatCommonBuiltInMessageTypeInfo *pInfo = ChatCommon_GetBuiltInMessageTypeInfo(kType);
	return pInfo->pchDisplayNameKey;
}

static void ChatCommon_ReloadDefaults(const char *pchPath, S32 iWhen) {
	ChatLogEntryTypeList chatTypeList = {0};
	StructReset(parse_ChatConfigDefaults, &s_ConfigDefaults);
	StructReset(parse_ChatConfigDefaults, &s_ConfigAccountDefaults);
	StructReset(parse_ChatConfigDefaults, &s_ConfigDefaultsXBox);
	loadstart_printf("Loading chat defaults... ");
	ParserLoadFiles(NULL, "defs/config/ChatDefaults.def", "ChatDefaults.bin", 0, parse_ChatConfigDefaults, &s_ConfigDefaults);
	ParserLoadFiles(NULL, "defs/config/ChatAccountDefaults.def", "ChatAccountDefaults.bin", 0, parse_ChatConfigDefaults, &s_ConfigAccountDefaults);
	ParserLoadFiles(NULL, "defs/config/ChatDefaultsXBox.def", "ChatDefaultsXBox.bin", PARSER_OPTIONALFLAG, parse_ChatConfigDefaults, &s_ConfigDefaultsXBox);
	ParserLoadFiles(NULL, "defs/config/ChatTypeList.def", "ChatTypeList.bin", 0, parse_ChatLogEntryTypeList, &chatTypeList);
	eaiDestroy(&s_FilterableMessageTypes);
	eaiDestroy(&s_ChatTypeList);
	EARRAY_CONST_FOREACH_BEGIN(chatTypeList.eaTypeList, i, s);
	{
		eaiPush(&s_ChatTypeList, chatTypeList.eaTypeList[i]->eType);
	}
	EARRAY_FOREACH_END;
	StructDeInit(parse_ChatLogEntryTypeList, &chatTypeList);
	loadend_printf("done.");
}

AUTO_STARTUP(Chat);
void ChatCommon_LoadDefaults(void) {
	ChatCommon_ReloadDefaults("", 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/ChatDefaults.def", ChatCommon_ReloadDefaults);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/ChatAccountDefaults.def", ChatCommon_ReloadDefaults);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/ChatTypeList.def", ChatCommon_ReloadDefaults);
}

DefaultChatConfigSource ChatCommon_GetChatConfigSourceForEntity(SA_PARAM_OP_VALID Entity *pEntity)
{
#if GAMECLIENT
	#if _XBOX
	return CHAT_CONFIG_SOURCE_XBOX;
	#else
	return CHAT_CONFIG_SOURCE_PC;
	#endif
#elif GAMESERVER
	// Server uses the netlink to check the client type
	if (pEntity &&
		pEntity->pPlayer &&
		pEntity->pPlayer->clientLink &&
		pEntity->pPlayer->clientLink->netLink &&
		pEntity->pPlayer->clientLink->netLink->eClientType == CLIENT_TYPE_XBOX)
	{
		return CHAT_CONFIG_SOURCE_XBOX;
	}
	else
	{
		return CHAT_CONFIG_SOURCE_PC;
	}
#else
	return CHAT_CONFIG_SOURCE_PC;
#endif
}

ChatConfigDefaults *ChatCommon_GetConfigDefaults(DefaultChatConfigSource eSource) 
{
	assert(eSource != CHAT_CONFIG_SOURCE_NONE);

	switch(eSource)
	{
	case CHAT_CONFIG_SOURCE_PC_ACCOUNT:
		return &s_ConfigAccountDefaults;
	case CHAT_CONFIG_SOURCE_PC:
		return &s_ConfigDefaults;
	case CHAT_CONFIG_SOURCE_XBOX:
		return &s_ConfigDefaultsXBox;
	default:
		return NULL;
	}
}

ChatConfig *ChatCommon_GetChatConfig(Entity *pEntity) {
	if (pEntity && pEntity->pPlayer && pEntity->pPlayer->pUI) {
		return pEntity->pPlayer->pUI->pChatConfig;
	}

	return NULL;
}

ChatTabGroupConfig *ChatCommon_GetTabGroupConfig(const ChatConfig *pConfig, const char *pchTabGroup)
{
	if (!pConfig || !pConfig->eaChatTabGroupConfigs || !pchTabGroup)
		return NULL;
	return (ChatTabGroupConfig*) eaIndexedGetUsingString(&pConfig->eaChatTabGroupConfigs, pchTabGroup);
}

ChatTabConfig *ChatCommon_GetTabConfig(const ChatConfig *pConfig, const char *pchTabGroup, S32 iTab)
{
	ChatTabGroupConfig *pTabGroup = ChatCommon_GetTabGroupConfig(pConfig, pchTabGroup);
	if (!pTabGroup || !pTabGroup->eaChatTabConfigs) {
		return NULL;
	}
	return eaGet(&pTabGroup->eaChatTabConfigs, iTab);
}

ChatTabGroupConfig *ChatCommon_GetCurrentTabGroupConfig(ChatConfig *pConfig, DefaultChatConfigSource eDefaultConfigSource) {
	if (!pConfig || !pConfig->eaChatTabGroupConfigs) {
		return NULL;
	}

	if (!pConfig->pchCurrentTabGroup || !*pConfig->pchCurrentTabGroup) {
		ChatTabGroupConfig *pTabGroup = eaGet(&pConfig->eaChatTabGroupConfigs, 0);
		if (devassert(pTabGroup)) {
			if (!pTabGroup->pchName || !*pTabGroup->pchName) {
				ChatConfigDefaults *pDefaults = ChatCommon_GetConfigDefaults(eDefaultConfigSource);
				estrCopy2(&CONTAINER_NOCONST(ChatTabGroupConfig, pTabGroup)->pchName, pDefaults->pchDefaultTabGroupName);
			}

			pConfig->pchCurrentTabGroup = StructAllocString(pTabGroup->pchName);
		}
	}

	return ChatCommon_GetTabGroupConfig(pConfig, pConfig->pchCurrentTabGroup);
}

ChatTabConfig *ChatCommon_GetCurrentTabConfig(ChatConfig *pConfig, DefaultChatConfigSource eDefaultConfigSource) {
	ChatTabGroupConfig *pTabGroup = ChatCommon_GetCurrentTabGroupConfig(pConfig, eDefaultConfigSource);
	if (pTabGroup) {
		devassert(pConfig && pConfig->pchCurrentTabGroup && *pConfig->pchCurrentTabGroup);
		return ChatCommon_GetTabConfig(pConfig, pConfig->pchCurrentTabGroup, pTabGroup->iCurrentTab);
	}

	return NULL;
}

extern S32 ChatCommon_FindTabConfigIndex(ChatTabGroupConfig *pTabGroup, const char *pchTab) {
	if (pTabGroup && pchTab) {
		S32 i;
		for (i = 0; i < eaSize(&pTabGroup->eaChatTabConfigs); i++) {
			if (!stricmp_safe(pTabGroup->eaChatTabConfigs[i]->pchTitle, pchTab))
				return i;
		}
	}
	return -1;
}

extern S32 ChatCommon_AddTabConfig(NOCONST(ChatTabGroupConfig) *pTabGroup, NOCONST(ChatTabConfig) *pTab)
{
	if (pTabGroup && pTab)
		return eaPush(&pTabGroup->eaChatTabConfigs, pTab);
	return -1;
}

extern bool ChatCommon_InsertTabConfig(NOCONST(ChatTabGroupConfig) *pTabGroup, const S32 iIndex, NOCONST(ChatTabConfig) *pTab)
{
	if (pTabGroup && pTab)
	{
		if (eaInsert(&pTabGroup->eaChatTabConfigs, pTab, iIndex))
			return true;
	}
	return false;
}

extern NOCONST(ChatTabConfig) *ChatCommon_RemoveTabConfig(NOCONST(ChatTabGroupConfig) *pTabGroup, const S32 iIndex)
{
	if (pTabGroup)
	{
		NOCONST(ChatTabConfig) *pTab = eaRemove(&pTabGroup->eaChatTabConfigs, iIndex);
		return pTab;
	}
	return NULL;
}

AUTO_TRANS_HELPER_SIMPLE;
bool ChatCommon_IsFilterableMessageType(ChatLogEntryType eType) {
	switch (eType) {
		case kChatLogEntryType_Unknown:
		case kChatLogEntryType_Admin:
		case kChatLogEntryType_Channel:
		case kChatLogEntryType_Error:
		case kChatLogEntryType_Private_Sent:
		case kChatLogEntryType_Spy:
			return false;

		default:
			return true;
	}
}

ChatLogEntryType **ChatCommon_GetFilterableMessageTypes(void)
{		
	if (s_FilterableMessageTypes == NULL) {
		if (eaiSize(&s_ChatTypeList) == 0)
		{
			int i;
			for (i=1; ChatLogEntryTypeEnum[i].key; i++)
			{
				if (ChatCommon_IsFilterableMessageType(ChatLogEntryTypeEnum[i].value))
					eaiPush(&s_FilterableMessageTypes, ChatLogEntryTypeEnum[i].value);
			}
		}
		else
		{
			EARRAY_INT_CONST_FOREACH_BEGIN(s_ChatTypeList, i, n);
			{
				if (ChatCommon_IsFilterableMessageType(s_ChatTypeList[i]))
					eaiPush(&s_FilterableMessageTypes, s_ChatTypeList[i]);
			}
			EARRAY_FOREACH_END;
		}
	}
	return &s_FilterableMessageTypes;
}

bool ChatCommon_IsLogEntryVisibleInTab(ChatTabConfig *pTabConfig, ChatLogEntryType eType, const char *pchChannel)
{
	PERFINFO_AUTO_START_FUNC();
	if (pTabConfig)
	{
		// The following types are always visible
		switch (eType) {
			case kChatLogEntryType_Unknown:
			case kChatLogEntryType_Admin:
			case kChatLogEntryType_Error:
			case kChatLogEntryType_Spy:
				PERFINFO_AUTO_STOP_FUNC();
				return true;
			default:
				// continue on
				break;
		}

		// Treat all private sent messages as Private for filtering purposes
		if (eType == kChatLogEntryType_Private_Sent) {
			eType = kChatLogEntryType_Private;
		}

		if (eType == kChatLogEntryType_Channel) {
			if (eaFindString(&pTabConfig->eaChannels, pchChannel) >= 0) {
				PERFINFO_AUTO_STOP_FUNC();
				return pTabConfig->eChannelFilterMode == kChatTabFilterMode_Inclusive;
			}

			PERFINFO_AUTO_STOP_FUNC();
			return pTabConfig->eChannelFilterMode == kChatTabFilterMode_Exclusive;
		} else {
			pchChannel = ChatLogEntryTypeEnum[eType+1].key;
			if (eaFindString(&pTabConfig->eaMessageTypes, pchChannel) >= 0) {
				PERFINFO_AUTO_STOP_FUNC();
				return pTabConfig->eMessageTypeFilterMode == kChatTabFilterMode_Inclusive;
			}

			PERFINFO_AUTO_STOP_FUNC();
			return pTabConfig->eMessageTypeFilterMode == kChatTabFilterMode_Exclusive;
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
	return false;
}

bool ChatCommon_IsLogEntryVisible(ChatConfig *pConfig, const char *pchTabGroup, S32 iTab, ChatLogEntryType eType, const char *pchChannel) {
	ChatTabConfig *pTabConfig = ChatCommon_GetTabConfig(pConfig, pchTabGroup, iTab);
	return ChatCommon_IsLogEntryVisibleInTab(pTabConfig, eType, pchChannel);
}

bool ChatCommon_IsLogEntryVisibleInPrivateMessageWindow(const ChatMessage *pMsg, const char * pchExpectedHandle)
{
	if (pMsg->eType == kChatLogEntryType_System && pMsg->pchText)
	{
		// HACK: We look into the message text and see if there is @handle in the text because there is no better way for now.
		// This helps us to display messages such as "@handle is offline." in the private chat windows.
		char *estrHandleWithAt = NULL;
		estrStackCreate(&estrHandleWithAt);
		estrCopy2(&estrHandleWithAt, "@");
		estrAppend2(&estrHandleWithAt, pchExpectedHandle);

		if (strEndsWith(pMsg->pchText, estrHandleWithAt))
		{
			estrDestroy(&estrHandleWithAt);
			return true;
		}

		estrAppend2(&estrHandleWithAt, " ");

		if (strstri(pMsg->pchText, estrHandleWithAt))
		{
			estrDestroy(&estrHandleWithAt);
			return true;
		}

		estrDestroy(&estrHandleWithAt);
	}

	if (pMsg->eType == kChatLogEntryType_Private || pMsg->eType == kChatLogEntryType_Private_Sent)
	{
		const char *pchFromHandle = pMsg->pFrom && pMsg->pFrom->pchHandle && pMsg->pFrom->pchHandle[0] ? pMsg->pFrom->pchHandle : NULL;
		if (pchFromHandle && stricmp_safe(pchFromHandle, pchExpectedHandle) == 0)
		{
			return true;
		}
		else
		{
			const char *pchToHandle = pMsg->pTo && pMsg->pTo->pchHandle && pMsg->pTo->pchHandle[0] ? pMsg->pTo->pchHandle : NULL;

			if (pchToHandle && stricmp_safe(pchToHandle, pchExpectedHandle) == 0)
			{
				return true;
			}
		}
	}
	return false;
}

bool ChatCommon_IsBuiltInChannelMessageType(ChatLogEntryType kType) {
	if (kType == kChatLogEntryType_Local)
		return true;
	else
	{
		ChatCommonBuiltInMessageTypeInfo *pInfo = ChatCommon_GetBuiltInMessageTypeInfo(kType);
		return !!pInfo->pchChannelPrefix; 
	}
}

static ChatCommonBuiltInMessageTypeInfo ***ChatCommon_GetBuiltInChannelMessageTypeInfos(void) {
	static ChatCommonBuiltInMessageTypeInfo **eaChannelMessageTypes = NULL;

	if (eaChannelMessageTypes == NULL) {
		ChatCommonBuiltInMessageTypeInfo ***peaInfos = ChatCommon_GetBuiltInMessageTypeInfos();
		int i;
		for (i=0; i < eaSize(peaInfos); i++) {
			ChatCommonBuiltInMessageTypeInfo *pInfo = (*peaInfos)[i];
			if (pInfo->pchChannelPrefix) {
				eaPush(&eaChannelMessageTypes, pInfo);
			}
		}
	}

	return &eaChannelMessageTypes;
}

ChatLogEntryType **ChatCommon_GetBuiltInChannelMessageTypes(void) {
	static ChatLogEntryType *eaiBuiltInChannels = NULL;
	
	if (eaiBuiltInChannels == NULL) {
		ChatCommonBuiltInMessageTypeInfo ***peaInfos = ChatCommon_GetBuiltInChannelMessageTypeInfos();
		int i;

		for (i=0; i < eaSize(peaInfos); i++) {
			ChatCommonBuiltInMessageTypeInfo *pInfo = (*peaInfos)[i];
			eaiPush(&eaiBuiltInChannels, pInfo->kType);
		}
	}

	return &eaiBuiltInChannels;
}

bool ChatCommon_IsBuiltInChannel(const char *pChannel) {
	ChatCommonBuiltInMessageTypeInfo ***peaInfos = ChatCommon_GetBuiltInChannelMessageTypeInfos();
	int i;

	// Treat NULL as "Local" and treat "Local" as built-in
	if (!pChannel || !stricmp_safe(pChannel, LOCAL_CHANNEL_NAME)) {
		return true;
	}

	// If this is a channel prefix or a message type, it's built-in.
	// That way users can't create a channel called "Guild" to confuse people.
	for (i=0; i < eaSize(peaInfos); i++) {
		ChatCommonBuiltInMessageTypeInfo *pInfo = (*peaInfos)[i];
		if (pInfo->kType == kChatLogEntryType_Private || pInfo->kType == kChatLogEntryType_Private_Sent) {
			if (stricmp_safe(pInfo->pchChannelPrefix, pChannel) == 0) {
				return true;
			}
		} else if (strStartsWith(pChannel, pInfo->pchChannelPrefix)) {
			return true;
		} else if (!stricmp_safe(pChannel, pInfo->pchTypeName)) {
			return true;
		}
	}

	return false;
}

ChatLogEntryType ChatCommon_GetChannelMessageType(const char *pchChannel) {
	ChatCommonBuiltInMessageTypeInfo ***peaInfos = ChatCommon_GetBuiltInChannelMessageTypeInfos();
	int i;

	// Treat NULL as "Local" and treat "Local" as built-in
	if (!pchChannel || !*pchChannel || !stricmp(pchChannel, LOCAL_CHANNEL_NAME)) {
		return kChatLogEntryType_Local;
	}

	// First test against the channel prefices
	for (i=0; i < eaSize(peaInfos); i++) {
		ChatCommonBuiltInMessageTypeInfo *pInfo = (*peaInfos)[i];
		if (pInfo->kType == kChatLogEntryType_Private || pInfo->kType == kChatLogEntryType_Private_Sent) {
			if (stricmp_safe(pInfo->pchChannelPrefix, pchChannel) == 0) {
				return pInfo->kType;
			}
		} else if (strStartsWith(pchChannel, pInfo->pchChannelPrefix)) {
			return pInfo->kType;
		}
	}

	// Test against the type names
	for (i=0; i < eaSize(peaInfos); i++) {
		ChatCommonBuiltInMessageTypeInfo *pInfo = (*peaInfos)[i];
		if (!stricmp_safe(pchChannel, pInfo->pchTypeName)) {
			return pInfo->kType;
		}
	}

	return kChatLogEntryType_Channel;
}

const char *ChatCommon_GetMessageTypeName(ChatLogEntryType kType) {
	return StaticDefineIntRevLookup(ChatLogEntryTypeEnum, kType);
}

// fPercent := 0, leaves color unchanged
// fPercent := [-1,0), darkens color by 1 - percent
// fPercent := (0,1], lightens color by percent
__forceinline static U32 ChatCommon_GetChatColorPercent(U32 uColor, F32 fPercent)
{
	if (fPercent < 0)
		return RGBAFromColor(ColorDarkenPercent(colorFromRGBA(uColor), 1 + fPercent));
	if (fPercent > 0)
		return RGBAFromColor(ColorLightenPercent(colorFromRGBA(uColor), fPercent));
	return uColor;
}

extern U32 ChatCommon_GetChatColor(ChatConfig *pConfig, ChatLogEntryType kType, const char *pchChannel, DefaultChatConfigSource eDefaultConfigSource) {
	ChatConfigDefaults *pConfigDefaults = ChatCommon_GetConfigDefaults(eDefaultConfigSource);
	bool bIsChannel = kType == kChatLogEntryType_Channel;
	const char *pchName = pchChannel;
	F32 fPercent = 0.0f;
	CONST_EARRAY_OF(ChatConfigColorDef) *peaColorDefs = NULL;

	if (kType == kChatLogEntryType_Private_Sent) {
		kType = kChatLogEntryType_Private;
		fPercent = pConfigDefaults ? pConfigDefaults->fPrivateSentColorShift : -0.4f;
	}

	if (!bIsChannel) {
		pchName = StaticDefineIntRevLookup(ChatLogEntryTypeEnum, kType);
	}

	if (pConfig) {
		peaColorDefs = bIsChannel ?
			&pConfig->eaChannelColorDefs :
			&pConfig->eaMessageTypeColorDefs;

		if (peaColorDefs && pchName) {
			ChatConfigColorDef *pColorDef = eaIndexedGetUsingString(peaColorDefs, pchName);
			if (pColorDef) {
				return fPercent ? ChatCommon_GetChatColorPercent(pColorDef->iColor, fPercent) : pColorDef->iColor;
			}
		}
	}

	if (pConfigDefaults) {
		peaColorDefs = bIsChannel ?
			&pConfigDefaults->eaChannelColorDefs :
			&pConfigDefaults->eaMessageTypeColorDefs;

		if (peaColorDefs && pchName) {
			ChatConfigColorDef *pColorDef = eaIndexedGetUsingString(peaColorDefs, pchName);
			if (pColorDef) {
				return fPercent ? ChatCommon_GetChatColorPercent(pColorDef->iColor, fPercent) : pColorDef->iColor;
			}
		}

		return fPercent ? ChatCommon_GetChatColorPercent(pConfigDefaults->iDefaultChatColor, fPercent) : pConfigDefaults->iDefaultChatColor;
	}

	return fPercent ? ChatCommon_GetChatColorPercent(FALLBACK_DEFAULT_CHAT_COLOR, fPercent) : FALLBACK_DEFAULT_CHAT_COLOR;
}

extern void ChatCommon_SetVectorFromColor(Vec4 vColor, U32 iColor) {
	vColor[3] = iColor && 0xff;
	vColor[2] = (iColor >>= 8) & 0xff;
	vColor[1] = (iColor >>= 8) & 0xff;
	vColor[0] = (iColor >>= 8) & 0xff;
}

extern U32 ChatCommon_GetColorFromVector(Vec4 vColor) {
	U32 iColor = 0;

	iColor += ((U32)vColor[0]) & 0xff;	
	iColor <<= 8;
	iColor += ((U32)vColor[1]) & 0xff;	
	iColor <<= 8;
	iColor += ((U32)vColor[2]) & 0xff;	
	iColor <<= 8;
	iColor += ((U32)vColor[3]) & 0xff;	

	return iColor;
}

extern U32 ChatCommon_GetUnsubscribedChatColor(DefaultChatConfigSource eDefaultConfigSource) {
	ChatConfigDefaults *pConfigDefaults = ChatCommon_GetConfigDefaults(eDefaultConfigSource);
	
	if (pConfigDefaults) {
		return pConfigDefaults->iUnsubscribedInputChannelColor;
	}

	return 0x696969ff;
}

ChatPlayerStruct *ChatCommon_FindPlayerInListByAccount(ChatPlayerStruct ***peaList, U32 accountID) {
	if (peaList) {
		int i;

		for (i=0; i < eaSize(peaList); i++) {
			ChatPlayerStruct *pPlayer = (*peaList)[i];
			if (pPlayer && pPlayer->accountID == accountID) {
				return pPlayer;
			}
		}
	}

	return NULL;
}

ChatPlayerStruct* ChatCommon_FindIgnoreByAccount(Entity *pEnt, U32 accountID) {
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pUI && pEnt->pPlayer->pUI->pChatState) {
		return ChatCommon_FindPlayerInListByAccount(&pEnt->pPlayer->pUI->pChatState->eaIgnores, accountID);
	}

	return NULL;
}

// Zone, Guild, and Team Channel Names

char *zone_GetZoneChannelNameFromMapName(char *Buffer, int Buffer_size, const char *mapShortName, 
										 int mapInstanceIndex, ZoneMapType eMapType, int iVirtualShardID)
{
	if (Buffer_size > 0)
	{
		Buffer[0] = 0;
		switch (eMapType)
		{
		case ZMTYPE_UNSPECIFIED:
		case ZMTYPE_STATIC:
			if (iVirtualShardID)
				sprintf_s(SAFESTR2(Buffer), "%s%s_%s%d", ZONE_CHANNEL_PREFIX, mapShortName, VSHARD_PREFIX, iVirtualShardID);
			else
				sprintf_s(SAFESTR2(Buffer), "%s%s", ZONE_CHANNEL_PREFIX, mapShortName);
		xdefault:
			if (iVirtualShardID)
				sprintf_s(SAFESTR2(Buffer), "%s%s-%d_%s%d", ZONE_CHANNEL_PREFIX, mapShortName, mapInstanceIndex, VSHARD_PREFIX, iVirtualShardID);
			else
				sprintf_s(SAFESTR2(Buffer), "%s%s-%d", ZONE_CHANNEL_PREFIX, mapShortName, mapInstanceIndex);
		}
		return Buffer;
	}
	return NULL;
}

char *guild_GetGuildChannelNameFromID(char *Buffer, int Buffer_size, int iGuildID, int iVirtualShardID)
{
	if (iVirtualShardID)
		sprintf_s(SAFESTR2(Buffer), "%s%s%u_%s%u", GUILD_CHANNEL_PREFIX, GetShardNameFromShardInfoString(), iGuildID,
			VSHARD_PREFIX, iVirtualShardID);
	else
		sprintf_s(SAFESTR2(Buffer), "%s%s%u", GUILD_CHANNEL_PREFIX, GetShardNameFromShardInfoString(), iGuildID);
	return Buffer;
}

char *guild_GetOfficerChannelNameFromID(char *Buffer, int Buffer_size, int iGuildID, int iVirtualShardID)
{
	if (iVirtualShardID)
		sprintf_s(SAFESTR2(Buffer), "%s%s%u_%s%u", OFFICER_CHANNEL_PREFIX, GetShardNameFromShardInfoString(), iGuildID,
			VSHARD_PREFIX, iVirtualShardID);
	else
		sprintf_s(SAFESTR2(Buffer), "%s%s%u", OFFICER_CHANNEL_PREFIX, GetShardNameFromShardInfoString(), iGuildID);		
	return Buffer;
}

char *team_MakeTeamChannelNameFromID(char *Buffer, int Buffer_size, int iTeamID)
{
	sprintf_s(SAFESTR2(Buffer), "%s%s%u", TEAM_CHANNEL_PREFIX, GetShardNameFromShardInfoString(), iTeamID);
	return Buffer;
}

char *teamUp_MakeTeamChannelNameFromID(char *Buffer, int Buffer_size, int iPartitionIdx, int iTeamID, int iServerID, const char *pchMapName)
{
	sprintf_s(SAFESTR2(Buffer), "%s%s%u_%u_%u_%s", TEAMUP_CHANNEL_PREFIX, GetShardNameFromShardInfoString(), iPartitionIdx, iTeamID, iServerID, pchMapName);
	return Buffer;
}

extern const char *ChatCommon_GetHandleFromNameOrHandle(const char *pchNameOrHandle) {
	if (pchNameOrHandle) {
		const char *pchNameEnd = strchr(pchNameOrHandle, '@');
		const char *pchHandle = pchNameEnd ? pchNameEnd+1 : NULL;
		return pchHandle && *pchHandle ? pchHandle : NULL;
	}

	return NULL;
}

extern ChatUserInfo *ChatCommon_CreateUserInfoFromNameOrHandle(const char *pchNameOrHandle) {
	if (pchNameOrHandle && *pchNameOrHandle) {
		const char *pchName = pchNameOrHandle;
		const char *pchNameEnd = strchr(pchNameOrHandle, '@');
		const char *pchHandle = pchNameEnd ? pchNameEnd+1 : NULL;
		S32 iNameLength = pchNameEnd ? pchNameEnd-pchName : (S32) strlen(pchName);
		ChatUserInfo *pUser;

		pUser = StructCreate(parse_ChatUserInfo);
		
		if (iNameLength) {
			estrConcat(&pUser->pchName, pchName, iNameLength);
		}

		if (pchHandle && *pchHandle) {
			estrCopy2(&pUser->pchHandle, pchHandle);
		}

		return pUser;
	}

	return NULL;
}


extern ChatUserInfo *ChatCommon_CreateUserInfo(ContainerID accountID, ContainerID playerID, const char *pchHandle, const char *pchName) {
	ChatUserInfo *pUser;

	if (!accountID && !playerID && (!pchHandle || !*pchHandle) && (!pchName || !*pchName)) {
		return NULL;
	}

	pUser = StructCreate(parse_ChatUserInfo);
	pUser->accountID = accountID;
	pUser->playerID = playerID;
	estrCopy2(&pUser->pchHandle, pchHandle);
	estrCopy2(&pUser->pchName, pchName);

	return pUser;
}

extern ChatMessage *ChatCommon_CreateMsg(
		const ChatUserInfo *pFrom, const ChatUserInfo *pTo, 
		ChatLogEntryType eType, const char *pchChannel, 
		const char *pchText, const ChatData *pData)
{
	ChatMessage *pMsg;

	if (!pchText || !*pchText) {
		return NULL;
	}
	
	pMsg = StructCreate(parse_ChatMessage);

	if (pFrom) {
		pMsg->pFrom = StructClone(parse_ChatUserInfo, pFrom);
	}

	if (pTo) {
		pMsg->pTo = StructClone(parse_ChatUserInfo, pTo);
	}

	pMsg->eType = eType;

	if (pchChannel) {
		estrCopy2(&pMsg->pchChannel, pchChannel);
	}
	if (pchText) {
		const char *pchTmpText = removeLeadingWhiteSpaces(pchText);
		estrCopy2(&pMsg->pchText, pchTmpText);
		removeTrailingWhiteSpaces(pMsg->pchText);
	}

	if (pData) {
		pMsg->pData = StructClone(parse_ChatData, pData);
	}

	return pMsg;
}

extern bool ChatCommon_EncodeMessage(SA_PARAM_OP_VALID char **ppchEncodedMsg, SA_PARAM_OP_VALID const ChatMessage *pMsg) {
	if (ppchEncodedMsg) {
		estrClear(ppchEncodedMsg);

		if (pMsg) {
			return ParserWriteTextEscaped(ppchEncodedMsg, parse_ChatMessage, (void*) pMsg, 0, 0, 0);
		}
	}

	return false;
}

extern bool ChatCommon_DecodeMessage(SA_PARAM_OP_VALID const char *pchEncodedMsg, SA_PARAM_OP_VALID ChatMessage *pMsg) {
	if (pchEncodedMsg && *pchEncodedMsg && pMsg) {
		return ParserReadTextEscaped(&pchEncodedMsg, parse_ChatMessage, pMsg, 0);
	}

	return false;
}

PlayerInfoPriority PlayerInfo_GetDisplayPriority(PlayerInfoStruct *pInfo, U32 uServerShardID)
{
	if (pInfo->uChatServerID == XMPP_CHAT_ID)
		return PINFO_PRIORITY_XMPP;
	if (!pInfo->uShardHash && pInfo->uChatServerID == 0 && pInfo->uShardHash == 0)
	{	// Initialize shard hash - guaranteed to be same shard; virtual shard ID should always be set appropriately
		pInfo->uShardHash = Game_GetShardHash();
	}
	if (pInfo->uShardHash != Game_GetShardHash())
		return PINFO_PRIORITY_OTHER;
	if (pInfo->uVirtualShardID != uServerShardID)
		return PINFO_PRIORITY_PSHARD;
	return PINFO_PRIORITY_VSHARD;
}

bool ChatIsPlayerInfoSameShard(PlayerInfoStruct *pInfo)
{
	// Virtual Shard ID can be ignored with the assumption that a user can only have 
	// a single login to a physical shard at a time.

	if (!pInfo->uShardHash && pInfo->uChatServerID == 0 && pInfo->uShardHash == 0)
	{	// Initialize it - guaranteed to be same shard
		pInfo->uShardHash = Game_GetShardHash();
		return true;
	}
	return pInfo->uShardHash == Game_GetShardHash();
}

bool ChatArePlayerInfoSameShard(PlayerInfoStruct *pInfo, PlayerInfoStruct *pInfo2)
{
	// Virtual Shard ID can be ignored with the assumption that a user can only have 
	// a single login to a physical shard at a time.
	return (pInfo && pInfo2 && pInfo->uShardHash == pInfo2->uShardHash);
}

// NOTE: this is for the physical shard and does NOT differentiate Virtual Shards!
U32 Game_GetShardHash(void)
{
	static U32 uHash = 0;
	if (uHash == 0)
	{
		char *uniqueId = NULL;
		estrPrintf(&uniqueId, "%s,%s,%s", 
			GetShardNameFromShardInfoString(),
			GetShardCategoryFromShardInfoString(), 
			GetProductName());
		uHash = hashStringInsensitive(uniqueId);
		estrDestroy(&uniqueId);
	}
	return uHash;
}

const char *ShardChannel_StripVShardPrefix(const char *channel_name)
{
	const char *strippedName = channel_name;
	if (!channel_name)
		return NULL;
	// First strip any virtual shard prefix from the string
	if (strStartsWith(strippedName, VSHARD_PREFIX))
	{
		strippedName += strlen(VSHARD_PREFIX);
		while (isdigit(*strippedName)) // move past virtual shard ID
			strippedName++;
		if (*strippedName == '_') // move past '_' delimiter
			strippedName++;
	}
	return strippedName;
}

bool ChatMail_CheckItemShard(ChatMailStruct *mail, const char *qualifiedShardName)
{
	return stricmp(mail->shardName, qualifiedShardName) != 0;
}

char *GetVShardQualifiedName(char *buffer, int buffer_size, ContainerID vshardID)
{
	if (vshardID)
		sprintf_s(SAFESTR2(buffer), "%s_%d", GetShardNameFromShardInfoString(), vshardID);
	else
		sprintf_s(SAFESTR2(buffer), "%s", GetShardNameFromShardInfoString());
	return buffer;
}


static int ComparePlayerStructFlag(const ChatPlayerStruct *ps1, const ChatPlayerStruct *ps2, int flag)
{
	if ((ps1->flags & flag) != (ps2->flags & flag))
		return ps1->flags & flag ? -1 : 1;
	return 0;
}
// Default sort order for lists of players (friends, ignores, search, etc.)
int ComparePlayerStructs(const ChatPlayerStruct **pps1, const ChatPlayerStruct **pps2)
{
	int result;
	bool bOnline1, bOnline2;
	const ChatPlayerStruct *ps1, *ps2;

	if (pps1 == pps2)
		return 0;

	if (!pps1 || !pps2)
		return pps1 ? -1 : 1;

	ps1 = *pps1;
	ps2 = *pps2;

	if (ps1 == ps2)
		return 0;

	if (!ps1 || !ps2)
		return ps1 ? -1 : 1;

	// Friend status flags
	if (ps1->flags != ps2->flags)
	{
		// Order of interest: RECEIVED, PENDING, NONE, IGNORE
		result = ComparePlayerStructFlag(ps1, ps2, FRIEND_FLAG_RECEIVEDREQUEST);
		if (result)
			return result;
		result = ComparePlayerStructFlag(ps1, ps2, FRIEND_FLAG_PENDINGREQUEST);
		if (result)
			return result;
		if (ps1->flags == FRIEND_FLAG_NONE || ps2->flags == FRIEND_FLAG_NONE)
			return ps2->flags - ps1->flags;
		result = ComparePlayerStructFlag(ps1, ps2, FRIEND_FLAG_IGNORE);
		if (result)
			return result;
	}

	// Online status
	bOnline1 = ps1->pPlayerInfo.onlinePlayerName != NULL;
	bOnline2 = ps2->pPlayerInfo.onlinePlayerName != NULL;
	if (bOnline1 != bOnline2)
		return bOnline1 ? -1 : 1;

	// Chat handle
	result = stricmp_safe(ps1->chatHandle, ps2->chatHandle);
	if (result)
		return result;
	return 0;
}

int ChatPlayer_FindInList(ChatPlayerStruct ***peaList, ChatPlayerStruct *pPlayer)
{
	int i;
	bool bFound = false;

	for (i=0; i < eaSize(peaList); i++) {
		ChatPlayerStruct *pListedPlayer = eaGet(peaList, i);
		if (pListedPlayer && pPlayer->accountID == pListedPlayer->accountID) {
			return i;
		}
	}

	return -1;
}

void ChatPlayer_AddToList(ChatPlayerStruct ***peaList, ChatPlayerStruct *pPlayer, U32 uVShardID, bool bForcePInfoOverwrite)
{
	if (peaList && pPlayer)
	{
		int index = ChatPlayer_FindInList(peaList, pPlayer);

		if (index >= 0)
		{
			ChatPlayerStruct *curInfo = (*peaList)[index];
			PlayerInfoStruct temp = {0};
			PlayerInfoPriority oldPriority = PlayerInfo_GetDisplayPriority(&curInfo->pPlayerInfo, uVShardID);
			PlayerInfoPriority newPriority = PlayerInfo_GetDisplayPriority(&pPlayer->pPlayerInfo, uVShardID);

			// PlayerInfo_GetDisplayPriority will make sure stored shard hashes are initialized
			// Back up old info
			StructCopyAll(parse_PlayerInfoStruct, &curInfo->pPlayerInfo, &temp);
			// Copy new info over old info
			StructCopyAll(parse_ChatPlayerStruct, pPlayer, (*peaList)[index]);

			// Indicates that user logged out of whatever shard was stored; toss out the old info
			if (eaSize(&pPlayer->ppExtraPlayerInfo) && ChatArePlayerInfoSameShard(&temp, pPlayer->ppExtraPlayerInfo[0]))
				bForcePInfoOverwrite = true;
			if (temp.uChatServerID && !bForcePInfoOverwrite)
			{
				switch (oldPriority)
				{
				case PINFO_PRIORITY_XMPP:
					// old info was XMPP, always toss this for newer info on anything
				xcase PINFO_PRIORITY_OTHER:
					if (newPriority < PINFO_PRIORITY_PSHARD && !ChatArePlayerInfoSameShard(&pPlayer->pPlayerInfo, &temp))
					{
						// New info is for a different shard AND is NOT the same shard as the old info
						// Keep the info for the old-non-matching shard so it doesn't constantly change!
						StructDeInit(parse_PlayerInfoStruct, &curInfo->pPlayerInfo);
						StructCopyAll(parse_PlayerInfoStruct, &temp, &curInfo->pPlayerInfo);
					}

				xcase PINFO_PRIORITY_PSHARD:
				case PINFO_PRIORITY_VSHARD: // both are handled the same, since physical shard logins are unique
					// Old info was for this shard
					if (newPriority < PINFO_PRIORITY_PSHARD)
					{   // ... and new info is not for this shard
						StructDeInit(parse_PlayerInfoStruct, &curInfo->pPlayerInfo);
						StructCopyAll(parse_PlayerInfoStruct, &temp, &curInfo->pPlayerInfo);
					}
				}
			}
			StructDeInit(parse_PlayerInfoStruct, &temp);
		}
		else
		{
			eaPush(peaList, StructClone(parse_ChatPlayerStruct, pPlayer));
		}
		eaQSort(*peaList, ComparePlayerStructs);
	}
}

void ChatPlayer_RemoveFromList(ChatPlayerStruct ***peaList, ChatPlayerStruct *pPlayer)
{
	if (peaList && pPlayer) {
		int index = ChatPlayer_FindInList(peaList, pPlayer);

		if (index >= 0)
			StructDestroy(parse_ChatPlayerStruct, eaRemove(peaList, index));
	}
}

// General Chat Config Functions
void ChatConfigCommon_InitToDefault(SA_PARAM_NN_VALID NOCONST(ChatConfig) *pChatConfig, DefaultChatConfigSource eSource, Language eLanguage)
{
	ChatConfigDefaults *pConfigDefaults = ChatCommon_GetConfigDefaults(eSource);
	NOCONST(ChatTabGroupConfig) *pChatTabGroupConfig = StructCreateNoConst(parse_ChatTabGroupConfig);

	if (pConfigDefaults)
	{
		EARRAY_CONST_FOREACH_BEGIN(pConfigDefaults->eaDefaultTabConfigs, i, s);
		{
			ChatTabConfig *pDefaultTabConfig = eaGet(&pConfigDefaults->eaDefaultTabConfigs, i);
			NOCONST(ChatTabConfig) *pNewTabConfig = StructCloneDeConst(parse_ChatTabConfig, pDefaultTabConfig);
			const char *pchKey = pNewTabConfig->pchTitle;
			estrCopy2(&pNewTabConfig->pchTitle, langTranslateMessageKey(eLanguage, pchKey));
			ChatCommon_AddTabConfig(pChatTabGroupConfig, pNewTabConfig);
		}
		EARRAY_FOREACH_END;

		pChatTabGroupConfig->pchName = estrCreateFromStr(pConfigDefaults->pchDefaultTabGroupName);
		pChatTabGroupConfig->uiTime = timeSecondsSince2000();
		eaPush(&pChatConfig->eaChatTabGroupConfigs, pChatTabGroupConfig);

		pChatConfig->pchCurrentTabGroup = StructAllocString(pChatTabGroupConfig->pchName);
		pChatConfig->bShowDate = pConfigDefaults->bShowDate;
		pChatConfig->bShowTime = pConfigDefaults->bShowTime;
		pChatConfig->bShowMessageTypeNames = pConfigDefaults->bShowMessageTypeNames;
		pChatConfig->bHideAccountNames = pConfigDefaults->bHideAccountNames;
		pChatConfig->bProfanityFilter = pConfigDefaults->bProfanityFilter;
		pChatConfig->bTextFadesWithWindow = pConfigDefaults->bTextFadesWithWindow;
		pChatConfig->bShowChannelNames = pConfigDefaults->bShowChannelNames;
		pChatConfig->bAnnotateAutoComplete = pConfigDefaults->bAnnotateAutoComplete;
		pChatConfig->siTimeRequiredToStartFading = pConfigDefaults->siTimeRequiredToStartFading;
		pChatConfig->siFadeAwayDuration = pConfigDefaults->siFadeAwayDuration;
		pChatConfig->fFontScale = pConfigDefaults->fFontScale;
		pChatConfig->fInactiveWindowAlpha = pConfigDefaults->fInactiveWindowAlpha;
		pChatConfig->fActiveWindowAlpha = pConfigDefaults->fActiveWindowAlpha;
	}
	else // no defaults found
	{
		// Create a best effort initial tab group with a single chat tab
		// that has all possible message types (by using exclusive 
		// filtering and not defining and explicit message types or 
		// channels to be filtered).  This way the user won't be screwed.
		NOCONST(ChatTabConfig) *pTabConfig = StructCreateNoConst(parse_ChatTabConfig);
		estrCopy2(&pTabConfig->pchTitle, "Chat");
		estrCopy2(&pTabConfig->pchDefaultChannel, LOCAL_CHANNEL_NAME);
		pTabConfig->eMessageTypeFilterMode = kChatTabFilterMode_Exclusive;
		pTabConfig->eChannelFilterMode = kChatTabFilterMode_Exclusive;

		// Note: ChatWindow_tabgroup MUST match the name of the tab group
		//       gen used for the chat window.
		pChatTabGroupConfig->pchName = estrCreateFromStr("Chatwindow_tabgroup");
		pChatTabGroupConfig->uiTime = timeSecondsSince2000();
		eaPush(&pChatConfig->eaChatTabGroupConfigs, pChatTabGroupConfig);
		ChatCommon_AddTabConfig(pChatTabGroupConfig, pTabConfig);

		pChatConfig->pchCurrentTabGroup = StructAllocString(pChatTabGroupConfig->pchName);
		pChatConfig->bShowDate = false;
		pChatConfig->bShowDate = false;
		pChatConfig->bShowTime = false;
		pChatConfig->bShowMessageTypeNames = false;
		pChatConfig->bHideAccountNames = false;
		pChatConfig->bProfanityFilter = true;
		pChatConfig->bTextFadesWithWindow = false;
		pChatConfig->bShowChannelNames = true;
		pChatConfig->bAnnotateAutoComplete = false;
		pChatConfig->siTimeRequiredToStartFading = 0;
		pChatConfig->siFadeAwayDuration = 0;	
		pChatConfig->iVersion++;
	}
}

NOCONST(ChatConfigColorDef) *ChatConfigCommon_CreateColorDef(const char *pchName, U32 iColor)
{
	NOCONST(ChatConfigColorDef) *pColorDef = StructCreateNoConst(parse_ChatConfigColorDef);
	pColorDef->pchName = estrCreateFromStr(pchName);
	pColorDef->iColor = iColor;

	return pColorDef;
}

// Create a new tab config using the first default tab config
NOCONST(ChatTabConfig) *ChatConfigCommon_CreateDefaultTabConfig(DefaultChatConfigSource eSource, Language eLanguage)
{
	ChatConfigDefaults *pConfigDefaults = ChatCommon_GetConfigDefaults(eSource);

	// If we have defaults, then use the first default tab
	if (pConfigDefaults && eaSize(&pConfigDefaults->eaDefaultTabConfigs) > 0)
	{
		ChatTabConfig *pTabConfig = eaGet(&pConfigDefaults->eaDefaultTabConfigs, 0);
		if (pTabConfig) {
			NOCONST(ChatTabConfig) *pNewTabConfig = StructCloneDeConst(parse_ChatTabConfig, pTabConfig);
			const char *pchKey = pNewTabConfig->pchTitle;
			estrCopy2(&pNewTabConfig->pchTitle, langTranslateMessageKey(eLanguage, pchKey));
			return pNewTabConfig;
		}
	}

	// Otherwise concoct a decent default tab
	{
		NOCONST(ChatTabConfig) *pTabConfig = StructCreateNoConst(parse_ChatTabConfig);
		pTabConfig->pchTitle = estrCreateFromStr("Chat");
		pTabConfig->pchDefaultChannel = LOCAL_CHANNEL_NAME;

		//Include all message types & channels by default by not defining any message types/channels to be excluded
		pTabConfig->eMessageTypeFilterMode = kChatTabFilterMode_Exclusive;
		pTabConfig->eChannelFilterMode = kChatTabFilterMode_Exclusive;

		return pTabConfig;
	}
}


static char *GetUniqueTabBaseName(const char *pchName) {
	static char *pchUniqueName = NULL;
	size_t lastDigitIndex = 0;
	size_t len, i;

	long uniqueNumber = 0;

	if (!pchName || !pchName[0]) {
		return "";
	}

	// Note: We need to loop in forward order
	// because size_t is unsigned and we can't 
	// therefore check for "i < 0" as an exit
	// condition for the loop.
	len = strlen(pchName);
	for (i = 1; i <= len; i++) {
		if (!isdigit(pchName[len-i])) {
			lastDigitIndex = len-i+1;
			break;
		}
	}

	// Copy and then clip to the beginning of the number
	estrCopy2(&pchUniqueName, pchName);
	pchUniqueName[lastDigitIndex] = '\0';

	return pchUniqueName;
}

AUTO_TRANS_HELPER;
bool ChatConfigCommon_CheckTabNameExists(ATH_ARG NOCONST(ChatConfig) *pConfig, const char *pchName)
{
	EARRAY_CONST_FOREACH_BEGIN(pConfig->eaChatTabGroupConfigs, i, s);
	{
		NOCONST(ChatTabGroupConfig) *pTabGroup = pConfig->eaChatTabGroupConfigs[i];
		S32 iIndex = ChatCommon_FindTabConfigIndex((ChatTabGroupConfig*) pTabGroup, pchName);
		if (iIndex >= 0) {
			return true;
		}
	}
	EARRAY_FOREACH_END;
	return false;
}

// Returns a static estr that contains a unique tab name
AUTO_TRANS_HELPER;
const char **ChatConfigCommon_GetUniqueTabName(ATH_ARG NOCONST(ChatConfig) *pConfig, const char *pchName)
{
	int count = 2;
	static char *pchUniqueName;
	static char *pchBaseName;

	if (!ChatConfigCommon_CheckTabNameExists(pConfig, pchName))
	{
		// It's already unique, no need to modify it
		estrCopy2(&pchUniqueName, pchName);
		return &pchUniqueName;
	}

	pchBaseName = GetUniqueTabBaseName(pchName);
	do {
		estrPrintf(&pchUniqueName, "%s%i", pchBaseName, count++);
		if (!ChatConfigCommon_CheckTabNameExists(pConfig, pchUniqueName))
			break;
	} while (true);

	return &pchUniqueName;
}

void ChatConfigCommon_SetCurrentTab(ChatConfig *pConfig, const char *pchTabGroup, int iTab)
{
	NOCONST(ChatTabGroupConfig) *pTabGroup = eaIndexedGetUsingString(&pConfig->eaChatTabGroupConfigs, pchTabGroup);
	NOCONST(ChatTabConfig) *pCurrentTab;
	
	if (!pTabGroup)
		return;
	pCurrentTab = eaGet(&pTabGroup->eaChatTabConfigs, iTab);
	// Update the current tab group & index
	// - Only update group if it changed
	if (pConfig->pchCurrentTabGroup && stricmp(pConfig->pchCurrentTabGroup, pchTabGroup))
	{
		free(pConfig->pchCurrentTabGroup);
		pConfig->pchCurrentTabGroup = StructAllocString(pchTabGroup);
	}
	else if (!pConfig->pchCurrentTabGroup)
		pConfig->pchCurrentTabGroup = StructAllocString(pchTabGroup);

	pTabGroup->iCurrentTab = iTab;
	if (pCurrentTab && pCurrentTab->pchDefaultChannel && pCurrentTab->pchDefaultChannel[0])
	{
		if (pConfig->pchCurrentInputChannel)
			free(pConfig->pchCurrentInputChannel);
		pConfig->pchCurrentInputChannel = StructAllocString(pCurrentTab->pchDefaultChannel);
	}
}

bool ChatConfigCommon_SetCurrentInputChannel(ChatConfig *pConfig, const char *pchTabGroup, S32 iTab, const char *pchChannel)
{
	NOCONST(ChatTabConfig) *pTabConfig = CONTAINER_NOCONST(ChatTabConfig, ChatCommon_GetTabConfig(pConfig, pchTabGroup, iTab));

	if (!pTabConfig)
		return false;
	if (stricmp_safe(pConfig->pchCurrentInputChannel, pchChannel))
	{
		if (pConfig->pchCurrentInputChannel)
			free(pConfig->pchCurrentInputChannel);
		if (strStartsWith(pchChannel, TEAM_CHANNEL_PREFIX))
			pConfig->pchCurrentInputChannel = StructAllocString(CHAT_TEAM_SHORTCUT);
		else if (strStartsWith(pchChannel, ZONE_CHANNEL_PREFIX) && stricmp(pchChannel, UGCEDIT_CHANNEL_NAME) != 0)
			pConfig->pchCurrentInputChannel = StructAllocString(CHAT_ZONE_SHORTCUT);
		else if (strStartsWith(pchChannel, GUILD_CHANNEL_PREFIX))
			pConfig->pchCurrentInputChannel = StructAllocString(CHAT_GUILD_SHORTCUT);
		else if (strStartsWith(pchChannel, OFFICER_CHANNEL_PREFIX))
			pConfig->pchCurrentInputChannel = StructAllocString(CHAT_GUILD_OFFICER_SHORTCUT);
		else if (strStartsWith(pchChannel, QUEUE_CHANNEL_PREFIX))
			pConfig->pchCurrentInputChannel = StructAllocString(CHAT_QUEUE_SHORTCUT);
		else
			pConfig->pchCurrentInputChannel = StructAllocString(pchChannel);
		
		estrCopy2(&pTabConfig->pchDefaultChannel, pchChannel);
		return true;
	}
	return false;
}

AUTO_TRANS_HELPER;
NOCONST(ChatTabConfig) *ChatConfigCommon_GetTabConfig(ATH_ARG NOCONST(ChatConfig) *pChatConfig, const char *pchTabGroup, int iTab)
{
	NOCONST(ChatTabGroupConfig) *pTabGroup = eaIndexedGetUsingString(&pChatConfig->eaChatTabGroupConfigs, pchTabGroup);
	if (!pTabGroup)
		return NULL;
	return eaGet(&pTabGroup->eaChatTabConfigs, iTab);
}

AUTO_TRANS_HELPER;
void ChatConfigCommon_SetChannelColor(ATH_ARG NOCONST(ChatConfig) *pChatConfig, const char *pchChannelName, U32 iColor, U32 iDefaultColor)
{
	int idx = eaIndexedFindUsingString(&pChatConfig->eaChannelColorDefs, pchChannelName);
	NOCONST(ChatConfigColorDef) *pColorDef = eaGet(&pChatConfig->eaChannelColorDefs, idx);

	if (pColorDef)
	{
		if (iColor == iDefaultColor)					
			StructDestroyNoConst(parse_ChatConfigColorDef, eaRemove(&pChatConfig->eaChannelColorDefs, idx));
		else
			pColorDef->iColor = iColor;
	}
	else if (iDefaultColor != iColor)
	{
		pColorDef = ChatConfigCommon_CreateColorDef(pchChannelName, iColor);
		if (!pChatConfig->eaChannelColorDefs)
			eaIndexedEnableNoConst(&pChatConfig->eaChannelColorDefs, parse_ChatConfigColorDef);
		eaIndexedAdd(&pChatConfig->eaChannelColorDefs, pColorDef);
	}
}

AUTO_TRANS_HELPER;
void ChatConfigCommon_SetMessageColor(ATH_ARG NOCONST(ChatConfig) *pChatConfig, const char *pchMessageName, U32 iColor, U32 iDefaultColor)
{
	int idx = eaIndexedFindUsingString(&pChatConfig->eaMessageTypeColorDefs, pchMessageName);
	NOCONST(ChatConfigColorDef) *pColorDef = eaGet(&pChatConfig->eaMessageTypeColorDefs, idx);

	if (pColorDef)
	{
		if (iColor == iDefaultColor)					
			StructDestroyNoConst(parse_ChatConfigColorDef, eaRemove(&pChatConfig->eaMessageTypeColorDefs, idx));
		else
			pColorDef->iColor = iColor;
	}
	else if (iDefaultColor != iColor)
	{
		pColorDef = ChatConfigCommon_CreateColorDef(pchMessageName, iColor);
		if (!pChatConfig->eaMessageTypeColorDefs)
			eaIndexedEnableNoConst(&pChatConfig->eaMessageTypeColorDefs, parse_ChatConfigColorDef);
		eaIndexedAdd(&pChatConfig->eaMessageTypeColorDefs, pColorDef);
	}
}

AUTO_TRANS_HELPER;
void ChatConfigCommon_ResetTabGroup(ATH_ARG NOCONST(ChatConfig) *pChatConfig, const char *pchTabGroup, Language eLanguage, DefaultChatConfigSource eSource)
{
	NOCONST(ChatTabGroupConfig) *pTabGroupConfig = eaIndexedGetUsingString(&pChatConfig->eaChatTabGroupConfigs, pchTabGroup);
	ChatConfigDefaults *pConfigDefaults = ChatCommon_GetConfigDefaults(eSource);

	if (!pTabGroupConfig)
		return;

	eaClearStructNoConst(&pTabGroupConfig->eaChatTabConfigs, parse_ChatTabConfig);	

	if (pConfigDefaults)
	{
		EARRAY_CONST_FOREACH_BEGIN(pConfigDefaults->eaDefaultTabConfigs, i, s);
		{
			ChatTabConfig *pDefaultTabConfig = eaGet(&pConfigDefaults->eaDefaultTabConfigs, i);
			NOCONST(ChatTabConfig) *pTabConfig = StructCloneDeConst(parse_ChatTabConfig, pDefaultTabConfig);
			const char *pchKey = pTabConfig->pchTitle;
			estrCopy2(&pTabConfig->pchTitle, langTranslateMessageKey(eLanguage, pchKey));
			eaPush(&pTabGroupConfig->eaChatTabConfigs, pTabConfig);
		}
		EARRAY_FOREACH_END;
	}
	else
	{
		// Create a best effort initial tab group with a single chat tab
		// that has all possible message types (by using exclusive 
		// filtering and not defining and explicit message types or 
		// channels to be filtered).  This way the user won't be screwed.
		NOCONST(ChatTabConfig) *pTabConfig = StructCreateNoConst(parse_ChatTabConfig);
		estrCopy2(&pTabConfig->pchTitle, "Chat");
		estrCopy2(&pTabConfig->pchDefaultChannel, LOCAL_CHANNEL_NAME);
		pTabConfig->eMessageTypeFilterMode = kChatTabFilterMode_Exclusive;
		pTabConfig->eChannelFilterMode = kChatTabFilterMode_Exclusive;
		eaPush(&pTabGroupConfig->eaChatTabConfigs, pTabConfig);
	}
}

AUTO_TRANS_HELPER;
void ChatConfigCommon_CreateTabGroup(ATH_ARG NOCONST(ChatConfig) *pChatConfig, const char *pchTabGroup, bool bInitTabs, Language eLanguage, DefaultChatConfigSource eSource)
{
	NOCONST(ChatTabGroupConfig) *pTabGroupConfig = eaIndexedGetUsingString(&pChatConfig->eaChatTabGroupConfigs, pchTabGroup);
	const S32 c_iMaxRememberedTabGroups = 25;
	U32 uiTime = timeSecondsSince2000();

	if (pTabGroupConfig)
		return;
	
	if (!pChatConfig->eaChatTabGroupConfigs)
		eaIndexedEnableNoConst(&pChatConfig->eaChatTabGroupConfigs, parse_ChatTabGroupConfig);
	// Create using the default chat settings
	pTabGroupConfig = StructCreateNoConst(parse_ChatTabGroupConfig);
	estrCopy2(&pTabGroupConfig->pchName, pchTabGroup);
	pTabGroupConfig->uiTime = uiTime;
	eaIndexedAdd(&pChatConfig->eaChatTabGroupConfigs, pTabGroupConfig);

	if (bInitTabs)
		ChatConfigCommon_ResetTabGroup(pChatConfig, pTabGroupConfig->pchName, eLanguage, eSource);

	while (eaSize(&pChatConfig->eaChatTabGroupConfigs) > c_iMaxRememberedTabGroups)
	{
		S32 iMinIndex = 0;
		U32 uiMinTime = uiTime + 1;
		EARRAY_CONST_FOREACH_BEGIN(pChatConfig->eaChatTabGroupConfigs, i, s);
		{
			pTabGroupConfig = pChatConfig->eaChatTabGroupConfigs[i];
			if (pTabGroupConfig->uiTime < uiMinTime)
			{
				iMinIndex = i;
				uiMinTime = pTabGroupConfig->uiTime;
			}
		}
		EARRAY_FOREACH_END;
		StructDestroyNoConst(parse_ChatTabGroupConfig, eaRemove(&pChatConfig->eaChatTabGroupConfigs, iMinIndex));
	}
}

AUTO_TRANS_HELPER;
int ChatConfigCommon_CreateTab(ATH_ARG NOCONST(ChatConfig) *pChatConfig, const char *pchTabGroup, Language eLanguage, DefaultChatConfigSource eSource)
{
	NOCONST(ChatTabGroupConfig) *pTabGroupConfig = eaIndexedGetUsingString(&pChatConfig->eaChatTabGroupConfigs, pchTabGroup);
	NOCONST(ChatTabConfig) *pTabConfig;
	const char **ppchUniqueTabName;

	if (!pTabGroupConfig)
		return -1;
	pTabConfig = ChatConfigCommon_CreateDefaultTabConfig(eSource, eLanguage);
	ppchUniqueTabName = ChatConfigCommon_GetUniqueTabName(pChatConfig, pTabConfig->pchTitle);

	pTabGroupConfig->uiTime = timeSecondsSince2000();

	estrCopy(&pTabConfig->pchTitle, ppchUniqueTabName);
	return eaPush(&pTabGroupConfig->eaChatTabConfigs, pTabConfig);
}

AUTO_TRANS_HELPER;
int ChatConfigCommon_CopyTab(ATH_ARG NOCONST(ChatConfig) *pChatConfig, const char *pchDstTabGroup, const char *pchDstName, const char *pchSrcTabGroup, int iSrcTab)
{
	NOCONST(ChatTabGroupConfig) *pDstTabGroupConfig = eaIndexedGetUsingString(&pChatConfig->eaChatTabGroupConfigs, pchDstTabGroup);
	NOCONST(ChatTabConfig) *pTabConfig;

	if (!pDstTabGroupConfig)
		return -1;

	pTabConfig = ChatConfigCommon_GetTabConfig(pChatConfig, pchSrcTabGroup, iSrcTab);
	if (pTabConfig)
	{
		NOCONST(ChatTabConfig) *pNewTabConfig = StructCloneNoConst(parse_ChatTabConfig, pTabConfig);
		if (pNewTabConfig)
		{
			const char **ppchUniqueTabName = ChatConfigCommon_GetUniqueTabName(pChatConfig, pchDstName && *pchDstName ? pchDstName : pTabConfig->pchTitle);

			pDstTabGroupConfig->uiTime = timeSecondsSince2000();
			// If the titles are not the same, make sure the new tab name is unique
			if (stricmp(*ppchUniqueTabName, pTabConfig->pchTitle) != 0)
				estrCopy(&pNewTabConfig->pchTitle, ppchUniqueTabName);
			else
				estrCopy2(&pNewTabConfig->pchTitle, pchDstName);

			return eaPush(&pDstTabGroupConfig->eaChatTabConfigs, pNewTabConfig);
		}
	}
	return -1;
}

AUTO_TRANS_HELPER;
int ChatConfigCommon_DeleteTab(ATH_ARG NOCONST(ChatConfig) *pChatConfig, const char *pchTabGroup, int iTab)
{
	NOCONST(ChatTabGroupConfig) *pTabGroup;
	NOCONST(ChatTabConfig) *pTab = NULL;

	pTabGroup = eaIndexedGetUsingString(&pChatConfig->eaChatTabGroupConfigs, pchTabGroup);
	if (!pTabGroup)
		return -1;

	pTabGroup->uiTime = timeSecondsSince2000();

	// Not allowed to delete the last tab of a tab group
	// - we may need to revise this when we have multiple tab groups (chat windows)
	if (eaSize(&pTabGroup->eaChatTabConfigs) <= 1)
		return -1;

	if (pTab = eaRemove(&pTabGroup->eaChatTabConfigs, iTab))
	{
		S32 iNewTab = iTab;
		S32 iTabCount = eaSize(&pTabGroup->eaChatTabConfigs);
		if (iNewTab >= iTabCount)
			iNewTab = iTabCount-1;
		StructDestroyNoConst(parse_ChatTabConfig, pTab);
		return iNewTab;
	}
	return -1; // eaRemove failed
}

AUTO_TRANS_HELPER;
int ChatConfigCommon_MoveTab(ATH_ARG NOCONST(ChatConfig) *pChatConfig, const char *pchFromGroupName, S32 iFromIndex, const char *pchToGroupName, S32 iToIndex)
{
	NOCONST(ChatTabGroupConfig) *pFromTabGroup;
	NOCONST(ChatTabGroupConfig) *pToTabGroup;
	NOCONST(ChatTabConfig) *pTabConfig;
	S32 iNewSelectedFromIndex;

	pFromTabGroup = eaIndexedGetUsingString(&pChatConfig->eaChatTabGroupConfigs, pchFromGroupName);
	pToTabGroup = eaIndexedGetUsingString(&pChatConfig->eaChatTabGroupConfigs, pchToGroupName);

	if (!pFromTabGroup || !pToTabGroup)
		return -1;
	// Make sure the target index is valid before removing
	if (iToIndex > eaSize(&pToTabGroup->eaChatTabConfigs))
		return -1;
		
	pTabConfig = eaRemove(&pFromTabGroup->eaChatTabConfigs, iFromIndex);
	iNewSelectedFromIndex = iFromIndex;
	if (iNewSelectedFromIndex >= eaSize(&pFromTabGroup->eaChatTabConfigs)) 
		iNewSelectedFromIndex--;

	//TODO: We should change notify tab index updated to include the tab group name!!!
	//GClientCmd_ChatConfig_NotifyTabIndexUpdated(relayUser->uAccountID, iNewSelectedFromIndex);

	if (pTabConfig)
	{
		if (pToTabGroup == pFromTabGroup) {
			// Adjust the to index if it's in the same tab group and
			// the one we removed was in a lower index than the desired
			// target location.  This accounts for the shift caused by 
			// the 'from' removal.
			if (iFromIndex < iToIndex)
				iToIndex--;
		}
		eaInsert(&pToTabGroup->eaChatTabConfigs, pTabConfig, iToIndex);
		//TODO: We should change notify tab index updated to include the tab group name!!!
		//GClientCmd_ChatConfig_NotifyTabIndexUpdated(relayUser->uAccountID, iToIndex);
		return iToIndex;
	}
	else
		return -1;
}

AUTO_TRANS_HELPER;
void ChatConfigCommon_UpdateCommonTabs(ATH_ARG NOCONST(ChatConfig) *pConfig, NOCONST(ChatTabConfig) *pTabConfig)
{
	if (!pTabConfig)
		return;

	EARRAY_CONST_FOREACH_BEGIN(pConfig->eaChatTabGroupConfigs, i, s);
	{
		NOCONST(ChatTabGroupConfig) *pTabGroupConfig = eaGet(&pConfig->eaChatTabGroupConfigs, i);
		if (pTabGroupConfig && pTabGroupConfig->eaChatTabConfigs)
		{
			EARRAY_CONST_FOREACH_BEGIN(pTabGroupConfig->eaChatTabConfigs, j, t);
			{
				NOCONST(ChatTabConfig) *pOtherTabConfig = eaGet(&pTabGroupConfig->eaChatTabConfigs, j);
				if (pOtherTabConfig && pOtherTabConfig != pTabConfig && !stricmp_safe(pOtherTabConfig->pchTitle, pTabConfig->pchTitle))
				{
					// Update the configuration
					pTabGroupConfig->eaChatTabConfigs[j] = StructCloneNoConst(parse_ChatTabConfig, pTabConfig);
					StructDestroyNoConst(parse_ChatTabConfig, pOtherTabConfig);
				}
			}
			EARRAY_FOREACH_END;
		}
	}
	EARRAY_FOREACH_END;
}

AUTO_TRANS_HELPER;
bool ChatConfigCommon_SetTabName(ATH_ARG NOCONST(ChatConfig)* pConfig, const char *pchTabGroup, int iTab, const char *pchName)
{
	NOCONST(ChatTabConfig) *pTabConfig = ChatConfigCommon_GetTabConfig(pConfig, pchTabGroup, iTab);
	if (pTabConfig)
	{
		if (stricmp_safe(pTabConfig->pchTitle, pchName))
		{
			const char **ppchUniqueTabName = ChatConfigCommon_GetUniqueTabName(pConfig, pchName);
			estrCopy(&pTabConfig->pchTitle, ppchUniqueTabName);
			return true;
		}
	}
	return false;
}

AUTO_TRANS_HELPER;
bool ChatConfigCommon_SetMessageTypeFilter(ATH_ARG NOCONST(ChatConfig) *pChatConfig, const char *pchTabGroup, int iTab, const char *pchFilterName, int bFiltered)
{
	NOCONST(ChatTabConfig) *pTabConfig = ChatConfigCommon_GetTabConfig(pChatConfig, pchTabGroup, iTab);
	int iExistingIndex;
	bool bExists;

	if (!pTabConfig)
		return false;

	// We need to store the opposite of what the user wants if this is exclusive rather than inclusive.
	if (pTabConfig->eMessageTypeFilterMode == kChatTabFilterMode_Exclusive)
		bFiltered = !bFiltered;

	iExistingIndex = eaFindString(&pTabConfig->eaMessageTypes, pchFilterName);
	bExists = iExistingIndex >= 0;
	if (bExists == bFiltered)
		return false;
	if (bFiltered)
		eaPush(&pTabConfig->eaMessageTypes, strdup(pchFilterName));
	else
		free(eaRemove(&pTabConfig->eaMessageTypes, iExistingIndex));
	ChatConfigCommon_UpdateCommonTabs(pChatConfig, pTabConfig);
	return true;
}

AUTO_TRANS_HELPER;
bool ChatConfigCommon_SetChannelFilter(ATH_ARG NOCONST(ChatConfig) *pChatConfig, const char *pchTabGroup, int iTab, const char *pchFilterName, int bFiltered)
{
	NOCONST(ChatTabConfig) *pTabConfig = ChatConfigCommon_GetTabConfig(pChatConfig, pchTabGroup, iTab);
	int iExistingIndex;
	bool bExists;

	if (!pTabConfig)
		return false;

	// We need to store the opposite of what the user wants if this is exclusive rather than inclusive.
	if (pTabConfig->eChannelFilterMode == kChatTabFilterMode_Exclusive)
		bFiltered = !bFiltered;

	iExistingIndex = eaFindString(&pTabConfig->eaChannels, pchFilterName);
	bExists = iExistingIndex >= 0;
	if (bExists == bFiltered)
		return false;
	if (bFiltered)
		eaPush(&pTabConfig->eaChannels, strdup(pchFilterName));
	else
		free(eaRemove(&pTabConfig->eaChannels, iExistingIndex));
	ChatConfigCommon_UpdateCommonTabs(pChatConfig, pTabConfig);
	return true;
}

// If bFiltered is false, this clears everything the filter, otherwise everything is filtered.
AUTO_TRANS_HELPER;
void ChatConfigCommon_ResetMessageTypeFilters(ATH_ARG NOCONST(ChatTabConfig) *pTabConfig, bool bFiltered)
{
	// First clean up the existing filters
	// This is a little lazy, but I don't have to deal with
	// checking which channels/message types are/aren't currently
	// filtered.
	while (eaSize(&pTabConfig->eaMessageTypes) > 0)
		free(eaPop(&pTabConfig->eaMessageTypes));

	// Create/recreate filters as necessary
	if (bFiltered)
	{
		int i;
		// Filter all message types
		for (i=1; ChatLogEntryTypeEnum[i].key; i++)
		{
			const char *pchMessageTypeName = ChatLogEntryTypeEnum[i].key;
			if (ChatCommon_IsFilterableMessageType(ChatLogEntryTypeEnum[i].value))
				eaPush(&pTabConfig->eaMessageTypes, strdup(pchMessageTypeName));
		}
	}
}

// If bFiltered is false, this clears everything the filter, otherwise everything is filtered.
AUTO_TRANS_HELPER;
void ChatConfigCommon_ResetChannelFilters(ATH_ARG NOCONST(ChatTabConfig) *pTabConfig, CONST_STRING_EARRAY *eaSubscribedChannels, bool bFiltered)
{
	// First clean up the existing filters
	// This is a little lazy, but I don't have to deal with
	// checking which channels/message types are/aren't currently
	// filtered.
	while (eaSize(&pTabConfig->eaChannels) > 0)
		free(eaPop(&pTabConfig->eaChannels));

	// Create/recreate filters as necessary
	if (bFiltered)
	{
		EARRAY_CONST_FOREACH_BEGIN(*eaSubscribedChannels, i, s);
		{
			if ((*eaSubscribedChannels)[i])
				eaPush(&pTabConfig->eaChannels, strdup((*eaSubscribedChannels)[i]));
		}
		EARRAY_FOREACH_END;
	}
}

AUTO_TRANS_HELPER;
bool ChatConfigCommon_SetTabFilterMode(ATH_ARG NOCONST(ChatTabConfig) *pTabConfig, CONST_STRING_EARRAY *eaSubscribedChannels, const char *pchTabGroup, int iTab, int bExclusiveFilter)
{
	ChatTabFilterMode eOldMessageTypeMode;
	ChatTabFilterMode eOldChannelMode;
	bool bChanged = false;

	// We store the filter as two separate filters because 
	// it's conceivable that it will one day be desirable to
	// deal with the filter modes separately and if that happens,
	// I'd rather have the data structures already set up and 
	// operating assuming they are separate than fix them up later.
	eOldMessageTypeMode = pTabConfig->eMessageTypeFilterMode;
	eOldChannelMode = pTabConfig->eChannelFilterMode;

	// Update the desired filter modes (we need to do this before we reset the filter)
	pTabConfig->eMessageTypeFilterMode = bExclusiveFilter ? kChatTabFilterMode_Exclusive : kChatTabFilterMode_Inclusive;
	pTabConfig->eChannelFilterMode = bExclusiveFilter ? kChatTabFilterMode_Exclusive : kChatTabFilterMode_Inclusive;

	// Check to see if anything changed.  If so, then re-create the 
	// filters, inverting them where necessary.
	// To convert an inclusion filter into an exclusion filter or vice versa, we must:
	// 1. Save a list of the old explicitly included/excluded items
	// 2. Clear the list and put all possible entries in it
	// 3. Remove all entries that match those in the saved list
	if (pTabConfig->eMessageTypeFilterMode != eOldMessageTypeMode)
	{
		// Move current message type list so we can invert it
		char **eaOriginalList = pTabConfig->eaMessageTypes;
		pTabConfig->eaMessageTypes = NULL;
		bChanged = true;

		// Clear and fully populate
		ChatConfigCommon_ResetMessageTypeFilters(pTabConfig, true);

		// Removed entries that match the saved ones
		EARRAY_CONST_FOREACH_BEGIN(eaOriginalList, i, s);
		{
			int index = eaFindString(&pTabConfig->eaMessageTypes, eaOriginalList[i]);
			if (index >= 0)
				free(eaRemove(&pTabConfig->eaMessageTypes, index));
		}
		EARRAY_FOREACH_END;
		eaDestroyEx(&eaOriginalList, NULL);
	}

	if (eaSubscribedChannels && eaSize(eaSubscribedChannels) && pTabConfig->eChannelFilterMode != eOldChannelMode)
	{
		// Move current channel list so we can invert it
		char **eaOriginalList = pTabConfig->eaChannels;
		pTabConfig->eaChannels = NULL;
		bChanged = true;

		// Clear and fully populate
		ChatConfigCommon_ResetChannelFilters(pTabConfig, eaSubscribedChannels, true); 

		// Removed entries that match the saved ones
		EARRAY_CONST_FOREACH_BEGIN(eaOriginalList, i, s);
		{
			int index = eaFindString(&pTabConfig->eaChannels, eaOriginalList[i]);
			if (index >= 0)
				free(eaRemove(&pTabConfig->eaChannels, index));
		}
		EARRAY_FOREACH_END;
		eaDestroyEx(&eaOriginalList, NULL);
	}
	return bChanged;
}

AUTO_TRANS_HELPER;
void ChatConfigCommon_SetFadeAwayDuration(ATH_ARG NOCONST(ChatConfig) *pChatConfig, float fFadeAwayDuration)
{
	pChatConfig->siFadeAwayDuration = (U16)(fFadeAwayDuration * 1000.0f);
	if (pChatConfig->siFadeAwayDuration <= 100)
		pChatConfig->siFadeAwayDuration = 0;
}

AUTO_TRANS_HELPER;
void ChatConfigCommon_SetTimeRequiredToStartFading(ATH_ARG NOCONST(ChatConfig) *pChatConfig, F32 fTimeRequiredToStartFading)
{
	pChatConfig->siTimeRequiredToStartFading = (U16)(fTimeRequiredToStartFading * 1000.0f);
	if (pChatConfig->siTimeRequiredToStartFading <= 100)
		pChatConfig->siTimeRequiredToStartFading = 0;
}

AUTO_TRANS_HELPER;
ChatConfigStatus ChatConfigCommon_SetStatusMessage(ATH_ARG NOCONST(ChatConfig) *pChatConfig, const char *pchMessage)
{
	bool bChanged = false;

	if (pchMessage && IsAnyProfane(pchMessage))
	{
		return CHATCONFIG_STATUS_PROFANE;
	}

	if (strcmp_safe(pchMessage, pChatConfig->pchStatusMessage))
	{
		if (pChatConfig->pchStatusMessage)
		{
			free(pChatConfig->pchStatusMessage);
			pChatConfig->pchStatusMessage = NULL;
		}

		if (pchMessage && *pchMessage)
			pChatConfig->pchStatusMessage = StructAllocStringLen(pchMessage, MIN((int)strlen(pchMessage), MAX_STATUS_LENGTH));
		return CHATCONFIG_STATUS_CHANGED;
	}
	return CHATCONFIG_STATUS_UNCHANGED;
}

#include "AutoGen/chatCommon_h_ast.c"
#include "AutoGen/chatCommonStructs_h_ast.c"

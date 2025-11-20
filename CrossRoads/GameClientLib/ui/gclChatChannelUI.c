#include "gclChatChannelUI.h"

#include "StringFormat.h"
/*#include "color.h"
#include "cmdparse.h"
#include "earray.h"
#include "StashTable.h"
#include "StringUtil.h"
#include "MemoryPool.h"

#include "GfxConsole.h"
#include "UIGen.h"
#include "gclDialogBox.h"*/

#include "chatCommon.h"
#include "chatCommonStructs.h"
#include "ChatData.h"
#include "gclChat.h"
#include "gclChatConfig.h"
#include "chat/gclClientChat.h"
#include "GameClientLib.h"
#include "gclUIGen.h"
#include "rand.h"
#include "HashFunctions.h"
#include "fileutil.h"
#include "StringUtil.h"

/*#include "Powers.h"
#include "SimpleParser.h"
#include "FCInventoryUI.h"
#include "ui/UIGenChatLog.h"
#include "UI/MapUI.h"
#include "soundLib.h"*/
#include "sndVoice.h"

#include "Autogen/chatCommon_h_ast.h"
#include "Autogen/chatCommonStructs_h_ast.h"
#include "Autogen/ChatData_h_ast.h"
#include "Autogen/gclChatConfig_h_ast.h"
#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "Autogen/UIGen_h_ast.h"
#include "AutoGen/ChatRelay_autogen_GenericServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););
//AUTO_RUN_ANON(memBudgetAddMapping("chatCommon", BUDGET_GameSystems););
//AUTO_RUN_ANON(memBudgetAddMapping("chatCommonStructs", BUDGET_GameSystems););
//AUTO_RUN_ANON(memBudgetAddMapping("gameserverlib_autogen_servercmdwrappers", BUDGET_GameSystems););

extern ChatChannelMember *GetChannelMember(SA_PARAM_OP_VALID const ChatChannelInfo *pInfo, SA_PARAM_OP_STR const char *pchHandle);
extern void ClientChat_AddChannelToDropdown(ChatChannelInfo *channelInfo, bool bSetAsCurrentChannel);
extern void ClientChat_RemoveChannelFromDropdown(const char *pchSystemName);
static char *spSelectedAdminChannelName = NULL;
static ChatChannelInfo *s_pAdminChannelDetail = NULL;

static int ClientChat_GetChannelInfoIndex(const char *pchChannelName)
{
	ChatState *pChatState = ClientChat_GetChatState();
	int i, size;
	if (!pChatState || !pchChannelName || !*pchChannelName)
		return -1;
	size = eaSize(&pChatState->eaChannels);
	for (i=0; i<size; i++)
	{
		if (stricmp(pChatState->eaChannels[i]->pName, pchChannelName) == 0)
			return i;
	}
	return -1;
}
static int ClientChat_GetReservedInfoIndex(const char *pchChannelName)
{
	ChatState *pChatState = ClientChat_GetChatState();
	if (!pChatState || !pchChannelName || !*pchChannelName)
		return -1;
	return eaIndexedFindUsingString(&pChatState->eaReservedChannels, pchChannelName);
}
static ChatChannelInfo *ClientChat_GetChannelInfo(const char *pchChannelName)
{
	ChatState *pChatState = ClientChat_GetChatState();
	int idx;
	if (!pChatState || !pchChannelName || !*pchChannelName)
		return NULL;
	idx = ClientChat_GetChannelInfoIndex(pchChannelName);
	if (idx >= 0)
		return pChatState->eaChannels[idx];
	idx = ClientChat_GetReservedInfoIndex(pchChannelName);
	if (idx >= 0)
		return pChatState->eaReservedChannels[idx];
	return NULL;
}
static ChatChannelInfo *ClientChat_GetReservedChannelInfo(const char *pchChannelName)
{
	ChatState *pChatState = ClientChat_GetChatState();
	if (!pChatState || !pchChannelName || !*pchChannelName)
		return NULL;
	return eaIndexedGetUsingString(&pChatState->eaReservedChannels, pchChannelName);
}
ChatChannelInfo *ClientChat_GetCachedChannelInfo(const char *pchChannelName)
{
	ChatChannelInfo *info = ClientChat_GetChannelInfo(pchChannelName);
	if (info)
		return info;
	return ClientChat_GetReservedChannelInfo(pchChannelName);
}


static int ClientChat_FindMemberIndex(SA_PARAM_NN_VALID ChatChannelInfo *info, const char *memberHandle)
{
	int i, size;
	if (!memberHandle || !*memberHandle)
		return -1;
	size = eaSize(&info->ppMembers);
	for (i=0; i<size; i++)
	{
		if (stricmp(info->ppMembers[i]->handle, memberHandle) == 0)
			return i;
	}
	return -1;
}

void ClientChat_ClearSelectedAdminChannel(void)
{
	s_pAdminChannelDetail = NULL;
}

static S32 s_iChatChannelFakeMembers;
AUTO_CMD_INT(s_iChatChannelFakeMembers, Chat_ChannelFakeMembers) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug);

static const char *CreateName(int *seed)
{
	static char s_v[] = { 'a', 'e', 'i', 'o', 'u', 'y' };
	static char s_c[] = { 'b', 'c', 'd', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'p', 'q', 'r', 's', 't', 'v', 'w', 'x', 'y', 'z' };
	static char s_ncb[] = "pc tk wk dt pw";
	static char s_achBuffer[256];
	int state = 0;
	int length = 0;
	int i;
	enum {
		vowel = 1, consonant = 2,
		blend = 4,
	} flags[5*3] = {0};

#define RNG_VALUE	randomU32Seeded(seed, RandType_LCG)
#define RNG_BLEND	(RNG_VALUE <= 0x3FFFFFFF ? blend : 0)
#define RANDOM_ELEMENT(array)	((array)[RNG_VALUE % ARRAY_SIZE(array)])
	for (i = 0; i < 5; i++)
	{
		switch (RNG_VALUE % 4)
		{
		xcase 0:
			flags[state++] = vowel | RNG_BLEND;
		xcase 1:
			flags[state++] = vowel | RNG_BLEND;
			flags[state++] = consonant | RNG_BLEND;
		xcase 2:
			flags[state++] = consonant | RNG_BLEND;
			flags[state++] = vowel | RNG_BLEND;
		xcase 3:
			flags[state++] = vowel | RNG_BLEND;
			flags[state++] = consonant | RNG_BLEND;
			flags[state++] = vowel | RNG_BLEND;
		}
	}

	for (i = 0; i < state && length < ARRAY_SIZE(s_achBuffer) - 4; i++)
	{
		if (flags[i] & vowel)
		{
			s_achBuffer[length++] = RANDOM_ELEMENT(s_v);
			if (flags[i] & blend)
				s_achBuffer[length++] = RANDOM_ELEMENT(s_v);
		}
		else
		{
			s_achBuffer[length++] = RANDOM_ELEMENT(s_c);
			if (flags[i] & blend)
			{
				do
				{
					s_achBuffer[length] = RANDOM_ELEMENT(s_c);
					s_achBuffer[length + 1] = 0;
				}
				while (strstr(s_ncb, &s_achBuffer[length-1]));
				length++;
			}
		}
	}
#undef RANDOM_ELEMENT
#undef RNG_BLEND
#undef RNG_VALUE

	s_achBuffer[length] = '\0';
	return s_achBuffer;
}

static void ClientChat_AddFakeMembers(ChatChannelInfo *pInfo)
{
	int seed = pInfo->pName ? hashStringInsensitive(pInfo->pName) : hashStringInsensitive("Bad Channel Name");
	S32 i;
	bool bDisableIndex = false;
	char *estrTemp = NULL;

	if (!eaIndexedGetTable(&pInfo->ppMembers))
	{
		eaIndexedEnable(&pInfo->ppMembers, parse_ChatChannelMember);
		bDisableIndex = true;
	}

	estrStackCreate(&estrTemp);

	for (i = 0; i < s_iChatChannelFakeMembers; i++)
	{
		ChatChannelMember *pMember = StructCreate(parse_ChatChannelMember);
		F32 fRng;
		int try = 0;

		do
		{
			if (try++ < 100)
				estrPrintf(&estrTemp, "%s", CreateName(&seed));
			else
				estrPrintf(&estrTemp, "%s%d", CreateName(&seed), i);
		}
		while (eaIndexedGetUsingString(&pInfo->ppMembers, estrTemp));

		pMember->uID = hashStringInsensitive(estrTemp) | 0x80000000;
		estrCopy2(&pMember->handle, estrTemp);

		fRng = randomF32Seeded(&seed, RandType_LCG);
		pMember->bOnline = fRng >= 0.55;
		if (!pMember->bOnline)
			pMember->bInvited = fRng >= 0.50;

		if (!pMember->bInvited)
			pMember->bSilenced = randomF32Seeded(&seed, RandType_LCG) >= 0.95;

		fRng = randomF32Seeded(&seed, RandType_LCG);
		pMember->eUserLevel = fRng >= 0.08 ? CHANUSER_USER : fRng >= 0.02 ? CHANUSER_OPERATOR :  fRng >= 0.01 ? CHANUSER_ADMIN : CHANUSER_USER; // Unlucky member that had a random value < 0.01 gets to be a plain user

		if (!pMember->bInvited)
		{
			switch (pMember->eUserLevel)
			{
			case CHANUSER_ADMIN:
				pMember->uPermissionFlags |= CHANPERM_DEMOTE | CHANPERM_PROMOTE | CHANPERM_MODIFYCHANNEL | CHANPERM_DESCRIPTION | CHANPERM_MOTD;

				// NOTE: fallthrough to get OPERATOR and USER permissions
			case CHANUSER_OPERATOR:
				pMember->uPermissionFlags |= CHANPERM_MUTE | CHANPERM_KICK | CHANPERM_INVITE;

				// NOTE: fallthrough to get USER permissions
			case CHANUSER_USER:
				pMember->uPermissionFlags |= CHANPERM_JOIN | CHANPERM_SEND | CHANPERM_RECEIVE;
			}
		}

		eaPush(&pInfo->ppMembers, pMember);
		if (pMember->bInvited)
			pInfo->uInvitedMemberCount++;
		if (pMember->bOnline)
			pInfo->uOnlineMemberCount++;
		pInfo->uMemberCount++;
	}

	if (bDisableIndex)
	{
		eaIndexedDisable(&pInfo->ppMembers);
	}

	estrDestroy(&estrTemp);
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_GENERICCLIENTCMD ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(CHATRELAY) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void ClientChat_ReceiveUserChannelList (ChatChannelInfoList *pList)
{
	ChatState *pChatState = ClientChat_GetChatState();
	int i=0, size;

	// Remove all cached user channels
	ClientChat_ResetSubscribedChannels(false);

	if (!pChatState)
		return;

	ClientChat_ClearSelectedAdminChannel(); // clear this first before destroying stuff
	eaClearStruct(&pChatState->eaChannels, parse_ChatChannelInfo);
	eaClearStruct(&pChatState->eaReservedChannels, parse_ChatChannelInfo);
	eaCopyStructs(&pList->ppChannels, &pChatState->eaChannels, parse_ChatChannelInfo);

	eaIndexedDisable(&pChatState->eaChannels);
	size = eaSize(&pChatState->eaChannels);
	while (i<size)
	{
		ChatChannelInfo *pInfo = pChatState->eaChannels[i];
		if (pInfo->ppMembers)
		{
			ClientChat_AddFakeMembers(pInfo);
		}
		FillChannelStatus(pInfo, false);
		FillAccessLevel(&pInfo->pchUserLevel, pInfo->eUserLevel);
		FillChannelMemberStatuses(pInfo);
		eaIndexedDisable(&pInfo->ppMembers);
		eaQSort(pInfo->ppMembers, ChannelMemberComparator); // Sort the members, if present
		ClientChat_AddChannelToDropdown(pInfo, false);

		if (IsChannelFlagShardOnly(pInfo->uReservedFlags))
		{   // After adding to dropdown, remove shard reserved channels from the config UI list
			eaIndexedAdd(&pChatState->eaReservedChannels, pChatState->eaChannels[i]);
			eaRemove(&pChatState->eaChannels, i);
			size--;
		}
		else
			i++;
	}
	eaQSort(pChatState->eaChannels, ChannelComparator);
	// Previously selected admin channel name for the dialog was removed
	s_pAdminChannelDetail = ClientChat_GetChannelInfo(spSelectedAdminChannelName);
	if (spSelectedAdminChannelName && !s_pAdminChannelDetail)
		estrDestroy(&spSelectedAdminChannelName);
#ifdef USE_CHATRELAY
	// Inform the GameServer to put the subscribed channel list on the Entity for filter resetting
	ServerCmd_ServerChat_SetSubscribedCustomChannels(pList);
#endif
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_GENERICCLIENTCMD ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(CHATRELAY) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void ClientChat_ChannelUpdate(ChatChannelInfo *channel_info, ChannelUpdateEnum eChangeType)
{
	ChatState *pChatState = ClientChat_GetChatState();
	ChatChannelInfo *pChannelInfo;
	if (!pChatState)
		return;
	pChannelInfo = ClientChat_GetChannelInfo(channel_info->pName);
	switch (eChangeType)
	{
		// Reserved channels only have UPDATE and REMOVE updates
	case CHANNELUPDATE_UPDATE: // also for adding channels - reinits ALL data
	case CHANNELUPDATE_UPDATE_NO_MEMBERS: // intentional fall-through; this will un-init members if they were inited
		{
			if (!pChannelInfo && IsChannelFlagShardOnly(channel_info->uReservedFlags))
				pChannelInfo = ClientChat_GetReservedChannelInfo(channel_info->pName);
			if (pChannelInfo)
			{
				ChatChannelMember **ppMembers = NULL;
				if (!channel_info->bMembersInitialized && pChannelInfo->bMembersInitialized)
				{   // Save the member info!
					ppMembers = pChannelInfo->ppMembers;
					pChannelInfo->ppMembers = NULL; // so it doesn't get freed
				}
				StructDeInit(parse_ChatChannelInfo, pChannelInfo);
				StructCopy(parse_ChatChannelInfo, channel_info, pChannelInfo, 0, 0, 0);
				if (ppMembers)
				{
					pChannelInfo->ppMembers = ppMembers;
					pChannelInfo->bMembersInitialized = true;
				}
				ClientChat_AddFakeMembers(pChannelInfo);
				FillChannelStatus(pChannelInfo, false);
				FillAccessLevel(&pChannelInfo->pchUserLevel, pChannelInfo->eUserLevel);
				FillChannelMemberStatuses(pChannelInfo);
				ClientChat_AddChannelToDropdown(pChannelInfo, false);
			}
			else
			{
				pChannelInfo = StructClone(parse_ChatChannelInfo, channel_info);
				if (pChannelInfo)
				{
					FillChannelStatus(pChannelInfo, false);
					FillAccessLevel(&pChannelInfo->pchUserLevel, pChannelInfo->eUserLevel);
					FillChannelMemberStatuses(pChannelInfo);
					ClientChat_AddChannelToDropdown(pChannelInfo, false);
					if (IsChannelFlagShardOnly(pChannelInfo->uReservedFlags))
					{
						eaIndexedAdd(&pChatState->eaReservedChannels, pChannelInfo);
					}
					else
					{
						eaIndexedDisable(&pChatState->eaChannels);
						eaPush(&pChatState->eaChannels, pChannelInfo);
						eaQSort(pChatState->eaChannels, ChannelComparator);
					}
				}
			}
		}
	xcase CHANNELUPDATE_REMOVE:
		{
			int idx = ClientChat_GetChannelInfoIndex(channel_info->pName);
			ChatChannelInfo *pRemoved = NULL;
			ChatConfigInputChannel *pCurChannel  = ClientChatConfig_GetCurrentInputChannel();
			if (idx >= 0)
			{
				pRemoved = eaRemove(&pChatState->eaChannels, idx);
			}
			else
			{
				idx = ClientChat_GetReservedInfoIndex(channel_info->pName);
				if (idx >= 0)
				{
					pRemoved = eaRemove(&pChatState->eaReservedChannels, idx);
				}
			}
			if (pRemoved && pRemoved == s_pAdminChannelDetail)
			{
				s_pAdminChannelDetail = NULL; // this was just destroyed
				estrDestroy(&spSelectedAdminChannelName);
			}
			if (pRemoved && pCurChannel && 
				((pRemoved->uReservedFlags == 0 && stricmp_safe(pRemoved->pName, pCurChannel->pchSystemName) == 0) ||
				(pRemoved->uReservedFlags && stricmp_safe(pRemoved->pDisplayName, pCurChannel->pchSystemName) == 0)) )
			{
				ClientChatConfig_SetCurrentChannelForTab(ClientChatConfig_GetCurrentTabGroupName(), 
					(int) ClientChatConfig_GetCurrentTabIndex(), 
					LOCAL_CHANNEL_NAME);
			}
			if (pRemoved)
				StructDestroy(parse_ChatChannelInfo, pRemoved);
			ClientChat_RemoveChannelFromDropdown(channel_info->pName);
			svChannelLeaveByName(channel_info->pName, NULL, true);
		}
	xcase CHANNELUPDATE_MEMBER_UPDATE: // also for adding members
		{
			if (pChannelInfo)
			{
				int i, size = eaSize(&channel_info->ppMembers);
				for (i=0; i<size; i++)
				{
					ChatChannelMember *pMemberUpdate = channel_info->ppMembers[i];
					ChatChannelMember *pMember = GetChannelMember(pChannelInfo, pMemberUpdate->handle);
					if (pMember)
					{
						StructDeInit(parse_ChatChannelMember, pMember);
						StructCopy(parse_ChatChannelMember, pMemberUpdate, pMember, 0, 0, 0);
						FillChannelMemberStatus(pMember);
						FillAccessLevel(&pMember->pchUserLevel, pMember->eUserLevel);
					}
					else
					{
						eaPush(&pChannelInfo->ppMembers, StructClone(parse_ChatChannelMember, pMemberUpdate));
						eaQSort(pChannelInfo->ppMembers, ChannelMemberComparator);
					}
				}
				pChannelInfo->uMemberCount = channel_info->uMemberCount;
				pChannelInfo->uInvitedMemberCount = channel_info->uInvitedMemberCount;
				pChannelInfo->uOnlineMemberCount = channel_info->uOnlineMemberCount;
			}
		}
	xcase CHANNELUPDATE_MEMBER_REMOVE:
		{
			if (pChannelInfo)
			{
				int i, size = eaSize(&channel_info->ppMembers);
				for (i=0; i<size; i++)
				{
					ChatChannelMember *pMemberUpdate = channel_info->ppMembers[i];
					int idx = ClientChat_FindMemberIndex(pChannelInfo, pMemberUpdate->handle);
					if (idx >= 0)
					{
						StructDestroy(parse_ChatChannelMember, pChannelInfo->ppMembers[idx]);
						eaRemove(&pChannelInfo->ppMembers, idx);
					}
				}
				pChannelInfo->uMemberCount = channel_info->uMemberCount;
				pChannelInfo->uInvitedMemberCount = channel_info->uInvitedMemberCount;
				pChannelInfo->uOnlineMemberCount = channel_info->uOnlineMemberCount;
			}
		}
	xcase CHANNELUPDATE_VOICE_ENABLED: 
		{
			if(pChannelInfo)
			{
				pChannelInfo->voiceEnabled = channel_info->voiceEnabled;
				pChannelInfo->voiceId = channel_info->voiceId;
				pChannelInfo->voiceURI = StructAllocString(channel_info->voiceURI);

				svChannelJoinByName(NULL, pChannelInfo->pName, pChannelInfo->voiceURI, true);
			}
		}
	xcase CHANNELUPDATE_DESCRIPTION:
		{
			if (pChannelInfo)
			{
				if (channel_info->pDescription)
					estrCopy2(&pChannelInfo->pDescription, channel_info->pDescription);
				else
					estrDestroy(&pChannelInfo->pDescription);
			}
		}
	xcase CHANNELUPDATE_MOTD:
		{
			if (pChannelInfo)
			{
				StructDeInit(parse_ChatChannelMessage, &pChannelInfo->motd);
				StructCopy(parse_ChatChannelMessage, &channel_info->motd, &pChannelInfo->motd, 0, 0, 0);
			}
		}
	xcase CHANNELUPDATE_CHANNEL_PERMISSIONS:
		{
			if (pChannelInfo)
			{
				int i;
				pChannelInfo->eChannelAccess = channel_info->eChannelAccess;
				for (i=0; i<CHANUSER_COUNT; i++)
				{
					pChannelInfo->permissionLevels[i] = channel_info->permissionLevels[i];
				}
				pChannelInfo->uReservedFlags = channel_info->uReservedFlags;
				FillChannelStatus(pChannelInfo, false);
			}

		}
	xcase CHANNELUPDATE_USER_PERMISSIONS:
		{
			if (pChannelInfo)
			{
				pChannelInfo->eUserLevel = channel_info->eUserLevel;
				pChannelInfo->uPermissionFlags = channel_info->uPermissionFlags;
				pChannelInfo->bUserInvited = channel_info->bUserInvited;
				pChannelInfo->bUserSubscribed = channel_info->bUserSubscribed;
				pChannelInfo->bSilenced = channel_info->bSilenced;
				pChannelInfo->uAccessLevel = channel_info->uAccessLevel;
				FillAccessLevel(&pChannelInfo->pchUserLevel, pChannelInfo->eUserLevel);
			}
		}
	xcase CHANNELUPDATE_USER_ONLINE:
	case CHANNELUPDATE_USER_OFFLINE: // Intentional fall-through
		{
			if (pChannelInfo)
			{
				int i, size = eaSize(&channel_info->ppMembers);
				for (i=0; i<size; i++)
				{
					ChatChannelMember *pMemberUpdate = channel_info->ppMembers[i];
					ChatChannelMember *member = GetChannelMember(pChannelInfo, pMemberUpdate->handle);
					if (member)
						member->bOnline = pMemberUpdate->bOnline;
				}
				// Update Online Count
				pChannelInfo->uOnlineMemberCount = channel_info->uOnlineMemberCount;
			}
		}
	}
}

static ChatAccess GetChannelAccessFromString(const char *pchAccess) {
	if (pchAccess && *pchAccess) {
		return StaticDefineIntGetIntDefault(ChatAccessEnum, pchAccess, 0);
	}
	devassertmsgf(0, "GetChannelPrivilegeFromString: Invalid privilege: '%s'", pchAccess);
	return 0;
}

static ChannelUserPrivileges GetUserPrivilegeFromString(const char *pchPrivilege) {
	if (pchPrivilege && *pchPrivilege) {
		return StaticDefineIntGetIntDefault(ChatAccessEnum, pchPrivilege, 0);
	}
	devassertmsgf(0, "GetUserPrivilegeFromString: Invalid privilege: '%s'", pchPrivilege);
	return 0;
}
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_IsChannelDestroyable);
bool ClientChat_IsChannelDestroyable(const char *pchChannel) {
	ChatChannelInfo *pInfo = ClientChat_GetChannelInfo(pchChannel);
	if (!pInfo)
		return false;
	if (pInfo->eUserLevel == CHANUSER_GM)
		return true;
	if (pInfo->eUserLevel <= CHANUSER_OWNER)
		return (pInfo->permissionLevels[pInfo->eUserLevel] & CHANPERM_DESTROY) != 0;
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_IsChannelLeavable);
bool ClientChat_IsChannelLeavable(const char *pchChannel) {
	ChatChannelInfo *pInfo = ClientChat_GetChannelInfo(pchChannel);
	return (pInfo && pInfo->bUserSubscribed);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_IsChannelOperable);
bool ClientChat_IsChannelOperable(const char *pchChannel) {
	ChatChannelInfo *pInfo = ClientChat_GetChannelInfo(pchChannel);
	return (pInfo && pInfo->eUserLevel != CHANUSER_USER);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_IsReservedChannel);
bool ClientChat_IsReservedChannel(const char *pchChannel) {
	return ChatCommon_IsBuiltInChannel(pchChannel);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_GetChannelAccess);
bool ClientChat_GetChannelAccess(const char* pchChannel, const char *pchAccess) {
	ChatChannelInfo *pInfo = ClientChat_GetChannelInfo(pchChannel);

	if (pInfo) {
		U32 iPrivelege = GetChannelAccessFromString(pchAccess);
		return !!(pInfo->eChannelAccess & iPrivelege);
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_GetChannelPlayerPrivilege);
bool ClientChat_GetChannelPlayerPrivilege(const char *pchChannel, const char *pchPrivilege) {
	ChatChannelInfo *pInfo = ClientChat_GetChannelInfo(pchChannel);

	if (pInfo) {
		U32 iPrivilege = GetUserPrivilegeFromString(pchPrivilege);
		return !!(pInfo->uPermissionFlags & iPrivilege);
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_GetChannelMemberPrivilege);
bool ClientChat_GetChannelMemberPrivilege(const char *pchChannel, const char *pchMember, const char *pchPrivilege) {
	ChatChannelInfo *pInfo = ClientChat_GetChannelInfo(pchChannel);
	ChatChannelMember *pMember = GetChannelMember(pInfo, pchMember);

	if (pMember) {
		U32 iPrivilege = GetUserPrivilegeFromString(pchPrivilege);
		return !!(pMember->uPermissionFlags & iPrivilege);
	}

	return false;
}

/////////////////////////////////////
// Channel Admin UI Functions

static void RefreshChannelInfoWithMembers(const char *pchChannel, bool bForced) {
	ChatChannelInfo *pChannelInfo = ClientChat_GetChannelInfo(pchChannel);
	if (!bForced && pChannelInfo && pChannelInfo->bMembersInitialized) // already initialized
		return;
#ifdef USE_CHATRELAY
	GServerCmd_crRequestFullChannelInfo(GLOBALTYPE_CHATRELAY, pchChannel);
#else
	ServerCmd_cmdServerChat_RequestFullChannelInfo(pchChannel);
#endif
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Channel_InitAdminDetail);
void ClientChatAdmin_InitChannelDetail(const char *pchChannel) {
	estrCopy2(&spSelectedAdminChannelName, pchChannel);
	s_pAdminChannelDetail = ClientChat_GetChannelInfo(pchChannel);
	if (s_pAdminChannelDetail)
		RefreshChannelInfoWithMembers(pchChannel, false);
	else
		estrDestroy(&spSelectedAdminChannelName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Channel_RefreshAdminDetail);
void ClientChatAdmin_RefreshChannelDetail(const char *pchChannel) {
	estrCopy2(&spSelectedAdminChannelName, pchChannel);
	s_pAdminChannelDetail = ClientChat_GetChannelInfo(pchChannel);
	if (s_pAdminChannelDetail)
		RefreshChannelInfoWithMembers(pchChannel, true);
	else
		estrDestroy(&spSelectedAdminChannelName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_GetAdminChannelDetail);
SA_RET_OP_VALID ChatChannelInfo *ClientChatAdmin_GetChannelDetail(void)
{
	return s_pAdminChannelDetail;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_GetAdminMemberCount);
S32 ClientChatAdmin_GetMemberCount(void)
{
	return s_pAdminChannelDetail ? eaSize(&s_pAdminChannelDetail->ppMembers) : 0;
}

static bool StringCheckFilter(const char *pchString, const char *pchString2, const char ***peaRequireAll, const char ***peaRequireAny, const char ***peaExcludeAny)
{
	bool bString = pchString && *pchString;
	bool bString2 = pchString2 && *pchString2;
	S32 i;

	if (!bString && !bString2)
		return (!peaRequireAll || !eaSize(peaRequireAll)) && (!peaRequireAny || !eaSize(peaRequireAny));

	if (peaRequireAll)
	{
		for (i = eaSize(peaRequireAll) - 1; i >= 0; i--)
			if (bString && !isWildcardMatch((*peaRequireAll)[i], pchString, false, false)
				&& bString2 && !isWildcardMatch((*peaRequireAll)[i], pchString2, false, false))
				return false;
	}

	if (peaExcludeAny)
	{
		for (i = eaSize(peaExcludeAny) - 1; i >= 0; i--)
			if (bString && isWildcardMatch((*peaExcludeAny)[i], pchString, false, false)
				|| bString2 && isWildcardMatch((*peaExcludeAny)[i], pchString2, false, false))
				return false;
	}

	if (peaRequireAny && eaSize(peaRequireAny) > 0)
	{
		for (i = eaSize(peaRequireAny) - 1; i >= 0; i--)
			if (bString && isWildcardMatch((*peaRequireAny)[i], pchString, false, false)
				|| bString2 && isWildcardMatch((*peaRequireAny)[i], pchString2, false, false))
				break;
		if (i < 0)
			return false;
	}

	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_FillAdminChannelMembersEx);
S32 ClientChatAdmin_FillChannelMembersEx(SA_PARAM_NN_VALID UIGen *pGen, S32 iPage, S32 iPageSize, const char *pchFilter)
{
	static ChatChannelMember **s_eaCache;
	ChatChannelMember ***peaMembers = ui_GenGetManagedListSafe(pGen, ChatChannelMember);
	S32 iTotal = 0, iSkip;

	MAX1(iPageSize, 1);
	MAX1(iPage, 0);
	iSkip = iPage * iPageSize;

	if (s_pAdminChannelDetail && s_pAdminChannelDetail->bMembersInitialized) {
		int numMembers = eaSize(&s_pAdminChannelDetail->ppMembers);
		int numEntries = numMembers;
		int i, iCount = 0;
		char *pchFilterBuffer, *pchToken, *pchContext;

		// Filters
		static const char **s_eaRequireAllHandle;
		static const char **s_eaRequireAnyHandle;
		static const char **s_eaExcludeAnyHandle;
		static const char **s_eaRequireAllStatus;
		static const char **s_eaRequireAnyStatus;
		static const char **s_eaExcludeAnyStatus;
		static const char **s_eaRequireAllLevel;
		static const char **s_eaRequireAnyLevel;
		static const char **s_eaExcludeAnyLevel;
		static const char **s_eaRequireAll;
		static const char **s_eaRequireAny;
		static const char **s_eaExcludeAny;
		const char ***peaRequireAllToken = &s_eaRequireAll;
		const char ***peaRequireAnyToken = &s_eaRequireAny;
		const char ***peaExcludeAnyToken = &s_eaExcludeAny;
		int chMode = 0;
		bool bAll = pchFilter && strcmp(pchFilter, "*") == 0;
		bool bFilterSet = false;

		eaClear(&s_eaRequireAllHandle);
		eaClear(&s_eaRequireAnyHandle);
		eaClear(&s_eaExcludeAnyHandle);
		eaClear(&s_eaRequireAllStatus);
		eaClear(&s_eaRequireAnyStatus);
		eaClear(&s_eaExcludeAnyStatus);
		eaClear(&s_eaRequireAllLevel);
		eaClear(&s_eaRequireAnyLevel);
		eaClear(&s_eaExcludeAnyLevel);
		eaClear(&s_eaRequireAll);
		eaClear(&s_eaRequireAny);
		eaClear(&s_eaExcludeAny);

		strdup_alloca(pchFilterBuffer, pchFilter);

		// Parse tokens
		pchToken = pchContext = pchFilterBuffer;
		for (;;)
		{
			bool bBoundary = false;

			if (!*pchContext || *pchContext == ':' || isspace(*pchContext & 0xff))
				bBoundary = true;

			if (pchToken == pchContext && (bBoundary || (peaRequireAllToken == &s_eaRequireAll && (*pchContext == '+' || *pchContext == '-'))))
			{
				if (*pchContext == '+' || *pchContext == '-')
					chMode = *pchContext;
				else
					chMode = 0;
				peaRequireAllToken = &s_eaRequireAll;
				peaRequireAnyToken = &s_eaRequireAny;
				peaExcludeAnyToken = &s_eaExcludeAny;
				if (*pchContext)
					pchToken = ++pchContext;
			}
			else if (bBoundary)
			{
				if (*pchContext == ':')
				{
					if (strStartsWith(pchToken, "handle:"))
					{
						peaRequireAllToken = &s_eaRequireAllHandle;
						peaRequireAnyToken = &s_eaRequireAnyHandle;
						peaExcludeAnyToken = &s_eaExcludeAnyHandle;
					}
					else if (strStartsWith(pchToken, "status:"))
					{
						peaRequireAllToken = &s_eaRequireAllStatus;
						peaRequireAnyToken = &s_eaRequireAnyStatus;
						peaExcludeAnyToken = &s_eaExcludeAnyStatus;
					}
					else if (strStartsWith(pchToken, "level:"))
					{
						peaRequireAllToken = &s_eaRequireAllLevel;
						peaRequireAnyToken = &s_eaRequireAnyLevel;
						peaExcludeAnyToken = &s_eaExcludeAnyLevel;
					}
					else
					{
						peaRequireAllToken = &s_eaRequireAll;
						peaRequireAnyToken = &s_eaRequireAny;
						peaExcludeAnyToken = &s_eaExcludeAny;
					}
					pchToken = ++pchContext;
				}
				else
				{
					if (*pchContext)
						*pchContext++ = '\0';
					if (pchToken && *pchToken)
					{
						bFilterSet = true;
						if (chMode == '+')
							eaPush(peaRequireAllToken, pchToken);
						else if (chMode == '-')
							eaPush(peaExcludeAnyToken, pchToken);
						else
							eaPush(peaRequireAnyToken, pchToken);
					}
					peaRequireAllToken = &s_eaRequireAll;
					peaRequireAnyToken = &s_eaRequireAny;
					peaExcludeAnyToken = &s_eaExcludeAny;
					pchToken = pchContext;
					chMode = 0;
				}
			}
			else
			{
				pchContext++;
			}

			if (bBoundary && !*pchContext)
				break;
		}

		// Presort the member list according to the Gen
		// Sort in place is possible because ppMembers is assumed by everything to not have a defined order.
		ui_GenListSortListSafe(pGen, s_pAdminChannelDetail->ppMembers, ChatChannelMember);

		// Fill in member info based on the selected page.
		// If iPage > the last possible page, 
		for (i = 0; i < numMembers; i++) {
			ChatChannelMember *pMember;
			ChatChannelMember *pSrcMember = eaGet(&s_pAdminChannelDetail->ppMembers, i);

			if (!bAll)
			{
				// Hooray! Hardcoding strings here. This is totally not intended to be localized.
				// This is intended for check buttons to insert hardcoded search text into the filter.
				const char *pchOnline = pSrcMember->bOnline ? "Online" : "Offline";

				if (!bFilterSet)
					continue;

				if (!StringCheckFilter(pSrcMember->pchStatus, pchOnline, &s_eaRequireAllStatus, &s_eaRequireAnyStatus, &s_eaExcludeAnyStatus))
					continue;
				if (!StringCheckFilter(pSrcMember->pchUserLevel, NULL, &s_eaRequireAllLevel, &s_eaRequireAnyLevel, &s_eaExcludeAnyLevel))
					continue;
				if (!StringCheckFilter(pSrcMember->handle, NULL, &s_eaRequireAllHandle, &s_eaRequireAnyHandle, &s_eaExcludeAnyHandle))
					continue;

				if (eaSize(&s_eaExcludeAny))
				{
					if (!StringCheckFilter(pSrcMember->pchStatus, pchOnline, NULL, NULL, &s_eaExcludeAny)
						|| !StringCheckFilter(pSrcMember->pchUserLevel, NULL, NULL, NULL, &s_eaExcludeAny)
						|| !StringCheckFilter(pSrcMember->handle, NULL, NULL, NULL, &s_eaExcludeAny))
						continue;
				}

				if (eaSize(&s_eaRequireAll) || eaSize(&s_eaRequireAny))
				{
					if (!StringCheckFilter(pSrcMember->pchStatus, pchOnline, &s_eaRequireAll, &s_eaRequireAny, NULL)
						&& !StringCheckFilter(pSrcMember->pchUserLevel, NULL, &s_eaRequireAll, &s_eaRequireAny, NULL)
						&& !StringCheckFilter(pSrcMember->handle, NULL, &s_eaRequireAll, &s_eaRequireAny, NULL))
						continue;
				}
			}

			++iTotal;

			// Check to see if the previous page was filled
			if (iCount >= iPageSize)
			{
				// If all the previous pages have been skipped, stop here.
				if (iSkip < iTotal - 1)
					continue;

				// Start a new page
				iCount = 0;
			}

			pMember = eaGet(peaMembers, iCount++);
			if (!pMember)
			{
				pMember = eaSize(&s_eaCache) ? eaPop(&s_eaCache) : StructCreate(parse_ChatChannelMember);
				eaSet(peaMembers, pMember, iCount - 1);
			}

			if (!pMember->handle || stricmp(pMember->handle, pSrcMember->handle))
				estrCopy(&pMember->handle, &pSrcMember->handle);
			pMember->uID = pSrcMember->uID;

			// Member Status
			pMember->bOnline   = pSrcMember->bOnline;
			pMember->bInvited  = pSrcMember->bInvited;
			pMember->bSilenced = pSrcMember->bSilenced;

			pMember->eUserLevel       = pSrcMember->eUserLevel;
			pMember->uPermissionFlags = pSrcMember->uPermissionFlags;
			estrCopy(&pMember->pchStatus, &pSrcMember->pchStatus);
			pMember->pchUserLevel = pSrcMember->pchUserLevel; // Pooled string - ok to copy pointer
		}

		while (eaSize(peaMembers) > iCount)
			eaPush(&s_eaCache, eaPop(peaMembers));
	} else {
		eaClear(peaMembers);
	}

	ui_GenSetManagedListSafe(pGen, peaMembers, ChatChannelMember, true);
	return iTotal;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_FillAdminChannelMember);
void ClientChatAdmin_FillChannelMembers(SA_PARAM_NN_VALID UIGen *pGen)
{
	ClientChatAdmin_FillChannelMembersEx(pGen, 0, INT_MAX, "*");
}

// TODO fix everything below here
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_IsAdminChannelMember);
bool ClientChatAdmin_IsChannelMember(SA_PARAM_OP_STR const char *pchMember) {
	ChatChannelMember *pMember = GetChannelMember(s_pAdminChannelDetail, pchMember);
	return pMember != NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_CanChangeAdminChannelDescription);
bool ClientChat_CanChangeAdminChannelDescription(void) {
	if (s_pAdminChannelDetail) {
		return !!(s_pAdminChannelDetail->uPermissionFlags & CHANPERM_DESCRIPTION);
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_CanChangeAdminChannelMotd);
bool ClientChat_CanChangeAdminChannelMotd(void) {
	if (s_pAdminChannelDetail) {
		return !!(s_pAdminChannelDetail->uPermissionFlags & CHANPERM_MOTD);
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_CanModifyAdminChannel);
bool ClientChat_CanModifyAdminChannel(void) {
	if (s_pAdminChannelDetail) {
		return !!(s_pAdminChannelDetail->uPermissionFlags & CHANPERM_MODIFYCHANNEL);
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_CanDemoteAdminChannelMember);
bool ClientChat_CanDemoteAdminChannelMember(const char *pchMember) {
	ChatChannelMember *pMember = GetChannelMember(s_pAdminChannelDetail, pchMember);

	if (s_pAdminChannelDetail && pMember) {
		// First check to see if the user has DEMOTE privilege
		if (!(s_pAdminChannelDetail->uPermissionFlags & CHANPERM_DEMOTE)) {
			return false;
		}

		// We can only demote subscribed members
		if (pMember->bInvited) {
			return false;
		}

		// A member may only be demoted if their current access
		// level is below that of the user. (Note this may change
		// in the future to being "below or equal").
		return s_pAdminChannelDetail->eUserLevel > pMember->eUserLevel
			&& pMember->eUserLevel > CHANUSER_USER;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_CanInviteAdminChannelMember);
bool ClientChat_CanInviteAdminChannelMember(const char *pchMember) {
	ChatChannelMember *pMember = GetChannelMember(s_pAdminChannelDetail, pchMember);

	if (s_pAdminChannelDetail && !pMember && pchMember && *pchMember) {
		// First check to see if the user has INVITE privilege
		if (!(s_pAdminChannelDetail->uPermissionFlags & CHANPERM_INVITE)) {
			return false;
		}

		// pMember == null if the member is not subscribed or hasn't been invited, 
		// so if we reach this point the member may be invited.
		return true; 
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_CanKickAdminChannelMember);
bool ClientChat_CanKickAdminChannelMember(const char *pchMember) {
	ChatChannelMember *pMember = GetChannelMember(s_pAdminChannelDetail, pchMember);

	if (s_pAdminChannelDetail && pMember) {
		// First check to see if the user has KICK privilege
		if (!(s_pAdminChannelDetail->uPermissionFlags & CHANPERM_KICK)) {
			return false;
		}

		// A member may only kick be kicked if their level is below
		// the user.
		return s_pAdminChannelDetail->eUserLevel > pMember->eUserLevel;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_CanMuteAdminChannelMember);
bool ClientChat_CanMuteAdminChannelMember(const char *pchMember) {
	ChatChannelMember *pMember = GetChannelMember(s_pAdminChannelDetail, pchMember);

	if (s_pAdminChannelDetail && pMember) {
		// First check to see if the user has MUTE privilege
		if (!(s_pAdminChannelDetail->uPermissionFlags & CHANPERM_MUTE)) {
			return false;
		}

		// We can only mute subscribed members
		if (pMember->bInvited) {
			return false;
		}

		// A member may only muted if their level is below the user.
		return s_pAdminChannelDetail->eUserLevel > pMember->eUserLevel;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_CanPromoteAdminChannelMember);
bool ClientChat_CanPromoteAdminChannelMember(const char *pchMember) {
	ChatChannelMember *pMember = GetChannelMember(s_pAdminChannelDetail, pchMember);

	if (s_pAdminChannelDetail && pMember) {
		// First check to see if the user has PROMOTE privilege
		if (!(s_pAdminChannelDetail->uPermissionFlags & CHANPERM_PROMOTE)) {
			return false;
		}

		// We can only promote subscribed members
		if (pMember->bInvited) {
			return false;
		}

		// A member may only be promoted up to one level below
		// the user.  (Note this may change in the future to 
		// being "below or equal").
		return s_pAdminChannelDetail->eUserLevel-1 > pMember->eUserLevel
			&& pMember->eUserLevel < CHANUSER_COUNT-1;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_IsAdminChannelMemberMuted);
bool ClientChat_IsAdminChannelMemberMuted(const char *pchMember) {
	ChatChannelMember *pMember = GetChannelMember(s_pAdminChannelDetail, pchMember);
	return pMember ? pMember->bSilenced : false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_IsAdminChannelMemberInvited);
bool ClientChat_IsAdminChannelMemberInvited(const char *pchMember) {
	ChatChannelMember *pMember = GetChannelMember(s_pAdminChannelDetail, pchMember);
	return pMember ? pMember->bInvited : false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_DemoteAdminChannelMember);
void ClientChat_DemoteAdminChannelMember(const char *pchMember) {
	if (s_pAdminChannelDetail) {
#ifdef USE_CHATRELAY
		gclClientChat_ChannelDemoteUser(s_pAdminChannelDetail->pName, (char*)pchMember);
#else
		ServerCmd_chandemote(s_pAdminChannelDetail->pName, pchMember);
#endif
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_KickAdminChannelMember);
void ClientChat_KickAdminChannelMember(const char *pchMember) {
	if (s_pAdminChannelDetail) {
#ifdef USE_CHATRELAY
		gclClientChat_ChannelKickUser(s_pAdminChannelDetail->pName, (char*)pchMember);
#else
		ServerCmd_channel_kick(s_pAdminChannelDetail->pName, pchMember);
#endif
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_MuteAdminChannelMember);
void ClientChat_MuteAdminChannelMember(const char *pchMember, bool bValue) {
	if (s_pAdminChannelDetail) {
#ifdef USE_CHATRELAY
		if (bValue) {
			gclClientChat_MuteUser(s_pAdminChannelDetail->pName, (char*)pchMember);
		} else {
			gclClientChat_UnmuteUser(s_pAdminChannelDetail->pName, (char*)pchMember);
		}
#else
		if (bValue) {
			ServerCmd_mute(s_pAdminChannelDetail->pName, pchMember);
		} else {
			ServerCmd_unmute(s_pAdminChannelDetail->pName, pchMember);
		}
#endif
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_PromoteAdminChannelMember);
void ClientChat_PromoteAdminChannelMember(const char *pchMember) {
	if (s_pAdminChannelDetail) {
#ifdef USE_CHATRELAY
		gclClientChat_ChannelPromoteUser(s_pAdminChannelDetail->pName, (char*)pchMember);
#else
		ServerCmd_chanpromote(s_pAdminChannelDetail->pName, pchMember);
#endif
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_GetAdminChannelPrivilegesTableSMF);
char *ClientChat_GetAdminChannelPrivilegesTableSMF(void) {
	static char *pchPrivilegesTable = NULL;
	static U32 iPrivileges[CHANUSER_COUNT];

	if (s_pAdminChannelDetail) {
		bool bChanged = false;
		int i;

		if (!pchPrivilegesTable) {
			bChanged = true;
		}

		// Check/Update privileges
		if (pchPrivilegesTable) {
			for (i=0; i < CHANUSER_COUNT; i++) {
				if (s_pAdminChannelDetail->permissionLevels[i] != iPrivileges[i]) {
					iPrivileges[i] = s_pAdminChannelDetail->permissionLevels[i];
					bChanged = true;
				}
			}
		}

		// Recreate the privilege table string, if there's a change
		if (bChanged) {
			// Constant strings
			const char *pchAccessLevelTitle;
			const char *pchYes;
			const char *pchNo;
			const char *pchPriv;

			// Reusable EStrings
			char *pchRows = NULL;
			char *pchRow = NULL;
			char *pchRowItems = NULL;
			char *pchItem = NULL;
			char *pchPrivKey = NULL;

			int row;
			int col;

			// Fill in constants
			pchAccessLevelTitle = TranslateMessageKey("ChatConfig_ChannelAdmin_UserPriv");
			pchYes = TranslateMessageKey("ChatConfig_ChannelAdmin_List_AccessLevel_Col_Tooltip_Item_Yes");
			pchNo = TranslateMessageKey("ChatConfig_ChannelAdmin_List_AccessLevel_Col_Tooltip_Item_No");

			// Create Header Row

			estrClear(&pchItem);
			FormatMessageKey(&pchRowItems, "ChatConfig_ChannelAdmin_List_AccessLevel_Col_Tooltip_HeaderRowItem",
				STRFMT_STRING("Item", pchAccessLevelTitle), STRFMT_END);

			for (col=0; col < CHANUSER_COUNT; col++) {
				const char *pchAccessLevel = NULL;
				FillAccessLevel(&pchAccessLevel, col);

				estrClear(&pchItem);
				FormatMessageKey(&pchItem, "ChatConfig_ChannelAdmin_List_AccessLevel_Col_Tooltip_HeaderRowItem",
					STRFMT_STRING("Item", pchAccessLevel), STRFMT_END);
				estrAppend(&pchRowItems, &pchItem);
			}

			estrClear(&pchRow);
			FormatMessageKey(&pchRow, "ChatConfig_ChannelAdmin_List_AccessLevel_Col_Tooltip_HeaderRow", 
				STRFMT_STRING("HeaderRowItems", pchRowItems), STRFMT_END);
			estrAppend(&pchRows, &pchRow);
			estrAppend2(&pchRows, "\n"); // Simple pretty print to help with debugging

			// Create Body
			for (row=1; ChannelUserPrivilegesEnum[row].key; row++) {
				ChannelUserPrivileges priv = ChannelUserPrivilegesEnum[row].value;
				if (priv == CHANPERM_JOIN) {
					// Skip the 'join' privilege...it's kinda meaningless
					continue;
				}

				estrClear(&pchRow);
				estrClear(&pchRowItems);
				estrClear(&pchItem);
				estrClear(&pchPrivKey);

				// Row header item
				estrPrintf(&pchPrivKey, "ChatConfig_ChannelAdmin_UserPriv_%s", ChannelUserPrivilegesEnum[row].key);
				pchPriv = TranslateMessageKey(pchPrivKey);

				estrClear(&pchItem);
				FormatMessageKey(&pchItem, "ChatConfig_ChannelAdmin_List_AccessLevel_Col_Tooltip_BodyRowHeaderItem",
					STRFMT_STRING("Item", pchPriv), STRFMT_END);
				estrAppend(&pchRowItems, &pchItem);

				for (col=0; col < CHANUSER_COUNT; col++) {
					const char *pchValue = iPrivileges[col] & priv ? pchYes : pchNo;

					estrClear(&pchItem);
					FormatMessageKey(&pchItem, "ChatConfig_ChannelAdmin_List_AccessLevel_Col_Tooltip_BodyRowItem",
						STRFMT_STRING("Item", pchValue), STRFMT_END);
					estrAppend(&pchRowItems, &pchItem);
				}

				estrClear(&pchRow);
				FormatMessageKey(&pchRow, "ChatConfig_ChannelAdmin_List_AccessLevel_Col_Tooltip_BodyRow", 
					STRFMT_STRING("BodyRowItems", pchRowItems), STRFMT_END);
				estrAppend(&pchRows, &pchRow);
				estrAppend2(&pchRows, "\n"); // Simple pretty print to help with debugging

			}

			estrClear(&pchPrivilegesTable);
			FormatMessageKey(&pchPrivilegesTable, "ChatConfig_ChannelAdmin_List_AccessLevel_Col_Tooltip_Table", 
				STRFMT_STRING("Rows", pchRows), STRFMT_END);

			estrDestroy(&pchRows);
			estrDestroy(&pchRow);
			estrDestroy(&pchRowItems);
			estrDestroy(&pchItem);
			estrDestroy(&pchPrivKey);
		}

		return pchPrivilegesTable;
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_GetTeamUpChatChannelName);
const char *exprFuncChat_GetTeamUpChatChannelName(void)
{
	ChatState *state = ClientChat_GetChatState();

	if(state)
	{
		FOR_EACH_IN_EARRAY(state->eaReservedChannels, ChatChannelInfo, info)
		{
			if(info->uReservedFlags & CHANNEL_SPECIAL_TEAMUP)
				return info->pName;
		}
		FOR_EACH_END
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_GetTeamChatChannelName);
const char* exprFuncChat_GetTeamChatChannelSystemName(void)
{
	ChatState *state = ClientChat_GetChatState();

	if(state)
	{
		FOR_EACH_IN_EARRAY(state->eaReservedChannels, ChatChannelInfo, info)
		{
			if(info->uReservedFlags & CHANNEL_SPECIAL_TEAM)
				return info->pName;
		}
		FOR_EACH_END
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_GetGuildChatChannelName);
const char* exprFuncChat_GetGuildChatChannelSystemName(void)
{
	ChatState *state = ClientChat_GetChatState();

	if(state)
	{
		FOR_EACH_IN_EARRAY(state->eaReservedChannels, ChatChannelInfo, info)
		{
			if(info->uReservedFlags & CHANNEL_SPECIAL_GUILD)
				return info->pName;
		}
		FOR_EACH_END
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_ChannelIsVoiceEnabled);
bool exprFuncChat_ChannelIsVoiceEnabled(const char* channelName)
{
	ChatChannelInfo *info = ClientChat_GetChannelInfo(channelName);

	return info && info->voiceEnabled;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_GetChannelList);
void exprFuncChat_GetChannelList(UIGen *gen)
{
	ChatChannelInfo ***channels = ui_GenGetManagedListSafe(gen, ChatChannelInfo);
	ChatState *state = ClientChat_GetChatState();
	eaClearFast(channels);

	eaPushEArray(channels, &state->eaChannels);
	eaPushEArray(channels, &state->eaReservedChannels);

	ui_GenSetManagedListSafe(gen, channels, ChatChannelInfo, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_ChannelGetDisplayName);
const char* exprFuncChat_ChannelGetDisplayName(ChatChannelInfo *chan)
{
	if(chan->pDisplayName)
		return chan->pDisplayName;
	return chan->pName;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_ChannelSetVoiceTransmit);
void exprFuncChat_ChannelSetVoiceTransmit(ChatChannelInfo *chan)
{
	if(chan->voiceEnabled)
		svChannelSetTransmitByName(chan->pName, NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_ChannelIsVoiceTransmitting);
int exprFuncChat_ChannelIsVoiceTransmitting(ChatChannelInfo *chan)
{
	return chan->voiceEnabled && svChannelIsActive();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_ChannelIsVoiceTransmittingByChannelName);
int exprFuncChat_ChannelIsVoiceTransmittingByChannelName(const char* channelName)
{
	ChatChannelInfo *chan = ClientChat_GetChannelInfo(channelName);
	return chan && chan->voiceEnabled && svChannelIsActive();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_ChannelClearVoiceTransmit);
void exprFuncChat_ChannelClearVoiceTransmit(void)
{
	svClearTransmit();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_GetVoiceChannelList);
void exprFuncChat_GetVoiceChannelList(UIGen *gen)
{
	static ChatChannelInfo **channels = NULL;
	ChatState *state = ClientChat_GetChatState();
	eaClearFast(&channels);

	if(!state)
	{
		ui_GenSetManagedListSafe(gen, &channels, ChatChannelInfo, false);
		return;
	}

	if(!g_VoiceState.signed_in)
	{
		ui_GenSetManagedListSafe(gen, &channels, ChatChannelInfo, false);
		return;
	}

	FOR_EACH_IN_EARRAY_FORWARDS(state->eaChannels, ChatChannelInfo, info)
	{
		if(info->voiceEnabled)
			eaPush(&channels, info);
	}
	FOR_EACH_END

	FOR_EACH_IN_EARRAY_FORWARDS(state->eaReservedChannels, ChatChannelInfo, info)
	{
		if(info->voiceEnabled)
			eaPush(&channels, info);
	}
	FOR_EACH_END

	ui_GenSetManagedListSafe(gen, &channels, ChatChannelInfo, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_ChannelIsTeamChannel);
bool exprFuncChat_ChannelIsTeamChannel(ChatChannelInfo *chan)
{
	return chan && (chan->uReservedFlags & CHANNEL_SPECIAL_TEAM);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_ChannelIsGuildChannel);
bool exprFuncChat_ChannelIsGuildChannel(ChatChannelInfo *chan)
{
	return chan && (chan->uReservedFlags & CHANNEL_SPECIAL_GUILD);
}
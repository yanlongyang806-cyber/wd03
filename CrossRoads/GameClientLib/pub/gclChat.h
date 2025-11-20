/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM

#include "GlobalTypeEnum.h"

#include "chatCommon.h"

typedef enum ChannelUserLevel ChannelUserLevel;
typedef struct ChatChannelMember ChatChannelMember;
typedef struct ChatChannelInfo ChatChannelInfo;
typedef struct ChatChannelInfoList ChatChannelInfoList;
typedef struct ChatConfigInputChannel ChatConfigInputChannel;
typedef struct ChatData ChatData;
typedef struct ChatLink ChatLink;
typedef struct ChatPlayerList ChatPlayerList;
typedef struct ChatPlayerStruct ChatPlayerStruct;
typedef struct ChatUserInfo ChatUserInfo;
typedef struct CmdContext CmdContext;
typedef struct Item Item;
typedef struct ItemDef ItemDef;
typedef struct PowerDef PowerDef;
typedef struct UIGen UIGen;
typedef struct ChatLogFilter ChatLogFilter;

// Communication
void ClientChat_SendMessage(SA_PARAM_OP_VALID const char *pchText, SA_PARAM_OP_VALID ChatData *pData);
void ClientChat_SendPrivateMessage(SA_PARAM_OP_VALID const char *pchText, SA_PARAM_NN_STR const char *handle);
void ClientChat_SendChannelChat(Entity *pEnt, const char *channel_name, const char *pchText);
void ClientChat_SendGuildChat(Entity *pEnt, const char *pchText);
void ClientChat_SendLocalChat(Entity *pEnt, const char *pchText);
void ClientChat_SendOfficerChat(Entity *pEnt, const char *pchText);
void ClientChat_SendTeamChat(Entity *pEnt, const char *pchText);
void ClientChat_SendTellChat(CmdContext *pCmdContext, Entity *pEnt, const char *pchText);
void ClientChat_SendReplyChat(CmdContext *pCmdContext, Entity *pEnt, const char *pchText);
void ClientChat_SendReplyLastChat(CmdContext *pCmdContext, Entity *pEnt, const char *pchText);
void ClientChat_SendZoneChat(Entity *pEnt, const char *pchText);
void ClientChat_SendTellChatErrorFunc(CmdContext *pCmdContext);

// Tries to convert a command into a subscribed channel and send a message to it
// Returns false if the channel is not subscribed/DNE or if there was no message
bool ClientChat_AttemptChannelMessage(const char *cmdString, char **estrCmdName);

// Channels
void ClientChat_AddChannel(const char *channel_name, bool bSetAsCurrentChannel);
void ClientChat_ChannelDescription(const char *pchChannel, const char *pchDescription);
void ClientChat_FillInputChannelInput(ChatConfigInputChannel *pInputChannel, const char *pchSystemName, bool bForceSubscribed);
void ClientChat_FillChannelSummary(SA_PARAM_NN_VALID UIGen *pGen);
void ClientChat_FillAdminChannelMembers(UIGen *pGen);
ChatChannelInfo *ClientChat_GetAdminChannelDetail(void);
SA_RET_OP_VALID ChatChannelInfo *ClientChat_GetJoinChannelDetail(void);
const char *ClientChat_GetCurrentChannel(void);
U32 ClientChat_GetCurrentChannelColor(void);
const char *ClientChat_GetCurrentChannelColorHex(void);
const char *ClientChat_GetCurrentChannelSystemName(void);
const char *ClientChat_GetSubscribedChannelSystemName(const char *pchChannel);
bool ClientChat_IsChannelJoinable(const char *pchChannel);
bool ClientChat_IsChannelLeavable(const char *pchChannel);
bool ClientChat_IsAdminChannelMember(const char *pchMember);
bool ClientChat_IsChannelOperable(const char *pchChannel);
bool ClientChat_IsCurrentlySubscribedChannel(const char *pchChannel);
bool ClientChat_IsReservedChannel(const char *pchChannel);
void ClientChat_JoinChannel(const char *channel_name);
void ClientChat_JoinOrCreateChannel(const char *pchChannel);
void ClientChat_LeaveChannel(const char *channel_name);
void ClientChat_ChannelMotd(const char *pchChannel, const char *pchMotd);
void ClientChat_RemoveChannel(const char *channel_name);
void ClientChat_SetCurrentChannel(S32 channelIdx);
void ClientChat_SetCurrentChannelByName(const char *pchName);
const char ***ClientChat_GetSubscribedCustomChannels(void);
void ClientChat_ReceiveJoinChannelInfo(ChatChannelInfo *pInfo);
void ClientChat_RefreshChannelSummary(bool bSubscribed, bool bInvited, bool bReserved);
void ClientChat_RefreshJoinChannelDetail(const char *pchChannel);
void ClientChat_RefreshAdminChannelDetail(const char *pchChannel);
void ClientChat_AddChannelSummary(ChatChannelInfo *pInfo, bool *bRefreshedJoinDetail, bool *bRefreshedAdminDetail);
void ClientChat_GetChannelMembers(const char *pchChannel);

// Channel Member States
bool ClientChat_CanChangeAdminChannelDescription(void);
bool ClientChat_CanChangeAdminChannelMotd(void);
bool ClientChat_CanModifyAdminChannel(void);
bool ClientChat_CanDemoteAdminChannelMember(const char *pchMember);
bool ClientChat_CanKickAdminChannelMember(const char *pchMember);
bool ClientChat_CanInviteAdminChannelMember(const char *pchMember);
bool ClientChat_CanMuteAdminChannelMember(const char *pchMember);
bool ClientChat_CanPromoteAdminChannelMember(const char *pchMember);
bool ClientChat_IsAdminChannelMemberInvited(const char *pchMember);
bool ClientChat_IsAdminChannelMemberMuted(const char *pchMember);
void ClientChat_DemoteAdminChannelMember(const char *pchMember);
void ClientChat_KickAdminChannelMember(const char *pchMember);
void ClientChat_MuteAdminChannelMember(const char *pchMember, bool bValue);
void ClientChat_PromoteAdminChannelMember(const char *pchMember);

// Channel privileges
bool ClientChat_GetChannelMemberPrivilege(const char *pchChannel, const char *pchMember, const char *pchPrivilege);
bool ClientChat_GetChannelAccess(const char* pchChannel, const char *pchAccess);
bool ClientChat_GetChannelPlayerPrivilege(const char *pchChannel, const char *pchPrivilege);
void ClientChat_SetChannelMemberPrivilege(const char *pchChannel, const char *pchMember, const char *pchPrivilege, bool bValue);
void ClientChat_SetChannelAccess(const char* pchChannel, const char *pchAccess, bool bValue);
char *ClientChat_GetAdminChannelPrivilegesTableSMF(void);

// Linking
const char *ClientChat_CreateItemLink(SA_PARAM_OP_VALID Item *pItem);
const char *ClientChat_CreateInventorySlotLink(SA_PARAM_OP_VALID const char *pchInventorySlotKey);
const char *ClientChat_CreatePowerDefLink(S32 iPowerId);
SA_RET_OP_VALID const Item *ClientChat_GetItemFromLink(ChatLink *pLink);
SA_RET_OP_VALID const ItemDef *ClientChat_GetItemDefFromLink(SA_PARAM_OP_VALID ChatLink *pLink);
SA_RET_OP_VALID const PowerDef *ClientChat_GetPowerDefFromLink(SA_PARAM_OP_VALID ChatLink *pLink);
U32 ChatClient_GetChatLogItemLinkColor(SA_PARAM_OP_VALID ChatLink *pRenderingLink, SA_PARAM_OP_VALID Item **ppItemCache);
U32 ChatClient_GetChatLogPlayerHandleLinkColor(SA_PARAM_OP_VALID ChatConfig *pConfig, ChatLogEntryType eType, const char *pchChannel);

// Miscellaneous
bool ClientChat_SetReplyText(const char *pchGen);
void ClientChat_Escape(void);
const char* ClientChat_HistoryDown(const char *current_text, SA_PARAM_OP_VALID const ChatData *pData);
const char* ClientChat_HistoryUp(const char *current_text, SA_PARAM_OP_VALID const ChatData *pData);
const char* ClientChat_GetEncodedCurrentHistoryData(void);
void ClientChat_GetMessageTypeDisplayNameByType(char **ppchDisplayName, ChatLogEntryType kType);
void ClientChat_GetMessageTypeDisplayNameByMessage(char **ppchDisplayName, SA_PARAM_NN_VALID const ChatMessage *msg);
bool ClientChat_IsChatVisible(void);
void ClientChat_SetChatVisible(S32 bVisible);
const char* gclChat_GetNamePart(SA_PARAM_OP_VALID const char *pchNameAndHandle);
const char* gclChat_GetHandlePart(SA_PARAM_OP_VALID const char *pchNameAndHandle);
void ClientChat_DumpChatTypes(void);
void ClientChat_FillChatLog(void);

bool ClientChat_FilterProfanityForPlayer(void);

void ClientChat_ResetSubscribedChannels(bool bBuiltIn);

// Utility
void FillAccessLevel(const char **ppchAccessLevel, ChannelUserLevel eUserLevel);
void AppendStatus(char **ppchStatus, const char *state);
void FillChannelMemberStatus(SA_PARAM_NN_VALID ChatChannelMember *pMember);
void FillChannelMemberStatuses(ChatChannelInfo *pInfo);
void FillChannelStatus(SA_PARAM_NN_VALID ChatChannelInfo *pInfo, bool bShowSubscribed);
int ChannelMemberComparator(const ChatChannelMember **m1, const ChatChannelMember **m2);
int ChannelComparator(const ChatChannelInfo **c1, const ChatChannelInfo **c2);

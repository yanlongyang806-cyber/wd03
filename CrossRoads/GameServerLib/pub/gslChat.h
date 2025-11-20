/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef SERVERCHAT_H_
#define SERVERCHAT_H_

#include "chatCommonStructs.h"

#define GAMESERVER_IS_UGCEDIT (gGSLState.gameServerDescription.baseMapDescription.bUGCEdit)

typedef U32 ContainerID;
typedef enum ChatLogEntryType ChatLogEntryType;
typedef enum ChannelUserLevel ChannelUserLevel;
typedef struct ChatChannelInfo ChatChannelInfo;
typedef struct ChatChannelInfoList ChatChannelInfoList;
typedef struct ChatData ChatData;
typedef struct ChatMessage ChatMessage;
typedef struct ChatUserInfo ChatUserInfo;
typedef struct ChatPlayerStruct ChatPlayerStruct;
typedef struct CmdContext CmdContext;
typedef struct Entity Entity;
typedef struct PlayerFindFilterStruct PlayerFindFilterStruct;

typedef void (*ResolveChatUserCallback)(Entity *pCallingEnt, const ChatUserInfo *pUser, const char *pchNameAndHandle, void *pUserData);

const char *getZoneChannelName(Entity *pEnt);
void ServerChat_MapLoad(void);
void ServerChat_MapUnload(void);

// Channel admin
const char ***ServerChat_GetSubscribedCustomChannels(Entity *pEntity);
#ifndef USE_CHATRELAY
void ServerChat_DestroyChannel(Entity *pEnt, const char *channel_name);
void ServerChat_ChannelList(Entity *pEnt);
void ServerChat_ChannelListMembers(Entity *pEnt, const char *channel_name);
void ServerChat_CreateChannel(Entity *pEnt, const char *channel_name);
#endif
void ServerChat_DestroyZoneChannel(void);
void ServerChat_InitializeZoneChannel(void);
#ifndef USE_CHATRELAY
void ServerChat_InviteChatHandleToChannel(Entity *pEnt, const char *channel_name, char *chatHandle);
void ServerChat_JoinChannel(Entity *pEnt, const char *channel_name, bool bCreate);
void ServerChat_LeaveChannel(Entity *pEnt, const char *channel_name);
void ServerChat_ReceiveUserChannelList (U32 entID, ChatChannelInfoList *pList);
void ServerChat_RequestUserChannelList(Entity *pEnt, U32 uFlags);
void ServerChat_SetChannelAccess(Entity *pEnt, const char *channel_name, char *accessString);
void ServerChat_SetChannelDescription(Entity *pEnt, const char *channel_name, const ACMD_SENTENCE description);
void ServerChat_SetChannelMotd(Entity *pEnt, const char *channel_name, const ACMD_SENTENCE motd);
void ServerChat_SetUserAccess(Entity *pEnt, const char *channel_name, char *targetHandle, U32 uAddFlags, U32 uRemoveFlags);
void ServerChat_ModifyChannelPermissions(Entity *pEnt, const char *channel_name, ChannelUserLevel eLevel, U32 uPermissions);

void ServerChat_RequestFullChannelInfo(Entity *pEnt, ACMD_SENTENCE channel_name);
void ServerChat_ReceiveJoinChannelInfo (U32 entID, ChatChannelInfo *pInfo);
void ServerChat_RequestJoinChannelInfo(Entity *pEnt, ACMD_SENTENCE channel_name);
#endif

// Messaging
void ServerChat_SendMessage(Entity *pFromEnt, SA_PARAM_NN_VALID ChatMessage *pMsg);
void ServerChat_SendEncodedMessage(Entity *pFromEnt, const char *pchEncodedMessage);
void ServerChat_SendEmoteChatMsg(Entity *pFromEnt, const char *pMsg, const ChatData *pData);
// From Chat Server
int ServerChat_SendChatMessage(SA_PARAM_OP_VALID Entity *pToEntity, ChatLogEntryType eType, SA_PARAM_OP_VALID const char *pchText, SA_PARAM_OP_VALID const ChatData *pData);
int ServerChat_SendChannelSystemMessage(ContainerID entID, int eType, const char *channel_name, const char *msg);
#ifndef USE_CHATRELAY
int ServerChat_SendSystemAlert(ContainerID entID, const char *title, const char *text);
int ServerChat_MessageReceive(ContainerID targetID, SA_PARAM_NN_VALID ChatMessage *pMsg);
#endif
void ServerChat_BroadcastMessage(ACMD_SENTENCE msg, S32 eNotifyType);

// To the client
SA_RET_OP_VALID ChatUserInfo *ServerChat_CreateLocalizedUserInfoFromEnt(SA_PARAM_OP_VALID Entity *pEntFrom, SA_PARAM_OP_VALID Entity *pEntTo);

//
// Miscellaneous
//

void ServerChat_ReconnectToChatServer(void);
void MailItemQueueTick(void); // in gslmail_old.c

void gslChat_Tick(void);
// Always returns true if Shard Chat Server is in Local-Only mode
bool GameServer_IsGlobalChatOnline(void);
void GameServer_ReconnectGlobalChat(bool bOnline);

void ServerChat_FindPlayers(Entity *pEnt, PlayerFindFilterStruct *pFilters);
void ServerChat_FindPlayersSimple(Entity *pEnt, bool bSendList, const char *pchFilter);
void ServerChat_FindTeams(Entity *pEnt, PlayerFindFilterStruct *pFilters);
void ServerChat_Login(Entity *player);
#ifndef USE_CHATRELAY
void ServerChat_LoginSucceeded(ContainerID entID);
#endif
void ServerChat_PlayerEnteredMap(Entity *pEnt);
void ServerChat_PlayerLeftMap(Entity *pEnt);
void ServerChat_PlayerUpdate( Entity *pEnt, ChatUserUpdateEnum eForwardToGlobalFriends );
void ServerChat_StatusUpdate(Entity *pEnt);
void ServerChat_SetHidden(Entity *pEnt);
void ServerChat_SetFriendsOnly(Entity *pEnt);
void ServerChat_SetVisible(Entity *pEnt);
void ServerChat_SetLFGMode(Entity *pEnt, TeamMode mode);
void ServerChat_SetLFGDifficultyMode(Entity *pEnt, LFGDifficultyMode eMode);
void ServerChat_SetPlayingStyles(Entity *pEnt, const char *pchPlayStyles, bool bSendUpdate);
void ServerChat_JoinSpecialChannel(Entity *pEnt, const char *channel_name);

void gslAutoAwayCheck(Entity *pEnt);

// Similar to gslAutoAwayCheck(), but specifically to handle entity player containers that have been transferred to
//  the gameserver, but the client has not yet connected.
void gslIgnoredPlayerIdleCheck(Entity *pEnt);

// Voice chat
void ServerChat_LeaveVoiceChat(ContainerID entID, ContainerID leaderID);
void ServerChat_ReceiveVoiceData(ContainerID entID, char *xuid, char *pktData, U32 pktSize);
void ServerChat_RegisterRemoteTalker(ContainerID entID, ContainerID cid, char *xuid, U32 teamID);
void ServerChat_SendRegisterRemoteTalkerRequest(ContainerID entID, ContainerID cid);
void ServerChat_SendVoiceData(Entity *pEnt, ContainerID entTo, char *pktData, U32 pktSize);

// Mail
// see gslmail_old.h

#endif

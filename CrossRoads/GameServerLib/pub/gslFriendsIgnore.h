/***************************************************************************
*     Copyright (c) 2005-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GSL_FRIENDSIGNORE_H_
#define GSL_FRIENDSIGNORE_H_

typedef ChatPlayerStruct ChatPlayerStruct;
typedef ChatPlayerList ChatPlayerList;
typedef ContainerID ContainerID;
typedef Entity Entity;

void gslChat_ForwardFriendsCallback(ContainerID entID, ChatPlayerStruct *chatPlayerStruct, FriendResponseEnum eType, const char *errorString);
void gslChat_ForwardIgnoreCallback(ContainerID entID, ChatPlayerStruct *chatPlayerStruct, IgnoreResponseEnum eType, const char *errorString);
//void gslChat_AddNameStruct(ContainerID entID, ChatPlayerStruct *chatPlayerStruct);
//void gslChat_RemoveNameFromListReturn(ContainerID entID, ContainerID iAccountIDToAdd, char * chatHandle);
//void gslChat_UpdateFriendStruct(ContainerID entID, ChatPlayerStruct *chatPlayerStruct);
void gslChat_AddIgnore(Entity *pEnt, const ACMD_SENTENCE ignoreName);
void gslChat_AddIgnoreSpammer(Entity *pEnt, const ACMD_SENTENCE ignoreName);
void gslChat_AddIgnoreByAccountID(Entity *pEnt, U32 ignoreAccountID);
void gslChat_AddIgnoreByContainerID(Entity *pEnt, U32 ignoreContainerID);
void gslChat_RemoveIgnore(Entity *pEnt, const ACMD_SENTENCE ignoreName);
void gslChat_AddFriend(Entity *pEnt, ACMD_SENTENCE friendHandleOrName);
void gslChat_AddFriendByAccountID(Entity *pEnt, U32 friendAccountID);
void gslChat_AddFriendByContainerID(Entity *pEnt, U32 friendContainerID);
#ifndef USE_CHATRELAY
void gslChat_AddFriendComment(Entity *pEnt, char *friendHandleOrName, ACMD_SENTENCE pcComment);
void gslChat_AddFriendCommentByAccountID(Entity *pEnt, U32 friendAccountID, ACMD_SENTENCE pcComment);
void gslChat_AddFriendCommentByContainerID(Entity *pEnt, U32 friendContainerID, ACMD_SENTENCE pcComment);
#endif
void gslChat_AcceptFriend(Entity *pEnt, ACMD_SENTENCE friendHandleOrName);
void gslChat_AcceptFriendByAccountID(Entity *pEnt, U32 friendAccountID);
void gslChat_AcceptFriendByContainerID(Entity *pEnt, U32 friendContainerID);
void gslChat_RejectFriend(Entity *pEnt, ACMD_SENTENCE friendHandleOrName);
void gslChat_RejectFriendByAccountID(Entity *pEnt, U32 friendAccountID);
void gslChat_RejectFriendByContainerID(Entity *pEnt, U32 friendContainerID);
void gslChat_RemoveFriend(Entity *pEnt, ACMD_SENTENCE friendHandleOrName);
void gslChat_RemoveFriendByAccountID(Entity *pEnt, U32 friendAccountID);
void gslChat_RemoveFriendByContainerID(Entity *pEnt, U32 friendContainerID);
void gslChat_RefreshFriendListReturn(U32 uID, ChatPlayerList *friendList);
void gslChat_RefreshClientIgnoreListReturn(U32 uID, ChatPlayerList *ignoreList);
#ifndef USE_CHATRELAY
void gslChat_ToggleWhitelist(Entity *pEnt, U32 enabled);
void gslChat_ToggleWhitelistTells(Entity *pEnt, U32 enabled);
void gslChat_ToggleWhitelistEmails(Entity *pEnt, U32 enabled);
#endif
void ServerChat_RefreshClientNameLists(Entity *pEnt);

#endif // GSL_FRIENDSIGNORE_H_
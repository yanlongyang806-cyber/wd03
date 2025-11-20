#ifndef _GCL_FRIENDSIGNORE_H_
#define _GCL_FRIENDSIGNORE_H_
#pragma once
GCC_SYSTEM

typedef struct ChatMapList ChatMapList;
typedef struct ChatPlayerList ChatPlayerList;
typedef struct ChatPlayerStruct ChatPlayerStruct;
typedef struct ChatTeamToJoinList ChatTeamToJoinList;
typedef struct ClientChatMap ClientChatMap;
typedef struct ClientTeamMemberStruct ClientTeamMemberStruct;
typedef struct Entity Entity;
typedef struct UIGen UIGen;

typedef enum FriendResponseEnum FriendResponseEnum;
typedef enum IgnoreResponseEnum IgnoreResponseEnum;
typedef enum LFGDifficultyMode LFGDifficultyMode;
typedef enum TeamMode TeamMode;
typedef enum UserStatus UserStatus;

AUTO_STRUCT;
typedef struct ClientPlayerStruct
{
	U32 accountID;				AST(NAME(accountID))
	U32 onlineCharacterID;		AST(NAME(onlineCharacterID))
	U32 uLoginServerID;			AST(NAME(loginServerID))
	char *pchHandle;			AST(NAME(Handle) ESTRING)
	char *pchName;				AST(NAME(Name) ESTRING)
	char *pchLocation;			AST(NAME(Location) ESTRING)
	char *pchMapNameAndInstance;AST(NAME(MapNameAndInstance) ESTRING) // Always just the map name + instance, or blank
	S32 iPlayerLevel;			AST(NAME(PlayerLevel))
	S32 iPlayerTeam;			AST(NAME(PlayerTeam))
	S32 iPlayerRank;			AST(NAME(PlayerRank))
	const char *pchAllegiance;	AST(NAME(PlayerAllegiance) POOL_STRING)
	const char *pchClassName;	AST(NAME(ClassName) POOL_STRING)
	char *pchClassDispName;		AST(NAME(ClassDispName) ESTRING)
	const char *pchPathName;    AST(NAME(PathName) POOL_STRING)
	char *pchGuildName;			AST(NAME(GuildName) ESTRING)
	char *pchComment;			AST(NAME(Comment) ESTRING)
	char *pchStatus;			AST(NAME(Status) ESTRING)
	char *pchActivity;			AST(NAME(Activity) ESTRING)
	TeamMode eLFGMode;			AST(NAME(LFGMode))
	LFGDifficultyMode eLFGDifficultyMode; AST(NAME(LFGDifficultyMode))
	char *pcDifficulty;			AST(NAME(Difficulty))
	int iDifficultyIdx;			AST(NAME(DifficultyIdx))
	char *pchTeamStatusMessage;	AST(ESTRING)

	bool bSameInstance;			AST(NAME(SameInstance))
	UserStatus eOnlineStatus;	AST(NAME(OnlineStatus))
	char *pchPlayingStyles;		AST(NAME(PlayingStyles) ESTRING)
	S32 iTeamMembers;			AST(NAME(TeamMembers))
	ClientTeamMemberStruct **ppExtraMembers; AST(NAME(ExtraMembers))
	bool bIgnored;				AST(NAME(Ignored))
	bool bFriend;				AST(NAME(Friend))
	bool bGuildMate;			AST(NAME(GuildMate))
	bool bBuddy;				AST(NAME(Buddy))		//If this person is your recruit or recruiter
	bool bAfk;
	bool bDnd;
	bool bDifferentGame;
	bool bDifferentVShard;
	bool bUGCShard;
} ClientPlayerStruct;
extern ParseTable parse_ClientPlayerStruct[];
#define TYPE_parse_ClientPlayerStruct ClientPlayerStruct

// Friends
void ClientChat_AcceptAllFriendRequests(void);
void ClientChat_AcceptFriend(SA_PARAM_OP_VALID Entity *pEnt);
void ClientChat_AcceptFriendByAccount(U32 friendAccountID);
void ClientChat_AcceptLastFriendRequest(void);
void ClientChat_AddFriend(SA_PARAM_OP_VALID Entity *pEnt);
void ClientChat_AddFriendByHandle(SA_PARAM_NN_VALID const char *chatHandle);
void gclChat_FillFriendList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt);
void gclChat_FillFriendListStructs(ClientPlayerStruct*** peaFriends, Entity* pEnt);
bool ClientChat_FriendAccountRequestPending(U32 accountId);
bool ClientChat_FriendAccountRequestReceived(U32 accountId);
bool ClientChat_FriendRequestPendingByHandle(const char *pchHandle);
bool ClientChat_FriendRequestReceivedByHandle(const char *pchHandle);
bool ClientChat_FriendRequestReceived(void);
int  ClientChat_FriendRequestsReceived(void);
void ClientChat_HandleFriendRequest(void);
bool ClientChat_IsFriend(const char *pchHandle);
bool ClientChat_IsFriendAccount(U32 accountId);
void ClientChat_RejectAllFriendRequests(void);
void ClientChat_RejectFriend(SA_PARAM_OP_VALID Entity *pEnt);
void ClientChat_RejectFriendByAccount(U32 friendAccountID);
void ClientChat_RejectLastFriendRequest(void);
char *ClientChat_GetLastFriendRequestName(void);
void ClientChat_RemoveFriend(SA_PARAM_OP_VALID Entity *pEnt);
void ClientChat_RemoveFriendByAccount(U32 friendAccountID);
void ClientChat_ReceiveFriendRequest(ChatPlayerStruct *friendStruct);
ChatPlayerStruct *FindFriendByHandle(SA_PARAM_OP_STR const char *pchHandle);

// Ignores
void ClientChat_AddIgnore(SA_PARAM_OP_VALID Entity *pEnt);
void ClientChat_AddIgnoreByHandle(SA_PARAM_NN_VALID const char *chatHandle);
void gclChat_FillIgnoreList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt);
bool ClientChat_IsIgnored(const char *pchHandle);
bool ClientChat_IsIgnoredAccount(U32 accountId);

// Search
void gclChat_ClearFindCache(SA_PARAM_OP_VALID UIGen *pGen);
void gclChat_FillFoundPlayers(SA_PARAM_NN_VALID UIGen *pGen);
void gclChat_FillMaps(SA_PARAM_NN_VALID UIGen *pGen);
void gclChat_FindPlayers(SA_PARAM_NN_VALID UIGen *pGen,
		const char *pchNameFilter, const char *pchHandleFilter,
		const char *pchGuildFilter, 
		bool bOpen, bool bRequestOnly, bool bClosed);
void gclChat_FindPlayersSimple(SA_PARAM_OP_VALID UIGen *pGen, const char *pchFilter);
void gclChat_FindPlayersSimple_ErrorFunc();
void gclChat_FindTeams(SA_PARAM_OP_VALID UIGen *pGen, bool bFindTeams, bool bFindSolosForTeam);
void gclChat_FillFoundTeams(SA_PARAM_NN_VALID UIGen *pGen);
const char *gclChat_GetDefaultSearchMapName();
const char *gclChat_GetDefaultSearchNeighborhoodName();
void gclChat_RequestActiveMaps(void);
bool gclChat_WhoSentList(void);
bool gclChat_TeamSearchSentList(void);
void gclChat_UpdateFoundPlayers(bool bFromSimple, ChatPlayerList *pList);
void gclChat_UpdateFoundTeams(ChatTeamToJoinList *pList);
void gclChat_UpdateMaps(ChatMapList *pList);
void gclChat_AddMapFilter(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID ClientChatMap *pMap);
void gclChat_RemoveMapFilter(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID ClientChatMap *pMap);
bool gclChat_IsMapFilter(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID ClientChatMap *pMap);
void gclChat_ResetMapFilter(SA_PARAM_NN_VALID UIGen *pGen);
void gclChat_AddLevelRange(SA_PARAM_NN_VALID UIGen *pGen, int iMinLevel, int iMaxLevel);
void gclChat_RemoveLevelRange(SA_PARAM_NN_VALID UIGen *pGen, int iMinLevel, int iMaxLevel);
bool gclChat_IsLevelRangeIncluded(SA_PARAM_NN_VALID UIGen *pGen, int iMinLevel, int iMaxLevel);
void gclChat_ResetLevelRange(SA_PARAM_NN_VALID UIGen *pGen);
int gclChat_GetFoundPlayers(void);
int gclChat_GetMaxFoundPlayers(void);
int gclChat_GetFoundTeams(void);
bool gclChat_IsSearchingPlayingStyle(const char *pchType);
void gclChat_SearchAddPlayingStyle(const char *pchType);
void gclChat_SearchRemovePlayingStyle(const char *pchType);
void gclChat_SearchResetPlayingStyle(void);

// Searches all of the maintained lists, used to update the context menu
ChatPlayerStruct *FindChatPlayerByHandle(SA_PARAM_OP_STR const char *pchHandle);
ChatPlayerStruct *FindChatPlayerByAccountID(U32 accountID);
ChatPlayerStruct *FindChatPlayerByPlayerID(U32 containerID);

#endif //_GCL_FRIENDSIGNORE_H_


#ifndef _CHAT_COMMON_H_
#define _CHAT_COMMON_H_
GCC_SYSTEM

#include "ChatData.h"
#include "GlobalEnums.h"

#define XMPP_CHAT_ID 1 // For the ChatServerID in the PlayerInfo structs
#define LOCAL_CHAT_ID_START 2

AUTO_ENUM;
typedef enum
{
	USERRELATION_NONE = 0,
	USERRELATION_USER_DNE = BIT(1),
	USERRELATION_FRIENDS = BIT(2),
	USERRELATION_IGNORED = BIT(3),
} UserRelation;

AUTO_ENUM;
typedef enum
{
	USERSTATUS_OFFLINE	= 0,
	USERSTATUS_ONLINE	= 1 << 0,
	USERSTATUS_AFK		= 1 << 1,
	USERSTATUS_DND		= 1 << 2, 
	USERSTATUS_DEAF		= 1 << 3, // reserved for CSR
	USERSTATUS_HIDDEN	= 1 << 4, // Hidden from everyone
	USERSTATUS_FRIENDSONLY = 1 << 5, // Hidden from non-friends
	
	USERSTATUS_AUTOAFK  = 1 << 22, // Auto-AFKed; used exclusively on the game (is just an AFK on the Chat Server)
	USERSTATUS_XMPP		= 1 << 23,	//This is an XMPP client.
} UserStatus;

AUTO_ENUM;
typedef enum
{
	USERSTATUSCHANGE_NONE = 0,
	USERSTATUSCHANGE_AFK,
	USERSTATUSCHANGE_DND,
	USERSTATUSCHANGE_BACK,
	USERSTATUSCHANGE_AUTOAFK,
	USERSTATUSCHANGE_REMOVE_AUTOAFK,
} UserStatusChange;

// These same values are used for teams and individuals
AUTO_ENUM;
typedef enum TeamMode
{
	TeamMode_Prompt,		// Team leader hasn't yet chosen a mode, so prompt for a mode. Invalid for users or created teams
	TeamMode_Open,			// A player is looking for a team or a team is looking for players, and invites are automatically accepted
	TeamMode_RequestOnly,	// A player is looking for a team or a team is looking for players, but invites are required	
	TeamMode_Closed,		// Player or Team is not looking for any more
	TeamMode_Count,			EIGNORE
} TeamMode;

// When looking for a group, use the rules for matching difficulty settings
AUTO_ENUM;
typedef enum LFGDifficultyMode
{
	LFGDifficultyMode_Player = 0,	//The difficulty setting must match the player's
	LFGDifficultyMode_Any = 1,		//Match with any difficulty setting
} LFGDifficultyMode;


/******
READ ME FOR ADDING CHAT LOG ENTRY TYPES:
1. Do not insert in the middle or you will break compatibility with other Shards / Global Chat Server
2. Translations for the Type display string need to be added to each game project under 
   data/ui/gens/Windows/Chat/ChatConfig.uigen.ms (exact location may vary depending on project)
3. If the type should appear in the Client UI Filter list, it must be added to data/defs/config/ChatTypeList.def 
   for each game project
4. Update the kChatLogEntryType_Count value immediately after the AUTO_ENUM
******/
AUTO_ENUM;
typedef enum ChatLogEntryType
{
	// Non-filterable entry types
	kChatLogEntryType_Unknown,		// Unknown message type -- Should never be explicitly set
	kChatLogEntryType_Admin,		// Shard-wide announcements from Admins
	kChatLogEntryType_Channel,		// Special - For custom channels
	kChatLogEntryType_ChatSystem,	// DEPRECATED - DO NOT REMOVE OR THIS WILL NOT MATCH OTHER SYSTEMS
	kChatLogEntryType_Error,		// High-priority errors that the user should always see
	kChatLogEntryType_Spy,			// Used for CSRs to track player conversations

	// Non-channel message types
	kChatLogEntryType_CombatSelf,	// For combat pertaining to you & your pets
	kChatLogEntryType_CombatTeam,	// For combat pertaining to your team (that doesn't includes you & your pets)
	kChatLogEntryType_CombatOther,	// For combat involving others
	kChatLogEntryType_Friend,		// For all friend/ignore responses/updates.  Includes online/offline/added/removed types of messages
	kChatLogEntryType_Inventory,	// For item adds/removes to/from inventory, trade, crafting (recipes & messages).
									// Does not include items granted/revoked by missions.  Also does not include rewards.
	kChatLogEntryType_Mission,		// All messages associated with the mission system.  Does not include rewards.
	kChatLogEntryType_NPC,			// All NPC generated chat
	kChatLogEntryType_Reward,		// For all significant rewards in the form of items, xp, skills, leveling, power/costume/lore reveals, etc.
	kChatLogEntryType_RewardMinor,	// For all insignificant rewards in the form of items, xp, skills, leveling, power/costume/lore reveals, etc.
	kChatLogEntryType_System,		// All generic system messages & errors that don't fit anywhere else.

	// Channel/user-chat message types
	kChatLogEntryType_Guild,		// All guild chat.  Includes global guild notifications like member added/removed or 'member did something noteworthy'.
	kChatLogEntryType_Local,		// All local chat.  Includes emotes messages, but not actions.
	kChatLogEntryType_Officer,		// All guild officer chat.  Includes guild administration notifications such as privilege changes, emblem changes, etc.
	kChatLogEntryType_Private,		// All sent & received tells. (includes messages marked Private_Sent).
	kChatLogEntryType_Private_Sent,	// All sent tells. This is special.  Messages marked with this get treated as "Private"
	kChatLogEntryType_Team,			// All team chat.  Includes team specific notifications, such as "Mike joined the team."
	kChatLogEntryType_TeamUp,
	kChatLogEntryType_Zone,			// All zone chat.
	kChatLogEntryType_Match,		// All queue match chat.
	kChatLogEntryType_Global,

	kChatLogEntryType_Minigame,		// For all minigame messages
	kChatLogEntryType_Emote,        // Emote actions
	kChatLogEntryType_Events,       // Events and Contests
	kChatLogEntryType_LootRolls,    // Rolls for loot SPECIFICALLY (not loot awarding)
	kChatLogEntryType_NeighborhoodChange, // Moving
} ChatLogEntryType;
extern StaticDefineInt ChatLogEntryTypeEnum[];
// This MUST be changed whenever you append a value to ChatLogEntryType!
#define kChatLogEntryType_Count (kChatLogEntryType_NeighborhoodChange+1)

AUTO_STRUCT;
typedef struct ChatLogEntryTypeWrapper
{
	ChatLogEntryType eType; AST(REQUIRED STRUCTPARAM)
} ChatLogEntryTypeWrapper;

AUTO_STRUCT;
typedef struct ChatLogEntryTypeList
{
	EARRAY_OF(ChatLogEntryTypeWrapper) eaTypeList; AST(NAME(TypeList))
} ChatLogEntryTypeList;

#define CHATTYPE_ISLOCAL(eType) (eType == kChatLogEntryType_Local || eType == kChatLogEntryType_Emote)

// Version of packed data format in ChatGuildMemberArray.
#define CHATSERVER_CHATGUILDMEMBERARRAY_VERSION 1

// Magic number of packed data format in ChatGuildMemberArray; version-specific.
#define CHATSERVER_CHATGUILDMEMBERARRAY_MAGIC ((int)(0xdecade + CHATSERVER_CHATGUILDMEMBERARRAY_VERSION))

// Permission flags
#define CHATSERVER_CHATGUILDMEMBERARRAY_CHAT		1		// Allowed to join the guild chat
#define CHATSERVER_CHATGUILDMEMBERARRAY_OFFICER		2		// Allowed to join the officer chat

AUTO_ENUM;
typedef enum ChatTabFilterMode
{
	kChatTabFilterMode_Inclusive,
	kChatTabFilterMode_Exclusive,
} ChatTabFilterMode;

typedef U32 ContainerID;
typedef struct ChatData ChatData;
typedef struct ChatLinkInfo ChatLinkInfo;
typedef struct ChatMessage ChatMessage;
typedef struct ChatPlayerStruct ChatPlayerStruct;
typedef struct ChatUserInfo ChatUserInfo;
typedef struct Entity Entity;
typedef struct StashTableImp StashTableImp;
typedef struct StashTableImp* StashTable;

#define LOCAL_CHANNEL_NAME "Local"
#define ZONE_CHANNEL_NAME "Zone"
#define PRIVATE_CHANNEL_NAME "Private"
#define PRIVATE_CHANNEL_SENT_NAME "PrivateSent"
#define ZONE_CHANNEL_PREFIX "ZONE_"
#define TEAM_CHANNEL_PREFIX "TEAMID_"
#define GUILD_CHANNEL_PREFIX "GUILDID_"
#define OFFICER_CHANNEL_PREFIX "G_OFF_ID_"
#define TEAMUP_CHANNEL_PREFIX "TEAMUPID_"
#define VSHARD_PREFIX "VSHARD"
#define SHARD_HELP_CHANNEL_NAME "Help"
//#define SHARD_LIFETIME_CHANNEL_NAME gProjectGameServerConfig.pLifetimeChannelName
#define SHARD_LIFETIME_CHANNEL_NAME "Lifetime"
#define SHARD_GLOBAL_CHANNEL_NAME "Global"
#define QUEUE_CHANNEL_PREFIX "MATCH_"
#define UGCEDIT_CHANNEL_NAME "ZONE_Foundry"

#define CHAT_ZONE_SHORTCUT "Zone"
#define CHAT_TEAM_SHORTCUT "Team"
#define CHAT_GUILD_SHORTCUT "Guild"
#define CHAT_GUILD_OFFICER_SHORTCUT "Officer"
#define CHAT_TEAMUP_SHORTCUT "TeamUp"
#define CHAT_QUEUE_SHORTCUT "Match"

typedef enum 
{
	IgnoreList,
	FriendList,
	FriendRequestedList,
	FriendRequestList,
} ListToAdd;

#define FRIEND_FLAG_NONE 0x0
#define FRIEND_FLAG_PENDINGREQUEST 0x1
#define FRIEND_FLAG_RECEIVEDREQUEST 0x2
#define FRIEND_FLAG_IGNORE 0x04

#define ChatFlagIsFriend(flag) ((flag & (FRIEND_FLAG_PENDINGREQUEST | FRIEND_FLAG_RECEIVEDREQUEST)) == 0)

// Flags for special channels
#define CHANNEL_SPECIAL_NONE			0x00
#define CHANNEL_SPECIAL_TEAM			0x01
#define CHANNEL_SPECIAL_ZONE			0x02
#define CHANNEL_SPECIAL_GUILD			0x04
#define CHANNEL_SPECIAL_OFFICER			0x08
#define CHANNEL_SPECIAL_SHARDGLOBAL		0x10 // Generic flag for shard global special channels
#define CHANNEL_SPECIAL_GLOBAL			0x20 // Generic flag for global special channels
#define CHANNEL_SPECIAL_PVP				0x40
#define CHANNEL_SPECIAL_TEAMUP			0x80

#define CHANNEL_SPECIAL_VOICE			(CHANNEL_SPECIAL_TEAM | CHANNEL_SPECIAL_GUILD | CHANNEL_SPECIAL_TEAMUP)

// User for checking reserved flags
#define IsChannelFlagShardOnly(flags) (flags && (flags & CHANNEL_SPECIAL_GLOBAL) == 0)

#define MAX_CHAT_LENGTH 200
#define MAX_FIND_PLAYER_LIST 100
#define MAX_STATUS_LENGTH 140
#define MAX_CHANNEL_MOTD_LENGTH 1000
#define MAX_CHANNEL_DESCRIPTION_LENGTH 1000

AUTO_ENUM;
typedef enum {
	CHATRETURN_VOIDRETURN = -1, // Function had no return value
	CHATRETURN_NONE = 0,

	CHATRETURN_FWD_NONE,
	CHATRETURN_FWD_SENDER, // default; same as CHATRETURN_NONE and all non-FWD return codes
	CHATRETURN_FWD_ALLLOCAL,
	CHATRETURN_FWD_ALLLOCAL_MINUSENDER,

	CHATRETURN_UNSPECIFIED,
	CHATRETURN_INVALIDNAME,
	CHATRETURN_USER_OFFLINE, // Target user is offline
	CHATRETURN_USER_DNE, // Target user does not exist
	CHATRETURN_USER_PERMISSIONS, // User does not have sufficient permissions
	CHATRETURN_CHANNEL_ALREADYEXISTS,
	CHATRETURN_CHANNEL_RESERVEDPREFIX,
	CHATRETURN_CHANNEL_WATCHINGMAX,
	CHATRETURN_CHANNEL_FULL,
	CHATRETURN_CHANNEL_ALREADYMEMBER,
	CHATRETURN_CHANNEL_NOTMEMBER,
	CHATRETURN_CHANNEL_DNE,
	CHATRETURN_USER_IGNORING,
	CHATRETURN_INVALIDMSG,

	CHATRETURN_UNKNOWN_COMMAND,
	CHATRETURN_PROFANITY_NOT_ALLOWED,
	CHATRETURN_CHANNEL_ALREADYINVITED,

	CHATRETURN_MAILBOX_FULL,

	CHATRETURN_NAME_LENGTH_LONG,
	CHATRETURN_NAME_LENGTH_SHORT,

	CHATRETURN_CHANNEL_MOTD_LENGTH_LONG,
	CHATRETURN_CHANNEL_DESCRIPTION_LENGTH_LONG,

	CHATRETURN_COUNT,
	CHATRETURN_TIMEOUT,
	CHATRETURN_DISCONNECTED,
} ChatServerReturnCodes;

#define CHATRETURN_SUCCESS(val) (CHATRETURN_NONE <= val && val <= CHATRETURN_FWD_ALLLOCAL_MINUSENDER)

AUTO_STRUCT AST_CONTAINER;
typedef struct ChatTabConfig
{
	CONST_STRING_MODIFIABLE pchTitle;		AST(PERSIST ESTRING NAME(Title))
	CONST_STRING_MODIFIABLE pchDefaultChannel; AST(PERSIST ESTRING NAME(DefaultChannel) POOL_STRING_DB)
	const ChatTabFilterMode eChannelFilterMode; AST(PERSIST NAME(ChannelFilterMode))
	const ChatTabFilterMode eMessageTypeFilterMode; AST(PERSIST NAME(MessageTypeFilterMode))
	CONST_STRING_EARRAY eaChannels;  AST(PERSIST NAME(Channel) POOL_STRING_DB)
	CONST_STRING_EARRAY eaMessageTypes; AST(PERSIST NAME(MessageType) POOL_STRING_DB)
} ChatTabConfig;

AUTO_STRUCT AST_CONTAINER;
typedef struct ChatTabGroupConfig
{
	// Name of the gen associated with this chat configuration
	CONST_STRING_MODIFIABLE pchName;						AST(KEY PERSIST ESTRING NAME(Name) POOL_STRING_DB)
	CONST_EARRAY_OF(ChatTabConfig) eaChatTabConfigs;	AST(PERSIST)
	S32 iCurrentTab;					AST(PERSIST NO_TRANSACT NAME(CurrentTab))
	const U32 uiTime;							AST(PERSIST NAME(Time))
} ChatTabGroupConfig;

AUTO_STRUCT AST_CONTAINER;
typedef struct ChatConfigColorDef
{	
	CONST_STRING_MODIFIABLE pchName;	AST(KEY PERSIST ESTRING NAME(Name))
	const U32 iColor;		AST(PERSIST NAME(Color))
} ChatConfigColorDef;

// Minimum time between reset requests
#define CHAT_CONFIG_RESET_TIME (3) // in seconds

// Player-persisted Chat settings and data
AUTO_STRUCT AST_CONTAINER AST_IGNORE(pchAwayMessage) AST_IGNORE(bAutoAway) AST_IGNORE(bAway);
typedef struct ChatConfig
{
	// Note the TOK_USEROPTIONBIT_2 is used to indicate
	// fields that should be checked for formatting changes
	// by UIGenChatLog.  Anything that changes the textual contents
	// or size of a rendered chat log entry should be marked with this flag.  
	// Effects on the visibility/filtering/color of 
	// chat log entries should not be considered for setting this flag.
	// TODO: We should be able to not consider size (font change),
	//       but that didn't quite work and I don't have time to make
	//       it otherwise at this time.

	// Global chat config
	CONST_EARRAY_OF(ChatConfigColorDef) eaMessageTypeColorDefs; AST(PERSIST)
	CONST_EARRAY_OF(ChatConfigColorDef) eaChannelColorDefs; AST(PERSIST)

	// Chat window-specific configs
	CONST_EARRAY_OF(ChatTabGroupConfig) eaChatTabGroupConfigs; AST(PERSIST)

	STRING_MODIFIABLE pchCurrentTabGroup;			AST(PERSIST NO_TRANSACT POOL_STRING_DB)
	STRING_MODIFIABLE pchCurrentInputChannel;		AST(PERSIST NO_TRANSACT POOL_STRING_DB)
	
	// The status message is persistent because it's the long-term status such as "looking for raid"
	CONST_STRING_MODIFIABLE pchStatusMessage;				AST(PERSIST) 

	// Chat config version number. Changing this will cause the UI to reformat
	// all chat lines. It's necessary when e.g. bShowDate changes.
	const S32	iVersion;						AST(PERSIST)

	const F32 fFontScale;						AST(PERSIST DEFAULT(1))
	const F32 fActiveWindowAlpha;				AST(PERSIST DEFAULT(0.9))
	const F32 fInactiveWindowAlpha;			AST(PERSIST DEFAULT(0.6))

	// Formatting
	const bool bShowDate : 1;					AST(PERSIST)
	const bool bShowTime : 1;					AST(PERSIST)
	const bool bShowMessageTypeNames : 1;		AST(PERSIST)
	const bool bHideAccountNames : 1;			AST(PERSIST)
	const bool bProfanityFilter : 1;			AST(PERSIST)

	const bool bChatHidden : 1;				AST(PERSIST)
	const bool bGlobalChannelSubscribed : 1;  AST(PERSIST)

	// Show auto-complete annotation
	const bool bAnnotateAutoComplete : 1;		AST(PERSIST)

	const bool bTextFadesWithWindow : 1;		AST(PERSIST)
	
	const bool bShowChannelNames : 1;			AST(PERSIST)

	// The chatserver status for this character
	const U32 status;							AST(PERSIST)

	// Defaults for fade away animation for chat text
	const U16 siTimeRequiredToStartFading; AST(PERSIST) // In milliseconds
	const U16 siFadeAwayDuration; AST(PERSIST) // In milliseconds	

	// List of shard-global channels user is a member of
	CONST_STRING_EARRAY ppShardGlobalChannels; AST(PERSIST ESTRING)


	AST_STOP
	U32					bForceDrawAllEntries : 1;	

} ChatConfig;

AUTO_STRUCT;
typedef struct ChatConfigDefaults {
	// Tab defaults
	char *pchDefaultTabGroupName; AST(NAME(DefaultTabGroupName))
	ChatTabConfig **eaDefaultTabConfigs; AST(NAME(DefaultTabConfig))

	ChatTabGroupConfig **eaDefaultTabGroups; AST(NAME(DefaultTabGroups))

	// Default message coloring
	U32 iDefaultChatColor; AST(NAME(DefaultChatColor)) // Overall default
	U32 iUnsubscribedInputChannelColor; AST(NAME(UnsubscribedInputChannelColor))
	ChatConfigColorDef **eaMessageTypeColorDefs; AST(NAME(MessageTypeColorDef))
	ChatConfigColorDef **eaChannelColorDefs; AST(NAME(ChannelColorDef))

	// Formatting defaults
	bool bShowDate : 1;
	bool bShowTime : 1;
	bool bShowMessageTypeNames : 1;
	bool bHideAccountNames : 1;
	bool bProfanityFilter : 1;
	bool bTextFadesWithWindow : 1;

	// Indicates whether the private chat window system is enabled
	bool bEnablePrivateChatWindowSystem : 1;

	bool bShowChannelNames : 1;
	bool bAnnotateAutoComplete : 1;


	// Defaults for fade away animation for chat text
	U16 siTimeRequiredToStartFading; AST(NAME(TimeRequiredToStartFading)) // In milliseconds
	U16 siFadeAwayDuration; AST(NAME(FadeAwayDuration)) // In milliseconds	

	// Defaults for window visibility and font scaling
	F32 fFontScale;						AST(NAME(FontScale) DEFAULT(1))
	F32 fActiveWindowAlpha;				AST(NAME(ActiveWindowAlpha) DEFAULT(0.9))
	F32 fInactiveWindowAlpha;			AST(NAME(InactiveWindowAlpha) DEFAULT(0.6))
	F32 fPrivateSentColorShift;			AST(NAME(PrivateSentColorShift) DEFAULT(-0.4))
} ChatConfigDefaults;

AUTO_STRUCT;
typedef struct ChatCommonBuiltInMessageTypeInfo {
	ChatLogEntryType kType;
	const char *pchTypeName;
	const char *pchDisplayNameKey;
	const char *pchChannelPrefix;
} ChatCommonBuiltInMessageTypeInfo;

typedef enum DefaultChatConfigSource
{
	CHAT_CONFIG_SOURCE_NONE,
	CHAT_CONFIG_SOURCE_PC,
	CHAT_CONFIG_SOURCE_XBOX,
	CHAT_CONFIG_SOURCE_PC_ACCOUNT,
} DefaultChatConfigSource;

// Utility functions
typedef struct NOCONST(ChatTabGroupConfig) NOCONST(ChatTabGroupConfig);
typedef struct NOCONST(ChatTabConfig) NOCONST(ChatTabConfig);

DefaultChatConfigSource ChatCommon_GetChatConfigSourceForEntity(SA_PARAM_OP_VALID Entity *pEntity);
SA_ORET_NN_VALID extern ChatConfigDefaults *ChatCommon_GetConfigDefaults(DefaultChatConfigSource eSource);
SA_RET_OP_VALID extern ChatConfig		   *ChatCommon_GetChatConfig(SA_PARAM_OP_VALID Entity *pEntity);
SA_RET_OP_VALID extern ChatTabGroupConfig *ChatCommon_GetTabGroupConfig(SA_PARAM_OP_VALID const ChatConfig *pConfig, SA_PARAM_OP_STR const char *pchTabGroup);
SA_RET_OP_VALID extern ChatTabConfig      *ChatCommon_GetTabConfig(SA_PARAM_OP_VALID const ChatConfig *pConfig, SA_PARAM_OP_STR const char *pchTabGroup, S32 iTab);
SA_RET_OP_VALID extern ChatTabGroupConfig *ChatCommon_GetCurrentTabGroupConfig(SA_PARAM_OP_VALID ChatConfig *pConfig, DefaultChatConfigSource eDefaultConfigSource);
SA_RET_OP_VALID extern ChatTabConfig      *ChatCommon_GetCurrentTabConfig(SA_PARAM_OP_VALID ChatConfig *pConfig, DefaultChatConfigSource eDefaultConfigSource);
extern S32                              ChatCommon_FindTabConfigIndex(SA_PARAM_OP_VALID ChatTabGroupConfig *pTabGroup, SA_PARAM_OP_STR const char *pchTab);
extern S32 ChatCommon_AddTabConfig(SA_PARAM_OP_VALID NOCONST(ChatTabGroupConfig) *pTabGroup, SA_PARAM_OP_VALID NOCONST(ChatTabConfig) *pTab);
extern bool ChatCommon_InsertTabConfig(SA_PARAM_OP_VALID NOCONST(ChatTabGroupConfig) *pTabGroup, const S32 iIndex,
	SA_PARAM_OP_VALID NOCONST(ChatTabConfig) *pTab);
SA_RET_OP_VALID extern NOCONST(ChatTabConfig) *ChatCommon_RemoveTabConfig(SA_PARAM_OP_VALID NOCONST(ChatTabGroupConfig) *pTabGroup, const S32 iIndex);

extern const char *ChatCommon_GetMessageTypeName(ChatLogEntryType kType);
extern void		   ChatCommon_GetMessageTypeDisplayNameKey(char **pppchDisplayName, const char *pchMessageTypeName);
extern const char *ChatCommon_GetMessageTypeDisplayNameKeyByType(ChatLogEntryType kType);


extern ChatLogEntryType **ChatCommon_GetFilterableMessageTypes(void);
extern ChatLogEntryType **ChatCommon_GetBuiltInChannelMessageTypes(void);
extern ChatLogEntryType    ChatCommon_GetChannelMessageType(const char *pchChannel);

extern U32 ChatCommon_GetChatColor(ChatConfig *pConfig, ChatLogEntryType kType, const char *pchChannel, DefaultChatConfigSource eDefaultConfigSource);
extern U32 ChatCommon_GetUnsubscribedChatColor(DefaultChatConfigSource eDefaultConfigSource);
extern void ChatCommon_SetVectorFromColor(float* vColor, U32 iColor);
extern U32 ChatCommon_GetColorFromVector(float *vColor);

extern bool ChatCommon_IsBuiltInChannel(const char *pChannel);
extern bool ChatCommon_IsBuiltInChannelMessageType(ChatLogEntryType kType);
extern bool ChatCommon_IsLogEntryVisibleInTab(ChatTabConfig *pTabConfig, ChatLogEntryType eType, const char *pchChannel);
extern bool ChatCommon_IsLogEntryVisible(ChatConfig *pConfig, const char *pchTabGroup, S32 iTab, ChatLogEntryType kType, const char *pchChannel);
extern bool ChatCommon_IsFilterableMessageType(ChatLogEntryType kType);

ChatPlayerStruct *ChatCommon_FindPlayerInListByAccount(ChatPlayerStruct ***peaList, U32 accountID);
ChatPlayerStruct* ChatCommon_FindIgnoreByAccount(Entity* pEnt, U32 accountID);

char *zone_GetZoneChannelNameFromMapName(char *pcBuffer, int iBufferSize, SA_PARAM_NN_STR const char *mapShortName, int mapInstanceIndex, ZoneMapType eMapType, int iVirtualShardID);
char *team_MakeTeamChannelNameFromID(char *pcBuffer, int iBufferSize, int iTeamID);
char *teamUp_MakeTeamChannelNameFromID(char *Buffer, int Buffer_size, int iPartitionIdx, int iTeamID, int iServerID, const char *pchMapName);

char *guild_GetGuildChannelNameFromID(char *pcBuffer, int iBufferSize, int iGuildID, int iVirtualShardID);
char *guild_GetOfficerChannelNameFromID(char *pcBuffer, int iBufferSize, int iGuildID, int iVirtualShardID);

SA_RET_OP_VALID extern const char *ChatCommon_GetHandleFromNameOrHandle(SA_PARAM_OP_STR const char *pchNameOrHandle);
SA_RET_OP_VALID extern ChatUserInfo *ChatCommon_CreateUserInfoFromNameOrHandle(SA_PARAM_OP_STR const char *pchNameOrHandle);
SA_RET_OP_VALID extern ChatUserInfo *ChatCommon_CreateUserInfo(ContainerID accountID, ContainerID playerId, SA_PARAM_OP_STR const char *pchHandle, SA_PARAM_OP_STR const char *pchName);

SA_RET_OP_VALID extern ChatMessage *ChatCommon_CreateMsg(
	SA_PARAM_OP_VALID const ChatUserInfo *pFrom, SA_PARAM_OP_VALID const ChatUserInfo *pTo, 
	ChatLogEntryType eType, SA_PARAM_OP_STR const char *pchChannel, 
	SA_PARAM_OP_STR const char *pchText, SA_PARAM_OP_VALID const ChatData *pData);

extern bool ChatCommon_EncodeMessage(SA_PARAM_OP_VALID char **ppchEncodedMsg, SA_PARAM_OP_VALID const ChatMessage *pMsg);
extern bool ChatCommon_DecodeMessage(SA_PARAM_OP_VALID const char *pchEncodedMsg, SA_PARAM_OP_VALID ChatMessage *pMsg);

AUTO_ENUM;
typedef enum PlayerInfoPriority
{
	PINFO_PRIORITY_XMPP = 0, // XMPP is lowest priority for the game
	PINFO_PRIORITY_OTHER, // In a different physical game shard
	PINFO_PRIORITY_PSHARD, // Physical shard is the same
	PINFO_PRIORITY_VSHARD, // Physical AND virtual shard is the same, or neither is on a virtual shard
} PlayerInfoPriority;

typedef struct PlayerInfoStruct PlayerInfoStruct;
PlayerInfoPriority PlayerInfo_GetDisplayPriority(PlayerInfoStruct *pInfo, U32 uServerShardID);
bool ChatIsPlayerInfoSameShard(PlayerInfoStruct *pInfo);
bool ChatArePlayerInfoSameShard(PlayerInfoStruct *pInfo, PlayerInfoStruct *pInfo2);
U32 Game_GetShardHash(void);

const char *ShardChannel_StripVShardPrefix(const char *channel_name);

typedef struct ChatMailStruct ChatMailStruct;
bool ChatMail_CheckItemShard(ChatMailStruct *mail, const char *qualifiedShardName);
char *GetVShardQualifiedName(char *buffer, int buffer_size, ContainerID vshardID);

int ComparePlayerStructs(const ChatPlayerStruct **pps1, const ChatPlayerStruct **pps2);
int ChatPlayer_FindInList(ChatPlayerStruct ***peaList, ChatPlayerStruct *pPlayer);
void ChatPlayer_AddToList(ChatPlayerStruct ***peaList, ChatPlayerStruct *pPlayer, U32 uVShardID, bool bForcePInfoOverwrite);
void ChatPlayer_RemoveFromList(ChatPlayerStruct ***peaList, ChatPlayerStruct *pPlayer);

// General Chat Config functions
typedef struct NOCONST(ChatConfig) NOCONST(ChatConfig);
typedef struct NOCONST(ChatConfigColorDef) NOCONST(ChatConfigColorDef);
typedef enum Language Language;
void ChatConfigCommon_InitToDefault(SA_PARAM_NN_VALID NOCONST(ChatConfig) *pChatConfig, DefaultChatConfigSource eSource, Language eLanguage);

NOCONST(ChatConfigColorDef) *ChatConfigCommon_CreateColorDef(const char *pchName, U32 iColor);
NOCONST(ChatTabConfig) *ChatConfigCommon_CreateDefaultTabConfig(DefaultChatConfigSource eSource, Language eLanguage);
const char **ChatConfigCommon_GetUniqueTabName(ATH_ARG SA_PARAM_NN_VALID NOCONST(ChatConfig) *pConfig, const char *pchName);
void ChatConfigCommon_SetCurrentTab(ChatConfig *pConfig, const char *pchTabGroup, int iTab);
bool ChatConfigCommon_SetCurrentInputChannel(ChatConfig *pConfig, const char *pchTabGroup, S32 iTab, const char *pchChannel);

// AUTO_TRANS_HELPERS
NOCONST(ChatTabConfig) *ChatConfigCommon_GetTabConfig(ATH_ARG NOCONST(ChatConfig) *pChatConfig, const char *pchTabGroup, int iTab);

void ChatConfigCommon_SetChannelColor(ATH_ARG NOCONST(ChatConfig) *pChatConfig, const char *pchChannelName, U32 iColor, U32 iDefaultColor);
void ChatConfigCommon_SetMessageColor(ATH_ARG NOCONST(ChatConfig) *pChatConfig, const char *pchMessageName, U32 iColor, U32 iDefaultColor);
void ChatConfigCommon_ResetTabGroup(ATH_ARG NOCONST(ChatConfig) *pChatConfig, const char *pchTabGroup, Language eLanguage, DefaultChatConfigSource eSource);
void ChatConfigCommon_CreateTabGroup(ATH_ARG NOCONST(ChatConfig) *pChatConfig, const char *pchTabGroup, bool bInitTabs, Language eLanguage, DefaultChatConfigSource eSource);

int ChatConfigCommon_CreateTab(ATH_ARG NOCONST(ChatConfig) *pChatConfig, const char *pchTabGroup, Language eLanguage, DefaultChatConfigSource eSource);
int ChatConfigCommon_CopyTab(ATH_ARG NOCONST(ChatConfig) *pChatConfig, const char *pchDstTabGroup, const char *pchDstName, const char *pchSrcTabGroup, int iSrcTab);
int ChatConfigCommon_DeleteTab(ATH_ARG NOCONST(ChatConfig) *pChatConfig, const char *pchTabGroup, int iTab);
int ChatConfigCommon_MoveTab(ATH_ARG NOCONST(ChatConfig) *pChatConfig, const char *pchFromGroupName, S32 iFromIndex, const char *pchToGroupName, S32 iToIndex);
bool ChatConfigCommon_SetTabName(ATH_ARG NOCONST(ChatConfig)* pConfig, const char *pchTabGroup, int iTab, const char *pchName);

void ChatConfigCommon_UpdateCommonTabs(ATH_ARG NOCONST(ChatConfig) *pConfig, NOCONST(ChatTabConfig) *pTabConfig);

bool ChatConfigCommon_SetMessageTypeFilter(ATH_ARG NOCONST(ChatConfig) *pChatConfig, const char *pchTabGroup, int iTab, const char *pchFilterName, int bFiltered);
bool ChatConfigCommon_SetChannelFilter(ATH_ARG NOCONST(ChatConfig) *pChatConfig, const char *pchTabGroup, int iTab, const char *pchFilterName, int bFiltered);

void ChatConfigCommon_ResetMessageTypeFilters(ATH_ARG NOCONST(ChatTabConfig) *pTabConfig, bool bFiltered);
void ChatConfigCommon_ResetChannelFilters(ATH_ARG NOCONST(ChatTabConfig) *pTabConfig, CONST_STRING_EARRAY *eaSubscribedChannels, bool bFiltered);
bool ChatConfigCommon_SetTabFilterMode(ATH_ARG NOCONST(ChatTabConfig) *pTabConfig, CONST_STRING_EARRAY *eaSubscribedChannels, const char *pchTabGroup, int iTab, int bExclusiveFilter);
void ChatConfigCommon_SetFadeAwayDuration(ATH_ARG NOCONST(ChatConfig) *pChatConfig, float fFadeAwayDuration);
void ChatConfigCommon_SetTimeRequiredToStartFading(ATH_ARG NOCONST(ChatConfig) *pChatConfig, F32 fTimeRequiredToStartFading);
bool ChatCommon_IsLogEntryVisibleInPrivateMessageWindow(SA_PARAM_NN_VALID const ChatMessage *pMsg, SA_PARAM_NN_STR const char * pchExpectedHandle);

typedef enum ChatConfigStatus
{
	CHATCONFIG_STATUS_CHANGED = 0,
	CHATCONFIG_STATUS_UNCHANGED,
	CHATCONFIG_STATUS_PROFANE,
} ChatConfigStatus;
ChatConfigStatus ChatConfigCommon_SetStatusMessage(ATH_ARG NOCONST(ChatConfig) *pChatConfig, const char *pchMessage);
#endif

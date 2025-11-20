#pragma once
GCC_SYSTEM

#include "GlobalTypeEnum.h"

#include "Autogen/chatCommonStructs_h_ast.h"
#include "Message.h"
#include "chatCommon.h"

typedef U32 ContainerID;
typedef struct ChatData ChatData;
typedef struct ChatUser ChatUser;
typedef struct FindFilterTokenStruct FindFilterTokenStruct;
typedef struct MicroTransactionDef MicroTransactionDef;
typedef struct NetLink NetLink;

// Wrapper for earray of ContainerIDs (U32)
AUTO_STRUCT;
typedef struct ChatContainerIDList
{
	U32 *piContainerIDList;
} ChatContainerIDList;

// User Update Request Forward Flags
AUTO_ENUM;
typedef enum ChatUserUpdateEnum
{
	CHATUSER_UPDATE_NONE = 0, // Do not forward to any clients
	CHATUSER_UPDATE_SHARD, // Forward only to clients online on the same shard
	CHATUSER_UPDATE_GLOBAL, // Forward to clients on all shards
} ChatUserUpdateEnum;

// Channel Permission Bit Flags
AUTO_ENUM;
typedef enum ChannelUserPrivileges
{
	                                  // Defaulted Privilege For:
	CHANPERM_JOIN          = 1 << 0,  // User
	CHANPERM_SEND          = 1 << 1,  // User
	CHANPERM_RECEIVE       = 1 << 2,  // User
	CHANPERM_INVITE        = 1 << 3,  // Operator
	CHANPERM_KICK          = 1 << 4,  // Operator
	CHANPERM_MUTE          = 1 << 5,  // Operator (and unmute)
	CHANPERM_MOTD          = 1 << 6,  // Admin
	CHANPERM_DESCRIPTION   = 1 << 7,  // Admin
	CHANPERM_MODIFYCHANNEL = 1 << 8,  // Admin
	CHANPERM_PROMOTE       = 1 << 9,  // Admin
	CHANPERM_DEMOTE        = 1 << 10, // Admin
	CHANPERM_DESTROY       = 1 << 11, // Owner
} ChannelUserPrivileges;

#define CHANPERM_DEFAULTUSER (CHANPERM_JOIN | CHANPERM_SEND | CHANPERM_RECEIVE)
#define CHANPERM_DEFAULTOP (CHANPERM_DEFAULTUSER | CHANPERM_INVITE | CHANPERM_KICK | CHANPERM_MUTE)
#define CHANPERM_DEFAULTADMIN (CHANPERM_DEFAULTOP | CHANPERM_MODIFYCHANNEL | CHANPERM_MOTD | CHANPERM_DESCRIPTION | CHANPERM_PROMOTE | CHANPERM_DEMOTE)
#define CHANPERM_OWNER 0xFFFF // all bits flipped!

#define MAIL_ITEM_MAX 5

AUTO_ENUM;
typedef enum ChannelUserLevel
{
	CHANUSER_USER = 0,
	CHANUSER_OPERATOR,
	CHANUSER_ADMIN,
	CHANUSER_OWNER,

	CHANUSER_COUNT, EIGNORE
	CHANUSER_GM = CHANUSER_COUNT// This is a special flag passed on to the Game Client and not persisted in the DB
} ChannelUserLevel;

AUTO_ENUM;
typedef enum
{
	CHATACCESS_NONE		= 0,
	CHATACCESS_JOIN		= 1 << 0, 
	CHATACCESS_SEND		= 1 << 1, 
	CHATACCESS_OPERATOR	= 1 << 2, // channels automatically makes new members operators
	//CHATACCESS_ADMIN	= 1 << 3, // NOT USED
	CHATACCESS_RESERVED	= 1 << 4, // channel is a reserved channel (eg. guild, zone)
} ChatAccess;

AUTO_ENUM;
typedef enum 
{
	USER_CHANNEL_SUBSCRIBED = 0x1, // subscribed global channels
	USER_CHANNEL_INVITED    = 0x2, // invited channels
	USER_CHANNEL_RESERVED   = 0x4, // subscribed reserved channels
} ChatChannelCategory;

AUTO_ENUM;
typedef enum EMailType
{
	EMAIL_TYPE_PLAYER,							// player email is the default
	EMAIL_TYPE_NPC_NO_REPLY,					// NPC sent mail (actually from self) with no replay allowed
	EMAIL_TYPE_NPC_FROM_PLAYER,					// NPC sent mail that is stored in the player email struct

} EMailType;

AUTO_ENUM;
typedef enum ChatGamePermissionInfoEnum
{
	CHAT_GAME_PERMISSION_INFO_NONE = 0,			// Normal email and chat rates
	CHAT_GAME_PERMISSION_INFO_RESTRICTED,		// This user is online with a character with lower chat and email rates
	
}ChatGamePermissionInfoEnum;

AUTO_STRUCT AST_CONTAINER;
typedef struct ChatUserInfo {
	// Look right for mappings --->                   // Game Server Store Location       // Chat Server Store Location
	//                                                // -------------------------------- // -------------------------------------------
	ContainerID accountID; AST(PERSIST NO_TRANSACT)   // pEnt->pPlayer->accountID         // pChatUser->pPlayerInfo->onlineCharacterID
	ContainerID playerID; AST(PERSIST NO_TRANSACT)    // pEnt->myContainerId              // pChatUser->ID
	U32 nonPlayerEntityRef; AST(PERSIST NO_TRANSACT)			  // (only used for 'fake' entities, such as NPC's and contacts)
	char *pchName; AST(PERSIST NO_TRANSACT ESTRING)   // entGetLocalName(pEnt)            // pChatUser->pPlayerInfo->onlineCharacterName
	char *pchHandle; AST(PERSIST NO_TRANSACT ESTRING) // pEnt->pPlayer->publicAccountName // pChatUser->handle
	//                                                //   OR privateAccountName if empty //
	bool bIsGM; AST(PERSIST NO_TRANSACT)
	bool bIsDev; AST(PERSIST NO_TRANSACT)
} ChatUserInfo;

AUTO_STRUCT;
typedef struct ChatMessage {
	// Header (routing)
	ChatUserInfo *pFrom;
	ChatUserInfo *pTo;
	ChatLogEntryType eType;
	char *pchChannel; AST(ESTRING)
	char *pchChannelDisplay; AST(ESTRING)
	int iInstanceIndex; // Instance index the sender is on; Used for Zone chat to append the index
	U32 uVirtualShardID;

	// Subject
	char *pchSubject; AST(FORMATSTRING(XML_ENCODE_BASE64=1) ESTRING NAME(subject))

	// Body
	char *pchText; AST(FORMATSTRING(XML_ENCODE_BASE64=1) ESTRING)

	// Thread Identifier
	char *pchThread; AST(ESTRING NAME(thread))

	// Message Identifier
	char *pchId; AST(ESTRING NAME(id))

	//char *pchEncodedChatData; AST(ESTRING)
	ChatData *pData;

	int xmppType;
	bool bBlacklistViolation; // set by ChatRelay
} ChatMessage;

AUTO_STRUCT;
typedef struct StringKeyStruct
{
	char *pKey;			AST(KEY ESTRING)
} StringKeyStruct;

//Simple guild struct updated by the guild server as they are created and renamed
AUTO_STRUCT;
typedef struct ChatGuild
{
	U32 iGuildID;				AST(KEY)			// Shard-specific guild identifier
	U32 iLinkID;				NO_AST				// Shard identifier
	const char *pchName;		AST(POOL_STRING)	// Guild name
	U32 *pGuildMembers;								// Guild member chat user IDs.
} ChatGuild;

AUTO_STRUCT;
typedef struct ChatGuildMember
{
	U32 uAccountID;
	U32 uCharacterID;
	bool bCanChat : 1;
	bool bIsOfficer : 1;
} ChatGuildMember;

AUTO_STRUCT;
typedef struct ChatGuildList
{
	ChatGuild **ppGuildList;
} ChatGuildList;

// Packed array of membership information of all guilds.
AUTO_STRUCT;
typedef struct ChatGuildMemberArray
{
	INT_EARRAY pMemberData;
} ChatGuildMemberArray;

AUTO_STRUCT;
typedef struct ChatGuildRankData
{
	ContainerID iGuildID;
	CONTAINERID_EARRAY eaiMembers;
	bool bCanGuildChat;
	bool bIsOfficer;
} ChatGuildRankData;

//A simplified struct of the map that player is on used in the chat system
AUTO_STRUCT;
typedef struct ChatMap
{
	const char *pchMapName;					AST(POOL_STRING)
	const char *pchMapNameMsgKey;			AST(POOL_STRING)
	const char *pchMapVars;					// Not pooled
	const char *pchNeighborhoodNameMsgKey;	AST(POOL_STRING)
	S32 iMapInstance;
	ZoneMapType eMapType;

	// Concatenation of pchMapName and pchNeighborhoodNameMsgKey, used for indexing in cache
	char *pKey; AST(KEY ESTRING)
	U32 uNumPlayers; AST(NO_NETSEND)
} ChatMap;

AUTO_STRUCT;
typedef struct ChatMapList
{
	ChatMap **ppMapList;
} ChatMapList;

AUTO_STRUCT;
typedef struct FindFilterTokenStruct
{
	char *pchToken;					AST(ESTRING)
	S32 iRangeLow;
	S32 iRangeHigh;
	FindFilterTokenStruct **eaAcceptableTokens;
	bool bFlag : 1;					AST(NAME(flag))

	bool bCheckName : 1;
	bool bCheckHandle : 1;
	bool bCheckMap : 1;
	bool bCheckNeighborhood : 1;
	bool bCheckInstance : 1;
	bool bCheckGuild : 1;
	bool bCheckStatus : 1;

	bool bCheckRank : 1;

	bool bCheckOpen : 1;
	bool bCheckRequestOnly : 1;
	bool bCheckClosed : 1;

	bool bCheckPlayingStyles : 1;

	bool bExact : 1;
	bool bSoft : 1;
} FindFilterTokenStruct;

AUTO_STRUCT;
typedef struct PlayerStatusChange
{
	U32 uAccountID;
	U32 uChatServerID;

	UserStatus eStatus;
	char *pActivity;
	char *pStatusMsg;

	INT_EARRAY eaiAccountIDs; // who to send this to
} PlayerStatusChange;

AUTO_STRUCT;
typedef struct PlayerFindFilterStruct
{
	FindFilterTokenStruct **eaExcludeFilters;
	FindFilterTokenStruct **eaRequiredFilters;
	const char *pchFilterString;
	Language eLanguage;				AST(DEFAULT(LANGUAGE_DEFAULT))
	bool bFindAnonymous : 1;
	bool bFindTeams : 1;
	bool bFindSoloForTeam : 1;
	S32 iMaxAccessLevel;
	U32 searchAccountID;

	// Used during search process, searcher responsible for cleaning up
	ChatUser *pCachedSearcher; NO_AST
	ChatUser **ppPossibleTeammates; NO_AST
} PlayerFindFilterStruct;

AUTO_STRUCT;
typedef struct PlayerInfoStruct
{
	U32 onlineCharacterID;
	S32 onlineCharacterAccessLevel;
	char *onlinePlayerName;
	const char *onlinePlayerAllegiance;	AST(POOL_STRING)
	char *gamePublicNameKey; AST(POOL_STRING)
	ChatMap playerMap;				AST(EMBEDDED_FLAT)
	char *pLocationMessageKey; // Display message for non-map locaation information (eg. Login status)

	S32	iPlayerLevel;
	S32 iPlayerRank; // Currently used specifically for officer ranks
	const char *pchClassName;			AST(NAME(ClassName) POOL_STRING) // Character Class
	const char *pchPathName;			AST(NAME(PathName) POOL_STRING) // Character Path
	const char *playingStyles;			AST(NAME(PlayingStyles) POOL_STRING)
	char *playerActivity;

	// Voice data
	U32 voiceId;
	const char* voiceURI;

	TeamMode eLFGMode; // Mode of individual player
	LFGDifficultyMode eLFGDifficultyMode;

	U32 uLoginServerID;
	S32 iPlayerTeam;
	char *pchTeamStatusMessage;		AST(ESTRING)
	TeamMode eTeamMode; // Mode of team as whole if player is leader, or Unknown if otherwise
	int iDifficulty;

	S32 iPlayerGuild;
	bool bCanGuildChat; AST(DEFAULT(1)) // Has permission for SENDING to regular chat (everyone can see it)
	bool bIsOfficer; // Has permission for officer chat

	Language eLanguage;
	bool bIsGM;
	bool bIsDev;
	bool bIsAutoAFK;

	// These are deprecated and will be ignored by newer searches
	bool bLookingForGroup;
	bool bAnonymous;
	U32 iTeamInviteResponse;

	// Shard identifiers
	U32 uShardHash; // Hash of the shard name + product for lookups outside of GCS
	U32 uChatServerID; // For use by global chat server only
	U32 uVirtualShardID;
	const char *shardName; AST(POOL_STRING)
	
	ChatGamePermissionInfoEnum eGamePermissionInfo;
	bool bSocialRestricted;
	
	// Purchase Info
	bool bGoldSubscriber;
	U32 uLastPurchase;
} PlayerInfoStruct;

AUTO_STRUCT;
typedef struct PlayerExtraInfo
{
	char *pAccountName; AST(ESTRING)
	char *pDisplayName; AST(ESTRING)
	U32 uAccessLevel;
	char *pStatus; AST(ESTRING)
} PlayerExtraInfo;

AUTO_STRUCT;
typedef struct PlayerInfoList
{
	U32 *piAccountIDs;
	PlayerExtraInfo **ppPlayerNames;
	PlayerInfoStruct ** ppPlayerInfos;
} PlayerInfoList;

AUTO_STRUCT;
typedef struct ChatPlayerStruct {
	U32 accountID;
	char *chatHandle;

	PlayerInfoStruct pPlayerInfo; AST(EMBEDDED_FLAT)
	// For other games; Currently unused except for sending logout-from-shard info
	PlayerInfoStruct **ppExtraPlayerInfo; 
	const char *pchGuildName;
	UserStatus online_status;
	char *comment;  AST(ESTRING)
	char *status;  AST(ESTRING)
	STRING_EARRAY eaCharacterShards; AST(POOL_STRING)

	int flags;
} ChatPlayerStruct;

AUTO_STRUCT;
typedef struct ChatPlayerList{
	ChatPlayerStruct ** chatAccounts; AST(NO_INDEX)
} ChatPlayerList;

AUTO_STRUCT;
typedef struct ChatUserMinimal 
{
	U32 id; AST(KEY)
	char *handle;
} ChatUserMinimal;

AUTO_STRUCT;
typedef struct ChatUserMinimalNameIndex 
{
	U32 id;
	char *handle; AST(KEY)
} ChatUserMinimalNameIndex;

AUTO_STRUCT;
typedef struct ChatUserMinimalList
{
	EARRAY_OF(ChatUserMinimal) ppUsers;
} ChatUserMinimalList;


AUTO_STRUCT;
typedef struct ChatTeamMemberStruct {
	char *chatHandle;
	char *onlinePlayerName;

	S32	iPlayerLevel;
	S32 iPlayerRank; // Currently used specifically for officer ranks
	const char *playingStyles;			AST(NAME(PlayingStyles) POOL_STRING)
} ChatTeamMemberStruct;

AUTO_STRUCT;
typedef struct ChatTeamToJoin{
	ChatUser *leaderUser; NO_AST
	ChatPlayerStruct *pLeader;	
	ChatTeamMemberStruct **ppTeamMembers;
	bool bHasFriend;
	F32 fAverageLevel;
	F32 fTeamRating;
} ChatTeamToJoin;

AUTO_STRUCT;
typedef struct ChatTeamToJoinList{
	ChatTeamToJoin ** chatAccounts;
} ChatTeamToJoinList;

AUTO_STRUCT;
typedef struct ChatChannelMessage
{
	U32   userID;			AST(NAME(UserID)) // Account ID of author
	char *userHandle;		AST(NAME(UserHandle) ESTRING) // Handle of author
	char *body;				AST(NAME(Body) ESTRING)
	U32   uTime;			AST(NAME(Time))
} ChatChannelMessage;

AUTO_STRUCT AST_IGNORE(eUserAccess);
typedef struct ChatChannelMember
{
	U32   uID;					 AST(NAME(ID))
	char *handle;				 AST(NAME(Handle) ESTRING KEY)
	ChannelUserLevel eUserLevel; AST(NAME(UserLevel))
	ChannelUserPrivileges uPermissionFlags; AST(NAME(PermissionFlags))
	bool bOnline;				 AST(NAME(Online))
	bool bInvited;				 AST(NAME(Invited))
	bool bSilenced;				 AST(NAME(Silenced))

	// Client only fields
	char *pchStatus;			 AST(NAME(Status) ESTRING CLIENT_ONLY)
	const char *pchUserLevel;	 AST(NAME(AccessLevel) POOL_STRING CLIENT_ONLY)
} ChatChannelMember;

AUTO_STRUCT AST_IGNORE(eUserAccess) AST_IGNORE(uUserStatus);
typedef struct ChatChannelInfo
{
	char *pName;					AST(NAME(Name) ESTRING KEY)
	char *pDisplayName;				AST(ESTRING) // For special system channels
	ChatAccess eChannelAccess;		AST(NAME(ChannelAccess))
	U32 permissionLevels[CHANUSER_COUNT]; AST(NAME(PermissionLevels))
	U32 uReservedFlags;

	ChatChannelMessage motd;		AST(NAME(motd) STRUCT(parse_ChatChannelMessage))
	char *pDescription;				AST(NAME(Description) ESTRING)

	U32 uMemberCount;				AST(NAME(MemberCount))
	U32 uOnlineMemberCount;			AST(NAME(OnlineMemberCount))
	U32 uInvitedMemberCount;		AST(NAME(InvitedMemberCount))

	// User specific fields
	ChannelUserLevel eUserLevel;    AST(NAME(UserLevel))
	ChannelUserPrivileges uPermissionFlags;	AST(NAME(PermissionFlags))
	bool bUserInvited;				AST(NAME(UserInvited))
	bool bUserSubscribed;			AST(NAME(UserSubscribed))
	bool bSilenced;
	U32 uAccessLevel;

	// Voice fields
	U32 voiceId;
	char* voiceURI;
	U8 voiceEnabled : 1;

	// Channel Members
	// lazy initialized on the client; only sent on a ChatServerGetChannelInfo request
	ChatChannelMember ** ppMembers; AST(NAME(Members))
	bool bMembersInitialized;

	// Localized status display string (client only)
	char *pchStatus;				AST(NAME(status) ESTRING CLIENT_ONLY)
	const char *pchUserLevel;		AST(NAME(AccessLevel) POOL_STRING CLIENT_ONLY)
} ChatChannelInfo;

AUTO_ENUM;
typedef enum ChannelUpdateEnum
{
	CHANNELUPDATE_NONE = 0, // undefined response
	CHANNELUPDATE_UPDATE, // also for adding channels
	CHANNELUPDATE_UPDATE_NO_MEMBERS, // same as the above, but info will NOT contain users
	CHANNELUPDATE_REMOVE,

	// Member changes should also explicitly update all member counts
	CHANNELUPDATE_MEMBER_UPDATE, // also for adding members
	CHANNELUPDATE_MEMBER_REMOVE,

	// Voice info updates
	CHANNELUPDATE_VOICE_ENABLED,

	CHANNELUPDATE_DESCRIPTION, // Channel description
	CHANNELUPDATE_MOTD, // Message of the Day

	CHANNELUPDATE_CHANNEL_PERMISSIONS,
	CHANNELUPDATE_USER_PERMISSIONS,

	CHANNELUPDATE_USER_ONLINE,
	CHANNELUPDATE_USER_OFFLINE, 

	CHANNELUPDATE_COUNT, EIGNORE
} ChannelUpdateEnum;

AUTO_STRUCT;
typedef struct ChatChannelUpdateData
{
	// Single-user update
	U32 uTargetUserID; // this is also used for global-updates to filter out the single-user that 
					   // receives a more comprehensive update

	// Global update flags - if any are set, uTargetUserID is used as a filter
	bool bSendToOperators; // send to all operators and above
	bool bSendToAll; // send update to all subscribed members, includes invitees

	// Update info
	ChatChannelInfo *channelInfo; AST(NAME(Channelinfo))
	ChannelUpdateEnum eUpdateType;
} ChatChannelUpdateData;

#define ChatChannelIsShardReserved(pCInfo) ((pCInfo->eChannelAccess & CHATACCESS_RESERVED) != 0 && \
	(pCInfo->uReservedFlags & CHANNEL_SPECIAL_GLOBAL) == 0)

AUTO_STRUCT;
typedef struct ChatChannelInfoList
{
	ChatChannelInfo **ppChannels;
} ChatChannelInfoList;

// This is a non-persisted copy of data from GCS stored in the Player struct
AUTO_STRUCT;
typedef struct ChatState {
	ChatPlayerStruct **eaFriends; AST(NAME(Friends))
	ChatPlayerStruct **eaIgnores; AST(NAME(Ignores))
	ChatChannelInfo **eaChannels; AST(NAME(Channels) CLIENT_ONLY)
	ChatChannelInfo **eaReservedChannels; AST(NAME(ReservedChannels) CLIENT_ONLY)
} ChatState;


AUTO_STRUCT;
typedef struct ChatMailStruct {
	U32 uID; AST(KEY) // Mail ID (per Account unique)
	U32	sent; // time sent
	U32	fromID; // Sender's Account ID
	char *fromHandle; // Sender's Account Display Name
	const char *shardName; AST(POOL_STRING) // Shard this mail originated from
	
	char *subject;
	char *body;
	bool bRead : 1;

	U32 uLotID; // Lot ID on Auction Server that holds the items

	char *toName;
	char *fromName;					// character name this is from
	EMailType eTypeOfEmail;			// the type of email
	ContainerID toContainerID;		// ID of the player that this was mailed to, used for NPC mail
	S32 iNPCEMailID;				// email id from player 

	void *data; NO_AST

	// Age information
	U32 uTimeLeft; // Seconds left this mail is active
	U32 uTimeReceived; // Set on the client. Used to dynamically update the uTimeLeft
	U32 uFutureSendTime;
} ChatMailStruct;

AUTO_STRUCT;
typedef struct ChatMailList {
	U32 uID; // Account ID this mail belongs to
	ChatMailStruct ** mail;

	// Used for paging
	U32 uTotalMail; // Total mail this account has
	U32 uPage; // Page # (starting with 0) that this mail list represents
	U32 uPageSize; // Max Number of mails per page this was using
} ChatMailList;

// Info of game if user is online
AUTO_STRUCT;
typedef struct OnlineGamesChatCommand // this struct used for returns of chatCommands
{
	char *characterName;
	U32  characterContainerID;
	char *productName;
	char *mapName;
	const char *shardName; AST(POOL_STRING)
} OnlineGamesChatCommand;

// Friend info
AUTO_STRUCT;
typedef struct FriendChatCommand // this struct used for returns of chatCommands
{
	U32 uID;
	char *accountName;
	char *displayName; 
	U32 last_online_time; //AST(FROMATSTRING(XML_ISO8601=1))
	U32 friend_request_send_date; //AST(FROMATSTRING(XML_ISO8601=1))
	U32 online_status;
	char *status_message;
	EARRAY_OF(OnlineGamesChatCommand) eaOnlineGames;
} FriendChatCommand;

// Ignore List
AUTO_STRUCT;
typedef struct IgnoreChatCommand // this struct used for returns of chatCommands
{
	U32 uID;
	char *displayName;
} IgnoreChatCommand;

AUTO_STRUCT;
typedef struct IgnoreListChatCommand  // this struct used for returns of chatCommands
{
	EARRAY_OF(IgnoreChatCommand) allIgnores;
} IgnoreListChatCommand;

// Friend list
AUTO_STRUCT;
typedef struct FriendsListChatCommand  // this struct used for returns of chatCommands
{
	EARRAY_OF(FriendChatCommand) allFriends;
	EARRAY_OF(FriendChatCommand) pendingFriends;
	EARRAY_OF(FriendChatCommand) incomingFriends;
} FriendsListChatCommand;

// List of user with online status
AUTO_STRUCT;
typedef struct UserListChatCommand  // this struct used for returns of chatCommands
{
	EARRAY_OF(FriendChatCommand) allUsers;
} UserListChatCommand;

AUTO_ENUM;
typedef enum FriendResponseEnum
{
	FRIEND_NONE = 0, // undefined response

	// Originator
	FRIEND_REQUEST_SENT,
	FRIEND_REQUEST_ACCEPTED,
	FRIEND_REQUEST_REJECTED,
	FRIEND_ADDED,
	FRIEND_REMOVED,

	// Recipient
	FRIEND_REQUEST_RECEIVED,
	FRIEND_REQUEST_ACCEPT_RECEIVED,
	FRIEND_REQUEST_REJECT_RECEIVED,
	FRIEND_REMOVE_RECEIVED,

	// State changes
	FRIEND_OFFLINE,
	FRIEND_ONLINE,
	FRIEND_UPDATED,

	FRIEND_COMMENT,

	FRIEND_COUNT, EIGNORE
} FriendResponseEnum;

AUTO_ENUM;
typedef enum IgnoreResponseEnum
{
	IGNORE_NONE = 0, // undefined response

	// Originator
	IGNORE_ADDED,
	IGNORE_REMOVED,
	IGNORE_UPDATED,

	IGNORE_COUNT, EIGNORE
} IgnoreResponseEnum;

AUTO_STRUCT;
typedef struct MailItemStruct
{
	EntityRef entRef;
	U32 uLotID;
	ChatMailStruct *mail;
	char *pchItemString;
	// Container IDs used to remove auction/mail items if the items are owned by a pet
	INT_EARRAY auctionPetContainerIDs;
	INT_EARRAY mailPetContainerIDs;

	// Cached list of container IDs stored on kItemDefType_Container type items in the auction lot
	INT_EARRAY containerItemPetIDs;
	
	char * pFullItemString;					AST(ESTRING)
} MailItemStruct;

// Error codes for commands that need to be passed down to Entity's Game Client
AUTO_ENUM;
typedef enum ChatEntityCommandReturnCode
{
	CHATENTITY_SUCCESS = 0,
	CHATENTITY_NOTFOUND,
	CHATENTITY_MAPTRANSFERRING,
	CHATENTITY_OFFLINE,
} ChatEntityCommandReturnCode;

AUTO_ENUM;
typedef enum ChatLogFilterEnum
{
	CHATLOGFILTER_INCLUDE = 0, // default = inclusion
	CHATLOGFILTER_EXCLUDE,
} ChatLogFilterEnum;

AUTO_STRUCT;
typedef struct ChatLogFilter
{
	ChatLogEntryType *pTypeFilter;
	ChatLogFilterEnum eTypeInclusion; // Include or Exclude above types

	ChatUserInfo **ppNameFilter; // Names be of the format "<character name>@<display name>"
	ChatLogFilterEnum eNameInclusion;
	
	U32 uStartTime;
	U32 uEndTime;
} ChatLogFilter;

AUTO_STRUCT;
typedef struct MailedAuctionLots
{
	ContainerID iOwnerAccountID;
	ContainerID iRecipientEntityID;
	const char* pchShardName;
	
	// array of all auction lots that are mailed 
	INT_EARRAY uLotIds;
	
	// Translated Strings
	char *pReturnedSubject;
	char *pReturnedBody;
	char *pReturnedFrom;
}MailedAuctionLots;

AUTO_STRUCT;
typedef struct ChatUserBatch
{
	INT_EARRAY eaiAccountIDs;
} ChatUserBatch;

AUTO_STRUCT;
typedef struct BulletinMessage
{
	Language eLanguage;				AST(NAME(Language) SUBTABLE(LanguageEnum))
		// The language for this message
	char* pchTranslatedTitle;		AST(NAME(Title))
		// Pre-translated title of this message
	char* pchTranslatedString;		AST(NAME(MessageBody))
		// Pre-translated body of this message
} BulletinMessage;

// An event that will take place at a designated time
AUTO_STRUCT;
typedef struct BulletinEvent
{
	U32 uEventTime;					
		// This is set automatically from pchDisplayDate
	char* pchEventDate;				AST(NAME(EventDate))
		// The time that the event will take place
	const char* pchTexture;			AST(NAME(Texture, Image) POOL_STRING)
		// An image to show for this event
	char* pchMissionDef;			AST(NAME(Mission))
		// The mission name associated with this event
	BulletinMessage** eaMessages;	AST(NAME(Message))
		// Messages describing the event
} BulletinEvent;

AUTO_STRUCT;
typedef struct BulletinDef
{
	U32 uActivateTime;				
		// This is set automatically from pchDisplayDate
	U32 uIgnoreTime;
		// This is set automatically from pchIgnoreDate
	char* pchDisplayDate;				AST(NAME(DisplayDate))
		// The date that this bulletin should be displayed
	char* pchIgnoreDate;				AST(NAME(IgnoreDate))
		// The date that this bulletin should no longer be displayed
	BulletinMessage** eaMessages;		AST(NAME(Message))
		// Messages for each language
	BulletinEvent* pEvent;				AST(NAME(Event))
		// An optional event that this bulletin describes
	char* pchMicroTransDef;				AST(NAME(MicroTransactionDef))
		// A MicroTransaction associated with this bulletin
	char* pchLink;						AST(NAME(Link))
		// Optional web link
	char* pchCategory;					AST(NAME(Category))
		// The category that this bulletin falls under
	S32 eCategory;						NO_AST
		// Cached integer generated for the category for faster comparisons
	REF_TO(MicroTransactionDef) hMTDef; AST(CLIENT_ONLY)
		// Set the reference on the client
} BulletinDef;

AUTO_STRUCT;
typedef struct BulletinCategory
{
	char* pchName;						AST(NAME(Name) STRUCTPARAM)
		// The internal name of the category
	const char* pchTexture;				AST(NAME(Texture, Image) POOL_STRING)
		// An image to display for this category
	BulletinMessage** eaMessages;		AST(NAME(Message))
		// Different category names and descriptions for each language
	char* pchMicroTransDef;				AST(NAME(MicroTransactionDef))
		// A MicroTransaction associated with this bulletin category
	S32 eCategory;						NO_AST
		// Cached integer generated for the category for faster comparisons
	REF_TO(MicroTransactionDef) hMTDef; AST(CLIENT_ONLY)
		// Set the reference on the client
} BulletinCategory;

AUTO_STRUCT;
typedef struct BulletinsStruct
{
	U32 uVersion; 
	BulletinDef** eaDefs;				AST(NAME(Bulletin))
	BulletinCategory** eaCategories;	AST(NAME(Category))
} BulletinsStruct;

AUTO_STRUCT;
typedef struct ChatAuthRequestData
{
	U32 uAccountID;
	U32 uCharacterID; // optional
	U32 uAccountAccessLevel;
	bool bSocialRestricted;

	char *pAccountName;
	char *pDisplayName;
} ChatAuthRequestData;
// Also used for the response data

AUTO_STRUCT;
typedef struct ChatLoginData
{
	U32 uAccountID;
	char *pAccountName;
	char *pDisplayName;
	U32 uAccessLevel;

	// TODO(Theo) implement this for userLoginOnly
	UserStatus eInitialStatus;

	PlayerInfoStruct *pPlayerInfo;
} ChatLoginData;

AUTO_STRUCT;
typedef struct ChatAuthData
{
	U32 uAccountID;
	U32 uCharacterID;
	char *pRelayIPString;
	U32 uRelayPort;
	U32 uSecretValue;
	
	ChatLoginData userLoginData;

	bool bAuthenticated; NO_AST
	bool bLoggedIn; NO_AST
	NetLink *pChatRelayLink; NO_AST
} ChatAuthData;

AUTO_STRUCT;
typedef struct ChatAccessChangeMessage
{
	U32 uAccountID;
	U32 uChannelID;
	char *targetHandle;

	U32 uAddFlags;
	U32 uRemoveFlags;
	ChannelUserLevel ePermissionLevel;
} ChatAccessChangeMessage;

AUTO_ENUM;
typedef enum ChatGMUpdate
{
	CHATGMUPDATE_FULL = 0,
	CHATGMUPDATE_ADD,
	CHATGMUPDATE_REMOVE,
} ChatGMUpdate;

AUTO_STRUCT;
typedef struct ChatIgnoreData
{
	U32 uUserAccountID;

	U32 uTargetAccountID;
	const char *pTargetHandle; AST(NAME(targetHandle))
	bool bSpammer;
	const char *pSpamMessage;
} ChatIgnoreData;
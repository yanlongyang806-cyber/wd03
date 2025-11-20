#ifndef _CHAT_DB_H
#define _CHAT_DB_H

#include "GlobalTypeEnum.h"
#include "ReferenceSystem.h"
#include "chatCommonStructs.h"
//#include "netio.h"

typedef struct StashTableImp *StashTable;
typedef struct ChatUser ChatUser;
typedef struct PlayerInfoStruct PlayerInfoStruct;
typedef struct XMPPChat_Settings XMPPChat_Settings;
typedef enum Language Language;

AUTO_ENUM;
typedef enum ChannelAccess
{
	CHANFLAGS_NONE      = 0,
	CHANFLAGS_JOIN		= 1,
	CHANFLAGS_SEND		= 2,
	CHANFLAGS_OPERATOR	= 4,
	CHANFLAGS_ADMIN		= 8,
	CHANFLAGS_RENAMED	= 16,
	CHANFLAGS_INVISIBLE	= 32,
	CHANFLAGS_CSR       = 64,
	CHANFLAGS_VOICE		= 128,
	CHANFLAGS_COMMON	= CHANFLAGS_JOIN | CHANFLAGS_SEND | CHANFLAGS_VOICE,
	CHANFLAGS_COPYMASK	= CHANFLAGS_COMMON | CHANFLAGS_OPERATOR,
	CHANFLAGS_GM        = CHANFLAGS_ADMIN | CHANFLAGS_CSR,
} ChannelAccess;

#define MAX_CHANMOTD 5
#define MAX_STOREDMSG 10
#define MAX_GFRIENDS 100

#define MAX_PLAYERNAME 21
#define MAX_CHANNELNAME 21
#define MAX_CHANNELDESC 127
#define MAX_FRIENDSTATUS 200

// Defaults
#define MAX_WATCHING 5
#define EMAIL_MAX_COUNT 100
#define EMAIL_MAX_AGE (30 * 24 * 60 * 60) // 30 days, in seconds
#define EMAIL_READ_MAX_AGE (5 * 24 * 60 * 60) // 5 days, in seconds
#define DEFAULT_MAX_NAUGHTY 100
#define DEFAULT_HANDLE_CACHE_EXPIRATION (180)

#define CHATRESULT_MAX_LEN 300

#define UserIsAdmin(user) (user->access_level >= ACCESS_GM)

// Chat Relay defaults
#define CHATRELAY_DEFAULT_MAX_USERS 2000
#define CHATRELAY_USERCOUNT_WARNING_CRITERIA 1750

AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "internalChannelName, displayChannelName");
typedef struct SpecialChannelDisplayNameMap
{
	char *internalChannelName; AST(ESTRING KEY)
	char *displayChannelName; AST(ESTRING)
} SpecialChannelDisplayNameMap;

AST_PREFIX(PERSIST)
AUTO_STRUCT AST_CONTAINER;
typedef struct Email
{
	const U32 uID; AST(KEY) // unique-per-account ID
	const U32 sent; AST(FORMATSTRING(HTML_SECS = 1))// time sent
	const U32 expireTime; AST(FORMATSTRING(HTML_SECS = 1))
	const ContainerID	from; // ID from
	CONST_STRING_MODIFIABLE subject;
	CONST_STRING_MODIFIABLE body;

	CONST_STRING_POOLED shardName; AST(POOL_STRING) // originating shard of message

	const U32 uLotID; // Lot ID on Auction Server that holds the items

	const U32				senderContainerID;			// entity container ID, type is based on email type
	const U32				recipientContainerID;		// entity container ID always of player type
	CONST_STRING_MODIFIABLE	senderName;					// sender name, character name for players
	const EMailType			eTypeOfEmail;				// type of email this is
	const S32				iNPCEMailID;				// ID of email (NPC player emails)

	const bool bRead : 1;
} Email;

#define CHANNEL_FLAG_SYSTEM 0x01 // System channel flag (zones, teams, etc)
#define CHANNEL_FLAG_GLOBAL 0x02 // Global channel flag (default: local-only)

AUTO_STRUCT AST_CONTAINER;
typedef struct ChatChannel
{
	const U32 uKey; AST(KEY)
	CONST_STRING_MODIFIABLE name;
	const ChannelAccess	access;
	CONST_EARRAY_OF(Email) messageOfTheDay; AST( ADDNAMES(email) )
	CONST_STRING_MODIFIABLE description;

	const U32 permissionLevels[CHANUSER_COUNT];

	CONST_CONTAINERID_EARRAY members;		AST(INT)
	U32 *online;	AST_NOT(PERSIST)
	CONST_CONTAINERID_EARRAY invites;	AST(INT)

	const U32 uFlags;
	const U32 reserved; // flags set for system-created channels; persisted for SPECIAL_GLOBAL channels
	U8 adminSent : 1; NO_AST

	// Voice data
	U32 voiceId;			NO_AST
	char* voiceURI;			NO_AST
	U8 voiceEnabled : 1;	NO_AST

	// These two fields ignore GM users (users with access level > ACCESS_GM)
	U32 uOnlineCount; AST_NOT(PERSIST)
	const U32 uMemberCount;

	CONST_CONTAINERID_EARRAY bannedUsers; AST(INT)
	CONST_CONTAINERID_EARRAY mutedUsers; AST(INT)

	// These commands are only available on the GCS; Shard Chat Servers cannot mess with ChatUsers
	AST_COMMAND("Add User to Channel", "csrForceChannelJoin $FIELD(name) $STRING(User Handle)")
	AST_COMMAND("Remove User from Channel", "csrForceChannelKick $FIELD(name) $STRING(User Handle)")
	AST_COMMAND("Change Channel Access", "csrChannelAccess $FIELD(name) $STRING(Options)")
	AST_COMMAND("Change User Access", "csrUserAccess $FIELD(name) $STRING(User Handle) $STRING(Options)")
	AST_COMMAND("Destroy Channel", "channelCmdDestroy $FIELD(uKey)")
} ChatChannel;

AUTO_STRUCT AST_CONTAINER AST_IGNORE(access);
typedef struct Watching
{
	CONST_STRING_MODIFIABLE name; AST(KEY)
	const U32 last_read;
	//const ChannelAccess	access;
	const ChannelUserLevel ePermissionLevel;
	const ContainerID channelID;
	const bool bSilenced; // deprecated
} Watching;

AUTO_STRUCT AST_CONTAINER;
typedef struct ChatFriendRequest
{
	const U32 userID;
	const U32 targetID;
	const U32 uTimeSent;
} ChatFriendRequest;

AUTO_STRUCT AST_CONTAINER;
typedef struct ChatFriendComment
{
	const ContainerID id; AST(KEY)
	CONST_STRING_MODIFIABLE comment; AST(ESTRING)
} ChatFriendComment;

AUTO_ENUM;
typedef enum ChatFriendDirection
{
	CHATFRIEND_OUTGOING = 0,
	CHATFRIEND_INCOMING,
} ChatFriendDirection;

AUTO_STRUCT AST_CONTAINER;
typedef struct ChatFriendRequestStruct
{
	const U32 userID; AST(KEY) // Request To or From this user
	const ChatFriendDirection eDirection;
	const U32 uTimeSent;
} ChatFriendRequestStruct;

#define CHATUSER_FLAG_GM 0x1
#define CHATUSER_FLAG_BANNED 0x2 // Banned from using all chat features == perma-silence
#define CHATUSER_FLAG_BOT 0x4 // Special GM/Auto-reply bot accounts that cannot be replied to

AUTO_STRUCT AST_CONTAINER;
typedef struct SpamMessage
{
	const U32 lastReportTime;
	CONST_STRING_MODIFIABLE hash;
	CONST_STRING_MODIFIABLE message;
} SpamMessage;

AUTO_STRUCT AST_CONTAINER;
typedef struct ChatUser
{
	const ContainerID		id; AST(KEY)
	const char accountName[128];
	const char handle[128];
	char *escapedHandle; AST_NOT(PERSIST) AST(ESTRING NO_NETSEND NO_TEXT_SAVE)
	const U32 uHandleUpdateTime; AST(FORMATSTRING(HTML_SECS = 1)) // last time when handle was updated from account server
	U32 uHandleLastChecked; NO_AST // last time the veracity of the handle was checked; not persisted
	const ChannelAccess	access;
	const U8 access_level;
	const U32 last_online;
	const Language eLastLanguage;
	
	// Global system/special channels go in here, as well as custom channels
	CONST_EARRAY_OF(Watching)		watching;
	// SHARD system channels that the user is watching; not persisted (never used on GCS)
	EARRAY_OF(Watching)       reserved; AST_NOT(PERSIST) AST(NO_NETSEND NO_TEXT_SAVE)

	const U32 uLastEmailID;
	int uUnreadCount; AST_NOT(PERSIST) AST(DEFAULT(-1))
	CONST_EARRAY_OF(Email) email; AST(NO_NETSEND) // Offline messages

	CONST_CONTAINERID_EARRAY		ignore;		AST(INT)
	CONST_CONTAINERID_EARRAY		friends;		AST(INT)

	CONST_EARRAY_OF(ChatFriendComment) friend_comments;

	//deprecated
	CONST_EARRAY_OF(ChatFriendRequest) befriend_reqs; // userID == this account
	CONST_EARRAY_OF(ChatFriendRequest) befrienders; // targetID == this account
	//end deprecated

	CONST_EARRAY_OF(ChatFriendRequestStruct) friendReqs_out; // requests from this account
	CONST_EARRAY_OF(ChatFriendRequestStruct) friendReqs_in; // requests to this account

	// Time user is silenced until
	const U32 silenced; AST(FORMATSTRING(HTML_SECS = 1))
	CONST_CONTAINERID_EARRAY invites; AST(INT)

	PlayerInfoStruct *pPlayerInfo; AST_NOT(PERSIST) AST(NO_NETSEND)
	U32 online_status;		AST_NOT(PERSIST)
	char *status;			AST_NOT(PERSIST) AST(ESTRING)
	// This is set when the user goes from logged out to logged in and a "user is online" message needs to be sent
	bool bOnlineChange; AST_NOT(PERSIST) AST(NO_NETSEND NO_TEXT_SAVE)

	// the exception for USERSTATUS_DEAFEN whose private chat won't be ignored (user ID)
	U32				deafen_exceptionID; NO_AST 
	
	U8				adminSent : 1; NO_AST
	U8				searchFriend : 1; NO_AST // set during team search to cache result of slow "is friend" check

	CONST_CONTAINERID_EARRAY spying; AST(INT) // admins that are spying on this account's chat
	CONST_CONTAINERID_EARRAY spyingOn; AST(INT) // users this admin is spying on

	// For use by the Global Chat server
	PlayerInfoStruct ** ppPlayerInfo; AST_NOT(PERSIST) AST(NO_NETSEND NO_TEXT_SAVE) // List of characters this account has online

	// Number of messages sent within 1 second of each other.  This number is decremented by 1 for each second that passes without
	// a message being sent.
	S32 spamMessages; AST_NOT(PERSIST) AST(NO_NETSEND NO_TEXT_SAVE)
	// Time of most recent message sent by this user
	U32 lastMessageTime; AST_NOT(PERSIST) AST(NO_NETSEND NO_TEXT_SAVE) 
	// Same thing, except for mails
	S32 spamMails; AST_NOT(PERSIST) AST(NO_NETSEND NO_TEXT_SAVE)
	U32 lastMailTime; AST_NOT(PERSIST) AST(NO_NETSEND NO_TEXT_SAVE)

	// Local chat spam values - used only on local shard
	U32 uLastLocalMessage; AST_NOT(PERSIST) AST(NO_NETSEND NO_TEXT_SAVE)
	U32 uMessageBufferUsed; AST_NOT(PERSIST) AST(NO_NETSEND NO_TEXT_SAVE)

	// naughty value, incremented when player spams.
	// If naughty value reaches a predefined threshold, the player will be silenced and a csr ticket will be initiated
	const S32 naughty; 
	const U32 uLastNaughtyTime; AST(NO_NETSEND)
	// number of times this user has unignored someone
	S32 unignoreCount; AST_NOT(PERSIST) AST(NO_NETSEND NO_TEXT_SAVE)
	// Spam Information
	INT_EARRAY eaiSpamIgnorers; AST_NOT(PERSIST) AST(NO_NETSEND NO_TEXT_SAVE)
	EARRAY_OF(SpamMessage) eaSpamMessages; AST(NO_NETSEND) // saves the last X spam messages (configurable)

	// Chat whitelist
	const U8 chatWhitelistEnabled : 1;
	// Tell whitelist
	const U8 tellWhitelistEnabled : 1;
	// Email whitelist
	const U8 emailWhitelistEnabled : 1;

	const U32 uFlags;

	// Voice chat data (SHARD only)
	const char*				voice_username;			NO_AST
	const char*				voice_uri;				NO_AST
	int						voice_accountid;		NO_AST

	// Cached Data
	bool bMailReadInit; NO_AST
	bool bHasUnreadMail; NO_AST
	U32 uNextFutureMail; NO_AST // TODO(Theo) how to handle per-user mails

	// Persistent XMPP client settings.
	CONST_OPTIONAL_STRUCT(XMPPChat_Settings) xmppSettings;	AST(FORCE_CONTAINER LATEBIND NO_NETSEND)

	const U32 uShardMergerVersion; // @see chatShardCluster.h:ShardMergerInfoList
	CONST_STRING_EARRAY eaCharacterShards; AST(POOL_STRING)
	
	// Shard-only
	U32 uChatRelayID; AST_NOT(PERSIST) AST(NO_NETSEND NO_TEXT_SAVE)
	EARRAY_OF(ChatUserMinimalNameIndex) eaCachedFriendInfo; AST_NOT(PERSIST) AST(NO_NETSEND NO_TEXT_SAVE)
	U32 uBlacklistCount; AST_NOT(PERSIST) AST(NO_NETSEND NO_TEXT_SAVE)
	U32 uBlacklistLastTime; AST_NOT(PERSIST) AST(NO_NETSEND NO_TEXT_SAVE)

	// These commands are only available on the GCS; Shard Chat Servers cannot mess with ChatUsers
	AST_COMMAND("Delete User", "userDelete $FIELD(id)")
	AST_COMMAND("Add User to Channel", "csrForceChannelJoin $STRING(Channel Name) $FIELD(handle)")
	AST_COMMAND("Remove User from Channel", "csrForceChannelKick $STRING(Channel Name) $FIELD(handle)")
	AST_COMMAND("Change User Access", "csrUserAccess $STRING(Channel Name) $FIELD(handle) $STRING(Options)")
	AST_COMMAND("Silence User", "chatServer_UserSilence $FIELD(id) $INT(Duration)")
	AST_COMMAND("Unsilence User", "chatServer_UserUnsilence $FIELD(id)")
	AST_COMMAND("Reassociate Usernames", "ChatServerReassociateUser $FIELD(id)")
	AST_COMMAND("Chat Ban User", "chatServer_UserBan $FIELD(id)")
	AST_COMMAND("Chat Unban User", "chatServer_UserUnban $FIELD(id)")
} ChatUser;
AST_PREFIX();

AUTO_STRUCT;
typedef struct ChatDb
{
	StashTable		channel_names;
	StashTable		user_names;
	StashTable		account_names;
	StashTable		gad_by_id;
	
	U32					online_count;
} ChatDb;

typedef struct ChatTransData
{
	ContainerID userID;
	ContainerID targetID;
	char * channel_name;
} ChatTransData;

extern ChatDb	chat_db;

enum {kChatId_Handle	= 0,
	  kChatId_DbId		= 1,
	  kChatId_AuthId	= 2,
	  NUM_CHAT_ID_TYPES};

AUTO_STRUCT;
typedef struct ChatTranslationParam
{
	char *key;

	char *pchStringValue;
	int iIntValue;

	// Only supports strings and ints right now; see StringFormat.h for definitions
	U8 strFmt_Code;
} ChatTranslationParam;

AUTO_STRUCT;
typedef struct ChatTranslation
{
	char *key;
	int eType;
	char *channel_name;
	ChatTranslationParam **ppParameters;
} ChatTranslation;

AUTO_STRUCT;
typedef struct ChatMailReceipt
{
	U32 uSenderID;

	// For Item Mails
	U32 uRequestID;
	U32 uAuctionLot;

	ChatTranslation *error;
} ChatMailReceipt;

AUTO_STRUCT;
typedef struct ChatMailRequest
{
	// For item mails
	U32 uRequestID; 

	// One of these must be non-zero/NULL
	ContainerID recipientAccountID;
	char *recipientHandle;

	// Future send time for delayed mails
	//U32 uFutureSendTime;
	// Contains all the other info needed
	ChatMailStruct *mail;
} ChatMailRequest;

#endif

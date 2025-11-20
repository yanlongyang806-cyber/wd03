/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef ASLLOGINSERVER_H_
#define ASLLOGINSERVER_H_

#include "GlobalComm.h"
#include "MapDescription.h"
#include "referencesystem.h"

typedef struct SentCostume SentCostume;
typedef struct PossibleCharacterChoices PossibleCharacterChoices;
typedef struct PossibleCharacterChoice PossibleCharacterChoice;
typedef struct PossibleMapChoices PossibleMapChoices;
typedef struct ReturnedGameServerAddress ReturnedGameServerAddress;
typedef struct CharacterCreationDataHolder CharacterCreationDataHolder;
typedef struct Entity_AutoGen_NoConst Entity_AutoGen_NoConst;
typedef struct LoginLink LoginLink;
typedef struct Container Container;
typedef struct ResourceCache ResourceCache;
typedef struct AccountValidator AccountValidator;
typedef struct TimedCallback TimedCallback;
typedef struct AccountProxyKeyValueInfoList AccountProxyKeyValueInfoList;
typedef struct TransactionReturnVal TransactionReturnVal;
typedef struct GameAccountData GameAccountData;
typedef struct GamePermissionDef GamePermissionDef;
typedef struct PossibleUGCProjects PossibleUGCProjects;
typedef struct PossibleUGCProject PossibleUGCProject;
typedef struct PlayerTypeConversion PlayerTypeConversion;
typedef struct GameSession GameSession;
typedef enum LoginLinkState LoginLinkState;
typedef struct LoadScreenDynamic LoadScreenDynamic;
typedef struct GameContentNodeRef GameContentNodeRef;
typedef enum NotifyType NotifyType;
typedef enum PlayerType PlayerType;
typedef struct Login2State Login2State;

// The AccountServer persisted key storing the last terms of use agreed to
#define TERMS_OF_USE_KEY "TermsOfUse"

extern bool gbMachineLockDisable;

extern bool gUsingLogin2;

// Global state for the login server
typedef struct LoginServerState
{
	LoginLink **failedLogins;
	LoginLink **queueAuth; // logins awaiting ticket validation
	NetListen **loginLinks;
	int bIsTestingMode;
	int bAllowVersionMismatch;

	//if true, then treat local IPs like all others, don't automatically give them level 9
	int bNoLocalIPExceptions;
	
	// if true loads maps like development mode
	bool bAllowSkipTutoral;		
} LoginServerState;

AUTO_STRUCT;
// Per-link information
typedef struct LoginLink
{
	char ISM_State[256];
	U32 accountID;
	char accountName[128];
	char pwAccountName[128];
	char displayName[128];
	char characterName[128];
	char characterSubName[128];
	int loginCookie; AST(KEY)
	NetLink* netLink; NO_AST
	GlobalType clientType;
	ContainerID clientID;
	ContainerID clientTeamID;
	int clientAccessLevel;
	char* failedReason;
	int clientLangID;
	char *machineID;

	U32 ipRequest;
		// If non-zero, indicates the IP address of the client being logged in.
		// This is used by the login proxies (such as the GatewayProxy server) which uses
		//   a single netlink (and hence IP) to log in multiple clients.

	int iEditQueueCookie; //used by the queue waiting to be able to have an edit game server

	bool bFailedLogin; // This login has failed and is being kept around for cleanup
	bool bMovedToLoginServer; // We think we've moved it to the login server
	bool bMovedToDestination; // We think we've moved it to the destination
	bool bGotMapSearchRequest;
	bool bJoiningInProgressGameSession;
	bool bAutoLoginAfterPlayerTypeConversion; // if true, when we complete conversion of player type (generally silver to gold), 
		// just log the character in rather than return to character select

	PossibleCharacterChoices *pAllCharacterChoices; //remember all the character choices,
		//so that we can boot all the player's other characters

	SentCostume **ppSentCostumes;

	// FIXME: This makes some of the other fields (character name,
	// clientID, deleteIfExists) redundant.
	PossibleCharacterChoice *pChosenCharacter;
	ReturnedGameServerAddress *pReturnedAddress;

	PlayerTypeConversion *pConversion;
		// PlayerType Conversion attempt requested by client.  When we get one of these, 
		//  it triggers transferring the character to the login server, but not for
		//  logging in - the existence of this indicates we should attempt conversion
		//  and then hand it back to the objectdb.

	MapSearchInfo *pMapSearchInfoFromClient; //sent to us by the client, not authoritative

	MapSearchInfo *pMainMapSearchInfo; //where we really want to go
	MapSearchInfo *pBackupMapSearchInfo; //used when the first map being requested is one like a PVP map where we only
										 //want to go there if the same instance exists
										 //
										 //always has the same flags and stuff, just a different map description
	PossibleMapChoice *pChosenMap;

	// Active game session the client is participating
	GameSession *pActiveGameSession;

	//store all the possible map choices, so that when we get one back we can verify it is one we allow
	PossibleMapChoices *pAllMapChoices;

	ContainerID destinationServer;
	GlobalType destinationType;

	NOCONST(Entity) *pNewCharacter; NO_AST
	ResourceCache *pResourceCache; NO_AST
	
	AccountValidator *validator;  NO_AST
	int loginTries;	

	TimedCallback*	corruptionCallback; NO_AST

	// This actually contains the entire key/value list for the account, not just permissions.
	AccountProxyKeyValueInfoList *pAccountPermissions;

	GameAccountData *pGameAccount;
	REF_TO(GameAccountData) hGAD;		AST(COPYDICT(GameAccountData))
	GamePermissionDef **eaGamePermissions;
	U32 ePlayerType;		AST(SUBTABLE(PlayerTypeEnum))

	time_t uHeartbeatTimer; AST(INT)

	U32 iQueueID; //your ID in the queue on the controller. Non-zero means you are queued
	int iQueuePosition; //your last reported position in the queue
	int iQueuePositionOffset; //when we get a "ID 17 just logged in" command from the controller, we need an offset saved to remember how close to the front of the queue we are

	time_t uQueueTimeTotal; AST(INT)

	int iNumEntitiesHavingVersionFixed;
	int iMigrateTag;

	// Previously sent to client data about UGC
	int iPrevSentToClientUGCProjectMaxSlots;
	int iPrevSentToClientUGCSeriesMaxSlots;

	bool bIsNewCharacter:1;				// Set upon creating struct for new character old character will not have this set
	bool bAddedToPending:1;
	bool bCharacterChoicesReceived:1;
	bool bCharacterCostumesReceived:1;
	bool bCharacterChoicesSent:1;
	bool bGameAccountFixed:1;
	bool bGameAccountDataFinalRequestAfterDaysPlayedCheck:1;
	bool bGameAccountDataReady:1;

	bool bLifetime:1;					// Lifetime subscription
	bool bPress:1;						// Press account

	bool bIgnoreQueue:1;				// Ignore the login queue entirely, regardless of shard cap
	bool bVIPQueue:1;					// VIP access, jump to the front of the queue

	bool bGoToUGCChoiceAfterCharacterTransfer:1; 
	//the user has clicked the "Edit UGC" button on the character select screen... 
	//do all the character transfer as normal, but then go to the choose UGC state
	//instead of the choose map state


	bool bWentThroughLoginQueue : 1;

	bool bUGCShardCharactersOnly : 1;
	// If aslProcessSpecialTokens fails but account has ACCOUNT_PERMISSION_UGC_ALLOWED login is allowed but only ugc characters

	bool bMoveToGatewayServer: 1; // If true, sends the chosen character to the GatewayServer (who then owns it).
	bool bNoPlayerBooting: 1;     // If true, will not kick existing logins for this account.
	bool bIsProxy: 1;             // If true, this link is to a login proxy which handles logging in for multiple players.

	int iCurBootings;

	//received from map manager, sent down to client
	PossibleUGCProjects *pPossibleUGCProjects;
	PossibleUGCProjects *pPossibleUGCImports;
	CONTAINERID_EARRAY eaUGCProjectIDsFromSearch;

	//chosen by client, sent back up
	PossibleUGCProject *pChosenUGCProject;

	// Affiliate name to be set after authn
	char affiliate[32];

	// The destination player selected in the quest selector
	GameContentNodeRef *pSoloPlayDestination;

	//for transfers to Gateway Servers
	ContainerID iGatewayServerID;
} LoginLink;

static __forceinline void aslLoginLinkSafeDestroyValidator(LoginLink *loginLink)
{
	if (loginLink->validator)
	{
		accountValidatorDestroy(loginLink->validator);
		loginLink->validator = NULL;
	}
}

extern LoginServerState gLoginServerState;

AUTO_STRUCT;
typedef struct SubscriberCharacterSlotBonus
{
	U32 iCharacterSlotBonusDays;
	U32 iCharacterSlotBonus;
    bool bGrantForLifetime;
} SubscriberCharacterSlotBonus;

AUTO_STRUCT;
typedef struct ProjectLoginServerConfig
{
	U32 iMaximumNumOfCharacters;			AST( NAME(MaximumNumOfCharacters) )
	U32 iBonusCharactersAccessLevel9;		AST( NAME(BonusCharactersAccessLevel9) )
	CONST_EARRAY_OF(SubscriberCharacterSlotBonus) pBonusSubscriberCharacterSlots;
	
} ProjectLoginServerConfig;

extern ProjectLoginServerConfig gProjectLoginServerConfig;

extern U32 guPendingLogins;
extern bool gbUGCEnabled;

// Basic Login Server functions
int aslLoginInit(void);
int aslLoginOncePerFrame(F32 fElapsed);

// Writes out the login state to log
void aslLoginDumpLinkState(LoginLink *pLink);

// Finds the appropriate LoginLink for the given character
LoginLink *aslFindLoginLinkForCookie(int searchCookie);
LoginLink *aslFindLoginLinkForContainer(GlobalType containerType, ContainerID containerID);

// Set the login link to be a failed one, and send message to client
void aslFailLogin(LoginLink *link, const char *pErrorString);

// Notify the link of an error (but don't fail it)
void aslNotifyLogin(LoginLink *loginLink, const char *pNotifyString, NotifyType eType, const char *pLogicalString);

// Notify the link of an error (but don't fail it), login2 version.
void aslLogin2_Notify(Login2State *loginState, const char *pNotifyString, NotifyType eType, const char *pLogicalString);

// Cancel any ongoing logins for the specified character
int aslCancelLoginProcess(GlobalType containerType, ContainerID containerID, char *message);

// Sets up a new character, given the information on the LoginLink
bool aslInitializeNewCharacter(LoginLink *link, Entity_AutoGen_NoConst *entity);

// Figures out which maps to list for a player
bool aslDetermineMapSearchInfo(LoginLink *link, MapSearchInfo *requestedSearch);

// Modify the possible map choice list
bool aslModifyPossibleMapChoices(LoginLink *link, PossibleMapChoices *ppChoices);

// Gets the data needed by the client for creating a character
void aslGetCharacterCreationData(GameAccountData *gameAccountData, PlayerType playerType, ContainerID accountID, CharacterCreationDataHolder *holder);

// Send updates to connected client
void aslLoginSendRefDictDataUpdates(Login2State *loginState);


// State machine for the login links

#define LOGIN_STATE_MACHINE "aslLoginServer"

// Simple machine, all states are siblings

// Basic states, handled in aslLoginServer

// First login, before any communication with client
#define LOGINSTATE_INITIAL "LoginInitial" 
// Login has failed. Client has disconnected and we're cleaning up state
#define LOGINSTATE_FAILED "LoginFailed"
// Transfer to destination complete, waiting for client to disconnect
#define LOGINSTATE_COMPLETE "LoginComplete"

//handled in aslLoginUGCProject

//client has asked for possible UGC projects, will choose between them
#define LOGINSTATE_SELECTING_UGC_PROJECT "LoginSelectingUGCProject"

// Handled in aslLoginCharacterSelect

// Client sitting at character select screen
#define LOGINSTATE_SELECTING_CHARACTER "LoginSelectingCharacter"
// Client in the process of creating a new character
#define LOGINSTATE_CREATING_CHARACTER "LoginCreatingCharacter"
// Client has selected an existing character
#define LOGINSTATE_GETTING_EXISTING_CHARACTER "LoginGettingCharacter"
// We're returning to the character selection screen, and returning character to db server
#define LOGINSTATE_GOING_TO_CHARACTER_SELECT "LoginReturningToSelectingCharacter"

//after login, before creating the game account or selecting a character
#define LOGINSTATE_GETTING_ACCOUNT_PERMISSIONS "LoginGettingAccountPermissions"

//after getting account permissions, before getting game account - only if one-time code is needed
#define LOGINSTATE_WAITING_FOR_ONETIMECODE "LoginAwaitingOneTimeCode"

//after getting account permissions, before selecting a character
#define LOGINSTATE_GETTING_GAME_ACCOUNT "LoginGettingGameAccount"

// Handled in aslLoginMapTransfer

// Client sitting at map select screen
#define LOGINSTATE_SELECTING_MAP "LoginSelectingMap"
// In the process of transfering character to destination
#define LOGINSTATE_TRANSFERRING_CHARACTER "LoginTransferringCharacter"
//going to move to a GatewayServer
#define LOGINSTATE_TRANSFERRING_TO_GATEWAYSERVER "LoginTransferringToGatewayServer"


// How often to send heartbeat packets
#define LOGINSERVER_HEARTBEAT_INTERVAL 30.0

bool aslLoginServerClientsAreUntrustworthy(void);

void loginLinkLog(LoginLink *pLink, char *pAction, char *pFormatStr, ...);

extern StashTable sLoginLinksByID;


U32 GetUGCVirtualShardID(void);

// Get the const LoadScreenDynamic *pointer
const LoadScreenDynamic *aslGetLoadScreenDynamic();

void ReportThatPlayerWhoWentThroughQueueHasLeft(void);

extern int giCurLinkCount;
#endif

#pragma once
/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "stdtypes.h"
#include "referencesystem.h"

typedef U32 ContainerID;
typedef struct GameAccountData GameAccountData;
typedef struct NetLink NetLink;
typedef struct BeginLoginPacketData BeginLoginPacketData;
typedef struct SaveNextMachinePacketData SaveNextMachinePacketData;
typedef struct OneTimeCodePacketData OneTimeCodePacketData;
typedef enum PlayerType PlayerType;
typedef struct AccountTicket AccountTicket;
typedef struct GamePermissionDef GamePermissionDef;
typedef struct Login2CharacterSelectionData Login2CharacterSelectionData;
typedef struct RequestCharacterDetailPacketData RequestCharacterDetailPacketData;
typedef enum Login2RedirectType Login2RedirectType;
typedef struct RedirectLoginPacketData RedirectLoginPacketData;
typedef struct ChooseCharacterPacketData ChooseCharacterPacketData;
typedef struct DeleteCharacterPacketData DeleteCharacterPacketData;
typedef struct RenameCharacterPacketData RenameCharacterPacketData;
typedef struct CreateCharacterPacketData CreateCharacterPacketData;
typedef struct Login2CharacterChoice Login2CharacterChoice;
typedef struct MapSearchInfo MapSearchInfo;
typedef struct PossibleMapChoices PossibleMapChoices;
typedef struct PossibleMapChoice PossibleMapChoice;
typedef struct ReturnedGameServerAddress ReturnedGameServerAddress;
typedef struct Entity Entity;
typedef struct PossibleUGCProjects PossibleUGCProjects;
typedef struct PossibleUGCProject PossibleUGCProject;
typedef struct Login2RedirectDestinationInfo Login2RedirectDestinationInfo;
typedef struct Login2CharacterCreationData Login2CharacterCreationData;

AUTO_STRUCT;
typedef struct Login2StateDebugInfo
{
    const char *stateName;  NO_AST
    U32 timeStarted;
} Login2StateDebugInfo;

AUTO_STRUCT;
typedef struct Login2State
{
    ContainerID accountID;

    // State name is a static string.  No need to alloc or free. 
    const char *currentStateName;   NO_AST
    const char *accountName;
    const char *accountDisplayName;
    const char *pweAccountName;
    U32 ticketID;
    const char *machineID;
    U32 clientIP;
    U32 clientLanguageID;
    S32 clientAccessLevel;
    PlayerType playerType;
    const char *affiliate;          AST(POOL_STRING)
    S32 characterBootCount;
    Login2CharacterChoice *selectedCharacterChoice;
    Login2CharacterCreationData *characterCreationData;
    U32 numCharactersFromAccountServer;
    U32 maxLevelFromAccountServer;

    // Time player has played since last rest break.  Used for anti-addiction enforcement.
    U32 addictionPlayedTime;

    // The time of the last logoff for this player.  Used by anti-addiction code.
    U32 lastLogoutTime;

    // The time that the current state was entered.  Used for checking timeouts.
    U32 timeEnteredCurrentState;

    // The timeout in seconds for the current state.  A value of zero means no timeout.
    U32 timeoutForState;

    // The container ID of the character selected for login.
    ContainerID selectedCharacterID;

    // Container ID of newly created character.
    ContainerID newCharacterID;

    // The prototype character entity used for character creation.
    Entity *newCharacterEnt;

    // This array container the names of any GamePermissions that are granted via account permissions.
    STRING_EARRAY gamePermissionsFromAccountPermissions;    AST(POOL_STRING)

    AccountTicket *accountTicket;
    U64 loginCookie;

    // The character detail request that has been requested and we are currently awaiting the results for.
    RequestCharacterDetailPacketData *activeCharacterDetailRequest;

    // Redirect info
    STRING_POOLED redirectShardName;                        AST(POOL_STRING)

    // Queue info
    U32 lastClientQueueUpdateTime;
    int queueID;

    // Map Search info
    MapSearchInfo *activeMapSearch;
    MapSearchInfo *mainSearchInfo;
    MapSearchInfo *backupSearchInfo;
    PossibleMapChoices *newMapChoices;
    PossibleMapChoices *lastMapChoices;

    ReturnedGameServerAddress *gameserverAddress;

    NetLink* netLink;                   NO_AST
    ResourceCache *resourceCache;       NO_AST

    bool isClientOnTrustedIP;
    bool isClientOnLocalHost;
    bool loginFailed;
    bool isLifetime;
    bool isPress;
    bool ignoreQueue;
    bool isQueueVIP;
    bool isThroughQueue;
    bool UGCCharacterPlayOnly;
    bool bootFailed;
    bool selectedCharacterOwnedByObjectDB;
    bool selectedCharacterArrived;
    bool playerTypeConversionComplete;
    bool transferToGameserverComplete;
    bool playerHasUGCProjectSlots;
    bool authorAllowsFeaturedCleared;
    bool afterRedirect;
    bool isProxy;
    bool noPlayerBooting;
    bool clientRequestedReturnToCharacterSelect;
    bool deletedCharacterIsUGC;
    bool onlineAndFixupComplete;

    // Convert the logged in character to the PlayerType that the player is logging in as.
    bool convertPlayerType;

    // Player is logging in to do UGC editing.
    bool requestedUGCEdit;

    // Data from various packets.
    BeginLoginPacketData *beginLoginPacketData;
    RedirectLoginPacketData *redirectLoginPacketData;
    SaveNextMachinePacketData *saveNextMachinePacketData;
    OneTimeCodePacketData *oneTimeCodePacketData;
    RequestCharacterDetailPacketData *requestCharacterDetailPacketData;
    ChooseCharacterPacketData *chooseCharacterPacketData;
    DeleteCharacterPacketData *deleteCharacterPacketData;
    RenameCharacterPacketData *renameCharacterPacketData;
    CreateCharacterPacketData *createCharacterPacketData;
    MapSearchInfo *requestedMapSearch;
    PossibleMapChoice *requestedGameserver;
    Login2RedirectDestinationInfo *redirectDestinationInfo;

    // Debug Data
    EARRAY_OF(Login2StateDebugInfo) debugStateHistory;

    // This reference will only be set after the GameAccountData has been refreshed.  The login2 code has been written so that
    //  GameAccountData is not required before the refresh is complete.
    REF_TO(GameAccountData) hGameAccountData;           AST(COPYDICT(GameAccountData))

    // The data needed for character selection and creation, which is sent down to the client.
    Login2CharacterSelectionData *characterSelectionData;

    // Fields needed for UGC project management.
    PossibleUGCProjects *possibleUGCProjects;
    PossibleUGCProjects *possibleUGCImports;
    PossibleUGCProject *chosenUGCProjectForEdit;
    PossibleUGCProject *chosenUGCProjectForDelete;
    int editQueueCookie;
    U32 prevSentToClientUGCProjectMaxSlots;
    U32 prevSentToClientUGCSeriesMaxSlots;

    // Fields related to cluster status.
    U32 lastClusterStatusSendTime;
} Login2State;

// The name of the login state machine.
#define LOGIN2_STATE_MACHINE "aslLogin2Server"

// States for the loginserver login state machine.
#define LOGIN2_STATE_INITIAL_CONNECTION "Login2InitialConnection" 
#define LOGIN2_STATE_VALIDATE_TICKET "Login2ValidateTicket" 
#define LOGIN2_STATE_GENERATE_ONE_TIME_CODE "Login2GenerateOneTimeCode" 
#define LOGIN2_STATE_WAIT_FOR_ONE_TIME_CODE_FROM_CLIENT "Login2WaitForOneTimeCodeFromClient" 
#define LOGIN2_STATE_VALIDATE_ONE_TIME_CODE "Login2ValidateOneTimeCode"
#define LOGIN2_STATE_GET_MACHINE_NAME_FROM_CLIENT "Login2GetMachineNameFromClient"
#define LOGIN2_STATE_SET_MACHINE_NAME "Login2SetMachineName"
#define LOGIN2_STATE_REFRESH_GAME_ACCOUNT_DATA "Login2RefreshGameAccountData"
#define LOGIN2_STATE_GET_CHARACTER_LIST "Login2GetCharacterList"
#define LOGIN2_STATE_CHARACTER_SELECT "Login2CharacterSelect"
#define LOGIN2_STATE_DELETE_CHARACTER "Login2DeleteCharacter"
#define LOGIN2_STATE_RENAME_CHARACTER "Login2RenameCharacter"
#define LOGIN2_STATE_CREATE_CHARACTER "Login2CreateCharacter"
#define LOGIN2_STATE_BOOT_PLAYER "Login2BootPlayer"
#define LOGIN2_STATE_ONLINE_AND_FIXUP "Login2OnlineAndFixup"
#define LOGIN2_STATE_GET_SELECTED_CHARACTER_LOCATION "Login2GetSelectedCharacterLocation"
#define LOGIN2_STATE_GET_SELECTED_CHARACTER "Login2GetSelectedCharacter"
#define LOGIN2_STATE_QUEUED "Login2Queued"
#define LOGIN2_STATE_REDIRECT "Login2Redirect"
#define LOGIN2_STATE_CONVERT_PLAYER_TYPE "Login2ConvertPlayerType"
#define LOGIN2_STATE_GET_MAP_CHOICES "Login2GetMapChoices"
#define LOGIN2_STATE_REQUEST_GAMESERVER_ADDRESS "Login2RequestGameserverAddress"
#define LOGIN2_STATE_SEND_CHARACTER_TO_GAMESERVER "Login2SendCharacterToGameserver"
#define LOGIN2_STATE_RETURN_TO_CHARACTER_SELECT "Login2ReturnToCharacterSelect"
#define LOGIN2_STATE_COMPLETE "Login2StateComplete"

#define LOGIN2_STATE_UGC_PROJECT_SELECT "Login2UGCProjectSelect"
#define LOGIN2_STATE_UGC_CLEAR_AUTHOR_ALLOWS_FEATURED "Login2UGCClearAuthorAllowsFeatured"

#define LOGIN2_QUEUE_UPDATE_INTERVAL 30

const char *aslLogin2_GetAccountValuesFromKeyTemp(Login2State *loginState, const char *key);

void aslLogin2_QueueIDsUpdate(U32 mainQueueID, U32 VIPQueueID, int totalInQueues);
void aslLogin2_HereIsQueueID(U64 loginCookie, U32 queueID, int curPositionInQueue, int totalInQueues);
void aslLogin2_PlayerIsThroughQueue(U64 loginCookie);

int aslLogin2_ClientDisconnect(NetLink *link, Login2State *loginState);
int aslLogin2_NewClientConnection(NetLink *netLink, Login2State *loginState);
int aslLogin2_WebProxyConnection(NetLink *netLink, Login2State *loginState);

void aslLogin2_InitStateMachine(void);

Login2State *aslLogin2_GetActiveLoginState(U64 loginCookie);
Login2State *aslLogin2_GetActiveLoginStateByAccountID(ContainerID accountID);
Login2State *aslLogin2_GetActiveLoginStateShortCookie(U32 shortCookie);
Login2State *aslLogin2_GetActiveLoginStateShortCookiePointer(void *shortCookiePointer);
void *aslLogin2_GetShortLoginCookieAsPointer(Login2State *loginState);
void aslLogin2_SetAccountID(Login2State *loginState, ContainerID accountID);
void aslLogin2_UpdateClusterStatus(void);
void aslLogin2_ReturnToCharacterSelect(Login2State *loginState);

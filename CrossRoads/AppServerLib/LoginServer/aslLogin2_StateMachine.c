/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslLogin2_StateMachine.h"
#include "aslLogin2_ClientComm.h"
#include "aslLogin2_ValidateLoginTicket.h"
#include "aslLogin2_RefreshGAD.h"
#include "aslLogin2_Booting.h"
#include "aslLogin2_GetCharacterChoices.h"
#include "aslLogin2_GetCharacterDetail.h"
#include "aslLogin2_Error.h"
#include "aslLogin2_Redirect.h"
#include "aslLogin2_DeleteCharacter.h"
#include "aslLogin2_RenameCharacter.h"
#include "aslLogin2_UGC.h"
#include "aslLogin2_CharacterCreation.h"
#include "aslLoginServer.h"
#include "aslLoginTokenParsing.h"
#include "Login2Common.h"
#include "Login2ServerCommon.h"
#include "Login2CharacterDetail.h"
#include "LoginCommon.h"
#include "aslLoginCharacterSelect.h"
#include "net.h"
#include "netipfilter.h"
#include "ResourceManager.h"
#include "InstancedStateMachine.h"
#include "sock.h"
#include "sysutil.h"
#include "crypt.h"
#include "file.h"
#include "Player.h"
#include "GamePermissionsCommon.h"
#include "GlobalTypes.h"
#include "utilitiesLib.h"
#include "ContinuousBuilderSupport.h"
#include "GameAccountData/GameAccountData.h"
#include "earray.h"
#include "GlobalTypes.h"
#include "ServerLib.h"
#include "species_common.h"
#include "aslLoginUGCProject.h"
#include "VirtualShard.h"
#include "EntityLib.h"
#include "Entity.h"
#include "mission_common.h"
#include "Alerts.h"
#include "Guild.h"
#include "Team.h"
#include "EntitySavedData.h"
#include "StringCache.h"
#include "ZoneMap.h"
#include "StringUtil.h"
#include "logging.h"
#include "UGCCommon.h"
#include "ShardCluster.h"
#include "AccountProxyCommon.h"

#include "AutoGen/aslLogin2_StateMachine_h_ast.h"
#include "AutoGen/Login2Common_h_ast.h"
#include "AutoGen/Player_h_ast.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/aslLogin2_ClientComm_h_ast.h"
#include "AutoGen/Login2CharacterDetail_h_ast.h"
#include "AutoGen/LoginCommon_h_ast.h"
#include "AutoGen/aslLogin2_Redirect_h_ast.h"
#include "AutoGen/Controller_autogen_RemoteFuncs.h"
#include "AutoGen/GlobalTypes_h_ast.h"
#include "AutoGen/ServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/accountnet_h_ast.h"
#include "AutoGen/AppServerLib_autotransactions_autogen_wrappers.h"

extern bool g_ForceDebugPriv;

static bool s_Login2DebugStates = true;

static StashTable s_ActiveLoginStatesByCookie = NULL;
static StashTable s_ActiveLoginStatesByAccountID = NULL;

bool g_Login2PrintStateChanges = false;
AUTO_CMD_INT(g_Login2PrintStateChanges, Login2PrintStateChanges) ACMD_CMDLINE;

// If present, don't allow people to log in if they don't have this permission
char *g_RequiredLoginPermission = NULL;

//__CATEGORY Login Server related settings
// If this setting is configured, the specified account permission is required to log in to the shard.
AUTO_CMD_ESTRING(g_RequiredLoginPermission, RequiredLoginPermission) ACMD_AUTO_SETTING(LoginServer, LOGINSERVER);

// Forces all map searches to be DebugPriv. Used by CB for production mode testing, obviously insanely unsafe.
bool g_ForceDebugPriv = false;
AUTO_CMD_INT(g_ForceDebugPriv, ForceDebugPriv);

// Forces the client to be treated as coming from an untrusted network.
bool g_ForceUntrustedClient = false;
AUTO_CMD_INT(g_ForceUntrustedClient, ForceUntrustedClient);

// Skips permission checks for trusted clients and just gives them access level.
bool g_SkipPermissionsForTrustedClient = false;
AUTO_CMD_INT(g_SkipPermissionsForTrustedClient, SkipPermissionsForTrustedClient) ACMD_AUTO_SETTING(LoginServer, LOGINSERVER);

// Login state machine timeout for states that involve intershard operations.
static U32 s_TimeoutIntershard = 30;
AUTO_CMD_INT(s_TimeoutIntershard, Login2TimeoutIntershard) ACMD_AUTO_SETTING(LoginServer, LOGINSERVER);

// Login state machine timeout for states that wait for user input.  Generally there should not be a timeout for these states.
static U32 s_TimeoutUser = 0;
AUTO_CMD_INT(s_TimeoutUser, Login2TimeoutUser) ACMD_AUTO_SETTING(LoginServer, LOGINSERVER);

// Login state machine timeout for states that wait for a packet from the client.
static U32 s_TimeoutClient = 10;
AUTO_CMD_INT(s_TimeoutClient, Login2TimeoutClient) ACMD_AUTO_SETTING(LoginServer, LOGINSERVER);

// Login state machine timeout for states that may involve communicating with the account server.
static U32 s_TimeoutAccount = 30;
AUTO_CMD_INT(s_TimeoutAccount, Login2TimeoutAccount) ACMD_AUTO_SETTING(LoginServer, LOGINSERVER);

// Login state machine timeout for states that involve a transaction.
static U32 s_TimeoutTransaction = 20;
AUTO_CMD_INT(s_TimeoutTransaction, Login2TimeoutTransaction) ACMD_AUTO_SETTING(LoginServer, LOGINSERVER);

// Login state machine timeout for the queued state.  Should generally not have a timeout.
static U32 s_TimeoutQueued = 0;
AUTO_CMD_INT(s_TimeoutQueued, Login2TimeoutQueued) ACMD_AUTO_SETTING(LoginServer, LOGINSERVER);

// Login state machine timeout for states that involve communicating with other servers within a shard.
static U32 s_TimeoutInshard = 20;
AUTO_CMD_INT(s_TimeoutInshard, Login2TimeoutInshard) ACMD_AUTO_SETTING(LoginServer, LOGINSERVER);

// Login state machine timeout for states that may involve waiting for a gameserver to start.
static U32 s_TimeoutGameserver = 60;
AUTO_CMD_INT(s_TimeoutGameserver, Login2TimeoutGameserver) ACMD_AUTO_SETTING(LoginServer, LOGINSERVER);

// This setting is a comma separated list of shards that are not available for character creation for AL0 players.
char *s_DisableCharCreationShardListStr = NULL;
AUTO_CMD_ESTRING(s_DisableCharCreationShardListStr, DisableCharCreationShardList) ACMD_AUTO_SETTING(LoginServer, LOGINSERVER) ACMD_CALLBACK(aslLogin2_DisableCharCreationShardListUpdate);

// If set this setting will enable addiction playtime limits for players who have this account server key/value on their account.
static char *s_AddictionPlaytimeLimitKeyValueName = NULL;
AUTO_CMD_ESTRING(s_AddictionPlaytimeLimitKeyValueName, AddictionPlaytimeLimitKeyValueName) ACMD_AUTO_SETTING(LoginServer, LOGINSERVER) ACMD_CALLBACK(aslLogin2_DisableCharCreationShardListUpdate);

// If set this setting will enable addiction playtime limits for players who DO NOT have this account server key/value on their account.
static char *s_NoAddictionPlaytimeLimitKeyValueName = NULL;
AUTO_CMD_ESTRING(s_NoAddictionPlaytimeLimitKeyValueName, NoAddictionPlaytimeLimitKeyValueName) ACMD_AUTO_SETTING(LoginServer, LOGINSERVER) ACMD_CALLBACK(aslLogin2_DisableCharCreationShardListUpdate);

// The time in seconds that the player must be logged off to reset their addiction playtime timer.
static U32 s_AddictionPlaytimeResetTime = 0;
AUTO_CMD_INT(s_AddictionPlaytimeResetTime, AddictionPlaytimeResetTime) ACMD_AUTO_SETTING(LoginServer, LOGINSERVER);

typedef struct Login2CookieStruct
{
    U64 loginCookie;
} Login2CookieStruct;

Login2CookieStruct *
aslLogin2_GetLoginCookieStruct(Login2State *loginState)
{
    Login2CookieStruct *cookieStruct = malloc(sizeof(Login2CookieStruct));
    cookieStruct->loginCookie = loginState->loginCookie;
    return cookieStruct;
}

// Convert a Login2CookieStruct into a Login2State. Free and zero the pointer to the Login2CookieStruct.
Login2State *
aslLogin2_LoginCookieStructToLoginState(Login2CookieStruct **ppCookieStruct)
{
    if ( ppCookieStruct && *ppCookieStruct )
    {
        Login2State *loginState;

        loginState = aslLogin2_GetActiveLoginState((*ppCookieStruct)->loginCookie);
        free(*ppCookieStruct);
        *ppCookieStruct = NULL;

        return loginState;
    }

    return NULL;
}


void *
aslLogin2_GetShortLoginCookieAsPointer(Login2State *loginState)
{
    U32 shortCookie = Login2_ShortenToken(loginState->loginCookie);

    return (void *)(intptr_t)shortCookie;
}

static bool
AddActiveLoginState(Login2State *loginState)
{
    if ( s_ActiveLoginStatesByCookie == NULL )
    {
        s_ActiveLoginStatesByCookie = stashTableCreateFixedSize(128, sizeof(U64));
    }

    return stashAddPointer(s_ActiveLoginStatesByCookie, &loginState->loginCookie, loginState, false);
}

static bool
AddActiveLoginStateByAccountID(Login2State *loginState)
{
    if ( s_ActiveLoginStatesByAccountID == NULL )
    {
        s_ActiveLoginStatesByAccountID = stashTableCreateInt(128);
    }

    return stashIntAddPointer(s_ActiveLoginStatesByAccountID, loginState->accountID, loginState, false);
}

Login2State *
aslLogin2_GetActiveLoginState(U64 loginCookie)
{
    Login2State *loginState = NULL;
    if ( !stashFindPointer(s_ActiveLoginStatesByCookie, &loginCookie, &loginState) )
    {
        return NULL;
    }

    return loginState;
}

Login2State *
aslLogin2_GetActiveLoginStateByAccountID(ContainerID accountID)
{
    Login2State *loginState = NULL;
    if ( !stashIntFindPointer(s_ActiveLoginStatesByAccountID, accountID, &loginState) )
    {
        return NULL;
    }

    return loginState;
}

Login2State *
aslLogin2_GetActiveLoginStateShortCookie(U32 shortCookie)
{
    U64 loginCookie = Login2_LengthenShortToken(shortCookie);

    if ( loginCookie == 0 )
    {
        return NULL;
    }
    return aslLogin2_GetActiveLoginState(loginCookie);
}

Login2State *
aslLogin2_GetActiveLoginStateShortCookiePointer(void *shortCookiePointer)
{
    return aslLogin2_GetActiveLoginStateShortCookie((U32)(intptr_t)shortCookiePointer);
}

static bool
RemoveActiveLoginState(Login2State *loginState)
{
    Login2State *tmpState = NULL;

    return stashRemovePointer(s_ActiveLoginStatesByCookie, &loginState->loginCookie, &tmpState);
}

static bool
RemoveActiveLoginStateByAccountID(Login2State *loginState)
{
    Login2State *tmpState = NULL;
    Login2State *oldState = NULL;

    // Only remove from the accountID to login state mapping if the given loginState is the one currently mapped.
    oldState = aslLogin2_GetActiveLoginStateByAccountID(loginState->accountID);
    if ( oldState == loginState )
    {
        return stashIntRemovePointer(s_ActiveLoginStatesByAccountID, loginState->accountID, &tmpState);
    }
    else
    {
        return true;
    }
}

static Login2CharacterChoice *
GetCharacterChoiceByID(Login2State *loginState, ContainerID characterID)
{
    if ( loginState->characterSelectionData && loginState->characterSelectionData->characterChoices )
    {
        return eaIndexedGetUsingInt(&loginState->characterSelectionData->characterChoices->characterChoices, characterID);
    }

    return NULL;
}

void
aslLogin2_SetAccountID(Login2State *loginState, ContainerID accountID)
{
    Login2State *oldLoginState;

    // See if there is another login state for the same account ID.
    oldLoginState = aslLogin2_GetActiveLoginStateByAccountID(accountID);
    if ( oldLoginState && oldLoginState != loginState )
    {
        // There is an old login state with the same account ID.  Fail the old login immediately.
        aslLogin2_FailLogin(oldLoginState, "Login2_DuplicateLogin");
        RemoveActiveLoginStateByAccountID(oldLoginState);
    }

    if ( loginState->accountID != 0 )
    {
        if ( loginState->accountID != accountID )
        {
            // Remove from the index with old account ID before changing the account ID.
            RemoveActiveLoginStateByAccountID(loginState);
        }
    }

    // Set to new ID.
    loginState->accountID = accountID;

    // Add to index of loginStates.
    AddActiveLoginStateByAccountID(loginState);
}

static char **s_DisabledCharCreationShards = NULL;

void
aslLogin2_DisableCharCreationShardListUpdate(CMDARGS)
{
    if ( s_DisabledCharCreationShards != NULL )
    {
        // Clear the old array of shard names.
        eaDestroyEString(&s_DisabledCharCreationShards);
        s_DisabledCharCreationShards = NULL;
    }

    if ( s_DisableCharCreationShardListStr != NULL && s_DisableCharCreationShardListStr[0] != '\0' )
    {
        // Parse the string from the auto setting into the array of shard names.
        estrTokenize(&s_DisabledCharCreationShards, ",", s_DisableCharCreationShardListStr);
    }
}

static EARRAY_OF(Login2ShardStatus) s_ShardStatusList = NULL;
static U32 s_LastShardStatusUpdateTime = 0;

// Returns true if the passed in shard name is in the list of shards with character creation disabled.
static bool
CharacterCreationDisabledOnShard(const char *shardName)
{
    int i;
    for ( i = eaSize(&s_DisabledCharCreationShards) - 1; i >= 0; i-- )
    {
        if ( stricmp(shardName, s_DisabledCharCreationShards[i]) == 0 )
        {
            return true;
        }
    }

    return false;
}

// Return true if we need to enforce addiction playtime limits on this player.
static bool
AddictionPlaytimeLimitRequired(CONST_EARRAY_OF(AccountProxyKeyValueInfoContainer) accountKeyValues)
{
    if ( estrLength(&s_AddictionPlaytimeLimitKeyValueName) > 0 )
    {
        return ( AccountProxyFindValueFromKeyContainer(accountKeyValues, s_AddictionPlaytimeLimitKeyValueName) != NULL );
    }

    if ( estrLength(&s_NoAddictionPlaytimeLimitKeyValueName) > 0 )
    {
        return ( AccountProxyFindValueFromKeyContainer(accountKeyValues, s_NoAddictionPlaytimeLimitKeyValueName) == NULL );
    }

    return false;
}

static U32
GetAddictionPlayTime(CONST_EARRAY_OF(AccountProxyKeyValueInfoContainer) accountKeyValues)
{
    char *playTimeString;
    S32 playTimeSeconds;

    playTimeString = AccountProxyFindValueFromKeyContainer(accountKeyValues, "AddictionPlayTime");
    playTimeSeconds = (playTimeString ? atoi(playTimeString) : 0);

    if ( playTimeSeconds < 0 )
    {
        playTimeSeconds = 0;
    }

    return playTimeSeconds;
}

static void
ResetAddictionPlayTime(ContainerID accountID)
{
    APSetKeyValueSimple(accountID, "AddictionPlayTime", 0, false, NULL, NULL);
}

void
aslLogin2_UpdateClusterStatus(void)
{
    Cluster_Overview *clusterOverview;
    int i;
    U32 curTime = timeSecondsSince2000();

    if ( curTime <= ( s_LastShardStatusUpdateTime + 5 ) )
    {
        return;
    }

    s_LastShardStatusUpdateTime = curTime;

    clusterOverview = GetShardClusterOverview_EvenIfNotInCluster();

    if ( s_ShardStatusList == NULL )
    {
        // Initialize all the data that never changes.
        eaIndexedEnable(&s_ShardStatusList, parse_Login2ShardStatus);
        for ( i = eaSize(&clusterOverview->ppShards) - 1; i >= 0; i-- )
        {
            Login2ShardStatus *shardStatus;
            ClusterShardSummary *shardSummary;

            shardSummary = clusterOverview->ppShards[i];
            shardStatus = StructCreate(parse_Login2ShardStatus);
            shardStatus->shardName = allocAddString(shardSummary->pShardName);
            shardStatus->isCurrent = ( shardSummary->eState == CLUSTERSHARDSTATE_THATS_ME );
            shardStatus->shardType = shardSummary->eShardType;

            eaIndexedAdd(&s_ShardStatusList, shardStatus);
        }
    }

    // Fill in data than can change.
    for ( i = eaSize(&clusterOverview->ppShards) - 1; i >= 0; i-- )
    {
        Login2ShardStatus *shardStatus;
        ClusterShardSummary *shardSummary;

        shardSummary = clusterOverview->ppShards[i];
        shardStatus = eaIndexedGetUsingString(&s_ShardStatusList, shardSummary->pShardName);

        devassertmsgf(shardStatus != NULL, "Unable to find shard status for shard %s.", shardSummary->pShardName);
        if ( shardStatus )
        {
            shardStatus->isReady = ( ( shardSummary->eState == CLUSTERSHARDSTATE_CONNECTED ) || shardStatus->isCurrent );

            if ( shardSummary->pMostRecentStatus )
            {
                shardStatus->queueSize = shardSummary->pMostRecentStatus->periodicStatus.iNumInMainQueue + shardSummary->pMostRecentStatus->periodicStatus.iNumInVIPQueue;
            }
            else
            {
                shardStatus->queueSize = 0;
            }

            // Search the list of shards that have character creation disabled and set the flag if the shard matches the list.
            shardStatus->creationDisabled = CharacterCreationDisabledOnShard(shardSummary->pShardName);
        }
    }
}

// Initialize the loginState and the login state machine.
int
aslLogin2_NewClientConnection(NetLink *netLink, Login2State *loginState)
{

    aslLogin2_ConfigureClientLoginNetLink(netLink);

    StructInit(parse_Login2State, loginState);
    loginState->loginCookie = Login2_GenerateRequestToken();
    loginState->netLink = netLink;
    loginState->clientIP = linkGetIp(netLink);
    loginState->isClientOnLocalHost = ( linkGetSAddr(loginState->netLink) == getHostLocalIp() );
    loginState->isClientOnTrustedIP = ipfIsTrustedIp(loginState->clientIP) && !g_ForceUntrustedClient;

    loginState->resourceCache = resServerCreateResourceCache(linkID(loginState->netLink));

    ISM_CreateMachine(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_INITIAL_CONNECTION);

    AddActiveLoginState(loginState);

    return 1;
}

int
aslLogin2_WebProxyConnection(NetLink *netLink, Login2State *loginState)
{
    int ret;

    // Set this so that the real connect function can know we are on a proxy.
    loginState->isProxy = true;
    
    ret = aslLogin2_NewClientConnection(netLink, loginState);

    // Set it again since aslLogin2_NewClientConnection() does a StructInit() on the loginState.
    loginState->isProxy = true;
    loginState->noPlayerBooting = true;

    return ret;
}

static void
CleanupFailedLoginCB(TransactionReturnVal *returnVal, void *userData)
{
    Login2State *loginState = aslLogin2_GetActiveLoginStateShortCookiePointer(userData);

    if ( returnVal->eOutcome != TRANSACTION_OUTCOME_SUCCESS )
    {
        aslLogin2_Log("aslLogin2_CleanupFailedLogin: failed to return player container to ObjectDB");
    }

    if ( loginState != NULL )
    {
        // Selected character is no longer on this server.
        loginState->selectedCharacterArrived = false;
    }
}

void
aslLogin2_CleanupFailedLogin(Login2State *loginState)
{
    // Clean up if the client disconnected while the player container was resident on the loginserver.
    if ( loginState->selectedCharacterID && objGetContainer(GLOBALTYPE_ENTITYPLAYER, loginState->selectedCharacterID) )
    {
        // The container is still on this server.
        if ( loginState->transferToGameserverComplete )
        {
            devassertmsg(false, "Player entity still on login server after it was transferred to a game server.");
        }
        else
        {
            // Return the container to the objectDB.
            objRequestContainerMove(objCreateManagedReturnVal(CleanupFailedLoginCB, aslLogin2_GetShortLoginCookieAsPointer(loginState)),
                GLOBALTYPE_ENTITYPLAYER, loginState->selectedCharacterID, objServerType(), objServerID(), GLOBALTYPE_OBJECTDB, 0);
        }
    }
}

int 
aslLogin2_ClientDisconnect(NetLink *link, Login2State *loginState)
{
    char *pDisconnectReason = NULL;

    // Log disconnect information.
    estrStackCreate(&pDisconnectReason);
    linkGetDisconnectReason(link, &pDisconnectReason);
    servLog(LOG_CLIENTSERVERCOMM, "LoginDisconnect", "link 0x%p error %u reason \"%s\" ip %s failed \"%s\" accountid %u",
        link, linkGetDisconnectErrorCode(link),
        pDisconnectReason,
        makeIpStr(loginState->clientIP),
        "", loginState->accountID);
    estrDestroy(&pDisconnectReason);

    if ( loginState->isThroughQueue )
    {
        ReportThatPlayerWhoWentThroughQueueHasLeft();
    }

    if ( loginState->selectedCharacterArrived )
    {
        // have to do some cleanup
        aslLogin2_CleanupFailedLogin(loginState);
    }

    if ( loginState->accountID )
    {
        RemoteCommand_aslAPCmdRemoveAccountLocation(GLOBALTYPE_ACCOUNTPROXYSERVER, 0, loginState->accountID);
    }

    loginState->netLink = NULL;
    accountDisplayNameRemoveQueue(loginState);

    ISM_DestroyMachine(LOGIN2_STATE_MACHINE, loginState);

    resServerDestroyResourceCache(loginState->resourceCache);

    RemoveActiveLoginState(loginState);
    RemoveActiveLoginStateByAccountID(loginState);

    StructDeInit(parse_Login2State, loginState);
    //printf("Client disconnected.\n");

    return 1;
}

static U32 s_ClientCRC = 0;

const char *
aslLogin2_GetAccountValuesFromKeyTemp(Login2State *loginState, const char *key)
{
    GameAccountData *gameAccountData = GET_REF(loginState->hGameAccountData);
    if ( gameAccountData )
    {
        int i;

        for ( i = eaSize(&gameAccountData->eaAccountKeyValues) - 1; i >= 0; i-- )
        {
            AccountProxyKeyValueInfoContainer *keyValue = gameAccountData->eaAccountKeyValues[i];

            if (stricmp(keyValue->pKey, key) == 0)
            {
                return keyValue->pValue;
            }
        }
    }

    return NULL;
}

// Compute and cache the CRC of the client executable.
static U32
GetExpectedClientCRC(void)
{
    char fileName[MAX_PATH];
    char directoryBuf[MAX_PATH];

    if ( s_ClientCRC == 0 )
    {
        getExecutableDir(directoryBuf);
        sprintf(fileName, "%s/%s", directoryBuf, "GameClient.exe");
        s_ClientCRC = cryptAdlerFile(fileName);
    }

    return s_ClientCRC;
}

static void
SetAccountLocation(ContainerID accountID)
{
    RemoteCommand_aslAPCmdSetAccountLocation(GLOBALTYPE_ACCOUNTPROXYSERVER, 0, accountID, GetAppGlobalType(), GetAppGlobalID());		
}

// Push the state on the debug state stack, which is used to record all states that this loginState has been through.
static void
PushStateForDebug(Login2State *loginState, char *stateName)
{
    loginState->currentStateName = stateName;

    if ( g_Login2PrintStateChanges )
    {
        printf("Login2: account %u changed to state %s\n", loginState->accountID, stateName);
    }

    if ( s_Login2DebugStates )
    {
        Login2StateDebugInfo *debugInfo;

        devassert( ISM_IsStateActive(LOGIN2_STATE_MACHINE, loginState, stateName) );

        debugInfo = StructCreate(parse_Login2StateDebugInfo);
        debugInfo->stateName = stateName;
        debugInfo->timeStarted = timeSecondsSince2000();

        eaPush(&loginState->debugStateHistory, debugInfo);
    }
}

// Return false if the loginState is not valid, or the client has disconnected.
static bool
ValidateLoginState(Login2State *loginState)
{
    if ( loginState == NULL )
    {
        return false;
    }

    if ( loginState->loginFailed )
    {
        return false;
    }

    if ( linkDisconnected(loginState->netLink) )
    {
        aslLogin2_FailLogin(loginState, "Login2_ClientDisconnected");
        return false;
    }

    return true;
}

static bool
Login2EnterStateCommon(Login2State *loginState, char *stateName, U32 timeout)
{
    // First check that the loginState is still good and the client is still connected.
    if ( !ValidateLoginState(loginState) )
    {
        return false;
    }

    // Set up timeout for current state.
    loginState->timeEnteredCurrentState = timeSecondsSince2000();
    loginState->timeoutForState = timeout;

    // Push state onto debugging stack.
    PushStateForDebug(loginState, stateName);

    return true;
}

static bool
Login2BeginFrameCommon(Login2State *loginState)
{
    // First check that the loginState is still good and the client is still connected.
    if ( !ValidateLoginState(loginState) )
    {
        return false;
    }

    // Check timeout in production mode.
    if ( loginState->timeoutForState && isProductionMode() && !g_isContinuousBuilder )
    {
        if ( ( loginState->timeEnteredCurrentState + loginState->timeoutForState ) < timeSecondsSince2000() )
        {
            aslLogin2_Log("Login timeout in state %s.  AccountID=%u, TimeInState=%u, Timeout=%u", 
                loginState->currentStateName, loginState->accountID, 
                timeSecondsSince2000() - loginState->timeEnteredCurrentState, loginState->timeoutForState);

            aslLogin2_FailLogin(loginState, "Login2_Timeout");
            return false;
        }
    }

    return true;
}

//////////////////////////////////////////////////////////////////////////
//
// Code for state LOGIN2_STATE_INITIAL_CONNECTION
//
//////////////////////////////////////////////////////////////////////////

// Returns true if login fails because shard is locked.
static bool
FailIfShardLocked(Login2State *loginState)
{
    // Fail login if the shard is locked and the account is not allowed to bypass the lock.
    if ( ControllerReportsShardLocked() )
    {
        if ( loginState->clientAccessLevel == 0 && !loginState->isClientOnLocalHost && !loginState->isClientOnTrustedIP )
        {
            aslLogin2_FailLogin(loginState, "Login2_ShardLocked");
            return true;
        }
        else
        {
            aslLogin2_Log("User from %s logged in despite shard being locked. Access level: %d. Local: %d. Trusted: %d",
                makeIpStr(linkGetIp(loginState->netLink)), loginState->clientAccessLevel, loginState->isClientOnLocalHost, loginState->isClientOnTrustedIP);

            aslLogin2_SendShardLockedNotification(loginState);
        }
    }

    return false;
}

static void 
Login2InitialConnection_Enter(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2EnterStateCommon(loginState, LOGIN2_STATE_INITIAL_CONNECTION, s_TimeoutClient) )
    {
        return;
    }

    // Send game permissions to the client.
    aslLogin2_SendGamePermissions(loginState);
}

static void 
Login2InitialConnection_BeginFrame(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2BeginFrameCommon(loginState) )
    {
        return;
    }

    // Stay in this state until the begin login packet is received.
    if ( loginState->beginLoginPacketData )
    {
        // Process the begin login packet.
        BeginLoginPacketData *beginLoginPacketData = loginState->beginLoginPacketData;

        if ( ( beginLoginPacketData->clientCRC != GetExpectedClientCRC() ) && !gLoginServerState.bAllowVersionMismatch && !loginState->isClientOnTrustedIP && !loginState->isProxy)
        {
            // if the client CRC does not match, the allow version mismatch flag is not set and the client is not on the same computer, then the client is not valid.
            aslLogin2_FailLogin(loginState, "Login2_VersionMismatch");
            return;
        }

        // In dev mode and client asks for no link timeout.
        if ( beginLoginPacketData->noTimeout && isDevelopmentMode() )
        {
            linkSetTimeout(loginState->netLink,0.0);
        }

        // Save client language.
        loginState->clientLanguageID = beginLoginPacketData->clientLanguageID;

        // Save the affiliate.  String is pooled.
        loginState->affiliate = beginLoginPacketData->affiliate;

        if ( beginLoginPacketData->ticketID != 0 )
        {
            // Doing standard authentication using a AccountTicket.
            aslLogin2_SetAccountID(loginState, beginLoginPacketData->accountID);
            loginState->ticketID = beginLoginPacketData->ticketID;
            loginState->machineID = StructAllocString(beginLoginPacketData->machineID);

            // Switch to account ticket validation state.
            ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_VALIDATE_TICKET);
        }
        else if ( beginLoginPacketData->accountName && beginLoginPacketData->accountName[0] && loginState->isClientOnTrustedIP )
        {
            // This is used for dev mode, builders, etc.
            aslLogin2_SetAccountID(loginState, 1);
            loginState->accountName = StructAllocString(beginLoginPacketData->accountName);
            loginState->clientAccessLevel = 9;
            loginState->playerType = kPlayerType_Premium;

            // Go directly to game account data refresh state.
            ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_REFRESH_GAME_ACCOUNT_DATA);
        }
        else
        {
            aslLogin2_FailLogin(loginState, "Login2_NoTicket");
            return;
        }
    }
    else if ( loginState->redirectLoginPacketData )
    {
        RedirectLoginPacketData *redirectLoginPacketData = loginState->redirectLoginPacketData;

        // In dev mode and client asks for no link timeout.
        if ( redirectLoginPacketData->noTimeout && isDevelopmentMode() )
        {
            linkSetTimeout(loginState->netLink, 0.0);
        }

        // Fill loginState with data from redirect.
        if ( !aslLogin2_CreateLoginStateForRedirect(loginState) )
        {
            // Redirect failed.
            aslLogin2_FailLogin(loginState, "Login2_RedirectFailed");
            return;
        }

        aslLogin2_Log("Got login redirect for account %u", loginState->accountID);

        if ( FailIfShardLocked(loginState) )
        {
            return;
        }

        // Go directly to game account data refresh state.
        ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_REFRESH_GAME_ACCOUNT_DATA);
    }
}

//////////////////////////////////////////////////////////////////////////
//
// Code for state LOGIN2_STATE_VALIDATE_TICKET
//
//////////////////////////////////////////////////////////////////////////

static void
ValidateTicketCompletionCB(ContainerID accountID, AccountTicket *accountTicket, bool success, const char *userErrorMessageKey, Login2CookieStruct *cookieStruct)
{
    Login2State *loginState;

    loginState = aslLogin2_LoginCookieStructToLoginState(&cookieStruct);
    if ( !ValidateLoginState(loginState) )
    {
        return;
    }

    if ( !success )
    {
        aslLogin2_FailLogin(loginState, userErrorMessageKey);
        return;
    }

    if ( accountTicket == NULL )
    {
        // Should never really get here, but check anyway because the state machine will stall out if a NULL account ticket is returned.
        aslLogin2_FailLogin(loginState, "Login2_NoAccountTicket");
    }

    if ( !loginState->noPlayerBooting )
    {
        // Boot the player from all other login servers in the cluster.
        aslLogin2_BootAccountFromAllLoginServers(accountID, objServerID());
    }

    // Save the account ticket. The state machine will take over from here.
    loginState->accountTicket = accountTicket;
}

static void 
Login2ValidateTicket_Enter(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2EnterStateCommon(loginState, LOGIN2_STATE_VALIDATE_TICKET, s_TimeoutAccount) )
    {
        return;
    }

    aslLogin2_ValidateLoginTicket(loginState->accountID, loginState->ticketID, ValidateTicketCompletionCB, aslLogin2_GetLoginCookieStruct(loginState));
}

static void 
Login2ValidateTicket_BeginFrame(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2BeginFrameCommon(loginState) )
    {
        return;
    }

    // Stay in this state until the account ticket shows up.
    if ( loginState->accountTicket )
    {
        AccountTicket *accountTicket = loginState->accountTicket;
        U32 currentTime = timeSecondsSince2000();

        // Once the account ticket has arrived, we need to either fail login or switch to a new state.

        // Make sure ticket hasn't expired.
        if ( accountTicket->uExpirationTime < currentTime )
        {
            aslLogin2_FailLogin(loginState, "Login2_LoginTicketExpired");
            return;
        }

        // Make sure that the client wasn't trying to spoof the account ID.
        if ( accountTicket->accountID != loginState->accountID )
        {
            ErrorDetailsf("client account ID=%u, account server account ID=%u, client IP Address=0x%x", 
                loginState->accountID, accountTicket->accountID, loginState->clientIP);
            Errorf("Client tried to log in with incorrect account ID");
            aslLogin2_FailLogin(loginState, "Login2_InternalError");
        }

        // Copy account details from the ticket.
        loginState->accountName = StructAllocString(accountTicket->accountName);
        if ( accountTicket->displayName )
        {
            loginState->accountDisplayName = StructAllocString(accountTicket->displayName);
        }
        if ( accountTicket->pwAccountName )
        {
            loginState->pweAccountName = StructAllocString(accountTicket->pwAccountName);
        }

        if ( loginState->accountName[0] == 0 )
        {
            aslLogin2_FailLogin(loginState, "Login2_LoginTicketNoAccountName");
            return;
        }

        if ( ( isDevelopmentMode() || g_SkipPermissionsForTrustedClient ) && loginState->isClientOnTrustedIP )
        {
            // Check debug options used in dev mode.
            if (g_bDebugF2P)
            {
                loginState->playerType = kPlayerType_Standard;
            }
            else if (g_bDebugF2PPremium)
            {
                loginState->playerType = kPlayerType_Premium;
            }
            else
            {
                // Trusted IPs get the best permissions.
                loginState->clientAccessLevel = 9;
                loginState->isLifetime = true;
                loginState->playerType = kPlayerType_Premium;
            }
        }
        else
        {
            AccountPermissionStruct* productPermissions;
            char **tokens = NULL;
            bool tokensAllowPlay;
            bool unrestrictedLogin = false;

            productPermissions = findGamePermissionsByShard(loginState->accountTicket, GetProductName(), GetShardCategoryFromShardInfoString());
            if ( productPermissions == NULL )
            {
                // If there are not specific permissions for this shard, then try getting permissions for all shards.
                productPermissions = findGamePermissionsByShard(loginState->accountTicket, GetProductName(), "all");
            }

            if ( productPermissions == NULL )
            {
                // Couldn't find any permissions.
                aslLogin2_FailLogin(loginState, "Login2_LoginTicketNoPermissions");
                return;
            }

            if (productPermissions->uFlags & ACCOUNTPERMISSION_BANNED)
            {
                // The account has been banned.
                aslLogin2_FailLogin(loginState, "Login2_AccountSuspended");
                return;
            }

            // Set access level.
            loginState->clientAccessLevel = productPermissions->iAccessLevel;

            // Set player type based on access level or product permissions.
            if ( ( loginState->clientAccessLevel >= ACCESS_GM ) || ( permissionsGame(productPermissions, ACCOUNT_PERMISSION_PREMIUM) ) )
            {
                loginState->playerType = kPlayerType_Premium;
            }
            else
            {
                loginState->playerType = kPlayerType_Standard;
            }

            // Extract other flags from the product permissions.
            loginState->isLifetime = permissionsGame(productPermissions, ACCOUNT_PERMISSION_LIFETIME_SUB);
            loginState->isPress = permissionsGame(productPermissions, ACCOUNT_PERMISSION_PRESS);
            loginState->ignoreQueue = permissionsGame(productPermissions, ACCOUNT_PERMISSION_IGNORE_QUEUE);
            loginState->isQueueVIP = permissionsGame(productPermissions, ACCOUNT_PERMISSION_VIP_QUEUE);

            // Players with elevated access level or on a trusted network can bypass the login queue.
            if ( loginState->clientAccessLevel > 0 || loginState->isClientOnTrustedIP )
            {
                loginState->ignoreQueue = true;
            }

            // See if product permission tokens allow play at this time.
            permissionsParseTokenString(&tokens, productPermissions);
            tokensAllowPlay = aslLogin2_DoPermissionTokensAllowPlay(tokens);
            eaDestroyEx(&tokens, freeWrapper);

            // Players with elevated access level or trusted IP can bypass login restrictions.
            unrestrictedLogin = ( loginState->clientAccessLevel >= ACCESS_GM || loginState->isClientOnTrustedIP );

            // Players with elevated access level or trusted IP can bypass playtime token restrictions.
            if ( unrestrictedLogin )
            {
                tokensAllowPlay = true;
            }

            if ( !tokensAllowPlay )
            {
                // Player does not have a token that will allow them to play right now.
                if( permissionsGame(productPermissions, ACCOUNT_PERMISSION_UGC_ALLOWED) )
                {
                    // They do have the special permission that allows them to still use UGC virtual shard characters.
                    loginState->UGCCharacterPlayOnly = true;
                }
                else
                {
                    // Not allowed to play now.
                    aslLogin2_FailLogin(loginState, "Login2_NotAllowedRightNow");
                    return;
                }
            }

            if ( !unrestrictedLogin )
            {
                // If a required login permission is configured and the player doesn't have it, then their login will fail.
                if ( ( g_RequiredLoginPermission != NULL ) && ( g_RequiredLoginPermission[0] != '\0' ) && !permissionsGame(productPermissions, g_RequiredLoginPermission) )
                {
                    aslLogin2_FailLogin(loginState, "Login2_RequiredLoginPermissionMissing");
                    return;
                }

                // Check to see if permissions have time limitations.
                if ( !permissionsCheckStartTime(productPermissions, currentTime) || !permissionsCheckEndTime(productPermissions, currentTime) )
                {
                    aslLogin2_FailLogin(loginState, "Login2_NotAllowed");
                    return;
                }
            }

            // Save any GamePermissions that are enabled by account permissions.
            EARRAY_FOREACH_BEGIN(g_GamePermissions.eaPermissions, permissionIndex);
            {
                GamePermissionDef *gamePermissionDef = g_GamePermissions.eaPermissions[permissionIndex];

                if ( permissionsGame(productPermissions, gamePermissionDef->pchName) )
                {
                    // Note - GamePermissionDef name is pooled.
                    eaPush(&loginState->gamePermissionsFromAccountPermissions, gamePermissionDef->pchName);
                }
            }
            EARRAY_FOREACH_END;
        }

        // Fail login if the shard is locked and the account is not allowed to bypass the lock.
        if ( FailIfShardLocked(loginState) )
        {
            return;
        }

        // Builder can skip invalid display name and account guard checks.
        if ( !g_isContinuousBuilder )
        {
            if ( loginState->accountTicket->bInvalidDisplayName )
            {
                // LOGIN2TODO - handle invalid display name
            } 
        
            if ( !gbMachineLockDisable && loginState->accountTicket->bMachineRestricted && !loginState->isClientOnTrustedIP )
            {
                // Fail if we don't have a machine ID.
                if ( ( loginState->machineID == NULL ) || ( loginState->machineID[0] == 0 ) )
                {
                    aslLogin2_FailLogin(loginState, "Login2_NoMachineID");
                    return;
                }

                if ( loginState->accountTicket->bSavingNext )
                {
                    // Switch to get machine name state.
                    ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_GET_MACHINE_NAME_FROM_CLIENT);
                    return;
                }
                else
                {
                    // Switch to generate one time code state.
                    ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_GENERATE_ONE_TIME_CODE);
                    return;
                }
            }
        }

        // Continue login
        ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_REFRESH_GAME_ACCOUNT_DATA);
    }
}

//////////////////////////////////////////////////////////////////////////
//
// Code for state LOGIN2_STATE_GET_MACHINE_NAME_FROM_CLIENT
//
//////////////////////////////////////////////////////////////////////////

static void 
Login2GetMachineName_Enter(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    // Send packet to client requesting machine name.
    Packet *pak;

    if ( !Login2EnterStateCommon(loginState, LOGIN2_STATE_GET_MACHINE_NAME_FROM_CLIENT, s_TimeoutUser) )
    {
        return;
    }

    pak = pktCreate(loginState->netLink, LOGINSERVER_TO_CLIENT_SAVENEXTMACHINE);
    pktSend(&pak);
}

static void 
Login2GetMachineName_BeginFrame(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2BeginFrameCommon(loginState) )
    {
        return;
    }

    // Stay in this state until the machine name shows up.
    if ( loginState->saveNextMachinePacketData )
    {
        // Switch to set machine name state.
        ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_SET_MACHINE_NAME);
    }
}

//////////////////////////////////////////////////////////////////////////
//
// Code for state LOGIN2_STATE_SET_MACHINE_NAME
//
//////////////////////////////////////////////////////////////////////////

static void
SetMachineNameCompletionCB(ContainerID accountID, bool success, const char *userErrorMessageKey, Login2CookieStruct *cookieStruct)
{
    Login2State *loginState;

    loginState = aslLogin2_LoginCookieStructToLoginState(&cookieStruct);
    if ( !ValidateLoginState(loginState) )
    {
        return;
    }

    if ( !success )
    {
        aslLogin2_FailLogin(loginState, userErrorMessageKey);
        return;
    }

    // We can continue login once the machine name is saved.
    ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_REFRESH_GAME_ACCOUNT_DATA);
}

static void 
Login2SetMachineName_Enter(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2EnterStateCommon(loginState, LOGIN2_STATE_SET_MACHINE_NAME, s_TimeoutAccount) )
    {
        return;
    }

    aslLogin2_SetMachineName(loginState->accountID, loginState->machineID, loginState->saveNextMachinePacketData->machineName, loginState->clientIP, SetMachineNameCompletionCB, aslLogin2_GetLoginCookieStruct(loginState));
}

//////////////////////////////////////////////////////////////////////////
//
// Code for state LOGIN2_STATE_GENERATE_ONE_TIME_CODE
//
//////////////////////////////////////////////////////////////////////////

static void
GenerateOneTimeCodeCompletionCB(ContainerID accountID, bool success, const char *userErrorMessageKey, Login2CookieStruct *cookieStruct)
{
    Packet *pak;
    Login2State *loginState;

    loginState = aslLogin2_LoginCookieStructToLoginState(&cookieStruct);
    if ( !ValidateLoginState(loginState) )
    {
        return;
    }

    if ( !success )
    {
        aslLogin2_FailLogin(loginState, userErrorMessageKey);
        return;
    }

    // Tell the client we are waiting for the one time code.
    pak = pktCreate(loginState->netLink, LOGINSERVER_TO_CLIENT_REQUIRE_ONETIMECODE);
    pktSendBool(pak, true); // to indicate that it really needs the OTC
    pktSend(&pak);

    // We just stay in this state waiting for the one time code to be returned by the client.
}

static void 
Login2GenerateOneTimeCode_Enter(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2EnterStateCommon(loginState, LOGIN2_STATE_GENERATE_ONE_TIME_CODE, s_TimeoutUser) )
    {
        return;
    }

    aslLogin2_GenerateOneTimeCode(loginState->accountID, loginState->machineID, loginState->clientIP, GenerateOneTimeCodeCompletionCB, aslLogin2_GetLoginCookieStruct(loginState));
}

static void 
Login2GenerateOneTimeCode_BeginFrame(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2BeginFrameCommon(loginState) )
    {
        return;
    }

    // Stay in this state until the one time code shows up.
    if ( loginState->oneTimeCodePacketData )
    {
        OneTimeCodePacketData *oneTimeCodePacketData = loginState->oneTimeCodePacketData;

        if ( ( oneTimeCodePacketData->oneTimeCode == NULL ) || ( oneTimeCodePacketData->oneTimeCode[0] == 0 ) )
        {
            aslLogin2_FailLogin(loginState, "Login2_InvalidOneTimeCode");
            return;
        }

        if ( ( oneTimeCodePacketData->machineName == NULL ) || ( oneTimeCodePacketData->machineName[0] == 0 ) )
        {
            aslLogin2_FailLogin(loginState, "Login2_InvalidOneMachineName");
            return;
        }

        // Switch to validate one time code state.
        ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_VALIDATE_ONE_TIME_CODE);
    }
}

//////////////////////////////////////////////////////////////////////////
//
// Code for state LOGIN2_STATE_VALIDATE_ONE_TIME_CODE
//
//////////////////////////////////////////////////////////////////////////

static void
ValidateOneTimeCodeCompletionCB(ContainerID accountID, bool success, const char *userErrorMessageKey, Login2CookieStruct *cookieStruct)
{
    Login2State *loginState;

    loginState = aslLogin2_LoginCookieStructToLoginState(&cookieStruct);
    if ( !ValidateLoginState(loginState) )
    {
        return;
    }

    if ( !success )
    {
        aslLogin2_FailLogin(loginState, userErrorMessageKey);
        return;
    }
    
    // We can continue login once the one time code is validated.
    ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_REFRESH_GAME_ACCOUNT_DATA);
}

static void 
Login2ValidateOneTimeCode_Enter(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    OneTimeCodePacketData *oneTimeCodePacketData = loginState->oneTimeCodePacketData;

    if ( !Login2EnterStateCommon(loginState, LOGIN2_STATE_VALIDATE_ONE_TIME_CODE, s_TimeoutAccount) )
    {
        return;
    }

    aslLogin2_ValidateOneTimeCode(loginState->accountID, loginState->machineID, oneTimeCodePacketData->oneTimeCode, oneTimeCodePacketData->machineName, loginState->clientIP, ValidateOneTimeCodeCompletionCB, aslLogin2_GetLoginCookieStruct(loginState));
}

//////////////////////////////////////////////////////////////////////////
//
// Code for state LOGIN2_STATE_REFRESH_GAME_ACCOUNT_DATA
//
//////////////////////////////////////////////////////////////////////////

static void
RefreshGameAccountDataCompletionCB(ContainerID accountID, bool succeeded, U32 highestLevel, U32 numCharacters, U32 lastLogoutTime, Login2CookieStruct *cookieStruct)
{
    char idBuf[128];
    Login2State *loginState;

    loginState = aslLogin2_LoginCookieStructToLoginState(&cookieStruct);
    if ( !ValidateLoginState(loginState) )
    {
        return;
    }

    if ( !succeeded )
    {
        aslLogin2_FailLogin(loginState, "Login2_RefreshGADFailed");
        return;
    }

    loginState->numCharactersFromAccountServer = numCharacters;
    loginState->maxLevelFromAccountServer = highestLevel;

    // If last logout time is in the future, then set it to now.  This prevents underflow in time math later on.
    if ( lastLogoutTime > timeSecondsSince2000() )
    {
        lastLogoutTime = timeSecondsSince2000();
    }
    loginState->lastLogoutTime = lastLogoutTime;

    // Since the game account data is set up now, we can set a reference to it.
    SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GAMEACCOUNTDATA), ContainerIDToString(loginState->accountID, idBuf), loginState->hGameAccountData);
}

static void 
Login2RefreshGameAccountData_Enter(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2EnterStateCommon(loginState, LOGIN2_STATE_REFRESH_GAME_ACCOUNT_DATA, s_TimeoutAccount) )
    {
        return;
    }

    // Reset these.  Should be filled in when refresh is done.
    loginState->numCharactersFromAccountServer = 0;
    loginState->maxLevelFromAccountServer = 0;

    // Tell the account server where we are.
    SetAccountLocation(loginState->accountID);

    // Select starting game permissions based on player type.
    if ( ( loginState->playerType != kPlayerType_Standard ) && ( loginState->playerType != kPlayerType_Premium ) )
    {
        // should never get here.
        devassertmsg(0, "Login2RefreshGameAccountData_Enter: got invalid player type.");
        aslLogin2_FailLogin(loginState, "Login2_InternalError");
        return;
    }

    // Kick off the game account data refresh.
    aslLogin2_RefreshGAD(loginState->accountID, loginState->playerType, loginState->isLifetime, loginState->isPress, 0, loginState->gamePermissionsFromAccountPermissions, RefreshGameAccountDataCompletionCB, aslLogin2_GetLoginCookieStruct(loginState));
}

static void 
Login2RefreshGameAccountData_BeginFrame(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    GameAccountData *gameAccountData;

    if ( !Login2BeginFrameCommon(loginState) )
    {
        return;
    }

    gameAccountData = GET_REF(loginState->hGameAccountData);

    // Stay in this state until the game account data arrives.
    if ( gameAccountData )
    {
        // Set a flag indicating that the player has UGC project slots.
        loginState->playerHasUGCProjectSlots = ( ( Login2_UGCGetProjectMaxSlots(gameAccountData) > 0 ) || ( Login2_UGCGetProjectMaxSlots(gameAccountData) ) );

        // Send basic info to the client now that we have GameAccountData.
        aslLogin2_SendLoginInfo(loginState);

        if ( loginState->afterRedirect )
        {
            // Go directly to character select state if this is following a redirect.
            ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_CHARACTER_SELECT);
            
            // Tell the client we are done with redirect.
            aslLogin2_SendRedirectDone(loginState);
        }
        else
        {
            ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_GET_CHARACTER_LIST);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
//
// Code for state LOGIN2_STATE_GET_CHARACTER_LIST
//
//////////////////////////////////////////////////////////////////////////

void
Login2_RemoveUsedCharacterSlots(Login2CharacterChoices *characterChoices, AvailableCharSlots *availableCharSlots)
{
    int i;
    const char *allegianceName;

    for ( i = eaSize(&characterChoices->characterChoices) - 1; i >= 0; i-- )
    {
        Login2CharacterChoice *characterChoice = characterChoices->characterChoices[i];

        allegianceName = Login2_GetAllegianceFromCharacterChoice(characterChoice);
        if ( !CharSlots_MatchSlot(availableCharSlots, characterChoice->virtualShardID, allegianceName, true) )
        {
            ErrorDetailsf("%s: characterID=%u, virtualShardID=%d, allegiance=%s", characterChoice->savedName, characterChoice->containerID, characterChoice->virtualShardID, allegianceName);
            Errorf("%s: Could not find character slot for existing character", __FUNCTION__);
        }
    }
}

static Login2CharacterSelectionData *
CreateCharacterSelectionData(Login2CharacterChoices *characterChoices, GameAccountData *gameAccountData, S32 clientAccessLevel, bool UGCLoginOnly, const char *publicAccountName, const char *privateAccountName, U32 numCharactersFromAccountServer)
{
    static char **s_tempNames = NULL;
    static S32 *s_tempValues = NULL;
    Login2CharacterSelectionData *characterSelectionData;
    const char *keyValue;
    DictionaryEArrayStruct *deas;
    int i;
    char idBuf[128];
    PossibleVirtualShard *virtualShardInfo;
    RefDictIterator iterator;
    VirtualShard *virtualShard;
    const char *valueString;
    int tutorialCompletionValue = 0;
    int totalUnrestrictedSlots;
    int usedUnrestrictedSlots;

    characterSelectionData = StructCreate(parse_Login2CharacterSelectionData);
    characterSelectionData->characterChoices = characterChoices;

    characterSelectionData->accountID = characterChoices->accountID;
    characterSelectionData->publicAccountName = StructAllocString(publicAccountName);
    characterSelectionData->privateAccountName = StructAllocString(privateAccountName);

    // Compute total character slots.
    characterSelectionData->availableCharSlots = 
        BuildAvailableCharacterSlots(gameAccountData, clientAccessLevel);

    totalUnrestrictedSlots = characterSelectionData->availableCharSlots->numUnrestrictedSlots;

    // Remove used slots from the list.
    Login2_RemoveUsedCharacterSlots(characterChoices, characterSelectionData->availableCharSlots);

    usedUnrestrictedSlots = totalUnrestrictedSlots - characterSelectionData->availableCharSlots->numUnrestrictedSlots;

    if ( gConf.bUseSimplifiedMultiShardCharacterSlotRules )
    {
        // Special handling for simplified multi-shard character slot rules.
        if ( eaSize(&characterChoices->missingShardNames) > 0 )
        {
            // Shard is down.  Update the number unrestricted slots available based on the number of characters using unrestricted 
            //  character slots from the account server.
            characterSelectionData->availableCharSlots->numUnrestrictedSlots = totalUnrestrictedSlots - numCharactersFromAccountServer;
        }
        else
        {
            // No shards down.  If the number of unrestricted character slots used does not match the account server, then update the account server.
            if ( usedUnrestrictedSlots != (int)numCharactersFromAccountServer )
            {
                ErrorDetailsf("accountID=%u, slotsFromAccountServer=%d, slotsUsed=%d", gameAccountData->iAccountID, numCharactersFromAccountServer, usedUnrestrictedSlots);
                Errorf("Number of used unrestricted character slots on the account server is out of sync with the game");
                RemoteCommand_aslAPCmdSetNumCharacters(GLOBALTYPE_ACCOUNTPROXYSERVER, 0, gameAccountData->iAccountID, usedUnrestrictedSlots, false);
            }
        }
    }

    // Set UGC data.
    characterSelectionData->UGCLoginOnly = UGCLoginOnly;

    // Set up reference to game account data.
    SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GAMEACCOUNTDATA), ContainerIDToString(gameAccountData->iAccountID, idBuf), characterSelectionData->hGameAccountData);

    // Generate allegiance unlocks for player.
    eaClearFast(&s_tempNames);
    eaiClearFast(&s_tempValues);

    characterSelectionData->unlockAllegianceFlags = 0;
    DefineFillAllKeysAndValues(UnlockedAllegianceFlagsEnum,&s_tempNames,&s_tempValues);
    for ( i = eaSize(&s_tempNames) - 1; i > 0; i-- )
    {
        keyValue = gad_GetAttribString(gameAccountData, s_tempNames[i]);
        if ( keyValue )
        {
            ANALYSIS_ASSUME(keyValue != NULL);
            if ( atoi(keyValue) > 0 )
            {
                characterSelectionData->unlockAllegianceFlags |= s_tempValues[i];
            }
        }
    }

    // Generate create unlock flags for the player.
    eaClearFast(&s_tempNames);
    eaiClearFast(&s_tempValues);

    characterSelectionData->unlockCreateFlags = 0;
    DefineFillAllKeysAndValues(UnlockedCreateFlagsEnum,&s_tempNames,&s_tempValues);
    for ( i = eaSize(&s_tempNames) - 1; i > 0; i-- )
    {
        keyValue = gad_GetAttribString(gameAccountData, s_tempNames[i]);
        if ( keyValue )
        {
            ANALYSIS_ASSUME(keyValue);
            if ( atoi(keyValue) > 0 )
            {
                characterSelectionData->unlockCreateFlags |= s_tempValues[i];
            }
        }
    }

    // Generate species unlocks for the player.
    deas = resDictGetEArrayStruct("Species");
    eaDestroyStruct(&characterSelectionData->unlockedSpecies, parse_LoginSpeciesUnlockedRef);
    for( i = eaSize(&deas->ppReferents) - 1; i >= 0; i-- )
    {
        SpeciesDef *species = (SpeciesDef*)deas->ppReferents[i];
        if ( (!species->pcUnlockCode) || (!*species->pcUnlockCode) )
        {
            continue;
        }
        keyValue = gad_GetAttribString(gameAccountData, species->pcUnlockCode);
        if( keyValue && atoi(keyValue) > 0 )
        {
            LoginSpeciesUnlockedRef *loginSpeciesUnlockedRef = StructCreate(parse_LoginSpeciesUnlockedRef);
            SET_HANDLE_FROM_REFERENT("Species", species, loginSpeciesUnlockedRef->hSpecies);
            eaPush(&characterSelectionData->unlockedSpecies, loginSpeciesUnlockedRef);
        }
    }

    // Add virtual shard info for base virtual shard.
    if ( aslCanPlayerAccessVirtualShard(gameAccountData->eaAccountKeyValues, 0) )
    {
        virtualShardInfo = StructCreate(parse_PossibleVirtualShard);
        virtualShardInfo->iContainerID = 0;
        virtualShardInfo->bDisabled = false;
        virtualShardInfo->bUGCShard = false;
        virtualShardInfo->bAvailable = true;
        eaPush(&characterSelectionData->virtualShardInfos, virtualShardInfo);
    }

    // Add virtual shard info for any other virtual shards.
    RefSystem_InitRefDictIterator(GlobalTypeToCopyDictionaryName(GLOBALTYPE_VIRTUALSHARD), &iterator);
    while ( ( virtualShard = RefSystem_GetNextReferentFromIterator(&iterator) ) )
    {
        if ( aslCanPlayerAccessVirtualShard(gameAccountData->eaAccountKeyValues, virtualShard->id) )
        {
            virtualShardInfo = StructCreate(parse_PossibleVirtualShard);
            virtualShardInfo->iContainerID = virtualShard->id;
            virtualShardInfo->bDisabled = virtualShard->bDisabled;
            virtualShardInfo->bUGCShard = virtualShard->bUGCShard;
            virtualShardInfo->bAvailable = ( !virtualShard->bUGCShard ) || ( clientAccessLevel > 0 ) || Login2_UGCGetProjectMaxSlots(gameAccountData) || Login2_UGCGetSeriesMaxSlots(gameAccountData);
            virtualShardInfo->pName = StructAllocString(virtualShard->pName);
            eaPush(&characterSelectionData->virtualShardInfos, virtualShardInfo);
        }
    }

    // Set the flag to indicate that the player has completed the tutorial.
    valueString = AccountProxyFindValueFromKey((CONST_EARRAY_OF(AccountProxyKeyValueInfo))gameAccountData->eaAccountKeyValues, GetAccountTutorialDoneKey());
    if ( valueString )
    {
        tutorialCompletionValue = atoi(valueString);
    }

    if ( tutorialCompletionValue > 0 )
    {
        characterSelectionData->hasCompletedTutorial = true;
    }

    return characterSelectionData;
}

static void
GetCharacterListCompletionCB(Login2CharacterChoices *characterChoices, Login2CookieStruct *cookieStruct)
{
    GameAccountData *gameAccountData;
    Login2State *loginState;

    loginState = aslLogin2_LoginCookieStructToLoginState(&cookieStruct);
    if ( !ValidateLoginState(loginState) )
    {
        return;
    }

    gameAccountData = GET_REF(loginState->hGameAccountData);
    if ( gameAccountData == NULL )
    {
        aslLogin2_FailLogin(loginState, "Login2_InternalError");
    }

    // Build the character selection data struct that will be sent to the client.
    loginState->characterSelectionData = CreateCharacterSelectionData(characterChoices, gameAccountData, loginState->clientAccessLevel, loginState->UGCCharacterPlayOnly, loginState->accountDisplayName, loginState->accountName, loginState->numCharactersFromAccountServer);
}

static void 
Login2GetCharacterList_Enter(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    GameAccountData *gameAccountData;
    if ( !Login2EnterStateCommon(loginState, LOGIN2_STATE_GET_CHARACTER_LIST, s_TimeoutIntershard) )
    {
        return;
    }

    // Make sure there there is not some pre-existing character selection data.  We don't have to preserve selection data from a redirect
    //  because redirect doesn't use this state.
    if ( loginState->characterSelectionData )
    {
        StructDestroySafe(parse_Login2CharacterSelectionData, &loginState->characterSelectionData);
    }

    // Check if player is operating under anti-addiction rules, and whether they have exceeded their play time limit.
    gameAccountData = GET_REF(loginState->hGameAccountData);
    if ( AddictionPlaytimeLimitRequired(gameAccountData->eaAccountKeyValues) )
    {
        U32 loggedOffTime = timeSecondsSince2000() - loginState->lastLogoutTime;
        U32 playedTime;

        if ( loggedOffTime >= s_AddictionPlaytimeResetTime )
        {
            // Player has been logged off long enough to reset their play timer.
            ResetAddictionPlayTime(loginState->accountID);
            playedTime = 0;
        }
        else
        {
            playedTime = GetAddictionPlayTime(gameAccountData->eaAccountKeyValues);
        }

        if ( playedTime >= gAddictionMaxPlayTime )
        {
            // Player has played too much without enough of a break, so boot them.
            aslLogin2_FailLogin(loginState, "Login2_PlayTimeExceeded");
            return;
        }

        loginState->addictionPlayedTime = playedTime;
    }

    aslLogin2_GetCharacterChoices(loginState->accountID, GetCharacterListCompletionCB, aslLogin2_GetLoginCookieStruct(loginState));
}

static void 
Login2GetCharacterList_BeginFrame(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    int i;
    bool booting = false;

    if ( !Login2BeginFrameCommon(loginState) )
    {
        return;
    }

    // Stay in this state until character selection data arrives.
    if ( loginState->characterSelectionData )
    {
        if ( !loginState->noPlayerBooting )
        {
            // Boot any characters still logged on for this account.
            for ( i = eaSize(&loginState->characterSelectionData->characterChoices->characterChoices) - 1; i >= 0; i-- )
            {
                Login2CharacterChoice *characterChoice = loginState->characterSelectionData->characterChoices->characterChoices[i];
                if ( characterChoice->ownerType != GLOBALTYPE_OBJECTDB )
                {
                    // If any characters are logged in, switch to booging player state
                    loginState->characterBootCount = 0;
                    loginState->bootFailed = false;
                    ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_BOOT_PLAYER);
                    booting = true;
                    break;
                }
            }
        }

        if ( !booting )
        {
            ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_CHARACTER_SELECT);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
//
// Code for state LOGIN2_STATE_BOOT_PLAYER
//
//////////////////////////////////////////////////////////////////////////

static void
BootingPlayerCompleteCB(bool success, Login2CookieStruct *cookieStruct)
{
    Login2State *loginState;

    loginState = aslLogin2_LoginCookieStructToLoginState(&cookieStruct);
    if ( !ValidateLoginState(loginState) )
    {
        return;
    }

    if ( loginState->characterBootCount > 0)
    {
        loginState->characterBootCount--;
    }

    if ( !success )
    {
        loginState->bootFailed = true;
    }
}

static void 
Login2BootingPlayer_Enter(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    int i;

    if ( !Login2EnterStateCommon(loginState, LOGIN2_STATE_BOOT_PLAYER, s_TimeoutIntershard) )
    {
        return;
    }

    for ( i = eaSize(&loginState->characterSelectionData->characterChoices->characterChoices) - 1; i >= 0; i-- )
    {
        Login2CharacterChoice *characterChoice = loginState->characterSelectionData->characterChoices->characterChoices[i];
        if ( characterChoice->ownerType != GLOBALTYPE_OBJECTDB )
        {
            loginState->characterBootCount++;
            aslLogin2_BootPlayer(characterChoice->containerID, characterChoice->shardName, "Login2_DuplicateLogin", BootingPlayerCompleteCB, aslLogin2_GetLoginCookieStruct(loginState));
        }
    }
}

static void 
Login2BootingPlayer_BeginFrame(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2BeginFrameCommon(loginState) )
    {
        return;
    }

    if ( loginState->characterBootCount == 0 || loginState->bootFailed )
    {
        if ( loginState->bootFailed )
        {
            aslLogin2_FailLogin(loginState, "Login2_BootFailed");
            return;
        }
        else
        {
            ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_CHARACTER_SELECT);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
//
// Code for state LOGIN2_STATE_RETURN_TO_CHARACTER_SELECT
//
//////////////////////////////////////////////////////////////////////////

void 
aslLogin2_ReturnToCharacterSelect(Login2State *loginState)
{
    if ( ISM_IsStateActive(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_CHARACTER_SELECT) 
        || ISM_IsStateActive(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_GET_CHARACTER_LIST) 
        || ISM_IsStateActive(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_RETURN_TO_CHARACTER_SELECT) )
    {
        return;
        // Already there, ignore
    }

    // Player is already in game, so ignore this request.
    if ( loginState->transferToGameserverComplete )
    {
        return;
    }

    if ( !ISM_IsStateActive(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_GET_MAP_CHOICES)
        && !ISM_IsStateActive(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_REQUEST_GAMESERVER_ADDRESS)
        && !ISM_IsStateActive(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_CONVERT_PLAYER_TYPE)
        && !ISM_IsStateActive(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_GET_SELECTED_CHARACTER)
        && !ISM_IsStateActive(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_GET_SELECTED_CHARACTER_LOCATION)
        && !ISM_IsStateActive(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_ONLINE_AND_FIXUP)
        && !ISM_IsStateActive(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_UGC_PROJECT_SELECT) )
    {
        aslLogin2_Log("Told to go to character selection during wrong state");
        return;
    }

    // Set this flag to tell the other states that they should return to character select.
    loginState->clientRequestedReturnToCharacterSelect = true;
}

static void 
Login2ReturnToCharacterSelect_Enter(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2EnterStateCommon(loginState, LOGIN2_STATE_RETURN_TO_CHARACTER_SELECT, s_TimeoutTransaction) )
    {
        return;
    }
    
    // If the player container has already arrived on this server, return it to the objectDB.
    aslLogin2_CleanupFailedLogin(loginState);
}

static void 
Login2ReturnToCharacterSelect_BeginFrame(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2BeginFrameCommon(loginState) )
    {
        return;
    }

    // Wait until the previously selected character has been returned to the ObjectDB.
    if ( loginState->selectedCharacterArrived == false )
    {
        // Clear info about Redirect.
        if ( loginState->redirectLoginPacketData )
        {
            StructDestroySafe(parse_RedirectLoginPacketData, &loginState->redirectLoginPacketData);
        }

        // Clear info about selected character.
        loginState->selectedCharacterID = 0;
        if ( loginState->selectedCharacterChoice )
        {
            StructDestroySafe(parse_Login2CharacterChoice, &loginState->selectedCharacterChoice);
        }
        if ( loginState->chooseCharacterPacketData )
        {
            StructDestroySafe(parse_ChooseCharacterPacketData, &loginState->chooseCharacterPacketData);
        }

        // Clear info about created character.
        if ( loginState->characterCreationData )
        {
            StructDestroySafe(parse_Login2CharacterCreationData, &loginState->characterCreationData);
        }
        if ( loginState->createCharacterPacketData )
        {
            StructDestroySafe(parse_CreateCharacterPacketData, &loginState->createCharacterPacketData);
        }

        // Clear info about gameserver selection.
        if ( loginState->requestedGameserver )
        {
            StructDestroySafe(parse_PossibleMapChoice, &loginState->requestedGameserver);
        }
        if ( loginState->requestedMapSearch )
        {
            StructDestroySafe(parse_MapSearchInfo, &loginState->requestedMapSearch);
        }

        ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_GET_CHARACTER_LIST);
    }
}

//////////////////////////////////////////////////////////////////////////
//
// Code for state LOGIN2_STATE_CHARACTER_SELECT
//
//////////////////////////////////////////////////////////////////////////

static void
GetCharacterDetailCompletionCB(Login2CharacterDetail *characterDetail, Login2CookieStruct *cookieStruct)
{
    Login2State *loginState;

    loginState = aslLogin2_LoginCookieStructToLoginState(&cookieStruct);
    if ( !ValidateLoginState(loginState ) )
    {
        return;
    }

    if ( characterDetail && characterDetail->playerEnt)
    {
        if ( ( loginState->activeCharacterDetailRequest != NULL ) && ( characterDetail->playerID == loginState->activeCharacterDetailRequest->characterID ) )
        {
            Login2CharacterChoice *characterChoice = GetCharacterChoiceByID(loginState, characterDetail->playerID);

            // Cache the bad name flag on character choice in case the player wants to do a rename.
            if ( characterChoice ) 
            {
                characterChoice->detailReceived = true;

                if ( characterDetail->playerEnt->pSaved )
                {
                    characterChoice->hasBadName = characterDetail->playerEnt->pSaved->bBadName;
                    if ( characterChoice->oldBadName )
                    {
                        StructFreeString((char *)characterChoice->oldBadName);
                    }
                    characterChoice->oldBadName = StructAllocString(characterDetail->playerEnt->pSaved->esOldName);
                }
            }

            // Send the character detail to the client if it matches the currently active request.
            aslLogin2_SendCharacterDetail(loginState, characterDetail);

            // Clear out the active request, since it has been satisfied.
            StructDestroySafe(parse_RequestCharacterDetailPacketData, &loginState->activeCharacterDetailRequest);
        }
        else
        {
            Errorf("Got completion of a character detail request that is not the active one.");
        }

        // Free up the character detail.  There is no need to cache it here.  The client can cache it if necessary.
        StructDestroy(parse_Login2CharacterDetail, characterDetail);
    }
    else
    {
		if(characterDetail)
	        StructDestroy(parse_Login2CharacterDetail, characterDetail);
        Errorf("Failed to get character details.");
    }
}

static void 
Login2CharacterSelect_Enter(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2EnterStateCommon(loginState, LOGIN2_STATE_CHARACTER_SELECT, s_TimeoutUser) )
    {
        return;
    }

    // Send character selection data to client.
    if ( loginState->afterRedirect == false )
    {
        aslLogin2_SendCharacterSelectionData(loginState);
    }

    // Make sure that we send cluster status during the first frame of character select.
    loginState->lastClusterStatusSendTime = 0;
}

static bool
ValidateCharacterRename(RenameCharacterPacketData *renameCharacterPacketData, Login2CharacterChoice *characterChoice, int clientAccessLevel)
{
    // LOGIN2TODO - notifications

    if ( g_isContinuousBuilder )
    {
        // Builders can always change names.
        return true;
    }

    if ( characterChoice == NULL )
    {
        return false;
    }

    // Bad name flags have to match.
    if ( renameCharacterPacketData->badName != characterChoice->hasBadName )
    {
        return false;
    }

    // Can't change to the same name.
    if ( strcmp(characterChoice->savedName, renameCharacterPacketData->newName) == 0 )
    {
        return false;
    }

    // Is the name valid?
    if( StringIsInvalidCharacterName(renameCharacterPacketData->newName, clientAccessLevel) != STRINGERR_NONE )
    {
        return false;
    }

    return true;
}

static void 
Login2CharacterSelect_BeginFrame(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    Login2CharacterChoice *characterChoice;

    if ( !Login2BeginFrameCommon(loginState) )
    {
        return;
    }

    // We are already in character select, so we can reset this flag.
    loginState->clientRequestedReturnToCharacterSelect = false;

    if ( loginState->chooseCharacterPacketData || loginState->selectedCharacterChoice )
    {
        // Handle choose character requests.  If we arrive here after a redirect, then selectedCharacterChoice will already be set.
        if ( !loginState->selectedCharacterChoice )
        {
            // Make sure the character ID is valid for this account.
            characterChoice = GetCharacterChoiceByID(loginState, loginState->chooseCharacterPacketData->characterID);
            if ( characterChoice == NULL )
            {
                aslLogin2_FailLogin(loginState, "Login2_InvalidCharacterChoice");
                return;
            }

            loginState->selectedCharacterID = loginState->chooseCharacterPacketData->characterID;
            loginState->requestedUGCEdit = loginState->chooseCharacterPacketData->UGCEdit;

            // Record the chosen character.
            loginState->selectedCharacterChoice = StructClone(parse_Login2CharacterChoice, characterChoice);
        }
        else
        {
            devassertmsg(loginState->afterRedirect, "Login2CharacterSelect_BeginFrame: selectedCharacterChoice is set and it is not after a redirect");
        }

        if ( loginState->selectedCharacterChoice->shardName == Login2_GetPooledShardName() )
        {
            // Chosen character is local.

            if ( loginState->requestedUGCEdit )
            {
                // Fail if UGC is not allowed.
                if ( !aslLoginIsUGCAllowed() || !loginState->selectedCharacterChoice->isUGCEditAllowed )
                {
                    aslLogin2_FailLogin(loginState, "Login2_UGCEditNotAllowed");
                    return;
                }
            }

            if ( loginState->ignoreQueue )
            {
                // Get the character from the database.
                ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_ONLINE_AND_FIXUP);
            }
            else
            {
                // Go through the login queue.
                ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_QUEUED);
            }

            if ( loginState->afterRedirect )
            {
                // We are done with redirect processing.
                loginState->afterRedirect = false;
            }
            else
            {
                aslLogin2_SendClientNoRedirect(loginState);
            }
        }
        else
        {
            // Chosen character is on a remote shard.  We need to redirect.
            devassertmsg(loginState->afterRedirect == false, "Login2CharacterSelect_BeginFrame: Double redirect!");
            loginState->redirectShardName = loginState->selectedCharacterChoice->shardName;
            ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_REDIRECT);
        }

    }
    else if ( loginState->createCharacterPacketData || loginState->characterCreationData )
    {
        bool isUGCAllowed = aslLoginIsUGCAllowed();
        const char *requestedShardName;
        Cluster_Overview *clusterOverview;

        // If we arrive here after a redirect, then characterCreationData will already be set.
        if ( !loginState->characterCreationData )
        {
            loginState->requestedUGCEdit = loginState->createCharacterPacketData->UGCEdit;

            // Move ownership of the character creation data to loginState and make sure there is not a second pointer to it.
            loginState->characterCreationData = loginState->createCharacterPacketData->characterCreationData;
            loginState->createCharacterPacketData->characterCreationData = NULL;
        }

        requestedShardName = loginState->characterCreationData->shardName;
        clusterOverview = GetShardClusterOverview_EvenIfNotInCluster();

        if ( loginState->requestedUGCEdit && eaSize(&clusterOverview->ppShards) > 1 )
        {
            // Player requested UGC and this shard is part of a cluster, then find the name of the UGC shard.
            int i;

            for ( i = eaSize(&clusterOverview->ppShards) - 1; i >= 0; i-- )
            {
                ClusterShardSummary *shardSummary = clusterOverview->ppShards[i];
                if ( shardSummary->eShardType == SHARDTYPE_UGC )
                {
                    // Use the name of the UGC shard.
                    requestedShardName = allocAddString(shardSummary->pShardName);
                    loginState->characterCreationData->shardName = requestedShardName;
                    break;
                }
            }
        }

        if ( requestedShardName == Login2_GetPooledShardName() || 
            requestedShardName == NULL || requestedShardName[0] == 0 )
        {
            // Chosen character is local.

            // Fail login if this shard does not allow character creation.
            if ( CharacterCreationDisabledOnShard(Login2_GetPooledShardName()) && loginState->clientAccessLevel == 0 )
            {
                aslLogin2_FailLogin(loginState, "Login2_ShardLocked");
                return;
            }

            if ( loginState->requestedUGCEdit )
            {
                // Fail if UGC is not allowed.
                if ( !isUGCAllowed )
                {
                    aslLogin2_FailLogin(loginState, "Login2_UGCEditNotAllowed");
                    return;
                }
            }

            ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_CREATE_CHARACTER);

            if ( loginState->afterRedirect )
            {
                // We are done with redirect processing.
                loginState->afterRedirect = false;
            }
            else
            {
                aslLogin2_SendClientNoRedirect(loginState);
            }
        }
        else
        {
            // Creating character on a remote shard.  We need to redirect.
            devassertmsg(loginState->afterRedirect == false, "Login2CharacterSelect_BeginFrame: Double redirect!");

            // Fail login if this shard does not allow character creation.
            if ( CharacterCreationDisabledOnShard(loginState->characterCreationData->shardName) && loginState->clientAccessLevel == 0 )
            {
                aslLogin2_FailLogin(loginState, "Login2_ShardLocked");
                return;
            }

            loginState->redirectShardName = loginState->characterCreationData->shardName;
            ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_REDIRECT);
        }
    }
    else
    {
        // This stuff will not change the state.
        U32 curTime = timeSecondsSince2000();

        // Send the status if it has changed since the last send, or we haven't sent it in 5 seconds.  The repeat sends are to guard against a race condition
        //  where the client can clear its copy of the cluster status at a bad time.
        if ( loginState->lastClusterStatusSendTime < s_LastShardStatusUpdateTime || loginState->lastClusterStatusSendTime < (curTime - 5 ))
        {
            Login2ClusterStatus clusterStatus = {0};

            // Send the cluster status to the client if it has changed.
            clusterStatus.shardStatus = s_ShardStatusList;
            clusterStatus.isCluster = ( eaSize(&s_ShardStatusList) > 1 );
            aslLogin2_SendClusterStatus(loginState, &clusterStatus);

            loginState->lastClusterStatusSendTime = curTime;
        }

        if ( loginState->requestCharacterDetailPacketData != NULL )
        {
            // Handle character detail requests.
            devassertmsg(loginState->afterRedirect == false, "Login2CharacterSelect_BeginFrame: Got character details request after redirect!");

            // Only handle the request if another one is not currently active.
            if ( loginState->activeCharacterDetailRequest == NULL )
            {
                loginState->activeCharacterDetailRequest = loginState->requestCharacterDetailPacketData;
                loginState->requestCharacterDetailPacketData = NULL;

                // Make sure the character ID is valid for this account.
                characterChoice = GetCharacterChoiceByID(loginState, loginState->activeCharacterDetailRequest->characterID);
                if ( characterChoice == NULL )
                {
                    aslLogin2_FailLogin(loginState, "Login2_InvalidCharacterDetailRequest");
                    return;
                }

                characterChoice->detailRequested = true;

                aslLogin2_GetCharacterDetail(loginState->accountID, loginState->activeCharacterDetailRequest->characterID, characterChoice->shardName, gConf.bCharacterDetailIncludesPuppets, GetCharacterDetailCompletionCB, aslLogin2_GetLoginCookieStruct(loginState));
            }
        }
        if ( loginState->deleteCharacterPacketData != NULL )
        {
            devassertmsg(loginState->afterRedirect == false, "Login2CharacterSelect_BeginFrame: Got character delete request after redirect!");

            // Switch to delete character state if a request has come in.
            ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_DELETE_CHARACTER);
        }
        else if ( loginState->renameCharacterPacketData != NULL )
        {
            devassertmsg(loginState->afterRedirect == false, "Login2CharacterSelect_BeginFrame: Got character rename request after redirect!");

            characterChoice = GetCharacterChoiceByID(loginState, loginState->renameCharacterPacketData->characterID);

            if ( ValidateCharacterRename(loginState->renameCharacterPacketData, characterChoice, loginState->clientAccessLevel) )
            {
                ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_RENAME_CHARACTER);
            }
            else
            {
                // Name change is invalid.
                StructDestroy(parse_RenameCharacterPacketData, loginState->renameCharacterPacketData);
                loginState->renameCharacterPacketData = NULL;
            }
        }
    }
}

//////////////////////////////////////////////////////////////////////////
//
// Code for state LOGIN2_STATE_DELETE_CHARACTER
//
//////////////////////////////////////////////////////////////////////////

static void
DeleteCharacterCompletionCB(bool success, U64 loginCookie)
{
    Login2State *loginState;

    loginState = aslLogin2_GetActiveLoginState(loginCookie);

    if ( !ValidateLoginState(loginState) )
    {
        aslLogin2_Log("Command %s returned with invalid loginState.  loginCookie=%p", __FUNCTION__, loginCookie);
        return;
    }

    if ( ( !ISM_IsStateActive(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_DELETE_CHARACTER) ) || ( loginState->deleteCharacterPacketData == NULL ) )
    {
        aslLogin2_Log("Command %s returned for account %u while in wrong state", __FUNCTION__, loginState->accountID);
        aslLogin2_FailLogin(loginState, "Login2_InternalError");
        return;
    }

    if ( !success )
    {
        aslLogin2_Log("Character delete failed for character %u", loginState->deleteCharacterPacketData->characterID);
        aslLogin2_FailLogin(loginState, "Login2_CharacterDeleteFailed");
        return;
    }

    if ( !loginState->deletedCharacterIsUGC && gConf.bUseSimplifiedMultiShardCharacterSlotRules )
    {
	    // Report the deletion to the Account Server.  Only count non-ugc characters.  This is only done when using
        //  simplified multi-shard character slot rules.
	    RemoteCommand_aslAPCmdSetNumCharacters(GLOBALTYPE_ACCOUNTPROXYSERVER, 0, loginState->accountID, -1, true);
    }

    aslLogin2_Log("Character delete succeeded for character %u", loginState->deleteCharacterPacketData->characterID);

    // Clearing out the delete packet will trigger the next state.
    StructDestroy(parse_DeleteCharacterPacketData, loginState->deleteCharacterPacketData);
    loginState->deleteCharacterPacketData = NULL;
}

static void 
Login2DeleteCharacter_Enter(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    ContainerID characterID;
    Login2CharacterChoice *characterChoice;

    if ( !Login2EnterStateCommon(loginState, LOGIN2_STATE_DELETE_CHARACTER, s_TimeoutIntershard) )
    {
        return;
    }

    characterID = loginState->deleteCharacterPacketData->characterID;

    // Make sure the character ID is valid for this account.
    characterChoice = GetCharacterChoiceByID(loginState, characterID);
    if ( characterChoice == NULL )
    {
        aslLogin2_FailLogin(loginState, "Login2_InvalidDeleteCharacterRequest");
        return;
    }

    // Remember that the character being deleted is from the UGC virtual shard.
    loginState->deletedCharacterIsUGC = ( characterChoice->virtualShardID == 1 );

    aslLogin2_DeleteCharacter(characterID, characterChoice->shardName, DeleteCharacterCompletionCB, loginState->loginCookie);
}

static void 
Login2DeleteCharacter_BeginFrame(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2BeginFrameCommon(loginState) )
    {
        return;
    }

    // Wait for delete processing to complete and then re-start character selection.
    if ( loginState->deleteCharacterPacketData == NULL )
    {
        // We need to refresh the character list after deleting a character.
        ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_GET_CHARACTER_LIST);
    }
}

//////////////////////////////////////////////////////////////////////////
//
// Code for state LOGIN2_STATE_RENAME_CHARACTER
//
//////////////////////////////////////////////////////////////////////////

void
RenameCharacterCompletionCB(bool success, U64 loginCookie)
{
    Login2State *loginState;

    loginState = aslLogin2_GetActiveLoginState(loginCookie);

    if ( !ValidateLoginState(loginState) )
    {
        aslLogin2_Log("Command %s returned with invalid loginState.  loginCookie=%p", __FUNCTION__, loginCookie);
        return;
    }

    if ( ( !ISM_IsStateActive(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_RENAME_CHARACTER) ) || ( loginState->renameCharacterPacketData == NULL ) )
    {
        aslLogin2_Log("Command %s returned for account %u while in wrong state", __FUNCTION__, loginState->accountID);
        aslLogin2_FailLogin(loginState, "Login2_InternalError");
        return;
    }

    if ( !success )
    {
        aslLogin2_Log("Character rename failed for character %u", loginState->renameCharacterPacketData->characterID);
        aslLogin2_FailLogin(loginState, "Login2_CharacterRenameFailed");
        return;
    }

    aslLogin2_Log("Character rename succeeded for character %u", loginState->renameCharacterPacketData->characterID);

    // Clearing out the rename packet will trigger the next state.
    StructDestroy(parse_RenameCharacterPacketData, loginState->renameCharacterPacketData);
    loginState->renameCharacterPacketData = NULL;
}

static void 
Login2RenameCharacter_Enter(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    ContainerID characterID;
    Login2CharacterChoice *characterChoice;
    GlobalType puppetType = GLOBALTYPE_NONE;
    ContainerID puppetID = 0;

    if ( !Login2EnterStateCommon(loginState, LOGIN2_STATE_RENAME_CHARACTER, s_TimeoutIntershard) )
    {
        return;
    }

    characterID = loginState->renameCharacterPacketData->characterID;

    // Make sure the character ID is valid for this account.
    characterChoice = GetCharacterChoiceByID(loginState, characterID);
    if ( characterChoice == NULL )
    {
        aslLogin2_FailLogin(loginState, "Login2_InvalidRenameCharacterRequest");
        return;
    }

    aslLogin2_RenameCharacter(characterID, characterChoice->accountID, puppetType, puppetID, loginState->renameCharacterPacketData->newName,
        loginState->renameCharacterPacketData->badName, characterChoice->shardName, RenameCharacterCompletionCB, loginState->loginCookie);
}

static void 
Login2RenameCharacter_BeginFrame(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2BeginFrameCommon(loginState) )
    {
        return;
    }

    // Wait for rename processing to complete and then re-start character selection.
    if ( loginState->renameCharacterPacketData == NULL )
    {
        // We need to refresh the character list after renaming a character.
        ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_GET_CHARACTER_LIST);
    }
}

//////////////////////////////////////////////////////////////////////////
//
// Code for state LOGIN2_STATE_CREATE_CHARACTER
//
//////////////////////////////////////////////////////////////////////////

static void 
CreateCharacter_CB(TransactionReturnVal *returnVal, void *userData)
{
    Login2State *loginState = aslLogin2_GetActiveLoginStateShortCookiePointer(userData);
    int tmpNumUnrestrictedSlots;

    // In either of these first two cases, it's possible that we successfully created a character for a request
    // that was initiated before a connection dropped out. Instead of returning, we'll properly return the
    // container here.
    if( !ValidateLoginState(loginState) )
    {
        aslLogin2_Log("Command %s returned for invalid login id", __FUNCTION__);
        return;
    }
    else if( !ISM_IsStateActive(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_CREATE_CHARACTER) )
    {
        aslLogin2_Log("Character creation returned for accountID %u while in wrong state", loginState->accountID);
        aslLogin2_FailLogin(loginState, "Login2_CharacterCreationFailed");
        return;
    }

    if ( returnVal->eOutcome != TRANSACTION_OUTCOME_SUCCESS )
    {
        // Generate an error message if the create transaction failed.
        int i;
        static char *s_errorString = NULL;
        for ( i = 0; i < returnVal->iNumBaseTransactions; i++ )
        {			
            if ( returnVal->pBaseReturnVals[i].eOutcome == TRANSACTION_OUTCOME_FAILURE )
            {
                estrPrintf(&s_errorString, "Failed to create new character because: %s", returnVal->pBaseReturnVals[i].returnString);
                break;
            }
        }
        aslLogin2_Log(s_errorString);
        aslLogin2_FailLogin(loginState, "Login2_CharacterCreationFailed");

        return;
    }

    // Send the newly created container ID to the client so it will be able to map transfer.
    loginState->newCharacterID = atoi(returnVal->pBaseReturnVals[0].returnString);

    tmpNumUnrestrictedSlots = loginState->characterSelectionData->availableCharSlots->numUnrestrictedSlots;

    // Check for available character slots and consume the slot.
    if ( !CharSlots_MatchSlot(loginState->characterSelectionData->availableCharSlots, 
        loginState->characterCreationData->virtualShardID, 
        loginState->characterCreationData->allegianceName, true) )
    {
        ErrorDetailsf("accountID=%u, accountName=%s, virtualShardID=%d, allegiance=%s", 
            loginState->accountID, loginState->accountName, loginState->characterCreationData->virtualShardID,
            loginState->characterCreationData->allegianceName);
        Errorf("%s: Character creation completed when no slots are available.", __FUNCTION__);
    }

    if ( gConf.bUseSimplifiedMultiShardCharacterSlotRules && ( tmpNumUnrestrictedSlots != loginState->characterSelectionData->availableCharSlots->numUnrestrictedSlots ) )
    {
        // Inform the Account Server that a character was created.  We only count characters in unrestricted slots,
        //  so only tell the account proxy if we consumed an unrestricted slot.
        // This is only done when we are using simplified multi-shard character slot rules.
        RemoteCommand_aslAPCmdSetNumCharacters(GLOBALTYPE_ACCOUNTPROXYSERVER, 0, loginState->accountID, 1, true);
    }

    // Log success.
    aslLogin2_Log("Character creation completed successfully for characterID %u", loginState->newCharacterID);

}

static void 
Login2CreateCharacter_Enter(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    Login2CharacterCreationData *characterCreationData = loginState->characterCreationData;
    NOCONST(Entity) *newCharacterEnt;

	if(!characterCreationData->name)
	{
		Errorf("%s: Character creation failed as there wasn't a character name.", __FUNCTION__);
		aslLogin2_FailLogin(loginState, "Login2_CharacterCreationFailed");
		return;
	}

    // Check for an available character slot.
    if ( !CharSlots_MatchSlot(loginState->characterSelectionData->availableCharSlots, characterCreationData->virtualShardID, characterCreationData->allegianceName, false) )
    {
        ErrorDetailsf("accountID=%u, accountName=%s, virtualShardID=%d, allegiance=%s", 
            loginState->accountID, loginState->accountName, characterCreationData->virtualShardID, characterCreationData->allegianceName);
        Errorf("%s: Character creation requested when no slots are available.", __FUNCTION__);
        aslLogin2_FailLogin(loginState,"Login2_NoCharacterSlot");
        return;
    }

    // Allocate an entity to build in.
    newCharacterEnt = StructCreateWithComment(parse_Entity, "NewCharacter for character creation in Login2CreateCharacter_Enter");
    loginState->newCharacterEnt = CONTAINER_RECONST(Entity, newCharacterEnt);

    // Initialize the prototype entity.
    if ( !aslLogin2_InitializeNewCharacter(loginState, newCharacterEnt) )
    {
        aslLogin2_FailLogin(loginState, "Login2_CharacterCreationFailed");
        return;
    }

    // Ask the transaction system to create the entity.
    objRequestContainerCreate(objCreateManagedReturnVal(CreateCharacter_CB, aslLogin2_GetShortLoginCookieAsPointer(loginState)),
        GLOBALTYPE_ENTITYPLAYER, newCharacterEnt, objServerType(), objServerID());
}

static void 
Login2CreateCharacter_BeginFrame(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2BeginFrameCommon(loginState) )
    {
        return;
    }

    // Stay in this state until the new character has been created.
    if ( loginState->newCharacterID )
    {
        // Send the character ID to the client.
        aslLogin2_SendNewCharacterID(loginState, loginState->newCharacterID);

        // The character was created on the login server, so it should be here.
        loginState->selectedCharacterArrived = true;
        loginState->selectedCharacterID = loginState->newCharacterID;
		loginState->newCharacterID = 0;

        // Go to queue next.
        ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_QUEUED);
    }
}

//////////////////////////////////////////////////////////////////////////
//
// Code for state LOGIN2_STATE_QUEUED
//
//////////////////////////////////////////////////////////////////////////

static int s_totalQueueSize;
static int s_lastMainQueueID;
static int s_lastVIPQueueID;
static U32 s_lastQueueIDUpdateTime;

// Update global queue state.
void
aslLogin2_QueueIDsUpdate(U32 mainQueueID, U32 VIPQueueID, int totalInQueues)
{
    s_lastMainQueueID = mainQueueID;
    s_lastVIPQueueID = VIPQueueID;
    s_totalQueueSize = totalInQueues;
    s_lastQueueIDUpdateTime = timeSecondsSince2000();
}

// This function is called when the controller returns initial queue info after the player enters the queue.
void
aslLogin2_HereIsQueueID(U64 loginCookie, U32 queueID, int curPositionInQueue, int totalInQueues)
{
    Login2State *loginState;
    U32 curTime = timeSecondsSince2000();

    loginState = aslLogin2_GetActiveLoginState(loginCookie);
    if ( loginState == NULL || !ISM_IsStateActive(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_QUEUED) )
    {
        return;
    }

    // Save queueID so that we can compute queue position later.
    loginState->queueID = queueID;

    // Send the client an update immediately with reported queue position and size.
    aslLogin2_SendQueueUpdate(loginState, curPositionInQueue, totalInQueues);
    loginState->lastClientQueueUpdateTime = curTime;

    s_totalQueueSize = totalInQueues;
}

// This is called when the player has gotten through the queue.
void
aslLogin2_PlayerIsThroughQueue(U64 loginCookie)
{
    Login2State *loginState;

    loginState = aslLogin2_GetActiveLoginState(loginCookie);
    if ( loginState == NULL )
    {
        ReportThatPlayerWhoWentThroughQueueHasLeft();
        return;
    }

    // If this guy somehow went through queue and then back (ie, cancelled out of map choosing screen), then tell the controller 
    //  that so that we don't get our count out of sync.
    if ( loginState->isThroughQueue )
    {
        ReportThatPlayerWhoWentThroughQueueHasLeft();
    }
    loginState->isThroughQueue = true;
}

static void 
Login2Queued_Enter(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    U32 lastPlayedTime = 0;
    if ( !Login2EnterStateCommon(loginState, LOGIN2_STATE_QUEUED, s_TimeoutQueued) )
    {
        return;
    }
    
    if ( loginState->selectedCharacterChoice )
    {
        lastPlayedTime = loginState->selectedCharacterChoice->lastPlayedTime;
    }

    // Player can use the VIP queue if they have played in the last 5 minutes.
    if ( lastPlayedTime && ( ( timeSecondsSince2000() - lastPlayedTime ) <= ( 60 * 5 ) ) )
    {
        loginState->isQueueVIP = true;
    }

    RemoteCommand_QueuePlayerIfNecessary(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalID(), loginState->loginCookie, loginState->isQueueVIP);
}

static void 
Login2Queued_BeginFrame(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    U32 curTime = timeSecondsSince2000();

    if ( !Login2BeginFrameCommon(loginState) )
    {
        return;
    }

    if ( loginState->isThroughQueue )
    {
        if ( loginState->selectedCharacterArrived )
        {
            // The selected character will have arrived already if it was a newly created character.  
            //  Jump ahead to UGC project select or getting map choices.
            if ( loginState->requestedUGCEdit )
            {
                ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_UGC_PROJECT_SELECT);
            }
            else
            {
                ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_GET_MAP_CHOICES);
            }
        }
        else
        {
            // Get the character from the database.
            ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_ONLINE_AND_FIXUP);
        }
    }
    else if ( loginState->lastClientQueueUpdateTime )
    {
        if ( ( curTime - loginState->lastClientQueueUpdateTime ) >= LOGIN2_QUEUE_UPDATE_INTERVAL )
        {
            int queuePosition;

            // Compute our position in the queue based on last reported numbers.
            if ( loginState->isQueueVIP )
            {
                queuePosition = loginState->queueID - s_lastVIPQueueID;
            }
            else
            {
                queuePosition = loginState->queueID - s_lastMainQueueID;
            }

            // Send a queue position update to the client.
            aslLogin2_SendQueueUpdate(loginState, queuePosition, s_totalQueueSize);
            loginState->lastClientQueueUpdateTime = curTime;
        }
    }
}

//////////////////////////////////////////////////////////////////////////
//
// Code for state LOGIN2_STATE_ONLINE_AND_FIXUP
//
//////////////////////////////////////////////////////////////////////////

static void
OnlineAndFixupCB(Login2CharacterDetail *characterDetail, Login2CookieStruct *cookieStruct)
{
    Login2State *loginState;

    loginState = aslLogin2_LoginCookieStructToLoginState(&cookieStruct);
    if ( !ValidateLoginState(loginState) )
    {
        return;
    }

    loginState->onlineAndFixupComplete = true;
    StructDestroy(parse_Login2CharacterDetail, characterDetail);
}

static void 
Login2OnlineAndFixup_Enter(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2EnterStateCommon(loginState, LOGIN2_STATE_ONLINE_AND_FIXUP, s_TimeoutIntershard) )
    {
        return;
    }

    if ( loginState->selectedCharacterChoice == NULL )
    {
        aslLogin2_FailLogin(loginState, "Login2_InternalError");
        return;
    }

    if ( loginState->selectedCharacterChoice->detailReceived == true )
    {
        // Character details has already been received so we know that the character is online.
        ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_GET_SELECTED_CHARACTER_LOCATION);
    }
    else
    {
        // Get character detail to force onlining and fixup.
        aslLogin2_GetCharacterDetail(loginState->accountID, loginState->selectedCharacterChoice->containerID, loginState->selectedCharacterChoice->shardName, false, OnlineAndFixupCB, aslLogin2_GetLoginCookieStruct(loginState));
    }
}

static void 
Login2OnlineAndFixup_BeginFrame(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2BeginFrameCommon(loginState) )
    {
        return;
    }

    if ( loginState->onlineAndFixupComplete )
    {
        // Character details has been received so we know that the character is online.
        ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_GET_SELECTED_CHARACTER_LOCATION);
    }
}

//////////////////////////////////////////////////////////////////////////
//
// Code for state LOGIN2_STATE_GET_SELECTED_CHARACTER_LOCATION
//
//////////////////////////////////////////////////////////////////////////

bool
aslLogin2_IsVirtualShardAvailable(S32 clientAccessLevel, U32 virtualShardID, bool UGCEdit)
{
    VirtualShard *virtualShard;

    // Elevated access level always gets access to virtual shards.  
    if ( clientAccessLevel > 0 )
    {
        return true;
    }

    // Virtual shard 0 is always available for play but not edit.
    if ( virtualShardID == 0 )
    {
        // Can't edit on virtual shard 0.
        if ( UGCEdit )
        {
            return false;
        }
        else
        {
            return true;
        }
    }

    virtualShard = aslGetVirtualShardByID(virtualShardID);
    if ( virtualShard )
    {
        if ( UGCEdit )
        {
            // Editing was requested but this is not an editing virtual shard.
            if ( !virtualShard->bUGCShard )
            {
                return false;
            }
        }

        return !virtualShard->bDisabled;
    }

    // Virtual shard was not found.
    return false;
}

static void
GetSelectedCharacterLocation_CB(TransactionReturnVal *returnVal, void *userData)
{
    Login2State *loginState = aslLogin2_GetActiveLoginStateShortCookiePointer(userData);
    ContainerRef *containerLocation;
    GlobalType ownerType;
    ContainerID ownerID;

    if ( !ValidateLoginState(loginState) )
    {
        aslLogin2_Log("Command %s returned with invalid loginState.  shortCookie=%p", __FUNCTION__, userData);
        return;
    }

    if (!ISM_IsStateActive(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_GET_SELECTED_CHARACTER_LOCATION))
    {
        aslLogin2_Log("Command %s returned for account %u while in wrong state", __FUNCTION__, loginState->accountID);
        aslLogin2_FailLogin(loginState, "Login2_InternalError");
        return;
    }

    if ( RemoteCommandCheck_ContainerGetOwner(returnVal, &containerLocation) == TRANSACTION_OUTCOME_SUCCESS )
    {
        ownerType = containerLocation->containerType;
        ownerID = containerLocation->containerID;

        StructDestroy(parse_ContainerRef, containerLocation);

        // If the character is not owned by the database then our previous booting attempts must have failed or something else weird is going on.
        if ( ownerType != GLOBALTYPE_OBJECTDB )
        {
            aslLogin2_Log("%s: player container owned by server type %u id %u", __FUNCTION__, ownerType, ownerID);
            aslLogin2_FailLogin(loginState, "Login2_ContainerOwned");
            
            return;
        }

        loginState->selectedCharacterOwnedByObjectDB = true;
        return;
    }

    // If the check failed, then there must be something wrong with the character.
    aslLogin2_FailLogin(loginState, "Login2_InvalidCharacter");
    return;
}

static void 
Login2GetSelectedCharacterLocation_Enter(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2EnterStateCommon(loginState, LOGIN2_STATE_GET_SELECTED_CHARACTER_LOCATION, s_TimeoutInshard) )
    {
        return;
    }

    if ( loginState->selectedCharacterChoice == NULL )
    {
        aslLogin2_FailLogin(loginState, "Login2_InternalError");
        return;
    }

    // Make sure the virtual shard the character belongs to is enabled.
    if ( !aslLogin2_IsVirtualShardAvailable(loginState->clientAccessLevel, loginState->selectedCharacterChoice->virtualShardID, loginState->requestedUGCEdit) )
    {
        aslLogin2_FailLogin(loginState, "Login2_VirtualShardDisabled");
        return;
    }

    // Ask the ObjectDB who currently owns the player container.
    RemoteCommand_ContainerGetOwner(objCreateManagedReturnVal(GetSelectedCharacterLocation_CB, aslLogin2_GetShortLoginCookieAsPointer(loginState)),
        GLOBALTYPE_ENTITYPLAYER, loginState->selectedCharacterChoice->containerID);
}

static void 
Login2GetSelectedCharacterLocation_BeginFrame(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2BeginFrameCommon(loginState) )
    {
        return;
    }

    // Return to character selection if the client requests it.
    if ( loginState->clientRequestedReturnToCharacterSelect )
    {
        ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_RETURN_TO_CHARACTER_SELECT);
        return;
    }

    // Wait in this state until the character has been confirmed to be on the ObjectDB.
    if ( loginState->selectedCharacterOwnedByObjectDB )
    {
        // Now we can get the character from the database.
        ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_GET_SELECTED_CHARACTER);
    }
}

//////////////////////////////////////////////////////////////////////////
//
// Code for state LOGIN2_STATE_GET_SELECTED_CHARACTER
//
//////////////////////////////////////////////////////////////////////////

static void
GetSelectedCharacterFailReturn_CB(TransactionReturnVal *returnVal, void *userData)
{
    U32 containerID = (U32)(intptr_t)userData;

    if ( returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
    {
        aslLogin2_Log("%s:Return of player container %u to database after failure succeeded.", __FUNCTION__, containerID);
    }
    else
    {
        // XXX - Do we want an alert here?
        aslLogin2_Log("%s:Return of player container %u to database after failure failed.", __FUNCTION__, containerID);
    }
}

static void
GetSelectedCharacter_CB(TransactionReturnVal *returnVal, void *userData)
{
    Login2State *loginState = aslLogin2_GetActiveLoginStateShortCookiePointer(userData);
    bool failed = false;

    if ( !ValidateLoginState(loginState) )
    {
        aslLogin2_Log("Command %s returned with invalid loginState.  shortCookie=%p", __FUNCTION__, userData);
        failed = true;
    } 
    else if (!ISM_IsStateActive(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_GET_SELECTED_CHARACTER))
    {
        aslLogin2_Log("Command %s returned for account %u while in wrong state", __FUNCTION__, loginState->accountID);
        aslLogin2_FailLogin(loginState, "Login2_InternalError");
        failed = true;
    }

    if ( returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS )
    {
        if ( failed )
        {
            // If we got the container but failed for some other reason, then we need to return the container to the database.
            // NOTE - loginState might be NULL in this case, so don't use it.
            ContainerID playerID = atoi(returnVal->pBaseReturnVals[0].returnString);
            GlobalType playerType = GLOBALTYPE_ENTITYPLAYER;

            if (objGetContainer(playerType, playerID) )
            {
                // Container really exists here.  Return it to the ObjectDB.
                objRequestContainerMove(objCreateManagedReturnVal(GetSelectedCharacterFailReturn_CB, (void *)(intptr_t)playerID),
                    playerType, playerID, objServerType(), objServerID(), GLOBALTYPE_OBJECTDB, 0);
            }
        }
        else
        {
            // We got the container and everything is OK.
            loginState->selectedCharacterArrived = true;
        }
    }
    else if (!failed)
    {
        // Found the loginState ok, and in the right state, but container move failed.
        aslLogin2_Log("Failed to move character to loginserver.  accountID=%u, containerID=%u", __FUNCTION__, loginState->accountID, loginState->selectedCharacterChoice->containerID);
        aslLogin2_FailLogin(loginState, "Login2_InternalError");
    }
}

static void 
Login2GetSelectedCharacter_Enter(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2EnterStateCommon(loginState, LOGIN2_STATE_GET_SELECTED_CHARACTER, s_TimeoutTransaction) )
    {
        return;
    }

    // Request the selected character be moved to this server.
    objRequestContainerMove(objCreateManagedReturnVal(GetSelectedCharacter_CB, aslLogin2_GetShortLoginCookieAsPointer(loginState)),
        GLOBALTYPE_ENTITYPLAYER, loginState->selectedCharacterChoice->containerID, GLOBALTYPE_OBJECTDB, 0, objServerType(), objServerID());
}

static void 
Login2GetSelectedCharacter_BeginFrame(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2BeginFrameCommon(loginState) )
    {
        return;
    }

    // Return to character selection if the client requests it.
    if ( loginState->clientRequestedReturnToCharacterSelect )
    {
        ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_RETURN_TO_CHARACTER_SELECT);
        return;
    }

    if ( loginState->selectedCharacterArrived )
    {
        Entity *playerEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, loginState->selectedCharacterChoice->containerID);
        GameAccountData *gameAccountData;

        if ( playerEnt == NULL || playerEnt->pPlayer == NULL )
        {
            aslLogin2_FailLogin(loginState, "Login2_InternalError");
            return;
        }

        gameAccountData = GET_REF(loginState->hGameAccountData);
        if ( AddictionPlaytimeLimitRequired(gameAccountData->eaAccountKeyValues) )
        {
            U32 remainingPlayTime = gAddictionMaxPlayTime - loginState->addictionPlayedTime;
            U32 playTimeEnd = remainingPlayTime + timeSecondsSince2000();

            // Set the time to boot the player due to anti-addiction policy.
            AutoTrans_aslLogin2_tr_SetAddictionPlaySessionEndTime(NULL, GLOBALTYPE_LOGINSERVER, GLOBALTYPE_ENTITYPLAYER, playerEnt->myContainerID, playTimeEnd);
        }
        else if ( playerEnt->pPlayer->addictionPlaySessionEndTime != 0 )
        {
            // Clear the time to boot the player due to anti-addiction policy.
            AutoTrans_aslLogin2_tr_SetAddictionPlaySessionEndTime(NULL, GLOBALTYPE_LOGINSERVER, GLOBALTYPE_ENTITYPLAYER, playerEnt->myContainerID, 0);
        }

        if ( gamePermission_Enabled() && loginState->clientAccessLevel < ACCESS_GM )
        {
            // New login code only supports auto-conversion.
            devassert( (!gamePermission_Enabled()) || ( gConf.bAutoConvertCharacterToPremium && gConf.bAutoConvertCharacterToStandard ) );

            if ( loginState->playerType != playerEnt->pPlayer->playerType && gamePermission_Enabled() )
            {
                ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_CONVERT_PLAYER_TYPE);
                return;
            }
        }

        if ( loginState->requestedUGCEdit )
        {
            ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_UGC_PROJECT_SELECT);
        }
        else
        {
            ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_GET_MAP_CHOICES);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
//
// Code for state LOGIN2_STATE_CONVERT_PLAYER_TYPE
//
//////////////////////////////////////////////////////////////////////////

static void
ConvertPlayerType_CB(TransactionReturnVal *returnVal, void *userData)
{
    Login2State *loginState = aslLogin2_GetActiveLoginStateShortCookiePointer(userData);

    if ( !ValidateLoginState(loginState) )
    {
        aslLogin2_Log("Command %s returned with invalid loginState.  shortCookie=%p", __FUNCTION__, userData);
        return;
    }

    if ( returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS )
    {
        // Conversion was completed successfully.
        loginState->playerTypeConversionComplete = true;
    }
    else
    {
        aslLogin2_FailLogin(loginState, "Login2_InvalidPlayerTypeConversion");
    }
}

static void 
Login2ConvertPlayerType_Enter(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    Entity *playerEnt;
    if ( !Login2EnterStateCommon(loginState, LOGIN2_STATE_CONVERT_PLAYER_TYPE, s_TimeoutTransaction) )
    {
        return;
    }

    playerEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, loginState->selectedCharacterID);
    if ( playerEnt == NULL )
    {
        aslLogin2_FailLogin(loginState, "Login2_InternalError");
        return;
    }

    if ( !login_ConvertCharacterPlayerType(playerEnt, loginState->playerType, ConvertPlayerType_CB, aslLogin2_GetShortLoginCookieAsPointer(loginState)) )
    {
        aslLogin2_FailLogin(loginState, "Login2_InvalidPlayerTypeConversion");
        return;
    }

    loginState->playerTypeConversionComplete = false;
}

static void 
Login2ConvertPlayerType_BeginFrame(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2BeginFrameCommon(loginState) )
    {
        return;
    }

    // Return to character selection if the client requests it.
    if ( loginState->clientRequestedReturnToCharacterSelect )
    {
        ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_RETURN_TO_CHARACTER_SELECT);
        return;
    }

    if ( loginState->playerTypeConversionComplete )
    {
        if ( loginState->requestedUGCEdit )
        {
            ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_UGC_PROJECT_SELECT);
        }
        else
        {
            ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_GET_MAP_CHOICES);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
//
// Code for state LOGIN2_STATE_GET_MAP_CHOICES
//
//////////////////////////////////////////////////////////////////////////

//
// Wrapper function to get the most recent map from the map history.
//  If the new map history is not present, it knows how to look in the old
//  earray stack map history.
//
SavedMapDescription *
aslLogin_GetCurrentMap(Entity *pEnt)
{
    SavedMapDescription *pCurrentMap = NULL;

    if ( ( pEnt == NULL ) || ( pEnt->pSaved == NULL ) )
    {
        return NULL;
    }

    pCurrentMap = entity_GetLastMap(pEnt);
    if ( ( pCurrentMap == NULL ) && ( eaSize(&pEnt->pSaved->obsolete_mapHistory) > 0 ) )
    {
        // Old map history still exists, so use that
        int mapHistorySize = eaSize(&pEnt->pSaved->obsolete_mapHistory);

        pCurrentMap = (SavedMapDescription *)CopyOldSavedMapDescription(pEnt->pSaved->obsolete_mapHistory[mapHistorySize-1]);
    }

    return pCurrentMap;
}

//
// Wrapper function to get the most recent static map from the map history.
//  If the new map history is not present, it knows how to look in the old
//  earray stack map history.
//
SavedMapDescription *
aslLogin_GetLastStaticMap(Entity *pEnt)
{
    SavedMapDescription *pLastStaticMap = NULL;

    if ( ( pEnt == NULL ) || ( pEnt->pSaved == NULL ) )
    {
        return NULL;
    }

    pLastStaticMap = entity_GetLastStaticMap(pEnt);
    if ( ( pLastStaticMap == NULL ) && ( eaSize(&pEnt->pSaved->obsolete_mapHistory) > 0 ) )
    {
        // Old map history still exists, so use that
        int mapHistorySize = eaSize(&pEnt->pSaved->obsolete_mapHistory);
        int i;

        for ( i = mapHistorySize - 1; i >= 0; i-- )
        {
            ZoneMapInfo *zoneMapInfo = zmapInfoGetByPublicName(pEnt->pSaved->obsolete_mapHistory[i]->mapDescription);
            ZoneMapType mapType;

            if ( zoneMapInfo != NULL )
            {
                // don't trust the type in map history if we can avoid it
                mapType = zmapInfoGetMapType(zoneMapInfo);
            }
            else
            {
                mapType = pEnt->pSaved->obsolete_mapHistory[i]->eMapType;
            }
            if ( mapType == ZMTYPE_STATIC )
            {
                pLastStaticMap = (SavedMapDescription *)CopyOldSavedMapDescription(pEnt->pSaved->obsolete_mapHistory[i]);
                break;
            }
        }
    }

    return pLastStaticMap;
}

static void
BuildMapSearchInfo(Entity *playerEnt, MapSearchInfo *requestedSearch, bool newCharacter, bool skipTutorial, AccountProxyKeyValueInfoContainer **accountKeyValues, MapSearchInfo **mainSearchOut, MapSearchInfo **backupSearchOut)
{
    bool debugPriv;
    bool sendToTutorialMap = false;

    MapSearchInfo *mainSearchInfo = NULL;
    MapSearchInfo *backupSearchInfo = NULL;

    if ( playerEnt == NULL || playerEnt->pPlayer == NULL || playerEnt->pSaved == NULL )
    {
        if ( mainSearchOut )
        {
            *mainSearchOut = NULL;
        }
        if ( backupSearchOut )
        {
            *backupSearchOut = NULL;
        }
        return;
    }

    debugPriv = playerEnt->pPlayer->accountAccessLevel && playerEnt->pPlayer->accountAccessLevel >= ACCESS_DEBUG;

    if (gConf.pchTutorialMissionName)
    {
        MissionInfo *missionInfo = mission_GetInfoFromPlayer(playerEnt);
        if (missionInfo == NULL || 
            (!mission_StateSucceeded(missionInfo, gConf.pchTutorialMissionName) && mission_GetNumTimesCompletedByName(missionInfo, gConf.pchTutorialMissionName) == 0))
        {
            sendToTutorialMap = true;
        }
    }

    if (g_ForceDebugPriv && !debugPriv)
    {
        debugPriv = true;
        if (!g_isContinuousBuilder)
        {
            CRITICAL_NETOPS_ALERT("FORCE_DEBUG_PRIV", "sbForceDebugPriv is set in aslLoginMapTransfer.c. This should only be set for continuous builders");
        }
    }

    mainSearchInfo = StructCreate(parse_MapSearchInfo);


    mainSearchInfo->playerAccountID = playerEnt->pPlayer->accountID;
    mainSearchInfo->playerType = playerEnt->pPlayer->playerType;
    mainSearchInfo->playerID = playerEnt->myContainerID;
    mainSearchInfo->iGuildID = guild_GetGuildID(playerEnt);
    mainSearchInfo->newCharacter = newCharacter;
    mainSearchInfo->teamID = team_GetTeamID(playerEnt);
    mainSearchInfo->bSkipTutorial = skipTutorial;
    mainSearchInfo->iAccessLevel = playerEnt->pPlayer->accessLevel;
    mainSearchInfo->bExpectedTrasferFromShardNS = playerEnt->pSaved->timeLastImported > playerEnt->pPlayer->timeLastVerifyEntityMissionData;
    mainSearchInfo->requestFallback = true;

    if ( ( requestedSearch->developerAllStatic && debugPriv && !gLoginServerState.bAllowSkipTutoral ) ) 
    {
        mainSearchInfo->developerAllStatic = true;
        aslLogin2_Log("logging in with developerAllStatic");
    }
    else if ( requestedSearch->debugPosLogin && debugPriv )
    {
        mainSearchInfo->debugPosLogin = true;
        StructCopyAll(parse_MapDescription, &requestedSearch->baseMapDescription, &mainSearchInfo->baseMapDescription);
        mainSearchInfo->baseMapDescription.spawnPoint = allocAddString(POSITION_SET);
        aslLogin2_Log("logging in with debugPosLogin");
    }
    else if ( requestedSearch->safeLogin )
    {
        if ( playerEnt && aslLogin_GetLastStaticMap(playerEnt) )
        {
            //if someone is doing safe login, try to log them back into their last static map
            SavedMapDescription *pLastStaticMap = aslLogin_GetLastStaticMap(playerEnt);

            StructCopyAllVoid(parse_MapDescription, pLastStaticMap, &mainSearchInfo->baseMapDescription);
            aslLogin2_Log("doing safe mode login");
        }
        else
        {
            //fallback if there's somehow no character or map history, do new character map
            mainSearchInfo->newCharacter = true;
            aslLogin2_Log("wanted to do safe mode login, no map history, reverting to new char login");
        }
    }
    else if (sendToTutorialMap)
    {
        mainSearchInfo->eSearchType = MAPSEARCHTYPE_NEWPLAYER;
        mainSearchInfo->bSkipTutorial = false;
    }
    else
    {	
        SavedMapDescription *lastStaticMap = aslLogin_GetLastStaticMap(playerEnt);
        SavedMapDescription *lastMap = aslLogin_GetCurrentMap(playerEnt);
        if ( playerEnt && playerEnt->pSaved && lastMap )
        {
            bool normalLogin = true;

            //If this player crashed/disconnected while trying to warp AND it's been less than an hour since they tried
            if ( playerEnt && playerEnt->pPlayer && playerEnt->pPlayer->pWarp)
            {
                PlayerWarpToData *warpData = playerEnt->pPlayer->pWarp;
                ZoneMapInfo *zoneInfo = warpData->pchMap ? zmapInfoGetByPublicName(warpData->pchMap) : NULL;
                U32 currentTime = timeSecondsSince2000();

                //Only attempt to do the warp if there's a zone, an entity, an account and it was attempted in the last hour
                if( zoneInfo &&
                    warpData->iEntID &&
                    warpData->iAccountID && 
                    currentTime > warpData->iTimestamp && 
                    currentTime - warpData->iTimestamp < 3600)
                {
                    //Fill in the unnatural login information
                    normalLogin = false;
                    mainSearchInfo->baseMapDescription.containerID = warpData->iMapID;
                    mainSearchInfo->baseMapDescription.iPartitionID = warpData->uPartitionID;
                    mainSearchInfo->baseMapDescription.mapInstanceIndex = warpData->iInstance;
                    mainSearchInfo->baseMapDescription.mapVariables = allocAddString(warpData->pcMapVariables);
                    mainSearchInfo->baseMapDescription.eMapType = zmapInfoGetMapType(zoneInfo);
                    mainSearchInfo->baseMapDescription.mapDescription = allocAddString(warpData->pchMap);
                    mainSearchInfo->baseMapDescription.spawnPoint = allocAddString(START_SPAWN);

                    // And get a backup map that can be returned too
                    if ( lastStaticMap && !IsSameMapDescription((MapDescription *)lastStaticMap, (MapDescription *)lastMap) )
                    {
                        backupSearchInfo = StructClone(parse_MapSearchInfo, mainSearchInfo);
                        StructCopyAllVoid(parse_MapDescription, lastStaticMap, &backupSearchInfo->baseMapDescription);
                    }
                    aslLogin2_Log("Logging in attempting a warp");
                }
                //If the warp is invalid, just remove it
                else
                {
                    StructDestroySafe(parse_PlayerWarpToData, &playerEnt->pPlayer->pWarp);
                }
            }

            //Try logging in normally
            if(normalLogin)
            {
                char *errorString = NULL;
                char *alertKey;
                StructCopyAllVoid(parse_MapDescription, lastMap, &mainSearchInfo->baseMapDescription);

                // make sure the map history didn't have a bogus map type
                mainSearchInfo->baseMapDescription.eMapType = 
                    MapCheckRequestedType(mainSearchInfo->baseMapDescription.mapDescription, mainSearchInfo->baseMapDescription.eMapType, &errorString, &alertKey);
                if ( estrLength(&errorString) )
                {
                    char *mapSearchString = NULL;

                    if ( playerEnt->pPlayer->accessLevel == 0 )
                    {
                        ParserWriteText(&mapSearchString, parse_MapSearchInfo, mainSearchInfo, 0, 0, 0);

                        ErrorOrAlert(allocAddString(alertKey), "%s got an error from MapCheckRequestedType. Error: %s. MapSearchInfo: %s",
                            __FUNCTION__, errorString, mapSearchString);

                        estrDestroy(&mapSearchString);
                    }
                    estrDestroy(&errorString);
                }	

                // If that last map is special, then look back to find last non-special map as the backup choice
                if ( MapShouldOnlyBeReturnedToIfSameInstanceExists(&mainSearchInfo->baseMapDescription) )
                {
                    if ( lastStaticMap && !IsSameMapDescription((MapDescription *)lastStaticMap, (MapDescription *)lastMap) )
                    {
                        backupSearchInfo = StructClone(parse_MapSearchInfo, mainSearchInfo);
                        StructCopyAllVoid(parse_MapDescription, lastStaticMap, &backupSearchInfo->baseMapDescription);

                        if ( backupSearchInfo->baseMapDescription.containerID && backupSearchInfo->baseMapDescription.iPartitionID )
                        {
                            backupSearchInfo->eSearchType = MAPSEARCHTYPE_SPECIFIC_CONTAINER_AND_PARTITION_ID_OR_OTHER;
                        }
                        else
                        {
                            backupSearchInfo->baseMapDescription.containerID = 0;
                            backupSearchInfo->baseMapDescription.iPartitionID = 0;
                            backupSearchInfo->eSearchType = MAPSEARCHTYPE_ONE_MAPNAME_ALL_CHOICES;
                        }

                        if ( mainSearchInfo->baseMapDescription.containerID && mainSearchInfo->baseMapDescription.iPartitionID )
                        {
                            mainSearchInfo->eSearchType = MAPSEARCHTYPE_SPECIFIC_CONTAINER_AND_PARTITION_ID_ONLY;
                        }
                        else
                        {
                            //if we don't have both container and partition ID then the search can't possibly succeed,
                            //just fall back to backup map search
                            StructDestroy(parse_MapSearchInfo, mainSearchInfo);
                            mainSearchInfo = backupSearchInfo;
                            backupSearchInfo = NULL;
                        }					
                    }
                    else if (!lastStaticMap)
                    {
                        aslLogin2_Log("Found a mission map in the map history but no static map, doing the main/backup logic");
                        //this is the case where the player has never played a static map, but has played a mission map, presumably
                        //because the new player map is a mission map. So we want the backup map search info to be mostly the same as
                        //the main map search info, but with no spawn point, so they either play in precisely the same partition they were
                        //previously, or in a new one but spawning at default spawn
                        backupSearchInfo = StructClone(parse_MapSearchInfo, mainSearchInfo);

                        mainSearchInfo->eSearchType = MAPSEARCHTYPE_SPECIFIC_CONTAINER_AND_PARTITION_ID_ONLY;

                        backupSearchInfo->eSearchType = MAPSEARCHTYPE_OWNED_MAP;
                        backupSearchInfo->baseMapDescription.spawnPoint = START_SPAWN;
                    }
                }
                aslLogin2_Log("logging in normally (with map history)");
            }

        }
        else if ( playerEnt )
        {
            //in this case, we have an entity but no map history. This must mean that a new character crashed or quit
            //after character creation before successfully logging into a map.
            mainSearchInfo->newCharacter = true;

            aslLogin2_Log("logging in normally (no map history)");
        }
    }

    mainSearchInfo->baseMapDescription.iVirtualShardID = playerEnt->pPlayer->iVirtualShardID;
    if ( backupSearchInfo )
    {
        backupSearchInfo->baseMapDescription.iVirtualShardID = playerEnt->pPlayer->iVirtualShardID;
    }

    mainSearchInfo->pPlayerName = strdup(playerEnt->pSaved->savedName);
    mainSearchInfo->pAccountName = strdup(playerEnt->pPlayer->privateAccountName);
    mainSearchInfo->pAllegiance = strdup(REF_STRING_FROM_HANDLE(playerEnt->hAllegiance));

    // If account key values were provided, then add them to the search info.
    if ( eaSize(&accountKeyValues) )
    {
        int i;

        mainSearchInfo->pAccountPermissions = StructCreate(parse_AccountProxyKeyValueInfoList);
        for ( i = eaSize(&accountKeyValues) - 1; i >= 0; i-- )
        {
            eaPush(&mainSearchInfo->pAccountPermissions->ppList, (AccountProxyKeyValueInfo *)StructClone(parse_AccountProxyKeyValueInfoContainer, accountKeyValues[i]));
        }
    }

    if ( backupSearchInfo )
    {
        backupSearchInfo->pPlayerName = strdup(mainSearchInfo->pPlayerName);
        backupSearchInfo->pAccountName = strdup(mainSearchInfo->pAccountName);
        backupSearchInfo->pAllegiance = strdup(mainSearchInfo->pAllegiance);

        // If account key values were provided, then add them to the search info.
        if ( mainSearchInfo->pAccountPermissions )
        {
            backupSearchInfo->pAccountPermissions = StructClone(parse_AccountProxyKeyValueInfoList, mainSearchInfo->pAccountPermissions);
        }
    }

    if ( mainSearchOut )
    {
        *mainSearchOut = mainSearchInfo;
    }
    if ( backupSearchOut )
    {
        *backupSearchOut = backupSearchInfo;
    }

    return;
}

static void
ModifyPossibleMapChoices(PossibleMapChoices *mapChoices, MapDescription *lastMap, bool developerAllStatic, bool debugPosLogin)
{
    if ( lastMap && !debugPosLogin)
    {	
        int i;
        //first check for the precisely identical server/partition... if so, set it to bLastMap.

        for ( i = 0; i < eaSize(&mapChoices->ppChoices); i++ )
        {
            MapDescription *otherDescription = &mapChoices->ppChoices[i]->baseMapDescription;

            if ( otherDescription->iPartitionID == lastMap->iPartitionID 
                && otherDescription->containerID
                && otherDescription->containerID == lastMap->containerID

                //can't use IsSameMapDescription here because the map ownership may have changed
                && stricmp_safe(otherDescription->mapDescription, lastMap->mapDescription) == 0 )
            {
                // Add the last map to front of list
                PossibleMapChoice *newChoice;
                if ( !developerAllStatic )
                {
                    newChoice = mapChoices->ppChoices[i];
                    eaRemove(&mapChoices->ppChoices, i);
                }
                else
                {
                    newChoice = StructClone(parse_PossibleMapChoice, mapChoices->ppChoices[i]);
                }

                newChoice->bLastMap = true;
                eaInsert(&mapChoices->ppChoices, newChoice, 0);

                return; //Return for found instance, or for new map if instance not found					
            }
        }

        for ( i = 0; i < eaSize(&mapChoices->ppChoices); i++ )
        {
            if ( ( mapChoices->ppChoices[i]->baseMapDescription.mapInstanceIndex == 0 || mapChoices->ppChoices[i]->baseMapDescription.mapInstanceIndex == lastMap->mapInstanceIndex ) &&
                IsSameMapDescription(lastMap, &mapChoices->ppChoices[i]->baseMapDescription) )
            {
                // Add the last map to front of list
                PossibleMapChoice *newChoice;
                if ( !developerAllStatic )
                {
                    newChoice = mapChoices->ppChoices[i];
                    eaRemove(&mapChoices->ppChoices, i);
                }
                else
                {
                    newChoice = StructClone(parse_PossibleMapChoice, mapChoices->ppChoices[i]);
                }

                newChoice->bLastMap = true;
                eaInsert(&mapChoices->ppChoices, newChoice, 0);

                return; //Return for found instance, or for new map if instance not found				
            }
        }
    }
    return;
}

static void
GetMapChoices_CB(TransactionReturnVal *returnVal, void *userData)
{
    Login2State *loginState = aslLogin2_GetActiveLoginStateShortCookiePointer(userData);
    PossibleMapChoices *possibleMapChoices = NULL;
    Entity *playerEnt;

    if ( !ValidateLoginState(loginState) )
    {
        aslLogin2_Log("Command %s returned with invalid loginState.  shortCookie=%p", __FUNCTION__, userData);
        return;
    }

    if (!ISM_IsStateActive(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_GET_MAP_CHOICES))
    {
        aslLogin2_FailLogin(loginState, "Login2_InternalError");
        return;
    }

    // Get the player entity.
    playerEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, loginState->selectedCharacterID);
    if ( playerEnt == NULL || playerEnt->pPlayer == NULL || playerEnt->pSaved == NULL )
    {
        aslLogin2_FailLogin(loginState, "Login2_InternalError");
        return;
    }

    if ( RemoteCommandCheck_GetPossibleMapChoices(returnVal, &possibleMapChoices) == TRANSACTION_OUTCOME_SUCCESS )
    {
        MapDescription *lastMap = (MapDescription *)aslLogin_GetCurrentMap(playerEnt);
        ModifyPossibleMapChoices(possibleMapChoices, lastMap, loginState->mainSearchInfo->developerAllStatic, loginState->mainSearchInfo->debugPosLogin);

        if ( loginState->newMapChoices )
        {
            StructDestroy(parse_PossibleMapChoices, loginState->newMapChoices);
        }
        loginState->newMapChoices = possibleMapChoices;
    }
    else
    {
        aslLogin2_FailLogin(loginState, "Login2_MapSearchFailure");
        return;
    }
}

static void 
Login2GetMapChoices_Enter(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    Entity *playerEnt;
   
    if ( !Login2EnterStateCommon(loginState, LOGIN2_STATE_GET_MAP_CHOICES, s_TimeoutInshard) )
    {
        return;
    }

    // Get the player entity.
    playerEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, loginState->selectedCharacterID);
    if ( playerEnt == NULL || playerEnt->pPlayer == NULL || playerEnt->pSaved == NULL )
    {
        aslLogin2_FailLogin(loginState, "Login2_InternalError");
        return;
    }

    // At this point we switch from account access level to character access level.
    loginState->clientAccessLevel = playerEnt->pPlayer->accessLevel;
}

static void 
Login2GetMapChoices_BeginFrame(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2BeginFrameCommon(loginState) )
    {
        return;
    }

    // Return to character selection if the client requests it.
    if ( loginState->clientRequestedReturnToCharacterSelect )
    {
        ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_RETURN_TO_CHARACTER_SELECT);
        return;
    }

    // If the client has requested a gameserver address, we can move on to the next state.
    if ( loginState->requestedGameserver )
    {
        ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_REQUEST_GAMESERVER_ADDRESS);
        return;
    }

    // If there is a new search request, process it.
    if ( loginState->requestedMapSearch )
    {
        Entity *playerEnt;
        GameAccountData *gameAccountData;
        EARRAY_OF(AccountProxyKeyValueInfoContainer) accountKeyValues = NULL;

        // Move this search to active.
        if ( loginState->activeMapSearch )
        {
            StructDestroy(parse_MapSearchInfo, loginState->activeMapSearch);
        }
        loginState->activeMapSearch = loginState->requestedMapSearch;
        loginState->requestedMapSearch = NULL;

        // Get the player entity.
        playerEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, loginState->selectedCharacterID);
        if ( playerEnt == NULL || playerEnt->pPlayer == NULL || playerEnt->pSaved == NULL )
        {
            aslLogin2_FailLogin(loginState, "Login2_InternalError");
            return;
        }

        // Get account key/values from GameAccountData.
        gameAccountData = GET_REF(loginState->hGameAccountData);
        if ( gameAccountData )
        {
            accountKeyValues = (EARRAY_OF(AccountProxyKeyValueInfoContainer))gameAccountData->eaAccountKeyValues;
        }

        // Clear any previous search info.
        if ( loginState->mainSearchInfo )
        {
            StructDestroy(parse_MapSearchInfo, loginState->mainSearchInfo);
            loginState->mainSearchInfo = NULL;
        }
        if ( loginState->backupSearchInfo )
        {
            StructDestroy(parse_MapSearchInfo, loginState->backupSearchInfo);
            loginState->backupSearchInfo = NULL;
        }

        BuildMapSearchInfo(playerEnt, loginState->activeMapSearch, false, false, accountKeyValues, &loginState->mainSearchInfo, &loginState->backupSearchInfo);

        if ( loginState->mainSearchInfo == NULL )
        {
            aslLogin2_FailLogin(loginState, "Login2_MapSearchFailure");
            return;
        }

        RemoteCommand_GetPossibleMapChoices(objCreateManagedReturnVal(GetMapChoices_CB, aslLogin2_GetShortLoginCookieAsPointer(loginState)),
            GLOBALTYPE_MAPMANAGER, 0, loginState->mainSearchInfo, loginState->backupSearchInfo, "Initial Login from LoginServer");
    }

    // If new map choices are ready, send them to the client.
    if ( loginState->newMapChoices )
    {
        if ( loginState->lastMapChoices )
        {
            StructDestroy(parse_PossibleMapChoices, loginState->lastMapChoices);
        }
        loginState->lastMapChoices = loginState->newMapChoices;
        loginState->newMapChoices = NULL;

        // Send choices to the client.
        aslLogin2_SendMapChoices(loginState, loginState->lastMapChoices);
    }
}
//////////////////////////////////////////////////////////////////////////
//
// Code for state LOGIN2_STATE_REQUEST_GAMESERVER_ADDRESS
//
//////////////////////////////////////////////////////////////////////////

static void
RequestGameserverAddress_CB(TransactionReturnVal *returnVal, void *userData)
{
    Login2State *loginState = aslLogin2_GetActiveLoginStateShortCookiePointer(userData);
    if ( !ValidateLoginState(loginState) )
    {
        aslLogin2_Log("Command %s returned with invalid loginState.  shortCookie=%p", __FUNCTION__, userData);
        return;
    }

    if ( !ISM_IsStateActive(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_REQUEST_GAMESERVER_ADDRESS) )
    {
        aslLogin2_FailLogin(loginState, "Login2_InvalidState");
        return;
    }

    if ( loginState->gameserverAddress )
    {
        StructDestroy(parse_ReturnedGameServerAddress, loginState->gameserverAddress);
        loginState->gameserverAddress = NULL;
    }

    if ( RemoteCommandCheck_RequestNewOrExistingGameServerAddress(returnVal, &loginState->gameserverAddress) != TRANSACTION_OUTCOME_SUCCESS )
    {
        aslLogin2_FailLogin(loginState, "Login2_GameserverNotFound");
        return;
    }
    else
    {
        if ( loginState->gameserverAddress->iContainerID == 0 )
        {
            aslLogin2_FailLogin(loginState, "Login2_GameserverNotFound");
            return;
        }
    }
}

static void 
Login2RequestGameserverAddress_Enter(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    int i;
    bool found = false;
    NewOrExistingGameServerAddressRequesterInfo requesterInfo = {0};
    Entity *playerEnt;
    bool startingUGCEdit;

    if ( !Login2EnterStateCommon(loginState, LOGIN2_STATE_REQUEST_GAMESERVER_ADDRESS, s_TimeoutGameserver) )
    {
        return;
    }

    startingUGCEdit = ( loginState->chosenUGCProjectForEdit != NULL );

    if ( !startingUGCEdit )
    {
        // Verify that the requested map we got from the client is one of the choices we sent in the first place.
        for ( i = eaSize(&loginState->lastMapChoices->ppChoices) - 1; i >= 0; i-- )
        {
            if ( StructCompare(parse_PossibleMapChoice, loginState->requestedGameserver, loginState->lastMapChoices->ppChoices[i], 0, 0, TOK_USEROPTIONBIT_1) == 0 )
            {
                found = true;
                break;
            }
        }

        if ( !found )
        {
            aslLogin2_FailLogin(loginState, "Login2_BadMapChoice");
            return;
        }
    }

    playerEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, loginState->selectedCharacterID);
    if ( playerEnt == NULL || playerEnt->pPlayer == NULL || playerEnt->pSaved == NULL )
    {
        aslLogin2_FailLogin(loginState, "Login2_InternalError");
        return;
    }

    requesterInfo.pcRequestingShardName = GetShardNameFromShardInfoString();
    requesterInfo.eRequestingServerType = GetAppGlobalType();
    requesterInfo.iRequestingServerID = GetAppGlobalID();
    requesterInfo.iEntContainerID = playerEnt->myContainerID;
    requesterInfo.iPlayerAccountID = entGetAccountID(playerEnt);
    requesterInfo.iPlayerLangID = playerEnt->pPlayer->langID;
	if(g_isContinuousBuilder)
		requesterInfo.pPlayerAccountName = playerEnt->pPlayer->privateAccountName;
	else
		requesterInfo.pPlayerAccountName = playerEnt->pPlayer->publicAccountName;
    requesterInfo.iPlayerIdentificationCookie = Login2_ShortenToken(loginState->loginCookie);
    requesterInfo.pPlayerName = playerEnt->pSaved->savedName;
    requesterInfo.iRequestingTeamID = team_GetTeamID(playerEnt);

    // exactly one of chosenUGCProjectForEdit and requestedGameserver must be non-NULL, and the other one must be NULL.
    devassertmsg(( loginState->chosenUGCProjectForEdit == NULL ) != ( loginState->requestedGameserver == NULL ), "Must have exactly one of requested UGC project or requested gameserver");
    RemoteCommand_RequestNewOrExistingGameServerAddress(objCreateManagedReturnVal_TransactionMayTakeALongTime(RequestGameserverAddress_CB, aslLogin2_GetShortLoginCookieAsPointer(loginState)), 
        GLOBALTYPE_MAPMANAGER, 0, loginState->requestedGameserver, loginState->chosenUGCProjectForEdit, &requesterInfo);
}

static void 
Login2RequestGameserverAddress_BeginFrame(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2BeginFrameCommon(loginState) )
    {
        return;
    }

    // Return to character selection if the client requests it.
    if ( loginState->clientRequestedReturnToCharacterSelect )
    {
        ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_RETURN_TO_CHARACTER_SELECT);
        return;
    }

    // Once the gameserver address arrives, transfer the character.
    if ( loginState->gameserverAddress )
    {
        ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_SEND_CHARACTER_TO_GAMESERVER);
    }
}

//////////////////////////////////////////////////////////////////////////
//
// Code for state LOGIN2_STATE_SEND_CHARACTER_TO_GAMESERVER
//
//////////////////////////////////////////////////////////////////////////

static void
SendCharacterToGameserver_CB(TransactionReturnVal *returnVal, void *userData)
{
    Login2State *loginState = aslLogin2_GetActiveLoginStateShortCookiePointer(userData);
    if ( !ValidateLoginState(loginState) )
    {
        aslLogin2_Log("Command %s returned with invalid loginState.  shortCookie=%p", __FUNCTION__, userData);
        return;
    }

    if ( !ISM_IsStateActive(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_SEND_CHARACTER_TO_GAMESERVER) )
    {
        aslLogin2_FailLogin(loginState, "Login2_InvalidState");
        return;
    }

    if ( returnVal->eOutcome != TRANSACTION_OUTCOME_SUCCESS )
    {
        aslLogin2_FailLogin(loginState, "Login2_GameserverTransferFailed");
        return;
    }

    loginState->transferToGameserverComplete = true;
}

static void 
Login2SendCharacterToGameserver_Enter(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2EnterStateCommon(loginState, LOGIN2_STATE_SEND_CHARACTER_TO_GAMESERVER, s_TimeoutTransaction) )
    {
        return;
    }

    loginState->transferToGameserverComplete = false;

    objRequestContainerMove(objCreateManagedReturnVal(SendCharacterToGameserver_CB, aslLogin2_GetShortLoginCookieAsPointer(loginState)),
        GLOBALTYPE_ENTITYPLAYER, loginState->selectedCharacterID, objServerType(), objServerID(), GLOBALTYPE_GAMESERVER, loginState->gameserverAddress->iContainerID);
}

static void 
Login2SendCharacterToGameserver_BeginFrame(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2BeginFrameCommon(loginState) )
    {
        return;
    }

    if ( loginState->transferToGameserverComplete )
    {
        // Log for logparser unique login graph.
        servLog(LOG_LOGIN, "Login2TransferToGameserverComplete", "AccountID %u EntityID %u", loginState->accountID, loginState->selectedCharacterID);

        // Send the gameserver address to the client.
        loginState->gameserverAddress->iCookie = (int)Login2_ShortenToken(loginState->loginCookie);
        aslLogin2_SendGameserverAddress(loginState, loginState->gameserverAddress);

        // Close the client netlink.
        linkFlushAndClose(&loginState->netLink, "Transfer successful");

        ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_COMPLETE);
    }
}

//////////////////////////////////////////////////////////////////////////
//
// Code for state LOGIN2_STATE_COMPLETE
//
//////////////////////////////////////////////////////////////////////////

static void 
Login2Complete_Enter(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2EnterStateCommon(loginState, LOGIN2_STATE_COMPLETE, 0) )
    {
        return;
    }
}

//////////////////////////////////////////////////////////////////////////
//
// Code for state LOGIN2_STATE_UGC_PROJECT_SELECT
//
//////////////////////////////////////////////////////////////////////////


static bool 
UGCProjectIDEq(const PossibleUGCProject* chosenProj, const PossibleUGCProject* existingProj)
{
    if( chosenProj->iCopyID == 0 ) 
    {
        // edit -- must match exactly
        return (chosenProj->iID == existingProj->iID && chosenProj->iCopyID == existingProj->iCopyID);
    } 
    else 
    {
        // import -- the id may be in either field
        return chosenProj->iCopyID == existingProj->iID || chosenProj->iCopyID == existingProj->iCopyID;
    }
}

static void 
Login2UGCProjectSelect_Enter(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2EnterStateCommon(loginState, LOGIN2_STATE_UGC_PROJECT_SELECT, s_TimeoutUser) )
    {
        return;
    }

    if ( loginState->chosenUGCProjectForEdit )
    {
        StructDestroy(parse_PossibleUGCProject, loginState->chosenUGCProjectForEdit);
        loginState->chosenUGCProjectForEdit = NULL;
    }

    if ( loginState->chosenUGCProjectForDelete )
    {
        StructDestroy(parse_PossibleUGCProject, loginState->chosenUGCProjectForDelete);
        loginState->chosenUGCProjectForDelete = NULL;
    }

    aslLoginSendUGCProjects(loginState);
}

static void 
Login2UGCProjectSelect_BeginFrame(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    GameAccountData *gameAccountData;
    int i;
    bool bFound = false;

    if ( !Login2BeginFrameCommon(loginState) )
    {
        return;
    }

    // Return to character selection if the client requests it.
    if ( loginState->clientRequestedReturnToCharacterSelect )
    {
        ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_RETURN_TO_CHARACTER_SELECT);
        return;
    }

    gameAccountData = GET_REF(loginState->hGameAccountData);

    // Fail if UGC gets turned off.
    if ( !GetUGCVirtualShardID() )
    {
        aslLogin2_FailLogin(loginState, "Login2_NoUGCShard");
        return;
    }

    if ( !aslLoginIsUGCAllowed() )
    {
        aslLogin2_FailLogin(loginState, "Login2_UGCEditingNotAllowed");
        return;
    }

    // If the number of available project slots changes, send an update to the client.
    if( ( loginState->prevSentToClientUGCProjectMaxSlots != aslLoginUGCGetProjectMaxSlots( gameAccountData ) ) ||
        ( loginState->prevSentToClientUGCSeriesMaxSlots != aslLoginUGCGetSeriesMaxSlots( gameAccountData ) ) ) 
    {
        aslLoginSendUGCProjects(loginState);
    }

    // Stay in this state until the client sends a chosen project.
    if ( loginState->chosenUGCProjectForEdit )
    {
        if ( !loginState->clientAccessLevel )
        {
            if ( !loginState->chosenUGCProjectForEdit->iEditQueueCookie || loginState->chosenUGCProjectForEdit->iEditQueueCookie != loginState->editQueueCookie )
            {
                aslLogin2_FailLogin(loginState, "Login2_InvalidEditQueueCookie");
                return;
            }
        }

        if ( loginState->chosenUGCProjectForEdit->iID == 0 )
        {
            char* estr = NULL;
            if (!UGCProject_ValidateNewProjectRequest(loginState->chosenUGCProjectForEdit, &estr))
            {
                aslLogin2_FailLogin(loginState, "Login2_InvalidProjectRequest");
                aslLogin2_Log("Invalid UGC project request: %s", estr);
                estrDestroy(&estr);
                return;
            }
            estrDestroy(&estr);
        }

        //verify that the chosenmap we got is one of the choices we sent to the game server in the first place
        if( loginState->possibleUGCProjects )
        {
            for ( i=0; i < eaSize(&loginState->possibleUGCProjects->ppProjects); i++ )
            {
                if ( UGCProjectIDEq(loginState->chosenUGCProjectForEdit, loginState->possibleUGCProjects->ppProjects[i]) )
                {
                    //copy over all other fields just because we don't really trust the client
                    UGCProjectInfo* pProjectInfo = StructClone(parse_UGCProjectInfo, loginState->chosenUGCProjectForEdit->pProjectInfo);
                    bool isNewProj = (loginState->chosenUGCProjectForEdit->iID == 0);
                    int lastCopyID = loginState->chosenUGCProjectForEdit->iCopyID;
                    StructCopy(parse_PossibleUGCProject, loginState->possibleUGCProjects->ppProjects[i], loginState->chosenUGCProjectForEdit, 0, 0, 0);

                    if ( isNewProj )
                    {
                        // Don't clear the UGCProjectInfo at all
                        loginState->chosenUGCProjectForEdit->pProjectInfo = pProjectInfo;
                        pProjectInfo = NULL;
                        loginState->chosenUGCProjectForEdit->iID = 0;
                        loginState->chosenUGCProjectForEdit->iCopyID = lastCopyID;
                    }

                    //except restore the edit queue cookie
                    loginState->chosenUGCProjectForEdit->iEditQueueCookie = loginState->editQueueCookie;

                    StructDestroySafe( parse_UGCProjectInfo, &pProjectInfo );
                    bFound = true;
                    break;
                }
            }
        }
        else
        {
            aslLogin2_FailLogin(loginState, "Login2_UGCProjectChoiceCorruption");
            return;
        }

        if( loginState->possibleUGCImports )
        {
            for ( i=0; i < eaSize(&loginState->possibleUGCImports->ppProjects); i++ )
            {
                if ( UGCProjectIDEq(loginState->chosenUGCProjectForEdit, loginState->possibleUGCImports->ppProjects[i]) )
                {
                    //copy over all other fields just because we don't really trust the client
                    UGCProjectInfo* pProjectInfo = StructClone(parse_UGCProjectInfo, loginState->chosenUGCProjectForEdit->pProjectInfo);
                    bool isNewProj = (loginState->chosenUGCProjectForEdit->iID == 0);
                    StructCopy(parse_PossibleUGCProject, loginState->possibleUGCProjects->ppProjects[i], loginState->chosenUGCProjectForEdit, 0, 0, 0);

                    if ( isNewProj )
                    {
                        // Don't clear the UGCProjectInfo at all
                        loginState->chosenUGCProjectForEdit->pProjectInfo = pProjectInfo;
                        pProjectInfo = NULL;
                    }

                    //except restore the edit queue cookie
                    loginState->chosenUGCProjectForEdit->iEditQueueCookie = loginState->editQueueCookie;

                    StructDestroySafe( parse_UGCProjectInfo, &pProjectInfo );
                    bFound = true;
                    break;
                }
            }
        }

        if ( !bFound )
        {
            aslLogin2_FailLogin(loginState, "Login2_UGCProjectChoiceCorruption");
            return;
        }

        if ( !loginState->selectedCharacterID )
        {
            char *pTemp = NULL;
            ParserWriteText(&pTemp, parse_Login2State, loginState, 0, 0, 0);
            log_printf(LOG_BUG, "Login server was about to request a UGC map transfer with no ent container ID. LoginState: %s", pTemp);
            AssertOrAlert("NO_ENT_FOR_XFER", "Login server was about to request a UGC map transfer with no ent container ID... presumably causing STO-29274");
            aslLogin2_FailLogin(loginState, "Login2_UGCNoPlayerID");
            estrDestroy(&pTemp);
            return;
        }

        gameAccountData = GET_REF(loginState->hGameAccountData);
        if ( aslLoginCheckAccountPermissionsForUgcPublishBanned(gameAccountData) )
        {
            loginState->chosenUGCProjectForEdit->iPossibleUGCProjectFlags |= POSSIBLEUGCPROJECT_FLAG_NOPUBLISHING;
        }

        //access level players bypass the queue
        if ( loginState->clientAccessLevel )
        {
            loginState->chosenUGCProjectForEdit->iEditQueueCookie = 0;
        }

        if( ugcDefaultsAuthorAllowsFeaturedBlocksEditing() && SAFE_MEMBER(loginState->chosenUGCProjectForEdit->pStatus, bAuthorAllowsFeatured) ) 
        {
            // Need to clear the "author allows featured" flag on the project before editing.
            ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_UGC_CLEAR_AUTHOR_ALLOWS_FEATURED);
        }
        else
        {
            // Ready to edit, so get gameserver address.
            ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_REQUEST_GAMESERVER_ADDRESS);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
//
// Code for state LOGIN2_STATE_UGC_CLEAR_AUTHOR_ALLOWS_FEATURED
//
//////////////////////////////////////////////////////////////////////////

static void 
Login2UGCClearAuthorAllowsFeatured_Enter(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2EnterStateCommon(loginState, LOGIN2_STATE_UGC_CLEAR_AUTHOR_ALLOWS_FEATURED, s_TimeoutIntershard) )
    {
        return;
    }

    loginState->authorAllowsFeaturedCleared = false;

    RemoteCommand_Intershard_aslUGCDataManager_ClearAuthorAllowsFeatured(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
        loginState->chosenUGCProjectForEdit->iID, GetShardNameFromShardInfoString(), GetAppGlobalID(), loginState->loginCookie);
}

static void 
Login2UGCClearAuthorAllowsFeatured_BeginFrame(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2BeginFrameCommon(loginState) )
    {
        return;
    }

    // Stay in this state until we hear that the flag has been cleared.
    if ( loginState->authorAllowsFeaturedCleared )
    {
        // Ready to edit, so get gameserver address.
        ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_REQUEST_GAMESERVER_ADDRESS);
    }
}

//////////////////////////////////////////////////////////////////////////
//
// Code for state LOGIN2_STATE_REDIRECT
//
//////////////////////////////////////////////////////////////////////////

static void 
Login2Redirect_Enter(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2EnterStateCommon(loginState, LOGIN2_STATE_REDIRECT, s_TimeoutIntershard) )
    {
        return;
    }

    aslLogin2_RedirectLogin(loginState, loginState->redirectShardName);
}

static void 
Login2Redirect_BeginFrame(InstancedMachineHandleOrName stateMachineHandle, Login2State *loginState, F32 elapsedTime)
{
    if ( !Login2BeginFrameCommon(loginState) )
    {
        return;
    }

    // Stay in this state until we get the destination loginserver address info.
    if ( loginState->redirectDestinationInfo )
    {
        // Send address of loginserver in remote shard to the client.
        aslLogin2_SendClientRedirect(loginState, loginState->redirectDestinationInfo->destinationIP, loginState->redirectDestinationInfo->destinationPort, loginState->loginCookie);

        // Close the client link.
        linkFlushAndClose(&loginState->netLink, "Redirect complete");

        // We are done!
        ISM_SwitchToSibling(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_COMPLETE);
    }
}

void
aslLogin2_InitStateMachine(void)
{
    ISM_AddInstancedState(LOGIN2_STATE_MACHINE, LOGIN2_STATE_INITIAL_CONNECTION, Login2InitialConnection_Enter, Login2InitialConnection_BeginFrame, NULL, NULL);
    ISM_AddInstancedState(LOGIN2_STATE_MACHINE, LOGIN2_STATE_VALIDATE_TICKET, Login2ValidateTicket_Enter, Login2ValidateTicket_BeginFrame, NULL, NULL);
    ISM_AddInstancedState(LOGIN2_STATE_MACHINE, LOGIN2_STATE_GET_MACHINE_NAME_FROM_CLIENT, Login2GetMachineName_Enter, Login2GetMachineName_BeginFrame, NULL, NULL);
    ISM_AddInstancedState(LOGIN2_STATE_MACHINE, LOGIN2_STATE_SET_MACHINE_NAME, Login2SetMachineName_Enter, NULL, NULL, NULL);
    ISM_AddInstancedState(LOGIN2_STATE_MACHINE, LOGIN2_STATE_GENERATE_ONE_TIME_CODE, Login2GenerateOneTimeCode_Enter, Login2GenerateOneTimeCode_BeginFrame, NULL, NULL);
    ISM_AddInstancedState(LOGIN2_STATE_MACHINE, LOGIN2_STATE_VALIDATE_ONE_TIME_CODE, Login2ValidateOneTimeCode_Enter, NULL, NULL, NULL);
    ISM_AddInstancedState(LOGIN2_STATE_MACHINE, LOGIN2_STATE_REFRESH_GAME_ACCOUNT_DATA, Login2RefreshGameAccountData_Enter, Login2RefreshGameAccountData_BeginFrame, NULL, NULL);
    ISM_AddInstancedState(LOGIN2_STATE_MACHINE, LOGIN2_STATE_GET_CHARACTER_LIST, Login2GetCharacterList_Enter, Login2GetCharacterList_BeginFrame, NULL, NULL);
    ISM_AddInstancedState(LOGIN2_STATE_MACHINE, LOGIN2_STATE_BOOT_PLAYER, Login2BootingPlayer_Enter, Login2BootingPlayer_BeginFrame, NULL, NULL);
    ISM_AddInstancedState(LOGIN2_STATE_MACHINE, LOGIN2_STATE_RETURN_TO_CHARACTER_SELECT, Login2ReturnToCharacterSelect_Enter, Login2ReturnToCharacterSelect_BeginFrame, NULL, NULL);
    ISM_AddInstancedState(LOGIN2_STATE_MACHINE, LOGIN2_STATE_CHARACTER_SELECT, Login2CharacterSelect_Enter, Login2CharacterSelect_BeginFrame, NULL, NULL);
    ISM_AddInstancedState(LOGIN2_STATE_MACHINE, LOGIN2_STATE_DELETE_CHARACTER, Login2DeleteCharacter_Enter, Login2DeleteCharacter_BeginFrame, NULL, NULL);
    ISM_AddInstancedState(LOGIN2_STATE_MACHINE, LOGIN2_STATE_RENAME_CHARACTER, Login2RenameCharacter_Enter, Login2RenameCharacter_BeginFrame, NULL, NULL);
    ISM_AddInstancedState(LOGIN2_STATE_MACHINE, LOGIN2_STATE_CREATE_CHARACTER, Login2CreateCharacter_Enter, Login2CreateCharacter_BeginFrame, NULL, NULL);
    ISM_AddInstancedState(LOGIN2_STATE_MACHINE, LOGIN2_STATE_QUEUED, Login2Queued_Enter, Login2Queued_BeginFrame, NULL, NULL);
    ISM_AddInstancedState(LOGIN2_STATE_MACHINE, LOGIN2_STATE_ONLINE_AND_FIXUP, Login2OnlineAndFixup_Enter, Login2OnlineAndFixup_BeginFrame, NULL, NULL);
    ISM_AddInstancedState(LOGIN2_STATE_MACHINE, LOGIN2_STATE_GET_SELECTED_CHARACTER_LOCATION, Login2GetSelectedCharacterLocation_Enter, Login2GetSelectedCharacterLocation_BeginFrame, NULL, NULL);
    ISM_AddInstancedState(LOGIN2_STATE_MACHINE, LOGIN2_STATE_GET_SELECTED_CHARACTER, Login2GetSelectedCharacter_Enter, Login2GetSelectedCharacter_BeginFrame, NULL, NULL);
    ISM_AddInstancedState(LOGIN2_STATE_MACHINE, LOGIN2_STATE_CONVERT_PLAYER_TYPE, Login2ConvertPlayerType_Enter, Login2ConvertPlayerType_BeginFrame, NULL, NULL);
    ISM_AddInstancedState(LOGIN2_STATE_MACHINE, LOGIN2_STATE_GET_MAP_CHOICES, Login2GetMapChoices_Enter, Login2GetMapChoices_BeginFrame, NULL, NULL);
    ISM_AddInstancedState(LOGIN2_STATE_MACHINE, LOGIN2_STATE_REQUEST_GAMESERVER_ADDRESS, Login2RequestGameserverAddress_Enter, Login2RequestGameserverAddress_BeginFrame, NULL, NULL);
    ISM_AddInstancedState(LOGIN2_STATE_MACHINE, LOGIN2_STATE_SEND_CHARACTER_TO_GAMESERVER, Login2SendCharacterToGameserver_Enter, Login2SendCharacterToGameserver_BeginFrame, NULL, NULL);
    ISM_AddInstancedState(LOGIN2_STATE_MACHINE, LOGIN2_STATE_COMPLETE, Login2Complete_Enter, NULL, NULL, NULL);
    ISM_AddInstancedState(LOGIN2_STATE_MACHINE, LOGIN2_STATE_UGC_PROJECT_SELECT, Login2UGCProjectSelect_Enter, Login2UGCProjectSelect_BeginFrame, NULL, NULL);
    ISM_AddInstancedState(LOGIN2_STATE_MACHINE, LOGIN2_STATE_UGC_CLEAR_AUTHOR_ALLOWS_FEATURED, Login2UGCClearAuthorAllowsFeatured_Enter, Login2UGCClearAuthorAllowsFeatured_BeginFrame, NULL, NULL);
    ISM_AddInstancedState(LOGIN2_STATE_MACHINE, LOGIN2_STATE_REDIRECT, Login2Redirect_Enter, Login2Redirect_BeginFrame, NULL, NULL);
}

//tranfserLog all ISM state changes
void AslLogin2StateChangeLoggingCB(Login2State *loginState, char *pStateString)
{
	aslLogin2_Log("Account %u now in state %s\n", loginState->accountID, pStateString);

}

AUTO_COMMAND;
void LogAslLogin2StateChanges(int iSet)
{
	if (iSet)
	{
		ISM_SetNewStateDebugCB(LOGIN2_STATE_MACHINE, AslLogin2StateChangeLoggingCB);
	}
	else
	{
		ISM_SetNewStateDebugCB(LOGIN2_STATE_MACHINE, NULL);
	}
}

#include "AutoGen/aslLogin2_StateMachine_h_ast.c"

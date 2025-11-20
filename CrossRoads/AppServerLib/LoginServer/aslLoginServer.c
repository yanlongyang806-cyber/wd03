/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "sock.h"
#include "AppServerLib.h"
#include "aslLoginServer.h"
#include "aslLoginTokenParsing.h"
#include "aslLoginCStore.h"
#include "LoginCommon.h"
#include "AutoGen/LoginCommon_h_ast.h"
#include "Autogen/AppServerLib_autogen_SlowFuncs.h"
#include "AutoGen/GameServerLib_autogen_remotefuncs.h"
#include "AutoGen/ObjectDB_autogen_remotefuncs.h"
#include "AutoGen/AppServerLib_autogen_remotefuncs.h"
#include "AutoGen/ServerLib_autogen_RemoteFuncs.h"
#include "Serverlib.h"
#include "logcomm.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/EntitySavedData_h_ast.h"
#include "AutoGen/controller_autogen_remotefuncs.h"
#include "file.h"
#include "InstancedStateMachine.h"
#include "aslLoginCharacterSelect.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonTailor.h"
#include "netipfilter.h"
#include "AutoStartupSupport.h"
#include "SvrGlobalInfo.h"
#include "TimedCallback.h"
#include "StringCache.h"
#include "utilitiesLib.h"
#include "StringUtil.h"
#include "WorldLib.h"
#include "wlInteraction.h"
#include "WorldGrid.h"
#include "objSchema.h"
#include "Autogen/aslLoginServer_h_ast.h"
#include "ContinuousBuilderSupport.h"
#include "Player.h"
#include "Player_h_ast.h"
#include "PlayerBooter.h"
#include "timing.h"
#include "GameAccountData\GameAccountData.h"
#include "GameAccountDataCommon.h"
#include "NameGen.h"
#include "structNet.h"
#include "ResourceManager.h"
#include "AutoGen/GameAccountData_h_ast.h"
#include "AutoGen/NameGen_h_ast.h"
#include "UGCProjectCommon.h"
#include "UGCProjectCommon_h_ast.h"
#include "aslLoginUGCProject.h"
#include "autogen/AppServerLib_autotransactions_autogen_wrappers.h"
#include "..\NotifyCommon.h"
#include "VirtualShard_h_ast.h"
#include "GamePermissionsCommon.h"
#include "MicroTransactions.h"
#include "MicroTransactions_h_ast.h"
#include "GameSession.h"
#include "AutoGen/GameSession_h_ast.h"
#include "SteamCommonServer.h"
#include "SteamCommon.h"
#include "AutoGen/SteamCommon_h_ast.h"
#include "mission_common.h"
#include "Team.h"
#include "AutoGen/mission_common_h_ast.h"
#include "EntityLib.h"
#include "FolderCache.h"
#include "queue_common.h"
#include "ChoiceTable_common.h"
#include "UGCCommon.h"
#include "StringFormat.h"
#include "aslLogin2_EntityFixup.h"
#include "aslLogin2_GetCharacterChoices.h"
#include "aslLogin2_ValidateLoginTicket.h"
#include "aslLogin2_StateMachine.h"
#include "aslLogin2_ClientComm.h"
#include "aslLogin2_Error.h"
#include "Login2Common.h"
#include "ShardCommon.h"

#include "LoadScreen\LoadScreen_Common.h"
#include "AutoGen/LoadScreen_Common_h_ast.h"
#include "AutoGen/aslLoginServer_c_ast.h"
#include "AutoGen/accountnet_h_ast.h"

int giCurLinkCount = 0;

int gLoginServerListeningPort = 0;

LoginServerState gLoginServerState = {0};

ProjectLoginServerConfig gProjectLoginServerConfig = {0};

AUTO_CMD_INT(gLoginServerState.bAllowVersionMismatch, AllowVersionMismatch);
AUTO_CMD_INT(gLoginServerState.bIsTestingMode, SetTestingMode);
AUTO_CMD_INT(gLoginServerState.bAllowSkipTutoral, NormalMaps);	// allow developer to enter maps as if in production

StashTable sLoginLinksByID = NULL;

//force everyone into the login queue (presumably for testing purposes)
static bool sbNeverIgnoreLoginQueue = false;
AUTO_CMD_INT(sbNeverIgnoreLoginQueue, NeverIgnoreLoginQueue) ACMD_COMMANDLINE ACMD_CATEGORY(test);

//controller periodically reports to us the current totaly queue size
static int siReportedTotalQueueSize = 0;

//periodically updated value that is roughly equal the total number of active players on the shard
static U32 guTotalPlayers = 0;

static bool sbListenForGatewayProxies = false;
AUTO_CMD_INT(sbListenForGatewayProxies, ListenForGatewayProxies) ACMD_COMMANDLINE;

//if true, turn on link corruption for all client links to test for buffer overruns and such
bool giClientLinkCorruptionFrequency = 0;
AUTO_CMD_INT(giClientLinkCorruptionFrequency, ClientLinkCorruption) ACMD_CATEGORY(Debug);

//count of people past the LOGINSTATE_SELECTING_CHARACTER state
U32 guPendingLogins = 0;

//how long you can be connected after selecting a character
U32 guMapTransferTimeout = 5 * 60;
AUTO_CMD_INT(guMapTransferTimeout, MapTransferTimeout) ACMD_CMDLINE;

// Is UGC enabled (only valid if gConf->bUserContent is set)
bool gbUGCEnabled = true;
AUTO_CMD_INT(gbUGCEnabled, UGCEnabled);

//if true, do verbose link-state-updates even in prod mode
bool gbVerboseLinkUpdates = false;
AUTO_CMD_INT(gbVerboseLinkUpdates, VerboseLinkUpdates);

//if true, disable verbose printfs regardless of mode
bool gbNoVerboseUpdatesEver = false;
AUTO_CMD_INT(gbNoVerboseUpdatesEver, NoVerboseUpdatesEver);

//if true, disable machine locking check
bool gbMachineLockDisable = false;
AUTO_CMD_INT(gbMachineLockDisable, MachineLockDisable);

LoadScreenDynamic gLoadScreens = {0};

void ReportThatPlayerWhoWentThroughQueueHasLeft(void);

typedef struct LoginLinkSearchObject
{
	int searchCookie;
	GlobalType searchType;
	ContainerID searchID;
	LoginLink *foundLink;
} LoginLinkSearchObject;

bool aslLoginServerClientsAreUntrustworthy(void)
{
	return isProductionMode();
}

// LOGIN2TODO - remove
void loginLinkLog(LoginLink *pLink, char *pAction, char *pFormatStr, ...)
{
	char *pFullString = NULL;
	char *pSpecificString = NULL;
	char pDebugName[MAX_NAME_LEN];

	

	va_list ap;
	
	if (!pFormatStr)
	{
		pFormatStr = "";
	}

	va_start(ap, pFormatStr);


	estrStackCreate(&pFullString);
	estrStackCreate(&pSpecificString);


	estrConcatfv(&pFullString, pFormatStr, ap);
	va_end(ap);

	if (pLink->netLink)
	{
		estrPrintf(&pSpecificString, "IP %s%s", makeIpStr(pLink->ipRequest), pLink->bIsProxy ? "-proxied" : "");
	}

	objSetDebugName(pDebugName, MAX_NAME_LEN, pLink->clientType, pLink->clientID, pLink->accountID, pLink->characterName, pLink->displayName);

	objLog(LOG_LOGIN, pLink->clientType, pLink->clientID, pLink->accountID, pDebugName, NULL, pLink->accountName,
		pAction, pSpecificString, "%s", pFullString);

	estrDestroy(&pFullString);
	estrDestroy(&pSpecificString);
}

// LOGIN2TODO - remove
static int FindLoginLinkForCookieCB(NetLink *netLink, S32 index, LoginLink *link, LoginLinkSearchObject *searchObject)
{
	if (!searchObject->searchCookie || searchObject->foundLink)
	{
		return 0; // will never find it
	}
	if (link->loginCookie == searchObject->searchCookie)
	{
		searchObject->foundLink = link;
		return 0; // found it
	}
	return 1;
}

// LOGIN2TODO - remove
static int FindLoginLinkForContainerCB(NetLink *netLink, S32 index, LoginLink *link, LoginLinkSearchObject *searchObject)
{
	PERFINFO_AUTO_START_FUNC();
	if (!searchObject->searchType || !searchObject->searchID || searchObject->foundLink)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return 0; // will never find it
	}
	if (link->clientType == searchObject->searchType && link->clientID == searchObject->searchID)
	{
		searchObject->foundLink = link;
		PERFINFO_AUTO_STOP_FUNC();
		return 0; // found it
	}

	PERFINFO_AUTO_STOP_FUNC();
	return 1;
}

// LOGIN2TODO - remove
LoginLink *aslFindLoginLinkForCookie(int searchCookie)
{
	int i;
	LoginLinkSearchObject searchObject = {0};

    //LOGIN2TODO
    devassertmsg(false, "Find Login Link For Cookie");
	PERFINFO_AUTO_START_FUNC();
	searchObject.searchCookie = searchCookie;

	PERFINFO_AUTO_START("Walk Login Links", 1);
	for(i = 0; i < eaSize(&gLoginServerState.loginLinks); i++)
	{
		linkIterate2(gLoginServerState.loginLinks[i],FindLoginLinkForCookieCB, &searchObject);
	}
	PERFINFO_AUTO_STOP();

	if (searchObject.foundLink)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return searchObject.foundLink; 
	}

	PERFINFO_AUTO_START("Walk Queued Links", 1);
	for (i = 0; i < eaSize(&gLoginServerState.failedLogins); i++)
	{
		if (gLoginServerState.failedLogins[i]->loginCookie == searchCookie)
		{
			PERFINFO_AUTO_STOP();
			PERFINFO_AUTO_STOP_FUNC();
			return gLoginServerState.failedLogins[i];
		}
	}

	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP_FUNC();
	return NULL;
}

// LOGIN2TODO - remove
LoginLink *aslFindLoginLinkForContainer(GlobalType containerType, ContainerID containerID)
{
	int i;
	LoginLinkSearchObject searchObject = {0};

    //LOGIN2TODO
    devassertmsg(false, "Find Login Link For Container");

	PERFINFO_AUTO_START_FUNC();
	searchObject.searchType = containerType;
	searchObject.searchID = containerID;

	PERFINFO_AUTO_START("Walk Login Links", 1);
	for(i = 0; i < eaSize(&gLoginServerState.loginLinks); i++)
	{
		linkIterate2(gLoginServerState.loginLinks[i], FindLoginLinkForContainerCB, &searchObject);
	}
	PERFINFO_AUTO_STOP();

	if (searchObject.foundLink)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return searchObject.foundLink; 
	}

	PERFINFO_AUTO_START("Walk Queued Links", 1);
	for (i = 0; i < eaSize(&gLoginServerState.failedLogins); i++)
	{
		if (gLoginServerState.failedLogins[i]->clientType == containerType &&
			gLoginServerState.failedLogins[i]->clientID == containerID)
		{
			PERFINFO_AUTO_STOP();
			PERFINFO_AUTO_STOP_FUNC();
			return gLoginServerState.failedLogins[i];
		}
	}

	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP_FUNC();
	return NULL;
}

static int RunStatePerLink(NetLink *netLink, int index, Login2State *loginState, F32 *pElapsed)
{
	PERFINFO_AUTO_START_FUNC();
	giCurLinkCount++;
	ISM_Tick(LOGIN2_STATE_MACHINE, loginState, *pElapsed);
    aslLoginSendRefDictDataUpdates(loginState);

	resServerDestroySentUpdates(loginState->resourceCache);

	PERFINFO_AUTO_STOP_FUNC();
	return 1;
}

int aslLoginOncePerFrame(F32 fElapsed)
{
	int i;
	static bool bFirstTime = true;
	time_t now = time(NULL);
	U32 uiLoginQueueSize = 0;
	bool bCompactQueue = false, bCompactVIPQueue = false;

	PERFINFO_AUTO_START_FUNC();
	commMonitor(accountCommDefault());

	if (bFirstTime)
	{
		bFirstTime = false;

		RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), gServerLibState.containerID, "ready");

		RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(GLOBALTYPE_VIRTUALSHARD), false, parse_VirtualShard, false, false, NULL);
		resDictSetMaxUnreferencedResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_VIRTUALSHARD), RES_DICT_KEEP_ALL); // We need to keep them around even if not references
		objSubscribeToOnlineContainers(GLOBALTYPE_VIRTUALSHARD);

	}
	giCurLinkCount = 0;

    // Update cluster status if necessary.
    aslLogin2_UpdateClusterStatus();

	PERFINFO_AUTO_START("Walk Login Links", 1);
	for(i = 0; i < eaSize(&gLoginServerState.loginLinks); i++)
	{
		linkIterate2(gLoginServerState.loginLinks[i], RunStatePerLink, &fElapsed);
	}
	RefSystem_CopyQueuedToPrevious();
	PERFINFO_AUTO_STOP();

    aslLogin2_EntityFixupTick();
    aslLogin2_CharacterChoicesTick();
    aslLogin2_ValidateLoginTicketTick();

	accountDisplayNameTick();
	PERFINFO_AUTO_STOP_FUNC();

	return 1;
}


// LOGIN2TODO - update for login2
int aslCancelLoginProcess(GlobalType containerType, ContainerID containerID, char *message)
{
	LoginLink *foundLink;

    foundLink = aslFindLoginLinkForContainer(containerType, containerID);

	if (foundLink)
	{
		aslFailLogin(foundLink,message);
		return 1;
	}
	else
	{
		return 0;
	}
}

// LOGIN2TODO - update for login2
AUTO_COMMAND_REMOTE;
void AbortLoginByTypeAndID(GlobalType containerType, ContainerID containerID)
{
	aslCancelLoginProcess(containerType, containerID, "Login aborted by database");
}

// LOGIN2TODO - remove
void aslLoginDumpLinkState(LoginLink *pLink)
{
	static char *statusString;	

	if((isProductionMode() && !gbVerboseLinkUpdates) || gbNoVerboseUpdatesEver)
		return; // Turn this off in prod mode

	ISM_PutFullNextStateStackIntoEString(LOGIN_STATE_MACHINE, pLink, &statusString);	

	printf("Account '%s' (character %s) (locale %s) now in state %s\n", pLink->accountName, 
		pLink->characterName, locGetName(locGetIDByLanguage(pLink->clientLangID)), statusString);

	if(pLink->pAccountPermissions)
	{
		printf("\tPermissions:\n");
		FOR_EACH_IN_EARRAY(pLink->pAccountPermissions->ppList, AccountProxyKeyValueInfo, pInfo)
			printf("\t\t%s: %s\n", pInfo->pKey, pInfo->pValue);
		FOR_EACH_END
	}
}


static void AppServerLogout_CB(TransactionReturnVal *returnVal, void *userData)
{
	int loginCookie = (int)(intptr_t)userData;
	LoginLink *loginLink = aslFindLoginLinkForCookie(loginCookie);

	if (!loginLink || !loginLink->bFailedLogin)
	{
		log_printf(LOG_LOGIN, "Command %s returned for invalid login id %d", __FUNCTION__, loginCookie);
		return;	
	}
	
	switch(returnVal->eOutcome)
	{
	case TRANSACTION_OUTCOME_FAILURE:
		loginLinkLog(loginLink, "ReturnToObjectDB", "Failure, reason: %s", GetTransactionFailureString(returnVal));
		aslLoginBootPlayer(loginLink, loginLink->clientID, loginLink->characterName, ASLLOGINBOOT_NORMAL);
		free(loginLink);
		eaFindAndRemove(&gLoginServerState.failedLogins, loginLink);
		break;

	case TRANSACTION_OUTCOME_SUCCESS:
		loginLinkLog(loginLink, "ReturnToObjectDB", "Success");
		free(loginLink);
		eaFindAndRemove(&gLoginServerState.failedLogins, loginLink);
		break;			
	}
}

// LOGIN2TODO - set this up again for login2
void aslLoginServerStartLinkCorruption(TimedCallback *callback, F32 timeSinceLastCallback, NetLink *pLink)
{
	linkSetCorruptionFrequency(pLink, giClientLinkCorruptionFrequency);
}

// LOGIN2TODO - remove
void aslFailLogin(LoginLink *loginLink, const char *pErrorString)
{
	loginLink->bFailedLogin = true;

	if (!pErrorString)
	{
		pErrorString = "Unknown error. Possibly a message translation key was not found.";
	}
	else
	{
		pErrorString  = langTranslateMessageKeyDefault(loginLink->clientLangID, pErrorString, pErrorString);
	}

	if (g_isContinuousBuilder)
	{
		assertmsgf(0, "Login failed. Reason: %s\n", pErrorString);
	}

	// Make it so login failures are easier to understand in development
	if (isDevelopmentMode()) {
		Errorf("Login failed.  Reason: %s", pErrorString);
	}

	aslLoginLinkSafeDestroyValidator(loginLink);

	if (loginLink->netLink)
	{
		Packet *pak = pktCreate(loginLink->netLink, LOGINSERVER_TO_CLIENT_LOGIN_FAILED);

		assert(!loginLink->failedReason);
		
		loginLink->failedReason = strdup(pErrorString);
		
		pktSendString(pak, pErrorString);
		pktSend(&pak);

		loginLinkLog(loginLink, "LoginFailure", "%s", pErrorString);
		aslLoginDumpLinkState(loginLink);

		linkFlushAndClose(&loginLink->netLink, "Login Failed");
	}
	else
	{
		loginLinkLog(loginLink, "LoginFailure", "%s (NO LINK)", pErrorString);
	}
}

// LOGIN2TODO - remove
void aslNotifyLogin(LoginLink *loginLink, const char *pNotifyString, NotifyType eType, const char *pLogicalString)
{
	if (!pNotifyString)
		pNotifyString = "Unknown notification. Possibly a message translation key was not found.";

	if (g_isContinuousBuilder)
	{
		assertmsgf(0, "Login notify. Reason: %s\n", pNotifyString);
	}

	if (loginLink->netLink)
	{
		Packet *pak = pktCreate(loginLink->netLink, LOGINSERVER_TO_CLIENT_NOTIFYSEND);
		pktSendU32(pak, eType);
		pktSendString(pak, pNotifyString);
		pktSendString(pak, pLogicalString);
		pktSendString(pak, NULL);
		pktSend(&pak);

		loginLinkLog(loginLink, "LoginNotification", "%s", pNotifyString);
	}
	else
	{
		loginLinkLog(loginLink, "LoginNotification", "%s (NO LINK)", pNotifyString);
	}
}

void aslLogin2_Notify(Login2State *loginState, const char *pNotifyString, NotifyType eType, const char *pLogicalString)
{
    if (!pNotifyString)
        pNotifyString = "Unknown notification. Possibly a message translation key was not found.";

    if (g_isContinuousBuilder)
    {
        assertmsgf(0, "Login notify. Reason: %s\n", pNotifyString);
    }

    if (loginState->netLink)
    {
        Packet *pak = pktCreate(loginState->netLink, LOGINSERVER_TO_CLIENT_NOTIFYSEND);
        pktSendU32(pak, eType);
        pktSendString(pak, pNotifyString);
        pktSendString(pak, pLogicalString);
        pktSendString(pak, NULL);
        pktSend(&pak);

        aslLogin2_Log("LoginNotification", "%s", pNotifyString);
    }
    else
    {
        aslLogin2_Log("LoginNotification", "%s (NO LINK)", pNotifyString);
    }
}

void aslLoginSendRefDictDataUpdates(Login2State *loginState)
{
	PERFINFO_AUTO_START_FUNC();
	if ( resServerAreTherePendingUpdates(loginState->resourceCache) && loginState->netLink )
	{	
		Packet *pPak;
        pPak = pktCreate(loginState->netLink, LOGINSERVER_TO_CLIENT_SEND_REFDICT_DATA_UPDATES);
		resServerSendUpdatesToClient(pPak, loginState->resourceCache, NULL, loginState->clientLanguageID, true);
		pktSend(&pPak);
	}
	PERFINFO_AUTO_STOP_FUNC();
}

// Update the session-login-updated fields on the player (runs in parallel with any other changes)
enumTransactionOutcome SetPlayerLoginUpdateCB(ATR_ARGS, NOCONST(Entity) *newPlayer, NOCONST(Entity) *backupPlayer, GlobalType locationType, ContainerID locationID)
{
	Login2State *loginState = NULL;
	char *pKeyValue = NULL;
	S32 i;
	
	if ( !newPlayer || !newPlayer->pPlayer )
	{
		TRANSACTION_RETURN_LOG_FAILURE("Invalid Entity structure");
	}

    loginState = aslLogin2_GetActiveLoginStateByAccountID(newPlayer->pPlayer->accountID);

	if ( !loginState )
	{
		TRANSACTION_RETURN_LOG_FAILURE("Can't find Login State");
	}

	if ( ( loginState->selectedCharacterID == 0 ) && ( loginState->newCharacterEnt == NULL ) )
	{
		TRANSACTION_RETURN_LOG_FAILURE("No chosen character");
	}

	if ( newPlayer->pPlayer->xuid )
	{
		StructFreeStringSafe(&newPlayer->pPlayer->xuid); // update xuid
	}

	newPlayer->pPlayer->accountAccessLevel = loginState->clientAccessLevel; // update Account Access Level
	if ( newPlayer->pPlayer->accessLevel > loginState->clientAccessLevel )
    {
		newPlayer->pPlayer->accessLevel = loginState->clientAccessLevel;
    }

	// Clear the GM bit if it's been revoked i.e. access level < ACCESS_GM
	if( newPlayer->pPlayer->bIsGM && loginState->clientAccessLevel < ACCESS_GM )
	{
		newPlayer->pPlayer->bIsGM = false;
	}

    // NOTE - AL3 players can set the dev flag, so don't reset it for them.
	if( newPlayer->pPlayer->bIsDev && loginState->clientAccessLevel < 3 )
	{
		newPlayer->pPlayer->bIsDev = false;
	}

    if ( loginState->accountDisplayName )
    {
		strcpy(newPlayer->pPlayer->publicAccountName, loginState->accountDisplayName); //  update display name
    }
	if ( loginState->accountName )
    {
		strcpy(newPlayer->pPlayer->privateAccountName, loginState->accountName); //  update account name
    }
		
	// clear player wipe email flag, create list of characters
	eaiClear(&newPlayer->pPlayer->iCurrentCharacterIDs);
	for(i = 0; i < eaSize(&loginState->characterSelectionData->characterChoices->characterChoices); ++i)
	{
		eaiPush(&newPlayer->pPlayer->iCurrentCharacterIDs, loginState->characterSelectionData->characterChoices->characterChoices[i]->containerID);
	}

    // If we just created a character then add it to the list as well.
    if ( loginState->newCharacterID )
    {
        eaiPush(&newPlayer->pPlayer->iCurrentCharacterIDs, loginState->newCharacterID);
    }
	
	newPlayer->pPlayer->bWipeEmail = true;

	return TRANSACTION_OUTCOME_SUCCESS;
}

enumTransactionOutcome TransferToGameServerCB(ATR_ARGS, NOCONST(Entity) *newPlayer, NOCONST(Entity) *backupPlayer, GlobalType locationType, ContainerID locationID)
{
    Login2State *loginState;
    GameAccountData *gameAccountData;

    if ( locationType != GLOBALTYPE_GAMESERVER )
    {
        // this is only for login to mapservers
        return TRANSACTION_OUTCOME_SUCCESS;
    }

    if ( newPlayer->pPlayer == NULL )
    {
        TRANSACTION_RETURN_LOG_FAILURE("Corrupt entity missing pPlayer");
    }

    loginState = aslLogin2_GetActiveLoginStateByAccountID(newPlayer->pPlayer->accountID);

    if (!loginState)
    {
        TRANSACTION_RETURN_LOG_FAILURE("Can't find Login State");
    }

    if ( newPlayer->pSaved == NULL )
    {
        TRANSACTION_RETURN_LOG_FAILURE("Corrupt entity missing pSaved");
    }

    gameAccountData = GET_REF(loginState->hGameAccountData);
    if ( gameAccountData == NULL )
    {
        TRANSACTION_RETURN_LOG_FAILURE("Missing GameAccountData");
    }

    //always copy permissions into player, so they'll be around for gameserver-to-gameserver transfers
    if ( newPlayer->pSaved->pPermissionsOnMostRecentLogin )
    {
        eaClearStructNoConst(&newPlayer->pSaved->pPermissionsOnMostRecentLogin->ppList, parse_AccountProxyKeyValueInfoContainer);
    }

    if ( gameAccountData->eaAccountKeyValues )
    {
        int i;
        if ( newPlayer->pSaved->pPermissionsOnMostRecentLogin == NULL )
        {
            newPlayer->pSaved->pPermissionsOnMostRecentLogin = StructCreateNoConst(parse_AccountProxyKeyValueInfoListContainer);
        }
        for ( i = eaSize(&gameAccountData->eaAccountKeyValues) - 1; i >= 0; i-- )
        {
            NOCONST(AccountProxyKeyValueInfoContainer) *keyValueInfo;

            keyValueInfo = StructCloneNoConst(parse_AccountProxyKeyValueInfoContainer, CONTAINER_NOCONST(AccountProxyKeyValueInfoContainer, gameAccountData->eaAccountKeyValues[i]));
            eaPush(&newPlayer->pSaved->pPermissionsOnMostRecentLogin->ppList, keyValueInfo);
        }
    }

    return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_STARTUP(LoginSchemas) ASTRT_DEPS(AS_CharacterAttribs, AS_AttribSets, InventoryBags, ItemTags, Species, AS_ActivityLogEntryTypes);
void aslHandleSchemas(void)
{
	if (!isProductionMode())
	{	
		loadstart_printf("Writing out schema files... ");
		objExportNativeSchemas();
		loadend_printf("Done.");
	}

	objLoadAllGenericSchemas();

    objRegisterSpecialTransactionCallback(GLOBALTYPE_ENTITYPLAYER, TRANSACTION_MOVE_CONTAINER_TO, TransferToGameServerCB);
	objRegisterSpecialTransactionCallback(GLOBALTYPE_ENTITYPLAYER, TRANSACTION_RECEIVE_CONTAINER_FROM, SetPlayerLoginUpdateCB);
}

AUTO_STARTUP(LoginServer) ASTRT_DEPS(EntityCostumes, Items, Combat, LoginSchemas, RewardValTables, CritterFactions, NewCharacterValidation, AS_TextFilter, Species, UnlockedAllegianceFlags, UnlockedCreateFlags, PetStore_AppServer, AS_ControlSchemeRegions, RequiredPowersAtCreation, GamePermissions, Microtransactions, AS_GameProgression, GameAccountNumericPurchase, AS_LoginServerQueueLoad, GAMESPECIFIC);
void aslLoginServerStartup(void)
{
	worldLoadZoneMaps();
}

AUTO_STARTUP(AS_LoginServerQueueLoad) ASTRT_DEPS(QueueCategories, CharacterClasses);
void LoginServerQueueLoad(void)
{
	choice_Load();
	Queues_Load(NULL, Queues_ReloadQueues);	// Do not call the aslQueue version here. Bad container things will happen
	Queues_LoadConfig();
}

AUTO_STARTUP(GAMESPECIFIC);
void aslEmptyGamespecificStartup(void)
{
	//Empty method, without this projects that don't have a GAMESPECIFIC autostartup will assert due to an unrecognized task.
}

void aslLoginServerSendGlobalInfo(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	LoginServerGlobalInfo loginServerInfo = {0};
	time_t now = time(NULL);

	loginServerInfo.bUGCEnabled = gbUGCEnabled;
	loginServerInfo.iUGCVirtualShardID = GetUGCVirtualShardID();

	loginServerInfo.iNumLoggingIn = giCurLinkCount;

	loginServerInfo.iPort = gLoginServerListeningPort;

	RemoteCommand_HereIsLoginServerGlobalInfo(GLOBALTYPE_CONTROLLER, 0, objServerType(), objServerID(), &loginServerInfo);
}

static void requestTotalPlayersCB(TransactionReturnVal *returnStruct, void *userdata)
{
	U32 total;
	if(RemoteCommandCheck_dbGetActivePlayerCount(returnStruct, &total) == TRANSACTION_OUTCOME_SUCCESS)
		guTotalPlayers = total;
}

static void requestTotalPlayers(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	RemoteCommand_dbGetActivePlayerCount(objCreateManagedReturnVal(requestTotalPlayersCB, userData), GLOBALTYPE_OBJECTDB, 0); 
}

// LOGIN2TODO - handle map transfer timeouts
static int pruneLinksCB(NetLink *netLink, int index, LoginLink *loginLink, UserData userData)
{
	if(	ISM_IsStateActive(LOGIN_STATE_MACHINE, loginLink, LOGINSTATE_TRANSFERRING_CHARACTER) ||
		ISM_IsStateActive(LOGIN_STATE_MACHINE, loginLink, LOGINSTATE_SELECTING_MAP) ||
		ISM_IsStateActive(LOGIN_STATE_MACHINE, loginLink, LOGINSTATE_GETTING_ACCOUNT_PERMISSIONS))
	{
		U32 t = ISM_TimeEnteredCurrentState(LOGIN_STATE_MACHINE, loginLink, NULL);
		if(t)
		{
			t = timeSecondsSince2000() - t;
			if(t >= guMapTransferTimeout)
			{
				char *ipstr = makeIpStr(loginLink->ipRequest);
				log_printf(LOG_MOVEMENT, "Login link to %s%s timed out after %u seconds", ipstr, loginLink->bIsProxy ? "-proxied" : "", t);
				linkRemove_wReason(&netLink, "Max transfer timeout");
			}
		}
	}
	return 1;
}

static void pruneLinks(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	int i;
	PERFINFO_AUTO_START_FUNC();
	for(i = 0; i < eaSize(&gLoginServerState.loginLinks); i++)
	{
		linkIterate2(gLoginServerState.loginLinks[i], pruneLinksCB, userData);
	}
	PERFINFO_AUTO_STOP_FUNC();
}

#define LOAD_SCREENS_FILE_NAME "server/loadscreens.txt"

static void ShowLoadScreens(void)
{
	S32 i;
	for(i = 0; i < eaSize(&gLoadScreens.esLoadScreens); ++i)
	{
		if(gLoadScreens.esLoadScreens[i]->pMap && gLoadScreens.esLoadScreens[i]->pLoadScreen)
		{
			loadupdate_printf("Map: %s, LoadScreen: %s\n", gLoadScreens.esLoadScreens[i]->pMap, gLoadScreens.esLoadScreens[i]->pLoadScreen);
		}
	}
}

void aslLoadScreenReload(const char *relpath, int when)
{
	StructDeInit(parse_LoadScreenDynamic, &gLoadScreens);
	StructInit(parse_LoadScreenDynamic, &gLoadScreens);

	loadstart_printf("Reloading loadscreens...\n");
	ParserReadTextFile(relpath, parse_LoadScreenDynamic, &gLoadScreens, PARSER_OPTIONALFLAG);

	ShowLoadScreens();
	loadend_printf("done");
}


void aslLoadScreenInit(void)
{
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, LOAD_SCREENS_FILE_NAME, aslLoadScreenReload);

	// load in config file for login sever
	loadstart_printf("Loading loadscreens...\n");
	ParserReadTextFile(LOAD_SCREENS_FILE_NAME, parse_LoadScreenDynamic, &gLoadScreens, PARSER_OPTIONALFLAG);
	ShowLoadScreens();
	loadend_printf("done");

}

const LoadScreenDynamic *aslGetLoadScreenDynamic()
{
	return &gLoadScreens;
}

#define MAX_LISTEN_TRIES (MAX_EXTRA_LOGINSERVER_PORT - FIRST_EXTRA_LOGINSERVER_PORT + 2)

int aslLoginInit(void)
{
	NetListen * local_link = NULL, * public_link = NULL;
	int i;

	AutoStartup_SetTaskIsOn("LoginServer", 1);
	AutoStartup_RemoveAllDependenciesOn("WorldLib");
	
	loadstart_printf("Running Auto Startup...");
	DoAutoStartup();
	loadend_printf(" done.");

	resFinishLoading();

	assertmsg(GetAppGlobalType() == GLOBALTYPE_LOGINSERVER, "Login server type not set");

	loadstart_printf("Connecting LoginServer to TransactionServer (%s)... ", gServerLibState.transactionServerHost);

	// load in config file for login sever
	ParserReadTextFile("server/ProjectLoginServerConfig.txt", parse_ProjectLoginServerConfig, &gProjectLoginServerConfig, 0);

	aslLoadScreenInit();

	while (!InitObjectTransactionManager(
		GetAppGlobalType(),
		gServerLibState.containerID,
		gServerLibState.transactionServerHost,
		gServerLibState.transactionServerPort,
		gServerLibState.bUseMultiplexerForTransactions, NULL))
	{
		Sleep(1000);
	}
	if (!objLocalManager())
	{		
		loadend_printf("failed.");
		return 0;
	}

	for (i = 0; i < MAX_LISTEN_TRIES; i++)
	{
		gLoginServerListeningPort = i == 0 ? DEFAULT_LOGINSERVER_PORT : FIRST_EXTRA_LOGINSERVER_PORT + i - 1;

		printf("Going to try listening on port %d\n", gLoginServerListeningPort);

		commListenBoth(commDefault(), isDevelopmentMode()?LINKTYPE_TOUNTRUSTED_20MEG:LINKTYPE_TOUNTRUSTED_5MEG, 
			LINK_FORCE_FLUSH, 
			i == 0 ? DEFAULT_LOGINSERVER_PORT : FIRST_EXTRA_LOGINSERVER_PORT + i - 1, aslLogin2_HandleInput, aslLogin2_NewClientConnection,
			aslLogin2_ClientDisconnect, sizeof(Login2State), &local_link, &public_link);

		if (local_link || public_link)
		{
			printf("Succeeded!\n");
			break;
		}

		printf("Couldn't listen, no biggie, will try a different one\n");
	}

	if (!(local_link || public_link))
	{
		assertmsgf(0,"Error listening... tried all %d portsn", MAX_LISTEN_TRIES);
		return 0;
	}

	if(local_link)
		eaPush(&gLoginServerState.loginLinks, local_link);
	if(public_link)
		eaPush(&gLoginServerState.loginLinks, public_link);

    if (sbListenForGatewayProxies)
    {
		local_link = public_link = NULL;

		commListenBoth(commDefault(), isDevelopmentMode()?LINKTYPE_TOUNTRUSTED_20MEG:LINKTYPE_TOUNTRUSTED_5MEG, LINK_NO_COMPRESS|LINK_FORCE_FLUSH,
            DEFAULT_LOGINSERVER_GATEWAY_LOGIN_PORT, aslLogin2_HandleInput, aslLogin2_WebProxyConnection,
            aslLogin2_ClientDisconnect, sizeof(Login2State), &local_link, &public_link);

		// For the moment, we won't assert on not making these links. Multiple
		//   LoginServers can run on the same machine and only the first one
		//   will get the port. That's OK as long as one LS exists.
		// When the LS which got these ports goes down, a new one is auto-started
		//   started by the controller and it will get the ports. --poz
		// if (!(local_link || public_link))
        // {
        //     assertmsgf(0,"Error listening on port %d for Gateway Proxies!\n", DEFAULT_LOGINSERVER_GATEWAY_LOGIN_PORT);
        //     return 0;
        // }

        if(local_link)
            eaPush(&gLoginServerState.loginLinks, local_link);
        if(public_link)
            eaPush(&gLoginServerState.loginLinks, public_link);
    }

	loadend_printf("connected.");

	gAppServer->oncePerFrame = aslLoginOncePerFrame;
	aslProcessSpecialTokens(NULL, NULL, NULL);

	TimedCallback_Add(aslLoginServerSendGlobalInfo, NULL, 5.0f);
	TimedCallback_Add(requestTotalPlayers, NULL, 10.0f);

	if(isProductionMode() && !g_isContinuousBuilder)
		TimedCallback_Add(pruneLinks, NULL, 1.0f);
	if(isDevelopmentMode())
		TimedCallback_Add(resourcePeriodicUpdate, NULL, 5.0f);

	wlSetIsServer(true);
	wlInteractionSystemStartup();

	// UGC Resources -- needed because you can do UGCSeries editing on the LoginServer
	ugcResourceInfoPopulateDictionary();
	
	resServerIgnoreDictionaryRequests("AIAnimList");
	resServerIgnoreDictionaryRequests("AIConfig");
	resServerIgnoreDictionaryRequests("AIPowerConfigDef");
	resServerIgnoreDictionaryRequests("AICivilianMapDef");
	resServerIgnoreDictionaryRequests("AICombatRolesDef");
	resServerIgnoreDictionaryRequests("AIMastermindDef");
	resServerIgnoreDictionaryRequests("ChoiceTable");
	resServerIgnoreDictionaryRequests("ContactDef");
	resServerIgnoreDictionaryRequests("CritterDef");
	resServerIgnoreDictionaryRequests("FSM");
	resServerIgnoreDictionaryRequests("InteractionDef");
	resServerIgnoreDictionaryRequests("ItemGenData");
	resServerIgnoreDictionaryRequests("MissionDef");
	resServerIgnoreDictionaryRequests("MissionVarTable");
	resServerIgnoreDictionaryRequests("RewardTable");
    resServerIgnoreDictionaryRequests(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYPLAYER));
	resServerIgnoreDictionaryRequests(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYSAVEDPET));
	resServerIgnoreDictionaryRequests(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYPUPPET));
	resServerIgnoreDictionaryRequests(GlobalTypeToCopyDictionaryName(GLOBALTYPE_PLAYERDIARY));
	resServerIgnoreDictionaryRequests(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYSHAREDBANK));

	// WorldLib dictionaries which are defined but not loaded
	resServerIgnoreDictionaryRequests("AnimChart");
	resServerIgnoreDictionaryRequests("AnimGraph");
	resServerIgnoreDictionaryRequests("AnimTemplate");
	resServerIgnoreDictionaryRequests("BaseSkeleton");
	resServerIgnoreDictionaryRequests("DynAnimOverrideList");
	resServerIgnoreDictionaryRequests("DynBouncerGroup");
	resServerIgnoreDictionaryRequests("DynCostume");
	resServerIgnoreDictionaryRequests("DynMove");
	resServerIgnoreDictionaryRequests("MoveTransition");
	resServerIgnoreDictionaryRequests("DynRagdollData");
	resServerIgnoreDictionaryRequests("DynMovementSet");
	resServerIgnoreDictionaryRequests("DynClothCollision");
	resServerIgnoreDictionaryRequests("DynClothInfo");
	resServerIgnoreDictionaryRequests("DynParticle");
	resServerIgnoreDictionaryRequests("DynFxInfo");
	resServerIgnoreDictionaryRequests("IRQGroup");
	resServerIgnoreDictionaryRequests("LODTemplate");
	resServerIgnoreDictionaryRequests("ModelHeader");
	resServerIgnoreDictionaryRequests("ObjectLibrary");
	resServerIgnoreDictionaryRequests("SeqData");

    devassertmsg(( gConf.bUseSimplifiedMultiShardCharacterSlotRules == false ) || ( gConf.bVirtualShardsOnlyUseRestrictedCharacterSlots == true ),
        "gConf.bUseSimplifiedMultiShardCharacterSlotRules=true requires gConf.bVirtualShardsOnlyUseRestrictedCharacterSlots=true.");

	return 1;
}

typedef struct MessageHandlerStruct
{
	const char *title;
	const char *message;
	MessageStruct *pFmt;
} MessageHandlerStruct;

int aslLoginServerBroadcastMessageToLink(NetLink *link, S32 index, Login2State *loginState, MessageHandlerStruct *pStruct)
{
	if (pStruct->pFmt)
	{
		Packet *pak = pktCreate(link, LOGINSERVER_TO_CLIENT_NOTIFYSEND_STRUCT);
		pktSendU32(pak, kNotifyType_ServerBroadcast);
		pktSendStruct(pak, pStruct->pFmt, parse_MessageStruct);
		pktSend(&pak);
	}
	else
	{
		Packet *pak = pktCreate(link, LOGINSERVER_TO_CLIENT_NOTIFYSEND);
		pktSendU32(pak, kNotifyType_ServerBroadcast);
		pktSendString(pak, pStruct->message);
		pktSendString(pak, NULL);
		pktSendString(pak, NULL);
		pktSend(&pak);
	}
	return 1;
}

void aslLoginServerObjBroadcastMessage(GlobalType type, ContainerID id, const char *title, const char *string)
{
	if (type == objServerType())
	{
		int i;
		MessageHandlerStruct mStruct = {0};

		mStruct.title = title;
		mStruct.message = string;

		for(i = 0; i < eaSize(&gLoginServerState.loginLinks); i++)
			linkIterate2(gLoginServerState.loginLinks[i], aslLoginServerBroadcastMessageToLink, &mStruct);
	}
	else if (type)
	{
		RemoteCommand_RemoteObjBroadcastMessage(type, id, title, string);
	}
}

void aslLoginServerObjBroadcastMessageEx(GlobalType type, ContainerID id, const char *title, MessageStruct *pFmt)
{
	if (type == objServerType())
	{
		MessageHandlerStruct mStruct = {0};

		mStruct.title = title;
		mStruct.pFmt = pFmt;

		EARRAY_FOREACH_BEGIN(gLoginServerState.loginLinks, i);
			linkIterate2(gLoginServerState.loginLinks[i], aslLoginServerBroadcastMessageToLink, &mStruct);
		EARRAY_FOREACH_END;
	}
	else if (type)
	{
		RemoteCommand_RemoteObjBroadcastMessageEx(type, id, title, pFmt);
	}
}

AUTO_RUN_EARLY;
void setPrintCB(void)
{
	setObjBroadcastMessageCB(aslLoginServerObjBroadcastMessage);
	setObjBroadcastMessageExCB(aslLoginServerObjBroadcastMessageEx);
}

AUTO_RUN;
void LoginServerInitStuff(void)
{
	aslRegisterApp(GLOBALTYPE_LOGINSERVER, aslLoginInit, 0);

    aslLogin2_InitStateMachine();

    // LOGIN2TODO - remove
	sLoginLinksByID = stashTableCreateInt(64);
}

static int BootEveryoneFromLink(NetLink *netLink, int index, Login2State *loginState, char *pMessage)
{
    aslLogin2_FailLogin(loginState, pMessage);
	return 1;
}


//boot off everyone who is currently in the middle of logging in
AUTO_COMMAND_REMOTE;
void aslBootEveryone(char *pMessage)
{
	int i;

	for(i = 0; i < eaSize(&gLoginServerState.loginLinks); i++)
	{
		linkIterate2(gLoginServerState.loginLinks[i], BootEveryoneFromLink, pMessage);
	}
}

// LOGIN2TODO - update for login2
void OVERRIDE_LATELINK_SendDebugTransferMessageToClient(U32 iCookie, char *pMessage)
{
	LoginLink *pLink = aslFindLoginLinkForCookie(iCookie);

	if (pLink && pLink->netLink)
	{
		Packet *pPack = pktCreate(pLink->netLink, TO_CLIENT_DEBUG_MESSAGE);

		pktSendString(pPack, pMessage);
		pktSend(&pPack);
	}
}

// LOGIN2TODO - update for login2
static void LoginServerBooterLogout_CB(TransactionReturnVal *returnVal, void *userData)
{
	ContainerID iPlayerID = (ContainerID)((intptr_t)userData);

	switch(returnVal->eOutcome)
	{
	case TRANSACTION_OUTCOME_FAILURE:
		ErrorOrAlert("LOGINSERVER_BOOT_FAILURE", "The login server owns player container %u and tried to containermove it back to the object DB. This failed because: %s", 
			iPlayerID, GetTransactionFailureString(returnVal));
		break;

	case TRANSACTION_OUTCOME_SUCCESS:
		break;			
	}
}

void OVERRIDE_LATELINK_AttemptToBootPlayerWithBooter(ContainerID iPlayerToBootID, U32 iHandle, char *pReason)
{
    Container *con;
    Entity *playerEnt = NULL;
    Login2State *loginState = NULL;

    con = objGetContainer(GLOBALTYPE_ENTITYPLAYER, iPlayerToBootID);

    if ( con )
    {
        playerEnt = (Entity *)con->containerData;
    }

    if (!(con && con->meta.containerOwnerID == GetAppGlobalID() && con->meta.containerOwnerType == GetAppGlobalType() && playerEnt && playerEnt->pPlayer))
    {
        PlayerBooterAttemptReturn(iHandle, false, "Login server %u could not find a login link for player %u, and doesn't think it owns the container", GetAppGlobalID(), 
            iPlayerToBootID);
        return;
    }

    loginState = aslLogin2_GetActiveLoginStateByAccountID(playerEnt->pPlayer->accountID);

	if ( loginState == NULL || loginState->selectedCharacterID != iPlayerToBootID )
	{
		objRequestContainerMove(objCreateManagedReturnVal(LoginServerBooterLogout_CB, (void *)(intptr_t)iPlayerToBootID),
			GLOBALTYPE_ENTITYPLAYER, iPlayerToBootID, GetAppGlobalType(), GetAppGlobalID(), GLOBALTYPE_OBJECTDB, 0);

		PlayerBooterAttemptReturn(iHandle, false, "Login server %u could not find a login link for player %u, but did think it owned the container. Trying to container move.", GetAppGlobalID(), 
			iPlayerToBootID);

		return;
	}

    aslLogin2_FailLogin(loginState, "Login2_DuplicateLogin");

	PlayerBooterAttemptReturn(iHandle, false, "Login server %u has attempted to fail the login of %u, but that is not trustworthy, so we'll let the player booter cycle",
		GetAppGlobalID(), iPlayerToBootID);
}

AUTO_COMMAND_REMOTE;
void HereIsQueueID(U64 iCookie, U32 iQueueID, int iCurPositionInQueue, int iTotalInQueues)
{
    aslLogin2_HereIsQueueID(iCookie, iQueueID, iCurPositionInQueue, iTotalInQueues);
}

AUTO_COMMAND_REMOTE;
void PlayerIsThroughQueue(U64 iCookie)
{
    aslLogin2_PlayerIsThroughQueue(iCookie);
}


void ReportPlayerThroughQueueHasLeftCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	RemoteCommand_PlayerWhoWentThroughQueueHasLeftLoginServer(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalID());
}

//don't want to report this until enough time has passed that the player on the gameserver has hopefully been reported to the controller
//at least once (which happens every 5 seconds)
void ReportThatPlayerWhoWentThroughQueueHasLeft(void)
{
	TimedCallback_Run(ReportPlayerThroughQueueHasLeftCB, NULL, 7.5f);
}


AUTO_COMMAND_REMOTE;
void QueueIDsUpdate(U32 iMainQueueID, U32 iVIPQueueID, int iTotalInQueues)
{
    aslLogin2_QueueIDsUpdate(iMainQueueID, iVIPQueueID, iTotalInQueues);
}

AUTO_STRUCT;
typedef struct CharSlotAvailableCBData
{
    ContainerID accountID;              AST(KEY)
    ContainerID iVirtualShardID;
    STRING_POOLED allegianceName;       AST(POOL_STRING)
    REF_TO(GameAccountData) hGameAccountData;           AST(COPYDICT(GameAccountData))
    SlowRemoteCommandID iCmdID;
} CharSlotAvailableCBData;

static EARRAY_OF(CharSlotAvailableCBData) s_PendingSlotChecks = NULL;

// LOGIN2TODO - figure out what this should do for login2
AUTO_COMMAND_REMOTE_SLOW(bool);
void
aslLogin_IsCharacterSlotAvailableForTransfer(ContainerID accountID, U32 iVirtualShardID, const char *allegianceName, SlowRemoteCommandID iCmdID)
{
    SlowRemoteCommandReturn_aslLogin_IsCharacterSlotAvailableForTransfer(iCmdID, false);

    return;
}

#include "Autogen/aslloginserver_h_ast.c"
#include "AutoGen/aslLoginServer_c_ast.c"

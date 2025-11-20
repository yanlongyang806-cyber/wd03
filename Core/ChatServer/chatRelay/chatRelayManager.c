#include "chatRelayManager.h"

#include "Alerts.h"
#include "chatCommonStructs.h"
#include "chatdb.h"
#include "chatGlobal.h"
#include "ChatServer/chatBlacklist.h"
#include "ChatServer/chatShared.h"
#include "chatShardConfig.h"
#include "CrypticPorts.h"
#include "earray.h"
#include "GlobalComm.h"
#include "HashFunctions.h"
#include "logging.h"
#include "loggingEnums.h"
#include "mathutil.h"
#include "ResourceInfo.h"
#include "StashTable.h"
#include "users.h"

#include "ControllerPub.h"
#include "RemoteCommandGroup.h"

#include "AutoGen/chatCommonStructs_h_ast.h"
#include "AutoGen/chatRelayManager_h_ast.h"
#include "AutoGen/chatRelayManager_c_ast.h"

#include "AutoGen/ChatRelay_autogen_RemoteFuncs.h"
#include "AutoGen/Controller_autogen_RemoteFuncs.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"

#define CHATRELAY_HASH_DURATION (5 * SECONDS_PER_MINUTE) // TODO 5 minutes too long?

//#define CHATRELAY_MIN_RELAYS (2)
//#define CHATRELAY_MAX_RELAYS (10)
// Number of users each ChatRelay must have before it starts up a new one
// Currently used for starting alerts
#define CHATRELAY_USERSTRESS_ALERT_REPEAT_TIME (SECONDS_PER_HOUR)
#define CHATRELAY_ASSIGNMENT_RETRY (3)

extern bool gbChatVerbose;

bool gbDebugMode = false;
AUTO_CMD_INT(gbDebugMode, DebugMode);

AUTO_STRUCT;
typedef struct ChatRelayManagerUser
{
	U32 uAccountID; AST(KEY)
	U32 uRelayID;
	U32 uAccessLevel;
	bool bSocialRestricted;

	U32 uHashExpire;
	U32 uHashValue; // Hash of "[ID:HashExpire]"
	bool bAuthed;
} ChatRelayManagerUser;

static ChatRelayManagerUser **seaCRManagerUser= NULL;
static EARRAY_OF(ChatRelayServer) seaChatRelays = NULL;
static int siRelayCount = 0;
#define NUMBER_OF_CHATRELAYS (2)

static U32 suStartTime = 0;
#define CHATRELAY_STARTUP_TIME (60)

static U32 siNextChatRelayPort = STARTING_CHATRELAY_PORT;
//static U32 *seaiPortsInUse = NULL;

AUTO_RUN;
void initChatRelayTracking(void)
{
	eaCreate(&seaChatRelays);

	eaIndexedEnable(&seaChatRelays, parse_ChatRelayServer);
	eaIndexedEnable(&seaCRManagerUser, parse_ChatRelayManagerUser);

	resRegisterDictionaryForEArray("ChatRelay Auths", RESCATEGORY_OTHER, 0, &seaCRManagerUser, parse_ChatRelayManagerUser);
	resRegisterDictionaryForEArray("Chat Relays", RESCATEGORY_OTHER, 0, &seaChatRelays, parse_ChatRelayServer);
}

static ChatRelayManagerUser *crManager_GetRelayUser(U32 uAccountID)
{
	return eaIndexedGetUsingInt(&seaCRManagerUser, uAccountID);
}
static ChatRelayServer *crManager_GetRelayServer(U32 uRelayID)
{
	return eaIndexedGetUsingInt(&seaChatRelays, uRelayID);
}
static int crManager_GetTotalUserCount(void)
{
	int iCount = 0;
	EARRAY_FOREACH_BEGIN(seaChatRelays, i);
	{
		iCount += eaiSize(&seaChatRelays[i]->eaiUsers);
	}
	EARRAY_FOREACH_END;
	return iCount;
}

static void crManager_GenerateHash(SA_PARAM_NN_VALID ChatRelayManagerUser *user)
{
	char userStr[64] = "";
	PERFINFO_AUTO_START_FUNC();
	user->uHashExpire = timeSecondsSince2000() + CHATRELAY_HASH_DURATION;
	sprintf(userStr, "[%d:%d]", user->uAccountID, user->uHashExpire);
	user->uHashValue = hashString(userStr, true);
	PERFINFO_AUTO_STOP_FUNC();
}

static ChatRelayManagerUser *crManager_AssignUser(U32 uAccountID, U32 uAccessLevel, bool bSocialRestricted, ChatRelayServer *server)
{
	ChatRelayManagerUser *relayUser;

	if (!devassert(uAccountID && server->uID))
		return NULL;
	if (!seaCRManagerUser)
		eaIndexedEnable(&seaCRManagerUser, parse_ChatRelayManagerUser);
	relayUser = crManager_GetRelayUser(uAccountID);
	if (!relayUser)
	{
		relayUser = StructCreate(parse_ChatRelayManagerUser);
		relayUser->uAccountID = uAccountID;
		eaIndexedAdd(&seaCRManagerUser, relayUser);
	}
	else if (relayUser->uRelayID && relayUser->uRelayID != server->uID)
	{
		ChatRelayServer *pPreviousServer = crManager_GetRelayServer(relayUser->uRelayID);
		if (pPreviousServer)
			eaiFindAndRemove(&pPreviousServer->eaiUsers, relayUser->uAccountID);
	}
	// Update these values
	relayUser->uRelayID = server->uID;
	relayUser->uAccessLevel = uAccessLevel;
	relayUser->bSocialRestricted = bSocialRestricted;
	eaiPushUnique(&server->eaiUsers, uAccountID);
	crManager_GenerateHash(relayUser);
	return relayUser;
}

static void crManager_CleanupUser(ChatRelayManagerUser *relayUser)
{
	eaFindAndRemove(&seaCRManagerUser, relayUser);
	StructDestroy(parse_ChatRelayManagerUser, relayUser);
}

static __forceinline void crManager_SetUserRelayData(SA_PARAM_NN_VALID ChatUser *user, SA_PARAM_NN_VALID ChatRelayManagerUser *relayUser)
{
	user->uChatRelayID = relayUser->uRelayID;
	crManager_CleanupUser(relayUser);
}

void crManager_ReceiveChatUser (ChatUser *user)
{
	ChatRelayManagerUser *relayUser = crManager_GetRelayUser(user->id);
	if (relayUser && relayUser->bAuthed)
	{
		ANALYSIS_ASSUME(relayUser);
		crManager_SetUserRelayData(user, relayUser);
	}
}

static void startChatRelayComplete(TransactionReturnVal *returnVal, void *userData)
{
	enumTransactionOutcome eOutcome;
	Controller_SingleServerInfo *pServerInfo;

	eOutcome = RemoteCommandCheck_StartServer(returnVal, &pServerInfo);
	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		printf("Relay %d started.\n", pServerInfo->iGlobalID);
		StructDestroy(parse_Controller_SingleServerInfo, pServerInfo);
	}
	else
	{
		printf("A Chat Relay failed to start.\n");
	}
}

AUTO_COMMAND ACMD_CATEGORY(ChatRelay_Debug);
void startChatRelay(void)
{
	char *pExtraCommandLine = NULL;
	estrStackCreate(&pExtraCommandLine);
	estrPrintf(&pExtraCommandLine, "-Port %d", siNextChatRelayPort++);
	if (gbDebugMode)
		estrAppend2(&pExtraCommandLine, " -DebugMode");
	if (gbChatVerbose)
		estrAppend2(&pExtraCommandLine, " -ChatVerbose");

	RemoteCommand_StartServer(objCreateManagedReturnVal(startChatRelayComplete, NULL),
		GLOBALTYPE_CONTROLLER, 0, GLOBALTYPE_CHATRELAY, 0,
		NULL, pExtraCommandLine, 
		"Starting new Chat Relay", NULL, NULL, NULL, NULL);
	estrDestroy(&pExtraCommandLine);
}

// TODO
/*static bool crManager_ShouldStartNewRelay(void)
{

	// Ideally it should never be more than one less than the min
	// If it does run into that case, then more relays can be started manually
	if (eaSize(&seaChatRelays) < ChatConfig_GetNumberOfChatRelays())
		return true;
	return false;

	//EARRAY_FOREACH_BEGIN(seaChatRelays, i);
	//{
	//	if (eaiSize(&seaChatRelays[i]->eaiUsers) < CHATRELAY_USERCOUNT_START_CRITERIA)
	//		return false;
	//}
	//EARRAY_FOREACH_END;
	//return true;
}*/

static void crManager_RemoveRelay(U32 uRelayID)
{
	ChatRelayServer *chatRelay = crManager_GetRelayServer(uRelayID);
	if (chatRelay)
	{
		if (eaFindAndRemove(&seaChatRelays, chatRelay) != -1)
			siRelayCount--;
		StructDestroy(parse_ChatRelayServer, chatRelay);
		//if (crManager_ShouldStartNewRelay())
		//	startChatRelay();
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CONTROLLER);
void crManager_RelayCrashed(U32 uServerID)
{
	Alertf("CHAT_RELAY_FAILURE", "Relay %d crashed.", uServerID);
	crManager_RemoveRelay(uServerID);
	// Only restart a new one on crashes, not closes
	startChatRelay();
}
AUTO_COMMAND_REMOTE ACMD_IFDEF(CONTROLLER);
void crManager_RelayClosed(U32 uServerID)
{
	Alertf("CHAT_RELAY_FAILURE", "Relay %d closed.", uServerID);
	crManager_RemoveRelay(uServerID);
}
// Registers ChatRelay with manager, and sets up callbacks for when relay crashes/closes
// Also adds any users the ChatRelay has already authenticated against a previous ChatServer
AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATRELAY);
int crManager_RegisterRelay(U32 uRelayID, const char *ipStr, U32 uPort, PlayerInfoList *pAccounts)
{
	RemoteCommandGroup *pServerCrashGroup = CreateEmptyRemoteCommandGroup();
	RemoteCommandGroup *pServerCloseGroup = CreateEmptyRemoteCommandGroup();
	ChatRelayServer *chatRelay = StructCreate(parse_ChatRelayServer);
	estrCopy2(&chatRelay->ipStr, ipStr);
	chatRelay->iPort = uPort;
	chatRelay->uID = uRelayID;
	eaIndexedAdd(&seaChatRelays, chatRelay);
	siRelayCount++;
	
	//eaiPushUnique(&seaiPortsInUse, uPort);
	//crManager_SetNextUnusedPort();

	RemoteCommand_blacklist_RelayUpdate(GLOBALTYPE_CHATRELAY, uRelayID, blacklist_GetList(), CHATBLACKLIST_REPLACE);

	{
		U32 *eaiAccountsToRetrieve = NULL;
		eaiCopy(&chatRelay->eaiUsers, &pAccounts->piAccountIDs);

		EARRAY_INT_CONST_FOREACH_BEGIN(pAccounts->piAccountIDs, i, n);
		{
			U32 uAccountID = pAccounts->piAccountIDs[i];
			ChatUser *user = userFindByContainerId(uAccountID);
			if (user)
			{
				devassert(user->uChatRelayID == 0);
				user->uChatRelayID = uRelayID;
			}
			else
			{
				// Create ChatRelayManagerUser for when account is retrieved from GCS
				ChatRelayManagerUser *relayUser = crManager_GetRelayUser(uAccountID);
				if (devassertmsgf(!relayUser, "User Account ID %d logged on from multiple relays.", relayUser->uAccountID))
				{
					relayUser = StructCreate(parse_ChatRelayManagerUser);
					relayUser->uAccountID = uAccountID;
					relayUser->uRelayID = uRelayID;
					relayUser->bAuthed = true;
					eaIndexedAdd(&seaCRManagerUser, relayUser);
					eaiPush(&eaiAccountsToRetrieve, uAccountID);
				}
			}
		}
		EARRAY_FOREACH_END;
		if (eaiSize(&eaiAccountsToRetrieve))
		{
			getAccountListFromGlobalChatServer(eaiAccountsToRetrieve);
			eaiDestroy(&eaiAccountsToRetrieve);
		}
	}

	AddCommandToRemoteCommandGroup(pServerCrashGroup, GLOBALTYPE_CHATSERVER, 0, false, "crManager_RelayCrashed %d", uRelayID);
	AddCommandToRemoteCommandGroup(pServerCloseGroup, GLOBALTYPE_CHATSERVER, 0, false, "crManager_RelayClosed %d", uRelayID);

	RemoteCommand_AddThingToDoOnServerCrash(GLOBALTYPE_CONTROLLER, 0, GLOBALTYPE_CHATRELAY, uRelayID, pServerCrashGroup);
	RemoteCommand_AddThingToDoOnServerClose(GLOBALTYPE_CONTROLLER, 0, GLOBALTYPE_CHATRELAY, uRelayID, pServerCloseGroup);

	DestroyRemoteCommandGroup(pServerCrashGroup);
	DestroyRemoteCommandGroup(pServerCloseGroup);
	printf("Chat Relay Registered: %d @ %s:%d\n", chatRelay->uID, chatRelay->ipStr, chatRelay->iPort);
	return 1;
}

// Used for GCS disconnect-reconnects
void crManager_InformRelayGCSReconnect(void)
{
	EARRAY_FOREACH_BEGIN(seaChatRelays, i);
	{
		RemoteCommand_chatRelay_GlobalChatReconnect(GLOBALTYPE_CHATRELAY, seaChatRelays[i]->uID);
	}
	EARRAY_FOREACH_END;
}

static ChatAuthRequestData *crManager_UserAuthenticateComplete(U32 uRelayID, ChatRelayManagerUser *relayUser)
{
	ChatUser *user;
	ChatAuthRequestData *response = NULL;
	char accountIDString[11];
	char accessLevelString[3];
	
	response = StructCreate(parse_ChatAuthRequestData);
	response->uAccountID = relayUser->uAccountID;
	response->uAccountAccessLevel = relayUser->uAccessLevel;
	response->bSocialRestricted = relayUser->bSocialRestricted;

	sprintf(accountIDString, "%d", response->uAccountID);
	sprintf(accessLevelString, "%d", response->uAccountAccessLevel);
	servLogWithPairs(LOG_CHATPERMISSIONS, "ChatRelayAuthSuccess", 
		"accountID", accountIDString, 
		"accessLevel", accessLevelString, 
		"socialRestricted", response->bSocialRestricted ? "yes" : "no", 
		NULL);

	if (user = userFindByContainerId(relayUser->uAccountID))
	{
		crManager_SetUserRelayData(user, relayUser);
	}
	else // Haven't gotten the relay user yet - delay this
	{
		U32 *eaiAccountList = NULL;
		eaiPush(&eaiAccountList, relayUser->uAccountID);
		getAccountListFromGlobalChatServer(eaiAccountList);
		eaiDestroy(&eaiAccountList);
		relayUser->bAuthed = true;
	}
	return response;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATRELAY);
ChatAuthRequestData *crManager_AuthenticateUser(U32 uRelayID, U32 uAccountID, U32 uSecret)
{
	ChatRelayManagerUser *relayUser = crManager_GetRelayUser(uAccountID);
	if (gbChatVerbose)
		printf("Relay %d, User %d, Secret %d\n", uRelayID, uAccountID, uSecret);
	
	if (relayUser)
	{
		if (relayUser->uRelayID != uRelayID)
			return NULL;
		if (relayUser->uHashValue != uSecret)
			return NULL;
		if (relayUser->uHashExpire < timeSecondsSince2000())
			return NULL;
		return crManager_UserAuthenticateComplete(uRelayID, relayUser);
	}
	return NULL;
}

void ChatServerAddOrUpdateUser(ChatLoginData *pLoginData);
AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATRELAY);
ChatAuthRequestData *crManager_FakeAuthenticateUser(U32 uRelayID, U32 uAccountID, U32 uSecret)
{
	ChatRelayManagerUser *relayUser = crManager_GetRelayUser(uAccountID);
	ChatRelayServer *relayServer = crManager_GetRelayServer(uRelayID);
	ChatLoginData loginData = {0};

	if (!relayServer || !gbDebugMode)
		return NULL;
	if (!relayUser)
	{
		relayUser = StructCreate(parse_ChatRelayManagerUser);
		relayUser->uAccountID = uAccountID;
		relayUser->uAccessLevel = ACCESS_DEBUG;
		eaIndexedAdd(&seaCRManagerUser, relayUser);
		relayUser->uRelayID = uRelayID;
	}

	loginData.uAccountID = uAccountID;
	loginData.uAccessLevel = ACCESS_DEBUG;
	estrPrintf(&loginData.pAccountName, "TestUser_%d", uAccountID);
	estrPrintf(&loginData.pDisplayName, "TestUser_%d", uAccountID);
	ChatServerAddOrUpdateUser(&loginData);
	estrDestroy(&loginData.pAccountName);
	estrDestroy(&loginData.pDisplayName);
	return crManager_UserAuthenticateComplete(uRelayID, relayUser);
}

extern void ChatServerLogoutEx(ContainerID accountID, ContainerID characterID);
AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATRELAY);
void crManager_DropUser(U32 uRelayID, U32 uAccountID)
{
	ChatRelayManagerUser *relayUser = crManager_GetRelayUser(uAccountID);
	ChatUser *user = userFindByContainerId(uAccountID);
	ChatRelayServer *relayServer = crManager_GetRelayServer(uRelayID);

	if (gbChatVerbose)
		printf("crManager_DropUser (%d)\n", uAccountID);

	if (relayUser && relayUser->uRelayID == uRelayID)
	{
		crManager_CleanupUser(relayUser);
	}
	if (user && (user->uChatRelayID == uRelayID || user->uChatRelayID == 0))
	{
		user->uChatRelayID = 0;
		ChatServerLogoutEx(user->id, user->pPlayerInfo ? user->pPlayerInfo->onlineCharacterID : 0);
	}
	if (relayServer)
		eaiFindAndRemove(&relayServer->eaiUsers, uAccountID);
}

static ChatRelayServer *crManager_GetRelayToAssignUser(void)
{
	static U32 suLastAlert = 0;
	int iRandStartIdx, idx;
	
	if (siRelayCount < 1)
		return NULL;
	idx = iRandStartIdx = randInt(siRelayCount);
	// Try to find a non-full ChatRelay to assign the user to
	do
	{
		U32 iUserCount;
		iUserCount = eaiSize(&seaChatRelays[idx]->eaiUsers);
		if (iUserCount < ChatConfig_GetMaxUsersPerRelay())
		{
			iUserCount++; // Include this user
			// TODO(Theo) determine actual criteria for this stress alert/warning
			if (iUserCount >= ChatConfig_GetUsersPerRelayWarningLevel())
			{
				// Alert once per hour if a assigned chat relay is nearing user cap
				U32 uTime = timeSecondsSince2000();
				if (suLastAlert + CHATRELAY_USERSTRESS_ALERT_REPEAT_TIME < uTime)
				{
					ErrorOrAlert("CHAT_RELAY_WARNING", "A Chat Relay is nearing their user cap - currently at %d/%d - total of %d users online.",
						ChatConfig_GetUsersPerRelayWarningLevel(), ChatConfig_GetMaxUsersPerRelay(), crManager_GetTotalUserCount()+1);
					suLastAlert = uTime;
				}
			}
			return seaChatRelays[idx];
		}
		idx = (idx+1) % siRelayCount;
	} while (idx != iRandStartIdx);
	return NULL;
}

static void chatRelayServerList_CB(TransactionReturnVal *returnVal, void *userData)
{
	Controller_ServerList *pChatRelayList;
	enumTransactionOutcome eOutcome;

	eOutcome = RemoteCommandCheck_GetServerList(returnVal, &pChatRelayList);

	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		U32 iCount = 0;
		EARRAY_FOREACH_BEGIN(pChatRelayList->ppServers, i);
		{
			Controller_SingleServerInfo *serverInfo = pChatRelayList->ppServers[i];
			if (strstri(serverInfo->stateString, "ready"))
			{
				iCount++;
				// Inform ChatRelay that manager is online
				printf("Found Chat Relay %d.\n", serverInfo->iGlobalID);
				RemoteCommand_chatRelay_RegisterRelayWithChatServer(GLOBALTYPE_CHATRELAY, serverInfo->iGlobalID);
			}
		}
		EARRAY_FOREACH_END;

		// If less than the minimum number of relays were found, start more until the min
		for (; iCount<ChatConfig_GetNumberOfChatRelays(); iCount++)
		{
			startChatRelay();
		}
		StructDestroy(parse_Controller_ServerList, pChatRelayList);
	}
	else if (eOutcome == TRANSACTION_OUTCOME_FAILURE)
	{
		AssertOrAlert("CHATRELAYMANAGER_STARTUP", "Failure getting list of Chat Relays from Controller.");
	}
}

void initChatRelayManager(void)
{
	printf("Initializing Chat Relay...");
	suStartTime = timeSecondsSince2000();
	RemoteCommand_GetServerList(objCreateManagedReturnVal(chatRelayServerList_CB, NULL), GLOBALTYPE_CONTROLLER, 0, 
		GLOBALTYPE_CHATRELAY);
	printf("Done.\n");
}

// Character ID is optional
AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER) ACMD_IFDEF(GAMESERVER);
void crManager_GetUserSecretHash(GlobalType eRequestServerType, U32 uRequestServerID, ChatAuthRequestData *pRequestData)
{
	static U32 suLastAlert = 0;
	ChatUser *user = userFindByContainerId(pRequestData->uAccountID);
	ChatRelayManagerUser *relayUser = NULL;
	ChatRelayServer *relayServer = crManager_GetRelayToAssignUser();
	ChatAuthData data = {0};
	char accountIDString[11];
	char charIDString[11];
	char accessLevelString[3];

	sprintf(accountIDString, "%d", pRequestData->uAccountID);
	sprintf(charIDString, "%d", pRequestData->uCharacterID);
	sprintf(accessLevelString, "%d", pRequestData->uAccountAccessLevel);

	servLogWithPairs(LOG_CHATPERMISSIONS, "ChatRelayAuthStart", 
		"serverType", StaticDefineIntRevLookup(GlobalTypeEnum, eRequestServerType), 
		"accountID", accountIDString, 
		"charID", charIDString, 
		"accessLevel", accessLevelString, 
		"socialRestricted", pRequestData->bSocialRestricted ? "yes" : "no", 
		NULL);

	if (relayServer)
	{
		relayUser = crManager_AssignUser(pRequestData->uAccountID, pRequestData->uAccountAccessLevel, pRequestData->bSocialRestricted, relayServer);
		if (relayUser)
		{
			data.uAccountID = data.userLoginData.uAccountID = relayUser->uAccountID;
			data.uCharacterID = pRequestData->uCharacterID;
			data.userLoginData.uAccessLevel = pRequestData->uAccountAccessLevel;
			data.userLoginData.pAccountName = StructAllocString(pRequestData->pAccountName);
			data.userLoginData.pDisplayName = StructAllocString(pRequestData->pDisplayName);
			data.uRelayPort = relayServer->iPort;
			data.pRelayIPString = StructAllocString(relayServer->ipStr);
			data.uSecretValue = relayUser->uHashValue;
		}
	}
	else // No ChatRelays are online or non-full
	{
		// devasserts once per hour if user could not be assigned to a Chat Relay (for production mode)
		U32 uTime = timeSecondsSince2000();
		// Don't alert until at least 1 minute after startup to give ChatRelays time to start
		if (uTime > suStartTime + CHATRELAY_STARTUP_TIME)
		{
			if (suLastAlert + CHATRELAY_USERSTRESS_ALERT_REPEAT_TIME < uTime)
			{
				TriggerAlertf("CHATRELAYS_INSUFFICIENT", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, 
					GetAppGlobalType(), GetAppGlobalID(), GetAppGlobalType(), GetAppGlobalID(), getHostName(), 0, 
					"No useable chat relays were found to assign users to out of [%d] total Chat Relays.",
					eaSize(&seaChatRelays));
				suLastAlert = uTime;
			}
		}
	}

	// Always sends down response
    devassertmsg(eRequestServerType != GLOBALTYPE_LOGINSERVER, "Got chat relay request from login server");
    if (eRequestServerType == GLOBALTYPE_GAMESERVER || eRequestServerType == GLOBALTYPE_ENTITYPLAYER)
	{
		RemoteCommand_gslChat_ReceiveChatRelayData(eRequestServerType, uRequestServerID, &data);
	}
	StructDeInit(parse_ChatAuthData, &data);
}

void ChatServerAddToRelayUserList(SA_PARAM_NN_VALID EARRAY_OF(ChatRelayUserList) *eaRelays, SA_PARAM_NN_VALID ChatUser *user)
{
	ChatRelayUserList *list = eaIndexedGetUsingInt(eaRelays, user->uChatRelayID);
	if (!list)
	{
		list = StructCreate(parse_ChatRelayUserList);
		list->uChatRelayID = user->uChatRelayID;
		eaIndexedAdd(eaRelays, list);
	}
	eaiPush(&list->eaiUsers, user->id);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void blacklist_ShardUpdate(const ChatBlacklist *blacklist, ChatBlacklistUpdate eUpdateType)
{
	blacklist_HandleUpdate(blacklist, eUpdateType);
	EARRAY_FOREACH_BEGIN(seaChatRelays, i);
	{
		RemoteCommand_blacklist_RelayUpdate(GLOBALTYPE_CHATRELAY, seaChatRelays[i]->uID, blacklist, eUpdateType);
	}
	EARRAY_FOREACH_END;
}

#include "AutoGen/chatRelayManager_h_ast.c"
#include "AutoGen/chatRelayManager_c_ast.c"
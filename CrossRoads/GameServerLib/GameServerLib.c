/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GameServerLib.h"
#include "crypt.h"
#include "sysutil.h"
#include "logging.h"
#include "FolderCache.h"
#include "BitStream.h"
#include "net/netpacket.h"
#include <conio.h>
#include "WorldLib.h"
#include "MemoryMonitor.h"
#include "AppRegCache.h"
#include "EntityIterator.h"
#include "Team.h"
#include "Guild.h"
#include "ServerLib.h"
#include "EntityGrid.h"
#include "EntityLib.h"
#include "EntitySavedData.h"
#include "utilitiesLib.h"
#include "EntityMovementManager.h"
#include "entCritter.h"
#include "Autogen/GameServerLib_h_ast.h"
#include "WorldGrid.h"
#include "wlVolumes.h"
#include "gslInteractionManager.h"
#include "gslItemAssignments.h"
#include "InteractionManager_common.h"
#include "sock.h"
#include "StringCache.h"
#include "StringFormat.h"
#include "gslActivity.h"
#include "gslBulletins.h"
#include "gslCommandParse.h"
#include "gslInteractable.h"
#include "gslInteraction.h"
#include "gslPartition.h"
#include "gslSpawnPoint.h"
#include "gslVolume.h"
#include "character_target.h"
#include "gslTransactions.h"
#include "globalstatemachine.h"
#include "gslBaseStates.h"
#include "gslMapTransfer.h"
#include "wlBeacon.h"
#include "gslBeaconInterface.h"
#include "gslMission.h"
#include "gslOldEncounter.h"
#include "gslEntity.h"
#include "gslEncounter.h"
#include "gslUserExperience.h"
#include "inventoryCommon.h"
#include "inventoryTransactions.h"
#include "rewardCommon.h"
#include "reward.h"
#include "svrGlobalInfo.h"
#include "Character.h"
#include "PowerActivation.h"
#include "ScratchStack.h"
#include "gslSendToClient.h"
#include "aiLib.h"
#include "aiMovement.h"
#include "aiStruct.h"
#include "aiTeam.h"
#include "gslChat.h"
#include "timing_profiler_interface.h"
#include "gslAccountProxy.h"
#include "partition_enums.h"
#include "Player.h"
#include "gslGameAccountData.h"
#include "character_combat.h"
#include "gslQueue.h"
#include "ChoiceTable_common.h"
#include "gslPlayerDifficulty.h"
#include "gslContact.h"
#include "ExpressionFunc.h"
#include "UGCProjectCommon.h"
#include "alerts.h"
#include "wlBeacon.h"
#include "gslUGC.h"
#include "gslQueue.h"
#include "progression_common.h"
#include "gslGuild.h"
#include "gslSharedBank.h"
#include "UGCProjectCommon.h"
#include "WorldBounds.h"
#include "GslUtils.h"
#include "GameStringFormat.h"
#include "Login2Common.h"

#include "AutoGen/EntityInteraction_h_ast.h"
#include "AutoGen/AppServerLib_autogen_remotefuncs.h"
#include "AutoGen/ObjectDB_autogen_remotefuncs.h"
#include "AutoGen/ChatServer_autogen_remotefuncs.h"
#include "AutoGen/GameServerLib_autogen_remotefuncs.h"
#include "AutoGen/Controller_autogen_remotefuncs.h"
#include "AutoGen/UtilitiesLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/SoundLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/WorldLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/Entity_h_ast.h"
#include "Player_h_ast.h"

#define DEFAULT_ENT_SEND_INTERVAL (0.1f)

void UpdateBogDownTest(void);


AUTO_RUN_ANON(memBudgetSetConfigFile("editor/BudgetsGameServer.txt"););

GameServerLibState gGSLState = {0};
ProjectGameServerConfig gProjectGameServerConfig = {0};

static U32 s_uiEnableOpenInstancingDuration = 120;
AUTO_CMD_INT(s_uiEnableOpenInstancingDuration,EnableOpenInstancingDuration) ACMD_CATEGORY(debug);
static U32 s_uiEnableOpenInstancingTeamSize = 3;
AUTO_CMD_INT(s_uiEnableOpenInstancingTeamSize,EnableOpenInstancingTeamSize) ACMD_CATEGORY(debug);
static GameServerGlobalInfo gslGlobalInfo = {0};

AUTO_CMD_INT(gGSLState.clients.linkCorruptionFrequency, ClientLinkCorruption) ACMD_CATEGORY(Debug);

bool sbForceTimeoutNow = false;
AUTO_CMD_INT(sbForceTimeoutNow, ForceTimeoutNow) ACMD_CATEGORY(debug);

int OVERRIDE_LATELINK_comm_commandqueue_size(void)
{
	return (1<<15);
}

static int sPartitionInactivityTimeoutMinutes = 5;
AUTO_CMD_INT(sPartitionInactivityTimeoutMinutes, InactivityTimeoutMinutes) ACMD_CMDLINE;

static int sServerInactivityTimeoutMinutes = 1;
AUTO_CMD_INT(sServerInactivityTimeoutMinutes, ServerInactivityTimeoutMinutes) ACMD_CMDLINE;

static bool s_bSimulateProductionMapLifecycle = false;
AUTO_CMD_INT(s_bSimulateProductionMapLifecycle, SimulateProductMapLifecycle) ACMD_CMDLINE;


AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
void SetServerInactivityTimeoutMinutes(int iTimeoutMinutes)
{
	sServerInactivityTimeoutMinutes = iTimeoutMinutes;
}

//dummy... the actual parsing happens in GameServerMain.c because it has to be super-first
AUTO_COMMAND;
void WebRequestServer(int iDummy)
{
	//dummy... the actual parsing happens in GameServerMain.c because it has to be super-first
}
AUTO_COMMAND;
void GatewayServer(int iDummy)
{
	//dummy... the actual parsing happens in GameServerMain.c because it has to be super-first
}
AUTO_COMMAND;
void ServerBinner(int iDummy)
{
	//dummy... the actual parsing happens in GameServerMain.c because it has to be super-first
}

// Dummy command, is handled below
AUTO_COMMAND ACMD_HIDE;
void SafeLoad(int not_used)
{
}

#define STRING_CACHE_TOTAL_SIZE			(1024*1024*225)
#define STRING_CACHE_NUM_STRINGS		((2048+256)*1024)	
#define STRING_CACHE_NUM_STRINGS_PRODUCTION		(STRING_CACHE_NUM_STRINGS*70/100) // 70% of development size

AUTO_RUN_SECOND;
int InitializeStringCache(void)
{
	bool bProductionMode = isProductionMode() || strstri(GetCommandLine(), "productionmode");
	// This is called BEFORE productionmode is parsed
		
	autoPrintExecutableVersion(); // Want this printed before other debugging messages below, normally in AUTO_RUN_EARLY

	PreloadSharedAddress(1);

	if (strstri(GetCommandLine(), "beaconclient"))
	{
		stringCacheInitializeShared("SharedStringCacheBeaconClient", STRING_CACHE_TOTAL_SIZE, STRING_CACHE_NUM_STRINGS, true);
	}
	else if(strstri(GetCommandLine(), "beaconautoserver") || strstri(GetCommandLine(), "beaconserver"))
	{
		stringCacheInitializeShared("SharedStringCacheBeaconServer", STRING_CACHE_TOTAL_SIZE, STRING_CACHE_NUM_STRINGS, true);
	}
	else if(strstri(GetCommandLine(), "beaconrequestserver"))
	{
		stringCacheInitializeShared("SharedStringCacheBeaconRequestServer", STRING_CACHE_TOTAL_SIZE, STRING_CACHE_NUM_STRINGS, true);
	}
	else if(strstri(GetCommandLine(), "beaconmasterserver"))
	{
		stringCacheInitializeShared("SharedStringCacheBeaconMasterServer", STRING_CACHE_TOTAL_SIZE, STRING_CACHE_NUM_STRINGS, true);
	}
	else
	{
		bool bFastLoad = bProductionMode;
		if (strstri(GetCommandLine(), "SafeLoad"))
			bFastLoad = false;
		printf("Acquiring shared string cache...");
		stringCacheInitializeShared("SharedStringCache", STRING_CACHE_TOTAL_SIZE, bProductionMode?STRING_CACHE_NUM_STRINGS_PRODUCTION:STRING_CACHE_NUM_STRINGS, bFastLoad);
		printf(" Done\n");
	}
	ScratchStackSetThreadSize(1024*1024*2);
	g_ccase_string_cache = true;
	return 1;
}



typedef struct LinkSearchStruct
{
	U32 userID;
	ClientLink *linkOut;
} LinkSearchStruct;

static int gslCheckLinkID(NetLink* link, S32 index, ClientLink *clientLink, LinkSearchStruct *func_data)
{
	if (linkID(link) == func_data->userID)
	{
		func_data->linkOut = clientLink;
	}
	return 1;
}


ClientLink *gslGetClientLinkForLinkID(U32 linkID)
{
	LinkSearchStruct searchStruct = {0};
	int i;
	searchStruct.userID = linkID;
	for(i = 0; i < eaSize(&gGSLState.clients.netListens); i++)
	{
		linkIterate2(gGSLState.clients.netListens[i], gslCheckLinkID, &searchStruct);
	}
	return searchStruct.linkOut;
}

void gslHandleBadResourcePacket(U32 userID, const char *message)
{
	ClientLink *link = gslGetClientLinkForLinkID(userID);
	Entity *pEnt = gslPrimaryEntity(link);

	PERFINFO_AUTO_START_FUNC();

	if (pEnt)
	{
		entLog(LOG_BADCLIENT, pEnt, "BadResourcePacket", "%s", message);
		if (isProductionMode())
		{
			gslSendForceLogout(link, message);
			gslLogOutEntity(pEnt, 0, 0);
		}
	}
	else
	{
		log_printf(LOG_BADCLIENT, "Invalid Resource Packet on orphaned link: %s", message);
	}

	PERFINFO_AUTO_STOP();
}

void gslHandleClientPacketCorruption(ClientLink *link, const char *message)
{
	Entity *pEnt = gslPrimaryEntity(link);

	PERFINFO_AUTO_START_FUNC();
	
	if (pEnt)
	{
		entLog(LOG_BADCLIENT, pEnt, "Packet Corruption", "%s", message);

		if (link->netLink)
		{
			if (linkIsNotTrustworthy(link->netLink))
			{
				gslSendForceLogout(link, message);
				gslLogOutEntity(pEnt, 0, 0);
			}
			else
			{
				assertmsgf(0, "Packet corruption on trustworthy link: %s", message);
			}
		}
	}
	else
	{
		log_printf(LOG_BADCLIENT, "Invalid Resource Packet on orphaned link: %s", message);
	}
	
	PERFINFO_AUTO_STOP();
}


AUTO_RUN;
int InitializeDefaultGSLState(void)
{
	gGSLState.gameServerDescription.baseMapDescription.eMapType = ZMTYPE_UNSPECIFIED;
	gGSLState.gameServerDescription.baseMapDescription.mapDescription = allocAddString("maps/system/empty.zone");
	gGSLState.gameServerDescription.baseMapDescription.ownerID = 0;
	gGSLState.gameServerDescription.baseMapDescription.ownerType = GLOBALTYPE_NONE;

	gGSLState.gameServerDescription.baseMapDescription.mapInstanceIndex = 1;

	gGSLState.gameServerDescription.iMapPort = STARTING_GAMESERVER_PORT;
	
	gGSLState.entSend.seconds.interval = DEFAULT_ENT_SEND_INTERVAL;

	// Don't set the start time here, because this is before the server is really started and ready

	resServerSetBadPacketCallback(gslHandleBadResourcePacket);

	return 1;
}

static void GSLLinkInfoFromID(U32 linkID, NetLink** out_ppLink, ResourceCache** out_ppCache)
{
	ClientLink* cLink = gslGetClientLinkForLinkID(linkID);

	if (!cLink)
	{
		*out_ppLink = NULL;
		*out_ppCache = NULL;
	}
	else
	{
		*out_ppLink = cLink->netLink;
		*out_ppCache = cLink->pResourceCache;
	} 
}

AUTO_RUN;
void InitCallbacks(void)
{
	worldLibSetLinkInfoFromIDFunc(GSLLinkInfoFromID);
}

// Make bins and exit
AUTO_COMMAND ACMD_CMDLINE;
void makeBinsAndExit(bool bSet)
{
	if (!WorldGrid_DoingMultiplexedMakeBinsAsSlave())
	{
		ParserForceBinCreation(bSet);
	}
	gbMakeBinsAndExit = bSet;
	wlSetDeleteHoggs(bSet);
	stringCacheDisableWarnings();
}

// Make bins and exit for a single namespace
AUTO_COMMAND ACMD_CMDLINE;
void makeBinsAndExitForNamespace(char *ns)
{
	makeBinsAndExit(true);
	gpcMakeBinsAndExitNamespace = strdup(ns);
}


// Calculate dependencies and exit
AUTO_COMMAND  ACMD_CMDLINE;
void calculateDependenciesAndExit(char *filename)
{
	gpcCalcDepsAndExit = strdup(filename);
}

S64 gGSLTotalTicks;

//////////////////////////////////////////////////////////////////////////

// sets zonemap to load
AUTO_COMMAND ACMD_NAME(mapname);
void CommandMapName(char *publicName)
{
	strcpy(gGSLState.cmdLineMapPublicName,publicName);
}

// Memory Monitor
AUTO_COMMAND ACMD_NAME(smmds, smmonitor);
void CommandMemoryMonitor(void)
{
	memMonitorDisplayStats();
}

AUTO_COMMAND;
void serverTimerRecordStart(const char *filename)
{
	timerRecordStart(filename);
}

AUTO_COMMAND;
void serverTimerRecordEnd(void)
{
	timerRecordEnd();
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void InitMap(void)
{
	if (isProductionMode() && !isProductionEditMode())
		return;
	worldReloadMap();
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void InitMapUnsafe(void)
{
	worldReloadMap();
}

void gslSetConsoleTitle(void)
{
	char title[256];
	const char *s;
	sprintf(title,
			"%d: Server ID %d",
			_getpid(),
			gServerLibState.containerID);

	if (gGSLState.server_fps > 0.0)
	{
		strcatf(title, ", %.2f fps", gGSLState.server_fps);
	}

	strcatf(title,
			", %d client%s",
			gGSLState.clients.loggedInCount,
			gGSLState.clients.loggedInCount == 1 ? "" : "s");

	if(gGSLState.clients.notLoggedInCount)
	{
		strcatf(title, ", %d logging in", gGSLState.clients.notLoggedInCount);
	}

	if (s = zmapGetName(NULL))
	{
		char temp[CRYPTIC_MAX_PATH];
		getFileNameNoExtNoDirs(temp, s);
		strcatf(title, ", %s", temp);
	}
	
	setConsoleTitle(title);
}



// Search for (FIND_HOGGS_TO_IGNORE) to find related code
// Most hoggs
const char* serverHoggIgnores[] = {
	"client.hogg",
	"ns.hogg"
};

void gslPreMain(const char* pcAppName)
{
	loadstart_report_unaccounted(true);
	memCheckInit();
	preloadDLLs(0);
	regSetAppName(pcAppName);
	ServerLibPatch();

	if (isDevelopmentMode()) {
		// Search for (FIND_HOGGS_TO_IGNORE) to find related code
		FolderCacheAddIgnores(serverHoggIgnores, ARRAY_SIZE(serverHoggIgnores));
	}

	if ( !gServerLibState.writeSchemasAndExit) {
		FolderCacheChooseMode();
	} else {
		FolderCacheChooseModeNoPigsInDevelopment();
	}
	//FolderCacheEnableCallbacks(0);
	FolderCacheSetManualCallbackMode(1);
	FolderCacheStartMonitorThread();
	FolderCacheDoNotWarnOnOverruns(true);

	loadstart_printf("FileSystem startup...");
	fileLoadGameDataDirAndPiggs();
	loadend_printf(" done.");

	// !!!: Disabled pending further investigation of performance issues with large numbers of namespaces in StarTrek <NPK 2010-06-02>
	//if (isDevelopmentMode())
	//{
	//	loadstart_printf("Loading user namespaces...");
	//	fileLoadAllUserNamespaces(0);
	//	loadend_printf(" done.");
	//}

	bsAssertOnErrors(true);
	setDefaultAssertMode();

	logSetDir(GlobalTypeToName(GetAppGlobalType()));
	errorLogStart();
	cryptAdler32Init();
}

//////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////

void gslStartLinkCorruption(TimedCallback *callback, F32 timeSinceLastCallback, NetLink *pLink)
{
	linkSetCorruptionFrequency(pLink, gGSLState.clients.linkCorruptionFrequency);
}

int maxSendPacket;

void gslClientGetEntityString(	ClientLink* clientLink,
								char* bufferOut,
								S32 bufferOutLen)
{
	bufferOut[0] = 0;
	
	EARRAY_INT_CONST_FOREACH_BEGIN(clientLink->localEntities, i, isize);
		Entity* e = entFromEntityRefAnyPartition(clientLink->localEntities[i]);
		
		strcatf_s(	bufferOut,
					bufferOutLen,
					"%sc%d:i%d:%s:0x%8.8p",
					i ? ", " : "",
					SAFE_MEMBER(e, myContainerID),
					INDEX_FROM_REFERENCE(SAFE_MEMBER(e, myRef)),
					e ? e->debugName : "(nonexistent)",
					e);
	EARRAY_FOREACH_END;
}

void gslClientPrintfColor(	ClientLink* clientLink,
							U32 color,
							const char* format,
							...)
{
	U32		frameCount;
	char	buffer[1000];
	
	frameLockedTimerGetTotalFrames(gGSLState.flt, &frameCount);
	
	VA_START(va, format);
	vsprintf(buffer, format, va);
	VA_END();
	
	printfColor(color,
				"%6.6d, %s: Client 0x%8.8p:%d %s",
				frameCount % 1000000,
				timeGetLocalTimeString(),
				clientLink,
				clientLink->connectInstance,
				buffer);
	
	gslClientGetEntityString(clientLink, SAFESTR(buffer));
	
	printfColor(color,
				" ("
				"%s"	// IP
				"%s"	// ", ents: \""
				"%s"	// buffer
				"%s"	// "\""
				"%s"	// ", "
				"%s"	// clientInfoString
				").\n",
				makeIpStr(linkGetIp(clientLink->netLink)),
				buffer[0] ? ", ents: \"" : "",
				buffer,
				buffer[0] ? "\"" : "",
				clientLink->clientInfoString ? ", " : "",
				NULL_TO_EMPTY(clientLink->clientInfoString));
}

static void gslMovementClientMsgHandler(const MovementClientMsg* msg){
	ClientLink* clientLink = msg->userPointer;
	Entity*		e = gslPrimaryEntity(clientLink);

	switch(msg->msgType){
		xcase MC_MSG_CREATE_PACKET_TO_CLIENT:{
			NetLink* link = clientLink->netLink;
			
			if(!link){
				break;
			}
			
			*msg->createPacketToClient.pakOut = pktCreate(link, TOCLIENT_MOVEMENT_CLIENT);
		}
		
		xcase MC_MSG_SEND_PACKET_TO_CLIENT:{
			Packet* pak = msg->sendPacketToClient.pak;

			pktSend(&pak);
		}
		
		xcase MC_MSG_LOG_STRING:{
			if(e){
				const char* error = msg->logString.error;

				entLog(	LOG_MOVEMENT,
						e,
						"MovementClient Log",
						"%s",
						msg->logString.text);
				
				if(error){
					ErrorDetailsf(	"User[%s/%s/%s[0x%x]), Container[%d]",
									SAFE_MEMBER(e->pPlayer, privateAccountName),
									SAFE_MEMBER(e->pPlayer, publicAccountName),
									e->debugName,
									entGetRef(e),
									entGetContainerID(e));

					ErrorGroupedByFilef("MovementClient: %s", error);
				}
			}
		}
	}
}

int gslConnectCallback(NetLink* link,ClientLink *clientLink)
{
	PERFINFO_AUTO_START_FUNC();
	
	if (sbForceTimeoutNow)
	{
		linkRemove(&link);
		PERFINFO_AUTO_STOP();
		return 1;
	}

	
	clientLink->netLink = link;
	clientLink->connectInstance = ++gGSLState.clients.lastClientConnectInstance;
	clientLink->clientLoggedIn = false;
	clientLink->readyForGeneralUpdates = false;
	clientLink->pResourceCache = resServerCreateResourceCache(linkID(link));
	clientLink->pPendingTestClientCommandsPacket = NULL;
	
	if(isProductionMode())
		linkSetTimeout(link,CLIENT_LINK_TIMEOUT);
	else
		linkSetTimeout(link,CLIENT_LINK_DEV_TIMEOUT);
	
	mmClientCreate(	&clientLink->movementClient,
					gslMovementClientMsgHandler,
					clientLink);

	gGSLState.clients.notLoggedInCount++;

	//gslClientPrintfColor(clientLink, COLOR_GREEN, "connected");

	//when a client logs in, if we are testing link corruption, then start corruption 10 seconds in the future
	if (gGSLState.clients.linkCorruptionFrequency)
	{
		clientLink->corruptionCallback = TimedCallback_Run(gslStartLinkCorruption, link, 10.0f);
	}
	if (maxSendPacket)
		linkSetMaxAllowedPacket(link,maxSendPacket);

	if (gslClientsAreUntrustworthy())
	{
		linkSetIsNotTrustworthy(link, true);
	}

	PERFINFO_AUTO_STOP();

	return 1;
}
AUTO_CMD_INT(maxSendPacket, maxSendPacket);

int gslDisconnectCallback(NetLink *link, ClientLink *clientLink)
{
	bool bWasTransferring;
	char *pDisconnectReason = NULL;
	Entity *pEnt;

	PERFINFO_AUTO_START_FUNC();
	
	// Log disconnect information.
	estrStackCreate(&pDisconnectReason);
	linkGetDisconnectReason(link, &pDisconnectReason);
	pEnt = gslPrimaryEntity(clientLink);
	if (pEnt)
		entLog(LOG_CLIENTSERVERCOMM, pEnt, "gslDisconnect", "link 0x%p error %u reason \"%s\" ip %s", link, linkGetDisconnectErrorCode(link),
			pDisconnectReason, makeIpStr(linkGetIp(link)));
	else
		servLog(LOG_CLIENTSERVERCOMM, "gslDisconnectUnknown", "link 0x%p error %u reason \"%s\" ip %s", link, linkGetDisconnectErrorCode(link),
			pDisconnectReason, makeIpStr(linkGetIp(link)));
	estrDestroy(&pDisconnectReason);

	clientLink->disconnected = 1;

	TimedCallback_Remove(clientLink->corruptionCallback);

	//gslClientPrintfColor(clientLink, COLOR_RED, "disconnected");

	worldClearWorldLocks(linkID(link));

	gslUGC_HandleUserDisconnect();

	bWasTransferring = gslMapTransferHandleDisconnect(clientLink);
	if (clientLink->clientLoggedIn && !bWasTransferring)
	{	
		// If we're logged in, and weren't in the middle of a transfer, forcibly log out the chars		
		EARRAY_INT_CONST_FOREACH_BEGIN(clientLink->localEntities, i, isize);
		{
			Entity *e = entFromEntityRefAnyPartition(clientLink->localEntities[i]);

			if(SAFE_MEMBER(e, pPlayer))
			{
				gslLogOutEntityDisconnect(e);
			}
		}
		EARRAY_FOREACH_END;
	}

	// Clear ClientLink pointers.
	
	EARRAY_INT_CONST_FOREACH_BEGIN(clientLink->localEntities, i, isize);
	{
		Entity *e = entFromEntityRefAnyPartition(clientLink->localEntities[i]);

		if(SAFE_MEMBER(e, pPlayer))
		{
			e->pPlayer->clientLink = NULL;
			pktFree(&e->pPlayer->msgPak);
		}
	}
	EARRAY_FOREACH_END;

	pktFree(&clientLink->pPendingTestClientCommandsPacket);

	resServerDestroyResourceCache(clientLink->pResourceCache);
	
	eaiDestroy(&clientLink->sentEntIndices);
	eaiDestroy(&clientLink->localEntitiesMutable);
	
	mmClientDestroy(&clientLink->movementClient);
	
	if(clientLink->clientWasLoggedIn)
	{
		assert(gGSLState.clients.loggedInCount);
		gGSLState.clients.loggedInCount--;
	}
	else
	{
		assert(gGSLState.clients.notLoggedInCount);
		gGSLState.clients.notLoggedInCount--;
	}

	StructDestroySafe(parse_UGCSearchResult, &clientLink->ugcSearchResult);
	
	//in UGC edit mode, we never want gameservers running once the person editing is gone, so immediately do
	//graceful shutdown
	if (isProductionEditMode() && !ugc_DevMode)
	{
		sbForceTimeoutNow = true;
	}

	PERFINFO_AUTO_STOP();
	
	return 1;
}

void gslPlayerLoggedIn(ClientLink* clientLink, Entity* e, S32 noTimeout, int locale)
{
	PERFINFO_AUTO_START_FUNC();

	if (noTimeout && (e->pPlayer->accessLevel >= ACCESS_DEBUG || areEditorsAllowed()))
	{
		clientLink->noTimeOut = true;
		linkSetTimeout(clientLink->netLink,0.0);
	}
	gslAddEntityToLink(clientLink, e, true);
	
	resCacheSetDebugName(clientLink->pResourceCache, e->debugName);

	ClientCmd_thisIsYourIPOnTheGameServer(	e,
											linkGetIp(clientLink->netLink),
											linkGetPort(clientLink->netLink));

	// Set the non-persisted locale for the entity during login
	entSetLanguage(e, clientLink->clientLangID);

	ASSERT_FALSE_AND_SET(clientLink->clientLoggedIn);
	ASSERT_FALSE_AND_SET(clientLink->clientWasLoggedIn);
	
	assert(gGSLState.clients.notLoggedInCount);
	gGSLState.clients.notLoggedInCount--;
	gGSLState.clients.loggedInCount++;

	//gslClientPrintfColor(clientLink, COLOR_GREEN, "logged in");

	e->pPlayer->pUI->uiLastClientPoke = timeSecondsSince2000();
	clientLink->accessLevel = e->pPlayer->accessLevel;

	gslQueue_ent_CheckQueueState(e);

	progression_PlayerLoggedIn(e, e->pPlayer->bIsFirstSessionLogin);

	// Make sure it has an account ID before requesting key values.
	// This is to fix a problem with -autologin.
	if (e->pPlayer->bIsFirstSessionLogin)
	{
		PERFINFO_AUTO_START("FirstSessionLogin", 1);

		e->pPlayer->bIsFirstSessionLogin = 0;

		inv_ent_FixupBoundItems(e);

		if (&e->pInventoryV2)
			eaClearStruct(&e->pInventoryV2->eaiNewItemIDs, parse_NewItemID);

		// If Bulletins are enabled, send fresh bulletins to the client
		if (gConf.bEnableBulletins)
		{
			gslBulletins_Update(e, false);
		}

		// Make sure it has an account ID before requesting key values.
		// This is to fix a problem with -autologin.
		if (e->pPlayer->accountID)
		{
			gslGAD_FixupGameAccount(e, true);
			gslAPRequestAllKeyValues(e);

			PERFINFO_AUTO_START("RequestRecruitInfo", 1);
			//Request the recruit information
			RemoteCommand_aslAPCmdRequestRecruitInfo(GLOBALTYPE_ACCOUNTPROXYSERVER, 0, e->pPlayer->accountID);
			PERFINFO_AUTO_STOP();
		}

		// join a guild if new character and pass critieria
		gslGuild_AutoJoinGuild(e);

		UserExp_LogLogin(e); // Do this last so as much as possible is done
		ClientCmd_gclSendSpecsToServer(e); // Cause client to send and log system specs

		PERFINFO_AUTO_STOP();
	}
	else
	{
		// Make sure it has an account ID before requesting key values.
		// This is to fix a problem with -autologin.
		if (e->pPlayer->accountID)
		{
			gslGAD_FixupGameAccount(e, false);
		}
	}


	if (e->pTeam && e->pTeam->iNearbyTeamSize) {
		encounter_SetNearbyTeamsize(e);
	}
	gslpd_SetDifficultyIfOwner(e);

	PERFINFO_AUTO_STOP();
}



//////////////////////////////////////////////////////////////////////////


void gslSleepForRestOfFrame(FrameLockedTimer* flt, F32 frameMinSeconds)
{
	while(1){
		F32 frameSecondsSoFar;
		S32 msToSleep;

		frameLockedTimerGetCurSeconds(flt, &frameSecondsSoFar);

		if(frameSecondsSoFar >= frameMinSeconds){
			break;
		}
		
		msToSleep = ceil((frameMinSeconds - frameSecondsSoFar) * 1000);
		assert(msToSleep >= 0 && msToSleep < 10000); // check for ridiculous values
		Sleep(msToSleep);
	}
}

void gslFileChangedCallback(FolderCache* fc, FolderNode* node, int virtual_location, const char *relpath, int when, void *userData)
{
	Entity* currEnt;
	EntityIterator* iter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
	while ((currEnt = EntityIteratorGetNext(iter)))
	{
		if (entGetPlayer(currEnt)->needsFileUpdates)
		{
			switch(when) {
				xcase FOLDER_CACHE_CALLBACK_UPDATE:
					assert(node);
					ClientCmd_FolderCacheHandleRemoteUpdate(currEnt, FolderCache_GetFromVirtualLocation(folder_cache, virtual_location), relpath, (U32)node->size, node->timestamp, 
						(node->writeable?0:_A_RDONLY) |
						(node->hidden?_A_HIDDEN:0) |
						(node->system?_A_SYSTEM:0));
				xcase FOLDER_CACHE_CALLBACK_DELETE:
					ClientCmd_FolderCacheHandleRemoteDelete(currEnt, FolderCache_GetFromVirtualLocation(folder_cache, virtual_location), relpath);
			}
		}
	}
	EntityIteratorRelease(iter);
}

void gslMain(int in_argc, char **in_argv)
{
	int i;
	int argc;
	char *argv[1000];

	// process the commandline stuff into the argv/argc structure
	{
		char buf[1000]={0};

		loadCmdline("./cmdline.txt",buf,sizeof(buf));
		argv[0] = in_argv[0];
		argc = 1 + tokenize_line_quoted_safe(buf,&argv[1],ARRAY_SIZE(argv)-1,0);
		for (i=1; i<in_argc && argc<ARRAY_SIZE(argv); i++) {
			argv[argc++] = in_argv[i];
		}
	}

	cmdSetGlobalCmdParseFunc(GameServerDefaultParse);

	loadstart_printf("UtilitiesLib startup...");
	utilitiesLibStartup();
	loadend_printf(" done.");

	filePrintDataDirs();

	if (giMakeOneLocaleOPFilesAndExit)
	{
		msgLoadAllMessages();
		assert(0); //should never hit this, should already have exit()ed
	}

	serverLibStartup(argc, argv);

	// Don't do FRAMEPERF logging for Game Servers by default, due to the volume of logs this creates on large shards.
	if (GetAppGlobalType() == GLOBALTYPE_GAMESERVER)
		utilitiesLibEnableFramePerfLogging(false);

	if(runBeaconApp())
	{
		gslBeaconServerRun();
		return;
	}

	if (isDevelopmentMode())
		FolderCacheSetCallbackEx(FOLDER_CACHE_CALLBACK_ALL, "*", gslFileChangedCallback, NULL);

	GSM_Execute(GSL_INIT);

	errorShutdown();
}

AUTO_CMD_FLOAT(gConf.rollover_distance, RolloverDistance) ACMD_CATEGORY(debug) ACMD_ACCESSLEVEL(9);
AUTO_CMD_FLOAT(gConf.rollover_display_time, RolloverDisplayTime) ACMD_CATEGORY(debug) ACMD_ACCESSLEVEL(9);
AUTO_CMD_FLOAT(gConf.rollover_pickup_time, RolloverPickupTime) ACMD_CATEGORY(debug) ACMD_ACCESSLEVEL(9);

//update any "rolled over" ents for this player
__forceinline static void gslUpdate_rollovers(Entity* pEnt)
{
	static Entity** s_proxEnts = NULL;
	int ii;
	F32 fMinDist;
	Vec3 vSourcePos;
	InventoryBag *pLootBag = NULL;
	Entity *pentClosest = NULL;
	bool bRollover = false;

	// If this isn't a rollover loot entity, return.
	if(!inv_HasLoot(pEnt)){
		return;
	}

	FOR_EACH_IN_EARRAY(pEnt->pCritter->eaLootBags, InventoryBag, pBag)
	{
		if (pBag->pRewardBagInfo->PickupType == kRewardPickupType_Rollover)
		{
			bRollover = true;
			break;
		}
	}
	FOR_EACH_END

	if(!bRollover){
		return;
	}

	// Only pick up rollovers after the rollover_time.
	if(pEnt->pCritter->bDoNotAutoSetLootCostume)
	{
		if ( (pEnt->pCritter->StartingTimeToLinger - pEnt->pCritter->timeToLinger) < gConf.rollover_display_time )
		{
			return;
		}

		// Alert the client that this rollover is active
		pEnt->pCritter->bDoNotAutoSetLootCostume = false;
		entity_SetDirtyBit(pEnt, parse_Critter, pEnt->pCritter, false);
		entSetActive(pEnt); // The loot ent might have gone to sleep. Wake it up.
	}

	if ( (pEnt->pCritter->StartingTimeToLinger - pEnt->pCritter->timeToLinger) < gConf.rollover_pickup_time )
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	entGetPos(pEnt,vSourcePos);
	 
	// Get a list of all live ents potentially within range of the rollover ent
	eaSetSize(&s_proxEnts, 0);

	entGridProximityLookupExEArray(entGetPartitionIdx(pEnt), vSourcePos, &s_proxEnts, gConf.rollover_distance, gConf.bRolloverLootIsForPlayersOnly ? ENTITYFLAG_IS_PLAYER : 0, ENTITYFLAG_IGNORE|ENTITYFLAG_DEAD, pEnt);

	// Iterate through the ent list and find the closest ent within the rollover distance
	fMinDist = gConf.rollover_distance;
	for(ii=eaSize(&s_proxEnts)-1; ii>=0; ii--)
	{
		Entity* currEnt = s_proxEnts[ii];

		if(currEnt != pEnt && reward_MyDrop(currEnt, pEnt))
		{
			F32 f = entGetDistance(pEnt, NULL, currEnt, NULL, NULL);
			if(f <= fMinDist)
			{
				fMinDist = f;
				pentClosest = currEnt;
			}
		}
	}

	if(pentClosest)
	{

		FOR_EACH_IN_EARRAY(pEnt->pCritter->eaLootBags, InventoryBag, pBag)
		{
			reward_SendLootFXMessageToBagOwners(pentClosest, pEnt, pBag);

			//!!!!  this should have some type of transaction confirmation to ensure that loot was actually given
			if (reward_BagIsMyDrop(pentClosest, pBag))
			{
				ItemChangeReason reason = {0};
				inv_FillItemChangeReason(&reason, pEnt, "Loot:Rollover", NULL);
				reason.bFromRollover = true;
				reward_EntLoot(pentClosest, pBag, &reason );
				eaRemove(&pEnt->pCritter->eaLootBags, FOR_EACH_IDX(pEnt->pCritter->eaLootBags, pBag));
				entity_SetDirtyBit(pEnt, parse_Critter, pEnt->pCritter, 0);
			}
		}
		FOR_EACH_END

		if (eaSize(&pEnt->pCritter->eaLootBags) <= 0)
		{
			entDie(pEnt, gConf.rollover_postpickup_linger_time, 0, 0, NULL);
			// Force do not draw bit for faster removal of visible drop
			entSetCodeFlagBits(pEnt, ENTITYFLAG_DONOTDRAW); // calls entSetActive()
		}

	}

	PERFINFO_AUTO_STOP();
}


void gslEntityFrameUpdate(Entity* pEnt)
{
	S64 iAICombatTeamID = 0;
	Mat4 ent_mat;

	PERFINFO_AUTO_START("CheckFallThroughWorld", 1);

	// Moved from MovementManager, feel free to find a better place
	entGetPos(pEnt, ent_mat[3]);
	if(ent_mat[3][1] <= 50.f - MAX_PLAYABLE_COORDINATE){
		if (pEnt && pEnt->pPlayer) {
			spawnpoint_MovePlayerToStartSpawn(pEnt, false);
		} else {
			encounter_MoveCritterToSpawn(pEnt);
		}
	}

	PERFINFO_AUTO_STOP(); // CheckFallThroughWorld

	if(pEnt->pAttach)
		gslEntUpdateAttach(pEnt);

	// volume query
	// CD: we may not need to query for every entity
	if(	!entCheckFlag(pEnt, ENTITYFLAG_CIV_PROCESSING_ONLY) && 
		!(entCheckFlag(pEnt, ENTITYFLAG_IS_PLAYER) && entCheckFlag(pEnt, ENTITYFLAG_DONOTSEND)) )
	{
		static Vec3 box_min = {-1.5, 0, -1.5}, box_max = {1.5, 6, 1.5};
		const Capsule *cap;
		Quat rot;
		Mat4 world_mat;

		PERFINFO_AUTO_START("VolumeQuery", 1);

		cap = entGetPrimaryCapsule(pEnt);
		if(cap)
		{
			entGetRot(pEnt, rot);
			quatToMat(rot, world_mat);
			entGetPos(pEnt, world_mat[3]);
			wlVolumeCacheQueryCapsule(pEnt->volumeCache, cap, world_mat);
		}
		else
		{
			copyMat3(unitmat, ent_mat);
			entGetPos(pEnt, ent_mat[3]);
			wlVolumeCacheQueryBox(pEnt->volumeCache, ent_mat, box_min, box_max);
		}

		PERFINFO_AUTO_STOP();

		// The general query will potentially create or destroy this secondary cache, which has a custom enter/exit callback
		if(pEnt->externalInnate && pEnt->externalInnate->pPowerVolumeCache)
		{
			Vec3 center;

			PERFINFO_AUTO_START("ExternalInateVolumeCheck", 1);
			if(cap)
			{
				CapsuleMidlinePoint(cap,world_mat[3],rot,0.5,center);
			}
			else
			{
				copyVec3(ent_mat[3],center);
			}

			// TODO(JW): This really should be a point query, not a tiny sphere
			wlVolumeCacheQuerySphere(pEnt->externalInnate->pPowerVolumeCache,center,0.01);

			PERFINFO_AUTO_STOP();
		}

		// CD: after this point systems can look at what volumes the entity is in by calling
		//     wlVolumeCacheGetCachedVolumes and iterating through the list to look for a
		//     particular volume type
	}


	PERFINFO_AUTO_START("Check Rollovers", 1);
	//check to rollover loot ents to see if they have been "rolled over" by any ents
	//this needs to be outside IsAlive check since rollovers are corpses
	gslUpdate_rollovers(pEnt);
	PERFINFO_AUTO_STOP();
	
	// Update and validate the team info on any entity that has a team structure (and thus is,
	// or has at some point been, on a team). Needs to be outside the entIsAlive() check, as
	// dead players still need their team info kept up to date.
	if (pEnt->pTeam)
	{
		PERFINFO_AUTO_START("Update Team Info", 1);
		pEnt->pTeam->fLastUpdate += gGSLState.secondsElapsed.reality.cur;
		if (pEnt->pTeam->fLastUpdate > 1.0f)
		{
			gslTeam_EntityUpdate(pEnt);
			pEnt->pTeam->fLastUpdate = 0.0f;
		}
		PERFINFO_AUTO_STOP();
	}
	
	// handle changes to the AITeam based on changes to the player team
	if (pEnt->pPlayer)
	{
		PERFINFO_AUTO_START("Update AI Team", 1);
		aiTeamUpdatePlayerMembership(pEnt);
		PERFINFO_AUTO_STOP();
	}
	
	// Update and validate the guild info on any player that has a guild structure (and thus is,
	// or has at some point been, in a guild). Needs to be outside the entIsAlive() check, as
	// dead players still need their guild info kept up to date.
	if (pEnt->pPlayer && pEnt->pPlayer->pGuild)
	{
		PERFINFO_AUTO_START("Update Guild Info", 1);
		pEnt->pPlayer->pGuild->fLastUpdate += gGSLState.secondsElapsed.reality.cur;
		if (pEnt->pPlayer->pGuild->fLastUpdate > 1.0f)
		{
			gslGuild_EntityUpdate(pEnt);
			pEnt->pPlayer->pGuild->fLastUpdate = 0.0f;
		}
		PERFINFO_AUTO_STOP();
	}
	
	// Skip AI & interact behaviors if partition is paused
	if (!mapState_IsMapPausedForPartition(entGetPartitionIdx(pEnt)))
	{
		if(entIsAlive(pEnt))
		{		
			if (pEnt->pPlayer)
			{
				// Check to see if the player is near any world interaction nodes
				// If you ever want it to be more specific on which interaction class
				// change which flags it is querying instead of 0xffffffff
				PERFINFO_AUTO_START("interaction_OncePerFrameScanTick", 1);
					interaction_OncePerFrameScanTick(pEnt);
				PERFINFO_AUTO_STOP();
			}
		}
		else // !entIsAlive(pEnt)
		{
			entDeleteUIVar(pEnt, "Interact");	// clean up interaction UI var
		}

		PERFINFO_AUTO_START("Check AI Combat Team", 1);
		iAICombatTeamID = (S64)aiTeamGetCombatTeam(pEnt, pEnt->aibase);
		if (iAICombatTeamID != pEnt->iAICombatTeamID)
		{
			pEnt->iAICombatTeamID = iAICombatTeamID;
			entity_SetDirtyBit(pEnt, parse_Entity, pEnt, false);
		}
		PERFINFO_AUTO_STOP();
	}
}

//remote function called by the map manager if it needs to query the status of all
//currently running maps
AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
GameServerDescription *gslGetGameServerDescription(void)
{
	//if we're not running yet, we can't give the map manager trustworthy information, so we give it all zeros
	if (!GSM_IsStateActive(GSL_RUNNING))
	{
		GameServerDescription *pRetVal = StructCreate(parse_GameServerDescription);
		return pRetVal;
	}
	else
	{
		GameServerDescription *pRetVal = StructAlloc(parse_GameServerDescription);
		StructCopyFields(parse_GameServerDescription, &gGSLState.gameServerDescription, pRetVal, 0, 0);

		return pRetVal;
	}
}


//version of the above for NewMapTransfer
AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
GSDescription_And_ZoneMapInfo *gslGetGameServerDescriptionForNewMapTransfer(void)
{
	GSDescription_And_ZoneMapInfo *pRetVal = StructCreate(parse_GSDescription_And_ZoneMapInfo);
	MapPartitionSummary **ppPartitions = NULL;
	ZoneMapInfo *pZoneMapInfo = zmapGetInfo(NULL);

	//if we're not running yet, we can't give the map manager trustworthy information, so we give it all zeros
	if (!GSM_IsStateActive(GSL_RUNNING))
	{
		return pRetVal;
	}

	//we somehow don't know our own zonemapinfo. That seems bad
	if (!pZoneMapInfo)
	{
		return pRetVal;
	}

	pRetVal->pDescription = StructCreate(parse_GameServerExe_Description);
	pRetVal->pZoneMapInfo = StructClone(parse_ZoneMapInfo, pZoneMapInfo);

	if (GetAppGlobalType() != GLOBALTYPE_GAMESERVER)
	{
		pRetVal->pDescription->eServerType = GSTYPE_NOT_A_GS;
	}
	else if (isProductionEditMode())
	{
		pRetVal->pDescription->eServerType = GSTYPE_UGC_EDIT;
		pRetVal->pDescription->iUGCProjectID = GET_REF(gGSLState.hUGCProjectFromSubscription)->id;
	}
	else if (GET_REF(gGSLState.hUGCProjectFromSubscription))
	{
		pRetVal->pDescription->eServerType = GSTYPE_UGC_PLAY;
		pRetVal->pDescription->iUGCProjectID = GET_REF(gGSLState.hUGCProjectFromSubscription)->id;
	}
	else
	{
		pRetVal->pDescription->eServerType = GSTYPE_NORMAL;
	}

	pRetVal->pDescription->eMapType = gGSLState.gameServerDescription.baseMapDescription.eMapType;
	pRetVal->pDescription->iListeningPortNum = gGSLState.gameServerDescription.iMapPort;
	pRetVal->pDescription->pMapDescription = allocAddString(gGSLState.gameServerDescription.baseMapDescription.mapDescription);

	partition_GetUnownedPartitionSummaryEArray(&ppPartitions);

	FOR_EACH_IN_EARRAY(ppPartitions, MapPartitionSummary, pSummary)
	{
		eaPush(&pRetVal->pDescription->ppPartitions, StructClone(parse_MapPartitionSummary, pSummary));
	}
	FOR_EACH_END
	


	return pRetVal;
}




AUTO_COMMAND_REMOTE;
const char *gslGetGameServerName(void)
{
	//if we're not running yet, we can't give the map manager trustworthy information, so we give it all zeros
	if (!GSM_IsStateActive(GSL_RUNNING)) {
		return "";
	} else {
		return gGSLState.gameServerDescription.baseMapDescription.mapDescription;
	}
}

AUTO_COMMAND_REMOTE;
const char *gslGetZoneMapName(void)
{
	if (!GSM_IsStateActive(GSL_RUNNING)) {
		return "";
	} else {
		return zmapInfoGetPublicName(NULL);
	}
}

//this function needs to know the difference between maps that die if no one is in them and maps
//that stay around
//
//For now, servers time out after n minutes in production mode, so that the playtest shards and so forth can 
//clean themselves up, and never in dev mode, because why mess with servers in dev mode?
int gslGetPartitionInactivityTimeout(void)
{

	if ((isProductionMode() || s_bSimulateProductionMapLifecycle) && !GslIsDoingLoginServerStressTest())
	{	
		return sPartitionInactivityTimeoutMinutes * 60;
	}
	else
	{
		return 0;
	}
}

int gslGetServerInactivityTimeout(void)
{

	if ((isProductionMode() || s_bSimulateProductionMapLifecycle) && !GslIsDoingLoginServerStressTest())
	{	
		return sServerInactivityTimeoutMinutes * 60;
	}
	else
	{
		return 0;
	}
}

AUTO_COMMAND;
char *MapInfo(void)
{
	static char retString[256];
	sprintf(retString, "%s - %d", gGSLState.gameServerDescription.baseMapDescription.mapDescription, gGSLState.gameServerDescription.baseMapDescription.mapInstanceIndex);
	return retString;
}

AUTO_COMMAND;
void wcPrintActorsS(Entity *pEnt, S32 cellx, S32 cellz)
{
	wcPrintActors(entGetPartitionIdx(pEnt), cellx, cellz);
}



int gslSetMotdOnClient(NetLink* link, S32 index, ClientLink *clientLink, void *func_data)
{
	int i;

	if (clientLink->disconnected)
		return 1;

	for (i = 0; i < eaiSize(&clientLink->localEntities); i++)
	{
		Entity *ent = entFromEntityRefAnyPartition(clientLink->localEntities[i]);
		if (ent)
		{

			ClientCmd_SetMotd(ent, func_data);
		}
	}

	return 1;
}

void gslGameSpecificPeriodicUpdate(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	PERFINFO_AUTO_START_FUNC();
	{
		EntityIterator *iter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
		Entity *pent;

		while(pent = EntityIteratorGetNext(iter))
		{
			gameSpecific_EntityTick(pent,timeSinceLastCallback);
		}
		EntityIteratorRelease(iter);
	}
	PERFINFO_AUTO_STOP();// FUNC
}

void
AddictionPlaySessionBootPlayer(Entity *playerEnt)
{
    char *pch = NULL;
    entFormatGameMessageKey(playerEnt, &pch, "Addiction_Kick",
        STRFMT_TIMER("MaxPlayTime", gAddictionMaxPlayTime),
        STRFMT_END);
    if (entGetClientLink(playerEnt))
    {
        gslSendForceLogout(entGetClientLink(playerEnt), pch);
    }
    // Always logged. Not disabled behind gbEnableGamePlayDataLogging flag.
    entLog(LOG_PLAYER, playerEnt, "AddictionKick", "Sending an addiction KICK");
    gslLogOutEntity(playerEnt, 0, 0);
    estrDestroy(&pch);
}

void gslPeriodicUpdate(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	static int s_NoPartitionInactivity = 0;

	EntityIterator *iter;
	Entity *pent;
	U32 iTime = timeSecondsSince2000();
	int i;
	int iPartitionInactivityTimeout; // if 0, the map never dies
	int iServerInactivityTimeout; // if 0, the map never dies

	const char *pNewMapName;

	PERFINFO_AUTO_START_FUNC();

	iPartitionInactivityTimeout = gslGetPartitionInactivityTimeout(); // if 0, the map never dies
	iServerInactivityTimeout = gslGetServerInactivityTimeout(); // if 0, the map never dies

	pNewMapName = zmapGetFilename(NULL);

	gslGlobalInfo.bIsEditingServer = isProductionEditMode();
	gslGlobalInfo.iNumPartitions = partition_GetActualActivePartitionCount();

	if (gslGlobalInfo.mapName[0])
	{
		if (pNewMapName)
		{
			ANALYSIS_ASSUME(pNewMapName);
			if (strcmp(pNewMapName, gslGlobalInfo.mapName) != 0)
			{
				strcpy(gslGlobalInfo.mapName, pNewMapName);
				getFileNameNoExtNoDirs(gslGlobalInfo.mapNameShort, pNewMapName);
				gslGlobalInfo.bMapNameHasChanged = true;
			}
		}
	}
	else if(pNewMapName)
	{
		strcpy(gslGlobalInfo.mapName, pNewMapName?pNewMapName:"");
		getFileNameNoExtNoDirs(gslGlobalInfo.mapNameShort, gslGlobalInfo.mapName);
	}

	gslGlobalInfo.iSlowTransCount = gServerLibState.iSlowTransationCount;
	gslGlobalInfo.iLaggyFrameCount = gGSLState.iLaggyFrames;

	gslGlobalInfo.eMapType = gGSLState.gameServerDescription.baseMapDescription.eMapType;

	gslGlobalInfo.fFPS_gsl = gGSLState.server_fps;
	gslGlobalInfo.iNumEntities = gslGlobalInfo.iNumPlayers = 0;
	for (i = 0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		if (GlobalTypeParent(i) == GLOBALTYPE_ENTITY)
		{
			gslGlobalInfo.iNumEntities += objCountOwnedContainersWithType(i);
		}
	}
	gslGlobalInfo.iNumPlayers = objCountOwnedContainersWithType(GLOBALTYPE_ENTITYPLAYER);

	gslGlobalInfo.bEnableOpenInstancing = false;

	gslGlobalInfo.iVirtualShardID = gGSLState.gameServerDescription.baseMapDescription.iVirtualShardID;

	if (gGSLState.uiStartTime == 0)
	{
		gGSLState.uiStartTime = iTime;
	}

	PERFINFO_AUTO_START("EntCounting", 1);
	{
		int activeEnts = 0;
		int combatEnts = 0;
		int projectileEnts = 0;
		bool bCheckedTeam = false;
		U32 uiElapsedSeconds = iTime - gGSLState.uiStartTime;

		iter = entGetIteratorAllTypesAllPartitions(0,ENTITYFLAG_IGNORE);

		while(pent = EntityIteratorGetNext(iter))
		{
			if(ENTACTIVE(pent))
				activeEnts++;
			if(pent->aibase && pent->aibase->inCombat)
				combatEnts++;
			if(pent->pProjectile)
				projectileEnts++;
			if (pent->pPlayer)
			{
				NOCONST(Entity) *pMutableEnt = CONTAINER_NOCONST(Entity, pent);
				int iPartitionIdx = entGetPartitionIdx(pent);

				gslAutoAwayCheck(pent);
				pMutableEnt->pPlayer->fTotalPlayTime += timeSinceLastCallback;
				pMutableEnt->pPlayer->iLastPlayedTime = iTime;

                if ( pent->pPlayer->addictionPlaySessionEndTime && ( pent->pPlayer->addictionPlaySessionEndTime <= timeSecondsSince2000() ) )
                {
                    AddictionPlaySessionBootPlayer(pent);
                }

				gslItemAssignments_UpdatePlayerAssignments(pent);

				gslActivity_UpdateEvents(pent);

				// Update the number of slots available in the sahred bank (if it exists and is subscribed on this server)
				SharedBankFixupCheck(pent);

				if ( gslGlobalInfo.eMapType == ZMTYPE_MISSION && uiElapsedSeconds <= s_uiEnableOpenInstancingDuration )
				{
					PERFINFO_AUTO_START("OpenInstancing", 1);

					// don't enable open instancing on "teamNotRequired" maps (starship bridges)
					if ( !zmapInfoGetTeamNotRequired(gGSLState.gameServerDescription.baseMapDescription.pZoneMapInfo) )
					{
						if ( partition_OwnerTypeFromIdx(iPartitionIdx) == GLOBALTYPE_TEAM && !bCheckedTeam )
						{
							Team* pTeam = team_GetTeam(pent);
							if ( pTeam && pTeam->iContainerID == partition_OwnerIDFromIdx(iPartitionIdx) )
							{
								bCheckedTeam = true;
								if ( (pTeam->eMode == TeamMode_Open || pTeam->eMode == TeamMode_Prompt) && (U32)(team_NumTotalMembers(pTeam)) < s_uiEnableOpenInstancingTeamSize)
								{
									gslGlobalInfo.bEnableOpenInstancing = true;
									gslGlobalInfo.iDifficulty = pTeam->iDifficulty;
								}
							}
						}
						else if ( partition_OwnerTypeFromIdx(iPartitionIdx) == GLOBALTYPE_ENTITYPLAYER )
						{
							if ( entGetContainerID(pent) == partition_OwnerIDFromIdx(iPartitionIdx) )
							{
								if ( pent->pPlayer && pent->pPlayer->eLFGMode == TeamMode_Open )
								{
									gslGlobalInfo.bEnableOpenInstancing = true;
									gslGlobalInfo.iDifficulty = pent->pPlayer->iDifficulty;
								}
							}
						}
					}

					PERFINFO_AUTO_STOP(); // OpenInstancing
				}
			}
		}

		EntityIteratorRelease(iter);

		gslGlobalInfo.iNumActiveEnts = activeEnts;
		gslGlobalInfo.iNumCombatEnts = combatEnts;
		gslGlobalInfo.iNumProjectileEnts = projectileEnts;
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("IgnorePlayerIdleCheck", 1);

    iter = entGetIteratorSingleTypeAllPartitions(ENTITYFLAG_IGNORE, 0, GLOBALTYPE_ENTITYPLAYER);
    while(pent = EntityIteratorGetNext(iter))
    {
        gslIgnoredPlayerIdleCheck(pent);
    }

    EntityIteratorRelease(iter);

	PERFINFO_AUTO_STOP(); // IgnorePlayerIdleCheck

	PERFINFO_AUTO_START("ReportGSLGlobalInfo", 1);

	gslGlobalInfo.iNumPartitionsSinceServerStart = partition_GetNumPartitionsSinceServerStart();
	gslGlobalInfo.iMaxPartitionsSinceServerStart = partition_GetMaxPartitionsSinceServerStart();
	if (gslGlobalInfo.iNumPlayers > 0) {
		gslGlobalInfo.iLastTimeWithPlayers = iTime;
	}
	partition_GetUnownedPartitionSummaryEArray(&gslGlobalInfo.ppPartitions);
	partition_GetPublicIndicesEstring(&gslGlobalInfo.pIndices);

	RemoteCommand_HereIsGSLGlobalInfo(GLOBALTYPE_CONTROLLER, 0, gServerLibState.containerID, gGSLState.iLowLevelIndexOnController, &gslGlobalInfo);
	RemoteCommand_HereIsGSLGlobalInfo_ForMapManager(GLOBALTYPE_MAPMANAGER, 0, gServerLibState.containerID, &gslGlobalInfo);

	eaDestroy(&gslGlobalInfo.ppPartitions);

	PERFINFO_AUTO_STOP(); // ReportGSLGlobalInfo

	// Note that "iPartitionInactivityTimeout" is zero in non-production environments
	if ((iPartitionInactivityTimeout || sbForceTimeoutNow || ( s_bSimulateProductionMapLifecycle)) && !gslMapCanNotTimeOutRightNow())
	{
		int iPartitionIdx;
		int iNumPartitions = 0;

		PERFINFO_AUTO_START("InactivityTimeout", 1);

		// Check inactivity on each partition
		for(iPartitionIdx = partition_GetCurNumPartitionsCeiling()-1; iPartitionIdx>=0; --iPartitionIdx)
		{
			if (!partition_ExistsByIdx(iPartitionIdx)) 
			{
				continue; // Skip partitions that don't exist
			}

			++iNumPartitions;
			s_NoPartitionInactivity = 0;

			//this check may need to be made more sophisticated to deal with players in the process of logging in/out
			if (partition_GetPlayerCount(iPartitionIdx) == 0 || sbForceTimeoutNow)
			{
				partition_IncInactivity(iPartitionIdx, timeSinceLastCallback);

				if (partition_GetInactivity(iPartitionIdx) >= iPartitionInactivityTimeout || 
					sbForceTimeoutNow || 
					partition_TestForImmediateDeath(iPartitionIdx))
				{
					partition_DebugLogInternal(PARTITION_TIMEOUT_REQUESTED, iPartitionIdx, "Inactivity: %f. ForceTimeOutnow: %d. TestForImmediateDeath: %d",
						partition_GetInactivity(iPartitionIdx), sbForceTimeoutNow, partition_TestForImmediateDeath(iPartitionIdx));


					partition_ClearInactivity(iPartitionIdx);
					partition_SetTestForImmediateDeath(iPartitionIdx, false);

					// when a partition has had enough inactivity to kill itself, it sends a remote command to 
					// the map manager, which will decide whether it should, in fact, kill itself, and calls
					// another remote command to kill it if it decides that it should in fact die

					RemoteCommand_IHaveTimedOutPartition(GLOBALTYPE_MAPMANAGER, 0, gServerLibState.containerID, partition_IDFromIdx(iPartitionIdx));
				}
			}
			else
			{
				partition_ClearInactivity(iPartitionIdx);
			}
		}
		if (iNumPartitions == 0)
		{
			s_NoPartitionInactivity += timeSinceLastCallback;

			if ((s_NoPartitionInactivity >= iServerInactivityTimeout) || sbForceTimeoutNow)
			{
				s_NoPartitionInactivity = 0;

				//now when a gameserver has had enough inactivity to kill itself, it sends a remote command to 
				//the map manager, which will decide whether it should, in fact, kill itself, and calls
				//another remote command to kill it if it decides that it should in fact die
				RemoteCommand_IHaveTimedOutServer(GLOBALTYPE_MAPMANAGER, 0, gServerLibState.containerID);
			}
		}
		PERFINFO_AUTO_STOP();
	}


	PERFINFO_AUTO_START("Update MOTD", 1);
	{
		char* pMotdString = MapInfo();
		for(i = 0; i < eaSize(&gGSLState.clients.netListens); i++)
		{
			linkIterate2(gGSLState.clients.netListens[i], gslSetMotdOnClient, pMotdString);
		}
	}
	PERFINFO_AUTO_STOP();

	gslBulletins_Update(NULL, false);
	
	PurgeScreenShotCache();

	//debugging command to deliberately cripple another game server
	UpdateBogDownTest();

	PERFINFO_AUTO_STOP();// FUNC
}

void KillYourselfGracefullyCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	ErrorOrAlert("CONTROLLER_NEVER_SAID_DIE", "Called ServerIsGoingToDie on controller, didn't hear back in 10 minutes");


	GSM_Quit("KillYOurselfGracefully_DidntHearBack");

}

// from gslHandleMsg.c
extern bool gLoginAttempted;

AUTO_COMMAND_REMOTE;
void KillYourselfGracefully(void)
{
	char deathString[1024];
	sprintf(deathString, "Game Server %u killing itself gracefully, presumably due to timeout", gServerLibState.containerID);
	RemoteCommand_ServerIsGoingToDie(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), gServerLibState.containerID,deathString, false, true);
	log_printf(LOG_GSL, "%s", deathString);
	if ( !gLoginAttempted )
	{
		log_printf(LOG_LOGIN, "Game server shutting down gracefully after no clients logged in.  Port = %d\n", gGSLState.gameServerDescription.iMapPort);
	}
	TimedCallback_Run(KillYourselfGracefullyCB, NULL, 600);
}

//the equivalent of the above for partitions
AUTO_COMMAND_REMOTE;
void PartitionShouldClose(U32 uPartitionID)
{
	U32 iPartitionIdx = partition_IdxFromID(uPartitionID);
	if (iPartitionIdx >= 0) {
		partition_DestroyByIdx(iPartitionIdx, "PartitionShouldClose");
	}
}

void OVERRIDE_LATELINK_GoAheadAndDie_ServerSpecific(void)
{
	if(beaconIsBeaconizer())
		exit(0);
	else
		GSM_Quit("KillYOurselfGracefully");
}

AUTO_COMMAND_REMOTE;
void TestPartitionForImmediateDeath(U32 iPartitionID, S32 eReason)
{
	int iIdx;

	// This is called when a map transfer has completed successfully. The transferred-to gameserver
	//tells the transferred-from one that it can maybe kill itself

	switch (eReason)
	{
	xcase PARTITIONDEATH_PLAYER_TRANSFERRED_OFF:
		if (!MapCanBeClosedImmediatelyOnPlayerTransferOff(&gGSLState.gameServerDescription.baseMapDescription))
		{
			return;
		}
	}

	
	iIdx = partition_IdxFromID(iPartitionID);

	if (iIdx != -1)
	{
		partition_SetTestForImmediateDeath(iIdx, true);
	}
}

AUTO_COMMAND_REMOTE;
void ForceServerTimeout(void)
{
	// This is called to force a pet interior map to shut down.  It is generally called
	//  when the player changes the interior.
	sbForceTimeoutNow = true;
}

void timerRecordStartAutoCallback(TimedCallback* callback, F32 timeSinceLastCallback, UserData data)
{
	printf("Ending auto timer record\n");
	timerRecordThreadStop();
}

AUTO_COMMAND ACMD_CATEGORY(Profile, Standard);
void timerRecordStartAuto()
{
	static char* filename = NULL;
	char* findDecimal;
	char fileNameAbsolute[CRYPTIC_MAX_PATH];

	estrPrintf(&filename, "%s %s %dpl%dents%.2ffps", timeGetLocalDateString(),
		gslGlobalInfo.mapNameShort, gslGlobalInfo.iNumPlayers, gslGlobalInfo.iNumEntities, gslGlobalInfo.fFPS_gsl);

	for(findDecimal = filename; *findDecimal; findDecimal++)
	{
		if(*findDecimal == '.' || *findDecimal == ':')
			*findDecimal = '_';
	}

	printf("Starting auto timer record (%.2f FPS)\n", gslGlobalInfo.fFPS_gsl);
	
	timerGetProfilerFileName(filename, SAFESTR(fileNameAbsolute), 1);
	timerRecordThreadStart(fileNameAbsolute);

	TimedCallback_Run(timerRecordStartAutoCallback, NULL, 10);
}

AUTO_COMMAND_REMOTE;
void timerRecordStartAutoRemote()
{
	timerRecordStartAuto();
}

const char *OVERRIDE_LATELINK_GetExtraInfoForLogPrintf(void)
{
	return zmapGetFilename(NULL);
}
/*
AUTO_COMMAND;
void testKillsPerHour(Entity *pEnt)
{
	int i, j;

	for (i=0; i < 10000; i++)
	{
		for (j=1; j < 30; j++)
		{
			entLog(LOG_PLAYER, pEnt, "KillsPerHour", "Level %d KPH %f", j, (randomPositiveF32()) * 100 + (randomPositiveF32()) * 100 + (randomPositiveF32()) * 100 + (randomPositiveF32()) * (j * 10));
		}
	}
}*/

void gslVerifyServerTypeExistsInShardEx(GlobalType eType)
{
	RemoteCommand_CheckIfServerTypeExists(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalID(), eType);
}

bool gslClientsAreUntrustworthy(void)
{
	return isProductionMode();
}

static WorldVolumeEntry **g_eaPlayableEntries = 0;
void gslPlayableCreate(WorldVolumeEntry *ent)
{
	eaPush(&g_eaPlayableEntries, ent);
}

void gslPlayableDestroy(WorldVolumeEntry *ent)
{
	eaFindAndRemoveFast(&g_eaPlayableEntries, ent);
}

WorldVolumeEntry** gslPlayableGet(void)
{
	return g_eaPlayableEntries;
}

// Convert a string to lower case.
const char *strlower(char *str)
{
	for (; *str; ++str)
		*str = tolower(*str);
	return str;
}

// Logging for activity streams.  Terminate the parameters with a NULL.
void logActivity(Player *pPlayer, const char *pType, ...)
{
	va_list parameters;
	const char *key;
	char *lowerkey = 0;
	char *message = NULL;
	bool first = true;
	char *game, *first_game_space;

	// Get game name.
	game = strdup(GetShardInfoString());
	first_game_space = strchr(game, ' ');
	if (first_game_space)
		*first_game_space = 0;

	// Format JSON message.
	estrStackCreate(&message);
	estrPrintf(&message, "{\"privateaccountname\": \"%s\", \"type\": \"%s\", \"game\": \"%s\", \"date\": \"%lu\", \"data\": [", pPlayer->privateAccountName,
		pType, game, (unsigned long)timeSecondsSince2000());
	free(game);
	va_start(parameters, pType);
	for (key = va_arg(parameters, const char *); key; key = va_arg(parameters, const char *))
	{
		const char *value = va_arg(parameters, const char *);
		char *escapedValue = 0;
		if (!(value && *value))
			continue;
		estrStackCreate(&lowerkey);
		estrCopy2(&lowerkey, key);
		strlower(lowerkey);
		estrStackCreate(&escapedValue);
		estrCopy2(&escapedValue, value);
		estrReplaceOccurrences(&escapedValue, "\"", "\\\"");
		if (first)
		{
			estrAppend2(&message, "{\"");
			first = false;
		}
		else
			estrAppend2(&message, ", {\"");
		estrAppend2(&message, key);
		estrAppend2(&message, "\": \"");
		estrAppend2(&message, escapedValue);
		estrAppend2(&message, "\"}");
		estrDestroy(&lowerkey);
		estrDestroy(&escapedValue);
	}
	va_end(parameters);

	// Send message to LogParser.
	estrAppend2(&message,"]}");
	log_printf(LOG_ACTIVITY, "%s", message);
	estrDestroy(&message);
}

// Test the logActivity() API.
AUTO_COMMAND;
void ActivityStreamTest(const char *privateAccountName, const char *command, const char *data)
{
#define ACTIVITYSTREAMTEST_PAIRS 6  // Must match call to logActivity() below.
	char *key[ACTIVITYSTREAMTEST_PAIRS];
	char *value[ACTIVITYSTREAMTEST_PAIRS];
	Player p;
	char *split = strdup(data);
	unsigned i;

	char *check;
	char *context = 0;

	// Parse key-value pairs.
	for (i = 0; i != ACTIVITYSTREAMTEST_PAIRS; ++i)
	{
		key[i] = strtok_s(split, ",", &context);
		value[i] = strtok_s(NULL, ",", &context);
	}
	check = strtok_s(NULL, ",", &context);
	if (check)
		puts("Sorry, only " STRINGIZE(ACTIVITYSTREAMTEST_PAIRS) " key value pairs supported.");

	// Log activity.
	strcpy_s((char *)p.privateAccountName, sizeof(p.privateAccountName), privateAccountName);
	logActivity(&p, command, key[0], value[0], key[1], value[1], key[2], value[2], key[3], value[3], key[4], value[4], key[5], value[5], 0);
	printf("Sent test ItemAcquired for account \"%s\"\n", privateAccountName);
	free(split);
}

AUTO_COMMAND ACMD_SERVERCMD;
void gslOpenFile(const char *pcName, const char *pcType)
{
	if(isDevelopmentMode() && RefSystem_DoesDictionaryExist(pcType))
	{
		void *referent;
		char fullfilename[MAX_PATH];
		const char *filename;
		ParseTable *pti = RefSystem_GetDictionaryParseTable(pcType);

		if(!pti)
			return;

		referent = RefSystem_ReferentFromString(pcType, pcName);

		if(!referent)
			return;

		filename = ParserGetFilename(pti, referent);

		fileLocateWrite(filename, fullfilename);
		forwardSlashes(fullfilename);

		if (!fileExists(fullfilename))
			return;

		fileOpenWithEditor(fullfilename);
	}
}

AUTO_STARTUP(ProjectGameServerConfig);
void gslLoadProjectGameServerConfig(void)
{
	loadstart_printf("Loading ProjectSpecificGameServerConfig...");
	// Read in project game server specific data
	ParserReadTextFile("server/ProjectGameServerConfig.txt", parse_ProjectGameServerConfig, &gProjectGameServerConfig, PARSER_OPTIONALFLAG);
	loadend_printf("done.");
}




AUTO_COMMAND_REMOTE;
int TestBogDownCommand(int x)
{
	Sleep(1000);
	return 7;
}

static int siIDForBogDownTest = 0;
AUTO_CMD_INT(siIDForBogDownTest, IDForBogDownTest);


void UpdateBogDownTest(void)
{
	if (siIDForBogDownTest)
	{
		int i;
		for (i=0; i < 1000; i++)
		{
			RemoteCommand_TestBogDownCommand(NULL, GLOBALTYPE_GAMESERVER, siIDForBogDownTest, i);
		}
	}
}

bool gslLoadZoneMapByName(const char *map_name)
{
	static char currentZoneChannel[64] = "";
	if (worldLoadZoneMapByName(map_name))
	{
		ServerChat_InitializeZoneChannel();
		return true;
	}
	return false;
}

AUTO_STARTUP(ChoiceTable) ASTRT_DEPS(CritterGroups);
void choiceTableAutoStartup(void)
{
	choice_Load();
}

AUTO_COMMAND_REMOTE;
void HereIsYourLowLevelControllerIndex(int iLowLevelIndex)
{
	gGSLState.iLowLevelIndexOnController = iLowLevelIndex;
}


bool OVERRIDE_LATELINK_IsGameServerBasedType(void)
{
	return true;
}



#include "../../crossroads/appserverlib/pub/aslResourceDBPub.h"
#include "ResourceDBSupport.h"
#include "staticworld/worldGridPrivate.h"
#include "gameServerLib_c_ast.h"
#include "Mission_Common.h"
AUTO_STRUCT;
typedef struct TestResStruct
{
	REF_TO(ZoneMapInfo) hZoneMapInfo;
	REF_TO(Message) hMessage;
	REF_TO(MissionDef) hMissionDef;
} TestResStruct;

TestResStruct *pTestResStruct = NULL;

static void GetResourceNamesCB(TransactionReturnVal *returnStruct, U32 *pData)
{
	NameSpaceAllResourceList *pList;
	enumTransactionOutcome eOutcome = RemoteCommandCheck_RequestResourceNames(returnStruct, &pList);

	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{





		StructDestroy(parse_NameSpaceAllResourceList, pList);
	}


}

AUTO_COMMAND;
void ResDBTest(void)
{
	SetUseResourceDB(true);
	pTestResStruct = StructCreate(parse_TestResStruct);
	SET_HANDLE_FROM_REFDATA(g_ZoneMapDictionary, "Awtemp_Ugc_5_0fcf6e89:355537148", pTestResStruct->hZoneMapInfo);
//	SET_HANDLE_FROM_REFDATA(gMessageDict, "3_8d9a1d41:327091831.Displayname", pTestResStruct->hMessage);
//	SET_HANDLE_FROM_REFDATA(g_MissionDictionary, "3_8d9a1d41:327091831_Mission_Ugc_Openmission", pTestResStruct->hMissionDef);


//	SET_HANDLE_FROM_REFDATA(g_MissionDictionary, "Ugc_1_3b7fc339:Bananana_Mission_Ugc_Openmission", pTestResStruct->hMissionDef);


//	RemoteCommand_RequestResourceNames(objCreateManagedReturnVal(GetResourceNamesCB, NULL), GLOBALTYPE_RESOURCEDB, 0, 
//		"3_8d9a1d41");		
}

void OVERRIDE_LATELINK_MemLeakTracking_ExtraReport(char *pFilePrefix)
{
	char filename[CRYPTIC_MAX_PATH];
	FILE *pFile;
	char *pFullString = NULL;

	sprintf(filename, "%s_entities.txt", pFilePrefix);
	mkdirtree_const(filename);

	pFile = fopen(filename, "wt");
	if (!pFile)
	{
		CRITICAL_NETOPS_ALERT("CANT_WRITE_ENT_DUMP_FILE", "Tried to open %s for entity report, failed", filename);
		return;
	}

	createEntityReport(&pFullString);
	fprintf(pFile, "%s", pFullString);
	fclose(pFile);
	estrDestroy(&pFullString);
}

bool gslMapCanNotTimeOutRightNow(void)
{
	return gGSLState.bAtomicPartsOfUGCPublishHappening || !gGSLState.bInformedMapManagerMapIsDoneLoading;
}

void TellControllerWeMayBeStallyForNSeconds(int iNumSeconds, char *pReason)
{
	RemoteCommand_ServerMayBeStallyOverNextNSeconds(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), GetAppGlobalID(), iNumSeconds, pReason);
	commFlushAllLinks(commDefault());
}

void TellControllerToLog(char* strLog)
{
	RemoteCommand_ServerLogOnController(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), GetAppGlobalID(), strLog);
	commFlushAllLinks(commDefault());
}


//note that there's some redundancy here in that iMapID is both inside and outside pSummary. The idea is that at some point we'll
//have an optimization where NULL pSummary means "I've already sent you the summary for this partition, use what you already have"
AUTO_COMMAND_REMOTE;
void HereIsPartitionInfoForUpcomingMapTransfer(U32 iCommandID, ContainerID iPlayerID, U32 uPartitionID, MapPartitionSummary *pSummary)
{
	MapPartition *pPartition;
	int iPartitionIdx;

	if(!devassert(pSummary))
		return;

	iPartitionIdx = partition_IdxFromID(uPartitionID);
	if (iPartitionIdx >= 0)
		pPartition = partition_FromIdx(iPartitionIdx);
	else
		pPartition = partition_CreateAndInit(pSummary, "HereIsPartitionInfoForUpcomingMapTransfer for player %u, partition %u", iPlayerID, uPartitionID);

	partition_AddUpcomingTransferToPartitionID(pPartition->summary.uPartitionID, iPlayerID);
	// if(pEnt)aih
	// 	PartitionSet(pEnt, pPartition->iPartitionIdx);

	if (iCommandID)
	{
		RemoteCommand_PartitionInfoReceived(GLOBALTYPE_MAPMANAGER, 0, iPlayerID, iCommandID);
	}
}


#if 0
#include "structNet.h"
#include "serialize.h"
#include "structInternals.h"


static char *pResDictNamesForTimingTest[] = 
{
	"CritterDef",
	"Contact",
	"ItemDef",
	"PlayerCostume",
	"PowerDef",
	"EncounterDef",
};


AUTO_COMMAND;
void TextParserTimingTest(void)
{
	int i;
	ResourceIterator iter;



	for (i=0; i < ARRAY_SIZE(pResDictNamesForTimingTest); i++)
	{
		ParseTable *pTPI = resDictGetParseTable(pResDictNamesForTimingTest[i]);
		int iCounter = 0;
		void *pObj;

		loadstart_printf("Going to do text read/write test for dictionary %s (TPI %s)", pResDictNamesForTimingTest[i], 
			ParserGetTableName(pTPI));

		resInitIterator(pResDictNamesForTimingTest[i], &iter);

		while (resIteratorGetNext(&iter, NULL, &pObj))
		{
			char *pString = NULL;
			void *pOtherObj;
			
			ParserWriteText(&pString, pTPI, pObj, 0, 0, 0);
			pOtherObj = StructCreateVoid(pTPI);
			ParserReadText(pString, pTPI, pOtherObj, 0);
			iCounter++;
			StructDestroyVoid(pTPI, pOtherObj);
			estrDestroy(&pString);
		}
		resFreeIterator(&iter);

		loadend_printf("Done, %d objects\n", iCounter);
	}

	for (i=0; i < ARRAY_SIZE(pResDictNamesForTimingTest); i++)
	{
		ParseTable *pTPI = resDictGetParseTable(pResDictNamesForTimingTest[i]);
		int iCounter = 0;
		void *pObj;

		loadstart_printf("Going to do escaped text read/write test for dictionary %s (TPI %s)", pResDictNamesForTimingTest[i], 
			ParserGetTableName(pTPI));

		resInitIterator(pResDictNamesForTimingTest[i], &iter);

		while (resIteratorGetNext(&iter, NULL, &pObj))
		{
			char *pString = NULL;
			char *pTemp;
			void *pOtherObj;
			
			ParserWriteTextEscaped(&pString, pTPI, pObj, 0, 0, 0);
			pOtherObj = StructCreateVoid(pTPI);
			pTemp = pString;
			ParserReadTextEscaped(&pTemp, pTPI, pOtherObj, 0);
			iCounter++;
			StructDestroyVoid(pTPI, pOtherObj);
			estrDestroy(&pString);
		}
		resFreeIterator(&iter);

		loadend_printf("Done, %d objects\n", iCounter);
	}

	for (i=0; i < ARRAY_SIZE(pResDictNamesForTimingTest); i++)
	{
		ParseTable *pTPI = resDictGetParseTable(pResDictNamesForTimingTest[i]);
		int iCounter = 0;
		void *pObj;

		loadstart_printf("Going to do pktSend/pktReceive test for dictionary %s (TPI %s)", pResDictNamesForTimingTest[i], 
			ParserGetTableName(pTPI));

		resInitIterator(pResDictNamesForTimingTest[i], &iter);

		while (resIteratorGetNext(&iter, NULL, &pObj))
		{
			Packet *pPkt = pktCreateTemp(NULL);
			void *pOtherObj;

			ParserSendStruct(pTPI, pPkt, pObj);
			pOtherObj = StructCreateVoid(pTPI);
			ParserRecv(pTPI, pPkt, pOtherObj, 0);
			iCounter++;

			StructDestroyVoid(pTPI, pOtherObj);
			pktFree(&pPkt);
		}
		resFreeIterator(&iter);

		loadend_printf("Done, %d objects\n", iCounter);
	}

	for (i=0; i < ARRAY_SIZE(pResDictNamesForTimingTest); i++)
	{
		ParseTable *pTPI = resDictGetParseTable(pResDictNamesForTimingTest[i]);
		int iCounter = 0;
		void *pObj;
		char *pName;

		loadstart_printf("Going to do bin write test for dictionary %s (TPI %s)", pResDictNamesForTimingTest[i], 
			ParserGetTableName(pTPI));

		resInitIterator(pResDictNamesForTimingTest[i], &iter);

		while (resIteratorGetNext(&iter, &pName, &pObj))
		{
			SimpleBufHandle bufHandle = SimpleBufOpenWrite("", 0, NULL, 0, false);
			void *pOtherObj;
			int sum = 0;

			ParserWriteBinaryTable(bufHandle, NULL, pTPI, pObj, &sum, 0, TOK_NO_WRITE);

			pOtherObj = StructCreateVoid(pTPI);

			SimpleBufSeek(bufHandle, 0, 0);

			sum = 0;

			ParserReadBinaryTable(bufHandle, pTPI, pOtherObj, &sum, 0, TOK_NO_WRITE);

			SimpleBufClose(bufHandle);
			StructDestroyVoid(pTPI, pOtherObj);
		
		}
		resFreeIterator(&iter);

		loadend_printf("Done, %d objects\n", iCounter);
	}

	for (i=0; i < ARRAY_SIZE(pResDictNamesForTimingTest); i++)
	{
		ParseTable *pTPI = resDictGetParseTable(pResDictNamesForTimingTest[i]);
		int iCounter = 0;
		void *pObj;

		loadstart_printf("Going to do 64-bit-encoded pktSend/pktReceive test for dictionary %s (TPI %s)", pResDictNamesForTimingTest[i], 
			ParserGetTableName(pTPI));

		resInitIterator(pResDictNamesForTimingTest[i], &iter);

		while (resIteratorGetNext(&iter, NULL, &pObj))
		{
			Packet *pPkt = pktCreateTemp(NULL);
			void *pOtherObj;
			char *pEncodedString = NULL;
			void *pPayload;
			int iPayloadSize;
			char *pDecodeBuf;
			int iDecodedSize;

			ParserSendStruct(pTPI, pPkt, pObj);

			pPayload = pktGetEntirePayload(pPkt, &iPayloadSize);

			estrBase64Encode(&pEncodedString, pPayload, iPayloadSize);

			pDecodeBuf = malloc(estrLength(&pEncodedString) + 16);
			iDecodedSize = decodeBase64String(pEncodedString, estrLength(&pEncodedString), pDecodeBuf, estrLength(&pEncodedString) + 16);

			assert(iDecodedSize);

			pktFree(&pPkt);
			pPkt = pktCreateTempWithSetPayload(pDecodeBuf, iDecodedSize);

			pOtherObj = StructCreateVoid(pTPI);
			ParserRecv(pTPI, pPkt, pOtherObj, 0);
			iCounter++;

			StructDestroyVoid(pTPI, pOtherObj);
			pktFree(&pPkt);

			free(pDecodeBuf);
			estrDestroy(&pEncodedString);
		}
		resFreeIterator(&iter);

		loadend_printf("Done, %d objects\n", iCounter);
	}

}
		
#endif

static void DebugCheckContainerLocOnObjectDB_CB(TransactionReturnVal *returnVal, void *userData)
{
	char *pResultString = NULL;

	if(RemoteCommandCheck_GetDebugContainerLocString(returnVal, &pResultString) == TRANSACTION_OUTCOME_FAILURE)
	{
		printf("DebugCheckContainerLocOnObjectDB failed\n");
	}
	else
	{
		printf("DebugCheckContainerLocOnObjectDB succeeded: %s\n", pResultString);
		estrDestroy(&pResultString);
	}
}

AUTO_COMMAND;
void DebugCheckContainerLocOnObjectDB(char *pTypeName, U32 iID)
{
	RemoteCommand_GetDebugContainerLocString(objCreateManagedReturnVal(DebugCheckContainerLocOnObjectDB_CB, NULL),
		GLOBALTYPE_OBJECTDB, 0, NameToGlobalType(pTypeName), iID);
}

#include "gameServerLib_c_ast.c"


#include "autogen/UtilitiesLib_autogen_ClientCmdWrappers.c"
#include "autogen/GameClientLib_autogen_ClientCmdWrappers.c"
#include "autogen/GameServerLib_autogen_ServerCmdWrappers.c"
#include "AutoGen/GameServerLib_autogen_QueuedFuncs.c"
#include "AutoGen/SoundLib_autogen_ClientCmdWrappers.c"
#include "AutoGen/WorldLib_autogen_ClientCmdWrappers.c"
#include "autogen/UI2Lib_autogen_ClientCmdWrappers.c"
#include "Autogen/gameserverlib_h_ast.c"


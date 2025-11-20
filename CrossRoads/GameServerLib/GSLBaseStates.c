#include "gameserverlib.h"
#include "globalstatemachine.h"
#include "worldlib.h"
#include "entitylib.h"
#include "ailib.h"
#include "physicssdk.h"
#include "FolderCache.h"
#include "gslcommandparse.h"
#include "serverlib.h"
#include "CostumeCommonTailor.h"
#include "gslextern.h"
#include "gsltransactions.h"
#include "gslBaseStates.h"
#include "remoteautocommandsupport.h"
#include "RegistryReader.h"
#include "Prefs.h"
#include "AutoGen/AppServerLib_autogen_remotefuncs.h"
#include "AutoGen/Controller_autogen_remotefuncs.h"
#include "worldgrid.h"
#include "wltime.h"
#include "worldcoll.h"
#include "entitymovementmanager.h"
#include "utilitieslib.h"
#include "gslSendToClient.h"
#include "utils.h"
#include "ThreadManager.h"
#include "gslMapTransfer.h"
#include "gslPartition.h"
#include "PowerVars.h"
#include "CharacterClass.h"
#include "earray.h"
#include "beacon.h"
#include "wlBeacon.h"
#include "gslCombatCallbacks.h"
#include "gslWorldDebug.h"
#include "gslSound.h"
#include "inventoryCommon.h"
#include "AutoStartupSupport.h"
#include "ResourceCommands.h"
#include "StringCache.h"
#include "gslChat.h"
#include "gslEditor.h"
#include "gslHandleMsg.h"
#include "sock.h"
#include "ResourceManager.h"
#include "ticketnet.h"
#include "AutoTransDefs.h"
#include "Guild.h"
#include "AutoGen/guild_h_ast.h"
#include "logging.h"
#include "Player.h"
#include "JobManagerSupport.h"
#include "fileutil.h"
#include "sysutil.h"
#include "winutil.h"
#include "virtualShard.h"
#include "virtualShard_h_ast.h"
#include "gslMapState.h"
#include "memLeakTracking.h"
#include "gslAuction.h"
#include "eventCountingHeatMap.h"
#include "alerts.h"
#include "ResourceDBSupport.h"
#include "WorldLib.h"
#include "..\WorldLib\StaticWorld\WorldGridPrivate.h"
#include "WebRequestServer\wrContainerSubs.h"
#include "Gateway\gslGatewayServer.h"

#include "gslAccountProxy.h"
#include "gslUGC.h"
#include "gslUGC_cmd.h"
#include "UGCCommon.h"
#include "ugcProjectCommon.h"
#include "ugcProjectCommon_h_Ast.h"
#include "wlUGC.h"
#include "gslDataDump.h"
#include "gslEventtracker.h"
#include "SharedMemory.h"
#include "StringUtil.h"
#include "EntityIterator.h"

#include "SimpleCpuUsage.h"
#include "gslSimpleCpuUsage.h"

#define GSL_HANDSHAKE_KEEPALIVE_DELAY 15

extern ParseTable parse_NamedPathQueriesAndResults[];
#define TYPE_parse_NamedPathQueriesAndResults NamedPathQueriesAndResults

static int gMakeMemdumpBetweenMaps, gValidateDepCheck;

//if set, then write the mission wiki to a file and then quit
static char *spFileForMissionWiki = NULL;
AUTO_CMD_ESTRING(spFileForMissionWiki, FileForWritingMissionWikiAndExiting);

// if true and MakeBinsAndExit is set, write a memory dump between each map binning
AUTO_CMD_INT(gMakeMemdumpBetweenMaps, WriteMemdumpBetweenMaps) ACMD_CMDLINE;

// if true and MakeBinsAndExit is set, validate the fast dependency checker before binning
AUTO_CMD_INT(gValidateDepCheck, ValidateDependencyCheck) ACMD_CMDLINE;

static int chimeOnReady = 0;
AUTO_CMD_INT(chimeOnReady, readyChime);

AUTO_CMD_INT(gGSLState.threadPriorityOverride, threadPriority) ACMD_CMDLINE;


static F32 maxFPS = 30.f;
static F32 inverseMaxFPS = 1.f / 30.f;
static int server_startup_timer;

//stuff relating to slow frame counting/alerts

//a frame longer than this is "slow"
static float sfSlowFrameSeconds = 0.5f;
AUTO_CMD_FLOAT(sfSlowFrameSeconds, SlowFrameSeconds);

//alert when you see this many slow frames...
static int siNumSlowFramesToAlertOn = 2;

//...within this many seconds...
static int siSlowFramesWithinWindowToAlert = 300;

//...then don't alert again for this long
static int siSlowFrameAlertThrottle = 3600;

AUTO_CMD_INT(siNumSlowFramesToAlertOn, NumSlowFramesToAlertOn);
AUTO_CMD_INT(siSlowFramesWithinWindowToAlert, SlowFramesWithinWindowToAlert);
AUTO_CMD_INT(siSlowFrameAlertThrottle, SlowFrameAlertThrottle);



int siFloodLogServer = 0;
AUTO_CMD_INT(siFloodLogServer, FloodLogServer) ACMD_CATEGORY(test);

//time (seconds) after gslrunning begins before mem leak tracking begins (time to get into "normal" running mode))
static int siTimeBeforeStartingMemLeakTracking = 30;
AUTO_CMD_INT(siTimeBeforeStartingMemLeakTracking, TimeBeforeStartingMemLeakTracking);

//how often to do a mem leak tracking check
static int siMemLeakTrackingInterval = 300;
AUTO_CMD_INT(siMemLeakTrackingInterval, MemLeakTrackingInterval);

//memory increase that triggers a mem leak tracking alert
static int siMemLeakTrackingIncreaseAmount = 60 * 1024 * 1024;
AUTO_CMD_INT(siMemLeakTrackingIncreaseAmount, MemLeakTrackingIncreaseAmount);

//check for mem leaks with a corrected RAM amount consisting of actual RAM amount 
//minus this much per player
static int siMemLeakCorrectionPerPlayer = 9 * 1024 * 1024;
AUTO_CMD_INT(siMemLeakCorrectionPerPlayer, MemLeakCorrectionPerPlayer);

//minus this much per partition greater than 1
static int siMemLeakCorrectionPerPartition = 60 * 1024 * 1024;
AUTO_CMD_INT(siMemLeakCorrectionPerPartition, MemLeakCorrectionPerPartition);

#define UGC_EDIT_MEMORY_ALLOWANCE      120 * 1024 * 1024
#define STATIC_MAP_MEMORY_ALLOWANCE    80 * 1024 * 1024

static int siMaxPlayers = 0;
static int siFirstIncreaseAllowanceAmount = 0;

void gslIsDoneLoading(void);

AUTO_COMMAND_REMOTE;
void FloodLogServer_Remote(int iVal)
{
	siFloodLogServer = iVal;
}


AUTO_RUN;
void initServerStartupTimer(void)
{
	server_startup_timer = timerAlloc();
}

AUTO_CMD_FLOAT(maxFPS, serverMaxFPS) ACMD_CALLBACK(cmdServerMaxFPS);
void cmdServerMaxFPS(F32 fps){
	if(maxFPS == 0.f){
		maxFPS = gConf.maxServerFPS;
	}
	
	MINMAX1(maxFPS, 2.f, 100.f);
	
	inverseMaxFPS = 1.f / maxFPS;
}

void AccountSharedBankReceived_CB(enumResourceEventType eType, const char *pDictName, const char *pResourceName, Entity *pEnt, void *pUserData);

void gslWebRequestServer_Init()
{
	resDictRegisterEventCallback(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYSHAREDBANK), AccountSharedBankReceived_CB, NULL);
}

AUTO_STARTUP(GameServer) ASTRT_DEPS(GameServerNoData,
									Combat,
									EntityCostumes,
									Tray,
									Items,
									ItemCostumeClones,
									ItemGen,
									ItemVars,
									Critters,
									Missions,
									MissionSets,
									AS_GameProgression,
									Contacts,
									EncounterLayerInit,
									Encounters,
									AI,
									AICivilian,
									InventoryBags,
									Stores,
									PowerStores,
									Officers,
									PetContactLists,
									PetRestrictionsValidate,
									ContactAudioPhrases,
									ContactConfig,
									AlgoPet,
									AS_ControlSchemes,
									HUDOptionsDefaults,
									RegionRules,
									Guilds,
									LayerFSMInit,
									PlayerFSMStartup,
									AS_TextFilter,
									Emotes,
									Chat,
									PetCommandConfig,
									Species,
									PVP,
									PVPGame,
									Queues,
									AttribStatsPresets,
									PlayerStats,
									ItemTagInfo,
									ProjectGameServerConfig,
									ChoiceTable,
									DoorTransitionSequence,
									Interaction,
									GlobalExpressions,
									UnlockedAllegianceFlags,
									ShardVariables,
									DiaryDefs,
									AS_ActivityLogConfig,
									GameAccount,
									GameAccountNumericPurchase,
									RewardsServer,
									RequiredPowersAtCreation,
									WarpRestrictions,
									TimeControlConfig,
									WorldLibZone,
									PlayerDifficulty,
									GAMESPECIFIC,
									AS_GuildRecruitParam,
									MicroTransactions,
									Bulletins,
									LogoffConfig,
									UGCServer,
									LeaderboardServer,
									AS_GuildStats,
									AS_GuildThemes,
									GamePermissions,
									GslAuctionLoadConfig,
									MissionConfig,
									MissionWarpCostsServer,
									ItemAssignments,
									TeamTransferConfig,
									CharacterCombat,
									GlobalWorldVars,
									GameStringFormat,
									Activities,
                                    NumericConversion,
                                    CurrencyExchangeConfig,
									UGC,
                                    UGCTips,
									GameContentNodes,
									SuggestedContent,
									MovementManagerConfig,
									NotifySettings,
									MapNotificationsLoad,
									ItemUpgrade,
									AutoStart_AuctionBroker,
									SuperCritterPet,
                                    GroupProjects,
									UGCAchievements,
									AS_ExtraHeaderDataConfig
									GroupProjectLevelTreeDef
									);
void gslStartup(void)
{
	worldLibSetPlayableFunctions(gslPlayableCreate, gslPlayableDestroy);

	if (gGSLState.gbWebRequestServer)
	{
		gslWebRequestServer_Init();
	}
}

AUTO_STARTUP(GameServerNoData) ASTRT_DEPS(Schemas);
void gslStartupNoData(void)
{

}

// Search for (FIND_DATA_DIRS_TO_CACHE) to find a place 
// you can uncomment a line to verify if this is the correct list of folders to cache
//
// paths in comments are the lower level paths we can start scanning as soon as foldercache
// supports not rescanning subdirs
const char* serverPrecachePaths[] = {
	"ai",
	"animation_library",
	"bin",
	"character_library",
	"defs",
	"dyn",
	"environment",
	"fonts",
	"genesis",
	"maps",
	"materials",
	"messages",
	"powerart",
	"object_library",
	"server/bin/geobin",
	"server/objectdb/schemas",
	"shaders",
	"sound",
	"texts/English",
	"texture_library",
	"ui",
	"world",
	//"server/bin",
	//"dyn/fx",
	//"dyn/move",
	//"animation_library/skeletons",
	//"sound/gaelayers",
	//"world/physicalproperties",
	//"shaders/operations",
	//"environment/LodTemplates",
	//"texture_library/costumes",
	//"texture_library/core_costumes",
	//"texture_library/character_library",
};

void gslInit_BeginFrame(void)
{
	if (gServerLibState.dontLoadData)
	{
		wlSetLoadFlags( WL_NO_LOAD_DYNFX | WL_LOAD_DYNFX_NAMES_FOR_VERIFICATION | WL_NO_LOAD_DYNANIMATIONS | WL_NO_LOAD_MATERIALS | 
			WL_NO_LOAD_MODELS | WL_NO_LOAD_COSTUMES | WL_NO_LOAD_PROFILES);
	}
	else
	{
		if (isDevelopmentMode())
			wlSetLoadFlags( WL_NO_LOAD_DYNFX | WL_LOAD_DYNFX_NAMES_FOR_VERIFICATION );
		else
			wlSetLoadFlags( WL_NO_LOAD_DYNFX | WL_NO_LOAD_DYNANIMATIONS | WL_LOAD_DYNFX_NAMES_FOR_VERIFICATION );
	}
	wlSetIsServer(true);

	if (isDevelopmentMode())
	{
		// Set priority higher than other servers, but lower than the OS/Explorer/etc so
		//  we don't ruin the machine while we're loading/binning a map
		SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);
		tmSetGlobalThreadPriority(THREAD_PRIORITY_ABOVE_NORMAL); // Effective priority - 5
	}

	gslInitCombatCallbacks();

	gslLoadCmdConfig(NULL);

	gslSetConsoleTitle();

	if (!gServerLibState.dontLoadData && !gServerLibState.dontLoadExternData)
	{
		AutoStartup_SetTaskIsOn("GameServer", 1);
	}
	else
	{
		AutoStartup_SetTaskIsOn("GameServerNoData", 1);
	}
	
	if (!gServerLibState.dontLoadData && !gServerLibState.dontLoadExternData && !gServerLibState.writeSchemasAndExit)
	{
		// Search for (FIND_DATA_DIRS_TO_CACHE) to find places related to this caching 
		loadstart_printf("Precaching game data dirs...");
		fileCacheDirectories(serverPrecachePaths, ARRAY_SIZE(serverPrecachePaths));
		loadend_printf(" done.");
	}

	loadstart_printf("Running Auto Startup...");
	DoAutoStartup();
	loadend_printf(" done.");

	resFinishLoading();
	resWriteNamespaceManifests();

	if (gbMakeBinsAndExit)
	{
		NamedPathQueriesAndResults Queries = {0};
		const char *job_name = GetCurJobNameForJobManager();
		bool regenerated = false;

		if (gGSLState.gbServerBinner)
		{
			gslConnectToTransactionServer();
		}

		if (!gslUGC_ServerBinnerGenerateProject(gpcMakeBinsAndExitNamespace, &regenerated))
		{
			JobManagerUpdate_Complete(false, true, "Binning failed - Error regenerating project.");

			//switch to special GSL state where it will keep responding to remote commands and so forth but do nothing else
			GSM_SwitchToSibling(GSL_SPINNING, false);
			return;
		}

		worldGridMakeAllBins(gServerLibState.removeBins, gMakeMemdumpBetweenMaps, gValidateDepCheck);

		loadstart_printf("Binning login character info queries...");
		ParserLoadFiles(NULL, "server/LoginCharacterQueries.txt", "LoginCharacterQueries.bin", 0, parse_NamedPathQueriesAndResults, &Queries);
		loadend_printf(" done. (%d loaded)", eaSize(&Queries.eaQueries));

		zmapForceBinEncounterInfo();

		ugcResourceInfoPopulateDictionary();

		ugcPlatformDictionaryLoad();

		binWriteTouchedFileList();

		if (gGSLState.gbServerBinner)
		{
			char **paths = NULL;
			bool res = false;
			char *pcError = NULL;
			assertmsg(gpcMakeBinsAndExitNamespace, "No namespace given for server binner.");
			if (regenerated)
			{
				eaPush(&paths, strdupf("%s:/maps", gpcMakeBinsAndExitNamespace));
				eaPush(&paths, strdupf("%s:/ai", gpcMakeBinsAndExitNamespace));
				eaPush(&paths, strdupf("%s:/defs", gpcMakeBinsAndExitNamespace));
			}
			eaPush(&paths, strdupf("%s:/server/bin", gpcMakeBinsAndExitNamespace));
			res = ServerLibPatchUpload(paths, "ServerBinner", &pcError, "Serverbinner completed for job %s", GetCurJobNameForJobManager());
			eaDestroyEx(&paths, NULL);

			worldLoadEmptyMap();
			gslUGC_DeleteNamespaceDataFiles(gpcMakeBinsAndExitNamespace);
			if (res)
				JobManagerUpdate_Complete(true, true, "Binning complete");
			else
				JobManagerUpdate_Complete(false, true, "Binning failed - Error uploading results to ugcmaster: %s", pcError);

			//switch to special GSL state where it will keep responding to remote commands and so forth but do nothing else
			GSM_SwitchToSibling(GSL_SPINNING, false);
			return;
			
		}
	}

	if (gServerLibState.fixupAllMapsAndExit)
		worldGridFixupAllMaps(gServerLibState.fixupAllMapsDryRun);

	if (gServerLibState.calcSizesAllMapsAndExit)
		worldGridCalculateMapLayerSizes();

	if (gServerLibState.validateUGCProjects[0])
		gslUGCValidateProjects(gServerLibState.validateUGCProjects);

	if (gpcCalcDepsAndExit)
	{
		worldGridCalculateDependencies(gpcCalcDepsAndExit);
	}

	// Dump game data to flat files if needed
	gslDataDump();

	if (gServerLibState.writeSchemasAndExit || gbMakeBinsAndExit || gpcCalcDepsAndExit || gServerLibState.fixupAllMapsAndExit || gServerLibState.validateUGCProjects[0])
	{
		GSM_Quit("writeSchemasAndExit or makeBinsAndExit or fixupAllMapsAndExit");
		return;
	}

	if (resAnyCommandsPending())
	{
		resExecuteAllPendingCommands();
		GSM_Quit("resAnyCommandsPending()");
		return;
	}

	if (spFileForMissionWiki)
	{
		char *pCmdString = NULL;
		estrPrintf(&pCmdString, "Mission_WriteListToFile \"%s\" 0", spFileForMissionWiki);
		globCmdParse(pCmdString);
		GSM_Quit("WriteMissionWikiAndExit");
		estrDestroy(&pCmdString);
		return;
	}

	FolderCacheEnableCallbacks(fileIsUsingDevData());
	FolderCacheDoNotWarnOnOverruns(false);

	gslConnectToTransactionServer();

	if (UseResourceDB())
	{
		ProcessDeferredResDebRequests();
	}

	sharedMemoryProcessQueuedAlerts();

	GSM_SwitchToSibling(GSL_MAPMANAGERHANDSHAKE, false);
}

void gslMapManagerHandshake_BeginFrame(void)
{
	TransactionReturnVal hHandle = {0};
	U32 iNextKeepAliveTime = 0;
	bool bDoneHandshaking = false;
	
	//the OLD style description, will be phased out when new map transfer partition code will be turned on
	GameServerDescription *pDescriptionFromMapManager = NULL;

	//the NEW style description
	GameServerExe_Description *pGameServerExeDescriptionFromMapManager = NULL;



	enumTransactionOutcome eOutcome;

	//never run this function more than once, so might as well request a switch to LOADING
	GSM_SwitchToSibling(GSL_LOADING, false);

	loadstart_printf("About to try to handshake with map manager...");

	gGSLState.gameServerDescription.baseMapDescription.containerID = GetAppGlobalID();

	//web request servers always load "emptymap" and create a single partition
	if (gGSLState.gbWebRequestServer)
	{
		MapPartitionSummary summary = {0};
		gGSLState.gameServerDescription.baseMapDescription.mapDescription = allocAddString("emptymap");

		StructInit(parse_MapPartitionSummary, &summary);
		summary.eOwnerType = GLOBALTYPE_WEBREQUESTSERVER;
		summary.pMapVariables = StructAllocString("");
		partition_CreateAndInit(&summary, "Default WebRequestServer partition");
		StructDeInit(parse_MapPartitionSummary, &summary);
	}
	else if (gGSLState.gbGatewayServer)
	{
		// GatewayServers always load "emptymap" and create a single partition
		MapPartitionSummary summary = {0};
		gGSLState.gameServerDescription.baseMapDescription.mapDescription = allocAddString("emptymap");

		StructInit(parse_MapPartitionSummary, &summary);
		summary.eOwnerType = GLOBALTYPE_GATEWAYSERVER;
		summary.pMapVariables = StructAllocString("");
		partition_CreateAndInit(&summary, "Default GatewayServer partition");
		StructDeInit(parse_MapPartitionSummary, &summary);
	}
	else
	{
		const char *lastMapPublicName;
		char* map = NULL;

		estrStackCreate(&map);

		// try to get the last map from the registry
		if (lastMapPublicName = GamePrefGetString("LastMapPublicName", NULL))
		{
			estrCopy2(&map, lastMapPublicName);
			gGSLState.gameServerDescription.baseMapDescription.mapDescription = allocAddString(forwardSlashes(map));
		}

		//if we got a world name on the command line, skip everything and just use that world
		if (gGSLState.cmdLineMapPublicName[0])
		{
			estrCopy2(&map, gGSLState.cmdLineMapPublicName);
			gGSLState.gameServerDescription.baseMapDescription.mapDescription = allocAddString(forwardSlashes(map));
		}

		estrDestroy(&map);
	}

	if (!objLocalManager() || gGSLState.gbWebRequestServer || gGSLState.gbGatewayServer)
	{
		const char* pchMapDesc = gGSLState.gameServerDescription.baseMapDescription.mapDescription;
		gGSLState.eHowInitted = GSLHOWINITTED_NO_MAPMANAGER_MADE_UP_PORT;
			
		if (gGSLState.gbWebRequestServer)
		{
			loadend_printf("This is the web request server, so no handshaking");
		}
		else if (gGSLState.gbGatewayServer)
		{
			loadend_printf("This is the GatewayServer, so no handshaking");
		}
		else
		{
			loadend_printf("Failed earlier to connect to the trans server, so no obj local manager, so can't handshake");
		}
		gGSLState.gameServerDescription.bDescriptionIsActive = true;
	
		//for hand-started maps, pick an ID that it's very unlikely any other map is using. This will fail
		//if someone is hand-starting maps when the map manager has started over 100 at once. That seems unlikely.
		gGSLState.gameServerDescription.iMapPort = STARTING_GAMESERVER_PORT + 100 + gServerLibState.containerID;
		gGSLState.gameServerDescription.iMapPort %= BIT(16);
		gGSLState.gameServerDescription.baseMapDescription.eMapType = zmapInfoGetMapTypeByName(pchMapDesc);

		return;
	}


	//in production mode, we just keep requesting the handshake until it succeeds, alerting the first time. For dev mode, we
	//make up some fake values and use them
	while (!bDoneHandshaking)
	{
	
		RemoteCommand_GetGameServerDescriptionFromMapManager_NewMapTransferCode( &hHandle, GLOBALTYPE_MAPMANAGER, 0, gServerLibState.containerID);

		//while waiting for handshake, game servers send the controller a keepAlive so that it doesn't kill them

		iNextKeepAliveTime = timeSecondsSince2000_ForceRecalc() + GSL_HANDSHAKE_KEEPALIVE_DELAY;
		do
		{
			U32 iCurTime;
			serverLibOncePerFrame();
			commMonitor(commDefault());
			Sleep(10);

			if ((iCurTime = timeSecondsSince2000_ForceRecalc()) >= iNextKeepAliveTime)
			{
				iNextKeepAliveTime = iCurTime + GSL_HANDSHAKE_KEEPALIVE_DELAY;

				RemoteCommand_GameServerIsHandshaking(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), GetAppGlobalID());
			}
		} 
		while ((eOutcome = RemoteCommandCheck_GetGameServerDescriptionFromMapManager_NewMapTransferCode(&hHandle, &pGameServerExeDescriptionFromMapManager)) == TRANSACTION_OUTCOME_NONE);

		objReleaseTransactionReturn(&hHandle);

		if (eOutcome == TRANSACTION_OUTCOME_FAILURE)
		{
			if (isProductionMode())
			{
				static bool sbFirst = true;

				if (sbFirst)
				{
					sbFirst = false;
					WARNING_NETOPS_ALERT("GS_HANDSHAKE_FAILED", "GS handshake with mapmanager failed. This is probably because the mapmanager doesn't exist. If this happens in any other context, something is seriously wrong.");
				}
				loadupdate_printf("Transaction failed, will try again ");
				Sleep(1000);
			}
			else
			{
				const char* pchMapDesc = gGSLState.gameServerDescription.baseMapDescription.mapDescription;
				gGSLState.eHowInitted = GSLHOWINITTED_MAPMANAGER_HANDSHAKE_FAILED_MADE_UP_PORT;

				loadend_printf("handshaking failed... using defaults");
				//couldn't talk to map manager, for now just use default map
				gGSLState.gameServerDescription.bDescriptionIsActive = true;

				//for hand-started maps, pick a port and instance index that it's very unlikely any other map is using. This will fail
				//if someone is hand-starting maps when the map manager has started over 100 at once. That seems unlikely.
				gGSLState.gameServerDescription.iMapPort = STARTING_GAMESERVER_PORT + 100 + gServerLibState.containerID;
				gGSLState.gameServerDescription.iMapPort %= BIT(16);
				gGSLState.gameServerDescription.baseMapDescription.mapInstanceIndex = 100 + gServerLibState.containerID;
				gGSLState.gameServerDescription.baseMapDescription.eMapType = zmapInfoGetMapTypeByName(pchMapDesc);

				return;
			}
		}
		else
		{
			bDoneHandshaking = true;
		}
	}


	pDescriptionFromMapManager = StructCreate(parse_GameServerDescription);
	pDescriptionFromMapManager->bDescriptionIsActive = (pGameServerExeDescriptionFromMapManager->eServerType != GSTYPE_INVALID);
	pDescriptionFromMapManager->iMapPort = pGameServerExeDescriptionFromMapManager->iListeningPortNum;
	pDescriptionFromMapManager->baseMapDescription.containerID = GetAppGlobalID();
	pDescriptionFromMapManager->baseMapDescription.mapDescription = pGameServerExeDescriptionFromMapManager->pMapDescription;
	pDescriptionFromMapManager->baseMapDescription.eMapType = pGameServerExeDescriptionFromMapManager->eMapType;
	if (pGameServerExeDescriptionFromMapManager->eServerType == GSTYPE_UGC_PLAY)
	{
		pDescriptionFromMapManager->baseMapDescription.bUGC = true;
		pDescriptionFromMapManager->baseMapDescription.iUGCProjectID = pGameServerExeDescriptionFromMapManager->iUGCProjectID;
	}
	if (pGameServerExeDescriptionFromMapManager->eServerType == GSTYPE_UGC_EDIT)
	{
		pDescriptionFromMapManager->baseMapDescription.bUGC = true;
		pDescriptionFromMapManager->baseMapDescription.bUGCEdit = true;
		pDescriptionFromMapManager->baseMapDescription.iUGCProjectID = pGameServerExeDescriptionFromMapManager->iUGCProjectID;
	}
		
	pDescriptionFromMapManager->bAllowInstanceSwitchingBetweenOwnedMaps = pGameServerExeDescriptionFromMapManager->bAllowInstanceSwitchingBetweenOwnedMaps;

	StructDestroy(parse_GameServerExe_Description, pGameServerExeDescriptionFromMapManager);


	if (pDescriptionFromMapManager->bDescriptionIsActive == false)
	{
		const char* pchMapDesc = gGSLState.gameServerDescription.baseMapDescription.mapDescription;
		gGSLState.eHowInitted = GSLHOWINITTED_MAPMANAGER_HANDSHAKE_FAILED_MADE_UP_PORT;

//		gameserver should kill itself here, but for debugging purposes it just keeps running with default values
//		exit(-1);

		loadend_printf("handshaking failed... using defaults");
		//couldn't talk to map manager, for now just use default map
		gGSLState.gameServerDescription.bDescriptionIsActive = true;

		//for hand-started maps, pick an ID that it's very unlikely any other map is using. This will fail
		//if someone is hand-starting maps when the map manager has started over 100 at once. That seems unlikely.
		gGSLState.gameServerDescription.iMapPort = STARTING_GAMESERVER_PORT + 100 + gServerLibState.containerID;
		gGSLState.gameServerDescription.iMapPort %= BIT(16);
		gGSLState.gameServerDescription.baseMapDescription.mapInstanceIndex = 100 + gServerLibState.containerID;
		gGSLState.gameServerDescription.baseMapDescription.eMapType = zmapInfoGetMapTypeByName(pchMapDesc);

		return;

	}

	StructCopyFields(parse_GameServerDescription, pDescriptionFromMapManager, &gGSLState.gameServerDescription, 0, 0);
	StructDestroy(parse_GameServerDescription, pDescriptionFromMapManager);

	if (isProductionEditMode() && !worldGetZoneMapByPublicName(gGSLState.gameServerDescription.baseMapDescription.mapDescription))
	{
		// This map doesn't exist yet
		gGSLState.pCreateNewUGCMap = gGSLState.gameServerDescription.baseMapDescription.mapDescription;
		gGSLState.gameServerDescription.baseMapDescription.mapDescription = allocAddString("Emptymap");
	}

	gGSLState.gameServerDescription.bDescriptionIsActive = true;

	loadend_printf("handshaking succeeded");

	gGSLState.eHowInitted = GSLHOWINITTED_MAPMANAGER_HANDSHAKE_SUCCEEDED;

}

void gslLoading_BeginFrame(void)
{
	const char *public_name;

	switch (gGSLState.gameServerDescription.baseMapDescription.eMapType)
	{
	default:
		loadstart_printf("Loading map \"%s\"...", gGSLState.gameServerDescription.baseMapDescription.mapDescription);
		if (!gslLoadZoneMapByName(gGSLState.gameServerDescription.baseMapDescription.mapDescription))
		{
			// In production we want to assert here... in dev mode, just issue an error and go to EmptyMap
            if ( isDevelopmentMode() )
            {
                Errorf("Unable to load map because ZoneMap data was not found for the requested map: %s", gGSLState.gameServerDescription.baseMapDescription.mapDescription);
            }
            else
            {
                // Force a crash here in production mode.
                assertmsgf(false, "Unable to load map because ZoneMap data was not found for the requested map: %s", gGSLState.gameServerDescription.baseMapDescription.mapDescription);
            }
			worldLoadEmptyMap();
		}
		loadend_printf(" done.");

		if (public_name = zmapInfoGetPublicName(NULL))
			gGSLState.gameServerDescription.baseMapDescription.mapDescription = allocAddString(public_name);
		else
			gGSLState.gameServerDescription.baseMapDescription.mapDescription = allocAddString("");
	}

	if (!gServerLibState.dontLoadData && !gServerLibState.dontLoadExternData)
	{
		gslExternLoadPostMapLoad();
	}	

	gslExternInit();
	ServerChat_InitializeZoneChannel();
	
	RefSystem_ResizeAllStashTables();

	GSM_SwitchToSibling(GSL_NETINIT, false);
}


#define GSLNETINIT_TIMEOUT (120.0f)
#define DELAY_BETWEEN_PORT_REQUESTS (10)

static U32 *spPortsToTry = NULL;

void gslNetInit_Enter(void)
{
	int i;

	//if we have a port set in gGSLState.gameServerDescription.iMapPort, make sure it ends up last in our list (as we
	//will use ea32Pop)

	for (i = STARTING_GAMESERVER_PORT; i <= MAX_GAMESERVER_PORT; i++)
	{
		if (i != gGSLState.gameServerDescription.iMapPort)
		{
			ea32Push(&spPortsToTry, i);
		}
	}

	ea32Randomize(&spPortsToTry);

	if (gGSLState.gameServerDescription.iMapPort >= STARTING_GAMESERVER_PORT && 
		gGSLState.gameServerDescription.iMapPort <= MAX_GAMESERVER_PORT)
	{
		ea32Push(&spPortsToTry, gGSLState.gameServerDescription.iMapPort);
	}
}

void gslNetInit_BeginFrame(void)
{
	commSetMinReceiveTimeoutMS(commDefault(), 0);

	if (gGSLState.gbGatewayServer)
	{
		gslGatewayServer_Init();

		GSM_SwitchToSibling(GSL_RUNNING, false);

		utilitiesLibOncePerFrame(1, 1);
		serverLibOncePerFrame();
		commMonitor(commDefault());
	}
	else
	{
		printf("Entering NetInit. We have %d available port numbers, hopefully we can listen on one of them\n", ea32Size(&spPortsToTry));

		if (ea32Size(&spPortsToTry))
		{
			NetListen* local_listen = NULL;
			NetListen* public_listen = NULL;
			bool bFailed = false;

			gGSLState.gameServerDescription.iMapPort = ea32Pop(&spPortsToTry);

			loadstart_printf("Going to try %u\n", gGSLState.gameServerDescription.iMapPort);

			commListenBoth(	commDefault(),
							LINKTYPE_TOUNTRUSTED_20MEG,
							LINK_MEDIUM_LISTEN|LINK_FORCE_FLUSH,
							gGSLState.gameServerDescription.iMapPort,
							gslHandleInput,
							gslConnectCallback,
							gslDisconnectCallback,
							sizeof(ClientLink),
							&local_listen,
							&public_listen);
			
			if(areEditorsAllowed())
			{
				commSetSendTimeout(commDefault(), 60.f);
			}

			//check for failures... different check depending on if our local and public IP are different
			if (getHostLocalIp() != getHostPublicIp())
			{
				if (!local_listen || !public_listen)
				{
					bFailed = true;
				}
			}
			else
			{
				if (!local_listen)
				{
					bFailed = true;
				}
			}

			if (bFailed)
			{
				loadend_printf("Could not listen on that port, will try another one\n");
				if(local_listen || public_listen)
				{
					AssertOrAlert("LEAKING_LISTEN", "Somehow we have a local_listen or public_listen after CommListenBoth() failed, no way to destroy it");
				}
			}
			else
			{
				loadend_printf("Succeeded");
				if(local_listen)
					eaPush(&gGSLState.clients.netListens, local_listen);
				if(public_listen)
					eaPush(&gGSLState.clients.netListens, public_listen);

				//always make sure to tell map manager what port we finally ended up on, in case there was some timing
				//overlap where it ended up with two GameServerPorDidnt Work requests
				RemoteCommand_GameServerPortWorked(GLOBALTYPE_MAPMANAGER, 0, GetAppGlobalID(), getHostLocalIp(), gGSLState.gameServerDescription.iMapPort);

				GSM_SwitchToSibling(GSL_RUNNING, false);

				ea32Destroy(&spPortsToTry);
			}

			utilitiesLibOncePerFrame(1, 1);
			serverLibOncePerFrame();
			commMonitor(commDefault());

		}
		else
		{
			assertmsgf(0, "Tried to listen on every single potential GS port, failed on every single one."); 
		}
	}
}

void gslCheckForSubscribedContainersCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	if (gslStateIsWaitingForSomeSubscribedContainers())
	{
		AssertOrAlert("NO_GSL_SUB_CONTAINERS", "Game server %u has been waiting for 60 seconds and is still waiting for subscribed containers. UGC proj: %u %s Virtual shard: %u %s",
			gServerLibState.containerID,
			gGSLState.gameServerDescription.baseMapDescription.iUGCProjectID, GET_REF(gGSLState.hUGCProjectFromSubscription) ? "(PRESENT)" : "ABSENT",
			gGSLState.gameServerDescription.baseMapDescription.iVirtualShardID, GET_REF(gGSLState.hVirtualShardFromSubscription) ? "(PRESENT)" : "ABSENT");
	}
}


static void gslUGCProjectResCB(enumResourceEventType eType, const char *pDictName, const char *pResourceName, UGCProject *pProject, void *pUserData)
{
	switch (eType)
	{
	case RESEVENT_RESOURCE_ADDED:
		if (!gslStateIsWaitingForSomeSubscribedContainers() && !gGSLState.bInformedMapManagerMapIsDoneLoading)
		{
			gslIsDoneLoading();
		}
	xcase RESEVENT_RESOURCE_MODIFIED:
		//refresh project published state
		{
			EntityIterator *pIter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
			Entity *pEnt;
			while(pEnt = EntityIteratorGetNext(pIter))
			{
				QueryUGCProjectStatus(pEnt);
			}
			EntityIteratorRelease(pIter);
			break;
		}
	}
}

static void gslVirtualShardResCB(enumResourceEventType eType, const char *pDictName, const char *pResourceName, VirtualShard *pShard_in, void *pUserData)
{
	VirtualShard *pMyShard;

	switch (eType)
	{
	case RESEVENT_RESOURCE_ADDED:
		if (!gslStateIsWaitingForSomeSubscribedContainers() && !gGSLState.bInformedMapManagerMapIsDoneLoading)
		{
			gslIsDoneLoading();
		}

		pMyShard = GET_REF(gGSLState.hVirtualShardFromSubscription);

		if (pMyShard && pMyShard->id == pShard_in->id)
		{
			// TODO_PARTITION: some kind of shard state inherited by each partition on creation
			// mapState_SetPVPQueuesDisabled(pMyShard->bNoPVPQueues);
			// gslAuctions_SetAuctionsDisabled(pMyShard->bNoAuctions);
		}
		break;
	}
}

void Alloc100KCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	void *pBuf = malloc(100*1024);
}


void OVERRIDE_LATELINK_GetMemSizeForMemLeakTracking(size_t *pOutSize, size_t *pOutEffectiveSize, char **ppOutPrettySizeString)
{
	size_t iRaw = getProcessPageFileUsage();
	int iNumPlayers;
	char *pTempRaw = NULL;
	char *pTempCorrected = NULL;
	S64 iEffectiveSize64;
	int iMaxPartitions;

	estrStackCreate(&pTempRaw);
	estrStackCreate(&pTempCorrected);

	// Check number of players
	if(GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER)
	{
		iNumPlayers = gslGatewayServer_GetSessionCount();
	}
	else
	{
		iNumPlayers = objCountOwnedContainersWithType(GLOBALTYPE_ENTITYPLAYER);
	}

	if (iNumPlayers > siMaxPlayers)
	{
		siMaxPlayers = iNumPlayers;
	}

	// Check number of partitions
	// This is actually 1 less than the max so that the first partition doesn't get counted
	iMaxPartitions = partition_GetMaxPartitionsSinceServerStart() - 1;
	if (iMaxPartitions < 0) 
	{
		iMaxPartitions = 0;
	}

	// Calculate effective size
	iEffectiveSize64 = ((S64)iRaw) - siMaxPlayers * siMemLeakCorrectionPerPlayer - (iMaxPartitions * siMemLeakCorrectionPerPartition);
	if (iEffectiveSize64 < 0)
	{
		iEffectiveSize64 = 0;
	}

	*pOutSize = iRaw;
	*pOutEffectiveSize = iEffectiveSize64;
	
	estrMakePrettyBytesString(&pTempRaw, iRaw);
	estrMakePrettyBytesString(&pTempCorrected, *pOutEffectiveSize);

	estrPrintf(ppOutPrettySizeString, "%s (Effective %s with %d players)", pTempRaw, pTempCorrected, iNumPlayers);

	estrDestroy(&pTempCorrected);
	estrDestroy(&pTempRaw);
}

void gslIsDoneLoading(void)
{
	gGSLState.bInformedMapManagerMapIsDoneLoading = true;
	RemoteCommand_MapIsDoneLoading(GLOBALTYPE_MAPMANAGER, 0, gServerLibState.containerID);

	if( isProductionEditMode() ) {
		gslUGC_ServerIsDoneLoading();
	}
}

void gslRunning_Enter(void)
{
	printf("\nServer ready (%1.2fs load time).\n", timerElapsed(server_startup_timer));
	setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'S', 0x00ff00);
	servLog(LOG_GSL, "ServerLoadTime", "LoadTime %1.2f", timerElapsed(server_startup_timer));
	timerFree(server_startup_timer);
	server_startup_timer = 0;

	if(chimeOnReady)
		printf("\a");

	frameLockedTimerCreate(&gGSLState.flt, 3000, 3000 / 60);
	gGSLState.frameTimerManager = coarseTimerCreateManager(true);
	
	SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);
	if(gGSLState.threadPriorityOverride){
		MINMAX1(gGSLState.threadPriorityOverride,
				THREAD_PRIORITY_IDLE,
				THREAD_PRIORITY_TIME_CRITICAL);

		tmSetGlobalThreadPriority(gGSLState.threadPriorityOverride);
	}
	else if(isProductionMode()){
		tmSetGlobalThreadPriority(THREAD_PRIORITY_NORMAL); // Effective priority - 4
	}else{
		tmSetGlobalThreadPriority(THREAD_PRIORITY_ABOVE_NORMAL); // Effective priority - 5
	}

	if (!gGSLState.gbWebRequestServer && !gGSLState.gbGatewayServer)
	{
		TimedCallback_Add(gslPeriodicUpdate, NULL, 5.0f);
		TimedCallback_Add(gslGameSpecificPeriodicUpdate, NULL, 10.0f);
		if (isDevelopmentMode())
		{
			TimedCallback_Add(resourcePeriodicUpdate, NULL, 5.0f);
		}
	}
	if (gGSLState.gbGatewayServer)
	{
		TimedCallback_Add(gslGatewayServer_PeriodicUpdate, NULL, 5.0f);
	}


	//this isn't essential, because it gets initted when needed, but this allows server monitoring before an
	//ATR has actually happened
	ATR_DoLateInitialization();

	//UGC game servers:
	if (gGSLState.gameServerDescription.baseMapDescription.bUGCEdit && gGSLState.gameServerDescription.baseMapDescription.iUGCProjectID)
	{
		char tempStr[16];
		sprintf(tempStr, "%u", gGSLState.gameServerDescription.baseMapDescription.iUGCProjectID);
		RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(GLOBALTYPE_UGCPROJECT), false, parse_UGCProject, false, false, NULL);
		resDictRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_UGCPROJECT), RES_DICT_KEEP_NONE, false, objCopyDictHandleRequest);
		resDictProvideMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_UGCPROJECT));
		SET_HANDLE_FROM_REFDATA(GlobalTypeToCopyDictionaryName(GLOBALTYPE_UGCPROJECT), tempStr, gGSLState.hUGCProjectFromSubscription);
		resDictRegisterEventCallback(GlobalTypeToCopyDictionaryName(GLOBALTYPE_UGCPROJECT),gslUGCProjectResCB,NULL);

	}

	if (gGSLState.gameServerDescription.baseMapDescription.iVirtualShardID)
	{
		char tempStr[16];
		sprintf(tempStr, "%u", gGSLState.gameServerDescription.baseMapDescription.iVirtualShardID);
		RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(GLOBALTYPE_VIRTUALSHARD), false, parse_VirtualShard, false, false, NULL);
		resDictRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_VIRTUALSHARD), RES_DICT_KEEP_NONE, false, objCopyDictHandleRequest);
		resDictProvideMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_VIRTUALSHARD));
		SET_HANDLE_FROM_REFDATA(GlobalTypeToCopyDictionaryName(GLOBALTYPE_VIRTUALSHARD), tempStr, gGSLState.hVirtualShardFromSubscription);
		resDictRegisterEventCallback(GlobalTypeToCopyDictionaryName(GLOBALTYPE_VIRTUALSHARD),gslVirtualShardResCB,NULL);
	}

	if (gslStateIsWaitingForSomeSubscribedContainers())
	{
		TimedCallback_Run(gslCheckForSubscribedContainersCB, NULL, 60.0f);
	}
	else
	{
		gslIsDoneLoading();
	}

	//if this option is true, obviously want to play around with the mem leak tracking for debug purposes, so just start immediately
	if (gbTrackMemLeaksInDevMode)
	{
		gslStateBeginMemLeakTracking();
	}
}

void gslStateBeginMemLeakTracking(void)
{
	ATOMIC_INIT_BEGIN;

	if ((isProductionMode() || gbTrackMemLeaksInDevMode) && siMemLeakTrackingIncreaseAmount && siTimeBeforeStartingMemLeakTracking &&  siMemLeakTrackingInterval)
	{
		ZoneMapType eType = zmapInfoGetMapType(NULL);

		if (isProductionEditMode()) 
		{
			siFirstIncreaseAllowanceAmount += UGC_EDIT_MEMORY_ALLOWANCE;
		}
		if ((eType == ZMTYPE_STATIC) || (eType == ZMTYPE_SHARED) || (eType == ZMTYPE_QUEUED_PVE))
		{
			siFirstIncreaseAllowanceAmount += STATIC_MAP_MEMORY_ALLOWANCE;
		}
		BeginMemLeakTracking(siTimeBeforeStartingMemLeakTracking, siMemLeakTrackingInterval, siMemLeakTrackingIncreaseAmount, siFirstIncreaseAllowanceAmount);
	}

	ATOMIC_INIT_END;
}

bool gslStateIsWaitingForSomeSubscribedContainers(void)
{
	if (IS_HANDLE_ACTIVE(gGSLState.hUGCProjectFromSubscription) && GET_REF(gGSLState.hUGCProjectFromSubscription) == NULL)
	{
		return true;
	}
	if (IS_HANDLE_ACTIVE(gGSLState.hVirtualShardFromSubscription) && GET_REF(gGSLState.hVirtualShardFromSubscription) == NULL)
	{
		return true;
	}

	return false;
}

void gslCountASlowFrame(void)
{
	static SimpleEventCounter *pCounter = NULL;
	U32 iCurTime = timeSecondsSince2000();
	char* pCounterData = NULL;
	char* pAlertStr = NULL;
	char* pCoarseSlowestItemStr = NULL;

	// Don't alert on UGC edit maps
	if  (gGSLState.gameServerDescription.baseMapDescription.bUGCEdit) {
		return;
	}

	// Set map start time if not set yet
	if (!gGSLState.uiStartTime) {
		gGSLState.uiStartTime = iCurTime;
	}
	
	// Don't alert during first 30 seconds of the map
	if (iCurTime - gGSLState.uiStartTime <= 30) {
		return;
	}

	estrStackCreate(&pCounterData);
	estrStackCreate(&pAlertStr);
	estrStackCreate(&pCoarseSlowestItemStr);

	gGSLState.iLaggyFrames++;

	if (!pCounter)
	{
		pCounter = SimpleEventCounter_Create(siNumSlowFramesToAlertOn, siSlowFramesWithinWindowToAlert, siSlowFrameAlertThrottle);
	}

	coarseTimerPrune(gGSLState.frameTimerManager, 0);
	coarseTimerPrint(gGSLState.frameTimerManager, &pCounterData, &pCoarseSlowestItemStr);

	if (SimpleEventCounter_ItHappenedWithInfo(pCounter, iCurTime, pCounterData, &pAlertStr))
	{
		char *pTempWindow = NULL;
		char *pTempTotal = NULL;
		char *pAlertKeyToUse = NULL;

		estrStackCreate(&pTempWindow);
		estrStackCreate(&pTempTotal);

		timeSecondsDurationToPrettyEString(siSlowFramesWithinWindowToAlert, &pTempWindow);
		timeSecondsDurationToPrettyEString(iCurTime - gGSLState.uiStartTime, &pTempTotal);

		estrMakeAllAlphaNumAndUnderscores(&pCoarseSlowestItemStr);
		estrTrimLeadingAndTrailingUnderscores(&pCoarseSlowestItemStr);
		string_toupper(pCoarseSlowestItemStr);
		estrPrintf(&pAlertKeyToUse, "LAGGY_GS_%s", pCoarseSlowestItemStr);
		
		TriggerAlertf("LAGGY_GS", ALERTLEVEL_WARNING, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0, 
			"%s: GS had %d frames slower than %f seconds in last %s.\nMap has %d partitions with %d players and has been running for %s.\nProfile data:\n%s", 
			pAlertKeyToUse, siNumSlowFramesToAlertOn, sfSlowFrameSeconds, pTempWindow, 
			partition_GetActualActivePartitionCount(), partition_GetTotalPlayerCount(), pTempTotal,
			pAlertStr);

		estrDestroy(&pTempWindow);
		estrDestroy(&pTempTotal);
		estrDestroy(&pAlertKeyToUse);
	}
	estrDestroy(&pAlertStr);
	estrDestroy(&pCounterData);
	estrDestroy(&pCoarseSlowestItemStr);
}

static void gslRunning_ReadTimer(int firstFrame)
{
	F32 frameTime;

	gGSLState.secondsElapsed.sim.scale = wlTimeGetStepScale();
	
	frameLockedTimerStartNewFrame(	gGSLState.flt,
									gGSLState.secondsElapsed.sim.scale);
	
	frameLockedTimerGetPrevTimes(	gGSLState.flt,
									&gGSLState.secondsElapsed.sim.prev,
									NULL,
									NULL);
									
	frameLockedTimerGetCurTimes(gGSLState.flt,
								&gGSLState.secondsElapsed.sim.cur,
								NULL,
								NULL);
	
	frameLockedTimerGetPrevTimesReal(	gGSLState.flt,
										&gGSLState.secondsElapsed.reality.prev);

	frameLockedTimerGetCurTimesReal(gGSLState.flt,
									&gGSLState.secondsElapsed.reality.cur);

	if (!firstFrame)
		gslFrameTimerStopInstance("frame");

	frameTime = gGSLState.secondsElapsed.reality.cur - gGSLState.secondsElapsed.reality.prev;

	if (sfSlowFrameSeconds && gGSLState.secondsElapsed.reality.prev && frameTime > sfSlowFrameSeconds && !gGSLState.bCurrentlyInUGCPreviewMode && partition_HasPlayersOnAnyPartition())
	{
		gslCountASlowFrame();
	}

	coarseTimerClear(gGSLState.frameTimerManager);

	gslFrameTimerAddInstance("frame");
}

static void gslRunning_SendUpdates(void)
{
	gGSLState.entSend.seconds.acc += gGSLState.secondsElapsed.reality.cur;

	MINMAX1(gGSLState.entSend.seconds.interval, 0.f, 2.f);
	
	if (gGSLState.entSend.seconds.acc >= gGSLState.entSend.seconds.interval)
	{
		PerfInfoGuard* piGuard;

		PERFINFO_AUTO_START_FUNC_GUARD(&piGuard);
			gslSendGeneralUpdates();
		PERFINFO_AUTO_STOP_GUARD(&piGuard);
		
		gGSLState.entSend.seconds.acc = 0;
	}

	gslEditorOncePerFrame();
}

static void gslRunning_LibsOncePerFrame(void)
{
	PERFINFO_AUTO_START_FUNC();
	
	gslFrameTimerAddInstance("UltilitiesLibOncePerFrame");
	utilitiesLibOncePerFrame(	gGSLState.secondsElapsed.sim.cur,
								gGSLState.secondsElapsed.sim.scale);
	gslFrameTimerStopInstance("UltilitiesLibOncePerFrame");
								
	gslFrameTimerAddInstance("worldLibOncePerFrame");
	worldLibOncePerFrame(gGSLState.secondsElapsed.sim.prev);
	gslFrameTimerStopInstance("worldLibOncePerFrame");

	gslFrameTimerAddInstance("serverLibOncePerFrame");
	serverLibOncePerFrame();
	gslFrameTimerStopInstance("serverLibOncePerFrame");

	gslFrameTimerAddInstance("beaconDebugOncePerFrame");
	beaconDebugOncePerFrame(gGSLState.secondsElapsed.sim.prev);
	gslFrameTimerStopInstance("beaconDebugOncePerFrame");

	gslFrameTimerAddInstance("aiLibOncePerFrame");
	aiLibOncePerFrame();
	gslFrameTimerStopInstance("aiLibOncePerFrame");

	gslFrameTimerAddInstance("sndLibServerOncePerFrame");
	sndLibServerOncePerFrame(gGSLState.secondsElapsed.sim.prev);
	gslFrameTimerStopInstance("sndLibServerOncePerFrame");

	gslFrameTimerAddInstance("ProccessTicket OncePerFrame");
	ProcessTicketSendQueue();
	ProcessTicketRequestQueue();
	gslFrameTimerStopInstance("ProccessTicket OncePerFrame");

	gslFrameTimerAddInstance("GameServerHttp_OncePerFrame");
	GameServerHttp_OncePerFrame();
	gslFrameTimerStopInstance("GameServerHttp_OncePerFrame");

	gslFrameTimerAddInstance("EventTracker OncePerFrame");
	eventTracker_OncePerFrameUpdateAllPartitions();
	gslFrameTimerStopInstance("EventTracker OncePerFrame");

	if (gGSLState.gbWebRequestServer)
	{
		gslFrameTimerAddInstance("gslWebRequestTick");
		gslWebRequestTick();
		gslFrameTimerStopInstance("gslWebRequestTick");
	}
	
	if(gGSLState.gbGatewayServer)
	{
		gslFrameTimerAddInstance("gslGatewayServerTick");
		gslGatewayServer_OncePerFrame();
		gslFrameTimerStopInstance("gslGatewayServerTick");
	}

	PERFINFO_AUTO_STOP();
}

static void gslRunning_UpdateFPS(void)
{
	static F32 timeSinceLastUpdate;
	static int framesSinceLastUpdate;
	
	timeSinceLastUpdate += gGSLState.secondsElapsed.reality.cur;
	framesSinceLastUpdate++;
	if (timeSinceLastUpdate >= 1.0)
	{
		gGSLState.server_fps = framesSinceLastUpdate / timeSinceLastUpdate;
		gslSetConsoleTitle();
		framesSinceLastUpdate = 0;
		timeSinceLastUpdate = 0.0;
	}
}

void gslSpinning_BeginFrame(void)
{
	PERFINFO_AUTO_START("commMonitor(commDefault())", 1);
		commMonitor(commDefault());
	PERFINFO_AUTO_STOP();

	gslRunning_LibsOncePerFrame();
}

SIMPLE_CPU_DECLARE_TICKS(s_TicksStart);

void gslRunning_BeginFrame(void)
{
	static int firstFrame = true;
	if(!firstFrame)
		gslFrameTimerStopInstance("outside of gslRunning_BeginFrame");

	SIMPLE_CPU_TICKS(s_TicksStart);

	gslRunning_ReadTimer(firstFrame);

	gslFrameTimerAddInstance("mmCreateWorldCollIntegration");
	mmCreateWorldCollIntegration();
	gslFrameTimerStopInstance("mmCreateWorldCollIntegration");
	
	gslFrameTimerAddInstance("commMonitor");
	PERFINFO_AUTO_START("commMonitor(commDefault())", 1);
		commMonitor(commDefault());
	PERFINFO_AUTO_STOP();
	gslFrameTimerStopInstance("commMonitor");

	gslFrameTimerAddInstance("wcSwapSimulation");
	wcSwapSimulation(gGSLState.flt);
	gslFrameTimerStopInstance("wcSwapSimulation");

	gslFrameTimerAddInstance("gslRunning_SendUpdates");
	gslRunning_SendUpdates();
	gslFrameTimerStopInstance("gslRunning_SendUpdates");
	
	//commMonitor(commDefault());

	gslFrameTimerAddInstance("FolderCacheDoCallbacks");
	FolderCacheDoCallbacks();
	gslFrameTimerStopInstance("FolderCacheDoCallbacks");

	// This is frame timer'd internally
	gslRunning_LibsOncePerFrame();

	gslFrameTimerAddInstance("Misc OncePerFrame");
	gslRunning_UpdateFPS();

	gslMapTransferTick(gGSLState.secondsElapsed.sim.prev);
	gslFrameTimerStopInstance("Misc OncePerFrame");

	// This is frame timer'd internally
	gslRunning_BeginFrame_GameSystems();

	gslFrameTimerAddInstance("resCommands");
	if (resAnyCommandsPending())
	{
		resExecuteAllPendingCommands();		
	}
	gslFrameTimerStopInstance("resCommands");

	firstFrame = false;

	gslFrameTimerAddInstance("outside of gslRunning_BeginFrame");
}





void gslRunning_EndFrame(void)
{
	SIMPLE_CPU_DECLARE_TICKS(ticksEnd);

	gslEntityBackupTick();

	// Must clock the cycles used before sleeping
	SIMPLE_CPU_TICKS(ticksEnd);
	SIMPLE_CPU_THREAD_CLOCK(SIMPLE_CPU_USAGE_THREAD_GAMESERVER_MAIN, s_TicksStart, ticksEnd);

	gslSleepForRestOfFrame(gGSLState.flt, inverseMaxFPS);

	if (siFloodLogServer)
	{
		int i;

		for (i=0; i < siFloodLogServer; i++)
		{
			log_printf(randInt(LOG_LAST), "Here is a happy lovely integer: %d. I hope you enjoy it. Please continue to patronize this fine service.", i);
		}
	}

	gslSimpleCpu_CaptureFrames();
}

void gslRunning_Leave(void)
{
	worldLibShutdown();
	frameLockedTimerDestroy(&gGSLState.flt);
	coarseTimerDestroyManager(gGSLState.frameTimerManager);
	gGSLState.frameTimerManager = NULL;
}



AUTO_RUN;
void GSL_InitStates(void)
{
	GSM_AddGlobalState(GSL_INIT);
	GSM_AddGlobalStateCallbacks(GSL_INIT, NULL, gslInit_BeginFrame, NULL, NULL);

	GSM_AddGlobalState(GSL_MAPMANAGERHANDSHAKE);
	GSM_AddGlobalStateCallbacks(GSL_MAPMANAGERHANDSHAKE, NULL, gslMapManagerHandshake_BeginFrame, NULL, NULL);

	GSM_AddGlobalState(GSL_RUNNING);
	GSM_AddGlobalStateCallbacks(GSL_RUNNING, gslRunning_Enter, gslRunning_BeginFrame, gslRunning_EndFrame, gslRunning_Leave);

	GSM_AddGlobalState(GSL_LOADING);
	GSM_AddGlobalStateCallbacks(GSL_LOADING, NULL, gslLoading_BeginFrame, NULL, NULL);

	GSM_AddGlobalState(GSL_NETINIT);
	GSM_AddGlobalStateCallbacks(GSL_NETINIT, gslNetInit_Enter, gslNetInit_BeginFrame, NULL, NULL);

	GSM_AddGlobalState(GSL_SPINNING);
	GSM_AddGlobalStateCallbacks(GSL_SPINNING, NULL, gslSpinning_BeginFrame, NULL, NULL);

	slSetGSMReportsStateToController();

	if(gConf.maxServerFPS)
	{
		maxFPS = gConf.maxServerFPS;
		inverseMaxFPS = 1.f / gConf.maxServerFPS;
	}

}

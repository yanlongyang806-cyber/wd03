/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "AppServerLib.h"

#include "UtilitiesLib.h"
#include "crypt.h"
#include "sysutil.h"
#include "logging.h"
#include "FolderCache.h"
#include "BitStream.h"
#include <conio.h>
#include "version/AppRegCache.h"
#include "Entity.h"
#include "Entity_h_ast.h"
#include "ServerLib.h"
#include "WorldGrid.h"
#include "sock.h"
#include "StringCache.h"
#include "ScratchStack.h"
#include "timing_profiler_interface.h"
#include "TimedCallback.h"
#include "AutoTransDefs.h"
#include "fileutil.h"
#include "aslMapManager.h"

//For acquiring containers.
#include "AutoGen/ObjectDB_autogen_remotefuncs.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"

AppServerDef **gAppServerList;
AppServerDef *gAppServer;

// Change these if getting containers starts crashing the ObjectDB or AppServer again - VAS 06/09/09 & 01/28/10
static int aslContainerAcquireBatchSize = 256;
AUTO_CMD_INT(aslContainerAcquireBatchSize, ContainerAcquireBatchSize);

static int aslContainerAcquireMaxRequests = 2048;
AUTO_CMD_INT(aslContainerAcquireMaxRequests, ContainerAcquireMaxRequests);

// For servers after the first, acquire at a possibly different rate
static int aslContainerReacquireBatchSize = 256;
AUTO_CMD_INT(aslContainerReacquireBatchSize, ContainerReacquireBatchSize);

static int aslContainerReacquireMaxRequests = 256;
AUTO_CMD_INT(aslContainerReacquireMaxRequests, ContainerReacquireMaxRequests);

// Number of containers in the process of being transferred.
static U32 uTransferringContainers = 0;

static int server_startup_timer;

//////////////////////////////////////////////////////////////////////////

AUTO_RUN;
void initServerStartupTimer(void)
{
	server_startup_timer = timerAlloc();
}

AUTO_RUN_SECOND;
int InitializeStringCache(void)
{
	
	ScratchStackSetThreadSize(1024*1024*2);
	stringCacheSetInitialSize(1024*(1024+768));
	g_ccase_string_cache = true;
	//AppServers cannot share memory with gameservers, because parsetables are located in different places
	//Disable shared string cache, which will disable textparser shared memory, until gimme performance improves
	//stringCacheInitializeShared("SharedStringCache", 1024*1024*30, 250*1024);
	return 1;
}


AppServerDef *FindAppServer(GlobalType appType)
{
	int i;
	for (i = eaSize(&gAppServerList) - 1; i>= 0; i--)
	{
		if (appType == gAppServerList[i]->appType)
		{
			return gAppServerList[i];
		}
	}
	return NULL;
}

int  aslInitApp(void)
{
	GlobalType appType = GetAppGlobalType();
	AppServerDef *def = FindAppServer(appType);
	assertmsgf(def,"Invalid AppServer %s told to run!",GlobalTypeToName(appType));
	assertmsgf(!gAppServer,"Can't start new Appserver, %s is already running!",GlobalTypeToName(gAppServer->appType));
	assertmsgf(def->appInit,"AppServer %s has no init function!",GlobalTypeToName(appType));

	def->appRunning = true;
	gAppServer = def;
	
	
	return 1;
}

// Search for (FIND_DATA_DIRS_TO_CACHE) in to find a place 
// you can uncomment a line to verify if this is the correct list of folders to cache
const char* loginAuctionServerPrecachePaths[] = {
	"ai",
	"defs",
	"genesis",
	"maps",
	"messages",
	"object_library",
	"powerart",
	"server/objectdb/schemas",
	"ui"
};

// Search for (FIND_DATA_DIRS_TO_CACHE) in to find a place 
// you can uncomment a line to verify if this is the correct list of folders to cache
const char* mapManagerPrecachePaths[] = {
	"defs",
	"maps",
	"ns",
	"server/objectdb/schemas"
};

// Search for (FIND_DATA_DIRS_TO_CACHE) in to find a place 
// you can uncomment a line to verify if this is the correct list of folders to cache
const char* teamGuildQueueServerPrecachePaths[] = {
	"defs",
	"server/objectdb/schemas"
};

// Search for (FIND_HOGGS_TO_IGNORE) to find related code
// Only "basic.hogg", "data.hogg", "defs.hogg", "maps.hogg", "extended.hogg", "object_library.hogg"
const char* loginAndActionServerHoggIgnores[] = {
	"bin.hogg",
	"character_library.hogg",
	"client.hogg",
	"ns.hogg",
	"server.hogg",
	"sound.hogg",
	"texts.hogg",
	"texture_library.hogg"
};

// Search for (FIND_HOGGS_TO_IGNORE) to find related code
// Only "data.hogg", "maps.hogg", "ns.hogg"
const char* mapManagerHoggIgnores[] = {
	"bin.hogg",
	"basic.hogg",
	"character_library.hogg",
	"client.hogg",
	"defs.hogg",
	"extended.hogg",
	"object_library.hogg",
	"server.hogg",
	"sound.hogg",
	"texts.hogg",
	"texture_library.hogg"
};

void aslPreMain(const char* pcAppName, int argc, char **argv)
{
	aslInitApp();


	memCheckInit();
	newConsoleWindow();
	showConsoleWindow();
	preloadDLLs(0);
	regSetAppName(pcAppName);


	loadstart_printf("FileSystem startup...");
	if (isDevelopmentMode()) {
		// Search for (FIND_HOGGS_TO_IGNORE) to find related code
		if (IsLoginServer() || IsAuctionServer()) 
		{
			FolderCacheAddIgnores(loginAndActionServerHoggIgnores, ARRAY_SIZE(loginAndActionServerHoggIgnores));
		} 
		else if (IsMapManager()) 
		{
			FolderCacheAddIgnores(mapManagerHoggIgnores, ARRAY_SIZE(mapManagerHoggIgnores));
		}
	}
	if (gAppServer->iFlags & APPSERVERTYPEFLAG_NOPIGGSINDEVELOPMENT)
	{
		FolderCacheChooseModeNoPigsInDevelopment();
	}
	else
	{
		FolderCacheChooseMode();
	}

	//FolderCacheEnableCallbacks(0);
	FolderCacheSetManualCallbackMode(1);
	FolderCacheStartMonitorThread();

	fileLoadGameDataDirAndPiggs();
	loadend_printf(" done.");

	if (IsLoginServer() || IsAuctionServer())
	{
		// Search for (FIND_DATA_DIRS_TO_CACHE) to find places related to this caching 
		loadstart_printf("Caching folders...");
		fileCacheDirectories(loginAuctionServerPrecachePaths, ARRAY_SIZE(loginAuctionServerPrecachePaths));
		loadend_printf(" done.");
	} 
	else if (IsMapManager())
	{
		// Search for (FIND_DATA_DIRS_TO_CACHE) to find places related to this caching 
		loadstart_printf("Caching folders...");
		fileCacheDirectories(mapManagerPrecachePaths, ARRAY_SIZE(mapManagerPrecachePaths));
		loadend_printf(" done.");
	}
	else if (IsTeamServer() || IsQueueServer() || IsGuildServer())
	{
		// Search for (FIND_DATA_DIRS_TO_CACHE) to find places related to this caching 
		loadstart_printf("Caching folders...");
		fileCacheDirectories(teamGuildQueueServerPrecachePaths, ARRAY_SIZE(teamGuildQueueServerPrecachePaths));
		loadend_printf(" done.");
	}

	bsAssertOnErrors(true);
	setDefaultAssertMode();

	logSetDir(GlobalTypeToName(GetAppGlobalType()));
	cryptAdler32Init();
	
	serverLibStartup(argc, argv);
}

void AppServerSetConsoleTitle(void)
{
	char title[1024];
	sprintf(title, "(%d) %s ID %d",getpid(), GlobalTypeToName(GetAppGlobalType()),gServerLibState.containerID);
	setConsoleTitle(title);
}

void aslMain(void)
{
	U32 frame_timer, update_timer;

	frame_timer = timerAlloc();
	timerStart(frame_timer);

	update_timer = timerAlloc();

	FolderCacheEnableCallbacks(1);


	timerStart(update_timer);

	AppServerSetConsoleTitle();

	printf("\nServer ready (%1.2fs load time).\n", timerElapsed(server_startup_timer));

	while (1)
	{
		F32 elapsed;
		
		autoTimerThreadFrameBegin(__FUNCTION__);
		
		elapsed = timerElapsedAndStart(frame_timer);
		utilitiesLibOncePerFrame(elapsed, 1);
		serverLibOncePerFrame();
		aslOncePerFrame(elapsed);

		FolderCacheDoCallbacks();

		commMonitor(commDefault());

		autoTimerThreadFrameEnd();
	}

	timerFree(frame_timer);
	timerFree(update_timer);
}




void aslOncePerFrame(F32 fElapsed)
{
	AppServerDef *def = gAppServer;

	if (def->oncePerFrame)
	{
		PERFINFO_AUTO_START_FUNC();
		def->oncePerFrame(fElapsed);
		PERFINFO_AUTO_STOP();
	}
}

void aslRegisterApp(GlobalType appType, AppServerInitCB appInit, U32 iAppServerTypeFlags)
{
	AppServerDef *newdef = FindAppServer(appType);
	if (newdef)
	{
		assert(!newdef->appRunning);
		if (newdef->appInit == appInit) {
			Errorf("AppServer named %s is already registered!",GlobalTypeToName(appType));
			return;
		} else {
			// Allowing overriding so Projects can make their own definition of a
			//   given app (which needs to call the lib version of the functions)
		}
	} else {
		newdef = calloc(sizeof(AppServerDef),1);
		eaPush(&gAppServerList,newdef);
	}
	newdef->appType = appType;
	newdef->appInit = appInit;
	newdef->appRunning = false;
	newdef->iFlags = iAppServerTypeFlags;
}



int  aslStartApp(void)
{
	GlobalType appType = GetAppGlobalType();
	AppServerDef *def = FindAppServer(appType);
	
	if (!def->appInit())
	{
		assertmsgf(0,"AppServer %s failed to start!",GlobalTypeToName(appType));
	}

	ATR_DoLateInitialization();

	return 1;
}

static void entity_aslDictPreContainerSendCB(const char *dictName,
											const char *resourceName,
											const Entity* eNew,
											const Entity* ePlayer,
											StructTypeField *excludeFlagsInOut,
											StructTypeField *includeFlagsInOut)
{
	// Only send certain fields to client
	*includeFlagsInOut |= TOK_LOGIN_SUBSCRIBE;
}

extern ParseTable parse_Team[];
#define TYPE_parse_Team Team
extern ParseTable parse_Guild[];
#define TYPE_parse_Guild Guild
extern ParseTable parse_QueueInfo[];
#define TYPE_parse_QueueInfo QueueInfo
extern ParseTable parse_PlayerDiary[];
#define TYPE_parse_PlayerDiary PlayerDiary
extern ParseTable parse_PersistedStore[];
#define TYPE_parse_PersistedStore PersistedStore

AUTO_RUN_LATE;
int RegisterEntityContainers(void)
{
	int i;
	for (i = 0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		if (GlobalTypeParent(i) == GLOBALTYPE_ENTITY)
		{
			objRegisterNativeSchema(i,parse_Entity, NULL, NULL, NULL, NULL, NULL);
			RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(i), false, parse_Entity, false, false, NULL);
		}
	}
	
	// Only register player entity type to get or send to client
	// Don't register pets or other entity sub-types
	resDictRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYPLAYER), RES_DICT_KEEP_NONE, false, objCopyDictHandleRequest);
	resDictProvideMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYPLAYER));
	resRegisterPreContainerSendCB(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYPLAYER), entity_aslDictPreContainerSendCB);

	// Register team dictionary
	RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(GLOBALTYPE_TEAM), false, parse_Team, false, false, NULL);
	resDictRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_TEAM), RES_DICT_KEEP_NONE, false, objCopyDictHandleRequest);
	resDictProvideMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_TEAM));
	
	// Register guild dictionary
	RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GUILD), false, parse_Guild, false, false, NULL);
	resDictRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GUILD), RES_DICT_KEEP_NONE, false, objCopyDictHandleRequest);
	resDictProvideMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GUILD));

	// Register guild bank dictionary
	resDictRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYGUILDBANK), RES_DICT_KEEP_NONE, false, objCopyDictHandleRequest);
	resDictProvideMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYGUILDBANK));

	// Register queue dictionary
	RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(GLOBALTYPE_QUEUEINFO), false, parse_QueueInfo, false, false, NULL);
	resDictRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_QUEUEINFO), RES_DICT_KEEP_NONE, false, objCopyDictHandleRequest);
	resDictProvideMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_QUEUEINFO));

	// Register diary dictionary
	objRegisterNativeSchema(GLOBALTYPE_PLAYERDIARY, parse_PlayerDiary, NULL, NULL, NULL, NULL, NULL);
	RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(GLOBALTYPE_PLAYERDIARY), false, parse_PlayerDiary, false, false, NULL);
// Appservers dont actually need diary data.
//	resDictRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_PLAYERDIARY), RES_DICT_KEEP_NONE, false, objCopyDictHandleRequest);
//	resDictProvideMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_PLAYERDIARY));
	
	// Register persisted store dictionary
	RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(GLOBALTYPE_PERSISTEDSTORE), false, parse_PersistedStore, false, false, NULL);
	resDictRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_PERSISTEDSTORE), RES_DICT_KEEP_NONE, false, objCopyDictHandleRequest);
	resDictProvideMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_PERSISTEDSTORE));

	return 1;
}

Entity *entExternGetCommandEntity(CmdContext *context)
{
	assertmsg(0,"You can only call this from game or server!");
	return NULL;
}

// Used to update the AI's cached perception radius to get around it being in game libs
void aiExternUpdatePerceptionRadii(Entity* be, AIVarsBase* aib)
{

}

// Used for adding things like health to the aggro expr context
void aiExternRegisterGameStatusExprInfo(ExprContext* context)
{

}

void aiExternAddGameStatusExprInfo(Entity* be, AIVarsBase* aib, ExprContext* context)
{

}

void aiExternGetHealth(Entity* be, F32* health, F32* maxHealth)
{

}

F32 aiExternGetStealth(Entity* be)
{
	return 0;
}

// Data to pass to aslAcquireContainersDispatch.
typedef struct AcquireContainersContinueData
{
	ContainerList *pList;								// Container list
	AppServerAcquiredSingleContainerCB fpSingleCallback;// App-specific acquisition callback
	AppServerAcquiredContainersCB fpCallback;			// App-specific completion callback
    U32 uRemainingTransfers;                    // The number of containers this request is still waiting for

	U32 uBatchSize;
	U32 uMaxRequests;
} AcquireContainersContinueData;

typedef struct AcquireContainerData
{
	AcquireContainersContinueData *pData;
	ContainerID conid;
} AcquireContainerData;

void aslRequestContainerMove_CB(TransactionReturnVal *pReturn, AcquireContainerData *pSingleData)
{
	AcquireContainersContinueData *pData = pSingleData->pData;
	--uTransferringContainers;
    --pData->uRemainingTransfers;
	if(pReturn->eOutcome != TRANSACTION_OUTCOME_SUCCESS)
	{
		printf("Failed to fetch %s container: %s.\n", GlobalTypeToName(pData->pList->type), pReturn->pBaseReturnVals[0].returnString);
	}

	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS && pData->fpSingleCallback)
	{
		pData->fpSingleCallback(pSingleData->conid);
	}

	free(pSingleData);

	// Call completion callback.
	if(pData->uRemainingTransfers == 0 && eaiSize(&pData->pList->eaiContainers) == 0)
	{
		loadend_printf("...done.");
		if(pData->fpCallback)
		{
			pData->fpCallback();
		}

		StructDestroy(parse_ContainerList, pData->pList);
		free(pData);
	}
}

//return true if finished
void aslAcquireContainersDispatch(TimedCallback *pCallback, F32 timeSinceLastCallback, AcquireContainersContinueData *pData)
{
	U32 count = 0;

	while(++count < pData->uBatchSize && uTransferringContainers < pData->uMaxRequests && eaiSize(&pData->pList->eaiContainers) > 0) 
	{
		AcquireContainerData *pSingleData = malloc(sizeof(AcquireContainerData));
		U32 conid = eaiPop(&pData->pList->eaiContainers);
		++uTransferringContainers;
        ++pData->uRemainingTransfers;
		pSingleData->conid = conid;
		pSingleData->pData = pData;
		objRequestContainerMove(objCreateManagedReturnVal(aslRequestContainerMove_CB, pSingleData), pData->pList->type, conid, GLOBALTYPE_OBJECTDB, 0, objServerType(), objServerID());
	}

	if(eaiSize(&pData->pList->eaiContainers) > 0)
	{
		TimedCallback_Run(aslAcquireContainersDispatch, pData, 0.1f);
	}
}

void aslAcquireContainerOwnership_CB(TransactionReturnVal *pReturn, AcquireContainersContinueData *pData)
{
	if(RemoteCommandCheck_dbAcquireContainers(pReturn, &pData->pList) != TRANSACTION_OUTCOME_SUCCESS)
	{
		AssertOrAlert("ASL_CONTAINER_LOAD_FAILED", "%s failed to acquire list of containers from DB!. Failure string: %s", GlobalTypeToName(GetAppGlobalType()), GetTransactionFailureString(pReturn));
	}
	else
	{
		if ( eaiSize(&pData->pList->eaiContainers) == 0 )
		{
			if ( pData->fpCallback != NULL )
			{
				pData->fpCallback();
			}
			StructDestroy(parse_ContainerList, pData->pList);
			free(pData);
			printf("no containers to load.\n");
		}
		else
		{
			loadstart_printf("Acquiring ownership of %d %s containers from the ObjectDB...", eaiSize(&pData->pList->eaiContainers), GlobalTypeToName(pData->pList->type));
			TimedCallback_Run(aslAcquireContainersDispatch, pData, 0.1f);
		}
	}
}

void aslAcquireContainerOwnershipEx(GlobalType containerType, AppServerAcquiredContainersCB fpCallback, AppServerAcquiredSingleContainerCB fpSingleCallback)
{
	AcquireContainersContinueData *pData = calloc(1, sizeof(AcquireContainersContinueData));
	pData->pList = NULL;
	pData->fpCallback = fpCallback;
	pData->fpSingleCallback = fpSingleCallback;

	if (GetAppGlobalID() > 1)
	{
		pData->uBatchSize = aslContainerReacquireBatchSize;
		pData->uMaxRequests = aslContainerReacquireMaxRequests;
	}
	else
	{
		pData->uBatchSize = aslContainerAcquireBatchSize;
		pData->uMaxRequests = aslContainerAcquireMaxRequests;
	}

	RemoteCommand_dbAcquireContainers(objCreateManagedReturnVal(aslAcquireContainerOwnership_CB, pData), GLOBALTYPE_OBJECTDB, 0, objServerType(), objServerID(), containerType);
}


void LoginServer_GetCustomServerInfoStructForHttp(UrlArgumentList *pUrl, ParseTable **ppTPI, void **ppStruct);
void JobManager_GetCustomServerInfoStructForHttp(UrlArgumentList *pUrl, ParseTable **ppTPI, void **ppStruct);
void ResourceDB_GetCustomServerInfoStructForHttp(UrlArgumentList *pUrl, ParseTable **ppTPI, void **ppStruct);
void MapManager_GetCustomServerInfoStructForHttp(UrlArgumentList *pUrl, ParseTable **ppTPI, void **ppStruct);
void aslUGCDataManager_GetCustomServerInfoStructForHttp(UrlArgumentList *pUrl, ParseTable **ppTPI, void **ppStruct);
void CurrencyExchange_GetCustomServerInfoStructForHttp(UrlArgumentList *pUrl, ParseTable **ppTPI, void **ppStruct);
void GatewayLoginLauncher_GetCustomServerInfoStructForHttp(UrlArgumentList *pUrl, ParseTable **ppTPI, void **ppStruct);

void DEFAULT_LATELINK_GetCustomServerInfoStructForHttp(UrlArgumentList *pUrl, ParseTable **ppTPI, void **ppStruct);

void OVERRIDE_LATELINK_GetCustomServerInfoStructForHttp(UrlArgumentList *pUrl, ParseTable **ppTPI, void **ppStruct)
{
	switch (GetAppGlobalType())
	{
	xcase GLOBALTYPE_LOGINSERVER:
		LoginServer_GetCustomServerInfoStructForHttp(pUrl, ppTPI, ppStruct);
	xcase GLOBALTYPE_JOBMANAGER:
		JobManager_GetCustomServerInfoStructForHttp(pUrl, ppTPI, ppStruct);
	xcase GLOBALTYPE_RESOURCEDB:
		ResourceDB_GetCustomServerInfoStructForHttp(pUrl, ppTPI, ppStruct);
	xcase GLOBALTYPE_MAPMANAGER:
		MapManager_GetCustomServerInfoStructForHttp(pUrl, ppTPI, ppStruct);
	xcase GLOBALTYPE_UGCDATAMANAGER:
		aslUGCDataManager_GetCustomServerInfoStructForHttp(pUrl, ppTPI, ppStruct);
    xcase GLOBALTYPE_CURRENCYEXCHANGESERVER:
        CurrencyExchange_GetCustomServerInfoStructForHttp(pUrl, ppTPI, ppStruct);
	xcase GLOBALTYPE_GATEWAYLOGINLAUNCHER:
		GatewayLoginLauncher_GetCustomServerInfoStructForHttp(pUrl, ppTPI, ppStruct);
	xdefault:
		DEFAULT_LATELINK_GetCustomServerInfoStructForHttp(pUrl, ppTPI, ppStruct);
	}
}

bool OVERRIDE_LATELINK_IsAppServerBasedType(void)
{
	return true;
}

//special handling for MAPTYPECORRUPTION alerts
bool OVERRIDE_LATELINK_SpecialMapTypeCorruptionAlertHandling(const char *pKey, const char *pString)
{
	char *pNewString = NULL;
	if (GetAppGlobalType() ==  GLOBALTYPE_MAPMANAGER)
	{
		return MapManager_SpecialMapTypeCorruptionAlertHandling(pKey, pString);
	}

	estrPrintf(&pNewString, "(Redirecting from %s to MapManager for whitelist checking) %s", 
		GlobalTypeToName(GetAppGlobalType()), pString);

	RemoteCommand_HandleMapTypeCorruptionAlert(GLOBALTYPE_MAPMANAGER, 0, pKey, pNewString);
	estrDestroy(&pNewString);

	return true;
}

#include "autogen/GameClientLib_autogen_ClientCmdWrappers.c"
#include "autogen/GameServerLib_autogen_ServerCmdWrappers.c"

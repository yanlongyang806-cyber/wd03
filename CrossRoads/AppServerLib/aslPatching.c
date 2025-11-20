#include "Organization.h"
#include "ResourceInfo.h"
#include "UtilitiesLib.h"
#include "aslPatching.h"
#include "crypticPorts.h"
#include "error.h"
#include "file.h"
#include "mapDescription.h"
#include "pcl_client.h"
#include "ScratchStack.h"
#include "ThreadManager.h"
#include "timedCallback.h"
#include "UGCProjectCommon.h"
#include "UGCProjectUtils.h"
#include "wininclude.h"
#include "logging.h"
#include "ShardCommon.h"

bool gbUseShardNameInPatchProjectNames = false;
AUTO_CMD_INT(gbUseShardNameInPatchProjectNames, UseShardNameInPatchProjectNames) ACMD_CMDLINE;

// Number of days before a namespace will actually be deleted.
static int siNamespaceUndeleteGracePeriodDays = 31;
AUTO_CMD_INT(siNamespaceUndeleteGracePeriodDays, NamespaceUndeleteGracePeriodDays) ACMD_CMDLINE;

int siMSecsToSleepBetweenNSDeletions = 1000;
AUTO_CMD_INT(siMSecsToSleepBetweenNSDeletions, MSecsToSleepBetweenNSDeletions) ACMD_CMDLINE;

static int gPatchTimeout = 20;
AUTO_COMMAND ACMD_COMMANDLINE;
void PatchTimeout(int timeout)
{
	gPatchTimeout = timeout;
}


// patch information to use for star-cluster maps
static char *gDynamicPatchserver = NULL;
static int gDynamicPatchserverPort = DEFAULT_PATCHSERVER_PORT;
static int gDynamicPatchBranch = 0;
static char *gDynamicPatchName = NULL;
static NetComm *patch_comm = NULL;									// PCL comm

AUTO_COMMAND ACMD_COMMANDLINE;
void DynamicPatchserver(const char *server)
{
	SAFE_FREE(gDynamicPatchserver);
	gDynamicPatchserver = strdup(server);
}
AUTO_CMD_INT(gDynamicPatchserverPort, DynamicPatchserverPort) ACMD_CMDLINE;
AUTO_CMD_INT(gDynamicPatchBranch, DynamicPatchBranch) ACMD_CMDLINE;
AUTO_COMMAND ACMD_COMMANDLINE;
void DynamicPatchName(const char *name)
{
	SAFE_FREE(gDynamicPatchName);
	gDynamicPatchName = strdup(name);
}

// patch information to use for UGC maps
static char *gUGCPatchserver = NULL;
static char *gUGCPatchserver_ForGetting = NULL;
static int gUGCPatchserverPort = DEFAULT_PATCHSERVER_PORT;
static int gUGCPatchBranch = 0;
AUTO_COMMAND ACMD_COMMANDLINE;
void UGCPatchserver(const char *server)
{
	SAFE_FREE(gUGCPatchserver);
	gUGCPatchserver = strdup(server);
}

AUTO_COMMAND ACMD_COMMANDLINE;
void UGCPatchserver_ForGetting(const char *server)
{
	SAFE_FREE(gUGCPatchserver_ForGetting);
	gUGCPatchserver_ForGetting = strdup(server);
}

AUTO_CMD_INT(gUGCPatchserverPort, UGCPatchserverPort) ACMD_CMDLINE;
AUTO_CMD_INT(gUGCPatchBranch, UGCPatchBranch) ACMD_CMDLINE;

static bool aslDeleteNamespace(PCL_Client *client, const char *pName);

void aslFillInPatchInfoForUGC(DynamicPatchInfo *pPatchInfo, enumFillInPatchInfoFlags eFlags)
{
	if (eFlags & PATCHINFO_FOR_UGC_PLAYING)
	{
		pPatchInfo->pServer = strdup(gUGCPatchserver_ForGetting?gUGCPatchserver_ForGetting:"ugcmaster."ORGANIZATION_DOMAIN); 
	}
	else
	{
		pPatchInfo->pServer = strdup(gUGCPatchserver?gUGCPatchserver:"ugcmaster."ORGANIZATION_DOMAIN); 
	}
	pPatchInfo->iPort = gUGCPatchserverPort; 

	if (gbUseShardNameInPatchProjectNames)
	{
		char *cluster_name = ShardCommon_GetClusterName();
		char *shard_name = GetShardNameFromShardInfoString();
		char *actual_name = (cluster_name && cluster_name[0]) ? cluster_name : shard_name;

		pPatchInfo->pClientProject = strdupf("%s_%sUGCClient", actual_name, GetProductName()); 
		pPatchInfo->pServerProject = strdupf("%s_%sUGCServer", actual_name, GetProductName()); 
		pPatchInfo->pUploadProject = strdupf("%s_%sUGC", actual_name, GetProductName()); 
		pPatchInfo->pResourceProject = strdupf("%s_%sUGCResource", actual_name, GetProductName()); 
	}
	else
	{
		pPatchInfo->pClientProject = strdupf("%sUGCClient", GetProductName()); 
		pPatchInfo->pServerProject = strdupf("%sUGCServer", GetProductName()); 
		pPatchInfo->pUploadProject = strdupf("%sUGC", GetProductName()); 
		pPatchInfo->pResourceProject = strdupf("%sUGCResource", GetProductName()); 
	}
	pPatchInfo->iBranch = gUGCPatchBranch; 
}

bool aslFillInPatchInfo(DynamicPatchInfo *pPatchInfo, const char *pNameSpace, enumFillInPatchInfoFlags eFlags )
{
	if(namespaceIsUGC(pNameSpace))
		aslFillInPatchInfoForUGC(pPatchInfo, eFlags);
	else if(strStartsWith(pNameSpace, "dyn_"))
	{ 
		pPatchInfo->pServer = strdup(gDynamicPatchserver?gDynamicPatchserver:"patchserver."ORGANIZATION_DOMAIN); 
		pPatchInfo->iPort = gDynamicPatchserverPort; 
		pPatchInfo->pClientProject = strdupf("%sDynamicClient", GetProductName()); 
		pPatchInfo->pServerProject = strdupf("%sDynamicServer", GetProductName()); 
		pPatchInfo->pResourceProject = strdupf("%sDynamicResource", GetProductName()); 
		pPatchInfo->iBranch = gDynamicPatchBranch; 
		pPatchInfo->pViewName = strdup(gDynamicPatchName); 
	} 
	else
	{
		ErrorOrAutoGroupingAlert("BAD_PATCH_INFO_PREFIX", 30, "Namespace for maps must start with \"dyn_\" or \"ugc_\" or \"%s\", found namespace %s. This is probably just obsolete and we should get rid of it", UGC_GetShardSpecificNSPrefix(NULL), pNameSpace);
		return false;
	}

	pPatchInfo->pPrefix = strdupf("/data/ns/%s", pNameSpace);
	pPatchInfo->iTimeout = gPatchTimeout;

	return true;
}

bool aslCheckNamespaceForPatchInfoFillIn(const char *pNameSpace)
{
	if (namespaceIsUGC(pNameSpace) || strStartsWith(pNameSpace, "dyn_"))
	{
		return true;
	}

	return false;
}

// Handle a PCL error.
#define PCL_DO_ERROR()																	\
{																						\
	if(error != PCL_SUCCESS)															\
	{																					\
		char *msg = ScratchAlloc(MAX_PATH);												\
		pclGetErrorString(error, msg, MAX_PATH);										\
		strcat_s(msg + strlen(msg), MAX_PATH - strlen(msg), ", State: ");				\
		pclGetStateString(client, msg + strlen(msg), MAX_PATH - strlen(msg));			\
		log_printf(LOG_NSTHREADING, "PCL_DO_ERROR error: %s. Client: %s", msg, pclGetUsefulDebugString_Static(client)); \
		AssertOrAlert("MAPMANAGER_NAMESPACE_ERROR", "PCL error: %s. Client: %s",		\
			msg, pclGetUsefulDebugString_Static(client));								\
		ScratchFree(msg);																\
		return 0;																			\
	}																					\
}

// Run PCL commands.
#define PCL_DO(fn)	\
{					\
	error = fn;		\
	PCL_DO_ERROR();	\
}

// Run PCL commands, and wait for them to finish.
#define PCL_DO_WAIT(fn)			\
{								\
	PCL_DO(fn);					\
	error = pclWait(client);	\
	PCL_DO_ERROR();				\
}


typedef enum NamespaceListGettingState
{
	INACTIVE,
	ONGOING,
	COMPLETE
} NamespaceListGettingState;

static NamespaceListGettingState eNamespaceGettingState = INACTIVE;
static char **ppNamespaceList = NULL;								// List of namespaces, filled by aslGetNamespaceListDone()
static U32 *pNamespaceModifiedTimes = NULL;						// List of times, filled by aslGetNamespaceListDone()

static char **ppNamespacesToDelete_Main = NULL;
static char **ppNamespacesToDelete_BG = NULL;

// Get the list of namespaces, in a background thread.
static DWORD WINAPI GetNamespaceListThread(LPVOID lpParam)
{
	DynamicPatchInfo *info = lpParam;
	PCL_Client *client = NULL;
	PCL_ErrorCode error;
	int i;

	EXCEPTION_HANDLER_BEGIN;


	log_printf(LOG_NSTHREADING, "Inside querying thread");

	// Create comm.
	if(!patch_comm)
		patch_comm = commCreate(0, 1);

	// Get manifest.
	PCL_DO_WAIT(pclConnectAndCreate(&client, info->pServer, info->iPort, 60, patch_comm, "", "aslPatching", "", NULL, NULL));
	pclAddFileFlags(client, PCL_METADATA_IN_MEMORY);
	pclSetBadFilesDirectory(client, fileTempDir());
	PCL_DO_WAIT(pclSetNamedView(client, info->pUploadProject, info->pViewName, true, false, NULL, NULL));

	log_printf(LOG_NSTHREADING, "Inside querying thread, just passed setNamedView, about to delete %d namespaces", eaSize(&ppNamespacesToDelete_BG));

	for (i=0; i < eaSize(&ppNamespacesToDelete_BG); i++)
	{
		char temp[CRYPTIC_MAX_PATH];

		
		log_printf(LOG_NSTHREADING, "Inside thread, about to delete namespace %s", ppNamespacesToDelete_BG[i]);
		
		sprintf(temp, "data/ns/%s", ppNamespacesToDelete_BG[i]);
		aslDeleteNamespace(client, temp);
		Sleep(siMSecsToSleepBetweenNSDeletions);
	}

	log_printf(LOG_NSTHREADING, "Deleted all namespaces");
	
	
	eaDestroyEx(&ppNamespacesToDelete_BG, NULL);

	// Save results.
	pclListFilesInDir(client, "data/ns", &ppNamespaceList, &pNamespaceModifiedTimes, false);

	log_printf(LOG_NSTHREADING, "Queried all namespaces, found %d names, %d times",
		eaSize(&ppNamespaceList), ea32Size(&pNamespaceModifiedTimes));


	// Clean up.
	pclDisconnectAndDestroy(client);

	MemoryBarrier();

	eNamespaceGettingState = COMPLETE;


	EXCEPTION_HANDLER_END;

	return 0;
}

// Get the list of namespaces for a particular type of namespace.
//
//returns true if request is being sent off. If true returned, then start calling aslGetNamespaceListDone
bool aslBeginBackgroundThreadNamespaceListQuery(const char *namespace_prefix)
{
	DynamicPatchInfo *info = StructCreate(parse_DynamicPatchInfo);
	ManagedThread *background_thread;

	if (eNamespaceGettingState != INACTIVE)
	{
		log_printf(LOG_NSTHREADING, "can't begin another namespace list query, the old one is still running");
		return false;
	}

	assertmsgf(ppNamespacesToDelete_BG == NULL, "somehow the background list of namespaces to delete still exists");
	ppNamespacesToDelete_BG = ppNamespacesToDelete_Main;
	ppNamespacesToDelete_Main = NULL;

	eNamespaceGettingState = ONGOING;

	// Start background thread.
	aslFillInPatchInfo(info, namespace_prefix, 0);

	log_printf(LOG_NSTHREADING, "Beginning namespace querying thread, with %d namespaces to delete", eaSize(&ppNamespacesToDelete_BG));
	servLogWithStruct(LOG_NSTHREADING, "Patch info for querying thread", info, parse_DynamicPatchInfo);
	background_thread = tmCreateThread(GetNamespaceListThread, info);

	return true;
}

// Return true if a previous aslGetNamespaceList() request is done and namespace_list and namespace_newest_modified are valid.
bool aslGetNamespaceListDone(char ***pppNames, U32 **ppModTimes)
{
	if (eNamespaceGettingState == COMPLETE)
	{
		log_printf(LOG_NSTHREADING, "aslGetNamespaceListDone called, returning TRUE");
		*pppNames = ppNamespaceList;
		*ppModTimes = pNamespaceModifiedTimes;
		return true;
	}

	return false;
}

void aslNamespaceListCleanup(bool bDontCleanupArrays)
{

	if (eNamespaceGettingState != COMPLETE)
	{
		log_printf(LOG_NSTHREADING, "aslNamespaceListCleanup called INCORRECTLY");
		return;
	}

	log_printf(LOG_NSTHREADING, "aslNamespaceListCleanup called, threading returning to start state");


	if (bDontCleanupArrays)
	{
		ppNamespaceList = NULL;
		pNamespaceModifiedTimes = NULL;
	}
	else
	{
		eaDestroyEx(&ppNamespaceList, NULL);
		ea32Destroy(&pNamespaceModifiedTimes);
	}

	eNamespaceGettingState = INACTIVE;
}

void aslNamespaceListTestCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	char **ppNames = NULL;
	U32 *pTimes = NULL;
	if (aslGetNamespaceListDone(&ppNames, &pTimes))
	{
		int i;

		for (i=0; i < eaSize(&ppNames); i++)
		{
			printf("%s\n", ppNames[i]);
		}

		aslNamespaceListCleanup(false);
	}
	else
	{
		printf("Not done yet\n");
		TimedCallback_Run(aslNamespaceListTestCB, NULL, 1.0f);
	}

}

void aslAddNamespaceToBeDeletedNextQuery(const char *pName)
{
	eaPush(&ppNamespacesToDelete_Main, strdup(pName));
}

AUTO_COMMAND;
void NamespaceListTest(void)
{
	aslBeginBackgroundThreadNamespaceListQuery(UGC_GetShardSpecificNSPrefix(NULL));
	TimedCallback_Run(aslNamespaceListTestCB, NULL, 1.0f);
}

// Permanently remove a namespace from the patch database.
static bool aslDeleteNamespace(PCL_Client *client, const char *pName)
{
	PCL_ErrorCode error;
	const char *dummy_directory[1] = {"DOES|NOT|EXIST"};
	const char *path_name[1];
	int recurse = 1;
	bool exists;
	bool deleted;

	devassert(client);

	// Make sure the directory exists.
	PCL_DO(pclExistsInDb(client, pName, &exists));
	if (!exists)
	{
		log_printf(LOG_NSTHREADING, "Couldn't delete %s, pclExistsInDb failed", pName);
		return false;
	}

	// Make sure the directory has something in it.  Failing this most likely means the directory was already deleted.
	PCL_DO(pclIsDeleted(client, pName, &deleted));
	if (deleted)
	{
		log_printf(LOG_NSTHREADING, "Couldn't delete %s, pclIsDeleted failed", pName);
		AssertOrAlert("MAPMANAGER_NAMESPACE_DELETION_ERROR", "Attempt to delete empty directory.  Client: %s",
			pclGetUsefulDebugString_Static(client));
		return false;
	}

	// Set author.
	PCL_DO_WAIT(pclSetAuthor(client, "aslPatching", NULL, NULL));

	// Do an empty checkin that will do a "versioned delete" of the path, effective immediately.
	path_name[0] = pName;
	PCL_DO_WAIT(pclForceInFiles(client, dummy_directory, path_name, &recurse, 1, NULL, 0, "Namespace deletion", false, NULL, NULL));

	// Mark the file to be expired, which will cause a permanent delete after a specified number of days.
	PCL_DO_WAIT(pclSetFileExpiration(client, pName, siNamespaceUndeleteGracePeriodDays, NULL, NULL));

	return true;
}

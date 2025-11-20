#include "pcl_client_wt.h"
#include "WorkerThread.h"
#include "earray.h"
#include "StringCache.h"
#include "pcl_client_wt_h_ast.h"
#include "net/net.h"
#include "pcl_client.h"
#include "utilitiesLib.h"
#include "pcl_client_wt_c_ast.h"
#include "ResourceInfo.h"

WorkerThread *gpPCLWorkerThread = NULL;


//main-thread-to-worker-thread
enum
{
	PCLWTCMD_GET_VERSIONS = WT_CMD_USER_START,
	PCLWTCMD_GET_VERSIONS_DONE,
	PCLWTCMD_GET_FILE_INTO_RAM,
	PCLWTCMD_GET_FILE_INTO_RAM_DONE,

};

typedef struct PCLThread_GetVersionsStruct
{
	const char *pProduct;
	const char *pServer;
	GetPatchVersionsCB pCB;
	void *pUserData;
} PCLThread_GetVersionsStruct;

typedef struct PCLThread_GetVersionsResultStruct
{
	PatchVersionInfo **ppVersions;
	PCL_ErrorCode error;
	char *pErrorDetails;
	GetPatchVersionsCB pCB;
	void *pUserData;
} PCLThread_GetVersionsResultStruct;


typedef struct PCLThread_GetFileIntoRAMStruct
{
	const char *pProduct;
	const char *pServer;
	char view[256];
	char fileName[CRYPTIC_MAX_PATH];
	GetFileIntoRAMCB pCB;
	void *pUserData;
} PCLThread_GetFileIntoRAMStruct;

typedef struct PCLThread_GetFileIntoRAMResultStruct
{
	void *pFileData;
	int iFileSize;
	PCL_ErrorCode error;
	char *pErrorDetails;
	GetFileIntoRAMCB pCB;
	void *pUserData;
} PCLThread_GetFileIntoRAMResultStruct;

static NetComm *pCommPCLThreaded = NULL;

static void NameListCB(char ** names, int * branches, char ** sandboxes, U32 * revs, char ** comments, U32 * expires, int count, PCL_ErrorCode error, const char * error_details, void * userData)
{
	PCLThread_GetVersionsResultStruct *pResultsStruct = (PCLThread_GetVersionsResultStruct*)userData;
	int i;

	for (i = 0; i < count; i++)
	{
		PatchVersionInfo *pVersion = StructCreate(parse_PatchVersionInfo);
		pVersion->pName = strdup(names[i]);
		pVersion->iBranch = branches[i];
		pVersion->pSandBox = strdup(sandboxes[i]);
		pVersion->iRev = revs[i];
		pVersion->pComment = strdup(comments[i]);
		pVersion->iExpire = expires[i];

		eaPush(&pResultsStruct->ppVersions, pVersion);
	}
}


static void pclThread_GetVersions(void *user_data, void *data, WTCmdPacket *packet)
{
	PCLThread_GetVersionsStruct *pGetVersionsStruct = (PCLThread_GetVersionsStruct*)data;
	PCLThread_GetVersionsResultStruct resultsStruct = {0};

	PCL_Client *pPCLClient = NULL;

	if (!pCommPCLThreaded)
	{
		pCommPCLThreaded = commCreate(0, 0);
	}
	
	resultsStruct.pCB = pGetVersionsStruct->pCB;
	resultsStruct.pUserData = pGetVersionsStruct->pUserData;

	if ((resultsStruct.error = pclConnectAndCreate(&pPCLClient, pGetVersionsStruct->pServer, DEFAULT_PATCHSERVER_PORT, 60, pCommPCLThreaded, NULL, NULL, NULL, NULL, NULL)) != PCL_SUCCESS)
	{	
		estrPrintf(&resultsStruct.pErrorDetails, "Couldn't connect to patch server %s", pGetVersionsStruct->pServer);
		wtQueueMsg(gpPCLWorkerThread, PCLWTCMD_GET_VERSIONS_DONE, &resultsStruct, sizeof(resultsStruct));
		return;
	}

	pclSoftenAllErrors(pPCLClient);

	if ((resultsStruct.error = pclWait(pPCLClient)) != PCL_SUCCESS)
	{
		estrPrintf(&resultsStruct.pErrorDetails, "Couldn't connect to patch server %s (pclWait() failed after ConnectAndCreate)", pGetVersionsStruct->pServer);
		pclDisconnectAndDestroy(pPCLClient);
		wtQueueMsg(gpPCLWorkerThread, PCLWTCMD_GET_VERSIONS_DONE, &resultsStruct, sizeof(resultsStruct));
		return;
	}

	if ((resultsStruct.error = pclSetDefaultView(pPCLClient, pGetVersionsStruct->pProduct, false, NULL, NULL)) != PCL_SUCCESS)
	{
		estrPrintf(&resultsStruct.pErrorDetails, "Couldn't connect to patch server %s (pclSetDefaultView failed)", pGetVersionsStruct->pServer);
		pclDisconnectAndDestroy(pPCLClient);
		wtQueueMsg(gpPCLWorkerThread, PCLWTCMD_GET_VERSIONS_DONE, &resultsStruct, sizeof(resultsStruct));
		return;
	}

	if ((resultsStruct.error = pclWait(pPCLClient)) != PCL_SUCCESS)
	{
		estrPrintf(&resultsStruct.pErrorDetails, "Couldn't connect to patch server %s (pclWait() failed after pclSetDefaultView)", pGetVersionsStruct->pServer);
		pclDisconnectAndDestroy(pPCLClient);
		wtQueueMsg(gpPCLWorkerThread, PCLWTCMD_GET_VERSIONS_DONE, &resultsStruct, sizeof(resultsStruct));
		return;
	}

	if ((resultsStruct.error = pclGetNameList(pPCLClient, NameListCB, &resultsStruct)) != PCL_SUCCESS)
	{
		estrPrintf(&resultsStruct.pErrorDetails, "Couldn't connect to patch server %s (pclGetNameList() failed)", pGetVersionsStruct->pServer);
		pclDisconnectAndDestroy(pPCLClient);
		wtQueueMsg(gpPCLWorkerThread, PCLWTCMD_GET_VERSIONS_DONE, &resultsStruct, sizeof(resultsStruct));
		return;
	}

	if ((resultsStruct.error = pclWait(pPCLClient)) != PCL_SUCCESS)
	{
		estrPrintf(&resultsStruct.pErrorDetails, "Couldn't connect to patch server %s (pclWait() failed after pclGetNameList)", pGetVersionsStruct->pServer);
		pclDisconnectAndDestroy(pPCLClient);
		wtQueueMsg(gpPCLWorkerThread, PCLWTCMD_GET_VERSIONS_DONE, &resultsStruct, sizeof(resultsStruct));
		return;
	}

	pclDisconnectAndDestroy(pPCLClient);
	resultsStruct.error = PCL_SUCCESS;
	wtQueueMsg(gpPCLWorkerThread, PCLWTCMD_GET_VERSIONS_DONE, &resultsStruct, sizeof(resultsStruct));
}

static void pclThread_GetVersionsDone(void *user_data, void *data, WTCmdPacket *packet)
{
	PCLThread_GetVersionsResultStruct *pResultsStruct = (PCLThread_GetVersionsResultStruct*)data;

	pResultsStruct->pCB(pResultsStruct->ppVersions, pResultsStruct->error, pResultsStruct->pErrorDetails, pResultsStruct->pUserData);
	estrDestroy(&pResultsStruct->pErrorDetails);
	eaDestroyStruct(&pResultsStruct->ppVersions, parse_PatchVersionInfo);
}

void GetFileInRAMIterator(void* client, const char *filename, const char *data, U32 size, U32 modtime, PCLThread_GetFileIntoRAMResultStruct *pResultsStruct)
{
	if (pResultsStruct->pFileData)
	{
		SAFE_FREE(pResultsStruct->pFileData);
		estrPrintf(&pResultsStruct->pErrorDetails, "Found more than one file");
	}
	else
	{
		pResultsStruct->pFileData = malloc(size);
		memcpy(pResultsStruct->pFileData, data, size);
		pResultsStruct->iFileSize = size;
	}
}
static void pclThread_GetFileIntoRAM(void *user_data, void *data, WTCmdPacket *packet)
{
	PCLThread_GetFileIntoRAMStruct *pGetFileIntoRAMStruct = (PCLThread_GetFileIntoRAMStruct*)data;
	PCLThread_GetFileIntoRAMResultStruct resultsStruct = {0};

	PCL_Client *pPCLClient = NULL;

	if (!pCommPCLThreaded)
	{
		pCommPCLThreaded = commCreate(0, 0);
	}
	
	resultsStruct.pCB = pGetFileIntoRAMStruct->pCB;
	resultsStruct.pUserData = pGetFileIntoRAMStruct->pUserData;

	//FIXME COR-16088
	if ((resultsStruct.error = pclConnectAndCreate(&pPCLClient, pGetFileIntoRAMStruct->pServer, DEFAULT_PATCHSERVER_PORT, 60, pCommPCLThreaded, NULL, "ThreadedPCL_PatchClient", NULL, NULL, NULL)) != PCL_SUCCESS)
	{	
		estrPrintf(&resultsStruct.pErrorDetails, "Couldn't connect to patch server %s", pGetFileIntoRAMStruct->pServer);
		wtQueueMsg(gpPCLWorkerThread, PCLWTCMD_GET_FILE_INTO_RAM_DONE, &resultsStruct, sizeof(resultsStruct));
		return;
	}

	pclSoftenAllErrors(pPCLClient);

	if ((resultsStruct.error = pclWait(pPCLClient)) != PCL_SUCCESS)
	{
		estrPrintf(&resultsStruct.pErrorDetails, "Couldn't connect to patch server %s (pclWait() failed after ConnectAndCreate)", pGetFileIntoRAMStruct->pServer);
		pclDisconnectAndDestroy(pPCLClient);
		wtQueueMsg(gpPCLWorkerThread, PCLWTCMD_GET_FILE_INTO_RAM_DONE, &resultsStruct, sizeof(resultsStruct));
		return;
	}

	if ((resultsStruct.error = pclAddFileFlags(pPCLClient, PCL_IN_MEMORY)) != PCL_SUCCESS)
	{
		estrPrintf(&resultsStruct.pErrorDetails, "Couldn't add file flags");
		pclDisconnectAndDestroy(pPCLClient);
		wtQueueMsg(gpPCLWorkerThread, PCLWTCMD_GET_FILE_INTO_RAM_DONE, &resultsStruct, sizeof(resultsStruct));
		return;
	}

	if ((resultsStruct.error = pclSetNamedView(pPCLClient, pGetFileIntoRAMStruct->pProduct, pGetFileIntoRAMStruct->view, false, false, NULL, NULL)) != PCL_SUCCESS)
	{
		estrPrintf(&resultsStruct.pErrorDetails, "Couldn't connect to patch server %s (pclSetNamedView failed)", pGetFileIntoRAMStruct->pServer);
		pclDisconnectAndDestroy(pPCLClient);
		wtQueueMsg(gpPCLWorkerThread, PCLWTCMD_GET_FILE_INTO_RAM_DONE, &resultsStruct, sizeof(resultsStruct));
		return;
	}

	if ((resultsStruct.error = pclWait(pPCLClient)) != PCL_SUCCESS)
	{
		estrPrintf(&resultsStruct.pErrorDetails, "Couldn't connect to patch server %s (pclWait() failed after pclSetNamedView)", pGetFileIntoRAMStruct->pServer);
		pclDisconnectAndDestroy(pPCLClient);
		wtQueueMsg(gpPCLWorkerThread, PCLWTCMD_GET_FILE_INTO_RAM_DONE, &resultsStruct, sizeof(resultsStruct));
		return;
	}


	if ((resultsStruct.error = pclInjectFileVersionFromServer(pPCLClient, pGetFileIntoRAMStruct->fileName))  != PCL_SUCCESS)
	{
		estrPrintf(&resultsStruct.pErrorDetails, "pclInjectFileVersionFromServer failed");
		pclDisconnectAndDestroy(pPCLClient);
		wtQueueMsg(gpPCLWorkerThread, PCLWTCMD_GET_FILE_INTO_RAM_DONE, &resultsStruct, sizeof(resultsStruct));
		return;
	}

	if ((resultsStruct.error = pclWait(pPCLClient)) != PCL_SUCCESS)
	{
		estrPrintf(&resultsStruct.pErrorDetails, "pclWait() failed after pclInjectFileVersionFromServer)");
		pclDisconnectAndDestroy(pPCLClient);
		wtQueueMsg(gpPCLWorkerThread, PCLWTCMD_GET_FILE_INTO_RAM_DONE, &resultsStruct, sizeof(resultsStruct));
		return;
	}

	if ((resultsStruct.error = pclGetFile(pPCLClient, pGetFileIntoRAMStruct->fileName, 0, NULL, NULL))  != PCL_SUCCESS)
	{
		estrPrintf(&resultsStruct.pErrorDetails, "pclGetfile failed");
		pclDisconnectAndDestroy(pPCLClient);
		wtQueueMsg(gpPCLWorkerThread, PCLWTCMD_GET_FILE_INTO_RAM_DONE, &resultsStruct, sizeof(resultsStruct));
		return;
	}

	if ((resultsStruct.error = pclWait(pPCLClient)) != PCL_SUCCESS)
	{
		estrPrintf(&resultsStruct.pErrorDetails, "pclWait() failed after pclGetFile)");
		pclDisconnectAndDestroy(pPCLClient);
		wtQueueMsg(gpPCLWorkerThread, PCLWTCMD_GET_FILE_INTO_RAM_DONE, &resultsStruct, sizeof(resultsStruct));
		return;
	}

	if ((resultsStruct.error = pclForEachInMemory(pPCLClient, GetFileInRAMIterator, &resultsStruct)) != PCL_SUCCESS)
	{
		SAFE_FREE(resultsStruct.pFileData);
		estrPrintf(&resultsStruct.pErrorDetails, "pclForEachInMemory failed");
		pclDisconnectAndDestroy(pPCLClient);
		wtQueueMsg(gpPCLWorkerThread, PCLWTCMD_GET_FILE_INTO_RAM_DONE, &resultsStruct, sizeof(resultsStruct));
		return;
	}

	if (!resultsStruct.pFileData)
	{
		estrPrintf(&resultsStruct.pErrorDetails, "No file returned");
	}

	pclDisconnectAndDestroy(pPCLClient);
	wtQueueMsg(gpPCLWorkerThread, PCLWTCMD_GET_FILE_INTO_RAM_DONE, &resultsStruct, sizeof(resultsStruct));
}

static void pclThread_GetFileIntoRAMDone(void *user_data, void *data, WTCmdPacket *packet)
{
	PCLThread_GetFileIntoRAMResultStruct *pResultsStruct = (PCLThread_GetFileIntoRAMResultStruct*)data;
	pResultsStruct->pCB(pResultsStruct->pFileData, pResultsStruct->iFileSize, pResultsStruct->error, pResultsStruct->pErrorDetails, pResultsStruct->pUserData);
	estrDestroy(&pResultsStruct->pErrorDetails);
}

static void pclThread_Tick(void)
{
	wtMonitor(gpPCLWorkerThread);
}

static void LazyWorkerThreadInit(void)
{
	if (!gpPCLWorkerThread)
	{
		gpPCLWorkerThread = wtCreate(16, 16, NULL, "PCL Thread");
	
		wtRegisterCmdDispatch(gpPCLWorkerThread, PCLWTCMD_GET_VERSIONS, pclThread_GetVersions);
		wtRegisterMsgDispatch(gpPCLWorkerThread, PCLWTCMD_GET_VERSIONS_DONE, pclThread_GetVersionsDone);

		wtRegisterCmdDispatch(gpPCLWorkerThread, PCLWTCMD_GET_FILE_INTO_RAM, pclThread_GetFileIntoRAM);
		wtRegisterMsgDispatch(gpPCLWorkerThread, PCLWTCMD_GET_FILE_INTO_RAM_DONE, pclThread_GetFileIntoRAMDone);
	
		wtSetThreaded(gpPCLWorkerThread, true, 0, false);
		wtStart(gpPCLWorkerThread);
		UtilitiesLib_AddExtraTickFunction(pclThread_Tick);
	}
}



void ThreadedPCL_GetPatchVersions(char *pServer, char *pProduct, GetPatchVersionsCB pCB, void *pUserData)
{
	PCLThread_GetVersionsStruct msgStruct = {0};
	LazyWorkerThreadInit();

	msgStruct.pCB = pCB;
	msgStruct.pProduct = allocAddString(pProduct);
	msgStruct.pServer = allocAddString(pServer);
	msgStruct.pUserData = pUserData;

	wtQueueCmd(gpPCLWorkerThread, PCLWTCMD_GET_VERSIONS, &msgStruct, sizeof(msgStruct));
}

void ThreadedPCL_GetFileIntoRAM(char *pServer, char *pProduct, char *pView, char *pFileName, GetFileIntoRAMCB pCB, void *pUserData)
{
	PCLThread_GetFileIntoRAMStruct msgStruct = {0};
	LazyWorkerThreadInit();

	msgStruct.pCB = pCB;
	strcpy(msgStruct.fileName, pFileName);
	strcpy(msgStruct.view, pView);
	msgStruct.pProduct = allocAddString(pProduct);
	msgStruct.pServer = allocAddString(pServer);
	msgStruct.pUserData = pUserData;

	wtQueueCmd(gpPCLWorkerThread, PCLWTCMD_GET_FILE_INTO_RAM, &msgStruct, sizeof(msgStruct));
}

AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "Comment");
typedef struct PatchVersionInfoForServerMon
{
	char *pName; AST(KEY)
	char *pComment; 
} PatchVersionInfoForServerMon;

AUTO_STRUCT;
typedef struct PatchServerMonStuff
{
	char *pProductName; AST(KEY)
	PatchVersionInfoForServerMon **ppVersions;
	PatchVersionInfoForServerMon **ppRecentVersions;
} PatchServerMonStuff;

static StashTable sServerMonitorableVersionsByProduct = NULL;

void ThreadedPCL_MakePatchVersionsServerMonitorableCB(PatchVersionInfo **ppVersions, PCL_ErrorCode error, char *pErrorDetails, 
	PatchServerMonStuff *pStuff)
{
	int i, iSize;

	eaClearStruct(&pStuff->ppVersions, parse_PatchVersionInfoForServerMon);
	eaClearStruct(&pStuff->ppRecentVersions, parse_PatchVersionInfoForServerMon);

	FOR_EACH_IN_EARRAY(ppVersions, PatchVersionInfo, pVersion)
	{
		PatchVersionInfoForServerMon *pVersionForServerMon = StructCreate(parse_PatchVersionInfoForServerMon);
		pVersionForServerMon->pName = strdup(pVersion->pName);
		pVersionForServerMon->pComment = strdup(pVersion->pComment);

		eaPush(&pStuff->ppVersions, pVersionForServerMon);
	}
	FOR_EACH_END;

	iSize = eaSize(&ppVersions);
	for (i = iSize - 100; i < iSize; i++)
	{
		if (i >= 0)
		{
			PatchVersionInfoForServerMon *pVersionForServerMon = StructCreate(parse_PatchVersionInfoForServerMon);
			PatchVersionInfo *pVersion = ppVersions[i];
			pVersionForServerMon->pName = strdup(pVersion->pName);
			pVersionForServerMon->pComment = strdup(pVersion->pComment);

			eaPush(&pStuff->ppRecentVersions, pVersionForServerMon);
		}
	}

}

AUTO_COMMAND;
void MakePatchVersionsServerMonitorable(char *pServer, char *pProductName)
{
	PatchServerMonStuff *pStuff;

	if (!sServerMonitorableVersionsByProduct)
	{
		sServerMonitorableVersionsByProduct = stashTableCreateWithStringKeys(8, StashDefault);
	}

	if (!stashFindPointer(sServerMonitorableVersionsByProduct, pProductName, &pStuff))
	{
		char dictName[128];
		sprintf(dictName, "%s_PatchVersions", pProductName);
		pStuff = StructCreate(parse_PatchServerMonStuff);
		pStuff->pProductName = strdup(pProductName);
		stashAddPointer(sServerMonitorableVersionsByProduct, pStuff->pProductName, pStuff, false);
	
		resRegisterDictionaryForEArray(allocAddString(dictName), RESCATEGORY_OTHER, 0, 
			&pStuff->ppVersions, parse_PatchVersionInfoForServerMon); 
		resDictSetHTMLExtraCommand(dictName, "Refresh", STACK_SPRINTF("MakePatchVersionsServerMonitorable %s %s", pServer, pProductName));


		sprintf(dictName, "%s_RecentPatchVersions", pProductName);
		resRegisterDictionaryForEArray(allocAddString(dictName), RESCATEGORY_OTHER, 0, 
			&pStuff->ppRecentVersions, parse_PatchVersionInfoForServerMon); 
		resDictSetHTMLExtraCommand(dictName, "Refresh", STACK_SPRINTF("MakePatchVersionsServerMonitorable %s %s", pServer, pProductName));

	}

	ThreadedPCL_GetPatchVersions(pServer, pProductName, ThreadedPCL_MakePatchVersionsServerMonitorableCB, pStuff);
}

char *GetCommentFromServerMonitorablePatchVersion(char *pProduct, char *pVersionName)
{
	PatchServerMonStuff *pStuff;
	if (stashFindPointer(sServerMonitorableVersionsByProduct, pProduct, &pStuff))
	{
		PatchVersionInfoForServerMon *pInfo = eaIndexedGetUsingString(&pStuff->ppVersions, pVersionName);
		if (pInfo)
		{
			return pInfo->pComment;
		}
	}

	return NULL;


}
#include "pcl_client_wt_h_ast.c"
#include "pcl_client_wt_c_ast.c"

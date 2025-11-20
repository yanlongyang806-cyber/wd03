#include "gslBeaconInterface.h"

#include "../../core/controller/pub/controllerpub.h"
#include "ControllerLink.h"
#include "GameServerLib.h"
#include "ServerLib.h"
#include "wlBeacon.h"
#include "GenericHttpServing.h"
#include "WorldLib.h"
#include "WorldGrid.h"
#include "pcl_client.h"
#include "LocalTransactionManager.h"
#include "EntityMovementManager.h"
#include "EntityMovementDefault.h"
#include "gslPartition.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "TransactionOutcomes.h"
#include "windefinclude.h"
#include "beaconClientServerPrivate.h"
#include "utilitiesLib.h"
#include "RemoteCommandGroup.h"
#include "JobManagerSupport.h"
#include "ServerLib.h"
#include "BeaconMovement.h"
#include "gslUGC.h"
#include "url.h"
#include "stdtypes.h"

#include "wininclude.h"

#include "AutoGen/url_h_ast.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "autogen/controller_autogen_remotefuncs.h"

typedef struct ServerListCallbackData {
	BeaconServerListCallback cb;
	void *data;
} ServerListCallbackData;

typedef struct ServerNameCallbackData {
	BeaconServerNameCallback cb;
	void *data;
} ServerNameCallbackData;

void gslBeaconServerListReceiveCB(TransactionReturnVal *retVal, ServerListCallbackData *data)
{
	Controller_ServerList *listContainer = NULL;
	U32 *ipList = NULL;
	if(RemoteCommandCheck_GetServerList(retVal, &listContainer)==TRANSACTION_OUTCOME_SUCCESS)
	{
		int i;
		for(i=0; i<eaSize(&listContainer->ppServers); i++)
			ea32Push(&ipList, listContainer->ppServers[i]->iIP);
	}
	
	data->cb(ipList, data->data);

	free(data);

	StructDestroy(parse_Controller_ServerList, listContainer);
}

void gslBeaconServerNameReceiveCB(TransactionReturnVal *retVal, ServerNameCallbackData *data)
{
	char *address = NULL;

	if(RemoteCommandCheck_GetShardBeaconMasterServer(retVal, &address)!=TRANSACTION_OUTCOME_SUCCESS)
		address = NULL;

	data->cb(address, data->data);

	free(data);

	estrDestroy(&address);
}

void gslBeaconGetServerList(GlobalType gtype, BeaconServerListCallback cb, void* userdata)
{
	ServerListCallbackData *data = calloc(1, sizeof(ServerListCallbackData));

	data->cb = cb;
	data->data = userdata;
	RemoteCommand_GetServerList(objCreateManagedReturnVal(gslBeaconServerListReceiveCB, data), 
									GLOBALTYPE_CONTROLLER,
									0, 
									gtype);
}

void gslBeaconGetServerName(BeaconServerNameCallback cb, void* userdata)
{
	ServerNameCallbackData *data = calloc(1, sizeof(ServerNameCallbackData));

	data->cb = cb;
	data->data = userdata;

	RemoteCommand_GetShardBeaconMasterServer(objCreateManagedReturnVal(gslBeaconServerNameReceiveCB, data),
												GLOBALTYPE_CONTROLLER, 
												0);
}

void gslBeaconCheckRequestServer(void)
{
	static S64 g_UpdateTime = 0;

	if(beaconRequestServerIsComplete())
	{
		static bool bPushed = false;
		if(!bPushed)
		{
			// Do checkin, report success, close
			char **files = NULL;
			bool res = true;
			char *pcError = NULL;
			bPushed = true;
			beaconRequestServerGetFilenames(&files);

			if(eaSize(&files))
				res = ServerLibPatchUpload(files, "Beaconizer", &pcError, "Updated beacons for job %s", GetCurJobNameForJobManager());
			eaDestroyEx(&files, NULL);

			gslUGC_DeleteNamespaceDataFiles(beaconRequestServerGetNamespace());

			if (res)
				JobManagerUpdate_Complete(true, true, "Beaconizing completed");
			else
				JobManagerUpdate_Complete(true, true, "Beaconizing failed - Failed to push results to ugcmaster: %s", pcError);
		}
	}
	else if(0)
	{
		// Report failure, close
		gslUGC_DeleteNamespaceDataFiles(beaconRequestServerGetNamespace());
		JobManagerUpdate_Complete(false, true, "Beaconizing failed");
	}
	else
	{
		if(ABS_TIME_SINCE(g_UpdateTime)>SEC_TO_ABS_TIME(5))
		{
			static F32 last_percent = -1;
			static char* statusStr = NULL;
			F32 percent = 0;
			g_UpdateTime = ABS_TIME;

			estrClear(&statusStr);
			percent = beaconRequestServerGetCompletion(&statusStr);

			if(percent!=last_percent)
			{
				last_percent = percent;

				JobManagerUpdate_Status(percent*100, statusStr);
			}
		}
	}
}

void gslBeaconGetInfoStructForHttp(UrlArgumentList *pUrl, ParseTable **ppTPI, void **ppStruct)
{
	static BeaconServerInfo info = {0};

	beaconServerGetServerInfo(&info);

	*ppStruct = &info;
	*ppTPI = parse_BeaconServerInfo;
}

// Information used by beaconizer to simulate player movement

static struct {
	MovementManager*							beaconMM;
	MovementRequester*							surfaceMR;
	MovementRequester*							beaconMR;
	MovementNoCollHandle*						beaconNCH;
	Vec3										mmStart;
	Vec3										mmTarget;
	S32											mmCount;
	struct{
		U32										mmEnabled					: 1;
		U32										mmDoMove					: 1;
		U32										mmMoveDone					: 1;
		U32										mmReached					: 1;
	} flags;
} beaconMovement;

void mmBeaconControllerSetPosTargetSteps( int iPartitionIdx,
										  const Vec3 pos,
										  const Vec3 target,
										  S32 count)
{
	if(!beaconMovement.beaconMM){
		mmCreate(&beaconMovement.beaconMM, NULL, NULL, 0, 0, pos, worldGetActiveColl(iPartitionIdx));

		mrSurfaceCreate(beaconMovement.beaconMM, &beaconMovement.surfaceMR);
		beaconMovementCreate(&beaconMovement.beaconMR, beaconMovement.beaconMM);
		mmNoCollHandleCreateFG(beaconMovement.beaconMM, &beaconMovement.beaconNCH, __FILE__, __LINE__);

		// Slow down to get false positives
		{
			F32 speed = mrSurfaceGetSpeed(beaconMovement.surfaceMR);
			mrSurfaceSetSpeed(beaconMovement.surfaceMR, MR_SURFACE_SPEED_FAST, speed*0.8);
			mrSurfaceSetSpeed(beaconMovement.surfaceMR, MR_SURFACE_SPEED_MEDIUM, speed*0.5);
			mrSurfaceSetSpeed(beaconMovement.surfaceMR, MR_SURFACE_SPEED_SLOW, speed*0.3);
		}
	}
	mmSetWorldColl(beaconMovement.beaconMM, worldGetActiveColl(iPartitionIdx));

	beaconMovement.mmCount = count;
	copyVec3(pos, beaconMovement.mmStart);
	copyVec3(target, beaconMovement.mmTarget);

	mmSetPositionFG(beaconMovement.beaconMM, beaconMovement.mmStart, __FUNCTION__);
	beaconMovementSetTarget(beaconMovement.beaconMR, beaconMovement.mmTarget);
	beaconMovementSetCount(beaconMovement.beaconMR, count);

	beaconMovement.flags.mmDoMove = 1;
	beaconMovement.flags.mmEnabled = 1;
	beaconMovement.flags.mmReached = 0;
}

void mmBeaconControllerGetResults(	S32* result,
									S32* optional,
									F32* dist)
{
	*result = beaconMovementReachedTarget(beaconMovement.beaconMR);
	*optional = beaconMovementFailedOptionalTest(beaconMovement.beaconMR);
	*dist = 0;
}

S32 mmBeaconControllerReachedTarget(void)
{
	assert(beaconMovement.flags.mmEnabled);

	return beaconMovementFinished(beaconMovement.beaconMR);
}

void gslBeaconXPathReturnCB(U32 iReqID1, U32 iReqID2, StructInfoForHttpXpath *pStructInfo)
{
	HttpServing_XPathReturn(iReqID1, pStructInfo);
}

//typedef void HttpServingXPathCallback(GlobalType eContainerType, 
//										ContainerID iContainerID, 
//										int iRequestID, 
//										char *pXPath,
//										UrlArgument **ppServerSideURLArgs, 
//										int iAccessLevel);

void gslBeaconXPathCB(GlobalType eContainerType, 
					  ContainerID iContainerID, 
					  int iRequestID, 
					  char *pXPath,
					  UrlArgument **ppServerSideURLArgs, 
					  int iAccessLevel, 
					  GetHttpFlags eFlags, const char *pAuthNameAndIP)
{
	UrlArgumentList url = {0};
	UrlArgumentList *pUrlListToUse;
	char *pStruct = NULL;
	ParseTable *pTPI;

	url.pBaseURL = pXPath;
	url.ppUrlArgList = ppServerSideURLArgs;

	pUrlListToUse = StructClone(parse_UrlArgumentList, &url);

	gslBeaconGetInfoStructForHttp(&url, &pTPI, &pStruct);

	if (pStruct)
	{
		StructInfoForHttpXpath structInfo = {0};

		if (ProcessStructIntoStructInfoForHttp(url.pBaseURL + strlen(CUSTOM_DOMAIN_NAME), &url, pStruct, pTPI, iAccessLevel, 0, &structInfo, 0))
		{
			gslBeaconXPathReturnCB(iRequestID, 0, &structInfo);
			StructDeInit(parse_StructInfoForHttpXpath, &structInfo);
			return;
		}
	}

	StructDestroy(parse_UrlArgumentList, pUrlListToUse);

	GenericHttpServer_Activity();
}

void gslBeaconServerRun(void)
{
	frameLockedTimerCreate(&gGSLState.flt, 3000, 3000 / 60);

	beaconSetMMMovementCallbacks(	mmBeaconControllerSetPosTargetSteps,
									mmBeaconControllerReachedTarget,
									mmBeaconControllerGetResults);

	beaconSetPCLCallbacks(	pclConnectAndCreate,
							pclDisconnectAndDestroy,
							pclForceInFiles,
							pclSetViewLatest,
							pclSetDefaultView,
							pclGetAllFiles,
							pclProcess,
							pclNeedsRestart,
							pclCheckCurrentView,
							pclSetProcessCallback,
							pclSetUploadCallback,
							pclGetCurrentBranch);

	beaconSetGetServerListCallback(gslBeaconGetServerList);
	beaconSetGetServerNameCallback(gslBeaconGetServerName);

	beaconSetWCICallbacks(mmCreateWorldCollIntegration);
	mmSetIsServer();

	if(ShardInfoStringWasSet())
	{
		beaconPrintf(COLOR_GREEN, "BeaconServer is in SHARD.\n");
		beacon_common.isSharded = 1;
	}

	beaconizerStartup();

	if(beaconDoShardStuff())
	{
		while (!InitObjectTransactionManager(	GetAppGlobalType(),
			gServerLibState.containerID,
			gServerLibState.transactionServerHost,
			gServerLibState.transactionServerPort,
			gServerLibState.bUseMultiplexerForTransactions, 
			NULL)) 
		{
			Sleep(1000);
		}
	}

	if(beaconDoShardStuff())
		AttemptToConnectToController(0, NULL, 0);

	if(!beaconIsClient() && !beaconIsSharded())
	{
		int port = 80;
		if(beaconIsMasterServer())
			port = 8081;
		while(!GenericHttpServing_BeginCBs(port, NULL, NULL, gslBeaconXPathCB, NULL, NULL, 1))
			port += 1;
	}

	gbHttpServingAllowCommandsInURL = true;
	
	while(1)
	{
		static U32 lastTime = 0;
		U32 curTime = 0;
		U32 elapsed = 0;

		autoTimerThreadFrameBegin("Beaconizer");

		curTime = timeGetTime();

		commMonitor(commDefault());

		if(beaconDoShardStuff())
		{
			UpdateControllerConnection();
			UpdateObjectTransactionManager();
		}

		if(beaconIsRequestServer() && GetCurJobNameForJobManager())
		{
			gslBeaconCheckRequestServer();
		}

		beaconizerRun();

		GenericHttpServing_Tick();
		utilitiesLibUpdateAbsTime(1.0f);

		if(lastTime == 0)
			elapsed = 0;
		else
			elapsed = curTime - lastTime;
		TimedCallback_Tick(elapsed, 1);
		lastTime = curTime;

		autoTimerThreadFrameEnd();
	}
} 

static void beaconGetStatusReturnCallback(TransactionReturnVal *returnVal, char *pJobName)
{
	JobManagerGroupResult *pResult = NULL;
	enumTransactionOutcome eOutcome = RemoteCommandCheck_RequestJobGroupStatus(returnVal, &pResult);

	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		char *pStatusString = NULL;
		ParserWriteText(&pStatusString, parse_JobManagerGroupResult, pResult, 0, 0, 0);
		printf("Status for %s:\n%s\n\n", pJobName, pStatusString);
		estrDestroy(&pStatusString);
		StructDestroy(parse_JobManagerGroupResult, pResult);
	}
}

void beaconGetJobStatusCB(TimedCallback *callback, F32 timeSinceLastCallback, char *pName)
{
	RemoteCommand_RequestJobGroupStatus(objCreateManagedReturnVal(beaconGetStatusReturnCallback, pName), 
		GLOBALTYPE_JOBMANAGER, 0, pName);
}

AUTO_RUN;
void beaconAutoRun(void)
{
	beaconSetPartitionCallbacks(partition_ExecuteOnEachPartition);
}

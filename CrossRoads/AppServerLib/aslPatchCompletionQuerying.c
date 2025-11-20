#include "aslPatchCompletionQuerying.h"
#include "pcl_Client.h"
#include "aslPatching.h"
#include "globalTypes.h"
#include "mapDescription.h"
#include "mapDescription_h_ast.h"
#include "scratchStack.h"
#include "alerts.h"
#include "error.h"
#include "aslPatchCompletionQuerying_c_ast.h"
#include "timing.h"
#include "file.h"

static bool sbConnected = false;
static bool sbViewSet = false;
static bool sbQueryInProgress = false;
static int sSecondsBetweenQueries = 0;
static bool sbNeedReconnection = false;
static bool sbInitted = false;

static PCL_Client *spClient = NULL;
static DynamicPatchInfo *spPatchInfo = NULL;

AUTO_COMMAND;
void ForceRestartCompletionQuerying(void)
{
	sbNeedReconnection = true;
}

AUTO_STRUCT;
typedef struct PatchCompletionQueryingNS
{
	char *pNameSpaceName;
	PatchCompletionQueryingCB pSuccessCB; NO_AST
	void *pUserData; NO_AST
	U32 iLastQueryTime;
} PatchCompletionQueryingNS;
	
PatchCompletionQueryingNS **ppNameSpacesToQuery = NULL;

#define PCL_ALERT_ERROR()																\
{																						\
	if(error != PCL_SUCCESS)															\
	{																					\
		char *msg = ScratchAlloc(MAX_PATH);												\
		pclGetErrorString(error, msg, MAX_PATH);										\
		strcat_s(msg + strlen(msg), MAX_PATH - strlen(msg), ", State: ");				\
		pclGetStateString(spClient, msg + strlen(msg), MAX_PATH - strlen(msg));			\
		AssertOrAlert("PCL_QUERYING_ERROR", "PCL error: %s%s%s. Client: %s",			\
			msg,																		\
			error_details ? ": " : "",													\
			NULL_TO_EMPTY(error_details),												\
			spClient ? pclGetUsefulDebugString_Static(spClient) : "none");				\
		ScratchFree(msg);																\
	}																					\
}

void aslPatchViewSetCB(PCL_Client * client, PCL_ErrorCode error, const char * error_details, void * userData)
{
	if(error != PCL_SUCCESS)															\
	{
		PCL_ALERT_ERROR();
		sbNeedReconnection = true;
	}
	else
	{
		sbViewSet = true;
	}
}
void aslPatchConnectionCB(PCL_Client * client, bool updated, PCL_ErrorCode error, const char * error_details, void * userData)
{
	if(error != PCL_SUCCESS)															\
	{
		PCL_ALERT_ERROR();
		sbNeedReconnection = true;
	}
	else
	{
		sbConnected = true;
		sbQueryInProgress = false;
		sbViewSet = false;

		pclSetNamedView(client, spPatchInfo->pResourceProject, spPatchInfo->pViewName, true, false, aslPatchViewSetCB, NULL);
	}
}

void aslPatchCompletionQuerying_Begin(const char *pPatchingNameSpace, int iSecondsBetweenQueryPerNamespace)
{
	spPatchInfo = StructCreate(parse_DynamicPatchInfo);
	assertmsgf(aslFillInPatchInfo(spPatchInfo, pPatchingNameSpace, 0), "Can't call aslPatchCompletionQuerying_Begin for %s", pPatchingNameSpace);

	assertmsgf(!sbInitted, "Can't call aslPatchCompletionQuerying_Begin twice");
	sbInitted = true;
	sSecondsBetweenQueries = iSecondsBetweenQueryPerNamespace;

	pclConnectAndCreate(&spClient, spPatchInfo->pServer, spPatchInfo->iPort, 60, commDefault(), "", "aslPatching", "", aslPatchConnectionCB, NULL);
	pclSetBadFilesDirectory(spClient, fileTempDir());
}

void aslPatchCompletionQuerying_InternalCB(PCL_Client* client, bool synced, bool exists, void *userdata)
{
	PatchCompletionQueryingNS *pNameSpace = (PatchCompletionQueryingNS *)userdata;
	sbQueryInProgress = false;

	if (!exists) {
		AssertOrAlert("UGC_SYNC_ERROR", "Was waiting for sync of namespace %s, but it does not even exist.  %s",
					  pNameSpace->pNameSpaceName, pclGetUsefulDebugString_Static( client ));
	}

	if (synced)
	{
		eaFindAndRemove(&ppNameSpacesToQuery, pNameSpace);

		pNameSpace->pSuccessCB(pNameSpace->pUserData);
		StructDestroy(parse_PatchCompletionQueryingNS, pNameSpace);
		
	}
}


void aslPatchCompletionQuerying_Update(void)
{
	PCL_ErrorCode error;
	char *error_details = NULL;
	
	if (!sbInitted)
	{
		return;
	}

	if (sbNeedReconnection || !spClient)
	{
		sbNeedReconnection = false;
		sbConnected = false;

		if (spClient)
		{
			error = pclDisconnectAndDestroy(spClient);
			if (error != PCL_SUCCESS)
				pclGetErrorDetails(spClient, &error_details);
			PCL_ALERT_ERROR();
			spClient = NULL;
		}

		pclConnectAndCreate(&spClient, spPatchInfo->pServer, spPatchInfo->iPort, 60, commDefault(), "", "aslPatching", "", aslPatchConnectionCB, NULL);
		pclSetBadFilesDirectory(spClient, fileTempDir());
		return;
	}

	error = pclProcess(spClient);
	if (error == PCL_WAITING)
	{
		return;
	}

	if (error != PCL_SUCCESS)
	{
		if (error != PCL_LOST_CONNECTION)
		{
			pclGetErrorDetails(spClient, &error_details);
			PCL_ALERT_ERROR();
		}

		sbNeedReconnection = true;
		return;
	}

	if (sbConnected && sbViewSet && !sbQueryInProgress && eaSize(&ppNameSpacesToQuery))
	{
		PatchCompletionQueryingNS *pNameSpace = eaRemove(&ppNameSpacesToQuery, 0);
		eaPush(&ppNameSpacesToQuery, pNameSpace);

		if (pNameSpace->iLastQueryTime < timeSecondsSince2000() - sSecondsBetweenQueries)
		{
			
			error = pclIsCompletelySynced(spClient, pNameSpace->pNameSpaceName, aslPatchCompletionQuerying_InternalCB, pNameSpace);
			pNameSpace->iLastQueryTime = timeSecondsSince2000();

			if (error != PCL_SUCCESS)
				pclGetErrorDetails(spClient, &error_details);
			PCL_ALERT_ERROR();

			if (error == PCL_SUCCESS)
			{
				sbQueryInProgress = true;
			}
		}
	}
}

void aslPatchCompletionQuerying_Query(char *pNameSpace, PatchCompletionQueryingCB pSuccessCB, void *pUserData)
{
	PatchCompletionQueryingNS *pNS = StructCreate(parse_PatchCompletionQueryingNS);

	if (!sbInitted)
	{
		AssertOrAlert("PATCH_COMPLETION_QUERYING_NOT_ACTIVE", "Someone is querying patch completion when patch completion querying is not active");
	}

	pNS->pNameSpaceName = strdupf("data/ns/%s", pNameSpace);
	pNS->pSuccessCB = pSuccessCB;
	pNS->pUserData = pUserData;

	eaPush(&ppNameSpacesToQuery, pNS);
}

#include "aslPatchCompletionQuerying_c_ast.c"

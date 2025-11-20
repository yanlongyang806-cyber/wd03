#include "Controller_ClusterController.h"
#include "textparser.h"
#include "earray.h"
#include "EString.h"
#include "net.h"
#include "Controller.h"
#include "CrypticPorts.h"
#include "structNet.h"
#include "timing.h"
#include "ControllerPub_h_ast.h"
#include "ServerLib.h"
#include "utilitiesLib.h"
#include "Controller.h"
#include "StringCache.h"
#include "cmdparse.h"
#include "alerts.h"
#include "Controller_ShardCluster.h"
#include "ShardCommon.h"
#include "logging.h"
#include "Controller_AutoSettings.h"
#include "Controller_ClusterController_c_ast.h"
#include "objTransactions.h"
#include "autogen/serverLib_Autogen_RemoteFuncs.h"
#include "ShardVariableCommon_h_ast.h"
#include "TimedCallback.h"
#include "../../CrossRoads/Common/ShardVariableCommon.h"
#include "Controller_Utils.h"

//if this is set, it's the name of the cluster controller machine, and the system is "on", otherwise it's off
static char sClusterControllerName_DontUse[64] = "";

static int siSummaryInterval = 5;
AUTO_CMD_INT(siSummaryInterval, ClusterControllerSummaryInterval);

static CommConnectFSM *spClusterControllerConnectFSM = NULL;
static NetLink *spClusterControllerNetLink = NULL;

static U32 siLastTimeSentSummary = 0;

static int siNumPlayers = 0;

//dummy AUTO_COMMAND
AUTO_COMMAND;
void ClusterController(char *pStr)
{
}

char *Controller_GetClusterControllerName(void)
{
	ONCE( ParseCommandOutOfCommandLine( "ClusterController", sClusterControllerName_DontUse ));

	return sClusterControllerName_DontUse[0] ? sClusterControllerName_DontUse : NULL;
}

void Controller_ClusterControllerHereIsNumPlayers(int iNumPlayers)
{
	siNumPlayers = iNumPlayers;
}

ControllerSummaryForClusterController *ClusterControllerGetSummary(void)
{
	static ControllerSummaryForClusterController *spSummary = NULL;

	if (!spSummary)
	{
		spSummary = StructCreate(parse_ControllerSummaryForClusterController);
		spSummary->pShardNameForSummary = allocAddString(GetShardNameFromShardInfoString());
		spSummary->pPatchDir = strdup(gExecutableDirectory);
			spSummary->pProductName = strdup(GetProductName());
		spSummary->pClusterName = ShardCommon_GetClusterName();
	}

	spSummary->iNumPlayers = siNumPlayers;
	spSummary->iNumGameServers = giTotalNumServersByType[GLOBALTYPE_GAMESERVER];

	spSummary->pOverview = GetControllerOverview(false, NULL);

	if (!spSummary->pInterestingStuff)
	{
		spSummary->pInterestingStuff = StructCreate(parse_ControllerInterestingStuff);
	}

	StructCopy(parse_ControllerInterestingStuff, GetInterestingStuff(), spSummary->pInterestingStuff, 0, 0, 0);

	spSummary->pStartupStatus = Controller_GetStartupStatusString();

	GetShardLockageString(&spSummary->pLocked);

	return spSummary;
}

void ClusterControllerSendSummary(void)
{
	ControllerSummaryForClusterController *pSummary = ClusterControllerGetSummary();
	Packet *pPack = pktCreate(spClusterControllerNetLink, CONTROLLER_TO_CLUSTERCONTROLLER__STATUS);
	ParserSendStructSafe(parse_ControllerSummaryForClusterController, pPack, pSummary);
	pktSend(&pPack);
	siLastTimeSentSummary = timeSecondsSince2000();
}

static void ClusterControllerSlowReturnCB(ContainerID iMCPID, int iRequestID, int iClientID, char *pMessageString, void *pUserData)
{
	if (spClusterControllerNetLink)
	{
		Packet *pReturnPack = pktCreate(spClusterControllerNetLink, CONTROLLER_TO_CLUSTERCONTROLLER__COMMAND_RETURN);
	
		pktSendBits(pReturnPack, 32, iRequestID);
		pktSendBits(pReturnPack, 1, 1);
		pktSendString(pReturnPack, pMessageString);
		pktSend(&pReturnPack);
	}
}


static void HandleCommand(Packet *pak, NetLink *link)
{
	int iCmdID = pktGetBits(pak, 32);
	char *pCommandString = pktGetStringTemp(pak);
	char *pRetString = NULL;
	bool bReturnIsSlow = false;

	if (!cmdParseForClusterController(pCommandString, 9, &pRetString, &bReturnIsSlow, 0, iCmdID, 0, 
		ClusterControllerSlowReturnCB, NULL, "ClusterController"))
	{
		Packet *pReturnPack = pktCreate(link, CONTROLLER_TO_CLUSTERCONTROLLER__COMMAND_RETURN);
		pktSendBits(pReturnPack, 32, iCmdID);
		pktSendBits(pReturnPack, 1, 0);
		pktSend(&pReturnPack);
		return;
	}

	if (!bReturnIsSlow)
	{
		Packet *pReturnPack = pktCreate(link, CONTROLLER_TO_CLUSTERCONTROLLER__COMMAND_RETURN);
	
		pktSendBits(pReturnPack, 32, iCmdID);
		pktSendBits(pReturnPack, 1, 1);
		pktSendString(pReturnPack, pRetString);
		pktSend(&pReturnPack);
	}

	estrDestroy(&pRetString);
}

void HandleRequestAutoSettings(Packet *pak, NetLink *link)
{
	Packet *pReturnPak = pktCreate(link, CONTROLLER_TO_CLUSTERCONTROLLER__HERE_ARE_AUTO_SETTINGS);
	FOR_EACH_IN_STASHTABLE(gControllerAutoSettingCategories, ControllerAutoSetting_Category, pCategory)
	{
		pktSendBits(pReturnPak, 1, 1);
		ParserSendStructSafe(parse_ControllerAutoSetting_Category, pReturnPak, pCategory);
	}
	FOR_EACH_END;
	pktSendBits(pReturnPak, 1, 0);
	pktSend(&pReturnPak);
}
	
ShardVariableForClusterController_List *MakeShardVariableList(ShardVariableContainer *pContainer)
{
	const char ***pppShardVariableNames = shardvariable_GetShardVariableNames();
	ShardVariableForClusterController_List *pList = StructCreate(parse_ShardVariableForClusterController_List);
	int i;

	for (i=0; i < eaSize(pppShardVariableNames); i++)
	{
		ShardVariableForClusterController *pVariable = StructCreate(parse_ShardVariableForClusterController);
		WorldVariableContainer *pWVContainer;
		const WorldVariable *pDefaultWV;
		
		pVariable->pName = allocAddString((*pppShardVariableNames)[i]);
		


	
		pDefaultWV = shardvariable_GetDefaultValue(pVariable->pName);
		if (pDefaultWV)
		{
			worldVariableToEString(pDefaultWV, &pVariable->pDefaultValueString);
		}
		else
		{
			estrPrintf(&pVariable->pDefaultValueString, SHARDVARIABLEFORCLUSTERCONTROLLER_BAD_DEFAULT_VALUE);
		}

		pWVContainer = eaIndexedGetUsingString(&pContainer->eaWorldVars, pVariable->pName);
		if (pWVContainer)
		{
			WorldVariable *pWV;
			pWV = StructCreate(parse_WorldVariable);
			worldVariableCopyFromContainer(pWV, pWVContainer);
			worldVariableToEString(pWV, &pVariable->pValueString);
			StructDestroy(parse_WorldVariable, pWV);
		}
		else
		{
			estrCopy2(&pVariable->pValueString, pVariable->pDefaultValueString);
		}

		eaPush(&pList->ppVariables, pVariable);
	}

	return pList;

}

void RequestShardVariablesCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	bool bAlreadyWaiting;
	ShardVariableContainer *pContainer = Controller_GetShardVariableContainer(&bAlreadyWaiting);
	if (pContainer)
	{

		if (spClusterControllerNetLink)
		{
			ShardVariableForClusterController_List *pShardVarList = MakeShardVariableList(pContainer);
			Packet *pOutPack = pktCreate(spClusterControllerNetLink, CONTROLLER_TO_CLUSTERCONTROLLER__HERE_ARE_SHARD_VARIABLES);
			ParserSendStructSafe(parse_ShardVariableForClusterController_List, pOutPack, pShardVarList);
			pktSend(&pOutPack);
			StructDestroy(parse_ShardVariableForClusterController_List, pShardVarList);
		}
	}
	else
	{
		int iRetryCount = (intptr_t)userData;
		iRetryCount--;
		if (iRetryCount)
		{
			TimedCallback_Run(RequestShardVariablesCB, (void*)((intptr_t)5), 1.0f);
		}
	}


}


//when you call Controller_GetShardVariableContainer, it actually sets up a subscription request, so will not 
//return anything the very first time it's called. So just put in a delayed retry
void HandleRequestShardVariables(NetLink *link)
{
	bool bAlreadyWaiting;
	ShardVariableContainer *pContainer = Controller_GetShardVariableContainer(&bAlreadyWaiting);
	if (pContainer)
	{
		ShardVariableForClusterController_List *pShardVarList = MakeShardVariableList(pContainer);
		Packet *pOutPack = pktCreate(link, CONTROLLER_TO_CLUSTERCONTROLLER__HERE_ARE_SHARD_VARIABLES);
		ParserSendStructSafe(parse_ShardVariableForClusterController_List, pOutPack, pShardVarList);
		pktSend(&pOutPack);
		StructDestroy(parse_ShardVariableForClusterController_List, pShardVarList);
	}
	else
	{
		TimedCallback_Run(RequestShardVariablesCB, (void*)((intptr_t)5), 1.0f);
	}
}

static void ClusterControllerMessageCB(Packet *pak,int cmd, NetLink *link, void *pUserData)
{
	if (linkFileSendingMode_ReceiveHelper(link, cmd, pak)) return;

	switch (cmd)
	{
	xcase CLUSTERCONTROLLER_TO_CONTROLLER__COMMAND:
		HandleCommand(pak, link);

	xcase CLUSTERCONTROLLER_TO_CONTROLLER__KILLYOURSELF:
		log_printf(LOG_SHARD, "Shutting down because ClusterController told us to die");
		exit(0);

	xcase CLUSTERCONTROLLER_TO_CONTROLLER__REQUEST_AUTO_SETTINGS:
		HandleRequestAutoSettings(pak, link);

	xcase CLUSTERCONTROLLER_TO_CONTROLLER__REQUEST_SHARD_VARIABLES:
		HandleRequestShardVariables(link);
	}	
}

static void ClusterController_ReceivingErrorCB(char *pError)
{
	CRITICAL_NETOPS_ALERT("FILE_RECV_ERROR", "Got an error while receiving a file from the ClusterController: %s",
		pError);
}

void ClusterControllerFileRecvCB(int iCmd, char *pFileName)
{
	printf("Received file %s from ClusterController\n", pFileName);
}

static void ClusterControllerConnectCB(NetLink* link,void *user_data)
{
	linkFileSendingMode_InitReceiving(link, ClusterController_ReceivingErrorCB);
	linkFileSendingMode_RegisterCallback(link, CLUSTERCONTROLLER_TO_CONTROLLER__SENDFILE, 
		"", ClusterControllerFileRecvCB);

}

void Controller_ClusterControllerTick(void)
{
	static bool sbConnected = false;

	if (!Controller_GetClusterControllerName())
	{
		return;
	}

	if (commConnectFSMForTickFunctionWithRetrying(&spClusterControllerConnectFSM, &spClusterControllerNetLink, 
		"link to cluster controller",
			2.0f, comm_controller, LINKTYPE_SHARD_CRITICAL_20MEG, LINK_FORCE_FLUSH,
			Controller_GetClusterControllerName(), CLUSTERCONTROLLER_PORT, ClusterControllerMessageCB,ClusterControllerConnectCB,0,0, NULL, 0, NULL, 0))
	{


		if (!sbConnected)
		{
			sbConnected = true;
			ClusterControllerSendSummary();
		}

		if (siLastTimeSentSummary < timeSecondsSince2000() - siSummaryInterval)
		{
			ClusterControllerSendSummary();
		}
	}
	else
	{
		sbConnected = false;
	}

}

AUTO_STRUCT;
typedef struct ClusterControllerToServerCommandCache
{
	int iKey; AST(KEY)
	U32 iSentTime;
	char *pCommandString;
	CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo; NO_AST
} ClusterControllerToServerCommandCache;

static StashTable sClusterControllerToServerCommandCaches = NULL;

AUTO_FIXUPFUNC;
TextParserResult ClusterControllerToServerCommandCacheFixupFunc(ClusterControllerToServerCommandCache *pCache, enumTextParserFixupType eFixupType, void *pExtraData)
{
	switch (eFixupType)
	{
		case FIXUPTYPE_DESTRUCTOR:
			SAFE_FREE(pCache->pSlowReturnInfo);
			break;
	}

	return true;
}


AUTO_COMMAND;
char *SendClusterControllerCommandToServer(CmdContext *pContext, char *pServerTypeName, ACMD_SENTENCE pCommandString)
{
	GlobalType eType = NameToGlobalType(pServerTypeName);
	TrackedServerState *pServer;
	static int siNextID = 1;
	ClusterControllerToServerCommandCache *pCommandCache = NULL;

	if (!eType)
	{
		return "Unknown server type";
	}

	if (!gpServersByType[eType])
	{
		return "No server of that type exists";
	}

	pServer = gpServersByType[eType];

	if (!sClusterControllerToServerCommandCaches)
	{
		sClusterControllerToServerCommandCaches = stashTableCreateInt(16);
	}

	pCommandCache = StructCreate(parse_ClusterControllerToServerCommandCache);
	pCommandCache->iKey = siNextID++;
	pCommandCache->iSentTime = timeSecondsSince2000();
	pCommandCache->pCommandString = strdup(pCommandString);

	pCommandCache->pSlowReturnInfo = calloc(sizeof(CmdSlowReturnForServerMonitorInfo), 1);
	memcpy(pCommandCache->pSlowReturnInfo, &pContext->slowReturnInfo, sizeof(CmdSlowReturnForServerMonitorInfo));
	pContext->slowReturnInfo.bDoingSlowReturn = true;
	stashIntAddPointer(sClusterControllerToServerCommandCaches, pCommandCache->iKey, pCommandCache, true);


	if (pServer->pLink)
	{
		Packet *pPak = pktCreate(pServer->pLink, FROM_CONTROLLER_REQUESTING_MONITORING_COMMAND);
		pktSendBits(pPak, 32, 0);
		pktSendBits(pPak, 32, pCommandCache->iKey);
		PutContainerIDIntoPacket(pPak, 0);
		pktSendString(pPak, pCommandString);
		pktSendString(pPak, "ClusterController");
		pktSendBits(pPak, 32, 9);
		pktSendBits(pPak, 1, 0);
		pktSend(&pPak);
	}
	else if (objLocalManager())
	{
		RemoteCommand_CallLocalCommandRemotelyAndReturnVerboseHtmlString(eType, pServer->iContainerID, 
			0, pCommandCache->iKey, 0, pCommandString, 9, 0, "ClusterController");
	}
	else
	{
		//leak a command cache here... who cares, this will never happen
		pContext->slowReturnInfo.bDoingSlowReturn = false;
		return "Unable to issue command for some reason";
	}
	


	return "fake";
}


void Controller_ClusterControllerHandleCommandReturnFromServer(int iRequestID, char *pMessageString)
{
	ClusterControllerToServerCommandCache *pCache;

	if (stashIntRemovePointer(sClusterControllerToServerCommandCaches, iRequestID, &pCache))
	{
		DoSlowCmdReturn(1, pMessageString, pCache->pSlowReturnInfo);
		StructDestroy(parse_ClusterControllerToServerCommandCache, pCache);
	}
}



#include "Controller_ClusterController_c_ast.c"
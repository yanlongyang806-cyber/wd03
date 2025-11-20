#include "Controller_DynHoggPruning.h"
#include "ControllerPub.h"
#include "earray.h"
#include "autogen\Controller_DynHoggPruning_c_ast.h"
#include "logging.h"
#include "timing.h"
#include "alerts.h"
#include "resourceInfo.h"
#include "serverlib.h"
#include "Controller.h"
#include "net.h"
#include "error.h"

AUTO_STRUCT;
typedef struct DynHoggPruningNamespaceTracker
{
	char *pNameSpace; AST(KEY)
	U32 iMostRecentTime; AST(FORMATSTRING(HTML_SECS_AGO = 1))
	bool bCurrentlyActive; NO_AST //used internally during sending-to-launcher stuff, don't refer to this otherwise
} DynHoggPruningNamespaceTracker;

AUTO_STRUCT;
typedef struct DynHoggPruningState
{
	bool bActive;
	bool bRequested;
	bool bComplete;
	char *pErrorString; AST(ESTRING) //if set, there was an error of some sort during pruning

	U32 iTimeBegan;
	U32 iTimeSendRequestToLauncher; 
	bool bAlertedLongPruning;

	DynHoggPruningNamespaceTracker **ppNameSpaceTrackers;
} DynHoggPruningState;

bool gbDoDynHoggPruning = false;
AUTO_CMD_INT(gbDoDynHoggPruning, DoDynHoggPruning);

int gbMaxNamespacesPerMachineBeforeDynHoggPruning = 800;
AUTO_CMD_INT(gbMaxNamespacesPerMachineBeforeDynHoggPruning, MaxNamespacesPerMachineBeforeDynHoggPruning);

int gbNamespacesPerMachineToPruneDownTo = 600;
AUTO_CMD_INT(gbNamespacesPerMachineToPruneDownTo, NamespacesPerMachineToPruneDownTo);



static int siMaxSimultaneousMachinesForDynHoggPruning = 1;
AUTO_CMD_INT(siMaxSimultaneousMachinesForDynHoggPruning, MaxSimultaneousMachinesForDynHoggPruning);

static U32 siDynHoggPruning_MaxExpectedTimeForGameServersToAllBeRunning = 480;
AUTO_CMD_INT(siDynHoggPruning_MaxExpectedTimeForGameServersToAllBeRunning, DynHoggPruning_MaxExpectedTimeForGameServersToAllBeRunning);

static U32 siMaxSecondsPruningBeforeAlert = 600;
AUTO_CMD_INT(siMaxSecondsPruningBeforeAlert, MaxSecondsPruningBeforeAlert);



static TrackedMachineState **sppRequestedMachines = NULL;
static TrackedMachineState **sppActiveMachines = NULL;

void AddNamespaceForDynHoggPruning(TrackedMachineState *pMachine, char *pNameSpace)
{
	DynHoggPruningNamespaceTracker *pTracker;

	if (!pMachine->pDynHoggPruningState)
	{
		pMachine->pDynHoggPruningState = StructCreate(parse_DynHoggPruningState);
	}

	if ((pTracker = eaIndexedGetUsingString(&pMachine->pDynHoggPruningState->ppNameSpaceTrackers, pNameSpace)))
	{
		pTracker->iMostRecentTime = timeSecondsSince2000();
	}
	else
	{
		pTracker = StructCreate(parse_DynHoggPruningNamespaceTracker);
		pTracker->pNameSpace = strdup(pNameSpace);
		pTracker->iMostRecentTime = timeSecondsSince2000();
		eaPush(&pMachine->pDynHoggPruningState->ppNameSpaceTrackers, pTracker);
		if (eaSize(&pMachine->pDynHoggPruningState->ppNameSpaceTrackers) >= gbMaxNamespacesPerMachineBeforeDynHoggPruning && !pMachine->pDynHoggPruningState->bRequested)
		{
			pMachine->pDynHoggPruningState->bRequested = true;
			eaPush(&sppRequestedMachines, pMachine);
		}
	}
}



static void ResetMachine(TrackedMachineState *pMachine)
{
	pMachine->pDynHoggPruningState->bActive = false;
	pMachine->pDynHoggPruningState->bAlertedLongPruning = false;
	pMachine->pDynHoggPruningState->bComplete = false;
	pMachine->pDynHoggPruningState->bRequested = false;
	pMachine->pDynHoggPruningState->iTimeBegan = 0;
	pMachine->pDynHoggPruningState->iTimeSendRequestToLauncher = 0;
	estrDestroy(&pMachine->pDynHoggPruningState->pErrorString);	
}

//want to end up with newest/most high priority ones first
static int SortTrackers(const DynHoggPruningNamespaceTracker **ppTracker1, const DynHoggPruningNamespaceTracker **ppTracker2)
{
	if ((*ppTracker1)->bCurrentlyActive)
	{
		return -1;
	}

	if ((*ppTracker2)->bCurrentlyActive)
	{
		return 1;
	}

	if ((*ppTracker1)->iMostRecentTime > (*ppTracker2)->iMostRecentTime)
	{
		return -1;
	}

	if ((*ppTracker1)->iMostRecentTime < (*ppTracker2)->iMostRecentTime)
	{
		return 1;
	}

	return 0;
}


static void SendPruningRequestToLauncher(TrackedMachineState *pMachine)
{
	int i;
	DynHoggPruningState *pState = pMachine->pDynHoggPruningState;
	TrackedServerState *pServer;
	DynHoggPruningNamespaceTracker **ppTrackersForSorting = NULL; //note that this is NOT keyed
	Packet *pPak;

	for (i=0; i < eaSize(&pState->ppNameSpaceTrackers); i++)
	{
		pState->ppNameSpaceTrackers[i]->bCurrentlyActive = false;
	}

	pServer = pMachine->pServersByType[GLOBALTYPE_GAMESERVER];

	while (pServer && pServer->pMachine == pMachine)
	{
		char ns[RESOURCE_NAME_MAX_SIZE], base[RESOURCE_NAME_MAX_SIZE];

		if (resExtractNameSpace(pServer->pGameServerSpecificInfo->mapName, ns, base))
		{
			DynHoggPruningNamespaceTracker *pTracker = eaIndexedGetUsingString(&pState->ppNameSpaceTrackers, ns);
			if (pTracker)
			{
				pTracker->bCurrentlyActive = true;
			}
			else
			{
				pTracker = StructCreate(parse_DynHoggPruningNamespaceTracker);
				pTracker->pNameSpace = strdup(ns);
				pTracker->bCurrentlyActive = true;
				//don't really care what the timestamp says as it's irrelevant
				eaPush(&pState->ppNameSpaceTrackers, pTracker);
			}
		}

		pServer = pServer->pNext;
	}

	//at this point, pState->ppNameSpaceTrackers definitely contains all currently active namespaces, and all are marked correctly

	for (i=0; i < eaSize(&pState->ppNameSpaceTrackers); i++)
	{
		eaPush(&ppTrackersForSorting, pState->ppNameSpaceTrackers[i]);
	}

	assert(ppTrackersForSorting);
	eaQSort(ppTrackersForSorting, SortTrackers);

	//the first gbNamespacesPerMachineToPruneDownTo in this list are now the ones we want to prune down to, except that we never want to prune one
	//that is still actively running
	for (i = eaSize(&ppTrackersForSorting) - 1; i >= gbNamespacesPerMachineToPruneDownTo; i--)
	{
		if (ppTrackersForSorting[i]->bCurrentlyActive)
		{
			CRITICAL_NETOPS_ALERT("TOO_MANY_ACTIVE_NS_FOR_PRUNING", "On machine %s, wanted to prune down to %d namespaces in dynamic.hogg, but %d appear to still be active at once",
				pMachine->machineName, gbNamespacesPerMachineToPruneDownTo, i);
			break;
		}

		eaRemove(&pState->ppNameSpaceTrackers, eaIndexedFindUsingString(&pState->ppNameSpaceTrackers, ppTrackersForSorting[i]->pNameSpace));
		StructDestroy(parse_DynHoggPruningNamespaceTracker, ppTrackersForSorting[i]);
		eaRemove(&ppTrackersForSorting, i);
	}

	if (!(pMachine->pServersByType[GLOBALTYPE_LAUNCHER] && pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->pLink))
	{
		CRITICAL_NETOPS_ALERT("NO_LAUNCHER_FOR_PRUNING", "Was all ready to do dynamic hogg namespace pruning on machine %s, but connection to launcher has been lost. Pruning will be skipped",
			pMachine->machineName);
		eaDestroy(&ppTrackersForSorting);

		return;
	}

	pPak = pktCreate(pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->pLink, LAUNCHERQUERY_REQUEST_DYN_HOG_PRUNING);
	for (i=0 ; i < eaSize(&ppTrackersForSorting); i++)
	{
		pktSendString(pPak, ppTrackersForSorting[i]->pNameSpace);
	}

	pktSendString(pPak, "");

	pktSend(&pPak);

	log_printf(LOG_UGC, "Sent %d namespaces to launcher on machine %s, instructing it to prune all others",
		eaSize(&ppTrackersForSorting), pMachine->machineName);

	eaDestroy(&ppTrackersForSorting);
}


static bool UpdateActiveMachine(TrackedMachineState *pMachine)
{
	DynHoggPruningState *pState = pMachine->pDynHoggPruningState;

	if (pState->iTimeSendRequestToLauncher)
	{
		if (!pState->bComplete)
		{
			if (!pState->bAlertedLongPruning && timeSecondsSince2000() - pState->iTimeSendRequestToLauncher > siMaxSecondsPruningBeforeAlert)
			{
				pState->bAlertedLongPruning = true;
				CRITICAL_NETOPS_ALERT("SLOW_DYNHOGG_PRUNING", "Pruning dynamic hoggs on machine %s took > %d seconds, something may be wrong", pMachine->machineName, siMaxSecondsPruningBeforeAlert);
				log_printf(LOG_UGC, "Pruning dynamic hoggs on machine %s took > %d seconds, something may be wrong", pMachine->machineName, siMaxSecondsPruningBeforeAlert);
			}
		}
		else
		{
			if (pState->pErrorString)
			{
				CRITICAL_NETOPS_ALERT("DYNHOGG_PRUNING_ERROR", "While pruning dyn hoggs on machine %s got error %s", pMachine->machineName, pState->pErrorString);
				log_printf(LOG_UGC, "While pruning dyn hoggs on machine %s got error %s", pMachine->machineName, pState->pErrorString);
			}
			else
			{
				log_printf(LOG_UGC, "Dyn hogg pruning reported successfully completed for machine %s", pMachine->machineName);
			}

			ResetMachine(pMachine);
			return true;
		}
	}
	else
	{
		TrackedServerState *pServer = pMachine->pServersByType[GLOBALTYPE_GAMESERVER];
		TrackedServerState *pServerStillLoading = NULL;

		while (pServer && pServer->pMachine == pMachine)
		{
			if (!strstri(pServer->stateString, "gslRunning"))
			{
				pServerStillLoading = pServer;
				break;
			}

			pServer = pServer->pNext;
		}

		if (pServerStillLoading && timeSecondsSince2000() - pState->iTimeBegan > siDynHoggPruning_MaxExpectedTimeForGameServersToAllBeRunning)
		{
			CRITICAL_NETOPS_ALERT("DYN_HOGG_PRUNING_TIMEOUT", "On machine %s, waited %d seconds with no new gameserers launching, but server %u is still in state %s, cancelling pruning for now",
				pMachine->machineName, siDynHoggPruning_MaxExpectedTimeForGameServersToAllBeRunning, pServerStillLoading->iContainerID, pServerStillLoading->stateString);
			ResetMachine(pMachine);
			return true;
		}

		if (!pServerStillLoading)
		{
			pState->iTimeSendRequestToLauncher = timeSecondsSince2000();
			log_printf(LOG_UGC, "All gameservers loaded and running on machine %s, sending pruning request to launcher", pMachine->machineName);
			SendPruningRequestToLauncher(pMachine);
		}

	}

	return false;
}

void DynHoggPruning_Update(void)
{
	int i;

	for (i = eaSize(&sppActiveMachines)-1; i >= 0; i--)
	{
		if (UpdateActiveMachine(sppActiveMachines[i]))
		{
			eaRemoveFast(&sppActiveMachines, i);
		}
	}

	while (eaSize(&sppActiveMachines) < siMaxSimultaneousMachinesForDynHoggPruning && eaSize(&sppRequestedMachines))
	{
		TrackedMachineState *pMachine = eaRemove(&sppRequestedMachines, 0);
		eaPush(&sppActiveMachines, pMachine);
		pMachine->pDynHoggPruningState->iTimeBegan = timeSecondsSince2000();
	}
}

bool DynHoggPruningActiveForMachine(TrackedMachineState *pMachine)
{
	return pMachine->pDynHoggPruningState && pMachine->pDynHoggPruningState->bActive;
}

void HandleDynHogPruningComment(Packet *pPak, NetLink *pLink)
{
	TrackedMachineState *pMachine = GetMachineFromNetLink(pLink);
	
	if (!(pMachine && pMachine->pDynHoggPruningState))
	{	
		AssertOrAlert("DYN_HOG_PRUNING_CORRUPTION", "Got a DynHogPruningComment for a machine that is not doing dyn hog pruning");
		return;
	}


	log_printf(LOG_UGC, "dynHogPatching comment from machine %s: %s", pMachine->machineName, pktGetStringTemp(pPak));
}
void HandleDynHogPruningSuccess(Packet *pPak, NetLink *pLink)
{
	TrackedMachineState *pMachine = GetMachineFromNetLink(pLink);
	if (!(pMachine && pMachine->pDynHoggPruningState))
	{	
		AssertOrAlert("DYN_HOG_PRUNING_CORRUPTION", "Got a DynHogPruningSuccess for a machine that is not doing dyn hog pruning");
		return;
	}

	pMachine->pDynHoggPruningState->bComplete = true;
}
void HandleDynHogPruningFailure(Packet *pPak, NetLink *pLink)
{
	TrackedMachineState *pMachine = GetMachineFromNetLink(pLink);
	if (!(pMachine && pMachine->pDynHoggPruningState))
	{	
		AssertOrAlert("DYN_HOG_PRUNING_CORRUPTION", "Got a DynHogPruningFailure for a machine that is not doing dyn hog pruning");
		return;
	}
	pMachine->pDynHoggPruningState->bComplete = true;
	estrCopy2(&pMachine->pDynHoggPruningState->pErrorString, pktGetStringTemp(pPak));

}

AUTO_COMMAND;
void DynHoggPruningTest(void)
{
	gbMaxNamespacesPerMachineBeforeDynHoggPruning = 4;
	gbNamespacesPerMachineToPruneDownTo = 2;
	AddNamespaceForDynHoggPruning(spLocalMachine, "Dyn_SC_0000_0150");
	AddNamespaceForDynHoggPruning(spLocalMachine, "Dyn_SC_0000_0151");
	AddNamespaceForDynHoggPruning(spLocalMachine, "Dyn_SC_0000_0152");
	AddNamespaceForDynHoggPruning(spLocalMachine, "Dyn_SC_0000_0155");
}


#include "autogen\Controller_DynHoggPruning_c_ast.c"

#include "globalstatemachine.h"

#include "earray.h"
#include "timing.h"
#include "utils.h"
#include "estring.h"
#include "memlog.h"
#include "net/net.h"
#include "ScratchStack.h"
#include "trivia.h"
#include "timing_profiler_interface.h"
#include "logging.h"
#include "cmdParse.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

typedef struct RequestedStateTransition
{
	//earrays of state handles
	GlobalStateHandle *pFrom;
	GlobalStateHandle *pTo;
	GlobalStateHandle *pNewTo;
} RequestedStateTransition;

typedef struct GlobalStatePerfInfo
{
	char*					name;
	PERFINFO_TYPE*			perfInfo;
} GlobalStatePerfInfo;

typedef enum GlobalStateCallbackType
{
	GSCT_ENTER_LIB,
	GSCT_ENTER_APP,
	GSCT_BEGIN_LIB,
	GSCT_BEGIN_APP,
	GSCT_END_LIB,
	GSCT_END_APP,
	GSCT_LEAVE_LIB,
	GSCT_LEAVE_APP,
	GSCT_COUNT,
} GlobalStateCallbackType;

typedef struct GlobalState
{
	//static stuff defined at creation time
	char stateName[GSM_STATENAME_MAX_LENGTH];
	GlobalStateHandle hHandle;

	GlobalStateMachineCB *pEnterStateCB_FromLib;
	GlobalStateMachineCB *pEnterStateCB_FromApp;

	GlobalStateMachineCB *pBeginFrameCB_FromLib;
	GlobalStateMachineCB *pBeginFrameCB_FromApp;

	GlobalStateMachineCB *pEndFrameCB_FromLib;
	GlobalStateMachineCB *pEndFrameCB_FromApp;

	GlobalStateMachineCB *pLeaveStateCB_FromLib;
	GlobalStateMachineCB *pLeaveStateCB_FromApp;

	bool bAddStateCalled;


	//dynamic stuff that changes each time the state becomes active
	int iFrameCountWhenActivated;
	float fTimeWhenActivated;

	RequestedStateTransition **ppTransitions; //eArray of requested transitions
	
	GlobalStatePerfInfo			perfInfo[GSCT_COUNT];
} GlobalState;

#define PERFINFO_GSCT_START(state, i)															\
	PERFINFO_BLOCK({																			\
		PerfInfoGuard* piGuard;																	\
		PERFINFO_RUN(																			\
			if(!state->perfInfo[i].name){														\
				GSM_MakePerfInfoName(state, i);													\
			}																					\
			PERFINFO_AUTO_START_STATIC_GUARD(	state->perfInfo[i].name,						\
												&pState->perfInfo[i].perfInfo,					\
												1,												\
												&piGuard);										\
		)																						\
	)
#define PERFINFO_GSCT_STOP(state, i)															\
	PERFINFO_BLOCK(																				\
		PERFINFO_AUTO_STOP_GUARD(&piGuard);														\
	})																							\

//EArray containing master list of all possible states, indexed by (handle - 1)
GlobalState **spAllStateList = NULL;

//EArrays defining the "Stack" of states
GlobalStateHandle *spPreviousStates = NULL;
GlobalStateHandle *spCurrentStates = NULL;
GlobalStateHandle *spNextStates = NULL;

static Packet **sppGSMPacketsToSendOnStateTransition = NULL;

static GlobalStateHandle shEmptyState = NULL;

int gDepthOfCurrentlyProcessingState = -1;

static GSM_GetTimeSetScaleCB *spGSMTimeSetScaleCB = NULL;
static int siGSMFrameCount = 0;
static int sGSMTimer;
static float sGSMFrameTimeBeginningOfThisFrame;
static float sGSMRawFrameTimeBeginningOfThisFrame;
static GSM_StatesChangedCB *spGSMStatesChangedCB = NULL;

static GSM_IndividualStateEnterLeaveCB *spGSMIndividualStateEnterLeaveCB = NULL;

//GSM transitions are usually allowed, but are not allowed during leave state callbacks.
static bool GSM_TransitionRequestsAllowed = true;

static bool sbGSMRunning = false;
static bool sbGSMQuitting = false;

bool GSM_IsRunning(void)
{
	return sbGSMRunning;
}

void GSM_SetIndividualStateEnterLeaveCB(GSM_IndividualStateEnterLeaveCB *pCB)
{
	assertmsg(pCB == NULL || spGSMIndividualStateEnterLeaveCB == NULL, "Overriding preexisting enterLeave CB...");
	spGSMIndividualStateEnterLeaveCB = pCB;
}



//forward declarations
void CheckForTransitionOverrides(void);

static S32 gsmPrintStateChanges;
AUTO_CMD_INT(gsmPrintStateChanges, gsmPrintStateChanges) ACMD_CMDLINE;

//if true, printf all GSM transitions
#if _PS3
int gbDumpGSMTransitions = 1;
#else
int gbDumpGSMTransitions = 0;
#endif

AUTO_CMD_INT(gbDumpGSMTransitions, DumpGSMTransitions) ACMD_CMDLINE;

//EArray of requested transitions that are not associated with any particular state
static RequestedStateTransition **sppGlobalTransitions = NULL;


static void SendStateTransitionPackets(void)
{
	int i;

	for (i=0; i < eaSize(&sppGSMPacketsToSendOnStateTransition); i++)
	{
		pktSend(&sppGSMPacketsToSendOnStateTransition[i]);
	}

	eaDestroy(&sppGSMPacketsToSendOnStateTransition);
}

void GSM_SetStatesChangedCB(GSM_StatesChangedCB *pStatesChangedCB)
{
	spGSMStatesChangedCB = pStatesChangedCB;
}


GlobalState *GlobalStateFromNameOrHandle(GlobalStateHandleOrName hHandle)
{
	int i;
	assert(hHandle);

	if ((uintptr_t)hHandle <= (uintptr_t)eaSize(&spAllStateList))
	{
		return spAllStateList[((uintptr_t)hHandle) - 1];
	}

	for (i=0; i < eaSize(&spAllStateList); i++)
	{
		if (stricmp(spAllStateList[i]->stateName, (char*)hHandle) == 0)
		{
			return spAllStateList[i];
		}
	}

	return NULL;
}

GlobalState *CreateNewGlobalState(char *pStateName)
{
	GlobalState *pState = (GlobalState*)calloc(sizeof(GlobalState), 1);
	int iCurCount;

	assertmsgf(strlen(pStateName) < GSM_STATENAME_MAX_LENGTH - 1, "State name %s is too long", pStateName);
	strcpy(pState->stateName, pStateName);

	iCurCount = eaSize(&spAllStateList);

	pState->hHandle = (void*)((intptr_t)iCurCount + 1);

	eaPush(&spAllStateList, pState);

	return pState;
}

static void GSM_MakePerfInfoName(GlobalState* state, GlobalStateCallbackType type)
{
	char		name[100];
	const char*	suffix = "";
	PERFINFO_AUTO_START_FUNC();
	assert(type >= 0 && type < GSCT_COUNT);
	switch(type){
		xcase GSCT_ENTER_LIB:suffix = "EnterLib";
		xcase GSCT_ENTER_APP:suffix = "EnterApp";
		xcase GSCT_BEGIN_LIB:suffix = "BeginLib";
		xcase GSCT_BEGIN_APP:suffix = "BeginApp";
		xcase GSCT_END_LIB:suffix = "EndLib";
		xcase GSCT_END_APP:suffix = "EndApp";
		xcase GSCT_LEAVE_LIB:suffix = "LeaveLib";
		xcase GSCT_LEAVE_APP:suffix = "LeaveApp";
	}
	sprintf(name, "%s_%s", state->stateName, suffix);
	state->perfInfo[type].name = strdup(name);
	PERFINFO_AUTO_STOP();
}

void GSM_SetTimeSetScaleCB(GSM_GetTimeSetScaleCB *pSetScaleCB)
{
	spGSMTimeSetScaleCB = pSetScaleCB;
}


GlobalStateHandle GSM_AddGlobalState(char *pStateName)
{
	GlobalState *pState = GlobalStateFromNameOrHandle(pStateName);

	if (pState)
	{
		assertmsgf(!pState->bAddStateCalled, "GSM_AddGlobalState called twice for state %s", pStateName);
		pState->bAddStateCalled = true;
		return pState->hHandle;
	}

	pState = CreateNewGlobalState(pStateName);
	pState->bAddStateCalled = true;

	return pState->hHandle;
}

void GSM_AddGlobalStateCallbacks_Internal(GlobalStateHandleOrName hState,
	GlobalStateMachineCB *pEnterStateCB,
	GlobalStateMachineCB *pBeginFrameCB,
	GlobalStateMachineCB *pEndFrameCB,
	GlobalStateMachineCB *pLeaveStateCB, bool bFromApp)
{
	GlobalState *pState = GlobalStateFromNameOrHandle(hState);

	if (!pState)
	{
		pState = CreateNewGlobalState(hState);
	}

		
		
	if (bFromApp)
	{
		if (pEnterStateCB)
		{
			assertmsgf(!pState->pEnterStateCB_FromApp, "State %s already has pEnterStateCB_FromApp", pState->stateName);
			pState->pEnterStateCB_FromApp = pEnterStateCB;
		}
		if (pBeginFrameCB)
		{
			assertmsgf(!pState->pBeginFrameCB_FromApp, "State %s already has pBeginFrameCB_FromApp", pState->stateName);
			pState->pBeginFrameCB_FromApp = pBeginFrameCB;
		}
		if (pEndFrameCB)
		{
			assertmsgf(!pState->pEndFrameCB_FromApp, "State %s already has pEndFrameCB_FromApp", pState->stateName);
			pState->pEndFrameCB_FromApp = pEndFrameCB;
		}
		if (pLeaveStateCB)
		{
			assertmsgf(!pState->pLeaveStateCB_FromApp, "State %s already has pLeaveStateCB_FromApp", pState->stateName);
			pState->pLeaveStateCB_FromApp = pLeaveStateCB;
		}
	}
	else
	{
		if (pEnterStateCB)
		{
			assertmsgf(!pState->pEnterStateCB_FromLib, "State %s already has pEnterStateCB_FromLib", pState->stateName);
			pState->pEnterStateCB_FromLib = pEnterStateCB;
		}
		if (pBeginFrameCB)
		{
			assertmsgf(!pState->pBeginFrameCB_FromLib, "State %s already has pBeginFrameCB_FromLib", pState->stateName);
			pState->pBeginFrameCB_FromLib = pBeginFrameCB;
		}
		if (pEndFrameCB)
		{
			assertmsgf(!pState->pEndFrameCB_FromLib, "State %s already has pEndFrameCB_FromLib", pState->stateName);
			pState->pEndFrameCB_FromLib = pEndFrameCB;
		}
		if (pLeaveStateCB)
		{
			assertmsgf(!pState->pLeaveStateCB_FromLib, "State %s already has pLeaveStateCB_FromLib", pState->stateName);
			pState->pLeaveStateCB_FromLib = pLeaveStateCB;
		}
	}
}

void GSM_DeactivateState(GlobalState *pState)
{
	int i;
	int iNumTransitions = eaSize(&pState->ppTransitions);

	for (i=0; i < iNumTransitions; i++)
	{
		RequestedStateTransition *pTransition = pState->ppTransitions[i];
		eaDestroy(&pTransition->pFrom);
		eaDestroy(&pTransition->pTo);
		eaDestroy(&pTransition->pNewTo);

		free(pTransition);
	}

	eaDestroy(&pState->ppTransitions);
}

static void GSM_PrintStateChange(S32 color, const char* format, ...)
{
	char buffer[200];

	VA_START(va, format);
		vsprintf(buffer, format, va);
	VA_END();
	
	printfColor(color, "%s\n", buffer);
}

#define GSM_PRINT_STATE_CHANGE(color, format, ...) {if(gsmPrintStateChanges)GSM_PrintStateChange(color, format, __VA_ARGS__);}((void)0)

void GSM_Execute(char *pStateName)
{
	int i;
	GlobalStateHandle hStartupHandle = 0;

	int iCurDepth;
	int iPrevDepth;
	int iNextDepth;
	GlobalState *pState0 = GlobalStateFromNameOrHandle(pStateName);
	char *pStateString = NULL;

	sGSMRawFrameTimeBeginningOfThisFrame = sGSMFrameTimeBeginningOfThisFrame = 0;
	siGSMFrameCount = 0;

	assertmsg(gDepthOfCurrentlyProcessingState == -1, "State machine corruption, or recursive calls to GSM_Execute");

	sbGSMRunning = true;

	for (i=0; i < eaSize(&spAllStateList); i++)
	{
		if (!spAllStateList[i]->bAddStateCalled)
		{
			assertmsgf(0, "State %s never had GSM_AddGlobalState() called... possible typo", spAllStateList[i]->stateName);
		}	
	}

	assertmsg(pState0 && pState0->hHandle, "No startup state for global state machine");

	hStartupHandle = pState0->hHandle;

	eaSetSize(&spPreviousStates, 0);
	eaSetSize(&spCurrentStates, 0);
	eaSetSize(&spNextStates, 0);

	eaPush(&spCurrentStates, hStartupHandle);
	eaPush(&spNextStates, hStartupHandle);

	sGSMTimer = timerAlloc();
	timerStart(sGSMTimer);

	

	//main loop
	while (eaSize(&spCurrentStates))
	{
	
		bool bAnyStateChanges = false;
		bool bPossibleStateChanges;
		
		autoTimerThreadFrameBegin("GSM");

		PERFINFO_AUTO_START_PIX("all states", 1);

		// Assert if there are outstanding scratch stack allocations.
		ScratchVerifyNoOutstanding();

		//update frame time
		if (spGSMTimeSetScaleCB)
		{
			float fCurRawTime = timerElapsed(sGSMTimer);
			float fDelta = fCurRawTime - sGSMRawFrameTimeBeginningOfThisFrame;
			sGSMFrameTimeBeginningOfThisFrame += fDelta * spGSMTimeSetScaleCB();

		}
		else
		{
			sGSMRawFrameTimeBeginningOfThisFrame = sGSMFrameTimeBeginningOfThisFrame = timerElapsed(sGSMTimer);
		}

		//check for which of our states are new
		iCurDepth = eaSize(&spCurrentStates);
		iPrevDepth = eaSize(&spPreviousStates);

		for (gDepthOfCurrentlyProcessingState=0; gDepthOfCurrentlyProcessingState < iCurDepth; gDepthOfCurrentlyProcessingState++)
		{
			if (gDepthOfCurrentlyProcessingState >= iPrevDepth || spCurrentStates[gDepthOfCurrentlyProcessingState] != spPreviousStates[gDepthOfCurrentlyProcessingState])
			{
				//for each new state, set its timing info, and call its EnterState callbacks

				GlobalState *pState = GlobalStateFromNameOrHandle(spCurrentStates[gDepthOfCurrentlyProcessingState]);


				pState->fTimeWhenActivated = sGSMFrameTimeBeginningOfThisFrame;
				pState->iFrameCountWhenActivated = siGSMFrameCount;

				bAnyStateChanges = true;

				if (spGSMIndividualStateEnterLeaveCB)
				{
					spGSMIndividualStateEnterLeaveCB(true, pState->stateName);
				}

				if (pState->pEnterStateCB_FromLib)
				{
					GSM_PRINT_STATE_CHANGE(COLOR_GREEN, "GSM_EnterLib: %f %s", timerGetSecondsAsFloat(), pState->stateName);
					
					PERFINFO_GSCT_START(pState, GSCT_ENTER_LIB);
					globMovementLog("[GSM] Starting state %s_EnterLib", pState->stateName);
					pState->pEnterStateCB_FromLib();
					globMovementLog("[GSM] Finished state %s_EnterLib", pState->stateName);
					PERFINFO_GSCT_STOP(pState, GSCT_ENTER_LIB);
				}

				if (pState->pEnterStateCB_FromApp)
				{
					GSM_PRINT_STATE_CHANGE(COLOR_GREEN, "GSM_EnterApp: %f %s", timerGetSecondsAsFloat(), pState->stateName);
					
					PERFINFO_GSCT_START(pState, GSCT_ENTER_APP);
					globMovementLog("[GSM] Starting state %s_EnterApp", pState->stateName);
					pState->pEnterStateCB_FromApp();
					globMovementLog("[GSM] Finished state %s_EnterApp", pState->stateName);
					PERFINFO_GSCT_STOP(pState, GSCT_ENTER_APP);
				}
			}
		}

		//now check if any state changes have occurred, and if so, call our states-changed callback
		if (bAnyStateChanges)
		{
	

			bAnyStateChanges = false;

			GSM_PutFullStateStackIntoEString(&pStateString);

			if (spGSMStatesChangedCB)
			{			
				spGSMStatesChangedCB(pStateString);
			}

			memlog_printf(NULL, "Now changed to state %s", pStateString);
			triviaPrintf("Global State", "%s", pStateString);

			if (gbDumpGSMTransitions)
			{
				printf("Now changed to state %s\n", pStateString);
#if _PS3
                {
                    extern uint32_t gcm_iframe;
                    printf("frame %08x\n", gcm_iframe);
                }
#endif
			}

			SendStateTransitionPackets();
		}

		//now update all states... begin frame first
		for (gDepthOfCurrentlyProcessingState=0; gDepthOfCurrentlyProcessingState < iCurDepth; gDepthOfCurrentlyProcessingState++)
		{
			GlobalState *pState = GlobalStateFromNameOrHandle(spCurrentStates[gDepthOfCurrentlyProcessingState]);

			if (pState->pBeginFrameCB_FromLib)
			{
				PERFINFO_GSCT_START(pState, GSCT_BEGIN_LIB);
				globMovementLog("[GSM] Starting state %s_BeginFrameLib", pState->stateName);
				pState->pBeginFrameCB_FromLib();
				globMovementLog("[GSM] Finished state %s_BeginFrameLib", pState->stateName);
				PERFINFO_GSCT_STOP(pState, GSCT_BEGIN_LIB);
			}

			if (pState->pBeginFrameCB_FromApp)
			{
				PERFINFO_GSCT_START(pState, GSCT_BEGIN_APP);
				globMovementLog("[GSM] Starting state %s_BeginFrameApp", pState->stateName);
				pState->pBeginFrameCB_FromApp();
				globMovementLog("[GSM] Finished state %s_BeginFrameApp", pState->stateName);
				PERFINFO_GSCT_STOP(pState, GSCT_BEGIN_APP);
			}
		}

		for (gDepthOfCurrentlyProcessingState = iCurDepth - 1; gDepthOfCurrentlyProcessingState >= 0; gDepthOfCurrentlyProcessingState--)
		{
			GlobalState *pState = GlobalStateFromNameOrHandle(spCurrentStates[gDepthOfCurrentlyProcessingState]);

			if (pState->pEndFrameCB_FromApp)
			{
				PERFINFO_GSCT_START(pState, GSCT_END_APP);
				globMovementLog("[GSM] Starting state %s_EndFrameApp", pState->stateName);
				pState->pEndFrameCB_FromApp();
				globMovementLog("[GSM] Finished state %s_EndFrameApp", pState->stateName);
				PERFINFO_GSCT_STOP(pState, GSCT_END_APP);
			}
	
			if (pState->pEndFrameCB_FromLib)
			{
				PERFINFO_GSCT_START(pState, GSCT_END_LIB);
				globMovementLog("[GSM] Starting state %s_EndFrameLib", pState->stateName);
				pState->pEndFrameCB_FromLib();
				globMovementLog("[GSM] Finished state %s_EndFrameLib", pState->stateName);
				PERFINFO_GSCT_STOP(pState, GSCT_END_LIB);
			}
		}


		//now check for states are being exited
		iNextDepth = eaSize(&spNextStates);
		bPossibleStateChanges = false;

		for (gDepthOfCurrentlyProcessingState = iCurDepth - 1; gDepthOfCurrentlyProcessingState >= 0; gDepthOfCurrentlyProcessingState--)
		{
			if (gDepthOfCurrentlyProcessingState >= iNextDepth || spCurrentStates[gDepthOfCurrentlyProcessingState] != spNextStates[gDepthOfCurrentlyProcessingState])
			{
				bPossibleStateChanges = true;
				break;
			}
		}

		if (bPossibleStateChanges)
		{
			GSM_TransitionRequestsAllowed = false;
			

			CheckForTransitionOverrides();
		
			iNextDepth = eaSize(&spNextStates);

			for (gDepthOfCurrentlyProcessingState = iCurDepth - 1; gDepthOfCurrentlyProcessingState >= 0; gDepthOfCurrentlyProcessingState--)
			{
				if (gDepthOfCurrentlyProcessingState >= iNextDepth || spCurrentStates[gDepthOfCurrentlyProcessingState] != spNextStates[gDepthOfCurrentlyProcessingState])
				{
					GlobalState *pState = GlobalStateFromNameOrHandle(spCurrentStates[gDepthOfCurrentlyProcessingState]);

	
					bAnyStateChanges = true;

					if (pState->pLeaveStateCB_FromApp)
					{
						GSM_PRINT_STATE_CHANGE(COLOR_RED, "GSM_LeaveApp: %f %s", timerGetSecondsAsFloat(), pState->stateName);
						
						PERFINFO_GSCT_START(pState, GSCT_LEAVE_APP);
						globMovementLog("[GSM] Starting state %s_LeaveStateApp", pState->stateName);
						pState->pLeaveStateCB_FromApp();
						globMovementLog("[GSM] Finished state %s_LeaveStateApp", pState->stateName);
						PERFINFO_GSCT_STOP(pState, GSCT_LEAVE_APP);
					}

					if (pState->pLeaveStateCB_FromLib)
					{
						GSM_PRINT_STATE_CHANGE(COLOR_RED, "GSM_LeaveLib: %f %s", timerGetSecondsAsFloat(), pState->stateName);

						PERFINFO_GSCT_START(pState, GSCT_LEAVE_LIB);
						globMovementLog("[GSM] Starting state %s_LeaveStateLib", pState->stateName);
						pState->pLeaveStateCB_FromLib();
						globMovementLog("[GSM] Finished state %s_LeaveStateLib", pState->stateName);
						PERFINFO_GSCT_STOP(pState, GSCT_LEAVE_LIB);
					}

					if (spGSMIndividualStateEnterLeaveCB)
					{
						spGSMIndividualStateEnterLeaveCB(false, pState->stateName);
					}

					GSM_DeactivateState(pState);
				}
			}

			GSM_TransitionRequestsAllowed = true;
		}

		gDepthOfCurrentlyProcessingState = -1;

		siGSMFrameCount++;

		eaCopy(&spPreviousStates, &spCurrentStates);
		eaCopy(&spCurrentStates, &spNextStates);


/*check for empty state. If it exists, then the current situation must be:
prev = x y
cur = x EMPTY y
next = x EMPTY y
switch this around so that it's
prev = x EMPTY y
cur = x y
next = x y
*/
		if (bAnyStateChanges)
		{
			for (i=0; i < eaSize(&spNextStates); i++)
			{
				if (spNextStates[i] == shEmptyState)
				{
					eaCopy(&spNextStates, &spPreviousStates);
					eaCopy(&spPreviousStates, &spCurrentStates);
					eaCopy(&spCurrentStates, &spNextStates);
					break;
				}
			}
		}
		
		PERFINFO_AUTO_STOP_CHECKED_PIX("all states");

		autoTimerThreadFrameEnd();
	}

	sbGSMRunning = false;
}

void GSM_CheckStateChangeValidity()
{
	int i, j;
	int iSize = eaSize(&spNextStates);

	for (i=0; i < iSize - 1; i++)
	{
		for (j= i+1; j < iSize; j++)
		{
			if (spNextStates[i] == spNextStates[j])
			{
				assertmsgf(0, "ERROR in Global state machine... two occurences of state %s",
					GlobalStateFromNameOrHandle(spNextStates[i])->stateName);
			}
		}
	}
}

void GSM_PutFullStateStackIntoEString(char **ppEString)
{
	int i;
	int iSize = eaSize(&spCurrentStates);
	estrClear(ppEString);

	for (i=0; i < iSize; i++)
	{
		estrAppend2(ppEString, "/");
		estrAppend2(ppEString, GlobalStateFromNameOrHandle(spCurrentStates[i])->stateName);
	}
}

//primarily for use by testClient
AUTO_COMMAND;
char *GSM_GetFullStateString(void)
{
	static char *pRetString = NULL;
	GSM_PutFullStateStackIntoEString(&pRetString);
	return pRetString;
}


void GSM_PutFullNextStateStackIntoEString(char **ppEString)
{
int i;
	int iSize = eaSize(&spNextStates);
	estrClear(ppEString);

	for (i=0; i < iSize; i++)
	{
		estrAppend2(ppEString, "/");
		estrAppend2(ppEString, GlobalStateFromNameOrHandle(spNextStates[i])->stateName);
	}
}

char *pCurStateString;
char *pNextStateString;

//for logging of state changes and requests
#define STATECHANGE_LOG {GSM_PutFullStateStackIntoEString(&pCurStateString); GSM_PutFullNextStateStackIntoEString(&pNextStateString); memlog_printf(NULL, "Function %s, called from %s(%d), requests change from %s to %s", __FUNCTION__, pFileName, iLineNum, pCurStateString, pNextStateString);}


void GSM_AddChildState_int(GlobalStateHandleOrName hState, bool bOverwriteCurrentChildIfAny, char *pFileName, int iLineNum)
{
	GlobalState *pNewState = GlobalStateFromNameOrHandle(hState);
	assertmsg(pNewState, "Unknown state requested");
	assertmsg(GSM_TransitionRequestsAllowed, "Can't request state changes during exit callbacks");

	if (!bOverwriteCurrentChildIfAny)
	{
		assertmsg(gDepthOfCurrentlyProcessingState == eaSize(&spCurrentStates) - 1, "Someone who isn't the current top state is trying to create a child state");
	}

	eaCopy(&spNextStates, &spCurrentStates);
	
	while (eaSize(&spNextStates) > gDepthOfCurrentlyProcessingState + 1)
	{
		eaPop(&spNextStates);
	}

	eaPush(&spNextStates, pNewState->hHandle);

	GSM_CheckStateChangeValidity();

	STATECHANGE_LOG
}

void GSM_ResetState_int(GlobalStateHandleOrName hState, char *pFileName, int iLineNum)
{
	GlobalState *pStateToReset = GlobalStateFromNameOrHandle(hState);
	GlobalState *pEmptyState = GlobalStateFromNameOrHandle(shEmptyState);

	int i;

	assertmsg(pStateToReset, "Unknown state for ResetState");
	assertmsg(GSM_TransitionRequestsAllowed, "Can't request state changes during exit callbacks");

	eaCopy(&spNextStates, &spCurrentStates);

	i = eaFind(&spNextStates, pStateToReset->hHandle);

	if (i != -1)
	{
		eaInsert(&spNextStates, pEmptyState->hHandle, i);
	}

	GSM_CheckStateChangeValidity();

	STATECHANGE_LOG
}


void GSM_CloseCurState_int(bool bKillChildrenIfAny, char *pFileName, int iLineNum)
{
	assertmsg(GSM_TransitionRequestsAllowed, "Can't request state changes during exit callbacks");

	if (!bKillChildrenIfAny)
	{
		assertmsg(gDepthOfCurrentlyProcessingState == eaSize(&spCurrentStates) - 1, "Someone who isn't the current top state is trying to exit to their parent");
	}

	eaCopy(&spNextStates, &spCurrentStates);

	while (eaSize(&spNextStates) > gDepthOfCurrentlyProcessingState + 1)
	{
		eaPop(&spNextStates);
	}

	eaPop(&spNextStates);

	GSM_CheckStateChangeValidity();

	STATECHANGE_LOG

}

void GSM_SwitchToSibling_int(GlobalStateHandleOrName hState, bool bKillChildrenIfAny, char *pFileName, int iLineNum)
{
	GlobalState *pNewState = GlobalStateFromNameOrHandle(hState);
	assertmsg(pNewState, "Unknown state requested");
	assertmsg(GSM_TransitionRequestsAllowed, "Can't request state changes during exit callbacks");

	if (!bKillChildrenIfAny)
	{
		assertmsg(gDepthOfCurrentlyProcessingState == eaSize(&spCurrentStates) - 1, "Someone who isn't the current top state is trying to switch to a sibling");
	}

	eaCopy(&spNextStates, &spCurrentStates);

	while (eaSize(&spNextStates) > gDepthOfCurrentlyProcessingState + 1)
	{
		eaPop(&spNextStates);
	}

	eaPop(&spNextStates);
	eaPush(&spNextStates, pNewState->hHandle);

	GSM_CheckStateChangeValidity();

	STATECHANGE_LOG

}

void GSM_KillAllChildStates_int(char *pFileName, int iLineNum)
{
	assertmsg(GSM_TransitionRequestsAllowed, "Can't request state changes during exit callbacks");

	eaCopy(&spNextStates, &spCurrentStates);

	while (eaSize(&spNextStates) > gDepthOfCurrentlyProcessingState + 1)
	{
		eaPop(&spNextStates);
	}

	GSM_CheckStateChangeValidity();

	STATECHANGE_LOG

}

char *GSM_GetCurActiveStateName(void )
{
	if (gDepthOfCurrentlyProcessingState < 0 || gDepthOfCurrentlyProcessingState >= eaSize(&spCurrentStates))
	{
		return NULL;
	}

	return GlobalStateFromNameOrHandle(spCurrentStates[gDepthOfCurrentlyProcessingState])->stateName;
}



AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CLIENTONLY;
void GSM_SwitchToState(ACMD_SENTENCE pchNewState)
{
	GSM_SwitchToState_Complex(pchNewState);
}

void GSM_SwitchToState_Complex_int(char *pFullStackName, char *pFileName, int iLineNum)
{
	char *pToken;
	assertmsg(GSM_TransitionRequestsAllowed, "Can't request state changes during exit callbacks");
	
	GSM_PRINT_STATE_CHANGE(	COLOR_GREEN|COLOR_BRIGHT,
							"GSM switching state: %s (%s:%d) %f",
							pFullStackName,
							pFileName,
							iLineNum, timerGetSecondsAsFloat());
	
	memlog_printf(NULL, "GSM_SwitchToState_Complex(%s)", pFullStackName);

	//meaningless call to reset strTok
	pToken = strTokWithSpacesAndPunctuation(NULL, NULL);

	eaCopy(&spNextStates, &spCurrentStates);

	pToken = strTokWithSpacesAndPunctuation(pFullStackName, "/");


	assertmsg(pToken, "empty string passed into GSM_SwitchToState");

	if (strcmp(pToken, "/") == 0)
	{
		//stackname begins with /. clear entire stack
		eaSetSize(&spNextStates, 0);
	}
	else if (strcmp(pToken, "..") == 0)
	{
		//stackname begins with "..", start at parent of current state
		while (eaSize(&spNextStates) > gDepthOfCurrentlyProcessingState)
		{
			eaPop(&spNextStates);
		}

		pToken = strTokWithSpacesAndPunctuation(pFullStackName, "/");

	}
	else if (strcmp(pToken, ".") == 0)
	{
		//stackname begins with ".", start at current state
		while (eaSize(&spNextStates) > gDepthOfCurrentlyProcessingState + 1)
		{
			eaPop(&spNextStates);
		}

		pToken = strTokWithSpacesAndPunctuation(pFullStackName, "/");

	}
	else
	{
		GlobalState *pState = GlobalStateFromNameOrHandle(pToken);

		assertmsgf(pState, "Unknown state %s requested in GSM_SwitchToState", pToken);

		while (eaSize(&spNextStates) && spNextStates[eaSize(&spNextStates)-1] != pState->hHandle)
		{
			eaPop(&spNextStates);
		}

		assertmsgf(eaSize(&spNextStates), "GSM_SwitchToState requested switch to non-active state %s", pToken);

		pToken = strTokWithSpacesAndPunctuation(pFullStackName, "/");
	}



	while (pToken)
	{
		assertmsgf(strcmp(pToken, "/") == 0, "String %s passed in to GSM_SwitchToState has syntax error", pFullStackName);

		pToken = strTokWithSpacesAndPunctuation(pFullStackName, "/");

		if (!pToken)
		{
			break;
		}

		if (strcmp(pToken, "..") == 0)
		{
			assertmsgf(eaSize(&spNextStates), "GSM_SwitchToState has too many ..s");

			eaPop(&spNextStates);
		}
		else
		{
			GlobalState *pState = GlobalStateFromNameOrHandle(pToken);

			assertmsgf(pState, "Unknown state %s requested in GSM_SwitchToState", pToken);

			eaPush(&spNextStates, pState->hHandle);
		}

		pToken = strTokWithSpacesAndPunctuation(pFullStackName, "/");
	}

	GSM_CheckStateChangeValidity();

	STATECHANGE_LOG


}


		

GlobalStateHandle GSM_GetHandleFromName(char *pStateName)
{
	GlobalState *pState = GlobalStateFromNameOrHandle(pStateName);

	if (pState)
	{
		return pState->hHandle;
	}
	else
	{
		return NULL;
	}
}


bool GSM_AreAnyStateChangesRequested(void)
{
	int iCurSize = eaSize(&spCurrentStates);
	int iNextSize = eaSize(&spNextStates);
	int i;

	if (iCurSize != iNextSize)
	{
		return true;
	}

	for (i=0; i < iCurSize; i++)
	{
		if (spCurrentStates[i] != spNextStates[i])
		{
			return true;
		}
	}

	return false;
}


void GSM_CancelRequestedTransitions_int(char *pFileName, int iLineNum)
{
	assertmsg(GSM_TransitionRequestsAllowed, "Can't request state changes during exit callbacks");
	eaCopy(&spNextStates, &spCurrentStates);
	STATECHANGE_LOG
}


//returns true if the named state is anywhere in the current stack of states, false otherwise
bool GSM_IsStateActive(GlobalStateHandleOrName hState)
{
	GlobalState *pState = GlobalStateFromNameOrHandle(hState);
	int i;
	int iCurSize = eaSize(&spCurrentStates);


	assertmsg(pState, "Unknown state passed to IsStateActive");

	for (i=0; i < iCurSize; i++)
	{
		if (spCurrentStates[i] == pState->hHandle)
		{
			return true;
		}
	}

	return false;
}

//returns true if the named state is anywhere in the current stack of states, false otherwise or if the state doesn't exist
bool GSM_IsStateRealAndActive(GlobalStateHandleOrName hState)
{
	GlobalState *pState = GlobalStateFromNameOrHandle(hState);
	return pState ? GSM_IsStateActive(pState->hHandle) : false;
}

//returns true if the named state is anywhere in the current stack of states or 
//next frame's stack of states, false otherwise
bool GSM_IsStateActiveOrPending(GlobalStateHandleOrName hState)
{
	GlobalState *pState = GlobalStateFromNameOrHandle(hState);
	int i;
	int iCurSize = eaSize(&spCurrentStates);


	assertmsg(pState, "Unknown state passed to GSM_IsStateActiveOrPending");

	for (i=0; i < iCurSize; i++)
	{
		if (spCurrentStates[i] == pState->hHandle)
		{
			return true;
		}
	}

	iCurSize = eaSize(&spNextStates);


	assertmsg(pState, "Unknown state passed to GSM_IsStateActiveOrPending");

	for (i=0; i < iCurSize; i++)
	{
		if (spNextStates[i] == pState->hHandle)
		{
			return true;
		}
	}


	return false;
}

//for these two functions, pass in a NULL handle to indicate "the current state"
bool GSM_DoesStateHaveChildStates(GlobalStateHandleOrName hState)
{
	if (hState == NULL)
	{
		return (gDepthOfCurrentlyProcessingState < eaSize(&spCurrentStates) - 1);
	}
	else
	{
		GlobalState *pState = GlobalStateFromNameOrHandle(hState);
		int i;
		int iCurSize = eaSize(&spCurrentStates);


		assertmsg(pState, "Unknown state passed to IsStateActive");

		for (i=0; i < iCurSize - 1; i++)
		{
			if (spCurrentStates[i] == pState->hHandle)
			{
				return true;
			}
		}

		return false;
	}
}

int GSM_FramesInState(GlobalStateHandleOrName hState)
{
	GlobalState *pState;
	if (hState == NULL)
	{
		return siGSMFrameCount - GlobalStateFromNameOrHandle(spCurrentStates[gDepthOfCurrentlyProcessingState])->iFrameCountWhenActivated;
	}

	pState = GlobalStateFromNameOrHandle(hState);

	if (GSM_IsStateActive(pState->hHandle))
	{
		return siGSMFrameCount - pState->iFrameCountWhenActivated;
	}
	else
	{
		return -1;
	}
}

float GSM_TimeInStateIncludeCurrentFrame(GlobalStateHandleOrName hState)
{
	float frametime = GSM_TimeInState(hState);
	float fCurRawTime = timerElapsed(sGSMTimer);
	float fDelta = fCurRawTime - sGSMFrameTimeBeginningOfThisFrame;
	if (spGSMTimeSetScaleCB)
	{
	
		return frametime + fDelta * spGSMTimeSetScaleCB();
	}
	else
	{
		return frametime + fDelta;
	}
}

float GSM_TimeInState(GlobalStateHandleOrName hState)
{
	GlobalState *pState;
	if (hState == NULL)
	{
		return sGSMFrameTimeBeginningOfThisFrame - GlobalStateFromNameOrHandle(spCurrentStates[gDepthOfCurrentlyProcessingState])->fTimeWhenActivated;
	}

	pState = GlobalStateFromNameOrHandle(hState);

	if (GSM_IsStateActive(pState->hHandle))
	{
		return sGSMFrameTimeBeginningOfThisFrame - pState->fTimeWhenActivated;
	}
	else
	{
		return -1.0f;
	}
}

void GSM_ResetStateTimers(GlobalStateHandleOrName hState)
{
	GlobalState *pState;
	if(hState == NULL)
		hState = spCurrentStates[gDepthOfCurrentlyProcessingState];
	pState = GlobalStateFromNameOrHandle(hState);
	if (GSM_IsStateActive(pState->hHandle))
	{
		pState->fTimeWhenActivated = sGSMFrameTimeBeginningOfThisFrame;
		pState->iFrameCountWhenActivated = siGSMFrameCount;
	}
}

//converts a string like "state1/state2/state3" into an earray containing
//the state handles for those stacks. Ignores leading and trailing /s
void GSM_StateStringToStack(char *pString, GlobalStateHandle **ppStack)
{

	
	//meaningless call to reset strTok
	char *pToken = strTokWithSpacesAndPunctuation(NULL, NULL);

	eaCreate(ppStack);



	while ((pToken = strTokWithSpacesAndPunctuation(pString, "/")))
	{
		GlobalState *pState = GlobalStateFromNameOrHandle(pToken);

		assertmsgf(pState, "Unknown state name %s in state string", pToken);

		eaPush(ppStack, pState->hHandle);
	}
}





void GSM_SetTransitionOverride(char *pFrom, char *pTo, char *pNewTo)
{
	RequestedStateTransition *pTransition = (RequestedStateTransition*)calloc(sizeof(RequestedStateTransition), 1);
	GlobalState *pCurState = GlobalStateFromNameOrHandle(spCurrentStates[gDepthOfCurrentlyProcessingState]);

	assertmsg(pTo && pNewTo, "can't call setTransitionOverride without pTo and pNewTo");

	if (pFrom)
	{
		GSM_StateStringToStack(pFrom, &pTransition->pFrom);
		assertmsg(eaSize(&pTransition->pFrom), "pFrom string in SetTransitionOverride can be NULL, but not \"/\" or \"\"");
	}

	GSM_StateStringToStack(pTo, &pTransition->pTo);
	GSM_StateStringToStack(pNewTo, &pTransition->pNewTo);

	if (pCurState)
	{
		eaPush(&pCurState->ppTransitions, pTransition);
	}
	else
	{
		eaPush(&sppGlobalTransitions, pTransition);
	}
}

bool GSM_DoesTransitionStateStackMatchStateStack(GlobalStateHandle **ppTransStack, GlobalStateHandle **ppRealStack)
{
	int iTransSize = eaSize(ppTransStack);
	int iRealSize = eaSize(ppRealStack);

	int i;

	//if either is empty, they only match if both are empty
	if (iTransSize == 0 || iRealSize == 0)
	{
		return (iTransSize == 0 && iRealSize == 0);
	}

	//otherwise, check if the trans stack is a suffix of the real stack

	if (iRealSize < iTransSize)
	{
		return false;
	}

	for (i=0;i < iTransSize; i++)
	{
		if ((*ppTransStack)[i] != (*ppRealStack)[i + iRealSize - iTransSize])
		{
			return false;
		}

	}

	return true;
}

bool CheckTransitionListForTransitionOverrides(RequestedStateTransition ***pppTransitionList)
{
	int iListSize = eaSize(pppTransitionList);

	int i;

	for (i=0; i < iListSize; i++)
	{
		RequestedStateTransition *pTransition = (*pppTransitionList)[i];

		int iToSize;
		int iNewToSize;
		int iCurSize;
		int iNextSize;
		int j;

		if (pTransition->pFrom)
		{
			if (!GSM_DoesTransitionStateStackMatchStateStack(&pTransition->pFrom, &spCurrentStates))
			{
				return false;
			}
		}

		if (!GSM_DoesTransitionStateStackMatchStateStack(&pTransition->pTo, &spNextStates))
		{
			return false;
		}

		iToSize = eaSize(&pTransition->pTo);
		iNewToSize = eaSize(&pTransition->pNewTo);
		iCurSize = eaSize(&spCurrentStates);
		iNextSize = eaSize(&spNextStates);

		for (j=0; j < iToSize; j++)
		{
			eaPop(&spNextStates);
		}

		for (j=0; j < iNewToSize; j++)
		{
			eaPush(&spNextStates, pTransition->pNewTo[j]);
		}

		return true;
	}

	return false;
}


void CheckForTransitionOverrides(void)
{
	int iCurSize = eaSize(&spCurrentStates);
	int i;

	for (i = iCurSize - 1; i >= 0; i--)
	{
		GlobalState *pState = GlobalStateFromNameOrHandle(spCurrentStates[i]);

		if (CheckTransitionListForTransitionOverrides(&pState->ppTransitions))
		{
			return;
		}
	}

	CheckTransitionListForTransitionOverrides(&sppGlobalTransitions);
}



void GSM_Quit(char *pReason, ...)
{
	char *pFullReasonString = NULL;
	va_list ap;


	va_start(ap, pReason);

	estrConcatfv(&pFullReasonString, pReason, ap);

	va_end(ap);

	log_printf(LOG_CRASH, "GSM_Quit(). Reason: %s", pFullReasonString);
	filelog_printf("Shutdown.log", "GSM_Quit(). Reason: %s", pFullReasonString);
	
	logFlush();
	logWaitForQueueToEmpty();

	GSM_SwitchToState_Complex("/");

	estrDestroy(&pFullReasonString);

	sbGSMQuitting = true;
}

bool GSM_IsQuitting(void)
{
	return sbGSMQuitting;
}

AUTO_COMMAND ACMD_CLIENTONLY ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_NAME(GSM_Quit);
void GSM_QuitCommand(CmdContext *pContext)
{
	GSM_Quit("Called as a Command via %s", GetContextHowString(pContext));
}





AUTO_RUN;
void GSM_AutoInit(void)
{
	//add the "empty" global state, which is used as a placeholder to facilitate various complex state transitions
	shEmptyState = GSM_AddGlobalState("__EMPTY__");
}

void GSM_DestroyStateTransitionPackets(void)
{
	int i;

	for (i=0; i < eaSize(&sppGSMPacketsToSendOnStateTransition); i++)
	{
		pktFree(&sppGSMPacketsToSendOnStateTransition[i]);
	}

	eaDestroy(&sppGSMPacketsToSendOnStateTransition);
}

void GSM_SendPacketOnStateTransition(Packet *pPacket)
{
	eaPush(&sppGSMPacketsToSendOnStateTransition, pPacket);
}

AUTO_EXPR_FUNC(util);
bool GSM_StateContains(char *pState)
{
	char *pTemp = NULL;
	bool bRetVal;
	estrStackCreate(&pTemp);
	GSM_PutFullStateStackIntoEString(&pTemp);

	bRetVal = !!strstri(pTemp, pState);

	estrDestroy(&pTemp);

	return bRetVal;
}

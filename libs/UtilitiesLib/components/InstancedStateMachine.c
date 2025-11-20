#include "instancedstatemachine.h"

#include "earray.h"
#include "utils.h"
#include "estring.h"
#include "stashtable.h"
#include "logging.h"
#include "ResourceInfo.h"
#include "InstancedStateMachine_c_ast.h"
#include "timing.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

AUTO_STRUCT;
typedef struct InstancedState
{
	//static stuff defined at creation time
	char stateName[ISM_NAME_MAX_LENGTH];
	InstancedStateHandle hHandle; NO_AST

	InstancedStateMachineCB *pEnterStateCB_FromLib; NO_AST
	InstancedStateMachineCB *pEnterStateCB_FromApp; NO_AST

	InstancedStateMachineCB *pBeginFrameCB_FromLib; NO_AST
	InstancedStateMachineCB *pBeginFrameCB_FromApp; NO_AST

	InstancedStateMachineCB *pEndFrameCB_FromLib; NO_AST 
	InstancedStateMachineCB *pEndFrameCB_FromApp; NO_AST

	InstancedStateMachineCB *pLeaveStateCB_FromLib; NO_AST
	InstancedStateMachineCB *pLeaveStateCB_FromApp; NO_AST

	int iMachinesInState;
} InstancedState;

typedef struct StateMachineType StateMachineType;

typedef struct InstancedStateMachine
{	
	InstancedStateHandle *ppPreviousStates;
	InstancedStateHandle *ppCurrentStates;
	InstancedStateHandle *ppNextStates;

	int currentStateDepth;
	U32 transitionRequestsAllowed:1;
	U32 debugLogging:1;

	StateMachineType *pMachineType;

	void *pUserObject;

	ISM_LoggingCallback *pLoggingCallback;

	U32 iTimeEnteredState;

} InstancedStateMachine;

AUTO_STRUCT;
struct StateMachineType
{
	char stateMachineName[ISM_NAME_MAX_LENGTH]; AST(KEY)
	InstancedStateHandle hHandle; NO_AST

	InstancedState **ppAllStateList;

	StashTable machineInstances; NO_AST

	InstanceStateMachineStateChangeDebugCB *pStateChangeDebugCB; NO_AST
};

InstancedState *InstancedStateFromNameOrHandle(StateMachineType *machineType, InstancedStateHandleOrName hHandle);





void RegisterThatMachineIsEnteringState(InstancedStateMachine *pMachine)
{
	int i;

	PERFINFO_AUTO_START_FUNC();
	for (i = 0; i < eaSize(&pMachine->ppCurrentStates); i++)
	{
		InstancedState *pState = InstancedStateFromNameOrHandle(pMachine->pMachineType, pMachine->ppCurrentStates[i]);

		if (pState)
		{
			pState->iMachinesInState++;
		}
	}

	if (pMachine->pMachineType->pStateChangeDebugCB)
	{
		char *pTempString = NULL;
		int iSize = eaSize(&pMachine->ppCurrentStates);
		estrStackCreate(&pTempString);

		for (i=0; i < iSize; i++)
		{
			estrAppend2(&pTempString, "/");
			estrAppend2(&pTempString, InstancedStateFromNameOrHandle(pMachine->pMachineType, pMachine->ppCurrentStates[i])->stateName);
		}

		pMachine->pMachineType->pStateChangeDebugCB(pMachine->pUserObject, pTempString);

		estrDestroy(&pTempString);

	}

	pMachine->iTimeEnteredState = timeSecondsSince2000();
	PERFINFO_AUTO_STOP_FUNC();
}

void RegisterThatMachineIsLeavingState(InstancedStateMachine *pMachine)
{
	int i;

	PERFINFO_AUTO_START_FUNC();
	for (i = 0; i < eaSize(&pMachine->ppCurrentStates); i++)
	{
		InstancedState *pState = InstancedStateFromNameOrHandle(pMachine->pMachineType, pMachine->ppCurrentStates[i]);

		if (pState)
		{
			pState->iMachinesInState--;
		}
	}
	PERFINFO_AUTO_STOP_FUNC();
}



//EArray containing master list of all possible state machines, indexed by (handle - 1)
StateMachineType **gppMachineTypes = NULL;

StateMachineType *MachineTypeFromNameOrHandle(InstancedMachineHandleOrName hHandle)
{
	int i;
	assert(hHandle);

	if ((uintptr_t)hHandle <= (uintptr_t)eaSize(&gppMachineTypes))
	{
		return gppMachineTypes[((uintptr_t)hHandle) - 1];
	}

	for (i=0; i < eaSize(&gppMachineTypes); i++)
	{
		if (strcmp(gppMachineTypes[i]->stateMachineName, (char*)hHandle) == 0)
		{
			return gppMachineTypes[i];
		}
	}

	return NULL;
}

InstancedStateMachine *FindInstancedMachine(StateMachineType *type, void *userObject)
{
	InstancedStateMachine *foundMachine;
	PERFINFO_AUTO_START_FUNC();
	if (stashFindPointer(type->machineInstances, userObject, &foundMachine))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return foundMachine;
	}
	PERFINFO_AUTO_STOP_FUNC();
	return NULL;
}


InstancedStateMachine *InstancedMachineFromNameOrHandle(InstancedMachineHandleOrName hHandle, void *userObject)
{
	StateMachineType *type;
	PERFINFO_AUTO_START_FUNC();
	type = MachineTypeFromNameOrHandle(hHandle);
	if (!type)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return NULL;
	}
	PERFINFO_AUTO_STOP_FUNC();
	return FindInstancedMachine(type, userObject);
}

InstancedState *InstancedStateFromNameOrHandle(StateMachineType *machineType, InstancedStateHandleOrName hHandle)
{
	int i;

	PERFINFO_AUTO_START_FUNC();
	assert(hHandle);

	if ((uintptr_t)hHandle <= (uintptr_t)eaSize(&machineType->ppAllStateList))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return machineType->ppAllStateList[((uintptr_t)hHandle) - 1];
	}

	PERFINFO_AUTO_START("Walk Defined States", 1);
	for (i=0; i < eaSize(&machineType->ppAllStateList); i++)
	{
		if (strcmp(machineType->ppAllStateList[i]->stateName, (char*)hHandle) == 0)
		{
			PERFINFO_AUTO_STOP();
			PERFINFO_AUTO_STOP_FUNC();
			return machineType->ppAllStateList[i];
		}
	}

	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP_FUNC();
	return NULL;
}

InstancedState *CreateNewInstancedState(StateMachineType *machineType, char *pStateName)
{
	InstancedState *pState = (InstancedState*)calloc(sizeof(InstancedState), 1);
	int iCurCount;

	assertmsgf(strlen(pStateName) < ISM_NAME_MAX_LENGTH - 1, "State name %s is too long", pStateName);
	strcpy(pState->stateName, pStateName);

	iCurCount = eaSize(&machineType->ppAllStateList);

	pState->hHandle = (void*)((intptr_t)iCurCount + 1);

	eaPush(&machineType->ppAllStateList, pState);

	return pState;
}

StateMachineType *CreateNewMachineType(char *pMachineName)
{
	StateMachineType *pMachine = (StateMachineType*)calloc(sizeof(StateMachineType), 1);
	int iCurCount;

	assertmsgf(strlen(pMachineName) < ISM_NAME_MAX_LENGTH - 1, "State name %s is too long", pMachineName);
	strcpy(pMachine->stateMachineName, pMachineName);

	iCurCount = eaSize(&gppMachineTypes);

	pMachine->hHandle = (void*)((intptr_t)iCurCount + 1);

	eaPush(&gppMachineTypes, pMachine);

	pMachine->machineInstances = stashTableCreateAddress(64);

	return pMachine;
}


void ISM_SetNewStateDebugCB(InstancedMachineHandleOrName pStateMachineHandle, InstanceStateMachineStateChangeDebugCB *pCB)
{
	StateMachineType *pMachineType = MachineTypeFromNameOrHandle(pStateMachineHandle);
	if (!pMachineType)
	{
		pMachineType = CreateNewMachineType(pStateMachineHandle);
	}

	pMachineType->pStateChangeDebugCB = pCB;
}


InstancedStateHandle ISM_AddInstancedState_Internal(InstancedMachineHandleOrName hStateMachine,
											 InstancedStateHandleOrName hState,
											 InstancedStateMachineCB *pEnterStateCB,
											 InstancedStateMachineCB *pBeginFrameCB,
											 InstancedStateMachineCB *pEndFrameCB,
											 InstancedStateMachineCB *pLeaveStateCB, bool bFromApp)
{
	InstancedState *pState;
	StateMachineType *pMachineType = MachineTypeFromNameOrHandle(hStateMachine);
	if (!pMachineType)
	{
		pMachineType = CreateNewMachineType(hStateMachine);
	}
	
	pState = InstancedStateFromNameOrHandle(pMachineType, hState);

	if (!pState)
	{
		pState = CreateNewInstancedState(pMachineType, hState);
	}

	if (bFromApp)
	{
		if (pEnterStateCB)
		{
			assertmsgf(!pState->pEnterStateCB_FromApp, "State %s in machine %s already has pEnterStateCB_FromApp", pState->stateName, pMachineType->stateMachineName);
			pState->pEnterStateCB_FromApp = pEnterStateCB;
		}
		if (pBeginFrameCB)
		{
			assertmsgf(!pState->pBeginFrameCB_FromApp, "State %s in machine %s already has pBeginFrameCB_FromApp", pState->stateName, pMachineType->stateMachineName);
			pState->pBeginFrameCB_FromApp = pBeginFrameCB;
		}
		if (pEndFrameCB)
		{
			assertmsgf(!pState->pEndFrameCB_FromApp, "State %s in machine %s already has pEndFrameCB_FromApp", pState->stateName, pMachineType->stateMachineName);
			pState->pEndFrameCB_FromApp = pEndFrameCB;
		}
		if (pLeaveStateCB)
		{
			assertmsgf(!pState->pLeaveStateCB_FromApp, "State %s in machine %s already has pLeaveStateCB_FromApp", pState->stateName, pMachineType->stateMachineName);
			pState->pLeaveStateCB_FromApp = pLeaveStateCB;
		}
	}
	else
	{
		if (pEnterStateCB)
		{
			assertmsgf(!pState->pEnterStateCB_FromLib, "State %s in machine %s already has pEnterStateCB_FromLib", pState->stateName, pMachineType->stateMachineName);
			pState->pEnterStateCB_FromLib = pEnterStateCB;
		}
		if (pBeginFrameCB)
		{
			assertmsgf(!pState->pBeginFrameCB_FromLib, "State %s in machine %s already has pBeginFrameCB_FromLib", pState->stateName, pMachineType->stateMachineName);
			pState->pBeginFrameCB_FromLib = pBeginFrameCB;
		}
		if (pEndFrameCB)
		{
			assertmsgf(!pState->pEndFrameCB_FromLib, "State %s in machine %s already has pEndFrameCB_FromLib", pState->stateName, pMachineType->stateMachineName);
			pState->pEndFrameCB_FromLib = pEndFrameCB;
		}
		if (pLeaveStateCB)
		{
			assertmsgf(!pState->pLeaveStateCB_FromLib, "State %s in machine %s already has pLeaveStateCB_FromLib", pState->stateName, pMachineType->stateMachineName);
			pState->pLeaveStateCB_FromLib = pLeaveStateCB;
		}
	}
	return pState->hHandle;
}



bool ISM_CreateMachine(InstancedMachineHandleOrName pStateMachineHandle, void *pUserObject, char* initialState)
{
	InstancedStateMachine *pMachine;
	StateMachineType *pMachineType = MachineTypeFromNameOrHandle(pStateMachineHandle);
	InstancedState *pState;
	if (!pMachineType)
	{
		return false;
	}
	if (pMachine = FindInstancedMachine(pMachineType, pUserObject))
	{
		return false; // already there
	}
	pState = InstancedStateFromNameOrHandle(pMachineType, initialState);
	if (!pState)
	{
		return false; // Invalid initial state
	}
	pMachine = calloc(sizeof(InstancedStateMachine),1);
	pMachine->pMachineType = pMachineType;
	pMachine->pUserObject = pUserObject;
	pMachine->currentStateDepth = -1;
	pMachine->transitionRequestsAllowed = true;
	
	eaPush(&pMachine->ppNextStates, pState->hHandle);
	if (!stashAddPointer(pMachineType->machineInstances, pUserObject, pMachine, false))
	{
		return false;
	}


	return true;
}


bool ISM_DestroyMachine(InstancedMachineHandleOrName pStateMachineHandle, void *pUserObject)
{
	InstancedStateMachine *pMachine = InstancedMachineFromNameOrHandle(pStateMachineHandle, pUserObject);
	if (!pMachine)
	{
		return false;
	}
	if (!stashRemovePointer(pMachine->pMachineType->machineInstances, pUserObject, &pMachine))
	{
		return false;
	}

	RegisterThatMachineIsLeavingState(pMachine);

	eaDestroy(&pMachine->ppPreviousStates);
	eaDestroy(&pMachine->ppNextStates);
	eaDestroy(&pMachine->ppCurrentStates);
	SAFE_FREE(pMachine);
	return true;
}

static __inline void ISM_TickInternal(InstancedStateMachine *pMachine, void *pUserObject, F32 fElapsed)
{
	int iCurDepth;
	int iPrevDepth;
	int iNextDepth;
	char *pStateString = NULL;

	PERFINFO_AUTO_START_FUNC();

	assertmsg(pMachine, "State machine not found for tick");
	assertmsg(pMachine->currentStateDepth == -1, "State machine corruption, or recursive calls to GSM_Execute");

	//main loop
	if (eaSize(&pMachine->ppCurrentStates) || eaSize(&pMachine->ppNextStates))
	{
		bool bAnyStateChanges = false;
		bool bPossibleStateChanges;

		iCurDepth = eaSize(&pMachine->ppCurrentStates);
		iPrevDepth = eaSize(&pMachine->ppPreviousStates);
		iNextDepth = eaSize(&pMachine->ppNextStates);
		bPossibleStateChanges = false;

		for (pMachine->currentStateDepth = iCurDepth - 1; pMachine->currentStateDepth >= 0; pMachine->currentStateDepth--)
		{
			if (pMachine->currentStateDepth >= iNextDepth || 
				pMachine->ppCurrentStates[pMachine->currentStateDepth] != pMachine->ppNextStates[pMachine->currentStateDepth])
			{
				bPossibleStateChanges = true;
				break;
			}
		}

		if (bPossibleStateChanges)
		{
			pMachine->transitionRequestsAllowed = false;

			iNextDepth = eaSize(&pMachine->ppNextStates);

			for (pMachine->currentStateDepth = iCurDepth - 1; pMachine->currentStateDepth >= 0; pMachine->currentStateDepth--)
			{
				if (pMachine->currentStateDepth >= iNextDepth || 
					pMachine->ppCurrentStates[pMachine->currentStateDepth] != pMachine->ppNextStates[pMachine->currentStateDepth])
				{
					InstancedState *pState = InstancedStateFromNameOrHandle(pMachine->pMachineType, 
						pMachine->ppCurrentStates[pMachine->currentStateDepth]);

					bAnyStateChanges = true;

					if (pState->pLeaveStateCB_FromApp)
					{
						pState->pLeaveStateCB_FromApp(pMachine->pMachineType->hHandle, pUserObject, fElapsed);
					}

					if (pState->pLeaveStateCB_FromLib)
					{
						pState->pLeaveStateCB_FromLib(pMachine->pMachineType->hHandle, pUserObject, fElapsed);
					}
				}
			}

			pMachine->transitionRequestsAllowed = true;
		}

		if (iNextDepth > iCurDepth)
		{
			bAnyStateChanges = true;
		}


		if (bAnyStateChanges && pMachine->debugLogging)
		{
			char *pFullLogString = NULL;
			char *pStringFromCB = NULL;
			char *pOldState = NULL;
			char *pNewState = NULL;

			ISM_PutFullStateStackIntoEString(pMachine->pMachineType->hHandle, pUserObject, &pOldState);
			ISM_PutFullNextStateStackIntoEString(pMachine->pMachineType->hHandle, pUserObject, &pNewState);

			if (pMachine->pLoggingCallback)
			{
				pMachine->pLoggingCallback(pUserObject, &pStringFromCB);
			}
			else
			{
				estrPrintf(&pStringFromCB, "(No CB)");
			}

			estrPrintf(&pFullLogString, "ISM %p transitioning from state %s to state %s.\n%s\n\n", 
				pMachine, pOldState, pNewState, pStringFromCB);

			printf("%s", pFullLogString);
			log_printf(LOG_ISM, "%s", pFullLogString);

			estrDestroy(&pFullLogString);
			estrDestroy(&pStringFromCB);
			estrDestroy(&pOldState);
			estrDestroy(&pNewState);
		}

		pMachine->currentStateDepth = -1;

		if (bAnyStateChanges)
		{
			RegisterThatMachineIsLeavingState(pMachine);
		}



		eaCopy(&pMachine->ppPreviousStates, &pMachine->ppCurrentStates);
		eaCopy(&pMachine->ppCurrentStates, &pMachine->ppNextStates);

		if (bAnyStateChanges)
		{
			RegisterThatMachineIsEnteringState(pMachine);
		}


		//check for which of our states are new
		iCurDepth = eaSize(&pMachine->ppCurrentStates);
		iPrevDepth = eaSize(&pMachine->ppPreviousStates);

		for (pMachine->currentStateDepth=0; pMachine->currentStateDepth < iCurDepth; pMachine->currentStateDepth++)
		{
			if (pMachine->currentStateDepth >= iPrevDepth || 
				pMachine->ppCurrentStates[pMachine->currentStateDepth] != pMachine->ppPreviousStates[pMachine->currentStateDepth])
			{
				//for each new state, set its timing info, and call its EnterState callbacks

				InstancedState *pState = InstancedStateFromNameOrHandle(pMachine->pMachineType, 
					pMachine->ppCurrentStates[pMachine->currentStateDepth]);

				if (pState->pEnterStateCB_FromLib)
				{
					pState->pEnterStateCB_FromLib(pMachine->pMachineType->hHandle, pUserObject, fElapsed);
				}

				if (pState->pEnterStateCB_FromApp)
				{
					pState->pEnterStateCB_FromApp(pMachine->pMachineType->hHandle, pUserObject, fElapsed);
				}
			}
		}

		//now update all states... begin frame first
		for (pMachine->currentStateDepth=0; pMachine->currentStateDepth < iCurDepth; pMachine->currentStateDepth++)
		{
			InstancedState *pState = InstancedStateFromNameOrHandle(pMachine->pMachineType, 
				pMachine->ppCurrentStates[pMachine->currentStateDepth]);

			if (pState->pBeginFrameCB_FromLib)
			{
				pState->pBeginFrameCB_FromLib(pMachine->pMachineType->hHandle, pUserObject, fElapsed);
			}

			if (pState->pBeginFrameCB_FromApp)
			{
				pState->pBeginFrameCB_FromApp(pMachine->pMachineType->hHandle, pUserObject, fElapsed);
			}
		}

		for (pMachine->currentStateDepth = iCurDepth - 1; pMachine->currentStateDepth >= 0; pMachine->currentStateDepth--)
		{
			InstancedState *pState = InstancedStateFromNameOrHandle(pMachine->pMachineType,
				pMachine->ppCurrentStates[pMachine->currentStateDepth]);

			if (pState->pEndFrameCB_FromApp)
			{
				pState->pEndFrameCB_FromApp(pMachine->pMachineType->hHandle, pUserObject, fElapsed);
			}

			if (pState->pEndFrameCB_FromLib)
			{
				pState->pEndFrameCB_FromLib(pMachine->pMachineType->hHandle, pUserObject, fElapsed);
			}
		}
	}
	PERFINFO_AUTO_STOP_FUNC();
}

void ISM_Tick(InstancedMachineHandleOrName pStateMachineHandle, void *pUserObject, F32 fElapsed)
{
	InstancedStateMachine *pMachine;
	PERFINFO_AUTO_START_FUNC();
	pMachine = InstancedMachineFromNameOrHandle(pStateMachineHandle, pUserObject);
	ISM_TickInternal(pMachine,pUserObject,fElapsed);
	PERFINFO_AUTO_STOP_FUNC();
}

void ISM_TickAll(InstancedMachineHandleOrName pStateMachineHandle, F32 fElapsed)
{
	StateMachineType *type = MachineTypeFromNameOrHandle(pStateMachineHandle);
	if (!type) return;

	FOR_EACH_IN_STASHTABLE2(type->machineInstances,el) {
		ISM_TickInternal(stashElementGetPointer(el),stashElementGetKey(el),fElapsed);
	} FOR_EACH_END;
}


void ISM_CheckStateChangeValidity(InstancedStateMachine *pMachine)
{
	int i, j;
	int iSize = eaSize(&pMachine->ppNextStates);

	for (i=0; i < iSize - 1; i++)
	{
		for (j= i+1; j < iSize; j++)
		{
			if (pMachine->ppNextStates[i] == pMachine->ppNextStates[j])
			{
				assertmsgf(0, "ERROR in Global state machine... two occurences of state %s",
					InstancedStateFromNameOrHandle(pMachine->pMachineType, pMachine->ppNextStates[i])->stateName);
			}
		}
	}
}


bool ISM_PutFullStateStackIntoEString(InstancedMachineHandleOrName pStateMachineHandle, void *pUserObject, char **ppEString)
{
	InstancedStateMachine *pMachine = InstancedMachineFromNameOrHandle(pStateMachineHandle, pUserObject);
	if (pMachine)
	{	
		int i;
		int iSize = eaSize(&pMachine->ppCurrentStates);
		estrClear(ppEString);

		for (i=0; i < iSize; i++)
		{
			estrAppend2(ppEString, "/");
			estrAppend2(ppEString, InstancedStateFromNameOrHandle(pMachine->pMachineType, pMachine->ppCurrentStates[i])->stateName);
		}
		return true;
	}
	return false;
}

bool ISM_PutFullNextStateStackIntoEString(InstancedMachineHandleOrName pStateMachineHandle, void *pUserObject, char **ppEString)
{
	InstancedStateMachine *pMachine = InstancedMachineFromNameOrHandle(pStateMachineHandle, pUserObject);
	if (pMachine)
	{	
		int i;
		int iSize = eaSize(&pMachine->ppNextStates);
		estrClear(ppEString);

		for (i=0; i < iSize; i++)
		{
			estrAppend2(ppEString, "/");
			estrAppend2(ppEString, InstancedStateFromNameOrHandle(pMachine->pMachineType, pMachine->ppNextStates[i])->stateName);
		}
		return true;
	}
	return false;
}


bool ISM_SwitchToSibling(InstancedMachineHandleOrName pStateMachineHandle, void *pUserObject, InstancedStateHandleOrName hState)
{
	InstancedStateMachine *pMachine = InstancedMachineFromNameOrHandle(pStateMachineHandle, pUserObject);
	if (pMachine)
	{
		InstancedState *pNewState = InstancedStateFromNameOrHandle(pMachine->pMachineType, hState);
		assertmsg(pNewState, "Unknown state requested");
		assertmsg(pMachine->transitionRequestsAllowed, "Can't request state changes during exit callbacks");

		eaCopy(&pMachine->ppNextStates, &pMachine->ppCurrentStates);

		while (eaSize(&pMachine->ppNextStates) > pMachine->currentStateDepth + 1)
		{
			eaPop(&pMachine->ppNextStates);
		}

		eaPop(&pMachine->ppNextStates);
		eaPush(&pMachine->ppNextStates, pNewState->hHandle);

		ISM_CheckStateChangeValidity(pMachine);
		return true;
	}
	return false;

}

bool ISM_SwitchToState_Complex(InstancedMachineHandleOrName pStateMachineHandle, void *pUserObject, char *pFullStackName)
{
	char *pToken;

	InstancedStateMachine *pMachine = InstancedMachineFromNameOrHandle(pStateMachineHandle, pUserObject);
	if (!pMachine)
	{
		return false;
	}

	assertmsg(pMachine->transitionRequestsAllowed, "Can't request state changes during exit callbacks");

	//meaningless call to reset strTok
	pToken = strTokWithSpacesAndPunctuation(NULL, NULL);

	eaCopy(&pMachine->ppNextStates, &pMachine->ppCurrentStates);

	pToken = strTokWithSpacesAndPunctuation(pFullStackName, "/");


	assertmsg(pToken, "empty string passed into ISM_SwitchToState");

	if (strcmp(pToken, "/") == 0)
	{
		//stackname begins with /. clear entire stack
		eaSetSize(&pMachine->ppNextStates, 0);
	}
	else if (strcmp(pToken, "..") == 0)
	{
		//stackname begins with "..", start at parent of current state
		while (eaSize(&pMachine->ppNextStates) > pMachine->currentStateDepth)
		{
			eaPop(&pMachine->ppNextStates);
		}

		pToken = strTokWithSpacesAndPunctuation(pFullStackName, "/");

	}
	else if (strcmp(pToken, ".") == 0)
	{
		//stackname begins with ".", start at current state
		while (eaSize(&pMachine->ppNextStates) > pMachine->currentStateDepth + 1)
		{
			eaPop(&pMachine->ppNextStates);
		}

		pToken = strTokWithSpacesAndPunctuation(pFullStackName, "/");

	}
	else
	{
		InstancedState *pState = InstancedStateFromNameOrHandle(pMachine->pMachineType, pToken);

		assertmsgf(pState, "Unknown state %s requested in ISM_SwitchToState", pToken);

		while (eaSize(&pMachine->ppNextStates) && pMachine->ppNextStates[eaSize(&pMachine->ppNextStates)-1] != pState->hHandle)
		{
			eaPop(&pMachine->ppNextStates);
		}

		assertmsgf(eaSize(&pMachine->ppNextStates), "ISM_SwitchToState requested switch to non-active state %s", pToken);

		pToken = strTokWithSpacesAndPunctuation(pFullStackName, "/");
	}



	while (pToken)
	{
		assertmsgf(strcmp(pToken, "/") == 0, "String %s passed in to ISM_SwitchToState has syntax error", pFullStackName);

		pToken = strTokWithSpacesAndPunctuation(pFullStackName, "/");

		if (!pToken)
		{
			break;
		}

		if (strcmp(pToken, "..") == 0)
		{
			assertmsgf(eaSize(&pMachine->ppNextStates), "ISM_SwitchToState has too many ..s");

			eaPop(&pMachine->ppNextStates);
		}
		else
		{
			InstancedState *pState = InstancedStateFromNameOrHandle(pMachine->pMachineType, pToken);

			assertmsgf(pState, "Unknown state %s requested in ISM_SwitchToState", pToken);

			eaPush(&pMachine->ppNextStates, pState->hHandle);
		}

		pToken = strTokWithSpacesAndPunctuation(pFullStackName, "/");
	}

	ISM_CheckStateChangeValidity(pMachine);
	return true;

}

bool ISM_CancelRequestedTransitions(InstancedMachineHandleOrName pStateMachineHandle, void *pUserObject)
{
	InstancedStateMachine *pMachine = InstancedMachineFromNameOrHandle(pStateMachineHandle, pUserObject);
	if (pMachine)
	{
		assertmsg(pMachine->transitionRequestsAllowed, "Can't request state changes during exit callbacks");
		eaCopy(&pMachine->ppNextStates, &pMachine->ppCurrentStates);
		return true;
	}
	return false;
}

bool ISM_IsStateActive(InstancedMachineHandleOrName pStateMachineHandle, void *pUserObject, InstancedStateHandleOrName hState)
{
	InstancedStateMachine *pMachine = InstancedMachineFromNameOrHandle(pStateMachineHandle, pUserObject);
	if (pMachine)
	{
		InstancedState *pState = InstancedStateFromNameOrHandle(pMachine->pMachineType, hState);
		int i;
		int iCurSize = eaSize(&pMachine->ppCurrentStates);

		assertmsg(pState, "Unknown state passed to IsStateActive");

		for (i=0; i < iCurSize; i++)
		{
			if (pMachine->ppCurrentStates[i] == pState->hHandle)
			{
				return true;
			}
		}
	}
	return false;
}

bool ISM_IsStateActiveOrPending(InstancedMachineHandleOrName pStateMachineHandle, void *pUserObject, InstancedStateHandleOrName hState)
{
	InstancedStateMachine *pMachine = InstancedMachineFromNameOrHandle(pStateMachineHandle, pUserObject);
	if (pMachine)
	{
		InstancedState *pState = InstancedStateFromNameOrHandle(pMachine->pMachineType, hState);
		int i;
		int iCurSize = eaSize(&pMachine->ppCurrentStates);

		assertmsg(pState, "Unknown state passed to IsStateActive");

		for (i=0; i < iCurSize; i++)
		{
			if (pMachine->ppCurrentStates[i] == pState->hHandle)
			{
				return true;
			}
		}

		iCurSize = eaSize(&pMachine->ppNextStates);

		for (i=0; i < iCurSize; i++)
		{
			if (pMachine->ppNextStates[i] == pState->hHandle)
			{
				return true;
			}
		}
	}
	return false;
}

bool ISM_AreAnyStateChangesRequested(InstancedMachineHandleOrName pStateMachineHandle, void *pUserObject)
{
	InstancedStateMachine *pMachine = InstancedMachineFromNameOrHandle(pStateMachineHandle, pUserObject);
	if (pMachine)
	{
		int iCurSize = eaSize(&pMachine->ppCurrentStates);
		int iNextSize = eaSize(&pMachine->ppNextStates);
		int i;

		if (iCurSize != iNextSize)
		{
			return true;
		}

		for (i=0; i < iCurSize; i++)
		{
			if (pMachine->ppCurrentStates[i] != pMachine->ppNextStates[i])
			{
				return true;
			}
		}	
	}
		return false;
}


void ISM_BeginDebugLogging(InstancedMachineHandleOrName pStateMachineHandle, void *pUserObject, ISM_LoggingCallback *pCallback)
{
	InstancedStateMachine *pMachine = InstancedMachineFromNameOrHandle(pStateMachineHandle, pUserObject);
	if (pMachine)
	{
		pMachine->debugLogging = true;
		pMachine->pLoggingCallback = pCallback;
	}
}

U32 ISM_TimeEnteredCurrentState(InstancedMachineHandleOrName pStateMachineHandle, void *pUserObject, char **ppCurrentStateName)
{
	InstancedStateMachine *pMachine = InstancedMachineFromNameOrHandle(pStateMachineHandle, pUserObject);
	if (pMachine)
	{
		if(ppCurrentStateName)
		{
			int i;
			int iSize = eaSize(&pMachine->ppCurrentStates);
			estrClear(ppCurrentStateName);

			for (i=0; i < iSize; i++)
			{
				estrAppend2(ppCurrentStateName, "/");
				estrAppend2(ppCurrentStateName, InstancedStateFromNameOrHandle(pMachine->pMachineType, pMachine->ppCurrentStates[i])->stateName);
			}
		}
		return pMachine->iTimeEnteredState;
	}

	return 0;
}


AUTO_RUN;
void ISM_AddResources(void)
{
	resRegisterDictionaryForEArray("InstancedStateMachines", RESCATEGORY_SYSTEM, 0,&gppMachineTypes, parse_StateMachineType);
}


#include "InstancedStateMachine_c_ast.c"

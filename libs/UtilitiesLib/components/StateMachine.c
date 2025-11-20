#include "StateMachine.h"

#include "Breakpoint.h"
#include "CommandQueue.h"
#include "estring.h"
#include "ExpressionPrivate.h"
#include "file.h"
#include "MemoryPool.h"
#include "StringCache.h"
#include "timing.h"
#include "ResourceManager.h"

#include "StateMachine_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

MP_DEFINE(FSMStateTrackerEntry);

typedef struct FSMReloadData
{
	const char* path;
	const char* groupName;
	struct {
		ExprFuncTable* state_action;
		ExprFuncTable* state_onentry;
		ExprFuncTable* state_onentryfirst;
		ExprFuncTable* trans_condition;
		ExprFuncTable* trans_action;
	} funcTables;
	ExprContext* exprContext;
}FSMReloadData;

FSMReloadData** allReloadData = NULL;
 
DictionaryHandle gFSMDict = NULL;
DictionaryHandle gFSMStateDict = NULL;
ExprLocalDataDestroyCallback gLocalDataDestroyFunc = NULL;
FSMPartitionTimeCallback gPartitionTimeCallback = NULL;

static bool fsmMakeFileName(const char *name, const char *scope, char **estr);

void fsmSetLocalDataDestroyFunc(ExprLocalDataDestroyCallback cb)
{
	gLocalDataDestroyFunc = cb;
}

void fsmSetPartitionTimeCallback(FSMPartitionTimeCallback cb)
{
	gPartitionTimeCallback = cb;
}

void fsmLocalDataDestroy(ExprLocalData* data)
{
	if(gLocalDataDestroyFunc)
		gLocalDataDestroyFunc(data);
	else
		Errorf("Tried to destroy local data without callback set.  This will probably leak memory.");
}

FSMStateTrackerEntry* fsmStateTrackerEntryCreate()
{
	MP_CREATE(FSMStateTrackerEntry, 16);
	return MP_ALLOC(FSMStateTrackerEntry);
}

// statePath is always STRING_POOLed
FSMStateTrackerEntry* fsmStateTrackerEntryAdd(FSMContext* context, const char* statePath)
{
	FSMStateTrackerEntry* tracker;

	PERFINFO_AUTO_START_FUNC_L2();

	tracker = fsmStateTrackerEntryCreate();

	tracker->statePath = statePath;
	stashAddPointer(context->stateTrackerTable, statePath, tracker, false);

	PERFINFO_AUTO_STOP_L2();

	return tracker;
}

FSMStateTrackerEntry* fsmStateTrackerEntryGet(FSMContext* context, const char* statePath)
{
	FSMStateTrackerEntry* tracker = NULL;

	PERFINFO_AUTO_START_FUNC_L2();

	stashFindPointer(context->stateTrackerTable, statePath, &tracker);

	PERFINFO_AUTO_STOP_L2();

	return tracker;
}

void fsmStateTrackerEntryDestroy(FSMStateTrackerEntry* tracker)
{
	if(tracker->exitHandlers)
		CommandQueue_Destroy(tracker->exitHandlers);

	REMOVE_HANDLE(tracker->subFSMOverride);

	eaDestroyEx(&tracker->localData, fsmLocalDataDestroy);
	MP_FREE(FSMStateTrackerEntry, tracker);
}

MP_DEFINE(FSMStateReference);

FSMStateReference *fsmStateReferenceCreate(FSM* fsm, const char *stateName)
{
	FSMStateReference *stateRef;
	
	MP_CREATE(FSMStateReference, 16);
	stateRef = MP_ALLOC(FSMStateReference);

	SET_HANDLE_FROM_STRING(gFSMStateDict, stateName, stateRef->stateRef);
	stateRef->fsmName = fsm->name;
	stateRef->fsmStateName = stateName;
	stateRef->fsmStatePath = NULL;
	return stateRef;
}

void fsmStateReferenceDestroy(FSMStateReference *ref)
{
	REMOVE_HANDLE(ref->stateRef);
	MP_FREE(FSMStateReference, ref);
}


MP_DEFINE(FSMContext);

FSMContext* fsmContextCreate(FSM *fsm)
{
	FSMContext* context;
	int i;

	MP_CREATE(FSMContext, 16);
	context = MP_ALLOC(FSMContext);

	context->debugTransition = -1;
	context->debugTransitionLevel = -1;

	context->initialState = 1;
	context->timeElapsed = 1; // set a default time elapsed value so onFirstEntry works

	//added by cg to test generic FSM timer
	context->timer = 0; // reset fsm timer

	SET_HANDLE_FROM_REFERENT(gFSMDict, fsm, context->origFSM);

	if(fsm->onLoadStartState)
		eaPush(&context->curStateList, fsmStateReferenceCreate(fsm, fsm->onLoadStartState));
	else
		eaPush(&context->curStateList, fsmStateReferenceCreate(fsm, fsm->myStartState));

	// TODO: create curStateTracker here so the start state can have onEntry things referencing timers etc
	context->stateTrackerTable = stashTableCreateWithStringKeys(4, StashDefault);

	for(i = eaSize(&fsm->overrides)-1; i >= 0; i--)
	{
		FSMStateTrackerEntry* tracker = fsmStateTrackerEntryAdd(context, fsm->overrides[i]->statePath);
		SET_HANDLE_FROM_STRING(gFSMDict, fsm->overrides[i]->subFSMOverride, tracker->subFSMOverride);
		if(!GET_REF(tracker->subFSMOverride))
		{
			ErrorFilenamef(fsm->fileName, "Invalid sub fsm override %s", fsm->overrides[i]->subFSMOverride);
			REMOVE_HANDLE(tracker->subFSMOverride);
		}
	}

	return context;
}

void fsmCopyCurrentFSMHandle(FSMContext* context, ReferenceHandle *pDstHandle)
{
	if (IS_HANDLE_ACTIVE(context->origFSM))
	{
		RefSystem_CopyHandle(pDstHandle, REF_HANDLEPTR(context->origFSM));
	}
	else
	{
		RefSystem_RemoveHandle(pDstHandle);
	}
}

FSM* fsmGetByName(const char* fsmName)
{
	return (FSM*)RefSystem_ReferentFromString(gFSMDict, fsmName);
}

FSMContext* fsmContextCreateByName(const char* fsmName, const char* defaultFsm)
{
	FSM* fsm = NULL;
	FSMContext* context;
	
	PERFINFO_AUTO_START_FUNC();

	if(fsmName)
	{
		fsm = (FSM*)RefSystem_ReferentFromString(gFSMDict, fsmName);
		if(!fsm)
			Errorf("Unknown state machine '%s'", fsmName);
	}

	if(!fsm)
	{
		if(defaultFsm)
			fsm = (FSM*)RefSystem_ReferentFromString(gFSMDict, defaultFsm);

		if(!fsm)
		{
			PERFINFO_AUTO_STOP();
			return NULL;
		}
	}

	context = fsmContextCreate(fsm);

	PERFINFO_AUTO_STOP();

	return context;
}

void fsmContextDestroy(FSMContext* context)
{
	if(context->onDestroyCleanup)
	{
		CommandQueue_ExecuteAllCommands(context->onDestroyCleanup);
		CommandQueue_Destroy(context->onDestroyCleanup);
		context->onDestroyCleanup = NULL;
	}

	REMOVE_HANDLE(context->origFSM);
	eaDestroyEx(&context->curStateList, fsmStateReferenceDestroy);
	stashTableDestroyEx(context->stateTrackerTable, NULL, fsmStateTrackerEntryDestroy);
	MP_FREE(FSMContext, context);
}

void fsmContextSetFunctionTables(	FSMContext* context, 
									ExprFuncTable* ftStateAction, 
									ExprFuncTable* ftStateOnEntry, 
									ExprFuncTable* ftStateOnEntryFirst,
									ExprFuncTable* ftTransCondition,
									ExprFuncTable* ftTransAction)
{
	context->funcTables[FFTT_State_Action]			= ftStateAction;
	context->funcTables[FFTT_State_OnEntry]			= ftStateOnEntry;
	context->funcTables[FFTT_State_OnEntryFirst]	= ftStateOnEntryFirst;
	context->funcTables[FFTT_Trans_Condition]		= ftTransCondition;
	context->funcTables[FFTT_Trans_Action]			= ftTransAction;
}

void fsmContextEnableFlavorExpr(FSMContext* context)
{
	context->doFlavorExpr = 1;
}

const char* fsmGetState( FSMContext* fsmContext )
{
	FSMState* state=0;
	int arrayDepth=0;

	if(!fsmContext)
		return NULL;

	arrayDepth = eaSize(&fsmContext->curStateList);
	if(!arrayDepth)
		return NULL;

	state = GET_REF(fsmContext->curStateList[arrayDepth-1]->stateRef);

	if(state)
		return state->name;
	else
		return NULL;
}

void fsmExitState(FSMStateTrackerEntry* tracker)
{
	PERFINFO_AUTO_START_FUNC_L2();
	if(tracker && tracker->exitHandlers)
	{
		CommandQueue_ExecuteAllCommands(tracker->exitHandlers);
		CommandQueue_Destroy(tracker->exitHandlers);
		tracker->exitHandlers = NULL;
	}
	PERFINFO_AUTO_STOP_L2();
}

const char* fsmGetName(FSMContext* context, int level)
{
	FSMStateReference *stateRef = eaGet(&context->curStateList, level);

	if(!stateRef)
		return NULL;

	return stateRef->fsmName;
}

const char* fsmCreateStatePath(FSMContext* fsmContext, int level)
{
	int i;
	static char* path = NULL;

	if(level<0 || level>=eaSize(&fsmContext->curStateList))
		return NULL;

	if(fsmContext->curStateList[level]->fsmStatePath)
		return fsmContext->curStateList[level]->fsmStatePath;
	
	estrClear(&path);
	for(i = 0; i <= level; i++)
	{
		FSMStateReference *stateRef = fsmContext->curStateList[i];
		estrConcatf(&path, "%s", stateRef->fsmStateName);
		if(i < level)
			estrAppend2(&path, ",");
	}

	fsmContext->curStateList[level]->fsmStatePath = allocAddString(path);
	return fsmContext->curStateList[level]->fsmStatePath;
}

const char* fsmGetFullStatePath(FSMContext* fsmContext)
{
	int totalSize = eaSize(&fsmContext->curStateList);
	return fsmCreateStatePath(fsmContext, totalSize - 1);
}

FSMStateTrackerEntry* fsmGetStateTrackerFromPath(FSMContext* fsmContext, const char* statePath)
{
	FSMStateTrackerEntry* tracker;
	stashFindPointer(fsmContext->stateTrackerTable, statePath, &tracker);
	return tracker;
}

void fsmExitCurState(FSMContext* fsmContext)
{
	int numLayers = eaSize(&fsmContext->curStateList);
	int i;
	const char* statePath = NULL;

	// go through and call exit handlers for each level of the fsm hierarchy
	for(i = numLayers - 1; i >= 0; i--)
	{
		FSMStateTrackerEntry* tracker;
		statePath = fsmCreateStatePath(fsmContext, i);
		tracker = fsmGetStateTrackerFromPath(fsmContext, statePath);
		fsmExitState(tracker);
	}
}

void fsmResetExitHandlers(FSMContext* fsmContext)
{
	int numLayers = eaSize(&fsmContext->curStateList);
	int i;
	const char* statePath = NULL;

	for(i = numLayers - 1; i >= 0; i--)
	{
		FSMStateTrackerEntry* tracker;
		statePath = fsmCreateStatePath(fsmContext, i);
		tracker = fsmGetStateTrackerFromPath(fsmContext, statePath);

		if(tracker && tracker->exitHandlers)
		{
			CommandQueue_Destroy(tracker->exitHandlers);
			tracker->exitHandlers = NULL;
		}
	}
}

const char* curStateTrackerString;
int curStateTrackerHandle;

AUTO_RUN;
void fsmRegisterPooledString(void)
{
	curStateTrackerString = allocAddStaticString("CurStateTracker");
}

void fsmExecuteInternal(FSMContext* fsmContext, ExprContext* exprContext, int arrayDepth, int forceSwitchedState, int executeAction, int executeOnEntry);

__forceinline void fsmExecuteState(FSMContext* fsmContext, ExprContext* exprContext, int arrayDepth, FSMState* state,
					 int switchedState, int executeAction, int executeOnEntry)
{
	FSMStateTrackerEntry* tracker;
	int	numLayers = eaSize(&fsmContext->curStateList);
	const char* statePath = NULL;
	S64 time;

	PERFINFO_AUTO_START_FUNC_L2();

	if(arrayDepth < numLayers - 1)
	{
		fsmExecuteInternal(fsmContext, exprContext, arrayDepth + 1, switchedState, executeAction, executeOnEntry);
		PERFINFO_AUTO_STOP_L2();
		return;
	}
	else // This is the lowest level state in the current hierarchy, process it
	{
		MultiVal retval = {0};
		FSM* subFSM;
		int firstEntry = false;

		PERFINFO_AUTO_START_L2("createStatePath", 1);
		statePath = fsmCreateStatePath(fsmContext, numLayers-1);
		PERFINFO_AUTO_STOP_START_L2("getStateTrackerFromPath", 1);
		tracker = fsmGetStateTrackerFromPath(fsmContext, statePath);
		PERFINFO_AUTO_STOP_L2();
		
		if(!tracker)
			tracker = fsmStateTrackerEntryAdd(fsmContext, statePath);

		if(!tracker->lastRunTime)
			firstEntry = true;

		if(tracker != fsmContext->oldTracker && fsmContext->oldTracker)
		{
			exprContextCleanupPush(exprContext, NULL, &fsmContext->oldTracker->localData);
			fsmExitState(fsmContext->oldTracker);
			exprContextCleanupPop(exprContext);
		}

		exprContextCleanupPush(exprContext, &tracker->exitHandlers, &tracker->localData);

		exprContextSetPointerVarPooledCached(exprContext, curStateTrackerString, tracker,
			parse_FSMStateTrackerEntry, false, true, &curStateTrackerHandle);

		fsmContext->curTracker = tracker;

		if(switchedState)
		{
			// Do this before onEntry because it's counter-intuitive otherwise
			time = ABS_TIME;
			if(gPartitionTimeCallback && exprContext->partitionIsSet)
				time = gPartitionTimeCallback(exprContextGetPartition(exprContext));

			tracker->lastEntryTime = time;
		}

		if(firstEntry)
		{
			if(state->onFirstEntry && executeOnEntry)
			{
				PERFINFO_AUTO_START_L2("OnFirstEntry", 1);
				exprEvaluateWithFuncTable(state->onFirstEntry, exprContext, fsmContext->funcTables[FFTT_State_OnEntryFirst], &retval);
				PERFINFO_AUTO_STOP_L2();
			}
		}

		if(switchedState)
		{
			if(state->onEntry && executeOnEntry)
			{
				PERFINFO_AUTO_START_L2("OnEntry", 1);
				exprEvaluateWithFuncTable(state->onEntry, exprContext, fsmContext->funcTables[FFTT_State_OnEntry], &retval);
				PERFINFO_AUTO_STOP_L2();
			}
		}

		if((subFSM = GET_REF(tracker->subFSMOverride)))
		{
			PERFINFO_AUTO_START_L2("subFSMOverride", 1);
			eaPush(&fsmContext->curStateList, fsmStateReferenceCreate(subFSM, subFSM->myStartState));
			
			fsmExecuteInternal(fsmContext, exprContext, arrayDepth+1, switchedState, executeAction, executeOnEntry);
			PERFINFO_AUTO_STOP_L2();
		}
		else
		{
			subFSM = GET_REF(state->subFSM);

			if(subFSM)
			{
				PERFINFO_AUTO_START_L2("subFSM", 1);
				devassertmsg(arrayDepth == numLayers-1, "Switching states should make this the top layer");
				eaPush(&fsmContext->curStateList, fsmStateReferenceCreate(subFSM, subFSM->myStartState));

				fsmExecuteInternal(fsmContext, exprContext, arrayDepth+1, switchedState, executeAction, executeOnEntry);
				PERFINFO_AUTO_STOP_L2();
			}
			else if(state->action && executeAction)
			{
				PERFINFO_AUTO_START_L2("Action", 1);
				exprEvaluateWithFuncTable(state->action, exprContext, fsmContext->funcTables[FFTT_State_Action], &retval);
				PERFINFO_AUTO_STOP_L2();
			}
		}

		exprContextCleanupPop(exprContext);
	}

	tracker->totalTimeRun += fsmContext->timeElapsed;

	time = ABS_TIME;
	if(gPartitionTimeCallback && exprContext->partitionIsSet)
		time = gPartitionTimeCallback(exprContextGetPartition(exprContext));

	tracker->lastRunTime = time;

	PERFINFO_AUTO_STOP_L2();
}

void fsmExecuteInternal(FSMContext* fsmContext, ExprContext* exprContext, int arrayDepth, int forceSwitchedState, int executeAction, int executeOnEntry)
{
	FSMState* state = GET_REF(fsmContext->curStateList[arrayDepth]->stateRef);
	int i, num;
	int switchedState = forceSwitchedState;
	MultiVal retval = {0};

	PERFINFO_AUTO_START_FUNC_L2();

	if(!state)
	{
		FSM* orig = GET_REF(fsmContext->origFSM);

		eaClearEx(&fsmContext->curStateList, fsmStateReferenceDestroy);

		if(orig)
		{
			Alertf("You seem to have deleted or renamed a state that was in use,"
				"the current FSM will be reset to the FSM's start state"
				"please notify a programmer if this isn't the case");

			if(orig->onLoadStartState)
				eaPush(&fsmContext->curStateList, fsmStateReferenceCreate(orig, orig->onLoadStartState));
			else
				eaPush(&fsmContext->curStateList, fsmStateReferenceCreate(orig, orig->myStartState));

			state = GET_REF(fsmContext->curStateList[0]->stateRef);
		}
		else
		{
			Alertf("You seem to have deleted an FSM that was in use, whatever was using the"
				"FSM might not function properly anymore, please notify a programmer if"
				"this isn't the case");
		}

		PERFINFO_AUTO_STOP_L2();
		return;
	}

	// if this is the first time this context is executed, do the onEntry for the start state
	if(fsmContext->initialState)
	{
		fsmContext->initialState = 0;
		switchedState = true;
	}
	else if(!switchedState)
	{
		const char *transitionState = fsmCreateStatePath(fsmContext, arrayDepth);
		FSMStateTrackerEntry *transitionTracker = fsmStateTrackerEntryGet(fsmContext, transitionState);
		num = eaSize(&state->transitions);

		// Set up the tracker entry for the transitions checks specially, or else TimeSince transitions work
		// very poorly.
		exprContextSetPointerVarPooledCached(exprContext, curStateTrackerString, transitionTracker,
			parse_FSMStateTrackerEntry, false, true, &curStateTrackerHandle);

		fsmContext->curTracker = transitionTracker;
		exprContextCleanupPush(exprContext, NULL, &transitionTracker->localData);

		for(i = 0; i < num; i++)
		{
			PERFINFO_AUTO_START_L2("transition", 1);

			exprEvaluateWithFuncTable(state->transitions[i]->expr, exprContext, fsmContext->funcTables[FFTT_Trans_Condition], &retval);

			if(retval.type == MULTI_INVALID)
			{
				PERFINFO_AUTO_STOP_L2();
				continue;	// this error should've already been reported in the expression
							// evaluator, and is probably valid if it wasn't for that error
			}
			else if(retval.type != MULTI_INT)
			{
				ErrorDetailsf("FSM Name: %s, State: %s, Expr: %s", REF_STRING_FROM_HANDLE(fsmContext->origFSM), state->name, exprGetCompleteString(state->transitions[i]->expr));
				ErrorFilenamef(state->filename,
					"Transition expression returned non-int");
			}
			else if(QuickGetInt(&retval) || 
				(fsmContext->debugTransition==i && fsmContext->debugTransitionLevel==arrayDepth))
			{
				int num2;
				const char* targetName = state->transitions[i]->targetName;
				FSM* orig = GET_REF(fsmContext->origFSM);

				if(fsmContext->debugTransition==i)
				{
					fsmContext->debugTransitionLevel = -1;
					fsmContext->debugTransition = -1;
				}

				if(state->transitions[i]->action)
					exprEvaluateWithFuncTable(state->transitions[i]->action, exprContext, fsmContext->funcTables[FFTT_Trans_Action], &retval);

				while((num2 = eaSize(&fsmContext->curStateList)) > arrayDepth)
				{
					const char *statePath;
					FSMStateReference *stateRef;

					stateRef = eaPop(&fsmContext->curStateList);
					fsmStateReferenceDestroy(stateRef);

					if(num2>arrayDepth+1)
					{
						exprContextCleanupPush(exprContext, NULL, &fsmContext->oldTracker->localData);
						fsmExitState(fsmContext->oldTracker);
						exprContextCleanupPop(exprContext);

						statePath = fsmCreateStatePath(fsmContext, num2-2);					
						fsmContext->oldTracker = fsmStateTrackerEntryGet(fsmContext, statePath);
					}
				}

				eaPush(&fsmContext->curStateList, fsmStateReferenceCreate(orig, targetName));

				state = GET_REF(fsmContext->curStateList[arrayDepth]->stateRef);

				if(!devassertmsg(state, "Not having a state here means this transition is bad somehow..."))
				{
					PERFINFO_AUTO_STOP_L2();
					continue;  // Continue because it doesn't matter and might as well work 'mostly'
				}

				switchedState = true;
				fsmContext->changedState = 1;

				PERFINFO_AUTO_STOP_L2();
				break;
			}

			PERFINFO_AUTO_STOP_L2();
		}

		exprContextCleanupPop(exprContext);
	}

	PERFINFO_AUTO_STOP_L2();

	fsmExecuteState(fsmContext, exprContext, arrayDepth, state, switchedState, executeAction, executeOnEntry);
}

static int fsmValidate(FSMContext *fsmContext, ExprContext *exprContext)
{
	if(!devassertmsg(fsmContext, "Can't use fsmExecute without a context!"))
		return false;

	// this should only happen if an fsm gets removed when in use, see fsmExecuteInternal
	if(!eaSize(&fsmContext->curStateList))
		return false;

	return true;
}

int fsmExecuteEx(FSMContext *fsmContext, ExprContext *exprContext, int doAction, int doOnEntry)
{
	PERFINFO_AUTO_START_FUNC();

	if(!fsmValidate(fsmContext, exprContext))
		return false;

	// this was being set once during initialization before, but got out of sync with jobs
	exprContextSetFSMContext(exprContext, fsmContext);

	fsmContext->changedState = 0;
	fsmContext->oldTracker = fsmContext->curTracker;
	fsmExecuteInternal(fsmContext, exprContext, 0, false, doAction, doOnEntry);
	fsmContext->timer += fsmContext->timeElapsed;

	PERFINFO_AUTO_STOP();
	return fsmContext->changedState;
}

int fsmChangeState(FSMContext* fsmContext, const char* targetState)
{
	FSM* orig = GET_REF(fsmContext->origFSM);
	FSMStateReference* newStateRef;
	static char* statePath = NULL;

	if(!orig)
	{
		Errorf("Original FSM not found: %s", REF_STRING_FROM_HANDLE(fsmContext->origFSM));
		return 0;
	}

	estrPrintf(&statePath, "%s:%s", orig->name, targetState);

	newStateRef = fsmStateReferenceCreate(orig, statePath);
	if(!GET_REF(newStateRef->stateRef))
	{
		ErrorFilenamef(orig->fileName, "FSM %s has no state named %s in it", orig->name, targetState);
		fsmStateReferenceDestroy(newStateRef);
		return 0;
	}

	eaClearEx(&fsmContext->curStateList, fsmStateReferenceDestroy);
	eaPush(&fsmContext->curStateList, newStateRef);
	fsmContext->initialState = true;
	return 1;
}

static FSMReloadData* fsmGetGenerateData(FSM* fsm)
{
	int i;
	FSMReloadData* reloadData = NULL;

	for(i = eaSize(&allReloadData) - 1; i >= 0; i--)
	{
		if ((fsm->group && stricmp(allReloadData[i]->groupName, fsm->group) == 0) ||
			(!strnicmp(allReloadData[i]->path, fsm->fileName, strlen(allReloadData[i]->path))))
		{
			return allReloadData[i];
		}
	}

	if(!reloadData)
	{
		Errorf("FSM doesn't match any group that was loaded?!?");
		return NULL;
	}

	return reloadData;
}

static ExprFuncTable* fsmGetGenerateFuncTable(FSMReloadData *reloadData, FSMFuncTableType type)
{
	if(!reloadData)
		return NULL;

	switch(type)
	{
		xcase FFTT_State_Action: {
			return reloadData->funcTables.state_action;
		}
		xcase FFTT_State_OnEntry: {
			return reloadData->funcTables.state_onentry;
		}
		xcase FFTT_State_OnEntryFirst: {
			return reloadData->funcTables.state_onentryfirst;
		}
		xcase FFTT_Trans_Condition: {
			return reloadData->funcTables.trans_condition;
		}
		xcase FFTT_Trans_Action: {
			return reloadData->funcTables.trans_action;
		}
	}

	return reloadData->funcTables.state_action;
}

FSM* curFSM = NULL;

void fsmRegisterExternVarSCType(ExprContext* context, void* externVar, const char* staticCheckType, int scTypeCategory)
{
	FSMExternVar* var = externVar;
	var->scType = StructAllocString(staticCheckType);
	var->scTypeCategory = scTypeCategory;
}

int fsmRegisterExternVar(ExprContext* context, const char* category, const char* varName, const char* enablerVar,
						 const char* limitName, MultiValType type, void** userDataOut)
{
	FSMExternVar* var = NULL;
	
	if(!curFSM)
		return false;
	
	var = StructAlloc(parse_FSMExternVar);
	var->category = StructAllocString(category);
	var->name = StructAllocString(varName);
	var->enabler = enablerVar ? StructAllocString(enablerVar) : NULL;
	var->limitName = allocAddString(limitName);
	var->type = type;
	eaPush(&curFSM->externVarList, var);
	*userDataOut = var;
	return true;
}

int fsmGenerate(FSM* fsm)
{
	int i, j;
	int numStates = eaSize(&fsm->states);
	char* buf = NULL;
	char *estrTempName = NULL;
	StashTable stateNames;
	FSMReloadData *reloadData = fsmGetGenerateData(fsm);

	int success = true;

	if(!reloadData)
	{
		ErrorFilenamef( fsm->fileName, "FSM group not found: %s", fsm->group);
		return 0;
	}

	curFSM = fsm;

	if( !resIsValidName(fsm->name) )
	{
		ErrorFilenamef( fsm->fileName, "FSM name is illegal: '%s'", fsm->name );
		return 0;
	}

	if( !resIsValidScope(fsm->scope) )
	{
		ErrorFilenamef( fsm->fileName, "FSM scope is illegal: '%s'", fsm->scope );
		return 0;
	}

	resAddValueDep("FSMLoadVersion");

	fsmMakeFileName(fsm->name, fsm->scope, &estrTempName);
	if (stricmp(estrTempName, fsm->fileName) != 0) {
		ErrorFilenamef( fsm->fileName, "FSM filename does not match name '%s' scope '%s'", fsm->name, fsm->scope);
	}
	estrDestroy(&estrTempName);

	if(!numStates)
	{
		Errorf("State machine %s does not have any states", fsm->name);
		return 0;
	}

	// just running an FSM makes you at least cost 1
	fsm->cost = 1;

	estrStackCreate(&buf);

	stateNames = stashTableCreateWithStringKeys(4, StashDefault);

	for(i = 0; i < numStates; i++)
	{
		FSMState* curState = fsm->states[i];

		estrPrintf(&buf, "%s:%s", fsm->name, curState->name);
		curState->fullname = allocAddString(buf);

		if(!stashFindElement(stateNames, curState->fullname, NULL))
			stashAddPointer(stateNames, curState->fullname, curState, false);
		else
		{
			Errorf("Duplicate state %s found in FSM %s", curState->name, fsm->name);
			success = false;
		}
	}

	// the start state is the first state specified in the state machine and gets
	// added in the context using a reference string
	fsm->myStartState = allocAddString(fsm->states[0]->fullname);

	for(i = 0; i < numStates; i++)
	{
		FSMState* curState = fsm->states[i];
		int numtrans = eaSize(&curState->transitions);
		F32 exprCost;

		for(j = 0; j < numtrans; j++)
		{
			FSMTransition* curTrans = curState->transitions[j];
			const char *targetName = strrchr(curTrans->targetName, ':');

			// this happens during editing
			if(!curTrans->expr)
				continue;

			if(!targetName)
				targetName = curTrans->targetName;
			else
				targetName++;

			estrPrintf(&buf, "%s:%s", fsm->name, targetName);

			if(!stashFindElement(stateNames, buf, NULL))
			{
				Errorf("Transition to nonexistent state %s", buf);
				success = false;
			}

			curTrans->targetName = (char*)allocAddString(buf);

			// these get evaluated every tick, so as soon as we deem anything but movement
			// "expensive" it should be included (getting ents etc etc)
			exprContextSetFuncTable(reloadData->exprContext, fsmGetGenerateFuncTable(reloadData, FFTT_Trans_Condition));
			success &= exprGenerate(curTrans->expr, reloadData->exprContext);
			exprCost = exprGetCost(curTrans->expr);
			fsm->cost = MAX(fsm->cost, exprCost);

			// even though it might not be immediately obvious, designers can start anything
			// movement related in the onEntry or onFirstEntry (or a transition) and it'll
			// keep going until the state changes so the fsm cost has to look at all of these
			if(curTrans->action)
			{
				exprContextSetFuncTable(reloadData->exprContext, fsmGetGenerateFuncTable(reloadData, FFTT_Trans_Action));
				success &= exprGenerate(curTrans->action, reloadData->exprContext);
				exprCost = exprGetCost(curTrans->expr);
				fsm->cost = MAX(fsm->cost, exprCost);
			}
		}

		if(curState->onEntry)
		{
			exprContextSetFuncTable(reloadData->exprContext, fsmGetGenerateFuncTable(reloadData, FFTT_State_OnEntry));
			success &= exprGenerate(curState->onEntry, reloadData->exprContext);
			exprCost = exprGetCost(curState->onEntry);
			fsm->cost = MAX(fsm->cost, exprCost);
		}

		if(curState->onFirstEntry)
		{
			exprContextSetFuncTable(reloadData->exprContext, fsmGetGenerateFuncTable(reloadData, FFTT_State_OnEntryFirst));
			success &= exprGenerate(curState->onFirstEntry, reloadData->exprContext);
			exprCost = exprGetCost(curState->onFirstEntry);
			fsm->cost = MAX(fsm->cost, exprCost);
		}

		if(curState->action)
		{
			exprContextSetFuncTable(reloadData->exprContext, fsmGetGenerateFuncTable(reloadData, FFTT_State_Action));
			success &= exprGenerate(curState->action, reloadData->exprContext);
			exprCost = exprGetCost(curState->action);
			fsm->cost = MAX(fsm->cost, exprCost);
		}

		if(IS_HANDLE_ACTIVE(curState->subFSM))
		{
			FSMDependency* dep = StructAlloc(parse_FSMDependency);
			COPY_HANDLE(dep->fsm, curState->subFSM);
			eaPush(&fsm->dependencies, dep);
		}
	}

	for(i = eaSize(&fsm->overrides)-1; i >= 0; i--)
	{
		FSMDependency* dep;
		const char* colon;
		estrCopy2(&buf, fsm->overrides[i]->statePath);
		colon = strchr(buf, ':');
		if(!colon)
		{
			ErrorFilenamef(fsm->fileName, "Invalid sub FSM override string %s", buf);
			continue;
		}
		dep = StructAlloc(parse_FSMDependency);
		buf[colon-buf] = '\0';
		SET_HANDLE_FROM_STRING(gFSMDict, buf, dep->fsm);
		eaPush(&fsm->dependencies, dep);
	}

	estrDestroy(&buf);
	stashTableDestroy(stateNames);

	return success;
}

static FSMReloadData *fsmGetReloadDataFromScope(const char *scope) 
{
	char groupBuf[1024];
	int i;
	char *ptr;
	
	if (!scope)
		return NULL;

	groupBuf[0] = '\0';
	ptr = strchr(scope, '/');
	if (ptr)
	{
		strncpy(groupBuf, scope, ptr-scope);
		groupBuf[ptr-scope] = '\0';
	}
	else
	{
		strcpy(groupBuf, scope);
	}

	for(i = eaSize(&allReloadData) - 1; i >= 0; i--) {
		if(!strnicmp(allReloadData[i]->groupName, groupBuf, strlen(allReloadData[i]->groupName))) {
			return allReloadData[i];
		}
	}
	return NULL;
}

static bool fsmMakeFileName(const char *name, const char *scope, char **estr)
{
	const char *parsedScope = NULL;
	char *estrNewScope = NULL;
	FSMReloadData *reloadData;
	char ns[RESOURCE_NAME_MAX_SIZE], base[RESOURCE_NAME_MAX_SIZE];

	if (!name || !strlen(name)) 
	{
		estrPrintf(estr, "Name is not currently legal");
		return false;
	}

	// Remove illegal characters in the scope
	if (resFixScope(scope, &estrNewScope))
	{
		scope = estrNewScope;
	}

	// Determine group from the scope
	reloadData = fsmGetReloadDataFromScope(scope);
	if (!reloadData)
	{
		estrPrintf(estr, "Scope is not currently legal");
		estrDestroy(&estrNewScope);
		return false;
	}

	// Remove group name from the parsed version of the scope
	parsedScope = scope + strlen(reloadData->groupName);
	if (*parsedScope == '/')
		++parsedScope;

	estrPrintf(estr, "%s", reloadData->path);
	estrConcatChar(estr, '/');
	if (parsedScope)
	{
		estrConcat(estr, parsedScope, (int)strlen(parsedScope));
		estrConcatChar(estr, '/');
	}
	if(resExtractNameSpace(name, ns, base))
	{
		estrPrintf(estr, NAMESPACE_PATH"%s/%s/%s/%s", ns, reloadData->path, parsedScope, base);
	}
	else
	{
		estrConcat(estr, name, (int)strlen(name));
	}
	estrConcat(estr, ".fsm", 4);

	estrFixFilename(estr);

	estrDestroy(&estrNewScope);

	return true;
}

void fsmAssignGroupAndScope(FSM *fsm)
{
	FSMReloadData* reloadData = NULL;
	int i;

	const char *name = NULL;
	const char *path = NULL;
	for(i = eaSize(&allReloadData) - 1; i >= 0; i--) {
		if(!strnicmp(allReloadData[i]->path, fsm->fileName, strlen(allReloadData[i]->path))) {
			reloadData = allReloadData[i];
			break;
		}
	}
	if (!reloadData) {
		Alertf("FSM group name not found for %s\n", fsm->fileName);
		return;
	}
	if (!fsm->group && reloadData->groupName) {
		fsm->group = (char*)allocAddString(reloadData->groupName);
	}
	if (!fsm->scope && reloadData->path) {
		char buf[1024];
		char *ptr;
		strcpy(buf, fsm->group);
		ptr = strrchr(fsm->fileName + strlen(reloadData->path) + 1, '/');
		if (ptr) {
			strcat(buf, "/");
			strcat(buf, fsm->fileName + strlen(reloadData->path) + 1);
			ptr = strrchr(buf, '/');
			*ptr = '\0';
		}
		fsm->scope = (char*)allocAddString(buf);
	}
}

// This gets called on save via the dictionary and is the equivalent of
// a text read fixup function's behavior when reading from file.
static int fsmReferenceModifyCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, void *pResource, U32 userID)
{
	FSM *fsm = pResource;
	switch (eType)
	{
	xcase RESVALIDATE_POST_TEXT_READING:
		if (!fsm->group || !fsm->scope)
			fsmAssignGroupAndScope(fsm);
		fsmGenerate((FSM*)fsm);
		return VALIDATE_HANDLED;

	xcase RESVALIDATE_FIX_FILENAME:
		{			
			char *newFile = NULL;

			if (!fsmMakeFileName(fsm->name, fsm->scope, &newFile)) 
			{
				// newFile contains error if returned false
				ErrorFilenamef(fsm->fileName, "%s", newFile);
			}
			else if (stricmp(newFile, fsm->fileName) != 0)
			{
				fsm->fileName = (char*)allocAddString(newFile);
			}

			estrDestroy(&newFile);
		}
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
int RegisterFSMDictionaries(void)
{
	gFSMDict = RefSystem_RegisterSelfDefiningDictionary("FSM", false, parse_FSM, true, true, "FSM:");

	gFSMStateDict = RefSystem_RegisterSelfDefiningDictionary("FSMStateDict", false, parse_FSMState, true, false, NULL);

	resDictManageValidation(gFSMDict, fsmReferenceModifyCB);

	if (IsServer())
	{
		resDictProvideMissingResources(gFSMDict);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(gFSMDict, ".Name", ".Scope", ".tags", NULL, NULL);
		}
		ParserBinRegisterDepValue("FSMLoadVersion", 6);
	}
	else
	{
		// Server loading client
		resDictRequestMissingResources(gFSMDict, 8, false, resClientRequestSendReferentCommand);
	}
	resDictProvideMissingRequiresEditMode(gFSMDict);

	return 1;
}

static void fsmDictChangeHandler(enumResourceEventType eType, const char *pDictName, const char *name, FSM* fsm, void *userData)
{
	if ((eType == RESEVENT_RESOURCE_ADDED) ||
		(eType == RESEVENT_RESOURCE_MODIFIED))
	{
		int i, numStates = eaSize(&fsm->states);

		// Add States
		for(i = 0; i < numStates; i++)
			RefSystem_AddReferent(gFSMStateDict, fsm->states[i]->fullname, fsm->states[i]);
	}
	else if ((eType == RESEVENT_RESOURCE_REMOVED) ||
			 (eType == RESEVENT_RESOURCE_PRE_MODIFIED))
	{
		int i, numStates = eaSize(&fsm->states);

		// Remove States
		for(i = 0; i < numStates; i++)
			RefSystem_RemoveReferent(fsm->states[i], false);
	}
}

void fsmLoad(	const char* groupName, 
				const char* path, 
				const char* binName, 
				ExprContext* context, 
				ExprFuncTable* state_action,
				ExprFuncTable* state_onentry, 
				ExprFuncTable* state_onentryfirst, 
				ExprFuncTable* trans_condition, 
				ExprFuncTable* trans_action)
{
	FSMReloadData* reloadData = calloc(1, sizeof(FSMReloadData));
	int oldSize = RefSystem_GetDictionaryNumberOfReferents(gFSMDict);

	loadstart_printf("Loading %s state machines.. ", groupName);

	reloadData->groupName						= groupName;
	reloadData->path							= path;
	reloadData->funcTables.state_action			= state_action;
	reloadData->funcTables.state_onentry		= state_onentry;
	reloadData->funcTables.state_onentryfirst	= state_onentryfirst;
	reloadData->funcTables.trans_condition		= trans_condition;
	reloadData->funcTables.trans_action			= trans_action;
	reloadData->exprContext						= context;
	eaPush(&allReloadData, reloadData);

	resDictRegisterEventCallback(gFSMDict, fsmDictChangeHandler, NULL);

	resLoadResourcesFromDisk(gFSMDict, path, ".fsm", binName, PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY | RESOURCELOAD_USERDATA);

	loadend_printf("done (%d state machines).", RefSystem_GetDictionaryNumberOfReferents(gFSMDict) - oldSize);
}

static void fsmGetExternVarNamesRecursive_Internal(SA_PARAM_NN_VALID FSM *fsm, FSMExternVar*** fsmVarList, const char *category, FSM*** expandedFSMs)
{
	int varIdx, numVars = eaSize(&fsm->externVarList);
	int depIdx, numDeps = eaSize(&fsm->dependencies);

	// If we've already explored this FSM, return
	if (eaFind(expandedFSMs, fsm) >= 0)
		return;

	eaPush(expandedFSMs, fsm);

	// Add all the extern vars to the list
	for (varIdx = 0; varIdx < numVars; varIdx++)
	{
		FSMExternVar* fsmVar = fsm->externVarList[varIdx];
		if (fsmVar->name && fsmVar->category && !stricmp(category, fsmVar->category))
		{
			// Only add unique strings
			int i, n = eaSize(fsmVarList);
			bool found = false;
			for (i = 0; i < n; i++)
			{
				if (!stricmp((*fsmVarList)[i]->name, fsmVar->name))
				{
					found = true;
					break;
				}
			}
			if (!found)
			{
				eaPush(fsmVarList, fsmVar);
			}
		}
	}
	
	// Add extern vars of all dependencies
	for (depIdx = 0; depIdx < numDeps; depIdx++)
	{
		FSMDependency *dependency = fsm->dependencies[depIdx];
		FSM *depFSM = GET_REF(dependency->fsm);
		if (depFSM)
			fsmGetExternVarNamesRecursive_Internal(depFSM, fsmVarList, category, expandedFSMs);
	}
}

void fsmGetExternVarNamesRecursive(SA_PARAM_NN_VALID FSM *fsm, FSMExternVar*** fsmVarList, const char *category)
{
	FSM** expandedFSMs = NULL; // prevents infinite recursion
	fsmGetExternVarNamesRecursive_Internal(fsm, fsmVarList, category, &expandedFSMs);
	eaDestroy(&expandedFSMs);
}

static FSMExternVar* fsmExternVarFromName_Internal(SA_PARAM_NN_VALID FSM *fsm, const char* fsmVarName, const char *category, FSM*** expandedFSMs)
{
	int varIdx, numVars = eaSize(&fsm->externVarList);
	int depIdx, numDeps = eaSize(&fsm->dependencies);

	// If we've already explored this FSM, return
	if (eaFind(expandedFSMs, fsm) >= 0)
		return NULL;

	eaPush(expandedFSMs, fsm);

	// search for the externvar
	for (varIdx = 0; varIdx < numVars; varIdx++)
	{
		FSMExternVar* fsmVar = fsm->externVarList[varIdx];
		if (fsmVar->name && !stricmp(fsmVar->name, fsmVarName) && (!fsmVar->category || !stricmp(category, fsmVar->category)))
			return fsmVar;
	}
	
	// Not found, search dependencies recursively
	for (depIdx = 0; (depIdx < numDeps); depIdx++)
	{
		FSMDependency *dependency = fsm->dependencies[depIdx];
		FSM *depFSM = GET_REF(dependency->fsm);
		if (depFSM)
		{
			FSMExternVar* fsmVar = fsmExternVarFromName_Internal(depFSM, fsmVarName, category, expandedFSMs);
			if (fsmVar)
				return fsmVar;
		}
	}

	return NULL;
}

FSMExternVar* fsmExternVarFromName(SA_PARAM_NN_VALID FSM *fsm, const char *fsmVarName, const char *category)
{
	FSM** expandedFSMs = NULL; // prevents infinite recursion
	FSMExternVar *var = fsmExternVarFromName_Internal(fsm, fsmVarName, category, &expandedFSMs);
	eaDestroy(&expandedFSMs);
	return var;
}

static bool fsm_GetAudioAssets_HandleString(const char *pcFilename, const char *pcString, const char ***peaStrings)
{
	bool bResourceHasAudio = false;

	if (pcString)
	{
		const char *pcParse = pcString;

		//if (strstri(pcParse,"PlayMusic(")					||
		//	strstri(pcParse,"ReplaceMusic(")				||
		//	strstri(pcParse,"PlayVoiceSetSound(")			||
		//	strstri(pcParse,"PlayMusicEvent(")				||
		//	strstri(pcParse,"PlayOneShotSound(")			||
		//	strstri(pcParse,"PlayOneShotSoundFromEntity(")	||
		//	strstri(pcParse,"PlaySound(")					)
		//{
		//	printf("%s\n",pcParse);
		//}

		while (*pcParse)
		{
			S32 iSkip = 0;

			if      (strnicmp(pcParse, "PlayMusic(",					10) == 0) iSkip = 10;
			else if (strnicmp(pcParse, "ReplaceMusic(",					13) == 0) iSkip = 13;
			else if (strnicmp(pcParse, "PlayVoiceSetSound(",			18) == 0) iSkip = 18;
			else if (strnicmp(pcParse, "PlayMusicEvent(",				15) == 0) iSkip = 15;
			else if (strnicmp(pcParse, "PlayOneShotSound(",				17)	== 0) iSkip = 17;
			else if (strnicmp(pcParse, "PlayOneShotSoundFromEntity(",	27) == 0) iSkip = 27;
			else if (strnicmp(pcParse, "PlaySound(",					10) == 0) iSkip = 10;

			if (iSkip > 0)
			{
				//skip any white space
				while ( pcParse[iSkip] == ' ' ||
						pcParse[iSkip] == '\t')
				{
					iSkip++;
				}

				if (strnicmp(pcParse+iSkip, "GetExternStringVar(", 19) == 0)
				{
					Errorf("%s : GetExternStringVar not currently supported for FSM expressions that trigger audio (see line: %s)", pcFilename, pcString);
				}
				else if (pcParse[iSkip] == '\"')
				{
					const char *pcCopyFrom = pcParse + iSkip + 1;
					S32 iStrLength = 0;

					//determine the length of the audio signal
					while (	'A'  <= pcCopyFrom[iStrLength] && pcCopyFrom[iStrLength] <= 'Z' ||
							'a'  <= pcCopyFrom[iStrLength] && pcCopyFrom[iStrLength] <= 'z' ||
							'0'  <= pcCopyFrom[iStrLength] && pcCopyFrom[iStrLength] <= '9' ||
							'_'  == pcCopyFrom[iStrLength] ||
							'\'' == pcCopyFrom[iStrLength] ||
							'/'  == pcCopyFrom[iStrLength] ||
							' '  == pcCopyFrom[iStrLength] ||
							'\t' == pcCopyFrom[iStrLength] )
					{
						iStrLength++;
					}

					//add the word
					if (iStrLength > 0 &&
						pcCopyFrom[iStrLength] == '\"')
					{
						//remove any trailing whitespace
						while (	0 <= iStrLength-1 &&
								(	pcCopyFrom[iStrLength-1] == ' ' ||
									pcCopyFrom[iStrLength-1] == '\t' ))
						{
							iStrLength--;
						}

						if (iStrLength > 0)
						{
							char *pcNewString;
							S32 iCopy;

							pcNewString = malloc(iStrLength+1);
							for (iCopy = 0; iCopy < iStrLength; iCopy++) {
								pcNewString[iCopy] = pcCopyFrom[iCopy];
							}
							pcNewString[iStrLength] = '\0';
							eaPush(peaStrings, pcNewString);
							bResourceHasAudio = true;

							//printfColor(COLOR_BRIGHT,"%s\n",pcNewString);
						}
						else
						{
							Errorf("%s : Found poorly formated sound tag in message %s\n", pcFilename, pcString);
						}
					}
					else
					{
						Errorf("%s : Found poorly formated sound tag in message %s\n", pcFilename, pcString);
					}
				}
				else
				{
					Errorf("%s : Found poorly formated sound tag in message %s\n", pcFilename, pcString);
				}
			}

			pcParse++;
		}
	}

	return bResourceHasAudio;
}

static bool fsm_GetAudioAssets_HandleExpression(const char *pcFilename, Expression *pExpression, const char ***peaStrings)
{
	bool bResourceHasAudio = false;
	if (pExpression) {
		FOR_EACH_IN_EARRAY(pExpression->lines, ExprLine, pExprLine)
		{
			bResourceHasAudio |= fsm_GetAudioAssets_HandleString(pcFilename, pExprLine->origStr, peaStrings);
		}
		FOR_EACH_END;
	}
	return bResourceHasAudio;
}

void fsm_GetAudioAssets(const char **ppcType, const char ***peaStrings, U32 *puiNumData, U32 *puiNumDataWithAudio)
{
	FSM *pFSM;
	ResourceIterator rI;

	*ppcType = strdup("FSMs");

	resInitIterator(gFSMDict, &rI);
	while (resIteratorGetNext(&rI, NULL, &pFSM))
	{
		bool bResourceHasAudio = false;

		FOR_EACH_IN_EARRAY(pFSM->states, FSMState, pFSMState) 
		{
			bResourceHasAudio |= fsm_GetAudioAssets_HandleExpression(pFSM->fileName, pFSMState->action,				peaStrings);
			bResourceHasAudio |= fsm_GetAudioAssets_HandleExpression(pFSM->fileName, pFSMState->actionFlavor,		peaStrings);
			bResourceHasAudio |= fsm_GetAudioAssets_HandleExpression(pFSM->fileName, pFSMState->onEntry,			peaStrings);
			bResourceHasAudio |= fsm_GetAudioAssets_HandleExpression(pFSM->fileName, pFSMState->onEntryFlavor,		peaStrings);
			bResourceHasAudio |= fsm_GetAudioAssets_HandleExpression(pFSM->fileName, pFSMState->onFirstEntry,		peaStrings);
			bResourceHasAudio |= fsm_GetAudioAssets_HandleExpression(pFSM->fileName, pFSMState->onFirstEntryFlavor,	peaStrings);
			FOR_EACH_IN_EARRAY(pFSMState->transitions, FSMTransition, pFSMTransition) {
				bResourceHasAudio |= fsm_GetAudioAssets_HandleExpression(pFSM->fileName, pFSMTransition->expr,		peaStrings);
				bResourceHasAudio |= fsm_GetAudioAssets_HandleExpression(pFSM->fileName, pFSMTransition->action,	peaStrings);
			} FOR_EACH_END;
		}
		FOR_EACH_END;

		*puiNumData = *puiNumData + 1;
		if (false) {
			*puiNumDataWithAudio = *puiNumDataWithAudio + 1;
		}
	}
	resFreeIterator(&rI);
}

#include "StateMachine_h_ast.c"

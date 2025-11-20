#ifndef STATEMACHINE_H
#define STATEMACHINE_H
GCC_SYSTEM

#include "MultiVal.h"
#include "referencesystem.h"

typedef struct CommandQueue			CommandQueue;
typedef struct Expression			Expression;
typedef struct ExprContext			ExprContext;
typedef struct ExprLocalData		ExprLocalData;
typedef struct ExprFuncTable		ExprFuncTable;
typedef struct FSM					FSM;
typedef struct FSMState				FSMState;
typedef struct StashTableImp*		StashTable;

typedef enum FSMFuncTableType {
	FFTT_State_Action,
	FFTT_State_OnEntry,
	FFTT_State_OnEntryFirst,
	FFTT_Trans_Condition,
	FFTT_Trans_Action,
	FFTT_MAX,
} FSMFuncTableType;

AUTO_STRUCT;
typedef struct FSMTransitionLayout
{
	bool open;
} FSMTransitionLayout;

AUTO_STRUCT;
typedef struct FSMTransition
{
	const char* targetName;			AST(NAME("Target","Target:") POOL_STRING)
	Expression* expr;				AST(NAME("Expr"), REDUNDANT_STRUCT("Expr:", parse_Expression_StructParam), LATEBIND)
	Expression* action; 			AST(NAME("Action"), REDUNDANT_STRUCT("Action:", parse_Expression_StructParam), LATEBIND)

	FSMTransitionLayout *layout;	AST(NAME("Layout"))
}FSMTransition;

AUTO_STRUCT;
typedef struct FSMStateLayout
{
	int x;							// x position
	int y;							// y position

	bool onFirstEntryOpen;			// onFirstEntry expander state
	bool onEntryOpen;				// onEntry expander state
	bool actionOpen;				// action expander state
	bool subFSMOpen;				// subFSM expander state
} FSMStateLayout;


AUTO_STRUCT;
typedef struct FSMState
{
	const char* name;				AST(STRUCTPARAM POOL_STRING)
	const char* fullname;			AST(NO_TEXT_SAVE POOL_STRING)

	FSMTransition** transitions;	AST(NAME(Transition))

	char *comment;					AST(NAME("Comment"))

	Expression* onEntry;			AST(NAME("OnEntry"), REDUNDANT_STRUCT("OnEntry:", parse_Expression_StructParam), LATEBIND)
	Expression* onEntryFlavor;		AST(NAME("OnEntryFlavor"), REDUNDANT_STRUCT("OnEntryFlavor:", parse_Expression_StructParam), LATEBIND)

	Expression* onFirstEntry;		AST(NAME("OnFirstEntry"), REDUNDANT_STRUCT("OnFirstEntry:", parse_Expression_StructParam), LATEBIND)
	Expression* onFirstEntryFlavor; AST(NAME("OnFirstEntryFlavor"), REDUNDANT_STRUCT("OnFirstEntryFlavor:", parse_Expression_StructParam), LATEBIND)

	Expression* action;				AST(NAME("Action"), REDUNDANT_STRUCT("Action:", parse_Expression_StructParam), LATEBIND)
	Expression* actionFlavor;		AST(NAME("ActionFlavor"), REDUNDANT_STRUCT("ActionFlavor:", parse_Expression_StructParam), LATEBIND)

	REF_TO(FSM) subFSM;				AST(NAME("SubFSM","SubFSM:"), REFDICT(FSM), VITAL_REF)

	char* filename;					AST(CURRENTFILE)

	FSMStateLayout *layout;			AST(NAME("Layout"))
}FSMState;

AUTO_STRUCT;
typedef struct FSMStates
{
	FSMState **ppStates;
} FSMStates;

AUTO_STRUCT;
typedef struct FSMExternVar
{
	const char* category;
	const char* name;
	const char* enabler;			// Name of the var (same category) that must be true before this one is checked
	MultiValType type;				AST(INT)
	const char* scType;
	int scTypeCategory;

	// In editor, suggestion.  In UGC, enforced.  For strings, it's character count.
	const char* limitName;			AST( POOL_STRING )// reference to ExprFuncArgLimit struct
}FSMExternVar;
extern ParseTable parse_FSMExternVar[];
#define TYPE_parse_FSMExternVar FSMExternVar

AUTO_STRUCT;
typedef struct FSMOverrideMapping
{
	const char* statePath;			AST(POOL_STRING NAME("StatePath","StatePath:") POOL_STRING )
	const char* subFSMOverride;		AST(POOL_STRING NAME("SubFSMOverride", "SubFSMOverride:") POOL_STRING)
}FSMOverrideMapping;

AUTO_STRUCT;
typedef struct FSMDependency
{
	REF_TO(FSM) fsm;				AST(REFDICT(FSM))
}FSMDependency;

AUTO_STRUCT AST_IGNORE_STRUCT(externVarList) AST_IGNORE(flavorCost) AST_IGNORE(Tags) AST_IGNORE_STRUCT(UGCProperties);
typedef struct FSM
{
	const char* name;				AST(STRUCTPARAM, NAME("Name","Name:"), KEY POOL_STRING)
	const char* fileName;			AST(CURRENTFILE)

	const char* group;				AST( POOL_STRING ) // The group determines the runtime context for the FSM
	const char* scope;				AST( POOL_STRING ) // The scope is used for UI tree organization

	char *comment;					AST(NAME("Comment"))

	// has the reference dictionary string for which state to load if someone has this fsm
	// this is only for FSMs that are always embedded in something else, and should be NULL otherwise
	const char* onLoadStartState;	AST(NAME("OnLoadStartState","OnLoadStartState:") POOL_STRING)
	// has the reference dictionary string for the start state when someone does this particular fsm
	const char* myStartState;		AST(NAME("") POOL_STRING)
	FSMState** states;				AST(NAME("State", "State:"))

	FSMOverrideMapping** overrides;	AST(NAME("Override","Override:"))

	FSMExternVar** externVarList;	AST(NAME("ExternVar"), NO_TEXT_SAVE)
	FSMDependency** dependencies;	AST(NO_TEXT_SAVE)

	F32 cost;						AST(NO_TEXT_SAVE)
} FSM;

extern ParseTable parse_FSMTransitionLayout[];
#define TYPE_parse_FSMTransitionLayout FSMTransitionLayout
extern ParseTable parse_FSMTransition[];
#define TYPE_parse_FSMTransition FSMTransition
extern ParseTable parse_FSMStateLayout[];
#define TYPE_parse_FSMStateLayout FSMStateLayout
extern ParseTable parse_FSMState[];
#define TYPE_parse_FSMState FSMState
extern ParseTable parse_FSMStates[];
#define TYPE_parse_FSMStates FSMStates
extern ParseTable parse_FSM[];
#define TYPE_parse_FSM FSM
extern ParseTable parse_FSMOverrideMapping[];
#define TYPE_parse_FSMOverrideMapping FSMOverrideMapping

typedef struct FSMStateTrackerEntry FSMStateTrackerEntry;

AUTO_STRUCT;
typedef struct FSMStateTrackerEntry
{
	S64 totalTimeRun;
	S64 lastEntryTime;	// updated after onEntry and onFirstEntry expressions are processed
						// to allow "time since this state was last entered" behavior
	S64 lastRunTime;

	REF_TO(FSM) subFSMOverride;		AST(REFDICT(FSM))

	ExprLocalData** localData;		NO_AST
	CommandQueue* exitHandlers;		NO_AST

	const char* statePath;
}FSMStateTrackerEntry;

typedef struct FSMStateReference
{
	REF_TO(FSMState) stateRef;
	const char* fsmName;
	const char* fsmStateName;
	const char* fsmStatePath;
} FSMStateReference;

AUTO_STRUCT;
typedef struct FSMContext
{
	FSMStateReference** curStateList;	NO_AST
	StashTable stateTrackerTable;		NO_AST	// instead of the array of states, just have a hashtable
												// of FSMState reference data strings to tracker entries

	ExprFuncTable* funcTables[FFTT_MAX];NO_AST	// The various function tables to use for specific evaluations

	FSMStateTrackerEntry* oldTracker;	AST(UNOWNED) // needed for exit handler processing
	FSMStateTrackerEntry* curTracker;	AST(UNOWNED)

	CommandQueue* onDestroyCleanup;		NO_AST
	StashTable messages;				NO_AST // aiMessages

	REF_TO(FSM) origFSM;
	U32 timeElapsed;
	U32 timer;

	int debugTransition;
	int debugTransitionLevel;

	U32 changedState : 1;
	U32 initialState : 1;
	U32 doFlavorExpr : 1;
	U32 messageRecvd : 1;			// TODO(AM): Move this to AIVarsBase
}FSMContext;

extern DictionaryHandle gFSMDict;

typedef void (*ExprLocalDataDestroyCallback)(ExprLocalData *data);
typedef S64 (*FSMPartitionTimeCallback)(int partition);

void fsmSetPartitionTimeCallback(FSMPartitionTimeCallback cb);

void fsmLocalDataDestroy(ExprLocalData* data);
void fsmSetLocalDataDestroyFunc(ExprLocalDataDestroyCallback cb);

SA_RET_NN_VALID FSMContext* fsmContextCreate(SA_PARAM_NN_VALID FSM *fsm);
SA_RET_OP_VALID FSMContext* fsmContextCreateByName(const char* fsmName, const char* defaultFsm);
void fsmCopyCurrentFSMHandle(SA_PARAM_NN_VALID FSMContext* context, SA_PARAM_NN_VALID ReferenceHandle *pDstHandle);
FSM* fsmGetByName(const char* fsmName);

void fsmContextDestroy(SA_PRE_NN_VALID SA_POST_P_FREE FSMContext* context);
void fsmContextEnableFlavorExpr(SA_PARAM_NN_VALID FSMContext* context);

void fsmStateReferenceDestroy(FSMStateReference *ref);
FSMStateReference *fsmStateReferenceCreate(FSM *fsm, const char *stateName);

int fsmGenerate(SA_PARAM_NN_VALID FSM* fsm);
void fsmLoad(SA_PARAM_NN_STR const char* groupName, 
			 SA_PARAM_NN_STR const char* path, 
			 SA_PARAM_OP_STR const char* binName, 
			 SA_PARAM_NN_VALID ExprContext *exprContext,
			 SA_PARAM_NN_VALID ExprFuncTable* state_action, 
			 SA_PARAM_NN_VALID ExprFuncTable* state_onentry, 
			 SA_PARAM_NN_VALID ExprFuncTable* state_onentryfirst, 
			 SA_PARAM_NN_VALID ExprFuncTable* trans_condition, 
			 SA_PARAM_NN_VALID ExprFuncTable* trans_action);
#define fsmExecute(fsmContext, exprContext) fsmExecuteEx(fsmContext, exprContext, true, true)
int fsmExecuteEx(SA_PARAM_NN_VALID FSMContext *fsmContext, SA_PARAM_NN_VALID ExprContext *exprContext, int doAction, int doOnEntry);
SA_RET_OP_STR const char* fsmGetState(SA_PARAM_NN_VALID FSMContext* fsm);
SA_RET_OP_STR const char* fsmGetFullStatePath(SA_PARAM_NN_VALID FSMContext* fsmContext);

SA_RET_OP_STR const char* fsmGetName(SA_PARAM_NN_VALID FSMContext* context, int level);

void fsmExecuteState(FSMContext* fsmContext, ExprContext* exprContext, int arrayDepth,
					 FSMState* state, int switchedState, int executeAction, int executeOnEntry);

int fsmRegisterExternVar(SA_PARAM_NN_VALID ExprContext* context, SA_PARAM_NN_STR const char* category,
						SA_PARAM_NN_STR const char* varname, SA_PARAM_OP_STR const char* varEnabler, 
						SA_PARAM_OP_STR const char* limitName,
						MultiValType type, SA_PRE_NN_VALID SA_POST_NN_RELEMS(1) void** userDataOut);
void fsmRegisterExternVarSCType(SA_PARAM_NN_VALID ExprContext* context, SA_PARAM_NN_VALID void* externVar,
						SA_PARAM_OP_STR const char* staticCheckType, int scTypeCategory);

// needed for things like ai jobs, which have to call the exit handlers on a state without
// the state actually switching through the FSM
void fsmExitCurState(SA_PARAM_NN_VALID FSMContext* fsmContext);

// this goes through and deletes the command queues on all current states
void fsmResetExitHandlers(SA_PARAM_NN_VALID FSMContext* fsmContext);

// Gets a list of all extern vars on this FSM and its dependencies
void fsmGetExternVarNamesRecursive(SA_PARAM_NN_VALID FSM *fsm, FSMExternVar*** fsmVarList, const char *category);

// Gets the specified var from the FSM, also searching dependencies
FSMExternVar* fsmExternVarFromName(SA_PARAM_NN_VALID FSM *fsm, const char *fsmVarName, const char *category);

void fsmAssignGroupAndScope(FSM *fsm);

// Changes the FSMContext to now be on the specified state (has to be a substate of original FSM)
// returns success
int fsmChangeState(SA_PARAM_NN_VALID FSMContext* context, SA_PARAM_NN_VALID const char* targetState);

void fsm_GetAudioAssets(const char **ppcType, const char ***peaStrings, U32 *puiNumData, U32 *puiNumDataWithAudio);

#endif

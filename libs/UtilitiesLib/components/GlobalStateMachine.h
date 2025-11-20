#pragma once
GCC_SYSTEM

#define GSM_STATENAME_MAX_LENGTH 64

//global states can be referred to two ways:
//(1) By string (name) (slower)
//(2) By GlobalStateHandle (faster)
//
//All the Global State Machine functions are set up to take either type of argument, and automatically
//figure out how to treat it
typedef char *GlobalStateHandle;

//a define to make it super-obvious from looking at the function prototype that it takes a handle or string
#define GlobalStateHandleOrName GlobalStateHandle 

//the generic callback functions used by all states for all types of things
typedef void GlobalStateMachineCB(void);

//callback used by the GSM to get the time Step Scale
typedef float GSM_GetTimeSetScaleCB(void);

//sets the time step scale callback - optional
void GSM_SetTimeSetScaleCB(GSM_GetTimeSetScaleCB *pSetScaleCB);


//an optional callback that is called whenever there is a change in the state stack
typedef void GSM_StatesChangedCB(char *pStateString);

//sets the callback to call when global states change
void GSM_SetStatesChangedCB(GSM_StatesChangedCB *pStatesChangedCB);

//an optional callback that is called for every individual state that is left/entered
typedef void GSM_IndividualStateEnterLeaveCB(bool bEnter, const char *pIndividualStateName);

//sets the callback to call whenever any state is entered/left (this is called at the same time
//enter/leave callbacks would be called, but it's a single callback passed the name of the state, useful
//for global transition-time processing
void GSM_SetIndividualStateEnterLeaveCB(GSM_IndividualStateEnterLeaveCB *pCB);


//adds a global state, and sets whether it is the (single unique) startup state
GlobalStateHandle GSM_AddGlobalState(char *pStateName);

//gets the handle to an already existing state, or NULL if it doesn't exist
GlobalStateHandle GSM_GetHandleFromName(char *pStateName);

//don't call this
void GSM_AddGlobalStateCallbacks_Internal(GlobalStateHandleOrName hState,
	GlobalStateMachineCB *pEnterStateCB,
	GlobalStateMachineCB *pBeginFrameCB,
	GlobalStateMachineCB *pEndFrameCB,
	GlobalStateMachineCB *pLeaveStateCB, bool bFromApp);

//adds the callbacks for a global state
__forceinline static void GSM_AddGlobalStateCallbacks(GlobalStateHandleOrName hState,
	GlobalStateMachineCB *pEnterStateCB,
	GlobalStateMachineCB *pBeginFrameCB,
	GlobalStateMachineCB *pEndFrameCB,
	GlobalStateMachineCB *pLeaveStateCB)
{
	GSM_AddGlobalStateCallbacks_Internal(hState, pEnterStateCB, pBeginFrameCB, pEndFrameCB, pLeaveStateCB,
#ifdef _LIB
	false
#else
	true
#endif
	);
}

#define GSM_AddGlobalStateCallbacksLib(hState, pEnterStateCB, pBeginFrameCB, pEndFrameCB, pLeaveStateCB) GSM_AddGlobalStateCallbacks_Internal(hState, pEnterStateCB, pBeginFrameCB, pEndFrameCB, pLeaveStateCB, false)
#define GSM_AddGlobalStateCallbacksApp(hState, pEnterStateCB, pBeginFrameCB, pEndFrameCB, pLeaveStateCB) GSM_AddGlobalStateCallbacks_Internal(hState, pEnterStateCB, pBeginFrameCB, pEndFrameCB, pLeaveStateCB, true)


//do not call the _int versions of the state change functions. Use the macros which include
//file/line info for logging
void GSM_AddChildState_int(GlobalStateHandleOrName hState, bool bOverwriteCurrentChildIfAny, char *pFileName, int iLineNum);
void GSM_CloseCurState_int(bool bKillChildrenIfAny, char *pFileName, int iLineNum);
void GSM_SwitchToSibling_int(GlobalStateHandleOrName hState, bool bKillChildrenIfAny, char *pFileName, int iLineNum);
void GSM_KillAllChildStates_int(char *pFileName, int iLineNum);
void GSM_ResetState_int(GlobalStateHandleOrName hState, char *pFileName, int iLineNum);

void GSM_SwitchToState_Complex_int(char *pFullStateName, char *pFileName, int iLineNum);

void GSM_CancelRequestedTransitions_int(char *pFileName, int iLineNum);

#define GSM_AddChildState(hState, bOverwrite) GSM_AddChildState_int(hState, bOverwrite, __FILE__, __LINE__)
#define GSM_CloseCurState(bKillChildrenIfAny) GSM_CloseCurState_int(bKillChildrenIfAny, __FILE__, __LINE__)
#define GSM_SwitchToSibling(hState, bKillChildrenIfAny) GSM_SwitchToSibling_int(hState, bKillChildrenIfAny, __FILE__, __LINE__)
#define GSM_KillAllChildStates() GSM_KillAllChildStates_int(__FILE__, __LINE__)
#define GSM_SwitchToState_Complex(pFullStateName) GSM_SwitchToState_Complex_int(pFullStateName, __FILE__, __LINE__)
#define GSM_CancelRequestedTransitions() GSM_CancelRequestedTransitions_int(__FILE__, __LINE__)
#define GSM_ResetState(hState) GSM_ResetState_int(hState, __FILE__, __LINE__)



void GSM_Execute(char *pStateName);

char *GSM_GetCurActiveStateName(void);

void GSM_PutFullStateStackIntoEString(char **ppEString);
void GSM_PutFullNextStateStackIntoEString(char **ppEString);

bool GSM_IsRunning(void);

//returns true if anyone has made any state-transition requests this frame
bool GSM_AreAnyStateChangesRequested(void);

//returns true if the named state is anywhere in the current stack of states, false otherwise
bool GSM_IsStateActive(GlobalStateHandleOrName hState);

//returns true if the named state is anywhere in the current stack of states, false otherwise or if the state doesn't exist
bool GSM_IsStateRealAndActive(GlobalStateHandleOrName hState);

//returns true if the named state is anywhere in the current stack of states or 
//next frame's stack of states, false otherwise
bool GSM_IsStateActiveOrPending(GlobalStateHandleOrName hState);


//for these four functions, pass in a NULL handle to indicate "the current state"
bool GSM_DoesStateHaveChildStates(GlobalStateHandleOrName hState);
//both of these return -1 if the state is not active
int GSM_FramesInState(GlobalStateHandleOrName hState);
float GSM_TimeInStateIncludeCurrentFrame(GlobalStateHandleOrName hState);
float GSM_TimeInState(GlobalStateHandleOrName hState);
void GSM_ResetStateTimers(GlobalStateHandleOrName hState);

void GSM_Quit(char *pReason, ...);
bool GSM_IsQuitting(void);


//NOTE: Any server that uses GSM and links with ServerLib (ie, anything but the game client) should
//probably call void slSetGSMReportsStateToController(void);


//sets up a hook so that whenever a transition would happen from a state stack matching
//pFrom to a state stack matching pTo, instead switch to pNewTo. If pFrom is NULL, then
//any transition to pTo counts.
void GSM_SetTransitionOverride(char *pFrom, char *pTo, char *pNewTo);

typedef struct Packet Packet;
//tells the GSM that, the next time it switches states, it should send this packet.
//(This works fine with multiple packets... they're put in an earray internally)
void GSM_DestroyStateTransitionPackets(void);
void GSM_SendPacketOnStateTransition(Packet *pPacket);

bool GSM_StateContains(char *pState);

// An instanced state machine is based on the model of the InstancedStateMachine, 
// but you can have multiple of them and they are much simpler
#pragma once
GCC_SYSTEM

#define ISM_NAME_MAX_LENGTH 64

// Handles for machine and state names are just char *'s, but may be integers
typedef char *InstancedStateHandle;

// Define for handles/names of a full state machine
#define InstancedMachineHandleOrName InstancedStateHandle 

// Define for handles/names of individual states
#define InstancedStateHandleOrName InstancedStateHandle 


// State Registration Functions
// Deal with initial creation of states, associated with a given state machine
// When you add a state with callbacks, it will automatically register the proper State Machine for you (and state)

// Typedef of callbacks you register with the ISM
typedef void InstancedStateMachineCB(InstancedMachineHandleOrName pStateMachineHandle, void *pUserObject, F32 fElapsed);

//an optional callback called whenever a state machine switches states, so you can track what state you are in
//from the outside world easily
typedef void InstanceStateMachineStateChangeDebugCB(void *pUserObject, char *pStateString);

void ISM_SetNewStateDebugCB(InstancedMachineHandleOrName pStateMachineHandle, InstanceStateMachineStateChangeDebugCB *pCB);


//don't call this
InstancedStateHandle ISM_AddInstancedState_Internal(InstancedMachineHandleOrName pStateMachineHandle,
											 InstancedStateHandleOrName hState,
											 InstancedStateMachineCB *pEnterStateCB,
											 InstancedStateMachineCB *pBeginFrameCB,
										     InstancedStateMachineCB *pEndFrameCB,
										     InstancedStateMachineCB *pLeaveStateCB, bool bFromApp);

//adds the callbacks for a instanced state
__forceinline static InstancedStateHandle ISM_AddInstancedState(InstancedMachineHandleOrName pStateMachineHandle,
														 InstancedStateHandleOrName hState,
														 InstancedStateMachineCB *pEnterStateCB,
														 InstancedStateMachineCB *pBeginFrameCB,
														 InstancedStateMachineCB *pEndFrameCB,
														 InstancedStateMachineCB *pLeaveStateCB)
{
	return ISM_AddInstancedState_Internal(pStateMachineHandle, hState, pEnterStateCB, pBeginFrameCB, pEndFrameCB, pLeaveStateCB,
#ifdef _LIB
		false
#else
		true
#endif
		);
}

// State transition functions, for changes within an specific instance of a machine

// Simplest function, switch to a sibling in the state tree
bool ISM_SwitchToSibling(InstancedMachineHandleOrName pStateMachineHandle, void *pUserObject, InstancedStateHandleOrName hState);

// For all more complicated transitions, use the complex form, which is documented in the GSM wiki page
bool ISM_SwitchToState_Complex(InstancedMachineHandleOrName pStateMachineHandle, void *pUserObject, char *pFullStateName);

// Cancel any pending transitions
bool ISM_CancelRequestedTransitions(InstancedMachineHandleOrName pStateMachineHandle, void *pUserObject);

//returns true if anyone has made any state-transition requests this frame
bool ISM_AreAnyStateChangesRequested(InstancedMachineHandleOrName pStateMachineHandle, void *pUserObject);

// General use functions

// Attempts to create a new state machine of the specified type, bound to the passed in user object
// Returns failure if creation failed, normally due to the userObject already having a bound state machine
// Initial state is set to the passed in state string (which may contain nested states)
bool ISM_CreateMachine(InstancedMachineHandleOrName pStateMachineHandle, void *pUserObject, char* initialState);

// Attempts to destroy an existing ISM. Returns failure if not successful, probably because it doesn't exist
bool ISM_DestroyMachine(InstancedMachineHandleOrName pStateMachineHandle, void *pUserObject);

// Runs one tick of the specified ISM
// First, it handles any pending state transitions
// Then, it runs the ticks for the state stack
void ISM_Tick(InstancedMachineHandleOrName pStateMachineHandle, void *pUserObject, F32 fElapsed);

//Runs on tick of all the ISMs of the given type.
void ISM_TickAll(InstancedMachineHandleOrName pStateMachineHandle, F32 fElapsed);

// Returns true if the named state is anywhere in the current stack of states, false otherwise
bool ISM_IsStateActive(InstancedMachineHandleOrName pStateMachineHandle, void *pUserObject, InstancedStateHandleOrName hState);

// Returns true if the named state is anywhere in the current stack of states or 
// next frame's stack of states, false otherwise
bool ISM_IsStateActiveOrPending(InstancedMachineHandleOrName pStateMachineHandle, void *pUserObject, InstancedStateHandleOrName hState);

// For debugging purposes, write out the state stack. Returns false on error
bool ISM_PutFullStateStackIntoEString(InstancedMachineHandleOrName pStateMachineHandle, void *pUserObject, char **ppEString);
bool ISM_PutFullNextStateStackIntoEString(InstancedMachineHandleOrName pStateMachineHandle, void *pUserObject, char **ppEString);

typedef void ISM_LoggingCallback(void *pUserObject, char **ppEString);

//for debugging purposes, print out and log all state transitions for this machine. The optional callback function is passed
//the user object and dumps info about it into an estring.
void ISM_BeginDebugLogging(InstancedMachineHandleOrName pStateMachineHandle, void *pUserObject, ISM_LoggingCallback *pCallback);


U32 ISM_TimeEnteredCurrentState(InstancedMachineHandleOrName pStateMachineHandle, void *pUserObject, char **ppCurrentStateName);
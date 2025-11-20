#ifndef AIMESSAGES_H
#define AIMESSAGES_H

/*
 An AI message is a message used by the AI FSMs and some other FSMs to communicate.  The messages
 are stored in the FSM context, and FSMs can remember all messages they've gotten recently.
 Critters with AIs can't send messages past a certain distance.
 Since FSMs are polled rather than event-driven, messages are also how they receive game events.
 Each game event sends a message when it occurs, which the FSM can then receive on its next processing
 tick.
 */

#define AIMESSAGE_MEMORY_TIME 120

typedef struct AIMessageEntry
{
	S64 time;
	F32 value;
}AIMessageEntry;

typedef struct AIMessage
{
	const char* tag;
	AIMessageEntry** entries;
	EntityRef* refArray;
	U32 totalCount;
	F32 totalValue;

	S64 timeLastPruneCheck;
	S64 timeLastReceived;
}AIMessage;

typedef struct AIMessage		AIMessage;
typedef struct AIVarsBase		AIVarsBase;
typedef struct Entity			Entity;
typedef struct FSMLDSendMessage	FSMLDSendMessage;
typedef struct FSMContext		FSMContext;

void aiMessageSendAbstract(int partitionIdx, FSMContext* fsmContext, const char* tag, Entity* source,
						   F32 value, Entity*** entArrayData);
int aiMessageCheck(FSMContext* fsmContext, const char* tag, AIMessage** retMsg);
int aiMessageCheckLastAbsTime(int partitionIdx, FSMContext* fsmContext, const char* tag, S64 absTimeDiff);
int aiMessageCheckLastXSec(int partitionIdx, FSMContext* aib, const char* tag, F32 sec);
F32 aiMessageCheckLastAbsTimeValue(int partitionIdx, FSMContext* fsmContext, const char* tag, S64 absTimeDiff);
F32 aiMessageCheckLastXSecValue(int partitionIdx, FSMContext* aib, const char* tag, F32 sec);

void aiMessageProcessChat(Entity* e, Entity *src, const char *msg);

int aiNotify_ShouldIgnoreApplyIDDueToRespawn(SA_PARAM_NN_VALID Entity* sourceBE, U32 uiApplyID);

#endif
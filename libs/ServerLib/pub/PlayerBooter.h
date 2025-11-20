#pragma once
#include "GlobalTypes.h"
/*A PlayerBooter is a little FSM which very persistently attempts to boot an entity to the object DB. It should
deal properly with all the nasty cases where the entity is in the middle of map transfer, etc. It works by sending a remote
command, which is dealt with via latelink differently on different kinds of servers, and then retrying until it succeeds*/



AUTO_STRUCT;
typedef struct PlayerBooterResult
{
	U32 iTime;
	bool bSucceeded;
	char *pResultString; AST(ESTRING)
} PlayerBooterResult;


AUTO_STRUCT;
typedef struct PlayerBooterResults
{
	bool bFinallySucceeded;
	PlayerBooterResult **ppResults;
} PlayerBooterResults;


typedef void PlayerBooterResultCB(PlayerBooterResults *pResults, void *pUserData);

//CALL THIS
void BootPlayerWithBooter(ContainerID iEntityID, PlayerBooterResultCB *pResultCB, void *pUserData, const char *pReason);


//DO NOT CALL THIS
LATELINK;
void AttemptToBootPlayerWithBooter(ContainerID iPlayerToBootID, U32 iHandle, char *pReason);

void PlayerBooterAttemptReturn(U32 iHandle, bool bSucceeded, char *pComment, ...);

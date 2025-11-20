#include "PlayerBooter.h"
#include "Earray.h"
#include "TextParser.h"
#include "PlayerBooter_h_ast.h"
#include "estring.h"
#include "AutoGen/ServerLib_autogen_remotefuncs.h"
#include "AutoGen/ServerLib_autogen_slowfuncs.h"
#include "timedCallback.h"

//__CATEGORY Stuff relating to player booters
//After this many failed attempts to boot a player, give up and alert
static int siBooterFailAttempts = 10;
AUTO_CMD_INT(siBooterFailAttempts, BooterFailAttempts) ACMD_AUTO_SETTING(PlayerBooter, LOGINSERVER);

//wait this many seconds after the first booting attempt, then twice this long after the second one, three times after
//the third, etc.
static float sfBooterInitialDelay = 0.5f;
AUTO_CMD_INT(sfBooterInitialDelay, BooterInitialDelay) ACMD_AUTO_SETTING(PlayerBooter, LOGINSERVER);


typedef struct
{
	ContainerID iEntityID;
	PlayerBooterResultCB *pCB; 
	PlayerBooterResults results;
	U32 iCreationTime;
	char *pReason;
	void *pUserData;
} PlayerBooter;


void DEFAULT_LATELINK_AttemptToBootPlayerWithBooter(ContainerID iPlayerToBootID, U32 iHandle, char *pReason)
{
	PlayerBooterAttemptReturn(iHandle, false, "%s[%u] has no idea how to do player booting",
		GlobalTypeToName(GetAppGlobalType()), GetAppGlobalID());
}

AUTO_COMMAND_REMOTE_SLOW(PlayerBooterResult*);
void AttemptToBootPlayerWithBooter_Outer(ContainerID iPlayerToBootID, SlowRemoteCommandID iID, char *pReason)
{
	AttemptToBootPlayerWithBooter(iPlayerToBootID, iID, pReason);
}


void AttemptToBootPlayer_CB(TransactionReturnVal *returnVal, void *userData);

void PlayerBooter_ReAttempt(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	PlayerBooter *pBooter = (PlayerBooter*)userData;

	RemoteCommand_AttemptToBootPlayerWithBooter_Outer(objCreateManagedReturnVal(AttemptToBootPlayer_CB, pBooter), GLOBALTYPE_ENTITYPLAYER, pBooter->iEntityID, pBooter->iEntityID, pBooter->pReason);
}


static void AttemptToBootPlayer_CB(TransactionReturnVal *returnVal, void *userData)
{
	U32 iCurTime = timeSecondsSince2000();
	PlayerBooterResult *pResult;
	PlayerBooter *pBooter = userData;

	switch(RemoteCommandCheck_AttemptToBootPlayerWithBooter_Outer(returnVal, &pResult))
	{	
	case TRANSACTION_OUTCOME_SUCCESS:
		pResult->iTime = iCurTime;
		eaPush(&pBooter->results.ppResults, pResult);

		if (pResult->bSucceeded)
		{
			pBooter->results.bFinallySucceeded = true;
			if (pBooter->pCB)
			{
				pBooter->pCB(&pBooter->results, pBooter->pUserData);
			}

			StructDeInit(parse_PlayerBooterResults, &pBooter->results);
			free(pBooter->pReason);
			free(pBooter);
			return;
		}
		break;


		
	case TRANSACTION_OUTCOME_FAILURE:
		pResult = StructCreate(parse_PlayerBooterResult);
		pResult->bSucceeded = false;
		pResult->iTime = timeSecondsSince2000();
		estrPrintf(&pResult->pResultString, "Booting transaction failed. Failure string: %s", GetTransactionFailureString(returnVal));
		eaPush(&pBooter->results.ppResults, pResult);
		break;

	}


	if (eaSize(&pBooter->results.ppResults) > siBooterFailAttempts)
	{
		pBooter->results.bFinallySucceeded = false;
		if (pBooter->pCB)
		{
			pBooter->pCB(&pBooter->results, pBooter->pUserData);
		}
		StructDeInit(parse_PlayerBooterResults, &pBooter->results);
		free(pBooter->pReason);
		free(pBooter);
	}
	else
	{
		TimedCallback_Run(PlayerBooter_ReAttempt, pBooter, sfBooterInitialDelay * eaSize(&pBooter->results.ppResults));
	}
}



void BootPlayerWithBooter(ContainerID iEntityID, PlayerBooterResultCB *pResultCB, void *pUserData, const char *pReason)
{
	PlayerBooter *pBooter = calloc(sizeof(PlayerBooter), 1);

	pBooter->iEntityID = iEntityID;
	pBooter->pCB = pResultCB;
	StructInit(parse_PlayerBooterResults, &pBooter->results);
	pBooter->iCreationTime = timeSecondsSince2000();
	pBooter->pReason = strdup(pReason);
	pBooter->pUserData = pUserData;

	RemoteCommand_AttemptToBootPlayerWithBooter_Outer(objCreateManagedReturnVal(AttemptToBootPlayer_CB, pBooter), GLOBALTYPE_ENTITYPLAYER, iEntityID, iEntityID, pReason);
}

void PlayerBooterAttemptReturn(U32 iHandle, bool bSucceeded, char *pComment, ...)
{
	
	PlayerBooterResult *pResult = StructCreate(parse_PlayerBooterResult);

	estrGetVarArgs(&pResult->pResultString, pComment);

	pResult->bSucceeded = bSucceeded;

	SlowRemoteCommandReturn_AttemptToBootPlayerWithBooter_Outer(iHandle, pResult);
	StructDestroy(parse_PlayerBooterResult, pResult);
}

void PlayerBooterTestResultCB(PlayerBooterResults *pResults, void *pUserData)
{
	int iBrk = 0;
}

AUTO_COMMAND;
void PlayerBooterTest(int iID)
{
	BootPlayerWithBooter(iID, PlayerBooterTestResultCB, NULL, "PlayerBooterTest");
}




#include "PlayerBooter_h_ast.c"

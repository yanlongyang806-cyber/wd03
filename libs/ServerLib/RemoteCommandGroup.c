#include "RemoteCommandGroup.h"
#include "LocalTransactionManager.h"
#include "RemoteCommandGroup_h_ast.h"
#include "RemoteCommandGroup_c_ast.h"
#include "objTransactions.h"
#include "timedCallback.h"
#include "Alerts.h"
#include "winInclude.h"
#include "ControllerScriptingSupport.h"

#define REMOTECOMMANDGROUP_NUM_RETRIES 5

typedef struct RemoteCommandGroup_GroupRetryer
{
	int iRemainingCount;
	RemoteCommandGroupDoneExecutingFunc pCB;
	void *pUserData;
} RemoteCommandGroup_GroupRetryer;

AUTO_STRUCT;
typedef struct RemoteCommandGroup_SingleCommand_Retryer
{
	RemoteCommandGroup_SingleCommand *pCommand;
	int iCount;
	RemoteCommandGroup_GroupRetryer *pGroupRetryer; NO_AST
} RemoteCommandGroup_SingleCommand_Retryer;




void RetryRemoteCommand(RemoteCommandGroup_SingleCommand_Retryer *pRetryer);


void RetryRemoteCommandCB(TimedCallback *callback, F32 timeSinceLastCallback, RemoteCommandGroup_SingleCommand_Retryer *pRetryer)
{
	RetryRemoteCommand(pRetryer);
}



static void RetryRemoteCommandComplete(TransactionReturnVal *returnVal, RemoteCommandGroup_SingleCommand_Retryer *pRetryer)
{
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_FAILURE)
	{
		pRetryer->iCount++;

		if (pRetryer->iCount < REMOTECOMMANDGROUP_NUM_RETRIES)
		{
			TimedCallback_Run(RetryRemoteCommandCB, pRetryer, 20.0f);
			return;
		}
		else
		{
			ErrorOrAlert("RGC_RETRY_FAILURE", "After %d retries, still unable to execute \"%s\" on %s as part of a remote command group",
				REMOTECOMMANDGROUP_NUM_RETRIES, pRetryer->pCommand->pCommandString, 
				GlobalTypeAndIDToString(pRetryer->pCommand->eServerTypeToExecuteOn, pRetryer->pCommand->iServerIDToExecuteOn));
		}
	}
	if (pRetryer->pGroupRetryer)
	{
		pRetryer->pGroupRetryer->iRemainingCount--;
		if (pRetryer->pGroupRetryer->iRemainingCount == 0)
		{
			pRetryer->pGroupRetryer->pCB(pRetryer->pGroupRetryer->pUserData);
			free(pRetryer->pGroupRetryer);
		}
	}

	StructDestroy(parse_RemoteCommandGroup_SingleCommand_Retryer, pRetryer);	
}


void RetryRemoteCommand(RemoteCommandGroup_SingleCommand_Retryer *pRetryer)
{

	char *pCommandString = NULL;
	char *pCommandDebugNameString = NULL;
	BaseTransaction baseTransaction;
	BaseTransaction **ppBaseTransactions = NULL;
	estrStackCreateSize(&pCommandString, 4096);
	estrStackCreateSize(&pCommandDebugNameString, 4096);

	estrCopy2(&pCommandDebugNameString, pRetryer->pCommand->pCommandString);
	estrTruncateAtFirstOccurrence(&pCommandDebugNameString, ' ');

	estrConcatf(&pCommandString, "%sremotecommand %s", pRetryer->pCommand->bCommandIsSlow ? "slow" : "", pRetryer->pCommand->pCommandString);

	baseTransaction.pData = pCommandString;
	baseTransaction.recipient.containerID = pRetryer->pCommand->iServerIDToExecuteOn;
	baseTransaction.recipient.containerType = pRetryer->pCommand->eServerTypeToExecuteOn;
	baseTransaction.pRequestedTransVariableNames = NULL;
	eaPush(&ppBaseTransactions, &baseTransaction);
	

	RequestNewTransaction( objLocalManager(), pCommandDebugNameString, ppBaseTransactions, TRANS_TYPE_SEQUENTIAL_ATOMIC, objCreateManagedReturnVal(RetryRemoteCommandComplete, pRetryer), 0);

	eaDestroy(&ppBaseTransactions);
	estrDestroy(&pCommandDebugNameString);
	estrDestroy(&pCommandString);
}

void ExecuteAndFreeRemoteCommandGroup(RemoteCommandGroup *pGroup, RemoteCommandGroupDoneExecutingFunc pCB, void *pUserData)
{
	int i;
	RemoteCommandGroup_GroupRetryer *pGroupRetryer = NULL;

	if (objLocalManager())
	{
		if (pCB)
		{
			pGroupRetryer = calloc(sizeof(RemoteCommandGroup_GroupRetryer), 1);
			pGroupRetryer->pCB = pCB;
			pGroupRetryer->pUserData = pUserData;
			pGroupRetryer->iRemainingCount = eaSize(&pGroup->ppCommands);
		}


		for (i=0; i < eaSize(&pGroup->ppCommands); i++)
		{
			RemoteCommandGroup_SingleCommand_Retryer *pRetryer = StructCreate(parse_RemoteCommandGroup_SingleCommand_Retryer);

			pRetryer->pCommand = pGroup->ppCommands[i];
			pRetryer->pGroupRetryer = pGroupRetryer;	


			RetryRemoteCommand(pRetryer);
		}
	}

	eaDestroy(&pGroup->ppCommands);
	StructDestroy(parse_RemoteCommandGroup, pGroup);
}



RemoteCommandGroup *CreateRCGWithPrintf(FORMAT_STR const char *pFmt, ...)
{
	RemoteCommandGroup *pGroup = CreateEmptyRemoteCommandGroup();
	char *pTemp = NULL;
	RemoteCommandGroup_SingleCommand *pCommand = StructCreate(parse_RemoteCommandGroup_SingleCommand);
	eaPush(&pGroup->ppCommands, pCommand);

	pCommand->eServerTypeToExecuteOn = GetAppGlobalType();
	estrGetVarArgs(&pTemp, pFmt);
	estrPrintf(&pCommand->pCommandString, "PrintWithReturn \"");
	estrAppendEscaped(&pCommand->pCommandString, pTemp);
	estrConcatf(&pCommand->pCommandString, "\"");
	estrDestroy(&pTemp);

	return pGroup;
}
	
void GetRemoteCommandGroupKeyString(char outString[32], GlobalType eSubmitterServerType, ContainerID iSubmitterContainerID, U32 iUidFromSubmitter)
{
	//id first, as it will be most variable, so strcmps can fail as quickly as possible
	snprintf_s(outString, 32, "%u %u %u", iUidFromSubmitter, eSubmitterServerType, iSubmitterContainerID);
}

RemoteCommandGroup *CreateEmptyRemoteCommandGroup(void)
{
	static U32 iNextID = 0;
	RemoteCommandGroup *pGroup = StructCreate(parse_RemoteCommandGroup);
	pGroup->eSubmitterServerType = GetAppGlobalType();
	pGroup->iSubmitterContainerID = GetAppGlobalID();


	pGroup->iUidFromSubmitter = InterlockedIncrement(&iNextID);

	GetRemoteCommandGroupKeyString(pGroup->keyString, pGroup->eSubmitterServerType, pGroup->iSubmitterContainerID,
		pGroup->iUidFromSubmitter);

	return pGroup;
}

#undef AddCommandToRemoteCommandGroup
void AddCommandToRemoteCommandGroup(RemoteCommandGroup *pGroup, GlobalType eTypeToExecuteOn, ContainerID iIDToExecuteOn, bool bCommandIsSlow,
	FORMAT_STR const char* pCommandFmt, ...)
{
	RemoteCommandGroup_SingleCommand *pCommand = StructCreate(parse_RemoteCommandGroup_SingleCommand);

	pCommand->eServerTypeToExecuteOn = eTypeToExecuteOn;
	pCommand->iServerIDToExecuteOn = iIDToExecuteOn;
	pCommand->bCommandIsSlow = bCommandIsSlow;

	estrGetVarArgs(&pCommand->pCommandString, pCommandFmt);
	
	eaPush(&pGroup->ppCommands, pCommand);
}

void DestroyRemoteCommandGroup(RemoteCommandGroup *pGroup)
{
	if (pGroup)
	{
		StructDestroy(parse_RemoteCommandGroup, pGroup);
	}
}



void AddCommandToRemoteCommandGroup_SentNPCEmail(RemoteCommandGroup *pGroup, ContainerID iAccountID, ContainerID vshardID, const char *pAccountName, const char *pFrom, const char *pTitle, const char *pBody)
{
	RemoteCommandGroup_SingleCommand *pCommand = StructCreate(parse_RemoteCommandGroup_SingleCommand);
	pCommand->eServerTypeToExecuteOn = GLOBALTYPE_CHATSERVER;
	pCommand->iServerIDToExecuteOn = 0;
	estrPrintf(&pCommand->pCommandString, "ChatServerSendNPCEmail_Simple %u %u \"", iAccountID, vshardID);
	estrAppendEscaped(&pCommand->pCommandString, pAccountName ? pAccountName : "");
	estrConcatf(&pCommand->pCommandString, "\" \"");
	estrAppendEscaped(&pCommand->pCommandString, pFrom ? pFrom : "");
	estrConcatf(&pCommand->pCommandString, "\" \"");
	estrAppendEscaped(&pCommand->pCommandString, pTitle ? pTitle : "");
	estrConcatf(&pCommand->pCommandString, "\" \"");
	estrAppendEscaped(&pCommand->pCommandString, pBody ? pBody : "");
	estrConcatf(&pCommand->pCommandString, "\"");

	eaPush(&pGroup->ppCommands, pCommand);
}



//returning AUTO_COMMAND versions so that it can be called from remote command groups
AUTO_COMMAND_REMOTE;
int ControllerScript_Succeeded_RC(void)
{
	ControllerScript_Succeeded();
	return 1;
}
AUTO_COMMAND_REMOTE;
int ControllerScript_Failed_RC(char *pStr)
{
	ControllerScript_Failed(pStr);
	return 1;
}



#include "RemoteCommandGroup_h_ast.c"
#include "RemoteCommandGroup_c_ast.c"

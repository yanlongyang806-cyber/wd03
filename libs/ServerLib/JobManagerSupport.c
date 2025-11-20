//stuff for servers that are launched by the Job manager and need to communicate their status back to it

#include "error.h"
#include "globalTypes.h"
#include "GlobalStateMachine.h"
#include "JobManagerSupport.h"
#include "aslJobManagerPub_h_ast.h"
#include "timedCallback.h"
#include "EString.h"
#include "TextParser.h"
#include "../../CrossRoads/appserverlib/pub/aslJobManagerPub.h"
#include "AutoGen/Controller_autogen_remotefuncs.h"
#include "Autogen/ServerLib_autogen_SlowFuncs.h"
#include "rand.h"

#define PROJ_SPECIFIC_COMMANDS_ONLY 1
#define Job_MANAGER_SUPPORT 1

#include "../../CrossRoads/common/autogen/AppServerLib_autogen_RemoteFuncs.h"

static char *spJobName = NULL;

static JobGroupOwner sJobOwner = {0};

AUTO_COMMAND ACMD_COMMANDLINE;
void SetJobOwner(char *pSuperEscString)
{
	char *pString = NULL;
	estrSuperUnescapeString(&pString, pSuperEscString);
	ParserReadText(pString, parse_JobGroupOwner, &sJobOwner, 0);
	estrDestroy(&pString);
}

AUTO_COMMAND ACMD_COMMANDLINE;
void NameForJobManager(ACMD_SENTENCE pName)
{
	estrCopy2(&spJobName, pName);
}


char *GetCurJobNameForJobManager(void)
{
	return spJobName;
}

JobGroupOwner *GetOwnerOfCurrentJob(void)
{
	return &sJobOwner;
}

void JobManagerUpdate_Status(int iPercentComplete, char *pString_In, ...)
{
	static char *spStringToUse = NULL;

	if (pString_In)
	{
		estrGetVarArgs(&spStringToUse, pString_In);
	}
	else
	{
		estrPrintf(&spStringToUse, "(Unspecified)");
	}
	if (!spJobName)
	{
		AssertOrAlert("INVALID_JOB_UPDATE", "Trying to update a Job manager Job while not running one");
		return;
	}

	if (iPercentComplete < 0 || iPercentComplete > 100)
	{
		static bool bAlertedAlready = false;

		if (!bAlertedAlready)
		{
			bAlertedAlready = true;
			AssertOrAlert("BAD_JOB_UPDATE_PERCENT", "While updating job %s with string %s, percent %d was specified. This is out of range",
				spJobName, spStringToUse, iPercentComplete);
		}
	}

	RemoteCommand_SetJobStatus(GLOBALTYPE_JOBMANAGER, 0, spJobName, iPercentComplete, spStringToUse);
}


static void JobManagerStatus_CB(TransactionReturnVal *returnVal, char *pJobName)
{
	RemoteCommand_ServerIsGoingToDie(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), GetAppGlobalID(), "Done with job manager job", false, true);
}


void JobManagerUpdate_CompleteWithJobName(char *pJobName, bool bSucceeded, bool bShutSelfDown, char *pString_In, ...)
{
	static char *spStringToUse = NULL;

	if (!pJobName)
	{
		pJobName = spJobName;
	}

	if (pString_In)
	{
		estrGetVarArgs(&spStringToUse, pString_In);
	}
	else
	{
		estrPrintf(&spStringToUse, "(Unspecified)");
	}

	if (!pJobName)
	{
		AssertOrAlert("INVALID_Job_UPDATE", "Trying to update a Job manager Job while not running one");
		return;
	}

	if (bShutSelfDown)
	{
		RemoteCommand_SetJobComplete(objCreateManagedReturnVal(JobManagerStatus_CB, NULL), GLOBALTYPE_JOBMANAGER, 0, pJobName, bSucceeded, spStringToUse);
	}
	else
	{
		RemoteCommand_SetJobComplete(NULL, GLOBALTYPE_JOBMANAGER, 0, pJobName, bSucceeded, spStringToUse);
	}
}






AUTO_COMMAND;
void TestFailJob(void)
{
	JobManagerUpdate_Complete(false, true, "TestFailJob called");
}


AUTO_COMMAND;
void TestSucceedJob(void)
{
	JobManagerUpdate_Complete(true, true, "TestSucceedJob called");
}

void TestJobStepCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	SlowRemoteCommandReturn_TestJobStep((SlowRemoteCommandID)((intptr_t)userData), 1);
}


static char *pTestJobStepName = NULL;
AUTO_COMMAND_REMOTE_SLOW(int);
void TestJobStep(int x, char *pJobName, int y, SlowRemoteCommandID iCmdID)
{
	TimedCallback_Run(TestJobStepCB, (void*)((intptr_t)iCmdID), randomPositiveF32() * 30.0f + 1.0f);
}


AUTO_COMMAND;
void TestFailJobWithName(void)
{
	JobManagerUpdate_CompleteWithJobName(pTestJobStepName, false, false, "TestFailJob called");
}


AUTO_COMMAND;
void TestSucceedJobWithName(void)
{
	JobManagerUpdate_CompleteWithJobName(pTestJobStepName, true, false, "TestSucceedJob called");
}



char *GetUniqueJobGroupName(char *pBaseString)
{
	static char *pRetVal = NULL;
	static U32 iNextID = 0;
	estrCopy2(&pRetVal, pBaseString);
	estrMakeAllAlphaNumAndUnderscores(&pRetVal);
	estrConcatf(&pRetVal, " %s %u %u %u", GlobalTypeToName(GetAppGlobalType()), GetAppGlobalID(), iNextID++, timeSecondsSince2000());
	return pRetVal;
}

void DEFAULT_LATELINK_UpdateJobLogging_Internal(U32 iUserData, char *pName, char *pString)
{
}

AUTO_COMMAND_REMOTE;
void UpdateJobLogging(U32 iUserData, char *pName, char *pString)
{
	UpdateJobLogging_Internal(iUserData, pName, pString);
}


void JobManagerUpdate_Log(FORMAT_STR const char *pFmt, ...)
{
	char *pFullString = NULL;

	if (!spJobName)
	{
		AssertOrAlert("INVALID_JOB_LOG", "Trying to JobManagerUpdate_Log a Job manager Job while not running one");
		return;
	}

	estrGetVarArgs(&pFullString, pFmt);
	RemoteCommand_JobLogRemotely(GLOBALTYPE_JOBMANAGER, 0, spJobName, pFullString);
	estrDestroy(&pFullString);
}

void JobManagerUpdate_LogWithJobName(char *pJobName, FORMAT_STR const char *pFmt, ...)
{
	char *pFullString = NULL;

	estrGetVarArgs(&pFullString, pFmt);
	RemoteCommand_JobLogRemotely(GLOBALTYPE_JOBMANAGER, 0, pJobName, pFullString);
	estrDestroy(&pFullString);
}




#include "../../CrossRoads/common/autogen/AppServerLib_autogen_RemoteFuncs.c"


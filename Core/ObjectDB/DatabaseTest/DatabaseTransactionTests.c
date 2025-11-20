/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/


#include "objContainerIO.h"
#include "DatabaseTest.h"
#include "DatabaseTransactionTests.h"
#include "ObjectDB.h"
#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"
#include "StringFormat.h"
#include "ControllerScriptingSupport.h"

/*
static void SendClearDatabase_CB(TransactionReturnVal *returnVal, void *userData)
{
	int val;
	enumTransactionOutcome eOutcome;
	eOutcome = RemoteCommandCheck_RemoteClearDatabase(returnVal, &val);
	if (eOutcome != TRANSACTION_OUTCOME_SUCCESS)
	{
		ControllerScript_Failedf("Clear database failed with %s", returnVal->pBaseReturnVals[0].returnString);
		return;
	}
	ControllerScript_Succeeded();
}

AUTO_COMMAND;
void SendClearDatabase(void)
{
	if (gDBTestState.testingEnabled)
		RemoteCommand_RemoteClearDatabase(objCreateManagedReturnVal(SendClearDatabase_CB, NULL),GLOBALTYPE_OBJECTDB,0);
}

static void SendAddOneContainer_CB(TransactionReturnVal *returnVal, void *userData)
{
	int val;
	enumTransactionOutcome eOutcome;
	eOutcome = RemoteCommandCheck_RemoteAddOneContainer(returnVal, &val);
	if (eOutcome != TRANSACTION_OUTCOME_SUCCESS)
	{
		ControllerScript_Failedf("Add one container failed with %s", returnVal->pBaseReturnVals[0].returnString);
		return;
	}
	ControllerScript_Succeeded();
}


AUTO_COMMAND;
void SendAddOneContainer(void)
{
	if (gDBTestState.testingEnabled)
		RemoteCommand_RemoteAddOneContainer(objCreateManagedReturnVal(SendAddOneContainer_CB, NULL),GLOBALTYPE_OBJECTDB, 0);
}
*/
static void ScanLogsForQueries_CB(TransactionReturnVal *returnVal, void *userData)
{
	StringQueryList *list = NULL;
	enumTransactionOutcome eOutcome;

	eOutcome = RemoteCommandCheck_RemoteScanLogsForQueries(returnVal, &list);
	if (eOutcome != TRANSACTION_OUTCOME_SUCCESS)
	{
		ControllerScript_Failedf("Failed to get queries from database:%s", returnVal->pBaseReturnVals[0].returnString);
		return;
	}

	if (gDBQueryList)
	{
		StructDestroy(parse_StringQueryList, gDBQueryList);
	}
	gDBQueryList = list;

	printf("%d queries scanned.\n", eaSize(&list->eaList));

	ControllerScript_Succeeded();
}

AUTO_COMMAND;
void ScanLogsForQueries(void)
{
	RemoteCommand_RemoteScanLogsForQueries(objCreateManagedReturnVal(ScanLogsForQueries_CB, NULL),GLOBALTYPE_OBJECTDB, 0);
}

static void ChangeDirectory_CB(TransactionReturnVal *returnVal, void *userData)
{
	char *testdir = (char *)userData;
	int val;
	enumTransactionOutcome eOutcome;
	eOutcome = RemoteCommandCheck_RemoteChangeToDatabaseTestDirectory(returnVal, &val);
	if (eOutcome != TRANSACTION_OUTCOME_SUCCESS)
	{
		ControllerScript_Failedf("Failed to change database directory to %s:%s", testdir, returnVal->pBaseReturnVals[0].returnString);
		return;
	}
	ControllerScript_Succeeded();
}

AUTO_COMMAND;
void ChangeDirectory(void)
{
	if (gDBTestState.testDir)
		RemoteCommand_RemoteChangeToDatabaseTestDirectory(objCreateManagedReturnVal(ChangeDirectory_CB, gDBTestState.testDir),
		GLOBALTYPE_OBJECTDB, 0, gDBTestState.testDir);
}

/*
static void SendAddManyContainers_CB(TransactionReturnVal *returnVal, void *userData)
{
	int val;
	enumTransactionOutcome eOutcome;
	eOutcome = RemoteCommandCheck_RemoteAddManyContainers(returnVal, &val);
	if (eOutcome != TRANSACTION_OUTCOME_SUCCESS)
	{
		ControllerScript_Failedf("Add one container failed with %s", returnVal->pBaseReturnVals[0].returnString);
		return;
	}
	ControllerScript_Succeeded();
}

AUTO_COMMAND;
void SendAddManyContainers(void)
{
	if (gDBTestState.testingEnabled)
		RemoteCommand_RemoteAddManyContainers(objCreateManagedReturnVal(SendAddManyContainers_CB, NULL),GLOBALTYPE_OBJECTDB, 0, gDBTestState.totalUsers);
}
*/

void TransactAddTestContainer(int id)
{
	static char *query;
	NamedPathQuery *newCharacterQuery = objGetNamedQuery("NewCharacter");
	estrPrintf(&query,"dbUpdateContainer %d %d ",
		GLOBALTYPE_ENTITYPLAYER,id);

	strfmt_FromArgs(&query, newCharacterQuery->queryString, STRFMT_INT("ID", id), STRFMT_END);

	estrConcatf(&query,"$$dbUpdateContainerOwner %d %d %d %d",
		GLOBALTYPE_ENTITYPLAYER,id,GLOBALTYPE_OBJECTDB,0);
	dbHandleDatabaseUpdateString(query,0,0);
}

void TransactRemoveTestContainer(int id)
{
	char query[1024];
	sprintf(query,"dbDestroyContainer TestContainer %d",id);
	dbHandleDatabaseUpdateString(query,0,0);
}

static void WaitOnClone_CB(TransactionReturnVal *returnVal, void *userData)
{
	int val;
	enumTransactionOutcome eOutcome;
	eOutcome = RemoteCommandCheck_dbCloneFlush(returnVal, &val);
	if (eOutcome != TRANSACTION_OUTCOME_SUCCESS)
	{
		ControllerScript_Failedf("Waiting on clone failed with %s", returnVal->pBaseReturnVals[0].returnString);
		return;
	}
	ControllerScript_Succeeded();
}

AUTO_COMMAND;
void WaitOnClone(void)
{
	if (!gDBTestState.testingEnabled)
		return;

	RemoteCommand_dbCloneFlush(objCreateManagedReturnVal(WaitOnClone_CB, NULL),GLOBALTYPE_CLONEOBJECTDB,0);		
}

static void WaitOnMaster_CB(TransactionReturnVal *returnVal, void *userData)
{
	int val;
	enumTransactionOutcome eOutcome;
	eOutcome = RemoteCommandCheck_dbMasterFlush(returnVal, &val);
	if (eOutcome != TRANSACTION_OUTCOME_SUCCESS)
	{
		ControllerScript_Failedf("Waiting on master failed with %s", returnVal->pBaseReturnVals[0].returnString);
		return;
	}
	ControllerScript_Succeeded();
}


AUTO_COMMAND;
void WaitOnMaster(void)
{
	if (!gDBTestState.testingEnabled)
		return;

	RemoteCommand_dbMasterFlush(objCreateManagedReturnVal(WaitOnMaster_CB, NULL), GLOBALTYPE_OBJECTDB,0);
}

AUTO_COMMAND_REMOTE;
int RemoteClearDatabase(void)
{
	ContainerIterator iter;
	Container *con;
	PERFINFO_AUTO_START("RemoteClearDatabase",1);
	objInitAllContainerIterator(&iter);

	while (con = objGetNextContainerFromIterator(&iter))
	{
		TransactRemoveTestContainer(objGetContainerID(con));
		objInitAllContainerIterator(&iter);
	}
	PERFINFO_AUTO_STOP();
	return 1;
}

AUTO_COMMAND_REMOTE;
StringQueryList * RemoteScanLogsForQueries(void)
{
	StringQueryList *returnVal = StructCreate(parse_StringQueryList);
	eaPush(&returnVal->eaCommandList, strdup("dbUpdateContainer "));

	objScanLogsForQueries(returnVal);

	return returnVal;
}

AUTO_COMMAND_REMOTE;
int RemoteChangeToDatabaseTestDirectory(char *testDir)
{
	if (testDir && testDir[0])
		objSetContainerSourceToDirectory(testDir);

	objForceRotateIncrementalHog();
	
	return 1;
}


AUTO_COMMAND_REMOTE;
int RemoteAddOneContainer(void)
{
	PERFINFO_AUTO_START("RemoteAddOneContainer",1);
	RemoteClearDatabase();
	TransactAddTestContainer(1);
	PERFINFO_AUTO_STOP();
	return 1;
}

AUTO_COMMAND_REMOTE;
int RemoteAddManyContainers(int count)
{
	int i;
	PERFINFO_AUTO_START("RemoteAddManyContainers",1);
	RemoteClearDatabase();
	for (i = 1; i <= count; i++)
	{
		TransactAddTestContainer(i);
	}
	PERFINFO_AUTO_STOP();
	return 1;
}

AUTO_COMMAND;
int AddManyContainers(int count)
{
	int i;
	printf("Adding %d containers... ", count);
	PERFINFO_AUTO_START("AddManyContainers",1);
	for (i = 1; i <= count; i++)
	{
		TransactAddTestContainer(i);
	}
	PERFINFO_AUTO_STOP();
	printf("done!\n");
	return 1;
}

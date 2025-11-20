/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "CCGPrintRun.h"
#include "CCGServer.h"

#include "AppServerLib.h"
#include "UtilitiesLib.h"
#include "StringCache.h"
#include "EString.h"

#include "cmdparse.h"
#include "LocalTransactionManager.h"
#include "objTransactions.h"
#include "objContainer.h"

#include "stdtypes.h"

#include "AutoGen/CCGPrintRun_h_ast.h"
#include "AutoGen/CCGServer_h_ast.h"
#include "AutoGen/AppServerLib_autotransactions_autogen_wrappers.h"

static StashTable printRunIndex;

//
// Search an object path from the bottom up, looking for a sub-object of  the
//  top container that is given type.
//
static void *
findObjInPath(ParseTable *topParseTable, void *topObj, ParseTable *subjectParseTable, const char *objPath)
{
	ParseTable *resultParseTable = NULL;
	void *resultObj = NULL;
	char *fullPath = NULL;
	char *pathEnd = NULL;

	fullPath = strdup(objPath);
	if (fullPath)
	{
		pathEnd = fullPath + strlen(fullPath);
	}

	// start by looking up the original path
	objPathGetStruct(fullPath, topParseTable, topObj, &resultParseTable, &resultObj);

	// now loop until we either run out of path elements or find the object with the correct
	//  parse table
	while (pathEnd && pathEnd > fullPath && resultParseTable != subjectParseTable)
	{
		// remove the last element on the path
		while (pathEnd > fullPath && *pathEnd != '.')
		{
			--pathEnd;
		}
		if (pathEnd > fullPath)
		{
			*pathEnd = '\0';
		}

		// do the lookup again
		resultObj = NULL;
		objPathGetStruct(fullPath, topParseTable, topObj, &resultParseTable, &resultObj);
	}

	return resultObj;
}

//
// Global container add/remove callbacks
//
// These are used to keep the by name lookup stash tables up to date.
//
static void
addPrintRunContainer_CB(Container *container, CCGPrintRun *printRun)
{
	if ( stashAddInt(printRunIndex, printRun->name, printRun->containerID, false) )
	{
		printf("PrintRun Add Callback, containerID:%u\n", printRun->containerID);
	}
	else
	{
		printf("Failed to add printRun %s to index\n", printRun->name);
	}
}

static void
removePrintRunContainer_CB(Container *container, CCGPrintRun *printRun)
{
	if ( stashRemoveInt(printRunIndex, printRun->name, NULL) )
	{
		printf("PrintRun Remove Callback, containerID:%u\n", printRun->containerID);
	}
	else
	{
		printf("Failed to remove PrintRun %s from index\n", printRun->name);
	}
}

static void
groupCommit_CB(Container *container, ObjectPathOperation *operation)
{
//	CCGPrintRun *printRun = container->containerData;
//	CCGPrintRunCardGroup *cardGroup;

	printf("group commit op: %d, path: %s", operation->op, operation->pathEString);
}

//
// Init code for print run module
//
void
CCG_PrintRunInitEarly(void)
{
}

void
CCG_PrintRunInitLate(void)
{
	static GlobalType printRunContainerType = GLOBALTYPE_CCGPRINTRUN;

	printRunIndex = stashTableCreateWithStringKeys(100, StashDefault);

	objRegisterContainerTypeAddCallback(GLOBALTYPE_CCGPRINTRUN, addPrintRunContainer_CB);
	objRegisterContainerTypeRemoveCallback(GLOBALTYPE_CCGPRINTRUN, removePrintRunContainer_CB);
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_CCGPRINTRUN, groupCommit_CB, ".cardGroups[*]", true, false, NULL);
	aslAcquireContainerOwnership(&printRunContainerType);
}

///////////////////////////////////////////////////////////////////////////////////////////
// PrintRun related transactions and commands
///////////////////////////////////////////////////////////////////////////////////////////

//
// look up a print run by name
//
CCGPrintRun *
CCG_GetPrintRun(const char *printRunName)
{
	ContainerID containerID;
	Container *container;

	if ( stashFindInt(printRunIndex, printRunName, &containerID) )
	{
		container = objGetContainer(GLOBALTYPE_CCGPRINTRUN, containerID);
		if ( container != NULL )
		{
			return (CCGPrintRun *)container->containerData;
		}
	}
	return NULL;
}

//
// command to get full print run data by name
//
AUTO_COMMAND ACMD_NAME(CCG_GetPrintRun) ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
CCGPrintRun *
CCG_GetPrintRunCmd(char *printRunName)
{
	CCGPrintRun *printRun = CCG_GetPrintRun(printRunName);

	if ( printRun != NULL )
	{
		return StructClone(parse_CCGPrintRun, printRun);
	}

	return NULL;
}

//
// get a print run id by name
//
ContainerID
CCG_GetPrintRunID(char *printRunName)
{
	ContainerID containerID = 0;

	stashFindInt(printRunIndex, printRunName, &containerID);

	return containerID;
}

//
// Create a new Print Run
//
bool
CCG_CreatePrintRun(TransactionReturnVal *pReturn, char *name, bool repeating)
{
	NOCONST(CCGPrintRun) *printRun;

	if ( CCG_GetPrintRun(name) != NULL )
	{
		return false;
	}

	printRun = StructCreate(parse_CCGPrintRun);
	printRun->name = allocAddString(name);
	printRun->inProduction = false;
	printRun->repeating = repeating;

	objRequestContainerCreate(pReturn, GLOBALTYPE_CCGPRINTRUN, printRun, objServerType(), objServerID());

	StructDestroy(parse_CCGPrintRun, printRun);

	return true;
}

static void 
CreatePrintRunCmd_CB(TransactionReturnVal *pReturnVal, CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo)
{
	char *pFullRetString = CCG_CreateSimpleReturnString("Create print run", pReturnVal);

	DoSlowCmdReturn(pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS, pFullRetString, pSlowReturnInfo);
}

AUTO_COMMAND ACMD_NAME(CCG_CreatePrintRun) ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
void
CCG_CreatePrintRunCmd(CmdContext *pContext, char *name, bool repeating)
{
	TransactionReturnVal *pReturn = objCreateManagedReturnVal(CreatePrintRunCmd_CB, CCG_SetupSlowReturn(pContext));
	CCG_CreatePrintRun(pReturn, name, repeating);
}

//
// Set the print run production flag
//
AUTO_TRANSACTION;
enumTransactionOutcome
CCG_tr_PrintRunSetProductionFlag(ATR_ARGS, NOCONST(CCGPrintRun) *printRun, int val)
{
	printRun->inProduction = val;

	return TRANSACTION_OUTCOME_SUCCESS;
}

static void 
PrintRunSetProductionFlag_CB(TransactionReturnVal *pReturnVal, CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo)
{
	char *pFullRetString = CCG_CreateSimpleReturnString("set print run production flag", pReturnVal);

	DoSlowCmdReturn(pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS, pFullRetString, pSlowReturnInfo);
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
void
CCG_PrintRunSetProductionFlag(CmdContext *pContext, char *printRunName, bool productionFlag)
{
	ContainerID printRunID = CCG_GetPrintRunID(printRunName);
	TransactionReturnVal *pReturn = objCreateManagedReturnVal(PrintRunSetProductionFlag_CB, CCG_SetupSlowReturn(pContext));
	AutoTrans_CCG_tr_PrintRunSetProductionFlag(pReturn, GetAppGlobalType(), GLOBALTYPE_CCGPRINTRUN, printRunID, productionFlag);
}

//
// create a new card group in a print run
//
AUTO_TRANSACTION;
enumTransactionOutcome
CCG_tr_PrintRunCreateCardGroup(ATR_ARGS, NOCONST(CCGPrintRun) *pPrintRun, char *name)
{
	NOCONST(CCGPrintRunCardGroup) *pCardGroup = StructCreate(parse_CCGPrintRunCardGroup);
	pCardGroup->name = allocAddString(name);
	pCardGroup->currentCount = 0;
	pCardGroup->originalCount = 0;

	eaPush(&pPrintRun->cardGroups, pCardGroup);

	return TRANSACTION_OUTCOME_SUCCESS;
}

static void 
PrintRunCreateCardGroup_CB(TransactionReturnVal *pReturnVal, CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo)
{
	char *pFullRetString = CCG_CreateSimpleReturnString("create card group", pReturnVal);

	DoSlowCmdReturn(pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS, pFullRetString, pSlowReturnInfo);
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
void
CCG_PrintRunCreateCardGroup(CmdContext *pContext, char * printRunName, char *name)
{
	ContainerID printRunID = CCG_GetPrintRunID(printRunName);

	TransactionReturnVal *pReturn = objCreateManagedReturnVal(PrintRunCreateCardGroup_CB, CCG_SetupSlowReturn(pContext));
	AutoTrans_CCG_tr_PrintRunCreateCardGroup(pReturn, GetAppGlobalType(), GLOBALTYPE_CCGPRINTRUN, printRunID, name);
}

//
// Create a new card definition in a print run
//
AUTO_TRANSACTION;
enumTransactionOutcome
CCG_tr_PrintRunCreateCardDef(ATR_ARGS, NOCONST(CCGPrintRun) *pPrintRun, char *groupName, U32 cardNum, U32 count)
{
	NOCONST(CCGPrintRunCardDef) *pCardDef;
	NOCONST(CCGPrintRunCardGroup) *pCardGroup = eaIndexedGetUsingString(&pPrintRun->cardGroups, groupName);
	// fail if the named group doesn't exist
	if ( pCardGroup == NULL )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// create the card def
	pCardDef = StructCreate(parse_CCGPrintRunCardDef);
	pCardDef->cardNum = cardNum;
	pCardDef->originalCount = count;
	pCardDef->currentCount = count;

	// update counts on the group
	pCardGroup->currentCount += count;
	pCardGroup->originalCount += count;

	// push the card into the group
	eaPush(&pCardGroup->cardDefs, pCardDef);

	return TRANSACTION_OUTCOME_SUCCESS;
}

static void 
PrintRunCreateCardDef_CB(TransactionReturnVal *pReturnVal, CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo)
{
	char *pFullRetString = CCG_CreateSimpleReturnString("create card def", pReturnVal);

	DoSlowCmdReturn(pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS, pFullRetString, pSlowReturnInfo);
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
void
CCG_PrintRunCreateCardDef(CmdContext *pContext, char * printRunName, char *groupName, U32 cardNum, U32 count)
{
	ContainerID printRunID = CCG_GetPrintRunID(printRunName);

	TransactionReturnVal *pReturn = objCreateManagedReturnVal(PrintRunCreateCardDef_CB, CCG_SetupSlowReturn(pContext));
	AutoTrans_CCG_tr_PrintRunCreateCardDef(pReturn, GetAppGlobalType(), GLOBALTYPE_CCGPRINTRUN, printRunID, groupName, cardNum, count);
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
bool
CCG_PrintRunGetProductionFlag(char * printRunName)
{
	CCGPrintRun *printRun = CCG_GetPrintRun(printRunName);

	if ( printRun )
	{
		return printRun->inProduction;
	}

	return false;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
CCGPooledStringList *
CCG_PrintRunGetCardGroupNames(char * printRunName)
{
	CCGPrintRun *printRun = CCG_GetPrintRun(printRunName);
	int i;

	if ( printRun )
	{
		CCGPooledStringList *list = StructCreate(parse_CCGPooledStringList);

		for ( i = 0; i < eaSize(&printRun->cardGroups); i++ )
		{
			eaPush(&list->list, (void *)printRun->cardGroups[i]->name);
		}

		return list;
	}

	return NULL;
}

//
// return the names of all print runs
//
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
CCGPooledStringList *
CCG_GetPrintRunNames()
{
	ContainerIterator iter;
	CCGPrintRun *printRun;

	CCGPooledStringList *list = StructCreate(parse_CCGPooledStringList);

	objInitContainerIteratorFromType(GLOBALTYPE_CCGPRINTRUN, &iter);
	while (printRun = objGetNextObjectFromIterator(&iter)) 
	{
		eaPush(&list->list, (void *)printRun->name);
	}

	return list;
}

//
// get rid of a print run
//
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
void
CCG_DestroyPrintRun(char *printRunName)
{
	ContainerID containerID = CCG_GetPrintRunID(printRunName);
	if ( containerID )
	{
		objRequestContainerDestroy(NULL, GLOBALTYPE_CCGPRINTRUN, containerID, objServerType(), objServerID());
	}
}

#include "CCGPrintRun_h_ast.c"
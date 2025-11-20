/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "CCGPackDef.h"
#include "CCGPrintRun.h"
#include "CCGServer.h"
#include "CCGTransactionReturnVal.h"

#include "AppServerLib.h"
#include "UtilitiesLib.h"
#include "StringCache.h"
#include "EString.h"

#include "cmdparse.h"
#include "LocalTransactionManager.h"
#include "objTransactions.h"
#include "objContainer.h"

#include "stdtypes.h"

#include "AutoGen/CCGPackDef_h_ast.h"
#include "AutoGen/CCGServer_h_ast.h"
#include "AutoGen/AppServerLib_autotransactions_autogen_wrappers.h"

static StashTable packDefIndex;

#define TRV_STRING(pRetVal) CCG_TRVToXMLResponseString(CCG_TRVFromTransactionRet((pRetVal), NULL), true)

static void
addPackDefContainer_CB(Container *container, CCGPackDef *packDef)
{
	if ( stashAddInt(packDefIndex, packDef->packName, packDef->containerID, false) )
	{
		printf("PackDef Add Callback, containerID:%u\n", packDef->containerID);
	}
	else
	{
		printf("Failed to add PackDef %s to index\n", packDef->packName);
	}
}

static void
removePackDefContainer_CB(Container *container, CCGPackDef *packDef)
{
	if ( stashRemoveInt(packDefIndex, packDef->packName, NULL) )
	{
		printf("PackDef Remove Callback, containerID:%u\n", packDef->containerID);
	}
	else
	{
		printf("Failed to remove PackDef %s from index\n", packDef->packName);
	}
}

//
// Init code for packs module
//
void
CCG_PacksInitLate(void)
{
	static GlobalType packDefContainerType = GLOBALTYPE_CCGPACKDEF;

	packDefIndex = stashTableCreateWithStringKeys(100, StashDefault);

	objRegisterContainerTypeAddCallback(GLOBALTYPE_CCGPACKDEF, addPackDefContainer_CB);
	objRegisterContainerTypeRemoveCallback(GLOBALTYPE_CCGPACKDEF, removePackDefContainer_CB);

	aslAcquireContainerOwnership(&packDefContainerType);

}

///////////////////////////////////////////////////////////////////////////////////////////
// PackDef related transactions and commands
///////////////////////////////////////////////////////////////////////////////////////////

//
// Look up a pack def by name
//
CCGPackDef *
CCG_GetPackDef(const char *packDefName)
{
	ContainerID containerID;
	Container *container;

	if ( stashFindInt(packDefIndex, packDefName, &containerID) )
	{
		container = objGetContainer(GLOBALTYPE_CCGPACKDEF, containerID);
		if ( container != NULL )
		{
			return (CCGPackDef *)container->containerData;
		}
	}
	return NULL;
}

//
// Check whether the the named pack is available in the quantity specified
//
bool
CCG_CheckPacksAvailable(char *packName, U32 count)
{
	CCGPackDef *packDef = CCG_GetPackDef(packName);

	if ( packDef != NULL )
	{
		if ( packDef->unlimited || ( packDef->packCount >= count ) )
		{
			return true;
		}
	}

	return false;
}

AUTO_TRANS_HELPER;
bool
CCG_trh_CheckPacksAvailable(ATH_ARG NOCONST(CCGPackDef) *packDef, U32 count)
{
	return packDef->unlimited || ( packDef->packCount >= count );
}

//
// NOTE - this just decrements the count without doing any checking, so
//  callers had better be sure there are enough packs before calling this function.
//
AUTO_TRANS_HELPER;
void
CCG_trh_DecrementPackCount(ATH_ARG NOCONST(CCGPackDef) *packDef, U32 count)
{
	if ( !packDef->unlimited )
	{
		packDef->packCount -= count;
	}
}

//
// command to fetch full pack def data by name
//
AUTO_COMMAND ACMD_NAME(CCG_GetPackDef) ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
CCGPackDef *
CCG_GetPackDefCmd(char *packDefName)
{
	CCGPackDef *packDef = CCG_GetPackDef(packDefName);

	if ( packDef != NULL )
	{
		return StructClone(parse_CCGPackDef, packDef);
	}

	return NULL;
}

//
// Lookup the ID of a pack def
//
ContainerID
CCG_GetPackDefID(char *packDefName)
{
	ContainerID containerID = 0;

	stashFindInt(packDefIndex, packDefName, &containerID);

	return containerID;
}

//
// Create a new PackDef
//
void
CCG_CreatePackDef(TransactionReturnVal *pReturn, char *name, U32 packCount)
{
	NOCONST(CCGPackDef) *packDef;

	packDef = StructCreate(parse_CCGPackDef);
	packDef->packName = allocAddString(name);
	packDef->packCount = packCount;
	packDef->unlimited = (packCount == 0);
	packDef->containsRandomCards = false;

	objRequestContainerCreate(pReturn, GLOBALTYPE_CCGPACKDEF, packDef, objServerType(), objServerID());

	StructDestroy(parse_CCGPackDef, packDef);
}


static void 
CreatePackDefCmd_CB(TransactionReturnVal *pReturnVal, CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo)
{
	DoSlowCmdReturn(pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS, TRV_STRING(pReturnVal), pSlowReturnInfo);
}

AUTO_COMMAND ACMD_NAME(CCG_CreatePackDef) ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
void
CCG_CreatePackDefCmd(CmdContext *pContext, char *name, U32 packCount)
{
	TransactionReturnVal *pReturn = objCreateManagedReturnVal(CreatePackDefCmd_CB, CCG_SetupSlowReturn(pContext));
	CCG_CreatePackDef(pReturn, name, packCount);
}

//
// add a fixed card to a PackDef
//
AUTO_TRANSACTION;
enumTransactionOutcome
CCG_tr_PackDefAddFixedCard(ATR_ARGS, NOCONST(CCGPackDef) *packDef, U32 cardNum)
{
	ea32Push(&packDef->fixedCards, cardNum);

	return TRANSACTION_OUTCOME_SUCCESS;
}

static void 
PackDefAddFixedCard_CB(TransactionReturnVal *pReturnVal, CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo)
{
	DoSlowCmdReturn(pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS, TRV_STRING(pReturnVal), pSlowReturnInfo);
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
void
CCG_PackDefAddFixedCard(CmdContext *pContext, char *packDefName, U32 cardNum)
{
	ContainerID packDefID = CCG_GetPackDefID(packDefName);

	TransactionReturnVal *pReturn = objCreateManagedReturnVal(PackDefAddFixedCard_CB, CCG_SetupSlowReturn(pContext));
	AutoTrans_CCG_tr_PackDefAddFixedCard(pReturn, GetAppGlobalType(), GLOBALTYPE_CCGPACKDEF, packDefID, cardNum);
}

//
// add a random card to a PackDef
//
AUTO_TRANSACTION;
enumTransactionOutcome
CCG_tr_PackDefAddRandomCard(ATR_ARGS, NOCONST(CCGPackDef) *packDef, char *printRunName, char *groupName)
{
	NOCONST(CCGRandomSlotDef) *packSlot;

	const char *poolPrintRun = allocAddString(printRunName);
	const char *poolGroup = allocAddString(groupName);

	FOR_EACH_IN_EARRAY(packDef->randomCards, NOCONST(CCGRandomSlotDef), slot)
	{
		if ( ( poolPrintRun == slot->printRunName ) && ( poolGroup == slot->groupName ) )
		{
			// found the slot
			slot->count++;
			return TRANSACTION_OUTCOME_SUCCESS;
		}
	}
	FOR_EACH_END

	// need to make a new slot
	packSlot = StructCreate(parse_CCGRandomSlotDef);
	packSlot->count = 1;
	packSlot->groupName = poolGroup;
	packSlot->printRunName = poolPrintRun;

	packDef->containsRandomCards = true;

	eaPush(&packDef->randomCards, packSlot);

	return TRANSACTION_OUTCOME_SUCCESS;
}

static void 
PackDefAddRandomCard_CB(TransactionReturnVal *pReturnVal, CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo)
{
	char *pFullRetString = CCG_CreateSimpleReturnString("PackDef add random card", pReturnVal);

	DoSlowCmdReturn(pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS, TRV_STRING(pReturnVal), pSlowReturnInfo);
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
void
CCG_PackDefAddRandomCard(CmdContext *pContext, char *packDefName, char *printRunName, char *groupName)
{
	ContainerID packDefID = CCG_GetPackDefID(packDefName);

	TransactionReturnVal *pReturn = objCreateManagedReturnVal(PackDefAddRandomCard_CB, CCG_SetupSlowReturn(pContext));
	AutoTrans_CCG_tr_PackDefAddRandomCard(pReturn, GetAppGlobalType(), GLOBALTYPE_CCGPACKDEF, packDefID, printRunName, groupName);
}

//
// return the names of all packs
//
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
CCGPooledStringList *
CCG_GetPackDefNames()
{
	ContainerIterator iter;
	CCGPackDef *packDef;

	CCGPooledStringList *list = StructCreate(parse_CCGPooledStringList);

	objInitContainerIteratorFromType(GLOBALTYPE_CCGPACKDEF, &iter);
	while (packDef = objGetNextObjectFromIterator(&iter)) 
	{
		eaPush(&list->list, (void *)packDef->packName);
	}

	return list;
}

//
// get rid of a pack definition
//
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
void
CCG_DestroyPackDef(char *packDefName)
{
	ContainerID containerID = CCG_GetPackDefID(packDefName);
	if ( containerID )
	{
		objRequestContainerDestroy(NULL, GLOBALTYPE_CCGPACKDEF, containerID, objServerType(), objServerID());
	}
}

#include "CCGPackDef_h_ast.c"
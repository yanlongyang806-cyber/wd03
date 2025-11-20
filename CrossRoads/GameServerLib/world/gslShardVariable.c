/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "earray.h"
#include "error.h"
#include "gslShardVariable.h"
#include "gslWorldVariable.h"
#include "objTransactions.h"
#include "ResourceManager.h"
#include "ShardVariableCommon.h"
#include "StringCache.h"
#include "WorldGrid.h"
#include "WorldVariable.h"
#include "ShardVariable_Transact.h"

#include "autogen/gameserverlib_autotransactions_autogen_wrappers.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// ----------------------------------------------------------------------------------
// Map Load Logic
// ----------------------------------------------------------------------------------

void shardvariable_MapValidate(ZoneMap *pZoneMap)
{
	int i,j;

	const ShardVariable * const * const * const peaShardList = shardvariable_GetList();

	// Make sure there are no duplicate variables defined
	for(i=eaSize(peaShardList)-1; i>=0; --i) {
		for(j=i-1; j>=0; --j) {
			if ((*peaShardList)[i]->pcName == (*peaShardList)[j]->pcName) {
				ErrorFilenamef(SHARD_VAR_FILENAME, "Duplicate shard variable named '%s'", (*peaShardList)[i]->pcName);
			}
		}
	}

	// Check types and defaults
	for(i=eaSize(peaShardList)-1; i>=0; --i) {
		WorldVariable *pDef = (*peaShardList)[i]->pDefault;

		if (!resIsValidName(pDef->pcName)) {
			ErrorFilenamef(SHARD_VAR_FILENAME, "Shard variable named '%s' is not properly formed.  It must start with an alphabetic character and contain only alphanumerics, underscore, dot, and dash.", (*peaShardList)[i]->pcName);
		}

		if (pDef->eType == WVAR_NONE) {
			ErrorFilenamef(SHARD_VAR_FILENAME, "Shard variable named '%s' has illegal type 'NONE'", (*peaShardList)[i]->pcName);
		}
		if (pDef->eType == WVAR_MESSAGE) {
			if (!GET_REF(pDef->messageVal.hMessage)) {
				if (REF_STRING_FROM_HANDLE(pDef->messageVal.hMessage)) {
					ErrorFilenamef(SHARD_VAR_FILENAME, "Shard variable named '%s' refers to non-existent message '%s'", (*peaShardList)[i]->pcName, REF_STRING_FROM_HANDLE(pDef->messageVal.hMessage));
				}
			}
		}
	}
}


void shardvariable_MapLoad(ZoneMap *pZoneMap)
{
	bool bMapRequested=true;
	bool bBroadcast=true;
	
	shardvariable_ClearList();
	
	if (!zmapInfoGetEnableShardVariables(NULL))
	{
		// No mapRequested if not actually requested
		bMapRequested=false;
	}
	shardVariable_AddAllVariables(bMapRequested, bBroadcast);
}


void shardvariable_MapUnload(void)
{
	// Clear the list
	shardvariable_ClearList();

	// Remove the handle to drop interest in the container
	REMOVE_HANDLE(g_ShardVariableRef.hMapRequestedContainer);
	REMOVE_HANDLE(g_ShardVariableRef.hBroadcastContainer);
}

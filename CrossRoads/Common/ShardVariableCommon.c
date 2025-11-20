/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "error.h"
#include "ShardVariableCommon.h"
#include "StringCache.h"

#include "AutoGen/ShardVariableCommon_h_ast.h"

#if (defined(GAMESERVER) || defined(APPSERVER))
#include "objSchema.h"
#include "objTransactions.h"
#include "ResourceManager.h"
#endif

#ifdef GAMESERVER
#include "autogen/gameserverlib_autotransactions_autogen_wrappers.h"
#include "WorldGrid.h"
#endif

#ifdef APPSERVER
#include "autogen/appserverlib_autotransactions_autogen_wrappers.h"
#endif

#define TICKS_TO_WAIT_IF_CONTAINER_MISSING_TO_REREQUEST  1800

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// ----------------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------------

ShardVariableDefs g_ShardVariableDefs = {0};

static const char **g_eaShardVariableNames = NULL;

static ShardVariable **s_eaShardVariables = NULL;

ShardVariableContainerRef g_ShardVariableRef = {0};

static 	U32* g_eaShardVarContainerIDList = NULL;



#if (defined(GAMESERVER) || defined(APPSERVER))

// ----------------------------------------------------------------------------------
// Support Functions
// ----------------------------------------------------------------------------------

// Gets the Earray of containerIDs we pass to transactions so we don't have to keep creating it. Set up in shardvariable_Init(void)
U32** shardVariable_GetContainerIDList()
{
	return(&g_eaShardVarContainerIDList);
}

// Gets the correct referenced container for the given ShardVariable. 
ShardVariableContainer* shardVariable_GetContainer(ShardVariable* pShardVar)
{
	if (pShardVar)
	{
		if (pShardVar->iSubscribeTypeContainerID==SHARD_VAR_MAPREQUESTED_CONTAINER_ID)
		{
			return(GET_REF(g_ShardVariableRef.hMapRequestedContainer));
		}
		else if (pShardVar->iSubscribeTypeContainerID==SHARD_VAR_BROADCAST_CONTAINER_ID)
		{
			return(GET_REF(g_ShardVariableRef.hBroadcastContainer));
		}
	}
	return(NULL);
}


// Gets a variable, if one exists
SA_RET_OP_VALID ShardVariable *shardvariable_GetByName(SA_PARAM_NN_STR const char *pcName)
{
	ShardVariable *pShardVar;
	int i, j;

	pcName = allocAddString(pcName);

	for(i=eaSize(&s_eaShardVariables)-1; i>=0; --i) {
		pShardVar = s_eaShardVariables[i];
		if (pShardVar->pcName == pcName) {
			// If container clock differs from current, then need to reset the value
			ShardVariableContainer *pContainer = shardVariable_GetContainer(pShardVar);
			if (pContainer && (pShardVar->uClock != pContainer->uClock)) {
				for(j=eaSize(&pContainer->eaWorldVars)-1; j>=0; --j) {
					WorldVariableContainer *pContainerVar = pContainer->eaWorldVars[j];
					if (pContainerVar->pcName == pcName) {
						// Found value on container so use it
						worldVariableCopyFromContainer(pShardVar->pVariable, pContainerVar);
						break;
					}
				}
				if (j < 0) {
					// Value not found on container, so reset to default
					StructCopy(parse_WorldVariable, pShardVar->pDefault, pShardVar->pVariable, 0, 0, 0);
				}
				pShardVar->uClock = pContainer->uClock;
			}

			return pShardVar;
		}
	}

	return NULL;
}

// We couldn't find a local ShardVariable. Deal with various error messages
void shardvariable_ErrorOnNotFound(const char *pcName, char **estrError)
{
	pcName = allocAddString(pcName);
	
	if (estrError)
	{
		int i;
		// Find the original def.
		for(i=0; i<eaSize(&g_ShardVariableDefs.eaVariables); i++)
		{
			WorldVariable *pVar = &(g_ShardVariableDefs.eaVariables[i]->WorldVar);
			if (pcName == pVar->pcName)
			{
				// There was a def for it.
				// see if it's map requested or broadcast
				if (g_ShardVariableDefs.eaVariables[i]->bBroadcast)
				{
					// Something strange happened. We should have a local version, but we do not.
#ifdef GAMESERVER
					estrPrintf(estrError, "Existing Broadcast shard variable (%s) not found for this map ('%s')", pcName, zmapGetName(NULL));
#else
					estrPrintf(estrError, "Existing Broadcast shard variable (%s) not found.", pcName);
#endif					
					return;
				}
#ifdef GAMESERVER
				else
				{
					// We're on a gameserver, so we can see if we didn't find it because they're disabled
					if (!zmapInfoGetEnableShardVariables(NULL))
					{
						estrPrintf(estrError, "Map-Requested shard variable (%s) is not enabled for this map ('%s')", pcName, zmapGetName(NULL));
						return;
					}
				}
#endif					
			}
					
		}
	}
	// Generic error
	estrPrintf(estrError, "Shard variable '%s' not found", pcName);
}


static void shardvariable_Free(ShardVariable *pShardVar)
{
	StructDestroy(parse_WorldVariable, pShardVar->pVariable);
	free(pShardVar);
}

// Adds a shard variable to the list stored in the server
static void shardvariable_AddVariable(ShardWorldVarDef *pDef)
{
	ShardVariable *pShardVar = calloc(1,sizeof(ShardVariable));
	pShardVar->pcName = pDef->WorldVar.pcName;
	pShardVar->pDefault = &(pDef->WorldVar);
	pShardVar->uClock = 0;
	pShardVar->pVariable = StructClone(parse_WorldVariable, &(pDef->WorldVar));
	
	if (pDef->bBroadcast)
	{
		pShardVar->iSubscribeTypeContainerID = SHARD_VAR_BROADCAST_CONTAINER_ID;
		g_ShardVariableRef.bWantsBroadcast=true;
	}
	else
	{
		pShardVar->iSubscribeTypeContainerID = SHARD_VAR_MAPREQUESTED_CONTAINER_ID;
		g_ShardVariableRef.bWantsMapRequested=true;
	}
	eaPush(&s_eaShardVariables, pShardVar);
}


// Clears the list of shard variables stored in the server
void shardvariable_ClearList(void)
{
	eaDestroyEx(&s_eaShardVariables, shardvariable_Free);
}

// Returns a const list of shard variables stored in the server
const ShardVariable * const * const * const shardvariable_GetList(void)
{
	return &s_eaShardVariables;
}

// ----------------------------------------------------------------------------------
// Transaction wrapper functions
// ----------------------------------------------------------------------------------


bool shardvariable_ResetVariable(const char *pcName, char **estrError)
{
	ShardVariable *pShardVar = shardvariable_GetByName(pcName);
	if (pShardVar) {
		AutoTrans_shardvariable_tr_ClearVariable(NULL, GetAppGlobalType(), GLOBALTYPE_SHARDVARIABLE, pShardVar->iSubscribeTypeContainerID, pcName);
		return true;
	} else if (estrError) {
		shardvariable_ErrorOnNotFound(pcName, estrError);
	}
	return false;
}


// Let's no longer test for zmapInfoGetEnableShardVariables().  This is only
//  used from an AcccessLevel9 command (shardvariable_CmdResetAllVariables()) and as such,
//  shouldn't worry about whether they are enabled on a particular GameServer or not.
//  If the AccessLevel9 user wants to reset ALL variables, we will let them.
bool shardvariable_ResetAllVariables(char **estrError)
{
	AutoTrans_shardvariable_tr_ClearAllVariables(NULL, GetAppGlobalType(), GLOBALTYPE_SHARDVARIABLE, SHARD_VAR_MAPREQUESTED_CONTAINER_ID);
	AutoTrans_shardvariable_tr_ClearAllVariables(NULL, GetAppGlobalType(), GLOBALTYPE_SHARDVARIABLE, SHARD_VAR_BROADCAST_CONTAINER_ID);
	
	return true;
}


bool shardvariable_SetVariable(WorldVariable *pWorldVar, char **estrError)
{
	ShardVariable *pShardVar = shardvariable_GetByName(pWorldVar->pcName);
	if (pShardVar) {
		if (pShardVar->pDefault->eType != pWorldVar->eType) {
			if (estrError) {
				estrPrintf(estrError, "Shard variable '%s' is of type '%s' and not '%s'", pWorldVar->pcName, worldVariableTypeToString(pShardVar->pDefault->eType), worldVariableTypeToString(pWorldVar->eType));
			}
		} else {
			AutoTrans_shardvariable_tr_SetVariable(NULL, GetAppGlobalType(), GLOBALTYPE_SHARDVARIABLE, pShardVar->iSubscribeTypeContainerID, pWorldVar);
			return true;
		}
	} else if (estrError) {
		shardvariable_ErrorOnNotFound(pWorldVar->pcName, estrError);
	}
	return false;
}


bool shardvariable_IncrementIntVariable(const char *pcName, int iValue, char **estrError)
{
	ShardVariable *pShardVar = shardvariable_GetByName(pcName);
	if (pShardVar) {
		if (pShardVar->pDefault->eType != WVAR_INT) {
			if (estrError) {
				estrPrintf(estrError, "Shard variable '%s' is of type '%s' and not 'INT'", pcName, worldVariableTypeToString(pShardVar->pDefault->eType));
			}
		} else {
			AutoTrans_shardvariable_tr_IncrementIntVariable(NULL, GetAppGlobalType(), GLOBALTYPE_SHARDVARIABLE, pShardVar->iSubscribeTypeContainerID, pShardVar->pVariable, iValue);
			return true;
		}
	} else if (estrError) {
		shardvariable_ErrorOnNotFound(pcName, estrError);
	}
	return false;
}


bool shardvariable_IncrementFloatVariable(const char *pcName, float fValue, char **estrError)
{
	ShardVariable *pShardVar = shardvariable_GetByName(pcName);
	if (pShardVar) {
		if (pShardVar->pDefault->eType != WVAR_FLOAT) {
			if (estrError) {
				estrPrintf(estrError, "Shard variable '%s' is of type '%s' and not 'FLOAT'", pcName, worldVariableTypeToString(pShardVar->pDefault->eType));
			}
		} else {
			AutoTrans_shardvariable_tr_IncrementFloatVariable(NULL, GetAppGlobalType(), GLOBALTYPE_SHARDVARIABLE,  pShardVar->iSubscribeTypeContainerID, pShardVar->pVariable, fValue);
			return true;
		}
	} else if (estrError) {
		shardvariable_ErrorOnNotFound(pcName, estrError);
	}
	return false;
}

void shardvariable_OncePerFrame(void)
{
	// Check that some variables exist exists
	if (eaSize(&g_ShardVariableDefs.eaVariables))
	{
		/////////////////////////
		// Only subscribe to mapRequest container if this map wants it
		if (g_ShardVariableRef.bWantsMapRequested)
		{
			if (!IS_HANDLE_ACTIVE(g_ShardVariableRef.hMapRequestedContainer)) {
				char idBuf[128];
				// Set up subscription to primary container
				SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_SHARDVARIABLE),
									   ContainerIDToString(SHARD_VAR_MAPREQUESTED_CONTAINER_ID, idBuf), g_ShardVariableRef.hMapRequestedContainer);
			}
			else if (!GET_REF(g_ShardVariableRef.hMapRequestedContainer))
			{
				static int s_iShardVarRequestTickCount = 0;
				s_iShardVarRequestTickCount++;
				if ((s_iShardVarRequestTickCount % TICKS_TO_WAIT_IF_CONTAINER_MISSING_TO_REREQUEST) == 0) {
					// Re-request missing resources so  reference gets filled
					resReRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_SHARDVARIABLE));
				}
			}
		}

		/////////////////////////
		// Only subscribe to Broadcast container if there are any broadcast vars
		if (g_ShardVariableRef.bWantsBroadcast)
		{
			if (!IS_HANDLE_ACTIVE(g_ShardVariableRef.hBroadcastContainer)) {
				char idBuf[128];
				// Set up subscription to primary container
				SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_SHARDVARIABLE),
									   ContainerIDToString(SHARD_VAR_BROADCAST_CONTAINER_ID, idBuf), g_ShardVariableRef.hBroadcastContainer);
			}
			else if (!GET_REF(g_ShardVariableRef.hBroadcastContainer))
			{
				static int s_iShardVarRequestTickCount = 0;
				s_iShardVarRequestTickCount++;
				if ((s_iShardVarRequestTickCount % TICKS_TO_WAIT_IF_CONTAINER_MISSING_TO_REREQUEST) == 0) {
					// Re-request missing resources so  reference gets filled
					resReRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_SHARDVARIABLE));
				}
			}
		}
	}
}

// Adds all shard variables in the server shard variable list. Controlled by MapRequested/Broadcast
// AppServers load both vias hardvariable_Load() in this file.
// GameServers load both or just broadcast (based on map preference in shardvariable_MapLoad() in gslShardVariable
void shardVariable_AddAllVariables(bool bMapRequested, bool bBroadcast)
{
	S32 i;

	// Initialize the shard variable defs
	for (i = 0; i < eaSize(&g_ShardVariableDefs.eaVariables); ++i) 
	{
		if ((g_ShardVariableDefs.eaVariables[i]->bBroadcast && bBroadcast) || (!g_ShardVariableDefs.eaVariables[i]->bBroadcast && bMapRequested))
		{
			shardvariable_AddVariable(g_ShardVariableDefs.eaVariables[i]);
		}
	}
}

#endif

// This function needs to work even if shard variables are not enabled
// It does so by checking the loaded def data and not the running shared var data
const WorldVariable *shardvariable_GetDefaultValue(const char *pcName)
{
	int i;

	pcName = allocAddString(pcName);

	for(i=0; i<eaSize(&g_ShardVariableDefs.eaVariables); i++)
	{
		WorldVariable *pVar = &(g_ShardVariableDefs.eaVariables[i]->WorldVar);
		if (pcName == pVar->pcName)
		{
			return pVar;
		}
	}
	return NULL;
}


const char ***shardvariable_GetShardVariableNames(void)
{
	return &g_eaShardVariableNames;
}


// ----------------------------------------------------------------------------------
// Load Logic
// ----------------------------------------------------------------------------------

AUTO_STARTUP(ShardVariables);
void shardvariable_Load(void)
{
	char *pSharedMemoryName = NULL;	
	int i;

	loadstart_printf("Loading ShardVariables...");

	ParserLoadFiles(NULL, "defs/config/ShardVariables.def", "ShardVariables.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, parse_ShardVariableDefs, &g_ShardVariableDefs);

	for(i=0; i<eaSize(&g_ShardVariableDefs.eaVariables); ++i) {
		eaPush(&g_eaShardVariableNames, g_ShardVariableDefs.eaVariables[i]->WorldVar.pcName);
	}

#ifdef APPSERVER
	// Shard variables are enabled by default in app servers.
	{
		bool bMapRequested=true;
		bool bBroadcast=true;
		shardVariable_AddAllVariables(bMapRequested, bBroadcast);
	}
#endif

	loadend_printf(" done.");
}

AUTO_RUN_LATE;
void shardvariable_Init(void)
{
#if (defined(GAMESERVER) || defined(APPSERVER))
	const char *pcDictName = GlobalTypeToCopyDictionaryName(GLOBALTYPE_SHARDVARIABLE);

	// set up schema and copy dictionary for container references
	objRegisterNativeSchema(GLOBALTYPE_SHARDVARIABLE, parse_ShardVariableContainer, NULL, NULL, NULL, NULL, NULL);
	RefSystem_RegisterSelfDefiningDictionary(pcDictName, false, parse_ShardVariableContainer, false, false, NULL);
	resDictRequestMissingResources(pcDictName, RES_DICT_KEEP_NONE, false, objCopyDictHandleRequest);

	// Set up the list of containerIDs we expect (used for calling transactions with an array of containers)
	ea32Create(&g_eaShardVarContainerIDList);
	ea32Push(&g_eaShardVarContainerIDList, SHARD_VAR_MAPREQUESTED_CONTAINER_ID);
	ea32Push(&g_eaShardVarContainerIDList, SHARD_VAR_BROADCAST_CONTAINER_ID);
#endif
}

#include "AutoGen/ShardVariableCommon_h_ast.c"

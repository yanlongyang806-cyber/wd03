#include "sysutil.h"
#include "file.h"
#include "memorymonitor.h"
#include "foldercache.h"
#include "winutil.h"
#include "gimmeDLLWrapper.h"
#include "serverlib.h"
#include "UtilitiesLib.h"
#include "ResourceInfo.h"
#include "timing.h"

#include "GenericHttpServing.h"
#include "StringCache.h"

#include "controllerpub_h_ast.h"
#include "structNet.h"
#include "ClusterController_c_ast.h"
#include "ResourceInfo.h"
#include "Alerts.h"
#include "TimedCallback.h"
#include "StringUtil.h"
#include "structDefines.h"
#include "cmdparse.h"
#include "logging.h"
#include "ClusterController_ShardVariables.h"
#include "ClusterController_Commands.h"
#include "../../Crossroads/Common/ShardVariableCommon.h"
#include "ShardVariableCommon_h_ast.h"
#include "ClusterController_ShardVariables_c_ast.h"

AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "Name, ValueString, DefaultValueString, Errors, Set, RestoreToDefault");
typedef struct ClusterControllerShardVariable
{
	const char *pName; AST(KEY, POOL_STRING)
	char *pValueString;
	char *pDefaultValueString;
	char *pErrors; AST(ESTRING, FORMATSTRING(HTML_CLASS_IFEXPR = "$ ; divWarning2"))
	AST_COMMAND("Set", "SendCommandToShard All SetShardVar $FIELD(Name) $STRING(New value)")
	AST_COMMAND("RestoreToDefault", "SendCommandToShard All ResetShardVarToDefault $FIELD(Name)")
} ClusterControllerShardVariable;

static StashTable sShardVariablesByName = NULL;

void UpdateShardVariable(const char *pName /*pooled*/)
{
	ClusterControllerShardVariable *pVariable = StructCreate(parse_ClusterControllerShardVariable);
	char *pExistenceErrors = NULL;
	bool bValuesDontMatch = false;
	char *pAllValuesString = NULL;
	char *pAllDefaultsString = NULL;
	bool bDefaultsDontMatch = false;
	ShardVariableForClusterController *pVariableInShard;
	bool bFirst = true;

	pVariable->pName = pName;
	stashAddPointer(sShardVariablesByName, pName, pVariable, true);

	FOR_EACH_IN_STASHTABLE(gShardsByName, Shard, pShard)
	{
		if (pShard->pShardVariableList && (pVariableInShard = eaIndexedGetUsingString(&pShard->pShardVariableList->ppVariables, pName)))
		{
			estrConcatf(&pAllValuesString, "%s%s:%s", estrLength(&pAllValuesString) ? ", " : "", pShard->pShardName, pVariableInShard->pValueString);
			estrConcatf(&pAllDefaultsString, "%s%s:%s", estrLength(&pAllDefaultsString) ? ", " : "", pShard->pShardName, pVariableInShard->pDefaultValueString);

			if (bFirst)
			{
				pVariable->pValueString = strdup(pVariableInShard->pValueString ? pVariableInShard->pValueString : "");
				pVariable->pDefaultValueString = strdup(pVariableInShard->pDefaultValueString ? pVariableInShard->pDefaultValueString : "");
				bFirst = false;
			}
			else
			{
				if (stricmp_safe(pVariable->pValueString, pVariableInShard->pValueString) != 0)
				{
					SAFE_FREE(pVariable->pValueString);
					bValuesDontMatch = true;
				}

				if (stricmp_safe(pVariable->pDefaultValueString, pVariableInShard->pDefaultValueString) != 0)
				{
					SAFE_FREE(pVariable->pDefaultValueString);
					bDefaultsDontMatch = true;
				}
			}
		}
		else
		{
			estrConcatf(&pExistenceErrors, "Shard %s doesn't have this variable\n", pShard->pShardName);
		}
	}
	FOR_EACH_END;

	if (pExistenceErrors)
	{
		estrConcatf(&pVariable->pErrors, "%s\n", pExistenceErrors);
	}

	if (bValuesDontMatch)
	{
		estrConcatf(&pVariable->pErrors, "Shards have non-matching values: %s\n", pAllValuesString);
	}

	if (bDefaultsDontMatch)
	{
		estrConcatf(&pVariable->pErrors, "Shards have non-matching defaults: %s\n", pAllDefaultsString);
	}

	estrDestroy(&pExistenceErrors);
	estrDestroy(&pAllValuesString);
	estrDestroy(&pAllDefaultsString);
}

void UpdateShardVariables(void)
{
	const char **ppAllVariableNames = NULL;
	int i;

	if (!sShardVariablesByName)
	{
		sShardVariablesByName = stashTableCreateAddress(16);
		resRegisterDictionaryForStashTable("ShardVariables", RESCATEGORY_SYSTEM, 0, sShardVariablesByName, parse_ClusterControllerShardVariable);
		resDictSetHTMLExtraCommand("ShardVariables", "Reload ShardVariables from shards", "RequestAllShardVariables");
	}

	stashTableClearStruct(sShardVariablesByName, NULL, parse_ClusterControllerShardVariable);

	FOR_EACH_IN_STASHTABLE(gShardsByName, Shard, pShard)
	{
		if (pShard->pShardVariableList)
		{
			FOR_EACH_IN_EARRAY(pShard->pShardVariableList->ppVariables, ShardVariableForClusterController, pVariable)
			{
				eaPushUnique(&ppAllVariableNames, pVariable->pName);
			}
			FOR_EACH_END;
		}
	}
	FOR_EACH_END;

	for (i = 0; i < eaSize(&ppAllVariableNames); i++)
	{
		UpdateShardVariable(ppAllVariableNames[i]);
	}

	eaDestroy(&ppAllVariableNames);
}

AUTO_COMMAND;
void RequestAllShardVariables(void)
{
	FOR_EACH_IN_STASHTABLE(gShardsByName, Shard, pShard)
	{
		StructDestroySafe(parse_ShardVariableForClusterController_List, &pShard->pShardVariableList);

		if (pShard->pLink)
		{
			Packet *pPak = pktCreate(pShard->pLink, CLUSTERCONTROLLER_TO_CONTROLLER__REQUEST_SHARD_VARIABLES);
			pktSend(&pPak);
		}
	}
	FOR_EACH_END;
}

void ClusterController_HandleHereAreShardVariables(Packet *pPack, Shard *pShard)
{
	StructDestroySafe(parse_ShardVariableForClusterController_List, &pShard->pShardVariableList);
	pShard->pShardVariableList = ParserRecvStructSafe_Create(parse_ShardVariableForClusterController_List, pPack);

	UpdateShardVariables();
}

#include "../../Crossroads/Common/ShardVariableCommon.c"
#include "ClusterController_ShardVariables_c_ast.c"


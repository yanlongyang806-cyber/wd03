#include "ShardCluster.h"

#include "ShardCluster_h_ast.h"
#include "EString.h"
#include "textparser.h"
#include "ResourceInfo.h"
#include "GlobalTypes_h_ast.h"

static Cluster_Overview *spClusterOverview = NULL;

Cluster_Overview *GetShardClusterOverview_IfInCluster(void)
{
	if (spClusterOverview && eaSize(&spClusterOverview->ppShards) > 1)
	{
		return spClusterOverview;
	}

	return NULL;
}

Cluster_Overview *GetShardClusterOverview_EvenIfNotInCluster(void)
{
	return spClusterOverview;
}


//on the command line, 
AUTO_COMMAND ACMD_COMMANDLINE;
void SetShardClusterOverview_SuperEsc(char *pStr)
{
	char *pUnescaped = NULL;
	Cluster_Overview *pClusterOverview;
	estrSuperUnescapeString(&pUnescaped, pStr);
	
	
	
	pClusterOverview = StructCreate(parse_Cluster_Overview);
	ParserReadText(pUnescaped, parse_Cluster_Overview, pClusterOverview, 0);

	SetShardClusterOverview(pClusterOverview);

	estrDestroy(&pUnescaped);
}


void OVERRIDE_LATELINK_SetShardClusterOverview(Cluster_Overview *pOverview)
{
	if (!spClusterOverview)
	{
		spClusterOverview = pOverview;
		resRegisterDictionaryForEArray("svrClusterShards", RESCATEGORY_SYSTEM, 0, &spClusterOverview->ppShards, parse_ClusterShardSummary);
		ShardClusterOverviewChanged();
	}
	else
	{
		//have to be careful not to blow away the local name value pairs that hang off each ClusterShardSummary
		ClusterShardSummary **ppOldShardList = spClusterOverview->ppShards;

		spClusterOverview->ppShards = NULL;
		StructReset(parse_Cluster_Overview, spClusterOverview);

		spClusterOverview->ppShards = pOverview->ppShards;
		pOverview->ppShards = NULL;
		StructDestroy(parse_Cluster_Overview, pOverview);

		FOR_EACH_IN_EARRAY(spClusterOverview->ppShards, ClusterShardSummary, pNewSummary)
		{
			ClusterShardSummary *pOldSummary = eaIndexedGetUsingString(&ppOldShardList, pNewSummary->pShardName);
			if (pOldSummary->pLocalNameValuePairs)
			{
				pNewSummary->pLocalNameValuePairs = pOldSummary->pLocalNameValuePairs;
				pOldSummary->pLocalNameValuePairs = NULL;
			}
		}
		FOR_EACH_END;

		eaDestroyStruct(&ppOldShardList, parse_ClusterShardSummary);

		ShardClusterOverviewChanged();
	}
}

static char *spClusterName = NULL;
AUTO_CMD_ESTRING(spClusterName, ShardClusterName) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

static ClusterShardType eShardType = SHARDTYPE_UNDEFINED;

AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void ShardTypeInCluster(char *pTypeName) 
{
	eShardType = StaticDefineInt_FastStringToInt(ClusterShardTypeEnum, pTypeName, SHARDTYPE_UNDEFINED);
	if (eShardType == SHARDTYPE_UNDEFINED)
	{
		assertmsgf(0, "Didn't recognize shard type name %s", pTypeName);
	}
}

ClusterShardType ShardCluster_GetShardType(void)
{
	return eShardType;
}

char *OVERRIDE_LATELINK_ShardCommon_GetClusterName(void)
{
	return spClusterName;
}

AUTO_COMMAND_REMOTE;
void HereIsShardClusterOverview(ACMD_OWNABLE(Cluster_Overview) ppOverview)
{
	SetShardClusterOverview(*ppOverview);
	*ppOverview = NULL;
	
}









//now included from ControllerLink.c in UtilitiesLib
//#include "ShardCluster_h_ast.c"

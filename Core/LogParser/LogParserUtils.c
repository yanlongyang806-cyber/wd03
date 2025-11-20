#include "LogParser.h"
#include "earray.h"
#include "EString.h"
#include "LogParserUtils_c_ast.h"
#include "utilitiesLib.h"
AUTO_STRUCT;
typedef struct ClusterStartingID
{
	int iID; AST(KEY)
	char *pShardName;
} ClusterStartingID;

static ClusterStartingID **sppClusters = NULL;
bool sbLogParserGotShardNames = false;

//when the logparser is the cluster-level log parser, it will get several calls to this on the command line,
//specifying the starting ID ranges for the shards in the cluster. This allows us to write a magical
//little expression function which lets graphs get access to the shard names
AUTO_COMMAND;
void SetClusterShardStartingID(char *pShardName, int iID)
{
	ClusterStartingID *pCluster = StructCreate(parse_ClusterStartingID);

	if (!sppClusters)
	{
		eaIndexedEnable(&sppClusters, parse_ClusterStartingID);
	}

	pCluster->iID = iID;
	pCluster->pShardName = strdup(pShardName);

	eaPush(&sppClusters, pCluster);
	sbLogParserGotShardNames = true;

}

AUTO_COMMAND ACMD_NAME(GetClusterNameFromID);
char *LogParser_GetClusterNameFromID(int iID)
{
	static char *pRetVal = NULL;
	int i;

	if (!eaSize(&sppClusters))
	{
		estrCopy2(&pRetVal, GetShardNameFromShardInfoString());
		return pRetVal;
	}

	if (iID < sppClusters[0]->iID)
	{
		return "unknown";
	}

	for (i = 0; i < eaSize(&sppClusters) - 1; i++)
	{
		if (iID >= sppClusters[i]->iID && iID < sppClusters[i+1]->iID)
		{
			return sppClusters[i]->pShardName;
		}
	}

	return sppClusters[eaSize(&sppClusters) - 1]->pShardName;
}


void LogParser_AddClusterStuffToStandAloneCommandLine(char **ppCmdLine)
{
	int i;

	for (i = 0; i < eaSize(&sppClusters) - 1; i++)
	{
		estrConcatf(ppCmdLine, " -SetClusterShardStartingID %s %d ", 
			sppClusters[i]->pShardName, sppClusters[i]->iID);
	}
}

#include "LogParserUtils_c_ast.c"

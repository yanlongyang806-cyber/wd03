#include "ClusterController.h"
#include "ClusterController_InterestingStuff.h"
#include "textparser.h"
#include "ClusterController_InterestingStuff_h_ast.h"
#include "ClusterController_Commands.h"
#include "StashTable.h"
#include "HttpXpathSupport.h"

ClusterController_InterestingStuff *GetClusterControllerInterestingStuff(void)
{
	static ClusterController_InterestingStuff *spStuff = NULL;
	Shard *pWorstControllerFPS = NULL;
	Shard *pWorstObjDBLastContactSecsAgo = NULL;
	Shard *pWorstTransServerLastContactSecsAgo = NULL;
	Shard *pWorstMainQueueWaitTime = NULL;
	Shard *pWorstVIPQueueWaitTime = NULL;

	if (!spStuff)
	{
		spStuff = StructCreate(parse_ClusterController_InterestingStuff);
	}
	else
	{
		StructReset(parse_ClusterController_InterestingStuff, spStuff);
	}


	FOR_EACH_IN_STASHTABLE(gShardsByName, Shard, pShard)
	{
		if (pShard->summary.pInterestingStuff)
		{		
			spStuff->iNumShards++;

			spStuff->iNumPlayers += pShard->summary.iNumPlayers;
			spStuff->iNumGameServers += pShard->summary.iNumGameServers;


			spStuff->iNumLoggingIn += pShard->summary.pInterestingStuff->iPlayersLoggingIn;
			spStuff->iNumGatewaySessions += pShard->summary.pInterestingStuff->iNumGatewaySessions;
			spStuff->iTotalQueueSize += pShard->summary.pInterestingStuff->iMainQueueSize;
			spStuff->iTotalVIPQueueSize += pShard->summary.pInterestingStuff->iVIPQueueSize;

			spStuff->iNumGameServersBelow10fps += pShard->summary.pInterestingStuff->iNumGameServersBelow10fps;
			spStuff->iNumGameServers10to25fps += pShard->summary.pInterestingStuff->iNumGameServers10to25fps;
			spStuff->iNumGameServersAbove25fps += pShard->summary.pInterestingStuff->iNumGameServersAbove25fps;

			if (!pWorstControllerFPS 
				|| pShard->summary.pInterestingStuff->fControllerFps < pWorstControllerFPS->summary.pInterestingStuff->fControllerFps)
			{
				pWorstControllerFPS = pShard;
			}

			if (!pWorstObjDBLastContactSecsAgo 
				|| pShard->summary.pInterestingStuff->iObjDBLastContactSecsAgo > pWorstObjDBLastContactSecsAgo->summary.pInterestingStuff->iObjDBLastContactSecsAgo)
			{
				pWorstObjDBLastContactSecsAgo = pShard;
			}
		
			if (!pWorstTransServerLastContactSecsAgo 
				|| pShard->summary.pInterestingStuff->iTransServerLastContactSecsAgo > pWorstTransServerLastContactSecsAgo->summary.pInterestingStuff->iTransServerLastContactSecsAgo)
			{
				pWorstTransServerLastContactSecsAgo = pShard;
			}

			if (!pWorstMainQueueWaitTime 
				|| pShard->summary.pInterestingStuff->iHowLongFirstGuyInMainQueueHasBeenWaiting > pWorstMainQueueWaitTime->summary.pInterestingStuff->iHowLongFirstGuyInMainQueueHasBeenWaiting)
			{
				pWorstMainQueueWaitTime = pShard;
			}

			if (!pWorstVIPQueueWaitTime 
				|| pShard->summary.pInterestingStuff->iHowLongFirstGuyInVIPQueueHasBeenWaiting > pWorstVIPQueueWaitTime->summary.pInterestingStuff->iHowLongFirstGuyInVIPQueueHasBeenWaiting)
			{
				pWorstVIPQueueWaitTime = pShard;
			}

		}
	}
	FOR_EACH_END;

	if (pWorstControllerFPS)
	{
		estrPrintf(&spStuff->pWorstControllerFps, "%f (%s)", pWorstControllerFPS->summary.pInterestingStuff->fControllerFps, 
			pWorstControllerFPS->pShardName);
	}

	if (pWorstObjDBLastContactSecsAgo)
	{
		estrPrintf(&spStuff->pWorstObjDBLastContactSecsAgo, "%d (%s)", pWorstObjDBLastContactSecsAgo->summary.pInterestingStuff->iObjDBLastContactSecsAgo, 
			pWorstObjDBLastContactSecsAgo->pShardName);
	}

	if (pWorstTransServerLastContactSecsAgo)
	{
		estrPrintf(&spStuff->pWorstTransServerLastContactSecsAgo, "%d (%s)", pWorstTransServerLastContactSecsAgo->summary.pInterestingStuff->iTransServerLastContactSecsAgo, 
			pWorstTransServerLastContactSecsAgo->pShardName);
	}

	if (pWorstMainQueueWaitTime)
	{
		estrPrintf(&spStuff->pWorstMainQueueWaitTime, "%d (%s)", pWorstMainQueueWaitTime->summary.pInterestingStuff->iHowLongFirstGuyInMainQueueHasBeenWaiting, 
			pWorstMainQueueWaitTime->pShardName);
	}

	if (pWorstVIPQueueWaitTime)
	{
		estrPrintf(&spStuff->pWorstVIPQueueWaitTime, "%d (%s)", pWorstVIPQueueWaitTime->summary.pInterestingStuff->iHowLongFirstGuyInVIPQueueHasBeenWaiting, 
			pWorstVIPQueueWaitTime->pShardName);
	}

	return spStuff;
}

bool ProcessInterestingStuffIntoStructInfoForHttp(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags)
{
	ClusterController_InterestingStuff *pStuff = GetClusterControllerInterestingStuff();
	bool bRetVal;
	
	
	bRetVal = ProcessStructIntoStructInfoForHttp("", pArgList,
		pStuff, parse_ClusterController_InterestingStuff, iAccessLevel, 0, pStructInfo, eFlags);
	
	return bRetVal;
}


AUTO_RUN;
void SetupInterestingStuffList(void)
{
	RegisterCustomXPathDomain(".InterestingStuff", ProcessInterestingStuffIntoStructInfoForHttp, NULL);
}

#include "ClusterController_InterestingStuff_h_ast.c"

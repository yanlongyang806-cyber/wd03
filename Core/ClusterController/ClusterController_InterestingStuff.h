#pragma once

AUTO_STRUCT;
typedef struct ClusterController_InterestingStuff
{
	int iNumShards;


	int iNumPlayers;
	int iNumLoggingIn;
	int iNumGatewaySessions;
	int iTotalQueueSize;
	int iTotalVIPQueueSize;

	int iNumGameServers;
	int iNumGameServersBelow10fps;
	int iNumGameServers10to25fps;
	int iNumGameServersAbove25fps;

	//all of these are a number and then a shard name in parentheses
	char *pWorstControllerFps; AST(ESTRING)
	char *pWorstObjDBLastContactSecsAgo; AST(ESTRING)
	char *pWorstTransServerLastContactSecsAgo; AST(ESTRING)

	char *pWorstMainQueueWaitTime; AST(ESTRING) //worst of iHowLongFirstGuyInMainQueueHasBeenWaiting
	char *pWorstVIPQueueWaitTime; AST(ESTRING) //worst of iHowLongFirstGuyInVIPQueueHasBeenWaiting
} ClusterController_InterestingStuff;

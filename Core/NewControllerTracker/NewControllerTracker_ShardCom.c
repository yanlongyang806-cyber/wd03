#include "NewControllerTracker.h"
#include "Stashtable.h"
#include "NewControllerTracker_pub_h_ast.h"
#include "Structnet.h"
#include "serverlib.h"
#include "wininclude.h"
#include "Estring.h"
#include "timing.h"
#include "StringUtil.h"
#include "NewControllerTracker_h_ast.h"
#include "logging.h"
#include "Alerts.h"

#define SHARD_LINK_TIMEOUT 15

NetComm *gpShardComm = NULL;
NetListen *pShardListen = NULL;
void RemoveAndDestroyShard(ShardInfo_Full *pShard);

ShardInfo_Basic *FindPermanentShardByName(char *pShardName)
{
	ShardInfo_Perm *pPermShard = eaIndexedGetUsingString(&gStaticData.ppPermanentShards, pShardName);
	if (pPermShard)
	{
		return &pPermShard->basicInfo;
	}

	return NULL;
}


void AddPermanentShardByName(char *pShardName)
{
	ShardInfo_Full *pShard;

	ShardInfo_Perm *pPermShard;

	if (FindPermanentShardByName(pShardName))
	{
		return;
	}

	if (!stashFindPointer(gShardsByName, pShardName, &pShard))
	{
		return;
	}

	pPermShard = StructCreate(parse_ShardInfo_Perm);
	pPermShard->pName = strdup(pShardName);
	StructCopy(parse_ShardInfo_Basic, &pShard->basicInfo, &pPermShard->basicInfo, 0, 0, 0);
	pPermShard->basicInfo.bNotReallyThere = true;

	eaPush(&gStaticData.ppPermanentShards, pPermShard);
	SaveControllerTrackerStaticData();

	log_printf(LOG_MISC, "Added permanent shard: %s", pShardName);


}

void RemovePermanentShardByName(char *pShardName)
{
	int iIndex = eaIndexedFindUsingString(&gStaticData.ppPermanentShards, pShardName);
	if (iIndex >= 0)
	{
		StructDestroy(parse_ShardInfo_Perm, gStaticData.ppPermanentShards[iIndex]);
		eaRemove(&gStaticData.ppPermanentShards, iIndex);
		SaveControllerTrackerStaticData();

	}
}



int siNextUniqueID = 1;

void RemovePrePatchCommandLine(char *pShardOrClusterName)
{
	int i;

	for (i=0; i < eaSize(&gStaticData.ppPrePatchInfos); i++)
	{
		if (stricmp(gStaticData.ppPrePatchInfos[i]->pShardOrClusterName, pShardOrClusterName) == 0)
		{
			StructDestroy(parse_PrePatchInfo, eaRemove(&gStaticData.ppPrePatchInfos, i));
			return;
		}
	}
}

void SetPrePatchCommandLineInternal(char *pShardOrClusterName, char *pCurPatchCommandLine, char *pNewPatchCommandLine)
{
	int i;

	PrePatchInfo *pNew;

	log_printf(LOG_MISC, "Settinginternal  prepatch command line for shard or cluster %s to %s", pShardOrClusterName, pNewPatchCommandLine);

	for (i=0; i < eaSize(&gStaticData.ppPrePatchInfos); i++)
	{
		if (stricmp(gStaticData.ppPrePatchInfos[i]->pShardOrClusterName, pShardOrClusterName) == 0)
		{
			SAFE_FREE(gStaticData.ppPrePatchInfos[i]->pCurVersionCmdLine);
			SAFE_FREE(gStaticData.ppPrePatchInfos[i]->pNewVersionCmdLine);

			gStaticData.ppPrePatchInfos[i]->pCurVersionCmdLine = strdup(pCurPatchCommandLine);
			gStaticData.ppPrePatchInfos[i]->pNewVersionCmdLine = strdup(pNewPatchCommandLine);

			SaveControllerTrackerStaticData();

			return;
		}
	}

	pNew = StructCreate(parse_PrePatchInfo);
	pNew->pShardOrClusterName = strdup(pShardOrClusterName);
	pNew->pCurVersionCmdLine = strdup(pCurPatchCommandLine);
	pNew->pNewVersionCmdLine = strdup(pNewPatchCommandLine);

	eaPush(&gStaticData.ppPrePatchInfos, pNew);

	SaveControllerTrackerStaticData();


}

void UpdatePrePatchCommandLine(ShardInfo_Full *pShard)
{
	int i;


	for (i=0; i < eaSize(&gStaticData.ppPrePatchInfos); i++)
	{
		if (stricmp_safe(gStaticData.ppPrePatchInfos[i]->pShardOrClusterName, pShard->basicInfo.pClusterName) == 0
			|| stricmp_safe(gStaticData.ppPrePatchInfos[i]->pShardOrClusterName, pShard->basicInfo.pShardName) == 0)
		{
		
			SAFE_FREE(pShard->basicInfo.pPrePatchCommandLine);
			pShard->basicInfo.pPrePatchCommandLine = strdup(gStaticData.ppPrePatchInfos[i]->pNewVersionCmdLine);
		

			return;
		}
	}
}

void UpdateAllShardPrepatchCommandLines(void)
{
	StashTableIterator stashIterator;
	StashElement stashElement;


	if (!gShardsByName)
	{
		return;
	}

	stashGetIterator(gShardsByName, &stashIterator);

	while (stashGetNextElement(&stashIterator, &stashElement))
	{
		ShardInfo_Full *pShard = stashElementGetPointer(stashElement);
		UpdatePrePatchCommandLine(pShard);
	}
}







void InitShardTablesAndStuff(void)
{
	gShardsByID = stashTableCreateInt(64);
	gShardsByName = stashTableCreateWithStringKeys(64, StashDefault);
}

ShardCategory *FindCategoryForShard(ShardInfo_Full *pShard)
{
	int i;
	ShardCategory *pNewCategory;

	for (i=0; i < eaSize(&gppShardCategories); i++)
	{
		if (gppShardCategories[i]->pCategoryName == pShard->basicInfo.pShardCategoryName && gppShardCategories[i]->pProductName == pShard->basicInfo.pProductName)
		{
			return gppShardCategories[i];
		}
	}

	pNewCategory = calloc(sizeof(ShardCategory), 1);
	pNewCategory->pProductName = pShard->basicInfo.pProductName;
	pNewCategory->pCategoryName = pShard->basicInfo.pShardCategoryName;

	eaPush(&gppShardCategories, pNewCategory);
	return pNewCategory;
}

	

void AddShard(ShardInfo_Full *pShard)
{
	ShardCategory *pCategory = FindCategoryForShard(pShard);

	ShardInfo_Full *pOtherShard;
	ShardInfo_Basic *pPermShard;

	eaPush(&pCategory->ppShards, pShard);
	pShard->basicInfo.iUniqueID = siNextUniqueID++;

	stashIntAddPointer(gShardsByID, pShard->basicInfo.iUniqueID, pShard, false);

	//uniquify the shard name if necessary
	if (stashFindPointer(gShardsByName, pShard->basicInfo.pShardName, &pOtherShard))
	{
		if (stricmp(pShard->basicInfo.pShardCategoryName, "Dev") == 0)
		{
			int i = 1;
			char *pTempEString = NULL;

			estrStackCreate(&pTempEString);

			while (1)
			{

				estrPrintf(&pTempEString, "%s_%d", pShard->basicInfo.pShardName, i);
				if (!stashFindPointer(gShardsByName, pTempEString, &pOtherShard))
				{
					free(pShard->basicInfo.pShardName);
					pShard->basicInfo.pShardName = strdup(pTempEString);
					estrDestroy(&pTempEString);
					break;
				}

				i++;
			}
		}
		else
		{
			RemoveAndDestroyShard(pOtherShard);
			pOtherShard->pParent->pShardInfo = NULL;

		}
	}

		
	stashAddPointer(gShardsByName, pShard->basicInfo.pShardName, pShard, false);
	
	if ((pPermShard = FindPermanentShardByName(pShard->basicInfo.pShardName)))
	{
		if (StructCompare(parse_ShardInfo_Basic, pPermShard, &pShard->basicInfo, 0, 0, 0) != 0)
		{
			StructCopy(parse_ShardInfo_Basic, &pShard->basicInfo, pPermShard, 0, 0, 0);
			pPermShard->bNotReallyThere = true;
			SaveControllerTrackerStaticData();
		}
	}
}


void RemoveAndDestroyShard(ShardInfo_Full *pShard)
{
	int i;

	ShardCategory *pCategory = FindCategoryForShard(pShard);

	for (i=0; i < eaSize(&pCategory->ppShards); i++)
	{
		if (pCategory->ppShards[i] == pShard)
		{
			stashIntRemovePointer(gShardsByID, pShard->basicInfo.iUniqueID, NULL);
			stashRemovePointer(gShardsByName, pShard->basicInfo.pShardName, NULL);
			StructDestroy(parse_ShardInfo_Full, pShard);
			eaRemoveFast(&pCategory->ppShards, i);
			return;
		}
	}

	assertmsgf(0, "Couldn't find and destroy shard %s (%s %s)",
		pShard->basicInfo.pShardName,
		pShard->basicInfo.pProductName,
		pShard->basicInfo.pShardCategoryName);
}


int ShardConnectionDisconnect(NetLink* link,ShardConnectionUserData *pUserData)
{
	if (pUserData->pShardInfo)
	{
		char *pDisconnectReason = NULL;
		estrStackCreate(&pDisconnectReason);

		linkGetDisconnectReason(link, &pDisconnectReason);
		printf("%s  Disconnecting shard: %s. Reason: %s\n", timeGetLocalDateStringFromSecondsSince2000(timeSecondsSince2000()), pUserData->pShardInfo->basicInfo.pShardName, pDisconnectReason);
		log_printf(LOG_MISC, "Disconnecting shard: %s. Reason: %s\n", pUserData->pShardInfo->basicInfo.pShardName, pDisconnectReason);
		estrDestroy(&pDisconnectReason);

		RemoveAndDestroyShard(pUserData->pShardInfo);
	}

	pUserData->pShardInfo = NULL;
	return 1;
}

int ShardConnectionConnect(NetLink* link,ShardConnectionUserData *pUserData)
{
	pUserData->iLastUpdateTime = timeSecondsSince2000();
//	linkSetTimeout(link, 120.0f);
	return 1;
}

void UpdateShardClusterVersion(ShardInfo_Full *pUpdatingShard)
{
	bool bSomethingChanged = false;

	FOR_EACH_IN_STASHTABLE(gShardsByName, ShardInfo_Full, pShard)
	{
		if (pShard != pUpdatingShard)
		{
			if (stricmp_safe(pShard->basicInfo.pClusterName, pUpdatingShard->basicInfo.pClusterName) == 0)
			{
				if (stricmp_safe(pShard->basicInfo.pVersionString, pUpdatingShard->basicInfo.pVersionString) != 0)
				{
					CRITICAL_NETOPS_ALERT("NONMATCHING_CLUSTER_VERSIONS", "Shards %s and %s both claim to be in cluster %s, but have non-matching versions %s and %s",
						pUpdatingShard->basicInfo.pShardName, pShard->basicInfo.pShardName, pShard->basicInfo.pClusterName, pUpdatingShard->basicInfo.pVersionString, pShard->basicInfo.pVersionString);
					return;
				}
		
				if (stricmp_safe(pShard->basicInfo.pPatchCommandLine, pUpdatingShard->basicInfo.pPatchCommandLine) != 0)
				{
					CRITICAL_NETOPS_ALERT("NONMATCHING_CLUSTER_PCMDLINES", "Shards %s and %s both claim to be in cluster %s, but have non-matching patch command lines %s and %s",
						pUpdatingShard->basicInfo.pShardName, pShard->basicInfo.pShardName, pShard->basicInfo.pClusterName, pUpdatingShard->basicInfo.pPatchCommandLine, pShard->basicInfo.pPatchCommandLine);
					return;
				}
			}
		}
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(gStaticData.ppPermanentShards, ShardInfo_Perm, pPermShard)
	{
		if (stricmp_safe(pPermShard->basicInfo.pClusterName, pUpdatingShard->basicInfo.pClusterName) == 0)
		{
			if (stricmp_safe(pPermShard->basicInfo.pVersionString, pUpdatingShard->basicInfo.pVersionString) == 0
				&& stricmp_safe(pPermShard->basicInfo.pPatchCommandLine, pUpdatingShard->basicInfo.pPatchCommandLine) == 0)
			{
				//do nothing
			}
			else
			{
				bSomethingChanged = true;
				SAFE_FREE(pPermShard->basicInfo.pVersionString);
				SAFE_FREE(pPermShard->basicInfo.pPatchCommandLine);
				pPermShard->basicInfo.pVersionString = strdup(pUpdatingShard->basicInfo.pVersionString);
				pPermShard->basicInfo.pPatchCommandLine = strdup(pUpdatingShard->basicInfo.pPatchCommandLine);
			}
		}
	}
	FOR_EACH_END;

	if (bSomethingChanged)
	{
		SaveControllerTrackerStaticData();
	}

	

}



void HandleHereIsShardInfo(Packet *pPack, ShardConnectionUserData *pUserData)
{
	pUserData->iLastUpdateTime = timeSecondsSince2000();


	if (pUserData->pShardInfo)
	{
		RemoveAndDestroyShard(pUserData->pShardInfo);
	}

	pUserData->pShardInfo = StructCreate(parse_ShardInfo_Full);

	ParserRecvStructSafe(parse_ShardInfo_Full, pPack, pUserData->pShardInfo);

	pUserData->pShardInfo->pParent = pUserData;

	AddShard(pUserData->pShardInfo);

	printf("%s  New shard: %s\n", timeGetLocalDateStringFromSecondsSince2000(timeSecondsSince2000()), pUserData->pShardInfo->basicInfo.pShardName);
	log_printf(LOG_MISC, "New shard: %s", pUserData->pShardInfo->basicInfo.pShardName);

	UpdatePrePatchCommandLine(pUserData->pShardInfo);
	
	if (pUserData->pShardInfo->basicInfo.pClusterName)
	{
		UpdateShardClusterVersion(pUserData->pShardInfo);
	}
	
}

void HandleHereAreLastMinuteFiles(Packet *pPack, ShardConnectionUserData *pUserData)
{
	if (!pUserData->pShardInfo)
	{
		return;
	}

	if (pUserData->pShardInfo->pAllLastMinuteFiles)
	{
		StructDestroy(parse_AllLastMinuteFilesInfo, pUserData->pShardInfo->pAllLastMinuteFiles);
	}

	pUserData->pShardInfo->pAllLastMinuteFiles = StructCreate(parse_AllLastMinuteFilesInfo);

	ParserRecvStructSafe(parse_AllLastMinuteFilesInfo, pPack, pUserData->pShardInfo->pAllLastMinuteFiles);
}

void HandlereHereIsShardPerfInfo(Packet *pPack, ShardConnectionUserData *pUserData)
{
	pUserData->iLastUpdateTime = timeSecondsSince2000();


	if (!pUserData->pShardInfo)
	{
		return;
	}
	StructDeInit(parse_ShardInfo_Perf, &pUserData->pShardInfo->perfInfo);

	ParserRecvStructSafe(parse_ShardInfo_Perf, pPack, &pUserData->pShardInfo->perfInfo);
	pUserData->pShardInfo->perfInfo.iLastUpdateTime = timeSecondsSince2000();
}

void HandleHereAreLoginServerIPs(Packet *pPack, ShardConnectionUserData *pUserData)
{
	U32 iIP;

	if (!pUserData->pShardInfo)
	{
		return;
	}

	ea32Destroy(&pUserData->pShardInfo->basicInfo.allLoginServerIPs);


	while ((iIP = pktGetBits(pPack, 32)))
	{
		ea32Push(&pUserData->pShardInfo->basicInfo.allLoginServerIPs, iIP);
	}
}

void HandleHereAreLoginServerIPsAndPorts(Packet *pPack, ShardConnectionUserData *pUserData)
{
	if (!pUserData->pShardInfo)
	{
		return;
	}

	StructDestroySafe(parse_PortIPPairList, &pUserData->pShardInfo->basicInfo.pLoginServerPortsAndIPs);
	pUserData->pShardInfo->basicInfo.pLoginServerPortsAndIPs = ParserRecvStructSafe_Create(parse_PortIPPairList, pPack);

	//whenever we are sent this, copy the data into allLoginServerIPs, for compatibility purposes (note that
	//controllers always send that first and this second if they send both)
	ea32Destroy(&pUserData->pShardInfo->basicInfo.allLoginServerIPs);

	FOR_EACH_IN_EARRAY_FORWARDS(pUserData->pShardInfo->basicInfo.pLoginServerPortsAndIPs->ppPortIPPairs, PortIPPair, pPair)
	{
		if (pPair->iPort == DEFAULT_LOGINSERVER_PORT)
		{
			ea32Push(&pUserData->pShardInfo->basicInfo.allLoginServerIPs, pPair->iIP);
		}
	}
	FOR_EACH_END;
}

void SendGlobalChatOnline(NetLink* link, ShardConnectionUserData *pUserData)
{
	if (linkConnected(link))
	{
		Packet *pkt = pktCreate(link, FROM_NEWCONTROLLERTRACKER_TO_SHARD_GLOBALCHAT_ONLINE);
		pktSend(&pkt);
	}
}

void HandleHereIsVersionForOfflineShard(Packet *pak)
{
	ShardVersionInfoFromStandaloneUtil *pVersionInfo = StructCreate(parse_ShardVersionInfoFromStandaloneUtil);
	ShardInfo_Basic *pPermShard;

	ParserRecvStructSafe(parse_ShardVersionInfoFromStandaloneUtil, pak, pVersionInfo);

	//don't do anything if we think this shard is already running
	if (stashFindPointer(gShardsByName, pVersionInfo->pShardName, NULL))
	{
		log_printf(LOG_ERRORS, "Got HandleHereIsVersionForOfflineShard for shard %s while it was already running... possibly alarming",
			pVersionInfo->pShardName);
	}
	else if (!(pPermShard = FindPermanentShardByName(pVersionInfo->pShardName)))
	{
		//do nothing, not at all alarming, most shards aren't perm shards
	}
	else
	{
		SAFE_FREE(pPermShard->pVersionString);
		SAFE_FREE(pPermShard->pPatchCommandLine);

		pPermShard->pVersionString = strdup(pVersionInfo->pVersionString);
		pPermShard->pPatchCommandLine = strdup(pVersionInfo->pPatchCommandLine);

		//note that there's some redundancy here, in that when the parent CT gets this update, it will send it to all the children, but
		//they will also get it directly from the standalone tool. But that's OK.
		SaveControllerTrackerStaticData();
	}

	StructDestroy(parse_ShardVersionInfoFromStandaloneUtil, pVersionInfo);
}

void ShardConnectionHandleMsg(Packet *pak,int cmd, NetLink *link,ShardConnectionUserData *pUserData)
{
	switch(cmd)
	{
	xcase TO_NEWCONTROLLERTRACKER_FROM_SHARD_HERE_IS_SHARD_INFO:
		HandleHereIsShardInfo(pak, pUserData);
		break;

	xcase TO_NEWCONTROLLERTRACKER_FROM_SHARD_HERE_ARE_LAST_MINUTE_FILES:
		HandleHereAreLastMinuteFiles(pak, pUserData);
		break;

	xcase TO_NEWCONTROLLERTRACKER_FROM_SHARD_HERE_IS_SHARD_PERF_INFO:
		HandlereHereIsShardPerfInfo(pak, pUserData);
		break;

	xcase TO_NEWCONTROLLERTRACKER_FROM_GLOBAL_CHATSERVER_ONLINE_MESSAGE:
		linkIterate(pShardListen, SendGlobalChatOnline);
		break;

	xcase TO_NEWCONTROLLERTRACKER_FROM_SHARD_HERE_ARE_LOGINSERVER_IPS:
		HandleHereAreLoginServerIPs(pak, pUserData);

	xcase TO_NEWCONTROLLERTRACKER_FROM_SHARD_HERE_ARE_LOGINSERVER_IPS_AND_PORTS:
		HandleHereAreLoginServerIPsAndPorts(pak, pUserData);

	xcase TO_NEWCONTROLLERTRACKER_HERE_IS_VERSION_FOR_OFFLINE_SHARD:
		HandleHereIsVersionForOfflineShard(pak);
		break;

	}
}

static void CheckForTimeout(NetLink* link, ShardConnectionUserData *pUserData)
{
	if (pUserData->iLastUpdateTime && pUserData->iLastUpdateTime < timeSecondsSince2000() - SHARD_LINK_TIMEOUT)
	{
		if (pUserData->pShardInfo)
		{
			log_printf(LOG_MISC, "Timing out shard %s", pUserData->pShardInfo->basicInfo.pShardName);
		}

		linkRemove(&link);
	}
}


void ShardComPeriodicUpdate(void)
{
	linkIterate(pShardListen, CheckForTimeout);
}


void InitShardCom(void)
{
	InitShardTablesAndStuff();

	loadstart_printf("Trying to start listening for shards...");

	gpShardComm = commCreate(0, 0);

	while (!(pShardListen = commListen(gpShardComm,LINKTYPE_SHARD_NONCRITICAL_20MEG, LINK_FORCE_FLUSH,CONTROLLERTRACKER_SHARD_INFO_PORT,
		ShardConnectionHandleMsg,ShardConnectionConnect,ShardConnectionDisconnect,sizeof(ShardConnectionUserData))))
	{
		Sleep(1);
	}

	loadend_printf("done");
}

void FindAllShardsInCluster(ShardInfo_Full ***pppShards, char *pClusterName)
{
	FOR_EACH_IN_STASHTABLE(gShardsByName, ShardInfo_Full, pShard)
	{
		if (stricmp_safe(pShard->basicInfo.pClusterName, pClusterName) == 0)
		{
			eaPush(pppShards, pShard);
		}
	}
	FOR_EACH_END;
}

void FindAllPermShardsInCluster(ShardInfo_Perm ***pppPermShards, char *pClusterName)
{
	FOR_EACH_IN_EARRAY(gStaticData.ppPermanentShards, ShardInfo_Perm, pPermShard)
	{
		if (stricmp_safe(pPermShard->basicInfo.pClusterName, pClusterName) == 0)
		{
			eaPush(pppPermShards, pPermShard);
		}
	}
	FOR_EACH_END;
}

bool UpdatePatchingCommandLineWithVersion(char **ppCommandLine, char *pVersionString)
{
	char *pBeginning;
	char *pEnd;
//just to be a bit careful, we want to find the "-name foo" in the command line and replace "foo",
	//rather than assume we know what the rest of the string will be

	pBeginning = strstri(*ppCommandLine, "-name ");

	if (!pBeginning)
	{
		return false;
	}

	pBeginning += 5;

	while (IS_WHITESPACE(*pBeginning))
	{
		pBeginning++;
	}

	pEnd = pBeginning;

	while (*pEnd && !IS_WHITESPACE(*pEnd))
	{
		pEnd++;
	}

	estrReplaceRangeWithString(ppCommandLine, pBeginning - *ppCommandLine, pEnd - pBeginning, pVersionString);

	return true;
}

AUTO_COMMAND;
char *SetPrePatchVersionForCluster(char *pClusterName, char *pVersionString)
{
	ShardInfo_Full **ppShards = NULL;
	ShardInfo_Perm **ppPermShards = NULL;
	char *pNewPatchString = NULL;
	char *pOldPatchString = NULL;

	FindAllShardsInCluster(&ppShards, pClusterName);
	FindAllPermShardsInCluster(&ppPermShards, pClusterName);

	if (StringIsAllWhiteSpace(pVersionString))
	{
		FOR_EACH_IN_EARRAY(ppShards, ShardInfo_Full, pShard)
		{
			RemovePrePatchCommandLine(pShard->basicInfo.pShardName);
			SAFE_FREE(pShard->basicInfo.pPrePatchCommandLine);
			pShard->basicInfo.pPrePatchCommandLine = strdup("");
		}
		FOR_EACH_END;

		FOR_EACH_IN_EARRAY(ppPermShards, ShardInfo_Perm, pPermShard)
		{
			RemovePrePatchCommandLine(pPermShard->basicInfo.pShardName);
			SAFE_FREE(pPermShard->basicInfo.pPrePatchCommandLine);
			pPermShard->basicInfo.pPrePatchCommandLine = strdup("");
		}
		FOR_EACH_END;

		SetPrePatchCommandLineInternal(pClusterName, "", "");

		eaDestroy(&ppShards);
		eaDestroy(&ppPermShards);
		return "SUCCESS. Prepatch command set to be empty";
	}

	if (!ppShards && !ppPermShards)
	{
		return "FAILURE. Found no shards or perm shards in that cluster";
	}

	if (ppShards)
	{
		estrCopy2(&pOldPatchString, ppShards[0]->basicInfo.pPatchCommandLine);
	}
	else
	{
		estrCopy2(&pOldPatchString, ppPermShards[0]->basicInfo.pPatchCommandLine);
	}

	estrCopy2(&pNewPatchString, pOldPatchString);

	if (!UpdatePatchingCommandLineWithVersion(&pNewPatchString, pVersionString))
	{
		estrDestroy(&pOldPatchString);
		estrDestroy(&pNewPatchString);
		eaDestroy(&ppShards);
		eaDestroy(&ppPermShards);
		return "FAILURE: could not find -name in current patching command line";
	}

	FOR_EACH_IN_EARRAY(ppShards, ShardInfo_Full, pShard)
	{
		RemovePrePatchCommandLine(pShard->basicInfo.pShardName);
		SAFE_FREE(pShard->basicInfo.pPrePatchCommandLine);
		pShard->basicInfo.pPrePatchCommandLine = strdup(pNewPatchString);
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(ppPermShards, ShardInfo_Perm, pPermShard)
	{
		RemovePrePatchCommandLine(pPermShard->basicInfo.pShardName);
		SAFE_FREE(pPermShard->basicInfo.pPrePatchCommandLine);
		pPermShard->basicInfo.pPrePatchCommandLine = strdup(pNewPatchString);
	}
	FOR_EACH_END;

	SetPrePatchCommandLineInternal(pClusterName, pOldPatchString, pNewPatchString);

	estrDestroy(&pOldPatchString);
	estrDestroy(&pNewPatchString);
	eaDestroy(&ppShards);
	eaDestroy(&ppPermShards);

	return "SUCCESS";
}

AUTO_COMMAND;
char *SetPrePatchVersion(char *pShardName, char *pVersionString)
{
	ShardInfo_Full *pShard;
	ShardInfo_Basic *pPermShard = NULL;
	char *pNewPatchString = NULL;


	if (!stashFindPointer(gShardsByName, pShardName, &pShard))
	{
		return "FAILURE. Shard not found.";
	}

	if (pShard->basicInfo.pClusterName)
	{
		return SetPrePatchVersionForCluster(pShard->basicInfo.pClusterName, pVersionString);
	}

	pPermShard = FindPermanentShardByName(pShardName);
	




	if (StringIsAllWhiteSpace(pVersionString))
	{
		SAFE_FREE(pShard->basicInfo.pPrePatchCommandLine);
		pShard->basicInfo.pPrePatchCommandLine = strdup("");

		if (pPermShard)
		{
			SAFE_FREE(pPermShard->pPrePatchCommandLine);
			pPermShard->pPrePatchCommandLine = strdup("");
		}

		SetPrePatchCommandLineInternal(pShard->basicInfo.pShardName, "", "");
		return "SUCCESS. Prepatch command set to be empty";
	}


	estrCopy2(&pNewPatchString, pShard->basicInfo.pPatchCommandLine);

	if (!UpdatePatchingCommandLineWithVersion(&pNewPatchString, pVersionString))
	{
		return "FAILURE: could not find -name in current patching command line";
	}

	if (pPermShard)
	{
		SAFE_FREE(pPermShard->pPrePatchCommandLine);
		pPermShard->pPrePatchCommandLine = strdup(pNewPatchString);
	}

	SetPrePatchCommandLineInternal(pShard->basicInfo.pShardName, pShard->basicInfo.pPatchCommandLine, pNewPatchString);


	SAFE_FREE(pShard->basicInfo.pPrePatchCommandLine);
	pShard->basicInfo.pPrePatchCommandLine = strdup(pNewPatchString);


	estrDestroy(&pNewPatchString);


	return "SUCCESS";
	


}




AUTO_COMMAND;
void MakeShardPermanent(char *pShardName)
{
	AddPermanentShardByName(pShardName);
}

AUTO_COMMAND;
void RemovePermanentShard(char *pShardName)
{
	RemovePermanentShardByName(pShardName);
}
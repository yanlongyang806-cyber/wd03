#include "HttpServingStats.h"
#include "TimedCallback.h"
#include "TextParser.h"
#include "HttpServingStats_c_Ast.h"
#include "HttpServingStats_h_Ast.h"
#include "net.h"
#include "timing.h"

#define SECONDS_RECENT_DATA_WINDOW_SIZE 15

AUTO_STRUCT;
typedef struct HttpLinkStatsTracker
{
	U64 iSentBytesLastTime;
	NetLink *pLink; NO_AST
} HttpLinkStatsTracker;
	

AUTO_STRUCT;
typedef struct HttpIPStats_Internal
{
	U32 iIP;
	U64 iTotalSentOnClosedLinks;

	U64 iRing[SECONDS_RECENT_DATA_WINDOW_SIZE];
	U64 iRingSum;
	U32 iLastTimeProcessed;

	HttpLinkStatsTracker **ppLinkTrackers;
} HttpIPStats_Internal;

AUTO_STRUCT;
typedef struct HttpPortStats_Internal
{
	U32 iPortNum;
	HttpIPStats_Internal **ppIPStats;
} HttpPortStats_Internal;

static HttpPortStats_Internal **sppInternalStats = NULL;
static bool sbInitted = false;

HttpPortStats_Internal *HttpStats_GetPortStats(U32 iPort)
{
	int i;
	HttpPortStats_Internal *pPortStats;

	for (i=0; i < eaSize(&sppInternalStats); i++)
	{
		if (sppInternalStats[i]->iPortNum == iPort)
		{
			return sppInternalStats[i];
		}
	}

	pPortStats = StructCreate(parse_HttpPortStats_Internal);
	pPortStats->iPortNum = iPort;

	eaPush(&sppInternalStats, pPortStats);
	return pPortStats;
}



HttpIPStats_Internal *HttpStats_GetIPStats(HttpPortStats_Internal *pPortStats, U32 iIP)
{
	int i;
	HttpIPStats_Internal *pNew;


	for (i=0; i < eaSize(&pPortStats->ppIPStats); i++)
	{
		if (pPortStats->ppIPStats[i]->iIP == iIP)
		{
			return pPortStats->ppIPStats[i];
		}
	}

	pNew = StructCreate(parse_HttpIPStats_Internal);
	eaPush(&pPortStats->ppIPStats, pNew);
	pNew->iIP = iIP;
	return pNew;
}

	

void HttpStats_Update(U32 iCurTime)
{
	U64 iCurTotal = 0;
	int i;
	int j;
	int k;


	if (!sbInitted)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	for (k=0; k < eaSize(&sppInternalStats); k++)
	{
		HttpPortStats_Internal *pPortStats =  sppInternalStats[k];

		for (i=0; i < eaSize(&pPortStats->ppIPStats); i++)
		{
			HttpIPStats_Internal *pIPStats = pPortStats->ppIPStats[i];
			int iIndex = iCurTime % SECONDS_RECENT_DATA_WINDOW_SIZE;

			pIPStats->iRingSum -= pIPStats->iRing[iIndex];
			pIPStats->iRing[iIndex] = 0;

			for (j=0; j < eaSize(&pIPStats->ppLinkTrackers); j++)
			{
				HttpLinkStatsTracker *pLinkStatsTracker = pIPStats->ppLinkTrackers[j];
				const LinkStats *pLinkStats = linkStats(pLinkStatsTracker->pLink);
				if (pLinkStats)
				{
					pIPStats->iRing[iIndex] +=  pLinkStats->send.real_bytes - pLinkStatsTracker->iSentBytesLastTime;
					pLinkStatsTracker->iSentBytesLastTime = pLinkStats->send.real_bytes;
				}
			}

			pIPStats->iRingSum += pIPStats->iRing[iIndex];
			pIPStats->iLastTimeProcessed = iCurTime;

		}
	}

	PERFINFO_AUTO_STOP();
}


void HttpStats_Init(void)
{
	sbInitted = true;
}



void HttpStats_NewLink(NetLink *pLink)
{
	HttpIPStats_Internal *pIPStats;
	HttpLinkStatsTracker *pTracker;
	HttpPortStats_Internal *pPortStats;

	if (!sbInitted)
	{
		return;
	}

	pPortStats = HttpStats_GetPortStats(linkGetListenPort(pLink));

	pIPStats = HttpStats_GetIPStats(pPortStats, linkGetIp(pLink));
	pTracker = StructCreate(parse_HttpLinkStatsTracker);
	pTracker->pLink = pLink;

	eaPush(&pIPStats->ppLinkTrackers, pTracker);
}
void HttpStats_LinkClosed(NetLink *pLink)
{
	HttpIPStats_Internal *pIPStats;
	const LinkStats *pLinkStats = linkStats(pLink);
	int i;

	if (!pLinkStats)
	{
		return;
	}

	if (!sbInitted)
	{
		return;
	}

	pIPStats = HttpStats_GetIPStats(HttpStats_GetPortStats(linkGetListenPort(pLink)), linkGetIp(pLink));
	
	for (i=0; i < eaSize(&pIPStats->ppLinkTrackers); i++)
	{
		if (pIPStats->ppLinkTrackers[i]->pLink == pLink)
		{
			HttpLinkStatsTracker *pStatsTracker = pIPStats->ppLinkTrackers[i];

			//we want to know where to add bytes from a closed link into our ring buffer. But our ring buffer might
			//have just been created, and we might not know during which clock second stuff was last added
			U32 iPresumedRingTime;
			int iIndex;
			U64 iDelta;

			pIPStats->iTotalSentOnClosedLinks += pLinkStats->send.real_bytes;


			if (pIPStats->iLastTimeProcessed)
			{
				iPresumedRingTime = pIPStats->iLastTimeProcessed;
			}
			else
			{
				iPresumedRingTime = timeSecondsSince2000() - 1;
			}


			iIndex = iPresumedRingTime % SECONDS_RECENT_DATA_WINDOW_SIZE;
			iDelta = pLinkStats->send.real_bytes - pStatsTracker->iSentBytesLastTime;
			pIPStats->iRingSum += iDelta;
			pIPStats->iRing[iIndex] += iDelta;
			
			StructDestroy(parse_HttpLinkStatsTracker, pStatsTracker);
			eaRemoveFast(&pIPStats->ppLinkTrackers, i);
			return;
		}
	}
}

HttpStats *GetHttpStats(U32 iPort)
{
	static HttpStats stats = {0};
	int i, j;

	HttpPortStats_Internal *pPortStats;
	

	if (!sbInitted)
	{
		return &stats;
	}

	pPortStats = HttpStats_GetPortStats(iPort);
	StructReset(parse_HttpStats, &stats);

	for (i=0; i < eaSize(&pPortStats->ppIPStats); i++)
	{
		HttpIPStats_Internal *pIPStatsInternal = pPortStats->ppIPStats[i];
		HttpIPStats *pIPStats = StructCreate(parse_HttpIPStats);
		
		pIPStats->iIP = pIPStatsInternal->iIP;
		pIPStats->iBytesLast15Secs = pIPStatsInternal->iRingSum;
		stats.iBytesSentLast15Secs += pIPStatsInternal->iRingSum;
		pIPStats->iTotalBytesSent = pIPStatsInternal->iTotalSentOnClosedLinks;

		for (j=0; j < eaSize(&pIPStatsInternal->ppLinkTrackers); j++)
		{
			const LinkStats *pLinkStats = linkStats(pIPStatsInternal->ppLinkTrackers[j]->pLink);
			if (pLinkStats)
			{
				pIPStats->iTotalBytesSent += pLinkStats->send.real_bytes;
			}
		}

		eaPush(&stats.ppIPStats, pIPStats);

		stats.iTotalBytesSent += pIPStats->iTotalBytesSent;
	}

	return &stats;
}



#include "HttpServingStats_c_Ast.c"
#include "HttpServingStats_h_Ast.c"
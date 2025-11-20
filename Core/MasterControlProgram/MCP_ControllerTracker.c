#include "MasterControlProgram.h"
#include "globalcomm.h"
#include "GlobalTypes.h"
#include "structNet.h"
#include "autogen/NewControllerTracker_pub_h_ast.h"
#include "timing.h"
#include "estring.h"
#include "file.h"
#include "sock.h"
#include "accountnet.h"
#include "Organization.h"

bool gbAvailableShardsChanged = true;
ShardInfo_Basic_List gAvailableShardList = {NULL};

NetLink *gpLinkToNewControllerTracker = NULL;


int gbGotLastMinuteFiles;
bool gbWaitingForLastMinuteFiles = false;
bool gbCopyLastMinuteFilesToXBox = false;

void *DoSpecialLastMinuteFileProcessing(void *pInBuf, int iBufSize, char *pFileName, int *piNewSize)
{
	if (strEndsWith(pFileName, ".quickplay"))
	{
		char *pString = NULL;
		char *pNewBuf;

		estrStackCreate(&pString);

		estrSetSize(&pString, iBufSize);
		memcpy(pString, pInBuf, iBufSize);

		estrReplaceOccurrences(&pString, "$MACHINENAME$", getHostName());

		pNewBuf = malloc(estrLength(&pString));
		memcpy(pNewBuf, pString, estrLength(&pString));

		free(pInBuf);
		*piNewSize = estrLength(&pString);
		estrDestroy(&pString);

		return pNewBuf;
	}
	else
	{
		*piNewSize = iBufSize;
		return pInBuf;
	}
}


void HandleHereAreLastMinuteFiles(Packet *pPak)
{
	FILE *pOutFile;
	AllLastMinuteFilesInfo allFilesInfo = {0};
	int i;


	if (!gbWaitingForLastMinuteFiles)
	{
		return;
	}

	ParserRecvStructSafe(parse_AllLastMinuteFilesInfo, pPak, &allFilesInfo);


	for (i=0; i < eaSize(&allFilesInfo.ppFiles); i++)
	{
		char *pBuf, *pBufProcessed;
		int iBufSize;
		int iProcessedSize;
		char fullFileName[CRYPTIC_MAX_PATH];
		LastMinuteFileInfo *pFileInfo = allFilesInfo.ppFiles[i];

		sprintf(fullFileName, "c:\\%s_mcppatched\\%sClient\\data\\%s",
			GetProductName(), GetProductName(), pFileInfo->pFileName);
		mkdirtree(fullFileName);

		pBuf = TextParserBinaryBlock_PutIntoMallocedBuffer(pFileInfo->pFileData, &iBufSize);
		pBufProcessed = DoSpecialLastMinuteFileProcessing(pBuf, iBufSize, pFileInfo->pFileName, &iProcessedSize);

		pOutFile = fopen(fullFileName, "wb");

		assertmsgf(pOutFile, "Couldn't open %s for writing", fullFileName);

		fwrite(pBufProcessed, iProcessedSize, 1, pOutFile);

		fclose(pOutFile);

		free(pBufProcessed);

		if (gbCopyLastMinuteFilesToXBox)
		{
			char systemString[1024];
			char programFilesFolder[MAX_PATH];

			GetEnvironmentVariable("ProgramFiles", SAFESTR(programFilesFolder));

			sprintf(systemString, "\"%s\\microsoft xbox 360 sdk\\bin\\win32\\xbcp\" -Y -T %s xe:\\%s\\data\\%s", programFilesFolder, fullFileName, GetProductName(), pFileInfo->pFileName);
			backSlashes(systemString);
			system(systemString);
		}
	}


	StructDeInit(parse_AllLastMinuteFilesInfo, &allFilesInfo);
	gbGotLastMinuteFiles = true;
}

//production shards don't have last minute files
bool ShouldTryToGetLastMinuteFiles(ShardInfo_Basic *pShard)
{
	if (stricmp(pShard->pShardCategoryName, "production") == 0)
	{
		return false;
	}

	return true;
}



bool TryToGetLastMinuteFiles(int iShardID, bool bGetForXbox)
{
	Packet *pPak;
	U32 iStartTime = timeSecondsSince2000();
	char systemString[1024];

//erase all LastMinuteFiles from previous runs.
	sprintf(systemString, "erase c:\\%s_mcppatched\\%sClient\\data\\*.* /F /S /Q", GetProductName(), GetProductName());
	system(systemString);

	if (bGetForXbox)
	{
		char programFilesFolder[MAX_PATH];

		GetEnvironmentVariable("ProgramFiles", SAFESTR(programFilesFolder));

		sprintf(systemString, "\"%s\\microsoft xbox 360 sdk\\bin\\win32\\xbdel\" -R -S -F xe:\\%s\\data\\*.*", programFilesFolder, GetProductName());
		system(systemString);
	}


	gbGotLastMinuteFiles = false;
	gbWaitingForLastMinuteFiles = true;
	gbCopyLastMinuteFilesToXBox = bGetForXbox;


	if (!(gpLinkToNewControllerTracker  && linkConnected(gpLinkToNewControllerTracker) && !linkDisconnected(gpLinkToNewControllerTracker)))
	{
		gbWaitingForLastMinuteFiles = false;
		Errorf("No longer connected to controller tracker... couldn't get last minute files");
		return false;
	}

	pPak = pktCreate(gpLinkToNewControllerTracker, FROM_MCP_TO_NEWCONTROLLERTRACKER_REQUEST_LAST_MINUTE_FILES);
	pktSendBits(pPak, 32, iShardID);
	pktSend(&pPak);

	while (!gbGotLastMinuteFiles)
	{
		commMonitor(commDefault());

		Sleep(0.1f);

		if (timeSecondsSince2000() > iStartTime + 20)
		{
			Errorf("Timeout while waiting for last minute files from controller tracker");
			gbWaitingForLastMinuteFiles = false;
			return false;
		}
	}

	gbWaitingForLastMinuteFiles = false;
	return true;
}



#include "autogen/NewControllerTracker_pub_h_ast.c"




void MCPNewControllerTrackerCallback(Packet *pak,int cmd, NetLink *link, void *user_data)
{
	int i;
	LRESULT lResult;

	switch(cmd)
	{
	case FROM_NEWCONTROLLERTRACKER_TO_MCP_HERE_IS_SHARD_LIST:
		{
			SimpleWindow *pStartingWindow = SimpleWindowManager_FindWindow(MCPWINDOW_STARTING, 0);

			StructDeInit(parse_ShardInfo_Basic_List, &gAvailableShardList);

			ParserRecvStructSafe(parse_ShardInfo_Basic_List, pak, &gAvailableShardList);


			if (pStartingWindow)
			{
				SendMessage(GetDlgItem(pStartingWindow->hWnd, IDC_CHOOSESERVER), CB_RESETCONTENT, 0, 0);

				for (i=eaSize(&gAvailableShardList.ppShards)-1 ; i >= 0; i--)
				{
					if (!gAvailableShardList.ppShards[i]->pShardName || !gAvailableShardList.ppShards[i]->pShardCategoryName)
					{
						StructDestroy(parse_ShardInfo_Basic, gAvailableShardList.ppShards[i]);
						eaRemove(&gAvailableShardList.ppShards, i);
					}
				}

				for (i=0; i < eaSize(&gAvailableShardList.ppShards); i++)
				{
					lResult = SendMessage(GetDlgItem(pStartingWindow->hWnd, IDC_CHOOSESERVER), CB_ADDSTRING, 0, (LPARAM)gAvailableShardList.ppShards[i]->pShardName);
					SendMessage(GetDlgItem(pStartingWindow->hWnd, IDC_CHOOSESERVER), CB_SETITEMDATA, (WPARAM)lResult, (LPARAM)i);
				}

				if (gAvailableShardList.ppShards)
				{	
					SendMessage(GetDlgItem(pStartingWindow->hWnd, IDC_CHOOSESERVER), CB_SELECTSTRING, 0, (LPARAM)gAvailableShardList.ppShards[0]->pShardName);
				}

			}
			gbAvailableShardsChanged = true;
		}
		break;

	xcase FROM_NEWCONTROLLERTRACKER_TO_MCP_HERE_ARE_LAST_MINUTE_FILES:
		HandleHereAreLastMinuteFiles(pak);

	xdefault:
		printf("Unknown command %d\n",cmd);
	}
}

#define CONTROLLERTRACKER_ATTEMPT_TIMEOUT 1

char **ppUniqueControllerTrackerIPs = NULL;
int iCurControllerTrackerIndex = 0;
extern bool gbQaMode;

//returns true if connected
bool UpdateControllerTrackerConnection(ControllerTrackerConnectionStatusStruct *pStatus, char **ppResultEString)
{
	U32 iCurTime;

	char *pControllerTrackerHost;

	if (gpLinkToNewControllerTracker && linkConnected(gpLinkToNewControllerTracker) && !linkDisconnected(gpLinkToNewControllerTracker))
	{
		if (pStatus->iOverallBeginTime != 0)
		{
			Packet *pak;
			char *prodName = GetProductName();

			if(!stricmp(prodName, "CrypticLauncher"))
				prodName = "all";

			pak = pktCreate(gpLinkToNewControllerTracker, FROM_MCP_TO_NEWCONTROLLERTRACKER_REQUEST_SHARD_LIST);
			pktSendString(pak, prodName);
			if (gpTicket)
				pktSendString(pak, gpTicket);
			else
			{
				pktSendString(pak, ACCOUNT_FASTLOGIN_LABEL);
				pktSendU32(pak, guAccountID);
				pktSendU32(pak, guTicketID);
			}
			pktSend(&pak);
		}

		pStatus->iOverallBeginTime = 0;
		return true;
	}

#if STANDALONE
	pControllerTrackerHost = "controllertracker." ORGANIZATION_DOMAIN;
#else
	if(gbQaMode)
		pControllerTrackerHost = GetQAControllerTrackerHost();
	else
		pControllerTrackerHost = GetControllerTrackerHost();
#endif

	if (!eaSize(&ppUniqueControllerTrackerIPs))
	{
		if(gbQaMode)
			eaPush(&ppUniqueControllerTrackerIPs, strdup(pControllerTrackerHost));
		else
			GetAllUniqueIPs(pControllerTrackerHost, &ppUniqueControllerTrackerIPs);
		if (!eaSize(&ppUniqueControllerTrackerIPs))
		{
			estrPrintf(ppResultEString, "DNS can't resolve %s", pControllerTrackerHost);
			return false;
		}

		iCurControllerTrackerIndex = timeSecondsSince2000() % eaSize(&ppUniqueControllerTrackerIPs);
	}
	
	iCurTime = timeSecondsSince2000();

	if (pStatus->iOverallBeginTime == 0)
	{
		gpLinkToNewControllerTracker = commConnect(commDefault(),LINKTYPE_UNSPEC, LINK_FORCE_FLUSH,ppUniqueControllerTrackerIPs[iCurControllerTrackerIndex],NEWCONTROLLERTRACKER_GENERAL_MCP_PORT,MCPNewControllerTrackerCallback,0,0,0);
		pStatus->iCurBeginTime = pStatus->iOverallBeginTime = iCurTime;
	}

	if (iCurTime - pStatus->iCurBeginTime > CONTROLLERTRACKER_ATTEMPT_TIMEOUT)
	{
		linkRemove(&gpLinkToNewControllerTracker);
		iCurControllerTrackerIndex += 1;
		iCurControllerTrackerIndex %= eaSize(&ppUniqueControllerTrackerIPs);

		gpLinkToNewControllerTracker = commConnect(commDefault(),LINKTYPE_UNSPEC, LINK_FORCE_FLUSH,ppUniqueControllerTrackerIPs[iCurControllerTrackerIndex],NEWCONTROLLERTRACKER_GENERAL_MCP_PORT,MCPNewControllerTrackerCallback,0,0,0);
		pStatus->iCurBeginTime = iCurTime;
	}

	estrPrintf(ppResultEString, "Attempting to connect to controller tracker %s", ppUniqueControllerTrackerIPs[iCurControllerTrackerIndex]);
	return false;
}	




		

/*
void AttemptToConnectToControllerTracker(bool bForceRetry, bool bDontWait)
{
	static bool bFailedFirstTime = false;
	static bool bIsFirstTime = true;
	char *pControllerTrackerHost;

	if (linkConnected(gpLinkToNewControllerTracker))
	{
		return;
	}

	if (bFailedFirstTime && !bForceRetry)
	{
		return;
	}

	if (gpLinkToNewControllerTracker)
		linkRemove(&gpLinkToNewControllerTracker);

#if STANDALONE
	pControllerTrackerHost = "controllertracker." ORGANIZATION_DOMAIN;
#else
	pControllerTrackerHost = gServerLibLoadedConfig.newControllerTrackerHost;
#endif

	gpLinkToNewControllerTracker = commConnect(commDefault(),LINK_FORCE_FLUSH,pControllerTrackerHost,NEWCONTROLLERTRACKER_GENERAL_MCP_PORT,MCPNewControllerTrackerCallback,0,0,0);
	
	if (!linkConnectWait(&gpLinkToNewControllerTracker,1.f))
	{
		if (bIsFirstTime)
		{
			bFailedFirstTime = true;
		}
	}
	else
	{
		Packet *pak;

		pak = pktCreate(gpLinkToNewControllerTracker, FROM_MCP_TO_NEWCONTROLLERTRACKER_REQUEST_SHARD_LIST);
		pktSendString(pak, GetProductName());
		pktSendString(pak, gpTicket);
		pktSend(&pak);
	}

	bIsFirstTime = false;
}*/
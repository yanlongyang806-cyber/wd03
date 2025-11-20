#include "SentryServerComm.h"
#include "net.h"
#include "Textparser.h"
#include "estring.h"
#include "StashTable.h"
#include "SentryServerComm_c_Ast.h"
#include "error.h"
#include "alerts.h"
#include "../../utilities/sentryserver/sentryPub.h"
#include "../../utilities/sentryserver/sentry_comm.h"
#include "file.h"
#include "logging.h"
#include "sentrypub_h_ast.h"
#include "structNet.h"
#include "TimedCallback.h"
#include "rand.h"
#include "zutils.h"
#include "SentryServerComm_h_Ast.h"
#include "winUtil.h"
#include "TimedCallback.h"
#include "StringCache.h"
#include "crypt.h"
#include "CrypticPorts.h"


static void SentryServerComm_DeferredFileSending_Tick(void);

#define GETFILECRC_TIMEOUT (15.0f)
#define GETDIRECTORYCONTENTS_TIMEOUT (45.0f)

SentryClientList gSentryClientList = {0};

static bool sbUseOldSentryServerComm = true;
AUTO_CMD_INT(sbUseOldSentryServerComm, UseOldSentryServerComm);

static bool sbConnectionRequested = false;

static bool sbDeferredSends = false;

static StashTable sDeferredPacketsByPacketAddress = NULL;

static CommConnectFSM *spSentryServerConnectFSM = NULL;
static NetLink *spSentryServerNetLink = NULL;

//when trying to send a packet to the sentry server, alert if it takes more than this many seconds to send
static int siSecsBeforeAlertWhenSentryServerPacketSendsFail = 15;
AUTO_CMD_INT(siSecsBeforeAlertWhenSentryServerPacketSendsFail, SecsBeforeAlertWhenSentryServerPacketSendsFail) ACMD_CONTROLLER_AUTO_SETTING(Misc);

static S32 siSessionKey;
static U64 siNextPacketIndex = 1;
static U64 siHighestVerifiedIndex = 0;

static void handleHereIsFileCRC(Packet *pPak);
static void handleHereAreDirectoryContents(Packet *pPak);
static void handleRunningProcessesResponse(Packet *pPak);
static void handleSimpleMachinesResponse(Packet *pak);
static void handleGetFileContentsResponse(Packet *pak);

//if we send a packet and don't get a verify, re-send it every n seconds
static int siTimeBeforeResendWithNoVerify = 5;
AUTO_CMD_INT(siTimeBeforeResendWithNoVerify, TimeBeforeResendWithNoVerify);

char gSentryServerName[100] = "SentryServer";
AUTO_CMD_STRING(gSentryServerName, SentryServer) ACMD_CMDLINE;

static NetComm *spNetComm;

static CRITICAL_SECTION sSentryServerCommCritSec;

AUTO_RUN_EARLY;
void SentryServerComm_Init(void)
{
	InitializeCriticalSection(&sSentryServerCommCritSec);
}



void SentryServerComm_SetNetComm(NetComm *pComm)
{
	spNetComm = pComm;
}


static SentryServerQueryCB spQueryCB;

void SentryServerComm_SetQueryCB(SentryServerQueryCB pCB)
{
	spQueryCB = pCB;
}

AUTO_STRUCT;
typedef struct DeferredPacketForSentryServer
{
	Packet *pTempPacket; NO_AST
	int iTempPacketIdxAfterCreation; 
	int iTempPacketCmd; 
	char *pComment; AST(ESTRING)
	U32 iSendTime;
	U32 iTimeSentToServer;
	bool bAlerted;
	U64 iIndex;
} DeferredPacketForSentryServer;


#define TRACE(fmt, ...) { log_printf(LOG_CONTROLLER, fmt, __VA_ARGS__); /* printf(fmt, __VA_ARGS__); */}

AUTO_FIXUPFUNC;
TextParserResult DeferredPacketForSentryServer_fixup(DeferredPacketForSentryServer *pDeferredPacket, enumTextParserFixupType eFixupType, void *pExtraData)
{
	switch (eFixupType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		{
			pktFree(&pDeferredPacket->pTempPacket);
		}
	}

	return 1;
}

DeferredPacketForSentryServer **ppPacketsReadyToSend = NULL;
DeferredPacketForSentryServer **ppPacketsWaitingForVerification = NULL;

Packet *SentryServerComm_CreatePacket(int iCmd, FORMAT_STR const char *pCommentFmt, ...)
{
	DeferredPacketForSentryServer *pDeferredPacket = StructCreate(parse_DeferredPacketForSentryServer);
	estrGetVarArgs(&pDeferredPacket->pComment, pCommentFmt);
	pDeferredPacket->pTempPacket = pktCreateTemp(NULL);

	//somewhat kludgy... set this up to manage the default settings for comm_controller
	pktSetHasVerify(pDeferredPacket->pTempPacket, true);
	

	pDeferredPacket->iTempPacketCmd = iCmd;
	pDeferredPacket->iTempPacketIdxAfterCreation = pktGetIndex(pDeferredPacket->pTempPacket);


	EnterCriticalSection(&sSentryServerCommCritSec);
	pDeferredPacket->iIndex = siNextPacketIndex++;
	if (!sDeferredPacketsByPacketAddress)
	{
		sDeferredPacketsByPacketAddress = stashTableCreateAddress(32);
	}
	stashAddPointer(sDeferredPacketsByPacketAddress, pDeferredPacket->pTempPacket, pDeferredPacket, true);
	LeaveCriticalSection(&sSentryServerCommCritSec);

	return pDeferredPacket->pTempPacket;
}
void SentryServerComm_SendPacket(Packet **ppPacket)
{
	DeferredPacketForSentryServer *pDeferredPacket;
	if (!ppPacket || !(*ppPacket))
	{
		return;
	}

	sbConnectionRequested = true;

	EnterCriticalSection(&sSentryServerCommCritSec);
	if (!stashRemovePointer(sDeferredPacketsByPacketAddress, *ppPacket, &pDeferredPacket))
	{
		AssertOrAlert("CONTROLLERSENTRYSERVERPACKET_CORRUPTION", "Someone trying to send an unknown CSS packet");
		LeaveCriticalSection(&sSentryServerCommCritSec);
		return;
	}

	eaPush(&ppPacketsReadyToSend, pDeferredPacket);
	pDeferredPacket->iSendTime = timeServerSecondsSince2000();
	*ppPacket = NULL;
	LeaveCriticalSection(&sSentryServerCommCritSec);
}

// Run this when we successfully connect to a SentryServer.
static void SentryServerConnectCB(NetLink* link,void *user_data)
{
	// The link to the SentryServer has a keep-alive so that it will not be timed out by a stateful firewall, since the
	// Sentry Server may be on a different network.
	linkSetKeepAliveSeconds(link, 15);
}

static void handlePacketVerified(Packet *pak)
{
	U64 iIndex = pktGetBits64(pak, 64);
	int i;

	TRACE("Sentry server has verified receipt of packet %"FORM_LL"d\n", iIndex);

	if (iIndex > siHighestVerifiedIndex)
	{
		siHighestVerifiedIndex = iIndex;
	}

	EnterCriticalSection(&sSentryServerCommCritSec);
	for (i = eaSize(&ppPacketsWaitingForVerification) - 1; i >= 0; i--)
	{
		if (ppPacketsWaitingForVerification[i]->iIndex <= iIndex)
		{
			StructDestroy(parse_DeferredPacketForSentryServer, ppPacketsWaitingForVerification[i]);
			eaRemove(&ppPacketsWaitingForVerification, i);
		}
	}
	LeaveCriticalSection(&sSentryServerCommCritSec);
}

static void handleRequestingResends(NetLink *link, Packet *pak)
{
	int i;

	EnterCriticalSection(&sSentryServerCommCritSec);
	while (1)
	{
		U64 iIndex = pktGetBits64(pak, 64);
		if (!iIndex)
		{
			LeaveCriticalSection(&sSentryServerCommCritSec);
			return;
		}

		for (i = 0; i < eaSize(&ppPacketsWaitingForVerification); i++)
		{
			if (ppPacketsWaitingForVerification[i]->iIndex == iIndex)
			{

				Packet *pOutPacket = pktCreate(spSentryServerNetLink, MONITORCLIENT_PACKET_W_VERIFICATION_INFO);
				DeferredPacketForSentryServer *pDeferredPacket = ppPacketsWaitingForVerification[i];

				pktSendBits(pOutPacket, 32, siSessionKey);
				pktSendBits64(pOutPacket, 64, pDeferredPacket->iIndex);
				pktSendBits64(pOutPacket, 64, siHighestVerifiedIndex);
				pktSendBits(pOutPacket, 32, pDeferredPacket->iTempPacketCmd);
				pktSetIndex(pDeferredPacket->pTempPacket, pDeferredPacket->iTempPacketIdxAfterCreation);
				pktAppend(pOutPacket, pDeferredPacket->pTempPacket, -1);
	
				pktSend(&pOutPacket);
				pDeferredPacket->iTimeSentToServer = timeSecondsSince2000_ForceRecalc();

				TRACE("Was asked for resend of packet %s, with ID %"FORM_LL"d, to sentry server\n", pDeferredPacket->pComment, pDeferredPacket->iIndex);

			}
			break;
		}
	}
}

static void handleQuery(NetLink *pLink, Packet *pak)
{
	char **ppStrings = NULL;
	SentryServerCommQueryReturn *pReturn = NULL;
	char **ppSubStrings = NULL;
	SentryServerCommQueryProcInfo *pCurProc = NULL;
	SentryServerCommMachineInfo *pCurMachine = NULL;

	while(!pktEnd(pak))
	{
		char *pValStr = pktGetStringTemp(pak);
		F64 fVal;
		if (!pValStr[0])
		{
			eaPush(&ppStrings, "");
			continue;
		}
		fVal = pktGetF64(pak);

		eaPush(&ppStrings, pValStr);

	}

	pReturn = StructCreate(parse_SentryServerCommQueryReturn);

	while (eaSize(&ppStrings))
	{

		char *pCurString = eaRemove(&ppStrings, 0);
		if (strStartsWith(pCurString, "Machine"))
		{
			eaClearEx(&ppSubStrings, NULL);
			DivideString(pCurString, " ", &ppSubStrings, DIVIDESTRING_RESPECT_SIMPLE_QUOTES);
			if (eaSize(&ppSubStrings) > 1)
			{
				pCurMachine = StructCreate(parse_SentryServerCommMachineInfo);
				pCurMachine->pMachineName = strdup(ppSubStrings[1]);
				eaPush(&pReturn->ppMachines, pCurMachine);
			}

		}
		else if (strStartsWith(pCurString, "Process_name"))
		{
			eaClearEx(&ppSubStrings, NULL);
	
			pCurProc = NULL;
			DivideString(pCurString, " ", &ppSubStrings, DIVIDESTRING_RESPECT_SIMPLE_QUOTES);
			if (eaSize(&ppSubStrings) > 1)
			{
				if (stricmp(ppSubStrings[1], "UNKNOWN") != 0)
				{
					if (pCurMachine)
					{
						pCurProc = StructCreate(parse_SentryServerCommQueryProcInfo);
						pCurProc->pExeName = strdup(ppSubStrings[1]);
						eaPush(&pCurMachine->ppProcs, pCurProc);
					}
				}
			}


		}
		else if (strStartsWith(pCurString, "Process_Path") && pCurProc)
		{
			eaClearEx(&ppSubStrings, NULL);
			DivideString(pCurString, " ", &ppSubStrings, DIVIDESTRING_RESPECT_SIMPLE_QUOTES);
			if (eaSize(&ppSubStrings) > 1)
			{
				pCurProc->pPath = strdup(ppSubStrings[1]);			
			}


		}
		else if (strStartsWith(pCurString, "Process_PID") && pCurProc)
		{
			eaClearEx(&ppSubStrings, NULL);
			DivideString(pCurString, " ", &ppSubStrings, DIVIDESTRING_RESPECT_SIMPLE_QUOTES);
			if (eaSize(&ppSubStrings) > 2)
			{
				pCurProc->iPID = atoi(ppSubStrings[2]);			
			}


		}


	}
	if (spQueryCB)
	{
		spQueryCB(pReturn);
	}
	StructDestroy(parse_SentryServerCommQueryReturn, pReturn);
	eaDestroy(&ppStrings);
	eaDestroyEx(&ppSubStrings, NULL);
}



static void SentryServerMessageCB(Packet *pak,int cmd, NetLink *link, void *pUserData)
{
	switch (cmd)
	{
	xcase MONITORSERVER_EXPRESSIONQUERY_RESULT:
		StructDeInit(parse_SentryClientList, &gSentryClientList);
		ParserRecv(parse_SentryClientList, pak, &gSentryClientList, 0);
	xcase MONITORSERVER_EXPRESSIONQUERY_RESULT_SAFE:
		StructDeInit(parse_SentryClientList, &gSentryClientList);
		ParserRecvStructSafe(parse_SentryClientList, pak, &gSentryClientList);

	xcase MONITORSERVER_PACKET_VERIFIED:
		handlePacketVerified(pak);

	xcase MONITORSERVER_REQUESTING_RESENDS:
		handleRequestingResends(link, pak);

	xcase MONITORSERVER_QUERY:
		handleQuery(link, pak);

	xcase MONITORSERVER_HEREISFILECRC:
		handleHereIsFileCRC(pak);

	xcase MONITORSERVER_HEREAREDIRECTORYCONTENTS:
		handleHereAreDirectoryContents(pak);

	xcase MONITORSERVER_SIMPLEQUERY_PROCESSES_ON_ONE_MACHINE_RESPONSE:
		handleRunningProcessesResponse(pak);
	
	xcase MONITORSERVER_SIMPLEQUERY_MACHINES_RESPONSE:
		handleSimpleMachinesResponse(pak);

	xcase MONITORSERVER_GETFILECONTENTS_RESPONSE:
		handleGetFileContentsResponse(pak);
	}
}


void SentryServerComm_Tick(void)
{
	U32 iCurTime;
	static U32 threadID = 0;

	if (!sbConnectionRequested)
	{
		return;
	}

	ONCE(threadID = GetCurrentThreadId());
	assert(threadID == GetCurrentThreadId());

	iCurTime = timeSecondsSince2000_ForceRecalc();


	if (commConnectFSMForTickFunctionWithRetrying(&spSentryServerConnectFSM, &spSentryServerNetLink, 
		"link to Sentry Server",
			2.0f, spNetComm ? spNetComm : commDefault(), LINKTYPE_SHARD_CRITICAL_100MEG, LINK_FORCE_FLUSH,
			gSentryServerName,SENTRYSERVERMONITOR_PORT,SentryServerMessageCB,SentryServerConnectCB,0,0, NULL, 0, NULL, 0))
	{
		if (!siSessionKey)
		{
			siSessionKey = timeSecondsSince2000();
		}

		EnterCriticalSection(&sSentryServerCommCritSec);
		if (!sbUseOldSentryServerComm)
		{
			FOR_EACH_IN_EARRAY_FORWARDS(ppPacketsWaitingForVerification, DeferredPacketForSentryServer, pDeferredPacket)
			{
				if (pDeferredPacket->iTimeSentToServer < iCurTime - siTimeBeforeResendWithNoVerify)
				{
					Packet *pOutPacket = pktCreate(spSentryServerNetLink, MONITORCLIENT_PACKET_W_VERIFICATION_INFO);

					pktSendBits(pOutPacket, 32, siSessionKey);
					pktSendBits64(pOutPacket, 64, pDeferredPacket->iIndex);
					pktSendBits64(pOutPacket, 64, siHighestVerifiedIndex);
					pktSendBits(pOutPacket, 32, pDeferredPacket->iTempPacketCmd);
					pktSetIndex(pDeferredPacket->pTempPacket, pDeferredPacket->iTempPacketIdxAfterCreation);
					pktAppend(pOutPacket, pDeferredPacket->pTempPacket, -1);
	
					pktSend(&pOutPacket);
					pDeferredPacket->iTimeSentToServer = iCurTime;

					TRACE("Due to lack of verification, re-sent packet %s, with ID %"FORM_LL"d, to sentry server\n", pDeferredPacket->pComment, pDeferredPacket->iIndex);
				}
			}
			FOR_EACH_END;
		}

		FOR_EACH_IN_EARRAY_FORWARDS(ppPacketsReadyToSend, DeferredPacketForSentryServer, pDeferredPacket)
		{
			if (sbUseOldSentryServerComm)
			{

				Packet *pOutPacket = pktCreate(spSentryServerNetLink, pDeferredPacket->iTempPacketCmd);
	
				pktSetIndex(pDeferredPacket->pTempPacket, pDeferredPacket->iTempPacketIdxAfterCreation);
				pktAppend(pOutPacket, pDeferredPacket->pTempPacket, -1);
			
				pktSend(&pOutPacket);
				pktFree(&pDeferredPacket->pTempPacket);

				TRACE("Just sent packet %s to sentry server\n", pDeferredPacket->pComment);
			}
			else
			{
				Packet *pOutPacket = pktCreate(spSentryServerNetLink, MONITORCLIENT_PACKET_W_VERIFICATION_INFO);

				pktSendBits(pOutPacket, 32, siSessionKey);
				pktSendBits64(pOutPacket, 64, pDeferredPacket->iIndex);
				pktSendBits64(pOutPacket, 64, siHighestVerifiedIndex);
				pktSendBits(pOutPacket, 32, pDeferredPacket->iTempPacketCmd);
				pktSetIndex(pDeferredPacket->pTempPacket, pDeferredPacket->iTempPacketIdxAfterCreation);
				pktAppend(pOutPacket, pDeferredPacket->pTempPacket, -1);
	
				pktSend(&pOutPacket);

				pDeferredPacket->iTimeSentToServer = iCurTime;

				TRACE("Just sent packet %s, with ID %"FORM_LL"d, to sentry server\n", pDeferredPacket->pComment, pDeferredPacket->iIndex);
			}
		}
		FOR_EACH_END;

		if (sbUseOldSentryServerComm)
		{
			eaDestroyStruct(&ppPacketsReadyToSend, parse_DeferredPacketForSentryServer);
		}
		else
		{
			eaPushEArray(&ppPacketsWaitingForVerification, &ppPacketsReadyToSend);
			eaDestroy(&ppPacketsReadyToSend);
		}
		LeaveCriticalSection(&sSentryServerCommCritSec);
	}
	else
	{
		static U32 siLastTimeCheckedForLag = 0;
		
		EnterCriticalSection(&sSentryServerCommCritSec);

		if (siLastTimeCheckedForLag != iCurTime)
		{
			siLastTimeCheckedForLag = iCurTime;

			FOR_EACH_IN_EARRAY(ppPacketsReadyToSend, DeferredPacketForSentryServer, pDeferredPacket)
			{
				if (!pDeferredPacket->bAlerted && pDeferredPacket->iSendTime < iCurTime - siSecsBeforeAlertWhenSentryServerPacketSendsFail)
				{
					pDeferredPacket->bAlerted = true;
					CRITICAL_NETOPS_ALERT("SENTRYSERVER_PKT_FAIL", "Taking more than %d seconds to send a packet to sentry server %s for: %s (will keep trying forever)",
						siSecsBeforeAlertWhenSentryServerPacketSendsFail, gSentryServerName, pDeferredPacket->pComment);
				}
			}
			FOR_EACH_END;
		}

		LeaveCriticalSection(&sSentryServerCommCritSec);
	}

	if (sbDeferredSends)
	{
		SentryServerComm_DeferredFileSending_Tick();
	}
}

static F32 randomFloatInRange(F32 min, F32 max)
{
	float mag = max - min;
	float randFloat = min + (randomPositiveF32() * mag);

	return randFloat;
}

void SentryServerPrintfTestCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	static int iIndex = 1;
	char temp[256];
	Packet *pPack = SentryServerComm_CreatePacket(MONITORCLIENT_DEBUG_PRINT, "printf test pack %d", iIndex);
	sprintf(temp, "Packet #%d", iIndex);
	iIndex++;
	pktSendString(pPack, temp);
	SentryServerComm_SendPacket(&pPack);

	TimedCallback_Run(SentryServerPrintfTestCB, NULL, randomFloatInRange(0.01f, 5.0f));
}


AUTO_COMMAND;
void BeginSentryServerPrintfTest(void)
{
	SentryServerPrintfTestCB(NULL, 0, NULL);
}



void SentryServerComm_KillProcesses(char **ppMachineNames, char *pProcessName, char *pDirToRestrictTo)
{
	char **ppProcNames = NULL;
	DivideString(pProcessName, " ,", &ppProcNames, DIVIDESTRING_STANDARD);

	if (!eaSize(&ppProcNames))
	{
		eaDestroyEx(&ppProcNames, NULL);
		return;
	}

	FOR_EACH_IN_EARRAY(ppMachineNames, char, pMachine)
	{
		Packet *pPak;
		char *pFullCommandLine = NULL;
		int i;

		estrPrintf(&pFullCommandLine, "cryptickillall ");

		for (i = 0; i < eaSize(&ppProcNames); i++)
		{
			estrConcatf(&pFullCommandLine, "-kill %s ", ppProcNames[i]);
		}

		if (pDirToRestrictTo && pDirToRestrictTo[0])
		{
			estrConcatf(&pFullCommandLine, " -restrictToDir %s", pDirToRestrictTo);
		}

		pPak = SentryServerComm_CreatePacket(MONITORCLIENT_LAUNCH_AND_WAIT,
			"Deferred kills on %s", pMachine);
		pktSendString(pPak, pMachine);
		pktSendString(pPak, pFullCommandLine);
		SentryServerComm_SendPacket(&pPak);
		estrDestroy(&pFullCommandLine);
	}
	FOR_EACH_END;

	eaDestroyEx(&ppProcNames, NULL);

}

bool SentryServerComm_SendFile(char **ppMachineNames, char *pLocalFileName, char *pRemoteFileName)
{
	int iInFileSize;
	char *pCompressedBuffer = NULL;
	int iCompressedSize;
	char *pInBuffer = fileAlloc(pLocalFileName, &iInFileSize);
	Packet *pPak;
	if (!pInBuffer)
	{
		return false;
	}
	pCompressedBuffer = zipData(pInBuffer, iInFileSize, &iCompressedSize);
	
	FOR_EACH_IN_EARRAY(ppMachineNames, char, pMachineName)
	{
		pPak = SentryServerComm_CreatePacket(MONITORCLIENT_CREATEFILE, "Sending file %s to %s", pLocalFileName, pMachineName);	
		pktSendString(pPak, pMachineName);
		pktSendString(pPak, pRemoteFileName);
		pktSendBits(pPak, 32, iCompressedSize);
		pktSendBits(pPak, 32, iInFileSize);
		pktSendBytes(pPak, iCompressedSize, pCompressedBuffer);

		SentryServerComm_SendPacket(&pPak);
	}
	FOR_EACH_END;

	SAFE_FREE(pInBuffer);
	SAFE_FREE(pCompressedBuffer);

	return true;
}

void SentryServerComm_RunCommand(const char **ppMachineNames, char *pCommand)
{
	Packet *pPak;

	FOR_EACH_IN_EARRAY((char**)ppMachineNames, char, pMachineName)
	{
		pPak = SentryServerComm_CreatePacket(MONITORCLIENT_LAUNCH, "Executing command on %s",
			pMachineName);
		pktSendString(pPak, pMachineName);
		pktSendString(pPak, pCommand);
		SentryServerComm_SendPacket(&pPak);	
	}
	FOR_EACH_END;
}

bool IsActuallyLocalMachine(const char *pMachineName)
{
	return stricmp(getHostName(), pMachineName) == 0;
}

/*

typedef struct WrappedFileBuffer 
{
	char *pFileName;
	int iOrigSize;
	int iZippedSize;
	char *pZippedBuf;
} WrappedFileBuffer;

StashTable sWrappedFilesByName = NULL;

static WrappedFileBuffer *FindOrCreateWrappedBuffer(char *pLocalFileName)
{
	WrappedFileBuffer *pBuffer;
	WrappedFileBuffer *pOtherBuffer;
	char *pUncompressedBuf;

	EnterCriticalSection(&sSentryServerCommCritSec);
	if (!sWrappedFilesByName)
	{
		sWrappedFilesByName = stashTableCreateWithStringKeys(16, StashDefault);
	}

	if (stashFindPointer(sWrappedFilesByName, pLocalFileName, &pBuffer))
	{
		LeaveCriticalSection(&sSentryServerCommCritSec);
		return pBuffer;
	}
	LeaveCriticalSection(&sSentryServerCommCritSec);

	if (!fileExists(pLocalFileName))
	{
		return NULL;
	}

	pBuffer = calloc(sizeof(WrappedFileBuffer), 1);
	pUncompressedBuf = fileAlloc(pLocalFileName, &pBuffer->iOrigSize);

	if (!pUncompressedBuf)
	{
		free(pBuffer);
		return NULL;
	}

	pBuffer->pFileName = strdup(pLocalFileName);
	pBuffer->pZippedBuf = zipData(pUncompressedBuf, pBuffer->iOrigSize, &pBuffer->iZippedSize);

	free(pUncompressedBuf);

	EnterCriticalSection(&sSentryServerCommCritSec);
	if (stashFindPointer(sWrappedFilesByName, pLocalFileName, &pOtherBuffer))
	{
		LeaveCriticalSection(&sSentryServerCommCritSec);
		free(pBuffer->pFileName);
		free(pBuffer->pZippedBuf);
		free(pBuffer);
		return pOtherBuffer;
	}
	else
	{
		stashAddPointer(sWrappedFilesByName, pBuffer->pFileName, pBuffer, true);
		LeaveCriticalSection(&sSentryServerCommCritSec);
	}

	return pBuffer;
}



bool SentryServerComm_SendFileWrapped(char *pMachineName, char *pLocalFileName, char *pRemoteFileName)
{
	WrappedFileBuffer *pWrappedBuffer;
	Packet *pPak;

	if (IsActuallyLocalMachine(pMachineName))
	{
		char systemString[1024];
		printf("SentryServerComm asked to send %s to %s... but that appears to actually be this machine, so we'll just copy the files\n",
			pLocalFileName, pMachineName);
		if (stricmp(pLocalFileName, pRemoteFileName) == 0)
		{
			printf("And in fact the file seems to already be in the right place, doing nothing\n");
			return true;
		}

		mkdirtree_const(pRemoteFileName);
		sprintf(systemString, "copy %s %s", pLocalFileName, pRemoteFileName);
		backSlashes(systemString);
		if (system(systemString))
		{
			printf("Copying failed\n");
			return false;
		}

		return true;
	}

	pWrappedBuffer = FindOrCreateWrappedBuffer(pLocalFileName);

	if (!pWrappedBuffer)
	{
		printf("Couldn't find local file %s\n", pLocalFileName);
		return false;
	}

	pPak = SentryServerComm_CreatePacket(MONITORCLIENT_CREATEFILE, "Sending file %s to %s", pRemoteFileName, pMachineName);	
	pktSendString(pPak, pMachineName);
	pktSendString(pPak, pRemoteFileName);
	pktSendBits(pPak, 32, pWrappedBuffer->iZippedSize);
	pktSendBits(pPak, 32, pWrappedBuffer->iOrigSize);
	pktSendBytes(pPak, pWrappedBuffer->iZippedSize, pWrappedBuffer->pZippedBuf);

	SentryServerComm_SendPacket(&pPak);

	return true;
}
*/

void SentryServerComm_QueryMachineForRunningExes(char *pMachineName)
{
	Packet *pPak;
	pPak = SentryServerComm_CreatePacket(MONITORCLIENT_QUERY, "Querying Machine %s for running exes", pMachineName);
	pktSendStringf(pPak, "machine=%s process_physmem>1", pMachineName);
	SentryServerComm_SendPacket(&pPak);
}

void SentryServerComm_QueryForMachines(void)
{
	Packet *pPak;
	pPak = SentryServerComm_CreatePacket(MONITORCLIENT_QUERY, "Querying for machines");
	pktSendStringf(pPak, "machine=\"\"");
	SentryServerComm_SendPacket(&pPak);
}

//typedef void (*SentryServerComm_FileCRC_CB)(char *pMachineName, char *pFileName, void *pUserData, int iCRC, bool bTimedOut);
//void SentryServerComm_GetFileCRC(char *pMachineName, char *pFileName, SentryServerComm_FileCRC_CB *pCB, void *pUserData);


AUTO_STRUCT;
typedef struct GetFileCRCCache
{
	const char *pMachineName; AST(POOL_STRING)
	char *pFileName;
	void *pUserData; NO_AST
	SentryServerComm_FileCRC_CB pCB; NO_AST
	int iID;
} GetFileCRCCache;

static int siNextGetFileCRCID = 1;

static StashTable sGetFileCRCCachesByID = NULL;

void GetFileCRCTimeoutCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	GetFileCRCCache *pCache;
	if (stashIntRemovePointer(sGetFileCRCCachesByID, (int)(intptr_t)userData, &pCache))
	{
		pCache->pCB(pCache->pMachineName, pCache->pFileName, pCache->pUserData, 0, true);
		StructDestroy(parse_GetFileCRCCache, pCache);
	}
}

void SentryServerComm_GetFileCRC(const char *pMachineName, char *pFileName, SentryServerComm_FileCRC_CB pCB, void *pUserData)
{
	GetFileCRCCache *pCache = StructCreate(parse_GetFileCRCCache);
	Packet *pPak;

	if (!sGetFileCRCCachesByID)
	{
		sGetFileCRCCachesByID = stashTableCreateInt(16);
	}

	pCache->pMachineName = allocAddString(pMachineName);
	pCache->pFileName = strdup(pFileName);
	pCache->pUserData = pUserData;
	pCache->pCB = pCB;
	pCache->iID = siNextGetFileCRCID++;
	if (!siNextGetFileCRCID)
	{
		siNextGetFileCRCID++;
	}

	stashIntAddPointer(sGetFileCRCCachesByID, pCache->iID, pCache, true);

	pPak = SentryServerComm_CreatePacket(MONITORCLIENT_GETFILECRC, "Querying Machine %s for CRC of %s", pMachineName, pFileName);
	pktSendString(pPak, pMachineName);
	pktSendBits(pPak, 32, pCache->iID);
	pktSendString(pPak, pFileName);
	SentryServerComm_SendPacket(&pPak);

	TimedCallback_Run(GetFileCRCTimeoutCB, (void*)(intptr_t)(pCache->iID), GETFILECRC_TIMEOUT);
}

static void handleHereIsFileCRC(Packet *pPak)
{
	int iID = pktGetBits(pPak, 32);
	U32 iCRC = pktGetBits(pPak, 32);
	GetFileCRCCache *pCache;
	
	if (stashIntRemovePointer(sGetFileCRCCachesByID, iID, &pCache))
	{
		pCache->pCB(pCache->pMachineName, pCache->pFileName, pCache->pUserData, iCRC, 0);
		StructDestroy(parse_GetFileCRCCache, pCache);
	}
}

typedef struct DeferredSendSingleFile DeferredSendSingleFile;
typedef struct SentryServerCommDeferredSendHandle SentryServerCommDeferredSendHandle;

StashTable sTargetsByCRCQueryHandle = NULL;


AUTO_STRUCT;
typedef struct DeferredSendSingleTarget
{
	const char *pMachineName; AST(POOL_STRING)
	char *pRemoteFileName;
	int iCRCQueryHandle;
	DeferredSendSingleFile *pParent; NO_AST
} DeferredSendSingleTarget;

AUTO_FIXUPFUNC;
TextParserResult DeferredSendSingleTargetFixup(DeferredSendSingleTarget *pTarget, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		if (pTarget->iCRCQueryHandle)
		{
			stashIntRemovePointer(sTargetsByCRCQueryHandle, pTarget->iCRCQueryHandle, NULL);
		}
		break;
	}

	return PARSERESULT_SUCCESS;
}

AUTO_STRUCT;
typedef struct DeferredSendSingleFile
{
	char *pLocalFileName; AST(KEY)
	DeferredSendSingleTarget **ppTargets;
	int iQueriesOutstanding;
	int iCRC;
	SentryServerCommDeferredSendHandle *pParent; NO_AST
	bool bZipping;
	bool bZippingFailed;
	void *pZippedBuffer; NO_AST
	int iZippedBufferSize;
	int iUnzippedBufferSize;
} DeferredSendSingleFile;

AUTO_FIXUPFUNC;
TextParserResult DeferredSendSingleFileFixup(DeferredSendSingleFile *pFile, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		if (pFile->bZipping || pFile->pZippedBuffer)
		{
			assertmsgf(0, "Trying to destroy file send for %s while zipping is ongoing... that is illegal", pFile->pLocalFileName);
		}
		break;
	}

	return PARSERESULT_SUCCESS;
}

AUTO_STRUCT;
typedef struct SentryServerCommDeferredSendHandle 
{
	char *pName;
	DeferredSendSingleFile **ppFiles;
	DeferredSendsUpdateCB pUpdateCB; NO_AST
	DeferredSendsResultCB pResultsCB; NO_AST
	void *pUserData; NO_AST

	//stuff below here used while actually doing the processing?
	char *pErrorString; AST(ESTRING)
	DeferredSendSingleFile *pActiveFile;
} SentryServerCommDeferredSendHandle;

void DeferredSendLog(SentryServerCommDeferredSendHandle *pHandle, FORMAT_STR const char* format, ...)
{
	char *pFullString = NULL;
	estrGetVarArgs(&pFullString, format);
	printf("%s\n", pFullString);
	estrDestroy(&pFullString);
}

void DeferredSendError(SentryServerCommDeferredSendHandle *pHandle, FORMAT_STR const char* format, ...)
{
	char *pFullString = NULL;
	estrGetVarArgs(&pFullString, format);
	printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: %s\n", pFullString);
	estrConcatf(&pHandle->pErrorString, "%s\n", pFullString);
	estrDestroy(&pFullString);
}

static SentryServerCommDeferredSendHandle **sppActiveDeferredSends = NULL;

SentryServerCommDeferredSendHandle *SentryServerComm_BeginDeferredFileSending(char *pName)
{
	SentryServerCommDeferredSendHandle *pHandle = StructCreate(parse_SentryServerCommDeferredSendHandle);
	pHandle->pName = strdup(pName);
	return pHandle;
}

bool SentryServerComm_SendFileDeferred(SentryServerCommDeferredSendHandle *pHandle, char *pMachineName, char *pLocalFileName, char *pRemoteFileName)
{
	DeferredSendSingleFile *pLocalFile;
	DeferredSendSingleTarget *pTarget;
	pLocalFile = eaIndexedGetUsingString(&pHandle->ppFiles, pLocalFileName);

	if (!pLocalFile)
	{
		U32 iCRC;
		if (!fileExists(pLocalFileName))
		{
			return false;
		}

		iCRC = cryptAdlerFile(pLocalFileName);
		if (!iCRC)
		{
			return false;
		}

		pLocalFile = StructCreate(parse_DeferredSendSingleFile);
		pLocalFile->pLocalFileName = strdup(pLocalFileName);
		pLocalFile->iCRC = iCRC;
		pLocalFile->pParent = pHandle;

		eaPush(&pHandle->ppFiles, pLocalFile);
	}

	pTarget = StructCreate(parse_DeferredSendSingleTarget);
	pTarget->pMachineName = allocAddString(pMachineName);
	pTarget->pRemoteFileName = strdup(pRemoteFileName);
	pTarget->pParent = pLocalFile;
	eaPush(&pLocalFile->ppTargets, pTarget);

	return true;
}

void SentryServerComm_DeferredFileSending_DoIt(SentryServerCommDeferredSendHandle *pHandle, DeferredSendsUpdateCB pUpdateCB, DeferredSendsResultCB pResultsCB, void *pUserData)
{
	sbDeferredSends = true;

	pHandle->pUpdateCB = pUpdateCB;
	pHandle->pResultsCB = pResultsCB;
	pHandle->pUserData = pUserData;

	EnterCriticalSection(&sSentryServerCommCritSec);
	eaPush(&sppActiveDeferredSends, pHandle);
	LeaveCriticalSection(&sSentryServerCommCritSec);
}

void ActiveFileZipCB(void *pData, int iDataSize, int iUncompressedDataSize, U32 iCRC, DeferredSendSingleFile *pFile)
{
	pFile->bZipping = false;

	if (!pData)
	{
		pFile->bZippingFailed = true;
		return;
	}

	pFile->pZippedBuffer = pData;
	pFile->iZippedBufferSize = iDataSize;
	pFile->iUnzippedBufferSize = iUncompressedDataSize;
}



static void UpdateActiveFile(SentryServerCommDeferredSendHandle *pHandle)
{
	DeferredSendSingleFile *pFile = pHandle->pActiveFile;

	if (pFile->iQueriesOutstanding)
	{
		//do nothing
		return;
	}

	if (pFile->bZipping)
	{
		//do nothing
		return;
	}

	if (!eaSize(&pFile->ppTargets))
	{
		DeferredSendLog(pHandle, "Nowhere to actually send %s, done!", pFile->pLocalFileName);
		StructDestroy(parse_DeferredSendSingleFile, pFile);
		pHandle->pActiveFile = NULL;
		return;
	}
	else
	{
		if (pFile->bZippingFailed)
		{
			DeferredSendError(pHandle, "Zipping %s failed, can't send it", pFile->pLocalFileName);
			StructDestroy(parse_DeferredSendSingleFile, pFile);
			pHandle->pActiveFile = NULL;
			return;
		}
		else if (pFile->pZippedBuffer)
		{
			Packet *pPak;
			
			DeferredSendLog(pHandle, "%s has been zipped, ready to send it to %d machines", 
				pFile->pLocalFileName, eaSize(&pFile->ppTargets));
			//all queries are done, zipping is done, we can actually do our sending
			pPak = SentryServerComm_CreatePacket(MONITORCLIENT_CREATEFILE_MULTIPLEMACHINES, "Deferred send %s sending file %s to %d recipients",
				pHandle->pName, pFile->pLocalFileName, eaSize(&pFile->ppTargets));

			FOR_EACH_IN_EARRAY(pFile->ppTargets, DeferredSendSingleTarget, pTarget)
			{
				pktSendString(pPak, pTarget->pMachineName);
				pktSendString(pPak, pTarget->pRemoteFileName);
			}
			FOR_EACH_END;
			pktSendString(pPak, "");

			pktSendBits(pPak, 32, pFile->iZippedBufferSize);
			pktSendBits(pPak, 32, pFile->iUnzippedBufferSize);
			pktSendBytes(pPak, pFile->iZippedBufferSize, pFile->pZippedBuffer);
			SentryServerComm_SendPacket(&pPak);

			free(pFile->pZippedBuffer);
			pFile->pZippedBuffer = NULL;
			StructDestroy(parse_DeferredSendSingleFile, pFile);
			pHandle->pActiveFile = NULL;

		}
		else
		{
			//queries are done, zipping is not active and not done, so we must need to begin zipping
			ThreadedLoadAndZipFile(pFile->pLocalFileName, ActiveFileZipCB, pFile);
			pFile->bZipping = true;
			DeferredSendLog(pHandle, "Going to have to send %s through sentry server... beginning threaded zipping",
				pFile->pLocalFileName);
		}
	}
}

static void DeferredFileSendCRCCB(const char *pMachineName, char *pFileName, void *pUserData, int iCRC, bool bTimedOut)
{
	DeferredSendSingleTarget *pTarget;
	if (stashIntRemovePointer(sTargetsByCRCQueryHandle, (int)((intptr_t)pUserData), &pTarget))
	{
		bool bRemove = false;
		DeferredSendSingleFile *pFile = pTarget->pParent;
		SentryServerCommDeferredSendHandle *pHandle = pFile->pParent;

		pTarget->iCRCQueryHandle = 0;

		pFile->iQueriesOutstanding--;

		if (bTimedOut)
		{
			DeferredSendError(pHandle, "Got no response back from machine %s concerning %s", pTarget->pMachineName, pTarget->pRemoteFileName);
			bRemove = true;
		}
		else if (!iCRC)
		{
			DeferredSendLog(pHandle, "Machine %s seems to not have %s at all, will need to send it", pTarget->pMachineName, pTarget->pRemoteFileName);
		}
		else if (iCRC != pFile->iCRC)
		{
			DeferredSendLog(pHandle, "Machine %s has %s but the CRC doesn't match, will send it", pTarget->pMachineName, pTarget->pRemoteFileName);
		}
		else
		{
			DeferredSendLog(pHandle, "Machine %s has %s, CRCs match, not sending", pTarget->pMachineName, pTarget->pRemoteFileName);
			bRemove = true;
		}

		if (bRemove)
		{
			eaFindAndRemove(&pFile->ppTargets, pTarget);
			StructDestroy(parse_DeferredSendSingleTarget, pTarget);
		}
	}
}

static void AssignCRCQueryHandle(DeferredSendSingleTarget *pTarget)
{
	static int siNext = 1;
	pTarget->iCRCQueryHandle = siNext++;
	if (!siNext)
	{
		siNext++;
	}

	if (!sTargetsByCRCQueryHandle)
	{
		sTargetsByCRCQueryHandle = stashTableCreateInt(16);
	}

	stashIntAddPointer(sTargetsByCRCQueryHandle, pTarget->iCRCQueryHandle, pTarget, true);
}

static void BeginActiveFile(SentryServerCommDeferredSendHandle *pHandle)
{
	int i;

	DeferredSendSingleFile *pFile = pHandle->pActiveFile;
	DeferredSendLog(pHandle, "Beginning processing of %s, to be sent to %d recipients\n", pFile->pLocalFileName, eaSize(&pFile->ppTargets));
	

	for (i = eaSize(&pFile->ppTargets) - 1; i >= 0; i--)
	{
		DeferredSendSingleTarget *pTarget = pFile->ppTargets[i];
		if (IsActuallyLocalMachine(pTarget->pMachineName))
		{
			char *pSystemString = NULL;
			DeferredSendLog(pHandle, "Machine %s is the local machine... just doing file copy", pTarget->pMachineName);
			estrStackCreate(&pSystemString);
			mkdirtree_const(pTarget->pRemoteFileName);
			estrPrintf(&pSystemString, "%s %s", pFile->pLocalFileName,  pTarget->pRemoteFileName);
			backSlashes(pSystemString);
			estrInsertf(&pSystemString, 0, "cmd /c copy ");
			system_detach(pSystemString, 0, 0);
			estrDestroy(&pSystemString);

			eaRemove(&pFile->ppTargets, i);
			StructDestroy(parse_DeferredSendSingleTarget, pTarget);
		}
		else
		{
			AssignCRCQueryHandle(pTarget);
			SentryServerComm_GetFileCRC(pTarget->pMachineName, pTarget->pRemoteFileName, DeferredFileSendCRCCB, (void*)(intptr_t)(pTarget->iCRCQueryHandle));
			pFile->iQueriesOutstanding++;
		}

	}

}

static bool SentryServerComm_UpdateDeferredSend(SentryServerCommDeferredSendHandle *pHandle)
{
	if (pHandle->pActiveFile)
	{
		UpdateActiveFile(pHandle);
		return false;
	}

	if (!eaSize(&pHandle->ppFiles))
	{
		DeferredSendLog(pHandle, "No more files to send... we must be done!");
		if (estrLength(&pHandle->pErrorString))
		{
			pHandle->pResultsCB(false, NULL, pHandle->pUserData);
		}
		else
		{
			pHandle->pResultsCB(true, pHandle->pErrorString, pHandle->pUserData);
		}
		return true;
	}

	pHandle->pActiveFile = eaPop(&pHandle->ppFiles);
	BeginActiveFile(pHandle);

	return false;
}

static void SentryServerComm_DeferredFileSending_Tick(void)
{
	SentryServerCommDeferredSendHandle **ppHandles;
	int i;

	EnterCriticalSection(&sSentryServerCommCritSec);
	ppHandles = sppActiveDeferredSends;
	sppActiveDeferredSends = NULL;
	LeaveCriticalSection(&sSentryServerCommCritSec);

	for (i = eaSize(&ppHandles) - 1; i >= 0; i--)
	{
		SentryServerCommDeferredSendHandle *pHandle = ppHandles[i];
		if (SentryServerComm_UpdateDeferredSend(pHandle))
		{
			StructDestroy(parse_SentryServerCommDeferredSendHandle, pHandle);
			eaRemoveFast(&ppHandles, i);
		}
	}

	EnterCriticalSection(&sSentryServerCommCritSec);
	eaPushEArray(&sppActiveDeferredSends, &ppHandles);
	LeaveCriticalSection(&sSentryServerCommCritSec);
	eaDestroy(&ppHandles);
}






AUTO_STRUCT;
typedef struct GetDirectoryContentsCache
{
	const char *pMachineName; AST(POOL_STRING)
	char *pDirName;
	void *pUserData; NO_AST
	SentryServerComm_DirectoryContents_CB pCB; NO_AST
	int iID;
} GetDirectoryContentsCache;

static int siNextGetDirectoryContentsID = 1;

static StashTable sGetDirectoryContentsCachesByID = NULL;

void GetDirectoryContentsTimeoutCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	GetDirectoryContentsCache *pCache;
	if (stashIntRemovePointer(sGetDirectoryContentsCachesByID, (int)(intptr_t)userData, &pCache))
	{
		pCache->pCB(pCache->pMachineName, pCache->pDirName, pCache->pUserData, 0, true);
		StructDestroy(parse_GetDirectoryContentsCache, pCache);
	}
}

void SentryServerComm_GetDirectoryContents(const char *pMachineName, char *pDirName, SentryServerComm_DirectoryContents_CB pCB, void *pUserData)
{
	GetDirectoryContentsCache *pCache = StructCreate(parse_GetDirectoryContentsCache);
	Packet *pPak;

	if (!sGetDirectoryContentsCachesByID)
	{
		sGetDirectoryContentsCachesByID = stashTableCreateInt(16);
	}

	pCache->pMachineName = allocAddString(pMachineName);
	pCache->pDirName = strdup(pDirName);
	pCache->pUserData = pUserData;
	pCache->pCB = pCB;
	pCache->iID = siNextGetDirectoryContentsID++;
	if (!siNextGetDirectoryContentsID)
	{
		siNextGetDirectoryContentsID++;
	}

	stashIntAddPointer(sGetDirectoryContentsCachesByID, pCache->iID, pCache, true);

	pPak = SentryServerComm_CreatePacket(MONITORCLIENT_GETDIRECTORYCONTENTS, "Querying Machine %s for directory contents of %s", pMachineName, pDirName);
	pktSendString(pPak, pMachineName);
	pktSendBits(pPak, 32, pCache->iID);
	pktSendString(pPak, pDirName);
	SentryServerComm_SendPacket(&pPak);

	TimedCallback_Run(GetDirectoryContentsTimeoutCB, (void*)(intptr_t)(pCache->iID), GETDIRECTORYCONTENTS_TIMEOUT);
}

static void handleHereAreDirectoryContents(Packet *pPak)
{
	int iID = pktGetBits(pPak, 32);
	bool bSucceeded = pktGetBits(pPak, 1);
	GetDirectoryContentsCache *pCache;
	char *pDirContents = NULL;
	if (bSucceeded)
	{
		pDirContents = pktGetStringTemp(pPak);
	}

	if (stashIntRemovePointer(sGetDirectoryContentsCachesByID, iID, &pCache))
	{
		pCache->pCB(pCache->pMachineName, pCache->pDirName, pCache->pUserData, pDirContents, !bSucceeded);
		StructDestroy(parse_GetDirectoryContentsCache, pCache);
	}
}




StashTable sRunningExeQueries = NULL;

typedef struct RunningExeQueryCache
{
       int iID;
       SentryServerExesQueryCB pCB;
       void *pUserData;
} RunningExeQueryCache;

void SentryServerComm_QueryMachineForRunningExes_Simple(char *pMachineName, SentryServerExesQueryCB pCB, void *pUserData)
{
	static int siNextID = 1;
	Packet *pPak;

	RunningExeQueryCache *pCache = calloc(sizeof(RunningExeQueryCache), 1);
	pCache->iID = siNextID++;

	if (!siNextID)
	{
		siNextID++;
	}

	pCache->pCB = pCB;
	pCache->pUserData = pUserData;

	if (!sRunningExeQueries)
	{
		sRunningExeQueries = stashTableCreateInt(16);
	}

	stashIntAddPointer(sRunningExeQueries, pCache->iID, pCache, true);

	pPak = SentryServerComm_CreatePacket(MONITORCLIENT_SIMPLEQUERY_PROCESSES_ON_ONE_MACHINE, "Querying for processes");
	pktSendString(pPak, pMachineName);
	pktSendBits(pPak, 32, pCache->iID);
	SentryServerComm_SendPacket(&pPak);
}

static void handleRunningProcessesResponse(Packet *pak)
{
	RunningExeQueryCache *pCache;
	SentryProcess_FromSimpleQuery_List *pList = ParserRecvStructSafe_Create(parse_SentryProcess_FromSimpleQuery_List, pak);

	if (stashIntRemovePointer(sRunningExeQueries, pList->iQueryID, &pCache))
	{
		pCache->pCB(pList, pCache->pUserData);
		free(pCache);
	}
       
	StructDestroy(parse_SentryProcess_FromSimpleQuery_List, pList);
}



StashTable sMachineQueries = NULL;

typedef struct MachineQueryCache
{
       int iID;
       SentryServerMachinesQueryCB pCB;
       void *pUserData;
} MachineQueryCache;

void SentryServerComm_QueryForMachines_Simple(SentryServerMachinesQueryCB pCB, void *pUserData)
{
	static int siNextID = 1;
	Packet *pPak;

	MachineQueryCache *pCache = calloc(sizeof(MachineQueryCache), 1);
	pCache->iID = siNextID++;

	if (!siNextID)
	{
		siNextID++;
	}

	pCache->pCB = pCB;
	pCache->pUserData = pUserData;

	if (!sMachineQueries)
	{
		sMachineQueries = stashTableCreateInt(16);
	}

	stashIntAddPointer(sMachineQueries, pCache->iID, pCache, true);

	pPak = SentryServerComm_CreatePacket(MONITORCLIENT_SIMPLEQUERY_MACHINES, "Querying for machines");
	pktSendBits(pPak, 32, pCache->iID);
	SentryServerComm_SendPacket(&pPak);
}

static void handleSimpleMachinesResponse(Packet *pak)
{
    MachineQueryCache *pCache;
    SentryMachines_FromSimpleQuery *pList = ParserRecvStructSafe_Create(parse_SentryMachines_FromSimpleQuery, pak);

    if (stashIntRemovePointer(sMachineQueries, pList->iQueryID, &pCache))
    {
		pCache->pCB(pList, pCache->pUserData);
		free(pCache);
    }
       
    StructDestroy(parse_SentryMachines_FromSimpleQuery, pList);
}



StashTable sGetFileQueries = NULL;

typedef struct GetFileQueryCache
{
    int iID;
    SentryServerGetFileCB pCB;
    void *pUserData;
} GetFileQueryCache;

void SentryServerComm_GetFileContents(char *pMachineName, char *pFileName, SentryServerGetFileCB pCB, void *pUserData)
{
	static int siNextID = 1;
	Packet *pPak;

	GetFileQueryCache *pCache = calloc(sizeof(GetFileQueryCache), 1);
	pCache->iID = siNextID++;

	if (!siNextID)
	{
		siNextID++;
	}

	pCache->pCB = pCB;
	pCache->pUserData = pUserData;

	if (!sGetFileQueries)
	{
		sGetFileQueries = stashTableCreateInt(16);
	}

	stashIntAddPointer(sGetFileQueries, pCache->iID, pCache, true);

	pPak = SentryServerComm_CreatePacket(MONITORCLIENT_GETFILECONTENTS, "Querying for machines");
	pktSendBits(pPak, 32, pCache->iID);
	pktSendString(pPak, pMachineName);
	pktSendString(pPak, pFileName);
	SentryServerComm_SendPacket(&pPak);
}

static void handleGetFileContentsResponse(Packet *pak)
{
	GetFileQueryCache *pCache;
	FileContents_FromSimpleQuery *pContents = ParserRecvStructSafe_Create(parse_FileContents_FromSimpleQuery, pak);

	if (stashIntRemovePointer(sGetFileQueries, pContents->iQueryID, &pCache))
	{
		char *pFileContents = NULL;
		int iFileSize = 0;

		if (pContents->pContents)
		{
			pFileContents = TextParserBinaryBlock_PutIntoMallocedBuffer(pContents->pContents, &iFileSize);
		}

		pCache->pCB(pContents->pMachineName, pContents->pFileName, pFileContents, iFileSize, pCache->pUserData);
		free(pFileContents);
	}

	free(pCache);
	StructDestroy(parse_FileContents_FromSimpleQuery, pContents);
}

//specially wrapped version of ExecuteCommand that makes a little batch file that does some little magical batch commands to
//determine if the machine is 64-bit and then tries to launch commandX64.exe instead of command.exe if it does
void SentryServerComm_ExecuteCommandWith64BitFixup(const char **ppMachineNames, const char *pExeName, const char *pCommandLine, const char *pWorkingDir, const char *pUniqueComment)
{
	char *pBatchFileContents = NULL;
	char *pBatchFileName = NULL;
	estrConcatf(&pBatchFileContents, "cd %s\r\n", pWorkingDir);
	estrConcatf(&pBatchFileContents, "IF DEFINED programw6432 (set SUFFIX=X64.exe) else (SET SUFFIX=.exe)\r\n");
	estrConcatf(&pBatchFileContents, "start %s", pExeName);
	if (strEndsWith(pBatchFileContents, ".exe"))
	{
		estrRemove(&pBatchFileContents, estrLength(&pBatchFileContents) - 4, 4);
	}

	estrConcatf(&pBatchFileContents, "%%SUFFIX%% %s\r\nexit\r\n", pCommandLine);

	estrPrintf(&pBatchFileName, "Run_%s_%s", pExeName, pUniqueComment);
	estrMakeAllAlphaNumAndUnderscores(&pBatchFileName);
	estrConcatf(&pBatchFileName, ".bat");
	estrInsertf(&pBatchFileName, 0, "%s\\", pWorkingDir);

	{
		int iInFileSize = estrLength(&pBatchFileContents) + 1;
		char *pCompressedBuffer = NULL;
		int iCompressedSize;

		Packet *pPak;

		pCompressedBuffer = zipData(pBatchFileContents, iInFileSize, &iCompressedSize);
	
		FOR_EACH_IN_EARRAY((char**)ppMachineNames, char, pMachineName)
		{
			pPak = SentryServerComm_CreatePacket(MONITORCLIENT_CREATEFILE, "Sending %s for %s to %s", pExeName, pUniqueComment, pMachineName);	
			pktSendString(pPak, pMachineName);
			pktSendString(pPak, pBatchFileName);
			pktSendBits(pPak, 32, iCompressedSize);
			pktSendBits(pPak, 32, iInFileSize);
			pktSendBytes(pPak, iCompressedSize, pCompressedBuffer);

			SentryServerComm_SendPacket(&pPak);
		}
		FOR_EACH_END;

		SAFE_FREE(pCompressedBuffer);
	}

	SentryServerComm_RunCommand(ppMachineNames, pBatchFileName);

	estrDestroy(&pBatchFileContents);
	estrDestroy(&pBatchFileName);
}




#include "SentryServerComm_c_Ast.c"
#include "sentrypub_h_ast.c"
#include "SentryServerComm_h_Ast.c"

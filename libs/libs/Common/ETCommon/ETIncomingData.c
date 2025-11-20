#include "errornet.h"
#include "ETCommon/ETIncomingData.h"
#include "ETCommon/ETCommonStructs.h"
#include "ETCommon/ETDumps.h"
#include "ETCommon/ETShared.h"
#include "AutoGen/errornet_h_ast.h"
#include "AutoGen/ETCommonStructs_h_ast.h"
#include "AutoGen/ETIncomingData_h_ast.h"

#include "Alerts.h"
#include "CrypticPorts.h"
#include "estring.h"
#include "file.h"
#include "GlobalComm.h"
#include "GlobalTypeEnum.h"
#include "logging.h"
#include "net.h"
#include "objTransactions.h"
#include "sock.h"
#include "StashTable.h"
#include "StringUtil.h"
#include "StructInit.h"
#include "timing.h"
#include "winutil.h"
#include "utf8.h"

int giNumSlaves = 0;
U32 *gSlaveErrorTrackerLinks;
extern ErrorTrackerSettings gErrorTrackerSettings;
static IncomingDataHandler fpDataHandlers[INCOMINGDATATYPE_MAX-1] = {0};

void ETIncoming_SetDataTypeHandler(IncomingDataType eType, IncomingDataHandler func)
{
	if (eType > INCOMINGDATATYPE_UNKNOWN && eType < INCOMINGDATATYPE_MAX)
	{
		fpDataHandlers[eType-1] = func;
	}
}

void ProcessIncomingData(IncomingData *pIncomingData, NetLink *link, IncomingClientState *pClientState)
{
	if (pIncomingData->eType > INCOMINGDATATYPE_UNKNOWN && pIncomingData->eType < INCOMINGDATATYPE_MAX)
	{
		if (fpDataHandlers[pIncomingData->eType-1])
		{
			fpDataHandlers[pIncomingData->eType-1](pIncomingData, link, pClientState);
		}
	}
	StructDestroy(parse_IncomingData, pIncomingData);
}

/////////////////////////////////////////
// Old stuff from IncomingData.c

// This must match the value in sendlogtotracker.cpp!
// TODO: Move this into a header
static char readBuffer[DUMP_READ_BUFFER_SIZE];

extern int gMaxIncomingDumpsPerEntry;

// ---------------------------------------------------------------------------------

static void SwitchContext(Packet *pak,int cmd, NetLink *link, void *userData);
static void IncomingHandleMsg(Packet *pak,int cmd, NetLink *link, IncomingClientState *pClientState);
static int IncomingClientConnect(NetLink* link, IncomingClientState *pClientState);
static int IncomingClientDisconnect(NetLink* link, IncomingClientState *pClientState);

bool gbStopWaitingForDumpConnection = false;

// ---------------------------------------------------------------------------------

IncomingClientState **gActiveDumps = NULL;
int giCurrentDumpReceives = 0; // How many incoming dumps are there right now?
// used by dumpSendWait()

// Also used for generic file sends so there aren't too many concurrent ones
static void addActiveDump(IncomingClientState *pActiveDump)
{
	eaPush(&gActiveDumps, pActiveDump);
	giCurrentDumpReceives++;

	if(pActiveDump->uDumpID)
	{
		DumpData *pDumpData = findDumpData(pActiveDump->uDumpID);
		if(pDumpData && pDumpData->pEntry)
		{
			addIncomingDumpCount(pDumpData->pEntry->uID);
		}
	}
}

// Does NOT destroy the temporary file
static void removeActiveDump(IncomingClientState *pActiveDump)
{
	if(pActiveDump->pTempOutputFile)
	{
		fclose((FileWrapper*) pActiveDump->pTempOutputFile);
		pActiveDump->pTempOutputFile = NULL;
	}

	if (eaFindAndRemove(&gActiveDumps, pActiveDump) >= 0)
		giCurrentDumpReceives--;

	if(pActiveDump->uDumpID)
	{
		DumpData *pDumpData = findDumpData(pActiveDump->uDumpID);
		if(pDumpData && pDumpData->pEntry)
		{
			removeIncomingDumpCount(pDumpData->pEntry->uID);
		}
	}
}

// ---------------------------------------------------------------------------------

static char* gMasterToConnectTo = NULL;
bool gMasterETDisconnected = false;
static int gNumDumps = 0;
IncomingClientState *currContext;
static StashTable contextData = NULL;

static int MasterETDisconnect(NetLink* link, void* userData)
{
	FOR_EACH_IN_STASHTABLE(contextData, IncomingClientState, context)
	{
		context->link = NULL;

		if(context->pTempOutputFile != NULL)
		{
			removeActiveDump(context);

			// Cleanup after the failed dump send
			DeleteFile_UTF8(context->pTempOutputFileName);
			estrDestroy(&context->pTempOutputFileName);
		}

		if(context->pDeferredDumpData)
		{
			if (gbETVerbose) printf("Cleaning up deferred dump data.\n");
			StructDestroyNoConst(parse_DumpData, context->pDeferredDumpData);
		}
		if (context->pTempDescription)
		{
			free(context->pTempDescription);
			context->pTempDescription = NULL;
		}

		free(context);
	} FOR_EACH_END;
	
	gMasterETDisconnected = true;
	stashTableDestroy(contextData);
	contextData = NULL;
	currContext = NULL;

	return 1;
}

AUTO_COMMAND ACMD_CMDLINE;
void slaveOf(const char *pMasterET)
{
	gMasterToConnectTo = strdup(pMasterET);
	gMasterETDisconnected = true;
}

AUTO_COMMAND ACMD_CMDLINE;
void receiveDumps(U32 numDumps)
{
	gNumDumps = numDumps;
}

void connectToMasterErrorTracker()
{
	Packet *pPkt;
	NetLink *pMasterETlink = commConnect(commDefault(), LINKTYPE_USES_FULL_SENDBUFFER, LINK_NO_COMPRESS|LINK_FORCE_FLUSH, gMasterToConnectTo, 
								DEFAULT_ERRORTRACKER_PORT, SwitchContext, 0, MasterETDisconnect, 0);
	if (!linkConnectWait(&pMasterETlink, 5.f))
	{
		return;
	}

	gMasterETDisconnected = false;
	contextData = stashTableCreateInt(0);

	pPkt = pktCreate(pMasterETlink, TO_ERRORTRACKER_NEWSLAVE);
	//TODO: Send any useful information we want to send.
	pktSendU32(pPkt, gNumDumps);
	pktSend(&pPkt);
}

void addDumpIDInternal(int context, int ID, bool master, IncomingClientState *slaveState)
{
	int tableID;
	if (!stashIntRemoveInt(slaveState->initialIDs, context, &tableID))
		stashIntAddInt(slaveState->initialIDs, context, ID, true);
	else
	{
		if (master)
			stashIntAddInt(slaveState->masterIDtoSlaveIDTable, ID, tableID, true);
		else
			stashIntAddInt(slaveState->masterIDtoSlaveIDTable, tableID, ID, true);
	}
}

void addDumpID(int context, int ID, bool master, IncomingClientState *slaveState)
{
	if (!context || !ID)
		return;
	if (slaveState)
	{
		addDumpIDInternal(context, ID, master, slaveState);
	}
	else
	{
		int i;
		for(i = 0; i < eaiSize(&gSlaveErrorTrackerLinks); i++)
		{
			NetLink *link = linkFindByID(gSlaveErrorTrackerLinks[i]);
			IncomingClientState *state;
			if (!link)
				continue;
			state = (IncomingClientState *)linkGetUserData(link);
			addDumpIDInternal(context, ID, master, state);
		}
	}
}

void sendErrorDataToSlaves(Packet *pPkt, U32 id)
{
	char *pData = NULL;
	int i;
	PERFINFO_AUTO_START_FUNC();
	for (i = 0; i < eaiSize(&gSlaveErrorTrackerLinks); i++)
	{
		Packet *pReadPkt;
		Packet *pSendPkt;
		Packet *pContextPkt;
		NetLink *link = linkFindByID(gSlaveErrorTrackerLinks[i]);
		if (!link)
			continue;
		pContextPkt = pktCreate(link, TO_ERRORTRACKER_SWITCH_CONTEXT);
		pktSendU32(pContextPkt, id);
		pktSend(&pContextPkt);
		pReadPkt = pktDup(pPkt, link);
		pSendPkt = pktCreate(link, TO_ERRORTRACKER_ERROR);
		pData = strdup(pktGetStringTemp(pReadPkt));
		pktSendString(pSendPkt, pData);
		pktSend(&pSendPkt);
		pktFree(&pReadPkt);
		free(pData);
	}
	PERFINFO_AUTO_STOP();
}

void startSendDumpToSlaves(Packet *pPkt, U32 id)
{
	int i;
	for (i = 0; i < eaiSize(&gSlaveErrorTrackerLinks); i++)
	{
		NetLink *link = linkFindByID(gSlaveErrorTrackerLinks[i]);
		IncomingClientState *pState = (IncomingClientState*)linkGetUserData(link);
		if (!pState || pState->uDumpsToSend == 0)
			continue;
		pState->uDumpsSinceLastSend++;
		if (pState->uCurrentlySendingDump == 0 && pState->uDumpsSinceLastSend >= pState->uDumpsToSend)
		{
			U32 uDumpFlags;
			Packet *pReadPkt = pktDup(pPkt, link);
			Packet *pSendPkt = pktCreate(link, TO_ERRORTRACKER_DUMP_START);
			Packet *pContextPkt = pktCreate(link, TO_ERRORTRACKER_SWITCH_CONTEXT);
			U32 uDumpID;
			int iSlaveDumpID;
			
			
			uDumpFlags = pktGetU32(pReadPkt);
			pktSendU32(pSendPkt, uDumpFlags);
			if(uDumpFlags & DUMPFLAGS_DEFERRED)
				pktSendString(pSendPkt, pktGetStringTemp(pReadPkt));
			uDumpID = pktGetU32(pReadPkt);
			if(!stashIntFindInt(pState->masterIDtoSlaveIDTable, uDumpID, &iSlaveDumpID))
			{
				pktFree(&pReadPkt);
				pktFree(&pSendPkt);
				pktFree(&pContextPkt);
				continue;
			}
			pState->uCurrentlySendingDump = id;
			pState->uDumpsSinceLastSend = 0;

			pktSendU32(pContextPkt, id);
			pktSend(&pContextPkt);

			pktSendU32(pSendPkt, iSlaveDumpID);
			if (!(uDumpFlags & DUMPFLAGS_DEFERRED) && uDumpFlags & DUMPFLAGS_EXTERNAL)
				pktSendString(pSendPkt, pktGetStringTemp(pReadPkt));
			pktSend(&pSendPkt);
			pktFree(&pReadPkt);		
		}
	}
}

void continueSendDumpToSlaves(Packet *pPkt, U32 id)
{
	int i;
	for (i = 0; i < eaiSize(&gSlaveErrorTrackerLinks); i++)
	{
		NetLink *link = linkFindByID(gSlaveErrorTrackerLinks[i]);
		IncomingClientState *pState = (IncomingClientState*)linkGetUserData(link);
		if (!pState)
			continue;
		if (pState->uCurrentlySendingDump == id)
		{
			Packet *pReadPkt = pktDup(pPkt, link);
			Packet *pSendPkt = pktCreate(link, TO_ERRORTRACKER_DUMP_CONTINUE);
			Packet *pContextPkt = pktCreate(link, TO_ERRORTRACKER_SWITCH_CONTEXT);
			U32 uReadBytes = pktGetU32(pReadPkt);
			pktSendU32(pContextPkt, id);
			pktSend(&pContextPkt);
			pktSendU32(pSendPkt, uReadBytes);
			pktSendU32(pSendPkt, pktGetU32(pReadPkt));
			pktGetBytes(pReadPkt, uReadBytes, readBuffer);
			pktSendBytes(pSendPkt, uReadBytes, readBuffer);
			pktSend(&pSendPkt);
			pktFree(&pReadPkt);
		}
	}
}

void finishSendDumpToSlaves(Packet *pPkt, U32 id)
{
	int i;
	for (i = 0; i < eaiSize(&gSlaveErrorTrackerLinks); i++)
	{
		NetLink *link = linkFindByID(gSlaveErrorTrackerLinks[i]);
		IncomingClientState *pState = (IncomingClientState*)linkGetUserData(link);
		if (!pState)
			continue;
		if (pState->uCurrentlySendingDump == id)
		{
			Packet *pReadPkt = pktDup(pPkt, link);
			Packet *pSendPkt = pktCreate(link, TO_ERRORTRACKER_DUMP_FINISH);
			Packet *pContextPkt = pktCreate(link, TO_ERRORTRACKER_SWITCH_CONTEXT);
			int iSlaveDumpID;
			pktSendU32(pContextPkt, id);
			pktSend(&pContextPkt);
			pktSendU32(pSendPkt, pktGetU32(pReadPkt));
			stashIntRemoveInt(pState->masterIDtoSlaveIDTable, pktGetU32(pReadPkt), &iSlaveDumpID);
			pktSendU32(pSendPkt, iSlaveDumpID);
			pktSendU32(pSendPkt, pktGetU32(pReadPkt));
			pktSend(&pSendPkt);
			pState->uCurrentlySendingDump = 0;
			pktFree(&pReadPkt);
		}
	}
}

void cancelSendDumpToSlaves(Packet *pPkt, U32 id)
{
	int i;
	for (i = 0; i < eaiSize(&gSlaveErrorTrackerLinks); i++)
	{
		NetLink *link = linkFindByID(gSlaveErrorTrackerLinks[i]);
		IncomingClientState *pState = (IncomingClientState*)linkGetUserData(link);
		if (pState->uCurrentlySendingDump == id)
		{
			Packet *pReadPkt = pktDup(pPkt, link);
			Packet *pSendPkt = pktCreate(link, TO_ERRORTRACKER_DUMP_CANCELLED);
			Packet *pContextPkt = pktCreate(link, TO_ERRORTRACKER_SWITCH_CONTEXT);
			int iSlaveDumpID;
			pktSendU32(pContextPkt, id);
			pktSend(&pContextPkt);
			stashIntRemoveInt(pState->masterIDtoSlaveIDTable, pktGetU32(pReadPkt), &iSlaveDumpID);
			pktSendU32(pSendPkt, iSlaveDumpID);
			pktSend(&pSendPkt);
			pState->uCurrentlySendingDump = 0;
			pktFree(&pReadPkt);
		}
	}
}

// ---------------------------------------------------------------------------------

bool initIncomingSecureData(void)
{
	NetListen *pLocal_listen, *pPublic_listen;
	int val;
	for(;;)
	{
		val = commListenBoth(errorTrackerCommDefault(), LINKTYPE_TOUNTRUSTED_20MEG, LINK_NO_COMPRESS, DEFAULT_ERRORTRACKER_SECURE_PORT,
			IncomingHandleMsg,IncomingClientConnect,IncomingClientDisconnect, sizeof(IncomingClientState),
			&pLocal_listen, &pPublic_listen);
		if (val)
			break;
		Sleep(1);
	}

	return true;
}
// ---------------------------------------------------------------------------------

bool initIncomingPublicData(void)
{
	NetListen *pLocal_listen, *pPublic_listen;
	int val;
	for(;;)
	{
		val = commListenBoth(errorTrackerCommDefault(), LINKTYPE_TOUNTRUSTED_5MEG, LINK_NO_COMPRESS, DEFAULT_ERRORTRACKER_PUBLIC_PORT,
			IncomingHandleMsg,IncomingClientConnect,IncomingClientDisconnect, sizeof(IncomingClientState), 
			&pLocal_listen, &pPublic_listen);
		if (val)
			break;
		Sleep(1);
	}

	return true;
}

bool initIncomingData(void)
{
	NetListen *pLocal_listen, *pPublic_listen;
	int val;
	for(;;)
	{
		val = commListenBoth(errorTrackerCommDefault(), LINKTYPE_TOUNTRUSTED_20MEG, LINK_NO_COMPRESS, DEFAULT_ERRORTRACKER_PORT,
			IncomingHandleMsg,IncomingClientConnect,IncomingClientDisconnect, sizeof(IncomingClientState), 
			&pLocal_listen, &pPublic_listen);
		if (val)
			break;
		Sleep(1);
	}

	return true;
}

void shutdownIncomingData(void)
{
	// Doesn't do anything interesting yet
}

// ---------------------------------------------------------------------------------

static StashTable sActiveLinkTable = NULL;
void AddClientLink (NetLink *link)
{
	if (!linkConnected(link))
		return;
	if (!sActiveLinkTable)
		sActiveLinkTable = stashTableCreateInt(1000);
	stashIntAddPointer(sActiveLinkTable, linkID(link), link, true);
}

void RemoveClientLink (NetLink *link)
{
	if (!sActiveLinkTable)
		return;
	stashIntRemovePointer(sActiveLinkTable, linkID(link), NULL);
}

NetLink *FindClientLink (U32 uLinkID)
{
	NetLink *link = NULL;
	if (!sActiveLinkTable || uLinkID == 0)
		return NULL;
	stashIntFindPointer(sActiveLinkTable, uLinkID, &link);
	return link;
}

static void LogIncomingDumpStart(DumpData *pDumpData, IncomingClientState *pClientState, U32 uIP)
{
	U32 uETID = 0;
	char ipString[64];
	const char *pDumpType = "Mini";
	const char *exeName = NULL;

	GetIpStr(uIP, SAFESTR(ipString));

	if (pDumpData && pDumpData->pEntry)
	{
		uETID = pDumpData->pEntry->uID;
		if (eaSize(&pDumpData->pEntry->ppExecutableNames) && !nullStr(pDumpData->pEntry->ppExecutableNames[0]))
			exeName = pDumpData->pEntry->ppExecutableNames[0];
	}
	if (pClientState && (pClientState->uDumpFlags & DUMPFLAGS_FULLDUMP) != 0)
		pDumpType = "Full";
	SERVLOG_PAIRS(LOG_ERRORTRACKER_DUMP_START, "DumpStart", 
		("dumpid", "%d", pClientState->uDumpID)
		("etid", "%d", uETID)
		("type", "%s", pDumpType)
		("ip", "%s", ipString)
		("exe", "%s", exeName ? exeName : "unknown"));
}
static void LogIncomingDumpFinish(DumpData *pDumpData, IncomingClientState *pClientState, U32 uSize, U32 uIP)
{
	U32 uETID = 0;
	char ipString[64];
	const char *pDumpType = "Mini";

	GetIpStr(uIP, SAFESTR(ipString));

	if (pDumpData && pDumpData->pEntry)
		uETID = pDumpData->pEntry->uID;
	if (pClientState && (pClientState->uDumpFlags & DUMPFLAGS_FULLDUMP) != 0)
		pDumpType = "Full";
	SERVLOG_PAIRS(LOG_ERRORTRACKER_DUMP_FINISH, "DumpFinish", 
		("dumpid", "%d", pClientState->uDumpID)
		("etid", "%d", uETID)
		("size", "%d", uSize)
		("type", "%s", pDumpType)
		("ip", "%s", ipString));
}
static void LogIncomingDumpError(IncomingClientState *pClientState, U32 uIP, const char *error)
{
	U32 uETID = 0;
	char ipString[64];
	const char *pDumpType = "Mini";

	GetIpStr(uIP, SAFESTR(ipString));

	if (pClientState && (pClientState->uDumpFlags & DUMPFLAGS_FULLDUMP) != 0)
		pDumpType = "Full";
	SERVLOG_PAIRS(LOG_ERRORTRACKER_DUMP_FINISH, "DumpError", 
		("dumpid", "%d", pClientState->uDumpID)
		("type", "%s", pDumpType)
		("ip", "%s", ipString)
		("error", "%s", error));
}

static int IncomingClientConnect(NetLink* link, IncomingClientState *pClientState)
{
	char ip[IPV4_ADDR_STR_SIZE];
	servLog(LOG_CLIENTSERVERCOMM, "CrypticErrorConnected", "IP %s", linkGetIpStr(link, SAFESTR(ip)));
	pClientState->link                = link;
	pClientState->pTempOutputFileName = NULL;
	pClientState->pTempOutputFile     = NULL;
	pClientState->iCurrentLocation    = 0;
	pClientState->startTime           = 0;
	pClientState->isSlaveErrorTracker = false;
	return 1;
}

static int IncomingClientDisconnect(NetLink* link, IncomingClientState *pClientState)
{
	// Notify the main thread that particular "linkID" is going away
	IncomingData *pEntry = NULL;
	char ip[IPV4_ADDR_STR_SIZE];
	int i;

	servLog(LOG_CLIENTSERVERCOMM, "CrypticErrorDisconnected", "IP %s", linkGetIpStr(link, SAFESTR(ip)));

	pClientState->link = NULL;

	if(pClientState->pTempOutputFile != NULL)
	{
		removeActiveDump(pClientState);

		// Cleanup after the failed dump send
		DeleteFile_UTF8(pClientState->pTempOutputFileName);
		estrDestroy(&pClientState->pTempOutputFileName);

		LogIncomingDumpError(pClientState, linkGetIp(link), "ClientDisconnect");

		for (i = 0; i < eaiSize(&gSlaveErrorTrackerLinks); i++)
		{
			NetLink *slaveLink = linkFindByID(gSlaveErrorTrackerLinks[i]);
			IncomingClientState *pState;
			if (!slaveLink)
				continue;
			pState = (IncomingClientState*)linkGetUserData(slaveLink);
			if (pState && pState->uCurrentlySendingDump == pClientState->context)
			{
				linkShutdown(&slaveLink);
			}
		}
	}

	if (pClientState->isSlaveErrorTracker)
	{
		stashTableDestroy(pClientState->initialIDs);
		stashTableDestroy(pClientState->masterIDtoSlaveIDTable);
		giNumSlaves--;
		eaiFindAndRemoveFast(&gSlaveErrorTrackerLinks, linkID(link));
	}

	pEntry = StructCreate(parse_IncomingData);
	pEntry->eType  = INCOMINGDATATYPE_LINK_DROPPED;
	pEntry->id = pClientState->uDumpID;

	pClientState->startTime = 0;

	ProcessIncomingData(pEntry, link, pClientState);

	if(pClientState->pDeferredDumpData)
	{
		if (gbETVerbose) printf("Cleaning up deferred dump data.\n");
		StructDestroyNoConst(parse_DumpData, pClientState->pDeferredDumpData);
	}
	if (pClientState->pTempDescription)
	{
		free(pClientState->pTempDescription);
		pClientState->pTempDescription = NULL;
	}
	RemoveClientLink(link);

	return 1;
}


static bool makeTempFilename(const char *dir, const char *prefix, char **ppOutput)
{
	if (!dirExists(dir))
		mkdirtree((char*) dir);
	if(!GetTempFileName_UTF8(dir, prefix, 0, ppOutput))
	{
		GetTempFileName_UTF8(".", prefix, 0, ppOutput);
		if (gbETVerbose)
			printf("ERROR: GetTempFileName() failed, does this directory exist?: %s\n", dir);
		return false;
	}

	return true;
}

static void IncomingGenericFileStart(Packet *pak, NetLink *link, IncomingClientState *pClientState)
{
	switch (pClientState->eFileType)
	{
	case ERRORFILETYPE_Xperf:
		ADD_MISC_COUNT(1, "File: Xperf");
		{
			pClientState->uTotalBytes = pktGetU32(pak);
			
			// Create temp file and open it up for write
			makeTempFilename(gErrorTrackerSettings.pDumpTempDir, "MEM", &pClientState->pTempOutputFileName);
			pClientState->pTempOutputFile = fopen(pClientState->pTempOutputFileName, "wb");
			pClientState->iCurrentLocation = 0;
			if (pClientState->pTempOutputFile)
				addActiveDump(pClientState);
		}
	xdefault:
		linkFlushAndClose(&link, "Unknown File Type");
		break;
	}
}
static void IncomingGenericFileContinue(Packet *pak, NetLink *link, IncomingClientState *pClientState)
{
	switch (pClientState->eFileType)
	{
	case ERRORFILETYPE_Xperf:
		{
			int iReadBytes = pktGetU32(pak); // How many bytes in the packet?
			int iSentBytes = pktGetU32(pak); // How many bytes should be in the file now? (sanity check)

			pktGetBytes(pak, iReadBytes, readBuffer);
			fwrite(readBuffer, 1, iReadBytes, pClientState->pTempOutputFile);
			pClientState->iCurrentLocation += iReadBytes;

			if(pClientState->iCurrentLocation != iSentBytes)
			{
				if (gbETVerbose)
					printf("Generic File Receive: Received packet out of order! Something terrible has happened.\n");
				estrDestroy(&pClientState->pTempOutputFileName);
			}
		}
	xdefault:
		linkFlushAndClose(&link, "Unknown File Type");
		break;
	}
}
static void IncomingGenericFileEnd(Packet *pak, NetLink *link, IncomingClientState *pClientState)
{
	// All files are received compressed
	U32 uReceivedBytes = pktGetU32(pak);
	U32 uErrorID = pktGetU32(pak);
	U32 uTotalBytes = pktGetU32(pak);

	switch (pClientState->eFileType)
	{
	case ERRORFILETYPE_Xperf:
		{
			ErrorXperfData *pData = pktGetStruct(pak, parse_ErrorXperfData);
			
			if (pData)
			{
				devassert(pClientState->uTotalBytes == uTotalBytes);
				if (pClientState->pTempOutputFile)
				{
					IncomingData *pEntry = NULL;
					removeActiveDump(pClientState);

					pEntry = StructCreate(parse_IncomingData);
					pEntry->eType         = INCOMINGDATATYPE_XPERF_FILE;
					pEntry->pTempFilename = pClientState->pTempOutputFileName;
					pEntry->pPermanentFilename = pData->pFilename;
					pEntry->id = uErrorID;
					ProcessIncomingData(pEntry, link, pClientState);
				}
				StructDestroy(parse_ErrorXperfData, pData);
			}
			if (fileExists(pClientState->pTempOutputFileName))
				fileForceRemove(pClientState->pTempOutputFileName); // make sure it's deleted here
		}
	xdefault:
		linkFlushAndClose(&link, "Unknown File Type");
		break;
	}
}

static void SwitchContext(Packet *pak, int cmd, NetLink *link, void *userData)
{
	switch(cmd)
	{
	xcase TO_ERRORTRACKER_SWITCH_CONTEXT:
		{
			U32 id = pktGetU32(pak);
			if(!stashIntFindPointer(contextData, id, &currContext))
			{
				currContext = (IncomingClientState*)calloc(1, sizeof(IncomingClientState));
				currContext->context = id;
				stashIntAddPointer(contextData, id, currContext, true);
			}
		}
	xdefault:
		{
			IncomingHandleMsg(pak, cmd, link, currContext);
		}	
	}
}

static void IncomingHandleMsg(Packet *pak,int cmd, NetLink *link, IncomingClientState *pClientState)
{
	if (pClientState && pClientState->isSlaveErrorTracker)
	{
		if (cmd == FROM_ERRORTRACKER_DUMPFLAGS)
		{
			U32 uFlags = pktGetU32(pak);
			U32 uDumpID = pktGetU32(pak);
			U32 context;
			if(uFlags & DUMPFLAGS_UNIQUEID)
				pktGetU32(pak);
			if(uFlags & DUMPFLAGS_DUMPINDEX)
				pktGetU32(pak);
			if (pktCheckRemaining(pak, sizeof(U32)))
			{
				context = pktGetU32(pak);
				addDumpID(context, uDumpID, false, pClientState);
			}
		}
		return;
	}

	if (pClientState->context == 0)
	{
		pClientState->context = linkID(link);
	}

	switch(cmd)
	{
	xcase TO_ERRORTRACKER_ERROR:
		PERFINFO_AUTO_START("ET_IncomingError", 1);
		{
			ErrorData *pErrorData;
			if (giNumSlaves > 0)
				sendErrorDataToSlaves(pak, pClientState->context);
			pErrorData = getErrorDataFromPacket(pak);
			if(pErrorData != NULL)
			{
				IncomingData *pEntry;
				char ip[IPV4_ADDR_STR_SIZE];
				U32 pktNum = 0;
				// Grab the IP from our link
				pErrorData->uIP = linkGetIp(link);
				linkGetIpStr(link, SAFESTR(ip));
				if (pktCheckRemaining(pak, sizeof(U32)))
					pktNum = pktGetU32(pak);
				servLog(LOG_CLIENTSERVERCOMM, "ErrorTrackerGotErrorData", "IP %s User \"%s\" ErrorType %d Version \"%s\" CEPId %d PacketNum %d", 
					ip, pErrorData->pUserWhoGotIt, pErrorData->eType, pErrorData->pVersionString, pErrorData->uCEPId, pktNum);
				servLog(LOG_ERRORDATA, "ErrorTrackerStackData", "LinkID %d IP %s StackData %s", linkID(link), ip, pErrorData->pStackData);

				// We're going to use the link ptr as a unique ID for the connection, and notify
				// the code pulling in the data when the link closes in order to clean out any temp data.
				pEntry = StructCreate(parse_IncomingData);
				pEntry->eType      = INCOMINGDATATYPE_ERRORDATA;
				pEntry->pErrorData = pErrorData;
				ProcessIncomingData(pEntry, link, pClientState);
			}
			else
			{
				char ip[IPV4_ADDR_STR_SIZE];
				servLog(LOG_CLIENTSERVERCOMM, "ErrorTrackerNoData", "IP %s", linkGetIpStr(link, SAFESTR(ip)));
			}
		}
		PERFINFO_AUTO_STOP();
	xcase TO_ERRORTRACKER_DUMP_START:
		PERFINFO_AUTO_START("ET_DumpStart", 1);
		{
			// Find a place to temporarily store this incoming dump data
			DumpData *pDumpData = NULL;

			if(giNumSlaves > 0)
			{
				startSendDumpToSlaves(pak, pClientState->context);
			}

			if (!makeTempFilename(gErrorTrackerSettings.pDumpTempDir, "DMP", &pClientState->pTempOutputFileName))
			{
				estrCopy2(&pClientState->pTempOutputFileName, ""); // make sure it has something
			}


			// Notify errorTrackerLibWaitForDumpConnection()
			gbStopWaitingForDumpConnection = true;

			// Stash the dump flags
			pClientState->uDumpFlags = pktGetU32(pak);

			// Is this a dump coming from some long-lost crash?
			if(pClientState->uDumpFlags & DUMPFLAGS_DEFERRED)
			{
				NOCONST(DumpData) *pData;
				ErrorData *pErrorData = getErrorDataFromPacket(pak); // reads a string from the packet
				NOCONST(ErrorEntry) *pEntry;
				ErrorEntry *pTempMergedEntry;

				if (pktCheckRemaining(pak, sizeof(U32)))
					pClientState->uDumpID = pClientState->uDeferredDumpEntryID = pktGetU32(pak);

				pEntry = createErrorEntryFromErrorData(pErrorData, pClientState->uDeferredDumpEntryID, NULL);
				pTempMergedEntry = pClientState->uDeferredDumpEntryID ? 
					findErrorTrackerByID(pClientState->uDeferredDumpEntryID) : 
					findErrorTrackerEntryFromNewEntry(pEntry);

				StructDestroy(parse_ErrorData, pErrorData); // Don't need it anymore, have the entry now

				if(pTempMergedEntry == NULL || calcRequestedDumpFlags(pTempMergedEntry, CONST_ENTRY(pEntry)) == 0)
				{
					// We already have enough dumps for this ID ... 
					// ask the deferred dump sender to cancel,
					// cleanup our temporary pEntry, and bail out.
					Packet *pkt = pktCreate(link, FROM_ERRORTRACKER_CANCEL_DUMP);
					pktSend(&pkt);
					linkFlush(link);

					if (gbETVerbose) printf("Asking deferred dump from link 0x%p to cancel ...\n", link);
					StructDestroyNoConst(parse_ErrorEntry, pEntry);
					return;
				}

				if (gbETVerbose) printf("Receiving Deferred Dump for ID #%d\n", pTempMergedEntry->uID);

				if (pClientState->uDeferredDumpEntryID && pEntry != UNCONST_ENTRY(pTempMergedEntry) &&
					!pEntry->ppStackTraceLines && pTempMergedEntry->ppStackTraceLines)
				{
					// Copy stack trace lines over to 'new entry' struct from 'merged entry'
					// needed for cases where client sent instruction addresses for the stack
					int i;
					int numLines = eaSize(&pTempMergedEntry->ppStackTraceLines);
					for (i=0; i<numLines; i++)
					{
						NOCONST(StackTraceLine) *pNewST = 0;
						CopyStackTraceLine(&pNewST, pTempMergedEntry->ppStackTraceLines[i]);
						eaPush(&pEntry->ppStackTraceLines, pNewST);
					}
				}

				pData = StructCreateNoConst(parse_DumpData);
				pData->uFlags =  (pClientState->uDumpFlags & DUMPFLAGS_FULLDUMP) ? DUMPDATAFLAGS_FULLDUMP : 0;
				pData->pEntry = pEntry;
				pData->pEntry->uID = pTempMergedEntry->uID;
				pClientState->pDeferredDumpData = pData;
			}
			else
			{
				pClientState->uDumpID    = pktGetU32(pak);

				if (pClientState->uDumpFlags & DUMPFLAGS_EXTERNAL && pktCheckRemaining(pak, 1))
				{
					char *description = pktGetStringTemp(pak);
					if (strlen(description) < ASSERT_DESCRIPTION_MAX_LEN)
						pClientState->pTempDescription = strdup(description);
					else
					{
						pClientState->pTempDescription = calloc(ASSERT_DESCRIPTION_MAX_LEN, sizeof(char));
						strncpy_s(pClientState->pTempDescription, ASSERT_DESCRIPTION_MAX_LEN, description, ASSERT_DESCRIPTION_MAX_LEN - 1);
						pClientState->pTempDescription[ASSERT_DESCRIPTION_MAX_LEN - 1] = 0;
					}
				}
			}

			pDumpData = findDumpData(pClientState->uDumpID);
			if (gMaxIncomingDumpsPerEntry && pDumpData && pDumpData->pEntry && 
				pDumpData->pEntry->eType != ERRORDATATYPE_GAMEBUG && // Don't limit manual dumps
				(getIncomingDumpCount(pDumpData->pEntry->uID) >= gMaxIncomingDumpsPerEntry))
			{
				ErrorEntry *pEntry = findErrorTrackerByID(pDumpData->pEntry->uID);
				if (gbETVerbose) printf("Dropping incoming dump for ET ID #%d, too many concurrent start offers\n", pDumpData->pEntry->uID);
				if (pEntry)
				{
					objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, "setDumpRejected", 
						"set ppDumpData[%d].uFlags = %d", pDumpData->iDumpArrayIndex, pDumpData->uFlags | DUMPDATAFLAGS_REJECTED);
				}
				linkShutdown(&link);
			}
			else
			{
				// ... and open it up for write. Just write it directly out if it is pre-compressed.
				pClientState->pTempOutputFile = fopen(pClientState->pTempOutputFileName, 
					(pClientState->uDumpFlags & DUMPFLAGS_COMPRESSED) ? "wb" : "wbz");
				if(pClientState->pTempOutputFile)
					addActiveDump(pClientState);
				else
				{
					if (gbETVerbose) printf("ERROR: failed to open temp file: %s\n", pClientState->pTempOutputFileName);
				}

				pClientState->iCurrentLocation = 0;
				devassert(pClientState->startTime == 0);
				pClientState->startTime = timerCpuTicks64();

				LogIncomingDumpStart(pDumpData, pClientState, linkGetIp(link));
			}
		}
		PERFINFO_AUTO_STOP();
	xcase TO_ERRORTRACKER_DUMP_CONTINUE:
		PERFINFO_AUTO_START("ET_DumpContinue", 1);
		{
			if (giNumSlaves > 0)
			{
				continueSendDumpToSlaves(pak, pClientState->context);
			}

			if(pClientState->pTempOutputFile)
			{
				int iReadBytes = pktGetU32(pak); // How many bytes in the packet?
				int iSentBytes = pktGetU32(pak); // How many bytes should be in the file now? (sanity check)

				pktGetBytes(pak, iReadBytes, readBuffer);
				fwrite(readBuffer, 1, iReadBytes, pClientState->pTempOutputFile);
				pClientState->iCurrentLocation += iReadBytes;

				if(pClientState->iCurrentLocation != iSentBytes)
				{
					if (gbETVerbose) 
						printf("TO_ERRORTRACKER_DUMP_CONTINUE: Received packet out of order! Something terrible has happened.\n");
					
					LogIncomingDumpError(pClientState, linkGetIp(link), "PacketOutOfOrder");
					removeActiveDump(pClientState);
					DeleteFile_UTF8(pClientState->pTempOutputFileName);
					estrDestroy(&pClientState->pTempOutputFileName);
				}
			}
		}
		PERFINFO_AUTO_STOP();
	xcase TO_ERRORTRACKER_DUMP_FINISH:
		PERFINFO_AUTO_START("ET_DumpFinish", 1);
		{
			int iTotalBytes;
			U32 uDumpID;
			int iTotalUncompressedBytes;
			F32 fTime = 0.0f;
			DumpData *pDumpData = findDumpData(pClientState->uDumpID);

			if (giNumSlaves > 0)
			{
				finishSendDumpToSlaves(pak, pClientState->context);
			}

			iTotalBytes = pktGetU32(pak);
			uDumpID = pktGetU32(pak);
			iTotalUncompressedBytes = iTotalBytes;

			if(pClientState->startTime != 0) 
			{
				fTime = (F32)(timerCpuTicks64() - pClientState->startTime) / (F32)timerCpuSpeed64();
			}

			if(pClientState->uDumpFlags & DUMPFLAGS_COMPRESSED)
			{
				iTotalUncompressedBytes = pktGetU32(pak);
			}

			if(pClientState->pTempOutputFile)
			{
				removeActiveDump(pClientState);

				if(pClientState->iCurrentLocation == iTotalBytes)
				{
					// We've received an entire minidump. Notify via the incoming queue!
					IncomingData *pEntry = NULL;

					if(fTime > 0.0f)
					{
						if (gbETVerbose)
						{
							printf("DP: %db [%db phys.]: %2.2fsec - %2.2fMbits/sec [%2.2fMbits/sec phys.]\n", 
								iTotalUncompressedBytes, iTotalBytes, fTime, 
								(F32)iTotalUncompressedBytes / fTime / (1024.0f*1024.0f/8),
								(F32)iTotalBytes / fTime / (1024.0f*1024.0f/8));
						}
					}

					pClientState->startTime = 0;

					pEntry = StructCreate(parse_IncomingData);
					pEntry->eType         = (pClientState->uDumpFlags & DUMPFLAGS_FULLDUMP) 
						? INCOMINGDATATYPE_FULLDUMP_RECEIVED
						: INCOMINGDATATYPE_MINIDUMP_RECEIVED;
					pEntry->pTempFilename = pClientState->pTempOutputFileName;
					pEntry->id        = uDumpID;

					ProcessIncomingData(pEntry, link, pClientState);

					// We handed this estring off to the IncomingData* object, forget about it
					pClientState->pTempOutputFileName = NULL;
					// TODO cleanup pClientState->pTempDescription ?
				}
				else
				{
					if (gbETVerbose) printf("TO_ERRORTRACKER_DUMP_FINISH: Missed a packet. How is that possible?\n");
					if (fileExists(pClientState->pTempOutputFileName))
						DeleteFile_UTF8(pClientState->pTempOutputFileName);
					estrDestroy(&pClientState->pTempOutputFileName);
				}
			}

			pClientState->startTime = 0;
			LogIncomingDumpFinish(pDumpData, pClientState, iTotalBytes, linkGetIp(link));

		}
		PERFINFO_AUTO_STOP();
	xcase TO_ERRORTRACKER_DUMP_CANCELLED:
		{
			U32 uDumpID;
			IncomingData *pEntry = NULL;
	
			if (giNumSlaves > 0)
			{
				cancelSendDumpToSlaves(pak, linkID(link));
			}

			uDumpID = pktGetU32(pak);
			pClientState->startTime = 0;

			if(pClientState->pTempOutputFile)
			{
				removeActiveDump(pClientState);
				if (fileExists(pClientState->pTempOutputFileName))
					DeleteFile_UTF8(pClientState->pTempOutputFileName);
			}
			estrDestroy(&pClientState->pTempOutputFileName);
			if (gbETVerbose) printf("DP: dump cancelled\n");

			LogIncomingDumpError(pClientState, linkGetIp(link), "Cancelled");
			pEntry = StructCreate(parse_IncomingData);
			pEntry->eType = INCOMINGDATATYPE_DUMP_CANCELLED;
			pEntry->id = uDumpID;
			ProcessIncomingData(pEntry, link, pClientState);
		}
	xcase TO_ERRORTRACKER_MEMORY_DUMP:
		PERFINFO_AUTO_START("ET_MemDump", 1);
		{
			int iTotalBytes             = pktGetU32(pak);
			U32 uErrorID                = pktGetU32(pak);
			char *readData = malloc(iTotalBytes);

			if (readData)
			{
	
				if (!makeTempFilename(gErrorTrackerSettings.pDumpTempDir, "MEM", &pClientState->pTempOutputFileName))
				{
					estrCopy2(&pClientState->pTempOutputFileName, ""); // make sure it has something
				}

				pClientState->pTempOutputFile = fopen(pClientState->pTempOutputFileName, "wb");

				if (pClientState->pTempOutputFile)
				{
					IncomingData *pEntry = NULL;

					pktGetBytes(pak, iTotalBytes, readData);
					fwrite(readData, 1, iTotalBytes, pClientState->pTempOutputFile);
					fclose(pClientState->pTempOutputFile);
					pClientState->pTempOutputFile = NULL;

					pEntry = StructCreate(parse_IncomingData);
					pEntry->eType         = INCOMINGDATATYPE_MEMORYDUMP_RECEIVED;
					pEntry->pTempFilename = pClientState->pTempOutputFileName;
					pEntry->id        = uErrorID;

					ProcessIncomingData(pEntry, link, pClientState);
				}
				free(readData);
			}
			else
				assertmsg(STACK_SPRINTF("Failed to allocate %d bytes for the memory dump", iTotalBytes), 0);
		}
		PERFINFO_AUTO_STOP();
	xcase TO_ERRORTRACKER_MEMORY_DUMP_START:
		PERFINFO_AUTO_START("ET_MemDumpSart", 1);
		{
			// Find a place to temporarily store this incoming dump data
			
			int iTotalBytes = pktGetU32(pak);
			U32 uErrorID = pktGetU32(pak);

			makeTempFilename(gErrorTrackerSettings.pDumpTempDir, "MEM", &pClientState->pTempOutputFileName);
			// ... and open it up for write
			pClientState->pTempOutputFile = fopen(pClientState->pTempOutputFileName, "wb");

			if(pClientState->pTempOutputFile)
				addActiveDump(pClientState);

			pClientState->iCurrentLocation = 0;
		}
		PERFINFO_AUTO_STOP();
	xcase TO_ERRORTRACKER_MEMORY_DUMP_CONTINUE:
		{
			if(pClientState->pTempOutputFile)
			{
				int iReadBytes = pktGetU32(pak); // How many bytes in the packet?
				int iSentBytes = pktGetU32(pak); // How many bytes should be in the file now? (sanity check)

				pktGetBytes(pak, iReadBytes, readBuffer);
				fwrite(readBuffer, 1, iReadBytes, pClientState->pTempOutputFile);
				pClientState->iCurrentLocation += iReadBytes;

				if(pClientState->iCurrentLocation != iSentBytes)
				{
					if (gbETVerbose)
						printf("TO_ERRORTRACKER_DUMP_CONTINUE: Received packet out of order! Something terrible has happened.\n");
					removeActiveDump(pClientState);
					if (fileExists(pClientState->pTempOutputFileName))
						DeleteFile_UTF8(pClientState->pTempOutputFileName);
					estrDestroy(&pClientState->pTempOutputFileName);
				}
			}
	}
	xcase TO_ERRORTRACKER_MEMORY_DUMP_END:
		PERFINFO_AUTO_START("ET_MemDumpFinish", 1);
		{
			int iTotalBytes = pktGetU32(pak);
			U32 uErrorID = pktGetU32(pak);

			if (pClientState->pTempOutputFile)
			{
				IncomingData *pEntry = NULL;
				removeActiveDump(pClientState);

				pEntry = StructCreate(parse_IncomingData);
				pEntry->eType         = INCOMINGDATATYPE_MEMORYDUMP_RECEIVED;
				pEntry->pTempFilename = pClientState->pTempOutputFileName;
				pEntry->id        = uErrorID;
				pClientState->pTempOutputFileName = NULL;

				ProcessIncomingData(pEntry, link, pClientState);
			}
		}
		PERFINFO_AUTO_STOP();
	xcase TO_ERRORTRACKER_DUMP_DESCRIPTION_ONLY:
		{
			U32 uDumpID;
			// Notify errorTrackerLibWaitForDumpConnection()
			gbStopWaitingForDumpConnection = true;
			// Stash the dump flags
			pClientState->uDumpFlags = pktGetU32(pak);

			uDumpID = pktGetU32(pak);
			if (pClientState->uDumpFlags & DUMPFLAGS_EXTERNAL && pktCheckRemaining(pak, 1))
			{
				pClientState->pTempDescription = calloc(ASSERT_EXTERNAL_BUF_SIZE, 1);
				pktGetString(pak, pClientState->pTempDescription, ASSERT_EXTERNAL_BUF_SIZE);
			}
			{
				IncomingData *pEntry = NULL;
				pEntry = StructCreate(parse_IncomingData);
				pEntry->eType         = INCOMINGDATATYPE_DUMP_DESCRIPTION_RECEIVED;
				pEntry->id        = uDumpID;

				ProcessIncomingData(pEntry, link, pClientState);
			}
		}
	xcase TO_ERRORTRACKER_DUMP_DESCRIPTION_UPDATE:
		{
			U32 uID;
			U32 uIndex;

			uID = pktGetU32(pak);
			uIndex = pktGetU32(pak);
			if (pktCheckRemaining(pak, 1))
			{
				if(!pClientState->pTempDescription)
					pClientState->pTempDescription = calloc(ASSERT_EXTERNAL_BUF_SIZE, 1);
				pktGetString(pak, pClientState->pTempDescription, ASSERT_EXTERNAL_BUF_SIZE);
			}
			{
				IncomingData *pEntry = NULL;
				pEntry = StructCreate(parse_IncomingData);
				pEntry->eType = INCOMINGDATATYPE_DUMP_DESCRIPTION_UPDATE;
				pEntry->id    = uID;
				pEntry->index = uIndex;

				ProcessIncomingData(pEntry, link, pClientState);
			}
		}
	xcase TO_ERRORTRACKER_GENERICFILE_START:
		PERFINFO_AUTO_START("ET_GenericFileStart", 1);
		{
			ErrorFileType eFileType = pktGetU32(pak);
			if (ERRORFILETYPE_None < eFileType && eFileType < ERRORFILETYPE_Max)
			{
				pClientState->eFileType = eFileType;
				IncomingGenericFileStart(pak, link, pClientState);
			}
			else
			{
				linkFlushAndClose(&link, "Unknown File Type");
			}
		}
		PERFINFO_AUTO_STOP();
	xcase TO_ERRORTRACKER_GENERICFILE_CONTINUE:
		PERFINFO_AUTO_START("ET_GenericFileContinue", 1);
		if (pClientState->eFileType)
			IncomingGenericFileContinue(pak, link, pClientState);
		else
			linkFlushAndClose(&link, "Unknown File Type");
		PERFINFO_AUTO_STOP();
	xcase TO_ERRORTRACKER_GENERICFILE_END:
		PERFINFO_AUTO_START("ET_GenericFileEnd", 1);
		if (pClientState->eFileType)
			IncomingGenericFileEnd(pak, link, pClientState);
		else
			linkFlushAndClose(&link, "Unknown File Type");
		PERFINFO_AUTO_STOP();
	xcase TO_ERRORTRACKER_NEWSLAVE:
		if (linkGetListenPort(link) != DEFAULT_ERRORTRACKER_PORT)
			CRITICAL_NETOPS_ALERT("INVALID_SLAVE_ERRORTRACKER", "Someone tried to impersonate a slave ErrorTracker on the wrong port!");
		else
		{
			pClientState->isSlaveErrorTracker = true;
			giNumSlaves++;
			eaiPush(&gSlaveErrorTrackerLinks, linkID(link));
			pClientState->uDumpsToSend = pktGetU32(pak);
				
			pClientState->initialIDs = stashTableCreateInt(0);
			pClientState->masterIDtoSlaveIDTable = stashTableCreateInt(0);
			linkSetIsNotTrustworthy(link, true);
		}
	xdefault:
		if (gbETVerbose) printf("IncomingData: Unknown command %d\n",cmd);
	}
}

// ---------------------------------------------------------------------------------

void dumpSendWait(void)
{
	while(giCurrentDumpReceives > 0)
	{
		//errorTrackerLibOncePerFrame(); TODO
		Sleep(1);
	}
}

void disconnectAllDumpsByID(U32 id)
{
	EARRAY_FOREACH_BEGIN(gActiveDumps, i);
	{
		if(gActiveDumps[i]->link)
		{
			DumpData *pDumpData = findDumpData(gActiveDumps[i]->uDumpID);
			if(!id || (pDumpData && pDumpData->pEntry && (pDumpData->pEntry->uID == id)))
			{
				linkShutdown(&gActiveDumps[i]->link);
			}
		}
	}
	EARRAY_FOREACH_END;
}

#include "AutoGen/ETIncomingData_h_ast.c"
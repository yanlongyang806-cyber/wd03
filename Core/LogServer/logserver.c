
/*
 * LogServer
 */
      
#if _MSC_VER < 1600
#if _WIN64
#pragma comment(lib,"../../libs/AttachToDebuggerLib/x64/debug/AttachToDebuggerLibX64.lib")
#else
#pragma comment(lib,"../../libs/AttachToDebuggerLib/debug/AttachToDebuggerLib.lib")
#endif
#else
#if _WIN64
#pragma comment(lib,"../../libs/AttachToDebuggerLib/lib/AttachToDebuggerLibX64_vs10.lib")
#else
#pragma comment(lib,"../../libs/AttachToDebuggerLib/lib/AttachToDebuggerLib_vs10.lib")
#endif
#endif


#include "timing.h"
#include "timing_profiler_interface.h"


#include "logging.h"
#include <stdio.h>
#include <conio.h>
#include "MemoryMonitor.h"

#include "GlobalTypes.h"
#include "FolderCache.h"
#include "sysutil.h"
#include "winutil.h"
#include "serverlib.h"
#include "stashtable.h"
#include "stringcache.h"
#include "utilitieslib.h"
#include "MultiplexedNetLinkList.h"
#include "ControllerLink.h"
#include "Logging_h_ast.h"
#include "estring.h"
#include "file.h"
#include "alerts.h"
#include "ResourceInfo.h"
#include "LogServer_c_ast.h"
#include "svrGlobalInfo.h"
#include "controllerLink.h"
#include "StructNet.h"
#include "svrGlobalInfo_h_ast.h"
#include "LogServer_ClusterComm.h"
#include "logserver.h"
#include "referencesystem.h"
#include "MultiVal.h"

//every WRITEOUT_FREQUENCY seconds, we stop and write out all messages that are WRITEOUT_MIN_AGE seconds old or older. 
//
//WRITEOUT_MIN_AGE must be larger than SECS_PASSED_FORCE_SEND from logcomm.c by a good margin, so that our interleaving will work
#define WRITEOUT_FREQUENCY 3
#define WRITEOUT_MIN_AGE_BASE 15
#define WRITEOUT_MIN_AGE (siExtraWriteoutMinAge + WRITEOUT_MIN_AGE_BASE)

#define MAX_LOGPARSERS 16

#define DUMPSTATS_FREQUENCY 10

#define MAX_CLIENTS 500

//if this is true, then the logserver is running outside a shard. (eg, the Cryptic-wide log server).
bool gbNoShardMode = false; 
AUTO_CMD_INT(gbNoShardMode, NoShardMode) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

NetListen *logServerLinks;
NetListen *logServerForLogParserLinks;

NetLink **sppLogParserLinks = NULL;

// Set when the controller indicates that it is going away; a kill message will be sent to the Log Parser
__int64 gTimeToDie = 0;

// Set when the Log Server has specifically been told to die; this does not kill the Log Parser
static bool gbCloseLogServer = false;

//set this for things like the cluster-level logserver, which might get logs from a wider range of times than an in-shard logserver
//(because the in-shard logservers add an extra delay
static int siExtraWriteoutMinAge = 0;
AUTO_CMD_INT(siExtraWriteoutMinAge, ExtraWriteoutMinAge) ACMD_COMMANDLINE;

//if true, then we know that all the messages we get are already sorted by timestamp, so we can skip all the bucketing
//logic on this side
static bool sbMessagesAreAlreadySortedByTime = false;
AUTO_CMD_INT(sbMessagesAreAlreadySortedByTime, MessagesAreAlreadySortedByTime) ACMD_COMMANDLINE;

LogServerGlobalInfo gGlobalInfo;

AUTO_STRUCT;
typedef struct LogServerCategorySavedStatus
{
	char *pName;
	bool bActive;
} LogServerCategorySavedStatus;

AUTO_STRUCT;
typedef struct LogServerSavedOptions
{
	LogServerCategorySavedStatus **ppCategories;
} LogServerSavedOptions;

static LogServerSavedOptions gSavedOptions = {0};

//so that the cryptic logserver doesn't get out of sync
#define EXTRA_LOG_CATEGORIES 30

void LoadLogServerSavedOptions()
{
	char fileName[CRYPTIC_MAX_PATH];
	enumLogCategory eCategory;
	enumLogCategory uLoadedSize;

	StructDeInit(parse_LogServerSavedOptions, &gSavedOptions);
	StructInit(parse_LogServerSavedOptions, &gSavedOptions);

	sprintf(fileName, "%s/LogServerOptions.txt", fileLocalDataDir());

	if (fileExists(fileName))
	{
		ParserReadTextFile(fileName, parse_LogServerSavedOptions, &gSavedOptions, 0);
	}

	uLoadedSize = eaSize(&gSavedOptions.ppCategories);

	for (eCategory = 0; eCategory < LOG_LAST + EXTRA_LOG_CATEGORIES && eCategory < uLoadedSize; eCategory++)
	{
		gGlobalInfo.ppCategories[eCategory]->bActive = gSavedOptions.ppCategories[eCategory]->bActive;
	}
}

void SaveLogServerSavedOptions()
{
	char fileName[CRYPTIC_MAX_PATH];
	enumLogCategory eCategory;

	sprintf(fileName, "%s/LogServerOptions.txt", fileLocalDataDir());

	StructDeInit(parse_LogServerSavedOptions, &gSavedOptions);
	StructInit(parse_LogServerSavedOptions, &gSavedOptions);

	for (eCategory = 0; eCategory < LOG_LAST + EXTRA_LOG_CATEGORIES; eCategory++)
	{
		LogServerCategorySavedStatus *pCategory = StructCreate(parse_LogServerCategorySavedStatus);
		pCategory->pName = strdup(gGlobalInfo.ppCategories[eCategory]->pName);
		pCategory->bActive = gGlobalInfo.ppCategories[eCategory]->bActive;
		eaPush(&gSavedOptions.ppCategories, pCategory);
	}

	makeDirectoriesForFile(fileName);
	ParserWriteTextFile(fileName, parse_LogServerSavedOptions, &gSavedOptions, 0, 0);
}

extern bool gbZipAllLogs;

AUTO_RUN_FIRST;
void SetUpMyType(void)
{
	gbZipAllLogs = true;
	SetAppGlobalType(GLOBALTYPE_LOGSERVER);

	if(isProductionMode())
	{
		if (sizeof(void*) == 8)
		{
			logSetMsgQueueSize(1024*1024*1024);
		}
		else
		{
			logSetMsgQueueSize(256*1024*1024);
		}
	}
}






					

				
#define MESSAGES_PER_BLOCK 1024

typedef struct MessageBlock
{
	int iNumMessages;
	char *pMessages[MESSAGES_PER_BLOCK];
	struct MessageBlock *pNext;
} MessageBlock;

typedef struct MessageBlockList
{
	U32 iTime;
	int iNumMessages;
	U64 iNumBytes;
	MessageBlock *pFirst;
	MessageBlock *pLast;
} MessageBlockList;

AUTO_STRUCT  AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "name, CurMessages, CurBytes, TotalMessages, TotalBytes, LastContactTime");
typedef struct MessageCategory
{
	char name[256]; AST(KEY)
	//because there's only one list per second and we flush the lists frequently and the messages
	//usually come in sorted, we'll just use a sorted earray along with a pointer to where we last found
	//the list we are looking for
	MessageBlockList **ppLists; NO_AST
	int iLastFoundList; NO_AST
	int iCurMessages;
	U64 iCurBytes; AST(FORMATSTRING(HTML_BYTES=1))
	int iTotalMessages;
	U64 iTotalBytes;AST(FORMATSTRING(HTML_BYTES=1))
	U32 iLastContactTime; AST(FORMATSTRING(HTML_SECS_AGO=1))
} MessageCategory;

MessageCategory gCategories[GLOBALTYPE_MAXTYPES][LOG_LAST + EXTRA_LOG_CATEGORIES] = {0};
bool gTypeIsInUse[GLOBALTYPE_MAXTYPES] = {0};

StashTable sCategoriesByName = {0};

static char *spCategoryNames[LOG_LAST + EXTRA_LOG_CATEGORIES] = {0};

static char *spExtraCategoryNamesSource[EXTRA_LOG_CATEGORIES] = {0};


char *GetLogCategoryName(int iCatNum)
{
	if (!spCategoryNames[iCatNum])
	{
		if (iCatNum >= LOG_LAST)
		{
			char temp[256];
			sprintf(temp, "_UNKNOWN_%d", iCatNum);
			spCategoryNames[iCatNum] = strdup(temp);
		}
		else
		{
			spCategoryNames[iCatNum] = strdup(StaticDefineIntRevLookup(enumLogCategoryEnum, iCatNum));
		}
	}
	return spCategoryNames[iCatNum];

}

void handleCategoryNames(Packet *pak)
{
	char *newCategoryNames[EXTRA_LOG_CATEGORIES] = {0};
	int i = 0;
	char *pName;
	char *pSourceName = "UnknownSource";
	bool overflow = false;

	while (1)
	{
		pName = pktGetStringTemp(pak);
		if (!pName[0])
		{
			break;
		}

		if (i >= LOG_LAST + EXTRA_LOG_CATEGORIES / 2 && i < LOG_LAST + EXTRA_LOG_CATEGORIES)
		{
			static sbAlreadyAlerted = false;

			if (!sbAlreadyAlerted)
			{
				sbAlreadyAlerted = true;
				CRITICAL_NETOPS_ALERT("RUNNING_OUT_OF_EXTRA_LOG_CATEGORIES", "%d out of %d extra log categories have been used up (and you will get no further alerts if more are used)... it's probably about time to push a new version of the log server (this should only ever happen on the cryptic-global log server, not one inside a shard barring some crazy-ass frankenbuild)",
					EXTRA_LOG_CATEGORIES / 2, EXTRA_LOG_CATEGORIES);
			}
		}

		if(i >= LOG_LAST + EXTRA_LOG_CATEGORIES)
		{
			overflow = true;
		}
		else if (i >= LOG_LAST)
		{
			newCategoryNames[i - LOG_LAST] = pName;
		}
		i++;
	}

	if(!pktEnd(pak))
		pSourceName = pktGetStringTemp(pak);

	if(overflow)
	{
		AssertOrAlert("LOG_CAT_OVERFLOW", "Logserver has been given too many extra log categories by %s.", pSourceName);
	}

	for(i = 0; i < EXTRA_LOG_CATEGORIES && newCategoryNames[i]; ++i)
	{
		if (strStartsWith(spCategoryNames[LOG_LAST + i], "_UNKNOWN_"))
		{
			free(spCategoryNames[LOG_LAST + i]);
			spCategoryNames[LOG_LAST + i] = strdup(newCategoryNames[i]);
			spExtraCategoryNamesSource[i] = strdup(pSourceName);
		}
		else
		{
			if (stricmp(spCategoryNames[LOG_LAST + i], newCategoryNames[i]) != 0)
			{
				AssertOrAlert("LOG_CAT_MISMATCH", "Logserver has been given conflicting names for category %d: %s from %s, and %s from %s",
					LOG_LAST + i, spCategoryNames[LOG_LAST + i], spExtraCategoryNamesSource[i], newCategoryNames[i], pSourceName);
			}
		}
	}
}

//NOTE NOTE NOTE if you change the structure of logging packets (which seems like a terrible idea), 
//make sure to change the logMerger tool
void handleLogPrintf(Packet *pak)
{
	GlobalType eSourceContainerType = GetContainerTypeFromPacket(pak);
	U32 iTimeStamp;	
	int iMessageSize;
	int i;

	U32 iCurTime = timeSecondsSince2000();

	assert(eSourceContainerType < GLOBALTYPE_MAXTYPES);

	gTypeIsInUse[eSourceContainerType] = true;

	while((iTimeStamp = pktGetBits(pak, 32)))
	{

		enumLogCategory eCategory = pktGetBits(pak, 32);
		char *pMessageString = pktMallocStringAndGetLen(pak, &iMessageSize);

		{
			static int siCurOldest = 0;
			int iCurAge = iCurTime - iTimeStamp;
			if (iCurAge > siCurOldest)
			{
				printf("Oldest message: %s/%s: %d seconds\n", GlobalTypeToName(eSourceContainerType), 
					StaticDefineInt_FastIntToString(enumLogCategoryEnum, eCategory), iCurAge);
				siCurOldest = iCurAge;
			}
		}


		if (eCategory < 0)
		{
			AssertOrAlert("BAD_LOG_CATEGORY", "Negative log category.");
			free(pMessageString);
		}

		if (eCategory >= LOG_LAST + EXTRA_LOG_CATEGORIES)
		{
			AssertOrAlert("BAD_LOG_CATEGORY", "Unknown log category %d... is the log server way out of synch?", eCategory);
			free(pMessageString);
		}
		else if(!gGlobalInfo.ppCategories[eCategory]->bActive)
		{
#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '*pMessageString'"
			free(pMessageString);
		}
		else
		{
			if (sbMessagesAreAlreadySortedByTime)
			{
				char fileName[256];
				static U32 siLastTime = 0;
				assert(iTimeStamp >= siLastTime);
				siLastTime = iTimeStamp;

				sprintf(fileName, "%s/%s", GlobalTypeToName(eSourceContainerType), GetLogCategoryName(eCategory));
				logDirectWriteWithTime(fileName,pMessageString, iTimeStamp);

				gGlobalInfo.iNumLogsProcessed++;
				gGlobalInfo.iBytesProcessed += iMessageSize;

			}
			else
			{
				MessageCategory *pCategory = &gCategories[eSourceContainerType][eCategory];
				MessageBlockList *pList = NULL;
				int iCurIndex = pCategory->iLastFoundList;

				//if this is the first time logging to this category, prep it
				if (!pCategory->name[0])
				{
					sprintf(pCategory->name, "%s/%s", GlobalTypeToName(eSourceContainerType), 
						GetLogCategoryName(eCategory));
					stashAddPointer(sCategoriesByName, pCategory->name, pCategory, true);
				}


				//look for the list with our current timestamp. Start with the same index we found
				//a list at last time. This should usually be right. Besides, the list will always be short, so
				//just linear search
				for (i=0; i < eaSize(&pCategory->ppLists); i++)
				{
					if (pCategory->ppLists[iCurIndex]->iTime == iTimeStamp)
					{
						pCategory->iLastFoundList = iCurIndex;
						pList = pCategory->ppLists[iCurIndex];
						break;
					}
					iCurIndex++;
					if (iCurIndex == eaSize(&pCategory->ppLists))
					{
						iCurIndex = 0;
					}
				}

				if (!pList)
				{
					pList = calloc(sizeof(MessageBlockList), 1);
					pList->iTime = iTimeStamp;
					pList->pFirst = pList->pLast = calloc(sizeof(MessageBlock), 1);

					if (eaSize(&pCategory->ppLists) == 0 || iTimeStamp > pCategory->ppLists[eaSize(&pCategory->ppLists) - 1]->iTime)
					{
						eaPush(&pCategory->ppLists, pList);
						pCategory->iLastFoundList = eaSize(&pCategory->ppLists) - 1;
					}
					else
					{
						//rare-ish case where we're getting logs in funny time order...
						i = 0;
						while (iTimeStamp > pCategory->ppLists[i]->iTime)
						{
							i++;
						}
						eaInsert(&pCategory->ppLists, pList, i);
						pCategory->iLastFoundList = i;
					}
				}

				if (pList->pLast->iNumMessages == MESSAGES_PER_BLOCK)
				{
					MessageBlock *pNewBlock = calloc(sizeof(MessageBlock), 1);
					pList->pLast->pNext = pNewBlock;
					pList->pLast = pNewBlock;
				}

				assert(pList->pLast->iNumMessages < MESSAGES_PER_BLOCK);
				pList->pLast->pMessages[pList->pLast->iNumMessages++] = pMessageString;

				pList->iNumBytes += iMessageSize;
				pList->iNumMessages++;

				pCategory->iCurBytes += iMessageSize;
				pCategory->iCurMessages++;
				pCategory->iTotalBytes += iMessageSize;
				pCategory->iTotalMessages++;

				pCategory->iLastContactTime = iCurTime;
			
				gGlobalInfo.iNumLogsProcessed++;
				gGlobalInfo.iBytesProcessed += iMessageSize;


				gGlobalInfo.ppCategories[eCategory]->iNumLogsProcessed++;
				gGlobalInfo.ppCategories[eCategory]->iBytesProcessed += iMessageSize;
				gGlobalInfo.ppCategories[eCategory]->iCurLogs++;
				gGlobalInfo.ppCategories[eCategory]->iCurBytes += iMessageSize;
			}


		}
	}
}
	

int LogServerHandleMultiplexMsg(Packet *pak, int cmd, int iIndexOfSender, NetLink *pNetLink, void *userdata)
{

	int ret = 1;

	switch(cmd)
	{
		case LOGCLIENT_LOGPRINTF:
			handleLogPrintf(pak);
		break;
		case LOGCLIENT_HEREARECATEGORYNAMES:
			handleCategoryNames(pak);
		break;
		default:
			filelog_printf("logerrs","Unknown command %d\n",cmd);
			ret = 0;
	}
	return ret;



}

static void consoleCloseHandler(DWORD fdwCtrlType)
{
	printf("Shutting down...\n");
	gbCloseLogServer = true;
}

void OVERRIDE_LATELINK_StatusReporting_ShutdownSafely(void)
{
	printf("Shutdown requested via status monitoring down...\n");
	gbCloseLogServer = true;
}

//something that is monitoring our status has asked us to die... do so cleanly

//if the controller tells the logserver it is dying, the logserver starts dying 2.5 seconds later, to give
//all the other servers that will die when the controller dies time to flush their log sending queues, etc.
static int logServerHandleControllerMsg(Packet *pak,int cmd, NetLink *link,void *userdata)
{
	switch(cmd)
	{

		case FROM_CONTROLLER_KILLYOURSELF:
			printf("Controller is telling us to die");
			gbCloseLogServer = true;
			break;

		case FROM_CONTROLLER_IAMDYING:
			printf("Controller died... setting time to die 2.5 seconds in future\n");
			gTimeToDie = timeMsecsSince2000() + 2500;
			break;
		
	
	}
	return 1;
}


static int LogServerHandleMsg(Packet *pak,int cmd, NetLink *link,void *userdata)
{
	int ret = 1;

	switch(cmd)
	{
/*		xcase LOGCLIENT_SAVELISTS:
			logWaitForQueueToEmpty();
		break;*/
		case LOGCLIENT_LOGPRINTF:
			handleLogPrintf(pak);
		break;
		case LOGCLIENT_HEREARECATEGORYNAMES:
			handleCategoryNames(pak);
		break;
		default:
			filelog_printf("logerrs","Unknown command %d\n",cmd);
			ret = 0;
	}
	return ret;
}

#define LOGPARSER_LINK_FULL_TIMEOUT 5*60
static U32 giPacketLastSendTime;

void doWriteOut(U32 iCurTime, bool bKillLogParser)
{
	int i, j;
	GlobalType eGlobalType;
	enumLogCategory eCategory;
	U32 iWriteCutoffTime = iCurTime - WRITEOUT_MIN_AGE;
	int iNumLinks = eaSize(&sppLogParserLinks);
	Packet *pOutPacks[MAX_LOGPARSERS] = {0};
	char fileName[256] = {0};
	
	for (eGlobalType = 0; eGlobalType < GLOBALTYPE_MAXTYPES; eGlobalType++)
	{
		if (gTypeIsInUse[eGlobalType])
		{
			for (eCategory = 0; eCategory < LOG_LAST + EXTRA_LOG_CATEGORIES; eCategory++)
			{
				MessageCategory *pCategory = &gCategories[eGlobalType][eCategory];

				fileName[0] = 0;

		
				while (eaSize(&pCategory->ppLists) && pCategory->ppLists[0]->iTime <= iWriteCutoffTime)
				{
					MessageBlock *pBlock = pCategory->ppLists[0]->pFirst;

					if (!fileName[0])
					{
						sprintf(fileName, "%s/%s", GlobalTypeToName(eGlobalType), GetLogCategoryName(eCategory));
					}

					while (pBlock)
					{
						MessageBlock *pOldBlock;

#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '*pBlock'"
						for (i=0; i < pBlock->iNumMessages; i++)
						{
							MaybeSendLogToClusterLevelLogServer(eGlobalType, pCategory->ppLists[0]->iTime, eCategory, pBlock->pMessages[i]);

							logDirectWriteWithTime(fileName,pBlock->pMessages[i], pCategory->ppLists[0]->iTime);
							
							for (j=0; j < iNumLinks; j++)
							{
								if (!pOutPacks[j])
								{
									pOutPacks[j] = pktCreate(sppLogParserLinks[j], LOGSERVER_TO_LOGPARSER_HERE_ARE_LOGS);
								}

								pktSendString(pOutPacks[j], fileName);
								pktSendString(pOutPacks[j], pBlock->pMessages[i]);
							}

#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '**pBlock[4][0]'"
							free(pBlock->pMessages[i]);
						}		
							
						pOldBlock = pBlock;
						pBlock = pBlock->pNext;
						free(pOldBlock);
					}

					pCategory->iCurBytes -= pCategory->ppLists[0]->iNumBytes;
					pCategory->iCurMessages -= pCategory->ppLists[0]->iNumMessages;

					gGlobalInfo.ppCategories[eCategory]->iCurBytes -= pCategory->ppLists[0]->iNumBytes;
					gGlobalInfo.ppCategories[eCategory]->iCurLogs -= pCategory->ppLists[0]->iNumMessages;

					free(pCategory->ppLists[0]);
					eaRemove(&pCategory->ppLists, 0);
					pCategory->iLastFoundList = eaSize(&pCategory->ppLists) - 1;
				}


				if (fileName[0])
				{
					logFlushFile(fileName);
				}
			}
		}
	}


	for (i=0; i < iNumLinks; i++)
	{
		if (pOutPacks[i])
		{
			pktSendString(pOutPacks[i], "");

//in production mode, we want to make sure we don't crash because we have outrun the log parser. If we do,
//we can't really afford to slow down, so we just start ignoring logs, but make sure to trigger an alert

			if (isProductionMode())
			{
				if (linkSendBufFull(sppLogParserLinks[i]))
				{
					pktFree(&pOutPacks[i]);

					if(iCurTime > giPacketLastSendTime + LOGPARSER_LINK_FULL_TIMEOUT)
					{
						// kill link
						linkRemove(&sppLogParserLinks[i]);
						eaRemoveFast(&sppLogParserLinks, i);
					}
					else
					{
						TriggerAlert(allocAddString("LOGSERVER_OVERFLOW"), "The Log Parser is not parsing all logs.", 
							ALERTLEVEL_WARNING, ALERTCATEGORY_NETOPS, 120.0f, GetAppGlobalType(), GetAppGlobalID(), GetAppGlobalType(), GetAppGlobalID(), getHostName(), 0);
					}
				}
				else
				{
					giPacketLastSendTime = iCurTime;
					pktSend(&pOutPacks[i]);
				}
			}
			else
			{
				pktSend(&pOutPacks[i]);
			}

			if (bKillLogParser)
			{
				pOutPacks[i] = pktCreate(sppLogParserLinks[i], LOGSERVER_TO_LOGPARSER_ABOUT_TO_DIE);
				pktSend(&pOutPacks[i]);
				linkFlush(sppLogParserLinks[i]);
			}
		}
	}


}


void LogServerLogParser_HandleConnect(NetLink *link,void *pUserData)
{

	eaPush(&sppLogParserLinks, link);

	assertmsgf(eaSize(&sppLogParserLinks) <= MAX_LOGPARSERS, "Too many Log Parser");

}

void LogServerLogParser_HandleDisconnect(NetLink *link,void *pUserData)
{
	eaFindAndRemoveFast(&sppLogParserLinks, link);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void ToggleCategory(char *pCategory)
{
	int eCategory = StaticDefineIntGetInt(enumLogCategoryEnum, pCategory);
	if (eCategory >= 0 && eCategory < LOG_LAST + EXTRA_LOG_CATEGORIES)
	{
		gGlobalInfo.ppCategories[eCategory]->bActive = !gGlobalInfo.ppCategories[eCategory]->bActive;
		SaveLogServerSavedOptions();
	}	
}

void InitLogServerStatus(void)
{
	int i;
	estrPrintf(&gGlobalInfo.pGenericInfo, "<input type=\"hidden\" id=\"defaultautorefresh\" value=\"1\"><a href=\"/viewxpath?xpath=%s[%u].generic\">Generic ServerLib info for the %s</a>",
		GlobalTypeToName(GetAppGlobalType()), gServerLibState.containerID,GlobalTypeToName(GetAppGlobalType()));

	for (i=0; i < LOG_LAST + EXTRA_LOG_CATEGORIES; i++)
	{
		LogServerCategoryStatus *pCategory = StructCreate(parse_LogServerCategoryStatus);
		pCategory->pName = strdup(GetLogCategoryName(i));
		pCategory->bActive = true;
		eaPush(&gGlobalInfo.ppCategories, pCategory);
	}
	LoadLogServerSavedOptions();
}

void dumpStats(U32 iCurTime)
{
	static char *pBytesString = NULL;
	
	static U64 iBytesLastTime = 0;
	static U32 iTimeLastTime = 0;
	static U64 iLogsLastTime = 0;
	static U32 iStallsLastTime = 0;
	U32 iStallsThisTime;
	int iStallsDelta;

	PERFINFO_AUTO_START_FUNC();

	iStallsThisTime = logGetNumStalls();

	if (iTimeLastTime)
	{
		int i;
		U64 curTotalBytes = 0;
		gGlobalInfo.iBytesPerSecond = gGlobalInfo.iBytesProcessed - iBytesLastTime;
		gGlobalInfo.iBytesPerSecond /= iCurTime - iTimeLastTime;
		estrMakePrettyBytesString(&pBytesString, gGlobalInfo.iBytesPerSecond);

		gGlobalInfo.iLogsPerSecond = (gGlobalInfo.iNumLogsProcessed - iLogsLastTime)/(iCurTime - iTimeLastTime);
		printf("Currently processing %"FORM_LL"d messages (%s) per second. ", 
			gGlobalInfo.iLogsPerSecond, pBytesString);

		for (i=0; i < eaSize(&gGlobalInfo.ppCategories); i++)
		{
			curTotalBytes += gGlobalInfo.ppCategories[i]->iCurBytes;
		}

		estrMakePrettyBytesString(&pBytesString, curTotalBytes);

		printf("Foreground RAM: %s. ", pBytesString);
	
		estrMakePrettyBytesString(&pBytesString, logGetNumBytesInMessageQueue());
		printf("Background RAM: %s\n", pBytesString);

	

		
	}

	iStallsDelta = iStallsThisTime - iStallsLastTime;
	estrConcatf(&gGlobalInfo.pStallsPer10SecondPeriod, "%s%u", estrLength(&gGlobalInfo.pStallsPer10SecondPeriod) ? ", " : "",
		iStallsDelta);
	iStallsLastTime = iStallsThisTime;

	if (estrLength(&gGlobalInfo.pStallsPer10SecondPeriod) > 50)
	{
		char *pFirstSpace = strchr(gGlobalInfo.pStallsPer10SecondPeriod, ' ');
		if (pFirstSpace)
		{
			estrRemove(&gGlobalInfo.pStallsPer10SecondPeriod, 0, pFirstSpace - gGlobalInfo.pStallsPer10SecondPeriod + 1);
		}
	}


	iBytesLastTime = gGlobalInfo.iBytesProcessed;
	iTimeLastTime = iCurTime;
	iLogsLastTime = gGlobalInfo.iNumLogsProcessed;
	{
		char *pQueueSize = NULL;
		char *pBytesInQueue = NULL;

		estrStackCreate(&pQueueSize);
		estrStackCreate(&pBytesInQueue);

		estrMakePrettyBytesString(&pQueueSize, logGetQueueSize());
		estrMakePrettyBytesString(&pBytesInQueue, logGetNumBytesInMessageQueue());

		estrPrintf(&gGlobalInfo.pBackgroundQueue, "%s/%s", pBytesInQueue, pQueueSize);

		estrDestroy(&pQueueSize);
		estrDestroy(&pBytesInQueue);
	}



	if (GetControllerLink())
	{
		Packet *pPak;
			
		pktCreateWithCachedTracker(pPak, GetControllerLink(), TO_CONTROLLER_HERE_IS_GLOBAL_LOGSERVER_INFO);
		PutContainerIDIntoPacket(pPak, GetAppGlobalID());
		ParserSendStruct(parse_LogServerGlobalInfo, pPak, &gGlobalInfo);
		pktSend(&pPak);
	}

	PERFINFO_AUTO_STOP();
}



int main(int argc,char **argv)
{
	int		i, frameTimer;

	U32 iNextWriteoutTime;
	U32 iNextStatsTime = 0;
	char *pLogDir;

	EXCEPTION_HANDLER_BEGIN
	WAIT_FOR_DEBUGGER

	DO_AUTO_RUNS
	setDefaultAssertMode();
	memMonitorInit();
	for(i = 0; i < argc; i++){
		printf("%s ", argv[i]);
	}
	printf("\n");

	setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'L', 0x8080ff);

	FolderCacheChooseModeNoPigsInDevelopment();

	preloadDLLs(0);

	printf( "SVN Revision: %d\n", gBuildVersion);

	setConsoleTitle("LogServer");

	
	logSetMaxLogSize(10*1024*1024); // alert for logs over 10 million characters
	logSetDir(GlobalTypeToName(GetAppGlobalType()));
	logEnableHighPerformance();
	logAutoRotateLogFiles(true);
	logSetUseLogTimeForFileRotation(true);
	serverLibStartup(argc, argv);

	setSafeCloseAction(consoleCloseHandler);
	useSafeCloseHandler();
	disableConsoleCloseButton();

	PrepareForMultiplexedNetLinkListMode(LogServerHandleMultiplexMsg, NULL, LogServerHandleMsg, NULL);

	sCategoriesByName = stashTableCreateWithStringKeys(128, StashDefault);
	resRegisterDictionaryForStashTable("LogCategories", RESCATEGORY_OTHER, 0, sCategoriesByName, parse_MessageCategory);


	loadstart_printf("Opening main listens");
	for(;;)
	{
		logServerLinks = commListen(commDefault(),LINKTYPE_SHARD_CRITICAL_1MEG, LINK_FORCE_FLUSH,DEFAULT_LOGSERVER_PORT,MultiplexedNetLinkList_Wrapper_HandleMsg,
			NULL,MultiplexedNetLinkList_Wrapper_ClientDisconnect,0);
		if (logServerLinks)
			break;
		Sleep(DEFAULT_SERVER_SLEEP_TIME);
	}
	loadend_printf("done");

	loadstart_printf("Opening listens for logParser");
	for (;;)
	{
		logServerForLogParserLinks = commListen(commDefault(),LINKTYPE_SHARD_CRITICAL_20MEG, LINK_FORCE_FLUSH, LOGSERVER_LOGPARSER_PORT, NULL, LogServerLogParser_HandleConnect,
			LogServerLogParser_HandleDisconnect, 0);
		if (logServerForLogParserLinks)
			break;
		Sleep(DEFAULT_SERVER_SLEEP_TIME);
	}
	loadend_printf("done");

	if (gbNoShardMode)
	{
		ServerLibSetControllerHost("NONE");
	}
	else
	{
		AttemptToConnectToController(false, logServerHandleControllerMsg, true);
	}

//	sMessagesTable = stashTableCreate(128, StashDefault, StashKeyTypeStrings, 0 );

	DirectlyInformControllerOfState("ready");

	iNextWriteoutTime = timeSecondsSince2000() + WRITEOUT_FREQUENCY;
	frameTimer = timerAlloc();


	InitLogServerStatus();

	pLogDir = logGetDir();

	//if we're logging on the same drive as we're being executed from, then the launcher will take care of checking free disk space
	if (isProductionMode() && fileIsAbsolutePath(pLogDir) && isalpha(pLogDir[0]) && pLogDir[1] == ':' && toupper(pLogDir[0]) != toupper(getExecutableName()[0]))
	{
		BeginPeriodicFreeDiskSpaceCheck(pLogDir[0], 60, 3, 600);
	}

	for(;;)
	{
		U32 iCurTime;
		F32 frametime;
		
		autoTimerThreadFrameBegin(__FUNCTION__);
		
		frametime = timerElapsedAndStart(frameTimer);
		utilitiesLibOncePerFrame(frametime, 1);

		//Sleep(DEFAULT_SERVER_SLEEP_TIME);
		commMonitor(commDefault());
		
		serverLibOncePerFrame();

		LogServer_ClusterTick();

		iCurTime = timeSecondsSince2000();


		if (iCurTime > iNextStatsTime)
		{
			dumpStats(iCurTime);
			iNextStatsTime = iCurTime + DUMPSTATS_FREQUENCY;
		}

		if (iCurTime > iNextWriteoutTime)
		{
			iNextWriteoutTime = iCurTime + WRITEOUT_FREQUENCY;
			
			PERFINFO_AUTO_START("doWriteOut", 1);
			doWriteOut(iCurTime, false);
			PERFINFO_AUTO_STOP();
		}


		// Exit if the we've been asked to or the controller is dying
		if (gTimeToDie && timeMsecsSince2000() > gTimeToDie)
		{
			PERFINFO_AUTO_START("doWriteOut", 1);
			printf("time to die\n");
			doWriteOut(iCurTime, true);
			PERFINFO_AUTO_STOP();

			exit(0);
		}
		if (gbCloseLogServer)
		{
			printf("dying immediately\n");
			doWriteOut(iCurTime, false);

			exit(0);
		}
		
		PERFINFO_AUTO_START("updateTitle", 1);
		{
			char consoleTitle[256];
			
			sprintf(consoleTitle, "LogServer. %"FORM_LL"u logs. %"FORM_LL"u bytes.",
				gGlobalInfo.iNumLogsProcessed, gGlobalInfo.iBytesProcessed);

			setConsoleTitle(consoleTitle);
		}
		PERFINFO_AUTO_STOP();
		
		autoTimerThreadFrameEnd();

	}
	EXCEPTION_HANDLER_END
}


void OVERRIDE_LATELINK_GetCustomServerInfoStructForHttp(UrlArgumentList *pUrl, ParseTable **ppTPI, void **ppStruct)
{
	*ppTPI = parse_LogServerGlobalInfo;
	*ppStruct = &gGlobalInfo;
}

void OVERRIDE_LATELINK_LoggingResizeHappened(int iBlockSize, int iOldSize, int iNewSize, int iMaxSize)
{
	CRITICAL_NETOPS_ALERT("LOGSERVER_RESIZE", "The logserver's message queue has resized (from %d blocks of %d bytes to %d blocks, max %d blocks). This is VERY BAD and almost certainly means the log server can not keep up with logs as fast as they come in",
		iOldSize, iBlockSize, iNewSize, iMaxSize);
}

//IGNORE ALL THIS it is test code Alex W is using for the optimzied text diff writing
#if 0


#include "crypt.h"
#include "rand.h"

typedef struct DiffTestStruct DiffTestStruct;
	
AUTO_STRUCT;
typedef struct TestThingInRefDict
{
	int x;
} TestThingInRefDict;

AUTO_STRUCT;
typedef struct DiffTestStruct
{
	char *pName; AST(ESTRING KEY)

	int iTestInt;
	U8 iTestU8;
	S16 iTestS16;
	S64 iTestS64;
	int iTestBits1:4;
	MultiVal testMultiVal;
	bool bTestBool;
	REF_TO(TestThingInRefDict) testRef;
		
	char *pTestStr1; AST(ESTRING)
	char *pTestStr2; AST(ESTRING)
	char *pTestStr3; AST(ESTRING)
	char *pTestStr4; AST(ESTRING)
	float testFloat1;
	float testFloat2;
	DiffTestStruct *pOptionalChild;
	DiffTestStruct **ppChildArray1;
	DiffTestStruct **ppChildArray2;
	DiffTestStruct **ppChildArray3;
	DiffTestStruct **ppChildArray4;	
} DiffTestStruct;

void RandomlySetOneField(DiffTestStruct *pStruct, int iMaxDepth);

void RandomizeString(char **ppEstring)
{
	int iLen = randomIntRange(10, 200);
	int i;
	estrSetSize(ppEstring, iLen);
	for (i = 0; i < iLen; i++)
	{
		(*ppEstring)[i] = randomIntRange('a', 'z');
	}
}

void RandomizeFloat(float *pFloat)
{
	*pFloat = ((float)(randomIntRange(-100000,1000000))) / 10.0f;
}

void RandomlySetNFields(DiffTestStruct *pStruct, int n, int iMaxDepth)
{
	int i;

	for (i = 0; i < n; i++)
	{
		RandomlySetOneField(pStruct, iMaxDepth);
	}
}

DiffTestStruct *RandomlyCreateStruct(int iMaxDepth)
{
	static int siNextIndex = 1;
	DiffTestStruct *pStruct = StructCreate(parse_DiffTestStruct);
	U32 iKey;

	iKey = cryptAdler32((U8*)(&siNextIndex), sizeof(int));
	siNextIndex++;

	estrPrintf(&pStruct->pName, "Key%u", iKey);
	RandomlySetNFields(pStruct, 8, iMaxDepth);
	return pStruct;
}

void RandomizeOptionalStruct(DiffTestStruct **ppStruct, int iMaxDepth)
{
	DiffTestStruct *pStruct = *ppStruct;

	if (pStruct)
	{
		if (randomIntRange(0,10) > 8)
		{
			StructDestroy(parse_DiffTestStruct, pStruct);
			*ppStruct = NULL;
		}
		else
		{
			RandomlySetOneField(pStruct, iMaxDepth - 1);
		}
	}
	else
	{
		*ppStruct = RandomlyCreateStruct(iMaxDepth - 1);
	}

}

void RandomizeEarray(DiffTestStruct ***pppEarray, int iMaxDepth)
{
	int iSize = eaSize(pppEarray);
	int iDecider;

	if (iSize < 2 )
	{
		eaPush(pppEarray, RandomlyCreateStruct(iMaxDepth - 1));
		return;
	}

	if (iSize > 100)
	{
		StructDestroy(parse_DiffTestStruct, eaRemove(pppEarray,randomIntRange(0,100)));
		return;
	}

	iDecider = randomIntRange(0,10);

	if (iDecider == 0)
	{
		eaPush(pppEarray, RandomlyCreateStruct(iMaxDepth - 1));
		return;
	}

	if (iDecider == 1)
	{
		StructDestroy(parse_DiffTestStruct, eaRemove(pppEarray, randomIntRange(0,iSize - 1)));
		return;
	}

	RandomlySetOneField((*pppEarray)[randomIntRange(0,iSize - 1)], iMaxDepth - 1);
}

void RandomlySetOneField(DiffTestStruct *pStruct, int iMaxDepth)
{
	int iIndex = randomIntRange(0,18);

	switch (iIndex)
	{
	xcase 0:
		pStruct->iTestInt = randomIntRange(-2000000000, 2000000000);
	xcase 1:
		pStruct->iTestU8 = randomIntRange(0, 200);
	xcase 2:
		pStruct->iTestS16 = randomIntRange(-20000, 20000);
	xcase 3:
		pStruct->iTestS64 = ((S64)randomIntRange(-2000000000, 2000000000)) *  ((S64)randomIntRange(-2000000000, 2000000000));
	xcase 4:
		pStruct->iTestBits1 = randomIntRange(0,15);
	xcase 5:
		MultiValSetInt(&pStruct->testMultiVal, randomIntRange(-20000, 20000));
	xcase 6:
		pStruct->bTestBool = randomIntRange(0,1);
	xcase 7:
		{
			int iInner = randomIntRange(0,7);
			switch(iInner)
			{
			xcase 0:
				SET_HANDLE_FROM_REFDATA("TestDict", "Happy", pStruct->testRef);
			xcase 1:
				SET_HANDLE_FROM_REFDATA("TestDict", "Whappy", pStruct->testRef);
			xcase 2:
				SET_HANDLE_FROM_REFDATA("TestDict", "Crappy", pStruct->testRef);
			xcase 3:
				SET_HANDLE_FROM_REFDATA("TestDict", "Supersad", pStruct->testRef);
			xcase 4:
				SET_HANDLE_FROM_REFDATA("TestDict", "Zany", pStruct->testRef);
			xcase 5:
				SET_HANDLE_FROM_REFDATA("TestDict", "Unctuous", pStruct->testRef);
			xcase 6:
				SET_HANDLE_FROM_REFDATA("TestDict", "Doobie", pStruct->testRef);
			xcase 7:
				SET_HANDLE_FROM_REFDATA("TestDict", "Hippie", pStruct->testRef);
			}
		}
	xcase 8:
		RandomizeString(&pStruct->pTestStr1);
	xcase 9:
		RandomizeString(&pStruct->pTestStr2);
	xcase 10:
		RandomizeString(&pStruct->pTestStr3);
	xcase 11:
		RandomizeString(&pStruct->pTestStr4);
	xcase 12:
		RandomizeFloat(&pStruct->testFloat1);
	xcase 13:
		RandomizeFloat(&pStruct->testFloat2);
	xcase 14:
		if (iMaxDepth > 0)
		{
			RandomizeOptionalStruct(&pStruct->pOptionalChild, iMaxDepth);
		}
	xcase 15:
		if (iMaxDepth > 0)
		{
			RandomizeEarray(&pStruct->ppChildArray1, iMaxDepth);
		}
	xcase 16:
		if (iMaxDepth > 0)
		{
			RandomizeEarray(&pStruct->ppChildArray2, iMaxDepth);
		}
	xcase 17:		
		if (iMaxDepth > 0)
		{
			RandomizeEarray(&pStruct->ppChildArray3, iMaxDepth);
		}
	xcase 18:
		if (iMaxDepth > 0)
		{
			RandomizeEarray(&pStruct->ppChildArray4, iMaxDepth);
		}
	}
}

DiffTestStruct *MakeBigArrayTest(int iNumPerArray, int iDepth)
{
	DiffTestStruct *pRetVal = StructCreate(parse_DiffTestStruct);
	int i;
	static int siNextID = 1;
	estrPrintf(&pRetVal->pName, "%d", siNextID++);
	

	if (iDepth == 0)
	{
		return pRetVal;
	}

	for (i = 0; i < iNumPerArray; i++)
	{
		eaPush(&pRetVal->ppChildArray1, MakeBigArrayTest(iNumPerArray, iDepth - 1));
	}

	return pRetVal;
}



#define NUM_TO_TEST 40000


AUTO_RUN;
void DiffTest(void)
{
	DiffTestStruct **ppStartingStructs = NULL;
	DiffTestStruct **ppMungedStructs = NULL;
	int i;
	char *pEstr1 = NULL;
	char *pEstr2 = NULL;

	loadstart_printf("Randomizing %d structs...", NUM_TO_TEST);

	RefSystem_RegisterSelfDefiningDictionary("TestDict", false, parse_TestThingInRefDict, false, false, NULL);
	

	for (i = 0; i < NUM_TO_TEST; i++)
	{
		if (i % 1000 == 0)
		{
			printf("%d\n", i);
		}
		eaPush(&ppStartingStructs, RandomlyCreateStruct(6));
	}

	loadend_printf("Done");

	loadstart_printf("Munging %d structs...", NUM_TO_TEST);

	for (i = 0; i < NUM_TO_TEST; i++)
	{
		DiffTestStruct *pMunged = StructClone(parse_DiffTestStruct, ppStartingStructs[i]);
		RandomlySetNFields(pMunged, randomIntRange(1,6), 6);
		eaPush(&ppMungedStructs, pMunged);
		if (i % 1000 == 0)
		{
			printf("%d\n", i);
		}
	}

	loadend_printf("done");

	loadstart_printf("Verifying that old and new diffs produce identical strings");

	for (i = 0; i < NUM_TO_TEST; i++)
	{
		StructWriteTextDiff(&pEstr1, parse_DiffTestStruct, NULL, ppMungedStructs[i], "Foo", 0, 0, 0);
		StructTextDiffWithNull(&pEstr2, parse_DiffTestStruct, ppMungedStructs[i], "Foo", (int)strlen("Foo"), 0, 0, 0);

		assert(stricmp(pEstr1, pEstr2) == 0);

		estrClear(&pEstr1);
		estrClear(&pEstr2);

	}

	loadend_printf("done");

	while (1)
	{


		loadstart_printf("Old-style Diffing %d structs against NULL...", NUM_TO_TEST);

		for (i = 0; i < NUM_TO_TEST; i++)
		{
			StructWriteTextDiff(&pEstr1, parse_DiffTestStruct, NULL, ppMungedStructs[i], "Foo", 0, 0, 0);
			estrClear(&pEstr1);
	

		}

		loadend_printf("done");

		


		loadstart_printf("new-style Diffing %d structs against NULL...", NUM_TO_TEST);

		for (i = 0; i < NUM_TO_TEST; i++)
		{
			StructTextDiffWithNull(&pEstr2, parse_DiffTestStruct, ppMungedStructs[i], "Foo", (int)strlen("Foo"), 0, 0, 0);
			estrClear(&pEstr2);
		}

		loadend_printf("done");

	}



}
#endif


#include "LogServer_c_ast.c"


#include "estring.h"
#include "sysUtil.h"
#include "sock.h"
#include "GlobalTypes.h"
#include "earray.h"
#include "HttpLib.h"
#include "net.h"
#include "LogParser.h"
#include "timing.h"
#include "logging.h"
#include "LogParserUtils.h"

extern int siStartingPortNum;

typedef enum LogParser_Link_Commands
{
	STANDALONE_TO_LOGPARSER_HI,
	STANDALONE_TO_LOGPARSER_ACK_SHUTDOWN,
	LOGPARSER_TO_STANDALONE_SHUTDOWN,
};

NetLink *LiveLogParserLink;

typedef struct StandAloneLogParserData
{
	NetLink *pLink;
	bool bLaunched;
	U32 uLaunchTime;
	int iUID; //only specified in rare situations such as when LogParserFrontEndFiltering is being used
	QueryableProcessHandle *pHandle;
} StandAloneLogParserData;

static StandAloneLogParserData *spData = NULL;

NetListen *StandAloneLogParserLinks;

static U32 giNumLaunchedLogParsers = 0;
static char LogDirectoryForLaunched[CRYPTIC_MAX_PATH];

static U32 giLaunchingInitTime = 0;

bool gbConnectToLiveLogParser = false;
AUTO_CMD_INT(gbConnectToLiveLogParser, ConnectToLiveLogParser) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

AUTO_COMMAND;
void EnableLaunching(U32 iMax, char *pDirectory)
{
	if(!giNumLaunchedLogParsers)
	{
		giNumLaunchedLogParsers = iMax;
		strcpy(LogDirectoryForLaunched, pDirectory);
	}
}


void LogParserToStandAlone_HandleConnect(NetLink *link,void *pUserData)
{
}

void LogParserToStandAlone_LinkCallback(Packet *pkt, int cmd, NetLink *link, void *userdata)
{
	U32 index;
	switch (cmd)
	{
		xcase STANDALONE_TO_LOGPARSER_HI:
		{
			index = pktGetU32(pkt);
			if(index >= giNumLaunchedLogParsers)
			{
				ErrorOrAlert("LOGPARSER_STANDALONE_CONNECT", "Someone managed to launch a standalone LogParser on port %u, which is not in the allowed range", index + STARTING_LOGPARSER_PORT);
			}
			else if(spData[index].pLink)
			{
				ErrorOrAlert("LOGPARSER_STANDALONE_CONNECT", "Someone managed to launch two standalones on port %u", index + STARTING_LOGPARSER_PORT);
			}
			else
			{
				spData[index].pLink = link;
				spData[index].bLaunched = true;
			}
		}
		xcase STANDALONE_TO_LOGPARSER_ACK_SHUTDOWN:
		{
			index = pktGetU32(pkt);
		}
	}
}

void LogParserToStandAlone_HandleDisconnect(NetLink *link,void *pUserData)
{
	U32 i;
	for(i = 0; i < giNumLaunchedLogParsers; ++i)
	{
		if(spData[i].pLink == link)
		{
			spData[i].pLink = NULL;
			spData[i].bLaunched = false;
			spData[i].iUID = 0;
		}
	}
}

void StandAloneToLogParser_LinkCallback(Packet *pak, int cmd, NetLink *link, void *userdata)
{
	switch (cmd)
	{
		xcase LOGPARSER_TO_STANDALONE_SHUTDOWN:
		{
			char *optionFileName = NULL;
			Packet *pPkt = pktCreate(link, STANDALONE_TO_LOGPARSER_ACK_SHUTDOWN);
			pktSendU32(pPkt, siStartingPortNum - STARTING_LOGPARSER_PORT);
			pktSend(&pPkt);

			gbStandAloneForceExit = true;
		}
	}
}

void StandAloneToLogParser_Connect(NetLink *link,void *pUserData)
{
	Packet *pPkt = pktCreate(link, STANDALONE_TO_LOGPARSER_HI);
	pktSendU32(pPkt, siStartingPortNum - STARTING_LOGPARSER_PORT);
	pktSend(&pPkt);
}

void StandAloneToLogParser_Disconnect(NetLink *link,void *pUserData)
{
}

void InitStandAloneListening()
{
	if(StandAloneLogParserLinks)
		return;

	for (;;)
	{
		StandAloneLogParserLinks = commListen(commDefault(),LINKTYPE_SHARD_NONCRITICAL_500K, LINK_FORCE_FLUSH, LIVELOGPARSER_LISTEN_PORT, LogParserToStandAlone_LinkCallback, LogParserToStandAlone_HandleConnect,
			LogParserToStandAlone_HandleDisconnect, 0);
		if (StandAloneLogParserLinks)
			break;
		Sleep(DEFAULT_SERVER_SLEEP_TIME);
	}
}

void UpdateLinkToLiveLogParser()
{
	static U32 iLastConnectTime;
	static U32 iNextSendFPSTime;
	U32 iCurTime;

	if(!gbConnectToLiveLogParser)
		return;

	iCurTime = timeSecondsSince2000();

	if (!LiveLogParserLink)
	{
		LiveLogParserLink = commConnectIP(commDefault(),LINKTYPE_UNSPEC, LINK_FORCE_FLUSH,getHostLocalIp(),LIVELOGPARSER_LISTEN_PORT,StandAloneToLogParser_LinkCallback,StandAloneToLogParser_Connect,StandAloneToLogParser_Disconnect,0);
		iLastConnectTime = iCurTime;

		return;
	}

	//after 5 seconds, if we tried to connect but have not succeeded, start over
	if ((!linkConnected(LiveLogParserLink) || linkDisconnected(LiveLogParserLink)) && iCurTime > iLastConnectTime + 5)
	{
		linkRemove_wReason(&LiveLogParserLink, "Timed out or disconnected in UpdateLinkToLogServer");
	}
}

void LogParserLaunching_InitSystem(void)
{
	if(!giLaunchingInitTime)
	{
		giLaunchingInitTime = timeSecondsSince2000();
	}

	InitStandAloneListening();
	if (giNumLaunchedLogParsers)
	{
		if (!spData)
		{
			spData = calloc(giNumLaunchedLogParsers * sizeof(StandAloneLogParserData), 1);
		}
	}
}

#define STANDALONE_LOGPARSER_TIMEOUT 60

void LogParserLaunching_PeriodicUpdate(void)
{
	U32 i;
	U32 uCurrentTime = timeSecondsSince2000();

	if (!giNumLaunchedLogParsers)
	{
		return;
	}

	LogParserLaunching_InitSystem();

	for(i = 0; i < giNumLaunchedLogParsers; ++i)
	{
		if(spData[i].bLaunched && !spData[i].pLink)
		{
			if(uCurrentTime > spData[i].uLaunchTime + STANDALONE_LOGPARSER_TIMEOUT)
			{
				spData[i].bLaunched = false;
				spData[i].iUID = 0;
			}
		}
	}
}

static U32 LogParserLaunching_LaunchInternal(int iTimeOut, const char *pExtraArgs, U32 index, int iUID)
{
	char *pCommandLine = NULL;
	
	assert(index < giNumLaunchedLogParsers);

	estrPrintf(&pCommandLine, "%s -ConnectToLiveLogParser -StartingPortNum %d -SetDirectoriesToScan_Precise %s -InactivityTimeOut %d -SetProductName %s %s - %s", getExecutableName(),
		STARTING_LOGPARSER_PORT + index, LogDirectoryForLaunched, iTimeOut, GetProductName(), GetShortProductName(), pExtraArgs ? pExtraArgs : "");
	EARRAY_CONST_FOREACH_BEGIN(gppExtraLogParserConfigFiles, j, n);
	{
		estrConcatf(&pCommandLine, " -ExtraLogParserConfigFile \"%s\"", gppExtraLogParserConfigFiles[j]);
	}
	EARRAY_FOREACH_END;

	if (spData[index].pHandle)
	{
		KillQueryableProcess(&spData[index].pHandle);
	}

	LogParser_AddClusterStuffToStandAloneCommandLine(&pCommandLine);

	spData[index].pHandle = StartQueryableProcess(pCommandLine, NULL, false, false, false, NULL);

	if (!spData[index].pHandle)
	{
		return 0;
	}

	spData[index].bLaunched = true;
	spData[index].uLaunchTime = timeSecondsSince2000();
	spData[index].iUID = iUID;

	return STARTING_LOGPARSER_PORT + index;
}

U32 LogParserLaunching_LaunchOne(int iTimeOut, const char *pExtraArgs, U32 iForcedPort, int iUID)
{
	U32 i;
	if (iForcedPort && giNumLaunchedLogParsers == 0)
	{
		giNumLaunchedLogParsers = 1;
	}

	LogParserLaunching_InitSystem();

	if(iForcedPort)
	{
		U32 index = iForcedPort - STARTING_LOGPARSER_PORT;
		if(index >= giNumLaunchedLogParsers)
		{
			ErrorOrAlert("LOGPARSER_STANDALONE_PORT_OUT_OF_RANGE", "Trying to launch a standalone LogParser on port %u, which is not in the allowed range.", iForcedPort);
			return 0;
		}
		else if(!spData[index].pLink && !spData[index].bLaunched)
		{
			return LogParserLaunching_LaunchInternal(iTimeOut, pExtraArgs, index, iUID);
		}
	}
	else
	{
		for (i=0; i < giNumLaunchedLogParsers; i++)
		{
			if(!spData[i].pLink && !spData[i].bLaunched)
			{
				return LogParserLaunching_LaunchInternal(iTimeOut, pExtraArgs, i, iUID);
			}
		}
	}

	return 0;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
char *LaunchStandaloneAndReturnLink(char *pUserName)
{
	static char tempLink[1024];
	U32 iPort;
	U32 iCurrentTime = timeSecondsSince2000();

	LogParserLaunching_InitSystem();

	if(iCurrentTime < giLaunchingInitTime + STANDALONE_LOGPARSER_TIMEOUT)
	{
		sprintf(tempLink, "Waiting for previous standalone LogParser to reconnect. (%u seconds left)", giLaunchingInitTime + STANDALONE_LOGPARSER_TIMEOUT - iCurrentTime);
		return tempLink;
	}

	iPort = LogParserLaunching_LaunchOne(120, NULL, 0, 0);

	if (!iPort)
	{
		sprintf(tempLink, "Couldn't launch any more standalone logparsers... %d are already running", giNumLaunchedLogParsers);
		return tempLink;
	}

	sprintf(tempLink, "<a href=\"http://%s:%d\" target=\"_blank\">Local IP link to Stand-Alone LogParser</a> <a href=\"http://%s:%d\" target=\"_blank\">Public IP link to Stand-Alone LogParser</a>", makeIpStr(getHostLocalIp()), iPort, makeIpStr(getHostPublicIp()), iPort);
	log_printf(LOG_SERVERMONITOR, "%s launched a standalone LogParser at %s:%u", pUserName, makeIpStr(getHostLocalIp()), iPort);

	return tempLink;
}

AUTO_COMMAND;
void LaunchNoTimeoutStandAloneSpecifyPort(int iPortNum)
{
	LogParserLaunching_LaunchOne(0, NULL, iPortNum, 0);
}

bool LogParserLaunching_IsActive(void)
{
	return giNumLaunchedLogParsers != 0;
}

int FillStandAloneList(STRING_EARRAY *pArray)
{
	U32 i;
	int count = 0;
	char *pLocation = NULL;

	if (!giNumLaunchedLogParsers)
	{
		return 0;
	}

	LogParserLaunching_InitSystem();

	for (i=0; i < giNumLaunchedLogParsers; i++)
	{
		if(spData[i].pLink)
		{
			estrCreate(&pLocation);
			estrPrintf(&pLocation, "%s:%d", makeIpStr(getHostLocalIp()), STARTING_LOGPARSER_PORT + i);
			eaPush(pArray, pLocation);
			++count;
		}
	}
	return count;
}

void LogParserShutdownStandAlones()
{
	U32 i;
	U32 timeStarted = timeSecondsSince2000();

	for(i = 0; i < giNumLaunchedLogParsers; ++i)
	{
		if(spData[i].pLink && linkConnected(spData[i].pLink))
		{
			Packet *pPkt = pktCreate(spData[i].pLink, LOGPARSER_TO_STANDALONE_SHUTDOWN);
			pktSend(&pPkt);
		}
	}

	for(;;)
	{
		U32 loopTime = timeSecondsSince2000();
		bool stillConnected = false;
		commMonitor(commDefault());
		for(i = 0; i < giNumLaunchedLogParsers; ++i)
		{
			if(spData[i].pLink && linkConnected(spData[i].pLink))
			{
				stillConnected = true;
			}
		}
		if(!stillConnected)
			break;
		if(loopTime > timeStarted + 60)
		{
			ErrorOrAlert("LOGPARSER_SHUTDOWN", "Some standalone LogParsers have not killed themselves after one minute.");
			break;
		}
	}
	
}

void LogParserLaunching_KillByUID(int iUID)
{
	U32 i;

	if (!iUID)
	{
		return;
	}

	for(i = 0; i < giNumLaunchedLogParsers; ++i)
	{
		if(spData[i].iUID == iUID && spData[i].pHandle)
		{
			KillQueryableProcess(&spData[i].pHandle);
			spData[i].iUID = 0;
			spData[i].bLaunched = false;
		}
	}
}
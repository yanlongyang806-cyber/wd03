/***************************************************************************



***************************************************************************/

#include "GlobalTypes.h"
#include "logcomm.h"
#include "estring.h"
#include "timing.h"
#include "LinkToMultiplexer.h"
#include "ServerLib.h"
#include "sock.h"
#include "file.h"
#include "Logging_h_ast.h"
#include "StringCache.h"
#include "alerts.h"
#include "ControllerLink.h"
#include "utilitiesLib.h"
#include "Alerts.h"
#include "StringUtil.h"
#include "ThreadSafeMemoryPool.h"


//last-ditch-failure-case... if we have too many bytes of messages stored up and no
//log server, write some locally
#define MESSAGE_SIZE_WRITE_LOCALLY 20000000


#define MESSAGE_SIZE_FORCE_SEND 200000
#define SECS_PASSED_FORCE_SEND 10

static CRITICAL_SECTION sMessageForLogServerCS;


typedef struct 
{
	U32 iTime; //secs since 2000
	enumLogCategory eCategory;
	char *pMessage; //estring
} MessageForLogServer; 


typedef struct LogServerConnectionInfo
{
	char *pConnectionName;
	char *pServerName;
	LinkToMultiplexer *pLinkToMultiplexerForLogging;
	bool bLinkToMultiplexerForLoggingDisconnected;
	U32 iLastTimeAttemptedMultiplexConnection;

	U32 iLastSendTime;
	U32 iTotalMessageLength;
	U32 iNextLocalWriteAlertTime;
	NetLink *pLink;
	MessageForLogServer **ppMessages;
	CommConnectFSM *pLogConnectFSM;


} LogServerConnectionInfo;

S16 siConnectionIndicesByCategory[LOG_LAST] = {0};

static LogServerConnectionInfo **sppConnections = NULL;

static StringArray ssaAdditionalLogServerCommandlines = NULL;

//if true, keep retrying forever
bool gbMustConnectToLogServer = false;

static U32 siLocalWriteAlertThrottle = 0; //if set, then alert every num seconds when writing locally instead of to log server
AUTO_CMD_INT(siLocalWriteAlertThrottle, LocalWriteAlertThrottle);

static bool sbAlertOnLogServerReconnect = false;
AUTO_CMD_INT(sbAlertOnLogServerReconnect, AlertOnLogServerReconnect);

static bool sbAlertOnLogServerDisconnect = false;
AUTO_CMD_INT(sbAlertOnLogServerDisconnect, AlertOnLogServerDisconnect);

static bool sbSystemIsShuttingDown = false;

void svrLogSetSystemIsShuttingDown(void)
{
	sbSystemIsShuttingDown = true;
}



AUTO_CMD_INT(gbMustConnectToLogServer, MustConnectToLogServer);



NetComm	*log_comm;


void LazyInitConnections(void)
{
	//always want to reserve index zero for the "main" connection
	if (!eaSize(&sppConnections))
	{
		eaPush(&sppConnections, calloc(sizeof(LogServerConnectionInfo), 1));
	}
}

TSMP_DEFINE(MessageForLogServer);

MessageForLogServer *callocMessageForLogServer()
{
	ATOMIC_INIT_BEGIN;
	TSMP_SMART_CREATE(MessageForLogServer, 256, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ATOMIC_INIT_END;

	return TSMP_CALLOC(MessageForLogServer);
}

void freeMessageForLogServer(MessageForLogServer *m)
{
	TSMP_FREE(MessageForLogServer, m);
}
LogServerConnectionInfo *GetConnectionFromLinkToMultiplexer(LinkToMultiplexer *pLinkToMultiplexer)
{	
	int i;

		for (i = 0; i < eaSize(&sppConnections); i++)
		{
			if (sppConnections[i]->pLinkToMultiplexerForLogging == pLinkToMultiplexer)
			{
				return sppConnections[i];
			}
		}
	return NULL;
}

LogServerConnectionInfo *GetConnectionFromNetLink(NetLink *pLink)
{
	LogServerConnectionInfo *pRetVal = linkGetUserData(pLink);
	if (!pRetVal)
	{
		int i;

		for (i = 0; i < eaSize(&sppConnections); i++)
		{
			if (sppConnections[i]->pLink == pLink)
			{
				return sppConnections[i];
			}
		}
	}

	return pRetVal;
}


void FirstConnectionToLogServer(NetLink *pLink)
{
	Packet *pPak = pktCreate(pLink, LOGCLIENT_HEREARECATEGORYNAMES);
	int i;

	for (i=0; i < LOG_LAST; i++)
	{
		pktSendString(pPak, StaticDefineIntRevLookup(enumLogCategoryEnum, i));
	}

	pktSendString(pPak, "");

	pktSendString(pPak, GlobalTypeToName(GetAppGlobalType()));

	pktSend(&pPak);

}

void FirstConnectionToLogServer_Multiplexed(LinkToMultiplexer *pLink)
{
	Packet *pPak = CreateLinkToMultiplexerPacket(pLink, MULTIPLEX_CONST_ID_LOG_SERVER, LOGCLIENT_HEREARECATEGORYNAMES, NULL);
	int i;

	for (i=0; i < LOG_LAST; i++)
	{
		pktSendString(pPak, StaticDefineIntRevLookup(enumLogCategoryEnum, i));
	}

	pktSendString(pPak, "");

	pktSendString(pPak, GlobalTypeToName(GetAppGlobalType()));

	pktSend(&pPak);
}



void logMultiplexerDisconnect(int iIndexOfDeadServer, LinkToMultiplexer *pManager)
{
	if (iIndexOfDeadServer == MULTIPLEX_CONST_ID_LOG_SERVER)
	{
		LogServerConnectionInfo *pConnection = GetConnectionFromLinkToMultiplexer(pManager);

		if (pConnection)
		{
			pConnection->bLinkToMultiplexerForLoggingDisconnected = true;
		}
	}
}

void svrLogForceFlush(void)
{
	svrLogFlush(1);
}


void OVERRIDE_LATELINK_logFlush(void)
{
	svrLogFlush(1);
}

#define SECS_LOG_RESEND (60*3)



static void logMultiplexerServerDisconnect(LinkToMultiplexer *pManager)
{
	LogServerConnectionInfo *pConnection = GetConnectionFromLinkToMultiplexer(pManager);
	AttemptToConnectToController(true, NULL, false);
	if (pConnection)
	{
		ErrorOrAlert("LOST_LOGSERVER_CNCT", "%s lost connection to multiplexer for %s logging entirely. Failure imminent", GlobalTypeAndIDToString(GetAppGlobalType(), GetAppGlobalID()), pConnection->pConnectionName);
	}
	svrExit(-1);
}


static void logServerReconnectCB(NetLink *pLink, void *pUserData)
{
	LogServerConnectionInfo *pConnection = GetConnectionFromNetLink(pLink);
	if (pConnection)
	{
		if (sbAlertOnLogServerReconnect)
		{
			TriggerAlertf("LOG_SERVER_RECONNECT", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS,
				0, 0, 0, 0, 0, NULL, 0, "Restored connection to the %s log server!", pConnection->pConnectionName);
		}
	}

}

static void logServerDisconnectCB(NetLink *pLink, void *pUserData)
{
	LogServerConnectionInfo *pConnection = GetConnectionFromNetLink(pLink);
	if (pConnection)
	{
		if (sbAlertOnLogServerDisconnect)
		{
			TriggerAlertf("LOG_SERVER_DISCONNECT", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS,
				0, 0, 0, 0, 0, NULL, 0, "Lost connection to the %s log server!", pConnection->pConnectionName);
		}

		if (pConnection->pLink)
		{
			linkRemove_wReason(&pConnection->pLink, "Got disconnect CB");
		}
	}

}

void svrLogFlush(int force)
{
	U32	iCurTime = timeSecondsSince2000();
	Packet *pPacket = NULL;
	int i;

	PERFINFO_AUTO_START_FUNC();

	if (force)
		logWaitForQueueToEmpty(); // Flush this log system too, as everyone calling this is about to exit

	if ((commFlushAndCloseAllCommsCalled() || !log_comm) && !gServerLibState.bUseMultiplexerForLogging)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	EnterCriticalSection(&sMessageForLogServerCS);

	LazyInitConnections();


	FOR_EACH_IN_EARRAY(sppConnections, LogServerConnectionInfo, pConnection)
	{
		if (!pConnection->iTotalMessageLength)
		{
			continue;
		}

		if (!force && pConnection->iTotalMessageLength < MESSAGE_SIZE_FORCE_SEND && (iCurTime < pConnection->iLastSendTime + SECS_PASSED_FORCE_SEND))
		{
			continue;
		}

		if (gServerLibState.bUseMultiplexerForLogging)
		{
			if (pConnection->bLinkToMultiplexerForLoggingDisconnected)
			{
				DestroyLinkToMultiplexer(pConnection->pLinkToMultiplexerForLogging);
				pConnection->pLinkToMultiplexerForLogging = NULL;
				pConnection->bLinkToMultiplexerForLoggingDisconnected = false;
			}

			if (!pConnection->pLinkToMultiplexerForLogging)
			{
				int iRequiredServers[] = { MULTIPLEX_CONST_ID_LOG_SERVER, -1 };
				char *pMultiplexError = NULL;

				if (pConnection->iLastTimeAttemptedMultiplexConnection && (timeSecondsSince2000() - pConnection->iLastTimeAttemptedMultiplexConnection < 5))
				{
					continue;
				}

				pConnection->iLastTimeAttemptedMultiplexConnection = timeSecondsSince2000();

				pConnection->pLinkToMultiplexerForLogging = GetAndAttachLinkToMultiplexer("localhost", GetMultiplexerListenPort(), LINKTYPE_DEFAULT,
					NULL, NULL, logMultiplexerDisconnect, logMultiplexerServerDisconnect, iRequiredServers, MULTIPLEX_CONST_ID_LOG_SERVER, NULL, "Link to multiplexer, thence to log server", &pMultiplexError);

				if (pConnection->pLinkToMultiplexerForLogging)
				{
					FirstConnectionToLogServer_Multiplexed(pConnection->pLinkToMultiplexerForLogging);
				}
				else
				{
					Errorf("Error while connecting to multiplexer for logging %s: %s", pConnection->pConnectionName, pMultiplexError);
					printf("Error while connecting to multiplexer for logging %s: %s", pConnection->pConnectionName, pMultiplexError);
					estrDestroy(&pMultiplexError);
				}
			}

			if (pConnection->pLinkToMultiplexerForLogging)
			{
				static PacketTracker *pTracker;
				ONCE(pTracker = PacketTrackerFind("MultiplexedLog", 0, NULL));
				pPacket = CreateLinkToMultiplexerPacket(pConnection->pLinkToMultiplexerForLogging, MULTIPLEX_CONST_ID_LOG_SERVER, LOGCLIENT_LOGPRINTF, pTracker);
			}
		}
		else
		{
			if (!pConnection->pLink || linkDisconnected(pConnection->pLink))
			{
				linkRemove(&pConnection->pLink);

				if (!pConnection->pLogConnectFSM)
				{
					pConnection->pLogConnectFSM = commConnectFSM(COMMFSMTYPE_RETRY_FOREVER, 2.0f, log_comm,LINKTYPE_SHARD_NONCRITICAL_1MEG, LINK_FORCE_FLUSH,
						pConnection->pServerName, DEFAULT_LOGSERVER_PORT,0,logServerReconnectCB,logServerDisconnectCB,0, NULL, NULL);
				}

				commMonitor(log_comm);

				if (commConnectFSMUpdate(pConnection->pLogConnectFSM, &pConnection->pLink) == COMMFSMSTATUS_SUCCEEDED)
				{
					char debugName[128];
					sprintf(debugName, "link to %s log server", pConnection->pConnectionName);
					commConnectFSMDestroy(&pConnection->pLogConnectFSM);
					linkSetDebugName(pConnection->pLink, debugName);
					FirstConnectionToLogServer(pConnection->pLink);
					linkSetUserData(pConnection->pLink, pConnection);
				}
			}
			else if (pConnection->pLogConnectFSM)
			{
				//weird case where we were trying to connect via FSM, but someone else already connected in some other way... no harm, no foul, just 
				//get rid of the FSM
				commConnectFSMDestroy(&pConnection->pLogConnectFSM);
			}


			if (pConnection->pLink)
			{
				pktCreateWithCachedTracker(pPacket, pConnection->pLink,LOGCLIENT_LOGPRINTF);
			}
		}

		if (!pPacket)
		{
			if (pConnection->iTotalMessageLength > MESSAGE_SIZE_WRITE_LOCALLY || sbSystemIsShuttingDown)
			{
				int iNumToDirectWrite;
				//a backup list populated from the log server list when the log server list
				//is overflowing... copied to a separate list so it's outside a CS
				MessageForLogServer **ppMessagesForDirectWrite = 0;

				if (siLocalWriteAlertThrottle && timeSecondsSince2000() > pConnection->iNextLocalWriteAlertTime && !sbSystemIsShuttingDown)
				{
					TriggerAlertf(allocAddString("LOG_MESSAGE_OVERFLOW"),
						ALERTLEVEL_WARNING, ALERTCATEGORY_NETOPS, 0, GetAppGlobalType(), GetAppGlobalID(),
						GetAppGlobalType(), GetAppGlobalID(), getHostName(), 0,
						"No %s logserver found, too many messages stacked up, writing some of them out locally", pConnection->pConnectionName);
					pConnection->iNextLocalWriteAlertTime = timeSecondsSince2000() + siLocalWriteAlertThrottle;
				}

				for (iNumToDirectWrite=0; iNumToDirectWrite < eaSize(&pConnection->ppMessages); iNumToDirectWrite++)
				{	
					eaPush(&ppMessagesForDirectWrite, pConnection->ppMessages[iNumToDirectWrite]);

					pConnection->iTotalMessageLength -= estrLength(&pConnection->ppMessages[iNumToDirectWrite]->pMessage);
					if ((pConnection->iTotalMessageLength <  MESSAGE_SIZE_WRITE_LOCALLY * 0.95f) && !sbSystemIsShuttingDown)
					{
						break;
					}
				}

				eaRemoveRange(&pConnection->ppMessages, 0, iNumToDirectWrite);

				LeaveCriticalSection(&sMessageForLogServerCS);
	
				for (i=0; i < iNumToDirectWrite; i++)
				{
					logDirectWrite(StaticDefineIntRevLookup(enumLogCategoryEnum, ppMessagesForDirectWrite[i]->eCategory), ppMessagesForDirectWrite[i]->pMessage);
					estrDestroy(&ppMessagesForDirectWrite[i]->pMessage);
					freeMessageForLogServer(ppMessagesForDirectWrite[i]);
				}

				eaDestroy(&ppMessagesForDirectWrite);
				EnterCriticalSection(&sMessageForLogServerCS);
				continue;
			}

			continue;
		}
	
		pConnection->iLastSendTime = iCurTime;


		PutContainerTypeIntoPacket(pPacket, GetAppGlobalType());

		for (i=0; i < eaSize(&pConnection->ppMessages); i++)
		{
			pktSendBits(pPacket, 32, pConnection->ppMessages[i]->iTime);
			pktSendBits(pPacket, 32, pConnection->ppMessages[i]->eCategory);
			pktSendString(pPacket, pConnection->ppMessages[i]->pMessage);
		}


		//now free pMessages
		for (i=0; i < eaSize(&pConnection->ppMessages); i++)
		{
			estrDestroy(&pConnection->ppMessages[i]->pMessage);
			freeMessageForLogServer(pConnection->ppMessages[i]);
		}

		eaDestroy(&pConnection->ppMessages);
	
		pConnection->iTotalMessageLength = 0;


		//send a terminating empty message
		pktSendBits(pPacket, 32, 0);

		if (gServerLibState.bUseMultiplexerForLogging)
		{
			pktSend(&pPacket);
		}
		else
		{
			pktSend(&pPacket);
			linkFlush(pConnection->pLink);
			commMonitor(log_comm);
		}
	}
	FOR_EACH_END;
	
	LeaveCriticalSection(&sMessageForLogServerCS);
	PERFINFO_AUTO_STOP();
}

void svrLogDisconnectFlush(void)
{
	int i;
	char disconnectFileName[CRYPTIC_MAX_PATH];

	EnterCriticalSection(&sMessageForLogServerCS);


	FOR_EACH_IN_EARRAY(sppConnections, LogServerConnectionInfo, pConnection)
	{
		sprintf(disconnectFileName, "LogUnflushed_pid_%d.log", getpid());

		for (i=0; i < eaSize(&pConnection->ppMessages); i++)
		{
			filelog_printf(disconnectFileName, "%s", pConnection->ppMessages[i]->pMessage);
		}

		//now free pMessages
		for (i=0; i < eaSize(&pConnection->ppMessages); i++)
		{
			estrDestroy(&pConnection->ppMessages[i]->pMessage);
			freeMessageForLogServer(pConnection->ppMessages[i]);
		}

		eaDestroy(&pConnection->ppMessages);
	
		pConnection->iTotalMessageLength = 0;
	}
	FOR_EACH_END;

	LeaveCriticalSection(&sMessageForLogServerCS);

}

AUTO_RUN;
void logNetAutoInit(void)
{
	InitializeCriticalSection(&sMessageForLogServerCS);
}

int serverlog_send(enumLogCategory eCategory, const char *msg)
{

	MessageForLogServer *pMessage;

	PERFINFO_AUTO_START_FUNC();

	pMessage = callocMessageForLogServer();
	estrCopy2(&pMessage->pMessage, msg);
	pMessage->eCategory = eCategory;

	EnterCriticalSection(&sMessageForLogServerCS);

	{
		int iConnectionIndex = siConnectionIndicesByCategory[eCategory];
		LogServerConnectionInfo *pConnection;

		if (iConnectionIndex < 0 || iConnectionIndex >= eaSize(&sppConnections))
		{
			AssertOrAlert("BAD_LOG_CATEGORY", "Log category %d wants to send to log connection %d, which is corrupt", eCategory, iConnectionIndex);
			iConnectionIndex = 0;
		}

		pConnection = sppConnections[iConnectionIndex];

		eaPush(&pConnection->ppMessages, pMessage);

		PERFINFO_AUTO_START("timeGetSecondsSince2000FromLogDateString", 1);
		pMessage->iTime = timeGetSecondsSince2000FromLogDateString(msg);
		PERFINFO_AUTO_STOP();
		if (!pMessage->iTime)
		{
			pMessage->iTime = timeSecondsSince2000();
		}


		pConnection->iTotalMessageLength += estrLength(&pMessage->pMessage);
	}
	LeaveCriticalSection(&sMessageForLogServerCS);

	PERFINFO_AUTO_STOP();

	return true;
}

static bool sbDontDoBlockingConnect = false;
void SetDontDoBlockingLogConnect(bool bSet)
{
	sbDontDoBlockingConnect = bSet;
}

static bool sbSvrLogInitCalled = false;



int svrLogInit(void)
{
	sbSvrLogInitCalled = true;

	if (stricmp(gServerLibState.logServerHost, "none") == 0)
	{
		return 0;
	}

	LazyInitConnections();

	sppConnections[0]->pConnectionName = strdup("Main Logging");
	sppConnections[0]->pServerName = strdup(gServerLibState.logServerHost);

	if (sbDontDoBlockingConnect)
	{
		if (!log_comm)
			log_comm = commCreate(0,1);
	}
	else
	{
		FOR_EACH_IN_EARRAY(sppConnections, LogServerConnectionInfo, pConnection)
		{
			printf("About to attempt %s logserver connection\n", pConnection->pConnectionName);

			if (gServerLibState.bUseMultiplexerForLogging)
			{
				if (!pConnection->pLinkToMultiplexerForLogging)
				{		
					int iRequiredServers[] = { MULTIPLEX_CONST_ID_LOG_SERVER, -1 };
					char *pMultiplexError = NULL;

					loadstart_printf("Attempting multiplexer logserver connection for %s.%s\n", pConnection->pConnectionName, gbMustConnectToLogServer ? " Will retry forever" : "");

					do
					{

						pConnection->pLinkToMultiplexerForLogging = GetAndAttachLinkToMultiplexer("localhost", GetMultiplexerListenPort(), LINKTYPE_DEFAULT,
							NULL, NULL, logMultiplexerDisconnect, logMultiplexerServerDisconnect, iRequiredServers, MULTIPLEX_CONST_ID_LOG_SERVER, NULL, "link to multiplexer, thence to log server", &pMultiplexError);

						if (!pConnection->pLinkToMultiplexerForLogging)
						{
							printf("Error while connecting to multiplexer for logging: %s", pMultiplexError);
							Errorf("Error while connecting to multiplexer for logging: %s", pMultiplexError);
							estrDestroy(&pMultiplexError);
						}					
					}
					while (!pConnection->pLinkToMultiplexerForLogging && gbMustConnectToLogServer);

					if (pConnection->pLinkToMultiplexerForLogging)
					{
						FirstConnectionToLogServer_Multiplexed(pConnection->pLinkToMultiplexerForLogging);
					}

					loadend_printf("%s", pConnection->pLinkToMultiplexerForLogging ? "Succeeded" : "failed");
				}
			}
	
			if (!gServerLibState.bUseMultiplexerForLogging || (isDevelopmentMode() && !pConnection->pLinkToMultiplexerForLogging))
			{
				char commConnectError[512];

				loadstart_printf("Connecting to logserver (%s:%d) for %s...", pConnection->pServerName,DEFAULT_LOGSERVER_PORT, pConnection->pConnectionName);
				if (!log_comm)
					log_comm = commCreate(0,1);

				pConnection->pLink = commConnectEx(log_comm,LINKTYPE_SHARD_NONCRITICAL_1MEG, LINK_FORCE_FLUSH,pConnection->pServerName,DEFAULT_LOGSERVER_PORT,0,0,logServerDisconnectCB, 0, commConnectError, sizeof(commConnectError), __FILE__, __LINE__);
				if (!pConnection->pLink)
				{
					ONCE(ErrorOrAlert("LOG_CONNECT_FAIL", "Couldn't even create LogCommLink. Error: %s", commConnectError));

					continue;
				}
		

				linkSetDebugName(pConnection->pLink, "Link to log server");

				if (gbMustConnectToLogServer)
				{
					while (!linkConnectWaitNoRemove(&pConnection->pLink,3))
					{
						if(!pConnection->pLink)
						{
							pConnection->pLink = commConnect(log_comm,LINKTYPE_SHARD_NONCRITICAL_1MEG, LINK_FORCE_FLUSH,pConnection->pServerName,DEFAULT_LOGSERVER_PORT,0,0,logServerDisconnectCB,0);
						}
						loadupdate_printf("Still trying to connect to log server for %s... (will try forever)\n", pConnection->pConnectionName);
					}
				}
				else
				{
					if (!linkConnectWait(&pConnection->pLink,3))
					{
						if (sbAlertOnLogServerDisconnect)
						{
							TriggerAlert("LOG_SERVER_CONNECT_FAILED", "Failed to connect to log server... will reattempt", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, 0, 0);
						}

						loadend_printf("failed.");
						continue;
					}
				}

				if (pConnection->pLink)
				{
					FirstConnectionToLogServer(pConnection->pLink);
				}


				loadend_printf("connected.");
			}
		}
		FOR_EACH_END;
	}

	// set the utils logger
	logSetWriteCallback(serverlog_send);

	return 1;
}




void AddCategoryForServer(char *pConnectionName, char *pServerName, enumLogCategory eCategory)
{
	int i;
	LogServerConnectionInfo *pConnectionInfo;

	LazyInitConnections();

	for (i = 0; i < eaSize(&sppConnections); i++)
	{
		if (stricmp_safe(sppConnections[i]->pServerName, pServerName) == 0 && stricmp_safe(sppConnections[i]->pConnectionName, pConnectionName) == 0)
		{
			siConnectionIndicesByCategory[eCategory] = i;
			return;
		}
	}

	pConnectionInfo = calloc(sizeof(LogServerConnectionInfo), 1);
	pConnectionInfo->pServerName = strdup(pServerName);
	pConnectionInfo->pConnectionName = strdup(pConnectionName);

	eaPush(&sppConnections, pConnectionInfo);
	siConnectionIndicesByCategory[eCategory] = eaSize(&sppConnections) - 1;
}

AUTO_COMMAND ACMD_COMMANDLINE;
void AddAdditionalLogServer(char *pConnectionName, char *pServerName, char *pCategoriesString)
{
	char **ppCategoryNames = NULL;
	int i;
	char *pCommandLine = NULL;

	DivideString(pCategoriesString, ",", &ppCategoryNames, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);
	for (i = eaSize(&ppCategoryNames) - 1; i >=0; i--)
	{
		int iCategory = StaticDefineInt_FastStringToInt(enumLogCategoryEnum, ppCategoryNames[i], -1);
		if (iCategory == -1)
		{
			TriggerAlertDeferred("BAD_LOG_CATEGORY", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, "Wanted to send logs in category %s to %s, but category %s doesn't seem to exist",
				ppCategoryNames[i], pServerName, ppCategoryNames[i]);
		}
		else
		{
			AddCategoryForServer(pConnectionName, pServerName, iCategory);
		}
	}

	estrPrintf(&pCommandLine, "-%s %s %s \"%s\"", __FUNCTION__, pConnectionName, pServerName, pCategoriesString);
	eaPush(&ssaAdditionalLogServerCommandlines, pCommandLine);

	eaDestroyEx(&ppCategoryNames, NULL);
}

void GetAdditionalLogServerCommandLineString(char** ppOut)
{
	StringArrayJoin(ssaAdditionalLogServerCommandlines, " ", ppOut);
}

void UpdateMultiplexerLinksForLogging()
{
	if (gServerLibState.bUseMultiplexerForLogging)
	{
		FOR_EACH_IN_EARRAY(sppConnections, LogServerConnectionInfo, pConnection)
		{
			if (pConnection->pLinkToMultiplexerForLogging)
			{
				UpdateLinkToMultiplexer(pConnection->pLinkToMultiplexerForLogging);
			}
		}
		FOR_EACH_END;
	}
}

char *OVERRIDE_LATELINK_GetLoggingStatusString(void)
{
	static char *spRetVal = NULL;
	int iConnectionNum;
	LogServerConnectionInfo *pConnectionInfo;

	if (!sbSvrLogInitCalled)
	{
		estrPrintf(&spRetVal, "ServerLogging not initted, logging locally: %s", DEFAULT_LATELINK_GetLoggingStatusString());
		return spRetVal;
	}

	if (stricmp(gServerLibState.logServerHost, "none") == 0)
	{
		estrPrintf(&spRetVal, "No logserverHost specified, logging locally: %s", DEFAULT_LATELINK_GetLoggingStatusString());
		return spRetVal;
	}

	estrClear(&spRetVal);

	for (iConnectionNum = 0; iConnectionNum < eaSize(&sppConnections); iConnectionNum++)
	{
		pConnectionInfo = sppConnections[iConnectionNum];
		estrConcatf(&spRetVal, "Logging connection \"%s\" to %s%s: ", pConnectionInfo->pConnectionName, pConnectionInfo->pServerName,
			gServerLibState.bUseMultiplexerForLogging ? "(Multiplexed)" : "");

		if (pConnectionInfo->pLink && linkConnected(pConnectionInfo->pLink) && !linkDisconnected(pConnectionInfo->pLink))
		{
			estrConcatf(&spRetVal, "Connected");
		}
		else if (pConnectionInfo->iLastSendTime)
		{
			char *pDuration = NULL;
			estrStackCreate(&pDuration);
			timeSecondsDurationToPrettyEString(timeSecondsSince2000() - pConnectionInfo->iLastSendTime, &pDuration);
			estrConcatf(&spRetVal, "Disconnected - last sent logs %s ago", pDuration);
			estrDestroy(&pDuration);
		}
		else
		{
			estrConcatf(&spRetVal, "Never connected");
		}

		estrConcatf(&spRetVal, "\n");
	}

	return spRetVal;
}

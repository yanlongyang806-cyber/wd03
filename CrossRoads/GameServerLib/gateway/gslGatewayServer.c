/***************************************************************************
 *     Copyright (c) 2012-, Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 *
 ***************************************************************************/
#include "alerts.h"
#include "entity.h"
#include "GameAccountDataCommon.h"
#include "logging.h"
#include "ServerLib.h"
#include "svrGlobalInfo.h"

#include "AutoGen/Controller_autogen_RemoteFuncs.h"
#include "AutoGen/Message_c_ast.h"

#include "GatewayPerf.h"

#include "gslGatewayMessages.h"
#include "gslGatewaySession.h"
#include "gslGatewayServer.h"

#include "Player.h"

// Debugging flag that causes logging to be sent to the console.
bool g_bLogToConsole = false;
AUTO_CMD_INT(g_bLogToConsole, LogToConsole);

static GatewayServerGlobalInfo gslGatewayGlobalInfo = {0};

void gslSendGatewayCommand(Entity *pEntity, const char *pCmd, bool bPrivate, CmdContextFlag iFlags, enumCmdContextHowCalled eHow, bool bFast, CmdParseStructList *pStructs)
{
	GatewaySession *psess = wgsFindSessionForAccountId(entGetAccountID(pEntity));

	if(psess)
	{
		if(pStructs && eaSize(&pStructs->ppEntries))
		{
			//Convert anything that was listed as "STRUCT(...)" to a JSON structure

			char *pCmdJSON = NULL;
			char *estrStructBuf = NULL;
			char *estrJSONBuf = NULL;
			int i = 0;

			estrCopy(&pCmdJSON,&pCmd);
			estrCreate(&estrStructBuf);
			estrCreate(&estrJSONBuf);

			for(i=0;i<eaSize(&pStructs->ppEntries);i++)
			{
				estrPrintf(&estrStructBuf,"STRUCT(%d)",i);
				ParserWriteJSON(&estrJSONBuf,pStructs->ppEntries[i]->pTPI,pStructs->ppEntries[i]->pStruct,0,0,0);

				estrReplaceOccurrences(&pCmdJSON,estrStructBuf,estrJSONBuf);
			}
			
			session_sendClientCmd(psess,pCmdJSON);

			estrDestroy(&pCmdJSON);
			estrDestroy(&estrStructBuf);
			estrDestroy(&estrJSONBuf);
		}
		else
		{
			session_sendClientCmd(psess,pCmd);
		}
		

		
	}
}

/***********************************************************************
 * gslGatewayServer_Init
 *
 */
void gslGatewayServer_Init(void)
{
	wgsInitSessions();
	wgsStartGatewayProxy();
	cmdSetServerToClientCB(gslSendGatewayCommand);
}

/***********************************************************************
 * gslGatewayServer_GetSessionCount
 *
 */
int gslGatewayServer_GetSessionCount(void)
{
	return wgsGetSessionCount();
}


/***********************************************************************
 * gslGatewayServer_OncePerFrame
 *
 */
void gslGatewayServer_OncePerFrame(void)
{
	PERFINFO_AUTO_START_FUNC();

	wgsUpdateAllContainers();

	PERFINFO_AUTO_STOP();
}


/***********************************************************************
 * gslGatewayServer_ContainerSubscriptionUpdate
 *
 */
void gslGatewayServer_ContainerSubscriptionUpdate(enumResourceEventType eType, GlobalType type, ContainerID id)
{
	PERFINFO_AUTO_START_FUNC();
	if(eType == RESEVENT_RESOURCE_ADDED ||	eType == RESEVENT_RESOURCE_MODIFIED)
	{
		wgsDBContainerModified(type, id);
	}
	PERFINFO_AUTO_STOP();
}

/***********************************************************************
 * gslGatewayServer_GameAccountDataSubscribed
 *
 */
void gslGatewayServer_GameAccountDataSubscribed(GameAccountData *pGameAccountdData)
{
	GatewaySession *psess;

	PERFINFO_AUTO_START_FUNC();

	psess = wgsFindSessionForAccountId(pGameAccountdData->iAccountID);
	if(psess)
	{
		session_GameAccountDataSubscribed(psess);
	}

	PERFINFO_AUTO_STOP();
}

/***********************************************************************
 * gslGatewayServer_PeriodicUpdate
 *
 */
void gslGatewayServer_PeriodicUpdate(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	PERFINFO_AUTO_START_FUNC();

	gslGatewayGlobalInfo.iHeapTotal = gateperf_GetCount("mem:heapTotal", 0);
	gslGatewayGlobalInfo.iHeapUsed = gateperf_GetCount("mem:heapUsed", 0);
	gslGatewayGlobalInfo.iWorkingSet = gateperf_GetCount("mem:workingSet", 0);

	gslGatewayGlobalInfo.iNumSessions = wgsGetSessionCount();

	RemoteCommand_HereIsGatewayGlobalInfo(GLOBALTYPE_CONTROLLER, 0, gServerLibState.containerID, &gslGatewayGlobalInfo);

	PERFINFO_AUTO_STOP();
}

/***********************************************************************
 * gslGatewayServer_BootEveryone
 *
 */
AUTO_COMMAND_REMOTE;
void gslGatewayServer_BootEveryone(bool bHardBoot)
{
	printf("Controller told us to %s boot everyone. Closing all sessions.\n", bHardBoot ? "hard" : "soft");
	gslGatewayServer_Log(LOG_GATEWAY_SERVER, "Controller told us to boot everyone. Closing all sessions.");
	wgsDestroyAllSessions();
}

/***********************************************************************
 * gslGatewayServer_BroadcastMessage
 *
 */
AUTO_COMMAND_REMOTE;
void gslGatewayServer_BroadcastMessage(const char *pTitle, const char *pString)
{
	printf("Broadcasting message: %s: %s\n", pTitle, pString);
	gslGatewayServer_Log(LOG_GATEWAY_SERVER, "Broadcasting message: %s: %s", pTitle, pString);
	wgsBroadcastMessageToAllConnections(pTitle, pString);
}


/***********************************************************************/
/***********************************************************************/
/***********************************************************************/

#define NUM_RECENT_LOG_LINES_TO_SAVE 50
static int s_iNextLogIndex = 0;
static STRING_EARRAY s_eaRecentLogLines = NULL;
static U32 *s_eaRecentLogTimes = NULL;

void gslGatewayServer_Log(enumLogCategory ecat, char* format, ...)
{
	VA_START(ap, format);

	if(ecat == LOG_GATEWAY_SERVER || ecat == LOG_GATEWAY_PROXY)
	{
		char *estr = NULL;
		int iRecentLogSize = eaSize(&s_eaRecentLogLines);

		estrConcatfv(&estr, format, ap);
		log_printf(ecat, "%s", estr);

		if(strstr(estr, "PeriodicStats"))
		{
			// No need to have the PeriodicStats on the status page because
			//   that's recorded elsewhere on it.
			estrDestroy(&estr);
		}
		else
		{
			if(iRecentLogSize < NUM_RECENT_LOG_LINES_TO_SAVE)
			{
				// If the saved log lines array is not at max size yet, then just append to it.
				eaPush(&s_eaRecentLogLines, estr);
				ea32Push(&s_eaRecentLogTimes, timeSecondsSince2000());
			}
			else
			{
				devassert(s_iNextLogIndex < iRecentLogSize);
				if(s_iNextLogIndex < iRecentLogSize)
				{
					// Free the previous string in that slot.
					estrDestroy(&s_eaRecentLogLines[s_iNextLogIndex]);
					s_eaRecentLogLines[s_iNextLogIndex] = estr;
					s_eaRecentLogTimes[s_iNextLogIndex] = timeSecondsSince2000();
				}
			}

			// Increment the index for the next log line, wrapping if necessary.
			s_iNextLogIndex++;
			if(s_iNextLogIndex >= NUM_RECENT_LOG_LINES_TO_SAVE)
			{
				s_iNextLogIndex = 0;
			}
		}
	}
	else
	{
		log_vprintf(ecat, format, ap);
	}

	if(g_bLogToConsole)
	{
		vprintf(format, ap);
	}

	VA_END();
}

/***********************************************************************/
/***********************************************************************/
/***********************************************************************/

#include "ServerLib.h"
#include "GatewayPerf.h"
#include "url.h"
#include "HttpXpathSupport.h"
#include "autogen/gslGatewayServer_c_ast.h"
#include "../ServerLib/AutoGen/GatewayPerf_h_ast.h"


AUTO_STRUCT;
typedef struct GatewayLogLine
{
	U32 time; AST(FORMATSTRING(HTML_SECS=1))
	char *log;
} GatewayLogLine;

AUTO_STRUCT AST_FORMATSTRING(HTML_NOTES_AUTO=1);
typedef struct GatewayServerOverview
{
	char *pStyles; AST(ESTRING FORMATSTRING(HTML=1))
	GatewayPerf *pperf;
	char *pSessions; AST(ESTRING, FORMATSTRING(HTML=1))
	char *pGenericInfo; AST(ESTRING, FORMATSTRING(HTML=1))

	GatewayLogLine **ppLogLines;

} GatewayServerOverview;

/***********************************************************************
 * gslGatewayServer_GetCustomServerInfoStructForHttp
 *
 */
void gslGatewayServer_GetCustomServerInfoStructForHttp(UrlArgumentList *pUrl, ParseTable **ppTPI, void **ppStruct)
{
	static GatewayServerOverview overview = {0};
	int iLogSize;
	int i;
	int idx;

	StructReset(parse_GatewayServerOverview, &overview);
	overview.pperf = StructCreate(parse_GatewayPerf);
	if(g_pperfCur)
	{
		StructCopyAll(parse_GatewayPerf, g_pperfCur, overview.pperf);
	}

	// Set the size of the recent logs array.
	iLogSize = eaSize(&s_eaRecentLogLines);
	eaSetSize(&overview.ppLogLines, iLogSize);

	// Copy log lines into overview struct.
	idx = s_iNextLogIndex;
	if(idx >= iLogSize)
	{
		idx = 0;
	}

	for(i = 0; i < iLogSize; i++)
	{
		overview.ppLogLines[i] = StructCreate(parse_GatewayLogLine);
		overview.ppLogLines[i]->log = strdup(s_eaRecentLogLines[idx]);
		overview.ppLogLines[i]->time = s_eaRecentLogTimes[idx];

		idx++;
		if(idx >= iLogSize)
		{
			idx = 0;
		}
	}

	estrPrintf(&overview.pStyles, "<style>.tdBars { text-align:left }"
		" .ulBars { padding-left: 0px; margin-left: 10px }"
		" .trHistogram { list-style-image: url(data:image/gif;base64,R0lGODlhAQABAAAAACH5BAEKAAEALAAAAAABAAEAAAICTAEAOw==); }"
		" .tdTime { white-space: nowrap }"
		" .tdLog { text-align: left; font-family:'Lucida Console'; font-size:.9em }"
		"</style>");

	estrPrintf(&overview.pSessions, "<a href=\"%s%s%s\">Sessions</a>",
		LinkToThisServer(), GLOBALOBJECTS_DOMAIN_NAME, "GatewaySessions");

	estrPrintf(&overview.pGenericInfo, "<a href=\"/viewxpath?xpath=%s[%u].generic\">Generic ServerLib info for the %s</a>",
		GlobalTypeToName(GetAppGlobalType()), gServerLibState.containerID, GlobalTypeToName(GetAppGlobalType()));

	*ppTPI = parse_GatewayServerOverview;
	*ppStruct = &overview;
}

#include "AutoGen/gslGatewayServer_c_ast.c"

/* End of File */

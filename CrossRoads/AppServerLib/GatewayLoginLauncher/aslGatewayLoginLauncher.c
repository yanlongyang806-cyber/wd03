/***************************************************************************
*     Copyright (c) 2012-, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "AppServerLib.h"
#include "GlobalTypes.h"
#include "objSchema.h"
#include "error.h"
#include "serverLib.h"
#include "objTransactions.h"
#include "sock.h"
#include "sysUtil.h"
#include "file.h"
#include "fileutil.h"
#include "alerts.h"
#include "AccountNet.h"
#include "process_util.h"
#include "loggingEnums.h"
#include "logging.h"
#include "Message.h"
#include "ControllerLink.h"
#include "GatewayPerf.h"
#include "GatewayUtil.h"
#include "GatewayWatcher.h"

#include "../ServerLib/AutoGen/GatewayPerf_h_ast.h"

#include "AutoGen/aslGatewayLoginLauncher_c_ast.h"

static GatewayWatcher *s_pwatcher;
static NetLink **s_ppLinks = NULL;

#define CMD_UNUSED           1
#define CMD_LOG              2
#define CMD_PERF             3
#define CMD_LOCK             4
#define CMD_TRANSLATIONS     5
#define CMD_SHUTDOWN_MESSAGE 6

static void HandleLog(Packet *pktIn, NetLink *link)
{
	char *pch;
	enumLogCategory eCat;

	pch = pktGetStringTemp(pktIn);
	eCat = StaticDefineIntGetIntDefault(enumLogCategoryEnum, pch, LOG_GATEWAY_LOGIN);
	pch = pktGetStringTemp(pktIn);
	log_printf(eCat, "%s", pch);

}

static void HandlePerf(Packet *pktIn, NetLink *link)
{
	char *pch = pktGetStringTemp(pktIn);

	gateperf_SetCurFromString(pch);
}

static void HandleShutdownMessage(Packet *pktIn, NetLink *link)
{
	if(s_pwatcher)
	{
		char *pch = pktGetStringTemp(pktIn);
		gateway_SetShutdownMessage(s_pwatcher, pch);
	}
}

////////////////////////////////////////////////////////////////////////////

static void HandleTranslations(Packet *pktIn, NetLink *link)
{
	Packet *pkt;
	U32 i;

	Language lang = (Language)pktGetU32(pktIn);
	U32 count = pktGetU32(pktIn);

	pkt = pktCreate(link, CMD_TRANSLATIONS);
	pktSendU32(pkt, lang);
	pktSendU32(pkt, count);

	for(i = 0; i < count; i++)
	{
		char * pchKey = pktGetStringTemp(pktIn);

		pktSendString(pkt, pchKey);
		pktSendString(pkt, langTranslateMessageKey(lang, pchKey));
	}
	pktSend(&pkt);
}

//////////////////////////////////////////////////////////////////////////

static void glmMessageHandler(SA_PARAM_NN_VALID Packet *pktIn, int cmd, SA_PARAM_NN_VALID NetLink *link, SA_PARAM_OP_VALID void *userData)
{
	PERFINFO_AUTO_START_FUNC();

	switch(cmd)
	{
		case CMD_UNUSED:
			break;

		case CMD_LOG:
			HandleLog(pktIn, link);
			break;

		case CMD_PERF:
			HandlePerf(pktIn, link);
			break;

		case CMD_TRANSLATIONS:
			HandleTranslations(pktIn, link);
			break;

		case CMD_SHUTDOWN_MESSAGE:
			HandleShutdownMessage(pktIn, link);
			break;

	}

	PERFINFO_AUTO_STOP_FUNC();
}

static int glmConnectHandler(SA_PARAM_NN_VALID NetLink* link, SA_PARAM_OP_VALID void *userData)
{
	printf("New GatewayLogin connection.\n");

	gateway_WatcherConnected(s_pwatcher);

	eaPush(&s_ppLinks, link);

	return 0;
}

static int glmDisconnectHandler(SA_PARAM_NN_VALID NetLink* link, SA_PARAM_OP_VALID void *userData)
{
	printf("GatewayLogin disconnected!\n");

	eaFindAndRemove(&s_ppLinks, link);

	gateway_WatcherDisconnected(s_pwatcher);

	return 0;
}

//////////////////////////////////////////////////////////////////////////

void OVERRIDE_LATELINK_GatewayNotifyLockStatus(bool bLocked)
{
	EARRAY_FOREACH_BEGIN(s_ppLinks, i);
	{
		Packet *pkt = pktCreate(s_ppLinks[i], CMD_LOCK);
		pktSendU32(pkt, bLocked);
		pktSend(&pkt);
	}
	EARRAY_FOREACH_END;
}

//////////////////////////////////////////////////////////////////////////


static void StartGatewayLogin(void)
{
	char *pchIpAccountServer;
	char *estrOptions = NULL;

	pchIpAccountServer = getAccountServer();
	if(stricmp(pchIpAccountServer, "localhost") == 0)
	{
		// As far as I can tell, the AccountServer never attaches to localhost
		//  or 127.0.0.1. It attaches to the local IP and an external IP, if
		//  there is one. So, if it's localhost, turn it into the local IP address.
		pchIpAccountServer = makeIpStr(getHostLocalIp());
	}
	estrConcatf(&estrOptions, " --ipAccountServer %s", pchIpAccountServer);

	s_pwatcher = gateway_CreateAndStartWatcher("GatewayLoginLauncher",
		"login\\server\\GatewayLogin",
		estrOptions,
		kGatewayWatcherFlags_WatchConnection,
		"GatewayLoginLauncher", DEFAULT_GATEWAYLOGINLAUNCHER_PORT, DEFAULT_GATEWAYLOGINLAUNCHER_PORT,
		glmMessageHandler,
		glmConnectHandler,
		glmDisconnectHandler,
		0 /* user bytes */);

	estrDestroy(&estrOptions);
}

//////////////////////////////////////////////////////////////////////////

int GatewayLoginLauncher_OncePerFrame(F32 fElapsed)
{
	return 1;
}

int GatewayLoginLauncher_Init(void)
{
	objLoadAllGenericSchemas();
		
	assertmsg(GetAppGlobalType() == GLOBALTYPE_GATEWAYLOGINLAUNCHER, "Wrong global type set, should be GatewayLoginLauncher.");

	msgLoadAllMessages(); // For translation
	
	loadstart_printf("Connecting GatewayLoginLauncher to TransactionServer (%s)... ", gServerLibState.transactionServerHost);

	while (!InitObjectTransactionManager(
		GetAppGlobalType(),
		gServerLibState.containerID,
		gServerLibState.transactionServerHost,
		gServerLibState.transactionServerPort,
		gServerLibState.bUseMultiplexerForTransactions, NULL))
	{
		Sleep(1000);
	}

	if (!objLocalManager())
	{
		loadend_printf("failed.");
		return 0;
	}

	loadend_printf("connected.");

	// gAppServer->oncePerFrame = GatewayLoginLauncher_OncePerFrame;

	if (!GetControllerLink())
		AttemptToConnectToController(true, NULL, true);

	StartGatewayLogin();

	DirectlyInformControllerOfState("ready");

	return 1;
}
	

AUTO_RUN;
int GatewayLoginLauncher_Register(void)
{
	aslRegisterApp(GLOBALTYPE_GATEWAYLOGINLAUNCHER, GatewayLoginLauncher_Init, 0);
	return 1;
}

//////////////////////////////////////////////////////////////////////////
//
// Stuff for shard/server monitor
//

AUTO_STRUCT AST_FORMATSTRING(HTML_NOTES_AUTO=1);
typedef struct GatewayLoginLauncherOverview
{
	GatewayPerf *pperf;
	char *pGenericInfo; AST(ESTRING, FORMATSTRING(HTML=1))

} GatewayLoginLauncherOverview;

void GatewayLoginLauncher_GetCustomServerInfoStructForHttp(UrlArgumentList *pUrl, ParseTable **ppTPI, void **ppStruct)
{
	static GatewayLoginLauncherOverview overview = {0};

	StructReset(parse_GatewayLoginLauncherOverview, &overview);
	overview.pperf = StructCreate(parse_GatewayPerf);
	if(g_pperfCur)
	{
		StructCopyAll(parse_GatewayPerf, g_pperfCur, overview.pperf);
	}

	estrPrintf(&overview.pGenericInfo, "<a href=\"/viewxpath?xpath=%s[%u].generic\">Generic ServerLib info for the %s</a>",
		GlobalTypeToName(GetAppGlobalType()), gServerLibState.containerID,GlobalTypeToName(GetAppGlobalType()));

	*ppTPI = parse_GatewayLoginLauncherOverview;
	*ppStruct = &overview;
}

#include "autogen/aslGatewayLoginLauncher_c_ast.c"

// End of File

/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "AppServerLib.h"
#include "ServerLib.h"
#include "UtilitiesLib.h"

#include "GlobalTypeEnum.h"

#include "objContainer.h"
#include "AutoTransDefs.h"
#include "AutoStartupSupport.h"

#include "LoginCommon.h"
#include "GenericHttpServing.h"
#include "structInternals.h"
#include "CCGCommon.h"

#include "stddef.h"

#include "CCGCardID.h"
#include "CCGDefs.h"

#include "CCGServer.h"
#include "CCGPackDef.h"
#include "CCGPrintRun.h"
#include "CCGPlayerData.h"
#include "CCGPlayers.h"
#include "CCGAttribute.h"
#include "CCGDeck.h"
#include "CCGTransactionReturnVal.h"
#include "CCGChamps.h"

#include "net/net.h"
#include "HttpLib.h"

#include "winutil.h"

#include "AutoGen/CCGDefs_h_ast.h"
#include "AutoGen/CCGCommon_h_ast.h"
#include "AutoGen/Controller_autogen_RemoteFuncs.h"
#include "AutoGen/CCGPackDef_h_ast.h"
#include "AutoGen/CCGPrintRun_h_ast.h"
#include "AutoGen/CCGPlayerData_h_ast.h"
#include "AutoGen/CCGTransactionReturnVal_h_ast.h"

CCGCardDefList g_CardDefs;

//
// Callback related functions
//

//
// Create a callback object.  It can only be used once.  The CCGCallback
//  structure will be freed immediately after the callback function is called.
// It is the callback function's responsibility to free or not free the
//  userData. 
//
CCGCallback *
CCG_CreateCallback(CCGCallbackFunc userFunc, void *userData)
{
	CCGCallback *cb = (CCGCallback *)malloc(sizeof(CCGCallback));

	cb->userFunc = userFunc;
	cb->userData = userData;

	return cb;
}

void
CCG_FreeCallback(CCGCallback *cb)
{
	free(cb);
}

void
CCG_CallCallback(CCGCallback *cb, CCGTransactionReturnVal *trv)
{
	(*cb->userFunc)(trv, cb->userData);

	CCG_FreeCallback(cb);
}

void
CCG_GenericTransactionCallback(TransactionReturnVal *pReturnVal, CCGCallback *cb)
{
	CCGTransactionReturnVal *trv = CCG_TRVFromTransactionRet(pReturnVal, NULL);

	CCG_CallCallback(cb, trv);

	StructDestroy(parse_CCGTransactionReturnVal, trv);
}

void
CCG_GenericCommandCallback(CCGTransactionReturnVal *trv, CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo)
{
	char *pFullRetString = NULL;

	pFullRetString = CCG_TRVToXMLResponseString(trv, false);

	DoSlowCmdReturn(trv->success, pFullRetString, pSlowReturnInfo);
}

//
// Set up the context to do a slow return, and return a copy of the slow command info
//
CmdSlowReturnForServerMonitorInfo *
CCG_SetupSlowReturn(CmdContext *pContext)
{
	CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo;

	pContext->slowReturnInfo.bDoingSlowReturn = true;
	pSlowReturnInfo = malloc(sizeof(CmdSlowReturnForServerMonitorInfo));
	memcpy(pSlowReturnInfo, &pContext->slowReturnInfo, sizeof(CmdSlowReturnForServerMonitorInfo));

	return pSlowReturnInfo;
}

void
CCG_CancelSlowReturn(CmdContext *pContext)
{
	pContext->slowReturnInfo.bDoingSlowReturn = false;
}

// create a simple success/fail string to return from xmlrpc commands
char *
CCG_CreateSimpleReturnString(char *action, TransactionReturnVal *pReturnVal)
{
	char *estrRet = estrCreateFromStr(action);
	
	if ( pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS )
	{
		estrAppend2(&estrRet, " successful");
	}
	else
	{
		estrAppend2(&estrRet, " failed");
	}
	return estrRet;
}

void 
CreateTestPlayer_CB(TransactionReturnVal *pReturn, void *pData)
{
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		printf("Create test player successful");
	}
	else
	{
		printf("Create test player failed");
	}
}

void
CCG_BuildXMLResponseString(char **responseString, ParseTable *tpi, void *struct_mem)
{
	char *resultString = NULL;

	ParserWriteXMLEx(&resultString, tpi, struct_mem, TPXML_FORMAT_XMLRPC);

	estrPrintf(responseString, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<methodResponse><params><param><value>%s</value></param></params></methodResponse>",
		resultString);
	
	estrDestroy(&resultString);
	return;
}

void
CCG_BuildXMLResponseStringWithType(char **responseString, char *type, char *val)
{
	estrPrintf(responseString, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<methodResponse><params><param><value><%s>%s</%s></value></param></params></methodResponse>",
		type, val, type);
	return;
}


///////////////////////////////////////////////////////////////////////////////////////////
// Per frame processing
///////////////////////////////////////////////////////////////////////////////////////////

// forward reference
void InitLate(void);

static int 
OncePerFrame(F32 fTotalElapsed, F32 fElapsed)
{
	static bool bOnce = false;

	if(!bOnce) 
	{
		InitLate();

		bOnce = true;
	}

	GenericHttpServing_Tick();

	return 1;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Server initialization
///////////////////////////////////////////////////////////////////////////////////////////

static void
InitSchemas(void)
{
	objRegisterNativeSchema(GLOBALTYPE_CCGPLAYER, parse_CCGPlayerData, NULL, NULL, NULL, NULL, NULL);
	objRegisterNativeSchema(GLOBALTYPE_CCGPRINTRUN, parse_CCGPrintRun, NULL, NULL, NULL, NULL, NULL);
	objRegisterNativeSchema(GLOBALTYPE_CCGPACKDEF, parse_CCGPackDef, NULL, NULL, NULL, NULL, NULL);
	if (!isProductionMode())
	{	
		loadstart_printf("Writing out schema files... ");
		objExportContainerSchema(GLOBALTYPE_CCGPLAYER);
		objExportContainerSchema(GLOBALTYPE_CCGPRINTRUN);
		objExportContainerSchema(GLOBALTYPE_CCGPACKDEF);
		loadend_printf("Done.");
	}
	objLoadAllGenericSchemas();
}

static void
InitLate(void)
{
	InitSchemas();

	CCG_PlayerDataInitLate();
	CCG_PacksInitLate();
	CCG_PrintRunInitLate();
	CCG_PlayersInit();

	RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), gServerLibState.containerID, "ready");
	ATR_DoLateInitialization();
}

static void 
InitSocket(void)
{
	// set up http server which will be used to accept XMLRPC requests from the 
	//  website and test programs
	GenericHttpServing_Begin(CCG_SERVER_PORT, "CCGServer", DEFAULT_HTTP_CATEGORY_FILTER, 0);

	printf("Listening on http socket\n");
}

static int
InitTransactions(void)
{
	assertmsg(GetAppGlobalType() == GLOBALTYPE_CCGSERVER, "CCG server type not set");

	loadstart_printf("Connecting CCGServer to TransactionServer (%s)... ", gServerLibState.transactionServerHost);
	while (!InitObjectTransactionManager(
		GetAppGlobalType(),
		gServerLibState.containerID,
		gServerLibState.transactionServerHost,
		gServerLibState.transactionServerPort,
		gServerLibState.bUseMultiplexerForTransactions)) 
	{
			Sleep(1000);
	}
	if (!objLocalManager()) {
		loadend_printf("Failed.");
		return 0;
	}
	loadend_printf("Connected.");

	return 1;
}

AUTO_STARTUP(CCGSchemas);
void 
CCG_SchemaInit(void)
{
	//InitSchemas();
}

AUTO_STARTUP(CCGServer) ASTRT_DEPS(CCGSchemas);
void 
CCG_ServerStartup(void)
{
}

static int 
CCG_Init(void)
{
	AutoStartup_SetTaskIsOn("CCGServer", 1);
	loadstart_printf("Running Auto Startup...");
	DoAutoStartup();
	loadend_printf(" done.");

	gAppServer->oncePerFrame = OncePerFrame;

	if ( !InitTransactions() )
	{
		return 0;
	}

	CCG_PrintRunInitEarly();
	CCG_DeckInitEarly();
	CCGChamps_InitEarly();

	StructInit(parse_CCGCardDefList, &g_CardDefs);
	ParserLoadFiles("defs", "CCGCards.def", "CCGCards.bin", 0, parse_CCGCardDefList, &g_CardDefs);

	InitSocket();
	
	return 1;
}

AUTO_RUN;
int 
CCG_RegisterServer(void)
{
	aslRegisterApp(GLOBALTYPE_CCGSERVER, CCG_Init, 0); // Loads a bunch of stuff, can't do APPSERVERTYPEFLAG_NOPIGGSINDEVELOPMENT);
	return 1;
}

const char *
CCG_GetCardType(U32 cardID)
{
	U32 cardNum = CCG_GetCardNum(cardID);
	CCGCardDef *cardDef = eaIndexedGetUsingInt(&g_CardDefs.cardDefs, cardNum);

	if ( cardDef == NULL )
	{
		return NULL;
	}
	return cardDef->type;
}

#include "CCGDefs_h_ast.c"
#include "CCGCommon_h_ast.c"
#include "CCGServer_h_ast.c"

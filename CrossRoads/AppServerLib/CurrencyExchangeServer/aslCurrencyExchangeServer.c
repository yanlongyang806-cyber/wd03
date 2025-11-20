/***************************************************************************
*     Copyright (c) 2013, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslCurrencyExchangeServer.h"
#include "aslCurrencyExchange.h"

#include "AppServerLib.h"
#include "ServerLib.h"
#include "UtilitiesLib.h"
#include "AutoStartupSupport.h"
#include "stdtypes.h"
#include "url.h"
#include "EString.h"
#include "GlobalTypes.h"
#include "AutoTransDefs.h"
#include "error.h"
#include "ResourceManager.h"
#include "objSchema.h"
#include "StringCache.h"
#include "windefinclude.h"
#include "objTransactions.h"

#include "AutoGen/aslCurrencyExchangeServer_c_ast.h"

AUTO_STRUCT;
typedef struct CurrencyExchangeServerOverview 
{
    char *pGenericInfo; AST(ESTRING, FORMATSTRING(HTML=1))
    CurrencyExchangeOverview *currencyExchangeOverview;
} CurrencyExchangeServerOverview;

void 
CurrencyExchange_GetCustomServerInfoStructForHttp(UrlArgumentList *pUrl, ParseTable **ppTPI, void **ppStruct)
{
    static CurrencyExchangeServerOverview overview = {0};

    estrPrintf(&overview.pGenericInfo, "<a href=\"/viewxpath?xpath=%s[%u].generic\">Generic ServerLib info for the %s</a>",
        GlobalTypeToName(GetAppGlobalType()), gServerLibState.containerID,GlobalTypeToName(GetAppGlobalType()));

    overview.currencyExchangeOverview = CurrencyExchange_GetServerInfoOverview();

    *ppTPI = parse_CurrencyExchangeServerOverview;
    *ppStruct = &overview;

}

int 
CurrencyExchangeServerLibOncePerFrame(F32 fElapsed)
{
    static bool bOnce = false;
    if(!bOnce) 
    {
        ATR_DoLateInitialization();
        bOnce = true;
    }

    CurrencyExchangeOncePerFrame(fElapsed);

    return 1;
}

int CurrencyExchangeServerLibInit(void)
{
    AutoStartup_SetTaskIsOn("CurrencyExchangeServer", 1);
    AutoStartup_RemoveAllDependenciesOn("WorldLib");

    loadstart_printf("Running Auto Startup...");
    DoAutoStartup();
    loadend_printf(" done.");

    resFinishLoading();

    objLoadAllGenericSchemas();

    stringCacheFinalizeShared();

    assertmsg(GetAppGlobalType() == GLOBALTYPE_CURRENCYEXCHANGESERVER, "Currency Exchange server type not set");

    loadstart_printf("Connecting CurrencyExchangeServer to TransactionServer (%s)... ", gServerLibState.transactionServerHost);

    while (!InitObjectTransactionManager(GetAppGlobalType(),
        gServerLibState.containerID,
        gServerLibState.transactionServerHost,
        gServerLibState.transactionServerPort,
        gServerLibState.bUseMultiplexerForTransactions, NULL)) {
            Sleep(1000);
    }
    if (!objLocalManager()) {
        loadend_printf("Failed.");
        return 0;
    }

    loadend_printf("Connected.");

    gAppServer->oncePerFrame = CurrencyExchangeServerLibOncePerFrame;

    CurrencyExchangeInit();

    return 1;
}


AUTO_RUN;
int RegisterCurrencyExchangeServer(void)
{
    aslRegisterApp(GLOBALTYPE_CURRENCYEXCHANGESERVER, CurrencyExchangeServerLibInit, 0);
    return 1;
}

AUTO_STARTUP(CurrencyExchangeServer) ASTRT_DEPS(CurrencyExchangeSchema, CurrencyExchangeConfig);
void aslCurrencyExchangeServerStartup(void)
{
}

#include "AutoGen/aslCurrencyExchangeServer_c_ast.c"
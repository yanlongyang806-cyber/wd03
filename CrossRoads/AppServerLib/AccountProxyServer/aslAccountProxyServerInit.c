#include "aslAccountProxyServerInit.h"
#include "aslAccountProxyServer.h"
#include "AutoStartupSupport.h"
#include "AppServerLib.h"
#include "objSchema.h"
#include "ServerLib.h"
#include "objTransactions.h"
#include "StringCache.h"
#include "winutil.h"
#include "ResourceManager.h"

int AccountProxyServerLibInit(void)
{
	objLoadAllGenericSchemas();

	AutoStartup_SetTaskIsOn("AccountProxyServer", 1);
	loadstart_printf("Running Auto Startup...");
	DoAutoStartup();
	loadend_printf(" done.");

	resFinishLoading();

	stringCacheFinalizeShared();

	assertmsg(GetAppGlobalType() == GLOBALTYPE_ACCOUNTPROXYSERVER, "Account proxy server type not set");

	loadstart_printf("Connecting AccountProxyServer to TransactionServer (%s)... ", gServerLibState.transactionServerHost);

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

	gAppServer->oncePerFrame = AccountProxyServerLibOncePerFrame;

	return 1;
}
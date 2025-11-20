/***************************************************************************
*     Copyright (c) 2005-2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslLeaderboardServerInit.h"

#include "AppServerLib.h"
#include "aslLeaderboardDB.h"
#include "aslLeaderboardServer.h"
#include "AutoStartupSupport.h"
#include "ControllerLink.h"
#include "error.h"
#include "objContainer.h"
#include "objSchema.h"
#include "objTransactions.h"
#include "ServerLib.h"
#include "StringCache.h"
#include "winutil.h"
#include "ResourceManager.h"

AUTO_STARTUP(LeaderboardServer) ASTRT_DEPS(Leaderboard);
void aslLeaderboard_ServerStartup(void)
{	
}

void LeaderboardShutdownCallback(Packet *pkt,int cmd,NetLink* link,void *user_data)
{
	int iTime = timeSecondsSince2000();
	switch(cmd)
	{
		xcase FROM_CONTROLLER_IAMDYING:
			printf("Controller died... Saving data..\n");
			leaderboard_autoSave();
			exit(0);

		xdefault:
			printf("Unknown command %d\n",cmd);
	}
}

int LeaderboardServerLibInit(void)
{	
	AutoStartup_SetTaskIsOn("LeaderBoardServer", 1);
	AutoStartup_RemoveAllDependenciesOn("WorldLib");

	loadstart_printf("Running Auto Startup...");
	DoAutoStartup();
	loadend_printf(" done.");

	resFinishLoading();

	stringCacheFinalizeShared();

	assertmsg(GetAppGlobalType() == GLOBALTYPE_LEADERBOARDSERVER, "Leaderboard server type not set");

	loadstart_printf("Attempting to connect leader board server to transaction server...");

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

	AttemptToConnectToController(false,LeaderboardShutdownCallback,true);

	LeaderboardInit();

	loadstart_printf("Attempting to load existing leaderboards from database...");
	leaderboardDBInit();
	loadend_printf("Done!");

	

	gAppServer->oncePerFrame = LeaderboardLibOncePerFrame;
	return 1;
}
/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslTeamServerInit.h"
#include "aslTeamServer.h"
#include "AppServerLib.h"
#include "objContainer.h"
#include "objSchema.h"
#include "objTransactions.h"
#include "queue_common.h"
#include "queue_common_h_ast.h"
#include "ServerLib.h"
#include "StringCache.h"
#include "Team.h"
#include "Team_h_ast.h"
#include "winutil.h"
#include "AutoStartupSupport.h"

#include "autogen/AppServerLib_autogen_RemoteFuncs.h"


AUTO_STARTUP(TeamSchemas);
void aslTeam_TransactionInit(void)
{
	aslTeam_InitStashedTeamLookups();
	
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_TEAM, aslTeam_StatePreChangeMembers_CB, ".eaMembers*", true, false, true, NULL);
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_TEAM, aslTeam_StatePostChangeMembers_CB, ".eaMembers*", true, false, false, NULL);
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_TEAM, aslTeam_StatePreChangeDisconnecteds_CB, ".eaDisconnecteds*", true, false, true, NULL);
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_TEAM, aslTeam_StatePostChangeDisconnecteds_CB, ".eaDisconnecteds*", true, false, false, NULL);
	objRegisterContainerTypeAddCallback(GLOBALTYPE_TEAM, aslTeam_TeamAdd_CB);	// So we can catch the teams at startup as we get info from the ObjectDB.
	objRegisterContainerTypeRemoveCallback(GLOBALTYPE_TEAM, aslTeam_TeamRemove_CB);
	objLoadAllGenericSchemas();
}

AUTO_STARTUP(TeamServer) ASTRT_DEPS(AS_CharacterAttribs, AS_AttribSets, InventoryBags, TeamSchemas, AS_GameProgression, RewardValTables);
void aslTeamServerStartup(void)
{
	Queues_LoadConfig();
}

int TeamServerLibInit(void)
{	
	AutoStartup_SetTaskIsOn("TeamServer", 1);
	AutoStartup_RemoveAllDependenciesOn("WorldLib");
	
	loadstart_printf("Running Auto Startup...");
	DoAutoStartup();
	loadend_printf(" done.");

	resFinishLoading();
	
	stringCacheFinalizeShared();
	
	assertmsg(GetAppGlobalType() == GLOBALTYPE_TEAMSERVER, "Team server type not set");
	
	loadstart_printf("Connecting TeamServer to TransactionServer (%s)... ", gServerLibState.transactionServerHost);
	
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
	
	gAppServer->oncePerFrame = TeamServerLibOncePerFrame;

	return 1;
}

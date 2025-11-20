/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

// main() is here


#include "sysutil.h"
#include "ObjectDB.h"
#include "GlobalStateMachine.h"
#include "MemoryPool.h"
#include "MemAlloc.h"
#include "gimmeDLLWrapper.h"
#include "objTransactions.h"
#include "CostumeCommon.h"
#include "UIColor.h"
#include "DatabaseTest/FakeGameServer.h"

// *********************************************************************************
// *********************************************************************************
// main, etc.
// *********************************************************************************


int main(int argc,char **argv)
{
	EXCEPTION_HANDLER_BEGIN
	WAIT_FOR_DEBUGGER

	RegisterGenericGlobalTypes(); // We need to call these now, so the parsing works

	//very first thing is to find out what kind of app server we are, before all AUTO_RUN stuff happens.
	parseGlobalTypeArgc(argc, argv, GLOBALTYPE_OBJECTDB);

	DO_AUTO_RUNS;

	ObjectDBInit(argc, argv);
	
	if (GetAppGlobalType() == GLOBALTYPE_TESTGAMESERVER)
	{
		printf("Running as Fake GameServer\n");

		fgsInit();
		
		printf("Waiting 5 seconds...\n");
		Sleep(5000);

		GSM_Execute(FGSSTATE_WAITING);

		return 0;
	}

	GSM_Execute(DBSTATE_INIT);

	EXCEPTION_HANDLER_END
}


enumTransactionOutcome ConfirmPlayerCreate(ATR_ARGS, void *newPlayer, void *backupPlayer, GlobalType locationType, ContainerID locationID)
{
	ContainerSchema *schema = objFindContainerSchema(GLOBALTYPE_ENTITYPLAYER);
	U32 accountID = 0;
	U32 iVirtualShardID = 0;
	char *characterName = NULL;
	estrStackCreate(&characterName);
	if (!objPathGetEString(".pSaved.savedName", schema->classParse, newPlayer, &characterName))
	{
		estrDestroy(&characterName);
		TRANSACTION_RETURN_FAILURE("Player Name not specified");
	}
	if (!objPathGetInt(".pPlayer.accountID", schema->classParse, newPlayer, &accountID))
	{
		estrDestroy(&characterName);
		TRANSACTION_RETURN_FAILURE("Account ID not specified");
	}
	if (estrLength(&characterName) < 1)
	{
		estrDestroy(&characterName);
		TRANSACTION_RETURN_FAILURE("Player Name too short");
	}

	if (!objPathGetInt(".pPlayer.iVirtualShardID", schema->classParse, newPlayer, &iVirtualShardID))
	{
		estrDestroy(&characterName);
		TRANSACTION_RETURN_FAILURE("virtual shard ID not specified");
	}

	if (dbIDFromNameAndAccountID(GLOBALTYPE_ENTITYPLAYER, characterName, accountID, iVirtualShardID))
	{
		estrDestroy(&characterName);
		TRANSACTION_RETURN_FAILURE("Duplicate Player Name exists on Account");
	}

	estrDestroy(&characterName);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_RUN;
int registerDatabaseVersion(void)
{
	dbSetDatabaseCodeVersion(GetSchemaVersion());
	dbRegisterVersionTransition(GetSchemaVersion(), NULL, dbTransitionDeleteInvalidContainer);

	objRegisterSpecialTransactionCallback(GLOBALTYPE_ENTITYPLAYER, TRANSACTION_CREATE_CONTAINER, ConfirmPlayerCreate);

	return 1;
}

// ---- Code and data for costume processing ----

MP_DEFINE(PCBoneRef);
MP_DEFINE(PCCategoryRef);
MP_DEFINE(UIColor);
MP_DEFINE(PCExtraTexture);
MP_DEFINE(PCRegionRef);
MP_DEFINE(PCScaleEntry);
MP_DEFINE(PCScaleInfo);
MP_DEFINE(PCScaleInfoGroup);
MP_DEFINE(PCTextureDef);
MP_DEFINE(PCGeometryDef);
MP_DEFINE(PCMaterialDef);

AUTO_RUN;
void registerCostumeMemoryPools(void)
{
	MP_CREATE(PCBoneRef, 500);
	MP_CREATE(PCCategoryRef, 2000);
	MP_CREATE(UIColor, 300);
	MP_CREATE(PCExtraTexture, 500);
	MP_CREATE(PCRegionRef, 300);
	MP_CREATE(PCScaleEntry, 1000);
	MP_CREATE(PCScaleInfo, 300);
	MP_CREATE(PCScaleInfoGroup, 100);
}

#include "CostumeCommon_h_ast.c"

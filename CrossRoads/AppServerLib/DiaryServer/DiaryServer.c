/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "DiaryServer.h"
#include "DiaryCommon.h"
#include "AppServerLib.h"
#include "ServerLib.h"
#include "UtilitiesLib.h"
#include "GlobalTypeEnum.h"
#include "GenericHttpServing.h"
#include "AutoStartupSupport.h"
#include "AutoTransDefs.h"
#include "file.h"
#include "error.h"
#include "objSchema.h"
#include "winutil.h"
#include "objIndex.h"
#include "objContainer.h"
#include "structInternals.h"

#include "AutoGen/DiaryCommon_h_ast.h"
#include "AutoGen/DiaryEnums_h_ast.h"

#include "AutoGen/Controller_autogen_RemoteFuncs.h"

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

// Index of PlayerDiary objects by the player's entity container id
static ObjectIndex *playerIDIndex;

//
// Global container add/remove callbacks
//
// These are used to keep the by player id indices up to date.
//
static void
addPlayerDiaryContainer_CB(Container *container, PlayerDiary *diary)
{
	if ( !objIndexInsert(playerIDIndex, diary) )
	{
		printf("Failed to add PlayerDiary %u to player ID index\n", diary->entityID);
	}

	printf("PlayerDiary Add Callback, containerID:%u\n", diary->entityID);
}

static void
removePlayerDiaryContainer_CB(Container *container, PlayerDiary *diary)
{
	if ( !objIndexRemove(playerIDIndex, diary) )
	{
		printf("Failed to remove PlayerDiary %u from player ID index\n", diary->entityID);
	}

	printf("PlayerDiary Remove Callback, containerID:%u\n", diary->entityID);

}

static void
InitIndices(void)
{
	//For updating indices.
	objRegisterContainerTypeAddCallback(GLOBALTYPE_PLAYERDIARY, addPlayerDiaryContainer_CB);
	objRegisterContainerTypeRemoveCallback(GLOBALTYPE_PLAYERDIARY, removePlayerDiaryContainer_CB);

	playerIDIndex = objIndexCreateWithStringPath(0, 0, parse_PlayerDiary, ".entityID");
}

static void
InitSchemas(void)
{
	// registering schema in DiaryCommon.c now.
	// register native schemas here
	//objRegisterNativeSchema(GLOBALTYPE_PLAYERDIARY, parse_PlayerDiary, NULL, NULL, NULL, NULL, NULL);
	//if (!isProductionMode())
	//{	
	//	loadstart_printf("Writing out schema files... ");
	//	// export any native schemas here
	//	objExportContainerSchema(GLOBALTYPE_PLAYERDIARY);
	//	loadend_printf("Done.");
	//}
	objLoadAllGenericSchemas();
}

static void
InitLate(void)
{
	static GlobalType playerDiaryContainerType = GLOBALTYPE_PLAYERDIARY;

	InitSchemas();

	// set up container indices
	InitIndices();

	// acquire all diary containers from the objectdb
	aslAcquireContainerOwnership(&playerDiaryContainerType);

	RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), gServerLibState.containerID, "ready");
	ATR_DoLateInitialization();
}

static void 
InitSocket(void)
{
	// set up http server which will be used to accept XMLRPC requests from the 
	//  website and test programs
	GenericHttpServing_Begin(DIARY_SERVER_XMLRPC_PORT, "DiaryServer", DEFAULT_HTTP_CATEGORY_FILTER, 0);

	printf("Listening on http socket\n");
}

static int
InitTransactions(void)
{
	assertmsg(GetAppGlobalType() == GLOBALTYPE_DIARYSERVER, "Diary server type not set");

	loadstart_printf("Connecting DiaryServer to TransactionServer (%s)... ", gServerLibState.transactionServerHost);
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

static int 
DiaryServer_Init(void)
{
	AutoStartup_SetTaskIsOn("DiaryServer", 1);
	loadstart_printf("Running Auto Startup...");
	DoAutoStartup();
	loadend_printf(" done.");

	gAppServer->oncePerFrame = OncePerFrame;

	if ( !InitTransactions() )
	{
		return 0;
	}

	InitSocket();

	return 1;
}

AUTO_STARTUP(DiaryServer);
void DiaryServer_ServerStartup(void)
{
}

AUTO_RUN;
int 
DiaryServer_RegisterServer(void)
{
	aslRegisterApp(GLOBALTYPE_DIARYSERVER, DiaryServer_Init, 0); // Loads a bunch of stuff, can't do APPSERVERTYPEFLAG_NOPIGGSINDEVELOPMENT);
	return 1;
}


///////////////////////////////////////////////////////////////////////////////////////////
// 
///////////////////////////////////////////////////////////////////////////////////////////

//
// Set up the context to do a slow return, and return a copy of the slow command info
//
CmdSlowReturnForServerMonitorInfo *
DiaryServer_SetupSlowReturn(CmdContext *pContext)
{
	CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo;

	pContext->slowReturnInfo.bDoingSlowReturn = true;
	pSlowReturnInfo = malloc(sizeof(CmdSlowReturnForServerMonitorInfo));
	memcpy(pSlowReturnInfo, &pContext->slowReturnInfo, sizeof(CmdSlowReturnForServerMonitorInfo));

	return pSlowReturnInfo;
}

void
DiaryServer_CancelSlowReturn(CmdContext *pContext)
{
	pContext->slowReturnInfo.bDoingSlowReturn = false;
}

void
DiaryServer_BuildXMLResponseString(char **responseString, ParseTable *tpi, void *struct_mem)
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
DiaryServer_BuildXMLResponseStringWithType(char **responseString, char *type, char *val)
{
	estrPrintf(responseString, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<methodResponse><params><param><value><%s>%s</%s></value></param></params></methodResponse>",
		type, val, type);
	return;
}

//
// Initialize a new player diary, calling the transaction return callback when it is committed
//
static void
InitDiary(TransactionReturnVal *pReturn, ContainerID playerContainer)
{
	NOCONST(PlayerDiary) *diary;

	diary = StructCreateNoConst(parse_PlayerDiary);

	diary->entityID = playerContainer;

	diary->nextCommentID = 0;

	objRequestContainerCreate(pReturn, GLOBALTYPE_PLAYERDIARY, diary, objServerType(), objServerID());

	StructDestroy(parse_PlayerDiary, diary);
}

PlayerDiary *
DiaryServer_GetDiaryByPlayerID(U32 playerID)
{
	PlayerDiary *diary;

	ObjectIndexKey key = objIndexCreateKey_Int(playerIDIndex, playerID);

	if ( ( playerIDIndex->count > 0 ) && ( objIndexGet(playerIDIndex, key, 0, &diary) ) )
	{
		return diary;
	}

	return NULL;
}

//
// Get the title of a diary entry
//
// This function will need logic to get the properly localized string
//  from game generated entries such as missions.
static char *
GetEntryTitle(DiaryEntry *entry)
{
	char *ret = NULL;

	switch (entry->type)
	{
	case DiaryEntryType_Blog:
		ret = strdup(entry->comments[0]->title);
		break;
	case DiaryEntryType_MissionStarted:
		ret = strdup(entry->refName);
		break;
	default:
		break;
	}

	return ret;
}

static DiaryIndexEntry *
CreateIndexEntry(DiaryEntry *entry)
{
	DiaryIndexEntry *indexEntry = StructCreate(parse_DiaryIndexEntry);

	indexEntry->id = entry->id;
	indexEntry->time = entry->time;
	indexEntry->type = entry->type;
	indexEntry->title = GetEntryTitle(entry);

	return indexEntry;
}

static void 
CreateDiary_CB(TransactionReturnVal *pReturnVal, void *data)
{
}

DiaryIndex *
DiaryServer_GetIndex(ContainerID playerID)
{
	PlayerDiary *diary;
	DiaryIndex *index;
	DiaryIndexEntry *indexEntry;

	diary = DiaryServer_GetDiaryByPlayerID(playerID);

	index = StructCreate(parse_DiaryIndex);

	if ( diary == NULL )
	{
		// If the player doesn't have a diary, then create it and return an empty index.
		TransactionReturnVal *pReturn = objCreateManagedReturnVal(CreateDiary_CB, NULL);
		InitDiary(NULL, playerID);
	}
	else
	{
		FOR_EACH_IN_EARRAY(diary->entries, DiaryEntry, entry)
		{
			indexEntry = CreateIndexEntry(entry);

			eaPush(&index->entries, indexEntry);
		}
		FOR_EACH_END;
	}
	
	return index;	
}

DiaryEntry *
DiaryServer_GetEntry(ContainerID playerID, U32 entryID)
{
	DiaryEntry *entry;
	PlayerDiary *diary;

	diary = DiaryServer_GetDiaryByPlayerID(playerID);
	if ( diary == NULL )
	{
		// bad player ID
		return NULL;
	}

	if ( entryID >= (unsigned)eaSize(&diary->entries) )
	{
		// bad entry ID
		return NULL;
	}

	entry = eaIndexedGetUsingInt(&diary->entries, entryID);

	return entry;
}

#include "DiaryCommon_h_ast.c"
#include "DiaryEnums_h_ast.c"
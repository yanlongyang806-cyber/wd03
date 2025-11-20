#include "aslChatServerInit.h"
#include "ChatServer.h"
#include "ServerLib.h"
#include "loadsave.h"
#include "file.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "logging.h"
#include "objTransactions.h"
#include "objContainerIO.h"
#include "hoglib.h"
#include "AutoStartupSupport.h"
#include "GameAccountData/GameAccountData.h"

#include "StringCache.h"
#include "ScratchStack.h"
#include "sock.h"

#include "chatdb.h"
#include "chatdb_h_ast.h"
#include "chatGlobal.h"
#include "resourceManager.h"
#include "accountnet.h"
#include "users.h"

#include "GameAccountData_h_ast.h"
#include "AutoGen\accountnet_h_ast.h"
#include "AutoGen\aslChatServerInit_c_ast.h"
#include "AutoGen\GameServerLib_autogen_remotefuncs.h"

extern bool gbNoShardMode;
extern bool gbLocalShardOnlyMode;

extern void TextFilterLoad(void);

BulletinsStruct s_Bulletins = {0};
char gBulletinConfigFileName[CRYPTIC_MAX_PATH] = "server/Bulletins.txt";

static int SortBulletins(const BulletinDef** ppA, const BulletinDef** ppB)
{
	const BulletinDef* pA = (*ppA);
	const BulletinDef* pB = (*ppB);
	if (pA->pEvent && pB->pEvent)
	{
		return (int)(pA->pEvent->uEventTime - pB->pEvent->uEventTime);
	}
	return (int)(pA->uActivateTime - pB->uActivateTime);
}

#define LOAD_BULLETINS_SUCCESS "LOADED SUCCESSFULLY"
static char* aslBulletinsLoad(bool bLoadFromConfig)
{
	static char* s_estrResult = NULL;
	U32 uVersion = s_Bulletins.uVersion;
	S32 i;

	StructReset(parse_BulletinsStruct, &s_Bulletins);

	if (bLoadFromConfig)
	{
		if (!fileExists(gBulletinConfigFileName))
		{
			estrPrintf(&s_estrResult, "Failed to load bulletin config file (%s)", gBulletinConfigFileName);
			return s_estrResult;
		}
		else
		{
			if (!ParserReadTextFile(gBulletinConfigFileName, 
								    parse_BulletinsStruct,
								    &s_Bulletins,
								    0))
			{
				char* estrError = NULL;
				estrStackCreate(&estrError);
				ErrorfPushCallback(EstringErrorCallback, (void*)(&estrError));
				ParserReadTextFile(gBulletinConfigFileName, parse_BulletinsStruct, &s_Bulletins, 0);
				ErrorfPopCallback();
				estrPrintf(&s_estrResult, "Error while reading bulletin config file: %s", estrError);
				estrDestroy(&estrError);
				return s_estrResult;
			}
		}
	}
	else
	{
		ParserLoadFiles(NULL,
						"defs/config/Bulletins.def", 
						"Bulletins.bin", 
						PARSER_OPTIONALFLAG, 
						parse_BulletinsStruct, 
						&s_Bulletins);
	}

	for (i = eaSize(&s_Bulletins.eaDefs)-1; i >= 0; i--)
	{
		BulletinDef* pBulletin = s_Bulletins.eaDefs[i];
		const char* pchActiveDate = pBulletin->pchDisplayDate;
		const char* pchIgnoreDate = pBulletin->pchIgnoreDate;
		if (pchActiveDate && pchActiveDate[0])
		{
			U32 uTime = timeGetSecondsSince2000FromDateString(pchActiveDate);
			pBulletin->uActivateTime = uTime;
			StructFreeStringSafe(&pBulletin->pchDisplayDate);
			if (pBulletin->pEvent && pBulletin->pEvent->pchEventDate)
			{
				pchActiveDate = pBulletin->pEvent->pchEventDate;
				uTime = timeGetSecondsSince2000FromDateString(pchActiveDate);
				pBulletin->pEvent->uEventTime = uTime;
				StructFreeStringSafe(&pBulletin->pEvent->pchEventDate);
			}
		}
		if (pchIgnoreDate && pchIgnoreDate[0])
		{
			pBulletin->uIgnoreTime = timeGetSecondsSince2000FromDateString(pchIgnoreDate);
		}
	}
	eaQSort(s_Bulletins.eaDefs, SortBulletins);
	s_Bulletins.uVersion = uVersion + 1;

	return LOAD_BULLETINS_SUCCESS;
}

AUTO_COMMAND;
char* ReloadBulletins(void)
{
	return aslBulletinsLoad(true);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void aslUpdateBulletins(ContainerID uGameServerID, U32 uServerVersion)
{
	if (uServerVersion != s_Bulletins.uVersion)
	{
		RemoteCommand_gslBulletins_ReceiveList(GLOBALTYPE_GAMESERVER, uGameServerID, &s_Bulletins);
	}
	else
	{
		RemoteCommand_gslBulletins_ReceiveList(GLOBALTYPE_GAMESERVER, uGameServerID, NULL);
	}
}

static void aslReloadBulletins(const char *pchPath, S32 iWhen)
{
	if (pchPath)
	{
		fileWaitForExclusiveAccess(pchPath);
		errorLogFileIsBeingReloaded(pchPath);
	}
	aslBulletinsLoad(false);
}

AUTO_STARTUP(ChatServer, 1) ASTRT_DEPS(AS_Messages);
void aslChatServerStartup(void)
{
	aslBulletinsLoad(false);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/Bulletins.def", aslReloadBulletins);
}

char gChatDBLogFilename[MAX_PATH];
static void dbLogRotateCB(char *baseHog, char *oldIncrementalHog, char *newIncrementalHog)
{
	char *fname_orig = newIncrementalHog;
	char fname_final[MAX_PATH];
	char *dotStart = FindExtensionFromFilename(fname_orig);

	if (gChatDBLogFilename[0])
	{
		logFlushFile(gChatDBLogFilename);
	}

	strncpy(fname_final,fname_orig,dotStart - fname_orig);
	strcat(fname_final,".log");

	strcpy(gChatDBLogFilename, fname_final);
	logSetFileOptions_Filename(gChatDBLogFilename,true,0,0,1);
}
static void dbLogCloseCB(char *baseHog, char *oldIncrementalHog, char *newIncrementalHog)
{
	logCloseAllLogs();
}

static void dbLogTransaction(const char *commitString, U64 trSeq, U32 timestamp)
{
	static char *logString;
	static char timeString[128];
	size_t size = strlen(commitString);

	PERFINFO_AUTO_START("chatdbLogTransaction",1);

	if (!gChatDBLogFilename[0])
	{
		sprintf(gChatDBLogFilename,"%s/DB.log",chatServerGetDatabaseDir());			
		logSetFileOptions_Filename(gChatDBLogFilename,true,0,0,1);
	}

	estrClear(&logString);

	timeMakeDateStringFromSecondsSince2000(timeString,timestamp);
	estrPrintf(&logString,"%"FORM_LL"u %s: %s\n",trSeq, timeString,commitString);
	logDirectWrite(gChatDBLogFilename,logString);

	PERFINFO_AUTO_STOP();
}
CmdList gChatdbUpdateCmdList = {1,0,0};
static void chatdbHandleDatabaseUpdateStringEx(const char *cmd_orig, U64 sequence, U32 timestamp, bool replay)
{
	int result = 0;
	static CmdContext context = {0};
	static char *message;
	static char *cmd_start;
	char *lineBuffer, *cmd;
	PERFINFO_AUTO_START("chatdbHandleDatabaseUpdateString",1);

	context.output_msg = &message;
	context.access_level = 9;
	context.multi_line = true;

	estrCopy2(&cmd_start, cmd_orig);
	cmd = cmd_start;

	// This may cause a log rotation as well
	objRotateIncrementalHog();

	if (!timestamp)
	{
		timestamp = timeSecondsSince2000();
	}
	if (!sequence)
	{
		sequence = objContainerGetSequence() + 1;
	}
	objContainerSetSequence(sequence);
	objContainerSetTimestamp(timestamp);

	if (!replay) dbLogTransaction(cmd, sequence, timestamp);
	while (cmd)
	{
		lineBuffer = cmdReadNextLine(&cmd);
		result = cmdParseAndExecute(&gChatdbUpdateCmdList,lineBuffer,&context);
		if (result)
		{
			S64 val;
			bool valid = false;
			val = MultiValGetInt(&context.return_val,&valid);
			if (val && valid)
			{
				result = 1;
			}
			else
			{
				result = 0;
			}
		}
		if (!result)
		{
			Errorf("Error \"%s\" while executing DBUpdateCommand: %s",message,lineBuffer);
		}
	}
	PERFINFO_AUTO_STOP();
}
static void chatdbUpdateCB(TransDataBlock ***pppBlocks, void *pUserData)
{
	int i;
	for (i=0; i < eaSize(pppBlocks); i++)
	{
		char *pString = (*pppBlocks)[i]->pString1;
		if (pString)
		{
			chatdbHandleDatabaseUpdateStringEx(pString, 0, 0, false);
		}
		pString = (*pppBlocks)[i]->pString2;
		if (pString)
		{
			chatdbHandleDatabaseUpdateStringEx(pString, 0, 0, false);
		}
	}
}

AUTO_COMMAND ACMD_LIST(gChatdbUpdateCmdList);
int dbUpdateContainer(U32 containerType, U32 containerID, ACMD_SENTENCE diffString)
{
	Container *pObject;
	if (!containerType)
	{
		return 0;
	}
	PERFINFO_AUTO_START("dbUpdateContainer",1);
	verbose_printf("Updating data of container %s[%d] using diff %s\n",GlobalTypeToName(containerType),containerID,diffString);

	pObject = objGetContainer(containerType,containerID);

	if (pObject)
	{
		objModifyContainer(pObject,diffString);
	}
	else
	{
		if (!objAddToRepositoryFromString(containerType,containerID,diffString))
		{
			Errorf("Couldn't add to repository");
			PERFINFO_AUTO_STOP();
			return 0;
		}
	}
	PERFINFO_AUTO_STOP();
	return 1;
}
// Permanently destroys a container
AUTO_COMMAND ACMD_LIST(gChatdbUpdateCmdList);
int dbDestroyContainer(char *containerTypeName, U32 containerID)
{
	GlobalType containerType = NameToGlobalType(containerTypeName);
	if (!containerType)
	{
		return 0;
	}
	PERFINFO_AUTO_START("dbDestroyContainer",1);

	verbose_printf("Permanently destroying container %s[%d]\n",containerTypeName,containerID);
	objRemoveContainerFromRepository(containerType,containerID);

	PERFINFO_AUTO_STOP();
	return 1;
}

void chatdbHandleDatabaseReplayString(const char *cmd_orig, U64 sequence, U32 timestamp)
{
	chatdbHandleDatabaseUpdateStringEx(cmd_orig, sequence, timestamp, true);
}

bool gbIgnoreContainerSource = false;
AUTO_CMD_INT(gbIgnoreContainerSource, StaticHogg) ACMD_CMDLINE;

AUTO_STRUCT;
typedef struct DatabaseConfig
{
	char ** httpRequestIpList;
	char	*pDatabaseDir;						//"" Defaults to C:/<Product Name>/chatdb
	bool	bNoHogs;							//force discrete container file storage.

	bool	bShowSnapshots;						//Whether to show/hide the snapshot console window.
	bool	bFastSnapshot;						//If we're making a snapshot do it the fast hog->hog way
	bool	bBackupSnapshot;					//After making a snapshot back it up ourselves, only works with fast snapshot
	bool    bForceSnapshot; // Try to force the Chat Server to save a snapshot

	U32		iIncrementalInterval;				//The number of minutes between incremental hog rotation (defaults to 5)
	U32		iSnapshotInterval;					//The number of minutes between snapshot creation		 (defaults to 60)
	U32		iIncrementalHoursToKeep;			//The number of hours to keep incremental files around.	 (default to 4)
} DatabaseConfig;

#define CHATSERVER_HOG_BUFFER_SIZE 1024*1024*1024
DatabaseConfig gDatabaseConfig = {0};
char gDBConfigFileName[CRYPTIC_MAX_PATH] = "DBConfig.txt";
U32 * gChatServerHttpIps = NULL;

AUTO_CMD_INT(gDatabaseConfig.bForceSnapshot, ForceHoggSnapshot) ACMD_CMDLINE;

int getSnapshotInterval(void)
{
	return gDatabaseConfig.iSnapshotInterval;
}

static void ChatServerConfigureDatabase(void)
{
	objSetContainerSourceToHogFile(STACK_SPRINTF("%s/chatserver_local.hogg", chatServerGetDatabaseDir()),
		0, NULL, NULL);
}

static void ChatServerLoadDatabase(void)
{
	// does nothing
	objContainerLoadingFinished();
}

int ChatServerLibInit(void)
{
	// Chat Server-specific init
	loadstart_printf("Running Auto Startup...");
	DoAutoStartup();
	loadend_printf(" done.");

	resFinishLoading();

	assertmsg(GetAppGlobalType() == GLOBALTYPE_CHATSERVER, "chat server type not set");

	objRegisterNativeSchema(GLOBALTYPE_CHATCHANNEL, parse_ChatChannel, NULL, NULL, NULL, NULL, NULL);
	objRegisterNativeSchema(GLOBALTYPE_CHATUSER, parse_ChatUser, NULL, NULL, NULL, NULL, NULL);
	objRegisterNativeSchema(GLOBALTYPE_GAMEACCOUNTDATA, parse_GameAccountData, NULL, NULL, NULL, NULL, NULL);

	objLoadAllGenericSchemas();

	chatDbPreInit();
	ChatServerConfigureDatabase();
	ChatServerLoadDatabase();

	loadstart_printf("Connecting ChatServer to TransactionServer (%s)... ", gServerLibState.transactionServerHost);
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
		loadend_printf("failed.\n");
		return 0;
	}

	//RegisterDBUpdateDataCallback(objLocalManager(), &chatdbUpdateCB);
	//RegisterAsDefaultOwnerOfObjectTypeWithTransactionServer(objLocalManager(),GLOBALTYPE_CHATUSER);
	//RegisterAsDefaultOwnerOfObjectTypeWithTransactionServer(objLocalManager(),GLOBALTYPE_CHATCHANNEL);

	initLocalChatServer();

	loadend_printf("connected.\n");

	// Logging stuff
	//logSetDir(chatServerGetLogDir());
	logEnableHighPerformance();
	logAutoRotateLogFiles(true);
	chatDbInit();
	chatGuildsInit();
	return 1;
}

#define STRING_CACHE_TOTAL_SIZE			(1024*1024*2)
AUTO_RUN_SECOND;
int InitializeStringCache(void)
{	
	ScratchStackSetThreadSize(1024*1024*2);
	if(isProductionMode())
		stringCacheSetInitialSize(STRING_CACHE_TOTAL_SIZE);
	return 1;
}

void GameAccountDictChanged(enumResourceEventType eType, const char *pDictName, const char *pRefData, Referent pReferent, void *pUserData)
{
	GameAccountData *pData = (GameAccountData*) pReferent;
	switch(eType)
	{
		xcase RESEVENT_RESOURCE_ADDED: {
		}
	}
}

AUTO_RUN;
void chatServerAutoRun(void)
{
	RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GAMEACCOUNTDATA), false, parse_GameAccountData, false, false, NULL);
	resDictRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GAMEACCOUNTDATA), RES_DICT_KEEP_NONE, true, objCopyDictHandleRequest);
	resDictRegisterEventCallback(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GAMEACCOUNTDATA),GameAccountDictChanged, NULL);
}

#include "AutoGen\aslChatServerInit_c_ast.c"
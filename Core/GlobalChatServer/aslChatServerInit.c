#include "aslChatServerInit.h"

#include "accountnet.h"
#include "AutoStartupSupport.h"
#include "AutoTransDefs.h"
#include "chatdb.h"
#include "chatGlobal.h"
#include "ChatServer.h"
#include "chatShardCluster.h"
#include "ChatServer/chatShared.h"
#include "file.h"
#include "hoglib.h"
#include "loadsave.h"
#include "logging.h"
#include "objTransactions.h"
#include "objContainerIO.h"
#include "objTransactionCommands.h"
#include "objMerger.h"
#include "objBackupCache.h"
#include "resourceManager.h"
#include "ScratchStack.h"
#include "ServerLib.h"
#include "sock.h"
#include "StringCache.h"
#include "users.h"

#include "AutoGen/accountnet_h_ast.h"
#include "AutoGen/aslChatServerInit_c_ast.h"
#include "AutoGen/chatdb_h_ast.h"
#include "AutoGen/chatShardCluster_h_ast.h"

extern bool gbCreateSnapshotMode;
extern bool gbImportUserAccounts;

extern void TextFilterLoad(void);
void ChatServer_RegisterFastLocalCopy(void);

AUTO_STARTUP(ChatServer, 1) ASTRT_DEPS(AS_Messages);
void aslChatServerStartup(void)
{
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
static bool sbDBConfigLoaded = false;
char gDBConfigFileName[CRYPTIC_MAX_PATH] = "DBConfig.txt";
U32 * gChatServerHttpIps = NULL;

const DatabaseConfig *LoadDatabaseConfig(void)
{
	if (sbDBConfigLoaded)
	{
		return &gDatabaseConfig;
	}

	if (fileExists(STACK_SPRINTF("%s/%s", chatServerGetDatabaseDir(), gDBConfigFileName)))
	{
		printf("\nLoading Config File... \n\n");
		log_printf(LOG_CHATSERVER, "Loading Config File...");
		if (ParserReadTextFile(STACK_SPRINTF("%s/%s", chatServerGetDatabaseDir(), gDBConfigFileName), parse_DatabaseConfig, &gDatabaseConfig, 0))
		{
			EARRAY_CONST_FOREACH_BEGIN(gDatabaseConfig.httpRequestIpList, i, s);
			{
				eaiPush(&gChatServerHttpIps, ipFromString(gDatabaseConfig.httpRequestIpList[i]));
			}
			EARRAY_FOREACH_END;
		}
	}

	//clean up config defaults
	if (!gDatabaseConfig.iSnapshotInterval) 
		gDatabaseConfig.iSnapshotInterval = 60; //Default snapshots to an hour.
	if (!gDatabaseConfig.iIncrementalInterval && !gDatabaseConfig.bNoHogs) 
		gDatabaseConfig.iIncrementalInterval = 5; //Default incrementals to 5min.
	if (!gDatabaseConfig.iIncrementalHoursToKeep && !gDatabaseConfig.bNoHogs) 
		gDatabaseConfig.iIncrementalHoursToKeep = 4;

	sbDBConfigLoaded = true;
	return &gDatabaseConfig;
}

AUTO_CMD_INT(gDatabaseConfig.bForceSnapshot, ForceHoggSnapshot) ACMD_CMDLINE;

int getSnapshotInterval(void)
{
	if (!sbDBConfigLoaded)
		LoadDatabaseConfig();
	return gDatabaseConfig.iSnapshotInterval;
}

static void ChatServerConfigureDatabase(void)
{
	objSetContainerSourceToHogFile(STACK_SPRINTF("%s/chatserver.hogg", chatServerGetDatabaseDir()),
		gDatabaseConfig.iIncrementalInterval, dbLogRotateCB, dbLogCloseCB);
}

static void ChatServerLoadDatabase(void)
{
	objLoadAllContainers();
	LocalTransactionsTakeOwnership();
	objContainerLoadingFinished();
}

int ChatServerLibInit(void)
{
	PERFINFO_AUTO_START_FUNC();
	if (gbCreateSnapshotMode)
	{
		if (!LockMerger(GLOBALCHATSERVER_MERGER_NAME))
		{
			log_printf(LOG_CHATSERVER, "Merger already running");
			PERFINFO_AUTO_STOP_FUNC();
			return 0;
		}
	}
	// Chat Server-specific init
	loadstart_printf("Running Auto Startup...");
	DoAutoStartup();
	loadend_printf(" done.");

	resFinishLoading();

	loadstart_printf("Loading ChatDB...\n");
	objLoadAllGenericSchemas();

	assertmsg(GetAppGlobalType() == GLOBALTYPE_GLOBALCHATSERVER, "chat server type not set");

	objRegisterNativeSchema(GLOBALTYPE_CHATCHANNEL, parse_ChatChannel, NULL, NULL, NULL, NULL, NULL);
	objRegisterNativeSchema(GLOBALTYPE_CHATUSER, parse_ChatUser, NULL, NULL, NULL, NULL, NULL);
	ChatCluster_InitContainers();
	ChatServer_RegisterFastLocalCopy();

	chatDbPreInit();
	InitObjectLocalTransactionManager(GLOBALTYPE_GLOBALCHATSERVER, NULL);
	LoadDatabaseConfig();

	objSetContainerIgnoreSource(gbIgnoreContainerSource);
	objSetSnapshotMode(gbCreateSnapshotMode);

	if (gbIgnoreContainerSource)
	{
		gbCreateSnapshotMode = true;
		objSetContainerForceSnapshot(true);
		objSetSnapshotMode(true);
	}
	else if (gDatabaseConfig.bForceSnapshot)
		objSetContainerForceSnapshot(true);
	hogSetGlobalOpenMode(HogSafeAgainstAppCrash);
	hogSetMaxBufferSize(CHATSERVER_HOG_BUFFER_SIZE);

	objSetIncrementalHoursToKeep(gDatabaseConfig.iIncrementalHoursToKeep);
	objSetCommandReplayCallback(chatdbHandleDatabaseReplayString);
	ChatServerConfigureDatabase();

	RegisterDBUpdateDataCallback(objLocalManager(), &chatdbUpdateCB);
	if (gbCreateSnapshotMode)
	{
		objMergeIncrementalHogs(gDatabaseConfig.bBackupSnapshot, false, false);
		UnlockMerger();
		PERFINFO_AUTO_STOP_FUNC();
		return 1;
	}
	else
		ChatServerLoadDatabase();
	loadend_printf(" done.");

	if (!gbCreateSnapshotMode && !gbImportUserAccounts)
	{
		loadstart_printf("Global Chat Server Initialization...");
		initGlobalChatServer();
		loadend_printf(" done.");
	}

	if (gbImportUserAccounts)
	{
		AccountGenericList userList = {0};
		char importPath[MAX_PATH];
		int i, size;

		loadstart_printf("Importing users from file...\n");
		chatDbInit(); // init stash tables of names
		sprintf(importPath, "%s/%s", chatServerGetDatabaseDir(), ACCOUNTUSER_EXPORT_FILENAME);
		ParserReadTextFile(importPath, parse_AccountGenericList, &userList, 0);
		size = eaSize(&userList.ppAccounts);
		for (i=0; i<size; i++)
		{
			AccountTicket *account = userList.ppAccounts[i];
			ChatUser *existingUser= NULL;

			if (account->accountID == 0 || !account->accountName[0] || !account->displayName[0])
				continue;
			if (existingUser = userFindByContainerId(account->accountID))
			{
				if (stricmp(account->accountName, existingUser->accountName)) // account names do NOT match
				{
					/*FatalErrorf("Trying to import an Account ID with an account name that does not match the existing data! ID: %d, Account name: '%s', Existing User: '%s'.", 
					account->accountID, account->accountName, existingUser->accountName);*/
					userDelete(existingUser->id);
				}
				else
				{
					if (stricmp(account->displayName, existingUser->handle)) // display names do not match
					{
						userChangeChatHandle(existingUser->id, account->displayName);
					}
					continue;
				}
			}
			if (existingUser = userFindByAccountName(account->accountName))
			{
				// This is very bad!
				//FatalErrorf("Trying to import an Account with an account name that is already in the database! Account name: '%s'.", account->accountName);
				userDelete(existingUser->id);
			}
			// This overrides existing user display names
			userAdd(NULL, account->accountID, account->accountName, account->displayName);
		}
		loadend_printf("Finished Importing.\n");
	}
	else if (!gbCreateSnapshotMode)
	{
		// Logging stuff
		//logSetDir(chatServerGetLogDir());
		logEnableHighPerformance();
		logAutoRotateLogFiles(true);
		chatDbInit();
		ATR_DoLateInitialization();
	}
	PERFINFO_AUTO_STOP_FUNC();
	return 1;
}

AUTO_RUN_SECOND;
int InitializeStringCache(void)
{	
	ScratchStackSetThreadSize(1024*1024*2);
	stringCacheSetInitialSize(1024*256);
	return 1;
}

// Fast Local Copy stuff
void* ChatServerGetObject(GlobalType globalType, U32 uID)
{
	Container *con = objGetContainer(globalType, uID);
	if (con)
		return con->containerData;
	return NULL;
}

void *ChatServerGetBackup(GlobalType type, ContainerID id)
{
	Container *con = objGetContainer(type, id);
	if (!con)
		return NULL;
	return BackupCacheGet(con);
}

void ChatServer_RegisterFastLocalCopy(void)
{
	BackupCache_RegisterType(GLOBALTYPE_CHATUSER, BACKUPCACHE_STASH, 100000);
	RegisterFastLocalCopyCB(GLOBALTYPE_CHATUSER, ChatServerGetObject, ChatServerGetBackup);
	BackupCache_RegisterType(GLOBALTYPE_CHATCHANNEL, BACKUPCACHE_STASH, 100000);
	RegisterFastLocalCopyCB(GLOBALTYPE_CHATCHANNEL, ChatServerGetObject, ChatServerGetBackup);
	BackupCache_RegisterType(GLOBALTYPE_CHATSHARD, BACKUPCACHE_STASH, 10);
	RegisterFastLocalCopyCB(GLOBALTYPE_CHATSHARD, ChatServerGetObject, ChatServerGetBackup);
	BackupCache_RegisterType(GLOBALTYPE_CHATCLUSTER, BACKUPCACHE_STASH, 3);
	RegisterFastLocalCopyCB(GLOBALTYPE_CHATCLUSTER, ChatServerGetObject, ChatServerGetBackup);
}


#include "AutoGen\aslChatServerInit_c_ast.c"
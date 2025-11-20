#include "autogen/SVNUtils_h_ast.h"
#include "AppVersion.h"
#include "CrypticPorts.h"
#include "EString.h"
#include "FilespecMap.h"
#include "fileutil.h"
#include "filewatch.h"
#include "FolderCache.h"
#include "GlobalComm.h"
#include "hoglib.h"
#include "logging.h"
#include "memlog.h"
#include "MemoryMonitor.h"
#include "net.h"
#include "net/netlink.h"
#include "netpacketutil.h"
#include "PatchClientLibStatusMonitoring.h"
#include "PatchClientLibStatusMonitoring_h_ast.h"
#include "patchcommonutils.h"
#include "patchdb.h"
#include "patcher_comm_h_ast.h"
#include "patchfilespec.h"
#include "patchtrivia.h"
#include "patchxfer.h"
#include "pcl_client.h"
#include "pcl_client_internal.h"
#include "pcl_client_struct.h"
#include "pcl_client_struct_h_ast.h"
#include "pcl_typedefs_h_ast.h"
#include "ScratchStack.h"
#include "StashTable.h"
#include "StringCache.h"
#include "strings_opt.h"
#include "StringTable.h"
#include "stringutil.h"
#include "structNet.h"
#include "sysutil.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "trivia.h"
#include "UnitSpec.h"
#include "zutils.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FileSystem););

bool g_patcher_encrypt_connections=false;
static bool g_no_redirect=false;
static bool g_no_http=false;
bool g_PatchStreamingSimulate=false;

// PCL memlog and verbose memlog
MemLog pclmemlog={0};
MemLog pclmemlog2={0};

CRITICAL_SECTION StaticCmdPerfMutex;

AUTO_RUN;
void initializeStaticCmdPerfMutex(void)
{
	InitializeCriticalSection(&StaticCmdPerfMutex);
}

AUTO_RUN;
void initializeMemlog(void)
{
	memlog_enableThreadId(&pclmemlog);
	memlog_enableThreadId(&pclmemlog2);
}

void pclMSpf_dbg(int level, const char* format, ...){
	char *string = NULL;
	estrStackCreate(&string);
	estrGetVarArgs(&string, format);
	if (level < 2)
		memlog_printf(&pclmemlog, "%s", string);
	memlog_printf(&pclmemlog2, "%s", string);
	estrDestroy(&string);

#ifdef PCL_CRAZY_DEBUG
	static U32 lastTime;
	static U32 count;

	U32 curTime = timeGetTime();
	U32 color;
	
	if(lastTime && curTime - lastTime > 500){
		color = COLOR_RED;
	}else{
		color = COLOR_GREEN;
	}
	
	lastTime = curTime;

	count++;
	printfColor(COLOR_BRIGHT|color, "%3.3d-%5.5d: ", count % 1000, curTime % 60000);
	
	VA_START(va, format);
	{
		char buffer[5000];
		vsprintf(buffer, format, va);
		printfColor(color, "%s%s", buffer, strEndsWith(buffer, "\n") ? "" : "\n");
	}
	VA_END();
#endif
}

const char* pclStateGetName(PCL_State state)
{
	switch(state)
	{
		#define CASE(x) case x:return #x + 6;
		CASE(STATE_NONE);
		CASE(STATE_CONNECTING);
		CASE(STATE_AUTOUPDATE);
		CASE(STATE_IDLE);
		CASE(STATE_WAITING);
		CASE(STATE_XFERRING);
		CASE(STATE_FILE_HISTORY);
		CASE(STATE_VERSION_INFO);
		CASE(STATE_SET_VIEW);
		CASE(STATE_VIEW_RESPONSE);
		CASE(STATE_GET_MANIFEST);
		CASE(STATE_GET_FILE);
		CASE(STATE_NAME_VIEW);
		CASE(STATE_SET_EXPIRATION);
		CASE(STATE_SET_AUTHOR);
		CASE(STATE_CHECKIN_FILES);
		CASE(STATE_SENT_NAMES);
		CASE(STATE_SENDING_DATA);
		CASE(STATE_SENT_DATA);
		CASE(STATE_CHECKIN_FINISHING);
		CASE(STATE_FORCEIN_DIR);
		CASE(STATE_LOCK_FILE);
		CASE(STATE_LOCK_REQ);
		CASE(STATE_GETTING_LOCKED_FILE);
		CASE(STATE_UNLOCK_FILES);
		CASE(STATE_UNLOCK_REQ);
		CASE(STATE_GETTING_UNLOCKED_FILE);
		CASE(STATE_GET_REQUIRED_FILES);
		CASE(STATE_NOTIFY_ME);
		CASE(STATE_GET_HEADERS);
		CASE(STATE_GET_FILE_LIST);
		CASE(STATE_PROJECT_LIST);
		CASE(STATE_BRANCH_INFO);
		CASE(STATE_NAME_LIST);
		CASE(STATE_CHECK_DELETED);
		CASE(STATE_GET_FILEVERSION_INFO);
		CASE(STATE_IS_COMPLETELY_SYNCED);
		CASE(STATE_SET_FILE_EXPIRATION);
		#undef CASE
		default:{
			if(state >= 0 && state < STATE_COUNT){
				return "NAME NOT SET!!!!";
			}else{
				return "UNKNOWN!!!!!!!!!";
			}
		}
	}
}

const char* patchServerCmdGetName(int cmd)
{
	switch(cmd)
	{
		#define CASE(x) case x:return #x + 12;
		CASE(PATCHSERVER_CONNECT_OK);
		CASE(PATCHSERVER_FINGERPRINTS);
		CASE(PATCHSERVER_BLOCKS);
		CASE(PATCHSERVER_FILEINFO);
		CASE(PATCHSERVER_PROJECT_VIEW_STATUS);
		CASE(PATCHSERVER_AUTHOR_RESPONSE);
		CASE(PATCHSERVER_FILE_HISTORY_DEPRECATED);
		CASE(PATCHSERVER_LOCK);
		CASE(PATCHSERVER_CHECKIN);
		CASE(PATCHSERVER_FORCEIN);
		CASE(PATCHSERVER_BLOCK_RECV);
		CASE(PATCHSERVER_FINISH_CHECKIN);
		CASE(PATCHSERVER_FINISH_FORCEIN);
		CASE(PATCHSERVER_VIEW_NAMED);
		CASE(PATCHSERVER_UPDATE);
		CASE(PATCHSERVER_BLOCKS_COMPRESSED);
		CASE(PATCHSERVER_HEADERINFO);
		CASE(PATCHSERVER_HEADER_BLOCKS);
		CASE(PATCHSERVER_PROJECT_LIST);
		CASE(PATCHSERVER_NAME_LIST);
		CASE(PATCHSERVER_FINGERPRINTS_COMPRESSED);
		CASE(PATCHSERVER_EXPIRATION_SET);
		CASE(PATCHSERVER_UNLOCK);
		CASE(PATCHSERVER_DONT_RECONNECT);
		CASE(PATCHSERVER_AUTOUPDATE_FILE);
		CASE(PATCHSERVER_VERSION_INFO);
		CASE(PATCHSERVER_BRANCH_INFO);
		CASE(PATCHSERVER_CHECK_DELETED);
		CASE(PATCHSERVER_LASTAUTHOR);
		CASE(PATCHSERVER_LOCKAUTHOR);
		CASE(PATCHSERVER_FILE_HISTORY_STRUCTS);
		CASE(PATCHSERVER_FILEVERSIONINFO);
		#undef CASE
		default:{
			if(cmd >= 0 && cmd < PATCHSERVER_CMD_COUNT){
				return "NAME NOT SET!!!!";
			}else{
				return "UNKNOWN!!!!!!!!!";
			}
		}
	}
}

#define LOGPACKET(client,fmt,...) pclMSpf(fmt, __VA_ARGS__)

// Check that the client is not in an error state.
#define CHECK_ERROR(client)												\
	do {																\
		if(!(client))													\
		{																\
			return PCL_CLIENT_PTR_IS_NULL;								\
		}																\
		else if((client)->error)										\
		{																\
			RETURN_ERROR((client), (client)->error);					\
		}																\
	} while (0)
#define CHECK_ERROR_PI(client)											\
	do {																\
		if(!(client))													\
		{																\
			PERFINFO_AUTO_STOP();										\
			return PCL_CLIENT_PTR_IS_NULL;								\
		}																\
		else if((client)->error)										\
		{																\
			RETURN_ERROR_PERFINFO_STOP((client), (client)->error);		\
		}																\
	} while (0)

// Helper for CHECK_STATE().
static bool pclCheckState(PCL_Client *client)
{
	if (client->state[0]->general_state != STATE_IDLE)
		return true;
	return false;
}

// Check if the client is ready to accept commands.
#define CHECK_STATE(client)												\
	do {																\
		if (pclCheckState((client)))									\
			return PCL_WAITING;											\
	} while (0)
#define CHECK_STATE_PI(client)											\
	do {																\
		if (pclCheckState((client)))									\
		{																\
			PERFINFO_AUTO_STOP();										\
			return PCL_WAITING;											\
		}																\
	} while (0)

static void setViewResponse(PCL_Client * client, Packet * pak, int cmd);
static void nameViewResponse(PCL_Client * client, Packet * pak, int cmd);
static void setExpirationResponse(PCL_Client *client, Packet *pak, int cmd);
static void setFileExpirationResponse(PCL_Client *client, Packet *pak, int cmd);
static void fileHistoryResponse(PCL_Client * client, Packet * pak, int cmd);
static void versionInfoResponse(PCL_Client *client, Packet *pak_in, int cmd);
static void lockFileResponse(PCL_Client * client, Packet * pak, int cmd);
static void unlockFileResponse(PCL_Client *client, Packet *pak_in, int cmd);
static void checkinFilesResponse(PCL_Client * client, Packet * pak, int cmd);
static void setAuthorResponse(PCL_Client * client, Packet * pak, int cmd);
static void notifyResponse(PCL_Client * client, Packet * pak, int cmd);
static void getProjectListResponse(PCL_Client * client, Packet * pak, int cmd);
static void getBranchInfoResponse(PCL_Client *client, Packet *pak_in, int cmd);
static void getNameListResponse(PCL_Client * client, Packet * pak, int cmd);
static void checkDeletedResponse(PCL_Client *client, Packet *pak, int cmd);
static void getLastAuthorResponse(PCL_Client * client, Packet * pak_in, int cmd);
static void getLockAuthorResponse(PCL_Client * client, Packet * pak_in, int cmd);
static void getCheckinsBetweenTimesResponse(PCL_Client * client, Packet * pak_in, int cmd);
static void getCheckinInfoResponse(PCL_Client * client, Packet * pak_in, int cmd);
static void getFileVersionInfoResponse(PCL_Client * client, Packet * pak_in, int cmd);
static void isCompletelySyncedResponse(PCL_Client *client, Packet *pak_in, int cmd);
static void undoCheckinResponse(PCL_Client * client, Packet * pak_in, int cmd);
static void pingResponse(PCL_Client * client, Packet * pak_in, int cmd);

static void checkinFilesProcess(PCL_Client * client);
static void setViewProcess(PCL_Client * client);

static void setViewSendPacket(PCL_Client * client);
static void fileHistorySendPacket(PCL_Client * client);
static void setAuthorSendPacket(PCL_Client * client);
static void nameViewSendPacket(PCL_Client * client);
static void setExpirationSendPacket(PCL_Client *client);
static void setFileExpirationSendPacket(PCL_Client *client);
static void lockReqSendPacket(PCL_Client * client);
static void unlockReqSendPacket(PCL_Client * client);
static void notifyMeSendPacket(PCL_Client * client);
static void checkinSendNames(PCL_Client * client, int cmd_out);
static void getFileVersionInfoSendPacket(PCL_Client * client);
static void isCompletelySyncedSendPacket(PCL_Client *client);
static void undoCheckinSendPacket(PCL_Client * client);
static void pingSendPacket(PCL_Client * client);
static void handleHttpInfoUpdateMsg(Packet * pak, int cmd, PCL_Client * client);
static void handleAutoupInfoUpdateMsg(Packet * pak, int cmd, PCL_Client * client);


#ifdef _XBOX
void initPatchDrive(void);
#endif

static int getFileInternal(PCL_Client *client, const char *fileName, int priority, const char *rootFolder, bool add_progress, PCL_GetFileCallback callback, void *userData);
static bool isFileUpToDateInternal(PCL_Client * client, const char * fileName, DirEntry * dir_entry, HogFile * hogg, int index_to_hogg);

static void handleConnectOkMsg(Packet *pak_in, int cmd, PCL_Client *client);
static void handleAutoUpdateMsg(Packet *pak_in, int cmd, PCL_Client *client);
static PCL_ErrorCode s_connectToServer(PCL_Client * client, const char *verb, char *error, size_t error_size);

static void deleteAndClean(const char *file_path);

int g_ExactTimestamps = 0;

static bool timestampsMatch(U32 a, U32 b)
{
	return a == b ||
		(!g_ExactTimestamps && 
		 // FAT32/FATX has a resolution of 2 seconds, so GetFileTime may not return the same value given to SetFileTime
		 (ABS_UNS_DIFF(a,b) <= 3 ||
		  ABS_UNS_DIFF(a,b) == 3600
		 )
		);
}

AUTO_RUN;
void patchRunOnce(void)
{
	static volatile bool ran = false;
	static volatile int num_running = 0;
	if(!ran)
	{
		if(InterlockedIncrement(&num_running) == 1)
		{
#ifdef _XBOX
			initPatchDrive();
#endif
			ran = true;
		}
		else
		{
			while(!ran)
				Sleep(1);
		}
	}
}

PCL_ErrorCode g_PCL_NON_FATAL_LIST[] =
{
	PCL_XFERS_FULL,
	PCL_LOST_CONNECTION,
	PCL_LOCK_FAILED,
	PCL_WAITING,
	PCL_SUCCESS, // Must be last
};

static void pclSendReport(PCL_Client *client, PCLStatusMonitoringState state, const char *string);

#define HUGE_ERROR_STR 2048

// Report a PCL error.
void report_error_strings_function(PCL_Client *client, PCL_ErrorCode error_code, const char *intro_str, const char *additional_str)
{
	
	bool pcl_fatal = false;
	bool softened = false;
	int pcl_error_index;
	char * pcl_err;

	PERFINFO_AUTO_START_FUNC();

	// Check if the error is fatal.
	for(pcl_error_index = 0; g_PCL_NON_FATAL_LIST[pcl_error_index]; pcl_error_index++)
	{
		if(error_code && g_PCL_NON_FATAL_LIST[pcl_error_index] == error_code)
			pcl_fatal = false;
	}

	// Check if the error has been softened.
	if (client)
	{
		if (client->soften_all_errors)
		{
			softened = true;
		}
		else
		{
			EARRAY_INT_CONST_FOREACH_BEGIN(client->softened_errors, i, n);
			{
				if (client->softened_errors[i] == error_code)
				{
					softened = true;
					break;
				}
			}
			EARRAY_FOREACH_END;
		}
	}

	// Print error information.
	pcl_err = ScratchAlloc(HUGE_ERROR_STR);
	if(intro_str)
	{
		strcat_s(pcl_err, HUGE_ERROR_STR, (intro_str));
		strcat_s(pcl_err, HUGE_ERROR_STR, ": ");
	}
	snprintf_s(pcl_err + strlen(pcl_err), HUGE_ERROR_STR - (int)strlen(pcl_err), "%i\n", error_code);
	pclGetErrorString((error_code), pcl_err + strlen(pcl_err), HUGE_ERROR_STR - (int)strlen(pcl_err));
	if(additional_str)
	{
		strcat_s(pcl_err, HUGE_ERROR_STR, " - ");
		strcat_s(pcl_err, HUGE_ERROR_STR, (additional_str));
	}
	if (!softened)
	{
		// TODO: Should this be PCLLOG_FILEONLY?  Some PCL users, like CrypticLauncher, do their own printing, which results in duplication.  But perhaps CL
		// is the one doing it wrong.
		if (client)
			pcllog(client, PCLLOG_ERROR, "%s", pcl_err);

		if (client)
			pclSendReport(client, PCLSMS_FAILED, pcl_err);
		Errorf("%s", pcl_err);
		assertmsgf(!pcl_fatal && (!client || !((PCL_Client*)client)->assert_on_error),
			"patchclientlib asserting on error! %s", pcl_err);
		
	}
	ScratchFree(pcl_err);

	// Save error code.
	if(client)
	{
		client->error = error_code;
		if (additional_str && *additional_str)
			client->error_details = strdup(additional_str);
		else
			client->error_details = NULL;
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static bool isLockedByAnother(PCL_Client *client, DirEntry *dir)
{
	return dir->lockedby && stricmp(dir->lockedby, NULL_TO_EMPTY(client->author));
}

// TODO: Unify pcllog, patchmelog, pclMSpf, pclSendReport, pclSendLog(), and explicit printfs in some sane way.
void pcllog_dbg(PCL_Client *client, PCL_LogLevel level, const char *fmt, ...)
{
	char *str = NULL;
	char levelstrbuf[21];
	const char *levelstr;

	PERFINFO_AUTO_START_FUNC();

	if (level == PCLLOG_TITLE_DISCARDABLE) {
		U32 ms = timerCpuMs();
		if (ms - client->titleLogLastUpdate > 100) {
			client->titleLogLastUpdate = ms;
			level = PCLLOG_TITLE;
		} else {
			PERFINFO_AUTO_STOP_FUNC();
			return;
		}
	} else if (level == PCLLOG_TITLE) {
		client->titleLogLastUpdate = timerCpuMs();
	}

	estrStackCreate(&str);
	VA_START(args, fmt);
	estrConcatfv(&str, fmt, args);
	VA_END();

	levelstr = StaticDefineIntRevLookup(PCL_LogLevelEnum, level);
	if (!levelstr)
	{
		sprintf(levelstrbuf, "%d", (int)level);
		levelstr = levelstrbuf;
	}

	pclMSpf("pcllog: (%s) %s", levelstr, str);

	if(level < PCLLOG_PACKET && client->log_function)
	{
		client->log_function(level, str, client->log_userdata);
	}
	else switch(level)
	{
		xcase PCLLOG_ERROR:
			filelog_printf("pcl", "Error: %s", str);
			printf("PatchClientLib Error: %s\n", str);
		xcase PCLLOG_WARNING:
			filelog_printf("pcl", "Warning: %s", str);
			printf("PatchClientLib Warning: %s\n", str);
		xcase PCLLOG_INFO:
			filelog_printf("pcl", "%s", str);
			printf("PatchClientLib: %s\n", str);
		xcase PCLLOG_LINK:
		acase PCLLOG_SPAM:
			printf("PatchClientLib: %s\n", str);
		xcase PCLLOG_FILEONLY:
			filelog_printf("pcl", "%s", str);
		xcase PCLLOG_TITLE:
#if !PLATFORM_CONSOLE
			setConsoleTitle(str);
#else
			printf("%s\n");
#endif
		xcase PCLLOG_PACKET:
			filelog_printf("packetsent", "%s", str);
	}
	estrDestroy(&str);

	PERFINFO_AUTO_STOP_FUNC();
}

static void fileSpecPclLog(const char *string, void *client)
{
	// FIXME: [COR-13177] client is bad, for some reason.
	//pcllog(client, PCLLOG_INFO, "%s", string);
}

PCL_ErrorCode pclSetLoggingFunction(PCL_Client *client, PCL_LoggingFunction func, void *userdata)
{
	if(!client)
		return PCL_CLIENT_PTR_IS_NULL;
	if(!func)
		RETURN_ERROR(client, PCL_NULL_PARAMETER);

	client->log_function = func;
	client->log_userdata = userdata;
	return PCL_SUCCESS;
}

PCL_ErrorCode pclAssertOnError(PCL_Client * client, bool assert_on_error)
{
	CHECK_ERROR(client);

	client->assert_on_error = assert_on_error;

	return PCL_SUCCESS;
}

// Make an error soft, so that it will not Errorf() or print error information.
PCL_ErrorCode pclSoftenError(PCL_Client * client, PCL_ErrorCode error_code)
{
	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);

	eaiPushUnique(&client->softened_errors, error_code);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

PCL_ErrorCode pclSoftenAllErrors(PCL_Client * client)
{
	CHECK_ERROR_PI(client);
	client->soften_all_errors = true;
	return PCL_SUCCESS;
}

PCL_ErrorCode pclUnsoftenAllErrors(PCL_Client * client)
{
	CHECK_ERROR_PI(client);
	client->soften_all_errors = false;
	return PCL_SUCCESS;
}


// Reverse the effect of pclSoftenError().
PCL_ErrorCode pclUnsoftenError(PCL_Client * client, PCL_ErrorCode error_code)
{
	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);

	eaiFindAndRemoveFast(&client->softened_errors, error_code);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

PCL_ErrorCode pclReportError(PCL_Client * client, PCL_ErrorCode error_code, char * description)
{
	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);

	REPORT_ERROR_STRING(client, error_code, description);

	PERFINFO_AUTO_STOP_FUNC();

	return error_code;
}

PCL_ErrorCode pclErrorState(PCL_Client * client)
{
	if(client)
		return client->error;
	else
		return PCL_CLIENT_PTR_IS_NULL;
}

PCL_ErrorCode pclXfers(PCL_Client * client, int * xfers)
{
	CHECK_ERROR(client);

	if(xfers == NULL)
		RETURN_ERROR(client, PCL_NULL_PARAMETER);

	*xfers = eaSize(&client->xferrer->xfers);

	return PCL_SUCCESS;
}

PCL_ErrorCode pclMaxXfers(PCL_Client * client, int * max_xfers)
{
	CHECK_ERROR(client);

	if(max_xfers == NULL)
		RETURN_ERROR(client, PCL_NULL_PARAMETER);

	*max_xfers = MAX_XFERS;

	return PCL_SUCCESS;
}

PCL_ErrorCode pclRootDir(PCL_Client * client, char * buf, int buf_size)
{
	PERFINFO_AUTO_START_FUNC();

	if(!buf || !buf_size)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_NULL_PARAMETER);
	buf[0] = '\0';

	CHECK_ERROR_PI(client);

	if(client->root_folder)
	{
		char temp[MAX_PATH];
		machinePath(temp,client->root_folder);

		if(buf_size <= (int)strlen(temp))
			RETURN_ERROR_PERFINFO_STOP(client, PCL_STRING_BUFFER_SIZE_TOO_SMALL);

		strcpy_s(buf, buf_size, temp);
	}

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

static void stateDestroy(StateStruct * state)
{
	PERFINFO_AUTO_START_FUNC();

#define DESTROY_PRINTF(...) //printf(__VA_ARGS__)

	switch(state->general_state)
	{
		xcase STATE_IDLE:
		{
			DESTROY_PRINTF("Destroying STATE_IDLE\n");
		xcase STATE_QUERY_LASTAUTHOR:
			DESTROY_PRINTF("Destroying STATE_QUERY_LASTAUTHOR\n");
			SAFE_FREE(state->query_last_author.dbname);
		xcase STATE_QUERY_LOCKAUTHOR:
			DESTROY_PRINTF("Destroying STATE_QUERY_LOCKAUTHOR\n");
			SAFE_FREE(state->query_last_author.dbname);
		}
		xcase STATE_CONNECTING:
		{
			DESTROY_PRINTF("Destroying STATE_CONNECTING\n");
			SAFE_FREE(state->connect.autoupdate_path);
		}
		xcase STATE_WAITING:
		{
			switch(state->call_state)
			{
				xcase STATE_SET_AUTHOR:
					DESTROY_PRINTF("Destroying STATE_SET_AUTHOR\n");
				xcase STATE_FILE_HISTORY:
					DESTROY_PRINTF("Destroying STATE_FILE_HISTORY\n");
					SAFE_FREE(state->history.fileName);
				xcase STATE_VERSION_INFO:
					DESTROY_PRINTF("Destroying STATE_VERSION_INFO\n");
					SAFE_FREE(state->version_info.dbname);
				xcase STATE_NAME_VIEW:
					DESTROY_PRINTF("Destroying STATE_NAME_VIEW\n");
					SAFE_FREE(state->name_view.name);
				xcase STATE_SET_EXPIRATION:
					DESTROY_PRINTF("Destroying STATE_NAME_VIEW\n");
					SAFE_FREE(state->set_expiration.project);
					SAFE_FREE(state->set_expiration.name);
				xcase STATE_SET_FILE_EXPIRATION:
					DESTROY_PRINTF("Destroying STATE_NAME_VIEW\n");
					SAFE_FREE(state->set_file_expiration.path);
				xcase STATE_SET_VIEW:
					DESTROY_PRINTF("Destroying STATE_SET_VIEW\n");
					SAFE_FREE(state->set_view.name);
				xcase STATE_LOCK_FILE:
					DESTROY_PRINTF("Destroying STATE_LOCK_FILE\n");
					eaDestroy(&state->lock_file.files);
					eaDestroy(&state->lock_file.touch);
					ea32Destroy(&state->lock_file.checksums);
					StructDestroySafe(parse_PCLFileSpec, &state->lock_file.filespec);
				xcase STATE_UNLOCK_FILES:
					DESTROY_PRINTF("Destroying STATE_UNLOCK_FILES\n");
					eaDestroy(&state->unlock_files.files);
				xcase STATE_CHECKIN_FILES:
					DESTROY_PRINTF("Destroying STATE_CHECKIN_FILES\n");
					SAFE_FREE(state->checkin.comment);
					SAFE_FREE(state->checkin.file_mem);
					eaDestroyEx(&state->checkin.fnamesOnDisk, NULL);
					eaDestroyEx(&state->checkin.fnamesForUpload, NULL);
				xcase STATE_NOTIFY_ME:
					DESTROY_PRINTF("Destroying STATE_NOTIFY_ME\n");
				xcase STATE_PROJECT_LIST:
					DESTROY_PRINTF("Destroying STATE_PROJECT_LIST\n");
				xcase STATE_BRANCH_INFO:
					DESTROY_PRINTF("Destroying STATE_BRANCH_INFO\n");
				xcase STATE_NAME_LIST:
					DESTROY_PRINTF("Destroying STATE_NAME_LIST\n");
				xcase STATE_CHECK_DELETED:
					DESTROY_PRINTF("Destroying STATE_CHECK_DELETED\n");
					eaDestroy(&state->check_deleted.fnames);
				xcase STATE_GET_FILEVERSION_INFO:
					DESTROY_PRINTF("Destroying STATE_GET_FILEVERSION_INFO\n");
					free(state->fileversion_info.fname);
				xcase STATE_IS_COMPLETELY_SYNCED:
					DESTROY_PRINTF("Destroying STATE_IS_COMPLETELY_SYNCED\n");
					free(state->is_completely_synced.path);
				xcase STATE_UNDO_CHECKIN:
					DESTROY_PRINTF("Destroying STATE_UNDO_CHECKIN\n");
					free(state->undo_checkin.comment);
				xcase STATE_PING:
					DESTROY_PRINTF("Destroying STATE_PING\n");
					free(state->ping.data);
			}
		}
		xcase STATE_XFERRING:
		{
			switch(state->call_state)
			{
				xcase STATE_GET_FILE:
					DESTROY_PRINTF("Destroying STATE_GET_FILE\n");
				xcase STATE_GET_REQUIRED_FILES:
					DESTROY_PRINTF("Destroying STATE_GET_REQUIRED_FILES\n");
					eaiDestroy(&state->get_required.child_stack);
					eaDestroy(&state->get_required.dir_entry_stack);
				xcase STATE_GET_FILE_LIST:
					DESTROY_PRINTF("Destroying STATE_GET_FILE_LIST\n");
					eaDestroy(&state->get_list.fnames);
					eaDestroy(&state->get_list.mirror);
					eaDestroy(&state->get_list.touch);
			}
		}
	}

	free(state);

#undef DESTROY_PRINTF

	PERFINFO_AUTO_STOP_FUNC();
}

// Reset the progress-based timeout.
static void updateProgressTimer(PCL_Client *client)
{
	client->progress_timer = timerCpuTicks64();
	client->progress_timer_retries = 0;
}

// Handle a packet coming from a status monitoring server.
static void handleReporteePacket(Packet *pkt, int cmd, NetLink *link, void *user_data)
{
	switch (cmd)
	{
	 
		// The reportee has requested that we abort.
		case TO_PATCHCLIENT_FOR_STATUSREPORTING_ABORT:
			pclAbort(user_data);
			break;
	}
}

// Connect to a reportee host.
// If try_hard is set, keep retrying for some time interval to connect.
static bool pclReportingConnect(PCL_Client *client, PCLStatusReportee *reportee, bool try_hard)
{
	bool result;
	S64 started = 0;

	if (!client->statusReportingComm)
	{
		client->statusReportingComm = commCreate(1, 1);
	}

	
	for (;;)
	{
		S64 now;
		F32 duration;

		// Try once.
		result = commConnectFSMForTickFunctionWithRetrying(&reportee->fsm, &reportee->link, "patch status reporting", 5, client->statusReportingComm,
			LINKTYPE_UNSPEC, LINK_FORCE_FLUSH, reportee->host, reportee->port, handleReporteePacket, NULL, NULL, 0, NULL, 0, NULL, 0);
		if (result || !try_hard)
			break;



		// If trying hard, keep trying for a while.
		now = timerCpuTicks64();
		if (!started)
			started = now;
		duration = timerSeconds64(now - started);
		if (duration > (reportee->critical ? 30 : 1))
			break;
		linkConnectWaitNoRemove(&reportee->link, 1);
	}

	// Save the PCL_Client in the reportee NetLink.
	//Note that the reportee NetLink is always destroyed when the PCL_Client is.
	if (result)
		linkSetUserData(reportee->link, client);

	return result;
}

// Destroy patch status reporting information.
static void pclDestroyReporting(PCL_Client *client)
{
	EARRAY_CONST_FOREACH_BEGIN(client->status_reporting, i, n);
	{
		PCLStatusReportee *reportee = client->status_reporting[i];
#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '*reportee'"
		if (reportee->fsm)
			commConnectFSMDestroy(&reportee->fsm);
#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '**reportee[12]'"
		free(reportee->host);
#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '**reportee[8]'"
		free(reportee->id);
		linkFlushAndClose(&reportee->link, "pclDisconnectAndDestroy");
		free(reportee);
	}
	EARRAY_FOREACH_END;
	eaDestroy(&client->status_reporting);
}

// Send patching status report.
// TODO: Unify pcllog, patchmelog, pclMSpf, pclSendReport, pclSendLog(), and explicit printfs in some sane way.
static void pclSendReport(PCL_Client *client, PCLStatusMonitoringState state, const char *string)
{
	PCLStatusMonitoringUpdate_Internal update = {0};
	char path[MAX_PATH];

	PERFINFO_AUTO_START_FUNC();

	// Do nothing if there are no status reportees.
	if (!client->status_reporting)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	if (client->statusReportingComm)
	{
		commMonitorWithTimeout(client->statusReportingComm, 0);
	}

	// Fill in general update info.
	update.eState = state;
	update.iMyPID = getpid();
	update.uMyHWND = (U64)compatibleGetConsoleWindow();
	update.pUpdateString = (char *)string;
	if (client->root_folder && *client->root_folder)
	{
		if (strStartsWith(client->root_folder, "./"))
			machinePath(path, client->root_folder + 2);
		else
			machinePath(path, client->root_folder);
		update.pPatchDir = path;
	}
	else
		update.pPatchDir = NULL;
	update.pPatchName = client->cached_view_name;

	// Loop over each report.
	EARRAY_CONST_FOREACH_BEGIN(client->status_reporting, i, n);
	{
		PCLStatusReportee *reportee = client->status_reporting[i];
		Packet *pak;

		// Make sure we're connected.
		if (!pclReportingConnect(client, reportee, state != PCLSMS_UPDATE))
			continue;

		// Fill in per-report info.
		update.pMyIDString = reportee->id;

		// Send update.
		pak = pktCreate(reportee->link, FROM_PATCHCLIENT_FOR_STATUSREPORTING_UPDATE);
		ParserSendStructSafe(parse_PCLStatusMonitoringUpdate_Internal, pak, &update);
		pktSend(&pak);

		// Save last update time.
		client->last_reporting_update = timeSecondsSince2000();
	}
	EARRAY_FOREACH_END;

	// If this was something other than an update, don't send any more updates.
	if (state != PCLSMS_UPDATE)
		pclDestroyReporting(client);

	PERFINFO_AUTO_STOP_FUNC();
}

// Send a periodic percent progress report.
static void pclSendReportPercent(PCL_Client *client, const char *step, U64 progress, U64 total)
{
	// Do nothing if there are no status reportees.
	if (!client->status_reporting)
		return;

	PERFINFO_AUTO_START_FUNC();

	if (!progress || client->last_reporting_update + 4 < timeSecondsSince2000_ForceRecalc())
	{
		char *msg = NULL;

		estrStackCreate(&msg);
		if (total && progress <= total)
		{
			int percent = progress * 100 / total;
			if (percent == 100)
				--percent;
			estrPrintf(&msg, "%s: %"FORM_LL"u/%"FORM_LL"u: %d%%", step, progress, total, percent);
		}
		else
			estrPrintf(&msg, "%s: %"FORM_LL"u/%"FORM_LL"u", step, progress, total);

		pclSendReport(client, PCLSMS_UPDATE, msg);
		estrDestroy(&msg);
	}

	PERFINFO_AUTO_STOP();
}

// Get status reporting ready.
static void pclStartReporting(PCL_Client *client)
{
	// Loop over each report.
	EARRAY_CONST_FOREACH_BEGIN(client->status_reporting, i, n);
	{
		pclReportingConnect(client, client->status_reporting[i], false);
	}
	EARRAY_FOREACH_END;
}

// The Patch Server has reported a bad packet.
static void handleBadPacket(PCL_Client *client, Packet *pak)
{
	int error_cmd, extra1, extra2, extra3;
	char *error = NULL;
	estrStackCreate(&error);
	pclReadBadPacket(pak, &error_cmd, &extra1, &extra2, &extra3, &error);
	REPORT_ERROR_STRING(client, PCL_BAD_PACKET, error);
	estrDestroy(&error);
}

static void handleMsg(Packet * pak, int cmd, NetLink * link, void * user_data)
{
	PCL_Client * client = user_data;
	U64 ticks_start;
	StateStruct * state;
	static StaticCmdPerf cmdPerf[PATCHSERVER_CMD_COUNT];
	const char *cmdPerfName = NULL;

	EnterStaticCmdPerfMutex();
	if(cmd >= 0 && cmd < ARRAY_SIZE(cmdPerf)){
		if(!cmdPerf[cmd].name){
			char buffer[100];
			sprintf(buffer, "Cmd:%s", StaticDefineIntRevLookup(PatchServerCmdEnum, cmd));
			cmdPerf[cmd].name = strdup(buffer);
		}
		PERFINFO_AUTO_START_STATIC(cmdPerf[cmd].name, &cmdPerf[cmd].pi, 1);
		cmdPerfName = cmdPerf[cmd].name;
	}
	else
	{
		PERFINFO_AUTO_START("Cmd:Unknown", 1);
		cmdPerfName = "Cmd:Unknown";
	}
	LeaveStaticCmdPerfMutex();

	if (client->netEnterCallback)
		client->netEnterCallback(client, client->netCallbackUserData);

	ticks_start = timerCpuTicks64();
	state = client->state[0];

	pclMSpf("receiving msg:0x%8.8p cmd=%s (%d bytes), state general=%s, call=%s, func=%s\n",
			pak,
			patchServerCmdGetName(cmd),
			pktGetSize(pak),
			pclStateGetName(state->general_state),
			pclStateGetName(state->call_state),
			pclStateGetName(state->func_state));

	updateProgressTimer(client);
	if(cmd == PATCHSERVER_DONT_RECONNECT)
	{
		// setting an error should be sufficient to prevent reconnection
		char *msg = pktGetStringTemp(pak);
		REPORT_ERROR_STRING(client, PCL_UNEXPECTED_RESPONSE, msg);
	}

	else if (cmd == PATCHSERVER_BAD_PACKET && state->general_state != STATE_XFERRING)
		handleBadPacket(client, pak);

	else if (cmd == PATCHSERVER_UPDATE_HTTPINFO)
		handleHttpInfoUpdateMsg(pak, cmd, client);

	else if (cmd == PATCHSERVER_UPDATE_AUTOUPINFO)
		handleAutoupInfoUpdateMsg(pak, cmd, client);

	// TODO: this flow is all inside out
	else if(state->general_state == STATE_XFERRING)
	{
		handleXferMsg(pak, cmd, client->xferrer);
	}
	else if(state->general_state == STATE_WAITING)
	{
		switch(state->call_state)
		{
		xcase STATE_FILE_HISTORY:
			fileHistoryResponse(client, pak, cmd);
		xcase STATE_VERSION_INFO:
			versionInfoResponse(client, pak, cmd);
		xcase STATE_SET_VIEW:
			setViewResponse(client, pak, cmd);
		xcase STATE_NAME_VIEW:
			nameViewResponse(client, pak, cmd);
		xcase STATE_SET_EXPIRATION:
			setExpirationResponse(client, pak, cmd); 
		xcase STATE_SET_FILE_EXPIRATION:
			setFileExpirationResponse(client, pak, cmd);
		xcase STATE_CHECKIN_FILES:
		acase STATE_FORCEIN_DIR:
			checkinFilesResponse(client, pak, cmd);
		xcase STATE_SET_AUTHOR:
			setAuthorResponse(client, pak, cmd);
		xcase STATE_LOCK_FILE:
			lockFileResponse(client, pak, cmd);
		xcase STATE_UNLOCK_FILES:
			unlockFileResponse(client, pak, cmd);
		xcase STATE_NOTIFY_ME:
			notifyResponse(client, pak, cmd);
		xcase STATE_PROJECT_LIST:
			getProjectListResponse(client, pak, cmd);
		xcase STATE_BRANCH_INFO:
			getBranchInfoResponse(client, pak, cmd);
		xcase STATE_NAME_LIST:
			getNameListResponse(client, pak, cmd);
		xcase STATE_CHECK_DELETED:
			checkDeletedResponse(client, pak, cmd);
		xcase STATE_QUERY_LASTAUTHOR:
			getLastAuthorResponse(client, pak, cmd);
		xcase STATE_QUERY_LOCKAUTHOR:
			getLockAuthorResponse(client, pak, cmd);
		xcase STATE_GET_CHECKINS_BETWEEN_TIMES:
			getCheckinsBetweenTimesResponse(client, pak, cmd);
		xcase STATE_GET_CHECKIN_INFO:
			getCheckinInfoResponse(client, pak, cmd);
		xcase STATE_GET_FILEVERSION_INFO:
			getFileVersionInfoResponse(client, pak, cmd);
		xcase STATE_IS_COMPLETELY_SYNCED:
			isCompletelySyncedResponse(client, pak, cmd);
		xcase STATE_UNDO_CHECKIN:
			undoCheckinResponse(client, pak, cmd);
		xcase STATE_PING:
			pingResponse(client, pak, cmd);
		xdefault:
			REPORT_ERROR_STRING(client, PCL_NO_RESPONSE_FUNCTION, "Some waiting state has no response function.");
		};
	}
	else if(state->general_state == STATE_CONNECTING)
	{
		if(state->call_state == STATE_CONNECTING)
			handleConnectOkMsg(pak, cmd, client);
		else if(state->call_state == STATE_AUTOUPDATE)
			handleAutoUpdateMsg(pak, cmd, client);
		else
			REPORT_ERROR_STRING(client, PCL_NO_RESPONSE_FUNCTION, "Some connecting state has no response function.");
	}
	else
	{
		REPORT_ERROR_STRING(client, PCL_NO_RESPONSE_FUNCTION, "A message came in the idle state.");
	}

	if (client->netLeaveCallback)
		client->netLeaveCallback(client, client->netCallbackUserData);

	PERFINFO_AUTO_STOP_CHECKED(cmdPerfName);
}

// Handler to call when an Autoupdate patching info update packet comes in.
static void handleAutoupInfoUpdateMsg(Packet * pak, int cmd, PCL_Client * client)
{
	if (client->autoupinfoCallback)
	{
		U32 version = pktGetU32(pak);
		const char *info = pktGetStringTemp(pak);
		if (!info)
		{
			REPORT_ERROR_STRING(client, PCL_UNEXPECTED_RESPONSE, "Expecting Autoupdate patching update info.");
			return;
		}

		PERFINFO_AUTO_START("autoupinfoCallback", 1);
		client->autoupinfoCallback(client, version, info, client->autoupinfoUserData);
		PERFINFO_AUTO_STOP_CHECKED("autoupinfoCallback");
	}
}

// Handler to call when an HTTP patching info update packet comes in.
static void handleHttpInfoUpdateMsg(Packet * pak, int cmd, PCL_Client * client)
{
	if (client->httpinfoCallback)
	{
		const char *info = pktGetStringTemp(pak);
		if (!info)
		{
			REPORT_ERROR_STRING(client, PCL_UNEXPECTED_RESPONSE, "Expecting HTTP patching update info.");
			return;
		}

		PERFINFO_AUTO_START("httpinfoCallback", 1);
		client->httpinfoCallback(client, info, client->httpinfoUserData);
		PERFINFO_AUTO_STOP_CHECKED("httpinfoCallback");
	}
}

static void disconnectCallback(NetLink * link, void * user_data)
{
	PCL_Client * client = user_data;

	if(!client)
		return;

	client->disconnected = true;

	if(client->link)
	{
		client->was_ever_unexpectedly_disconnected = true;
		pcllog(client, PCLLOG_LINK, "unexpectedly disconnected");
		linkRemove_wReason(&client->link, "patchclientlib unexpectedly disconnected");
		if (!(client->file_flags & PCL_RECONNECT))
			REPORT_ERROR_STRING(client, PCL_LOST_CONNECTION, "unexpectedly disconnected");
	}
}

#define pclStateAdd(client, state) pclStateAdd_dbg(client, state, __FILE__, __LINE__)

static void pclStateAdd_dbg(PCL_Client* client,
							StateStruct* state,
							const char* fileName,
							S32 fileLine)
{
	eaInsert(&client->state, state, 0);
	
	pclMSpf("Adding state (%s:%d)\n",
			getFileNameConst(fileName),
			fileLine);
			
	EARRAY_CONST_FOREACH_BEGIN(client->state, i, isize);
		state = client->state[i];
		pclMSpf("    %d: general=%s, call=%s, func=%s\n",
				i,
				pclStateGetName(state->general_state),
				pclStateGetName(state->call_state),
				pclStateGetName(state->func_state));
	EARRAY_FOREACH_END;

	updateProgressTimer(client);
}

#define pclStateRemoveHead(client) pclStateRemoveHead_dbg(client, __FILE__, __LINE__)

static void pclStateRemoveHead_dbg(	PCL_Client* client,
									const char* fileName,
									S32 fileLine)
{
	StateStruct* state = client->state[0];
	
	pclMSpf("Removing state (%s:%d)\n",
			getFileNameConst(fileName),
			fileLine);

	EARRAY_CONST_FOREACH_BEGIN(client->state, i, isize);
		state = client->state[i];
		pclMSpf("    %d: general=%s, call=%s, func=%s\n",
				i,
				pclStateGetName(state->general_state),
				pclStateGetName(state->call_state),
				pclStateGetName(state->func_state));
	EARRAY_FOREACH_END;

	stateDestroy(eaRemove(&client->state, 0));
}

static void popConnectState(PCL_Client *client)
{
	PCL_ConnectCallback callback = client->state[0]->connect.callback;
	void * userData = client->state[0]->connect.userData;
	bool updated = client->state[0]->call_state == STATE_AUTOUPDATE;
	pclStateRemoveHead(client);

	pcllog(client, PCLLOG_LINK, "successfully connected");
	if(callback)
		callback(client, updated, client->error, client->error_details, userData);
}

static void handleConnectOkMsg(Packet *pak_in, int cmd, PCL_Client *client)
{
	StateStruct *state = client->state[0];

	if(cmd != PATCHSERVER_CONNECT_OK)
	{
		REPORT_ERROR_STRING(client, PCL_NO_RESPONSE_FUNCTION, "No response function for some command in connecting state.");
		return;
	}

	if(pktGetBool(pak_in)) // redirection
	{
		if (!g_no_redirect)
		{
			// make sure to update else to consume the same chunks of the packet
			SAFE_FREE(client->referrer);
			client->referrer = strdup(client->server);
			SAFE_FREE(client->server);
			client->server = pktMallocString(pak_in);
			client->port = pktGetU32(pak_in);
	
			// linkRemove sets the link to NULL, which handleDisconnect will check to see if the disconnection is intentional
			linkRemove_wReason(&client->link, "patchclientlib redirecting");
			s_connectToServer(client,"redirecting", NULL, 0);
			return;
		}
		else
		{
			// still need to pull out data to read server time later
			pktGetStringTemp(pak_in);
			pktGetU32(pak_in);
		}
	}
	else if(pktGetBool(pak_in)) // autoupdate info
	{
		U32 checksum = pktGetU32(pak_in);
		state->connect.autoupdate_size = pktGetU32(pak_in);
		state->connect.autoupdate_modified = pktGetU32(pak_in);
		if(state->connect.autoupdate_path) // always null on reconnection
		{
			U32 data_len;
			U8 *data = fileAlloc(state->connect.autoupdate_path, &data_len);
			if( state->connect.autoupdate_size != data_len
				||
				// NOTE: Removing this as it is superfluous and it creates an infinite loop on FAT32 (read: Mac+Boot Camp) <NPK 2008-06-20>
				//!timestampsMatch(	state->connect.autoupdate_modified,
				//					fileLastChangedAbsolute(state->connect.autoupdate_path))
				//||
				checksum != patchChecksum(data, data_len) )
			{
				Packet *pak_out = pktCreate(client->link, PATCHCLIENT_REQ_AUTOUPDATE);
				pktSend(&pak_out);
				client->current_pak_bytes_waiting_to_recv = 0;

				pcllog(client, PCLLOG_LINK, "autoupdating %s", state->connect.autoupdate_path);
				state->call_state = state->func_state = STATE_AUTOUPDATE;
				SAFE_FREE(data);
				return;
			}
			else
			{
				pcllog(client, PCLLOG_LINK, "skipping unneeded autoupdate %s", client->autoupdate_token);
			}
			SAFE_FREE(data);
		}
	}
	else if(state->connect.autoupdate_path)
	{
		pcllog(client, PCLLOG_LINK, "skipping unavailable autoupdate %s", client->autoupdate_token);
	}

	timeSecondsSince2000Update();
	// support backwards compatibility if the time is not at the end of the packet
	client->client_time = getCurrentFileTime();
	client->server_time = (pktGetNumRemainingRawBytes(pak_in) >= sizeof(U32)) ? pktGetU32(pak_in) : 0;

	popConnectState(client);
}

static void handleAutoUpdateMsg(Packet *pak_in, int cmd, PCL_Client *client)
{
	StateStruct *state = client->state[0];
	U32 len;
	U8 *data;
	char *ret;

	if(cmd != PATCHSERVER_AUTOUPDATE_FILE)
	{
		REPORT_ERROR_STRING(client, PCL_NO_RESPONSE_FUNCTION, "No response for some command in autoupdate state");
		return;
	}

	client->current_pak_bytes_waiting_to_recv = 0;

	if(pktGetBool(pak_in)) // zipped
	{
		data = pktGetZipped(pak_in, &len);
	}
	else
	{
		len = pktGetU32(pak_in);
		data = malloc(len);
		pktGetBytes(pak_in, len, data);
	}

	if(len != state->connect.autoupdate_size)
	{
		REPORT_ERROR_STRING(client, PCL_SUCCESS, "Server reported conflicting autoupdate filesizes");
		SAFE_FREE(data);
		return;
	}

	ret = xferWriteFileToDisk(client, state->connect.autoupdate_path, data, len, state->connect.autoupdate_modified, patchChecksum(data, len), false, 0, NULL, 0, NULL);
	if(ret != NULL)
	{
		REPORT_ERROR_STRING(client, PCL_COULD_NOT_WRITE_LOCKED_FILE, ret);
		estrDestroy(&ret);
	}
	else
	{
		popConnectState(client);
	}
	SAFE_FREE(data);
}

#if _XBOX
static void initPatchDrive(void)
{
	initXboxDrives();
	XFlushUtilityDrive();
	hogSetGlobalOpenMode(HogSafeAgainstAppCrash);
}
#endif

PCL_ErrorCode pclConnectAndCreate(PCL_Client **clientOut,
								  const char* serverName,
								  S32 port,
								  F32 timeout,
								  NetComm* comm,
								  const char* root_folder,
								  const char* autoupdate_token,
								  const char* autoupdate_path,
								  PCL_ConnectCallback callback,
								  void *userData)
{
	return pclConnectAndCreateEx(clientOut, serverName, port, timeout, comm, root_folder, autoupdate_token, autoupdate_path, callback, NULL, userData, NULL, 0);
}



PCL_ErrorCode pclConnectAndCreateEx(PCL_Client **clientOut,
									const char* serverName,
									S32 port,
									F32 timeout,
									NetComm* comm,
									const char* root_folder,
									const char* autoupdate_token,
									const char* autoupdate_path,
									PCL_ConnectCallback callback,
									PCL_PreconnectCallback preconnect,
									void *userData,
									char *errorstr,
									size_t errorstr_size)
{
	PCL_Client * client;
	StateStruct * state;
	PCL_ErrorCode error;

	PERFINFO_AUTO_START_FUNC();

	patchRunOnce();

	if(!clientOut)
		RETURN_ERROR_PERFINFO_STOP(NULL, PCL_CLIENT_PTR_IS_NULL);
	if(!serverName)
		RETURN_ERROR_PERFINFO_STOP(NULL, PCL_NULL_PARAMETER);
	if(SAFE_DEREF(autoupdate_path) && !SAFE_DEREF(autoupdate_token))
		RETURN_ERROR_PERFINFO_STOP(NULL, PCL_INVALID_PARAMETER);

	#if PLATFORM_CONSOLE
		hogSetGlobalOpenMode(HogUnsafe);
	#endif

	client = callocStruct(PCL_Client);
	client->comm = comm;
	client->xferrer = xferrerInit(client);
	client->timeout = timeout;
	client->port = port;
	client->original_port = port;
	if(!serverName || !serverName[0])
		serverName = PCL_DEFAULT_PATCHSERVER;
	client->server = strdup(serverName);
	client->original_server = strdup(serverName);
	client->callback_timer = timerCpuTicks64();
	updateProgressTimer(client);
	client->retry_timer = timerCpuTicks64();
	client->retry_timeout = 3.0;
	client->single_app_mode = true;
	if(SAFE_DEREF(root_folder))
	{
		client->root_folder = strdup(root_folder);
		forwardSlashes(client->root_folder);
		while(strEndsWith(client->root_folder, "/")){
			client->root_folder[strlen(client->root_folder)-1] = '\0';
		}
	}
	if(SAFE_DEREF(autoupdate_token))
	{
		client->autoupdate_token = strdup(autoupdate_token);
	}

	
	// Create the IDLE state.
	
	state = callocStruct(StateStruct);
	state->general_state = STATE_IDLE;
	state->call_state = STATE_IDLE;
	state->func_state = STATE_IDLE;
	pclStateAdd(client, state);
	
	// Create the CONNECTING state.

	state = callocStruct(StateStruct);
	state->general_state = STATE_CONNECTING;
	state->call_state = STATE_CONNECTING;
	state->func_state = STATE_CONNECTING;
	state->connect.callback = callback;
	state->connect.userData = userData;
	if(SAFE_DEREF(autoupdate_path))
	{
		char buf[MAX_PATH];
		machinePath(buf, autoupdate_path);
		state->connect.autoupdate_path = strdup(buf);
		strcat(buf, ".deleteme");
		unlink(buf);
	}
	pclStateAdd(client, state);

	// Call pre-connect callback.
	if (preconnect)
		preconnect(client, userData);
	
	// Connect to the server.

	error = s_connectToServer(client,"connecting", SAFESTR2(errorstr));
	if(error == PCL_SUCCESS)
	{
		*clientOut = client;
	}
	else // almost certainly a comm failure
	{
		pclDisconnectAndDestroy(client);
		*clientOut = NULL;
	}

	PERFINFO_AUTO_STOP_FUNC();

	return error;
}

PCL_ErrorCode	pclConnectAndCreateForFile(PCL_Client **client,
										   const char *filename,
										   F32 timeout,
										   NetComm *comm,
										   const char *autoupdate_token,
										   const char *autoupdate_path,
										   PCL_ConnectCallback callback,
										   void *userData)
{
	static StashTable client_cache = NULL;
	static CRITICAL_SECTION client_cache_lock;
	char root_path[MAX_PATH];
	char trivia_path[MAX_PATH];
	PCL_Client *client_tmp = NULL;
	PCL_ErrorCode error;

	PERFINFO_AUTO_START_FUNC();

	// Safely initialize the lock
	ATOMIC_INIT_BEGIN;
	InitializeCriticalSection(&client_cache_lock);
	ATOMIC_INIT_END;
	EnterCriticalSection(&client_cache_lock);

	// Initialize the StashTable
	if(!client_cache)
		client_cache = stashTableCreateWithStringKeys(10, StashDefault);

	// Find the root folder
	if(!getPatchTriviaList(SAFESTR(trivia_path), SAFESTR(root_path), filename))
		return PCL_INVALID_PARAMETER;

	// Check the cache
	if(stashFindPointer(client_cache, root_path, &client_tmp))
	{
		LeaveCriticalSection(&client_cache_lock);
		*client = client_tmp;
		return PCL_SUCCESS;
	}

	// Not in cache, get trivia and make client
	{
		const char *server, *port_str;
		S32 port;
		TriviaMutex mutex = triviaAcquireDumbMutex(trivia_path);
		TriviaList *list = triviaListCreateFromFile(trivia_path);
		triviaReleaseDumbMutex(mutex);
		if(list)
		{
			server = triviaListGetValue(list, "PatchServer");
			server = server ? strdup(server) : NULL;
			port_str = triviaListGetValue(list, "PatchPort");
			port = port_str ? atoi(port_str) : 0;
			triviaListDestroy(&list);
		}
		else
		{
			Errorf("Could not open patch trivia %s", trivia_path);
			return PCL_INVALID_PARAMETER;
		}

		if(!server || !port)
		{
			Errorf("Cannot load server and port from patch trivia");
			return PCL_INVALID_PARAMETER;
		}

		error = pclConnectAndCreate(client, server, port, timeout, comm, root_path,
			autoupdate_token, autoupdate_path, callback, userData);
		if(error != PCL_SUCCESS)
			return error;
	}

	// Update cache
	stashAddPointer(client_cache, (*client)->root_folder, *client, false);

	LeaveCriticalSection(&client_cache_lock);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

PCL_ErrorCode pclSetKeepAliveAndTimeout(SA_PARAM_NN_VALID PCL_Client* client, F32 timeout)
{
	NetLink* link;

	PERFINFO_AUTO_START_FUNC();

	link = SAFE_MEMBER(client, link);
	
	CHECK_ERROR_PI(client);
	
	if(!link)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_LOST_CONNECTION;
	}
	
	if(!client->keepAliveTimer)
	{
		// This will make the client send a ping to the server and also count the replies.
		
		linkSetKeepAliveSeconds(link,5);
		
		client->keepAliveTimer = timerCpuTicks64();
	}
	
	client->keepAliveTimeout = timeout;
	client->keepAlivePingAckRecvCount = linkGetPingAckReceiveCount(link);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

static void resetView(PCL_Client * client)
{
	if(client->view_status == VIEW_NEEDS_RESET)
	{
		StateStruct* state = callocStruct(StateStruct);

		state->general_state = STATE_WAITING;
		state->call_state = STATE_SET_VIEW;
		state->func_state = STATE_NONE;
		pclStateAdd(client, state);
	}
}

static void resetAuthor(PCL_Client * client)
{
	if(client->author_status == VIEW_NEEDS_RESET)
	{
		StateStruct* state = callocStruct(StateStruct);

		state->general_state = STATE_WAITING;
		state->call_state = STATE_SET_AUTHOR;
		state->func_state = STATE_NONE;
		pclStateAdd(client, state);
	}
}

static void connectCallback(NetLink * link, void * user_data)
{
	PCL_Client * client = user_data;
	Packet *pak_out;
	const char *error;

	PERFINFO_AUTO_START_FUNC();

	assert(client);

	devassert(client->state[0]->general_state == STATE_CONNECTING);

	if (!linkConnected(link)) {
		// Failed to connect
		error = linkError(link)?linkError(link):"No error message";
		pcllog(client, PCLLOG_ERROR, "The client was unable to connect : %s", error);
		if (linkErrorNeedsEncryption(link))
			g_patcher_encrypt_connections = true;
		client->disconnected = true;
		return;
	}

	pak_out = pktCreate(link, PATCHCLIENT_CONNECT);
	pktSendU32(pak_out, PATCHCLIENT_VERSION_OLD_VERSION_SCHEME);
	pktSendString(pak_out, client->autoupdate_token);
	pktSendString(pak_out, client->original_server);
	pktSendString(pak_out, client->referrer);
	pktSendU32(pak_out, PATCHCLIENT_VERSION_CURRENT);
	pktSend(&pak_out);

	PERFINFO_AUTO_STOP_FUNC();
}

static PCL_ErrorCode s_connectToServer(PCL_Client * client, const char *verb, char *error, size_t error_size)
{
	PERFINFO_AUTO_START_FUNC();

	// Check if this attempt has taken too long and we need to reconnect.
	if(client->link && timerSeconds64(timerCpuTicks64() - client->retry_timer) > client->retry_timeout)
	{

		// Remove the link.
		if (linkIsFake(client->link))
			SAFE_FREE(client->link);
		else
			linkRemove(&client->link);

		// Increase the retry timeout.
		client->retry_timeout += client->retry_backoff;
		MIN1(client->retry_timeout, 60);
		client->retry_count++;

		// If we were autoupdating, let's go back to the connecting state
		if (client->state[0]->call_state == STATE_AUTOUPDATE)
			client->state[0]->call_state = client->state[0]->func_state = STATE_CONNECTING;

		// When reconnecting, start again at the original server, in case the child we're on has died.
		free(client->server);
		client->server = strdup(client->original_server);
		client->port = client->original_port;
	}

	if(client->link == NULL)
	{
		client->disconnected = false;

		pclSendReport(client, PCLSMS_UPDATE, verb);

		commDefaultVerify(0);
		pcllog(client, PCLLOG_LINK, "%s to %s:%d", verb, client->server, client->port);

		// [NNO-16427] A starting size of 10 MB for the link's send buffer prevents resize alerts from ever occurring due to checkin/forcein files. This is because those functions are
		// coded to never exceed max_net_bytes (5 MB) without receiving ACK of bytes received from patchserver. See checkinFilesProcess(). Aaron L. approves this message.
		client->link = commConnectEx(client->comm,
			LINKTYPE_FLAG_SLEEP_ON_FULL | LINKTYPE_FLAG_RESIZE_AND_WARN | LINKTYPE_SIZE_10MEG,
			( (PATCHER_LINK_COMPRESSION_ON?LINK_COMPRESS:LINK_NO_COMPRESS) | (g_patcher_encrypt_connections?LINK_ENCRYPT:0)),
			client->server, client->port, handleMsg, connectCallback, disconnectCallback, 0, SAFESTR2(error), __FILE__, __LINE__);

		if(client->link)
		{
			linkSetUserData(client->link, client);
			client->retry_timer = timerCpuTicks64();
		}
		else
		{
			// Formerly, this code did the following:
			//RETURN_ERROR_PERFINFO_STOP(client, PCL_COMM_LINK_FAILURE);
			// but the DNS failure that caused this might be resolved if we continue trying, and the above code will certainly
			// continue to retry for any other type of connect failure, so it makes sense that it would continue to retry in this case.
			client->link = linkCreateFakeLink();
		}
	}

	PERFINFO_AUTO_STOP_FUNC();

	return client->disconnected ? PCL_LOST_CONNECTION : PCL_SUCCESS; // disconnected==false on first connect attempt
}

static void getFileCallback(PCL_Client *client, const char * filename, XferStateInfo * info, PCL_ErrorCode error_code, const char * error_details, void * userData)
{
	if(!error_code)
	{
		int * done = userData;
		*done = 1;
	}
	else
	{
		REPORT_ERROR_STRING(NULL, error_code, "Error detected on file completion.");
	}
}

static void fileHistoryResponse(PCL_Client * client, Packet * pak, int cmd)
{
	StateStruct *state = client->state[0];
	PCL_HistoryCallback callback;
	void * userData;
	PatcherFileHistory hist_default = {0};
	PatcherFileHistory *hist_ret = state->history.history_ret?state->history.history_ret:&hist_default;

	if(cmd == PATCHSERVER_FILE_HISTORY_STRUCTS)
	{
		ParserRecvStructSafe(parse_PatcherFileHistory, pak, hist_ret);
	}
	else
	{
		REPORT_ERROR_STRING(client, PCL_UNEXPECTED_RESPONSE, "Expecting file history response.");
	}

	callback = state->history.callback;
	userData = state->history.userData;
	pclStateRemoveHead(client);

	if(callback)
		callback(hist_ret, client->error, client->error_details, userData);

	if (hist_ret == &hist_default) {
		StructDeInit(parse_PatcherFileHistory, hist_ret);
	} else {
		// Caller responsible for freeing
	}
}

static void fileHistorySendPacket(PCL_Client * client)
{
	StateStruct * state = client->state[0];
	Packet * pak;

	pak = pktCreate(client->link, PATCHCLIENT_REQ_FILE_HISTORY_STRUCTS);
	pktSendString(pak, state->history.fileName);
	pktSend(&pak);
	LOGPACKET(client, "PATCHCLIENT_REQ_FILE_HISTORY_STRUCTS %s", client->state[0]->history.fileName);

	state->func_state = STATE_FILE_HISTORY;
}

PCL_ErrorCode pclFileHistory(PCL_Client * client, const char * fileName, PCL_HistoryCallback callback, void * userData, PatcherFileHistory *history_ret)
{
	StateStruct * state;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);

	if(fileName == NULL)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_NULL_PARAMETER;
	}

	state = callocStruct(StateStruct);
	state->general_state = STATE_WAITING;
	state->call_state = STATE_FILE_HISTORY;
	state->func_state = STATE_NONE;
	state->history.callback = callback;
	state->history.userData = userData;
	state->history.fileName = strdup(fileName);
	state->history.history_ret = history_ret;
	pclStateAdd(client, state);

	resetView(client);

	if(client->view_status == VIEW_SET)
	{
		fileHistorySendPacket(client);
	}

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

static void versionInfoResponse(PCL_Client *client, Packet *pak_in, int cmd)
{
	int branch = 0;
	const char *sandbox = NULL;
	U32 checkin_time = 0;
	const char *author = NULL;
	const char *comment = NULL;
	PCL_VersionInfoCallback callback;
	void *userData;

	if(cmd == PATCHSERVER_VERSION_INFO)
	{
		if(pktGetBool(pak_in)) // success
		{
			branch = pktGetBits(pak_in, 32);
			checkin_time = pktGetBits(pak_in, 32);
			sandbox = pktGetStringTemp(pak_in);
			author = pktGetStringTemp(pak_in);
			comment = pktGetStringTemp(pak_in);
		}
		else
		{
			REPORT_ERROR_STRING(client, PCL_FILE_NOT_FOUND, "Error getting file version information.");
		}
	}
	else
	{
		REPORT_ERROR_STRING(client, PCL_UNEXPECTED_RESPONSE, "Expecting file version info response.");
	}

	callback = client->state[0]->version_info.callback;
	userData = client->state[0]->version_info.userData;
	pclStateRemoveHead(client);

	if(callback)
		callback(branch, sandbox, checkin_time, author, comment, client->error, client->error_details, userData);
}

static void versionInfoSendPacket(PCL_Client *client)
{
	StateStruct * state = client->state[0];
	Packet *pak_out = pktCreate(client->link, PATCHCLIENT_REQ_VERSION_INFO);
	pktSendString(pak_out, state->version_info.dbname);
	pktSend(&pak_out);
	LOGPACKET(client, "PATCHCLIENT_REQ_VERSION_INFO %s", client->state[0]->version_info.dbname);

	state->func_state = STATE_VERSION_INFO;
}

PCL_ErrorCode pclGetVersionInfo(PCL_Client *client, const char *dbname, PCL_VersionInfoCallback callback, void *userData)
{
	StateStruct * state;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);
	if(!dbname)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_NULL_PARAMETER;
	}

	state = callocStruct(StateStruct);
	state->general_state = STATE_WAITING;
	state->call_state = STATE_VERSION_INFO;
	state->func_state = STATE_NONE;
	state->version_info.dbname = strdup(dbname);
	state->version_info.callback = callback;
	state->version_info.userData = userData;
	pclStateAdd(client, state);

	resetView(client);
	if(client->view_status == VIEW_SET)
	{
		versionInfoSendPacket(client);
	}

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

static void getProjectListResponse(PCL_Client * client, Packet * pak, int cmd)
{
	char ** projects = NULL;
	int * max_branch = NULL;
	int * no_upload = NULL;
	int count = 0, i;
	PCL_ProjectListCallback callback;
	void * userData;

	if(!client)
	{
		REPORT_ERROR_STRING(NULL, PCL_INTERNAL_LOGIC_BUG, "get project list response called without a client");
		return;
	}

	if(cmd == PATCHSERVER_PROJECT_LIST)
	{
		count = pktGetBits(pak, 32);
		for(i = 0; i < count; i++)
		{
			eaPush(&projects, pktMallocString(pak));
			eaiPush(&max_branch, pktGetBits(pak, 32));
			eaiPush(&no_upload, pktGetBits(pak, 1));
		}
	}
	else
	{
		REPORT_ERROR_STRING(client, PCL_UNEXPECTED_RESPONSE, "Expecting project list.");
	}

	callback = client->state[0]->project_list.callback;
	userData = client->state[0]->project_list.userData;
	pclStateRemoveHead(client);

	if(callback)
		callback(projects, max_branch, no_upload, count, client->error, client->error_details, userData);
	eaDestroyEx(&projects, NULL);
	eaiDestroy(&no_upload);
	eaiDestroy(&max_branch);
}

static void getProjectListSendPacket(PCL_Client * client)
{
	StateStruct * state = client->state[0];
	Packet * pak;

	pak = pktCreate(client->link, PATCHCLIENT_REQ_PROJECT_LIST);
	pktSend(&pak);
	LOGPACKET(client, "PATCHCLIENT_REQ_PROJECT_LIST");

	state->func_state = STATE_PROJECT_LIST;
}

PCL_ErrorCode pclGetProjectList(PCL_Client * client, PCL_ProjectListCallback callback, void * userData)
{
	StateStruct * state;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);

	state = calloc(1, sizeof(StateStruct));
	state->general_state = STATE_WAITING;
	state->call_state = STATE_PROJECT_LIST;
	state->func_state = STATE_NONE;
	state->project_list.callback = callback;
	state->project_list.userData = userData;
	pclStateAdd(client, state);

	getProjectListSendPacket(client);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

static void getBranchInfoResponse(PCL_Client * client, Packet * pak_in, int cmd)
{
	char *name = NULL, *warning = NULL;
	int parent_branch = -1;
	PCL_BranchInfoCallback callback;
	void *userData;

	if(cmd == PATCHSERVER_BRANCH_INFO)
	{
		if(pktCheckRemaining(pak_in, 1))
		{
			name = pktGetStringTemp(pak_in);
			parent_branch = pktGetBits(pak_in, 32);
			warning = pktGetStringTemp(pak_in);
		}
		else
			REPORT_ERROR_STRING(client, PCL_INVALID_VIEW, "Invalid branch.");
	}
	else
	{
		REPORT_ERROR_STRING(client, PCL_UNEXPECTED_RESPONSE, "Expecting branch info.");
	}

	callback = client->state[0]->branch_info.callback;
	userData = client->state[0]->branch_info.userData;
	pclStateRemoveHead(client);

	if(callback)
		callback(name, parent_branch, warning, client->error, client->error_details, userData);
}

static void getBranchInfoSendPacket(PCL_Client * client)
{
	StateStruct * state = client->state[0];
	Packet *pak_out = pktCreate(client->link, PATCHCLIENT_REQ_BRANCH_INFO);
	pktSendBits(pak_out, 32, state->branch_info.branch);
	pktSend(&pak_out);
	LOGPACKET(client, "PATCHCLIENT_REQ_BRANCH_INFO");
	state->func_state = STATE_BRANCH_INFO;
}

PCL_ErrorCode pclGetBranchInfo(PCL_Client *client, int branch, PCL_BranchInfoCallback callback, void *userData)
{
	StateStruct * state;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);

	state = calloc(1, sizeof(StateStruct));
	state->general_state = STATE_WAITING;
	state->call_state = STATE_BRANCH_INFO;
	state->func_state = STATE_NONE;
	state->branch_info.branch = branch < 0 ? client->branch : branch;
	state->branch_info.callback = callback;
	state->branch_info.userData = userData;
	pclStateAdd(client, state);

	resetView(client);
	if(client->view_status == VIEW_SET)
	{
		getBranchInfoSendPacket(client);
	}

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

static void getLastAuthorResponse(PCL_Client * client, Packet * pak_in, int cmd)
{
	StateStruct * state = client->state[0];
	const char *response_string=NULL;
	PatchServerLastAuthorResponse response;

	if(cmd == PATCHSERVER_LASTAUTHOR)
	{
		response = pktGetBitsAuto(pak_in);
		switch (response) {
			xcase LASTAUTHOR_GOT_AUTHOR:
				response_string = allocAddFilename(pktGetStringTemp(pak_in));
			xcase LASTAUTHOR_ERROR:
				response_string = allocAddFilename(pktGetStringTemp(pak_in));
				REPORT_ERROR_STRING(client, PCL_UNEXPECTED_RESPONSE, response_string);
			xcase LASTAUTHOR_NOT_IN_DATABASE:
			xcase LASTAUTHOR_CHECKEDOUT:
			xcase LASTAUTHOR_NOT_LATEST:
			xdefault:
				REPORT_ERROR_STRING(client, PCL_UNEXPECTED_RESPONSE, "Unknown enum value.");
		}
		if (state->query_last_author.response)
			*state->query_last_author.response = response;
		if (state->query_last_author.response_string)
			*state->query_last_author.response_string = response_string;
	}
	else
	{
		REPORT_ERROR_STRING(client, PCL_UNEXPECTED_RESPONSE, "Expecting last author.");
	}

	pclStateRemoveHead(client);
}

static void getAuthorQuerySendPacket(PCL_Client * client)
{
	StateStruct * state = client->state[0];
	Packet *pak_out = pktCreate(client->link, PATCHCLIENT_REQ_LASTAUTHOR);
	pktSendString(pak_out, state->query_last_author.dbname);
	pktSendBits(pak_out, 32, state->query_last_author.timestamp);
	pktSendBits(pak_out, 32, state->query_last_author.filesize);
	pktSend(&pak_out);
	LOGPACKET(client, "PATCHCLIENT_REQ_LASTAUTHOR");
	state->func_state = STATE_QUERY_LASTAUTHOR;
}

PCL_ErrorCode pclGetAuthorQuery(PCL_Client *client, const char *dbname, const char *fullpath, PatchServerLastAuthorResponse *response, const char **response_string)
{
	StateStruct * state;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);

	state = calloc(1, sizeof(StateStruct));
	state->general_state = STATE_WAITING;
	state->call_state = STATE_QUERY_LASTAUTHOR;
	state->func_state = STATE_NONE;
	state->query_last_author.dbname = strdup(dbname);
	if (client->mirrorFilespec && simpleFileSpecExcludesFile(dbname, client->mirrorFilespec))
	{
		HogFile *hogg_file;
		int hogg_index;
		// Not mirrored to disk, check in-hogg timestamp instead
		if (!fileSpecHasHoggsLoaded(client->filespec))
		{	
			char **root_folders=NULL;
			pclLoadHoggs(client);
			loadend_printf("done.");
		}
		hogg_index = fileSpecGetHoggIndexForFile(client->filespec, dbname);
		hogg_file = fileSpecGetHoggHandle(client->filespec, hogg_index);
		if (hogg_file)
		{
			char hogg_path[MAX_PATH];
			char *stripped;
			HogFileIndex file_index;

			strcpy(hogg_path, dbname);
			stripped = fileSpecGetStripPath(client->filespec, hogg_index);
			stripPath(hogg_path, stripped);
			file_index = hogFileFind(hogg_file, hogg_path);
			if (file_index == HOG_INVALID_INDEX)
			{
				state->query_last_author.timestamp = 0;
				state->query_last_author.filesize = -1;
			} else {
				state->query_last_author.timestamp = hogFileGetFileTimestamp(hogg_file, file_index);
				state->query_last_author.filesize = hogFileGetFileSize(hogg_file, file_index);
			}
		}
	} else {
		state->query_last_author.timestamp = fileLastChangedAbsolute(fullpath);
		state->query_last_author.filesize = fileSize(fullpath);
	}
	state->query_last_author.response = response;
	state->query_last_author.response_string = response_string;
	pclStateAdd(client, state);

	getAuthorQuerySendPacket(client);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

static void getLockAuthorResponse(PCL_Client * client, Packet * pak_in, int cmd)
{
	StateStruct * state = client->state[0];
	const char *response_string=NULL;

	if(cmd == PATCHSERVER_LOCKAUTHOR)
	{
		bool bHasAuthor = pktGetBits(pak_in, 1);
		if (bHasAuthor)
			response_string = allocAddFilename(pktGetStringTemp(pak_in));
		if (state->query_last_author.response_string)
			*state->query_last_author.response_string = response_string;
	}
	else
	{
		REPORT_ERROR_STRING(client, PCL_UNEXPECTED_RESPONSE, "Expecting last author.");
	}

	pclStateRemoveHead(client);
}

static void getLockAuthorQuerySendPacket(PCL_Client * client)
{
	StateStruct * state = client->state[0];
	Packet *pak_out = pktCreate(client->link, PATCHCLIENT_REQ_LOCKAUTHOR);
	pktSendString(pak_out, state->query_last_author.dbname);
	pktSend(&pak_out);
	LOGPACKET(client, "PATCHCLIENT_REQ_LOCKAUTHOR");
	state->func_state = STATE_QUERY_LOCKAUTHOR;
}

PCL_ErrorCode pclGetLockAuthorQuery(PCL_Client *client, const char *dbname, const char **author)
{
	StateStruct * state;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);

	state = calloc(1, sizeof(StateStruct));
	state->general_state = STATE_WAITING;
	state->call_state = STATE_QUERY_LOCKAUTHOR;
	state->func_state = STATE_NONE;
	state->query_last_author.dbname = strdup(dbname);
	state->query_last_author.response_string = author;
	pclStateAdd(client, state);

	getLockAuthorQuerySendPacket(client);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}



static void getNameListResponse(PCL_Client * client, Packet * pak, int cmd)
{
	char ** names = NULL;
	char ** sandboxes = NULL;
	int * branches = NULL;
	U32 * revs = NULL;
	char ** comments = NULL;
	U32 * expires = NULL;

	int count = 0, i;
	PCL_NameListCallback callback;
	void * userData;

	if(!client)
	{
		REPORT_ERROR_STRING(NULL, PCL_INTERNAL_LOGIC_BUG, "get name list response called without a client");
		return;
	}

	if(cmd == PATCHSERVER_NAME_LIST)
	{
		count = pktGetBits(pak, 32);
		for(i = 0; i < count; i++)
		{
			eaPush(&names, pktMallocString(pak));
			eaiPush(&branches, pktGetBits(pak, 32));
			eaPush(&sandboxes, pktMallocString(pak));
			ea32Push(&revs, pktGetBits(pak, 32));
			eaPush(&comments, pktMallocString(pak));
			ea32Push(&expires, pktGetBits(pak, 32));
		}
	}
	else
	{
		REPORT_ERROR_STRING(client, PCL_UNEXPECTED_RESPONSE, "Expecting name list.");
	}

	callback = client->state[0]->name_list.callback;
	userData = client->state[0]->name_list.userData;
	pclStateRemoveHead(client);

	if(callback)
		callback(names, branches, sandboxes, revs, comments, expires, count, client->error, client->error_details, userData);
	eaDestroyEx(&names, NULL);
	eaDestroyEx(&sandboxes, NULL);
	eaiDestroy(&branches);
	ea32Destroy(&revs);
	eaDestroyEx(&comments, NULL);
	ea32Destroy(&expires);
}

static void getNameListSendPacket(PCL_Client * client)
{
	StateStruct * state = client->state[0];
	Packet * pak;

	pak = pktCreate(client->link, PATCHCLIENT_REQ_NAME_LIST);
	pktSend(&pak);
	LOGPACKET(client, "PATCHCLIENT_REQ_NAME_LIST");

	state->func_state = STATE_NAME_LIST;
}

PCL_ErrorCode pclGetNameList(PCL_Client * client, PCL_NameListCallback callback, void * userData)
{
	StateStruct * state;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);

	state = callocStruct(StateStruct);
	state->general_state = STATE_WAITING;
	state->call_state = STATE_NAME_LIST;
	state->func_state = STATE_NONE;
	state->name_list.callback = callback;
	state->name_list.userData = userData;
	pclStateAdd(client, state);

	resetView(client);

	if(client->view_status == VIEW_SET)
	{
		getNameListSendPacket(client);
	}

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

static void getCheckinsBetweenTimesResponse(PCL_Client * client, Packet * pak, int cmd)
{
	if(!client)
	{
		REPORT_ERROR_STRING(NULL, PCL_INTERNAL_LOGIC_BUG, "getCheckinsBetweenTimesResponse called without a client");
		return;
	}

	if(cmd == PATCHSERVER_CHECKINS_BETWEEN_TIMES)
	{
		StateStruct* state = client->state[0];
		
		ParserRecvStructSafe(parse_CheckinList,
					pak,
					state->checkin_between_times.clOut);
	}
	else
	{
		REPORT_ERROR_STRING(client, PCL_UNEXPECTED_RESPONSE, "Expecting checkins between times.");
	}

	pclStateRemoveHead(client);
}

static void getCheckinsBetweenTimesSendPacket(PCL_Client * client)
{
	StateStruct * state = client->state[0];
	Packet * pak;

	pak = pktCreate(client->link, PATCHCLIENT_REQ_CHECKINS_BETWEEN_TIMES);
	pktSendBits(pak, 32, state->checkin_between_times.timeStart);
	pktSendBits(pak, 32, state->checkin_between_times.timeEnd);
	pktSend(&pak);
	LOGPACKET(client, "PATCHCLIENT_REQ_CHECKINS_BETWEEN_TIMES");

	state->func_state = STATE_GET_CHECKINS_BETWEEN_TIMES;
}

PCL_ErrorCode pclGetCheckinsBetweenTimes(	PCL_Client* client,
											U32 timeStart,
											U32 timeEnd,
											CheckinList* clOut)
{
	StateStruct * state;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);
	
	if(!clOut)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_NULL_PARAMETER;
	}
	
	state = callocStruct(StateStruct);
	state->general_state = STATE_WAITING;
	state->call_state = STATE_GET_CHECKINS_BETWEEN_TIMES;
	state->func_state = STATE_NONE;
	state->checkin_between_times.clOut = clOut;
	state->checkin_between_times.timeStart = timeStart;
	state->checkin_between_times.timeEnd = timeEnd;
	pclStateAdd(client, state);

	resetView(client);

	if(client->view_status == VIEW_SET)
	{
		getCheckinsBetweenTimesSendPacket(client);
	}

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

static void getCheckinInfoResponse(PCL_Client * client, Packet * pak, int cmd)
{
	if(!client)
	{
		REPORT_ERROR_STRING(NULL, PCL_INTERNAL_LOGIC_BUG, "getCheckinInfoResponse called without a client");
		return;
	}

	if(cmd == PATCHSERVER_CHECKIN_INFO)
	{
		StateStruct* state = client->state[0];
		U32 rev, branch, time, incr_from, n;
		char *sandbox, *author, *comment;
		char **files=NULL;

		if(pktGetBits(pak, 8) == 0)
		{
			if(state->checkin_info.callback)
				state->checkin_info.callback(client, 0, 0, 0, NULL, 0, NULL, NULL, NULL, state->checkin_info.userdata);
		}
		else
		{
			rev = pktGetU32(pak);
			branch = pktGetU32(pak);
			time = pktGetU32(pak);
			sandbox = pktGetStringTemp(pak);
			incr_from = pktGetU32(pak);
			author = pktGetStringTemp(pak);
			comment = pktGetStringTemp(pak);

			for(n = pktGetU32(pak); n > 0; n--)
			{
				eaPush(&files, pktGetStringTemp(pak));
			}

			if(state->checkin_info.callback)
				state->checkin_info.callback(client, rev, branch, time, sandbox, incr_from, author, comment, files, state->checkin_info.userdata);

			eaDestroy(&files);
		}
	}
	else
	{
		REPORT_ERROR_STRING(client, PCL_UNEXPECTED_RESPONSE, "Expecting checkin info.");
	}

	pclStateRemoveHead(client);
}

static void getCheckinInfoSendPacket(PCL_Client * client)
{
	StateStruct * state = client->state[0];
	Packet * pak;

	pak = pktCreate(client->link, PATCHCLIENT_REQ_CHECKIN_INFO);
	pktSendU32(pak, state->checkin_info.rev);
	pktSend(&pak);
	LOGPACKET(client, "PATCHCLIENT_REQ_CHECKIN_INFO");

	state->func_state = STATE_GET_CHECKIN_INFO;
}

PCL_ErrorCode pclGetCheckinInfo(	PCL_Client* client,
										 U32 rev,
										 PCL_GetCheckinInfoCallback callback,
										 void *userdata)
{
	StateStruct * state;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);

	state = callocStruct(StateStruct);
	state->general_state = STATE_WAITING;
	state->call_state = STATE_GET_CHECKIN_INFO;
	state->func_state = STATE_NONE;
	state->checkin_info.rev = rev;
	state->checkin_info.callback = callback;
	state->checkin_info.userdata = userdata;
	pclStateAdd(client, state);

	resetView(client);

	if(client->view_status == VIEW_SET)
	{
		getCheckinInfoSendPacket(client);
	}

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

static void setViewSendPacket(PCL_Client * client)
{
	StateStruct * state = client->state[0];

	if(state->set_view.incremental && !state->set_view.name)
		{
			Packet *pak = pktCreate(client->link, PATCHCLIENT_SET_PROJECT_VIEW_NEW_INCREMENTAL);
			pktSendString(pak, client->project);
			pktSendBits(pak, 32, client->branch);
			pktSendString(pak, client->sandbox);
			pktSendBits(pak, 32, client->rev);
			pktSend(&pak);
			LOGPACKET(client, "PATCHCLIENT_SET_PROJECT_VIEW_NEW_INCREMENTAL %s %i %s %i", client->project, client->branch, client->sandbox, client->rev);
	}
	else if(state->set_view.incremental && state->set_view.name)
	{
		Packet *pak = pktCreate(client->link, PATCHCLIENT_SET_PROJECT_VIEW_NEW_INCREMENTAL_NAME);
		pktSendString(pak, client->project);
		pktSendString(pak, client->sandbox);
		pktSendString(pak, state->set_view.name);
		pktSend(&pak);
		LOGPACKET(client, "PATCHCLIENT_SET_PROJECT_VIEW_NEW_INCREMENTAL_NAME %s %s %s", client->project, client->sandbox, state->set_view.name);
	}
	else if(state->set_view.name)
	{
		Packet *pak = pktCreate(client->link, PATCHCLIENT_SET_PROJECT_VIEW_NAME);
		pktSendString(pak, client->project);
		pktSendString(pak, state->set_view.name);
		pktSend(&pak);
		LOGPACKET(client, "PATCHCLIENT_SET_PROJECT_VIEW_NAME %s %s", client->project, state->set_view.name);
	}

	else if(state->set_view.time)
	{
		Packet *pak = pktCreate(client->link, PATCHCLIENT_SET_PROJECT_VIEW_BY_TIME);
		pktSendString(pak, client->project);
		pktSendBits(pak, 32, client->branch);
		pktSendString(pak, client->sandbox);
		pktSendBits(pak, 32, state->set_view.time);
		pktSend(&pak);
		LOGPACKET(client, "PATCHCLIENT_SET_PROJECT_VIEW_BY_TIME %s %i %s %u", client->project, client->branch, client->sandbox, state->set_view.time);
	}
	else if(client->branch >= 0)
	{
		Packet *pak = pktCreate(client->link, PATCHCLIENT_SET_PROJECT_VIEW_BY_REV);
		pktSendString(pak, client->project);
		pktSendBits(pak, 32, client->branch);
		pktSendString(pak, client->sandbox);
		pktSendBits(pak, 32, client->rev);
		pktSend(&pak);
		LOGPACKET(client, "PATCHCLIENT_SET_PROJECT_VIEW_BY_REV %s %i %s %i", client->project, client->branch, client->sandbox, client->rev);
	}
	else
	{
		Packet *pak = pktCreate(client->link, PATCHCLIENT_SET_PROJECT_VIEW_DEFAULT);
		pktSendString(pak, client->project);
		pktSend(&pak);
		LOGPACKET(client, "PATCHCLIENT_SET_PROJECT_VIEW_DEFAULT %s", client->project);
	}

	state->func_state = STATE_VIEW_RESPONSE;
}

static PCL_ErrorCode setViewAllData(PCL_Client *client,
									const char *name_in,
									const char *project,
									int branch,
									const char *sandbox_in,
									int rev,
									U32 time,
									bool incremental,
									bool getManifest,
									bool saveTrivia,
									PCL_SetViewCallback callback,
									void *userData)
{
	char fname[MAX_PATH];
	StateStruct * state;
	char *sandbox, *name;

	PERFINFO_AUTO_START_FUNC();

	pclSendReport(client, PCLSMS_UPDATE, "Setting view");

	sandbox = name = NULL;

	if(sandbox_in)
		sandbox = strdup(sandbox_in);
	if(name_in)
		name = strdup(name_in);

	CHECK_STATE_PI(client);
	patchDbDestroy(&client->db);
	fileSpecDestroy(&client->filespec);

	SAFE_FREE(client->project);
	SAFE_FREE(client->sandbox);
	SAFE_FREE(client->manifest_fname);
	SAFE_FREE(client->filespec_fname);
	SAFE_FREE(client->full_manifest_fname);
	
	client->project = strdup(project);
	if(!client->root_folder)
	{
		char folder[MAX_PATH];

		sprintf(folder, "./%s", project);
		client->root_folder = strdup(folder);
	}
	client->branch = branch;
	client->sandbox = (sandbox ? strdup(sandbox) : NULL);
	client->rev = rev;
	client->got_all = 0;

	sprintf(fname, "%s.manifest", project);
	client->manifest_fname = strdup(fname);
	sprintf(fname, "%s.filespec", project);
	client->filespec_fname = strdup(fname);
	sprintf(fname, "%s.full.manifest", project);
	client->full_manifest_fname = strdup(fname);

	// Never save trivia when in IN_MEMORY mode
	if(client->file_flags & (PCL_IN_MEMORY|PCL_METADATA_IN_MEMORY))
		saveTrivia = false;

	state = calloc(1, sizeof(StateStruct));
	state->general_state = STATE_WAITING;
	state->call_state = STATE_SET_VIEW;
	state->func_state = STATE_VIEW_RESPONSE;
	state->set_view.callback = callback;
	state->set_view.userData = userData;
	state->set_view.getManifest = getManifest;
	state->set_view.saveTrivia = saveTrivia;
	state->set_view.name = (name ? strdup(name) : NULL);
	state->set_view.time = time;
	state->set_view.incremental = incremental;
	pclStateAdd(client, state);

	setViewSendPacket(client);

	SAFE_FREE(name);
	SAFE_FREE(sandbox);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

PCL_ErrorCode pclSetViewByTime(PCL_Client *client, const char *project, int branch, const char *sandbox, U32 time, bool getManifest, bool saveTrivia, PCL_SetViewCallback callback, void * userData)
{
	PCL_ErrorCode error;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	if(branch < 0)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_INVALID_PARAMETER);

	error = setViewAllData(client, NULL, project, branch, sandbox, PATCHREVISION_NONE, time, false, getManifest, saveTrivia, callback, userData);
	PERFINFO_AUTO_STOP_FUNC();
	return error;
}

PCL_ErrorCode pclSetViewByRev(PCL_Client *client, const char *project, int branch, const char *sandbox, int rev, bool getManifest, bool saveTrivia, PCL_SetViewCallback callback, void * userData)
{
	PCL_ErrorCode error;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	if(branch < 0)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_INVALID_PARAMETER);

	error = setViewAllData(client, NULL, project, branch, sandbox, rev, 0, false, getManifest, saveTrivia, callback, userData);
	PERFINFO_AUTO_STOP_FUNC();
	return error;
}

PCL_ErrorCode pclSetViewLatest(PCL_Client *client, const char *project, int branch, const char *sandbox, bool getManifest, bool saveTrivia, PCL_SetViewCallback callback, void * userData)
{
	PCL_ErrorCode error;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	if(branch < 0)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_INVALID_PARAMETER);

	error = setViewAllData(client, NULL, project, branch, sandbox, PATCHREVISION_NONE, 0, false, getManifest, saveTrivia, callback, userData);
	PERFINFO_AUTO_STOP_FUNC();
	return error;
}

PCL_ErrorCode pclSetDefaultView(PCL_Client * client, const char * project, bool getManifest,
								PCL_SetViewCallback callback, void * userData)
{
	PCL_ErrorCode error;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	error = setViewAllData(client, NULL, project, -1, NULL, PATCHREVISION_NONE, 0, false, getManifest, getManifest, callback, userData);
	PERFINFO_AUTO_STOP_FUNC();
	return error;
}

PCL_ErrorCode pclSetNamedView(	PCL_Client * client,
								const char * project,
								const char * viewName,
								bool getManifest,
								bool saveTrivia,
								PCL_SetViewCallback callback,
								void * userData)
{
	PCL_ErrorCode error;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	error = setViewAllData(client, viewName, project, -1, NULL, PATCHREVISION_NONE, 0, false, getManifest, saveTrivia, callback, userData);
	PERFINFO_AUTO_STOP_FUNC();
	return error;
}

PCL_ErrorCode pclSetViewNewIncremental(PCL_Client *client, const char *project, int branch, const char *sandbox, int rev, bool getManifest, PCL_SetViewCallback callback, void *userData)
{
	PCL_ErrorCode error;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	error = setViewAllData(client, NULL, project, branch, sandbox, rev, 0, true, getManifest, getManifest, callback, userData);
	PERFINFO_AUTO_STOP_FUNC();
	return error;
}

PCL_ErrorCode pclSetViewNewIncrementalName(PCL_Client *client, const char *project, const char *sandbox, const char *incr_from, bool getManifest, PCL_SetViewCallback callback, void *userData)
{
	PCL_ErrorCode error;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	error = setViewAllData(client, incr_from, project, -1, sandbox, PATCHREVISION_NONE, 0, true, getManifest, getManifest, callback, userData);
	PERFINFO_AUTO_STOP_FUNC();
	return error;
}

PCL_ErrorCode pclSetCompression(PCL_Client *client, bool compress_as_server)
{
	CHECK_ERROR(client);
	client->xferrer->compress_as_server = compress_as_server;
	return PCL_SUCCESS;
}

static void removeFile(PCL_Client * client, HogFile * hogg, const char * fileName)
{
	int ret, hog_index;
	char fixedPath[MAX_PATH], diskPath[MAX_PATH];

	PERFINFO_AUTO_START_FUNC();

	// Used during pre-patching to avoid deleting files from the main hoggs incorrectly.
	if(client->file_flags & PCL_NO_DELETE)
		return;

	if (hogg)
	{
		hogFileModifyDeleteNamed(hogg, fileName);
		strcpy(fixedPath, fileName);
		hog_index = fileSpecGetHoggIndexForHandle(client->filespec, hogg);
		fixPath(fixedPath, fileSpecGetStripPath(client->filespec, hog_index));
		sprintf(diskPath, "%s/%s", client->root_folder, fixedPath);
	}
	else
	{
		strcpy(diskPath, fileName);
	}

	if(fileExists(diskPath))
	{
		ret = unlink(diskPath);
		if(ret != 0)
		{
			char new_name[MAX_PATH];
			sprintf(new_name, "%s.deleteme", diskPath);
			ret = rename(diskPath, new_name);
			assertmsgf(!ret, "Unable to rename %s to %s.deleteme", fixedPath, diskPath);
		}
	}

	// TODO: Delete empty directories.

	PERFINFO_AUTO_STOP_FUNC();
}

static bool examineHeaderHogg(HogFile * hogg, HogFileIndex index, const char * fileName, void * userData)
{
	PCL_Client * client = userData;
	char fname[MAX_PATH];
	DirEntry * dir;
	bool remove = false;

	PERFINFO_AUTO_START_FUNC();

	strcpy(fname, fileName);

	dir = patchFindPath(client->db, fname, 0);
	if(!dir)
	{
		remove = true;
	}
	else if(fileSpecIsNotRequired(client->filespec, fname))
	{
		
		if(eaSize(&dir->versions) > 0)
		{
			if(dir->versions[0]->header_size == 0)
			{
				remove = true;
			}
			else if(dir->versions[0]->header_size == hogFileGetFileSize(hogg, index) &&
				dir->versions[0]->header_checksum == hogFileGetFileChecksum(hogg, index))
			{
				dir->versions[0]->flags |= FILEVERSION_HEADER_MATCHED;
			}
			else
			{
				dir->versions[0]->flags &= ~FILEVERSION_HEADER_MATCHED;
			}
		}
		else
		{
			remove = true;
		}
	}
	else
	{
		remove = true;
	}

	if(remove)
	{
		hogFileModifyDelete(hogg, index);
	}

	PERFINFO_AUTO_STOP_FUNC();

	return true;
}

typedef struct ExamineHoggUserdata 
{
	PCL_Client * client;
	ProgressMeter pm;
	const PCLFileSpec * filespec;
} ExamineHoggUserdata;

// Use to simulate a bug we had that might have exposed other bugs
bool g_AllFilesInWrongHoggs=false;

static bool examineFile(HogFile * hogg, const char * fileName, ExamineHoggUserdata * userData)
{
	PCL_Client * client = userData->client;
	const char * stripped;
	char fixed_path[MAX_PATH];
	bool ignore;
	int hogg_index, true_hogg_index;
	DirEntry * dir_entry;
	FileVersion * file_version;

	PERFINFO_AUTO_START_FUNC();

	if(client->examineCallback)
	{
		PERFINFO_AUTO_START("examineCallback", 1);
		client->verifyAllFiles = client->examineCallback(client, client->examineUserData, 0, fileName, &userData->pm);
		PERFINFO_AUTO_STOP_CHECKED("examineCallback");
	}
	userData->pm.files_so_far++;

	true_hogg_index = fileSpecGetHoggIndexForHandle(client->filespec, hogg);
	stripped = fileSpecGetStripPath(client->filespec, true_hogg_index);
	strcpy(fixed_path, fileName);
	fixPath(fixed_path, stripped);

	// Ignore hidden files.
	// Note that relative paths aren't supported by getFileTree(), since that's mostly a Gimme thing.
	ignore = !pclFileIsIncludedByFileSpec(fixed_path, fixed_path, userData->filespec);
	if (ignore)
		pclMSpf("ignoring %s due to custom filespec", fixed_path);

	if(client->prefix && !strStartsWith(fixed_path, client->prefix)
		&& !(client->prefix[0] == '/' && strStartsWith(fixed_path, client->prefix + 1)))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return true;
	}

	hogg_index = fileSpecGetHoggIndexForFile(client->filespec, fixed_path);
	if(hogg_index < 0 || hogg_index != true_hogg_index || g_AllFilesInWrongHoggs)
	{
		pclMSpf("No listed hogg for %s", fixed_path);
		if (!ignore)
			removeFile(client, hogg, fileName);
		PERFINFO_AUTO_STOP_FUNC();
		return true;
	}

	dir_entry = patchFindPath(client->db, fixed_path, 0);
	if(!dir_entry)
	{
		pclMSpf("No dir_entry for %s", fixed_path);
		if (hogg && !ignore)
			removeFile(client, hogg, fileName);
		PERFINFO_AUTO_STOP_FUNC();
		return true;
	}

	if (!eaSize(&dir_entry->versions))
	{
		// Empty directory.
		PERFINFO_AUTO_STOP_FUNC();
		return true;
	}

	file_version = dir_entry->versions[0];
	if(!file_version)
	{
		pclMSpf("no file_version for %s", fileName);
		if (!ignore)
			removeFile(client, hogg, fileName);
		PERFINFO_AUTO_STOP_FUNC();
		return true;
	}

	// Determine if the file on disk or in the hog is up-to-date.
	// If we're ignoring the file, just assume that it is up-to-date.
	if (ignore)
		file_version->flags |= FILEVERSION_MATCHED;
	else
		isFileUpToDateInternal(client, fixed_path, dir_entry, hogg, hogg_index);

	PERFINFO_AUTO_STOP_FUNC();
	return true;
}

static bool examineFileInHogg(HogFile * hogg, HogFileIndex index_UNUSED, const char * fileName, ExamineHoggUserdata * userData)
{
	return examineFile(hogg, fileName, userData);
}

static FileScanAction examineFileOnDisk(char* dir, struct _finddata32_t* data, void *userdata)
{
	ExamineHoggUserdata * userData = userdata;
	PCL_Client * client = userData->client;
	const char *relative_dir;
	char fileName[256];

	// Get current relative path.
	if (strStartsWith(dir, client->root_folder))
	{
		if (strlen(client->root_folder) == strlen(dir))
			relative_dir = "";
		else
			relative_dir = dir + strlen(client->root_folder) + 1;
	}
	else
		relative_dir = dir;

	// Get full path to file.
	sprintf(fileName, "%s%s%s", relative_dir, *relative_dir ? "/" : "", data->name);

	if (!strcmp(fileName, PATCH_DIR) || strStartsWith(fileName, PATCH_DIR "/"))
		return FSA_NO_EXPLORE_DIRECTORY;

	return examineFile(NULL, fileName, userData) ? FSA_EXPLORE_DIRECTORY : FSA_STOP;
}

static void markFileMatched(PCL_Client *client, const char * filename, XferStateInfo * info, PCL_ErrorCode error_code, const char * error_details, void * userData)
{
	FileVerFlags * flags = userData;

	if(!error_code)
	{
		*flags |= FILEVERSION_MATCHED;
	}
	else
	{
		REPORT_ERROR_STRING(NULL, error_code, "Error discovered during a file complete callback.");
	}
}

static int getFilesForDirEntry(PCL_Client * client, DirEntry *** dir_entry_stack, int ** child_stack, bool requiredOnly, bool getHeaders, bool onlyHeaders)
{
	int ret, top;
	FileVersion * file_version;
	DirEntry * dir_entry;
	int * child;
	PatchXfer * xfer;

	PERFINFO_AUTO_START_FUNC();

	// Fail if we've encountered an error during the xfer.
	if (client->error)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return client->error;
	}

	if(xferrerFull(client->xferrer, NULL))
		return PCL_XFERS_FULL;

	while(eaSize(dir_entry_stack) > 0)
	{
		top = eaSize(dir_entry_stack) - 1;
		dir_entry = (*dir_entry_stack)[top];
		child = &((*child_stack)[top]);

		if(*child < 0)
		{
			if(eaSize(&dir_entry->versions))
			{
				file_version = dir_entry->versions[0];
				
				if(*child < -1 && file_version->header_size > 0 && !(file_version->flags & FILEVERSION_HEADER_MATCHED) &&
					fileSpecIsNotRequired(client->filespec, dir_entry->path))
				{
					xfer = xferStartHeader(client->xferrer, dir_entry->path, client->headers, client->headers);
					if(!xfer)
						return PCL_XFERS_FULL;
				}
				*child = -1;

				if(!(file_version->flags & FILEVERSION_MATCHED))
				{
					if((! requiredOnly || ! fileSpecIsNotRequired(client->filespec, dir_entry->path)) && !onlyHeaders)
					{
						ret = getFileInternal(client, dir_entry->path, 1, client->root_folder, false, markFileMatched, &file_version->flags);
						if(ret != PCL_SUCCESS)
							return ret;
					}
				}
			}
			*child = 0;
		}

		if(*child < eaSize(&dir_entry->children))
		{
			eaPush(dir_entry_stack, dir_entry->children[*child]);
			++(*child);
			eaiPush(child_stack, getHeaders ? -2 : -1);
		}
		else
		{
			eaRemove(dir_entry_stack, top);
			eaiRemove(child_stack, top);
		}
	}

	eaDestroy(dir_entry_stack);
	eaiDestroy(child_stack);

	if(!eaSize(&client->xferrer->xfers))
	{
		if (!(client->file_flags & (PCL_IN_MEMORY|PCL_METADATA_IN_MEMORY)))
			triviaSetPatchCompletion(client->root_folder, requiredOnly && fileSpecHasNotRequired(client->filespec));
		if(!requiredOnly)
			client->got_all = 1;
	}

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

// Warning: A bunch of initialization code in this file depends on the ordering of these fields.
typedef struct MirrorHoggUserData
{
	PCL_Client *client;
	PCL_MirrorFileCallback callback;
	void *userdata;
	ProgressMeter *pm;
	const PCLFileSpec *filespec;
} MirrorHoggUserData;

static bool mirrorHogg(HogFile * hogg, HogFileIndex hog_file_index, const char * fileName, MirrorHoggUserData *rename_data)
{
	PCL_Client * client = rename_data->client;
	char file_path[MAX_PATH], fix_path[MAX_PATH];
	char *stripped;
	int filespec_index;

	PERFINFO_AUTO_START_FUNC();

	if(hog_file_index == -1)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return true;
	}

	if(client->file_flags & PCL_NO_MIRROR)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return true;
	}

	strcpy(fix_path, fileName);
	filespec_index = fileSpecGetHoggIndexForHandle(client->filespec, hogg);
	if(filespec_index < 0)
	{
		REPORT_ERROR_STRING(client, PCL_INTERNAL_LOGIC_BUG, "Every file in the hogg should be in the filespec");
		PERFINFO_AUTO_STOP_FUNC();
		return true;
	}
	stripped = fileSpecGetStripPath(client->filespec, filespec_index);
	if(!fileSpecGetMirrorStriped(client->filespec, filespec_index))
		fixPath(fix_path, stripped);

	// Don't mirror hidden files.
	// Note that relative paths aren't supported by getFileTree(), since that's mostly a Gimme thing.
	if (!pclFileIsIncludedByFileSpec(fix_path, fix_path, rename_data->filespec))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return true;
	}

	if(fileSpecIsMirrored(client->filespec, fix_path))
	{
		U32 hoggtime = hogFileGetFileTimestamp(hogg, hog_file_index);
		U32 hoggsize = hogFileGetFileSize(hogg, hog_file_index);
		PCL_FileFlags file_flags = client->file_flags;
		FWStatType statInfo;

		if(client->root_folder)
			sprintf(file_path, "%s/%s", client->root_folder, fix_path);
		else
			strcpy(file_path, fix_path);

		machinePath(file_path,file_path);

		if(fileSpecIsBin(client->filespec, fix_path))
		{
			if(file_flags & PCL_SKIP_BINS && fileExists(file_path) && (U32)fileLastChangedAbsolute(file_path) > hoggtime)
				return true;
			file_flags &= ~PCL_FILEFLAGS_BINMASK;
		}

		if(rename_data->callback)
			rename_data->callback(SAFESTR(file_path),PCL_SUCCESS,NULL,rename_data->userdata);
		rename_data->pm->files_so_far++;
		pclSendReportPercent(client, "Mirroring", rename_data->pm->files_so_far, rename_data->pm->files_total);
		if(client->mirrorCallback)
		{
			PERFINFO_AUTO_START("mirrorCallback", 1);
			client->mirrorCallback(client, client->mirrorUserData, 0, fix_path, rename_data->pm);
			PERFINFO_AUTO_STOP_CHECKED("mirrorCallback");
		}

// 		if( fileExists(file_path) &&
// 			fileLastChangedAbsolute(file_path) == hoggtime &&
// 			fileSize(file_path) == hoggsize )
		if (!client->verifyAllFiles &&
			0==fwStat(file_path, &statInfo) &&
			statInfo.st_mtime == hoggtime &&
			statInfo.st_size == hoggsize)
		{
			if (!(file_flags & PCL_SET_READ_ONLY) != !!(statInfo.st_mode & _S_IWRITE)) //!fileIsReadOnly(file_path))
				if(fwChmod(file_path, (file_flags & PCL_SET_READ_ONLY) ? _S_IREAD : (_S_IREAD|_S_IWRITE)) != 0)
				{
					char *errstr = strdupf("Why couldn't the file be made writeable? %s", file_path);
					REPORT_ERROR_STRING(client, PCL_COULD_NOT_WRITE_LOCKED_FILE, errstr);
					free(errstr);
				}
		}
		else
		{
			bool renamed;
			U32 len, i, packed_len;
			U8 *data = NULL;
			char *err_str = NULL;
			void *handle = NULL;
			pclMSpf("Mirroring: %s\n", file_path);
			hogFileGetSizes(hogg, hog_file_index, &len, &packed_len);
			if (len && !packed_len && !(file_flags & (PCL_BACKUP_TIMESTAMP_CHANGES|PCL_BACKUP_WRITEABLES)))
				handle = hogFileDupHandle(hogg, hog_file_index);
			else
				data = hogFileExtract(hogg, hog_file_index, &len, NULL);
			for(i=0; i<=5; ++i)
			{
				if(err_str) estrDestroy(&err_str);
				err_str = xferWriteFileToDisk(client, file_path, data, len, hoggtime, hogFileGetFileChecksum(hogg, hog_file_index), false, file_flags, &renamed, 0, handle);
				if(err_str == NULL) break;
				Sleep(100);
			}
			SAFE_FREE(data);
			if(err_str != NULL)
			{
				REPORT_ERROR_STRING(client, PCL_COULD_NOT_WRITE_LOCKED_FILE, err_str);
				estrDestroy(&err_str);
				PERFINFO_AUTO_STOP_FUNC();
				return false;
			}
			if(renamed)
				client->needs_restart = 1;
		}
	}

	PERFINFO_AUTO_STOP_FUNC();

	return true;
}

PCL_ErrorCode pclGotFiles(PCL_Client * client, int * count)
{
	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);

	if(count == NULL)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_NULL_PARAMETER;
	}

	*count = SAFE_MEMBER(client->xferrer,completed_files);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

// Count all real bytes received by HTTP patching.
static U64 pclHttpBytesReceived(PCL_Client *client)
{
	if (client->xferrer)
		return xferBytesReceived(client->xferrer);
	return 0;
}

// Count all real bytes received by this PCL_Client.
static U64 pclRealBytesReceived(PCL_Client *client)
{
	const LinkStats *stats = linkStats(client->link);
	U64 count = 0;
	if (stats)
		count += stats->recv.real_bytes;
	count += pclHttpBytesReceived(client);
	return count;
}

// Get the total number of bytes received on on the link, the same as 'received' in the stats.
PCL_ErrorCode pclActualTransferred(PCL_Client *client, U64 *bytes)
{
	if (!client)
		return PCL_CLIENT_PTR_IS_NULL;
	if (!bytes)
		return PCL_NULL_PARAMETER;

	*bytes = pclRealBytesReceived(client);
	return PCL_SUCCESS;
}

PCL_ErrorCode pclClearCounts(PCL_Client *client)
{
	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);
	if(client->xferrer)
	{
		client->xferrer->progress_recieved = 0;
		client->xferrer->progress_total = 0;
		client->xferrer->completed_files = 0;
		client->xferrer->total_files = 0;
		client->bytes_read_at_start = pclRealBytesReceived(client);
		client->http_bytes_read_at_start = pclHttpBytesReceived(client);
		client->time_started = timerCpuSeconds();
	}

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

PCL_ErrorCode pclNeedsRestart(PCL_Client * client, bool * needs_restart)
{
	CHECK_ERROR(client);

	if(needs_restart == NULL)
		return PCL_NULL_PARAMETER;

	*needs_restart = (client->needs_restart != 0);

	return PCL_SUCCESS;
}

static void makeFoldersForDir(DirEntry * dir, char * buf, int buf_size)
{
	int i;
	char * found;

	PERFINFO_AUTO_START_FUNC();

	makeDirectories(buf);

	for(i = 0; i < eaSize(&dir->children); i++)
	{
		if(eaSize(&dir->children[i]->children) > 0)
		{
			strcat_s(buf, buf_size, "/");
			strcat_s(buf, buf_size, dir->children[i]->name);
			makeFoldersForDir(dir->children[i], buf, buf_size);
			found = strrchr(buf, '/');
			found[0] = '\0';
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static PCL_ErrorCode makeFolders(PCL_Client * client)
{
	char path[MAX_PATH];

	if(!client->db)
	{
		RETURN_ERROR(client, PCL_MANIFEST_NOT_LOADED);
	}

	if(client->root_folder)
		strcpy(path, client->root_folder);
	else
		strcpy(path, client->project); // client->db->root.name);
	forwardSlashes(path);
	if(path[strlen(path) - 1] == '/')
		path[strlen(path) - 1] = '\0';
	makeFoldersForDir(&client->db->root, SAFESTR(path));

	return PCL_SUCCESS;
}

static void touchFileListFile(PCL_Client *client, const char *fname, bool writeable)
{
	char file_path[MAX_PATH];
	if(fname[0] == '/')
		fname++;
	if(client->root_folder)
		sprintf(file_path, "%s/%s", client->root_folder, fname);
	else
		strcpy(file_path, fname);
	machinePath(file_path,file_path);

	if( fileExists(file_path) &&
		fwChmod(file_path, writeable ? (_S_IREAD|_S_IWRITE) : _S_IREAD) != 0)
	{
		char *errstr = strdupf("Why couldn't the file be made writeable? %s", file_path);
		REPORT_ERROR_STRING(client, PCL_COULD_NOT_WRITE_LOCKED_FILE, errstr);
		free(errstr);
	}
}

static void mirrorFileListFile(PCL_Client *client, const char *fname, bool touch_unhogged)
{
	int hogg_index;
	HogFile *hogg;
	DirEntry *dir;

	PERFINFO_AUTO_START_FUNC();

	hogg_index = fileSpecGetHoggIndexForFile(client->filespec, fname);
	hogg = fileSpecGetHoggHandle(client->filespec, hogg_index);
	dir = patchFindPath(client->db, fname, 0);
	
	if(!dir || !eaSize(&dir->versions) || (dir->versions[0]->flags & FILEVERSION_DELETED))
	{
		REPORT_ERROR_STRING(client, PCL_INTERNAL_LOGIC_BUG, "Manifest changed during an operation?");
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	
	if(hogg)
	{
		HogFileIndex hfi;
		char *stripped;
		char hogg_path[MAX_PATH];

		strcpy(hogg_path, fname);
		stripped = fileSpecGetStripPath(client->filespec, hogg_index);
		stripPath(hogg_path, stripped);
		hfi = hogFileFind(hogg, hogg_path);

		if (fileSpecIsMirrored(client->filespec, fname))
		{
			if (!client->mirrorFilespec || !simpleFileSpecExcludesFile(fname, client->mirrorFilespec)) // Allow overriding to not mirror mirrored files
			{
				MirrorHoggUserData data = { client, NULL, NULL };
				ProgressMeter pm = {0};
				data.pm = &pm;
				mirrorHogg(hogg, hfi, hogg_path, &data);
			} else {
				// Specifically excluded from being mirrored
				char file_path[MAX_PATH];
				if(client->root_folder)
					sprintf(file_path, "%s/%s", client->root_folder, fname);
				else
					strcpy(file_path, fname);
				machinePath(file_path,file_path);
				if (fileExists(file_path))
				{
					// Delete it!
					deleteAndClean(file_path);
				}
			}
		} else if (client->mirrorFilespec && simpleFileSpecIncludesFile(fname, client->mirrorFilespec)) // Allow overriding to mirror unmirrored files
		{
			MirrorHoggUserData data = { client, NULL, NULL };
			mirrorHogg(hogg, hfi, hogg_path, &data);
		}
	}
	else if(touch_unhogged)
	{
		PCL_FileFlags file_flags = client->file_flags;
		if(fileSpecIsBin(client->filespec, fname))
			file_flags &= ~PCL_FILEFLAGS_BINMASK;
		touchFileListFile(client, fname, !(file_flags & PCL_SET_READ_ONLY));
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static PCL_ErrorCode mirrorFileList(PCL_Client *client, const char **fnames, const char **mirror, const char **touch)
{
	int i;
	ProgressMeter pm = {0};

	PERFINFO_AUTO_START_FUNC();
	
	if(!client->filespec)
	{
		PERFINFO_AUTO_STOP_FUNC();
		RETURN_ERROR(client, PCL_FILESPEC_NOT_LOADED);
	}
		
	loadstart_printf(	"Mirroring files from hogg to disk (%d new, %d to check, %d checked out)...",
						eaSize(&fnames),
						eaSize(&mirror),
						eaSize(&touch));
						
	pclMSpf("mirroring %d files\n",
			eaSize(&fnames));

	pm.files_so_far = 0;
	pm.files_total = eaSize(&fnames);
	for(i = 0; i < eaSize(&fnames); i++)
	{
		pm.files_so_far++;
		pclSendReportPercent(client, "Mirroring", pm.files_so_far, pm.files_total);
		if(client->mirrorCallback){
			PERFINFO_AUTO_START("mirrorCallback", 1);
			client->mirrorCallback(client, client->mirrorUserData, 0, fnames[i], &pm);
			PERFINFO_AUTO_STOP_CHECKED("mirrorCallback");
		}
		mirrorFileListFile(client, fnames[i], false); // don't touch non-hogged files, they've already been written to
	}

	pclMSpf("mirroring %d mirrors\n",
			eaSize(&mirror));

	pm.files_so_far = 0;
	pm.files_total = eaSize(&mirror);
	for(i = 0; i < eaSize(&mirror); i++)
	{
		pm.files_so_far++;
		pclSendReportPercent(client, "Mirroring", pm.files_so_far, pm.files_total);
		if(client->mirrorCallback){
			PERFINFO_AUTO_START("mirrorCallback", 1);
			client->mirrorCallback(client, client->mirrorUserData, 0, mirror[i], &pm);
			PERFINFO_AUTO_STOP_CHECKED("mirrorCallback");
		}
		mirrorFileListFile(client, mirror[i], true); // GGFIXME: this is slow
	}

	pclMSpf("mirroring %d touches\n",
			eaSize(&touch));

	pm.files_so_far = 0;
	pm.files_total = eaSize(&touch);
	for(i = 0; i < eaSize(&touch); i++)
	{
		pm.files_so_far++;
		pclSendReportPercent(client, "Mirroring", pm.files_so_far, pm.files_total);
		if(client->mirrorCallback){
			PERFINFO_AUTO_START("mirrorCallback", 1);
			client->mirrorCallback(client, client->mirrorUserData, 0, touch[i], &pm);
			PERFINFO_AUTO_STOP_CHECKED("mirrorCallback");
		}
		touchFileListFile(client, touch[i], true);
	}

	pclMSpf("done mirroring files\n");
	loadend_printf("done.");

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

static PCL_ErrorCode mirrorFiles(PCL_Client * client, PCL_MirrorFileCallback callback, void *userdata, const PCLFileSpec * filespec)
{
	int hog_count, i;
	ProgressMeter pm = {0};
	MirrorHoggUserData data = { client, callback, userdata, &pm, filespec };

	PERFINFO_AUTO_START_FUNC();

	if(!client->filespec)
	{
		PERFINFO_AUTO_STOP_FUNC();
		RETURN_ERROR(client, PCL_FILESPEC_NOT_LOADED);
	}

	if(client->file_flags & PCL_NO_WRITE)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_SUCCESS;
	}

	hog_count = fileSpecHoggCount(client->filespec);
	for(i = 0; i < hog_count; i++)
	{
		HogFile *hogg = fileSpecGetHoggHandle(client->filespec, i);
		if(hogg)
		{
			// FIXME COR-13786: hogScanAllFiles() needs a flush for new files to appear
			hogFileModifyFlush(hogg);
			hogScanAllFiles(hogg, mirrorHogg, &data);
		}
		if (client->error)
		{
			PERFINFO_AUTO_STOP_FUNC();
			return client->error;
		}
	}

	if(client->file_flags & PCL_SET_MAKE_FOLDERS)
	{
		PCL_ErrorCode result = makeFolders(client);
		PERFINFO_AUTO_STOP_FUNC();
		return result;
	}
	else
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_SUCCESS;
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static void unmirrorFilesAndFinishCheckin(PCL_Client *client,  char **on_disk, char **in_db, bool forced)
{
	// TODO: give this the same structure as mirrorFiles() (callbacks etc.)
	int i;

	PERFINFO_AUTO_START_FUNC();

	if(!client->filespec)
	{
		REPORT_ERROR(client, PCL_FILESPEC_NOT_LOADED);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	if(eaSize(&in_db) != eaSize(&on_disk))
	{
		REPORT_ERROR(client, PCL_FILESPEC_NOT_LOADED);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	for(i = 0; i < eaSize(&in_db); i++)
	{
		char in_hogg[MAX_PATH];
		bool mirrored = fileSpecIsMirrored(client->filespec, in_db[i]);
		int hogg_index = fileSpecGetHoggIndexForFile(client->filespec, in_db[i]);
		HogFile *hogg = fileSpecGetHoggHandle(client->filespec, hogg_index);
		DirEntry *dir = patchFindPath(client->db, in_db[i], 0);

		if(hogg)
		{
			strcpy(in_hogg, in_db[i]);
			stripPath(in_hogg, fileSpecGetStripPath(client->filespec, hogg_index));
		}

		if(on_disk[i])
		{
			PCL_FileFlags file_flags = client->file_flags;
			if(fileSpecIsBin(client->filespec, in_db[i]))
				file_flags &= ~PCL_FILEFLAGS_BINMASK;

			// Remove Checkout
			patchSetLockedbyClient(client->db, dir, NULL);
			patchSetAuthorClient(client->db, dir, client->author);

			// Pack back into hoggs
			if(hogg && devassert(mirrored))
			{
				U32 len;
				U8 *data = fileAlloc(on_disk[i], &len);

				hogFileModifyUpdateNamed(hogg, in_hogg, data, len, fileLastChangedAbsolute(on_disk[i]), NULL);
			}

			// Mark read-only
			if(!forced)
			{
				if(fwChmod(on_disk[i], (file_flags & PCL_SET_READ_ONLY) ? _S_IREAD : (_S_IREAD|_S_IWRITE)) != 0)
				{
					char *errstr = strdupf("Why couldn't the file be made writeable? %s", on_disk[i]);
					REPORT_ERROR_STRING(client, PCL_COULD_NOT_WRITE_LOCKED_FILE, errstr);
					free(errstr);
				}
			}
		}
		else // deleted
		{
			direntryRemoveAndDestroy(client->db, dir);
			if(hogg && devassert(mirrored))
				hogFileModifyDeleteNamed(hogg, in_hogg);
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static void addToProgress(PCL_Client * client, DirEntry * dir_entry, bool requiredOnly, bool getHeaders, bool onlyHeaders)
{
	int i;
	FileVersion * file_version;

	if(eaSize(&dir_entry->versions))
	{
		file_version = dir_entry->versions[0];
		if(!(file_version->flags & FILEVERSION_MATCHED) && !onlyHeaders)
		{
			if(!requiredOnly || !fileSpecIsNotRequired(client->filespec, dir_entry->path))
			{
				client->xferrer->progress_total += file_version->size;
				client->xferrer->total_files++;
			}
		}
		if (!(file_version->flags & FILEVERSION_HEADER_MATCHED) && getHeaders && file_version->header_size)
		{
			client->xferrer->progress_total += file_version->header_size;
			client->xferrer->total_files++;
		}
	}

	for(i = 0; i < eaSize(&dir_entry->children); i++)
	{
		addToProgress(client, dir_entry->children[i], requiredOnly, getHeaders, onlyHeaders);
	}	
}

static int s_deleteme_count = 0;

static FileScanAction deleteDeleteMe(char* dir, struct _finddata32_t* data, PCL_Client *client)
{
	if(strEndsWith(data->name, ".deleteme"))
	{
		int ret, i;
		char full_path[MAX_PATH];

		s_deleteme_count++;
		sprintf(full_path, "%s/%s", dir, data->name);
		fwChmod(full_path, _S_IREAD|_S_IWRITE);
		ret = unlink(full_path);
		for(i = 0; ret != 0 && i < 60; i++)
		{
			Sleep(500);
			printf("Retrying to delete %s\n", full_path);
			fwChmod(full_path, _S_IREAD|_S_IWRITE);
			ret = unlink(full_path);
		}
		if(i >= 60)
			printf("Giving up deleting %s\n", full_path);
	}

	return FSA_EXPLORE_DIRECTORY;
}

static FileScanAction deleteRogueHoggsInDir(char* dir, struct _finddata32_t* data, PCL_Client *client)
{
	PERFINFO_AUTO_START_FUNC();

	if(strEndsWith(data->name, ".hogg"))
	{
		int ret, i;
		char full_path[MAX_PATH];
		char hogg_path[MAX_PATH];

		sprintf(hogg_path, "piggs/%s", data->name);
		if(fileSpecIsAHogg(client->filespec, hogg_path))
			return FSA_EXPLORE_DIRECTORY;

		// This is a special case for dynamic patch data
		if(stricmp(hogg_path, "piggs/dynamic.hogg")==0)
			return FSA_EXPLORE_DIRECTORY;

		s_deleteme_count++;
		sprintf(full_path, "%s/%s", dir, data->name);
		fwChmod(full_path, _S_IREAD|_S_IWRITE);
		ret = unlink(full_path);
		for(i = 0; ret != 0 && i < 60; i++)
		{
			Sleep(500);
			printf("Retrying to delete %s\n", full_path);
			fwChmod(full_path, _S_IREAD|_S_IWRITE);
			ret = unlink(full_path);
		}
		if(i >= 60)
			printf("Giving up deleting %s\n", full_path);
	}

	PERFINFO_AUTO_STOP();

	return FSA_EXPLORE_DIRECTORY;
}

// See deleteRogueFiles() for a description.
static FileScanAction deleteAccidentalBins(char* dir, struct _finddata32_t* data, PCL_Client *client)
{
	PERFINFO_AUTO_START_FUNC();

	// Don't follow links.
	if (data->attrib & FILE_ATTRIBUTE_REPARSE_POINT)
		return FSA_NO_EXPLORE_DIRECTORY;

	// Restrict ourselves to normal files within the target date range.
	if (!(data->attrib & (FILE_ATTRIBUTE_READONLY|FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM|FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_DEVICE|FILE_ATTRIBUTE_TEMPORARY))
		&& data->time_write > 1309478400		// July 1, 2011
		&& data->time_write < 1314835200)		// September 1, 2011
	{
		char full_path[MAX_PATH];
		s_deleteme_count++;
		sprintf(full_path, "%s/%s", dir, data->name);
		unlink(full_path);
	}

	PERFINFO_AUTO_STOP();

	return FSA_EXPLORE_DIRECTORY;
}

// If requested, delete files which don't belong.
static void deleteRogueFiles(PCL_Client *client)
{
	PERFINFO_AUTO_START_FUNC();

	if(client->file_flags & PCL_CLEAN_HOGGS)
	{
		char path[MAX_PATH];
		size_t len;

		loadstart_printf("Deleting rogue files...");
		s_deleteme_count = 0;

		// Delete rogue hoggs.
		machinePath(path, client->root_folder);
		len = strlen(path);
		strcat(path, "\\piggs");
		fileScanAllDataDirs(path, deleteRogueHoggsInDir, client);

		// Delete files that were accidentally written to disk on client machines by a previous Game Client binning bug.
		// See [COR-14415].
		// I can't find a JIRA for the original defect, but it was fixed with SVN 121972, SVN 121887,
		// Gimme StarTrek 202593, and Gimme StarTrek 202609.
		// The purpose of this change is to delete these files while minimizing the chances of collateral damage to
		// customer's computers, since recursive deletes in the wild always carry some inherent risk.
		if (client->project && strStartsWith(client->project, "startrek") && strstri(client->project, "client"))
		{
			path[len] = 0;
			strcat(path, "\\data\\bin\\geobin");
			fileScanAllDataDirs(path, deleteAccidentalBins, client);
		}

		loadend_printf("done (%d deleted).", s_deleteme_count);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static int getFileTree(PCL_Client * client, const char * rootFolder, bool requiredOnly, bool getHeaders, bool onlyHeaders,
						PCL_GetBatchCallback callback, void * userData, PCL_MirrorFileCallback mirror_callback, void *mirror_userdata, const PCLFileSpec * filespec)
{
	int hog_count, i;
	HogFile * hogg;
	StateStruct * state;
	char root_path[MAX_PATH];
	ExamineHoggUserdata ehud = {0};
	ehud.client = client;

	PERFINFO_AUTO_START_FUNC();

	CHECK_STATE_PI(client);
	if(!client->db)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_MANIFEST_NOT_LOADED);
	if(!client->filespec)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_FILESPEC_NOT_LOADED);

	pclSendReport(client, PCLSMS_UPDATE, "Initializing");

	if(rootFolder && rootFolder[0])
	{
		char root[MAX_PATH];
		int len;
		strcpy(root, rootFolder);
		forwardSlashes(root);
		len = (int)strlen(root);
		if(root[len - 1] == '/')
			root[len - 1] = '\0';
		client->root_folder = strdup(root);
	}
	else
	{
		client->root_folder = strdup(client->project); // client->db->root.name);
	}

	if(getHeaders)
	{
		bool created;
		char filename[MAX_PATH];
		int hog_error;

		sprintf(filename, "%s/%s/headers.hogg", client->root_folder, PATCH_DIR);
		machinePath(filename,filename);

		if(client->headers)
			hogFileDestroy(client->headers, true);
		client->headers = hogFileRead(filename, &created, PIGERR_PRINTF, &hog_error, HOG_MUST_BE_WRITABLE);
		if(hog_error)
		{
			RETURN_ERROR_PERFINFO_STOP(client, PCL_HOGG_READ_ERROR);
		}
		hogFileSetSingleAppMode(client->headers, true);
	}

	state = calloc(1, sizeof(StateStruct));
	state->general_state = STATE_XFERRING;
	state->call_state = STATE_GET_REQUIRED_FILES;
	state->func_state = STATE_GET_REQUIRED_FILES;
	state->get_required.callback = callback;
	state->get_required.userData = userData;
	state->get_required.mirror_callback = mirror_callback;
	state->get_required.mirror_userdata = mirror_userdata;
	pclStateAdd(client, state);

	pclSendReport(client, PCLSMS_UPDATE, "Loading hoggs");
	loadstart_printf("Loading hoggs...");
	pclLoadHoggs(client);
	loadend_printf("done.");

	pcllog(client, PCLLOG_FILEONLY, "Syncing %sfiles", requiredOnly ? "required " : "");

	pclSendReport(client, PCLSMS_UPDATE, "Deleting files");
	if (!onlyHeaders && !(client->file_flags & PCL_NO_DELETEME_CLEANUP))
	{
		loadstart_printf("Deleting files..."); s_deleteme_count = 0;
		machinePath(root_path,client->root_folder);
		fileScanAllDataDirs(root_path, deleteDeleteMe, client);
		loadend_printf("done (%d deleted).", s_deleteme_count);
	}

	// If requested, delete files which don't belong.
	deleteRogueFiles(client);

	pclSendReport(client, PCLSMS_UPDATE, "Scanning files");
	loadstart_printf("Examining hoggs...");
	client->xferrer->total_files = 0;
	addToProgress(client, &client->db->root, false, false, false);
	ehud.pm.files_total = client->xferrer->total_files;

	hog_count = fileSpecHoggCount(client->filespec);
	ehud.filespec = filespec;
	for(i = 0; i < hog_count; i++)
	{
		hogg = fileSpecGetHoggHandle(client->filespec, i);
		if(hogg)
		{
			// FIXME COR-13786: hogScanAllFiles() needs a flush for new files to appear
			hogFileModifyFlush(hogg);
			pcllog(client, PCLLOG_FILEONLY, "Scanning hogg %s", hogFileGetArchiveFileName(hogg));
			hogScanAllFiles(hogg, examineFileInHogg, &ehud);
		}
		else
		{
			char path[MAX_PATH];
			machinePath(path, client->root_folder);
			fileScanAllDataDirs(path, examineFileOnDisk, &ehud);
		}
	}
	if(getHeaders && client->headers)
	{
		// FIXME COR-13786: hogScanAllFiles() needs a flush for new files to appear
		hogFileModifyFlush(client->headers);
		hogScanAllFiles(client->headers, examineHeaderHogg, client);
	}
	if(client->examineCompleteCallback)
	{
		PERFINFO_AUTO_START("examineCompleteCallback", 1);
		client->examineCompleteCallback(client, client->examineCompleteUserData, 0, NULL, &ehud.pm);
		PERFINFO_AUTO_STOP_CHECKED("examineCompleteCallback");
	}
	loadend_printf("done.");

	client->xferrer->progress_recieved = 0;
	client->xferrer->progress_total = 0;
	client->xferrer->completed_files = 0;
	client->xferrer->total_files = 0;
	client->bytes_read_at_start = pclRealBytesReceived(client);
	client->http_bytes_read_at_start = pclHttpBytesReceived(client);
	client->time_started = timerCpuSeconds();
	addToProgress(client, &client->db->root, requiredOnly, getHeaders, onlyHeaders);

	eaPush(&state->get_required.dir_entry_stack, &client->db->root);
	eaiPush(&state->get_required.child_stack, getHeaders ? -2 : -1);
	state->get_required.requiredOnly = requiredOnly;
	state->get_required.getHeaders = getHeaders;
	state->get_required.onlyHeaders = onlyHeaders;

	// Save PCLFileSpec for later steps that may need it, like mirroring.
	if (filespec)
		state->get_required.filespec = *filespec;

	resetView(client);

	updateProgressTimer(client);

	pclSendReport(client, PCLSMS_UPDATE, "Starting download");

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

PCL_ErrorCode pclResetStateToIdle(PCL_Client* client)
{
	PERFINFO_AUTO_START_FUNC();

	while(	eaSize(&client->state) &&
			client->state[0]->general_state != STATE_IDLE)
	{
		pclStateRemoveHead(client);
	}
	
	client->error = PCL_SUCCESS;

	PERFINFO_AUTO_STOP_FUNC();
	
	return PCL_SUCCESS;
}

PCL_ErrorCode pclResetRoot(PCL_Client * client, const char * root_folder)
{
	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);

	SAFE_FREE(client->root_folder);

	if(root_folder)
	{
		int len = (int)strlen(root_folder);
		client->root_folder = strdup(root_folder);
		forwardSlashes(client->root_folder);
		if(client->root_folder[len-1] == '/')
			client->root_folder[len-1] = '\0';
	}

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

PCL_ErrorCode pclGetAllFiles(PCL_Client * client, PCL_GetBatchCallback callback, void * userData, const PCLFileSpec * filespec)
{
	PCL_ErrorCode error;
	PERFINFO_AUTO_START_FUNC();
	CHECK_ERROR_PI(client);

	error = getFileTree(client, client->root_folder, false, false, false, callback, userData, NULL, NULL, filespec);
	PERFINFO_AUTO_STOP_FUNC();
	return error;
}

PCL_ErrorCode pclGetRequiredFiles(PCL_Client * client, bool getHeaders, bool onlyHeaders, PCL_GetBatchCallback callback, void * userData, const PCLFileSpec * filespec)
{
	PCL_ErrorCode error;
	PERFINFO_AUTO_START_FUNC();
	CHECK_ERROR_PI(client);

	error = getFileTree(client, client->root_folder, true, getHeaders, onlyHeaders, callback, userData, NULL, NULL, filespec);
	PERFINFO_AUTO_STOP_FUNC();
	return error;
}

static bool isFileUpToDateInternal(PCL_Client * client, const char * fileName, DirEntry * dir_entry, HogFile * hogg, int index_to_hogg)
{
	int hogg_file_index;
	char * stripped;
	char nameInHogg[MAX_PATH];
	FileVersion * file_version;
	bool deleted, ret = true;

	PERFINFO_AUTO_START_FUNC();

	if(!dir_entry || !client || !fileName)
	{
		REPORT_ERROR(client, PCL_NULL_PARAMETER);
		PERFINFO_AUTO_STOP_FUNC();
		return true;
	}
	if(eaSize(&dir_entry->versions) == 0)
	{
		REPORT_ERROR(client, PCL_FILE_NOT_FOUND);
		PERFINFO_AUTO_STOP_FUNC();
		return true;
	}
	if(eaSize(&dir_entry->versions) > 1)
	{
		REPORT_ERROR(client, PCL_INTERNAL_LOGIC_BUG);
		PERFINFO_AUTO_STOP_FUNC();
		return true;
	}
	if(!fileName[0])
	{
		REPORT_ERROR(client, PCL_INVALID_PARAMETER);
		PERFINFO_AUTO_STOP_FUNC();
		return true;
	}

	file_version = dir_entry->versions[0];
	deleted = !!(file_version->flags & FILEVERSION_DELETED);
	strcpy(nameInHogg, fileName);

	stripped = fileSpecGetStripPath(client->filespec, index_to_hogg);
	stripPath(nameInHogg, stripped);

	if(file_version->header_size == 0)
	{
		file_version->flags |= FILEVERSION_HEADER_MATCHED;
	}
	else if(client->headers)
	{
		hogg_file_index = hogFileFind(client->headers, nameInHogg);
		if(hogg_file_index != HOG_INVALID_INDEX &&
			file_version->header_size == hogFileGetFileSize(client->headers, hogg_file_index) &&
			file_version->header_checksum == hogFileGetFileChecksum(client->headers, hogg_file_index))
		{
			file_version->flags |= FILEVERSION_HEADER_MATCHED;
		}
		else
		{
			file_version->flags &= ~FILEVERSION_HEADER_MATCHED;
		}
	}

	if(hogg)
	{
		char nameOnDisk[MAX_PATH];
		sprintf(nameOnDisk, "%s/%s", client->root_folder, fileName);

		hogg_file_index = hogFileFind(hogg, nameInHogg);

		if(deleted ? hogg_file_index != HOG_INVALID_INDEX : hogg_file_index == HOG_INVALID_INDEX)
		{
			pclMSpf("file not found in hogg: %s", fileName);
			ret = false;
		}
		else if(file_version->size != hogFileGetFileSize(hogg, hogg_file_index))
		{
			pclMSpf("size doesn't match: %s", fileName);
			ret = false;
		}
		else if(!timestampsMatch(file_version->modified, hogFileGetFileTimestamp(hogg, hogg_file_index)))
		{
			pclMSpf("timestamp doesn't match: %s\n", fileName);
			ret = false;
		}
		else if(file_version->checksum != hogFileGetFileChecksum(hogg, hogg_file_index))
		{
			pclMSpf("checksum doesn't match: %s\n", fileName);
			ret = false;
		}
		else if(client->file_flags & PCL_SKIP_BINS && fileSpecIsBin(client->filespec, nameInHogg) && fileExists(nameOnDisk) && (U32)fileLastChangedAbsolute(nameOnDisk) > file_version->modified)
		{
			ret = false;
		}
		else if(client->verifyAllFiles)
		{
			U32		fileBytesRead;
			bool	checksumIsValid;
			U8*		data = hogFileExtract(hogg, hogg_file_index, &fileBytesRead, &checksumIsValid);
			
			if(!data){
				ret = false;
				printfColor(COLOR_BRIGHT|COLOR_RED,
							"File failed to load from hogg: %s\n",
							nameInHogg);
				filelog_printf("force_verify", "File failed to load from hogg: %s", nameInHogg);
			}
			else if(fileBytesRead != file_version->size){
				ret = false;
				printfColor(COLOR_BRIGHT|COLOR_RED,
							"File loaded the wrong size from hogg (expected %d, got %d): %s\n",
							file_version->size,
							fileBytesRead,
							nameInHogg);
				filelog_printf("force_verify", "File loaded the wrong size from hogg (expected %d, got %d): %s", file_version->size, fileBytesRead, nameInHogg);
			}
			else if(!checksumIsValid){
				ret = false;
				printfColor(COLOR_BRIGHT|COLOR_RED,
							"File checksum invalid in hogg: %s\n",
							nameInHogg);
				filelog_printf("force_verify", "File checksum invalid in hogg: %s", nameInHogg);
			}
			
			SAFE_FREE(data);
		}
		
		if(ret)
		{
			U32 pack_size, unpack_size;
			NewPigEntry entry = {0};
			entry.fname = nameInHogg;
			hogFileGetSizes(hogg, hogg_file_index, &unpack_size, &pack_size);
			if((pack_size > 0) && doNotCompressMyExt(&entry))
				ret = false;
		}
	}
	else
	{
 		U32 checksum[4];
		char file_path[MAX_PATH];
		if(client->root_folder)
			sprintf(file_path, "%s/%s", client->root_folder, nameInHogg);
		else
			strcpy(file_path, nameInHogg);
		machinePath(file_path,file_path);

		if(deleted ? fileExists(file_path) : !fileExists(file_path))
		{
			ret = false;
		}
		else if(file_version->size != fileSize(file_path))
		{
			ret = false;
		}
		else if(!timestampsMatch(file_version->modified, fileLastChangedAbsolute(file_path)))
		{
			ret = false;
		}
		else if(client->file_flags & PCL_SKIP_BINS && fileSpecIsBin(client->filespec, nameInHogg) && fileExists(file_path) && (U32)fileLastChangedAbsolute(file_path) > file_version->modified)
		{
			ret = false;
		}
 		else if(((client->file_flags & PCL_VERIFY_CHECKSUM) || client->verifyAllFiles) &&
 				checksumFile(file_path, checksum) && file_version->checksum != checksum[0])
 		{
 			ret = false;
 		}
	}

	if(ret)
	{
		pclMSpf("up to date: %s\n", fileName);
		file_version->flags |= FILEVERSION_MATCHED;
	}
	else
	{
		file_version->flags &= ~FILEVERSION_MATCHED;
	}

	PERFINFO_AUTO_STOP_FUNC();

	return ret;
}

PCL_ErrorCode pclIsFileUpToDate(PCL_Client * client, const char * fileName, bool * up_to_date)
{
	DirEntry * dir_entry;
	int index_to_hogg;
	HogFile * hogg;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);

	if(!up_to_date)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_NULL_PARAMETER);
	if(!client->db)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_MANIFEST_NOT_LOADED);
	if(!client->filespec)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_FILESPEC_NOT_LOADED);
	
	dir_entry = patchFindPath(client->db, fileName, 0);
	if(!dir_entry || eaSize(&dir_entry->versions) == 0)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_FILE_NOT_FOUND);

	pclLoadHoggs(client);

	index_to_hogg = fileSpecGetHoggIndexForFile(client->filespec, fileName);
	hogg = fileSpecGetHoggHandle(client->filespec, index_to_hogg);
	*up_to_date = isFileUpToDateInternal(client, fileName, dir_entry, hogg, index_to_hogg);

	PERFINFO_AUTO_STOP_FUNC();

	return client->error;
}

// Allocates memory to store the header - caller must free
PCL_ErrorCode pclGetFileHeader(PCL_Client * client, const char * fileName, HogFile **hog_handle, HogFileIndex *hog_index)
{
	DirEntry * dir_entry;
	int hogg_index;
	HogFile * hogg;
	char fileNameInHogg[MAX_PATH], * stripped;
	HogFileIndex hfi;
	bool up_to_date;
	PCL_ErrorCode error_code;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);

	if(!(hog_handle && hog_index))
	{
		RETURN_ERROR_PERFINFO_STOP(client, PCL_NULL_PARAMETER);
	}

	CHECK_STATE_PI(client);
	
	if(!client->db)
	{
		RETURN_ERROR_PERFINFO_STOP(client, PCL_MANIFEST_NOT_LOADED);
	}
	else if(!client->filespec)
	{
		RETURN_ERROR_PERFINFO_STOP(client, PCL_FILESPEC_NOT_LOADED);
	}

	dir_entry = patchFindPath(client->db, fileName, 0);
	if(!dir_entry)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_FILE_NOT_FOUND;
	}

	if(!eaSize(&dir_entry->versions))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_FILE_NOT_FOUND;
	}

	if(dir_entry->versions[0]->header_size == 0)
	{
		*hog_handle = NULL;
		*hog_index = HOG_INVALID_INDEX;

		PERFINFO_AUTO_STOP_FUNC();
		return PCL_SUCCESS;
	}
	
	error_code = pclIsFileUpToDate(client, fileName, &up_to_date);
	if(error_code)
	{
		RETURN_ERROR_PERFINFO_STOP(client, error_code);
	}
	if(up_to_date)
	{
		hogg_index = fileSpecGetHoggIndexForFile(client->filespec, fileName);
		if(hogg_index < 0)
		{
			PERFINFO_AUTO_STOP_FUNC();
			return PCL_NOT_IN_FILESPEC;
		}

		hogg = fileSpecGetHoggHandle(client->filespec, hogg_index);
		stripped = fileSpecGetStripPath(client->filespec, hogg_index);

		strcpy(fileNameInHogg, fileName);
		stripPath(fileNameInHogg, stripped);

		hfi = hogFileFind(hogg, fileNameInHogg);
		if(hfi == HOG_INVALID_INDEX)
		{
			RETURN_ERROR_PERFINFO_STOP(client, PCL_FILE_NOT_FOUND);
		}

		*hog_handle = hogg;
		*hog_index = hfi;
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_SUCCESS;
	}

	strcpy(fileNameInHogg, fileName);
	if(!client->headers)
	{
		RETURN_ERROR_PERFINFO_STOP(client, PCL_HEADER_HOGG_NOT_LOADED);
	}

	hfi = hogFileFind(client->headers, fileNameInHogg);
	if(hfi == HOG_INVALID_INDEX)
	{
		RETURN_ERROR_PERFINFO_STOP(client, PCL_HEADER_NOT_UP_TO_DATE);
	}

	if(	dir_entry->versions[0]->header_size != hogFileGetFileSize(client->headers, hfi) ||
		dir_entry->versions[0]->header_checksum != hogFileGetFileChecksum(client->headers, hfi))
	{
		RETURN_ERROR_PERFINFO_STOP(client, PCL_HEADER_NOT_UP_TO_DATE);
	}

	*hog_handle = client->headers;
	*hog_index = hfi;

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

PCL_ErrorCode pclGetFileTo(PCL_Client * client, const char * fileName, const char * fileNameToWrite, HogFile * hogg,
						   int priority, PCL_GetFileCallback callback, void * userData)
{
	PatchXfer * xfer;
	StateStruct * state;
	PCL_ErrorCode error;

	PERFINFO_AUTO_START_FUNC();

	state = client->state[0];

	CHECK_ERROR_PI(client);

	if(state->general_state != STATE_XFERRING && state->general_state != STATE_IDLE)
		return PCL_WAITING;

	xfer = xferStart(client->xferrer, fileName, fileNameToWrite, priority, 0, false, false, client->file_flags, callback, hogg, hogg, NULL, userData);

	if(xfer && state->general_state == STATE_IDLE)
	{
		state = calloc(1, sizeof(StateStruct));
		state->general_state = STATE_XFERRING;
		state->call_state = STATE_GET_FILE;
		state->func_state = STATE_GET_FILE;
		pclStateAdd(client, state);
	}

	resetView(client);

	error = xfer ? PCL_SUCCESS : PCL_XFERS_FULL;

	PERFINFO_AUTO_STOP_FUNC();

	return error;
}

static void deleteAndClean(const char *file_path)
{
	char buffer[MAX_PATH];
	strcpy(buffer, file_path);
	fwChmod(file_path, _S_IREAD|_S_IWRITE);
	unlink(file_path);
	rmdirtree(buffer);
}

static void addToFileListInternal(PCL_Client *client,
							DirEntry *dir_entry,
							int recurse,
							bool get_locked,
							const char ***file_list,
							const char ***mirror,
							const char ***touch,
							const char ***deleted,
							const char* filespecBasePath,
							const PCLFileSpec* filespec)
{
	S32 i;
	S32 doInclude = 0;

	if(eaSize(&dir_entry->versions) > 0)
	{
		const char* subPath;
		
		filespecBasePath = NULL_TO_EMPTY(filespecBasePath);
		assert(strStartsWith(dir_entry->path, filespecBasePath));
		subPath = dir_entry->path + strlen(filespecBasePath);
		assert(	!filespecBasePath[0] ||
				!subPath[0] ||
				subPath[0] == '/');

		doInclude = pclFileIsIncludedByFileSpec(subPath, dir_entry->path, filespec);
	}

	if(doInclude)
	{
		if(get_locked || !isLockedByClient(client, dir_entry))
		{
			int hogg_index = fileSpecGetHoggIndexForFile(client->filespec, dir_entry->path);
			HogFile *hogg = fileSpecGetHoggHandle(client->filespec, hogg_index);

			if(dir_entry->versions[0]->flags & FILEVERSION_DELETED)
			{
				char file_path[MAX_PATH];
				if(client->root_folder)
					sprintf(file_path, "%s/%s", client->root_folder, dir_entry->path);
				else
					strcpy(file_path, dir_entry->path);
				machinePath_s(file_path, MAX_PATH, file_path);

				if(hogg)
				{
					const char*		stripped = fileSpecGetStripPath(client->filespec, hogg_index);
					char			pathInHogg[MAX_PATH];
					HogFileIndex	hfi;
					
					strcpy(pathInHogg, dir_entry->path);
					stripPath(pathInHogg, stripped);
					hfi = hogFileFind(hogg, pathInHogg);
					
					if(hfi != HOG_INVALID_INDEX)
					{
						//printf("Deleting in hogg: %d:%s\n", hfi, pathInHogg);
							
						hogFileModifyDeleteNamed(hogg, pathInHogg);
					}
				}

				if(fileExists(file_path))
				{
					if(!(client->file_flags & PCL_BACKUP_WRITEABLES) || fileIsReadOnly(file_path))
					{
						U32 on_disk_time = (U32)fileLastChangedAbsolute(file_path);
						if(!(client->file_flags & PCL_KEEP_RECREATED_DELETED) ||
							(on_disk_time <= dir_entry->versions[0]->modified ||
							timestampsMatch(on_disk_time, dir_entry->versions[0]->modified)) &&
							fileIsReadOnly(file_path))
						{
							// Old file, read-only, Silent delete
							deleteAndClean(file_path);
						}
						else if (	!(on_disk_time > dir_entry->versions[0]->modified &&
									!timestampsMatch(on_disk_time, dir_entry->versions[0]->modified) &&
									!fileIsReadOnly(file_path)))
						{
							// Really just checking if the file is read-only (all other cases are handled ablove)
							// Likely old file, fails the "will get checked in even though deleted" check far below, must
							//  remove it, otherwise we end up with a file on disk which neither gets removed on get latest
							//  or checked in!
							// Backup and delete
							pcllog(	client,
									PCLLOG_WARNING,
									"Local file is newer than deleted file in database, renaming to .bak\n"
									"   File: %s",
									file_path);
							fileRenameToBak(file_path);
							deleteAndClean(file_path);
						} else {
							eaPush(deleted, dir_entry->path);
						}
					}
				}
			}
			else if(isFileUpToDateInternal(client, dir_entry->path, dir_entry, hogg, hogg_index))
				eaPush(mirror, dir_entry->path);
			else
				eaPush(file_list, dir_entry->path);

			if(get_locked && isLockedByClient(client, dir_entry))
				eaPush(touch, dir_entry->path);
		}
		else
		{
			eaPush(touch, dir_entry->path);
		}
	}

	if(recurse < 0)
		return;

	for(i = 0; i < eaSize(&dir_entry->children); i++)
	{
		if(recurse)
		{
			addToFileListInternal(client,
							dir_entry->children[i],
							recurse,
							get_locked,
							file_list,
							mirror,
							touch,
							deleted,
							filespecBasePath,
							filespec);
		}
		else
		{
			DirEntry fake_dir;

			ZeroStruct(&fake_dir);
			fake_dir.name = dir_entry->children[i]->name;
			fake_dir.versions = dir_entry->children[i]->versions;
			fake_dir.parent = dir_entry->children[i]->parent;
			addToFileListInternal(client,
							&fake_dir,
							recurse,
							get_locked,
							file_list,
							mirror,
							touch,
							deleted,
							filespecBasePath,
							filespec);
		}
	}
}

static void addToFileList(	PCL_Client *client,
	DirEntry *dir_entry,
	int recurse,
	bool get_locked,
	char *** file_list,
	char ***mirror,
	char ***touch,
	char ***deleted,
	const char* filespecBasePath,
	const PCLFileSpec* filespec)
{
	PERFINFO_AUTO_START_FUNC();
	addToFileListInternal(client, dir_entry, recurse, get_locked, file_list, mirror, touch, deleted, filespecBasePath, filespec);
	PERFINFO_AUTO_STOP_FUNC();
}

static void makeFullPath(char * path, int path_size)
{
	// TODO: this should probably be integrated with machinePath_s()
	if(!isFullPath(path))
	{
		char *src = path;
		while(src[0] == '.' && src[1] == '/')
			src += 2;
		machinePath_s(path,path_size,src);
	}

	forwardSlashes(path);
	if(path[strlen(path) - 1] == '/')
		path[strlen(path) - 1] = '\0';
}

static void pushGetFileList(PCL_Client *client, char ***fnames, char ***mirror, char ***touch,
							PCL_GetFileListCallback callback, void *userData)
// takes ownership of fnames, mirror, and touch
{
	int i;

	StateStruct *state = calloc(1, sizeof(StateStruct));
	state->general_state = STATE_XFERRING;
	state->call_state = STATE_GET_FILE_LIST;
	state->func_state = STATE_GET_FILE_LIST;
	state->get_list.callback = callback;
	state->get_list.userData = userData;
	state->get_list.fnames = *fnames;
	state->get_list.mirror = *mirror;
	state->get_list.touch = *touch;
	state->get_list.i = 0;
	pclStateAdd(client, state);

	client->xferrer->progress_recieved = 0;
	client->xferrer->progress_total = 0;
	client->xferrer->completed_files = 0;
	client->xferrer->total_files = 0;
	client->bytes_read_at_start = pclRealBytesReceived(client);
	client->http_bytes_read_at_start = pclHttpBytesReceived(client);
	client->time_started = timerCpuSeconds();
	for(i = 0; i < eaSize(fnames); i++)
	{
		DirEntry *found_dir = patchFindPath(client->db, (*fnames)[i], 0);
		if(devassert(found_dir && eaSize(&found_dir->versions) > 0))
		{
			client->xferrer->progress_total += found_dir->versions[0]->size;
			client->xferrer->total_files++;
		}
	}

	*fnames = NULL;
	*mirror = NULL;
	*touch = NULL;

	loadstart_printf("Transferring files...");

}

static void deleteMatches(PCL_Client *client, char *dbname, int existing_version)
{
	if(existing_version)
	{
		char file_path[MAX_PATH];
		if(client->root_folder)
			sprintf(file_path, "%s/%s", client->root_folder, dbname);
		else
			strcpy(file_path, dbname);
		machinePath(file_path,file_path);
		deleteAndClean(file_path);
	}
}

static void checkDeletedResponse(PCL_Client *client, Packet *pak, int cmd)
{
	StateStruct *state = client->state[0];

	if(cmd != PATCHSERVER_CHECK_DELETED || state->func_state != STATE_CHECK_DELETED)
	{
		REPORT_ERROR_STRING(client, PCL_UNEXPECTED_RESPONSE, "Expecting a response about a check on deleted files.");
	}
	else
	{
		int success = pktGetBits(pak, 8);
		if(!success)
		{
			char *err_msg = pktGetStringTemp(pak);
			pcllog(client, PCLLOG_INFO, "delete check failed: %s", err_msg);
			REPORT_ERROR_STRING(client, PCL_FILE_NOT_FOUND, err_msg);
		}
		else if(!client->db)
		{
			REPORT_ERROR(client, PCL_MANIFEST_NOT_LOADED);
		}
		else if(!client->filespec)
		{
			REPORT_ERROR(client, PCL_FILESPEC_NOT_LOADED);
		}
		else
		{
			int i;
			for(i = 0; i < eaSize(&state->check_deleted.fnames); i++)
			{
				int existing_version = pktGetBits(pak, 8);
				state->check_deleted.callback(client, state->check_deleted.fnames[i], existing_version);
			}
		}
		pclStateRemoveHead(client);
	}
}

static void checkDeletedSendPacket(PCL_Client *client)
{
	StateStruct * state = client->state[0];
	char file_path[MAX_PATH];
	void *data;
	U32 size, i, count = eaSize(&state->check_deleted.fnames);

	Packet *pak_out = pktCreate(client->link, PATCHCLIENT_CHECK_DELETED);
	pktSendBits(pak_out, 32, count);
	for(i = 0; i < count; i++)
	{
		if(client->root_folder)
			sprintf(file_path, "%s/%s", client->root_folder, state->check_deleted.fnames[i]);
		else
			strcpy(file_path, state->check_deleted.fnames[i]);
		machinePath(file_path,file_path);

		pktSendString(pak_out, state->check_deleted.fnames[i]);
		pktSendBits(pak_out, 32, fileLastChangedAbsolute(file_path));
		data = fileAlloc(file_path, &size);
		pktSendBits(pak_out, 32, size);
		pktSendBits(pak_out, 32, patchChecksum(data, size));
		SAFE_FREE(data);
	}
	pktSend(&pak_out);

	LOGPACKET(client, "PATCHCLIENT_CHECK_DELETED %d", count);

	state->func_state = STATE_CHECK_DELETED;
}

static void pushCheckDeletedList(PCL_Client *client, char ***fnames, PCL_CheckDeletedCallback callback) // takes ownership of fnames
{
	if(eaSize(fnames))
	{
		StateStruct *state = calloc(1, sizeof(StateStruct));
		state->general_state = STATE_WAITING;
		state->call_state = STATE_CHECK_DELETED;
		state->func_state = STATE_NONE;
		state->check_deleted.callback = callback;
		state->check_deleted.fnames = *fnames;
		pclStateAdd(client, state);

		resetView(client);

		if(client->view_status == VIEW_SET)
		{
			checkDeletedSendPacket(client);
		}

		*fnames = NULL;
	}
	else
		eaDestroy(fnames);
}

PCL_ErrorCode pclGetFileList(	PCL_Client* client,
								const char*const* fileNames,
								int* recurse,
								bool get_locked,
								int count,
								PCL_GetFileListCallback callback,
								void* userData,
								const PCLFileSpec* filespec)
{
	int i, len;
	char **file_list = NULL, **mirror = NULL, **touch = NULL, **deleted = NULL;
	char root_folder[MAX_PATH];
	char path[MAX_PATH], path_to_get[MAX_PATH];

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);
	if(!client->db)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_MANIFEST_NOT_LOADED);
	if(!client->filespec)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_FILESPEC_NOT_LOADED);

	pclSendReport(client, PCLSMS_UPDATE, "Loading hoggs");

	pclLoadHoggs(client);

	if(client->root_folder)
	{
		forwardSlashes(client->root_folder);
		len = (int)strlen(client->root_folder);
		if(client->root_folder[len-1] == '/')
			client->root_folder[len-1] = '\0';
	}
	else
	{
		client->root_folder = strdup(client->project); // client->db->root.name);
	}

	pclSendReport(client, PCLSMS_UPDATE, "Deleting files");
	strcpy(root_folder, client->root_folder);
	makeFullPath(SAFESTR(root_folder));
	len = (int)strlen(root_folder);
	root_folder[len] = '/';
	root_folder[len+1] = '\0';
	if (count > 2 && !(client->file_flags & PCL_NO_DELETEME_CLEANUP))
	{
		// Scan the whole thing now, unless just one element (or one + its .ms), then just scan that one element below
		pclMSpf("Deleting .deleteme files (%s)...\n",
			root_folder);
		fileScanAllDataDirs(root_folder, deleteDeleteMe, client);
		pclMSpf("Done deleting .deleteme files...\n");
	}

	// If requested, delete files which don't belong.
	deleteRogueFiles(client);

	pclSendReport(client, PCLSMS_UPDATE, "Scanning files");
	pclMSpf("Adding files to some list...\n");
	for(i = 0; i < count; i++)
	{
		DirEntry *found_dir;
		strcpy(path, fileNames[i]);
		makeFullPath(SAFESTR(path));

		if( strnicmp(root_folder, path, len) == 0 &&
			path[len] == '\0')
		{
			strcpy(path_to_get, ""); // getting the root folder
		}
		else
		{
			if(!strStartsWith(path, root_folder))
			{
				pcllog(client, PCLLOG_INFO, "Get file %s is not in root folder %s", path, root_folder);
				eaDestroy(&file_list);
				eaDestroy(&mirror);
				eaDestroy(&touch);
				PERFINFO_AUTO_STOP_FUNC();
				return PCL_NOT_IN_ROOT_FOLDER;
			}
			sprintf(path_to_get, "%s", path + len + 1);
		}

		if (count <= 2 && !(client->file_flags & PCL_NO_DELETEME_CLEANUP))
		{
			pclMSpf("Deleting .deleteme files (%s)...\n",
				path);
			loadstart_printf("Cleaning up .deleteme files..."); s_deleteme_count = 0;
			fileScanAllDataDirs(path, deleteDeleteMe, client);
			loadend_printf("done (%d).", s_deleteme_count);
			pclMSpf("Done deleting .deleteme files...\n");
		}

		found_dir = patchFindPath(client->db, path_to_get, 0);
		if(found_dir)
		{
			loadstart_printf("Scanning for updates...");
			addToFileList(	client,
							found_dir,
							recurse[i],
							get_locked,
							&file_list,
							&mirror,
							&touch,
							&deleted,
							found_dir->path,
							filespec);
			loadend_printf("done (%d updates).", eaSize(&file_list) + eaSize(&touch) + eaSize(&deleted));
		}
		else
		{
			pcllog(client, PCLLOG_WARNING, "could not find %s in database", path_to_get);
		}
	}
	pclMSpf("Done adding files to some list...\n");

	pclMSpf("Pushing get file list...\n");
	pushGetFileList(client, &file_list, &mirror, &touch, callback, userData);
	pclMSpf("Done pushing get file list...\n");

	pclMSpf("Pushing check deleted list...\n");
	pushCheckDeletedList(client, &deleted, deleteMatches);
	pclMSpf("Done pushing check deleted list...\n");

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

PCL_ErrorCode pclForceGetFiles(	PCL_Client *client,
								const char*const*dbnames,
								int count,
								PCL_GetFileListCallback callback,
								void *userData)
{
	int i;
	char **file_list = NULL, **mirror = NULL, **touch = NULL, **deleted = NULL;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);
	if(!client->db)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_MANIFEST_NOT_LOADED);
	if(!client->filespec)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_FILESPEC_NOT_LOADED);

	pclLoadHoggs(client);

	for(i = 0; i < count; i++)
	{
		DirEntry *dir = patchFindPath(client->db, dbnames[i], 0);
		if(dir)
			addToFileList(client, dir, -1, true, &file_list, &mirror, &touch, &deleted, NULL, NULL);
		else
			pcllog(client, PCLLOG_WARNING, "could not find %s in database", dbnames[i]);
	}

	pushGetFileList(client, &file_list, &mirror, &touch, callback, userData);
	pushCheckDeletedList(client, &deleted, deleteMatches);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

PCL_ErrorCode pclGetFile(PCL_Client * client, char * fileName, int priority, PCL_GetFileCallback callback,
					void * userData)
{
	StateStruct * state;
	PCL_ErrorCode error;

	PERFINFO_AUTO_START_FUNC();

	state = client->state[0];

	CHECK_ERROR_PI(client);
	if(!client->filespec)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_FILESPEC_NOT_LOADED);

	if(state->general_state != STATE_XFERRING && state->general_state != STATE_IDLE)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_WAITING;
	}

	pclLoadHoggs(client);

	error = getFileInternal(client, fileName, priority, client->root_folder, true, callback, userData);

	PERFINFO_AUTO_STOP_FUNC();

	return error;
}

static int getFileInternal(PCL_Client *client, const char *fileName, int priority, const char *rootFolder, bool add_progress,
						   PCL_GetFileCallback callback, void *userData)
{
	DirEntry * dir_entry;
	PatchXfer * xfer;
	HogFile * hogg, * write_hogg;
	int hogg_index, folder_index;
	char fileNameToWrite[MAX_PATH];
	char * stripped;

	PERFINFO_AUTO_START_FUNC();

	if(	strchr(fileName, '/') == NULL &&
		(	!stricmp(fileName, client->manifest_fname) ||
			!stricmp(fileName, client->filespec_fname) ||
			!stricmp(fileName, client->full_manifest_fname))
		)
	{
		sprintf(fileNameToWrite, "%s/%s", rootFolder, fileName);
		// FIXME: The following should use client->file_flags, but some of the flags cause various strangenesses that are inappropriate for manifests.
		xfer = xferStart(client->xferrer, fileName, fileNameToWrite, priority, 0, true, false, 0, callback, NULL, NULL, NULL, userData);
	}
	else
	{
		PCL_FileFlags file_flags;
		FileVersion *ver;

		if(!client->db)
		{
			PERFINFO_AUTO_STOP_FUNC();
			RETURN_ERROR(client, PCL_MANIFEST_NOT_LOADED);
		}
		else if(!client->filespec)
		{
			PERFINFO_AUTO_STOP_FUNC();
			RETURN_ERROR(client, PCL_FILESPEC_NOT_LOADED);
		}

		dir_entry = patchFindPath(client->db, fileName, 0);
		if(!dir_entry)
		{
			PERFINFO_AUTO_STOP_FUNC();
			RETURN_ERROR(client, PCL_FILE_NOT_FOUND);
		}
		ver = dir_entry->versions[0];

		// If the server indicates that this file has been deleted, skip it.
		// Deletion is handled by a separate mechanism.
		if (ver->flags & FILEVERSION_DELETED)
		{
			PERFINFO_AUTO_STOP_FUNC();
			return PCL_SUCCESS;
		}

		hogg_index = fileSpecGetHoggIndexForFile(client->filespec, fileName);
		if(hogg_index < 0)
		{
			PERFINFO_AUTO_STOP_FUNC();
			RETURN_ERROR(client, PCL_NOT_IN_FILESPEC);
		}

		file_flags = client->file_flags;
		if(fileSpecIsBin(client->filespec, fileName))
			file_flags &= ~PCL_FILEFLAGS_BINMASK;

		folder_index = fileSpectGetFolderIndexForFileVersion(client->filespec, ver, hogg_index, true, NULL);
		hogg = fileSpecGetHoggHandleEx(client->filespec, MAX(folder_index, 0), hogg_index);
		write_hogg = client->write_index ? fileSpecGetHoggHandleEx(client->filespec, client->write_index, hogg_index) : fileSpecGetHoggHandle(client->filespec, hogg_index);
		stripped = fileSpecGetStripPath(client->filespec, hogg_index);

		strcpy(fileNameToWrite, fileName);
		stripPath(fileNameToWrite, stripped);

		if(!hogg)
		{
			char temp[MAX_PATH];
			strcpy(temp, fileNameToWrite);
			sprintf(fileNameToWrite, "%s/%s", rootFolder, temp);
		}

		xfer = xferStart(client->xferrer, fileName, fileNameToWrite, priority, dir_entry->versions[0]->modified, false, false, file_flags, callback, hogg, write_hogg, ver, userData);

		// Add this file to progress.
		if (xfer && add_progress)
		{
			client->xferrer->progress_total += ver->size;
			client->xferrer->total_files++;
		}
	}

	if(xfer)
	{
		StateStruct * state = client->state[0];

		if(state->general_state == STATE_IDLE)
		{
			state = calloc(1, sizeof(StateStruct));
			state->general_state = STATE_XFERRING;
			state->call_state = STATE_GET_FILE;
			state->func_state = STATE_GET_FILE;
			pclStateAdd(client, state);
		}

		resetView(client);

		PERFINFO_AUTO_STOP_FUNC();
		return PCL_SUCCESS;
	}

	PERFINFO_AUTO_STOP_FUNC();
	return PCL_XFERS_FULL;
}

PCL_ErrorCode pclCompareProject(PCL_Client * client, const char * project, bool * match)
{
	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	if(!match || !project)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_NULL_PARAMETER);

	*match = client->project && !stricmp(project, client->project);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

PCL_ErrorCode pclCompareView(PCL_Client *client, const char *project, int branch, const char *sandbox, int rev, bool *compare_view)
{
	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	if(!compare_view || !project)
	{
		RETURN_ERROR_PERFINFO_STOP(client, PCL_NULL_PARAMETER);
	}

	// Check for any differences.
	*compare_view = false;
	if(!client->project)
	{
		PERFINFO_AUTO_STOP();
		return PCL_SUCCESS;
	}
	if(branch != client->branch || rev != client->rev)
	{
		PERFINFO_AUTO_STOP();
		return PCL_SUCCESS;
	}
	if(stricmp(project, client->project))
	{
		PERFINFO_AUTO_STOP();
		return PCL_SUCCESS;
	}
	if(stricmp(NULL_TO_EMPTY(sandbox), NULL_TO_EMPTY(client->sandbox)))
	{
		PERFINFO_AUTO_STOP();
		return PCL_SUCCESS;
	}

	// No differences; it's the same view.
	*compare_view = true;
	PERFINFO_AUTO_STOP();
	return PCL_SUCCESS;
}

PCL_ErrorCode pclGetView(PCL_Client *client, char *project_buf, int project_buf_size, int *rev, int *branch, char *sandbox_buf, int sandbox_buf_size)
{
	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);

	if(project_buf)
		strcpy_s(project_buf, project_buf_size, NULL_TO_EMPTY(client->project));
	if(rev)
		*rev = client->rev;
	if(branch)
		*branch = client->branch;
	if(sandbox_buf)
		strcpy_s(sandbox_buf, sandbox_buf_size, NULL_TO_EMPTY(client->sandbox));

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

static void pclLoadJustFileSpec(PCL_Client* client)
{
	char path[MAX_PATH];
	char mirrorpath[MAX_PATH];
	TriviaMutex mutex;

	sprintf(path, "%s/%s/%s", client->root_folder, PATCH_DIR, client->filespec_fname);

	// Destroy old filespec.
	mutex = triviaAcquireDumbMutex(path);
	if(client->filespec)
		fileSpecDestroy(&client->filespec);

	// Load new filespec.
	if(client->file_flags & (PCL_IN_MEMORY|PCL_METADATA_IN_MEMORY) && client->in_memory_data.filespec)
		client->filespec = fileSpecLoadFromData(client->in_memory_data.filespec->in_memory_data,
			client->in_memory_data.filespec->size, 0, NULL, NULL, fileSpecPclLog, client);
	else if(fileExists(path))
		client->filespec = fileSpecLoad(path, client->extra_filespec_fname, 0, NULL, NULL, fileSpecPclLog, client);

	// Set single app mode appropriately.
	if (client->filespec)
		fileSpecSetSingleAppMode(client->filespec, client->single_app_mode);

	// Reload MirrorFilespec.
	sprintf(mirrorpath, "%s/%s/%s", client->root_folder, PATCH_DIR, "MirrorFilespec.txt");
	if (client->mirrorFilespec)
		simpleFileSpecDestroy(client->mirrorFilespec);
	if(fileExists(mirrorpath))
		client->mirrorFilespec = simpleFileSpecLoad(mirrorpath, NULL);
	triviaReleaseDumbMutex(mutex);
}

// Return true if this DirEntry should be removed from the manifest.
static bool pclShouldTrim(const char *path, FileVersion *ver, void *userdata)
{
	PCL_Client *client = userdata;
	return (ver->flags & FILEVERSION_DELETED)							// Deleted
		|| !fileSpecIsNotRequired(client->filespec, path);				// File doesn't have NotRequired flag
}

static void pclLoadManifestAndFileSpec(PCL_Client* client)
{
	char path[MAX_PATH];
	U32 flags = PATCHDB_DEFAULT;
	patchDbShouldTrimCallback trim_callback = NULL;

	// Load filespec.
	pclLoadJustFileSpec(client);

	// Print status.
	sprintf(path, "%s/%s/%s", client->root_folder, PATCH_DIR, client->manifest_fname);
	printf("Loading manifest: %s...", path);

	// Destroy old manifest.
	if(client->db)
		patchDbDestroy(&client->db);

	// Use pooled paths, if requested.
	if (client->file_flags & PCL_USE_POOLED_PATHS)
		flags |= PATCHDB_POOLED_PATHS;

	// Trim unneeded stuff from manifest, if requested.
	if(client->file_flags & PCL_TRIM_PATCHDB_FOR_STREAMING)
	{
		flags |= PATCHDB_OMIT_AUTHORS;
		trim_callback = pclShouldTrim;
	}

	// Load new manifest.
	if(client->file_flags & PCL_FORCE_EMPTY_DB)
		client->db = patchLoadDbClientFromData("", 0, flags, trim_callback, client);
	else if(client->file_flags & (PCL_IN_MEMORY|PCL_METADATA_IN_MEMORY))
	{
		if(client->in_memory_data.manifest)
			client->db = patchLoadDbClientFromData(client->in_memory_data.manifest->in_memory_data,
				client->in_memory_data.manifest->size, flags, trim_callback, client);
		else
			REPORT_ERROR_STRING(client, PCL_INTERNAL_LOGIC_BUG, "In-memory manifest not transferred");
	}
	else
		client->db = patchLoadDbClient(path, flags,	trim_callback, client);
	printf("done\n");
	if(!client->db)
	{
		REPORT_ERROR_STRING(client, PCL_MANIFEST_NOT_LOADED, "Could not load manifest");
	}
}

// Parse HTTP info, and apply it to the PCL_Client.
static void pclParseHttpInfo(PCL_Client * client, char *http_info)
{
	char server[1024];
	U16 port;
	char prefix[1024];
	bool success;

	// If HTTP patching has been disabled, return.
	if (g_no_http)
		return;

	// If a server is already set, return.
	if (client->xferrer->http_server)
		return;

	// Parse information.
	success = patchParseHttpInfo(http_info, SAFESTR(server), &port, SAFESTR(prefix));
	if (!success)
		return;

	// Set information.
	pclSetHttp(client, server, port, prefix);
}

static void setViewResponse(PCL_Client * client, Packet * pak, int cmd)
{
	int status;
	StateStruct * state = client->state[0];

	PERFINFO_AUTO_START_FUNC();

	switch(state->func_state)
	{
	case STATE_VIEW_RESPONSE:
		SAFE_FREE(client->sandbox);
		if(cmd != PATCHSERVER_PROJECT_VIEW_STATUS)
		{
			SAFE_FREE(state->set_view.name);
			REPORT_ERROR_STRING(client, PCL_UNEXPECTED_RESPONSE, "Expecting response for project view.");
		}
		else
		{
			status = pktGetBits(pak, 8);

			if(status)
			{
				U32 view_time, incr_time; // for trivia only
				int incr_rev;
				int core_branch_mapping;
				bool bGotCoreBranchMapping=false;
				char branch_name[MAX_PATH];
				bool bGotBranchName=false;

				client->branch = pktGetBits(pak, 32);
				view_time = pktGetBits(pak, 32);
				client->sandbox = pktMallocString(pak);
				incr_time = pktGetBits(pak, 32);
				client->rev = pktGetBits(pak, 32);
				incr_rev = pktGetBits(pak, 32);
				if (!pktEnd(pak)) {
					core_branch_mapping = pktGetBits(pak, 32);
					bGotCoreBranchMapping = true;
				}
				if (!pktEnd(pak)) {
					pktGetString(pak, SAFESTR(branch_name));
					bGotBranchName = true;
				}

				// Check for HTTP patching information.
				if (!pktEnd(pak)) {
					char *http_info = pktGetStringTemp(pak);
					pclParseHttpInfo(client, http_info);
				}

				// Set the view status.
				client->view_status = VIEW_SET;
				client->cached_view_name = state->set_view.name ? strdup(state->set_view.name) : NULL;

				client->xferrer->progress_recieved = 0;
				client->xferrer->progress_total = 0;
				client->bytes_read_at_start = pclRealBytesReceived(client);
				client->http_bytes_read_at_start = pclHttpBytesReceived(client);
				client->time_started = timerCpuSeconds();

				if(state->set_view.saveTrivia) // otherwise don't pollute the filesystem
				{
					TriviaMutex mutex;
					char trivia_path[MAX_PATH], trivia_temp[MAX_PATH];
					TriviaList *list = NULL;

					sprintf(trivia_path, "%s/%s/%s", client->root_folder, PATCH_DIR, TRIVIAFILE_PATCH);
					machinePath(trivia_path, trivia_path);

					mutex = triviaAcquireDumbMutex(trivia_path);
					list = triviaListCreateFromFile(trivia_path);
					if(!list)
						list = triviaListCreate();

					triviaListPrintf(list, "PatchProject", "%s", client->project);
					triviaListPrintf(list, "PatchServer", "%s", client->original_server);
					triviaListPrintf(list, "PatchRedirectServer", "%s", client->server);
					triviaListPrintf(list, "PatchPort", "%d", client->original_port);
					if (client->xferrer->http_server)
						triviaListPrintf(list, "HttpServer", "%s", client->xferrer->http_server);
					if (client->xferrer->http_port)
						triviaListPrintf(list, "HttpPort", "%d", client->xferrer->http_port);
					if (client->xferrer->path_prefix)
						triviaListPrintf(list, "HttpPrefix", "%s", client->xferrer->path_prefix);
					if(state->set_view.name)
						triviaListPrintf(list, "PatchName", "%s", state->set_view.name);
					triviaListPrintf(list, "PatchBranch", "%d", client->branch);
					triviaListPrintf(list, "PatchSandbox", "%s", client->sandbox);
					triviaListPrintf(list, "PatchRevision", "%d", client->rev);
					triviaListPrintf(list, "PatchTime", "%d", view_time);
					triviaListPrintf(list, "PatchTimeReadable", "%s", timeMakeLocalDateStringFromSecondsSince2000(trivia_temp, patchFileTimeToSS2000(view_time)));
					if(incr_time)
					{
						triviaListPrintf(list, "PatchIncrementalRevision", "%d", incr_rev);
						triviaListPrintf(list, "PatchIncrementalFrom", "%d", incr_time);
						triviaListPrintf(list, "PatchIncrementalFromReadable", "%s", timeMakeLocalDateStringFromSecondsSince2000(trivia_temp, patchFileTimeToSS2000(incr_time)));
					}

					STR_COMBINE_BEGIN(trivia_temp);
					STR_COMBINE_CAT("-sync -project ");
					STR_COMBINE_CAT(client->project);
// patchclients should be connecting to ORGANIZATION_DOMAIN unless they have special needs
 					STR_COMBINE_CAT(" -server ");
 					STR_COMBINE_CAT(client->original_server);
 					STR_COMBINE_CAT(" -port ");
 					STR_COMBINE_CAT_D(client->original_port);
					if(state->set_view.name)
					{
						STR_COMBINE_CAT(" -name ");
						STR_COMBINE_CAT(state->set_view.name);
					}
					else
					{
						STR_COMBINE_CAT(" -time ");
						STR_COMBINE_CAT_D(view_time);
						if(client->branch)
						{
							STR_COMBINE_CAT(" -branch ");
							STR_COMBINE_CAT_D(client->branch);
						}
						if(client->sandbox && client->sandbox[0])
						{
							STR_COMBINE_CAT(" -sandbox ");
							STR_COMBINE_CAT(client->sandbox);
						}
					}
					STR_COMBINE_END(trivia_temp);
					triviaListPrintf(list, "PatchCmdLine", "%s", trivia_temp);
					if (bGotCoreBranchMapping)
						triviaListPrintf(list, "CoreBranchMapping", "%d", core_branch_mapping);
					if (bGotBranchName)
						triviaListPrintf(list, "PatchBranchName", "%s", branch_name);
					triviaListPrintf(list, "PatchExecutable", "%s", getExecutableName());



					// Check to make sure we're not writing a trivia file with a core mapping into a non-core folder
					if(triviaListGetValue(list, "PatchProject") && strstri(trivia_path, "Core") && !strstri(triviaListGetValue(list, "PatchProject"), "Core"))
						Errorf("Writing non-core project (%s) data into a folder named Core (%s)", triviaListGetValue(list, "PatchProject"), trivia_path);
					makeDirectoriesForFile(trivia_path);
					triviaListWritePatchTriviaToFile(list, trivia_path);
					triviaReleaseDumbMutex(mutex);
					triviaListDestroy(&list);

					// Remove deprecated current_view.txt files
					sprintf(trivia_path, "%s/%s/%s", client->root_folder, PATCH_DIR, "current_view.txt");
					machinePath(trivia_path, trivia_path);
					fileForceRemove(trivia_path);
				}
			}

			SAFE_FREE(state->set_view.name);

			if(	status &&
				state->set_view.getManifest)
			{
				// Update manifest and filespec.
				state->set_view.manifest_done = -1;
				state->set_view.filespec_done = -1;
				state->func_state = STATE_GET_MANIFEST;
				pclSendReport(client, PCLSMS_UPDATE, "Getting manifest");
				setViewProcess(client);
			}
			else
			{
				PCL_SetViewCallback callback = state->set_view.callback;
				void * userData = state->set_view.userData;

				// Not downloading a new manifest, but we still need the filespecs loaded
				if (status)
				{

					// As a special optimization, try to load the filespec without updating it first.  This exposes us to some risk
					// if the filespec is mangled in a way that it still seems to load.  However, since ordinary operations which update
					// the manifest will fix it, this is probably a manageable risk.
					pclLoadJustFileSpec(client);

					// If the filespec didn't seem to load, we need to request it, or we risk proceeding with a broken or missing filespec.
					if (!client->filespec)
					{
						state->set_view.manifest_done = 1;
						state->set_view.filespec_done = -1;
						state->func_state = STATE_GET_MANIFEST;
						setViewProcess(client);
					}

					// If everything seems OK, finish up.
					if (client->filespec)
					{
						pclStateRemoveHead(client);
						if(callback)
							callback(client, client->error, client->error_details, userData);
					}
				}

				if(!status)
				{
					pclStateRemoveHead(client);
					if(pktCheckNullTerm(pak))
					{
						char *s = pktGetStringTemp(pak);
						REPORT_ERROR_STRING(client, PCL_INVALID_VIEW, s);
					}
					else
					{
						REPORT_ERROR_STRING(client, PCL_INVALID_VIEW, "No response for invalid view.");
					}
					if(callback)
						callback(client, client->error, client->error_details, userData);
				}
			}
		}
		break;

	case STATE_GET_MANIFEST:
		handleXferMsg(pak, cmd, client->xferrer);
		break;
	default:
		REPORT_ERROR_STRING(client, PCL_INTERNAL_LOGIC_BUG, "Set view got into an unknown state.");
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static void setViewProcess(PCL_Client * client)
{
	StateStruct * state = client->state[0];

	PERFINFO_AUTO_START_FUNC();

	pclMSpf2("setViewProcess\n");

	if(state->func_state == STATE_GET_MANIFEST)
	{
		char rootFolder[MAX_PATH];
		char filepath[MAX_PATH];

		if(client->root_folder)
			sprintf(rootFolder, "%s/%s", client->root_folder, PATCH_DIR);
		else
			strcpy(rootFolder, ".");

		xferProcess(client->xferrer);

		// Request manifest, if not yet requested.
		if(state->set_view.manifest_done < 0)
		{
			if (client->file_flags & PCL_FORCE_EMPTY_DB)
				state->set_view.manifest_done = 1;
			else if (client->file_flags & PCL_USE_SAVED_METADATA)
			{
				sprintf(filepath, "%s/%s", rootFolder, client->manifest_fname);
				if (fileExists(filepath))
					state->set_view.manifest_done = 1;
			}
			if(state->set_view.manifest_done < 0)
			{
				int ret = getFileInternal(client, client->manifest_fname, 1, rootFolder, false, getFileCallback, &(state->set_view.manifest_done));
				if(ret == PCL_SUCCESS)
					state->set_view.manifest_done = 0;
			}
		}

		// Request filespec, if not yet requested.
		if(state->set_view.filespec_done < 0)
		{
			if (client->file_flags & PCL_USE_SAVED_METADATA)
			{
				sprintf(filepath, "%s/%s", rootFolder, client->filespec_fname);
				if (fileExists(filepath))
					state->set_view.filespec_done = 1;
			}
			if(state->set_view.filespec_done < 0)
			{
				int ret = getFileInternal(client, client->filespec_fname, 1, rootFolder, false, getFileCallback, &(state->set_view.filespec_done));
				if(ret == PCL_SUCCESS)
					state->set_view.filespec_done = 0;
			}
		}

		// Check if we've received both files.
		if(state->set_view.manifest_done == 1 && state->set_view.filespec_done == 1)
		{
			PCL_SetViewCallback callback = state->set_view.callback;
			void * userData = state->set_view.userData;
			
			// Load manifest.
			if (state->set_view.getManifest)
			{
				pclSendReport(client, PCLSMS_UPDATE, "Loading manifest");
				pclLoadManifestAndFileSpec(client);
			}
			else
				pclLoadJustFileSpec(client);

			// Check for filespec being loaded.
			// The manifest loading is checked for elsewhere.
			if (!client->error && !client->filespec)
				REPORT_ERROR_STRING(client, PCL_FILESPEC_NOT_LOADED, "Unable to load filespec");
			
			pclStateRemoveHead(client);
			if(callback)
				callback(client, client->error, client->error_details, userData);
		}
	}

	pclMSpf2("done setViewProcess\n");

	PERFINFO_AUTO_STOP_FUNC();
}

static PCL_ErrorCode addFileListToXferrer(PCL_Client * client)
{
	int i;
	char ** fnames;
	PCL_ErrorCode error_code;

	PERFINFO_AUTO_START_FUNC();

	fnames = client->state[0]->get_list.fnames;

	// darn code analysis thinks a NULL fnames can have an eaSize greater than i
	if(client->state[0]->get_list.i < 0)
	{
		PERFINFO_AUTO_STOP_FUNC();
		RETURN_ERROR(client, PCL_INTERNAL_LOGIC_BUG);
	}

	if(xferrerFull(client->xferrer, NULL))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_XFERS_FULL;
	}

	for(i = client->state[0]->get_list.i; i < eaSize(&fnames); i++)
	{
		error_code = getFileInternal(client, fnames[i], 1, client->root_folder, false, NULL, NULL);
		if(error_code == PCL_XFERS_FULL)
		{
			client->state[0]->get_list.i = i;
			PERFINFO_AUTO_STOP_FUNC();
			return error_code;
		}
		else if(error_code != PCL_SUCCESS)
		{
			PERFINFO_AUTO_STOP_FUNC();
			RETURN_ERROR(client, error_code);
		}
	}

	client->state[0]->get_list.i = i;

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

// Fill in stats struct.
static void fillPatchProcessStats(PatchProcessStats *stats, PCL_Client *client, F32 elapsed)
{
	stats->received = client->xferrer->progress_recieved;
	stats->total = client->xferrer->progress_total;
	stats->received_files = client->xferrer->completed_files;
	stats->total_files = client->xferrer->total_files;
	stats->xfers = eaSize(&client->xferrer->xfers);
	stats->buffered = client->xferrer->max_net_bytes - client->xferrer->net_bytes_free;
	stats->actual_transferred = pclRealBytesReceived(client) - client->bytes_read_at_start;
	stats->overlay_bytes = client->xferrer->overlay_bytes;
	stats->written_bytes = client->xferrer->written_bytes;
	stats->http_actual_transferred = pclHttpBytesReceived(client) - client->http_bytes_read_at_start;
	stats->http_errors = client->xferrer->http_fails;
	stats->http_header_bytes = client->xferrer->http_header_bytes;
	stats->http_mime_bytes = client->xferrer->http_mime_bytes;
	stats->http_body_bytes = client->xferrer->http_body_bytes;
	stats->http_extra_bytes = client->xferrer->http_extra_bytes;
	stats->seconds = timerCpuSeconds() - client->time_started;
	stats->loops = client->process_loops;
	stats->state_info = NULL;
	xferrerGetStateInfo(client->xferrer, &stats->state_info);
	stats->elapsed = elapsed ? elapsed : timerSeconds64(timerCpuTicks64() - client->callback_timer);
	stats->error = client->error;
}

static void doProcessCallback(PCL_Client *client, F32 elapsed)
{
	PatchProcessStats stats = {0};
	bool success;

	PERFINFO_AUTO_START_FUNC();

	fillPatchProcessStats(&stats, client, elapsed);

	PERFINFO_AUTO_START("processCallback", 1);
	success = client->processCallback(&stats, client->processUserData);
	if(success)
	{
		client->callback_timer = timerCpuTicks64();
	}
	PERFINFO_AUTO_STOP_CHECKED("processCallback");

	pclSendReportPercent(client, "Downloading", stats.received, stats.total);

	eaDestroyEx(&stats.state_info, NULL);

	PERFINFO_AUTO_STOP_FUNC();
}

static void pclCheckForFailedKeepAlive(PCL_Client* client)
{
	if(	client->link &&
		client->keepAliveTimer
		&& client->state[0]->general_state != STATE_CONNECTING
		&& linkConnected(client->link))
	{
		const U32 pingAckCount = linkGetPingAckReceiveCount(client->link);
		
		if(pingAckCount != client->keepAlivePingAckRecvCount)
		{
			client->keepAlivePingAckRecvCount = pingAckCount;
			client->keepAliveTimer = timerCpuTicks64();
		}
		else if(timerSeconds64(timerCpuTicks64() - client->keepAliveTimer) > client->keepAliveTimeout)
		{
			printfColor(COLOR_BRIGHT|COLOR_RED,
						"Parent server failed to ack pings for %1.2f seconds, reconnecting...\n",
						timerSeconds64(timerCpuTicks64() - client->keepAliveTimer));
						
			linkRemove_wReason(&client->link, "Parent server not sending ping acks");
			client->disconnected = true;
			client->was_ever_unexpectedly_disconnected = true;
		}
	}
}

// Check to see if autoupdate is making progress.
static void checkAutoupdateStatus(PCL_Client* client)
{
	U32 bytesWaitingToRecv;

	if (!client->link)
		return;

	// Check if we've received any part of the auto-update packet.
	bytesWaitingToRecv = linkCurrentPacketBytesWaitingToRecv(client->link);
	if (!bytesWaitingToRecv)
		return;

	// If progress has been made, update the timeout.
	if (!client->current_pak_bytes_waiting_to_recv || bytesWaitingToRecv < client->current_pak_bytes_waiting_to_recv)
	{
		client->current_pak_bytes_waiting_to_recv = bytesWaitingToRecv;
		client->retry_timer = timerCpuTicks64();
		updateProgressTimer(client);
	}
}

// Set up the state object to be able to handle a reconnect.
// Historical note:
// It's not at all clear to me how reconnection was originally meant to work.  I've examined various older
// versions of the source for this file, and while there's been all of this code all over the place
// to support reconnection as far back as the SVN history goes, it doesn't really look, to me, like it would
// actually work.  The most obvious problem is that nothing ever seems to set the func_state to STATE_NONE,
// which the reconnect handling appears to need to re-send the active command.  A more subtle problem is that
// pclProcess() used to continue to run whatever function was active before the disconnection until things actually
// reconnect, at which point it creates the new CONNECTING state, which really just doesn't make a lot of sense,
// particularly since initial connections start out in CONNECTING.  So, I've reworked this somewhat just to
// try to make it work some sort of way.
static void pclSetReconnectState(PCL_Client * client)
{
	StateStruct * state, * new_state;

	// Note that we may need to reset the view.
	if(client->view_status == VIEW_SET)
		client->view_status = VIEW_NEEDS_RESET;
	if(client->author_status == VIEW_SET)
		client->author_status = VIEW_NEEDS_RESET;

	// Clear the func_state, if we were doing anything.
	// FIXME: See the note above.  I'm not really sure if this is the right thing to do, but it seems to be
	// necessary if reconnecting in the middle of an operation is going to have any hope of working.  It's possible
	// that this should actually set all states, rather than just the top one.
	state = client->state[0];
	if (state->general_state != STATE_IDLE)
		state->func_state =  STATE_NONE;

	// Set up the state.
	// FIXME: It's not clear to me why this is necessary, since since the the functions to restore the func_state
	// typically call resetView() to check view_status, before they attempt to resend the command.
	switch(state->general_state)
	{
	case STATE_IDLE:
		// Do Nothing
		break;
	case STATE_CONNECTING:
		state->call_state = state->func_state = STATE_CONNECTING;
		break;
	case STATE_WAITING:
		switch(state->call_state)
		{
		case STATE_LOCK_FILE:
		case STATE_UNLOCK_FILES:
		case STATE_CHECKIN_FILES:
		case STATE_FORCEIN_DIR:
			new_state = calloc(1, sizeof(StateStruct));
			new_state->general_state = STATE_WAITING;
			new_state->call_state = STATE_SET_AUTHOR;
			new_state->func_state = STATE_NONE;
			pclStateAdd(client, new_state);
			//NO BREAK

		case STATE_NAME_VIEW:
		case STATE_SET_AUTHOR:
		case STATE_FILE_HISTORY:
		case STATE_VERSION_INFO:
		case STATE_NAME_LIST:
		case STATE_BRANCH_INFO:
		case STATE_CHECK_DELETED:
			new_state = calloc(1, sizeof(StateStruct));
			new_state->general_state = STATE_WAITING;
			new_state->call_state = STATE_SET_VIEW;
			new_state->func_state = STATE_NONE;
			pclStateAdd(client, new_state);
			//NO BREAK

		case STATE_SET_VIEW:
		case STATE_NOTIFY_ME:
		case STATE_PROJECT_LIST:
		case STATE_SET_EXPIRATION:
		case STATE_SET_FILE_EXPIRATION:
			xferrerReset(client->xferrer);
			state->func_state = STATE_NONE;
			break;
		}
		break;
	case STATE_XFERRING:
		switch(state->call_state)
		{
		case STATE_GET_FILE:
		case STATE_GET_REQUIRED_FILES:
		case STATE_GET_FILE_LIST:
			new_state = calloc(1, sizeof(StateStruct));
			new_state->general_state = STATE_WAITING;
			new_state->call_state = STATE_SET_VIEW;
			new_state->func_state = STATE_NONE;
			pclStateAdd(client, new_state);
			xferrerReset(client->xferrer);
			break;
		}
		break;
	}

	// Add connecting state, to handle the rest of the connecting process.
	new_state = calloc(1, sizeof(StateStruct));
	new_state->general_state = STATE_CONNECTING;
	new_state->call_state = STATE_CONNECTING;
	new_state->func_state = STATE_CONNECTING;
	pclStateAdd(client, new_state);
}

PCL_ErrorCode pclProcess_dbg(PCL_Client * client, const char* fileName, S32 fileLine)
{
	int ret;
	F32 elapsed;
	StateStruct* state;
	PCL_State general_state;
	static StaticCmdPerf statePerf[PCL_STATE_COUNT*PCL_STATE_COUNT*2];
	int statePerfSlot;

	state = client->state[0];
	general_state = state->general_state;

	// PERFINFO timer with state information
	EnterStaticCmdPerfMutex();
	statePerfSlot = state->general_state + state->call_state * PCL_STATE_COUNT
		+ (state->func_state == STATE_NONE ? PCL_STATE_COUNT * PCL_STATE_COUNT : 0);
	if(statePerfSlot >= 0 && statePerfSlot < ARRAY_SIZE(statePerf)){
		if(!statePerf[statePerfSlot].name){
			char buffer[200];
			sprintf(buffer, "State:G:%s,C:%s,F:%s",
				pclStateGetName(state->general_state),
				pclStateGetName(state->call_state),
				state->func_state == STATE_NONE ? "None" : "Func");
			statePerf[statePerfSlot].name = allocAddString(buffer);
		}
		PERFINFO_AUTO_START_STATIC(statePerf[statePerfSlot].name, &statePerf[statePerfSlot].pi, 1);
	}else{
		PERFINFO_AUTO_START("State:Unknown", 1);
	}
	LeaveStaticCmdPerfMutex();

	// Record last process time
#ifdef DEBUG_PCL_TIMING
	client->last_process = timerCpuTicks64();
#endif
	
	pclCheckForFailedKeepAlive(client);

	if(SAFE_MEMBER(client, disconnected) || !client->link)
	{

		// Reconnect, if requested.
		if (client->file_flags & PCL_RECONNECT)
		{
			// Set up state to handle reconnection seamlessly.
			pclSetReconnectState(client);

			// Clear any PCL_LOST_CONNECTION error.
			if (client->error == PCL_LOST_CONNECTION)
				client->error = PCL_SUCCESS;

			// Reset server.
			free(client->server);
			client->server = strdup(client->original_server);
			client->port = client->original_port;

			// Reconnect.
			s_connectToServer(client, "reconnecting", NULL, 0);

			PERFINFO_AUTO_STOP();
			return PCL_WAITING;
		}

		PERFINFO_AUTO_STOP();
		return PCL_LOST_CONNECTION;
	}

	// If reporting patch status, report error.
	if (client->error && client->status_reporting)
	{
		char error_string[256];
		char *message = NULL;
		pclGetErrorString(client->error, SAFESTR(error_string));
		estrStackCreate(&message);
		estrPrintf(&message, "%s%s%s",
			error_string,
			client->error_details ? ": " : "",
			NULL_TO_EMPTY(client->error_details));
		pclSendReport(client, PCLSMS_FAILED, message);
		estrDestroy(&message);
	}
	
	CHECK_ERROR_PI(client);
	
	pclMSpf2("pclStateBegin(%d): general=%s, call=%s, func=%s (%s:%d)\n",
			eaSize(&client->state),
			pclStateGetName(state->general_state),
			pclStateGetName(state->call_state),
			pclStateGetName(state->func_state),
			getFileNameConst(fileName),
			fileLine);

	// Recovering from a disconnect
	if(state->func_state == STATE_NONE)
	{
		switch(state->call_state)
		{
			xcase STATE_LOCK_FILE:
				lockReqSendPacket(client);
			xcase STATE_UNLOCK_FILES:
				unlockReqSendPacket(client);
			xcase STATE_CHECKIN_FILES:
				checkinSendNames(client, PATCHCLIENT_REQ_CHECKIN);
			xcase STATE_FORCEIN_DIR:
				checkinSendNames(client, PATCHCLIENT_REQ_FORCEIN);
			xcase STATE_NAME_VIEW:
				nameViewSendPacket(client);
			xcase STATE_SET_EXPIRATION:
				setExpirationSendPacket(client);
			xcase STATE_SET_FILE_EXPIRATION:
				setFileExpirationSendPacket(client);
			xcase STATE_SET_AUTHOR:
				setAuthorSendPacket(client);
			xcase STATE_FILE_HISTORY:
				fileHistorySendPacket(client);
			xcase STATE_VERSION_INFO:
				versionInfoSendPacket(client);
			xcase STATE_SET_VIEW:
				setViewSendPacket(client);
			xcase STATE_NOTIFY_ME:
				notifyMeSendPacket(client);
			xcase STATE_PROJECT_LIST:
				getProjectListSendPacket(client);
			xcase STATE_BRANCH_INFO:
				getBranchInfoSendPacket(client);
			xcase STATE_NAME_LIST:
				getNameListSendPacket(client);
			xcase STATE_CHECK_DELETED:
				checkDeletedSendPacket(client);
			xcase STATE_QUERY_LASTAUTHOR:
				getAuthorQuerySendPacket(client);
			xcase STATE_QUERY_LOCKAUTHOR:
				getLockAuthorQuerySendPacket(client);
			xcase STATE_GET_CHECKINS_BETWEEN_TIMES:
				getCheckinsBetweenTimesSendPacket(client);
			xcase STATE_GET_FILEVERSION_INFO:
				getFileVersionInfoSendPacket(client);
			xcase STATE_IS_COMPLETELY_SYNCED:
				isCompletelySyncedSendPacket(client);
			xcase STATE_UNDO_CHECKIN:
				undoCheckinSendPacket(client);
			xcase STATE_PING:
				pingSendPacket(client);
		}
		CHECK_ERROR_PI(client);
	}

	switch(state->general_state)
	{
		xcase STATE_CONNECTING:{
			if(state->call_state == STATE_AUTOUPDATE)
				checkAutoupdateStatus(client);
			s_connectToServer(client,"still connecting", NULL, 0);
		}
		xcase STATE_IDLE:{
			// Do nothing.
		}
		xcase STATE_WAITING:{
			switch(state->call_state)
			{
				xcase STATE_SET_VIEW:
					setViewProcess(client);
				xcase STATE_LOCK_FILE:
					if(state->func_state == STATE_GETTING_LOCKED_FILE)
					{
						PCL_LockCallback callback = state->lock_file.callback;
						void *userData = state->lock_file.userData;
						pclStateRemoveHead(client);
						if(callback)
							callback(client->error, client->error_details, userData);
					}
				xcase STATE_UNLOCK_FILES:
					if(state->func_state == STATE_GETTING_UNLOCKED_FILE)
					{
						PCL_LockCallback callback = state->unlock_files.callback;
						void *userData = state->unlock_files.userData;
						pclStateRemoveHead(client);
						if(callback)
							callback(client->error, client->error_details, userData);
					}
				xcase STATE_CHECKIN_FILES:
				acase STATE_FORCEIN_DIR:
					checkinFilesProcess(client);
			}
		}
		xcase STATE_XFERRING:{
			xferProcess(client->xferrer);
			switch(state->call_state)
			{
				xcase STATE_GET_FILE:{
					if(eaSize(&client->xferrer->xfers) == 0)
					{
						if(client->processCallback)
							doProcessCallback(client, 0);

						pclStateRemoveHead(client);
						if(eaSize(&client->state)){
							state = client->state[0];
						}else{
							state = NULL;
						}
					}
				}
				xcase STATE_GET_REQUIRED_FILES:{
					ret = getFilesForDirEntry(client, &(state->get_required.dir_entry_stack), &(state->get_required.child_stack),
						state->get_required.requiredOnly, state->get_required.getHeaders, state->get_required.onlyHeaders);
					if(ret != PCL_XFERS_FULL)
					{
						if(ret == PCL_SUCCESS && eaSize(&client->xferrer->xfers) == 0)
						{
							PCL_GetBatchCallback callback = state->get_required.callback;
							void * userData = state->get_required.userData;

							if(client->processCallback)
								doProcessCallback(client, 0);

							if (!state->get_required.onlyHeaders)
							{
								PCL_MirrorFileCallback mirror_callback = state->get_required.mirror_callback;
								void *mirror_userdata = state->get_required.mirror_userdata;
								PCL_ErrorCode error_code;
								PCLFileSpec filespec = state->get_required.filespec;

								eaDestroy(&state->get_required.dir_entry_stack);
								eaiDestroy(&state->get_required.child_stack);
								error_code = mirrorFiles(client,mirror_callback,mirror_userdata, &filespec);
								if(error_code)
								{
									RETURN_ERROR_PERFINFO_STOP(client, error_code);
								}
							}
							pclStateRemoveHead(client);
							state = client->state[0];
							pclSendReport(client, PCLSMS_SUCCEEDED, NULL);
							if(callback)
								callback(client, client->error, client->error_details, userData);
						}
					}
				}
				xcase STATE_GET_FILE_LIST:{
					addFileListToXferrer(client);
					if(state->get_list.i >= eaSize(&state->get_list.fnames) && eaSize(&client->xferrer->xfers) == 0)
					{
						PCL_GetFileListCallback callback = state->get_list.callback;
						char **fnames=NULL;
						void * userData = state->get_list.userData;
						PCL_ErrorCode error_code;
						if(client->processCallback)
							doProcessCallback(client, 0);

						loadend_printf("done.");

						error_code = mirrorFileList(client,
													state->get_list.fnames,
													state->get_list.mirror,
													state->get_list.touch);

						if(error_code)
						{
							RETURN_ERROR_PERFINFO_STOP(client, error_code);
						}

						if (callback)
						{
							fnames = state->get_list.fnames;
							state->get_list.fnames = NULL;
						}
						pclStateRemoveHead(client);
						state = eaSize(&client->state) ? client->state[0] : NULL;

						// Verify the file checksums match the ones from the server.
						if(state && state->call_state == STATE_LOCK_FILE && state->lock_file.checksums)
						{
							int i;
							char buf[MAX_PATH];
							assertmsg(eaSize(&state->lock_file.files)==ea32Size(&state->lock_file.checksums), "Different number of files vs. checksums");
							for(i=0; i<eaSize(&state->lock_file.files); i++)
							{
								U32 sum;

								sprintf(buf, "%s/%s", client->root_folder, state->lock_file.files[i]);
								sum = patchChecksumFile(buf);
								assertmsgf(sum==state->lock_file.checksums[i], "Checksum mismatch on %s, expecting %u, local is %u.", state->lock_file.files[i], state->lock_file.checksums[i], sum);
							}
						}

						pclSendReport(client, PCLSMS_SUCCEEDED, NULL);

						if(callback)
						{
							callback(client, client->error, client->error_details, userData, fnames);
							eaDestroy(&fnames);
						}
					}
				}
			}
		}
		xdefault:{
			RETURN_ERROR_PERFINFO_STOP(client, PCL_INTERNAL_LOGIC_BUG);
		}
	};
	if(client->link)
		linkFlush(client->link);
	client->process_loops++;
	elapsed = timerSeconds64(timerCpuTicks64() - client->callback_timer);
	if(	general_state == STATE_XFERRING &&
		client->processCallback &&
		(	elapsed >= 0.05
			||
			eaSize(&client->state) &&
			client->state[0]->general_state == STATE_IDLE)
		)
	{
		doProcessCallback(client, elapsed);
	}

	if(eaSize(&client->state)){
		state = client->state[0];
	}else{
		state = NULL;
	}

	if(state){
		pclMSpf2("pclStateEnd(%d):   general=%s, call=%s, func=%s\n",
			eaSize(&client->state),
			pclStateGetName(state->general_state),
			pclStateGetName(state->call_state),
			pclStateGetName(state->func_state));
	}
		
	PERFINFO_AUTO_STOP();

	if(client->disconnected)
		return PCL_LOST_CONNECTION;
	else if(state && state->general_state == STATE_IDLE)
		return PCL_SUCCESS;
	else
		return PCL_WAITING;
}

PCL_ErrorCode pclProcess(SA_PARAM_NN_VALID PCL_Client * client){
	return pclProcessTracked(client);
}

PCL_ErrorCode pclWait(SA_PARAM_NN_VALID PCL_Client * client)
{
	return pclWaitFrames(client, false);
}

PCL_ErrorCode pclWaitFrames(SA_PARAM_NN_VALID PCL_Client * client, bool frames)
{
	PCL_ErrorCode status;
	#if _XBOX
		int flushTimer;
		int waitingTimer;
	#endif

	PERFINFO_AUTO_START_FUNC();

	if(!client)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_CLIENT_PTR_IS_NULL;
	}
	
	CHECK_ERROR_PI(client);
	
	#if _XBOX
		flushTimer = timerAlloc();
		waitingTimer = timerAlloc();
	#endif

	pclMSpf("pclWait\n");

	do
	{
		if (frames)
			autoTimerThreadFrameBegin("pclWait");

		commMonitorWithTimeout(client->comm, 1);
		
		if (client->statusReportingComm)
		{
			commMonitorWithTimeout(client->statusReportingComm, 0);
		}

		status = pclProcessTracked(client);

		if (pclCheckTimeout(client))
		{
			pclSendLog(client, "PCLTimeout", "");
			status = PCL_TIMED_OUT;
			break;
		}

		#if _XBOX
			if(timerElapsed(flushTimer) > 0.5)
			{
				timerStart(flushTimer);
				fileSpecFlushHoggs(client->filespec);
			}
		#endif

		if (frames)
			autoTimerThreadFrameEnd();

	} while(status == PCL_WAITING);

	#if _XBOX
		timerFree(flushTimer);
		timerFree(waitingTimer);
		fileSpecFlushHoggs(client->filespec);
	#endif

	PERFINFO_AUTO_STOP_FUNC();

	return status;
}

// Return PCL_TIMED_OUT if we should time out, or PCL_SUCCESS otherwise.
PCL_ErrorCode pclCheckTimeout(SA_PARAM_NN_VALID PCL_Client * client)
{
	PERFINFO_AUTO_START_FUNC();

	if(timerSeconds64(timerCpuTicks64() - client->progress_timer) > client->timeout && client->link && (linkRecvTimeElapsed(client->link) > client->timeout || !linkConnected(client->link)))
	{
		++client->progress_timer_retries;
		if (client->progress_timer_retries > 10)
		{
			PERFINFO_AUTO_STOP_FUNC();
			return PCL_TIMED_OUT;
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
	return PCL_SUCCESS;
}

PCL_ErrorCode pclClearError(PCL_Client *client)
{
	CHECK_STATE(client);
	client->error = PCL_SUCCESS;
	return PCL_SUCCESS;
}

PCL_ErrorCode pclVerifyAllFiles(SA_PARAM_NN_VALID PCL_Client *client, S32 enabled)
{
	CHECK_STATE(client);
	client->verifyAllFiles = !!enabled;
	return PCL_SUCCESS;
}

PCL_ErrorCode pclUseFileOverlay(SA_PARAM_NN_VALID PCL_Client *client, S32 enabled)
{
	CHECK_STATE(client);
	client->useFileOverlay = !!enabled;
	return PCL_SUCCESS;
}

PCL_ErrorCode pclVerboseLogging(SA_PARAM_NN_VALID PCL_Client *client, S32 enabled)
{
	CHECK_STATE(client);
	client->verbose_logging = !!enabled;
	return PCL_SUCCESS;
}

PCL_ErrorCode pclFlush(PCL_Client * client)
{
	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	fileSpecFlushHoggs(client->filespec);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

PCL_ErrorCode pclDisconnectAndDestroy(PCL_Client* client)
{
	PERFINFO_AUTO_START_FUNC();

	if(!client){
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_SUCCESS;
	}

	pclSendReport(client, PCLSMS_FAILED, "Aborted");

	eaDestroyEx(&client->state, stateDestroy);
	if(client->link)
	{
		linkSetUserData(client->link, NULL);
		linkFlushAndClose(&client->link, "pclDisconnectAndDestroy");
	}
	fileSpecDestroy(&client->filespec);
	eaDestroyEx(&client->extra_folders, NULL);
	patchDbDestroy(&client->db);
	xferrerDestroy(&client->xferrer);
	SAFE_FREE(client->server);
	SAFE_FREE(client->original_server);
	SAFE_FREE(client->referrer);
	SAFE_FREE(client->project);
	SAFE_FREE(client->sandbox);
	SAFE_FREE(client->prefix);
	SAFE_FREE(client->author);
	SAFE_FREE(client->root_folder);
	SAFE_FREE(client->autoupdate_token);
	SAFE_FREE(client->filespec_fname);
	SAFE_FREE(client->manifest_fname);
	SAFE_FREE(client->full_manifest_fname);
	SAFE_FREE(client->extra_filespec_fname);
	SAFE_FREE(client->bad_files);
	SAFE_FREE(client->error_details);
	fileVersionDestroy(client->in_memory_data.manifest);
	fileVersionDestroy(client->in_memory_data.filespec);
	if(client->headers)
		hogFileDestroy(client->headers, true);
	pclDestroyReporting(client);
	free(client);

	PERFINFO_AUTO_STOP_FUNC();
	
	return PCL_SUCCESS;
}

// Disconnect from the Patch Server.  This will initiate reconnection or go into the disconnected state, depending on whether PCL_RECONNECT is set.
PCL_ErrorCode pclDisconnect(PCL_Client* client)
{
	PERFINFO_AUTO_START_FUNC();

	if(!client){
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_SUCCESS;
	}

	linkFlushAndClose(&client->link, "pclDisconnect");

	PERFINFO_AUTO_STOP_FUNC();
	return PCL_SUCCESS;
}

static void nameViewResponse(PCL_Client * client, Packet * pak, int cmd)
{
	int success;
	PCL_NameViewCallback callback = client->state[0]->name_view.callback;
	void * userData = client->state[0]->name_view.callback;

	if(cmd != PATCHSERVER_VIEW_NAMED)
	{
		pcllog(client, PCLLOG_ERROR, "unexpected response to naming: %i", cmd);
		REPORT_ERROR_STRING(client, PCL_UNEXPECTED_RESPONSE, "Expecting a view named response");
	}
	else
	{
		success = pktGetBits(pak, 32);
		if(!success)
		{
			char * err_msg;
			err_msg = pktGetStringTemp(pak);
			pcllog(client, PCLLOG_INFO, "naming failed: %s", err_msg);
			REPORT_ERROR_STRING(client, PCL_NAMING_FAILED, err_msg);
		}
	}

	pclStateRemoveHead(client);
	if(callback)
		callback(client->error, client->error_details, userData);
}

static void nameViewSendPacket(PCL_Client * client)
{
	StateStruct * state = client->state[0];
	Packet *pak_out = pktCreate(client->link, PATCHCLIENT_NAME_VIEW);
	pktSendString(pak_out, state->name_view.name);
	pktSendBits(pak_out, 32, state->name_view.days);
	pktSendString(pak_out, state->name_view.comment);
	pktSend(&pak_out);
	LOGPACKET(client, "PATCHCLIENT_NAME_VIEW %s %u %s", state->name_view.name, state->name_view.days, state->name_view.comment);

	state->func_state = STATE_NAME_VIEW;
}

PCL_ErrorCode pclNameCurrentView(	SA_PARAM_NN_VALID PCL_Client * client,
									SA_PARAM_NN_STR const char * viewName,
									int days,
									const char* comment,
									PCL_NameViewCallback callback,
									void * userData)
{
	StateStruct * state;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);
	if (!viewName || !*viewName)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_NULL_PARAMETER;
	}
	
	state = calloc(1, sizeof(StateStruct));
	state->general_state = STATE_WAITING;
	state->call_state = STATE_NAME_VIEW;
	state->func_state = STATE_NONE;
	state->name_view.callback = callback;
	state->name_view.userData = userData;
	state->name_view.name = strdup(viewName);
	state->name_view.comment = strdup(NULL_TO_EMPTY(comment));
	state->name_view.days = days < 0 ? U32_MAX : days;
	pclStateAdd(client, state);

	resetView(client);

	if(client->view_status == VIEW_SET)
	{
		nameViewSendPacket(client);
	}

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

static void setExpirationResponse(PCL_Client *client, Packet *pak_in, int cmd)
{
	PCL_SetExpirationCallback callback = client->state[0]->set_expiration.callback;
	void * userData = client->state[0]->set_expiration.callback;
	bool success;
	char *msg = "";

	if(cmd != PATCHSERVER_EXPIRATION_SET)
	{
		pcllog(client, PCLLOG_ERROR, "unexpected response to naming: %i", cmd);
		REPORT_ERROR_STRING(client, PCL_UNEXPECTED_RESPONSE, "Expecting a view named response");
	}
	else
	{
		success = pktGetBool(pak_in);
		msg = pktGetStringTemp(pak_in);

		if(!success)
		{
			pcllog(client, PCLLOG_INFO, "setting expiration failed: %s", msg);
			REPORT_ERROR_STRING(client, PCL_SET_EXPIRATION_FAILED, msg);
		}
	}

	pclStateRemoveHead(client);
	if(callback)
		callback(client->error, msg, client->error_details, userData);
}

static void setExpirationSendPacket(PCL_Client * client)
{
	StateStruct * state = client->state[0];
	Packet *pak = pktCreate(client->link, PATCHCLIENT_SET_EXPIRATION);
	pktSendString(pak, state->set_expiration.project);
	pktSendString(pak, state->set_expiration.name);
	pktSendBits(pak, 32, state->set_expiration.days);
	pktSend(&pak);
	LOGPACKET(client, "PATCHCLIENT_SET_EXPIRATION %s %i", state->set_expiration.name, state->set_expiration.days);

	state->func_state = STATE_SET_EXPIRATION;
}

PCL_ErrorCode pclSetExpiration(PCL_Client *client, const char *project, const char *viewName, int days,
								PCL_SetExpirationCallback callback, void *userData)
{
	StateStruct * state;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);
	
	state = calloc(1, sizeof(StateStruct));
	state->general_state = STATE_WAITING;
	state->call_state = STATE_SET_EXPIRATION;
	state->func_state = STATE_SET_EXPIRATION;
	state->set_expiration.callback = callback;
	state->set_expiration.userData = userData;
	state->set_expiration.project = strdup(project);
	state->set_expiration.name = strdup(viewName);
	state->set_expiration.days = days < 0 ? U32_MAX : days;
	pclStateAdd(client, state);

	setExpirationSendPacket(client);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

static void setFileExpirationResponse(PCL_Client *client, Packet *pak_in, int cmd)
{
	PCL_SetFileExpirationCallback callback = client->state[0]->set_file_expiration.callback;
	void *userData = client->state[0]->set_file_expiration.userData;
	bool success;

	if(cmd != PATCHSERVER_FILE_EXPIRATION_SET)
	{
		pcllog(client, PCLLOG_ERROR, "unexpected response to expiration: %i", cmd);
		REPORT_ERROR_STRING(client, PCL_UNEXPECTED_RESPONSE, "Expecting a file expiration response");
	}
	else
	{
		success = pktGetBool(pak_in);

		if(!success)
		{
			pcllog(client, PCLLOG_INFO, "setting file expiration failed");
			REPORT_ERROR_STRING(client, PCL_SET_EXPIRATION_FAILED, "failed");
		}
	}

	pclStateRemoveHead(client);
	if(callback)
		callback(client->error, client->error_details, userData);
}

static void setFileExpirationSendPacket(PCL_Client * client)
{
	StateStruct * state = client->state[0];
	Packet *pak = pktCreate(client->link, PATCHCLIENT_SET_FILE_EXPIRATION);
	pktSendString(pak, state->set_file_expiration.path);
	pktSendBits(pak, 32, state->set_file_expiration.days);
	pktSend(&pak);
	LOGPACKET(client, "PATCHCLIENT_SET_FILE_EXPIRATION %s %i", state->set_file_expiration.path, state->set_file_expiration.days);

	state->func_state = STATE_SET_EXPIRATION;
}

PCL_ErrorCode pclSetFileExpiration(SA_PARAM_NN_VALID PCL_Client *client, const char *path, int days, PCL_SetFileExpirationCallback callback, void *userData)
{
	StateStruct *state;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);

	state = calloc(1, sizeof(StateStruct));
	state->general_state = STATE_WAITING;
	state->call_state = STATE_SET_FILE_EXPIRATION;
	state->func_state = STATE_SET_FILE_EXPIRATION;
	state->set_file_expiration.callback = callback;
	state->set_file_expiration.userData = userData;
	state->set_file_expiration.path = strdup(path);
	state->set_file_expiration.days = days < 0 ? U32_MAX : days;
	pclStateAdd(client, state);

	setFileExpirationSendPacket(client);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

static void setAuthorResponse(PCL_Client * client, Packet * pak, int cmd)
{
	PCL_SetAuthorCallback callback = client->state[0]->set_author.callback;
	void * userData = client->state[0]->set_author.userData;

	if(cmd != PATCHSERVER_AUTHOR_RESPONSE)
	{
		REPORT_ERROR_STRING(client, PCL_UNEXPECTED_RESPONSE, "Expecting a set author response");
	}
	else
	{
		int status = pktGetBits(pak, 32);
		if(!status)
		{
			REPORT_ERROR(client, PCL_AUTHOR_FAILED);
		}
		if(status)
		{
			client->author_status = VIEW_SET;
		}
	}

	pclStateRemoveHead(client);
	if(callback)
		callback(client->error, client->error_details, userData);
}

static void setAuthorSendPacket(PCL_Client * client)
{
	StateStruct * state = client->state[0];
	Packet * pak;

	pak = pktCreate(client->link, PATCHCLIENT_SET_AUTHOR);
	pktSendString(pak, client->author);
	pktSend(&pak);
	LOGPACKET(client, "PATCHCLIENT_SET_AUTHOR %s", client->author);

	state->func_state = STATE_SET_AUTHOR;
}

PCL_ErrorCode pclSetAuthor(PCL_Client * client, const char * author, PCL_SetAuthorCallback callback, void * userData)
{
	char authorTrimmed[MAX_PATH];

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);
	if(!author)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_NULL_PARAMETER);
		
	strcpy(authorTrimmed, author);
	removeLeadingAndFollowingSpaces(authorTrimmed);
	author = authorTrimmed;

	if(!client->author || strcmp(client->author, author))
	{
		StateStruct *state;

		SAFE_FREE(client->author);
		client->author = strdup(author);

		state = calloc(1, sizeof(StateStruct));
		state->set_author.callback = callback;
		state->set_author.userData = userData;
		state->general_state = STATE_WAITING;
		state->call_state = STATE_SET_AUTHOR;
		state->func_state = STATE_NONE;
		pclStateAdd(client, state);

		resetView(client);
		if(client->view_status == VIEW_SET)
		{
			setAuthorSendPacket(client);	
		}
	}

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

// Request that a Patch Server shut down.
PCL_ErrorCode pclShutdown(SA_PARAM_NN_VALID PCL_Client * client)
{
	Packet *pak;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);

	if(!client || !client->link)
		return PCL_NULL_PARAMETER;

	pak = pktCreate(client->link, PATCHCLIENT_SHUTDOWN);
	pktSend(&pak);
	LOGPACKET(client, "PATCHCLIENT_SHUTDOWN");

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

// Request that a Patch Server run its merger.
PCL_ErrorCode pclMergeServer(SA_PARAM_NN_VALID PCL_Client * client)
{
	Packet *pak;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);

	if(!client || !client->link)
		return PCL_NULL_PARAMETER;

	// Send the merge request packet.
	pak = pktCreate(client->link, PATCHCLIENT_SERVER_MERGE);
	pktSend(&pak);
	LOGPACKET(client, "PATCHCLIENT_SERVER_MERGE");

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}


PCL_ErrorCode pclNotifyView(SA_PARAM_NN_VALID PCL_Client * client, SA_PARAM_NN_STR const char * project, SA_PARAM_NN_STR const char * viewName, U32 ip)
{
	Packet *pak;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	//CHECK_STATE_PI(client);

	if(!client || !client->link || !viewName)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_NULL_PARAMETER;
	}

	pak = pktCreate(client->link, PATCHCLIENT_NOTIFYVIEW);
	pktSendString(pak, project);
	pktSendString(pak, viewName);
	pktSendU32(pak, ip);
	pktSend(&pak);
	LOGPACKET(client, "PATCHCLIENT_NOTIFYVIEW");

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

// Notify the parent Patch Server that a server (either itself or one of its children) has been activated.
PCL_ErrorCode	pclPatchServerActivate(PCL_Client *client, const char *name, const char *category, const char *parent)
{
	Packet *pak;

	PERFINFO_AUTO_START_FUNC();

	// Validate.
	CHECK_ERROR_PI(client);
	if (!client || !client->link || !name || !*name || !category || !*category)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_NULL_PARAMETER;
	}

	// Send packet
	pak = pktCreate(client->link, PATCHCLIENT_PATCHSERVER_ACTIVATED);
	pktSendString(pak, name);
	pktSendString(pak, category);
	pktSendString(pak, parent ? parent : "");
	pktSend(&pak);
	LOGPACKET(client, "PATCHCLIENT_PATCHSERVER_ACTIVATED");

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

// Notify the parent Patch Server that a server (one of its children) has been deactivated.
PCL_ErrorCode	pclPatchServerDeactivate(SA_PARAM_NN_VALID PCL_Client *client, SA_PARAM_NN_STR const char *name)
{
	Packet *pak;

	PERFINFO_AUTO_START_FUNC();

	// Validate.
	CHECK_ERROR_PI(client);
	if (!client || !client->link || !name || !*name)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_NULL_PARAMETER;
	}

	// Send packet
	pak = pktCreate(client->link, PATCHCLIENT_PATCHSERVER_DEACTIVATED);
	pktSendString(pak, name);
	pktSend(&pak);
	LOGPACKET(client, "PATCHCLIENT_PATCHSERVER_DEACTIVATED");

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

// Send updating status to parent; the format of the status string is defined by the Patch Server.
PCL_ErrorCode	pclPatchServerUpdateStatus(SA_PARAM_NN_VALID PCL_Client *client, SA_PARAM_NN_STR const char *name, SA_PARAM_NN_STR const char *status)
{
	Packet *pak;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);

	if (!client || !client->link || !name || !*name || !status || !*status)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_NULL_PARAMETER;
	}

	pak = pktCreate(client->link, PATCHCLIENT_PATCHSERVER_UPDATE_STATUS);
	pktSendString(pak, name);
	pktSendString(pak, status);
	pktSend(&pak);
	LOGPACKET(client, "PATCHCLIENT_PATCHSERVER_UPDATE_STATUS");

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

// Set handler for processing HTTP patching information updates.
PCL_ErrorCode	pclPatchServerHandleAutoupInfo(SA_PARAM_NN_VALID PCL_Client *client, PCL_AutoupInfoCallback callback, void *userData)
{
	PERFINFO_AUTO_START_FUNC();

	// Validate.
	CHECK_ERROR_PI(client);
	if (!client || !client->link)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_NULL_PARAMETER;
	}

	// Save callback information.
	client->autoupinfoCallback = callback;
	client->autoupinfoUserData = userData;

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

// Set handler for processing HTTP patching information updates.
PCL_ErrorCode	pclPatchServerHandleHttpInfo(SA_PARAM_NN_VALID PCL_Client *client, PCL_HttpInfoCallback callback, void *userData)
{
	PERFINFO_AUTO_START_FUNC();

	// Validate.
	CHECK_ERROR_PI(client);
	if (!client || !client->link)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_NULL_PARAMETER;
	}

	// Save callback information.
	client->httpinfoCallback = callback;
	client->httpinfoUserData = userData;

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

AUTO_COMMAND ACMD_NAME(ignorechecksum);
void ignorechecksum(int value) { FatalErrorf("This was removed, find the Patcher programmer to reimplement it"); }

// Periodically send a ping on the NetLink while we're doing something really slow on our side.
static void forceinKeepAlive(PCL_Client *client)
{
	S64 now = timerCpuTicks64();
	if (client->link && (!client->forceinKeepAlive || timerSeconds64(now - client->forceinKeepAlive) > 60))
	{
		linkSendKeepAlive(client->link);
		linkFlush(client->link);
		client->forceinKeepAlive = now;
	}
}

static void forceInCheckFile(const char *fname, PatchFilescanData *userdata, bool recreate_deleted, bool is_single_file)
{
	PCL_Client *client = userdata->client;
	char counts_as_buf[MAX_PATH], * counts_as, matchStr[MAX_PATH];
	const char * subpath;
	char *result;
	bool matched = false, upload = true;
	PCL_DiffType diff_type = PCLDIFF_CHANGED;
	int i;
	
	PERFINFO_AUTO_START_FUNC();

	subpath = fname + strlen(userdata->dir_name);
	if(subpath[0] == '/')
		++subpath;

	counts_as = counts_as_buf;
	strcpy(counts_as_buf, userdata->counts_as);
	if(subpath[0])
	{
		if(userdata->counts_as[0] && userdata->counts_as[strlen(userdata->counts_as) - 1] != '/')
			strcat(counts_as_buf, "/");
		strcat(counts_as_buf, subpath);
	}

	if(strnicmp(counts_as, "./", 2) == 0)
		counts_as += 2;

	// GGFIXME: Add feedback on the different cases of these.
	if( fileSpecIsIncluded(client->filespec, counts_as)
		&&
		fileSpecIsUnderSourceControl(client->filespec, counts_as)
		&&
		(	userdata->forceIfNotLockedByClient ||
			userdata->forceIfLockedByClient ||
			!fileSpecIsBin(client->filespec, counts_as)) 
		)
	{
		strcpy(matchStr, fname);
		string_toupper(matchStr);
		if(matchExact(userdata->match_str, matchStr))
		{
			matched = true;
			for(i = 0; i < eaSize(&userdata->hide); i++)
			{
				if(matchExact(userdata->hide[i], matchStr))
				{
					matched = false;
					break;
				}
			}
		}
	}

	if(!matched)
	{
		pclMSpf("%s as %s did not match wildcards/hidepaths", counts_as, fname);
		upload = false;
	}
	else if(stashFindPointer(userdata->stash, counts_as, &result))
	{
		pclMSpf("%s was already found with %s, skipping %s", counts_as, result, fname);
		upload = false;
	}
	else if(!fileExists(fname))
	{
		pclMSpf("%s does not exist, it must have been deleted during scanning", counts_as);
		upload = false;
	}
	else if(fileSpecIsAHogg(client->filespec, counts_as))
	{
		pclMSpf("%s as %s is a locally managed hogg", fname, counts_as);
		upload = false;
	}
	else
	{
		DirEntry *dir = patchFindPath(client->db, counts_as, 0);
		stashAddPointer(userdata->stash, counts_as, userdata->match_str, false);
		if(!dir)
		{
			pclMSpf("%s as %s was not found", fname, counts_as);
			diff_type = PCLDIFF_CREATED;
		}
		else if(dir->versions[0]->flags & FILEVERSION_DELETED)
		{
			U32 on_disk_time = (U32)fileLastChangedAbsolute(fname);
			// FIXME: this should push on a checkDeleted operation.
			// "will get checked in even though deleted" check.
			if(	recreate_deleted
				||
				userdata->forceIfNotLockedByClient
				||
				userdata->forceIfLockedByClient
				||
				on_disk_time > dir->versions[0]->modified &&
				!timestampsMatch(on_disk_time, dir->versions[0]->modified) &&
				!fileIsReadOnly(fname))
			{
				pclMSpf("%s as %s is a recreated deletion", fname, counts_as);
				diff_type = PCLDIFF_CREATED;
			}
			else
			{
				pclMSpf("%s as %s was remotely but not locally deleted", fname, counts_as);
				upload = false;
			}
		}
		else if(userdata->forceIfLockedByClient && !isLockedByClient(client, dir))
		{
			upload = false;
		}
		else if(!userdata->forceIfNotLockedByClient && !isLockedByClient(client, dir))
		{
			if (is_single_file)
				pcllog(client, PCLLOG_ERROR, "%s not checked out by you", fname);
			upload = false;
		}
		else if(userdata->ignore_checksum) // g_ignore_checksum allows for unchanged files to be uploaded
		{
			pclMSpf("%s as %s had its checksum ignored", fname, counts_as);
		}
		else
		{
			int	file_changed = 0,same_size;
			U32 on_disk_time = (U32)fileLastChangedAbsolute(fname);

			same_size = fileSize(fname) == dir->versions[0]->size;
			if (!same_size)
			{
				file_changed = 1;
			}
			else
			{
				file_changed = !timestampsMatch(on_disk_time, dir->versions[0]->modified);
				
				if(file_changed)
				{
					//U32 len;
					//char *file_data = fileAlloc(fname, &len);
					//U32 checksum = patchChecksum(file_data, len);
					//free(file_data);
					U32 checksum = patchChecksumFile(fname);
					file_changed = checksum != dir->versions[0]->checksum;
				}
			}

			if(!file_changed)
			{
				pclMSpf("%s as %s matched", fname, counts_as);
				if(isLockedByClient(client, dir))
					diff_type = PCLDIFF_NOCHANGE;
				else
					upload = false;
			}
			else
			{
				pclMSpf("%s as %s failed checksum", fname, counts_as);
			}
		}

		if(userdata->diff_types && diff_type != PCLDIFF_NOCHANGE && dir)
		{
			if(dir->versions[0]->flags & FILEVERSION_LINK_BACKWARD_SOLID)
				diff_type |= PCLDIFF_LINKWILLBREAK;
			if(dir->versions[0]->flags & FILEVERSION_LINK_FORWARD_BROKEN)
				diff_type |= PCLDIFF_LINKISBROKEN;
		}
	}

	if(upload)
	{
		PERFINFO_AUTO_START("upload", 1);
		if(userdata->disk_names)
			eaPush(userdata->disk_names, strdup(fname));
		if(userdata->db_names)
			eaPush(userdata->db_names, strdup(counts_as));
		if(userdata->diff_types)
		{
			if((diff_type & PCLDIFFMASK_ACTION) != PCLDIFF_NOCHANGE && fileSpecIsNoWarn(client->filespec, counts_as))
				diff_type |= PCLDIFF_DONTTEST;
			eaiPush(userdata->diff_types, diff_type);
		}
		if(diff_type == PCLDIFF_NOCHANGE && userdata->undo_names)
			eaPush(userdata->undo_names, strdup(counts_as));
		pclMSpf("added to the upload list: %s", fname);
		PERFINFO_AUTO_STOP();
	}

	userdata->file++;
	if (client->forceInScanCallback)
	{
		bool success;
		PERFINFO_AUTO_START("forceInScanCallback", 1);
		success = client->forceInScanCallback(fname, userdata->file, userdata->file_count, upload,
									timerSeconds64(timerCpuTicks64() - client->callback_timer), client->error, client->error_details,
									client->forceInScanUserData);
		PERFINFO_AUTO_STOP_CHECKED("forceInScanCallback");
		if (success)
		{
			client->callback_timer = timerCpuTicks64();
		}
	}
	forceinKeepAlive(client);	

	PERFINFO_AUTO_STOP_FUNC();// FUNC
}

static FileScanAction countFilesCallback(char *dir_name, struct _finddata32_t *data, PatchFilescanData *userdata)
{
	if(data->attrib & _A_SUBDIR)
	{
		if(userdata->recursive)
			return FSA_EXPLORE_DIRECTORY;
		else
			return FSA_NO_EXPLORE_DIRECTORY;
	}
	else
	{
		userdata->file_count++;
		return FSA_EXPLORE_DIRECTORY;
	}
}

static FileScanAction searchForForceInCallback(char *dir_name, struct _finddata32_t *data, PatchFilescanData *userdata)
{
	FileScanAction result;
	
	PERFINFO_AUTO_START_FUNC();

	if(data->attrib & _A_SUBDIR)
	{
		if(userdata->recursive)
			result = FSA_EXPLORE_DIRECTORY;
		else
			result = FSA_NO_EXPLORE_DIRECTORY;
	}
	else if(strEndsWith(data->name, ".deleteme"))
	{
		userdata->file++;
		result = FSA_EXPLORE_DIRECTORY;
	}
	else
	{
		char fname[MAX_PATH];
		if(strnicmp(dir_name, "./", 2) == 0 && strnicmp(userdata->dir_name, "./", 2) != 0)
			dir_name += 2;
		sprintf(fname, "%s/%s", dir_name, data->name);

		forceInCheckFile(fname, userdata, false, false);
		result = FSA_EXPLORE_DIRECTORY;
	}

	PERFINFO_AUTO_STOP_FUNC();
	
	return result;
}

static void remove_wildcard(char * path, int path_size)
{
	char * foundStar, * foundSlash, * foundFromFront;

	foundStar = strchr(path, '*');
	if(foundStar)
	{
		foundStar[0] = '\0';
		if(! path[0])
			strcpy_s(path, path_size, ".");

		foundSlash = strrchr(path, '/');
		if(foundSlash)
		{
			if(fileIsAbsolutePath(path))
			{
				if(strnicmp("//", path, 2) == 0)
					foundFromFront = path + 1;
				else
					foundFromFront = strchr(path, '/');

				if(foundFromFront == foundSlash)
					foundSlash[1] = '\0';
				else
					foundSlash[0] = '\0';
			}
			else
			{
				foundSlash[0] = '\0';
			}
		}
		else
		{
			strcpy_s(path, path_size, ".");
		}
	}
}

static void s_getDiffList(	PCL_Client *client,
							const char*const* dirNames,
							const char*const* countsAsDir,
							const int* recurse,
							int count,
							const char*const* hide_paths,
							int hide_count,
							bool matchesOnly,
							bool forceIfNotLockedByClient,
							bool forceIfLockedByClient,
							bool matchAsFiles,
							char ***disk_names,
							char ***db_names,
							PCL_DiffType **diff_types,
							char ***undo_names)
{
	int i;
	PatchFilescanData userdata = {0};

	PERFINFO_AUTO_START_FUNC();

	// Set up PatchFilescanData struct.
	PERFINFO_AUTO_START("Initializing", 1);
	userdata.client = client;
	userdata.stash = stashTableCreateWithStringKeys(1000, StashDeepCopyKeys_NeverRelease);
	userdata.force_delete = !matchesOnly;
	userdata.forceIfNotLockedByClient = forceIfNotLockedByClient;
	userdata.forceIfLockedByClient = forceIfLockedByClient;
	userdata.str_count = count;
	userdata.disk_names = disk_names;
	userdata.db_names = db_names;
	userdata.undo_names = undo_names;
	userdata.diff_types = diff_types;

	// Create arrays of filenames to check.
	for(i = 0; i < count; i++)
	{
		char onDisk[MAX_PATH], forUpload[MAX_PATH], matchString[MAX_PATH];

		if(fileIsAbsolutePath(dirNames[i]))
			strcpy(onDisk, dirNames[i]);
		else
			sprintf(onDisk, "./%s", dirNames[i]);
		forwardSlashes(onDisk);

		strcpy(forUpload, countsAsDir[i][0] == '/' ? countsAsDir[i]+1 : countsAsDir[i]);
		forwardSlashes(forUpload);

		strcpy(matchString, onDisk);
		string_toupper(matchString);

		remove_wildcard(SAFESTR(forUpload));
		remove_wildcard(SAFESTR(onDisk));

		if(strchr(matchString, '*') == NULL)
		{
			if(matchString[strlen(matchString) - 1] == '/')
				strcat(matchString, "*");
			else if(!matchAsFiles && !fileExists(matchString))
				strcat(matchString, "/*");
		}

		eaPush(&userdata.on_disk_strings, strdup(onDisk));
		eaPush(&userdata.counts_as_strings, strdup(forUpload));
		eaPush(&userdata.wildcard_strings, strdup(matchString));
	}

	// Create hide list.
	for(i = 0; i < hide_count; i++)
	{
		char path[MAX_PATH];

		if(fileIsAbsolutePath(hide_paths[i]))
			strcpy(path, hide_paths[i]);
		else
			sprintf(path, "./%s", hide_paths[i]);
		forwardSlashes(path);
		string_toupper(path);

		if(!strchr(path, '*'))
		{
			if(path[strlen(path) - 1] == '/')
				strcat(path, "*");
			else if(!fileExists(path))
				strcat(path, "/*");
		}

		eaPush(&userdata.hide, strdup(path));
	}
	{
		char patch_dir[MAX_PATH];
		sprintf(patch_dir, "*%s/*", PATCH_DIR);
		string_toupper(patch_dir);
		eaPush(&userdata.hide, strdup(patch_dir));
	}
	PERFINFO_AUTO_STOP_CHECKED("Initializing");

	// Get file count, for status callback.
	PERFINFO_AUTO_START("Counting", 1);
	for(i = 0; i < count; i++)
	{
		if(fileExists(userdata.on_disk_strings[i]))
		{
			userdata.file_count++;
		}
		else
		{
			userdata.recursive = recurse[i];
			fileScanAllDataDirs(userdata.on_disk_strings[i], countFilesCallback, &userdata);
		}
	}
	PERFINFO_AUTO_STOP_CHECKED("Counting");

	// Scan for new and changed files.
	PERFINFO_AUTO_START("Scanning", 1);
	for(i = 0; i < count; i++)
	{
		pcllog(client, PCLLOG_FILEONLY, "scanning %s as %s\n", userdata.on_disk_strings[i], userdata.counts_as_strings[i]);

		userdata.dir_name = userdata.on_disk_strings[i];
		userdata.counts_as = userdata.counts_as_strings[i];
		userdata.match_str = userdata.wildcard_strings[i];
		userdata.recursive = recurse[i];

		if(fileExists(userdata.dir_name))
		{
			forceInCheckFile(userdata.dir_name, &userdata, true, true);
		}
		else
		{
			userdata.recursive = recurse[i];
			fileScanAllDataDirs(userdata.dir_name, searchForForceInCallback, &userdata);
		}
	}
	PERFINFO_AUTO_STOP_CHECKED("Scanning");

	// Scan for deleted files.
	PERFINFO_AUTO_START("Deletions", 1);
	if (userdata.str_count == 1)
	{
		// Optimization: This optimizes for the case where we just need to scan one prefix, and not the entire manifest.
		patchForEachDirEntryPrefix(client->db, userdata.counts_as_strings[0], searchDbForDeletionsCB, &userdata);
	}
	else
		patchForEachDirEntry(client->db, searchDbForDeletionsCB, &userdata);
	PERFINFO_AUTO_STOP_CHECKED("Deletions");

//	assert(eaSize(userdata.disk_names) == eaSize(userdata.db_names));

	PERFINFO_AUTO_START("Cleanup", 1);
	eaDestroyEx(&userdata.on_disk_strings, NULL);
	eaDestroyEx(&userdata.counts_as_strings, NULL);
	eaDestroyEx(&userdata.wildcard_strings, NULL);
	eaDestroyEx(&userdata.hide, NULL);
	stashTableDestroy(userdata.stash);
	PERFINFO_AUTO_STOP_CHECKED("Cleanup");

	PERFINFO_AUTO_STOP_FUNC();
}

PCL_ErrorCode pclDiffFolder(PCL_Client *client,
							const char *dirName,
							bool forceIfNotLockedByClient,
							bool forceIfLockedByClient,
							bool matchAsFiles,
							char ***diff_names,
							PCL_DiffType **diff_types)
{
	char dirBuf[MAX_PATH], *dirPath = dirBuf;
	int recurse = 1;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);
	if(!dirName || !diff_names || !diff_types)
	{
		RETURN_ERROR_PERFINFO_STOP(client, PCL_NULL_PARAMETER);
	}
	if(eaSize(diff_names) != eaiSize(diff_types))
	{
		RETURN_ERROR_PERFINFO_STOP(client, PCL_INVALID_PARAMETER);
	}

	if(!client->root_folder)
		client->root_folder = strdup(client->project);
	if(dirName[0] == '/')
		dirName++;
	sprintf(dirBuf, "%s/%s", client->root_folder, dirName);
	forwardSlashes(dirBuf);
	if(dirBuf[strlen(dirBuf)-1] == '/')
		dirBuf[strlen(dirBuf)-1] = '\0';

	s_getDiffList(	client,
					&dirPath,
					&dirName,
					&recurse,
					1,
					NULL,
					0,
					true,
					forceIfNotLockedByClient,
					forceIfLockedByClient,
					matchAsFiles,
					NULL,
					diff_names,
					diff_types,
					NULL);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

static void checkinSendNames(PCL_Client * client, int cmd_out)
{
	Packet	*pak;
	int		i;
	S64		total = 0;
	U32 modified;
	StateStruct * state = client->state[0];
	int count = eaSize(&state->checkin.fnamesForUpload);
	char error_msg[MAX_PATH*2];

	state->checkin.i = 0;
	state->checkin.j = 0;
	state->checkin.total_bytes = 0;
	state->checkin.total_sent = 0;
	state->checkin.curr_sent = 0;

	pak = pktCreate(client->link, cmd_out);
	pktSendBits(pak,32,count);
	for(i = 0; i < count; i++)
	{
		int		size = -1;
		U32		checksum=0;

		pcllog(client, (i==count-1)?PCLLOG_TITLE:PCLLOG_TITLE_DISCARDABLE, "Sending info for file %i of %i", i+1, count);
		forceinKeepAlive(client);

		if (strlen(state->checkin.fnamesForUpload[i]) > PATCH_MAX_PATH)		//should match checks in pcl_client.c/checkinSendNames and patchmeui.c/patchmeDialogCheckin
		{
			sprintf(error_msg, "\"%s\" is over the limit of " STRINGIZE(PATCH_MAX_PATH) " characters.", state->checkin.fnamesForUpload[i]);
			REPORT_ERROR_STRING(client,PCL_CHECKIN_FAILED,error_msg);
			pktFree(&pak);
			return;
		}

		if(fileExists(state->checkin.fnamesOnDisk[i]))
		{
			U8 *file_mem = fileAlloc(state->checkin.fnamesOnDisk[i], &size);
			if(!file_mem)
			{
				sprintf(error_msg,"Could not open %s for checkin",state->checkin.fnamesOnDisk[i]);
				REPORT_ERROR_STRING(client,PCL_CHECKIN_FAILED,error_msg);
				pktFree(&pak);
				return;
			}
			else
			{
				if(size > 0)
				{
					checksum = patchChecksum(file_mem, size);
					total+=size;
				}
				free(file_mem);
			}
		}

		pktSendString(pak, state->checkin.fnamesForUpload[i]);
		pktSendBits(pak,32,size);
		pktSendBits(pak,32,checksum);
		modified = (state->checkin.fnamesOnDisk[i] ? fileLastChangedAbsolute(state->checkin.fnamesOnDisk[i]) : 0);
		if(modified == 0)
		{
			if(state->checkin.fnamesOnDisk[i])
				Errorf("Stat failed on %s during checkin", state->checkin.fnamesOnDisk[i]);
			modified = getCurrentFileTime();
		}
		pktSendBits(pak, 32, modified);
	}
	pktSend(&pak);
	LOGPACKET(client, "%s %i", (cmd_out == PATCHCLIENT_REQ_CHECKIN ? "PATCHCLIENT_REQ_CHECKIN" : "PATCHCLIENT_REQ_FORCEIN"), count);

	state->checkin.total_bytes = total;
	state->func_state = STATE_SENT_NAMES;
}

static void checkinSendData(PCL_Client* client,
							int* pI,
							int* pJ,
							U8** file_mem,
							U32* curr_sent,
							int* size,
							S64* total_bytes,
							S64* total_sent)
{
	Packet * pak;
	U32 pkt_bytes = 0;
	StateStruct * state = client->state[0];
	int count = eaSize(&state->checkin.fnamesForUpload);

	PERFINFO_AUTO_START_FUNC();

	if(	!*pI &&
		!*pJ &&
		client->uploadCallback)
	{
		bool success;

		PERFINFO_AUTO_START("uploadCallback", 1);
		success = client->uploadCallback(*total_sent,
									*total_bytes,
									timerSeconds64(timerCpuTicks64() - client->callback_timer),
									client->error,
									client->error_details,
									client->uploadUserData);
		PERFINFO_AUTO_STOP_CHECKED("uploadCallback");
		if (success)
		{
			client->callback_timer = timerCpuTicks64();
		}
		
	}

	if(*curr_sent + MAX_SINGLE_REQUEST_BYTES > client->xferrer->max_net_bytes)
	{
		return;
	}

	pak = pktCreate(client->link, PATCHCLIENT_BLOCK_SEND);

	for(; *pI < count; (*pI)++)
	{
		if(*pJ == 0)
		{
			*size = -1;
			SAFE_FREE(*file_mem);
			if(fileExists(state->checkin.fnamesOnDisk[*pI]))
			{
				U32 size_uncompressed;
				U8* file_mem_uncompressed = fileAlloc(	state->checkin.fnamesOnDisk[*pI],
														&size_uncompressed);

				if(!file_mem)
				{
					char error_msg[MAX_PATH];
					sprintf(error_msg,"Could not open %s for checkin",state->checkin.fnamesOnDisk[*pI]);
					REPORT_ERROR_STRING(client,PCL_CHECKIN_FAILED,error_msg);
					break;
				}

#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '*file_mem_uncompressed'"
				pclZipData(file_mem_uncompressed,
									size_uncompressed,
									size,
									file_mem);
									
				SAFE_FREE(file_mem_uncompressed);
			}
		}
		if(*file_mem)
		{
			for(; *pJ < *size; *pJ += MAX_SINGLE_REQUEST_BYTES)
			{
				U32 curr = MIN(*size - *pJ, MAX_SINGLE_REQUEST_BYTES);

				if(pkt_bytes + curr > MAX_SINGLE_REQUEST_BYTES)
				{
					*total_sent += pkt_bytes;
					*curr_sent += pkt_bytes;
					pktSendBits(pak, 32, -1);
					pktSend(&pak);
					pkt_bytes = 0;

					if (client->uploadCallback)
					{
						bool success;
						PERFINFO_AUTO_START("uploadCallback", 1);
						success = client->uploadCallback(*total_sent,
													*total_bytes,
													timerSeconds64(timerCpuTicks64() - client->callback_timer),
													client->error,
													client->error_details,
													client->uploadUserData);
						if (success)
						{
							client->callback_timer = timerCpuTicks64();
						}
						PERFINFO_AUTO_STOP_CHECKED("uploadCallback");
					}

					if(*curr_sent + MAX_SINGLE_REQUEST_BYTES > client->xferrer->max_net_bytes) {
						char buf1[64], buf2[64];
						pcllog(client, PCLLOG_TITLE_DISCARDABLE, "Sending file %i of %i  %s of %s  (%1.1f%%)", *pI+1, count, friendlyBytesBuf(*total_sent, buf1), friendlyBytesBuf(*total_bytes, buf2), (F32)(100.f* *total_sent / (F32)*total_bytes));
						PERFINFO_AUTO_STOP_FUNC();
						return;
					}
					
					pak = pktCreate(client->link, PATCHCLIENT_BLOCK_SEND);
				}

				pclMSpf("checkin %4i %8i %4i %8i %10"FORM_LL"i %s", *pI, *pJ, curr, *size, *total_sent, state->checkin.fnamesForUpload[*pI]);

				pktSendBits(pak, 32, *pI);
				pktSendBits(pak, 32, *pJ);
				if(!*pJ){
					pktSendBits(pak, 32, *size);
				}
				pktSendBits(pak, 32, curr);
				pktSendBytes(pak, curr, *file_mem + *pJ);
				pkt_bytes += curr;
			}
		}
		SAFE_FREE(*file_mem);
		*pJ = 0;
	}

	if (pkt_bytes)
	{
		*total_sent += pkt_bytes;
		*curr_sent += pkt_bytes;
		pktSendBits(pak, 32, -1);
		pktSend(&pak);
		pkt_bytes = 0;

		if (client->uploadCallback)
		{
			bool success;
			PERFINFO_AUTO_START("uploadCallback", 1);
			success = client->uploadCallback(*total_sent, *total_bytes, timerSeconds64(timerCpuTicks64() - client->callback_timer),
				client->error, client->error_details, client->uploadUserData);
			PERFINFO_AUTO_STOP_CHECKED("uploadCallback");
			if (success)
			{
				client->callback_timer = timerCpuTicks64();
			}
		}

		pcllog(client, PCLLOG_TITLE, "Sending %d files (%s) completed", count, friendlyBytes(*total_bytes));
	}
	else
		pktFree(&pak);

	client->state[0]->func_state = STATE_SENT_DATA;

	PERFINFO_AUTO_STOP_FUNC();
}

static void checkinFilesProcess(PCL_Client * client)
{
	StateStruct * state = client->state[0];

	switch(state->func_state)
	{
		xcase STATE_SENDING_DATA:
		{
			checkinSendData(client,
							&state->checkin.i,
							&state->checkin.j,
							&state->checkin.file_mem,
							&state->checkin.curr_sent,
							&state->checkin.file_size,
							&state->checkin.total_bytes,
							&state->checkin.total_sent);
		}
		
		xcase STATE_SENT_DATA:
		{
			if(state->checkin.curr_sent == 0)
			{
				Packet * pak;
				int cmd;

				state->func_state = STATE_CHECKIN_FINISHING;
				cmd = (state->call_state == STATE_CHECKIN_FILES) ? PATCHCLIENT_FINISH_CHECKIN : PATCHCLIENT_FINISH_FORCEIN;
				pak = pktCreate(client->link, cmd);
				pktSendString(pak, state->checkin.comment);
				pktSend(&pak);
				LOGPACKET(client, cmd == PATCHCLIENT_FINISH_CHECKIN ? "PATCHCLIENT_FINISH_CHECKIN" : "PATCHCLIENT_FINISH_FORCEIN");
			}
		}
	}
}

static void checkinFilesResponse(PCL_Client * client, Packet * pak, int cmd)
{
	char * err_msg = NULL;
	int expected_cmd;
	U32 view_time = 0;
	StateStruct * state = client->state[0];
	PCL_CheckinCallback callback = state->checkin.callback;
	void * userData = state->checkin.userData;

	switch(state->func_state)
	{
	xcase STATE_SENT_NAMES:
		expected_cmd = (state->call_state == STATE_CHECKIN_FILES) ? PATCHSERVER_CHECKIN : PATCHSERVER_FORCEIN;
		if(cmd != expected_cmd)
		{
			REPORT_ERROR_STRING(client, PCL_UNEXPECTED_RESPONSE, "Expecting a response that the named were received.");
			pclStateRemoveHead(client);
		}
		else
		{
			int success = pktGetBits(pak,32);
			if (!success)
			{
				err_msg = pktGetStringTemp(pak);
				pcllog(client, PCLLOG_INFO, "checkin failed: %s", err_msg);
				REPORT_ERROR_STRING(client, PCL_CHECKIN_FAILED, err_msg);
				pclStateRemoveHead(client);
			}
			else
			{
				state->func_state = STATE_SENDING_DATA;
				state->checkin.i = 0;
				state->checkin.j = 0;
				state->checkin.file_mem = NULL;
				state->checkin.total_sent = 0;
				state->checkin.curr_sent = 0;
				checkinSendData(client, &state->checkin.i, &state->checkin.j, &state->checkin.file_mem, &state->checkin.curr_sent, &state->checkin.file_size,
					&state->checkin.total_bytes, &state->checkin.total_sent);
			}
		}
	xcase STATE_SENDING_DATA:
	acase STATE_SENT_DATA:
		if(cmd != PATCHSERVER_BLOCK_RECV)
		{
			REPORT_ERROR_STRING(client, PCL_UNEXPECTED_RESPONSE, "Expecting a block received message.");
			pclStateRemoveHead(client);
		}
		else
		{
			state->checkin.curr_sent -= pktGetBits(pak, 32);
		}
	xcase STATE_CHECKIN_FINISHING:
		expected_cmd = (state->call_state != STATE_FORCEIN_DIR) ? PATCHSERVER_FINISH_CHECKIN : PATCHSERVER_FINISH_FORCEIN;
		if(cmd != expected_cmd)
		{
			REPORT_ERROR_STRING(client, PCL_UNEXPECTED_RESPONSE, "Expecting a response that the checkin finished");
			pclStateRemoveHead(client);
		}
		else
		{
			int success = pktGetBits(pak, 32);
			view_time = pktGetBits(pak, 32);
			err_msg = pktGetStringTemp(pak);
			client->rev = pktGetBits(pak, 32); // FIXME: this is not entirely safe, since the manifest is the earlier rev
			if(!success)
			{
				REPORT_ERROR_STRING(client, PCL_CHECKIN_FAILED, err_msg);
				pcllog(client, PCLLOG_INFO, "checkin failed: %s", err_msg);
			}
			else
			{
				pclLoadHoggs(client);

				unmirrorFilesAndFinishCheckin(client, state->checkin.fnamesOnDisk, state->checkin.fnamesForUpload,
												state->call_state == STATE_FORCEIN_DIR);
			}
			pclStateRemoveHead(client);
		}
	}

	if(client->state[0] != state)
	{
		if(callback)
			callback(client->rev, view_time, client->error, client->error_details, userData);
	}
}

PCL_ErrorCode pclCheckinFileList(	PCL_Client *client,
									const char **dbnames,
									PCL_DiffType *diff_types,
									int count,
									const char *comment,
									bool force,
									PCL_CheckinCallback callback,
									void *userData)
{
	StateStruct *state = NULL;
	char **fnamesOnDisk = NULL, **fnamesForUpload = NULL, **unlock = NULL;
	int i;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);

	for(i = 0; i < count; i++)
	{
		if((diff_types[i] & PCLDIFFMASK_ACTION) == PCLDIFF_NOCHANGE)
		{
			if (diff_types[i] & PCLDIFF_NEWFILE) {
				// Undoing checkout on a new file, no server interaction needed
				char diskname[MAX_PATH];
				sprintf(diskname, "%s/%s", client->root_folder, dbnames[i]);
				pcllog(client, PCLLOG_WARNING, "New file %s found while reverting directory, backing it up.", diskname);
				fileRenameToBak(diskname);
			} else {
				eaPush(&unlock, strdup(dbnames[i]));
			}
		}
		else
		{
			if((diff_types[i] & PCLDIFFMASK_ACTION) == PCLDIFF_DELETED)
			{
				eaPush(&fnamesOnDisk, NULL);
			}
			else
			{
				char diskname[MAX_PATH];
				sprintf(diskname, "%s/%s", client->root_folder, dbnames[i]);
				eaPush(&fnamesOnDisk, strdup(diskname));
			}
			eaPush(&fnamesForUpload, strdup(dbnames[i]));
		}
	}

	if(eaSize(&fnamesOnDisk))
	{
		state = calloc(1, sizeof(StateStruct));
		state->general_state = STATE_WAITING;
		state->call_state = (force ? STATE_FORCEIN_DIR : STATE_CHECKIN_FILES);
		state->func_state = STATE_NONE;
		state->checkin.comment = strdup(NULL_TO_EMPTY(comment));
		state->checkin.fnamesOnDisk = fnamesOnDisk;
		state->checkin.fnamesForUpload = fnamesForUpload;
		state->checkin.callback = callback;
		state->checkin.userData = userData;
		pclStateAdd(client, state);
	}
	else
	{
		// GGFIXME: callback?
		eaDestroy(&fnamesOnDisk);
		eaDestroy(&fnamesForUpload);
	}

	if(eaSize(&unlock))
	{
		state = calloc(1, sizeof(StateStruct));
		state->general_state = STATE_WAITING;
		state->call_state = STATE_UNLOCK_FILES;
		state->func_state = STATE_NONE;
		state->unlock_files.revert = !force;
		state->unlock_files.files = unlock;
		pclStateAdd(client, state);
	}
	else
	{
		eaDestroy(&unlock);
	}

	if(state)
	{
		resetAuthor(client);
		resetView(client);

		if(client->view_status == VIEW_SET && client->author_status == VIEW_SET)
		{
			if(state->call_state == STATE_UNLOCK_FILES)
			{
				unlockReqSendPacket(client);
			}
			else
			{
				checkinSendNames(client, force ? PATCHCLIENT_REQ_FORCEIN : PATCHCLIENT_REQ_CHECKIN);
			}
			CHECK_ERROR_PI(client);
		}
	}

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

PCL_ErrorCode checkinFilesEx(	PCL_Client * client,
								const char*const* dirNames,
								const char*const* countsAsDir,
								const int* recurse,
								int count,
								const char*const* hide_paths,
								int hide_count,
								const char* comment,
								bool force,
								bool matchesonly,
								PCL_CheckinCallback callback,
								void* userData)
{
	char **unlock = NULL;
	StateStruct *state = calloc(1, sizeof(StateStruct));
	state->general_state = STATE_WAITING;
	state->call_state = (force ? STATE_FORCEIN_DIR : STATE_CHECKIN_FILES);
	state->func_state = STATE_NONE;
	state->checkin.comment = strdup(NULL_TO_EMPTY(comment));
	s_getDiffList(	client,
					dirNames,
					countsAsDir,
					recurse,
					count,
					hide_paths,
					hide_count,
					!matchesonly,
					force,
					false,
					false,
					&state->checkin.fnamesOnDisk,
					&state->checkin.fnamesForUpload,
					NULL,
					&unlock);
	state->checkin.callback = callback;
	state->checkin.userData = userData;
	pclStateAdd(client, state);

	if(unlock)
	{
		state = calloc(1, sizeof(StateStruct));
		state->general_state = STATE_WAITING;
		state->call_state = STATE_UNLOCK_FILES;
		state->func_state = STATE_NONE;
		state->unlock_files.revert = !force;
		state->unlock_files.files = unlock;
		pclStateAdd(client, state);
	}

	resetAuthor(client);
	resetView(client);

	if(client->view_status == VIEW_SET && client->author_status == VIEW_SET)
	{
		if(state->call_state == STATE_UNLOCK_FILES)
		{
			unlockReqSendPacket(client);
		}
		else
		{
			checkinSendNames(client, force ? PATCHCLIENT_REQ_FORCEIN : PATCHCLIENT_REQ_CHECKIN);
		}
		CHECK_ERROR(client);
	}

	return PCL_SUCCESS;
}

PCL_ErrorCode pclCheckInFiles(PCL_Client * client, const char ** dirNames, const char ** countsAsDir, int * recurse, int count,
							  const char ** hide_paths, int hide_count, const char * comment,
							  PCL_CheckinCallback callback, void * userData)
{
	PCL_ErrorCode error;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);

	error = checkinFilesEx(client, dirNames, countsAsDir, recurse, count, hide_paths, hide_count, comment, false, true, callback, userData);
	PERFINFO_AUTO_STOP_FUNC();
	return error;
}

PCL_ErrorCode pclForceInFiles(	PCL_Client * client,
								const char*const* dirNames,
								const char*const* countsAsDir,
								const int* recurse,
								int count,
								const char*const* hide_paths,
								int hide_count,
								const char* comment,
								bool matchesonly,
								PCL_CheckinCallback callback,
								void * userData)
{
	PCL_ErrorCode error;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);

	error = checkinFilesEx(	client,
							dirNames,
							countsAsDir,
							recurse,
							count,
							hide_paths,
							hide_count,
							comment,
							true,
							matchesonly,
							callback,
							userData);
	PERFINFO_AUTO_STOP_FUNC();
	return error;
}

static void lockFileResponse(PCL_Client * client, Packet * pak, int cmd)
{
	int i;
	char * err_msg = NULL;
	StateStruct * state = client->state[0];

	if(cmd != PATCHSERVER_LOCK || state->func_state != STATE_LOCK_REQ)
	{
		REPORT_ERROR_STRING(client, PCL_UNEXPECTED_RESPONSE, "Expecting a response about a lock attempt.");
	}
	else
	{
		int success = pktGetBits(pak, 32);
		if(!success)
		{
			PCL_LockCallback callback = state->lock_file.callback;
			void *userData = state->lock_file.userData;

			err_msg = pktGetStringTemp(pak);
			pcllog(client, PCLLOG_INFO, "lock failed: %s", err_msg);
			REPORT_ERROR_STRING(client, PCL_LOCK_FAILED, err_msg);

			pclStateRemoveHead(client);
			if(callback)
				callback(client->error, client->error_details, userData);
		}
		else if(!client->db)
		{
			REPORT_ERROR(client, PCL_MANIFEST_NOT_LOADED);
		}
		else if(!client->filespec)
		{
			REPORT_ERROR(client, PCL_FILESPEC_NOT_LOADED);
		}
		else
		{
			char **fnames = NULL, **mirror = NULL, **deleted = NULL;
			state->func_state = STATE_GETTING_LOCKED_FILE; // leave this state on, to adjust file flags and callback
			pktGetStringTemp(pak); // Error message for a non-error, ignore
			if(!pktEnd(pak))
			{
				for(i = 0; i < eaSize(&state->lock_file.files); i++)
					ea32Push(&state->lock_file.checksums, pktGetU32(pak));
			}

			pclLoadHoggs(client);
			for(i = 0; i < eaSize(&state->lock_file.files); i++)
			{
				DirEntry *dir = patchFindPath(client->db, state->lock_file.files[i], 0);
				if(dir)
				{
					patchSetLockedbyClient(client->db, dir, client->author);
					addToFileList(	client,
									dir,
									-1,
									true,
									&fnames,
									&mirror,
									&state->lock_file.touch,
									&deleted,
									dir->path,
									state->lock_file.filespec);
				}
				else
					REPORT_ERROR(client, PCL_COULD_NOT_START_LOCK_XFER);
			}
			if(eaSize(&deleted))
			{
				REPORT_ERROR_STRING(client, PCL_INTERNAL_LOGIC_BUG, "Lock deleted files?");
				eaDestroy(&deleted);
			}
			pushGetFileList(client, &fnames, &mirror, &state->lock_file.touch, NULL, NULL);

			// TODO: this may be overkill, but it would ensure we get any checkins made after we set the view
			// setViewAllData(client, NULL, client->project, client->branch, client->sandbox, U32_MAX, true, NULL, NULL);
		}
	}
}

static void lockReqSendPacket(PCL_Client * client)
{
	StateStruct * state = client->state[0];
	int i;
	Packet *pak_out = pktCreate(client->link, PATCHCLIENT_REQ_LOCK);
	for(i = 0; i < eaSize(&state->lock_file.files); i++)
		pktSendString(pak_out, state->lock_file.files[i]);
	pktSend(&pak_out);
	LOGPACKET(client, "PATCHCLIENT_REQ_LOCK");

	state->func_state = STATE_LOCK_REQ;
}

static bool lockReqGetList(	PCL_Client *client,
							DirEntry *dir_entry,
							const char ***fnames,
							const char ***touch,
							S32 doReportOnError,
							const char* filespecBasePath,
							const PCLFileSpec* filespec)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	if(eaSize(&dir_entry->versions))
	{
		const char* subPath;
		
		filespecBasePath = NULL_TO_EMPTY(filespecBasePath);
		assert(strStartsWith(dir_entry->path, filespecBasePath));
		subPath = dir_entry->path + strlen(filespecBasePath);
		assert(	!filespecBasePath[0] ||
				!subPath[0] ||
				subPath[0] == '/');

		if(pclFileIsIncludedByFileSpec(subPath, dir_entry->path, filespec))
		{
			if(dir_entry->lockedby)
			{
				if(!eaSize(&dir_entry->versions))
				{
					REPORT_ERROR_STRING(client, PCL_INTERNAL_LOGIC_BUG, "A folder is checked out?");
					PERFINFO_AUTO_STOP_FUNC();
					return false;
				}
				else if(dir_entry->versions[0]->flags & FILEVERSION_DELETED)
				{
					REPORT_ERROR_STRING(client, PCL_INTERNAL_LOGIC_BUG, "A deleted file is checked out?");
					PERFINFO_AUTO_STOP_FUNC();
					return false;
				}
				else if(isLockedByClient(client, dir_entry))
				{
					// we already have it checked out, we need to ensure it's writeable but not touch the contents
					eaPush(touch, dir_entry->path);
					PERFINFO_AUTO_STOP_FUNC();
					return true;
				}
 				else
 				{
 					// someone else has it checked out, apparently this isn't an error for directories
					// JE: Still want to report the error, but continue and check out the rest of the files
 					//if(doReportOnError)
 					//{
						pcllog(client, PCLLOG_ERROR, "Unable to check out %s, currently checked out by %s",
							dir_entry->path, dir_entry->lockedby);
 						//REPORT_ERROR(client, PCL_LOCK_FAILED);
						((PCL_Client*)client)->error = PCL_LOCK_FAILED;
 						//return false;
 					//}
		 			
						PERFINFO_AUTO_STOP_FUNC();
 					return true;
 				}
				//return true;
			}
			
			if (!(dir_entry->versions[0]->flags &FILEVERSION_DELETED)) {
				eaPush(fnames, dir_entry->path);
				PERFINFO_AUTO_STOP_FUNC();
				return true;
			} else {
				// Checking out a deleted file, just make it writeable
				eaPush(touch, dir_entry->path);
				PERFINFO_AUTO_STOP_FUNC();
				return true;
			}
		}
	}

	for(i = 0; i < eaSize(&dir_entry->children); i++)
	{
		if(!lockReqGetList(	client,	
							dir_entry->children[i],
							fnames,
							touch,
							0,
							filespecBasePath,
							filespec))
		{
			return false;
		}
	}

	PERFINFO_AUTO_STOP_FUNC();

	return true;
}

S32 pclFileIsIncludedByFileSpec(const char* fileNameRelative,
								const char* fileNameAbsolute,
								const PCLFileSpec* filespec)
{
	const PCLFileSpecEntry*const* entries[2];

	PERFINFO_AUTO_START_FUNC();

	if(!filespec){
		PERFINFO_AUTO_STOP_FUNC();
		return 1;
	}
	
	entries[0] = filespec->entriesFirst;
	entries[1] = filespec->entriesSecond;

	ARRAY_FOREACH_BEGIN(entries, j);
		S32 foundInclude = 0;

		EARRAY_CONST_FOREACH_BEGIN(entries[j], i, isize);
			const PCLFileSpecEntry* e = entries[j][i];
			const char*				spec = e->filespec;
			
			foundInclude |= e->doInclude;
			
			while(	spec[0] == '/' ||
					spec[0] == '\\')
			{
				spec++;
			}
			
			if(	e->isAbsolute &&
				fileNameAbsolute &&
				simpleMatch(spec, fileNameAbsolute)
				||
				!e->isAbsolute &&
				fileNameRelative &&
				simpleMatch(spec, fileNameRelative))
			{
				if(!e->doInclude){
					return 0;
				}
				foundInclude = 0;
				break;
			}
		EARRAY_FOREACH_END;
		
		if(foundInclude){
			PERFINFO_AUTO_STOP_FUNC();
			return 0;
		}
	ARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP_FUNC();
	
	return 1;
}

PCL_ErrorCode pclLockFiles(	PCL_Client* client,
							const char*const* fileNames,
							int count,
							PCL_LockCallback callback,
							void* userData,
							PCL_PreLockCallback preLockCallback,
							void* preLockUserData,
							const PCLFileSpec* filespec)
{
	int i;
	StateStruct * state;
	char **fnames = NULL, **touch = NULL;
	PCL_ErrorCode final_ret = PCL_SUCCESS;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);
	if(!client->db)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_MANIFEST_NOT_LOADED);
	if(!client->author)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_AUTHOR_FAILED);

	for(i = 0; i < count; i++)
	{
		DirEntry *dir_entry;
		char fileName[MAX_PATH];

		strcpy(fileName, fileNames[i]);
		forwardSlashes(fileName);
		dir_entry = patchFindPath(client->db, fileName, 0);

		if(!dir_entry) {
			// Doesn't exist, but we still want to set it writeable
			char onDiskPath[MAX_PATH];
			sprintf(onDiskPath, "%s/%s", client->root_folder, fileName);
			fwChmod(onDiskPath, _S_IREAD|_S_IWRITE); 
			continue;
		}

		if(!lockReqGetList(	client,
							dir_entry,
							&fnames,
							&touch,
							1,
							dir_entry->path,
							filespec))
		{
			// Handled below if all locks failed
		}
	}

	if (!eaSize(&fnames) && !eaSize(&touch))
	{
		eaDestroy(&fnames);
		eaDestroy(&touch);
		// Special handling of return value to prevent spam of messages, just return the error to the caller.
		if (client->error == PCL_LOCK_FAILED) {
			client->error = PCL_SUCCESS;
			return PCL_LOCK_FAILED;
		} else {
			return client->error; // set in lockReqGetList
		}
	}

	if (client->error == PCL_LOCK_FAILED) {
		// General success, but some locks failed, must clear flag, otherwise pclWait will not wait
		// Notify if only *some* locks failed.
		final_ret = PCL_LOCK_FAILED;
		client->error = PCL_SUCCESS;
	}
	
	if(preLockCallback){
		if(!preLockCallback(client, preLockUserData, fnames, eaSize(&fnames))){
			eaDestroy(&fnames);
			eaDestroy(&touch);
			client->error = PCL_SUCCESS;
			return PCL_LOCK_FAILED;
		}
	}

	if(eaSize(&fnames) || eaSize(&touch))
	{
		state = calloc(1, sizeof(StateStruct));
		state->lock_file.files = fnames;
		state->lock_file.touch = touch;
		state->lock_file.callback = callback;
		state->lock_file.userData = userData; 
		state->general_state = STATE_WAITING;
		state->call_state = STATE_LOCK_FILE;
		state->func_state = STATE_NONE;
		state->lock_file.filespec = StructClone(parse_PCLFileSpec, filespec);
		pclStateAdd(client, state);

		resetAuthor(client);
		resetView(client);

		if(client->view_status == VIEW_SET && client->author_status == VIEW_SET)
		{
			lockReqSendPacket(client);
		}
	}
	else
	{
		// This branch can't ever get hit, I think...
		if(callback)
			callback(PCL_SUCCESS, NULL, userData);
		eaDestroy(&fnames);
		eaDestroy(&touch);
	}

	PERFINFO_AUTO_STOP_FUNC();

	return final_ret;
}

static void unlockFileResponse(PCL_Client *client, Packet *pak_in, int cmd)
{
	int i;
	char * err_msg = NULL;
	StateStruct * state = client->state[0];

	if(cmd != PATCHSERVER_UNLOCK || state->func_state != STATE_UNLOCK_REQ)
	{
		REPORT_ERROR_STRING(client, PCL_UNEXPECTED_RESPONSE, "Expecting a response about an unlock attempt.");
	}
	else
	{
		int success = pktGetBits(pak_in, 32);
		if(!success)
		{
			err_msg = pktGetStringTemp(pak_in);
			pcllog(client, PCLLOG_INFO, "unlock failed: %s", err_msg);
			REPORT_ERROR_STRING(client, PCL_CHECKIN_FAILED, err_msg);
		}
		else if(!client->db)
		{
			REPORT_ERROR(client, PCL_MANIFEST_NOT_LOADED);
		}
		else if(!client->filespec)
		{
			REPORT_ERROR(client, PCL_FILESPEC_NOT_LOADED);
		}
		else
		{
			char **fnames = NULL, **mirror = NULL, **touch = NULL, **deleted = NULL;
			state->func_state = STATE_GETTING_UNLOCKED_FILE; // leave this state on, to callback once the GetFileList finishes

			pclLoadHoggs(client);
			for(i = 0; i < eaSize(&state->unlock_files.files); i++)
			{
				DirEntry *dir = patchFindPath(client->db, state->unlock_files.files[i], 0);
				if(dir)
				{
					patchSetLockedbyClient(client->db, dir, NULL);
					if(state->unlock_files.revert)
					{
						addToFileList(	client,
										dir,
										-1,
										false,
										&fnames,
										&mirror,
										&touch,
										&deleted,
										NULL,
										NULL);
					}
				}
				else
					REPORT_ERROR_STRING(client, PCL_INTERNAL_LOGIC_BUG, "File entry disappeared?");
			}
			if(eaSize(&deleted))
			{
				REPORT_ERROR_STRING(client, PCL_INTERNAL_LOGIC_BUG, "Unlocked deleted files?");
				eaDestroy(&deleted);
			}
			if(state->unlock_files.revert)
			{
				if(eaSize(&touch))
					REPORT_ERROR_STRING(client, PCL_INTERNAL_LOGIC_BUG, "Files still locked?");
				pushGetFileList(client, &fnames, &mirror, &touch, NULL, NULL);

				// TODO: this may be overkill, but it would ensure we get any checkins made after we set the view
				// setViewAllData(client, NULL, client->project, client->branch, client->sandbox, U32_MAX, true, NULL, NULL);
			}
		}
	}
}

static void unlockReqSendPacket(PCL_Client *client)
{
	StateStruct * state = client->state[0];
	int i;
	Packet *pak_out = pktCreate(client->link, PATCHCLIENT_REQ_UNLOCK);
	for(i = 0; i < eaSize(&state->unlock_files.files); i++)
		pktSendString(pak_out, state->unlock_files.files[i]);
	pktSend(&pak_out);
	LOGPACKET(client, "PATCHCLIENT_REQ_UNLOCK");
	state->func_state = STATE_UNLOCK_REQ;
}

static bool unlockReqGetList(PCL_Client *client, DirEntry *dir_entry, const char ***fnames)
{
	int i;

	if(isLockedByClient(client, dir_entry))
		eaPush(fnames, dir_entry->path);

	for(i = 0; i < eaSize(&dir_entry->children); i++)
		if(!unlockReqGetList(client, dir_entry->children[i], fnames))
			return false;

	return true;
}

PCL_ErrorCode pclUnlockFiles(PCL_Client *client, const char **fileNames, int count, PCL_LockCallback callback, void *userData)
{
	int i;
	StateStruct * state;
	char **fnames = NULL;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);
	if(!client->db)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_MANIFEST_NOT_LOADED);
	if(!client->author)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_AUTHOR_FAILED);

	for(i = 0; i < count; i++)
	{
		DirEntry *dir_entry;
		char fileName[MAX_PATH];

		strcpy(fileName, fileNames[i]);
		forwardSlashes(fileName);
		dir_entry = patchFindPath(client->db, fileName, 0);

		if(!dir_entry)
			continue;

		if(!unlockReqGetList(client, dir_entry, &fnames))
		{
			eaDestroy(&fnames);
			return client->error; // set in lockReqGetList
		}
	}

	if(count == 1 && !eaSize(&fnames))
		RETURN_ERROR_PERFINFO_STOP(client, PCL_FILE_NOT_FOUND);

	state = calloc(1, sizeof(StateStruct));
	state->general_state = STATE_WAITING;
	state->call_state = STATE_UNLOCK_FILES;
	state->func_state = STATE_NONE;
	state->unlock_files.revert = true;
	state->unlock_files.files = fnames;
	state->unlock_files.callback = callback;
	state->unlock_files.userData = userData;
	pclStateAdd(client, state);

	resetAuthor(client);
	resetView(client);

	if(client->view_status == VIEW_SET && client->author_status == VIEW_SET)
	{
		unlockReqSendPacket(client);
	}

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

static void notifyMeSendPacket(PCL_Client * client)
{
	StateStruct * state = client->state[0];
	Packet * pak;

	pak = pktCreate(client->link, PATCHCLIENT_REQ_NOTIFICATION);
	pktSend(&pak);
	LOGPACKET(client, "PATCHCLIENT_REQ_NOTIFICATION");

	state->func_state = STATE_NOTIFY_ME;
}

PCL_ErrorCode pclNotifyMe(PCL_Client * client, PCL_NotifyCallback callback, void * userData)
{
	StateStruct * state;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);

	state = calloc(1, sizeof(StateStruct));
	state->notify.callback = callback;
	state->notify.userData = userData;
	state->general_state = STATE_WAITING;
	state->call_state = STATE_NOTIFY_ME;
	state->func_state = STATE_NOTIFY_ME;
	pclStateAdd(client, state);

	notifyMeSendPacket(client);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

PCL_ErrorCode pclNotifyHalt(PCL_Client * client)
{
	Packet * pak;

	PERFINFO_AUTO_START_FUNC();
	
	CHECK_ERROR_PI(client);

	if(!(client->state[0]->general_state == STATE_WAITING && client->state[0]->call_state == STATE_NOTIFY_ME))
		RETURN_ERROR_PERFINFO_STOP(client, PCL_NOT_WAITING_FOR_NOTIFICATION);

	pak = pktCreate(client->link, PATCHCLIENT_REQ_NOTIFICATION_OFF);
	pktSend(&pak);
	LOGPACKET(client, "PATCHCLIENT_REQ_NOTIFICATION_OFF");

	pclStateRemoveHead(client);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

static void notifyResponse(PCL_Client * client, Packet * pak, int cmd)
{
	PCL_NotifyCallback callback = client->state[0]->notify.callback;
	void * userData = client->state[0]->notify.userData;

	if(cmd != PATCHSERVER_UPDATE)
	{
		REPORT_ERROR_STRING(client, PCL_UNEXPECTED_RESPONSE, "Client was expecting a server update response");
	}

	pclStateRemoveHead(client);

	if(callback)
		callback(client->error, client->error_details, userData);
}

static FileScanAction folderCachePrint(const char *dir, FolderNode *node, void *proc, void *userdata)
{
	pclMSpf("dir: %s", dir);
	return FSA_EXPLORE_DIRECTORY;
}

static void addToFolderCacheDir(PCL_Client * client, FolderCache * cache, DirEntry * dir_entry, int * index_to_vl)
{
	int i, created;
	char filename[MAX_PATH], * str, * location;
	char buf[MAX_PATH], hogg[MAX_PATH];
	bool addToHashtable = !!cache->htAllFiles;

	PERFINFO_AUTO_START_FUNC();

	// TODO: there shouldn't really be any asserts in here, but i'm tracking down a bug

	if(eaSize(&dir_entry->versions) && !(dir_entry->versions[0]->flags & FILEVERSION_DELETED) && fileSpecIsNotRequired(client->filespec, dir_entry->path))
	{
		FileVersion * ver = dir_entry->versions[0];
		FolderNode * node;

		strcpy(filename, dir_entry->path);
		forwardSlashes(filename);
		assertmsg(fileSpecFolderCount(client->filespec), "No root folders in filespec");
		i = fileSpecGetHoggIndexForFile(client->filespec, filename);
		if (i!=-1 && fileSpecGetHoggHandle(client->filespec, i))
		{
			str = fileSpecGetStripPath(client->filespec, i);
			stripPath(filename, str);

			node = FolderCacheQueryEx(cache, filename, false, false); // Caller has CS locked
			if(node)
			{
				if(node->virtual_location < 0)
				{
					if (g_PatchStreamingSimulate)
					{
						if (node->size == ver->size && timestampsMatch(node->timestamp, ver->modified))
						{
							assert(!client->got_all);
							node->needs_patching = 1;
							node->seen_in_fs = 1;
						}
					} else {
						location = FolderCache_GetFromVirtualLocation(cache, node->virtual_location);
						fileSpecGetHoggName(client->filespec, 0, i, SAFESTR(buf));
						STR_COMBINE_SS(hogg, buf, ":");
						if(stricmp(location, hogg) != 0)
						{
							FolderNodeDeleteFromTree(cache, node);
							node = NULL;
						}
						else if (node->size != ver->size || !timestampsMatch(node->timestamp, ver->modified))
						{
							assert(!client->got_all);
							node->needs_patching = 1;
							node->seen_in_fs = 1;
							node->size = ver->size;
							node->timestamp = ver->modified;
						}
					}
				}
			}

			// If it's not in the tree add it
			// If we're in simulate mode, and it's not on disk, we'll have nothing to load, simulate it doesn't exist
			if(!node && !g_PatchStreamingSimulate)
			{
				int vl;

				vl = index_to_vl[i];
				node = FolderNodeAdd(&cache->root, &cache->root_tail, NULL, filename, ver->modified, ver->size, vl, -1, &created, 0, 0);
				if(node)
				{
					HogFile * hogg_handle = fileSpecGetHoggHandle(client->filespec, i);
					HogFileIndex hfi;
					
					hfi = hogFileFind(hogg_handle, filename);
					
					if(hfi != HOG_INVALID_INDEX)
					{
						// Node didn't exist, but it's in the hogg, so this is
						//  probably a file deleted from disk in development mode,
						//  treat it as if it does not exist, 'cept re-use the file index
						node->file_index = hfi;
						if(1)// hogFileGetFileSize(hogg_handle, hfi) != node->size || 
							//!timestampsMatch(hogFileGetFileTimestamp(hogg_handle, hfi), node->timestamp) )
						{
							node->needs_patching = 1;
							node->seen_in_fs = 1;
						}
					}
					else
					{
						node->needs_patching = 1;
						node->seen_in_fs = 1;
					}
					if (addToHashtable)
					{
						FolderCacheHashTableAdd(cache, storeHashKeyStringLocal(filename), node);
					}
					if(created && cache->nodeCreateCallback)
					{
						cache->nodeCreateCallback(cache, node);
					}
				}
			}
		}
	}

	for(i = 0; i < eaSize(&dir_entry->children); i++)
	{
		addToFolderCacheDir(client, cache, dir_entry->children[i], index_to_vl);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

PCL_ErrorCode pclFixPath(PCL_Client * client, char * path, int buf_size)
{
	char * strip = NULL, * found, filename[MAX_PATH];
	const char * hogg_name;
	int i, hoggs;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);

	found = strrchr(path, ':');
	if(found)
	{
		found[0] = '\0';
		found += 2;
		strcpy(filename, found);
		found = strrchr(path, '/');
		if(found)
			found++;
		else
			found = path;

		hoggs = fileSpecHoggCount(client->filespec);
		for(i = 0; i < hoggs; i++)
		{
			hogg_name = fileSpecGetHoggNameNoPath(client->filespec, i);
			if(stricmp(found, hogg_name) == 0)
			{
				strip = fileSpecGetStripPath(client->filespec, i);
				break;
			}
		}
	} else {
		strcpy(filename, path);
	}

	fixPath(filename, strip);
	strcpy_s(path, buf_size, filename);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

PCL_ErrorCode pclAddFilesToFolderCache(PCL_Client * client, FolderCache * cache)
{
	int i, hoggs, piggs_in_cache, vl, j, found;
	char buf[MAX_PATH], hogg_path[MAX_PATH], * location;
	int * index_to_vl = NULL;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);

	if(!client->db)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_MANIFEST_NOT_LOADED);
	if(!client->filespec)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_FILESPEC_NOT_LOADED);
	if(!eaSize(&client->db->root.children))
		RETURN_ERROR_PERFINFO_STOP(client, PCL_NO_FILES_LOADED);

	FolderNodeEnterCriticalSection();

	pclMSpf("Current folder cache:");
	FolderNodeRecurseEx(cache->root, folderCachePrint, NULL, NULL, "", false, false);

	assertmsg(fileSpecFolderCount(client->filespec), "No root folders in filespec");

	piggs_in_cache = FolderCache_GetNumPiggs(cache);
	hoggs = fileSpecHoggCount(client->filespec);
	for(i = 0; i < hoggs; i++)
	{
		if (fileSpecGetHoggHandle(client->filespec, i))
		{
			fileSpecGetHoggName(client->filespec, 0, i, SAFESTR(buf));

			STR_COMBINE_SS(hogg_path, buf, ":");
			found = false;
			for(j = 0; j < piggs_in_cache; j++)
			{
				location = FolderCache_GetFromVirtualLocation(cache, PIG_INDEX_TO_VIRTUAL_LOCATION(j));
				if(stricmp(hogg_path, location) == 0)
				{
					eaiPush(&index_to_vl, PIG_INDEX_TO_VIRTUAL_LOCATION(j));
					found = true;
					break;
				}
			}
			if(!found)
			{
				// Add hogg to folder cache
				HogFile *hog_file = hogFileRead(buf, NULL, PIGERR_PRINTF, NULL, HOG_DEFAULT);

				vl = PIG_INDEX_TO_VIRTUAL_LOCATION(piggs_in_cache);
				FolderCache_SetFromVirtualLocation(cache, vl, strdup(hogg_path), NULL, false); // PCL does not care about core
				PigSetAdd(hog_file);
				eaiPush(&index_to_vl, vl);
				piggs_in_cache++;
			}
		}
	}

	pclMSpf("Current folder cache:");
	FolderNodeRecurseEx(cache->root, folderCachePrint, NULL, NULL, "", false, false);

	addToFolderCacheDir(client, cache, &client->db->root, index_to_vl);

	eaiDestroy(&index_to_vl);

	FolderNodeLeaveCriticalSection();

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

struct CountCheckoutsUserData
{
	PCL_Client *client;
	int *count;
};

static void countCheckoutsCB(DirEntry *dir, struct CountCheckoutsUserData *userdata)
{
	if(isLockedByClient(userdata->client, dir))
		++*userdata->count;
}

PCL_ErrorCode pclCountCheckouts(PCL_Client *client, int *count)
{
	struct CountCheckoutsUserData userdata = { client, count };

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);
	if(!count)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_NULL_PARAMETER);
	if(!client->db)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_MANIFEST_NOT_LOADED);

	patchForEachDirEntry(client->db, countCheckoutsCB, &userdata);

	PERFINFO_AUTO_STOP_FUNC();

	return client->error;
}

struct GetCheckoutsUserData
{
	PCL_Client *client;
	char ***checkouts;
};

static void getCheckoutsCB(DirEntry *dir, struct GetCheckoutsUserData *userdata)
{
	if(isLockedByClient(userdata->client, dir))
		eaPush(userdata->checkouts, strdup(dir->path));
}

PCL_ErrorCode pclGetCheckouts(PCL_Client *client, char ***checkouts)
{
	struct GetCheckoutsUserData userdata = { client, checkouts };

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);
	if(!checkouts)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_NULL_PARAMETER);
	if(!client->db)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_MANIFEST_NOT_LOADED);

	patchForEachDirEntry(client->db, getCheckoutsCB, &userdata);

	PERFINFO_AUTO_STOP_FUNC();

	return client->error;
}

PCL_ErrorCode pclExistsInDb(PCL_Client *client, const char *filename, bool *exists)
{
	char *fixedname;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);
	if(!filename || !exists)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_NULL_PARAMETER);
	if(!client->db)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_MANIFEST_NOT_LOADED);

	strdup_alloca(fixedname, filename);
	forwardSlashes(fixedname);

	*exists = patchFindPath(client->db, fixedname, 0) != NULL;

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

PCL_ErrorCode pclIsDeleted(PCL_Client *client, const char *filename, bool *deleted)
{
	char *fixedname;
	DirEntry *dir;
	*deleted = false;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);
	if(!filename || !deleted)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_NULL_PARAMETER);
	if(!client->db)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_MANIFEST_NOT_LOADED);

	strdup_alloca(fixedname, filename);
	forwardSlashes(fixedname);

	dir = patchFindPath(client->db, fixedname, 0);

	// If this is a directory, attempt a recursive search for any non-deleted files to determine if the directory has been deleted.
	if (dir && !eaSize(&dir->versions))
	{
		bool nondeleted_file_exists = false;
		EARRAY_CONST_FOREACH_BEGIN(dir->children, i, n);
		{
			pclIsDeleted(client, dir->children[i]->path, deleted);
			if (!*deleted)
			{
				nondeleted_file_exists = true;
				break;
			}
		}
		EARRAY_FOREACH_END;
		*deleted = !nondeleted_file_exists;
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_SUCCESS;
	}

	*deleted = dir && eaSize(&dir->versions) && (dir->versions[0]->flags & FILEVERSION_DELETED);
	PERFINFO_AUTO_STOP_FUNC();
	return PCL_SUCCESS;
}

PCL_ErrorCode pclIsUnderSourceControl(PCL_Client *client, const char *dbname, bool *under_control)
{
	char *fixedname;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);
	if(!dbname || !under_control)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_NULL_PARAMETER);
	if(!client->filespec)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_FILESPEC_NOT_LOADED);

	strdup_alloca(fixedname, dbname);
	forwardSlashes(fixedname);

	*under_control = fileSpecIsUnderSourceControl(client->filespec, fixedname);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

// Is this file considered to be "included" in the filespec?
PCL_ErrorCode pclIsIncluded(SA_PARAM_NN_VALID PCL_Client *client, SA_PARAM_NN_STR const char *dbname, bool *is_included)
{
	char *fixedname;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);
	if(!dbname || !is_included)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_NULL_PARAMETER);
	if(!client->filespec)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_FILESPEC_NOT_LOADED);

	strdup_alloca(fixedname, dbname);
	forwardSlashes(fixedname);

	*is_included = fileSpecIsIncluded(client->filespec, fixedname);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

// Debug command for testing if a file is marked as required.
PCL_ErrorCode pclIsNotRequired(PCL_Client *client, const char *filename, bool *required, char **estrDebug)
{
	PERFINFO_AUTO_START_FUNC();

	// Validate parameters.
	if (!client)
		return PCL_CLIENT_PTR_IS_NULL;
	if (!required)
		return PCL_NULL_PARAMETER;
	if (!filename)
		return PCL_NULL_PARAMETER;
	if (!client->filespec)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_FILESPEC_NOT_LOADED);

	// Check for NotRequired.
	*required = fileSpecIsNotRequiredDebug(client->filespec, filename, estrDebug);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

PCL_ErrorCode pclGetAuthorUnsafe(PCL_Client *client, const char *dbname, const char **author)
{
	DirEntry *dir;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);
	if(!dbname || !author)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_NULL_PARAMETER);
	if(!client->db)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_MANIFEST_NOT_LOADED);

	dir = patchFindPath(client->db, dbname, 0);
	if(!dir)
	{
		*author = NULL;
		return PCL_FILE_NOT_FOUND;
	}

	*author = dir->author;

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

PCL_ErrorCode pclGetLockAuthorUnsafe(PCL_Client *client, const char *dbname, const char **author)
{
	DirEntry *dir;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);
	if(!dbname || !author)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_NULL_PARAMETER);
	if(!client->db)
		RETURN_ERROR_PERFINFO_STOP(client, PCL_MANIFEST_NOT_LOADED);

	dir = patchFindPath(client->db, dbname, 0);
	if(!dir)
	{
		*author = NULL;
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_FILE_NOT_FOUND;
	}
	*author = dir->lockedby;

	PERFINFO_AUTO_STOP_FUNC();
	return PCL_SUCCESS;
}

PCL_ErrorCode pclSetForceInScanCallback(PCL_Client * client, PCL_ForceInScanCallback callback, void * userData)
{
	CHECK_ERROR(client);

	client->forceInScanCallback = callback;
	client->forceInScanUserData = userData;

	return PCL_SUCCESS;
}

PCL_ErrorCode pclSetUploadCallback(PCL_Client * client, PCL_UploadCallback callback, void * userData)
{
	CHECK_ERROR(client);

	client->uploadCallback = callback;
	client->uploadUserData = userData;

	return PCL_SUCCESS;
}

PCL_ErrorCode pclSetProcessCallback(PCL_Client * client, PCL_ProcessCallback callback, void * userData)
{
	CHECK_ERROR(client);

	client->processCallback = callback;
	client->processUserData = userData;

	return PCL_SUCCESS;
}

PCL_ErrorCode pclGetStateString(PCL_Client* client, char * buf, size_t buf_size)
{
	sprintf_s(	SAFESTR2(buf),
				"%s, %s, %s",
				client && eaSize(&client->state) ? pclStateGetName(client->state[0]->general_state) : "none",
				client && eaSize(&client->state) ? pclStateGetName(client->state[0]->call_state) : "none",
				client && eaSize(&client->state) ? pclStateGetName(client->state[0]->func_state) : "none");
	
	return PCL_SUCCESS;
}

PCL_ErrorCode pclGetErrorString(PCL_ErrorCode error_code, char * buf, size_t buf_size)
{
	const char * error_str;
	PCL_ErrorCode ret = error_code;

	switch(error_code)
	{
	xcase PCL_SUCCESS:
		error_str = "Success";
	xcase PCL_XFERS_FULL:
		error_str = "Current transfer queue is full";
	xcase PCL_FILE_NOT_FOUND:
		error_str = "File not found";
	xcase PCL_UNEXPECTED_RESPONSE:
		error_str = "Unexpected response from server";
	xcase PCL_INVALID_VIEW:
		error_str = "Attempted view is invalid";
	xcase PCL_FILESPEC_NOT_LOADED:
		error_str = "The filespec is not loaded";
	xcase PCL_MANIFEST_NOT_LOADED:
		error_str = "The manifest is not loaded";
	xcase PCL_NOT_IN_FILESPEC:
		error_str = "A file did not match any filespec";
	xcase PCL_NULL_HOGG_IN_FILESPEC:
		error_str = "A hogg in the filespec is null";
	xcase PCL_NAMING_FAILED:
		error_str = "The attempt to name a view has failed";
	xcase PCL_AUTHOR_FAILED:
		error_str = "The attempt to set an author has failed";
	xcase PCL_CHECKIN_FAILED:
		error_str = "The checkin attempt has failed";
	xcase PCL_LOCK_FAILED:
		error_str = "The attempt to lock a file has failed";
	xcase PCL_LOST_CONNECTION:
		error_str = "The connection to the server was lost";
	xcase PCL_WAITING:
		error_str = "The client is currently working on another command";
	xcase PCL_TIMED_OUT:
		error_str = "The connection was idle for too long";
	xcase PCL_NO_VIEW_FILE:
		error_str = "No file was found that could indicate the current view";
	xcase PCL_DLL_NOT_LOADED:
		error_str = "Patch DLL not loaded";
	xcase PCL_NO_CONNECTION_FILE:
		error_str = "No file that contains connection parameters was found";
	xcase PCL_DIALOG_MESSAGE_ERROR:
		error_str = "A dialog box was passed a bad message";
	xcase PCL_HEADER_NOT_UP_TO_DATE:
		error_str = "The stored header for the file is not up to date";
	xcase PCL_HEADER_HOGG_NOT_LOADED:
		error_str = "The header hogg is not loaded";
	xcase PCL_NOT_IN_ROOT_FOLDER:
		error_str = "The requested file was not in the specified root folder";
	xcase PCL_CLIENT_PTR_IS_NULL:
		error_str = "The client pointer passed in for the patcher was null";
	xcase PCL_NO_ERROR_STRING:
		error_str = "No error string was found for some other error, and you looked up this one too";
	xcase PCL_NULL_PARAMETER:
		error_str = "Some parameter passed into a patchclientlib function was NULL";
	xcase PCL_STRING_BUFFER_SIZE_TOO_SMALL:
		error_str = "The buffer provided for a string was too small";
	xcase PCL_DLL_FUNCTION_NOT_LOADED:
		error_str = "The specific function called was not loaded from the Patch DLL";
	xcase PCL_COMM_LINK_FAILURE:
		error_str = "The net comm was not able to create a link for the patcher";
	xcase PCL_NO_FILES_LOADED:
		error_str = "The patch client has an empty manifest";
	xcase PCL_HOGG_READ_ERROR:
		error_str = "A hog returned an error while trying to read";
	xcase PCL_COULD_NOT_START_LOCK_XFER:
		error_str = "A locked file could not start its transfer";
	xcase PCL_NO_RESPONSE_FUNCTION:
		error_str = "The patchclient got a message while it was in a state with no response function";
	xcase PCL_INVALID_PARAMETER:
		error_str = "Some parameter passed into a patchclientlib function was found to be invalid";
	xcase PCL_INTERNAL_LOGIC_BUG:
		error_str = "The programmer screwed up somewhere, and something that shouldn't happen did";
	xcase PCL_COULD_NOT_WRITE_LOCKED_FILE:
		error_str = "A file was locked that could not be open for writing";
	xcase PCL_NOT_WAITING_FOR_NOTIFICATION:
		error_str = "The client was not waiting for notification from the server";
	xcase PCL_HOGG_UNLOAD_FAILED:
		error_str = "The hoggs could not be unloaded";
	xcase PCL_BAD_PACKET:
		error_str = "The server reports that the client sent a bad packet to it";
	xcase PCL_DESTROYED:
		error_str = "The xfer has been destroyed";
	xcase PCL_ABORT:
		error_str = "The operation has been aborted";
	xcase PCL_FILESPEC_PROCESSING_FAILED:
		error_str = "Filespec processing failed";
	xdefault:
		error_str = "No error string was found for the specified error";
		ret = PCL_NO_ERROR_STRING;
	}

	if(buf == NULL)
		return PCL_NULL_PARAMETER;

	strncpy_s(buf, buf_size, error_str, _TRUNCATE); // get as much of the message as you can
	if(buf_size <= (int)strlen(error_str))
		return PCL_STRING_BUFFER_SIZE_TOO_SMALL;

	return ret;
}

// Get additional error information for last error, if any.
PCL_ErrorCode pclGetErrorDetails(PCL_Client* client, const char **error_details)
{
	if (!client)
		return PCL_CLIENT_PTR_IS_NULL;
	if (!error_details)
		return PCL_NULL_PARAMETER;

	*error_details = client->error_details;

	return PCL_SUCCESS;
}

PCL_ErrorCode pclGetFileFlags(SA_PARAM_NN_VALID PCL_Client * client, U32 *file_flags)
{
	CHECK_ERROR(client);
	if (!file_flags)
		return PCL_NULL_PARAMETER;;

	// Get file flags.
	*file_flags = client->file_flags;

	return PCL_SUCCESS;
}

PCL_ErrorCode pclSetFileFlags(PCL_Client * client, U32 flags)
{
	CHECK_ERROR(client);

	client->file_flags = flags;

	return PCL_SUCCESS;
}

PCL_ErrorCode pclAddFileFlags(PCL_Client * client, U32 flags)
{
	CHECK_ERROR(client);

	client->file_flags |= flags;
	return PCL_SUCCESS;
}

PCL_ErrorCode pclRemoveFileFlags(PCL_Client * client, U32 flags)
{
	CHECK_ERROR(client);

	client->file_flags &= ~flags;
	return PCL_SUCCESS;
}

PCL_ErrorCode pclGetCurrentBranch(	PCL_Client* client,
									S32* branchOut)
{
	PERFINFO_AUTO_START_FUNC();

	if(	!client ||
		!branchOut)
	{
		return PCL_NULL_PARAMETER;
	}
	
	*branchOut = client->branch;

	PERFINFO_AUTO_STOP_FUNC();
	
	return PCL_SUCCESS;
}

PCL_ErrorCode pclGetCurrentProject(	PCL_Client* client,
									char* projectOut,
									S32 projectOutLen)
{
	PERFINFO_AUTO_START_FUNC();

	if(	!client ||
		!projectOut)
	{
		return PCL_NULL_PARAMETER;
	}
	
	if(!client->project){
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_MANIFEST_NOT_LOADED;
	}
	
	strcpy_s(	projectOut,
				projectOutLen,
				client->project);

	PERFINFO_AUTO_STOP_FUNC();
	
	return PCL_SUCCESS;
}

PCL_ErrorCode pclSetMirrorCallback(PCL_Client * client, PCL_MirrorCallback callback, void * userData)
{
	CHECK_ERROR(client);

	client->mirrorCallback = callback;
	client->mirrorUserData = userData;

	return PCL_SUCCESS;
}

PCL_ErrorCode pclSetExamineCallback(PCL_Client * client, PCL_MirrorCallback callback, void * userData)
{
	CHECK_ERROR(client);

	client->examineCallback = callback;
	client->examineUserData = userData;

	return PCL_SUCCESS;
}

PCL_ErrorCode pclSetExamineCompleteCallback(PCL_Client * client, PCL_MirrorCallback callback, void * userData)
{
	CHECK_ERROR(client);

	client->examineCompleteCallback = callback;
	client->examineCompleteUserData = userData;

	return PCL_SUCCESS;
}

PCL_ErrorCode pclSetNetCallbacks(SA_PARAM_NN_VALID PCL_Client * client, PCL_NetCallback enter_callback, PCL_NetCallback leave_callback, void * userData)
{
	CHECK_ERROR(client);

	client->netEnterCallback = enter_callback;
	client->netLeaveCallback = leave_callback;
	client->netCallbackUserData = userData;

	return PCL_SUCCESS;
}

PCL_ErrorCode pclSetHoggsSingleAppMode(PCL_Client * client, bool singleAppMode)
{
	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);

	client->single_app_mode = singleAppMode;

	if (client->headers)
		hogFileSetSingleAppMode(client->headers, client->single_app_mode);

	if (client->filespec)
		fileSpecSetSingleAppMode(client->filespec, client->single_app_mode);
	
	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

static void getFileVersionInfoResponse(PCL_Client * client, Packet * pak_in, int cmd)
{
	if(!client)
	{
		REPORT_ERROR_STRING(NULL, PCL_INTERNAL_LOGIC_BUG, "getFileVersionInfoResponse called without a client");
		return;
	}

	if(cmd == PATCHSERVER_FILEVERSIONINFO)
	{
		StateStruct* state = client->state[0];

		if(!pktGetBool(pak_in))
		{
			if(state->fileversion_info.callback)
				state->fileversion_info.callback(client, state->fileversion_info.fname, 0, 0, 0, 0, 0, 0, NULL, NULL, state->fileversion_info.userdata);
		}
		else
		{
			U32 modifed = pktGetU32(pak_in);
			U32 size = pktGetU32(pak_in);
			U32 checksum = pktGetU32(pak_in);
			U32 header_size = pktGetU32(pak_in);
			U32 header_checksum = pktGetU32(pak_in);
			U32 flags = pktGetU32(pak_in);
			char *author = pktGetStringTemp(pak_in);
			char *lockedby = pktGetStringTemp(pak_in);

			if(state->fileversion_info.callback)
				state->fileversion_info.callback(client, state->fileversion_info.fname, modifed, size, checksum, header_size, header_checksum, flags, author, lockedby, state->fileversion_info.userdata);
		}
	}
	else
	{
		REPORT_ERROR_STRING(client, PCL_UNEXPECTED_RESPONSE, "Expecting fileversion info.");
	}

	pclStateRemoveHead(client);
}

static void getFileVersionInfoSendPacket(PCL_Client * client)
{
	StateStruct * state = client->state[0];
	Packet * pak;

	pak = pktCreate(client->link, PATCHCLIENT_REQ_FILEVERSIONINFO);
	pktSendString(pak, state->fileversion_info.fname);
	pktSend(&pak);
	LOGPACKET(client, "PATCHCLIENT_REQ_FILEVERSIONINFO");

	state->func_state = STATE_GET_FILEVERSION_INFO;
}

PCL_ErrorCode pclGetFileVersionInfo(PCL_Client * client, const char * fname, PCL_GetFileVersionInfoCallback callback, void *userdata)
{
	StateStruct * state;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);

	state = calloc(1, sizeof(StateStruct));
	state->fileversion_info.fname = strdup(fname);
	state->fileversion_info.callback = callback;
	state->fileversion_info.userdata = userdata;
	state->general_state = STATE_WAITING;
	state->call_state = STATE_GET_FILEVERSION_INFO;
	state->func_state = STATE_GET_FILEVERSION_INFO;
	pclStateAdd(client, state);

	getFileVersionInfoSendPacket(client);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

static void isCompletelySyncedResponse(PCL_Client *client, Packet *pak_in, int cmd)
{
	if (!client)
	{
		REPORT_ERROR_STRING(NULL, PCL_INTERNAL_LOGIC_BUG, "isCompletelySyncedResponse called without a client");
		return;
	}

	if (cmd == PATCHSERVER_IS_COMPLETELY_SYNCED_RESPONSE)
	{
		StateStruct *state = client->state[0];
		bool synced = pktGetBool(pak_in);
		bool exists = true;

		if (!pktEnd(pak_in))
			exists = pktGetBool(pak_in);

		if(state->is_completely_synced.callback)
			state->is_completely_synced.callback(client, synced, exists, state->is_completely_synced.userdata);
	}
	else
	{
		REPORT_ERROR_STRING(client, PCL_UNEXPECTED_RESPONSE, "Expecting sync info.");
	}

	pclStateRemoveHead(client);
}

static void isCompletelySyncedSendPacket(PCL_Client * client)
{
	StateStruct * state = client->state[0];
	Packet * pak;

	pak = pktCreate(client->link, PATCHCLIENT_REQ_COMPLETELY_SYNCED);
	pktSendString(pak, state->fileversion_info.fname);
	pktSend(&pak);
	LOGPACKET(client, "PATCHCLIENT_REQ_COMPLETELY_SYNCED");

	state->func_state = STATE_IS_COMPLETELY_SYNCED;
}

PCL_ErrorCode	pclIsCompletelySynced(PCL_Client *client, const char *path, PCL_IsCompletelySyncedCallback callback, void *userdata)
{
	StateStruct *state;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);

	state = calloc(1, sizeof(StateStruct));
	state->is_completely_synced.path = strdup(path);
	state->is_completely_synced.callback = callback;
	state->is_completely_synced.userdata = userdata;
	state->general_state = STATE_WAITING;
	state->call_state = STATE_IS_COMPLETELY_SYNCED;
	state->func_state = STATE_IS_COMPLETELY_SYNCED;
	pclStateAdd(client, state);

	isCompletelySyncedSendPacket(client);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

static void pclInjectFileVersionFromServerCallback(PCL_Client* client, const char *fname, U32 modified, U32 size, U32 checksum, U32 header_size, U32 header_checksum, U32 flags, char *author, char *lockedby, void *userdata)
{
	DirEntry *dir;
	FileVersion *ver;

	// Check if the file wasn't found
	if(modified == 0)
	{
		dir = patchFindPath(client->db, fname, false);
		if(dir)
			direntryRemoveAndDestroy(client->db, dir);
		if(client->verbose_logging)
			filelog_printf("gimme_nomanifest", "pclInjectFileVersionFromServer: Injecting %s deleted", fname);
		return;
	}

	dir = patchFindPath(client->db, fname, true);
	ver = calloc(1, sizeof(FileVersion));
	ver->modified = modified;
	ver->size = size;
	ver->checksum = checksum;
	ver->header_size = header_size;
	ver->header_checksum = header_checksum;
	ver->flags = flags;
	ver->parent = dir;
	eaClear(&dir->versions);
	eaPush(&dir->versions, ver);
	patchSetAuthorClient(client->db, dir, author);
	patchSetLockedbyClient(client->db, dir, lockedby);
	if(client->verbose_logging)
		filelog_printf("gimme_nomanifest", "pclInjectFileVersionFromServer: Injecting %s mtime=%u size=%u crc=%u", fname, modified, size, checksum);
}

PCL_ErrorCode pclInjectFileVersionFromServer(PCL_Client * client, const char * fname)
{
	PCL_ErrorCode error;

	PERFINFO_AUTO_START_FUNC();

	assert(client);
	if(!client->db)
	{
		client->db = patchCreateDb(client->file_flags & PCL_USE_POOLED_PATHS ? PATCHDB_POOLED_PATHS : PATCHDB_DEFAULT, NULL);
		client->db->author_stash = stashTableCreateWithStringKeys(10, StashDefault);
		client->db->author_strings = strTableCreate(StrTableDefault, 512);
	}
	error = pclGetFileVersionInfo(client, fname, pclInjectFileVersionFromServerCallback, NULL);

	PERFINFO_AUTO_STOP_FUNC();

	return error;
}

PCL_ErrorCode pclLoadHoggs(PCL_Client * client)
{
	char **root_folders = NULL;
	U32 *hogg_flags = NULL;

	PERFINFO_AUTO_START_FUNC();

	if(client->file_flags & PCL_IN_MEMORY)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_SUCCESS;
	}
	eaPush(&root_folders, client->root_folder);
	ea32Push(&hogg_flags, HOG_DEFAULT);
	if(client->extra_folders)
		eaPushEArray(&root_folders, &client->extra_folders);
	if(client->extra_flags)
		ea32PushArray(&hogg_flags, &client->extra_flags);
	fileSpecSetRoot(client->filespec, root_folders, hogg_flags);
	eaDestroy(&root_folders);
	ea32Destroy(&hogg_flags);
	fileSpecLoadHoggs(client->filespec);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

PCL_ErrorCode pclUnloadHoggs(PCL_Client * client)
{
	bool success;
	PCL_ErrorCode error;

	PERFINFO_AUTO_START_FUNC();
	if (!client)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_CLIENT_PTR_IS_NULL;
	}
	success = fileSpecUnloadHoggs(client->filespec);
	error = success ? PCL_SUCCESS : PCL_HOGG_UNLOAD_FAILED;

	PERFINFO_AUTO_STOP_FUNC();

	return error;
}

PCL_ErrorCode pclAddExtraFolder(PCL_Client * client, const char * path, U32 flags)
{
	PERFINFO_AUTO_START_FUNC();

	eaPush(&client->extra_folders, strdup(path));
	ea32Push(&client->extra_flags, flags);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

PCL_ErrorCode pclSetWriteFolder(PCL_Client * client, const char * path)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	i = eaFindString(&client->extra_folders, path);
	if(i == -1)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_INVALID_PARAMETER;
	}
	client->write_index = i + 1;

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

PCL_ErrorCode pclAddExtraFilespec(PCL_Client * client, const char * fname)
{
	PERFINFO_AUTO_START_FUNC();

	if(!client)
		return PCL_CLIENT_PTR_IS_NULL;
	if (!fname || !*fname)
	{
		free(client->extra_filespec_fname);
		client->extra_filespec_fname = NULL;
		return PCL_SUCCESS;
	}
	free(client->extra_filespec_fname);
	client->extra_filespec_fname = strdup(fname);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

// Process a filespec to evaluate TextParser includes and macros, and make sure the syntax is valid.
PCL_ErrorCode pclProprocessFilespec(const char * input_filename, const char * output_filename)
{
	bool success = fileSpecPreprocessFile(input_filename, output_filename);
	if (!success)
		return PCL_FILESPEC_PROCESSING_FAILED;
	return PCL_SUCCESS;
}

// TODO: Unify pcllog, patchmelog, pclMSpf, pclSendReport, pclSendLog(), and explicit printfs in some sane way.
PCL_ErrorCode pclSendLog_dbg(PCL_Client * client, const char *action, const char *fmt_str, ...)
{
	Packet * pak;
	char buf[4096];
	va_list ap;

	PERFINFO_AUTO_START_FUNC();

	if (client->state[0]->general_state == STATE_CONNECTING || !client->link || client->disconnected)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_LOST_CONNECTION;
	}

	pak = pktCreate(client->link, PATCHCLIENT_LOG);

	va_start(ap, fmt_str);
	vsprintf(buf, fmt_str, ap);
	va_end(ap);

	pktSendString(pak, action);
	pktSendString(pak, buf);
	pktSend(&pak);
	linkFlush(client->link);
	LOGPACKET(client, "PATCHCLIENT_LOG");

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

// Send HTTP statistics to the server.
PCL_ErrorCode pclSendLogHttpStats(PCL_Client *client)
{
	PCL_ErrorCode error;
	PERFINFO_AUTO_START_FUNC();

	if (!client)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_CLIENT_PTR_IS_NULL;
	}

	error = pclSendLog(client, "HttpPatchingStats",
		"total_bytes %"FORM_LL"d http_bytes %"FORM_LL"d http_errors %d",
		pclRealBytesReceived(client),
		pclHttpBytesReceived(client),
		client->xferrer ? client->xferrer->http_fails : 0);
	PERFINFO_AUTO_STOP_FUNC();
	return ERROR;
}

static void undoCheckinResponse(PCL_Client * client, Packet * pak_in, int cmd)
{
	if(!client)
	{
		REPORT_ERROR_STRING(NULL, PCL_INTERNAL_LOGIC_BUG, "undoCheckinResponse called without a client");
		return;
	}

	if(cmd == PATCHSERVER_FINISH_CHECKIN)
	{
		StateStruct* state = client->state[0];
		bool success = pktGetU32(pak_in);
		U32 time = pktGetU32(pak_in);
		char *error_message = pktGetStringTemp(pak_in);
		U32 rev = pktGetU32(pak_in);

		if(!success)
			REPORT_ERROR_STRING(client, PCL_CHECKIN_FAILED, error_message);
		
		if(state->undo_checkin.callback)
			state->undo_checkin.callback(client, success, error_message, rev, time, state->undo_checkin.userdata);
	}
	else
	{
		REPORT_ERROR_STRING(client, PCL_UNEXPECTED_RESPONSE, "Expecting finished checkin.");
	}

	pclStateRemoveHead(client);
}


static void undoCheckinSendPacket(PCL_Client * client)
{
	StateStruct * state = client->state[0];
	Packet * pak;

	pak = pktCreate(client->link, PATCHCLIENT_REQ_UNDO_CHECKIN);
	pktSendString(pak, state->undo_checkin.comment);
	pktSend(&pak);
	LOGPACKET(client, "PATCHCLIENT_REQ_UNDO_CHECKIN");

	state->func_state = STATE_UNDO_CHECKIN;
}


PCL_ErrorCode pclUndoCheckin(PCL_Client * client, const char *comment, PCL_UndoCheckinCallback callback, void *userdata)
{
	StateStruct * state;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);

	state = calloc(1, sizeof(StateStruct));
	state->undo_checkin.comment = strdup(NULL_TO_EMPTY(comment));
	state->undo_checkin.callback = callback;
	state->undo_checkin.userdata = userdata;
	state->general_state = STATE_WAITING;
	state->call_state = STATE_UNDO_CHECKIN;
	state->func_state = STATE_UNDO_CHECKIN;
	pclStateAdd(client, state);

	undoCheckinSendPacket(client);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

static void pingResponse(PCL_Client * client, Packet * pak_in, int cmd)
{
	if(!client)
	{
		REPORT_ERROR_STRING(NULL, PCL_INTERNAL_LOGIC_BUG, "pingResponse called without a client");
		return;
	}

	if(cmd == PATCHSERVER_PONG)
	{
		StateStruct* state = client->state[0];
		char *data = pktGetStringTemp(pak_in);

		if(stricmp(state->ping.data, data)!=0)
		{
			char *errmsg = strdupf("Incorrect ping response. Sent \"%s\", got \"%s\"", state->ping.data, data);
			REPORT_ERROR_STRING(client, PCL_UNEXPECTED_RESPONSE, errmsg);
			free(errmsg);
		}
		else if(state->ping.callback)
			state->ping.callback(client, state->ping.data, state->ping.userdata);
	}
	else
	{
		REPORT_ERROR_STRING(client, PCL_UNEXPECTED_RESPONSE, "Expecting pong.");
	}

	pclStateRemoveHead(client);
}

static void pingSendPacket(PCL_Client * client)
{
	StateStruct * state = client->state[0];
	Packet * pak;

	pak = pktCreate(client->link, PATCHCLIENT_PING);
	pktSendString(pak, state->ping.data);
	pktSend(&pak);
	LOGPACKET(client, "PATCHCLIENT_PING");

	state->func_state = STATE_UNDO_CHECKIN;
}

PCL_ErrorCode pclPing(PCL_Client * client, const char *data, PCL_PingCallback callback, void *userdata)
{
	StateStruct * state;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);

	state = calloc(1, sizeof(StateStruct));
	if(data)
		state->ping.data = strdup(data);
	else
		state->ping.data = strdupf("%"FORM_LL"u", time(NULL));
	state->ping.callback = callback;
	state->ping.userdata = userdata;
	state->general_state = STATE_WAITING;
	state->call_state = STATE_PING;
	state->func_state = STATE_PING;
	pclStateAdd(client, state);

	pingSendPacket(client);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

PCL_ErrorCode pclHeartbeat(PCL_Client * client, const char *name)
{
	Packet *pak;

	PERFINFO_AUTO_START_FUNC();

	pak = pktCreate(client->link, PATCHCLIENT_HEARTBEAT);
	pktSendString(pak, name);
	pktSend(&pak);
	LOGPACKET(client, "PATCHCLIENT_HEARTBEAT");

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

PCL_ErrorCode pclSetPrefix(PCL_Client * client, const char *prefix)
{
	Packet *pak;

	PERFINFO_AUTO_START_FUNC();

	pak = pktCreate(client->link, PATCHCLIENT_SET_PREFIX);
	pktSendString(pak, prefix);
	pktSend(&pak);
	LOGPACKET(client, "PATCHCLIENT_SET_PREFIX");
	client->prefix = strdup(prefix);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

	   
PCL_ErrorCode pclGetPrefix(PCL_Client * client, char * buf, size_t buf_size)
{
	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);

	if(buf == NULL)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_NULL_PARAMETER;
	}

	if (!client->prefix)
	{
		buf[0] = 0;
	}
	else
	{
		strncpy_s(buf, buf_size, client->prefix, _TRUNCATE); // get as much of the message as you can
		if(buf_size <= (int)strlen(client->prefix))
		{
			PERFINFO_AUTO_STOP_FUNC();
			return PCL_STRING_BUFFER_SIZE_TOO_SMALL;
		}
	}

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

PCL_ErrorCode pclSetRetryTimes(PCL_Client * client, F32 timeout, F32 backoff)
{
	if(timeout)
		client->retry_timeout = timeout;
	if(backoff)
		client->retry_backoff = backoff;
	return PCL_SUCCESS;
}

// Get the number of bytes remaining to download in the autoupdate packet, or zero if unknown.
PCL_ErrorCode pclGetAutoupdateRemaining(PCL_Client * client, U32 * remaining)
{
	CHECK_ERROR(client);
	if (!remaining)
		return PCL_NULL_PARAMETER;

	// This only works while autoupdating.
	if (client->state[0]->general_state != STATE_CONNECTING || client->state[0]->call_state != STATE_AUTOUPDATE) 
		return PCL_WAITING;

	*remaining = client->current_pak_bytes_waiting_to_recv;

	return PCL_SUCCESS;
}

PCL_ErrorCode pclVerifyServerTimeDifference(PCL_Client * client, bool * should_warn, U32 * client_time, U32 * server_time)
{
	U32 diff;
	CHECK_ERROR(client);
	CHECK_STATE(client);

	if (should_warn == NULL)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_NULL_PARAMETER;
	}
	if (client_time == NULL)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_NULL_PARAMETER;
	}
	if (server_time == NULL)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_NULL_PARAMETER;
	}

	*client_time = client->client_time;
	*server_time = client->server_time;

	// don't warn if the server didn't send a time for backwards compatibility
	if (*server_time)
	{
		diff = ABS_UNS_DIFF(*client_time, *server_time);
		if (diff > MAX_CLIENT_SERVER_TIME_DIFFERENCE)
		{
			*should_warn = 1;
		}
	}

	return PCL_SUCCESS;
}

// PCL reference data zipping function, to attempt to ensure consistency in zipping critical for bindiff performance.
PCL_ErrorCode pclZipData(void *src, U32 src_len, U32 *zip_size_p, char **zipped)
{
	*zipped = zipDataEx(src,
		src_len,
		zip_size_p,
		9,
		true,
		0);
	return *zipped ? PCL_SUCCESS : PCL_INTERNAL_LOGIC_BUG;
}

typedef struct ForEachInMemoryData
{
	PCL_Client *client;
	PCL_InMemoryIteratorCallback callback;
	void *userdata;
} ForEachInMemoryData;

static void forEachInMemoryCallback(DirEntry *dir, ForEachInMemoryData *data)
{
	FileVersion *ver = eaHead(&dir->versions);
	if(ver)
	{
		data->callback(data->client, dir->path, ver->in_memory_data, ver->size, ver->modified, data->userdata);
	}
}

PCL_ErrorCode pclForEachInMemory(SA_PARAM_NN_VALID PCL_Client *client, PCL_InMemoryIteratorCallback callback, void *userdata)
{
	ForEachInMemoryData data;

	PERFINFO_AUTO_START_FUNC();

	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);

	if(!(client->file_flags & PCL_IN_MEMORY))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_INVALID_PARAMETER;
	}

	data.client = client;
	data.callback = callback;
	data.userdata = userdata;
	patchForEachDirEntry(client->db, forEachInMemoryCallback, &data);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

// Get the newest file modified time for the latest version of all files in a directory and all its subdirectories.
U32 pclNewestModifiedRecursive(DirEntry *dir)
{
	U32 newest = 0;
	if (eaSize(&dir->versions))
	{
		FileVersion *ver;
		devassert(dir->versions);
		ver = eaTail(&dir->versions);
		devassert(ver);
		newest = ver->modified;
	}
	EARRAY_CONST_FOREACH_BEGIN(dir->children, i, n);
	{
		newest = MAX(newest, pclNewestModifiedRecursive(dir->children[i]));
	}
	EARRAY_FOREACH_END;
	return newest;
}

// Get the list of files in a path in the manifest; must be destroyed with free() and eaDestroy().
PCL_ErrorCode pclListFilesInDir(SA_PARAM_NN_VALID PCL_Client *client, SA_PARAM_NN_STR const char *path, SA_PARAM_OP_VALID char ***children,
								SA_PARAM_OP_VALID U32 **newest_modified, bool include_deleted)
{
	DirEntry *dir;
	int last = -1;

	PERFINFO_AUTO_START_FUNC();

	// Validate parameters.
	if (!path || !children)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_INVALID_PARAMETER;
	}
	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);
	if (!client->db)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_MANIFEST_NOT_LOADED;
	}

	// Look up path.
	dir = patchFindPath(client->db, path, 0);
	if (!dir)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_FILE_NOT_FOUND;
	}

	// Copy children.
	eaSetSize(children, eaSize(&dir->children));
	devassert(*children);
	if (newest_modified)
		ea32SetSize(newest_modified, eaSize(&dir->children));
	EARRAY_CONST_FOREACH_BEGIN(dir->children, i, n);
	{
		bool deleted;
		PCL_ErrorCode error;
		if (include_deleted)
			deleted = false;
		else
		{
			error = pclIsDeleted(client, dir->children[i]->path, &deleted);
			devassert(error == PCL_SUCCESS);
		}
		if (!deleted)
		{
			++last;
			(*children)[last] = strdup(dir->children[i]->name);
			if (newest_modified)
				(*newest_modified)[last] = pclNewestModifiedRecursive(dir->children[i]);
		}
	}
	EARRAY_FOREACH_END;
	eaSetSize(children, last + 1);
	if (newest_modified)
		ea32SetSize(newest_modified, last + 1);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

// Recursively find the largest file size.
U32 pclGetLargestFileSizeInternal(DirEntry *dir)
{
	U32 largest = 0;
	EARRAY_CONST_FOREACH_BEGIN(dir->versions, i, n);
	{
		largest = MAX(largest, dir->versions[i]->size);
	}
	EARRAY_FOREACH_END;
	EARRAY_CONST_FOREACH_BEGIN(dir->children, i, n);
	{
		largest = MAX(largest, pclGetLargestFileSizeInternal(dir->children[i]));
	}
	EARRAY_FOREACH_END;
	return largest;
}

// Find the size of the largest file in the manifest.
PCL_ErrorCode pclGetLargestFileSize(SA_PARAM_NN_VALID PCL_Client *client, U32 *size)
{
	PERFINFO_AUTO_START_FUNC();

	// Validate parameters.
	if (!size)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_INVALID_PARAMETER;
	}
	CHECK_ERROR_PI(client);
	CHECK_STATE_PI(client);
	if (!client->db)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_MANIFEST_NOT_LOADED;
	}

	// Find the largest size.
	*size = pclGetLargestFileSizeInternal(&client->db->root);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

// Enable HTTP on a client.
PCL_ErrorCode	pclSetHttp(SA_PARAM_NN_VALID PCL_Client *client, const char *server, U16 port, const char *prefix)
{
	PERFINFO_AUTO_START_FUNC();

	if (!server || !*server || !port)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_INVALID_PARAMETER;
	}
	CHECK_ERROR_PI(client);

	pcllog(client, PCLLOG_INFO, "Using HTTP patching: server %s port %u prefix %s", server, port, prefix);

	// Set HTTP parameters on xferrer.
	client->xferrer->use_http = true;
	client->xferrer->http_server = strdup(server);
	client->xferrer->http_port = port;
	if (prefix)
		client->xferrer->path_prefix = strdup(prefix);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

// Get the server that we're currently connected to.
PCL_ErrorCode pclGetConnectedServer(SA_PARAM_NN_VALID PCL_Client *client, const char **server)
{
	CHECK_ERROR(client);
	if (!server)
		return PCL_NULL_PARAMETER;
	if (!client->server)
		return PCL_LOST_CONNECTION;

	*server = client->server;

	return PCL_SUCCESS;
}

// Get the general status of the PCL link, for reporting and debugging purposes.
PCL_ErrorCode pclGetLinkConnectionStatus(SA_PARAM_NN_VALID PCL_Client *client, PCL_ConnectionStatus *status)
{
	CHECK_ERROR(client);
	if (!status)
		return PCL_NULL_PARAMETER;
	
	if (client->disconnected)
		*status = PCL_CONNECTION_DISCONNECTED;
	else if (client->state[0]->general_state == STATE_CONNECTING)
	{
		if (client->link && linkConnected(client->link))
			*status = PCL_CONNECTION_NEGOTIATION;
		else if (client->was_ever_unexpectedly_disconnected)
			*status = PCL_CONNECTION_RECONNECTING;
		else
			*status = PCL_CONNECTION_INITIAL_CONNECT;
	}
	else if (client->view_status == VIEW_SET)
		*status = PCL_CONNECTION_VIEW_SET;
	else
		*status = PCL_CONNECTION_CONNECTED;

	return PCL_SUCCESS;
}

// Set idle to true if the client is in the idle state.
PCL_ErrorCode pclIsIdle(PCL_Client *client, bool *idle)
{
	CHECK_ERROR(client);
	if (!idle)
		return PCL_NULL_PARAMETER;

	*idle = client->state[0]->general_state == STATE_IDLE;

	return PCL_SUCCESS;
}

// Set idle to true if the client is in the idle state.
PCL_ErrorCode pclStartStatusReporting(PCL_Client *client, const char *id, const char *host, U16 port, bool bCritical)
{
	PCLStatusReportee *reportee;

	PERFINFO_AUTO_START_FUNC();

	// Check parameters.
	if (!client)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_CLIENT_PTR_IS_NULL;
	}
	if (!id || !*id || !host || !*host)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_NULL_PARAMETER;
	}
	
	// Add to status reporting list.
	reportee = calloc(1, sizeof(*reportee));
	reportee->id = strdup(id);
	reportee->host = strdup(host);
	reportee->port = port ? port : DEFAULT_MACHINESTATUS_PATCHSTATUS_PORT;
	reportee->critical = bCritical;
	eaPush(&client->status_reporting, reportee);

	// Start connecting.
	pclStartReporting(client);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

// Abort the current operation.
PCL_ErrorCode pclAbort(PCL_Client *client)
{
	CHECK_ERROR(client);

	// WARNING:
	// There's a thread-safety risk here, since the abort may realistically come from an arbitrary thread while
	// PCL is in the middle of processing.  This should be better, but that would complicate the design,
	// and currently, the requirements for this function are fairly limited; it just has to work
	// some sort of a way in the usual cases.
	client->error = PCL_ABORT;

	return PCL_SUCCESS;
}

void pclGetUsefulDebugString(PCL_Client *client, char **ppOutDebugString)
{
	if (!client)
	{
		estrPrintf(ppOutDebugString, "Client is NULL");
		return;
	}

	estrPrintf(ppOutDebugString, "Proj: %s. Server: %s. Sandbox: %s. Branch: %d. Rev: %d. Prefix: %s. Author: %s.",
		client->project, client->server, client->sandbox, client->branch, client->rev, client->prefix, client->author);
}

char *pclGetUsefulDebugString_Static(PCL_Client *client)
{
	static char *pRetVal = NULL;
	pclGetUsefulDebugString(client, &pRetVal);
	return pRetVal;
}

// If true, disable automatic redirection requests from server.
bool pclNoRedirect(bool enabled)
{
	bool old = g_no_redirect;
	g_no_redirect = !!enabled;
	return old;
}

// If true, ignore HTTP patching information from the server.
bool pclNoHttp(bool enabled)
{
	bool old = g_no_http;
	g_no_http = !!enabled;
	return old;
}

// The PCL xferrer has hung; report this to the Patch Server before we reset.
PCL_ErrorCode pclReportResetAfterHang(PCL_Client * client, F32 elapsed)
{
	PatchProcessStats stats = {0};
	char *debug = NULL;
	char *logline = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (!client)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_CLIENT_PTR_IS_NULL;
	}

	// Create debug information.
	fillPatchProcessStats(&stats, client, 0);
	ParserWriteText(&debug, parse_PatchProcessStats, &stats, 0, 0, 0);
	logAppendPairs(&logline,
		logPair("elapsed", "%f", elapsed),
		logPair("debug", "%s", debug),
		NULL);
	estrDestroy(&debug);

	// Report problems.
	pcllog(client, PCLLOG_INFO, "xferrer resetting after apparent hang.\n");
	pclSendLog(client, "ResetAfterHang", "%s", logline);
	ErrorDetailsf("%s", logline);
	Errorf("xferrer resetting after apparent hang.");
	estrDestroy(&logline);

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

// Set a directory to write failed checksum verification data to.
PCL_ErrorCode pclSetBadFilesDirectory(PCL_Client * client, const char *directory)
{
	PERFINFO_AUTO_START_FUNC();

	if (!client)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return PCL_CLIENT_PTR_IS_NULL;
	}
	free(client->bad_files);
	client->bad_files = directory ? strdup(directory) : NULL;

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

// Set client max_net_bytes.
PCL_ErrorCode pclSetMaxNetBytes(PCL_Client * client, U32 max_net_bytes)
{
	bool success;
	if (!client || !client->xferrer)
		return PCL_CLIENT_PTR_IS_NULL;

	success = xferSetMaxNetBytes(client->xferrer, max_net_bytes);

	return success ? PCL_SUCCESS : PCL_INVALID_PARAMETER;
}

// Set client max_mem_usage.
PCL_ErrorCode pclSetMaxMemUsage(PCL_Client * client, U64 max_mem_usage)
{
	bool success;
	if (!client || !client->xferrer)
		return PCL_CLIENT_PTR_IS_NULL;

	success = xferSetMaxMemUsage(client->xferrer, max_mem_usage);

	return success ? PCL_SUCCESS : PCL_INVALID_PARAMETER;
}

#include "pcl_client_struct_h_ast.c"
#include "patcher_comm_h_ast.c"
#include "pcl_typedefs_h_ast.c"

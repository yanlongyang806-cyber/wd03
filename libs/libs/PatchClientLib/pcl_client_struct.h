#pragma once

#include "patcher_comm.h"

typedef struct PatchDB PatchDB;
typedef struct NetComm NetComm;
typedef struct NetLink NetLink;
typedef struct PatchXferrer PatchXferrer;
typedef struct FileSpecMapper FileSpecMapper;
typedef struct SimpleFileSpec SimpleFileSpec;
typedef struct DirEntry DirEntry;
typedef struct FileVersion FileVersion;
typedef struct PatcherFileHistory PatcherFileHistory;
typedef struct HogFile HogFile;
typedef struct CheckinList CheckinList;
typedef void** EArrayHandle;
typedef struct CommConnectFSM CommConnectFSM;

// States marked as general states can be used in general_state, but they may also be used as a call_state.
typedef enum
{
	STATE_NONE = 0,			// func_state: This means we need to send a request.
	STATE_CONNECTING,		// general_state: Connecting to a server (including redirecting and autoupdate)
	STATE_AUTOUPDATE,
	STATE_IDLE,				// general_state: No outstanding operations
	STATE_WAITING,			// general_state: An operation is currently running
	STATE_XFERRING,			// general_state: We're downloading from the server
	STATE_FILE_HISTORY,
	STATE_VERSION_INFO,
	STATE_SET_VIEW,
	STATE_VIEW_RESPONSE,
	STATE_GET_MANIFEST,
	STATE_GET_FILE,
	STATE_NAME_VIEW,
	STATE_SET_EXPIRATION,
	STATE_SET_AUTHOR,
	STATE_CHECKIN_FILES,
	STATE_SENT_NAMES,
	STATE_SENDING_DATA,
	STATE_SENT_DATA,
	STATE_CHECKIN_FINISHING,
	STATE_FORCEIN_DIR,
	STATE_LOCK_FILE,
	STATE_LOCK_REQ,
	STATE_GETTING_LOCKED_FILE,
	STATE_UNLOCK_FILES,
	STATE_UNLOCK_REQ,
	STATE_GETTING_UNLOCKED_FILE,
	STATE_GET_REQUIRED_FILES,
	STATE_NOTIFY_ME,
	STATE_GET_HEADERS,
	STATE_GET_FILE_LIST,
	STATE_PROJECT_LIST,
	STATE_BRANCH_INFO,
	STATE_NAME_LIST,
	STATE_CHECK_DELETED,
	STATE_QUERY_LASTAUTHOR,
	STATE_QUERY_LOCKAUTHOR,
	STATE_GET_CHECKINS_BETWEEN_TIMES,
	STATE_GET_CHECKIN_INFO,
	STATE_GET_FILEVERSION_INFO,
	STATE_IS_COMPLETELY_SYNCED,
	STATE_UNDO_CHECKIN,
	STATE_PING,
	STATE_COUNT,
	STATE_SET_FILE_EXPIRATION,

	// Add new states above this line.
	PCL_STATE_COUNT
} PCL_State;

AUTO_STRUCT;
typedef struct PCLFileSpecEntry {
	S32			doInclude;
	S32			isAbsolute;
	char*		filespec;
} PCLFileSpecEntry;

AUTO_STRUCT;
typedef struct PCLFileSpec {
	PCLFileSpecEntry** entriesFirst;
	PCLFileSpecEntry** entriesSecond;
} PCLFileSpec;

typedef struct GetFileListStruct
{
	int i;
	char ** fnames;
	char ** mirror;
	char ** touch;
	PCL_GetFileListCallback callback;
	void * userData;
} GetFileListStruct;

typedef struct NotifyStruct
{
	PCL_NotifyCallback callback;
	void * userData;		
} NotifyStruct;

typedef struct SetViewStruct
{
	PCL_SetViewCallback callback;
	void * userData;
	bool getManifest;
	bool saveTrivia; // Set to false if doing a special override, such as -overridebranch
	int manifest_done;
	int filespec_done;
	char * name;
	U32 time;
	bool incremental;
} SetViewStruct;

typedef struct HistoryStruct
{
	PCL_HistoryCallback callback;
	void * userData;
	char * fileName;
	PatcherFileHistory *history_ret;
} HistoryStruct;

typedef struct VersionInfoStruct
{
	char *dbname;
	PCL_VersionInfoCallback callback;
	void *userData;
} VersionInfoStruct;

typedef struct SetAuthorStruct
{
	PCL_SetAuthorCallback callback;
	void * userData;
	char * author;
} SetAuthorStruct;

typedef struct NameViewStruct
{
	PCL_NameViewCallback callback;
	void* userData;
	char* name;
	char* comment;
	U32 days;
} NameViewStruct;

typedef struct SetExpirationStruct
{
	PCL_SetExpirationCallback callback;
	void *userData;
	char *project;
	char *name;
	U32 days;
} SetExpirationStruct;

typedef struct SetFileExpirationStruct
{
	PCL_SetFileExpirationCallback callback;
	void *userData;
	char *path;
	U32 days;
} SetFileExpirationStruct;

typedef struct ConnectStruct
{
	PCL_ConnectCallback callback;
	void * userData;
	char *autoupdate_path;
	U32 autoupdate_size;
	U32 autoupdate_modified;
} ConnectStruct;

typedef struct CheckinStruct
{
	PCL_CheckinCallback callback;
	void * userData;
	int i;
	int j;
	U8 * file_mem;
	int file_size;
	int curr_sent;
	char ** fnamesOnDisk;
	char ** fnamesForUpload;
	S64 total_bytes;
	S64 total_sent;
	char * comment;
} CheckinStruct;

typedef struct GetRequiredStruct
{
	PCL_GetBatchCallback callback;
	void * userData;
	PCL_MirrorFileCallback mirror_callback;
	void *mirror_userdata;
	DirEntry ** dir_entry_stack;
	int * child_stack;
	PCLFileSpec filespec;
	bool requiredOnly;
	bool getHeaders;
	bool onlyHeaders;
} GetRequiredStruct;

typedef struct LockFileStruct
{
	PCL_LockCallback callback;
	void * userData;
	char **files;
	char **touch;
	PCLFileSpec* filespec;
	U32 *checksums;
} LockFileStruct;

typedef struct UnlockFilesStruct
{
	PCL_LockCallback callback;
	void * userData;
	char **files;
	bool revert; // include a 'get latest' operation
} UnlockFilesStruct;

typedef struct GetProjectListStruct
{
	PCL_ProjectListCallback callback;
	void * userData;
} GetProjectListStruct;

typedef struct GetBranchInfoStruct
{
	int branch;
	PCL_BranchInfoCallback callback;
	void *userData;
} GetBranchInfoStruct;

typedef struct GetNameListStruct
{
	PCL_NameListCallback callback;
	void * userData;
} GetNameListStruct;

typedef void (*PCL_CheckDeletedCallback)(PCL_Client*, char*, int);
typedef struct CheckDeletedStruct
{
	PCL_CheckDeletedCallback callback;
	char **fnames;
} CheckDeletedStruct;

typedef struct QueryLastAuthorStruct
{
	char *dbname;
	U32 timestamp;
	U32 filesize;
	PatchServerLastAuthorResponse *response;
	const char **response_string;
} QueryLastAuthorStruct;

typedef struct GetCheckinsBetweenTimesStruct {
	CheckinList*	clOut;
	U32				timeStart;
	U32				timeEnd;
} GetCheckinsBetweenTimesStruct;

typedef struct GetCheckinInfoStruct {
	PCL_GetCheckinInfoCallback callback;
	U32 rev;
	void *userdata;
} GetCheckinInfoStruct;

typedef struct GetFileVersionInfoStruct {
	char *fname;
	PCL_GetFileVersionInfoCallback callback;
	void *userdata;
} GetFileVersionInfoStruct;

typedef struct IsCompletelySyncedStruct {
	char *path;
	PCL_IsCompletelySyncedCallback callback;
	void *userdata;
} IsCompletelySyncedStruct;

typedef struct UndoCheckinStruct {
	char *comment;
	PCL_UndoCheckinCallback callback;
	void *userdata;
} UndoCheckinStruct;

typedef struct PingStruct {
	char *data;
	PCL_PingCallback callback;
	void *userdata;
} PingStruct;

// A PCL state corresponds to the operation currently running.  It is the cross-product of the general, call, and func states.  Most call states have additional
// state data, which is stored in the union; generally this tracks things that need to be kept track of over the course of this particular operation, and it
// will usually culminate in a callback to the PCL_Client user at the end of the operation.
typedef struct StateStruct
{
	// General state
	// This is the high-level state that we're in:
	//    IDLE: PCL is idle and waiting for someone to tell it to do something
	//    WAITING: PCL is running an operation of some sort
	//    XFERRING: Like WAITING, but specifically, we're downloading from the xfer, meaning the xferrer is in control
	//    CONNECTING: We're connecting or reconnecting to the server, including autoupdate and redirecting
	PCL_State general_state;		

	// Call state
	// This is the specific PCL call or operation that has been requested, that is currently outstanding.  For situations where there is no call, such as IDLE, it is
	// typical that call_state == general_state.
	PCL_State call_state;

	// Func state (request)
	// This is the request that we've sent to the server, that we're waiting on a response to.  STATE_NONE indicates that we have not yet sent a request to the server,
	// or that we've been disconnected and reconnected, and we need to send it again.  Where a call only has one request, it is typical that func_state == call_state
	// once we've sent the packet to the server.
	PCL_State func_state;

	// The call_state will control which is these is valid.
	union
	{
		NotifyStruct notify;
		SetViewStruct set_view;
		HistoryStruct history;
		VersionInfoStruct version_info;
		SetAuthorStruct set_author;
		NameViewStruct name_view;
		SetExpirationStruct set_expiration;
		SetFileExpirationStruct set_file_expiration;
		LockFileStruct lock_file;
		UnlockFilesStruct unlock_files;
		CheckinStruct checkin;
		GetRequiredStruct get_required;
		GetFileListStruct get_list;
		ConnectStruct connect;
		GetProjectListStruct project_list;
		GetBranchInfoStruct branch_info;
		GetNameListStruct name_list;
		CheckDeletedStruct check_deleted;
		QueryLastAuthorStruct query_last_author; // Also for querying the lock author
		GetCheckinsBetweenTimesStruct checkin_between_times;
		GetCheckinInfoStruct checkin_info;
		GetFileVersionInfoStruct fileversion_info;
		IsCompletelySyncedStruct is_completely_synced;
		UndoCheckinStruct undo_checkin;
		PingStruct ping;
	};
} StateStruct;

typedef enum
{
	VIEW_NOT_SET = 0,
	VIEW_SET,
	VIEW_NEEDS_RESET,
} ViewStatus;

typedef struct InMemoryData 
{
	FileVersion *manifest;
	FileVersion *filespec;
} InMemoryData;

typedef struct PCLStatusReportee
{
	NetLink *link;
	CommConnectFSM *fsm;
	char *id;
	char *host;
	U16 port;
	bool critical;
} PCLStatusReportee;

// TODO: Optimize struct layout for memory use:
//   -collapse bitfields
//   -group fields by alignment, where helpful
//   -move optional members to a sub-struct
typedef struct PCL_Client
{
	char * project;
	PatchDB * db;
	NetComm * comm;
	NetComm * statusReportingComm;
	NetLink * link;
	PatchXferrer * xferrer;
	FileSpecMapper * filespec;
	HogFile * headers;
	SimpleFileSpec *mirrorFilespec;

	PCL_FileFlags file_flags; // Used during Get Latest and its variants

	bool soften_all_errors;

	bool disconnected;
	bool was_ever_unexpectedly_disconnected;
	PCL_ErrorCode error;
	char *error_details;			// Extra free-form error information
	bool assert_on_error;
	PCL_ErrorCode *softened_errors;
	bool single_app_mode;
	PCLStatusReportee **status_reporting;
	U32 last_reporting_update;

	StateStruct ** state; 

	char * server;
	char * original_server;
	char * referrer;
	U32 server_time;	//server time on connection
	U32 client_time;	//client time when server time received
	int port;
	int original_port;
	F32 timeout;
	S64 progress_timer;
	U32 progress_timer_retries;
	S64 retry_timer;
	F32 retry_timeout;
	F32 retry_backoff;
	U32 retry_count;
	U64 last_process;
	U32 current_pak_bytes_waiting_to_recv;
	
	S64 keepAliveTimer;
	F32 keepAliveTimeout;
	U32 keepAlivePingAckRecvCount;

	S64 forceinKeepAlive;			// Last time we sent a keepalive during the forcein process

	char * sandbox;
	int branch;
	int rev;
	char * prefix;
	char * author;

	char * root_folder;
	char ** extra_folders;
	U32 * extra_flags;
	U32 write_index;
	char *autoupdate_token;

	bool needs_restart;
	ViewStatus view_status;
	char *cached_view_name;
	ViewStatus author_status;
	bool got_all; // we did a get on everything, so there shouldn't be any out-of-sync files
	
	bool verifyAllFiles;
	bool verbose_logging;
	bool useFileOverlay;

	char * filespec_fname;
	char * manifest_fname;
	char * full_manifest_fname;
	char * extra_filespec_fname;

	char * bad_files;

	InMemoryData in_memory_data;

	U32 time_started;
	U64 bytes_read_at_start;
	U64 http_bytes_read_at_start;
	U64 process_loops;

	// Miscellaneous callbacks, including progress feedback
	PCL_LoggingFunction log_function;
	void *log_userdata;
	PCL_ForceInScanCallback forceInScanCallback;
	void * forceInScanUserData;
	PCL_UploadCallback uploadCallback;
	void * uploadUserData;
	PCL_ProcessCallback processCallback;
	void * processUserData;
	PCL_MirrorCallback mirrorCallback;
	void * mirrorUserData;
	PCL_MirrorCallback examineCallback;
	void * examineUserData;
	PCL_MirrorCallback examineCompleteCallback;
	void * examineCompleteUserData;
	PCL_NetCallback netEnterCallback;
	PCL_NetCallback netLeaveCallback;
	void * netCallbackUserData;
	PCL_HttpInfoCallback httpinfoCallback;
	void * httpinfoUserData;
	PCL_AutoupInfoCallback autoupinfoCallback;
	void * autoupinfoUserData;

	S64 callback_timer;

	U32 titleLogLastUpdate;
}
PCL_Client;

AUTO_STRUCT;
typedef struct MirrorFolder
{
	char * path;
	bool recurse;
} MirrorFolder;

AUTO_STRUCT;
typedef struct LinkInfo
{
	char * ip;
	int port;
	char * project;
	int branch;
	bool no_upload;
	MirrorFolder ** mirrors;
} LinkInfo;

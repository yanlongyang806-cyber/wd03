#pragma once
GCC_SYSTEM

#define MAX_RESTARTS				5
#define PATCH_DIR					".patch"
#define CONNECTION_FILE_NAME		"connection.txt"
#define MAX_CLIENT_SERVER_TIME_DIFFERENCE	120		// in seconds

typedef struct HogFile HogFile;
typedef struct PCL_Client PCL_Client;
typedef struct NetComm NetComm;
typedef struct FolderCache FolderCache;
typedef struct PatcherFileHistory PatcherFileHistory;
typedef struct CheckinList CheckinList;

typedef enum
{
	PCL_SET_DEFAULT					= 0,
	PCL_SET_READ_ONLY				= (1<<0),
	PCL_SET_MAKE_FOLDERS			= (1<<1),
	PCL_BACKUP_WRITEABLES			= (1<<2),
	PCL_BACKUP_TIMESTAMP_CHANGES	= (1<<3),
	PCL_KEEP_RECREATED_DELETED		= (1<<4),
	PCL_USE_POOLED_PATHS			= (1<<5),
	PCL_SKIP_BINS					= (1<<6),
	PCL_NO_WRITE					= (1<<7),
	PCL_CLEAN_HOGGS					= (1<<8),
	PCL_NO_DELETE					= (1<<9),
	PCL_NO_MIRROR					= (1<<10),
	PCL_IN_MEMORY					= (1<<11),		// Save patched files in memory rather than writing them to disk.
	PCL_METADATA_IN_MEMORY			= (1<<12),		// Don't save the manifest or filespec to disk.
	PCL_NO_DELETEME_CLEANUP			= (1<<13),		// When scanning for updates, don't delete .deleteme files.
	PCL_USE_SAVED_METADATA			= (1<<14),		// Use the manifest and filespec that was written to disk.
	PCL_FORCE_EMPTY_DB				= (1<<15),		// Force the manifest to be empty, even if the database exists.
	PCL_RECONNECT					= (1<<16),		// Reconnect to the server if disconnected
	PCL_XFER_COMPRESSED				= (1<<17),		// Try to use compressed xfers whenever possible (always on for HTTP)
	PCL_CALLBACK_ON_DESTROY			= (1<<18),		// Call xfer completion callback if the xferrer is destroyed
	PCL_DISABLE_BINDIFF				= (1<<19),		// Disable bindiffing
	PCL_TRIM_PATCHDB_FOR_STREAMING	= (1<<20),		// Remove parts of the manifest to reduce memory use patch streaming
	PCL_VERIFY_CHECKSUM				= (1<<21),		// When verifying if a file is up to date, check the checksum, even if we wouldn't normally
} PCL_FileFlags;

#define PCL_SET_GIMME_STYLE (PCL_SET_READ_ONLY | PCL_SET_MAKE_FOLDERS | PCL_BACKUP_WRITEABLES | PCL_BACKUP_TIMESTAMP_CHANGES | PCL_KEEP_RECREATED_DELETED)
#define PCL_FILEFLAGS_BINMASK (PCL_SET_READ_ONLY | PCL_BACKUP_WRITEABLES | PCL_BACKUP_TIMESTAMP_CHANGES | PCL_KEEP_RECREATED_DELETED) // not used on bins

AUTO_ENUM;
typedef enum
{
	PCL_SUCCESS = 0,
	PCL_XFERS_FULL,
	PCL_FILE_NOT_FOUND,
	PCL_UNEXPECTED_RESPONSE,
	PCL_INVALID_VIEW,
	PCL_FILESPEC_NOT_LOADED,
	PCL_MANIFEST_NOT_LOADED,
	PCL_NOT_IN_FILESPEC,
	PCL_NULL_HOGG_IN_FILESPEC,
	PCL_NAMING_FAILED,
	PCL_SET_EXPIRATION_FAILED,
	PCL_AUTHOR_FAILED,
	PCL_CHECKIN_FAILED,
	PCL_LOCK_FAILED,
	PCL_LOST_CONNECTION,
	PCL_WAITING,
	PCL_TIMED_OUT,
	PCL_NO_VIEW_FILE,
	PCL_DLL_NOT_LOADED,
	PCL_NO_CONNECTION_FILE,
	PCL_DIALOG_MESSAGE_ERROR,
	PCL_HEADER_NOT_UP_TO_DATE,
	PCL_HEADER_HOGG_NOT_LOADED,
	PCL_NOT_IN_ROOT_FOLDER,
	PCL_CLIENT_PTR_IS_NULL,
	PCL_NO_ERROR_STRING,
	PCL_NULL_PARAMETER,
	PCL_STRING_BUFFER_SIZE_TOO_SMALL,
	PCL_DLL_FUNCTION_NOT_LOADED,
	PCL_COMM_LINK_FAILURE,
	PCL_NO_FILES_LOADED,
	PCL_HOGG_READ_ERROR,
	PCL_COULD_NOT_START_LOCK_XFER,
	PCL_NO_RESPONSE_FUNCTION,
	PCL_INVALID_PARAMETER,
	PCL_INTERNAL_LOGIC_BUG,
	PCL_COULD_NOT_WRITE_LOCKED_FILE,
	PCL_NOT_WAITING_FOR_NOTIFICATION,
	PCL_HOGG_UNLOAD_FAILED,
	PCL_BAD_PACKET,
	PCL_DESTROYED,
	PCL_ABORT,
	PCL_FILESPEC_PROCESSING_FAILED,

	// Place new error codes above this line.
	PCL_ERROR_COUNT
} PCL_ErrorCode;

extern StaticDefineInt PCL_ErrorCodeEnum[];

AUTO_ENUM;
typedef enum PCL_LogLevel
{
	PCLLOG_ERROR,
	PCLLOG_WARNING,
	PCLLOG_INFO,
	PCLLOG_LINK, // connection spam
	PCLLOG_SPAM,
	PCLLOG_FILEONLY, // intended for logs only
	PCLLOG_TITLE,
	PCLLOG_TITLE_DISCARDABLE, // Will not update if it's not been long since last update

	// these are used internally and are not passed to the logging function
	PCLLOG_PACKET,
} PCL_LogLevel;

// These flags indicate the way in which a file is being changed, with respect to checkins, diffing, etc.
// [Bits 0-1] [Bits 2-4      ] [Bits 5-         ]
// [Action  ] [Testing Status] [Additional Flags]
typedef enum PCL_DiffType
{
	// Action
	PCLDIFFMASK_ACTION		= (3<<0),
	PCLDIFF_CHANGED			= (0<<0),
	PCLDIFF_NOCHANGE		= (1<<0),
	PCLDIFF_CREATED			= (2<<0),
	PCLDIFF_DELETED			= (3<<0),

	// Testing Status
	PCLDIFFMASK_TESTING		= (7<<2),
	PCLDIFF_NEEDSTESTING	= (0<<2), // the client should check if the file has been tested, and set another value
	PCLDIFF_DONTTEST		= (1<<2),
	PCLDIFF_TESTEDBAD		= (2<<2),
	PCLDIFF_NOTTESTED		= (3<<2),
	PCLDIFF_PRESUMEDGOOD	= (4<<2),
	PCLDIFF_TESTEXCLUDED	= (5<<2),

	// Additional flags
	PCLDIFF_LINKWILLBREAK	= (1<<5), // to the previous branch
	PCLDIFF_LINKISBROKEN	= (1<<6), // to the following branch
	PCLDIFF_NEWFILE			= (1<<7), // Flag to go with _NOCHANGE to undo checkout on a new file
	PCLDIFF_CONFLICT		= (1<<8), // Gimme Backup: Conflict while restoring from backup
	PCLDIFF_NEEDCHECKOUT	= (1<<9), // Gimme Backup: Needs to be checked out
	PCLDIFF_BLOCKED			= (1<<10),// file is marked as blocked in the registry
} PCL_DiffType;

// Low-level, externally-visible connection status information, for reporting and debugging.
// Typical transition diagram, without reconnection:
// INITIAL_CONNECT -> NEGOTIATION -> CONNECTED -> VIEW_SET -> DISCONNECTED
// Typical transition diagram, with reconnection:
// INITIAL_CONNECT -> NEGOTIATION -> CONNECTED -> VIEW_SET -> RECONNECTING -> NEGOTIATION -> CONNECTED -> VIEW_SET
typedef enum PCL_ConnectionStatus
{
	PCL_CONNECTION_CONNECTED = 1,		// Connected and negotiated, ready for commands, but no view set
	PCL_CONNECTION_VIEW_SET,			// View set; for most patcher clients, this is the typical idle state
	PCL_CONNECTION_INITIAL_CONNECT,		// Connecting for the first time, but no response from the server yet
	PCL_CONNECTION_NEGOTIATION,			// Connected, performing initial negotiation (version, redirect, autoupdate, etc)
	PCL_CONNECTION_RECONNECTING,		// Disconnected, but trying to reconnect
	PCL_CONNECTION_DISCONNECTED,		// Disconnected, and not trying to reconnect
} PCL_ConnectionStatus;

typedef struct PatchProjectInfo
{
	char * project;
	int max_branch;
	bool no_upload;
} PatchProjectInfo;

typedef struct ViewNameInfo
{
	char * names;
	int branch;
	char * sandbox;
	U32 timestamp;
} ViewNameInfo;

typedef struct ProgressMeter
{
	U32 files_so_far;
	U32 files_total;
	U64 bytes_so_far;
	U64 bytes_total;
} ProgressMeter;

AUTO_STRUCT;
typedef struct XferStateInfo
{
	const char * filename;
	const char * state;
	U32 bytes_requested;
	U64 start_ticks;
	U32 blocks_so_far;
	U32 blocks_total;
	U32 block_size;
	U32 cum_bytes_requested;
	U32 total_bytes_sent;
	U32 total_bytes_received;
	U32 filedata_size;				// com_len or unc_len, depending on is_compressed
} XferStateInfo;

typedef struct XferSnapshot
{
	XferStateInfo * xfer_infos;
	int xfer_count;
	int buffered;
	U64 actual_transferred;
	U32 seconds;
	U64 frames;
} XferSnapshot;

// Processing callback statistics
AUTO_STRUCT;
typedef struct PatchProcessStats
{
	S64 received;
	S64 total;
	U32 received_files;
	U32 total_files;
	int xfers;
	int buffered;
	S64 actual_transferred;
	U64 overlay_bytes;
	U64 written_bytes;
	S64 http_actual_transferred;
	S64 http_errors;
	U64 http_header_bytes;
	U64 http_mime_bytes;
	U64 http_body_bytes;
	U64 http_extra_bytes;
	U32 seconds;
	U64 loops;
	XferStateInfo ** state_info;
	F32 elapsed;
	PCL_ErrorCode error;
} PatchProcessStats;

// Old callbacks, for now

typedef void (*PCL_LoggingFunction)(PCL_LogLevel level, const char *str, void *userData);

typedef void (*PCL_ProjectListCallback)
	(char ** projects, int * max_branch, int * no_upload, int count, PCL_ErrorCode error, const char *error_details, void * userData);

typedef bool (*PCL_ProcessCallback)(PatchProcessStats *stats, void *userData);

typedef bool (*PCL_ForceInScanCallback)
	(const char *fileName, int number, int count, bool addedToList, F32 elapsed, PCL_ErrorCode error, const char *error_details, void *userData);

typedef void (*PCL_HistoryCallback)
	(PatcherFileHistory *history, PCL_ErrorCode error, const char *error_details, void * userData);

typedef void (*PCL_VersionInfoCallback)
	(int branch, const char *sandbox, U32 checkin_time, const char *author, const char *comment, PCL_ErrorCode error, const char *error_details, void *userData);

typedef bool (*PCL_UploadCallback)
	(S64 sent, S64 total, F32 elapsed, PCL_ErrorCode error, const char *error_details, void * userData);

typedef void (*PCL_NameListCallback)
	(char ** names, int * branches, char ** sandboxes, U32 * revs, char ** comments, U32 * expires, int count, PCL_ErrorCode error, const char *error_details, void * userData);

typedef void (*PCL_BranchInfoCallback)
	(const char *name, int parent_branch, const char *warning, PCL_ErrorCode error, const char *error_details, void *userData);

typedef void (*PCL_CheckinCallback)
	(int rev, U32 time, PCL_ErrorCode error, const char *error_details, void * userData);

typedef void (*PCL_GetFileCallback)
	(PCL_Client * client, const char * filename, XferStateInfo * info, PCL_ErrorCode error, const char *error_details, void * userData);

// The pre-connect callback is a special callback that lets you modify the PCL_Client object before it makes it first connection,
// in case something you would do to it would make a difference for the initial connection.
// In most cases, you should apply flags after pclConnectAndCreate() or in the connect callback instead of using this.
typedef void (*PCL_PreconnectCallback)
	(PCL_Client * client, void * userData);

typedef void (*PCL_ConnectCallback)
	(PCL_Client * client, bool updated, PCL_ErrorCode error, const char *error_details, void * userData);

typedef void (*PCL_SetViewCallback)
	(PCL_Client * client, PCL_ErrorCode error, const char *error_details, void * userData);

typedef void (*PCL_LockCallback)
	(PCL_ErrorCode error, const char *error_details, void * userData);

typedef bool (*PCL_PreLockCallback)
	(PCL_Client* client, void* userData, const char*const* fileNames, U32 count);

typedef void (*PCL_GetBatchCallback)
	(PCL_Client * client, PCL_ErrorCode error, const char *error_details, void * userData);

typedef void (*PCL_GetFileListCallback)
	(PCL_Client * client, PCL_ErrorCode error, const char *error_details, void * userData, const char*const* fileNames);

typedef void (*PCL_MirrorFileCallback)
	(char *filename, U32 filename_size, PCL_ErrorCode error, const char *error_details, void * userData);

typedef void (*PCL_NameViewCallback)
	(PCL_ErrorCode error, const char *error_details, void * userData);

typedef void (*PCL_SetExpirationCallback)
	(PCL_ErrorCode error, const char *error_details, const char *msg, void *userdata);

typedef void (*PCL_SetFileExpirationCallback)
	(PCL_ErrorCode error, const char *error_details, void *userdata);

typedef void (*PCL_SetAuthorCallback)
	(PCL_ErrorCode error, const char *error_details, void * userData);

typedef void (*PCL_NotifyCallback)
	(PCL_ErrorCode error, const char *error_details, void * userData);

typedef bool (*PCL_MirrorCallback)		(	PCL_Client * client,
											void * userData,
											F32 elapsed,
											const char* curFileName,
											ProgressMeter * progress);

typedef void (*PCL_NetCallback)
	(PCL_Client * client, void * userData);

typedef void (*PCL_GetCheckinInfoCallback)
	(PCL_Client* client, U32 rev, U32 branch, U32 time, char *sandbox, U32 incr_from, char *author, char *comment, char **files, void *userdata);

typedef void (*PCL_GetFileVersionInfoCallback)
(PCL_Client* client, const char *fname, U32 modified, U32 size, U32 checksum, U32 header_size, U32 header_checksum, U32 flags, char *author, char *lockedby, void *userdata);

typedef void (*PCL_IsCompletelySyncedCallback)(PCL_Client* client, bool synced, bool exists, void *userdata);

typedef void (*PCL_UndoCheckinCallback)(PCL_Client* client, bool success, char *error_message, U32 rev, U32 time, void *userdata);

typedef void (*PCL_PingCallback)(PCL_Client* client, char *data, void *userdata);

typedef void (*PCL_InMemoryIteratorCallback)(PCL_Client* client, const char *filename, const char *data, U32 size, U32 modtime, void *userdata);

typedef void (*PCL_HttpInfoCallback)
	(PCL_Client *client, const char *http_info, void *userData);

typedef void (*PCL_AutoupInfoCallback)
	(PCL_Client *client, U32 version, const char *autoup_info, void *userData);

#pragma once
GCC_SYSTEM

#include "Organization.h"
#include "patcher_comm.h"
#include "pcl_typedefs.h"

#define PCL_DEFAULT_PATCHSERVER ORGANIZATION_DOMAIN

typedef struct PCLFileSpec PCLFileSpec;
typedef U32 HogFileIndex;

// ************************************************************************
// * PatchClient features
// ************************************************************************
PCL_ErrorCode	pclConnectAndCreate(PCL_Client **client,
									const char *serverName,
									S32 port,
									F32 timeout,
									NetComm *comm,
									const char *rootFolder,
									const char *autoupdate_token,
									const char *autoupdate_path,
									PCL_ConnectCallback callback,
									void *userData);
PCL_ErrorCode	pclConnectAndCreateEx(PCL_Client **client,
									const char *serverName,
									S32 port,
									F32 timeout,
									NetComm *comm,
									const char *rootFolder,
									const char *autoupdate_token,
									const char *autoupdate_path,
									PCL_ConnectCallback callback,
									PCL_PreconnectCallback preconnect,
									void *userData,
									char *error,
									size_t error_size);

PCL_ErrorCode	pclConnectAndCreateForFile(PCL_Client **client,
									const char *fileName,
									F32 timeout,
									NetComm *comm,
									const char *autoupdate_token,
									const char *autoupdate_path,
									PCL_ConnectCallback callback,
									void *userData);
									
PCL_ErrorCode	pclSetKeepAliveAndTimeout(SA_PARAM_NN_VALID PCL_Client* client, F32 timeout);
									
PCL_ErrorCode 	pclSetViewByTime(SA_PARAM_NN_VALID PCL_Client *client, SA_PARAM_NN_STR const char *project, int branch,
									SA_PARAM_NN_STR const char *sandbox, U32 time, bool getManifest, bool saveTrivia, PCL_SetViewCallback callback, void *userData);
PCL_ErrorCode 	pclSetViewByRev(SA_PARAM_NN_VALID PCL_Client *client, SA_PARAM_NN_STR const char *project, int branch,
								  SA_PARAM_NN_STR const char *sandbox, int rev, bool getManifest, bool saveTrivia, PCL_SetViewCallback callback, void *userData);
PCL_ErrorCode 	pclSetViewLatest(SA_PARAM_NN_VALID PCL_Client *client, SA_PARAM_NN_STR const char *project, int branch,
							  SA_PARAM_OP_STR const char *sandbox, bool getManifest, bool saveTrivia, PCL_SetViewCallback callback, void *userData);
PCL_ErrorCode	pclSetDefaultView(SA_PARAM_NN_VALID PCL_Client * client, SA_PARAM_NN_STR const char * project, bool getManifest,
								  PCL_SetViewCallback callback, void * userData);
PCL_ErrorCode	pclSetNamedView(SA_PARAM_NN_VALID PCL_Client * client, SA_PARAM_NN_STR const char * project,
									 SA_PARAM_NN_STR const char * viewName, bool getManifest, bool saveTrivia,
									 PCL_SetViewCallback callback, void * userData);
PCL_ErrorCode	pclSetViewNewIncremental(SA_PARAM_NN_VALID PCL_Client *client, SA_PARAM_NN_STR const char *project, int branch,
										 SA_PARAM_NN_STR const char *sandbox, int rev, bool getManifest, PCL_SetViewCallback callback, void *userData);
PCL_ErrorCode	pclSetViewNewIncrementalName(SA_PARAM_NN_VALID PCL_Client *client, SA_PARAM_NN_STR const char *project, SA_PARAM_NN_STR const char *sandbox,
											 SA_PARAM_NN_STR const char *incr_from, bool getManifest, PCL_SetViewCallback callback, void *userData);
PCL_ErrorCode	pclCompareProject(SA_PARAM_NN_VALID PCL_Client * client, SA_PARAM_NN_STR const char * project, bool * match);
PCL_ErrorCode	pclCompareView(SA_PARAM_NN_VALID PCL_Client * client, SA_PARAM_NN_STR const char * project,
									int branch, SA_PARAM_NN_STR const char *sandbox, int rev, bool *view_match);
PCL_ErrorCode	pclGetView(SA_PARAM_NN_VALID PCL_Client *client, char *project_buf, int project_buf_size, int *rev, int *branch, char *sandbox_buf, int sandbox_buf_size);
PCL_ErrorCode	pclGetAllFiles(SA_PARAM_NN_VALID PCL_Client * client, PCL_GetBatchCallback callback, void * userData, const PCLFileSpec * filespec);
PCL_ErrorCode	pclGetRequiredFiles(SA_PARAM_NN_VALID PCL_Client * client, bool getHeaders, bool onlyHeaders, PCL_GetBatchCallback callback, void * userData, const PCLFileSpec * filespec);
PCL_ErrorCode	pclIsFileUpToDate(SA_PARAM_NN_VALID PCL_Client * client, SA_PARAM_NN_STR const char * fileName, bool * up_to_date);
PCL_ErrorCode	pclGetFileHeader(SA_PARAM_NN_VALID PCL_Client * client, SA_PARAM_NN_STR const char * fileName, SA_PARAM_NN_VALID HogFile **hog_handle, SA_PARAM_NN_VALID HogFileIndex *hog_index);
PCL_ErrorCode	pclGetFile(SA_PARAM_NN_VALID PCL_Client * client, SA_PARAM_NN_STR char * fileName, int priority, PCL_GetFileCallback callback, void * userData);
PCL_ErrorCode	pclGetFileList(SA_PARAM_NN_VALID PCL_Client * client, const char*const* fileNames, int * recurse, bool get_locked, int count, PCL_GetFileListCallback callback, void * userData, const PCLFileSpec* filespec);
PCL_ErrorCode	pclForceGetFiles(SA_PARAM_NN_VALID PCL_Client *client, const char*const*dbnames, int count, PCL_GetFileListCallback callback, void *userData);
PCL_ErrorCode	pclUndoCheckin(PCL_Client * client, const char *comment, PCL_UndoCheckinCallback callback, void *userdata);
PCL_ErrorCode	pclProcess(SA_PARAM_NN_VALID PCL_Client * client);
PCL_ErrorCode	pclProcess_dbg(SA_PARAM_NN_VALID PCL_Client * client, const char* fileName, S32 fileLine);
#define			pclProcessTracked(client) pclProcess_dbg(client, __FILE__, __LINE__)
PCL_ErrorCode	pclWait(SA_PARAM_NN_VALID PCL_Client * client);
PCL_ErrorCode	pclWaitFrames(SA_PARAM_NN_VALID PCL_Client * client, bool frames);
PCL_ErrorCode	pclCheckTimeout(SA_PARAM_NN_VALID PCL_Client * client);
PCL_ErrorCode	pclFlush(SA_PARAM_NN_VALID PCL_Client * client);
PCL_ErrorCode	pclDisconnectAndDestroy(PCL_Client* client);

// Disconnect from the Patch Server.  This will initiate reconnection or go into the disconnected state, depending on whether PCL_RECONNECT is set.
PCL_ErrorCode	pclDisconnect(PCL_Client* client);

PCL_ErrorCode	pclGotFiles(PCL_Client * client, int * count);

// Get the total number of bytes received on on the link, the same as 'received' in the stats.
PCL_ErrorCode	pclActualTransferred(PCL_Client *client, U64 *bytes);

PCL_ErrorCode	pclClearCounts(PCL_Client *client);
PCL_ErrorCode	pclClearError(SA_PARAM_NN_VALID PCL_Client *client); // clears the client's error state if it's idle
PCL_ErrorCode	pclUseFileOverlay(SA_PARAM_NN_VALID PCL_Client *client, S32 enabled); //Enables overlay for files on disk
PCL_ErrorCode	pclVerifyAllFiles(SA_PARAM_NN_VALID PCL_Client *client, S32 enabled);
PCL_ErrorCode	pclVerboseLogging(SA_PARAM_NN_VALID PCL_Client *client, S32 enabled);
PCL_ErrorCode	pclForEachInMemory(SA_PARAM_NN_VALID PCL_Client *client, PCL_InMemoryIteratorCallback callback, void *userdata);
PCL_ErrorCode	pclListFilesInDir(SA_PARAM_NN_VALID PCL_Client *client, SA_PARAM_NN_STR const char *path, SA_PARAM_OP_VALID char ***children,
								  SA_PARAM_OP_VALID U32 **newest_modified, bool include_deleted);

// Find the size of the largest file in the manifest.
PCL_ErrorCode	pclGetLargestFileSize(SA_PARAM_NN_VALID PCL_Client *client, U32 *size);

// Enable HTTP on a client.
PCL_ErrorCode	pclSetHttp(SA_PARAM_NN_VALID PCL_Client *client, const char *server, U16 port, const char *prefix);

// Get the server that we're currently connected to.
PCL_ErrorCode	pclGetConnectedServer(SA_PARAM_NN_VALID PCL_Client *client, const char **server);

// Get the general status of the PCL link, for reporting and debugging purposes.
PCL_ErrorCode	pclGetLinkConnectionStatus(SA_PARAM_NN_VALID PCL_Client *client, PCL_ConnectionStatus *status);

// Set idle to true if the client is in the idle state.
PCL_ErrorCode	pclIsIdle(PCL_Client *client, bool *idle);

// Set idle to true if the client is in the idle state.
PCL_ErrorCode	pclStartStatusReporting(PCL_Client *client, const char *id, const char *host, U16 port, bool bCritical);

// Abort the current operation.
PCL_ErrorCode	pclAbort(PCL_Client *client);


// ************************************************************************
// * GameClient features
// ************************************************************************
PCL_ErrorCode 	pclNeedsRestart(PCL_Client * client, bool * needs_restart);
PCL_ErrorCode 	pclAddFilesToFolderCache(SA_PARAM_NN_VALID PCL_Client * client, SA_PARAM_NN_VALID FolderCache * cache);
PCL_ErrorCode 	pclSetHoggsSingleAppMode(SA_PARAM_NN_VALID PCL_Client * client, bool singleAppMode);

// Make all non-fatal errors assert() immediately.
PCL_ErrorCode 	pclAssertOnError(SA_PARAM_NN_VALID PCL_Client * client, bool assert_on_error);

PCL_ErrorCode 	pclErrorState(SA_PARAM_NN_VALID PCL_Client * client);

// Make an error soft, so that it will not Errorf() or print error information.
// Note: It will still set an error state.
PCL_ErrorCode	pclSoftenError(PCL_Client * client, PCL_ErrorCode error_code);

// Reverse the effect of pclSoftenError().
PCL_ErrorCode	pclUnsoftenError(PCL_Client * client, PCL_ErrorCode error_code);

PCL_ErrorCode pclSoftenAllErrors(PCL_Client * client);
PCL_ErrorCode pclUnsoftenAllErrors(PCL_Client * client);


// ************************************************************************
// * GimmeClient features
// ************************************************************************
PCL_ErrorCode	pclSetAuthor(SA_PARAM_NN_VALID PCL_Client * client, SA_PARAM_NN_STR const char * author,
								  PCL_SetAuthorCallback callback, void * userData);
PCL_ErrorCode	pclNameCurrentView(	SA_PARAM_NN_VALID PCL_Client * client,
									SA_PARAM_NN_STR const char * viewName,
									int days,
									const char* comment,
									PCL_NameViewCallback callback,
									void * userData);
PCL_ErrorCode	pclSetExpiration(SA_PARAM_NN_VALID PCL_Client *client, SA_PARAM_NN_STR const char *project,
								 SA_PARAM_NN_STR const char *viewName, int days, PCL_SetExpirationCallback callback, void *userData);
PCL_ErrorCode	pclSetFileExpiration(SA_PARAM_NN_VALID PCL_Client *client, const char *path, int days, PCL_SetFileExpirationCallback callback, void *userData);
PCL_ErrorCode	pclDiffFolder(	SA_PARAM_NN_VALID PCL_Client *client,
								SA_PARAM_NN_STR const char *dirName,
								bool forceIfNotLockedByClient,
								bool forceIfLockedByClient,
								bool matchAsFiles,
								SA_PARAM_OP_VALID char ***diff_names,
								SA_PARAM_OP_VALID PCL_DiffType **diff_types);
PCL_ErrorCode	pclCheckinFileList(SA_PARAM_NN_VALID PCL_Client *client, const char **dbnames, PCL_DiffType *diff_types, int count,
								 const char *comment, bool force, PCL_CheckinCallback callback, void *userData);
PCL_ErrorCode	pclCheckInFiles(SA_PARAM_NN_VALID PCL_Client * client, const char ** dirNames, const char ** countsAsDir, int * recurse,
								int count, const char ** hide_paths, int hide_count, SA_PARAM_NN_STR const char * comment,
								PCL_CheckinCallback callback, void * userData);
PCL_ErrorCode	pclForceInFiles(SA_PARAM_NN_VALID PCL_Client * client,
								const char*const* dirNames,
								const char*const* countsAsDir,
								const int* recurse,
								int count,
								const char*const* hide_paths,
								int hide_count,
								const char* comment,
								bool matchesonly,
								PCL_CheckinCallback callback,
								void * userData);

S32				pclFileIsIncludedByFileSpec(const char* fileNameRelative,
											const char* fileNameAbsolute,
											const PCLFileSpec* filespec);

PCL_ErrorCode	pclLockFiles(	SA_PARAM_NN_VALID PCL_Client *client,
								const char*const* fileNames,
								int count,
								PCL_LockCallback callback,
								void * userData,
								PCL_PreLockCallback preLockCallback,
								void* preLockUserData,
								const PCLFileSpec* filespec);
								
PCL_ErrorCode	pclUnlockFiles(SA_PARAM_NN_VALID PCL_Client *client, const char **fileNames, int count, PCL_LockCallback callback, void *userData);
PCL_ErrorCode	pclFileHistory(SA_PARAM_NN_VALID PCL_Client * client, SA_PARAM_NN_STR const char * fileName,
							   PCL_HistoryCallback callback, void * userData, PatcherFileHistory *history_ret);
PCL_ErrorCode	pclGetVersionInfo(SA_PARAM_NN_VALID PCL_Client *client, const char *dbname, PCL_VersionInfoCallback callback, void *userData);
PCL_ErrorCode	pclResetStateToIdle(PCL_Client* client);
PCL_ErrorCode	pclResetRoot(SA_PARAM_NN_VALID PCL_Client * client, const char * root_folder);
PCL_ErrorCode	pclGetProjectList(SA_PARAM_NN_VALID PCL_Client * client, PCL_ProjectListCallback callback, void * userData);
PCL_ErrorCode	pclGetBranchInfo(SA_PARAM_NN_VALID PCL_Client *client, int branch, PCL_BranchInfoCallback callback, void *userData);
PCL_ErrorCode	pclGetNameList(SA_PARAM_NN_VALID PCL_Client * client, PCL_NameListCallback callback, void * userData);
PCL_ErrorCode	pclGetFileFlags(SA_PARAM_NN_VALID PCL_Client * client, U32 *file_flags);
PCL_ErrorCode	pclSetFileFlags(SA_PARAM_NN_VALID PCL_Client * client, U32 file_flags);
PCL_ErrorCode	pclAddFileFlags(SA_PARAM_NN_VALID PCL_Client * client, U32 file_flags);
PCL_ErrorCode	pclRemoveFileFlags(SA_PARAM_NN_VALID PCL_Client * client, U32 file_flags);
PCL_ErrorCode	pclGetCurrentBranch(SA_PARAM_NN_VALID PCL_Client* client, S32* branchOut);
PCL_ErrorCode	pclGetCurrentProject(SA_PARAM_NN_VALID PCL_Client* client, char* projectOut, S32 projectOutLen);
PCL_ErrorCode	pclGetAuthorQuery(PCL_Client *client, const char *dbname, const char *fullpath, PatchServerLastAuthorResponse *response, const char **response_string);
PCL_ErrorCode	pclGetLockAuthorQuery(PCL_Client *client, const char *dbname, const char **author);

PCL_ErrorCode	pclGetCheckinsBetweenTimes(PCL_Client *client, U32 timeStart, U32 timeEnd, CheckinList* clOut);
PCL_ErrorCode	pclGetCheckinInfo(PCL_Client *client, U32 rev, PCL_GetCheckinInfoCallback callback, void *userdata);
PCL_ErrorCode	pclGetFileVersionInfo(PCL_Client * client, const char * fname, PCL_GetFileVersionInfoCallback callback, void *userdata);
PCL_ErrorCode	pclIsCompletelySynced(PCL_Client *client, const char *path, PCL_IsCompletelySyncedCallback callback, void *userdata);
PCL_ErrorCode	pclInjectFileVersionFromServer(PCL_Client * client, const char * fname);
PCL_ErrorCode	pclAddExtraFolder(PCL_Client * client, const char * path, U32 flags);
PCL_ErrorCode	pclSetWriteFolder(PCL_Client * client, const char * path);
PCL_ErrorCode	pclAddExtraFilespec(PCL_Client * client, const char * fname);

// Process a filespec to evaluate TextParser includes and macros, and make sure the syntax is valid.
PCL_ErrorCode	pclProprocessFilespec(const char * input_filename, const char * output_filename);


// ************************************************************************
// * PatchServer features
// ************************************************************************
PCL_ErrorCode	pclNotifyMe(SA_PARAM_NN_VALID PCL_Client * client, PCL_NotifyCallback callback, void * userData);
PCL_ErrorCode	pclNotifyHalt(SA_PARAM_NN_VALID PCL_Client * client); 
PCL_ErrorCode	pclGetFileTo(SA_PARAM_NN_VALID PCL_Client * client, SA_PARAM_NN_STR const char * fileName,
							 SA_PARAM_NN_STR const char * fileNameToWrite, HogFile * hogg,
							 int priority, PCL_GetFileCallback callback, void * userData);
PCL_ErrorCode	pclSetCompression(SA_PARAM_NN_VALID PCL_Client *client, bool compress_as_server); // ignores file extension when compressing

// Request that a Patch Server shut down.
PCL_ErrorCode	pclShutdown(SA_PARAM_NN_VALID PCL_Client * client);

// Request that a Patch Server run its merger.
PCL_ErrorCode	pclMergeServer(SA_PARAM_NN_VALID PCL_Client * client);


// ************************************************************************
// * Patch Server parent-child communication
// ************************************************************************

// Notify a Patch Server parent that a client has accessed a view.
PCL_ErrorCode	pclNotifyView(SA_PARAM_NN_VALID PCL_Client * client, SA_PARAM_NN_STR const char * project, SA_PARAM_NN_STR const char * viewName, U32 ip);

// Notify the parent Patch Server that a server (either itself or one of its children) has been activated.
PCL_ErrorCode	pclPatchServerActivate(SA_PARAM_NN_VALID PCL_Client *client, SA_PARAM_NN_STR const char *name, SA_PARAM_NN_STR const char *category,
									   SA_PARAM_NN_STR const char *parent);

// Notify the parent Patch Server that a server (one of its children) has been deactivated.
PCL_ErrorCode	pclPatchServerDeactivate(SA_PARAM_NN_VALID PCL_Client *client, SA_PARAM_NN_STR const char *name);

// Send updating status to parent; the format of the status string is defined by the Patch Server.
PCL_ErrorCode	pclPatchServerUpdateStatus(SA_PARAM_NN_VALID PCL_Client *client, SA_PARAM_NN_STR const char *name, SA_PARAM_NN_STR const char *status);

// Set handler for processing HTTP patching information updates.
PCL_ErrorCode	pclPatchServerHandleHttpInfo(SA_PARAM_NN_VALID PCL_Client *client, PCL_HttpInfoCallback callback, void *userData);

// Set handler for processing Autpupdate patching information updates.
PCL_ErrorCode	pclPatchServerHandleAutoupInfo(SA_PARAM_NN_VALID PCL_Client *client, PCL_AutoupInfoCallback callback, void *userData);

// ************************************************************************
// * Feedback callbacks
// ************************************************************************
PCL_ErrorCode	pclSetLoggingFunction(SA_PARAM_NN_VALID PCL_Client *client, PCL_LoggingFunction func, void *userdata);
// These might become obsolete if the client can let someone else handle all waiting on files and networking
PCL_ErrorCode	pclSetForceInScanCallback(SA_PARAM_NN_VALID PCL_Client * client, PCL_ForceInScanCallback callback, void * userData);
PCL_ErrorCode	pclSetUploadCallback(SA_PARAM_NN_VALID PCL_Client * client, PCL_UploadCallback callback, void * userData);
PCL_ErrorCode	pclSetProcessCallback(SA_PARAM_NN_VALID PCL_Client * client, PCL_ProcessCallback callback, void * userData);
PCL_ErrorCode	pclSetMirrorCallback(SA_PARAM_NN_VALID PCL_Client * client, PCL_MirrorCallback callback, void * userData);
PCL_ErrorCode	pclSetExamineCallback(SA_PARAM_NN_VALID PCL_Client * client, PCL_MirrorCallback callback, void * userData);
PCL_ErrorCode	pclSetExamineCompleteCallback(SA_PARAM_NN_VALID PCL_Client * client, PCL_MirrorCallback callback, void * userData);
PCL_ErrorCode	pclSetNetCallbacks(SA_PARAM_NN_VALID PCL_Client * client, PCL_NetCallback enter_callback, PCL_NetCallback leave_callback, void * userData);


// ************************************************************************
// * Little stuff
// ************************************************************************
PCL_ErrorCode	pclXfers(SA_PARAM_NN_VALID PCL_Client * client, int * xfers);
PCL_ErrorCode	pclMaxXfers(SA_PARAM_NN_VALID PCL_Client * client, int * max_xfers);
PCL_ErrorCode	pclRootDir(SA_PARAM_NN_VALID PCL_Client * client, char * buf, int buf_size);
PCL_ErrorCode	pclFixPath(SA_PARAM_NN_VALID PCL_Client * client, char * path, int buf_size);
PCL_ErrorCode 	pclCountCheckouts(SA_PARAM_NN_VALID PCL_Client *client, int *count);
PCL_ErrorCode 	pclGetCheckouts(SA_PARAM_NN_VALID PCL_Client *client, char ***checkouts);
PCL_ErrorCode 	pclExistsInDb(SA_PARAM_NN_VALID PCL_Client *client, SA_PARAM_NN_STR const char *filename, bool *exists);
PCL_ErrorCode 	pclIsDeleted(SA_PARAM_NN_VALID PCL_Client *client, SA_PARAM_NN_STR const char *dbname, bool *deleted);
PCL_ErrorCode 	pclIsUnderSourceControl(SA_PARAM_NN_VALID PCL_Client *client, SA_PARAM_NN_STR const char *dbname, bool *under_control);

// Is this file considered to be "included" in the filespec?
PCL_ErrorCode 	pclIsIncluded(SA_PARAM_NN_VALID PCL_Client *client, SA_PARAM_NN_STR const char *dbname, bool *is_included);

// Debug command for testing if a file is marked as required.
PCL_ErrorCode	pclIsNotRequired(PCL_Client * client, const char *filename, bool * required, char ** estrDebug);

PCL_ErrorCode 	pclGetAuthorUnsafe(SA_PARAM_NN_VALID PCL_Client *client, SA_PARAM_NN_STR const char *dbname, const char **author);
PCL_ErrorCode 	pclGetLockAuthorUnsafe(SA_PARAM_NN_VALID PCL_Client *client, SA_PARAM_NN_STR const char *dbname, const char **author);
// PCL_ErrorCode	pclManifestRootDir(SA_PARAM_NN_VALID PCL_Client * client, char * buf, int buf_size);
PCL_ErrorCode	pclGetStateString(PCL_Client* client, char * buf, size_t buf_size);

// Format basic generic error string.
PCL_ErrorCode	pclGetErrorString(PCL_ErrorCode error, char * buf, size_t buf_size);

// Get additional error information for last error, if any.
PCL_ErrorCode	pclGetErrorDetails(PCL_Client* client, SA_PRE_GOOD SA_POST_NN_RELEMS(1) const char **error_details);

PCL_ErrorCode	pclLoadHoggs(PCL_Client * client);
PCL_ErrorCode	pclUnloadHoggs(PCL_Client * client);
PCL_ErrorCode	pclSendLog_dbg(PCL_Client * client, const char *action, FORMAT_STR const char *fmt_str, ...);
#define pclSendLog(client, action, fmt_str, ...) pclSendLog_dbg(client, action, FORMAT_STRING_CHECKED(fmt_str), __VA_ARGS__)
PCL_ErrorCode	pclSendLogHttpStats(PCL_Client * client);
PCL_ErrorCode	pclPing(PCL_Client * client, SA_PARAM_OP_STR const char *data, PCL_PingCallback callback, void *userdata);
PCL_ErrorCode	pclHeartbeat(PCL_Client * client, const char *name);
PCL_ErrorCode	pclSetPrefix(PCL_Client * client, const char *prefix);
PCL_ErrorCode	pclGetPrefix(PCL_Client * client, char * buf, size_t buf_size);

PCL_ErrorCode	pclSetRetryTimes(PCL_Client * client, F32 timeout, F32 backoff);

// Get the number of bytes remaining to download in the autoupdate packet, or zero if unknown.
PCL_ErrorCode	pclGetAutoupdateRemaining(PCL_Client * client, U32 * remaining);

// Verifies server and client time are similar
PCL_ErrorCode	pclVerifyServerTimeDifference(PCL_Client * client, bool * should_warn, U32 * client_time, U32 * server_time);

// ************************************************************************
// * Features useable without running a patchclient (some might run a client internally)
// ************************************************************************
PCL_ErrorCode	pclCheckCurrentView(const char * dir, char * label_buf, int label_buf_size, U32 * view_time, int * branch,
									char * sandbox_buf, int sandbox_buf_size, char * view_dir_buf, int view_dir_buf_size);
PCL_ErrorCode	pclCheckCurrentLink(const char * dir, char * ip_buf, int ip_buf_size, int * port, char * project_buf, int project_buf_size,
									int * branch, bool * no_upload, char * link_dir_buf, int link_dir_buf_size);

// PCL reference data zipping function, to attempt to ensure consistency in zipping critical for bindiff performance.
PCL_ErrorCode	pclZipData(SA_PRE_NN_ELEMS_VAR(src_len) SA_POST_OP_VALID void *src, U32 src_len, SA_PRE_NN_FREE SA_POST_NN_VALID U32 *zip_size_p, SA_PRE_NN_FREE SA_POST_NN_VALID char **zipped);

// ************************************************************************
// * For the insane who want to break their patchers
// ************************************************************************
PCL_ErrorCode	pclReportError(PCL_Client * client, PCL_ErrorCode error_code, char * description);

#if !PLATFORM_CONSOLE
PCL_ErrorCode	pclInitForShellExt(void);
PCL_ErrorCode	pclLinkDialog(const char * dir, bool message_loop);
PCL_ErrorCode	pclSyncDialog(const char * dir, bool message_loop);
PCL_ErrorCode	pclGetLatestDialog(const char ** dirs, int count, bool message_loop);
PCL_ErrorCode	pclMirrorDialog(const char ** dirs, int count, bool message_loop);
PCL_ErrorCode	pclUnmirrorDialog(const char ** dirs, int count, bool message_loop);
#endif

// The PCL xferrer has hung; report this to the Patch Server before we reset.
PCL_ErrorCode	pclReportResetAfterHang(PCL_Client * client, F32 elapsed);

// Set a directory to write failed checksum verification data to.
PCL_ErrorCode	pclSetBadFilesDirectory(PCL_Client * client, const char *directory);

// Set client max_net_bytes.
PCL_ErrorCode	pclSetMaxNetBytes(PCL_Client * client, U32 max_net_bytes);

// Set client max_mem_usage.
PCL_ErrorCode	pclSetMaxMemUsage(PCL_Client * client, U64 max_mem_usage);

// ************************************************************************
// * Miscellaneous stuff that should probably be categorized into one of the above categories...
// ************************************************************************

void pcllog_dbg(PCL_Client *client, PCL_LogLevel level, FORMAT_STR const char *fmt, ...);
#define pcllog(client, level, fmt, ...) pcllog_dbg(client, level, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

// If true, disable automatic redirection requests from server.
bool			pclNoRedirect(bool enabled);

// If true, ignore HTTP patching information from the server.
bool			pclNoHttp(bool enabled);

// patchxfer.c

extern bool g_patcher_encrypt_connections;

void pclGetUsefulDebugString(PCL_Client *client, char **ppOutDebugString);
char *pclGetUsefulDebugString_Static(PCL_Client *client);

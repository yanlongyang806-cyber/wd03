#if !PLATFORM_CONSOLE

#include "DirMonitor.h"
#include "earray.h"
#include "memlog.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include "timing.h"
#include "utils.h"
#include "winfiletime.h"
#include "UTF8.h"

//#define TESTING_IO_COMPLETION 1

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FileSystem););


typedef void (*DirMonCompletionPortCallback)(DirMonCompletionPortCallbackParams* params);

typedef struct DirMonCompletionPortObject {
	U16								selfIndex;
	HANDLE							handle;
	DirMonCompletionPortCallback	callback;
	void*							userData;
	const char*						fileName;
	S32								lineNumber;
	U16								uid;
} DirMonCompletionPortObject;

typedef struct DirChangeInfoImp {
	DirChangeInfo info;

	HANDLE		dir;
	OVERLAPPED	ol;
	char		*dirChangeBuffer;
	int			dirChangeBufferSizeOrig;
	DWORD		dirChangeBufferSize;
	U32			completionHandle;
} DirChangeInfoImp;

typedef struct DirMonitor
{
	int dir_mon_flags;
	DirChangeInfoImp **dirChangeInfos;
	HANDLE dirMonCompletionPort;
	int dir_mon_buffer_size;
	bool dir_mon_started;

	struct {
		struct {
			DirMonCompletionPortObject*	object;
			S32							count;
			S32							maxCount;
		} objects;

		struct {
			U32*						index;
			U32							count;
			U32							maxCount;
		} available;

		U16								curUID;
	} dirMonCompletionObjects;

	struct {
		DirChangeInfoImp*			info;
		FILE_NOTIFY_INFORMATION*	fileChangeInfo;
		DirChangeInfo**				bufferOverrun;
	} curDirMonOp;

} DirMonitor;

static DirMonitor g_default_dirMonitor;
#define GET_DEFAULT_DIRMONITOR int dummy_default_dirmon = (dirMonitor?1:((dirMonitor = &g_default_dirMonitor),0))

DirMonitor *dirMonCreate(void)
{
	DirMonitor *ret = calloc(sizeof(*ret), 1);
	// Don't put any initialization in here, it's not called on the default DirMonitor
	return ret;
}

static void dirMonOperationCompleted(DirMonCompletionPortCallbackParams* params);

void dirMonSetFlags(DirMonitor *dirMonitor, int flags)
{
	GET_DEFAULT_DIRMONITOR;
	dirMonitor->dir_mon_flags = flags;
}

void dirMonSetBufferSize(DirMonitor *dirMonitor, int size)
{
	GET_DEFAULT_DIRMONITOR;
	assert(!dirMonitor->dir_mon_started);
	dirMonitor->dir_mon_buffer_size = size;
}

// Returns 0 on failure
static int dirMonStartAsyncMonitor(DirChangeInfoImp* info){
	int result;
	result = ReadDirectoryChangesW(
		info->dir,							// handle to directory
		info->dirChangeBuffer,				// read results buffer
		info->dirChangeBufferSizeOrig,		// length of buffer
		1,									// monitoring option
		FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES |
		FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE | // FILE_NOTIFY_CHANGE_LAST_ACCESS |
		FILE_NOTIFY_CHANGE_CREATION | FILE_NOTIFY_CHANGE_SECURITY,                               // filter conditions
		&info->dirChangeBufferSize,		// bytes returned
		&info->ol,						// overlapped buffer
		NULL							// completion routine
		);
		
	#if TESTING_IO_COMPLETION
		printf("starting monitor: h=%d, ol=%d\n", info->dir, info->ol);
	#endif
		
	//assert(result && "DirMonitor init failed - not available on Network drives - dynamic reloading disabled, press Ignore.");
	return result;
}

static void dirMonFreeChangeInfo(DirMonitor *dirMonitor, DirChangeInfoImp* info){
	dirMonCompletionPortRemoveObject(dirMonitor, info->completionHandle);
	CloseHandle(info->dir);
	SAFE_FREE(info->dirChangeBuffer);
	SAFE_FREE(info->info.userData);
	free(info);
}

U32 dirMonCompletionPortAddObjectDbg(	DirMonitor *dirMonitor, 
										HANDLE handle,
										void* userData,
										DirMonCompletionPortCallback callback,
										const char* fileName,
										S32 lineNumber)
{
	GET_DEFAULT_DIRMONITOR;
	DirMonCompletionPortObject*			object;
	U32 								completionHandle;
	U32									index;
	STATIC_INFUNC_ASSERT(sizeof(ULONG_PTR) >= sizeof(U32));
	
	assert(dirMonitor->dirMonCompletionObjects.objects.count < (1 << 16));
	
	if(dirMonitor->dirMonCompletionObjects.available.count){
		assert(dirMonitor->dirMonCompletionObjects.available.count > 0);
		
		index = dirMonitor->dirMonCompletionObjects.available.index[--dirMonitor->dirMonCompletionObjects.available.count];
		
		assert(index < (U32)dirMonitor->dirMonCompletionObjects.objects.count);
		
		#if TESTING_IO_COMPLETION
			printf("Adding completion object: index=%d\n", index);
		#endif
		
		object = dirMonitor->dirMonCompletionObjects.objects.object + index;
		
		assert(!object->handle);
	}else{
		index = dirMonitor->dirMonCompletionObjects.objects.count;
		
		#if TESTING_IO_COMPLETION
			printf("Adding completion object: index=%d\n", index);
		#endif
		
		object = dynArrayAddStructType(	DirMonCompletionPortObject,
										dirMonitor->dirMonCompletionObjects.objects.object,
										dirMonitor->dirMonCompletionObjects.objects.count,
										dirMonitor->dirMonCompletionObjects.objects.maxCount);
	}
	
	while(!++dirMonitor->dirMonCompletionObjects.curUID);
	
	object->uid = dirMonitor->dirMonCompletionObjects.curUID;
	
	completionHandle = (index) | ((U32)object->uid << 16);

	object->selfIndex = index;
	object->callback = callback;
	object->handle = handle;
	object->userData = userData;
	object->fileName = fileName;
	object->lineNumber = lineNumber;
	
	//printf("Added 0x%8.8x: %s:%d\n", object, fileName, lineNumber);

	if(	handle ||
		!dirMonitor->dirMonCompletionPort)
	{
		dirMonitor->dirMonCompletionPort = CreateIoCompletionPort(	handle ?
																		handle :
																		INVALID_HANDLE_VALUE,
																	dirMonitor->dirMonCompletionPort,
																	(ULONG_PTR)completionHandle,
																	0);
	
		assert(dirMonitor->dirMonCompletionPort);
	}
	
	return completionHandle;
}

static DirMonCompletionPortObject* decodeCompletionHandle(DirMonitor *dirMonitor, U32 handle, S32* indexOut)
{
	S32 index = handle & 0xffff;
	S32 uid = (handle >> 16) & 0xffff;
	
	if(index >= 0 && index < dirMonitor->dirMonCompletionObjects.objects.count){
		DirMonCompletionPortObject* object = dirMonitor->dirMonCompletionObjects.objects.object + index;
		
		if(object->uid == uid){
			if(indexOut){
				*indexOut = index;
			}
			
			return object;
		}
	}
	
	return NULL;
}

void dirMonCompletionPortRemoveObject(DirMonitor *dirMonitor, U32 handle)
{
	GET_DEFAULT_DIRMONITOR;
	S32							index;
	DirMonCompletionPortObject* object = decodeCompletionHandle(dirMonitor, handle, &index);
	S32*						availableIndex;
	
	if(!object){
		return;
	}
	
	//printf("Removed 0x%8.8x: %s:%d\n", object, object->fileName, object->lineNumber);

	assert(object->selfIndex == index);
	
	if(!CancelIo(object->handle)){
		printf("CancelIo failed: %d\n", GetLastError());
	}else{
		//printf("CancelIo success: 0x%8.8x (%s:%d) error %d\n", object, object->fileName, object->lineNumber, GetLastError());
	}
	
	availableIndex = dynArrayAddStructType(	S32,
											dirMonitor->dirMonCompletionObjects.available.index,
											dirMonitor->dirMonCompletionObjects.available.count,
											dirMonitor->dirMonCompletionObjects.available.maxCount);
											
	*availableIndex = index;
		
	ZeroStruct(object);
}

int dirMonAddDirectory(DirMonitor *dirMonitor, const char* dirname, int monitorSubDirs, void* userData)
{
	GET_DEFAULT_DIRMONITOR;
	DirChangeInfoImp* info;
	HANDLE dir;
	char absdirname[CRYPTIC_MAX_PATH];

	assert(dirname);
	assert(strlen(dirname) <= CRYPTIC_MAX_PATH);
	if (*dirname == '.')
	{
		fullpath_UTF8(absdirname, dirname, ARRAY_SIZE(absdirname));
	}
	else
		strcpy(absdirname, dirname);

	// Start an async dir change monitor operation.
	//	Open the directory for monitoring.
	//		Get a handle to the directory to be watched.

	//		Make sure we share the directory in such a way that other programs
	//		are allowed full access to all files + directories.
	dir = CreateFile_UTF8(
		absdirname,											// pointer to the file name
		FILE_LIST_DIRECTORY,								// access (read/write) mode
		FILE_SHARE_WRITE|FILE_SHARE_READ|FILE_SHARE_DELETE,  // share mode
		NULL,												// security descriptor
		OPEN_EXISTING,										// how to create
		FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OVERLAPPED,	// file attributes
		NULL												// file with attributes to copy
		);

	//		Does the specified directory exist at all?
	//		If not, it cannot be watched.
	if(INVALID_HANDLE_VALUE == dir){
		return 0;
	}

	if (!dirMonitor->dir_mon_buffer_size) {
#if TESTING_IO_COMPLETION
		dirMonitor->dir_mon_buffer_size = 100;
#else
		dirMonitor->dir_mon_buffer_size = 512*1024;
#endif
	}


	//	Create a new dir change info.
	info = (DirChangeInfoImp*)calloc(1, sizeof(DirChangeInfoImp));
	info->dirChangeBuffer = (char*)calloc(1, dirMonitor->dir_mon_buffer_size);
	#if TESTING_IO_COMPLETION
		printf("Change buffer: %p\n", info->dirChangeBuffer);
	#endif
	dirMonitor->dir_mon_started = true;
	info->dirChangeBufferSizeOrig = dirMonitor->dir_mon_buffer_size;
	eaPush(&dirMonitor->dirChangeInfos, info);
	strcpy(info->info.dirname, absdirname);
	info->dir = dir;

	// Create the completion port BEFORE starting I/O.
	
	info->completionHandle = dirMonCompletionPortAddObject(dirMonitor, info->dir, info, dirMonOperationCompleted);
	
	info->info.userData = userData;
	
	//	Watch for changes on the specified directory.
	if (!dirMonStartAsyncMonitor(info)) {
		// Could not start it, free memory, and return
		eaPop(&dirMonitor->dirChangeInfos);
		dirMonFreeChangeInfo(dirMonitor, info);
		return 0;
	}

	// Add the new async operation to the the IO completion port.
	
	return 1;
}

void dirMonRemoveDirectory(DirMonitor *dirMonitor, const char* dirname)
{
	GET_DEFAULT_DIRMONITOR;
	S32 i;
	char absdirname[CRYPTIC_MAX_PATH];
	if (*dirname == '.')
	{
		fullpath_UTF8(absdirname, dirname, ARRAY_SIZE(absdirname));
	}
	else
		strcpy(absdirname, dirname);
	for(i = 0; i < eaSize(&dirMonitor->dirChangeInfos); i++){
		DirChangeInfoImp* info = dirMonitor->dirChangeInfos[i];
		
		if(!stricmp(info->info.dirname, absdirname)){
			eaRemove(&dirMonitor->dirChangeInfos, i);
			dirMonFreeChangeInfo(dirMonitor, info);
			break;
		}
	}
}

void dirMonDestroy(DirMonitor *dirMonitor)
{
	S32 i;
	assert(dirMonitor!=NULL); // Can't free the default dirMonitor.
	for(i = 0; i < eaSize(&dirMonitor->dirChangeInfos); i++){
		DirChangeInfoImp* info = dirMonitor->dirChangeInfos[i];
		dirMonFreeChangeInfo(dirMonitor, info);
	}
	eaDestroy(&dirMonitor->dirChangeInfos);
	SAFE_FREE(dirMonitor->dirMonCompletionObjects.objects.object);
	SAFE_FREE(dirMonitor->dirMonCompletionObjects.available.index);
	CloseHandle(dirMonitor->dirMonCompletionPort);
	free(dirMonitor);
}


static FILE_NOTIFY_INFORMATION *fniExtractFilename(FILE_NOTIFY_INFORMATION *fileChangeInfo, char *filename) {
	unsigned int i;
	// Turn the returned filename into a null terminating string.
	for(i = 0; i < fileChangeInfo->FileNameLength >> 1; i++){
		filename[i] = (char)fileChangeInfo->FileName[i];
	}
	filename[i] = '\0';
	if (fileChangeInfo->NextEntryOffset) {
		return (FILE_NOTIFY_INFORMATION *)((char*)fileChangeInfo + fileChangeInfo->NextEntryOffset);
	} else {
		return NULL;
	}
}

static void dirMonOperationCompleted(DirMonCompletionPortCallbackParams* params)
{
	DirMonitor *dirMonitor = params->dirMonitor;
	assert(dirMonitor);
	if(params->result){
		dirMonitor->curDirMonOp.info = (DirChangeInfoImp*)params->userData;
		
		if(params->bytesTransferred) {
			dirMonitor->curDirMonOp.fileChangeInfo = (FILE_NOTIFY_INFORMATION*)dirMonitor->curDirMonOp.info->dirChangeBuffer;
		} else {
			// Buffer overrun!
			dirMonitor->curDirMonOp.info->info.filename = NULL;
			dirMonitor->curDirMonOp.info->info.lastChangeTime = dirMonitor->curDirMonOp.info->info.changeTime;
			dirMonitor->curDirMonOp.info->info.changeTime = time(NULL);
			if(dirMonitor->curDirMonOp.bufferOverrun){
				*dirMonitor->curDirMonOp.bufferOverrun = &dirMonitor->curDirMonOp.info->info;
			}
			dirMonStartAsyncMonitor(dirMonitor->curDirMonOp.info);
		}
	}
	else if(params->aborted){
		dirMonitor->curDirMonOp.info = (DirChangeInfoImp*)params->userData;

		//printfColor(COLOR_BRIGHT|COLOR_RED, "Aborted dirMon operation!\n");
		memlog_printf(NULL, "Aborted dirMon operation!");
		// Treat as buffer overrun
		dirMonitor->curDirMonOp.info->info.filename = NULL;
		dirMonitor->curDirMonOp.info->info.lastChangeTime = dirMonitor->curDirMonOp.info->info.changeTime;
		dirMonitor->curDirMonOp.info->info.changeTime = time(NULL);
		if(dirMonitor->curDirMonOp.bufferOverrun){
			*dirMonitor->curDirMonOp.bufferOverrun = &dirMonitor->curDirMonOp.info->info;
		}
		dirMonStartAsyncMonitor(dirMonitor->curDirMonOp.info);
	}
}

static U32 g_saved_last_error;

DirChangeInfo* dirMonCheckDirs(DirMonitor *dirMonitor, int timeout, DirChangeInfo** bufferOverrun)
{
	GET_DEFAULT_DIRMONITOR;
	
	PERFINFO_AUTO_START_FUNC();
	
	dirMonitor->curDirMonOp.bufferOverrun = bufferOverrun;

	if(bufferOverrun){
		*bufferOverrun = NULL;
	}

	if (!dirMonitor->curDirMonOp.fileChangeInfo && dirMonitor->dirMonCompletionPort) {
		int result;
		DWORD bytesTransferred;
		OVERLAPPED* ol;
		ULONG_PTR handle = 0;
		U32 errorCode;
		
		#if TESTING_IO_COMPLETION
			printf("Waiting for %d completion objects.\n", dirMonitor->dirMonCompletionObjects.objects.count);
		#endif
		
		// Poll the IO completion port for directory changes.

		PERFINFO_AUTO_START_BLOCKING("GetQueuedCompletionStatus", 1);
		result = GetQueuedCompletionStatus(dirMonitor->dirMonCompletionPort, &bytesTransferred, &handle, &ol, timeout);
		g_saved_last_error = errorCode = GetLastError();
		PERFINFO_AUTO_STOP();
		
		#if TESTING_IO_COMPLETION
			printf(	"Completed: r=%d, b=%d, h=%d, ol=%d\n",
					result,
					bytesTransferred,
					(U32)handle,
					ol);
		#endif
		
		if(handle){
			DirMonCompletionPortObject* object = decodeCompletionHandle(dirMonitor, (U32)handle, NULL);
			
			#if TESTING_IO_COMPLETION
				printf("Completed something: r=%d, h=%d, o=%d, ol=%d\n", result, (U32)handle, object, ol);
			#endif

			if(object){
				DirMonCompletionPortCallbackParams params;
				
				params.dirMonitor = dirMonitor;
				params.result = result;
				params.aborted = !result && errorCode == ERROR_OPERATION_ABORTED;
				params.bytesTransferred = bytesTransferred;
				params.handle = object->handle;
				params.overlapped = ol;
				params.userData = object->userData;
				
				object->callback(&params);
			}
		}else{
			#if TESTING_IO_COMPLETION
				printf("Completed with no handle: r=%d, ol=%d\n", result, ol);
			#endif
		}
	}
	
	// Intentionally absent "else" because "dirMonitor->curDirMonOp.fileChangeInfo" can change in the io completion callback.
	
	if(dirMonitor->curDirMonOp.fileChangeInfo){
		static char filename[512];
		
		// There is still file change info to process.
		
		struct _stat32i64 fileInfo; // 32-bit time, 64-bit size

		assert(dirMonitor->curDirMonOp.info);

		
		dirMonitor->curDirMonOp.info->info.isDelete =	dirMonitor->curDirMonOp.fileChangeInfo->Action == FILE_ACTION_REMOVED ||
											dirMonitor->curDirMonOp.fileChangeInfo->Action == FILE_ACTION_RENAMED_OLD_NAME;

		dirMonitor->curDirMonOp.info->info.isCreate =	dirMonitor->curDirMonOp.fileChangeInfo->Action == FILE_ACTION_ADDED ||
											dirMonitor->curDirMonOp.fileChangeInfo->Action == FILE_ACTION_RENAMED_NEW_NAME;

		dirMonitor->curDirMonOp.fileChangeInfo = fniExtractFilename(dirMonitor->curDirMonOp.fileChangeInfo, filename);
		
		dirMonitor->curDirMonOp.info->info.filename = filename;

		if (!(dirMonitor->dir_mon_flags & DIR_MON_NO_TIMESTAMPS)) {
			dirMonitor->curDirMonOp.info->info.lastChangeTime = dirMonitor->curDirMonOp.info->info.changeTime;

			// If the file still exists, use the file time as the last change time...
			if(0 == cryptic_stat32i64_utc(filename, &fileInfo)){
				dirMonitor->curDirMonOp.info->info.changeTime = fileInfo.st_mtime;
			}else{
				// Otherwise, use the current system time as the last change time.
				dirMonitor->curDirMonOp.info->info.changeTime = time(NULL);
			}
		}
		if (dirMonitor->curDirMonOp.fileChangeInfo == NULL) { // We've returned all there is to return
			dirMonStartAsyncMonitor(dirMonitor->curDirMonOp.info);
		}
		PERFINFO_AUTO_STOP();// FUNC.
		return &dirMonitor->curDirMonOp.info->info;
	}
	PERFINFO_AUTO_STOP();// FUNC.
	return NULL;
}

void dirMonPostCompletion(	DirMonitor* dirMonitor,
							int completionKey)
{
	GET_DEFAULT_DIRMONITOR;
	PostQueuedCompletionStatus(	dirMonitor->dirMonCompletionPort,
								0,
								completionKey,
								NULL);
}

#else

#include "DirMonitor.h"

// Stub out the xbox implementation for now

int dirMonAddDirectory(DirMonitor *dirMonitor, const char* dirname, int monitorSubDirs, void* userData)
{
	return 0;
}
void dirMonRemoveDirectory(DirMonitor *dirMonitor, const char* dirname)
{
}

DirChangeInfo* dirMonCheckDirs(DirMonitor *dirMonitor, int timeout, DirChangeInfo** bufferOverrun)
{
	if(bufferOverrun){
		*bufferOverrun = NULL;
	}
	return 0;
}

void dirMonSetFlags(DirMonitor *dirMonitor, int flags)
{
}


void dirMonSetBufferSize(DirMonitor *dirMonitor, int size)
{
}

DirMonitor *dirMonCreate(void)
{
	return NULL;
}

#endif


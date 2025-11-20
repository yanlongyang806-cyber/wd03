#include "fileWatch.h"
#include "file.h"
#include "fileutil.h"
#include "mutex.h"
#include <process.h>
#include "strings_opt.h"
#include <sys/stat.h>
#include "sysutil.h"
#include "timing.h"
#include "utilitieslib.h"
#include "utils.h"
#include "winfiletime.h"
#include "UTF8.h"
#include "StringUtil.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FileSystem););

// Initialize disable_until_count with this.
#define DISABLE_UNTIL_COUNT_VALUE 10

typedef struct FindFileData {
	U32					handle;
	U32					noMatches;
	WIN32_FIND_DATAA*	wfd;
} FindFileData;

typedef struct StatData {
	S32					folderTracked;
	FWStatType*			statInfo;
} StatData;
STATIC_ASSERT(sizeof(FWStatType) == sizeof(struct _stat32));

static struct {

	// Server timeout support
	U32					timeout;						// Timeout, in milliseconds
	U32					disable_until_count;			// Disable server requests for this many more operations
	S64					disable_until_time;				// Disable server requests until timerCpuTicks64() reaches this value

	HANDLE				hPipe;
	CRITICAL_SECTION	cs;
	FileWatchBuffer*	buffer;

	// Disable flag
	U32					disabled	: 1;
} fileWatchClient = {1000};

// FileWatchBuffer functions.

void fwWriteBufferData(FileWatchBuffer* writeBuffer, const void* data, U32 byteCount){
	U32 remaining;

	remaining = writeBuffer->maxByteCount - writeBuffer->curByteCount;

	byteCount = min(byteCount, remaining);

	memcpy(writeBuffer->buffer + writeBuffer->curByteCount, data, byteCount);

	writeBuffer->curByteCount += byteCount;
	writeBuffer->curBytePos = writeBuffer->curByteCount;
}

void fwWriteBufferU32(FileWatchBuffer* writeBuffer, U32 value){
	fwWriteBufferData(writeBuffer, &value, sizeof(value));
}

void fwWriteBufferString(FileWatchBuffer* writeBuffer, const char* str){
	fwWriteBufferData(writeBuffer, str, (S32)strlen(str) + 1);
}

void fwReadBufferData(FileWatchBuffer* readBuffer, void* buffer, U32 byteCount){
	U32 remaining = readBuffer->curByteCount - readBuffer->curBytePos;

	byteCount = min(byteCount, remaining);

	memcpy(buffer, readBuffer->buffer + readBuffer->curBytePos, byteCount);

	readBuffer->curBytePos += byteCount;
}

U64 fwReadBufferU64(FileWatchBuffer* readBuffer){
	U64 value = 0;

	fwReadBufferData(readBuffer, &value, sizeof(value));

	return value;
}

U32 fwReadBufferU32(FileWatchBuffer* readBuffer){
	U32 value = 0;

	fwReadBufferData(readBuffer, &value, sizeof(value));

	return value;
}

U8 fwReadBufferU8(FileWatchBuffer* readBuffer){
	U8 value = 0;

	fwReadBufferData(readBuffer, &value, sizeof(value));

	return value;
}

const char* fwReadBufferString(FileWatchBuffer* readBuffer){
	const char* str = readBuffer->buffer + readBuffer->curBytePos;

	while(readBuffer->curBytePos < readBuffer->curByteCount){
		if(!readBuffer->buffer[readBuffer->curBytePos++]){
			return str;
		}
	}

	return "";
}

#if !_PS3
static HANDLE createMutex(const char *name) {
    HANDLE hMutex;
    hMutex = CreateMutex_UTF8(NULL, FALSE, name);
    return hMutex;
}

static S32 acquireMutex(HANDLE hMutex, S32 wait){
	DWORD dwResult;
	WaitForSingleObjectWithReturn(hMutex, wait, dwResult);
	switch(dwResult){
		xcase WAIT_OBJECT_0:
		case WAIT_ABANDONED:{
			return 1;
		}
	}

	return 0;
}

static void releaseAndCloseMutex(HANDLE hMutex){
	ReleaseMutex(hMutex);
	CloseHandle(hMutex);
}
#endif

// File watcher functions.
#if !PLATFORM_CONSOLE

// Disables filewatcher
AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void fileWatchSetDisabled(S32 disabled){
	fileWatchClient.disabled = disabled ? 1 : 0;
}

// Disables filewatcher
AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void fileWatchSetTimeout(U32 timeout){
	fileWatchClient.timeout = timeout;
}

const char* fileWatchGetPipeName(void){
	return "\\\\.\\pipe\\CrypticFileWatchClientPipe";
}

static FileWatchBuffer* getStaticBuffer(void){
	if(!fileWatchClient.buffer){
		fileWatchClient.buffer = calloc(sizeof(*fileWatchClient.buffer), 1);

		fileWatchClient.buffer->buffer = calloc(1000, 1);
		fileWatchClient.buffer->maxByteCount = 1000;
	}

	return fileWatchClient.buffer;
}

static void freeStaticBuffer(void){
	if(fileWatchClient.buffer){
		SAFE_FREE(fileWatchClient.buffer->buffer);
		SAFE_FREE(fileWatchClient.buffer);
	}
}

const char* fileWatchGetCheckRunningMutexName(void){
	return "Global\\FileWatcherCheckRunning";
}

const char* fileWatchGetRunningMutexName(void){
	return "Global\\FileWatcherRunning";
}

static void fileWatchDisconnect(void){
	if(fileWatchClient.hPipe){
		CloseHandle(fileWatchClient.hPipe);

		fileWatchClient.hPipe = NULL;

		freeStaticBuffer();
	}
}

static FileWatchBuffer* startMessageToServer(U32 cmd){
	FileWatchBuffer* writeBuffer = getStaticBuffer();

	writeBuffer->curByteCount = 0;

	fwWriteBufferU32(writeBuffer, cmd);

	return writeBuffer;
}

static void readFindFileInfo(FileWatchBuffer* readBuffer, FindFileData* data){
	assert(data->wfd);

	ZeroStruct(data->wfd);

	assert(data->wfd);

	Strncpyt(data->wfd->cFileName, fwReadBufferString(readBuffer));

	fwReadBufferData(readBuffer, &data->wfd->ftLastWriteTime, sizeof(data->wfd->ftLastWriteTime));

	data->wfd->nFileSizeLow = fwReadBufferU32(readBuffer);

	data->wfd->dwFileAttributes = fwReadBufferU32(readBuffer);
}

static void handleMessageFromServer(FileWatchBuffer* readBuffer, U32 cmdExpected, void* dataOut){
	U32 cmd = fwReadBufferU32(readBuffer);

	if(cmd != cmdExpected){
		assertmsg(0, "Wrong message from server!");
		return;
	}

	switch(cmd){
		xcase FWM_S2C_READY_FOR_REQUESTS:{
			// Yay!
		}

		xcase FWM_S2C_FIND_FIRST_FILE_REPLY:{
			FindFileData* data = dataOut;

			U32 handle = fwReadBufferU32(readBuffer);

			if(data){
				data->handle = handle;

				if(handle){
					// The folder is in the watch list...

					if(fwReadBufferU32(readBuffer)){
						// ...and at least one file matches.

						data->noMatches = 0;

						readFindFileInfo(readBuffer, data);
					}else{
						// ...but no files match.

						data->noMatches = 1;
					}
				}
			}
		}

		xcase FWM_S2C_FIND_NEXT_FILE_REPLY:{
			FindFileData* data = dataOut;

			U32 handle = fwReadBufferU32(readBuffer);

			assert(!handle || handle == data->handle);

			data->handle = handle;

			if(handle){
				readFindFileInfo(readBuffer, data);
			}
		}

		xcase FWM_S2C_FIND_CLOSE_REPLY:{
			FindFileData* data = dataOut;

			U32 handle = fwReadBufferU32(readBuffer);

			assert(!handle || handle == data->handle);

			data->handle = handle;
		}

		xcase FWM_S2C_STAT_REPLY:{
			StatData* statData = dataOut;

			if(!fwReadBufferU32(readBuffer)){
				statData->folderTracked = 0;
			}else{
				statData->folderTracked = 1;

				if(!fwReadBufferU32(readBuffer)){
					statData->statInfo = NULL;
				}else{
					FWStatType* statInfo = statData->statInfo;

					statInfo->st_mtime = fwReadBufferU32(readBuffer);

					statInfo->st_size = fwReadBufferU32(readBuffer);

					statInfo->st_mode = fwReadBufferU32(readBuffer);
				}
			}
		}

		xdefault:{
			// Unhandled message.
		}
	}
}

// Delay trying to open a new pipe to a server for a certain amount of time.
static void delayServerRetry()
{

	verbose_printf("FileWatcher timeout: temporarily disabling FileWatcher requests.\n");

	// Don't check if time has passed for this many more operations.
	// This divides the penalty of rechecking the time by this factor.
	fileWatchClient.disable_until_count = DISABLE_UNTIL_COUNT_VALUE;

	// Do not try to connect again until this amount of time has passed
	fileWatchClient.disable_until_time = timerCpuTicks64() + timerCpuSpeed64() * 60 * 2;

	// Future Idea:
	// Hypothetically, some other mechanism could be divised to tell when FileWatcher is scanning or not, like
	// an interprocess semaphore.  Then rather than doing a relatively slow retry after disable_until_time,
	// we could do a much quicker check to see if FileWatcher was back.  This could also be used to
	// preemptively detect that FileWatcher was scanning, so we wouldn't even have to stall the first time.
}

static S32 readMessageFromServer(U32 cmdExpected, void* dataOut){
	FileWatchBuffer* readBuffer;
	static OVERLAPPED ol = {0};
	U32 result;

	PERFINFO_AUTO_START_FUNC();

	readBuffer = getStaticBuffer();

	// Allocate an event object.
	if (!ol.hEvent)
	{
		ol.hEvent = CreateEvent(NULL, TRUE, TRUE, NULL);
		if (!ol.hEvent)
		{
			fileWatchDisconnect();
			PERFINFO_AUTO_STOP();
			return 0;
		}
	}

	// Start the read operation.
	if(!ReadFile(	fileWatchClient.hPipe,
					readBuffer->buffer,
					readBuffer->maxByteCount,
					NULL,
					&ol) && GetLastError() != ERROR_IO_PENDING)
	{
		fileWatchDisconnect();
		PERFINFO_AUTO_STOP();
		return 0;
	}

	// Wait for it to complete, up to our timeout.
	WaitForSingleObjectWithReturn(ol.hEvent, fileWatchClient.timeout ? fileWatchClient.timeout : INFINITE, result);
	if (result != WAIT_OBJECT_0)
	{
		CancelIo(fileWatchClient.hPipe);
		delayServerRetry();
		fileWatchDisconnect();
		PERFINFO_AUTO_STOP();
		return 0;
	}

	// Get the result of the read operation.
	if (!GetOverlappedResult(fileWatchClient.hPipe, &ol, &readBuffer->curByteCount, false))
	{
		fileWatchDisconnect();
		PERFINFO_AUTO_STOP();
		return 0;
	}

	readBuffer->curBytePos = 0;

	handleMessageFromServer(readBuffer, cmdExpected, dataOut);

	//printf("Read: %s\n", buffer);

	PERFINFO_AUTO_STOP();

	return 1;
}

static S32 sendMessageToServer(U32 cmdExpected, void* dataOut){
	FileWatchBuffer* writeBuffer = getStaticBuffer();
	DWORD writeByteCount;

	PERFINFO_AUTO_START_FUNC();

	if(!WriteFile(	fileWatchClient.hPipe,
					writeBuffer->buffer,
					writeBuffer->curByteCount,
					&writeByteCount,
					NULL))
	{
		//printf("Write failed: %d\n", GetLastError());

		fileWatchDisconnect();

		PERFINFO_AUTO_STOP();
		return 0;
	}

	// Read the reply from the server.

	PERFINFO_AUTO_STOP();
	return readMessageFromServer(cmdExpected, dataOut);
}

static S32 fileWatchSendConnectMessageToServer(void){
	FileWatchBuffer* writeBuffer = startMessageToServer(FWM_C2S_CONNECT);

	// Write the protocol version.

	fwWriteBufferU32(writeBuffer, 0);

	// Write the PID.

	fwWriteBufferU32(writeBuffer, _getpid());

	// Write the process name.

	fwWriteBufferString(writeBuffer, getExecutableName());

	return sendMessageToServer(FWM_S2C_READY_FOR_REQUESTS, NULL);
}

static S32 canCreateNamedPipe(void){
	HMODULE	hModule;
	S32		retVal = 1;

	typedef HANDLE (*CreateNamedPipeFuncType)(	LPCTSTR lpName,
									DWORD dwOpenMode,
									DWORD dwPipeMode,
									DWORD nMaxInstances,
									DWORD nOutBufferSize,
									DWORD nInBufferSize,
									DWORD nDefaultTimeOut,
									LPSECURITY_ATTRIBUTES lpSecurityAttributes);

	CreateNamedPipeFuncType CreateNamedPipeFunc;

	hModule = LoadLibrary(L"kernel32.dll");

	if(!hModule){
		retVal = 0;
	}else{
		CreateNamedPipeFunc = (CreateNamedPipeFuncType)GetProcAddress(hModule, "CreateNamedPipeA");

		if(!CreateNamedPipeFunc){
			retVal = 0;
		}else{
			char	name[100];
			HANDLE	hPipe;

			sprintf(name, "\\\\.\\pipe\\CrypticCheckPipeFileWatcher.%d.%d", _getpid(), GetCurrentThreadId());

			hPipe = CreateNamedPipe_UTF8(name,
									PIPE_ACCESS_DUPLEX |
										FILE_FLAG_OVERLAPPED,
									PIPE_TYPE_MESSAGE |
										PIPE_READMODE_MESSAGE |
										PIPE_WAIT,
									PIPE_UNLIMITED_INSTANCES,
									10000,
									10000,
									0,
									NULL);

			if(!hPipe){
				retVal = 0;
			}else{
				CloseHandle(hPipe);
			}
		}

		FreeLibrary(hModule);
	}

	return retVal;
}

S32 fileWatcherIsRunning(void)
{
	HANDLE	hMutexCheck = CreateMutex_UTF8(NULL, FALSE, fileWatchGetCheckRunningMutexName());
	HANDLE	hMutexRunning;
	S32		retVal = 1;

	if(!hMutexCheck){
		retVal = 0;
	}else{
		if(!acquireMutex(hMutexCheck, INFINITE)){
			assert(0);
		}

		hMutexRunning = CreateMutex_UTF8(NULL, FALSE, fileWatchGetRunningMutexName());

		if(!hMutexRunning){
			retVal = 0;
		}else{
			if(acquireMutex(hMutexRunning, 0)){
				retVal = 0;

				ReleaseMutex(hMutexRunning);
			}

			CloseHandle(hMutexRunning);
		}

		releaseAndCloseMutex(hMutexCheck);
	}

	return retVal;
}

static S32 fileWatchConnect(S32 connect){
	static S32 checkedCompatibility;
	static S32 isCompatible;

	if(!checkedCompatibility){
		checkedCompatibility = 1;

		if(!canCreateNamedPipe()){
			printf("FileWatcher support is disabled because this machine doesn't support it.\n");

			fileWatchSetDisabled(1);
		}
	}

	// If we previously timed out talking to the server, wait a while before retrying.
	if (fileWatchClient.disable_until_count)
		--fileWatchClient.disable_until_count;
	else if (fileWatchClient.disable_until_time)
	{
		if (timerCpuTicks64() >= fileWatchClient.disable_until_time)
		{
			verbose_printf("Trying to use FileWatcher again.\n");
			fileWatchClient.disable_until_time = 0;
		}
		else
			fileWatchClient.disable_until_count = DISABLE_UNTIL_COUNT_VALUE;
	}

	if(	connect &&
		!fileWatchClient.disabled &&
		!fileWatchClient.disable_until_time)
	{
		fileWatchClient.disable_until_time = 0;
		if(	!fileWatchClient.hPipe &&
			fileWatcherIsRunning())
		{
			const char* pipeName = fileWatchGetPipeName();

			// Wait for a moment for a named pipe instance to be available.
			//   Note: WaitNamedPipe will return 0 immediately if no instances exist.
			while(WaitNamedPipe_UTF8(pipeName, fileWatchClient.timeout ? fileWatchClient.timeout : NMPWAIT_WAIT_FOREVER)){
				// There's a pipe instance available for connecting to.

				fileWatchClient.hPipe = CreateFile_UTF8(	pipeName,
													GENERIC_WRITE | GENERIC_READ,
													0,
													NULL,
													OPEN_EXISTING,
													FILE_FLAG_OVERLAPPED,
													0);

				if(fileWatchClient.hPipe == INVALID_HANDLE_VALUE){
					// The pipe was closed, or another client attached to it, so keep trying.

					fileWatchClient.hPipe = NULL;
				}else{
					// Yay, I got the pipe connection.

					break;
				}
			}

			// If we didn't get one, wait a while before we try again.
			if (!fileWatchClient.hPipe && GetLastError() == ERROR_SEM_TIMEOUT)
				delayServerRetry();

			if(fileWatchClient.hPipe){
				fileWatchSendConnectMessageToServer();
			}
		}
	}

	return fileWatchClient.hPipe ? 1 : 0;
}

void startFileWatcher(void){
	if (fileExists("c:\\Night\\tools\\bin\\filewatcher.exe")) {
		ulShellExecute(NULL, "open", "c:\\Night\\tools\\bin\\filewatcher.exe", "-autostart", "", SW_HIDE);
	}
}

#else

// XBOX stubs

static S32 sendMessageToServer(U32 cmdExpected, void* dataOut){
	return 0;
}

static S32 fileWatchConnect(S32 connect){
	return 0;
}

static FileWatchBuffer* startMessageToServer(U32 cmd){
	return 0;
}

void fileWatchSetDisabled(S32 disabled){
}

#endif

static void atomicInitializeCriticalSection(volatile S32* init, CRITICAL_SECTION* cs)
{
#if !_PS3
	char name[100];
	HANDLE hMutex;

#if _XBOX
	strcpy(name, "InitCritSec:Xbox");
#else
	STR_COMBINE_SD(name, "InitCritSec:", _getpid());
#endif

	hMutex = CreateMutex_UTF8(NULL, FALSE, name);

	assertmsg(hMutex, "Can't create mutex.");

	if(!acquireMutex(hMutex, INFINITE)){
		// This should never happen.

		assert(0);
	}
#endif

	if(!*init){
		InitializeCriticalSection(cs);
		*init = 1;
	}

#if !_PS3
	ReleaseMutex(hMutex);
	CloseHandle(hMutex);
#endif
}

static void EnterCS(void) {

    static S32 init;
    if(!init)
        atomicInitializeCriticalSection(&init, &fileWatchClient.cs);

	EnterCriticalSection(&fileWatchClient.cs);
}

static void LeaveCS(void) {
	LeaveCriticalSection(&fileWatchClient.cs);
}

#define adjustFileName(src, dest) adjustFileName_s(src, SAFESTR(dest))
static void adjustFileName_s(const char* fileName, char* fileNameOut, int fileNameOut_size){
	const char* cur;
	char* curOut = NULL;

	assert(fileNameOut_size > 5); // Room for initial, unsafe copies

#if _PS3
    curOut = fileNameOut;
#elif _XBOX 
	{
		const char *start = fileName;
		while (*fileName)
		{
			if (*fileName == ':')
			{
				strncpyt(fileNameOut,start,fileName - start+2);
				curOut = fileNameOut + strlen(fileNameOut);
				*curOut = '/';
				curOut++;
			}
			else if (isSlash(*fileName))
			{
				if (!curOut)
					break;
			}
			else if (curOut)
			{
				break;
			}
			fileName++;
		}
		if (!curOut)
		{
			curOut = fileNameOut + sprintf_s(SAFESTR2(fileNameOut), "game:/");
		}
	}
#else
	// Check for an absolute filename (c:/something, OR \\server\share\filename).
	if(	isalpha(fileName[0]) &&
		fileName[1] == ':'
		||
		isSlash(fileName[0]) &&
		isSlash(fileName[1]))
	{
		memcpy_s(fileNameOut, fileNameOut_size, fileName, 2);

		if(isalpha(fileName[0])){
			fileNameOut[2] = '/';
			curOut = fileNameOut + 3;
			if(fileName[2]){
				fileName += 3;
			}else{
				fileName += 2;
			}
		}else{
			curOut = fileNameOut + 2;
			fileName += 2;
		}
	}else{
		char *pTempDir = NULL;
		estrStackCreate(&pTempDir);
		GetCurrentDirectory_UTF8(&pTempDir);
		strcpy_s(fileNameOut, fileNameOut_size, pTempDir);
		estrDestroy(&pTempDir);

		forwardSlashes(fileNameOut);

		curOut = fileNameOut + strlen(fileNameOut);

	}
#endif

	// Remove trailing slashes.

	while(	curOut - fileNameOut > 3 &&
			curOut[-1] == '/')
	{
		*--curOut = 0;
	}

	for(cur = fileName; *cur;){
		const char* delim;
		S32 len;

		for(; isSlash(*cur); cur++);

		for(delim = cur; !isNullOrSlash(*delim); delim++);

		len = delim - cur;

		if(	len <= 2 &&
			cur[0] == '.' &&
			(	isNullOrSlash(cur[1]) ||
				cur[1] == '.'))
		{
			if(cur[1] == '.'){
				// cur == ".."
				// fileNameOut: "c:/game/data/thing/whatever"
				//                                    curOut^  (null terminator)

				while(curOut - fileNameOut >= 3){
					char c = *--curOut;

					*curOut = 0;

					if(isSlash(c)){
						break;
					}
				}
			}
			else if(!cur[0]){
				// cur == ".", so don't add anything.
			}
		}else{
			// Not a "." or ".."

			if(curOut - fileNameOut > 3){
				*curOut++ = '/';
			}
			memcpy_s(curOut, fileNameOut_size - (curOut - fileNameOut), cur, len);
			curOut += len;
		}

		cur = delim + (*delim ? 1 : 0);
	}

	*curOut = 0;

	removeLeadingAndFollowingSpaces(fileNameOut);
}

typedef struct FileWatchFindFileStruct {
	S32				allocIndex;

#if _PS3
	char base_path[MAX_PATH];
	intptr_t handle;
#else
	union {
		HANDLE		windowsHandle;
		U32			fileWatchHandle;
		void		*fileServerHandle;
	};

	U32				isFileWatchHandle : 1;
	U32				isFileServerHandle : 1;
#endif
} FileWatchFindFileStruct;

static struct {
	FileWatchFindFileStruct*		finds;
	S32								count;
	S32								maxCount;
	S32								firstFree;
} finds;

static FileWatchFindFileStruct* getNewFind(bool isFileWatchHandle, bool isFileServerHandle, S32* handleOut){
	FileWatchFindFileStruct* find;

	if(!finds.firstFree){
		find = dynArrayAddStruct(finds.finds, finds.count, finds.maxCount);

		find->allocIndex = finds.count;
	}else{
		S32 index = finds.firstFree - 1;

		assert(index >= 0 && index < finds.count);

		find = finds.finds + index;

		finds.firstFree = find->allocIndex;

		find->allocIndex = index + 1;
	}

#if !_PS3
	find->isFileWatchHandle = isFileWatchHandle ? 1 : 0;
	find->isFileServerHandle = isFileServerHandle ? 1 : 0;
#endif

	*handleOut = find->allocIndex;

	return find;
}

static void freeFind(FileWatchFindFileStruct* find){
	S32 index = find->allocIndex;

	assert(index > 0 && index <= finds.count);
	assert(finds.finds + index - 1 == find);
	assert(finds.firstFree != index);

	find->allocIndex = finds.firstFree;

	finds.firstFree = index;
}

static FileWatchFindFileStruct* getFindByIndex(S32 index){
	FileWatchFindFileStruct* find;

	if(index <= 0 || index > finds.count){
		return NULL;
	}

	find = finds.finds + index - 1;

	if(find->allocIndex != index){
		return NULL;
	}

	return find;
}





// Function: fwFindFirstFile.
//
// ------------------------------------------------------------------------
// return | handleOut | Meaning
// -------+-----------+----------------------------------------------------
// 0      | any       | Folder isn't watched and no files match
// 1      | !0        | Here's the first matching file.

S32 fwFindFirstFile(U32* handleOut, const char* fileSpec, WIN32_FIND_DATAA* wfd){
#if _PS3
	struct _finddata32_t c_file;
	intptr_t hFile;
	FileWatchFindFileStruct* fs;
	char full_path[MAX_PATH];
	struct _stat32 statInfo;

	hFile = _findfirst32(fileSpec, &c_file);
	if (hFile == -1L)
		return 0;

	EnterCS();
	fs = getNewFind(false, false, handleOut);
	fs->handle = hFile;
	strcpy(fs->base_path, fileSpec);
	getDirectoryName(fs->base_path);
	STR_COMBINE_SSS(full_path, fs->base_path, "/", c_file.name);
	fwStat(full_path, &statInfo);
	wfd->nFileSizeLow = statInfo.st_size;
	strcpy(wfd->cFileName, c_file.name);
	//wfd->dwFileAttributes = statInfo.st_mode; // No idea if these translate...
	wfd->ftLastWriteTime.time = statInfo.st_mtime;
	wfd->ftCreationTime.time = statInfo.st_ctime;
	wfd->ftLastAccessTime.time = statInfo.st_atime;
	LeaveCS();
	return 1;
#else
	char				fileNameAdjusted[CRYPTIC_MAX_PATH];
	FindFileData		findData = {0};
	S32					retVal = 1;

	assert(handleOut);

	adjustFileName(fileSpec, fileNameAdjusted);

	EnterCS();

	if (fileIsFileServerPath(fileSpec))
	{
		void *server_handle=NULL;
		retVal = fileServerFindFirstFile(&server_handle, fileSpec, wfd);
		if (retVal)
		{
			FileWatchFindFileStruct* find = getNewFind(false, true, handleOut);
			find->fileServerHandle = server_handle;
		} else {
			*handleOut = -1;
		}
		LeaveCS();
		return retVal;
	}

#if _XBOX
	strcpy_s(fileNameAdjusted,CRYPTIC_MAX_PATH,fileSpec);
	backSlashes(fileNameAdjusted);
	fileSpec = fileNameAdjusted;
	retVal = 0;
#else

	if(!fileWatchConnect(1)){
		retVal = 0;
	}else{
		FileWatchBuffer* writeBuffer = startMessageToServer(FWM_C2S_FIND_FIRST_FILE);

		fwWriteBufferString(writeBuffer, fileNameAdjusted);

		findData.wfd = wfd;

		if(	!sendMessageToServer(FWM_S2C_FIND_FIRST_FILE_REPLY, &findData) ||
			!findData.handle)
		{
			retVal = 0;
		}
	}
#endif

	if(!retVal){
		// Folder isn't watched, or the server isn't running.

		S32 tryCount = 0;

		while(1){
			HANDLE handle;
			WIN32_FIND_DATA wideFindData = {0};
			S16 *pWideFileSpec = UTF8_To_UTF16_malloc(fileSpec);

			handle = FindFirstFile(pWideFileSpec, &wideFindData);

			free(pWideFileSpec);
			WideFindDataToUTF8(&wideFindData, wfd);


			if(handle != INVALID_HANDLE_VALUE){
				FileWatchFindFileStruct* find = getNewFind(false, false, handleOut);

				find->windowsHandle = handle;

				retVal = 1;

				break;
			}
			else if(GetLastError() == ERROR_SHARING_VIOLATION){
				if(++tryCount == 5){
					printfColor(COLOR_RED, "Sharing violation doing FindFirstFile: %s\n", fileSpec);
					break;
				}
				Sleep(300);
			}
			else{
				U32 error = GetLastError();
				UNUSED(error);
				//printf("unhandled error: %d\n", error);
				break;
			}
		}
	}
	else if(findData.handle){
		if(findData.noMatches){
			retVal = 0;
		}else{
			FileWatchFindFileStruct* find = getNewFind(true, false, handleOut);

			find->fileWatchHandle = findData.handle;
		}
	}

	LeaveCS();

	return retVal;
#endif
}

S32 fwFindNextFile(U32 handle, WIN32_FIND_DATAA* wfd){
	FileWatchFindFileStruct*	find;
#if _PS3
	struct _finddata32_t c_file;
	char full_path[MAX_PATH];
	struct _stat32 statInfo;

	EnterCS();
	find = getFindByIndex(handle);

	if (_findnext32(find->handle, &c_file) != 0)
	{
		LeaveCS();
		return 0;
	}

	STR_COMBINE_SSS(full_path, find->base_path, "/", c_file.name);
	fwStat(full_path, &statInfo);
	wfd->nFileSizeLow = statInfo.st_size;
	strcpy(wfd->cFileName, c_file.name);
	//wfd->dwFileAttributes = statInfo.st_mode; // No idea if these translate...
	wfd->ftLastWriteTime.time = statInfo.st_mtime;
	wfd->ftCreationTime.time = statInfo.st_ctime;
	wfd->ftLastAccessTime.time = statInfo.st_atime;
	LeaveCS();
    return 1;
#else
	FileWatchBuffer*			writeBuffer;
	FindFileData				findData;
	S32							retVal = 1;

	PERFINFO_AUTO_START_FUNC();
	EnterCS();

	find = getFindByIndex(handle);

	if(!find){
		// Invalid handle, user error.

		retVal = 0;
	}
	else if(find->isFileWatchHandle){
		if(	!find->fileWatchHandle ||
			!fileWatchConnect(0))
		{
			retVal = 0;
		}else{
			writeBuffer = startMessageToServer(FWM_C2S_FIND_NEXT_FILE);

			fwWriteBufferU32(writeBuffer, find->fileWatchHandle);

			findData.handle = find->fileWatchHandle;
			findData.wfd = wfd;

			if(!sendMessageToServer(FWM_S2C_FIND_NEXT_FILE_REPLY, &findData)){
				// There was an error.

				retVal = 0;
			}
			else if(!findData.handle){
				// Reached end of file list.

				retVal = 0;
			}
		}
	}
	else if (find->isFileServerHandle)
	{
		retVal = fileServerFindNextFile(find->fileServerHandle, wfd);
	}
	else
	{
		WIN32_FIND_DATA wideData = {0};

		retVal = FindNextFile(find->windowsHandle, &wideData);
		WideFindDataToUTF8(&wideData, wfd);
	}

	LeaveCS();

	PERFINFO_AUTO_STOP();

	return retVal;
#endif
}

S32 fwFindClose(U32 handle){
	FileWatchFindFileStruct*	find;
#if _PS3
	EnterCS();
	find = getFindByIndex(handle);
	if(find){
		_findclose(find->handle);
		freeFind(find);
	}
	LeaveCS();
    return 1;
#else
	FileWatchBuffer*			writeBuffer;
	FindFileData				findData;
	S32							retVal = 1;

	EnterCS();

	find = getFindByIndex(handle);

	if(!find){
		// Invalid handle, user error.

		retVal = 0;
	}else{
		if(find->isFileWatchHandle){
			if(	!find->fileWatchHandle ||
				!fileWatchConnect(0))
			{
				retVal = 0;
			}
			else{
				writeBuffer = startMessageToServer(FWM_C2S_FIND_CLOSE);

				fwWriteBufferU32(writeBuffer, find->fileWatchHandle);

				findData.handle = find->fileWatchHandle;
				findData.wfd = NULL;

				if(!sendMessageToServer(FWM_S2C_FIND_CLOSE_REPLY, &findData)){
					// There was an error.

					retVal = 0;
				}
				else if(!findData.handle){
					// Handle didn't exist, user error.

					retVal = 0;
				}
			}
		} else if (find->isFileServerHandle)
		{
			retVal = fileServerFindClose(find->fileServerHandle);
		}else{
			retVal = FindClose(find->windowsHandle);
		}

		freeFind(find);
	}

	LeaveCS();

	return retVal;
#endif
}

S32 fwStat(const char* fileName, FWStatType* statInfo){
	FWStatType tempStatInfo;
	char fileNameAdjusted[CRYPTIC_MAX_PATH];

	assertmsgf(strlen(fileName) < MAX_PATH, "Path \"%s\" is too long", fileName);

	if(!statInfo){
		statInfo = &tempStatInfo;
	}

#if _PS3
    Strcpy(fileNameAdjusted,fileName);
	forwardSlashes(fileNameAdjusted);
	fileName = fileNameAdjusted;
#elif _XBOX
	Strcpy(fileNameAdjusted,fileName);
	backSlashes(fileNameAdjusted);
	fileName = fileNameAdjusted;
#else
	EnterCS();

	adjustFileName(fileName, fileNameAdjusted);

	if(fileWatchConnect(1)){
		FileWatchBuffer*	writeBuffer = startMessageToServer(FWM_C2S_STAT);
		StatData			statData = {0};

		fwWriteBufferString(writeBuffer, fileNameAdjusted);

		statData.statInfo = statInfo;

		if(sendMessageToServer(FWM_S2C_STAT_REPLY, &statData)){
			if(statData.folderTracked){
				if(statData.statInfo){

					LeaveCS();

					return 0;
				}else{
					LeaveCS();

					return -1;
				}
			}
		}
	}

	LeaveCS();
#endif

	fileDiskAccessCheck();

	// Folder isn't tracked by FileWatcher, or FileWatcher isn't running.
	{
		struct _stat32i64 tempStatInfo2;
		int ret = cryptic_stat32i64_utc(fileName, &tempStatInfo2); // use our own stat() because MS's is too broken with respect to time
		if (ret == 0)
		{
			statInfo->st_dev = tempStatInfo2.st_dev;
			statInfo->st_ino = tempStatInfo2.st_ino;
			statInfo->st_mode = tempStatInfo2.st_mode;
			statInfo->st_nlink = tempStatInfo2.st_nlink;
			statInfo->st_uid = tempStatInfo2.st_uid;
			statInfo->st_gid = tempStatInfo2.st_gid;
			statInfo->st_rdev = tempStatInfo2.st_rdev;
			statInfo->st_size = tempStatInfo2.st_size; // truncation will happen on large files, match VS2005 CRT behavior
			statInfo->st_atime = tempStatInfo2.st_atime;
			statInfo->st_mtime = tempStatInfo2.st_mtime;
			statInfo->st_ctime = tempStatInfo2.st_ctime;
		}
		return ret;
	}
}

S32 fwChmod(const char* fileName, S32 pmode)
{
	FWStatType s;
	
	if(fwStat(fileName, &s)){
		return -1;
	}else{
		if((s.st_mode & (_S_IREAD|_S_IWRITE)) == pmode){
			return 0;
		}
		
		return chmod(fileName, pmode);
	}
}

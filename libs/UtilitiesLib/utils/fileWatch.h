#pragma once
GCC_SYSTEM

#ifndef INCLUDE_FILEWATCHER
#define INCLUDE_FILEWATCHER

#include "stdtypes.h"
#include "wininclude.h"

typedef enum FileWatchMessageClientToServer {
	FWM_C2S_CONNECT,
	FWM_C2S_FIND_FIRST_FILE,
	FWM_C2S_FIND_NEXT_FILE,
	FWM_C2S_FIND_CLOSE,
	FWM_C2S_STAT,
} FileWatchMessageClientToServer;

typedef enum FileWatchMessageServerToClient {
	FWM_S2C_READY_FOR_REQUESTS,
	FWM_S2C_FIND_FIRST_FILE_REPLY,
	FWM_S2C_FIND_NEXT_FILE_REPLY,
	FWM_S2C_FIND_CLOSE_REPLY,
	FWM_S2C_STAT_REPLY,
} FileWatchMessageServerToClient;

typedef struct FileWatchBuffer {
	U8*		buffer;
	U32		maxByteCount;
	U32		curBytePos;
	U32		curByteCount;
} FileWatchBuffer;

void		fwWriteBufferData(FileWatchBuffer* writeBuffer, const void* data, U32 byteCount);
void		fwWriteBufferU32(FileWatchBuffer* writeBuffer, U32 value);
void		fwWriteBufferString(FileWatchBuffer* writeBuffer, const char* str);

void		fwReadBufferData(FileWatchBuffer* readBuffer, void* buffer, U32 byteCount);
U64			fwReadBufferU64(FileWatchBuffer* readBuffer);
U32			fwReadBufferU32(FileWatchBuffer* readBuffer);
U8			fwReadBufferU8(FileWatchBuffer* readBuffer);
const char* fwReadBufferString(FileWatchBuffer* readBuffer);


void		fileWatchSetDisabled(S32 disabled);

const char* fileWatchGetCheckRunningMutexName(void);
const char* fileWatchGetRunningMutexName(void);

const char* fileWatchGetPipeName(void);

// TODO: It would be good to have a set of these that substitute for the CRT findfirst functions, that could replace findfirst*_SAFE() in file.h.
S32			fwFindFirstFile(U32* handleOut, const char* fileSpec, WIN32_FIND_DATAA* wfd);
S32			fwFindNextFile(U32 handle, WIN32_FIND_DATAA* wfd);
S32			fwFindClose(U32 handle);

typedef struct FWStatType // same as _stat32, 32-bit times, 32-bit file sizes (truncated)
{
	unsigned int	st_dev;
	unsigned short	st_ino;
	unsigned short	st_mode;
	short			st_nlink;
	short			st_uid;
	short			st_gid;
	unsigned int	st_rdev;
	unsigned int	st_size; // actual _stat32 is signed long here
	__time32_t		st_atime;
	__time32_t		st_mtime;
	__time32_t		st_ctime;
} FWStatType;

S32			fwStat(const char* fileName, FWStatType* statInfo);

S32			fwChmod(const char* fileName,
					S32 pmode);

void		startFileWatcher(void);
S32			fileWatcherIsRunning(void);

#endif

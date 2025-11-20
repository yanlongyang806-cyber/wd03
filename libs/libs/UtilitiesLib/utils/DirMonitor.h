#pragma once
GCC_SYSTEM

#include <stdlib.h>
#include "wininclude.h"

typedef struct DirMonitor DirMonitor;

// Passing a NULL dirMonitor to these functions uses the default dirMonitor,
//  which is essentially owned by the FolderCache (it needs to handle all
//  callbacks)
// For special purposes, you can create a custom DirMonitor

typedef struct DirChangeInfo {
	char	dirname[CRYPTIC_MAX_PATH];
	int		changeTime;
	int		lastChangeTime;
	char*	filename; // this points to a static var in dirMonCheckDirs, and is destroyed after each subsequent call
	int		isDelete;
	int		isCreate;
	void*	userData;
} DirChangeInfo;

typedef struct DirMonCompletionPortCallbackParams {
	DirMonitor	   *dirMonitor;
	S32				result;
	S32				aborted;
	HANDLE			handle;
	void*			userData;
	U32				bytesTransferred;
	OVERLAPPED*		overlapped;
} DirMonCompletionPortCallbackParams;

typedef struct DirMonCompletionPortObject DirMonCompletionPortObject;

typedef void (*DirMonCompletionPortCallback)(DirMonCompletionPortCallbackParams* params);

U32 dirMonCompletionPortAddObjectDbg(SA_PARAM_OP_VALID DirMonitor *dirMonitor, HANDLE handle, void* userData, DirMonCompletionPortCallback callback, const char* fileName, S32 lineNumber);
#define dirMonCompletionPortAddObject(dirMonitor, handle, userData, callback) dirMonCompletionPortAddObjectDbg(dirMonitor, handle, userData, callback, __FILE__, __LINE__)
void dirMonCompletionPortRemoveObject(SA_PARAM_OP_VALID DirMonitor *dirMonitor, U32 handle);

int dirMonAddDirectory(SA_PARAM_OP_VALID DirMonitor *dirMonitor, const char* dirname, int monitorSubDirs, void* userData); // Note: userData will be freed
void dirMonRemoveDirectory(SA_PARAM_OP_VALID DirMonitor *dirMonitor, const char* dirname);

DirChangeInfo* dirMonCheckDirs(SA_PARAM_OP_VALID DirMonitor *dirMonitor, int timeout, DirChangeInfo** bufferOverrun);

enum {
	DIR_MON_NORMAL = 0,
	DIR_MON_NO_TIMESTAMPS = 1 << 0,
};
void dirMonSetFlags(SA_PARAM_OP_VALID DirMonitor *dirMonitor, int flags);
void dirMonSetBufferSize(SA_PARAM_OP_VALID DirMonitor *dirMonitor, int size); // Defaults to 512KB

DirMonitor *dirMonCreate(void); // Creates a new DirMonitor structure
void dirMonDestroy(DirMonitor *dirMonitor);

void dirMonPostCompletion(DirMonitor* dirMonitor, int completionKey);


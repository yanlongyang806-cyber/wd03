#ifndef PROCESS_UTIL_H
#define PROCESS_UTIL_H

#include "wininclude.h"

C_DECLARATIONS_BEGIN

int kill(DWORD pid);
void killall(const char * module);
//bDoFDNameFixup means when asked to kill gameclient.exe, also kill gameclientFD.exe
//
//pRestrictToThisDir means only kill exes launched from the given dir (ie, strStartsWith on the full exe path)
//
//bDoFrankenBuildAwareComparisons means that "gameserver_foo.exe" and "gameserver.exe" will match (Along with all X64 and FD versions)
void KillAllEx(const char * module, bool bSkipSelf, U32 *pPIDsToIgnore, bool bDoFDNameFixup, bool bDoFrankenBuildAwareComparisons, char *pRestrictToThisDir);


int ProcessCount(char * procName, bool bDoFDNameFixup);

BOOL ProcessNameMatch( DWORD processID , char * targetName, bool bDoFDNameFixup);

bool processExists(char * procName, int iPid);

// Iteration over all the running processes.

typedef struct ForEachProcessCallbackData {
	void*		userPointer;
	U32			pid;
	char*		exeFileName;
	U32			pidParent;
} ForEachProcessCallbackData;

typedef S32 (*ForEachProcessCallback)(const ForEachProcessCallbackData* data);

void forEachProcess(ForEachProcessCallback callback,
					void* userPointer);

// Iteration over all the running threads (filtered by pid if pid != 0).

typedef struct ForEachThreadCallbackData {
	void*		userPointer;
	U32			pid;
	U32			tid;
} ForEachThreadCallbackData;

typedef S32 (*ForEachThreadCallback)(const ForEachThreadCallbackData* data);

void forEachThread(	ForEachThreadCallback callback,
					U32 pid,
					void* userPointer);

// Iteration over all the loaded modules (filtered by pid if pid != 0).

typedef struct ForEachModuleCallbackData {
	void*		userPointer;
	U32			pid;
	const void*	baseAddress;
	U32			baseSize;
	char*	modulePath;
	U32			tid;
} ForEachModuleCallbackData;

typedef S32 (*ForEachModuleCallback)(const ForEachModuleCallbackData* data);

void forEachModule(	ForEachModuleCallback callback,
					U32 pid,
					void* userPointer);

//calculate the value reported as "Memory (Private Working Set)" in procexp, which is something like
//non-shared physical RAM
U64 GetProcessPrivateWorkingSetRAMUsage(HANDLE hProcHandle);

C_DECLARATIONS_END

#endif // PROCESS_UTIL_H
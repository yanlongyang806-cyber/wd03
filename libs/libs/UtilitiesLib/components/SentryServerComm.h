#pragma once

#include "earray.h"
#include "..\..\Utilities\SentryServer\SentryPub.h"

typedef struct Packet Packet;
typedef struct SentryClientList SentryClientList;
typedef struct NetComm NetComm;
typedef struct SentryProcess_FromSimpleQuery_List SentryProcess_FromSimpleQuery_List;
typedef struct SentryMachines_FromSimpleQuery SentryMachines_FromSimpleQuery;

Packet *SentryServerComm_CreatePacket(int iCmd, FORMAT_STR const char *pCommentFmt, ...);
void SentryServerComm_SendPacket(Packet **ppPacket);
void SentryServerComm_Tick(void);

void SentryServerComm_SetNetComm(NetComm *pComm);

//when you do queries to get back lists of machines, they end up here
extern SentryClientList gSentryClientList;
extern char gSentryServerName[100];

//processName can actually be a comma-separated list of names
void SentryServerComm_KillProcesses(char **ppMachineNames, char *pProcessName, char *pDirToRestrictTo);
static void SentryServerComm_KillProcess_1Machine(const char *pMachineName, char *pProcessName, char *pDirToRestrictTo)
{
	if (pMachineName)
	{
		char **ppMachineNames = NULL; eaPush(&ppMachineNames, (char*)pMachineName); SentryServerComm_KillProcesses(ppMachineNames, pProcessName, pDirToRestrictTo); eaDestroy(&ppMachineNames); 
	}
}

//returns false if it couldn't load the file
bool SentryServerComm_SendFile(char **ppMachineNames, char *pLocalFileName, char *pRemoteFileName);

static void SentryServerComm_SendFile_1Machine(const char *pMachineName, char *pLocalFileName, char *pRemoteFileName)
{
	if (pMachineName)
	{
		char **ppMachineNames = NULL; eaPush(&ppMachineNames, (char*)pMachineName); SentryServerComm_SendFile(ppMachineNames, pLocalFileName, pRemoteFileName); eaDestroy(&ppMachineNames); 
	}
}
/*This sending mode adds several things:
(1) just does a file system copy when sending to the local machine
(2) queries whether the file is there on the remote machine via getCRC, doesn't send it again if it is
(3) Gathers up sends of the same file and does them at the same time

Note that this is all threadsafe, but the results CB will always be called in the main thread
*/

typedef struct SentryServerCommDeferredSendHandle SentryServerCommDeferredSendHandle;
SentryServerCommDeferredSendHandle *SentryServerComm_BeginDeferredFileSending(char *pName);

//returns false if the file doesn't exist (or can't be read) locally
bool SentryServerComm_SendFileDeferred(SentryServerCommDeferredSendHandle *pHandle, char *pMachineName, char *pLocalFileName, char *pRemoteFileName);


typedef void (*DeferredSendsUpdateCB)(char *pUpdateString, void *pUserData);
typedef void (*DeferredSendsResultCB)(bool bAllSucceeded, char *pErrorString, void *pUserData);

//destroys the handle
void SentryServerComm_DeferredFileSending_DoIt(SentryServerCommDeferredSendHandle *pHandle, DeferredSendsUpdateCB pUpdateCB, DeferredSendsResultCB pResultsCB, void *pUserData);


//goes through all the files in a directory, calls SendFileWrapped on each of them
void SentryServerComm_SendDirectoryRecurse(char *pMachineName, char *pLocalDirName, char *pRemoteDirName);

//note that putting WORKINGDIR(%s) at the beginning of the command string does what you expect it to
void SentryServerComm_RunCommand(const char **ppMachineNames, char *pCommand);

static __forceinline void SentryServerComm_RunCommand_1Machine(const char *pMachineName, char *pCommand)
{
	if (pMachineName)
	{
		const char **ppMachineNames = NULL; eaPush(&ppMachineNames, pMachineName); SentryServerComm_RunCommand(ppMachineNames, pCommand); eaDestroy(&ppMachineNames); 
	}
}


//Note that this whole business is a bit flakey... trying to do queries at once is a terrible idea, there's no userdata, etc.
//
//If you do QueryMachineForRunningExes, you'll get back a single machine and all its procs. If you do SentryServerComm_QueryForMachines,
//you'll get back all the machines, but with no proc info
AUTO_STRUCT;
typedef struct SentryServerCommQueryProcInfo
{
	char *pExeName; //short, no .exe
	char *pPath;
	U32 iPID;
} SentryServerCommQueryProcInfo;

AUTO_STRUCT;
typedef struct SentryServerCommMachineInfo
{
	char *pMachineName;
	SentryServerCommQueryProcInfo **ppProcs;
} SentryServerCommMachineInfo;

AUTO_STRUCT;
typedef struct SentryServerCommQueryReturn
{
	SentryServerCommMachineInfo **ppMachines;
} SentryServerCommQueryReturn;

typedef void (*SentryServerQueryCB)(SentryServerCommQueryReturn *pReturn);

void SentryServerComm_SetQueryCB(SentryServerQueryCB pCB);
void SentryServerComm_QueryMachineForRunningExes(char *pMachineName);


void SentryServerComm_QueryForMachines(void);


typedef void (*SentryServerComm_FileCRC_CB)(const char *pMachineName, char *pFileName, void *pUserData, int iCRC, bool bTimedOut);
void SentryServerComm_GetFileCRC(const char *pMachineName, char *pFileName, SentryServerComm_FileCRC_CB pCB, void *pUserData);

//directory contents comes back as a semicolon-separated list of filenames
typedef void (*SentryServerComm_DirectoryContents_CB)(const char *pMachineName, char *pDirName, void *pUserData, char *pFiles, bool bFailed);
void SentryServerComm_GetDirectoryContents(const char *pMachineName, char *pDirName, SentryServerComm_DirectoryContents_CB pCB, void *pUserData);



//new and improved, no longer sketchy
typedef void (*SentryServerExesQueryCB)(SentryProcess_FromSimpleQuery_List *pList, void *pUserData);
void SentryServerComm_QueryMachineForRunningExes_Simple(char *pMachineName, SentryServerExesQueryCB pCB, void *pUserData);

typedef void (*SentryServerMachinesQueryCB)(SentryMachines_FromSimpleQuery *pList, void *pUserData);
void SentryServerComm_QueryForMachines_Simple(SentryServerMachinesQueryCB pCB, void *pUserData);

typedef void (*SentryServerGetFileCB)(char *pMachineName, char *pFileName, void *pFileData, int iSize, void *pUserData);
void SentryServerComm_GetFileContents(char *pMachineName, char *pFileName, SentryServerGetFileCB pCB, void *pUserData);


//specially wrapped version of ExecuteCommand and sendFile that makes a little batch file that does some little magical batch commands to
//determine if the machine is 64-bit and then tries to launch commandX64.exe instead of command.exe if it does
void SentryServerComm_ExecuteCommandWith64BitFixup(const char **ppMachineName, const char *pExeName, const char *pCommandLine, const char *pWorkingDir, const char *pUniqueComment);
static __forceinline void SentryServerComm_ExecuteCommandWith64BitFixup_1Machine(const char *pMachineName, const char *pExeName, const char *pCommandLine, const char *pWorkingDir, const char *pUniqueComment)
{
	if (pMachineName)
	{
		const char **ppMachineNames = NULL; eaPush(&ppMachineNames, pMachineName); SentryServerComm_ExecuteCommandWith64BitFixup(ppMachineNames, pExeName, pCommandLine, pWorkingDir, pUniqueComment); eaDestroy(&ppMachineNames); 
	}
}
#pragma once
//interface for doing threaded PCL queries and getting a callback when the query completes

#include "pcl_typedefs.h"

typedef struct WorkerThread WorkerThread;
extern WorkerThread *gpPCLWorkerThread;


void ThreadedPCLTick_Internal(void);
static __forceinline void ThreadedPCLTick(void)
{
	if (gpPCLWorkerThread)
	{
		ThreadedPCLTick_Internal();
	}
}

AUTO_STRUCT;
typedef struct PatchVersionInfo
{
	char *pName;
	int iBranch;
	char *pSandBox;
	U32 iRev;
	char *pComment;
	U32 iExpire;
} PatchVersionInfo;

typedef void (*GetPatchVersionsCB)(PatchVersionInfo **ppVersions, PCL_ErrorCode error, char *pErrorDetails, void *pUserData);

void ThreadedPCL_GetPatchVersions(char *pServer, char *pProduct, GetPatchVersionsCB pCB, void *pUserData);

//used the above technology to server up all the patch versions for a product through server monitor, as 
//productname_PatchVersions
void MakePatchVersionsServerMonitorable(char *pServer, char *pProduct);

//gets the patch comment for a patch version, ONLY if it's already been loaded up via MakePatchVersionsServerMonitorable
char *GetCommentFromServerMonitorablePatchVersion(char *pProduct, char *pVersionName);

//pFileData will be NULL if things fail
typedef void (*GetFileIntoRAMCB)(void *pFileData, int iFileSize, PCL_ErrorCode error, char *pErrorDetails, void *pUserData);
void ThreadedPCL_GetFileIntoRAM(char *pServer, char *pProduct, char *pView, char *pFileName, GetFileIntoRAMCB pCB, void *pUserData);


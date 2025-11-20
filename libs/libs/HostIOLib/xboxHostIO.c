#include "xboxHostIO.h"
#include "estring.h"
#include "wininclude.h"
//#include <xbdm.h>
#include "threadManager.h"
#include "earray.h"
#include "timing.h"
#include "UTF8.h"

// Introduces a dependency on xbdm.dll and msvcp71.dll
//#pragma comment(lib, "xbdm.lib")

// How long, in milliseconds, we are willing to block on any xbox*() function call
#define XBOX_HOSTIO_MAX_BLOCK_MS 100

#if ENABLE_XBOX_HOSTIO
PDMN_SESSION gNotificationSession = NULL;
#endif

static char *spPrintfBuffer = NULL;
#define MAX_PRINTF_BUFFER_SIZE (1024 * 1024)
static CRITICAL_SECTION *spPrintfCriticalSection = NULL;
static int siPrintfCount = 0;

void xboxHandleError(U32 iRetVal)
{
	char buf[2048];
#if ENABLE_XBOX_HOSTIO
	DmTranslateError(iRetVal, buf, 2047);
#endif

	printf("Xbox DM Error: %s\n", buf);

}



__forceinline void xboxLimitTimeout()
{
#if ENABLE_XBOX_HOSTIO
	DmSetConnectionTimeout(XBOX_HOSTIO_MAX_BLOCK_MS, XBOX_HOSTIO_MAX_BLOCK_MS);
#endif
}

bool xboxReboot(bool bColdReboot)
{
#if ENABLE_XBOX_HOSTIO
	DWORD flags = bColdReboot ? DMBOOT_COLD : 0;
	xboxLimitTimeout();

	return (DmReboot(flags) == XBDM_NOERR);
#else
	return false;
#endif
}

// The xbdm API doesn't offer such a thing, but this seems accurate and fairly harmless. 
bool xboxIsReady(void)
{
#if ENABLE_XBOX_HOSTIO
	DM_XBE xbeInfo = {0};
	xboxLimitTimeout();

	return (DmGetXbeInfoEx("", &xbeInfo, 0) == XBDM_NOERR);
#else
	return false;
#endif

}

bool xboxGetRunningExePath(char **estr)
{
#if ENABLE_XBOX_HOSTIO
	DM_XBE xbeInfo = {0};
	xboxLimitTimeout();

	// Passing in an empty string to ensure failure if nothing is running,
	// as the docs suggest that it will return info on a non-running XBE
	// if you give a name of an XBE that it recognizes.
	if(DmGetXbeInfoEx("", &xbeInfo, 0) == XBDM_NOERR)
	{
		estrCopy2(estr, xbeInfo.LaunchPath);
		return true;
	}
#endif

	return false;
}

bool xboxGetXboxName(char **estr)
{
#if ENABLE_XBOX_HOSTIO
	DWORD size = 2048;
	char temp[2048];
	xboxLimitTimeout();

	if(DmGetNameOfXbox(temp, &size, TRUE) == XBDM_NOERR)
	{
		estrCopy2(estr, temp);
		return true;
	}
#endif

	return false;
}


DWORD __stdcall xboxPrintfNotifyFunc(
    ULONG dwNotification,
    DWORD dwParam
)
{
#if ENABLE_XBOX_HOSTIO
	if (dwNotification == DM_DEBUGSTR)
	{
		DMN_DEBUGSTR *pDebugStr = (DMN_DEBUGSTR*)(intptr_t)dwParam;
		int iStartingSize;

		EnterCriticalSection(spPrintfCriticalSection);
		iStartingSize = estrLength(&spPrintfBuffer);
		estrSetSize(&spPrintfBuffer, iStartingSize + pDebugStr->Length);
		memcpy(spPrintfBuffer + iStartingSize, pDebugStr->String, pDebugStr->Length);

		while(estrLength(&spPrintfBuffer) > MAX_PRINTF_BUFFER_SIZE)
		{
			estrRemoveUpToFirstOccurrence(&spPrintfBuffer, '\n');
		}

		siPrintfCount++;

		LeaveCriticalSection(spPrintfCriticalSection);
	}	
#endif

	return TRUE;
}



bool xboxBeginCapturingPrintfs(void)
{
#if ENABLE_XBOX_HOSTIO
	int iRetVal;

	xboxLimitTimeout();

	if (!spPrintfCriticalSection)
	{
		spPrintfCriticalSection = calloc(sizeof(CRITICAL_SECTION), 1);
		InitializeCriticalSection(spPrintfCriticalSection);
	}

	if (gNotificationSession)
	{
		DmCloseNotificationSession(gNotificationSession);
		gNotificationSession = NULL;
	}

	if ((iRetVal = DmOpenNotificationSession(0, &gNotificationSession)) != XBDM_NOERR)
	{
		xboxHandleError(iRetVal);
		return false;
	}

	if ((iRetVal = DmNotify(gNotificationSession, DM_DEBUGSTR, xboxPrintfNotifyFunc)) != XBDM_NOERR)
	{
		xboxHandleError(iRetVal);
		return false;
	}
#endif

	return true;
}


void xboxAccessCapturedPrintfs(char **ppBuf, int *pStrLen, int *piCounter)
{
	if (!spPrintfCriticalSection)
	{
		*ppBuf = NULL;
		*pStrLen = 0;
		*piCounter = -1;
		return;
	}

	EnterCriticalSection(spPrintfCriticalSection);
	*ppBuf = spPrintfBuffer;
	*pStrLen = estrLength(&spPrintfBuffer);
	*piCounter = siPrintfCount;
}

void xboxFinishedAccessingPrintfs(void)
{
	LeaveCriticalSection(spPrintfCriticalSection);
}

//call this to quickly get the counter without opening up the critical section or anything
int xboxGetPrintfCounter(void)
{
	return siPrintfCount;
}


void xboxResetPrintfCapturing(void)
{
	if (!spPrintfCriticalSection)
	{
		return;
	}
	EnterCriticalSection(spPrintfCriticalSection);
	estrClear(&spPrintfBuffer);
	siPrintfCount++;
	LeaveCriticalSection(spPrintfCriticalSection);
}





CRITICAL_SECTION *pStatusQueryCriticalSection = NULL;
static bool sbIsReady = false;
static bool sbWasEverReady = false;
static char *spXBoxName = NULL;
static char *spXBoxExe = NULL;


static DWORD WINAPI xboxStatusQueryThread( LPVOID lpParam )
{
	bool bLocalIsReady = false;
	char *pLocalXBoxName = NULL;
	char *pLocalXBoxExe = NULL;

	while (1)
	{
		if (xboxIsReady())
		{
			bLocalIsReady = true;
			sbWasEverReady = true;

			if (!xboxGetRunningExePath(&pLocalXBoxExe))
			{
				estrCopy2(&pLocalXBoxExe, "UNKNOWN");
			}

			if (!xboxGetXboxName(&pLocalXBoxName))
			{
				estrCopy2(&pLocalXBoxName, "UNKNOWN");
			}
		}
		else
		{
			bLocalIsReady = false;
			estrCopy2(&pLocalXBoxExe, "UNKNOWN");
			estrCopy2(&pLocalXBoxName, "UNKNOWN");
		}

		EnterCriticalSection(pStatusQueryCriticalSection);
		sbIsReady = bLocalIsReady;
		estrCopy(&spXBoxName, &pLocalXBoxName);
		estrCopy(&spXBoxExe, &pLocalXBoxExe);
		LeaveCriticalSection(pStatusQueryCriticalSection);

		Sleep(2000);
	}




	return 0;
}


void xboxBeginStatusQueryThread(void)
{
	if (pStatusQueryCriticalSection)
	{
		return;
	}

	estrCopy2(&spXBoxName, "UNKNOWN");
	estrCopy2(&spXBoxExe, "UNKNOWN");

	pStatusQueryCriticalSection = calloc(sizeof(CRITICAL_SECTION), 1);
	InitializeCriticalSection(pStatusQueryCriticalSection);
	
	assert(tmCreateThread(xboxStatusQueryThread, NULL));
}



void xboxQueryStatusFromThread(bool *bIsReady, char **ppXboxName, char **ppRunningExePath)
{
	if (!pStatusQueryCriticalSection)
	{
		xboxBeginStatusQueryThread();
	}
	assert(pStatusQueryCriticalSection);
	EnterCriticalSection(pStatusQueryCriticalSection);
	if (bIsReady)
	{	
		*bIsReady = sbIsReady;
	}
	if (ppXboxName)
	{
		estrCopy(ppXboxName, &spXBoxName);
	}

	if (ppRunningExePath)
	{
		estrCopy(ppRunningExePath, &spXBoxExe);
	}
	LeaveCriticalSection(pStatusQueryCriticalSection);
}


bool xboxQueryStatusXboxWasEverAttached(void)
{
	return sbWasEverReady;
}
/*
AUTO_RUN;
void xboxQueryDir(void)
{
	HRESULT error;
	PDM_WALK_DIR pWalkDir = NULL;
	DM_FILE_ATTRIBUTES fileAttr;

	do
	{
		error = DmWalkDir(&pWalkDir, "xe:\\", &fileAttr);

		// Examine the contents of fileAttr here.
	}
	while (error == XBDM_NOERR);

	if (error != XBDM_ENDOFLIST)
	{
		// Handle error.
	}

	DmCloseDir(pWalkDir);
}
*/


//recursively scan an xbox directory for all files
bool xboxRecurseScanDir(char *pDirName, XboxFileInfo ***pppFiles)
{
#if ENABLE_XBOX_HOSTIO
	HRESULT error;
	PDM_WALK_DIR pWalkDir = NULL;
	DM_FILE_ATTRIBUTES fileAttr;
	int i;

	while ((error = DmWalkDir(&pWalkDir, pDirName, &fileAttr))== XBDM_NOERR)
	{
		if (fileAttr.Attributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			char fullDirName[MAX_PATH];
			sprintf(fullDirName, "%s%s\\", pDirName, fileAttr.Name);
			
			//20 retries
			for (i=0; i < 20; i++)
			{
				if (xboxRecurseScanDir(fullDirName, pppFiles))
				{
					break;
				}

				Sleep(3000);
			}
		}
		else
		{
			bool bFound = false;

			for (i=0; i < eaSize(pppFiles); i++)
			{
				if (stricmp((*pppFiles)[i]->fullName, fileAttr.Name) == 0)
				{
					bFound = true;
					break;
				}
			}

			if (!bFound)
			{


				XboxFileInfo *pInfo = calloc(sizeof(XboxFileInfo), 1);
				sprintf(pInfo->fullName, "%s%s", pDirName, fileAttr.Name);
				pInfo->iSize = fileAttr.SizeLow + (((U64)fileAttr.SizeHigh) << 32);
			
				pInfo->iCreateTime = timeSecondsSince2000FromFileTime(&fileAttr.CreationTime);
				pInfo->iModTime = timeSecondsSince2000FromFileTime(&fileAttr.ChangeTime);


				eaPush(pppFiles, pInfo);
			}
		}

	}


	if (error != XBDM_ENDOFLIST)
	{
		DmCloseDir(pWalkDir);
		xboxHandleError(error);
		return false;
	}

	DmCloseDir(pWalkDir);
#endif
	return true;
}

char *xboxGetBinDir(void)
{
	static char sDir[MAX_PATH] = "";
	char *pProgramFilesFolder = NULL;


	if (!sDir[0])
	{
		GetEnvironmentVariable_UTF8("ProgramFiles", &pProgramFilesFolder);			
		sprintf(sDir, "%s\\microsoft xbox 360 sdk\\bin\\win32", pProgramFilesFolder);
		estrDestroy(&pProgramFilesFolder);
	}

	return sDir;
}

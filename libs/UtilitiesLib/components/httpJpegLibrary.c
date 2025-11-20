#include "httpJpegLibrary.h"
#include "StashTable.h"
#include "estring.h"
#include "utils.h"
#include "file.h"
#include "GlobalTypes.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

static StashTable sJpegCBTable = NULL;

static char sJPEGError[MAX_PATH + 64];



void JpegLibrary_GetJpeg(char *pInName, UrlArgumentList *pArgList, JpegLibrary_ReturnJpegCB *pReturnCB, void *pUserData)
{
	char *pFirstUnderscore = strchr(pInName, '_');
	void *pVoidCB;
	JpegLibrary_GetJpegCB *pCB;
	bool bResult;

	if (!sJpegCBTable)
	{
		pReturnCB(NULL, 0, 0, "JPEG system uninitialized", pUserData);
		return;
	}

	if (!pFirstUnderscore)
	{
		sprintf(sJPEGError, "Bad syntax in JPEG name %s", pInName);
		pReturnCB(NULL, 0, 0, sJPEGError, pUserData);
		return;
	}

	*pFirstUnderscore = 0;
	bResult = stashFindPointer(sJpegCBTable, pInName, &pVoidCB);
	*pFirstUnderscore = '_';

	if (!bResult)
	{
		sprintf(sJPEGError, "Unrecognized JPEG name %s", pInName);
		pReturnCB(NULL, 0, 0, sJPEGError, pUserData);
		return;
	}

	pCB = pVoidCB;

	pCB(pFirstUnderscore + 1, pArgList, pReturnCB, pUserData);
}

void JpegLibrary_RegisterCB(char *pPrefix, JpegLibrary_GetJpegCB *pCB)
{
	if (!sJpegCBTable)
	{
		sJpegCBTable = stashTableCreateWithStringKeys(16, StashDefault);
	}

	stashAddPointer(sJpegCBTable, pPrefix, pCB, true);
}


void JpegLibrary_FILE_CB(char *pName, UrlArgumentList *pArgList, JpegLibrary_ReturnJpegCB *pCB, void *pUserData)
{
	char *pBuf;
	int iSize;

	if (!strEndsWith(pName, ".jpg"))
	{
		sprintf(sJPEGError, "Invalid filename while opening %s... file doesn't end in .jpg", pName);
		pCB(NULL, 0, 0, sJPEGError, pUserData);
		return;
	}

	pBuf = fileAlloc(pName, &iSize);

	if (pBuf)
	{
		pCB(pBuf, iSize, 0, NULL, pUserData);
		free(pBuf);
		return;
	}

	sprintf(sJPEGError, "Couldn't open file %s", pName);
	pCB(NULL, 0, 0, sJPEGError, pUserData);

}



AUTO_RUN;
void JpegLibrary_RegisterDefaultCBs(void)
{
	JpegLibrary_RegisterCB("FILE", JpegLibrary_FILE_CB);
}



void JpegLibrary_GetFixedUpJpegFileName(char **ppEstrOut, char *pIn)
{
	estrPrintf(ppEstrOut, "%s_%d_FILE_%s", GlobalTypeToName(GetAppGlobalType()), GetAppGlobalID(), pIn);
}

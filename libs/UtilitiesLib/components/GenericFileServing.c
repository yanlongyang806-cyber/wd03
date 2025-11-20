#include "GenericFileServing.h"
#include "earray.h"
#include "file.h"
#include "timing.h"

static int sGenericMaxFileServingBytesPerFilePerTick = 1024 * 4;

static char **sppExposedDirectories = NULL;

static bool sbGenericFileServingRunning = false;
static int sGenericFileServingInactivityTimeoutPerHandle = 60;

typedef struct
{
	int iRequestID;
	FILE *pFile;
	U64 iBytesSent;
	U64 iFileSize;
	U64 iBytesRequestedUnsent;
	FileServingRequestFulfilledCallBack *pFulfillCB;
	U32 iLastUpdateTime;

} FileServingHandle;

static FileServingHandle **sppFileServingHandles = NULL;

typedef struct 
{
	char *pDomainName;
	GenericFileServing_SimpleDomainCB *pCB;
} FileServingSimpleDomain;

static FileServingSimpleDomain **sppSimpleDomains = {0};


void GenericFileServing_RegisterSimpleDomain(char *pDomainName, GenericFileServing_SimpleDomainCB *pCB)
{
	FileServingSimpleDomain *pNewDomain = calloc(sizeof(FileServingSimpleDomain), 1);
	pNewDomain->pDomainName = strdup(pDomainName);
	pNewDomain->pCB = pCB;

	eaPush(&sppSimpleDomains, pNewDomain);
}


static FileServingHandle *FindHandleFromID(int iRequestID, int *piIndex)
{
	int i;

	for (i=0; i < eaSize(&sppFileServingHandles); i++)
	{
		if (sppFileServingHandles[i]->iRequestID == iRequestID)
		{
			if (piIndex)
			{
				*piIndex = i;
			}

			return sppFileServingHandles[i];
		}
	}

	return NULL;
}

static void StopFileServing(FileServingHandle *pHandle, int iIndex)
{
	fclose(pHandle->pFile);
	free(pHandle);
	eaRemoveFast(&sppFileServingHandles, iIndex);
}



void GenericFileServing_CommandCallBack(char *pFileName, int iRequestID, enumFileServingCommand eCommand,
	U64 iBytesRequested, FileServingRequestFulfilledCallBack *pFulfillCB)
{
	GlobalType eContainerType;
	ContainerID iContainerID;
	static char *spTypeString = NULL;
	static char *spInnerName = NULL;

	if (!sbGenericFileServingRunning)
	{
		pFulfillCB(iRequestID, "File serving is not running on this server", 0, 0, 0, NULL);
		return;
	}

	switch(eCommand)
	{
	case FILESERVING_BEGIN:
		{
			int iDirNum;
			FILE *pFile = NULL;
			U64 iSize;
			FileServingHandle *pNewHandle;

			if (!DeconstructFileServingName(pFileName, &eContainerType, &iContainerID, &spTypeString, &spInnerName))
			{
				pFulfillCB(iRequestID, STACK_SPRINTF("Bad filename syntax: %s", pFileName), 0, 0, 0, NULL);
				return;
			}

			if (eContainerType != GetAppGlobalType())
			{
				pFulfillCB(iRequestID, STACK_SPRINTF("Server of type %s received file request for server type %s",
					GlobalTypeToName(GetAppGlobalType()), GlobalTypeToName(eContainerType)), 0, 0, 0, NULL);
				return;
			}

			if (iContainerID && iContainerID != GetAppGlobalID())
			{
				pFulfillCB(iRequestID, STACK_SPRINTF("Server %s[%u] received file request for server %s[%u]",
					GlobalTypeToName(GetAppGlobalType()), GetAppGlobalID(), GlobalTypeToName(GetAppGlobalType()), iContainerID), 0, 0, 0, NULL);
				return;
			}

			if (stricmp(spTypeString, "fileSystem") != 0)
			{
				int i;
				bool bFound = false;

				for (i=0; i < eaSize(&sppSimpleDomains); i++)
				{
					if (stricmp(spTypeString, sppSimpleDomains[i]->pDomainName) == 0)
					{
						char *pActualName;
						char *pErrorString = NULL;

						bFound = true;

						pActualName = sppSimpleDomains[i]->pCB(spInnerName, &pErrorString);

						if (!pActualName)
						{
							pFulfillCB(iRequestID, pErrorString ? pErrorString : "UNKNOWN DOMAIN ERROR", 0, 0, 0, NULL);
							estrDestroy(&pErrorString);
							return;
						}

						pFile = fopen(pActualName, "rb");

						if (!pFile)
						{
							pFulfillCB(iRequestID, STACK_SPRINTF("can't open %s", pActualName), 0, 0, 0, NULL);
							return;
						}

						break;
					}
				}

				if (!bFound)
				{
					pFulfillCB(iRequestID, STACK_SPRINTF("Unknown file serving domain %s", spTypeString), 0, 0, 0, NULL);
					return;
				}
			}

			if (!pFile)
			{
				for (iDirNum=0; iDirNum < eaSize(&sppExposedDirectories); iDirNum++)
				{
					char fullPath[CRYPTIC_MAX_PATH];
					sprintf(fullPath, "%s/%s", sppExposedDirectories[iDirNum], spInnerName);
					pFile = fopen(fullPath, "rb");
					if (pFile)
					{
						break;
					}
				}
			}

			if (!pFile)
			{
				pFulfillCB(iRequestID, STACK_SPRINTF("File %s not found", spInnerName), 0, 0, 0, NULL);
				return;
			}

			fseek(pFile, 0, SEEK_END);
			iSize = ftell(pFile);
			fseek(pFile, 0, SEEK_SET);

			if (iSize <= iBytesRequested && iSize <= sGenericMaxFileServingBytesPerFilePerTick)
			{
				char *pBuf = malloc(iSize);
				if (fread(pBuf, 1, iSize, pFile) != iSize)
				{
					free(pBuf);
					if(pFile && pFile->nameptr) {
						char* buffer = NULL;
						estrCreate(&buffer);
						estrPrintf(&buffer, "Error reading from file: %s", pFile->nameptr);
						pFulfillCB(iRequestID, buffer, 0, 0, 0, NULL);
						estrDestroy(&buffer);
					} else {
						pFulfillCB(iRequestID, "Error reading from file: (null)", 0, 0, 0, NULL);
					}
					return;
				}

				pFulfillCB(iRequestID, NULL, iSize, 0, iSize, pBuf);
				return;
			}

			pNewHandle = calloc(sizeof(*pNewHandle), 1);
			pNewHandle->iRequestID = iRequestID;
			pNewHandle->pFile = pFile;
			pNewHandle->iFileSize = iSize;
			pNewHandle->iBytesRequestedUnsent = iBytesRequested;
			pNewHandle->pFulfillCB = pFulfillCB;
			pNewHandle->iLastUpdateTime = timeSecondsSince2000();

			eaPush(&sppFileServingHandles, pNewHandle);
		}
		return;

	case FILESERVING_PUMP:
		{
			FileServingHandle *pHandle = FindHandleFromID(iRequestID, NULL);
			if (!pHandle)
			{
				pFulfillCB(iRequestID, "File serving request corrupted or timed out", 0, 0, 0, NULL);
				return;
			}

			pHandle->iBytesRequestedUnsent += iBytesRequested;
			pHandle->iLastUpdateTime = timeSecondsSince2000();
		}
		break;

	case FILESERVING_CANCEL:
		{
			int iIndex;
			FileServingHandle *pHandle = FindHandleFromID(iRequestID, &iIndex);

			if (pHandle)
			{
				StopFileServing(pHandle, iIndex);
			}
		}
		break;
	}
}

void GenericFileServing_Begin(int iBytesPerFilePerTick, int iInactivityTimeout)
{
	sbGenericFileServingRunning = true;
	
	if (iBytesPerFilePerTick)
	{
		sGenericMaxFileServingBytesPerFilePerTick = iBytesPerFilePerTick;
	}

	if (iInactivityTimeout)
	{
		sGenericFileServingInactivityTimeoutPerHandle = iInactivityTimeout;
	}
}

void GenericFileServing_ExposeDirectory(char *pDirName)
{
	eaPush(&sppExposedDirectories, pDirName);
}

void GenericFileServing_Tick(void)
{
	int iHandleNum;
	FileServingHandle *pHandle;
	U32 iOldTime;

	if (!sbGenericFileServingRunning)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	iOldTime = timeSecondsSince2000() - sGenericFileServingInactivityTimeoutPerHandle;

	for (iHandleNum = eaSize(&sppFileServingHandles) - 1; iHandleNum >= 0; iHandleNum--)
	{
		pHandle = sppFileServingHandles[iHandleNum];
		if (pHandle->iLastUpdateTime < iOldTime)
		{
			StopFileServing(pHandle, iHandleNum);
			continue;
		}

		if (pHandle->iBytesRequestedUnsent)
		{
			U64 iBytesToSendThisFrame = pHandle->iBytesRequestedUnsent;
			U64 iBytesLeftInFile = pHandle->iFileSize - pHandle->iBytesSent;
			char *pBuf;
			bool bDone = false;

			if (iBytesToSendThisFrame > sGenericMaxFileServingBytesPerFilePerTick)
			{
				iBytesToSendThisFrame = sGenericMaxFileServingBytesPerFilePerTick;
			}

			if (iBytesToSendThisFrame > iBytesLeftInFile)
			{
				iBytesToSendThisFrame = iBytesLeftInFile;
			}

			pBuf = malloc(iBytesToSendThisFrame);

			if (fread(pBuf, 1, iBytesToSendThisFrame, pHandle->pFile) != iBytesToSendThisFrame)
			{
				if(pHandle->pFile && pHandle->pFile->nameptr) {
					char* buffer = NULL;
					estrCreate(&buffer);
					estrPrintf(&buffer, "Error reading from file: %s", pHandle->pFile->nameptr);
					pHandle->pFulfillCB(pHandle->iRequestID, buffer, 0, 0, 0, NULL);
					estrDestroy(&buffer);
				} else {
					pHandle->pFulfillCB(pHandle->iRequestID, "Error reading from file: (null)", 0, 0, 0, NULL);
				}
				bDone = true;
				free(pBuf);
			}
			else
			{
				pHandle->pFulfillCB(pHandle->iRequestID, NULL, pHandle->iFileSize, pHandle->iBytesSent, iBytesToSendThisFrame, pBuf);
				pHandle->iBytesSent += iBytesToSendThisFrame;
				pHandle->iBytesRequestedUnsent -= iBytesToSendThisFrame;
				if (pHandle->iBytesSent == pHandle->iFileSize)
				{
					bDone = true;
				}
			}

			if (bDone)
			{
				StopFileServing(pHandle, iHandleNum);
			}
		}
	}

	PERFINFO_AUTO_STOP();
}



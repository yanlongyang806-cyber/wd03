#include "CommandQueue.h"
#include "estring.h"
#include "timing.h"

void ExpandQueue(CommandQueue *pQueue, int iNewMinSize)
{
	int iNewSize = pQueue->iSpaceSize * 2;
	char *pNewSpace;

	while (iNewSize < iNewMinSize)
	{
		iNewSize *= 2;
	}

	pNewSpace = stmalloc(iNewSize, pQueue);
	
	if (pQueue->iDataStartOffset + pQueue->iDataSize > pQueue->iSpaceSize)
	{
		int iSize1 = pQueue->iSpaceSize - pQueue->iDataStartOffset;
		memcpy(pNewSpace, pQueue->pSpace + pQueue->iDataStartOffset, iSize1);
		memcpy(pNewSpace + iSize1, pQueue->pSpace, pQueue->iDataSize - iSize1);
	}
	else
	{
		memcpy(pNewSpace, pQueue->pSpace + pQueue->iDataStartOffset, pQueue->iDataSize);
	}

	pQueue->iSpaceSize = iNewSize;
	pQueue->iDataStartOffset = 0;

	free(pQueue->pSpace);
	pQueue->pSpace = pNewSpace;
}

void CommandQueue_Write(CommandQueue *pQueue, const void *pInData, int iInDataSize)
{
	if (iInDataSize <= 0)
	{
		return;
	}


	CommandQueue_EnterCriticalSection(pQueue);

	devassert(!pQueue->dev_lock);			// For validating that the queue isn't being written to

	if (iInDataSize > pQueue->iSpaceSize - pQueue->iDataSize)
	{
		ExpandQueue(pQueue, iInDataSize + pQueue->iSpaceSize);

		//we just expanded the queue, so we know the data starts at 0, so the copying is simple
		memcpy(pQueue->pSpace + pQueue->iDataSize, pInData, iInDataSize);
	}
	else
	{
		int iContiguousSpace = pQueue->iSpaceSize - pQueue->iDataStartOffset - pQueue->iDataSize;

		if (iContiguousSpace > 0 && iContiguousSpace < iInDataSize)
		{
			memcpy(pQueue->pSpace + pQueue->iDataStartOffset + pQueue->iDataSize,
				pInData, iContiguousSpace);
			memcpy(pQueue->pSpace, ((char*)pInData) + iContiguousSpace, iInDataSize - iContiguousSpace);
		}
		else
		{
			memcpy(pQueue->pSpace + (pQueue->iDataStartOffset + pQueue->iDataSize) % pQueue->iSpaceSize,
				pInData, iInDataSize);
		}
	}

	pQueue->iDataSize += iInDataSize;


	CommandQueue_LeaveCriticalSection(pQueue);
	
}

int CommandQueue_Read(CommandQueue *pQueue, void *pDestBuffer, int iBytesToRead)
{
	if (iBytesToRead <= 0)
	{
		return 0;
	}


	CommandQueue_EnterCriticalSection(pQueue);
	

	if (iBytesToRead > pQueue->iDataSize)
	{
		iBytesToRead = pQueue->iDataSize;
	}

	if (pQueue->iDataStartOffset + iBytesToRead > pQueue->iSpaceSize)
	{
		int iSize1 = pQueue->iSpaceSize - pQueue->iDataStartOffset;
		memcpy(pDestBuffer, pQueue->pSpace + pQueue->iDataStartOffset, iSize1);
		memcpy(((char*)pDestBuffer) + iSize1, pQueue->pSpace, iBytesToRead - iSize1);
	}
	else
	{
		memcpy(pDestBuffer, pQueue->pSpace + pQueue->iDataStartOffset, iBytesToRead);
	}

	pQueue->iDataStartOffset += iBytesToRead;
	pQueue->iDataStartOffset %= pQueue->iSpaceSize;

	pQueue->iDataSize -= iBytesToRead;


	CommandQueue_LeaveCriticalSection(pQueue);
	

	return iBytesToRead;
}

U8 CommandQueue_ReadByte(CommandQueue *pQueue)
{
	U8 b;
	CommandQueue_Read(pQueue, &b, 1);
	return b;
}

void CommandQueue_WriteByte(CommandQueue *pQueue, U8 b)
{
	CommandQueue_Write(pQueue, &b, 1);
}

CommandQueue *CommandQueue_Create_Dbg(int iStartingSize, bool bThreadSafe MEM_DBG_PARMS)
{
	CommandQueue *pQueue;

	if (iStartingSize < 128)
	{
		iStartingSize = 128;
	}

	pQueue = (CommandQueue*)scalloc(1, sizeof(CommandQueue));

	pQueue->caller_fname = caller_fname;
	pQueue->line = line;

	pQueue->pSpace = smalloc(iStartingSize);

	pQueue->iSpaceSize = iStartingSize;
	pQueue->iDataStartOffset = 0;
	pQueue->iDataSize = 0;

	if (bThreadSafe)
	{
		pQueue->pCriticalSection = smalloc(sizeof(CRITICAL_SECTION));
		InitializeCriticalSection(pQueue->pCriticalSection);
	}
	else
	{
		pQueue->pCriticalSection = NULL;
	}

	return pQueue;
}

void CommandQueue_WriteString(CommandQueue *pQueue, const char *pString)
{
	if (pQueue)
	{
		int iSize = (int)strlen(pString);

		CommandQueue_EnterCriticalSection(pQueue);

		CommandQueue_Write(pQueue, (char*)&iSize, sizeof(int));
		CommandQueue_Write(pQueue, pString, iSize);

		CommandQueue_LeaveCriticalSection(pQueue);

	}
}

void CommandQueue_ReadString_Dbg(CommandQueue *pQueue, char **ppEString MEM_DBG_PARMS)
{
	int iSize;

	CommandQueue_EnterCriticalSection(pQueue);

	CommandQueue_Read(pQueue, &iSize, sizeof(int));

	if (iSize)
	{
		int iCurStringLength = estrLength(ppEString);

		estrForceSize_dbg(ppEString, iCurStringLength + iSize, caller_fname, line);
		CommandQueue_Read(pQueue, (*ppEString) + iCurStringLength, iSize);
	}

	CommandQueue_LeaveCriticalSection(pQueue);
}

void CommandQueue_ExecuteAllCommandsEx(CommandQueue *pQueue, bool bReset)
{
	PerfInfoGuard* piGuard;

	if(!pQueue)
	{
		return;
	}

	CommandQueue_EnterCriticalSection(pQueue);

	if(pQueue->iDataSize)
	{
		PERFINFO_AUTO_START_FUNC_GUARD(&piGuard);
		while (pQueue->iDataSize)
		{
			QueuedCommandCallBack *pCB;

			CommandQueue_Read(pQueue, &pCB, sizeof(QueuedCommandCallBack*));

			pCB(pQueue);
		}
		PERFINFO_AUTO_STOP_GUARD(&piGuard);
	}

	if (bReset)
	{
		// I believe the author's intention was for this to return the command queue to the same state is was in right before execute all commands was ran
		// this would NOT work if the queue's data starts to wrap around in memory and overwrite older values (which is why I moved the reset into the execute all)
		pQueue->iDataSize = pQueue->iDataSize + pQueue->iDataStartOffset;
		pQueue->iDataStartOffset = 0;
	}

	CommandQueue_LeaveCriticalSection(pQueue);
}

void CommandQueue_Destroy( CommandQueue *pQueue )
{
	if(!pQueue)
		return;

	PERFINFO_AUTO_START_FUNC();

	if (pQueue->pCriticalSection)
	{
		DeleteCriticalSection(pQueue->pCriticalSection);
		free(pQueue->pCriticalSection);
	}
	free(pQueue->pSpace);
	free(pQueue);

	PERFINFO_AUTO_STOP();
}


/*
char *pStrings[] = 
{
	"Alex is cool",
	"No, I mean it, Alex is cool",
	"Wow this string is extremely long and goes on and on and on and on and on and on",
	"",
	"--",
	".",
	"ThisOneIsMediumSized",
};

#define NUM_STRINGS (sizeof(pStrings) / sizeof(pStrings[0]))

void CheckString(char *pString)
{
	int i;

	for (i=0; i < NUM_STRINGS; i++)
	{
		if (strcmp(pString, pStrings[i]) == 0)
		{
			return;
		}
	}

	assertmsgf(0, "Unknown string %s\n", pString);
}

AUTO_COMMAND;
int CommandQueue_Test(void)
{
	CommandQueue *pQueue = CommandQueue_Create(16, false);
	char *pString;

	int iCounter = 0;

	estrCreate(&pString);

	while (1)
	{
		int iRand = rand() % 10000;
		iCounter++;

		if (iCounter % 100 == 0)
		{
			printf("Processed %d strings... current spacesize %d datasize %d\n",
				iCounter, pQueue->iSpaceSize, pQueue->iDataSize);
		}

		if (iRand == 0)
		{
			printf("Clearing Queue....\n");

			while (CommandQueue_GetSize(pQueue))
			{
				estrClear(&pString);
				CommandQueue_ReadString(pQueue, &pString);

				CheckString(pString);
			}
		}
		else if (iRand < 4500)
		{
			if (CommandQueue_GetSize(pQueue))
			{
				estrClear(&pString);
				CommandQueue_ReadString(pQueue, &pString);

				CheckString(pString);
			}
		}
		else
		{
			CommandQueue_WriteString(pQueue, pStrings[rand() % NUM_STRINGS]);
		}
	}

	return 1;
}

*/
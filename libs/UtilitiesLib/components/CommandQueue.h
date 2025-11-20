#pragma once
GCC_SYSTEM

#include "stdtypes.h"
#include "windefinclude.h"


typedef struct CommandQueue
{
	char *pSpace;
	int iSpaceSize;

	int iDataStartOffset;
	int iDataSize;

	CRITICAL_SECTION *pCriticalSection;

	const char *caller_fname;
	int line;

	U32	dev_lock : 1;		// If this is set, will assert on write
} CommandQueue;

typedef void QueuedCommandCallBack(CommandQueue *pQueue);

void CommandQueue_WriteByte(CommandQueue *pQueue, U8 iValue);
U8 CommandQueue_ReadByte(CommandQueue *pQueue);

void CommandQueue_Write(CommandQueue *pQueue, const void *pData, int iDataSize);
//returns number of bytes read
int CommandQueue_Read(CommandQueue *pQueue, void *pDestBuffer, int iBytesToRead);


__forceinline static int CommandQueue_GetSize(CommandQueue *pQueue)
{
	return pQueue->iDataSize;
}

__forceinline static void CommandQueue_EnterCriticalSection(CommandQueue *pQueue)
{
	if (pQueue->pCriticalSection)
	{
		EnterCriticalSection(pQueue->pCriticalSection);
	}
}

__forceinline static void CommandQueue_LeaveCriticalSection(CommandQueue *pQueue)
{
	if (pQueue->pCriticalSection)
	{
		LeaveCriticalSection(pQueue->pCriticalSection);
	}
}

__forceinline static void CommandQueue_Lock(CommandQueue *pQueue)
{
	ASSERT_FALSE_AND_SET(pQueue->dev_lock);
}

__forceinline static void CommandQueue_Unlock(CommandQueue *pQueue)
{
	ASSERT_TRUE_AND_RESET(pQueue->dev_lock);
}

CommandQueue *CommandQueue_Create_Dbg(int iStartingSize, bool bThreadSafe MEM_DBG_PARMS);
#define CommandQueue_Create(iStartingSize, bThreadSafe) CommandQueue_Create_Dbg(iStartingSize, bThreadSafe MEM_DBG_PARMS_INIT)

void CommandQueue_WriteString(CommandQueue *pQueue, const char *pString);
//concats the read string onto the EString
void CommandQueue_ReadString_Dbg(CommandQueue *pQueue, char **ppEString MEM_DBG_PARMS);
#define CommandQueue_ReadString(pQueue, ppEString) CommandQueue_ReadString_Dbg(pQueue, ppEString MEM_DBG_PARMS_INIT)

void CommandQueue_ExecuteAllCommandsEx(CommandQueue *pQueue, bool bReset);
#define CommandQueue_ExecuteAllCommands(pQueue) CommandQueue_ExecuteAllCommandsEx(pQueue, false)

void CommandQueue_Destroy(CommandQueue *pQueue);

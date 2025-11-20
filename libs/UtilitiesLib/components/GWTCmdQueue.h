#ifndef _GWTCMDQUEUE_H_
#define _GWTCMDQUEUE_H_
#pragma once
GCC_SYSTEM

#include "timing.h"

#include "GenericWorkerThread.h"

C_DECLARATIONS_BEGIN

typedef struct GenericWorkerThread GenericWorkerThread;

// TODO  - add WorkerThreadPrivate.h, export in WorkerThread.h, or make an accessor to patch the size
#define OVERRUN_CHECKS_ON 1

// TODO  - add WorkerThreadPrivate.h, export in WorkerThread.h, or make an accessor to patch the size
#define WT_OVERRUN_VALUE 0x4BADBEEF


// TODO  - export in WTCmdPacket.h, or make the accessor to patch the size not inline

typedef struct GWTQueuedCmd
{
	U32			processOrder;
	int			type;
	U32			dataSize;
	U32			numBlocks;
	U32			size;
	S64			queueTimeTicks;
	int			locked;
	int			needsProcessing : 1;
	void		*data;
} GWTQueuedCmd;

__forceinline static void *gwtGetQueuedCmdData(const GWTQueuedCmd *cmd)
{
	return cmd->data ? cmd->data : cmd + 1;
}

// Used to allow the thread commands to easily queue messages
typedef struct GWTCmdPacket
{
	GenericWorkerThreadManager	*manager;
	GenericWorkerThreadInternal	*thread;
} GWTCmdPacket;

SA_RET_OP_VALID GWTQueuedCmd *gwtAllocCmdPkt(GenericWorkerThreadManager *wt, int cmd_type, int size, int dataSize);

// TODO  - add WorkerThreadPrivate.h, export in WorkerThread.h, or make an accessor to patch the size
__forceinline static void gwtTruncateQueuedCmd(GWTQueuedCmd *queued_cmd, U32 length)
{
#if OVERRUN_CHECKS_ON
	U8 *data;
 	data = gwtGetQueuedCmdData(queued_cmd);
	// test and then move the buffer overrun check
	devassert(length <= queued_cmd->size);
	assertmsg(*(U32*)(data + queued_cmd->size - sizeof(U32)) == WT_OVERRUN_VALUE, "This command overran the CmdQueue");
	*(U32*)(data + length) = WT_OVERRUN_VALUE;
	length += sizeof(U32);
#endif
	queued_cmd->size = length;
}

typedef struct GWTCmdQueueThreadLock
{
	U32 localScanStart;
} GWTCmdQueueThreadLock;

typedef struct GWTCmdQueue
{
	GWTQueuedCmd		*queue;
	U32				maxQueued;
	volatile U32	start, end;
	volatile U32	startMoveLock;
	volatile U32	scanstart;
	U32				nextEnd;
	U32				insertLock;
	U32				readLock;
	U32				removeLock;
	U32				numThreads;
	GWTCmdQueueThreadLock *threadLocks;
	U32				*processCounter;
} GWTCmdQueue;

typedef GWTCmdQueue GWTMsgQueue;

__forceinline static bool isEarlierInTheQueue(U32 lhs, U32 rhs, U32 end, U32 maxQueued)
{
	//(rhs-lhs)*(maxQueued-1) is the distance from lhs to rhs in the queue.
	//If that is less than or equal to (end-lhs)&(maxQueued-1), it means that rhs is between lhs and end.
	//We also need the distance from lhs to rhs to be non-zero
	U32 dist = ((rhs-lhs)&(maxQueued-1));
	return (dist > 0) && (((rhs-lhs)&(maxQueued-1)) <= ((end-lhs)&(maxQueued-1)));
}

void advanceGWTCmdQueueStartPositions(GWTCmdQueue *cmdQueue, U32 threadIndex);
void handleQueuedGWTCmd(GWTQueuedCmd *cmd, GWTDispatchCallback *dispatch_table, GWTDefaultDispatchCallback default_dispatch, void *user_data, GWTCmdPacket * packet);
	
__forceinline static bool isGWTCmdQueueEmpty(GWTCmdQueue *cmdQueue)
{
	return cmdQueue->start == cmdQueue->end;
}

void removeStoredGWTCmd(GWTCmdQueue *cmdQueue, GWTQueuedCmd *cmd);

extern bool gBackgroundThreadsCanMoveScanStart;

typedef bool (*GWTLockCallback)(GWTQueuedCmd* cmd);

#define lockStoredGWTCmd(threadIndex, cmdQueue) lockStoredGWTCmdEx(threadIndex, cmdQueue, NULL)
GWTQueuedCmd *lockStoredGWTCmdEx(U32 threadIndex, GWTCmdQueue *cmdQueue, GWTLockCallback callback);

//Does this need to take a thread index?
__forceinline static void unlockStoredGWTCmd(GWTCmdQueue *cmdQueue, GWTQueuedCmd *cmd)
{
	U32 wasLocked = InterlockedCompareExchange(&cmd->locked, 0, 1);
	assert(wasLocked);
}

void initGWTCmdQueue(GWTCmdQueue *cmdQueue, int maxCmds, U32 numThreads, U32 *processCounter MEM_DBG_PARMS);

#define allocStoredGWTCmd(cmdQueue, cmdType, size, useMallocWhenFull) allocStoredGWTCmdEx(cmdQueue, cmdType, size, useMallocWhenFull, size)
// A return value of NULL means unable to add, so flush and try again.
GWTQueuedCmd *allocStoredGWTCmdEx(GWTCmdQueue *cmdQueue, int cmdType, U32 size, bool useMallocWhenFull, U32 dataSize);

__forceinline void *getDataFromGWTCmd(GWTQueuedCmd *cmd)
{
	void *data;
#if OVERRUN_CHECKS_ON
	if (cmd->data || cmd->size==4) // Size 0 -> data == NULL
#else
	if (cmd->data || cmd->size==0) // Size 0 -> data == NULL
#endif
		data = cmd->data;
	else
		data = (U8*)(cmd+1);

	return data;
}

void handleQueuedGWTCmd(GWTQueuedCmd *cmd, GWTDispatchCallback *dispatch_table, GWTDefaultDispatchCallback default_dispatch, void *user_data, GWTCmdPacket * packet);

C_DECLARATIONS_END

#endif
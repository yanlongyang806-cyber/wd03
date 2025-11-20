#include "wininclude.h"
#include "GenericWorkerThread.h"
#include "earray.h"
#include "ThreadManager.h"
#include "GWTCmdQueue.h"
#include "ScratchStack.h"
#include "EventTimingLog.h"
#include "timing.h"
#include "timing_profiler_interface.h"

// Assumptions required for correct operation:
// cmdQueue->start is earlier than or equal to cmdQueue->end
// cmdQueue->scanStart is between cmdQueue->start and cmdQueue->end inclusive.
// Any commmand that is earlier in the queue than cmdQueue->scanStart but after or equal to 
//		cmdQueue->start must have needsProcessing = false
// No background thread can access a command earlier in the queue than its own localscanstart
// scanStart, localScanStart, and start can only move towards end.
// No thread can change needsProcessing without the lock on the command.

bool gBackgroundThreadsCanMoveScanStart = true;
AUTO_CMD_INT(gBackgroundThreadsCanMoveScanStart, BackgroundThreadsCanMoveScanStart) ACMD_CMDLINE;

FORCEINLINE U32 getNextGWTCmd(GWTCmdQueue *cmdQueue, U32 index)
{
	return (index + cmdQueue->queue[index].numBlocks) & (cmdQueue->maxQueued - 1);
}

// This walks the command queue until it finds something it can process.
GWTQueuedCmd *lockStoredGWTCmdEx(U32 threadIndex, GWTCmdQueue *cmdQueue, GWTLockCallback callback)
{
	U32 wasLocked;
	U32 iter;

	// Attempt to move scanstart and start
	advanceGWTCmdQueueStartPositions(cmdQueue, threadIndex);

	if(isGWTCmdQueueEmpty(cmdQueue))
		return NULL;

	PERFINFO_AUTO_START_FUNC_L2();
	// Store cmdQueue->scanStart in our localScanStart so that other threads can know where this thread has started.
	// We set localScanStart both here and in advanceGWTCmdQueueStartPosition because we can't assume that anything
	// actually happens in advanceGWTCmdQueueStartPosition.
	cmdQueue->threadLocks[threadIndex].localScanStart = cmdQueue->scanstart; 
	iter = cmdQueue->threadLocks[threadIndex].localScanStart;

	// Loop until we find something or we hit the end
	do
	{
		GWTQueuedCmd *returnValue;
		GWTQueuedCmd *cmd = &cmdQueue->queue[iter];

		if(iter == (int)cmdQueue->end)
		{
			break;
		}

		if(InterlockedCompareExchange(&cmd->locked, 1, 0) != 0)
		{
			ADD_MISC_COUNT_L2(1, "Locked");
			// Can't get the lock, move on.
			continue;
		}

		if(!cmd->needsProcessing)
		{
			ADD_MISC_COUNT_L2(1, "Already processed");
			// Nothing to do here.
			wasLocked = InterlockedCompareExchange(&cmd->locked, 0, 1);
			assert(wasLocked);
			continue;
		}

		assert(cmd->numBlocks);
		assert(cmd->needsProcessing);

		if(callback)
		{
			if(!callback(cmd))
			{
				ADD_MISC_COUNT_L2(1, "Object locked");
				// Can't get the Object Lock
				wasLocked = InterlockedCompareExchange(&cmd->locked, 0, 1);
				assert(wasLocked);
				continue;
			}
		}

		// Returning a locked command that needs processing
		returnValue = &cmdQueue->queue[iter];
		PERFINFO_AUTO_STOP_L2();
		return returnValue;
	} while(iter = getNextGWTCmd(cmdQueue, iter));

	// Found nothing
	ADD_MISC_COUNT_L2(1, "Found nothing");
	PERFINFO_AUTO_STOP_L2();
	return NULL;
}

// Move scanStart and start forward to make room for new commands.
void advanceGWTCmdQueueStartPositions(GWTCmdQueue *cmdQueue, U32 threadIndex)
{
	U32 newScanStart;
	U32 wasLocked;
	U32 i;
	U32 target;
	// Try to acquire startMoveLock. If this fails, just return.
	// Another thread is already attempting to update.
	if(InterlockedCompareExchange(&cmdQueue->startMoveLock, 1, 0) != 0)
		return;

	// Update scan start
	// Starting at the current scanstart, walk until we find a command that is locked or that needs processing.
	newScanStart = cmdQueue->scanstart;
	do 
	{
		GWTQueuedCmd *cmd = &cmdQueue->queue[newScanStart];

		if(newScanStart == cmdQueue->end)
			break;

		if(InterlockedCompareExchange(&cmd->locked, 1, 0) != 0)
		{
			break;
		}

		if(cmd->needsProcessing)
		{
			wasLocked = InterlockedCompareExchange(&cmd->locked, 0, 1);
			assert(wasLocked);
			break;
		}
		// Doesn't need to check object locks because the command does not need processing
		newScanStart = getNextGWTCmd(cmdQueue, newScanStart);
		wasLocked = InterlockedCompareExchange(&cmd->locked, 0, 1);
		assert(wasLocked);
	} while (true);

	// Update scanstart to the new value, and update this thread's localScanStart
	cmdQueue->scanstart = newScanStart;
	cmdQueue->threadLocks[threadIndex].localScanStart = cmdQueue->scanstart;

	// Determine how far forward we can move start
	// The farthest we can possibly move it is scanstart, so start there.
	// We then push the target back towards start to make sure we don't stomp on any
	// currently active scan. 

	target = cmdQueue->scanstart;
	if(target == cmdQueue->start)
	{
		wasLocked = InterlockedCompareExchange(&cmdQueue->startMoveLock, 0, 1);
		assert(wasLocked);
		return;
	}
	// Now check each thread's localScanStart. This can only move towards end, so
	// we need no locking.
	for(i = 0; i < cmdQueue->numThreads; ++i)
	{
		GWTCmdQueueThreadLock *threadLock = &cmdQueue->threadLocks[i];
		U32 localScanStart = threadLock->localScanStart;
		if(isEarlierInTheQueue(localScanStart, target, cmdQueue->end, cmdQueue->maxQueued))
		{
			target = localScanStart;
		}
	}

	// Now actually move start. This does not need to lock the GWTQueuedCmd. 
	// Nothing between start and scanstart can have needsProcessing true, because scanstart
	// would have stopped at it. 
	while(isEarlierInTheQueue(cmdQueue->start, target, cmdQueue->end, cmdQueue->maxQueued))
	{
		// loop through everything up to target clearing numBlocks
		U32 numBlocks;
		GWTQueuedCmd *cmd = &cmdQueue->queue[cmdQueue->start];

		numBlocks = cmd->numBlocks;
		cmd->numBlocks = 0;
		assert(!cmd->locked);
		assert(numBlocks);
		assert(!cmd->needsProcessing);

		cmdQueue->start = (cmdQueue->start + numBlocks)&(cmdQueue->maxQueued-1);
	}

	wasLocked = InterlockedCompareExchange(&cmdQueue->startMoveLock, 0, 1);
	assert(wasLocked);
}

void removeStoredGWTCmd(GWTCmdQueue *cmdQueue, GWTQueuedCmd *cmd)
{
	U32 wasLocked;
	GWTQueuedCmd *entry;
	entry = cmd;
	assertmsgf(entry->numBlocks, "Trying to remove an invalid entry, entry->numBlocks == %d", entry->numBlocks); // Must be *before* the following line, lest the other thread use it right away

	entry->needsProcessing = false;
	wasLocked = InterlockedCompareExchange(&entry->locked, 0, 1);
	assert(wasLocked);
}

GWTQueuedCmd *allocStoredGWTCmdEx(GWTCmdQueue *cmdQueue, int cmdType, U32 size, bool useMallocWhenFull, U32 dataSize)
{
	int			free_blocks,numBlocks,filler_blocks,contiguous_blocks,malloc_size;
	GWTQueuedCmd	*cmd;
	PERFINFO_AUTO_START_FUNC_L2();
	if (size + sizeof(*cmd) > sizeof(*cmd) * cmdQueue->maxQueued/2)
	{
		malloc_size = size;
		size = 0;
		numBlocks = 1;
	}
	else
	{
		malloc_size = 0;
		numBlocks = 1 + (size + sizeof(GWTQueuedCmd) - 1) / sizeof(GWTQueuedCmd);
	}

	contiguous_blocks = cmdQueue->maxQueued - cmdQueue->end;
	if (numBlocks > contiguous_blocks)
		filler_blocks = contiguous_blocks;
	else
		filler_blocks = 0;
	for(;;)
	{
		free_blocks = (cmdQueue->start - (cmdQueue->end+1)) & (cmdQueue->maxQueued-1);
		if (numBlocks+filler_blocks <= free_blocks)
			break;

		if (useMallocWhenFull) {
			// Cannot flush and wait, switch to malloc
			if(!malloc_size && size)
			{
				malloc_size = size;
				size = 0;
				numBlocks = 1;
			}

			// Recalc padding/filler
			contiguous_blocks = cmdQueue->maxQueued - cmdQueue->end;
			if (numBlocks > contiguous_blocks)
				filler_blocks = contiguous_blocks;
			else
				filler_blocks = 0;
			continue;
		}
	
		PERFINFO_AUTO_STOP_L2();
		return NULL;
	}
	cmdQueue->nextEnd = (cmdQueue->end + numBlocks + filler_blocks) & (cmdQueue->maxQueued-1);

	cmd = &cmdQueue->queue[cmdQueue->end];
	if (filler_blocks)
	{
		memset(cmd,0,sizeof(*cmd));
		cmd->type = GWT_CMD_NOP;
		cmd->numBlocks = filler_blocks;
		cmd->needsProcessing = true;
		cmd = &cmdQueue->queue[(cmdQueue->end + filler_blocks) & (cmdQueue->maxQueued-1)];
		assert(cmd == &cmdQueue->queue[0]); // Otherwise OVERRUN_CHECKING won't work and my logic is wrong
	}
	memset(cmd,0,sizeof(*cmd));
	cmd->type		= cmdType;
	cmd->numBlocks = numBlocks;
	cmd->needsProcessing = true;
	cmd->dataSize = dataSize;
	cmd->processOrder = InterlockedIncrement(cmdQueue->processCounter);
	if (!malloc_size)
	{
		cmd->size = size;
		cmd->data = 0;
	}
	else
	{
		cmd->size = malloc_size;
		cmd->data = malloc(malloc_size);
	}
	PERFINFO_AUTO_STOP_L2();
	return cmd;
}

void handleQueuedGWTCmd(GWTQueuedCmd *cmd, GWTDispatchCallback *dispatch_table, GWTDefaultDispatchCallback default_dispatch, void *user_data,
	GWTCmdPacket * packet)
{
	void *data = getDataFromGWTCmd(cmd);

/*	gwtCmdSetupPacket(packet, cmd->type, data, 
#if OVERRUN_CHECKS_ON
		cmd->size - sizeof(U32)
#else
		cmd->size
#endif
		);*/
	if (cmd->type >= GWT_CMD_USER_START)
	{
		if (cmd->type < eaSize((void ***)&dispatch_table) && dispatch_table[cmd->type])
			dispatch_table[cmd->type](user_data, data, packet);
		else if (default_dispatch)
			default_dispatch(user_data, cmd->type, data, packet);
		else
		{
			static GWTQueuedCmd invalidCommand;
			invalidCommand = *cmd;
			assertmsg(0, "Unhandled command passed to worker thread!");
		}
	}
	else if (cmd->type == GWT_CMD_DEBUGCALLBACK)
	{
		(*((GWTDebugCallback*)data))((char*)data + sizeof(GWTDebugCallback), packet);
	}

	if (cmd->data)
		free(cmd->data);
}

void initGWTCmdQueue(GWTCmdQueue *cmdQueue, int maxCmds, U32 numThreads, U32 *processCounter MEM_DBG_PARMS)
{
	U32 i;
	cmdQueue->queue = scalloc(maxCmds, sizeof(GWTQueuedCmd));
	cmdQueue->maxQueued = maxCmds;
	cmdQueue->threadLocks = calloc(numThreads, sizeof(GWTCmdQueueThreadLock));
	for(i = 0; i < numThreads; ++i)
		cmdQueue->threadLocks[i].localScanStart = 0;
	cmdQueue->numThreads = numThreads;
	cmdQueue->processCounter = processCounter;
}


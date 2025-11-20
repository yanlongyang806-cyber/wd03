#include "wininclude.h"
#include "WorkerThread.h"
#include "earray.h"
#include "ThreadManager.h"
#include "WTCmdPacket.h"
#include "ScratchStack.h"
#include "EventTimingLog.h"
#include "timing.h"
#include "timing_profiler_interface.h"

#if _PS3
#elif _XBOX
    #include <ppcintrinsics.h>
    #define memcpy memcpy_fast
#else
	// PC does not need a memory barrier because it doesn't allow out-of-order writes.
	#ifdef MemoryBarrier
		#undef MemoryBarrier
	#endif
	#define MemoryBarrier()
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

#define SAFETY_CHECKS_ON 0

#define OVERRUN_CHECKS_ON 1

#if PLATFORM_CONSOLE
	#define USE_INTERLOCKED_WAIT_CHECK 0
#else
	#define USE_INTERLOCKED_WAIT_CHECK 1
#endif

#define WT_OVERRUN_VALUE 0x4BADBEEF

// TODO move to WTCmdPacket header file
void wtCmdPacketClose(SA_PARAM_NN_VALID WTCmdPacket *command_header)
{
	int packet_length = wtCmdCurrentLength(command_header);
	wtTruncateQueuedCmd(command_header->queued_cmd, packet_length);
	wtSendCmd(command_header->worker_thread); 
}

// TODO move to WTCmdPacket header file
void wtCmdPacketCloseToMark(SA_PARAM_NN_VALID WTCmdPacket *command_header)
{
	int packet_length = wtCmdCurrentLength(command_header);
	packet_length -= wtCmdMarkSize(command_header);
	wtTruncateQueuedCmd(command_header->queued_cmd, packet_length);
	wtSendCmd(command_header->worker_thread); 
}

// TODO move to WTCmdPacket header file
void * wtCmdWrite(WTCmdPacket * command_header, const void * data, U32 data_size)
{
	void * write_pointer = NULL;
	if (wtCmdCurrentLength(command_header) + data_size > command_header->length)
	{
		U8 * tail_data;
		U8 * new_packet;
		int tail_data_size = wtCmdMarkSize(command_header);

		// 1. truncate the actual queue cmd packet at the marked write point
		// 2. open a new packet, length = ( tail - mark ) + data_size
		// 3. copy from the mark to the current tail into the new packet
		// 4. reinitialize the command header for the newly started packet
		// 5. patch the size in the actual QueuedCmd struct
		// 6. append new data 
		tail_data = ScratchAlloc(tail_data_size);
		memcpy(tail_data, command_header->write_mark, tail_data_size);
		wtCmdPacketCloseToMark(command_header); 
		command_header->queued_cmd = wtAllocCmdPkt(command_header->worker_thread, 
			command_header->packet_type, data_size + tail_data_size);
		new_packet = wtGetQueuedCmdData(command_header->queued_cmd);
		memcpy(new_packet, tail_data, tail_data_size);
		command_header->head = new_packet;
		command_header->length = tail_data_size + data_size;
		command_header->write_pointer = new_packet + tail_data_size;
		command_header->write_mark = new_packet;
		ScratchFree(tail_data);
	}

	write_pointer = command_header->write_pointer;
	if (data)
		memcpy(write_pointer, data, data_size);
	command_header->write_pointer += data_size;

	return write_pointer;
}

void * wtCmdRead(WTCmdPacket *command_header, 
	void *dest, U32 data_size)
{
	void * read_pointer = NULL;
	assert(wtCmdCurrentLength(command_header) + data_size <= command_header->length);

	read_pointer = command_header->write_pointer;
	if (dest)
		memcpy(dest, read_pointer, data_size);
	command_header->write_pointer += data_size;

	return read_pointer;
}

static void wtDispatchMessages(WorkerThread *wt);

typedef struct CmdQueue
{
	QueuedCmd		*queue;
	U32				max_queued;
	volatile U32	start, end;
	U32				next_end;
} CmdQueue;

typedef enum
{
	THREAD_NOTSTARTED=0,
	THREAD_RUNNING,
	THREAD_TOLDTOEXIT,
	THREAD_EXITED,
} ThreadState;

#if USE_INTERLOCKED_WAIT_CHECK
	enum {
		WAIT_LOCK_WORKER_THREAD_IS_WAITING = BIT(0),
		WAIT_LOCK_SENDING_THREAD_ADDED_CMD = BIT(1),
	};
#endif

typedef struct WorkerThread
{
	U32 run_threaded : 1;
	U32 flush_requested : 1;
	U32 debug : 1;
	U32 skip_if_full : 1;
	U32 no_auto_timer : 1; // disables the auto timer frame start and stop on threaded WorkerThreads
	U32 disable_flush_every_frame : 1; // disables reqFlush every wtMonitor call

	CmdQueue cmd_queue;
	CmdQueue msg_queue;

	#if USE_INTERLOCKED_WAIT_CHECK
		U32				wait_lock;
	#else
		volatile int	thread_asleep; // thread sleeps when it has no data queued
	#endif
	
	HANDLE			data_queued; // event signalling that data is queued, wakes up sleeping thread
	HANDLE			data_processed; // event signalling that data is finished processing, wakes up sleeping main thread

	// thread info
	ManagedThread *thread_ptr;
	volatile ThreadState thread_state;

	// for wtQueueAllocCmd/wtQueueSendCmd
	int curr_active_cmdtype;

	// dispatch functions and user data
	void *user_data;
	WTDispatchCallback *cmd_dispatch_table;
	WTDispatchCallback *msg_dispatch_table;
	WTDefaultDispatchCallback cmd_default_dispatch;
	WTDefaultDispatchCallback msg_default_dispatch;
	WTPostCmdCallback post_cmd_callback;

	// for queueing commands (which can be queued from multiple source threads)
	CRITICAL_SECTION cmd_criticalsection;
	// for queueing messages (which can be queued from in and out of the thread)
	CRITICAL_SECTION msg_criticalsection;

	int processor_idx;
	int relative_priority;

	EventOwner *event_owner;
	const char *name;
} WorkerThread;


bool wtInWorkerThread(WorkerThread *wt)
{
	if (wt && wt->thread_ptr)
		return tmGetThreadId(wt->thread_ptr) == GetCurrentThreadId();
	return false;
}

void wtAssertWorkerThread(WorkerThread *wt)
{
	if (wt && wt->thread_ptr)
		assert(tmGetThreadId(wt->thread_ptr) == GetCurrentThreadId());
}

void wtAssertNotWorkerThread(WorkerThread *wt)
{
	if (wt && wt->thread_ptr)
		assert(tmGetThreadId(wt->thread_ptr) != GetCurrentThreadId());
}

void wtSetFlushRequested(WorkerThread *wt, int flush_requested)
{
	wt->flush_requested = !!flush_requested;
}

void wtSetEventOwner(WorkerThread *wt, EventOwner *event_owner)
{
	wt->event_owner = event_owner;
}


void wtSetSkipIfFull(WorkerThread *wt, int skip_if_full)
{
	wt->skip_if_full = skip_if_full;
}

__forceinline static void wtReqFlush(WorkerThread *wt, bool sleep)
{
	wt->flush_requested = 1;
	if (wt->data_processed)
		ResetEvent(wt->data_processed);
	if (wt->data_queued)
		SetEvent(wt->data_queued);
	if (sleep)
	{
		if (wt->data_processed)
		{
            WaitForEvent(wt->data_processed, 1);
		}
		else
			Sleep(1);
	}
	wt->flush_requested = 0;
}

__forceinline static bool isCmdQueueEmpty(CmdQueue *cmd_queue)
{
	return cmd_queue->start == cmd_queue->end;
}

__forceinline static void getStoredCmd(CmdQueue *cmd_queue)
{
	QueuedCmd *entry;
	if (isCmdQueueEmpty(cmd_queue))
		return;
	entry = &cmd_queue->queue[cmd_queue->start];
	assert(entry->num_blocks); // Must be *before* the following line, lest the other thread use it right away
	cmd_queue->start = (cmd_queue->start + entry->num_blocks) & (cmd_queue->max_queued-1);
}

__forceinline static QueuedCmd *peekStoredCmd(CmdQueue *cmd_queue)
{
	if (isCmdQueueEmpty(cmd_queue))
		return 0;
	assert(cmd_queue->queue[cmd_queue->start].num_blocks);
	return &cmd_queue->queue[cmd_queue->start];
}

// TODO reenable the inlining! __forceinline 
static void handleQueuedCmd(WorkerThread* wt, QueuedCmd *cmd, WTDispatchCallback *dispatch_table, WTDefaultDispatchCallback default_dispatch, void *user_data,
	WTCmdPacket * packet)
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

	wtCmdSetupPacket(packet, cmd->type, data, 
#if OVERRUN_CHECKS_ON
		cmd->size - sizeof(U32)
#else
		cmd->size
#endif
		);
	if (cmd->type >= WT_CMD_USER_START)
	{
		if (cmd->type < eaSize((void ***)&dispatch_table) && dispatch_table[cmd->type])
			dispatch_table[cmd->type](user_data, data, packet);
		else if (default_dispatch)
			default_dispatch(user_data, cmd->type, data, packet);
		else
		{
			static QueuedCmd invalidCommand;
			invalidCommand = *cmd;
			assertmsg(0, "Unhandled command passed to worker thread!");
		}
	}
	else if (cmd->type == WT_CMD_DEBUGCALLBACK)
	{
		(*((WTDebugCallback*)data))((char*)data + sizeof(WTDebugCallback), packet);
	}

	if (cmd->data)
		free(cmd->data);

	if (wt->post_cmd_callback)
	{
		wt->post_cmd_callback(user_data);
	}
}

__forceinline static QueuedCmd *allocStoredCmd(WorkerThread *wt, CmdQueue *cmd_queue, int cmd_type, U32 size)
{
	int			free_blocks,num_blocks,filler_blocks,contiguous_blocks,malloc_size;
	QueuedCmd	*cmd;

	if (size + sizeof(*cmd) > sizeof(*cmd) * cmd_queue->max_queued/2)
	{
		malloc_size = size;
		size = 0;
		num_blocks = 1;
	}
	else
	{
		malloc_size = 0;
		num_blocks = 1 + (size + sizeof(QueuedCmd) - 1) / sizeof(QueuedCmd);
	}

	contiguous_blocks = cmd_queue->max_queued - cmd_queue->end;
	if (num_blocks > contiguous_blocks)
		filler_blocks = contiguous_blocks;
	else
		filler_blocks = 0;
	for(;;)
	{
		free_blocks = (cmd_queue->start - (cmd_queue->end+1)) & (cmd_queue->max_queued-1);
		if (num_blocks+filler_blocks <= free_blocks)
			break;
		if (!wt) {
			// Cannot flush and wait, switch to malloc
			if(!malloc_size && size)
			{
				malloc_size = size;
				size = 0;
				num_blocks = 1;
			}

			// Recalc padding/filler
			contiguous_blocks = cmd_queue->max_queued - cmd_queue->end;
			if (num_blocks > contiguous_blocks)
				filler_blocks = contiguous_blocks;
			else
				filler_blocks = 0;
			continue;
		}
		
		if (wt->skip_if_full)
			return 0;
		
		// wait for worker thread to free some blocks
		wtReqFlush(wt, true);
		wtDispatchMessages(wt);
	}
	cmd_queue->next_end = (cmd_queue->end + num_blocks + filler_blocks) & (cmd_queue->max_queued-1);

	cmd = &cmd_queue->queue[cmd_queue->end];
	if (filler_blocks)
	{
		memset(cmd,0,sizeof(*cmd));
		cmd->type = WT_CMD_NOP;
		cmd->num_blocks = filler_blocks;
		cmd = &cmd_queue->queue[(cmd_queue->end + filler_blocks) & (cmd_queue->max_queued-1)];
		assert(cmd == &cmd_queue->queue[0]); // Otherwise OVERRUN_CHECKING won't work and my logic is wrong
	}
	cmd->type		= cmd_type;
	cmd->num_blocks = num_blocks;
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
	return cmd;
}

static void wtDispatchMessages(WorkerThread *wt)
{
	QueuedCmd *cmd;
	WTCmdPacket packet = { 0 };

#if SAFETY_CHECKS_ON
	wtAssertNotWorkerThread(wt);
#endif

	packet.worker_thread = wt;
	while (cmd = peekStoredCmd(&wt->msg_queue))
	{
		handleQueuedCmd(wt, cmd, wt->msg_dispatch_table, wt->msg_default_dispatch, wt->user_data, &packet);
		getStoredCmd(&wt->msg_queue);
	}
}

void wtFlushMessages(WorkerThread *wt)
{
	while (wt->run_threaded && wt->thread_ptr)
	{
		if (isCmdQueueEmpty(&wt->msg_queue))
			break;
		Sleep(1);
	}
}

void wtFlushEx(WorkerThread *wt, bool dispatch_messages)
{
#if SAFETY_CHECKS_ON
	wtAssertNotWorkerThread(wt);
#endif
	while (wt->run_threaded && wt->thread_ptr)
	{
		if (isCmdQueueEmpty(&wt->cmd_queue))
			break;
		wtReqFlush(wt, true);
		if (dispatch_messages)
			wtDispatchMessages(wt);
	}
}

void wtMonitor(WorkerThread *wt)
{
#if SAFETY_CHECKS_ON
	wtAssertNotWorkerThread(wt);
#endif
	if(!wt->disable_flush_every_frame || !isCmdQueueEmpty(&wt->cmd_queue))
		wtReqFlush(wt, false);
	wtDispatchMessages(wt);
}

void wtMonitorAndSleep(WorkerThread *wt)
{
#if SAFETY_CHECKS_ON
	wtAssertNotWorkerThread(wt);
#endif
	wtReqFlush(wt, true);
	wtDispatchMessages(wt);
}


__forceinline static void initCmdQueue(CmdQueue *cmd_queue, int max_cmds MEM_DBG_PARMS)
{
	cmd_queue->queue = scalloc(max_cmds, sizeof(QueuedCmd));
	cmd_queue->max_queued = max_cmds;
}

WorkerThread *wtCreate_dbg(int max_cmds, int max_msgs, void *user_data, const char *name, bool disable_flush_every_frame MEM_DBG_PARMS)
{
	WorkerThread *wt = scalloc(1, sizeof(*wt));
	assert(!(max_cmds & (max_cmds - 1)) && max_cmds);
	assert(!(max_msgs & (max_msgs - 1)) && max_msgs);
	wt->user_data = user_data;
	wt->processor_idx = -1;
	wt->name = name;
	wt->disable_flush_every_frame = !!disable_flush_every_frame;
	initCmdQueue(&wt->cmd_queue, max_cmds MEM_DBG_PARMS_CALL);
	initCmdQueue(&wt->msg_queue, max_msgs MEM_DBG_PARMS_CALL);
	InitializeCriticalSection(&wt->cmd_criticalsection);
	InitializeCriticalSection(&wt->msg_criticalsection);
	return wt;
}

void wtSetProcessor(WorkerThread *wt, int processor_idx)
{
	wt->processor_idx = processor_idx;
	if (wt->thread_ptr && processor_idx >= 0)
		tmSetThreadProcessorIdx(wt->thread_ptr, processor_idx);
}

void wtDestroy(WorkerThread *wt)
{
	wtFlush(wt);
	if (wt->thread_ptr && wt->thread_state != THREAD_EXITED)
	{
		wt->thread_state = THREAD_TOLDTOEXIT;
		while (wt->thread_state != THREAD_EXITED)
			wtReqFlush(wt, true);
#if _PS3
		DestroyEvent(wt->data_queued);
		DestroyEvent(wt->data_processed);
#else
		CloseHandle(wt->data_queued);
		CloseHandle(wt->data_processed);
#endif
		tmDestroyThread(wt->thread_ptr, false);
	}
	DeleteCriticalSection(&wt->cmd_criticalsection);
	DeleteCriticalSection(&wt->msg_criticalsection);
	free(wt->cmd_queue.queue);
	free(wt->msg_queue.queue);
	ZeroStruct(wt);
	free(wt);
}

void wtRegisterCmdDispatch(WorkerThread *wt, int cmd_type, WTDispatchCallback dispatch_callback)
{
	assert(cmd_type >= WT_CMD_USER_START);
	if (cmd_type >= eaSize((void ***)&wt->cmd_dispatch_table))
		eaSetSize((void ***)&wt->cmd_dispatch_table, cmd_type+1);
	wt->cmd_dispatch_table[cmd_type] = dispatch_callback;
}

void wtRegisterMsgDispatch(WorkerThread *wt, int msg_type, WTDispatchCallback dispatch_callback)
{
	assert(msg_type >= WT_CMD_USER_START);
	if (msg_type >= eaSize((void ***)&wt->msg_dispatch_table))
		eaSetSize((void ***)&wt->msg_dispatch_table, msg_type+1);
	wt->msg_dispatch_table[msg_type] = dispatch_callback;
}

void wtSetDefaultCmdDispatch(WorkerThread *wt, WTDefaultDispatchCallback dispatch_callback)
{
	wt->cmd_default_dispatch = dispatch_callback;
}

void wtSetDefaultMsgDispatch(WorkerThread *wt, WTDefaultDispatchCallback dispatch_callback)
{
	wt->msg_default_dispatch = dispatch_callback;
}

void wtSetThreaded(WorkerThread *wt, bool run_threaded, int relative_priority, bool no_auto_timer)
{
	if (wt->thread_ptr) {
		assertmsg(0, "Trying to change threadedness of a WorkerThread after creation, probably won't work"); // Doesn't work for RenderThread anyway, because of thread window ownership
	}

	wt->run_threaded = !!run_threaded;
	wt->no_auto_timer = !!no_auto_timer;
	wt->relative_priority = relative_priority;
}

void wtChangeThreaded(WorkerThread *wt, int run_threaded)
{
	// Doesn't work for RenderThread anyway, because of thread window ownership
	assertmsg(0, "Trying to change threadedness of a WorkerThread after creation, probably won't work"); 
	if (wt->thread_ptr && !run_threaded) {
		// Already running, attempt to switch to non-threaded mode.
		wtFlush(wt);
		wt->thread_state = THREAD_TOLDTOEXIT;
		while (wt->thread_state != THREAD_EXITED)
			wtReqFlush(wt, true);
		tmDestroyThread(wt->thread_ptr, false);
		wt->thread_ptr = NULL;
	}

	wt->run_threaded = !!run_threaded;
		
	if (!wt->thread_ptr && run_threaded) {
		wtStartEx(wt, 0);
	}
}

int wtIsThreaded(WorkerThread *wt)
{
	return wt->run_threaded;
}

const char *wtGetName(WorkerThread *wt)
{
	return wt->name;
}

HANDLE wtGetThreadHandle(WorkerThread *wt)
{
	if (!wt->run_threaded || !wt->thread_ptr)
		return NULL;
	return tmGetThreadHandle(wt->thread_ptr);
}

U32 wtGetThreadID(WorkerThread *wt)
{
	if (!wt->run_threaded || !wt->thread_ptr)
		return 0;
	return tmGetThreadId(wt->thread_ptr);
}

void wtSetDebug(WorkerThread *wt, bool debug)
{
	wt->debug = !!debug;
}

bool wtIsDebug(WorkerThread *wt)
{
	return wt->debug;
}

void wtSetPostCmdCallback(WorkerThread *wt, WTPostCmdCallback callback)
{
	wt->post_cmd_callback = callback;
}

static DWORD WINAPI wtThread( LPVOID lpParam )
{
	EXCEPTION_HANDLER_BEGIN

	WorkerThread *wt;
	QueuedCmd *cmd,last;
	WTCmdPacket packet = { 0 };


	wt = (WorkerThread *)lpParam;
	while (!wt->thread_ptr) // Let assignment finish in main thread
		SleepEx(1, TRUE);
	assert(wt->thread_ptr);
	wt->thread_state = THREAD_RUNNING;
	wt->data_queued = CreateEvent(0,0,0,0);
	wt->data_processed = CreateEvent(0,0,0,0);
	if (wt->processor_idx >= 0)
		tmSetThreadProcessorIdx(wt->thread_ptr, wt->processor_idx);

	packet.worker_thread = wt;

	for(;;)
	{
		if (!wt->no_auto_timer)
			autoTimerThreadFrameBegin(wt->name);
		
		if (wt->thread_state == THREAD_TOLDTOEXIT)
		{
			wt->thread_state = THREAD_EXITED;
			ExitThread(0);
		}

		if (wt->debug && !wt->flush_requested && wt->run_threaded)
			cmd = 0;
		else
			cmd = peekStoredCmd(&wt->cmd_queue);
		if (!cmd)
		{
			if (wt->data_queued)
			{
				#if USE_INTERLOCKED_WAIT_CHECK
					cmd = peekStoredCmd(&wt->cmd_queue);
					if(!cmd)
					{
						if(!_InterlockedOr(&wt->wait_lock, WAIT_LOCK_WORKER_THREAD_IS_WAITING))
						{
							// Sending thread hasn't queued anything, so wait for it.
							if (wt->flush_requested && wt->data_processed)
								SetEvent(wt->data_processed);
							if (wt->event_owner)
								etlAddEvent(wt->event_owner, "Wait for data", ELT_CODE, ELTT_BEGIN);
							WaitForSingleObjectEx(wt->data_queued, INFINITE, TRUE, "");
							if (wt->event_owner)
								etlAddEvent(wt->event_owner, "Wait for data", ELT_CODE, ELTT_END);
						}
						InterlockedExchange(&wt->wait_lock, 0);
					}
				#else
					ResetEvent(wt->data_queued);
					cmd = peekStoredCmd(&wt->cmd_queue);
					if (!cmd) {
						wt->thread_asleep = 1;
						if (wt->flush_requested && wt->data_processed)
							SetEvent(wt->data_processed);
						WaitForSingleObjectEx(wt->data_queued, INFINITE, TRUE, "");
						wt->thread_asleep = 0;
					}
				#endif
			}
			continue;
		}

		handleQueuedCmd(wt, cmd, wt->cmd_dispatch_table, wt->cmd_default_dispatch, wt->user_data, &packet);
		
		last = *cmd;
		getStoredCmd(&wt->cmd_queue);
		
		if (!wt->no_auto_timer)
			autoTimerThreadFrameEnd();
	}
	return 0; 
	EXCEPTION_HANDLER_END
} 

void wtStartEx(WorkerThread *wt, int stack_size)
{
	if (!wt->run_threaded)
		return;

	wt->thread_ptr = tmCreateThreadDebug(wtThread, wt, stack_size, 0, wt->name?wt->name:"wtThread", __FILE__, __LINE__);
	assert(wt->thread_ptr);

	tmSetThreadRelativePriority(wt->thread_ptr, wt->relative_priority);

	//ret = SetThreadIdealProcessor(render_thread_handle,1);
	//ret = SetThreadIdealProcessor(GetCurrentThread(),0);
	while (wt->thread_state != THREAD_RUNNING)
		Sleep(1);
}

void wtQueueDebugCmd(WorkerThread *wt,WTDebugCallback callback_func,void *data,int size)
{
	WTDebugCallback *cb_data = _alloca(size+sizeof(WTDebugCallback));
	*cb_data = callback_func;
	memcpy(cb_data+1,data,size);
	wtQueueCmd(wt,WT_CMD_DEBUGCALLBACK,cb_data,size+sizeof(WTDebugCallback));
}

QueuedCmd* wtAllocCmdPkt(WorkerThread *wt, int cmd_type, int size)
{
	QueuedCmd* packet_header;
	void* ret;
	assert(!wt->curr_active_cmdtype);
	wt->curr_active_cmdtype = cmd_type;
#if SAFETY_CHECKS_ON
	wtAssertNotWorkerThread(wt);
#endif
#if OVERRUN_CHECKS_ON
	size+=4;
#endif
	packet_header = allocStoredCmd(wt, &wt->cmd_queue, cmd_type, size);
	if (!packet_header)
	{
		wt->curr_active_cmdtype = 0;
		return 0;
	}
	ret = wtGetQueuedCmdData(packet_header);
#if OVERRUN_CHECKS_ON
	*(U32*)(((U8*)ret) + size - 4) = WT_OVERRUN_VALUE;
#endif
	return packet_header;
}

void *wtAllocCmd(WorkerThread *wt, int cmd_type, int size)
{
	QueuedCmd * packet_header = wtAllocCmdPkt(wt, cmd_type, size);

	if (!packet_header)
		return 0;
	return wtGetQueuedCmdData(packet_header);
}

void wtSendCmd(WorkerThread *wt)
{
	WTCmdPacket packet = { 0 };
#if OVERRUN_CHECKS_ON
	U8 *data;
	QueuedCmd *cmd = &wt->cmd_queue.queue[wt->cmd_queue.end];
	if (cmd->type == WT_CMD_NOP)
		cmd = &wt->cmd_queue.queue[0];
 	data = wtGetQueuedCmdData(cmd);
	assertmsg(*(U32*)(data + cmd->size - 4) == WT_OVERRUN_VALUE, "This command overran the CmdQueue");
#endif
	packet.worker_thread = wt;
	wt->curr_active_cmdtype = 0;
	if (!wt->run_threaded)
	{
		handleQueuedCmd(wt, &wt->cmd_queue.queue[0], wt->cmd_dispatch_table, wt->cmd_default_dispatch, 
			wt->user_data, &packet);
		return;
	}

	// We must have the command data written before the command queue end changes
	MemoryBarrier();

	wt->cmd_queue.end = wt->cmd_queue.next_end;
#if SAFETY_CHECKS_ON
	wtAssertNotWorkerThread(wt);
#endif
	#if USE_INTERLOCKED_WAIT_CHECK
		if(	wt->data_queued
			&&
			_InterlockedOr(&wt->wait_lock, WAIT_LOCK_SENDING_THREAD_ADDED_CMD) ==
			WAIT_LOCK_WORKER_THREAD_IS_WAITING)
		{
			// BG thread set it, and FG never set it, so BG will be waiting for data_queued.
			SetEvent(wt->data_queued);
		}
	#else
		if (wt->thread_asleep && wt->data_queued)
			SetEvent(wt->data_queued);
	#endif
}

void wtCancelCmd(WorkerThread *wt)
{
	QueuedCmd	*cmd = &wt->cmd_queue.queue[wt->cmd_queue.end];

	wt->curr_active_cmdtype = 0;
	if (cmd->data)
	{
		free(cmd->data);
		cmd->data = 0;
	}
}

void wtEnterCmdCritical(WorkerThread *wt)
{
#if SAFETY_CHECKS_ON
	wtAssertNotWorkerThread(wt);
#endif
	EnterCriticalSection(&wt->cmd_criticalsection);
}

void wtLeaveCmdCritical(WorkerThread *wt)
{
#if SAFETY_CHECKS_ON
	wtAssertNotWorkerThread(wt);
#endif
	LeaveCriticalSection(&wt->cmd_criticalsection);
}

int wtQueueCmd(WorkerThread *wt,int cmd_type,const void *cmd_data,int size)
{
	void	*mem;
#if SAFETY_CHECKS_ON
	wtAssertNotWorkerThread(wt);
#endif
	mem = wtAllocCmd(wt,cmd_type,size);
	if (!mem)
		return 0;
	if (size)
		memcpy(mem,cmd_data,size);
	wtSendCmd(wt);
	return 1;
}

void wtQueueMsg(WorkerThread *wt,int msg_type,void *msg_data,int size)
{
	void *mem;
	QueuedCmd * cmd;
	int allocsize = size;

	if (!wt)
		return;

	EnterCriticalSection(&wt->msg_criticalsection);
#if OVERRUN_CHECKS_ON
	allocsize+=4;
#endif
	cmd = allocStoredCmd(0, &wt->msg_queue, msg_type, allocsize);
	if (!cmd)
		goto abort;
	mem = wtGetQueuedCmdData(cmd);
#if OVERRUN_CHECKS_ON
	*(U32*)(((U8*)mem) + allocsize - 4) = WT_OVERRUN_VALUE;
#endif
	memcpy(mem,msg_data,size);

	// We must have the command data written before the command queue end changes
	MemoryBarrier();

	wt->msg_queue.end = wt->msg_queue.next_end;
abort:
	LeaveCriticalSection(&wt->msg_criticalsection);
}

// Provide read-only access to the worker thread.
const ManagedThread * wtGetWorkerThreadPtr(const WorkerThread *wt)
{
	return wt->thread_ptr;
}




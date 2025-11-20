#ifndef _WORKERTHREAD_H_
#define _WORKERTHREAD_H_
#pragma once
GCC_SYSTEM


C_DECLARATIONS_BEGIN

/*************************************************************************
/  Datatype to manage creating a data processing thread (ie for rendering).
/  The spawned worker thread sleeps when no commands are queued.
/  Commands go to the worker and messages come back.
*************************************************************************/


typedef struct WorkerThread WorkerThread;
typedef struct ManagedThread ManagedThread;
typedef struct WTCmdPacket WTCmdPacket;
typedef struct EventOwner EventOwner;

typedef void (*WTDebugCallback)(void *data, WTCmdPacket *packet);
typedef void (*WTDispatchCallback)(void *user_data, void *data, WTCmdPacket *packet);
typedef void (*WTDefaultDispatchCallback)(void *user_data, int cmd_type, void *data, WTCmdPacket *packet);
typedef void (*WTPostCmdCallback)(void *user_data);

enum
{
	// built in commands
	WT_CMD_NOP=1,
	WT_CMD_DEBUGCALLBACK,

	// user commands start here
	WT_CMD_USER_START
};


// creation/destruction
WorkerThread *wtCreate_dbg(int max_cmds, int max_msgs, void *user_data, const char *name, bool disable_flush_every_frame MEM_DBG_PARMS);
#define wtCreate(max_cmds, max_msgs, user_data, name) wtCreateEx(max_cmds, max_msgs, user_data, name, false)
#define wtCreateEx(max_cmds, max_msgs, user_data, name, disable_flush_every_frame) wtCreate_dbg(max_cmds, max_msgs, user_data, name, disable_flush_every_frame MEM_DBG_PARMS_INIT)
void wtDestroy(WorkerThread *wt);

// dispatch registration
void wtRegisterCmdDispatch(WorkerThread *wt, int cmd_type, WTDispatchCallback dispatch_callback);
void wtRegisterMsgDispatch(WorkerThread *wt, int msg_type, WTDispatchCallback dispatch_callback);
void wtSetDefaultCmdDispatch(WorkerThread *wt, WTDefaultDispatchCallback dispatch_callback);
void wtSetDefaultMsgDispatch(WorkerThread *wt, WTDefaultDispatchCallback dispatch_callback);

// set threading and start worker thread
void wtStartEx(WorkerThread *wt, int stack_size);
#define wtStart(wt) wtStartEx(wt, 0)
void wtSetThreaded(WorkerThread *wt, bool run_threaded, int relative_priority, bool no_auto_timer);
void wtSetProcessor(WorkerThread *wt, int processor_idx);
int wtIsThreaded(WorkerThread *wt);
const char *wtGetName(WorkerThread *wt);
typedef void *HANDLE;
HANDLE wtGetThreadHandle(WorkerThread *wt);
U32 wtGetThreadID(WorkerThread *wt);
void wtSetDebug(WorkerThread *wt, bool debug);
bool wtIsDebug(WorkerThread *wt);
void wtSetPostCmdCallback(WorkerThread *wt, WTPostCmdCallback callback);

// flush and monitor functions for main thread
void wtFlushEx(WorkerThread *wt, bool dispatch_messages);			// wait for worker thread to finish its commands, dispatch messages from worker thread
#define wtFlush(wt) wtFlushEx(wt, true)
void wtMonitor(WorkerThread *wt);		// dispatch messages from worker thread
void wtMonitorAndSleep(WorkerThread *wt);	// Also sleeps, and signals the background thread if wtSetDebug is set

// flush for worker thread
void wtFlushMessages(WorkerThread *wt);	// wait for main thread to dispatch messages

// insert a debugging callback into the worker thread queue
void wtQueueDebugCmd(WorkerThread *wt,WTDebugCallback callback_func,void *data,int size);

// tell worker thread to abort (rather than stall) if queue is full
void wtSetSkipIfFull(WorkerThread *wt, int skip_if_full);

// lock/unlock worker thread - use if multiple threads need to queue commands
void wtEnterCmdCritical(WorkerThread *wt);
void wtLeaveCmdCritical(WorkerThread *wt);

// send commands to worker thread
int wtQueueCmd(WorkerThread *wt,int cmd_type,const void *cmd_data,int size);
SA_RET_NN_BYTES_VAR(size) void *wtAllocCmd(WorkerThread *wt,int cmd_type,int size);

void wtCancelCmd(WorkerThread *wt);
void wtSendCmd(WorkerThread *wt);

// send message from worker thread to main thread
void wtQueueMsg(WorkerThread *wt,int msg_type,void *msg_data,int size);

// check if the current thread is the worker
bool wtInWorkerThread(WorkerThread *wt);
void wtAssertWorkerThread(WorkerThread *wt);
void wtAssertNotWorkerThread(WorkerThread *wt);

// debugging function
void wtSetFlushRequested(WorkerThread *wt, int flush_requested);

void wtSetEventOwner(WorkerThread *wt, EventOwner *event_owner);

const ManagedThread * wtGetWorkerThreadPtr(const WorkerThread *wt);

C_DECLARATIONS_END

#endif




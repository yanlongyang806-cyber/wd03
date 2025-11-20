#pragma once
GCC_SYSTEM

#include "GlobalTypeEnum.h"

typedef struct GenericWorkerThreadManager GenericWorkerThreadManager;
typedef struct GenericWorkerThreadInternal GenericWorkerThreadInternal;
typedef struct GWTCmdPacket GWTCmdPacket;
typedef struct EventOwner EventOwner;
typedef struct ObjectLock ObjectLock;

typedef enum
{
	GWT_LOCKSTYLE_STANDARD = 0,
	GWT_LOCKSTYLE_OBJECTLOCK,
} LockStyle;

typedef void (*GWTDebugCallback)(void *data, GWTCmdPacket *packet);
typedef void (*GWTDispatchCallback)(void *user_data, void *data, GWTCmdPacket *packet);
typedef void (*GWTDefaultDispatchCallback)(void *user_data, int cmd_type, void *data, GWTCmdPacket *packet);
typedef void (*GWTPostCmdCallback)(void *user_data);

// creation/destruction
GenericWorkerThreadManager *gwtCreate_dbg(int maxCmds, int maxMsgs, int numThreads, void *userData, const char *name, LockStyle cmdLockstyle MEM_DBG_PARMS);
#define gwtCreate(maxCmds, maxMsgs, numThreads, userData, name) gwtCreateEx(maxCmds, maxMsgs, numThreads, userData, name, GWT_LOCKSTYLE_STANDARD)
#define gwtCreateEx(maxCmds, maxMsgs, numThreads, userData, name, lockStyle) gwtCreate_dbg(maxCmds, maxMsgs, numThreads, userData, name, lockStyle MEM_DBG_PARMS_INIT)
void gwtDestroyEx(GenericWorkerThreadManager **wt);
#define gwtDestroy(wt) gwtDestroyEx(&wt);

bool gwtInWorkerThread(GenericWorkerThreadManager *wt);

// dispatch registration
void gwtRegisterCmdDispatch(GenericWorkerThreadManager *wt, int cmd_type, GWTDispatchCallback dispatch_callback);
void gwtRegisterMsgDispatch(GenericWorkerThreadManager *wt, int msg_type, GWTDispatchCallback dispatch_callback);
void gwtSetDefaultCmdDispatch(GenericWorkerThreadManager *wt, GWTDefaultDispatchCallback dispatch_callback);
void gwtSetDefaultMsgDispatch(GenericWorkerThreadManager *wt, GWTDefaultDispatchCallback dispatch_callback);

// flush and monitor functions for main thread
void gwtFlushEx(GenericWorkerThreadManager *wt, bool dispatch_messages);			// wait for worker thread to finish its commands, dispatch messages from worker thread
#define gwtFlush(wt) gwtFlushEx(wt, true)
void gwtFlushMessages(GenericWorkerThreadManager *wt);
void gwtMonitor(GenericWorkerThreadManager *wt);		// dispatch messages from worker thread
void gwtMonitorAndSleep(GenericWorkerThreadManager *wt);	// Also sleeps, and signals the background thread if wtSetDebug is set

void gwtStartEx(GenericWorkerThreadManager *wt, int stack_size MEM_DBG_PARMS);
#define gwtStart(wt) gwtStartEx(wt, 0 MEM_DBG_PARMS_INIT)

// Various Queue commands. 
// When queuing Structs, you pass in struct itself
// typedef struct HoldsStuff HoldsStuff
// HoldsStuff stuff;
// HoldsStuff *pointerToStuff;
// gwtQueueCmdStruct(wt, CMD_TYPE, stuff, HoldsStuff);
// gwtQueueCmdStruct(wt, CMD_TYPE, *pointerToStuff, HoldsStuff);
// This makes a copy of the contents of the struct in the queue. 
// The callback function receives a pointer to a struct in the queue, so do not try to free it.

// When queuing Pointers, you pass in the pointer
// gwtQueueCmdPointer(wt, CMD_TYPE, pointerToStuff)
// This stores the pointer in the queue.
// The callback function receives a pointer to the location of the pointer in the queue:
// HoldsStuff **pointerInQueue

int gwtQueueCmd(GenericWorkerThreadManager *wt,int cmdType,const void *cmdData,int size);
#define gwtQueueCmdStruct(wt, cmdType, cmdData, dataType) gwtQueueCmd(wt, cmdType, &(cmdData), sizeof(dataType))
#define gwtQueueCmdPointer(wt, cmdType, cmdData) gwtQueueCmd(wt, cmdType, &(cmdData), sizeof(int*))

void gwtQueueMsg(GWTCmdPacket *packet,int msgType,void *msgData,int size);
#define gwtQueueMsgStruct(packet, msgType, msgData, dataType) gwtQueueMsg(packet, msgType, &msgData, sizeof(dataType))
#define gwtQueueMsgPointer(packet, msgType, msgData) gwtQueueMsg(packet, msgType, &msgData, sizeof(int*))

int gwtQueueCmd_ObjectLock(GenericWorkerThreadManager *wt,int cmdType,const void *cmdData,int size, int lockCount, ...);
#define gwtQueueCmdStruct_ObjectLock(wt, cmdType, cmdData, dataType, lockCount, ...) gwtQueueCmd_ObjectLock(wt, cmdType, &(cmdData), sizeof(dataType), lockCount, __VA_ARGS__)
#define gwtQueueCmdPointer_ObjectLock(wt, cmdType, cmdData, lockCount, ...) gwtQueueCmd_ObjectLock(wt, cmdType, &(cmdData), sizeof(int*), lockCount, __VA_ARGS__)

int gwtQueueCmd_ObjectLockArray(GenericWorkerThreadManager *wt,int cmdType,const void *cmdData,int size, ObjectLock **objectLocks);
#define gwtQueueCmdStruct_ObjectLockArray(wt, cmdType, cmdData, dataType, objectLocks) gwtQueueCmd_ObjectLockArray(wt, cmdType, &(cmdData), sizeof(dataType), objectLocks)
#define gwtQueueCmdPointer_ObjectLockArray(wt, cmdType, cmdData, objectLocks) gwtQueueCmd_ObjectLockArray(wt, cmdType, &(cmdData), sizeof(int*), objectLocks)

void gwtSetThreaded(GenericWorkerThreadManager *wt, bool runThreaded, int relativePriority, bool noAutoTimer);
void gwtSetSkipIfFull(GenericWorkerThreadManager *wt, int skip_if_full);

U32 gwtGetThreadIndex(GWTCmdPacket *packet);

S64 gwtGetAverageCommandLatency(GenericWorkerThreadManager *wt);
S64 gwtGetLastMinuteCount(GenericWorkerThreadManager *wt);

enum
{
	// built in commands
	GWT_CMD_NOP=1,
	GWT_CMD_DEBUGCALLBACK,

	// user commands start here
	GWT_CMD_USER_START
};

//CMD_LOCKSTYLE_OBJECTLOCK
typedef struct ObjectLock ObjectLock;
typedef struct ObjectLockInstance ObjectLockInstance;

ObjectLock *initializeObjectLock(U32 *lockCounter);
void destroyObjectLock(ObjectLock **lock);
void fullyLockObjectLock(ObjectLock *lock);
void fullyUnlockObjectLock(ObjectLock *lock);

// ObjectLocks can have identifying information stored in them. For the moment this is 8 bytes of data
// If you want those 8 bytes to hold something other than a ContainerRef, add another struct to 
// the union in the definition of ObjectLock.
void SetObjectLockContainerRef(ObjectLock *lock, GlobalType containerType, ContainerID containerID);

bool isObjectLockInstanceReady(ObjectLockInstance *instance);
void processObjectLockInstance(ObjectLockInstance *instance);
void fullyLockObjectLockInstance(ObjectLockInstance *instance);
void fullyUnlockObjectLockInstance(ObjectLockInstance *instance);

#pragma once
GCC_SYSTEM

/*************************************************************************
/  Datatype to manage creating multiple threads that can read from the same queue.
/  The spawned worker threads sleep when no commands are queued.
/  Commands go to the worker and messages come back.
*************************************************************************/

typedef void (*ThreadDataProcessFunc)(void* pUserData);

typedef struct MultiWorkerThreadManager MultiWorkerThreadManager;

MultiWorkerThreadManager* mwtCreate(int iMaxInputCmds, int iMaxOutputCmds, int iNumThreads, int* pProcessorIndices, int* pThreadPriorities, ThreadDataProcessFunc pProcessInputFunc, ThreadDataProcessFunc pProcessOutputFunc, const char* pcThreadName);
void mwtDestroy(MultiWorkerThreadManager* pManager);

bool mwtQueueInput(MultiWorkerThreadManager* pManager, void* pData, bool bWakeThreads);

bool mwtQueueOutput(MultiWorkerThreadManager* pManager, void* pData);

void mwtWakeThreads(MultiWorkerThreadManager* pManager);
void mwtSleepUntilDone(MultiWorkerThreadManager* pManager);

//Keeps the main thread free to process output
void mwtProcessOutputQueueAndSleepForInput(MultiWorkerThreadManager* pManager);

#define mwtProcessOutputQueue(pManager) mwtProcessOutputQueueEx(pManager, 0)
S64 mwtProcessOutputQueueEx(MultiWorkerThreadManager* pManager, S64 count);


bool mwtProcessQueueDirect(MultiWorkerThreadManager* pManager);

//Return true if worker threads still have something to process.
bool mwtInputQueueIsEmpty(MultiWorkerThreadManager *pManager);
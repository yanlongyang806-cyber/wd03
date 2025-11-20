#ifndef _THREADMANAGER_H_
#define _THREADMANAGER_H_
#pragma once
GCC_SYSTEM

#include "wininclude.h"

typedef struct ManagedThread ManagedThread;

typedef DWORD (WINAPI *ThreadFunc)(LPVOID lpParam);

ManagedThread *tmGetMainThread(void);
bool tmIsMainThread(void);
void tmSetGlobalThreadPriority(int global_priority);

SA_RET_OP_VALID ManagedThread *tmCreateThreadDebug(SA_PARAM_NN_VALID ThreadFunc thread_function, void *thread_param, U32 stack_size, int processor_idx, SA_PARAM_OP_STR const char *thread_name, SA_PARAM_NN_STR const char *filename, int linenumber);
#define tmCreateThreadEx(thread_function, thread_param, stack_size, processor_idx) tmCreateThreadDebug(thread_function, thread_param, stack_size, processor_idx, #thread_function, __FILE__, __LINE__)
#define tmCreateThread(thread_function, thread_param) tmCreateThreadEx(thread_function, thread_param, 0, -1)

ManagedThread *tmAddExternalThreadDebug(HANDLE thread_handle, DWORD thread_id, SA_PARAM_OP_STR const char *thread_name, U32 stack_size, SA_PARAM_NN_STR const char *filename, int linenumber);
#define tmAddExternalThread(thread_handle, thread_id) tmAddExternalThreadDebug(thread_handle, thread_id, NULL, 0, __FILE__, __LINE__)
void tmDestroyThread(SA_PARAM_NN_VALID ManagedThread *thread_ptr, bool wait);

bool tmIsStillActive(SA_PARAM_NN_VALID ManagedThread *thread_ptr);

void tmSetThreadName(SA_PARAM_NN_VALID ManagedThread *thread_ptr, SA_PARAM_NN_STR const char *thread_name);
void tmSetThreadRelativePriority(SA_PARAM_NN_VALID ManagedThread *thread_ptr, int relative_priority);
void tmSetThreadProcessorIdx(SA_PARAM_NN_VALID ManagedThread *thread_ptr, int processor_idx);
bool tmDisableSetThreadProcessorIdx(bool disable); // make all calls to tmSetThreadProcessorIdx() into no-ops
void tmSetThreadAllowSync(SA_PARAM_NN_VALID ManagedThread *thread_ptr, bool bAllowSync); // For debugging
DWORD tmQueueUserAPC(PAPCFUNC pfnAPC, SA_PARAM_NN_VALID ManagedThread *thread_ptr, ULONG_PTR dwData);

DWORD tmGetThreadId(SA_PARAM_NN_VALID const ManagedThread *thread_ptr);
HANDLE tmGetThreadHandle(SA_PARAM_NN_VALID const ManagedThread *thread_ptr);

SA_RET_OP_VALID ManagedThread *tmGetThreadFromId(DWORD thread_id);
SA_RET_OP_STR const char *tmGetThreadNameFromId(DWORD thread_id);
SA_RET_OP_STR const char *tmGetThreadName(SA_PARAM_OP_VALID const ManagedThread *thread_ptr);

#if PROFILE_PERF
void tmProfileSuspendThreads(SA_PARAM_NN_VALID ManagedThread *exclude_thread);
void tmProfileResumeThreads(SA_PARAM_NN_VALID ManagedThread *exclude_thread);
#endif

typedef void (*ForEachThreadFunc)(ManagedThread *thread_ptr, void *userData);
void tmForEachThread(SA_PARAM_NN_VALID ForEachThreadFunc callback, void *userData);

void tmUpdateAllPerformance(bool requeryUnknownThreads);

void tmSetThreadNames(void);

AUTO_STRUCT;
typedef struct ManagedThreadPerformance {
	F64 kernelTime; NO_AST// In seconds
	F64 userTime; NO_AST// In Seconds
	F64 kernelTimeDelta; NO_AST
	F64 userTimeDelta; NO_AST

	//32-bit copies of the above which can be servermonitored
	F32 kernelTime32;
	F32 userTime32;
	F32 kernelTimeDelta32;
	F32 userTimeDelta32;
} ManagedThreadPerformance;
SA_RET_NN_VALID ManagedThreadPerformance *tmGetThreadPerformance(SA_PARAM_NN_VALID ManagedThread *thread_ptr);

#endif //_THREADMANAGER_H_


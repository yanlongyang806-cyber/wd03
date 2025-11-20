
#include "iocp.h"
#include "earray.h"
#include "timing.h"
#include "timing_profiler_interface.h"

#define PRINT_DEBUG_STUFF 0

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

typedef struct IOCompletionAssociation {
	IOCompletionPort*								iocp;
	
	HANDLE											handle;
	IOCompletionMsgHandler							msgHandler;
	void*											userPointer;
	
	U32												expectedCompletionCount;
	
	struct {
		U32											destroying					: 1;
		U32											needsBeforeCheckMsg			: 1;
		U32											sendingMsgBeforeCheck		: 1;
		U32											destroyedDuringBeforeCheck	: 1;
	} flags;
} IOCompletionAssociation;

typedef struct IOCompletionResult {
	U32												success;
	U32												bytesTransferred;
	void*											userPointer;
	OVERLAPPED*										ol;
	U32												errorCode;
} IOCompletionResult;

typedef struct IOCompletionPort {
	HANDLE											hCompletionPort;
	
	IOCompletionAssociation**						iocas;
	U32												expectedCompletionCount;

	struct {
		IOCompletionDoneWaitingInThreadCallback		callback;
		void*										userPointer;

		HANDLE										handle;
		HANDLE										eventCompletionFinished;
		HANDLE										eventStartWaiting;
		IOCompletionResult							result;
		S32											killOnWake;
	} waitThread;
	
	struct {
		U32											iocaNeedsBeforeCheckMsg		: 1;
		U32											waitingInThread				: 1;
		U32											userNotifiedThreadFinished	: 1;
	} flags;
} IOCompletionPort;

S32 iocpCreate(IOCompletionPort** iocpOut){
	IOCompletionPort* iocp;
	
	if(!iocpOut){
		return 0;
	}
	
	iocp = callocStruct(IOCompletionPort);

	*iocpOut = iocp;
	
	return 1;
}

static void iocpStopWaitingInThread(IOCompletionPort* iocp);

S32 iocpDestroy(IOCompletionPort** iocpInOut){
	IOCompletionPort* iocp = SAFE_DEREF(iocpInOut);
	
	if(!iocp){
		return 0;
	}

	assert(!iocp->expectedCompletionCount);
	assert(!iocp->iocas);

	if(iocp->hCompletionPort){
		CloseHandle(iocp->hCompletionPort);
		iocp->hCompletionPort = NULL;
	}
	
	// Shut down the thread.
	
	if(iocp->waitThread.handle){
		iocpStopWaitingInThread(iocp);

		iocp->waitThread.killOnWake = 1;
		SetEvent(iocp->waitThread.eventStartWaiting);
		WaitForSingleObject(iocp->waitThread.eventCompletionFinished, INFINITE);

		CloseHandle(iocp->waitThread.eventStartWaiting);
		iocp->waitThread.eventStartWaiting = NULL;
	}

	if(iocp->waitThread.eventCompletionFinished){
		CloseHandle(iocp->waitThread.eventCompletionFinished);
		iocp->waitThread.eventCompletionFinished = NULL;
	}

	SAFE_FREE(*iocpInOut);

	return 1;
}

void iocpAssociationDestroyInternal(IOCompletionAssociation* ioca){
	IOCompletionPort* iocp = ioca->iocp;
	
	PERFINFO_AUTO_START_FUNC();
	
	if(eaFindAndRemove(&iocp->iocas, ioca) < 0){
		assert(0);
	}
	
	if(!eaSize(&iocp->iocas)){
		eaDestroy(&iocp->iocas);
	}
	
	// Tell the owner that the ioca is gone.
	{
		IOCompletionMsg msg = {0};
		
		msg.msgType = IOCP_MSG_ASSOCIATION_DESTROYED;
		msg.iocp = iocp;
		msg.ioca = ioca;
		msg.userPointer = ioca->userPointer;
		msg.handle = ioca->handle;
		
		ioca->msgHandler(&msg);
	}
		
	SAFE_FREE(ioca);
	
	PERFINFO_AUTO_STOP();
}

static void iocpSendMsgsBeforeCheck(IOCompletionPort* iocp){
	if(!TRUE_THEN_RESET(iocp->flags.iocaNeedsBeforeCheckMsg)){
		return;
	}

	EARRAY_CONST_FOREACH_BEGIN(iocp->iocas, i, isize);
		IOCompletionAssociation* ioca = iocp->iocas[i];
		
		if(TRUE_THEN_RESET(ioca->flags.needsBeforeCheckMsg)){
			IOCompletionMsg msg = {0};
			S32				isFirst = FALSE_THEN_SET(ioca->flags.sendingMsgBeforeCheck);
			
			assert(!ioca->flags.destroyedDuringBeforeCheck);

			msg.msgType = IOCP_MSG_ASSOCIATION_BEFORE_CHECK;
			
			msg.iocp = iocp;
			
			msg.ioca = ioca;
			msg.userPointer = ioca->userPointer;
			msg.handle = ioca->handle;
			
			PERFINFO_AUTO_START("msgHandler:beforeCheck", 1);
			{
				ioca->msgHandler(&msg);
			}
			PERFINFO_AUTO_STOP();

			if(isFirst){
				ASSERT_TRUE_AND_RESET(ioca->flags.sendingMsgBeforeCheck);
			}
			
			if(TRUE_THEN_RESET(ioca->flags.destroyedDuringBeforeCheck)){
				iocpAssociationDestroy(ioca);
			}
		}
	EARRAY_FOREACH_END;
}

static void iocpGetQueuedCompletionStatus(	IOCompletionPort* iocp,
											U32 msTimeout,
											IOCompletionResult* resultOut)
{
	resultOut->bytesTransferred = 0;
	resultOut->userPointer = NULL;
	resultOut->ol = NULL;

	resultOut->success = GetQueuedCompletionStatus(	iocp->hCompletionPort,
													&resultOut->bytesTransferred,
													(intptr_t*)&resultOut->userPointer,
													&resultOut->ol,
													msTimeout);

	resultOut->errorCode = GetLastError();
}

static S32 __stdcall iocpWaitThreadMain(IOCompletionPort* iocp){
	EXCEPTION_HANDLER_BEGIN
	
	while(1){
		autoTimerThreadFrameBegin(__FUNCTION__);

		#if PRINT_DEBUG_STUFF
			printf("waiting for start\n");
		#endif
	
		WaitForSingleObject(iocp->waitThread.eventStartWaiting, INFINITE);
		
		if(iocp->waitThread.killOnWake){
			PERFINFO_AUTO_START("killed", 1);
			SetEvent(iocp->waitThread.eventCompletionFinished);
			PERFINFO_AUTO_STOP();
			break;
		}
		
		#if PRINT_DEBUG_STUFF
			printf("waiting for iocp\n");
		#endif

		PERFINFO_AUTO_START_BLOCKING("GetQueuedCompletionStatus", 1);
		iocpGetQueuedCompletionStatus(iocp, INFINITE, &iocp->waitThread.result);
		PERFINFO_AUTO_STOP();
		
		#if PRINT_DEBUG_STUFF
			printf("wait in thread finished.\n");
		#endif
		
		SetEvent(iocp->waitThread.eventCompletionFinished);

		autoTimerThreadFrameEnd();
	}
	
	EXCEPTION_HANDLER_END
	
	return 0;
}

static void iocpCreateCompletionEvent(IOCompletionPort* iocp){
	if(!iocp->waitThread.eventCompletionFinished){
		iocp->waitThread.eventCompletionFinished = CreateEvent(NULL, TRUE, FALSE, NULL);
	}
}

S32 iocpCheckInThread(	IOCompletionPort* iocp,
						IOCompletionDoneWaitingInThreadCallback callback,
						void* userPointer)
{
	if(!iocp){
		return 0;
	}
	
	PERFINFO_AUTO_START_FUNC();
	
	if(!iocp->flags.waitingInThread){
		iocpSendMsgsBeforeCheck(iocp);
		
		if(iocp->flags.iocaNeedsBeforeCheckMsg){
			PERFINFO_AUTO_STOP();
			return 0;
		}

		if(!iocp->waitThread.handle){
			iocpCreateCompletionEvent(iocp);
			
			iocp->waitThread.eventStartWaiting = CreateEvent(NULL, FALSE, FALSE, NULL);

			iocp->waitThread.handle = (HANDLE)_beginthreadex(	NULL,
																0,
																iocpWaitThreadMain,
																iocp,
																0,
																NULL);
		}
		
		iocp->flags.waitingInThread = 1;
		iocp->flags.userNotifiedThreadFinished = 0;
		
		iocp->waitThread.callback = callback;
		iocp->waitThread.userPointer = userPointer;
	
		SetEvent(iocp->waitThread.eventStartWaiting);
		
		#if PRINT_DEBUG_STUFF
			printf("started thread wait\n");
		#endif
	}
	
	PERFINFO_AUTO_STOP();
	
	return 1;
}

void iocpThreadWaitFinished(IOCompletionPort* iocp){
	if(	!iocp ||
		!iocp->flags.waitingInThread)
	{
		return;
	}
	
	iocp->flags.userNotifiedThreadFinished = 1;
}

S32 iocpCheck(	IOCompletionPort* iocp,
				U32 msTimeout,
				U32 msMaxTime,
				S32* moreCompletionsAvailableOut)
{
	S32 retVal = 0;
	U32 msStartTime = 0;
	U32 countGQCS = 0;
	
	PERFINFO_AUTO_START_FUNC();
	
	if(!SAFE_MEMBER(iocp, hCompletionPort)){
		PERFINFO_AUTO_STOP();
		return 0;
	}
	
	iocpSendMsgsBeforeCheck(iocp);
		
	if(iocp->flags.iocaNeedsBeforeCheckMsg){
		// Something still needs to reconnect, so don't allow infinite timeouts.
		
		msTimeout = 0;
	}

	if(msMaxTime){
		msStartTime = timeGetTime();
	}
	
	if(moreCompletionsAvailableOut){
		*moreCompletionsAvailableOut = 0;
	}

	while(iocp->expectedCompletionCount){
		IOCompletionResult result;
		
		if(iocp->flags.waitingInThread){
			// Check if the thread is done waiting.

			U32 waitResult = 0;
			
			if(!iocp->flags.userNotifiedThreadFinished){
				WaitForSingleObjectWithReturn(	iocp->waitThread.eventCompletionFinished,
												0,
												waitResult);
			}
			
			if(	iocp->flags.userNotifiedThreadFinished ||
				waitResult == WAIT_OBJECT_0)
			{
				ResetEvent(iocp->waitThread.eventCompletionFinished);

				#if PRINT_DEBUG_STUFF
					printf("got completion from thread\n");
				#endif
			
				iocp->flags.waitingInThread = 0;
				
				if(	!iocp->flags.userNotifiedThreadFinished &&
					iocp->waitThread.callback)
				{
					iocp->waitThread.callback(iocp, iocp->waitThread.userPointer);
				}

				// Copy the result from the wait thread.
				
				result = iocp->waitThread.result;
			}else{
				#if PRINT_DEBUG_STUFF
					printf("still waiting for completion from thread\n");
				#endif

				// Not done waiting, so quit.

				assert(waitResult == WAIT_TIMEOUT);
				break;
			}
		}else{
			if(!countGQCS){
				PERFINFO_AUTO_START_BLOCKING("GetQueuedCompletionStatus(first)", 1);
			}else{
				PERFINFO_AUTO_START_BLOCKING("GetQueuedCompletionStatus(not-first)", 1);
			}
			{
				iocpGetQueuedCompletionStatus(iocp, msTimeout, &result);
			}
			PERFINFO_AUTO_STOP();
			
			countGQCS++;
		}
		
		if(!result.userPointer){
			// Nothing completed within the timeout period.

			if(result.errorCode != WAIT_TIMEOUT){
				assertmsgf(0, "IOCompletion failed with error %d", result.errorCode);
			}

			break;
		}

		if(result.userPointer == iocp){
			// iocpStopWaitingInThread was called, so just ignore it and loop.
			
			continue;
		}else{
			IOCompletionAssociation* ioca = result.userPointer;
			
			assert(ioca->iocp == iocp);
			assert(ioca->msgHandler);

			if(ioca->handle){
				assert(result.ol);
			}
			
			assert(ioca->expectedCompletionCount);
			InterlockedDecrement(&ioca->expectedCompletionCount);
			InterlockedDecrement(&iocp->expectedCompletionCount);
			
			if(ioca->flags.destroying){
				if(!ioca->expectedCompletionCount){
					iocpAssociationDestroyInternal(ioca);
				}
			}else{
				IOCompletionMsg msg = {0};

				if(result.success){
					PERFINFO_AUTO_START("msgHandler:IO_SUCCESS", 1);

					msg.msgType = IOCP_MSG_IO_SUCCESS;
				}
				else if(result.errorCode == ERROR_OPERATION_ABORTED){
					PERFINFO_AUTO_START("msgHandler:IO_ABORTED", 1);

					msg.msgType = IOCP_MSG_IO_ABORTED;
				}else{
					PERFINFO_AUTO_START("msgHandler:IO_FAILED", 1);

					msg.msgType = IOCP_MSG_IO_FAILED;
				}
				
				msg.iocp = iocp;
				
				msg.ioca = ioca;
				msg.userPointer = ioca->userPointer;
				msg.handle = ioca->handle;
				msg.ol = result.ol;
				
				msg.bytesTransferred = result.bytesTransferred;
				
				msg.errorCode = result.errorCode;
				
				ioca->msgHandler(&msg);

				PERFINFO_AUTO_STOP();
			}
		}

		retVal = 1;
		
		// Check if max time has expired.

		if(	!msMaxTime ||
			(U32)(timeGetTime() - msStartTime) >= msMaxTime)
		{
			// msMaxTime has passed or isn't set, so stop checking for completions.
			
			if(moreCompletionsAvailableOut){
				*moreCompletionsAvailableOut = 1;
			}
			
			break;
		}

		msTimeout = 0;
	}
	
	PERFINFO_AUTO_STOP();

	return retVal;
}

S32 iocpAssociationCreate(	IOCompletionAssociation** iocaOut,
							IOCompletionPort* iocp,
							HANDLE handle,
							IOCompletionMsgHandler msgHandler,
							void* userPointer)
{
	IOCompletionAssociation*	ioca;
	
	if(	!iocaOut ||
		!iocp ||
		!msgHandler)
	{
		return 0;
	}
	
	ioca = callocStruct(IOCompletionAssociation);
	
	ioca->iocp = iocp;
	ioca->msgHandler = msgHandler;
	ioca->handle = handle;
	ioca->userPointer = userPointer;
	
	eaPush(&iocp->iocas, ioca);
	
	if(	handle ||
		!iocp->hCompletionPort)
	{
		HANDLE hNewPort = CreateIoCompletionPort(	FIRST_IF_SET(handle, INVALID_HANDLE_VALUE),
													iocp->hCompletionPort,
													(intptr_t)ioca,
													1);

		assert(	!iocp->hCompletionPort ||
				iocp->hCompletionPort == hNewPort);

		iocp->hCompletionPort = hNewPort;
	}

	*iocaOut = ioca;
	
	return 1;
}

static void iocpStopWaitingInThread(IOCompletionPort* iocp){
	if(iocp->flags.waitingInThread){
		#if PRINT_DEBUG_STUFF
			printf("posting completion\n");
		#endif

		PostQueuedCompletionStatus(	iocp->hCompletionPort,
									0,
									(intptr_t)iocp,
									NULL);
		
		WaitForSingleObject(iocp->waitThread.eventCompletionFinished, INFINITE);
	}
}

void iocpAssociationCancelIO(IOCompletionAssociation* ioca){
	IOCompletionPort*	iocp = SAFE_MEMBER(ioca, iocp);
	S32					result;
	
	if(	!iocp ||
		!ioca->expectedCompletionCount)
	{
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();
	
	iocpStopWaitingInThread(iocp);
	
	if(ioca->handle){
		PERFINFO_AUTO_START("CancelIo", 1);
		result = CancelIo(ioca->handle);
		PERFINFO_AUTO_STOP();

		if(!result){
			U32 errorCode = GetLastError();
			assertmsgf(0, "CancelIo failed: %d\n", errorCode);
		}
	}
	
	PERFINFO_AUTO_STOP();// FUNC
}

void iocpAssociationDestroy(IOCompletionAssociation* ioca){
	if(	!ioca ||
		ioca->flags.destroying)
	{
		return;
	}
	
	#if PRINT_DEBUG_STUFF
		printf("%s 0x%8.8p\n", __FUNCTION__, ioca);
	#endif
	
	if(ioca->flags.sendingMsgBeforeCheck){
		ioca->flags.destroyedDuringBeforeCheck = 1;
		ioca->flags.needsBeforeCheckMsg = 0;
	}
	else if(ioca->expectedCompletionCount){
		ioca->flags.destroying = 1;

		iocpAssociationCancelIO(ioca);
	}else{
		iocpAssociationDestroyInternal(ioca);
	}
}

void iocpAssociationExpectCompletion(IOCompletionAssociation* ioca){
	if(!ioca){
		return;
	}

	assert(!ioca->flags.destroying);

	InterlockedIncrement(&ioca->expectedCompletionCount);
	InterlockedIncrement(&ioca->iocp->expectedCompletionCount);
}

S32 iocpAssociationHasExpectedCompletions(IOCompletionAssociation* ioca){
	return !!SAFE_MEMBER(ioca, expectedCompletionCount);
}

void iocpAssociationNeedsBeforeCheckMsg(IOCompletionAssociation* ioca){
	IOCompletionPort* iocp = SAFE_MEMBER(ioca, iocp);

	if(	!iocp ||
		ioca->flags.destroyedDuringBeforeCheck)
	{
		return;
	}
	
	if(FALSE_THEN_SET(ioca->flags.needsBeforeCheckMsg)){
		iocp->flags.iocaNeedsBeforeCheckMsg = 1;
	}
}

void iocpAssociationTriggerCompletion(IOCompletionAssociation* ioca){
	IOCompletionPort* iocp = SAFE_MEMBER(ioca, iocp);

	if(!iocp){
		return;
	}

	iocpAssociationExpectCompletion(ioca);

	PostQueuedCompletionStatus(	iocp->hCompletionPort,
								0,
								(intptr_t)ioca,
								NULL);
}

S32 iocpGetThreadWaitFinishedEvent(	IOCompletionPort* iocp,
									HANDLE* eventThreadWaitFinishedOut)
{
	if(	!iocp ||
		!eventThreadWaitFinishedOut)
	{
		return 0;
	}
	
	iocpCreateCompletionEvent(iocp);

	*eventThreadWaitFinishedOut = iocp->waitThread.eventCompletionFinished;
	
	return 1;
}
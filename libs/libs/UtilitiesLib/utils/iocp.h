#pragma once
GCC_SYSTEM


#if !_PS3
#include "wininclude.h"

typedef struct IOCompletionAssociation	IOCompletionAssociation;
typedef struct IOCompletionPort			IOCompletionPort;

typedef enum IOCompletionMsgType {
	IOCP_MSG_IO_SUCCESS,
	IOCP_MSG_IO_ABORTED,
	IOCP_MSG_IO_FAILED,
	IOCP_MSG_ASSOCIATION_DESTROYED,
	IOCP_MSG_ASSOCIATION_BEFORE_CHECK,
} IOCompletionMsgType;

typedef struct IOCompletionMsg {
	IOCompletionMsgType			msgType;
	
	IOCompletionPort*			iocp;
	
	IOCompletionAssociation*	ioca;
	void*						userPointer;
	HANDLE						handle;
	OVERLAPPED*					ol;

	U32							bytesTransferred;
	
	U32							errorCode;
} IOCompletionMsg;

typedef void (*IOCompletionMsgHandler)(const IOCompletionMsg* msg);

typedef void (*IOCompletionDoneWaitingInThreadCallback)(IOCompletionPort* iocp,
														void* userPointer);

S32		iocpCreate(IOCompletionPort** iocpOut);

S32		iocpDestroy(IOCompletionPort** iocpInOut);

S32		iocpCheck(	IOCompletionPort* iocp,
					U32 msTimeout,
					U32 msMaxTime,
					S32* moreCompletionsAvailableOut);

S32		iocpCheckInThread(	IOCompletionPort* iocp,
							IOCompletionDoneWaitingInThreadCallback callback,
							void* userPointer);

void	iocpThreadWaitFinished(IOCompletionPort* iocp);

S32		iocpAssociationCreate(	IOCompletionAssociation** iocaOut,
								IOCompletionPort* iocp,
								HANDLE handle,
								IOCompletionMsgHandler msgHandler,
								void* userPointer);

void	iocpAssociationDestroy(IOCompletionAssociation* ioca);

void	iocpAssociationCancelIO(IOCompletionAssociation* ioca);

void	iocpAssociationExpectCompletion(IOCompletionAssociation* ioca);

S32		iocpAssociationHasExpectedCompletions(IOCompletionAssociation* ioca);

void	iocpAssociationNeedsBeforeCheckMsg(IOCompletionAssociation* ioca);

void	iocpAssociationTriggerCompletion(IOCompletionAssociation* ioca);

S32		iocpGetThreadWaitFinishedEvent(	IOCompletionPort* iocp,
										HANDLE* eventThreadWaitFinishedOut);

#endif

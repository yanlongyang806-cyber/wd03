
#if !PLATFORM_CONSOLE

#include "pipeClient.h"
#include "mathutil.h"
#include "timing.h"
#include "iocp.h"
#include "UTF8.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

#define PC_PRINT_RECEIVED_DATA	0
#define PC_PRINT_DATA_WRITTEN	0
#define PC_PRINT_ABORTED		0
#define PC_PRINT_DISCONNECTED	0

typedef struct PipeClientAsyncBuffer {
	U8							buffer[1000];
	U32							bufferSize;
	OVERLAPPED					ol;
	void*						userPointer;
} PipeClientAsyncBuffer;

typedef struct PipeClient {
	void*						userPointer;
	PipeClientMsgHandler		msgHandler;

	char*						pipeName;

	HANDLE						hPipe;
	IOCompletionPort*			iocp;
	IOCompletionAssociation*	ioca;
	PipeClientAsyncBuffer		readBuffer;
	PipeClientAsyncBuffer		writeBuffer;
	U32							bytesWritten;

	struct {
		U32						waitForAssociationDestroy	: 1;
		U32						isWriting					: 1;
		U32						iocpIsMine					: 1;
	} flags;
} PipeClient;

S32	pcCreate(	PipeClient** pcOut,
				void* userPointer,
				PipeClientMsgHandler msgHandler,
				IOCompletionPort* iocp)
{
	PipeClient* pc;

	if(	!pcOut ||
		!msgHandler)
	{
		return 0;
	}
	
	pc = callocStruct(PipeClient);
	
	pc->userPointer = userPointer;
	pc->msgHandler = msgHandler;
	pc->iocp = iocp;
	
	if(!iocp){
		pc->flags.iocpIsMine = 1;
		
		if(!iocpCreate(&pc->iocp)){
			pcDestroy(&pc);
			return 0;
		}
	}
	
	*pcOut = pc;
	
	return 1;
}

S32 pcDestroy(PipeClient** pcInOut){
	PipeClient* pc = SAFE_DEREF(pcInOut);
	
	if(!pc){
		return 0;
	}
	
	pcDisconnect(pc);
	
	while(pc->hPipe){
		assert(pc->iocp);
		iocpCheck(pc->iocp, 10, 10, NULL);
	}
	
	assert(!pc->ioca);
	
	if(pc->flags.iocpIsMine){
		iocpDestroy(&pc->iocp);
	}
	
	SAFE_FREE(*pcInOut);
	
	return 1;
}

S32 pcServerIsAvailable(const char* pipeName){
	char globalPipeName[MAX_PATH];

	sprintf(globalPipeName,
			"\\\\.\\pipe\\%s",
			pipeName);

	return !!WaitNamedPipe_UTF8(globalPipeName, 0);
}

static void pcWaitForRead(PipeClient* pc){
	S32 result;
	U32 error = 0;
	
	PERFINFO_AUTO_START("ReadFile", 1);
		result = ReadFile(	pc->hPipe,
							pc->readBuffer.buffer,
							sizeof(pc->readBuffer.buffer),
							NULL,
							&pc->readBuffer.ol);
							
		if(!result){
			error = GetLastError();
		}
	PERFINFO_AUTO_STOP();
	
	if(	!result &&
		error != ERROR_IO_PENDING)
	{
		pcDisconnect(pc);
	}else{
		iocpAssociationExpectCompletion(pc->ioca);
	}
}

static void pcIOCompletionMsgHandler(const IOCompletionMsg* msg){
	PipeClient* pc = msg->userPointer;
	
	switch(msg->msgType){
		xcase IOCP_MSG_ASSOCIATION_DESTROYED:{
			ASSERT_TRUE_AND_RESET(pc->flags.waitForAssociationDestroy);
			assert(pc->hPipe);
			assert(pc->ioca);
			
			pc->ioca = NULL;
			
			CloseHandle(pc->hPipe);
			pc->hPipe = NULL;

			{
				PipeClientMsg msgOut = {0};
				
				msgOut.msgType = PC_MSG_DISCONNECT;
				msgOut.pc.pc = pc;
				msgOut.pc.userPointer = pc->userPointer;
				
				pc->msgHandler(&msgOut);
			}
		}

		xcase IOCP_MSG_IO_SUCCESS:{
			if(msg->ol == &pc->readBuffer.ol){
				PipeClientMsg msgOut = {0};
				
				#if PC_PRINT_RECEIVED_DATA
				{
					printf(	"Client received data (%d bytes, error %d).\n",
							msg->bytesTransferred,
							msg->errorCode);
				}
				#endif
							
				msgOut.msgType = PC_MSG_DATA_RECEIVED;
				msgOut.pc.pc = pc;
				msgOut.pc.userPointer = pc->userPointer;
				
				msgOut.dataReceived.data = pc->readBuffer.buffer;
				msgOut.dataReceived.dataBytes = msg->bytesTransferred;
			
				PERFINFO_AUTO_START("pcMsgHandler:DATA_RECEIVED", 1);
					pc->msgHandler(&msgOut);
				PERFINFO_AUTO_STOP();
				
				pcWaitForRead(pc);
			}
			else if(msg->ol == &pc->writeBuffer.ol){
				PipeClientMsg msgOut = {0};

				ASSERT_TRUE_AND_RESET(pc->flags.isWriting);
				
				#if PC_PRINT_DATA_WRITTEN
				{
					printf("PipeClient wrote something.\n");
				}
				#endif

				msgOut.msgType = PC_MSG_DATA_SENT;
				msgOut.pc.pc = pc;
				msgOut.pc.userPointer = pc->userPointer;
				
				msgOut.dataSent.dataUserPointer = pc->writeBuffer.userPointer;
			
				PERFINFO_AUTO_START("pcMsgHandler:DATA_SENT", 1);
					pc->msgHandler(&msgOut);
				PERFINFO_AUTO_STOP();
			}else{
				assert(0);
			}
		}
		
		xcase IOCP_MSG_IO_ABORTED:{
			#if PC_PRINT_ABORTED
			{
				printf(	"Operation was aborted: %s.\n",
						msg->ol == &pc->readBuffer.ol ?
							"READ" :
							msg->ol == &pc->writeBuffer.ol ?
								"WRITE" :
								"UNKNOWN");
			}
			#endif
		}
		
		xcase IOCP_MSG_IO_FAILED:{
			assert(pc->ioca);
			
			// Server disconnected me.
			
			#if PC_PRINT_DISCONNECTED
			{
				printf("Server disconnected.\n");
			}
			#endif
			
			pcDisconnect(pc);
		}
	}
}

S32	pcConnect(	PipeClient* pc,
				const char* pipeName,
				U32 msTimeout)
{
	char	globalPipeName[MAX_PATH];
	U32		startTime;
	U32		remainingTime;
	
	if(	!pc
		||
		!pc->pipeName &&
		!SAFE_DEREF(pipeName)
		||
		pc->flags.waitForAssociationDestroy)
	{
		return 0;
	}
	
	PERFINFO_AUTO_START_FUNC();

	if(	pipeName
		&&
		(	!pc->pipeName ||
			stricmp(pipeName, pc->pipeName))
		)
	{
		SAFE_FREE(pc->pipeName);
		pc->pipeName = strdup(pipeName);
	}
	
	if(!pc->pipeName){
		PERFINFO_AUTO_STOP();
		return 0;
	}
	
	pcDisconnect(pc);
	
	sprintf(globalPipeName,
			"\\\\.\\pipe\\%s",
			pc->pipeName);
	
	if(msTimeout == INFINITE){
		msTimeout = NMPWAIT_WAIT_FOREVER;
	}else{
		assert(msTimeout < S32_MAX);
	}
	
	startTime = timeGetTime();
	remainingTime = msTimeout;
	
	while(WaitNamedPipe_UTF8(globalPipeName,
						remainingTime == INFINITE ?
							INFINITE :
							MIN(remainingTime, 1000)))
	{
		// There's a pipe instance available for connecting to.

		pc->hPipe = CreateFile_UTF8(	globalPipeName,
								GENERIC_WRITE | GENERIC_READ,
								0,
								NULL,
								OPEN_EXISTING,
								FILE_FLAG_OVERLAPPED,
								0);

		if(pc->hPipe == INVALID_HANDLE_VALUE){
			U32 errorCode = GetLastError();
			
			printf(	"Pipe open failed (%s): error %d.\n",
					globalPipeName,
					errorCode);
			
			pc->hPipe = NULL;

			if(errorCode == ERROR_ACCESS_DENIED){
				break;
			}else{
				U32 usedTime = subS32(timeGetTime(), startTime);
				
				// The pipe was closed, or another client attached to it, so keep trying.

				if(msTimeout != INFINITE){
					if(usedTime >= msTimeout){
						break;
					}else{
						remainingTime = msTimeout - usedTime;
					}
				}
			}
		}else{
			DWORD mode =	PIPE_READMODE_MESSAGE |
							PIPE_WAIT;
			
			SetNamedPipeHandleState(pc->hPipe,
									&mode,
									NULL,
									NULL);
			
			// Yay, I got the pipe connection.

			iocpAssociationCreate(	&pc->ioca,
									pc->iocp, 
									pc->hPipe,
									pcIOCompletionMsgHandler,
									pc);

			break;
		}
	}
	
	PERFINFO_AUTO_STOP();
	
	pcWaitForRead(pc);

	return !!pc->hPipe;
}

S32	pcDisconnect(PipeClient* pc){
	if(	!pc ||
		!pc->hPipe ||
		pc->flags.waitForAssociationDestroy)
	{
		return 0;
	}
	
	pc->flags.waitForAssociationDestroy = 1;

	iocpAssociationDestroy(pc->ioca);
	
	return 1;
}

S32 pcIsConnected(PipeClient* pc){
	return !!SAFE_MEMBER(pc, hPipe);
}

S32 pcListen(	PipeClient* pc,
				U32 msTimeout)
{
	S32 ret;
	
	PERFINFO_AUTO_START_FUNC();
		ret = iocpCheck(pc->iocp, msTimeout, 10, NULL);
	PERFINFO_AUTO_STOP();
	
	return ret;
}

S32 pcWrite(PipeClient* pc,
			const U8* data,
			U32 dataBytes,
			void* dataUserPointer)
{
	U32 result;
	U32 error = 0;

	if(	!pc ||
		!pc->hPipe ||
		!data ||
		!dataBytes ||
		pc->flags.isWriting ||
		dataBytes > sizeof(pc->writeBuffer.buffer))
	{
		return 0;
	}
	
	PERFINFO_AUTO_START_FUNC();
	
	memcpy(	pc->writeBuffer.buffer,
			data,
			dataBytes);
			
	pc->writeBuffer.bufferSize = dataBytes;
	pc->writeBuffer.userPointer = dataUserPointer;
	
	PERFINFO_AUTO_START("WriteFile", 1);
	{
		result = WriteFile(	pc->hPipe,
							pc->writeBuffer.buffer,
							pc->writeBuffer.bufferSize,
							NULL,
							&pc->writeBuffer.ol);
		
		if(!result){
			error = GetLastError();
		}
	}
	PERFINFO_AUTO_STOP();
	
	if(	!result &&
		error != ERROR_IO_PENDING)
	{
		pcDisconnect(pc);
		PERFINFO_AUTO_STOP();// FUNC
		return 0;
	}
	
	pc->flags.isWriting = 1;
	
	iocpAssociationExpectCompletion(pc->ioca);
	
	PERFINFO_AUTO_STOP();// FUNC
	return 1;
}
				
S32 pcWriteString(	PipeClient* pc,
					const char* str,
					void* dataUserPointer)
{
	return pcWrite(pc, str, (U32)strlen(str), dataUserPointer); 
}

#endif
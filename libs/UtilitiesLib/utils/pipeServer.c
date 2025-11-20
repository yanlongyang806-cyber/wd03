
#if !PLATFORM_CONSOLE

#include "pipeServer.h"
#include "iocp.h"
#include "EString.h"
#include "earray.h"
#include "timing.h"
#include "UTF8.h"

//#define PRINT_DEBUG_STUFF 1

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

typedef struct PipeServerAsyncBuffer {
	U8							buffer[1000];
	U32							bufferSize;
	OVERLAPPED					ol;
	U32							finishedCount;
} PipeServerAsyncBuffer;

typedef struct PipeServerClient {
	PipeServer*					ps;
	
	void*						userPointer;
	
	HANDLE						hPipe;
	IOCompletionAssociation*	ioca;
	
	char*						debugString;
	
	PipeServerAsyncBuffer		readBuffer;
	PipeServerAsyncBuffer		writeBuffer;
	
	U32							connectCount;
	
	struct {
		U32						isConnected					: 1;
		U32						pipeNeedsToWaitForConnect	: 1;
		
		U32						isReading					: 1;
		U32						isWriting					: 1;
	} flags;
} PipeServerClient;

typedef struct PipeServer {
	char*						pipeName;
	
	void*						userPointer;
	PipeServerMsgHandler		msgHandler;
	
	IOCompletionPort*			iocp;
	
	struct {
		PipeServerClient**		pscs;
		U32						connectedCount;
	} pscs;
	
	struct {
		U32						iocpIsMine					: 1;
	} flags;
} PipeServer;

static S32 psCreateNewPipe(PipeServer* ps);

static void psClientWaitForRead(PipeServerClient* psc){
	U32 result;
	U32 error = 0;
	
	PERFINFO_AUTO_START("ReadFile", 1);
	{
		result = ReadFile(	psc->hPipe,
							psc->readBuffer.buffer,
							sizeof(psc->readBuffer.buffer),
							NULL,
							&psc->readBuffer.ol);
		
		if(!result){
			error = GetLastError();
		}
	}
	PERFINFO_AUTO_STOP();	
	
	if(	!result &&
		error != ERROR_IO_PENDING)
	{
		psClientDisconnect(psc);
	}else{
		ASSERT_FALSE_AND_SET(psc->flags.isReading);
		iocpAssociationExpectCompletion(psc->ioca);
	}
}

static void psClientHandleConnect(	PipeServer* ps,
									PipeServerClient* psc)
{
	PERFINFO_AUTO_START_FUNC();
	
	assert(!psc->flags.pipeNeedsToWaitForConnect);

	ps->pscs.connectedCount++;

	psc->connectCount++;
	psc->readBuffer.finishedCount = 0;
	psc->writeBuffer.finishedCount = 0;
	
	psCreateNewPipe(ps);
	
	assert(!psc->flags.isWriting);
	assert(!psc->flags.isReading);

	{
		PipeServerMsg msg = {0};
		
		msg.msgType = PS_MSG_CLIENT_CONNECT;
		
		msg.ps.ps = ps;
		msg.ps.userPointer = ps->userPointer;
		
		msg.psc.psc = psc;

		ps->msgHandler(&msg);
	}
	
	PERFINFO_AUTO_STOP();
}

static void psClientHandleFinishedRead(	PipeServer* ps,
										PipeServerClient* psc,
										U32 bytesTransferred)
{
	PERFINFO_AUTO_START_FUNC();
	
	psc->readBuffer.finishedCount++;

	if(bytesTransferred){
		ASSERT_TRUE_AND_RESET(psc->flags.isReading);

		assert(bytesTransferred < ARRAY_SIZE(psc->readBuffer.buffer));
		psc->readBuffer.bufferSize = bytesTransferred;
		psc->readBuffer.buffer[bytesTransferred] = 0;
	
		//printf("Read %d bytes: %s\n", bytesTransferred, psc->readBuffer.buffer);

		// Send the buffer to the client's owner.
		
		{
			PipeServerMsg msg = {0};
			
			msg.msgType = PS_MSG_DATA_RECEIVED;
			
			msg.ps.ps = ps;
			msg.ps.userPointer = ps->userPointer;
			
			msg.psc.psc = psc;
			msg.psc.userPointer = psc->userPointer;
			
			msg.dataReceived.data = psc->readBuffer.buffer;
			msg.dataReceived.dataBytes = bytesTransferred;
			
			ps->msgHandler(&msg);
		}
	}

	psClientWaitForRead(psc);
	
	PERFINFO_AUTO_STOP();
}

static void psClientHandleFinishedWrite(PipeServer* ps,
										PipeServerClient* psc,
										U32 bytesTransferred)
{
	PERFINFO_AUTO_START_FUNC();

	psc->writeBuffer.finishedCount++;

	ASSERT_TRUE_AND_RESET(psc->flags.isWriting);
	
	{
		PipeServerMsg msg = {0};
		
		msg.msgType = PS_MSG_DATA_SENT;
		
		msg.ps.ps = ps;
		msg.ps.userPointer = ps->userPointer;
		
		msg.psc.psc = psc;
		msg.psc.userPointer = psc->userPointer;
		
		ps->msgHandler(&msg);
	}

	PERFINFO_AUTO_STOP();
}

static void psHandleFinishedIO(	PipeServerClient* psc,
								OVERLAPPED* ol,
								U32 bytesTransferred)
{
	PipeServer* ps = psc->ps;
	
	if(FALSE_THEN_SET(psc->flags.isConnected)){
		assert(ol == &psc->readBuffer.ol);

		psClientHandleConnect(ps, psc);
	}
	
	if(ol == &psc->readBuffer.ol){
		psClientHandleFinishedRead(ps, psc, bytesTransferred);
	}
	else if(ol == &psc->writeBuffer.ol){
		psClientHandleFinishedWrite(ps, psc, bytesTransferred);
	}else{
		assert(0);
	}
}

static void psWaitForConnectPipe(PipeServerClient* psc){
	if(!TRUE_THEN_RESET(psc->flags.pipeNeedsToWaitForConnect)){
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();
	
	assert(!psc->flags.isConnected);

	if(!ConnectNamedPipe(psc->hPipe, &psc->readBuffer.ol)){
		U32 error = GetLastError();

		switch(error){
			xcase ERROR_PIPE_CONNECTED:{
				// This means that the client connected AFTER CreateNamedPipe
				//   and BEFORE ConnectNamedPipe.
				
				//printf("Pipe already connected!\n");
				
				psHandleFinishedIO(psc, &psc->readBuffer.ol, 0);
			}

			xcase ERROR_NO_DATA:{
				// This means that the client connected AFTER CreateNamedPipe
				//   and disconnected BEFORE ConnectNamedPipe.
				
				//printf("Client connection failed!\n");

				psClientDisconnect(psc);
			}
			
			xcase ERROR_IO_PENDING:{
				// Success, and connection is now pending.
			
				iocpAssociationExpectCompletion(psc->ioca);
				
				//printf("Success, connection is pending.\n");
			}
			
			xdefault:{
				//printf("Failed opening pipe: %d\n", error);

				FatalErrorf("Can't connect on pipe: %d", error);
			}
		}
	}else{
		//printf("Connected!\n");
	}
	
	PERFINFO_AUTO_STOP();
}

static void psClientClearAsyncFlag(	PipeServerClient* psc,
									OVERLAPPED* ol)
{
	if(ol == &psc->readBuffer.ol){
		ASSERT_TRUE_AND_RESET(psc->flags.isReading);
	}
	else if(ol == &psc->writeBuffer.ol){
		ASSERT_TRUE_AND_RESET(psc->flags.isWriting);
	}else{
		assert(0);
	}
}

static void psClientIOCompletionMsgHandler(const IOCompletionMsg* msg){
	PipeServerClient* psc = msg->userPointer;
	
	PERFINFO_AUTO_START_FUNC();
	
	assert(	!psc ||
			psc->ioca == msg->ioca);
	
	switch(msg->msgType){
		xcase IOCP_MSG_ASSOCIATION_DESTROYED:{
			if(!psc){
				break;
			}
			
			psc->ioca = NULL;
		}

		xcase IOCP_MSG_ASSOCIATION_BEFORE_CHECK:{
			psWaitForConnectPipe(psc);
		}

		xcase IOCP_MSG_IO_SUCCESS:{
			// Some client IO is finished.
			
			psHandleFinishedIO(	psc,
								msg->ol,
								msg->bytesTransferred);
		}
		
		xcase IOCP_MSG_IO_ABORTED:{
			#if PRINT_DEBUG_STUFF
				printf("Client operation aborted: %d\n", msg->errorCode);
			#endif
			
			if(!psc){
				break;
			}
			
			psClientClearAsyncFlag(psc, msg->ol);
			psClientDisconnect(psc);
		}
		
		xcase IOCP_MSG_IO_FAILED:{
			assert(psc);
			
			// A client has disconnected during IO.
			
			#if PRINT_DEBUG_STUFF
				printf("Client disconnected: %d\n", msg->errorCode);
			#endif
			
			psClientClearAsyncFlag(psc, msg->ol);
			psClientDisconnect(psc);
		}
	}

	PERFINFO_AUTO_STOP();
}

static S32 psCreateNewPipe(PipeServer* ps){
	if(ps->pscs.connectedCount == eaUSize(&ps->pscs.pscs)){
		PipeServerClient*	psc;
		char				globalPipeName[MAX_PATH];

		#if PRINT_DEBUG_STUFF
			printf(	"Creating pipe %s, #%d.\n",
					ps->pipeName,
					ps->pscs.connectedCount);
		#endif

		psc = callocStruct(PipeServerClient);
		
		psc->ps = ps;

		sprintf(globalPipeName,
				"\\\\.\\pipe\\%s",
				ps->pipeName);

		psc->hPipe = CreateNamedPipe_UTF8(	globalPipeName,
										PIPE_ACCESS_DUPLEX |
											FILE_FLAG_OVERLAPPED,
										PIPE_TYPE_MESSAGE |
											PIPE_READMODE_MESSAGE |
											PIPE_WAIT,
										PIPE_UNLIMITED_INSTANCES,
										100 * 1000,
										100 * 1000,
										0,
										NULL);

		if(psc->hPipe == INVALID_HANDLE_VALUE){
			SAFE_FREE(psc);
			return 0;
		}
		
		eaPush(&ps->pscs.pscs, psc);
		
		iocpAssociationCreate(	&psc->ioca,
								ps->iocp,
								psc->hPipe,
								psClientIOCompletionMsgHandler,
								psc);
						
		psc->flags.pipeNeedsToWaitForConnect = 1;
						
		psWaitForConnectPipe(psc);
	}

	return 1;
}

S32	psCreate(	PipeServer** psOut,
				const char* pipeName,
				IOCompletionPort* iocp,
				void* userPointer,
				PipeServerMsgHandler msgHandler)
{
	PipeServer* ps;
	
	if(	!psOut ||
		!pipeName ||
		!msgHandler)
	{
		return 0;
	}
	
	ps = callocStruct(PipeServer);
	
	ps->pipeName = strdup(pipeName);

	ps->userPointer = userPointer;
	ps->msgHandler = msgHandler;
	ps->iocp = iocp;
	
	if(!iocp){
		ps->flags.iocpIsMine = 1;

		if(!iocpCreate(&ps->iocp)){
			psDestroy(&ps);
			return 0;
		}
	}
	
	if(!psCreateNewPipe(ps)){
		psDestroy(&ps);
		return 0;
	}

	*psOut = ps;
	
	return 1;
}

S32 psDestroy(PipeServer** psInOut){
	PipeServer* ps = SAFE_DEREF(psInOut);
	
	if(!ps){
		return 0;
	}
	
	while(eaSize(&ps->pscs.pscs)){
		PipeServerClient* psc = ps->pscs.pscs[0];
		
		psClientDisconnect(psc);
		iocpAssociationDestroy(psc->ioca);
		
		while(psc->ioca){
			iocpCheck(ps->iocp, 0, 10, NULL);
		}
		
		CloseHandle(psc->hPipe);
		
		SAFE_FREE(psc);
		
		eaRemove(&ps->pscs.pscs, 0);
	}
	
	if(ps->flags.iocpIsMine){
		iocpDestroy(&ps->iocp);
	}
	
	SAFE_FREE(*psInOut);
	
	return 1;
}

S32 psListen(	PipeServer* ps,
				U32 msTimeout)
{
	S32 ret;
	
	if(!ps){
		return 0;
	}
	
	PERFINFO_AUTO_START_FUNC();
	
	ret = iocpCheck(ps->iocp,
					msTimeout,
					10,
					NULL);
	
	PERFINFO_AUTO_STOP();
	
	return ret;
}

S32 psClientSetUserPointer(	PipeServerClient* psc,
							void* userPointer)
{
	if(	!psc ||
		!psc->ps)
	{
		return 0;
	}
	
	assert(!psc->userPointer);
	
	psc->userPointer = userPointer;
	
	return 1;
}

void psClientDisconnect(PipeServerClient* psc){
	if(!psc){
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();
	
	if(iocpAssociationHasExpectedCompletions(psc->ioca)){
		iocpAssociationCancelIO(psc->ioca);
	}else{
		PipeServer* ps = psc->ps;
		
		PERFINFO_AUTO_START("Disconnect", 1);
		
		assert(!psc->flags.isReading);
		assert(!psc->flags.isWriting);

		//printf("Disconnected: 0x%8.8x, %s\n", client, client->processName);
		
		DisconnectNamedPipe(psc->hPipe);

		if(TRUE_THEN_RESET(psc->flags.isConnected)){
			{
				PipeServerMsg msg = {0};

				msg.msgType = PS_MSG_CLIENT_DISCONNECT;
				
				msg.ps.ps = ps;
				msg.ps.userPointer = ps->userPointer;
				
				msg.psc.psc = psc;
				msg.psc.userPointer = psc->userPointer;

				ps->msgHandler(&msg);
			}
			
			psc->userPointer = NULL;

			assert(ps->pscs.connectedCount);
			ps->pscs.connectedCount--;

			estrDestroy(&psc->debugString);
		}
		
		psc->flags.pipeNeedsToWaitForConnect = 1;
		
		iocpAssociationNeedsBeforeCheckMsg(psc->ioca);
		
		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_STOP();// FUNC
}

S32 psClientWrite(	PipeServerClient* psc,
					const U8* data,
					U32 dataBytes,
					void* dataUserPointer)
{
	U32 result;
	U32 error = 0;
	
	if(	!psc ||
		!psc->hPipe ||
		!data ||
		!dataBytes ||
		psc->flags.isWriting ||
		dataBytes > sizeof(psc->writeBuffer.buffer))
	{
		return 0;
	}
	
	PERFINFO_AUTO_START_FUNC();
	
	memcpy(	psc->writeBuffer.buffer,
			data,
			dataBytes);
			
	psc->writeBuffer.bufferSize = dataBytes;
	
	PERFINFO_AUTO_START_BLOCKING("WriteFile", 1);
	{
		result = WriteFile(	psc->hPipe,
							psc->writeBuffer.buffer,
							psc->writeBuffer.bufferSize,
							NULL,
							&psc->writeBuffer.ol);

		if(!result){
			error = GetLastError();
		}
	}
	PERFINFO_AUTO_STOP();
						
	if(	!result &&
		error != ERROR_IO_PENDING)
	{
		psClientDisconnect(psc);
		PERFINFO_AUTO_STOP();//FUNC
		return 0;
	}

	psc->flags.isWriting = 1;
	
	iocpAssociationExpectCompletion(psc->ioca);
	
	PERFINFO_AUTO_STOP();// FUNC
	return 1;
}

S32 psClientWriteString(PipeServerClient* psc,
						const char* str,
						void* dataUserPointer)
{
	return psClientWrite(psc, str, (U32)strlen(str), dataUserPointer);
}

#endif
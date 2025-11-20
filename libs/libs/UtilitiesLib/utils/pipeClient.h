#pragma once
GCC_SYSTEM


#ifndef _XBOX

typedef struct PipeClient		PipeClient;
typedef struct IOCompletionPort IOCompletionPort;

typedef enum PipeClientMsgType {
	PC_MSG_CONNECT,
	PC_MSG_DISCONNECT,
	
	PC_MSG_DATA_RECEIVED,
	PC_MSG_DATA_SENT,
} PipeClientMsgType;

typedef struct PipeClientMsg {
	PipeClientMsgType		msgType;
	
	struct {
		PipeClient*			pc;
		void*				userPointer;
	} pc;
	
	union {
		struct {
			S32				unused;
		} connect;
		
		struct {
			S32				unused;
		} disconnect;
		
		struct {
			const U8*		data;
			U32				dataBytes;
		} dataReceived;

		struct {
			void*			dataUserPointer;
		} dataSent;
	};
} PipeClientMsg;

typedef void (*PipeClientMsgHandler)(const PipeClientMsg* msg);

S32		pcCreate(	PipeClient** pcOut,
					void* userPointer,
					PipeClientMsgHandler msgHandler,
					IOCompletionPort* iocp);

S32		pcDestroy(PipeClient** pcInOut);

S32		pcServerIsAvailable(const char* pipeName);

S32		pcConnect(	PipeClient* pc,
					const char* pipeName,
					U32 msTimeout);

S32		pcDisconnect(PipeClient* pc);

S32		pcIsConnected(PipeClient* pc);

S32		pcListen(	PipeClient* pc,
					U32 msTimeout);

S32		pcWrite(PipeClient* pc,
				const U8* data,
				U32 dataBytes,
				void* dataUserPointer);
				
S32		pcWriteString(	PipeClient* pc,
						const char* str,
						void* dataUserPointer);

#endif
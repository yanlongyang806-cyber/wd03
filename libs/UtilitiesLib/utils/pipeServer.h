#pragma once
GCC_SYSTEM


#ifndef _XBOX

typedef struct PipeServer		PipeServer;
typedef struct PipeServerClient	PipeServerClient;
typedef struct IOCompletionPort	IOCompletionPort;

typedef enum PipeServerMsgType {
	PS_MSG_CLIENT_DESTROYED,
	
	PS_MSG_CLIENT_CONNECT,
	PS_MSG_CLIENT_DISCONNECT,
	
	PS_MSG_DATA_RECEIVED,
	PS_MSG_DATA_SENT,
	
	PS_MSG_COUNT,
} PipeServerMsgType;

typedef struct PipeServerMsg {
	PipeServerMsgType			msgType;
	
	struct {
		PipeServer*				ps;
		void*					userPointer;
	} ps;
	
	struct {
		PipeServerClient*		psc;
		void*					userPointer;
	} psc;

	union {
		struct {
			S32					unused;
		} clientConnect;
		
		struct {
			S32					unused;
		} clientDisconnect;
		
		struct {
			const U8*			data;
			U32					dataBytes;
		} dataReceived;
	};
} PipeServerMsg;

typedef void (*PipeServerMsgHandler)(const PipeServerMsg* msg);

S32		psCreate(	PipeServer** psOut,
					const char* pipeName,
					IOCompletionPort* iocp,
					void* userPointer,
					PipeServerMsgHandler msgHandler);
							
S32		psDestroy(PipeServer** psInOut);

S32		psListen(	PipeServer* ps,
					U32 msTimeout);

S32		psClientSetUserPointer(	PipeServerClient* psc,
								void* userPointer);

void	psClientDisconnect(PipeServerClient* psc);

S32		psClientWrite(	PipeServerClient* psc,
						const U8* data,
						U32 dataBytes,
						void* dataUserPointer);

S32		psClientWriteString(PipeServerClient* psc,
							const char* str,
							void* dataUserPointer);
								
#endif
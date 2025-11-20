#pragma once

typedef struct ServerLaunchRequest ServerLaunchRequest;
typedef struct Packet Packet;
typedef struct NetLink NetLink;

AUTO_STRUCT;
typedef struct CachedControllerFileRequest
{
	char *pFileName;
	U32 iCRC;
	char *pZippedBuffer; NO_AST
	char *pUnzippedBuffer; NO_AST

	U32 iZippedBufferSize;
	U32 iBytesAlreadyReceived;
	ServerLaunchRequest **ppPendingLaunchRequests;
} CachedControllerFileRequest;



bool FileExistsAndMatchesCRC(char *pFileName, U32 iCRC);

CachedControllerFileRequest *GetControllerFileRequest(char *pFileName, U32 iCRC);
void HandleControllerFileRequestFulfilled(NetLink *pLink, Packet *pPack);
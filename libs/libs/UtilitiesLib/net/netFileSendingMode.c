#include "net.h"
#include "TextParser.h"
#include "earray.h"
#include "estring.h"
#include "file.h"
#include "stashTable.h"
#include "netFileSendingMode_c_ast.h"
#include "netprivate.h"
#include "globalComm.h"
#include "ScratchStack.h"
#include "net_h_ast.h"
#include "crypt.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Networking););

AUTO_STRUCT;
typedef struct LFSM_FileBeingSent
{
	int iHandle;
	int iCmd;
	char *pRemoteFileName;
	S64 iFileSize;
	S64 iBytesAlreadySent;
} LFSM_FileBeingSent;

AUTO_STRUCT;
typedef struct LFSM_ReceiveCommand
{
	int iCmd;
	char *pRootDir;
	linkFileSendingMode_ReceiveCB pCB; NO_AST
} LFSM_ReceiveCommand;

AUTO_STRUCT;
typedef struct LFSM_FileBeingReceived
{
	int iHandle;
	int iCmd;
	char *pFileName;
	FILE *pFile; NO_AST
	S64 iExpectedSize;
	S64 iSizeReceived;
	LFSM_ReceiveCommand *pReceiveCommand; NO_AST
} LFSM_FileBeingReceived;



AUTO_FIXUPFUNC;
TextParserResult fixupLFSM_FileBeingReceived(LFSM_FileBeingReceived *pFileBeingReceived, enumTextParserFixupType eType, void *pExtraData)
{
	switch(eType)
	{
		xcase FIXUPTYPE_DESTRUCTOR: 
		{
			if (pFileBeingReceived->pFile)
			{
				fclose(pFileBeingReceived->pFile);
			}			
		}
	}
	return PARSERESULT_SUCCESS;
}

AUTO_STRUCT;
typedef struct LinkFileSendingMode_ManagedFileSend
{
	U32 iUID;
	char *pLinkName;
	int iHandle;
	int iCmd;
	char *pLocalFileName;
	char *pRemoteFileName;
	U32 iBytesPerTick;
	FILE *pFile; NO_AST
	S64 iFileSize;
	S64 iBytesSent;
	ManagedSendFlags eFlags;
	ManagedFileSendCB pErrorCB; NO_AST
	void *pErrorCBUserData; NO_AST
	ManagedFileSendCB pSuccessCB; NO_AST
	void *pSuccessCBUserData; NO_AST
	Packet *pSuccessPacket; NO_AST
} LinkFileSendingMode_ManagedFileSend;

static LinkFileSendingMode_ManagedFileSend **sppAllManagedFileSends = NULL;
void linkFileSendingMode_FailManagedSend(NetLink *pLink, LinkFileSendingMode_ManagedFileSend *pManagedSend, FORMAT_STR const char* format, ...);


AUTO_FIXUPFUNC;
TextParserResult fixupLinkFileSendingMode_ManagedFileSend(LinkFileSendingMode_ManagedFileSend *pManagedSend, enumTextParserFixupType eFixupType, void *pExtraData)
{
	switch( eFixupType ) 
	{
		xcase FIXUPTYPE_DESTRUCTOR:
			eaFindAndRemove(&sppAllManagedFileSends, pManagedSend);

			if (pManagedSend->pFile)
			{
				fclose(pManagedSend->pFile);
				pManagedSend->pFile = NULL;
			}

			if (pManagedSend->pSuccessPacket)
			{
				pktFree(&pManagedSend->pSuccessPacket);
			}
	}

	return PARSERESULT_SUCCESS;
}



typedef struct LinkFileSendingMode_ReceiveManager
{
	linkFileReceivingMode_ErrorCB pErrorCB;
	StashTable sFilesBeingReceived;
	StashTable sReceiveCommands;
} LinkFileSendingMode_ReceiveManager;


typedef struct LinkFileSendingMode_SendManager
{
	int iNextHandle;
	StashTable sFilesBeingSentByHandle;
	LinkFileSendingMode_ManagedFileSend **ppManagedFileSends;
} LinkFileSendingMode_SendManager;

static LinkFileSendingMode_SendManager *linkFileSendingMode_CreateSendManager(void)
{
	LinkFileSendingMode_SendManager *pManager = calloc(sizeof(LinkFileSendingMode_SendManager), 1);
	pManager->iNextHandle = 1;
	pManager->sFilesBeingSentByHandle = stashTableCreateInt(16);

	return pManager;
}

void linkFileSendingMode_DestroySendManager(LinkFileSendingMode_SendManager *pManager)
{
	stashTableDestroyStruct(pManager->sFilesBeingSentByHandle, NULL, parse_LFSM_FileBeingSent);
	eaDestroyStruct(&pManager->ppManagedFileSends, parse_LinkFileSendingMode_ManagedFileSend);
	free(pManager);
}

static LinkFileSendingMode_ReceiveManager *linkFileSendingMode_CreateReceiveManager(linkFileReceivingMode_ErrorCB pErrorCB)
{
	LinkFileSendingMode_ReceiveManager *pManager = calloc(sizeof(LinkFileSendingMode_ReceiveManager), 1);
	pManager->pErrorCB = pErrorCB;
	pManager->sFilesBeingReceived = stashTableCreateInt(16);
	pManager->sReceiveCommands = stashTableCreateInt(16);

	return pManager;
}

void linkFileSendingMode_DestroyReceiveManager(LinkFileSendingMode_ReceiveManager *pManager)
{
	stashTableDestroyStruct(pManager->sFilesBeingReceived, NULL, parse_LFSM_FileBeingReceived);
	stashTableDestroyStruct(pManager->sReceiveCommands, NULL, parse_LFSM_ReceiveCommand);
	free(pManager);
}

void linkFileSendingMode_InitSending(NetLink *pLink)
{
	if (pLink->pLinkFileSendingMode_SendManager)
	{
		return;
	}

	pLink->pLinkFileSendingMode_SendManager  = linkFileSendingMode_CreateSendManager();
}

int linkFileSendingMode_BeginSendingFile(NetLink *pLink, int iCmd, char *pRemoteFileName, S64 iFileSize)
{
	LFSM_FileBeingSent *pFileBeingSent;
	LinkFileSendingMode_SendManager *pManager;
	Packet *pPak;

	if (!(pLink && pLink->pLinkFileSendingMode_SendManager))
	{
		return 0;
	}

	pManager = pLink->pLinkFileSendingMode_SendManager;
	pFileBeingSent = StructCreate(parse_LFSM_FileBeingSent);

	while (!pFileBeingSent->iHandle)
	{
		pFileBeingSent->iHandle = pManager->iNextHandle++;
	}

	pFileBeingSent->pRemoteFileName = strdup(pRemoteFileName);
	pFileBeingSent->iFileSize = iFileSize;
	pFileBeingSent->iCmd = iCmd;

	stashIntAddPointer(pManager->sFilesBeingSentByHandle, pFileBeingSent->iHandle, pFileBeingSent, true);

	pPak = pktCreate(pLink, SHAREDCMD_FILESENDINGMODE);
	pktSendBits(pPak, 32, LINKFILESENDINGMODE_CMD_BEGINSEND);
	pktSendBits(pPak, 32, pFileBeingSent->iHandle);
	pktSendBits(pPak, 32, pFileBeingSent->iCmd);
	pktSendString(pPak, pFileBeingSent->pRemoteFileName);
	pktSendBits64(pPak, 64, pFileBeingSent->iFileSize);
	pktSend(&pPak);

	return pFileBeingSent->iHandle;
}


bool linkFileSendingMode_SendBytes(NetLink *pLink, int iHandle, void *pData, S64 iDataSize)
{
	LFSM_FileBeingSent *pFileBeingSent;
	LinkFileSendingMode_SendManager *pManager;
	Packet *pPak;

	if (!(pLink && pLink->pLinkFileSendingMode_SendManager))
	{
		return false;
	}

	pManager = pLink->pLinkFileSendingMode_SendManager;
	
	if (!stashIntFindPointer(pManager->sFilesBeingSentByHandle, iHandle, &pFileBeingSent))
	{
		return false;
	}

	if (pFileBeingSent->iBytesAlreadySent + iDataSize > pFileBeingSent->iFileSize)
	{
		linkFileSendingMode_CancelSend(pLink, iHandle);
		return false;
	}

	pPak = pktCreate(pLink, SHAREDCMD_FILESENDINGMODE);
	pktSendBits(pPak, 32, LINKFILESENDINGMODE_CMD_UPDATESEND);
	pktSendBits(pPak, 32, iHandle);
	pktSendBits64(pPak, 64, iDataSize);
	pktSendBytes(pPak, iDataSize, pData);
	pktSend(&pPak);

	pFileBeingSent->iBytesAlreadySent += iDataSize;
	if (pFileBeingSent->iBytesAlreadySent == pFileBeingSent->iFileSize)
	{
		stashIntRemovePointer(pManager->sFilesBeingSentByHandle, iHandle, NULL);
		StructDestroy(parse_LFSM_FileBeingSent, pFileBeingSent);
	}

	return true;
}



void linkFileSendingMode_CancelSend(NetLink *pLink, int iHandle)
{
	LFSM_FileBeingSent *pFileBeingSent;
	LinkFileSendingMode_SendManager *pManager;
	Packet *pPak;

	if (!(pLink && pLink->pLinkFileSendingMode_SendManager && pLink->connected && !pLink->disconnected))
	{
		return;
	}

	pManager = pLink->pLinkFileSendingMode_SendManager;
	
	if (!stashIntRemovePointer(pManager->sFilesBeingSentByHandle, iHandle, &pFileBeingSent))
	{
		return;
	}

	pPak = pktCreate(pLink, SHAREDCMD_FILESENDINGMODE);
	pktSendBits(pPak, 32, LINKFILESENDINGMODE_CMD_CANCELSEND);
	pktSendBits(pPak, 32, iHandle);
	pktSend(&pPak);

	StructDestroy(parse_LFSM_FileBeingSent, pFileBeingSent);
}

static void RECEIVE_ERROR(NetLink *pLink, FORMAT_STR const char *pFmt, ...)
{
	char *pFullStr = NULL;

	if (!(pLink && pLink->pLinkFileSendingMode_ReceiveManager))
	{
		return;
	}

	estrGetVarArgs(&pFullStr, pFmt);

	pLink->pLinkFileSendingMode_ReceiveManager->pErrorCB(pFullStr);
	estrDestroy(&pFullStr);
}

typedef void (*linkFileSendingMode_ReceiveCB)(int iCmd, char *pFileName);
void linkFileSendingMode_InitReceiving(NetLink *pLink, linkFileReceivingMode_ErrorCB pErrorCB)
{
	if (pLink->pLinkFileSendingMode_ReceiveManager)
	{
		return;
	}

	pLink->pLinkFileSendingMode_ReceiveManager  = linkFileSendingMode_CreateReceiveManager(pErrorCB);
}

void linkFileSendingMode_RegisterCallback(NetLink *pLink, int iCmd, const char *pRootDir, linkFileSendingMode_ReceiveCB pCB)
{
	LinkFileSendingMode_ReceiveManager *pManager;
	LFSM_ReceiveCommand *pCommand;

	if (!(pLink && pLink->pLinkFileSendingMode_ReceiveManager))
	{
		return;
	}

	pManager = pLink->pLinkFileSendingMode_ReceiveManager;
	if (stashIntFindPointer(pManager->sReceiveCommands, iCmd, NULL))
	{
		RECEIVE_ERROR(pLink, "Command %d being registered twice", iCmd);
		return;
	}

	pCommand = StructCreate(parse_LFSM_ReceiveCommand);
	pCommand->iCmd = iCmd;
	pCommand->pRootDir = strdup(pRootDir);
	pCommand->pCB = pCB;

	stashIntAddPointer(pManager->sReceiveCommands, iCmd, pCommand, false);
}

/*
	pktSendBits(pPak, 32, pFileBeingSent->iHandle);
	pktSendBits(pPak, 32, pFileBeingSent->iCmd);
	pktSendString(pPak, pFileBeingSent->pRemoteFileName);
	pktSendBits64(pPak, 64, pFileBeingSent->iFileSize);*/
static void linkFileSendingMode_HandleBeginSend(NetLink *pLink, Packet *pPak)
{
	int iHandle = pktGetBits(pPak, 32);
	int iCmd = pktGetBits(pPak, 32);
	char *pFileName = pktGetStringTemp(pPak);
	S64 iFileSize = pktGetBits64(pPak, 64);

	LinkFileSendingMode_ReceiveManager *pManager;
	LFSM_ReceiveCommand *pReceiveCommand = NULL;
	LFSM_FileBeingReceived *pFileBeingReceived;
	FILE *pFileOnDisk;

	char fullFileName[CRYPTIC_MAX_PATH];


	if (!(pLink && pLink->pLinkFileSendingMode_ReceiveManager))
	{
		return;
	}

	pManager = pLink->pLinkFileSendingMode_ReceiveManager;
	if (stashIntFindPointer(pManager->sFilesBeingReceived, iHandle, NULL))
	{
		RECEIVE_ERROR(pLink, "Asked to receive file %s with cmd %d, handle %d, but already receiving a file with that handle",
			pFileName, iCmd, iHandle);
		return;
	}


	if (!stashIntFindPointer(pManager->sReceiveCommands, iCmd, &pReceiveCommand))
	{
		RECEIVE_ERROR(pLink, "Asked to receive a file with cmd %d, which is not registered",
			iCmd);
		return;
	}


	if (pReceiveCommand->pRootDir && pReceiveCommand->pRootDir[0])
	{
		sprintf(fullFileName, "%s/%s", pReceiveCommand->pRootDir, pFileName);
	}
	else
	{
		strcpy(fullFileName, pFileName);
	}

	mkdirtree_const(fullFileName);
	pFileOnDisk = fopen(fullFileName, "wb");
	if (!pFileOnDisk)
	{
		RECEIVE_ERROR(pLink, "Unable to open file %s for writing", fullFileName);
		return;
	}

	pFileBeingReceived = StructCreate(parse_LFSM_FileBeingReceived);
	pFileBeingReceived->iCmd = iCmd;
	pFileBeingReceived->iExpectedSize = iFileSize;
	pFileBeingReceived->iHandle = iHandle;
	pFileBeingReceived->pFileName = strdup(pFileName);
	pFileBeingReceived->pFile = pFileOnDisk;
	pFileBeingReceived->pReceiveCommand = pReceiveCommand;

	stashIntAddPointer(pManager->sFilesBeingReceived, iHandle, pFileBeingReceived, false);
}



static void CancelReceiveByHandle(LinkFileSendingMode_ReceiveManager *pManager, int iHandle)
{
	LFSM_FileBeingReceived *pFileBeingReceived;

	if (!stashIntRemovePointer(pManager->sFilesBeingReceived, iHandle, &pFileBeingReceived))
	{
		return;
	}

	StructDestroy(parse_LFSM_FileBeingReceived, pFileBeingReceived);
}

/*
	pktSendBits(pPak, 32, iHandle);
	pktSendBits64(pPak, 64, iDataSize);
	pktSendBytes(pPak, iDataSize, pData);
	*/
static void linkFileSendingMode_HandleUpdateSend(NetLink *pLink, Packet *pPak)
{
	int iHandle = pktGetBits(pPak, 32);
	S64 iDataSize = pktGetBits64(pPak, 64);
	S64 fwriteResult = 0; // should be size_t, but that causes issues

	LinkFileSendingMode_ReceiveManager *pManager;
	LFSM_FileBeingReceived *pFileBeingReceived;
	void *pBytes;

	if (!(pLink && pLink->pLinkFileSendingMode_ReceiveManager))
	{
		return;
	}

	pManager = pLink->pLinkFileSendingMode_ReceiveManager;
	if (!stashIntFindPointer(pManager->sFilesBeingReceived, iHandle, &pFileBeingReceived))
	{
		RECEIVE_ERROR(pLink, "Sent file update for file with handle %d, unknown file",
			iHandle);
		return;
	}

	if (pFileBeingReceived->iSizeReceived + iDataSize > pFileBeingReceived->iExpectedSize)
	{
		RECEIVE_ERROR(pLink, "Got file size overflow while receiving file %s", 
			pFileBeingReceived->pFileName);

		CancelReceiveByHandle(pManager, iHandle);
		return;
	}

	if (!iDataSize)
	{
		return;
	}

	pBytes = pktGetBytesTemp(pPak, iDataSize);

	fwriteResult = fwrite(pBytes, iDataSize, 1, pFileBeingReceived->pFile);
	if (fwriteResult != 1)
	{
		RECEIVE_ERROR(pLink, "Error during fwrite for file %s; fwrite = %"FORM_LL"d, errno = %d", pFileBeingReceived->pFileName, fwriteResult, errno);
		CancelReceiveByHandle(pManager, iHandle);
		return;
	}

	pFileBeingReceived->iSizeReceived += iDataSize;
	if (pFileBeingReceived->iSizeReceived == pFileBeingReceived->iExpectedSize)
	{
		pFileBeingReceived->pReceiveCommand->pCB(pFileBeingReceived->iCmd, pFileBeingReceived->pFileName);

		CancelReceiveByHandle(pManager, iHandle);
	}
}

void linkFileSendingMode_HandleCheckForFileExistence(NetLink *pLink, Packet *pPak)
{
	char *pLocalFile = pktGetStringTemp(pPak);
	U32 iUID = pktGetBits(pPak, 32);
	U32 iCRC = pktGetBits(pPak, 32);
	Packet *pReturnPak = pktCreate(pLink, SHAREDCMD_FILESENDINGMODE);
	pktSendBits(pReturnPak, 32, LINKFILESENDINGMODE_CMD_FILE_EXISTENCE_REPORT);
	pktSendBits(pReturnPak, 32, iUID);

	if (iCRC == cryptAdlerFile(pLocalFile))
	{
		pktSendBits(pReturnPak, 1, 1);
	}
	else
	{
		pktSendBits(pReturnPak, 1, 0);
	}

	pktSend(&pReturnPak);
}


void linkFileSendingMode_HandleFileExistenceReport(NetLink *pLink, Packet *pPak)
{
	LinkFileSendingMode_SendManager *pManager;
	U32 iUID = pktGetBits(pPak, 32);
	bool bAlreadyExists = pktGetBits(pPak, 1);
	LinkFileSendingMode_ManagedFileSend *pManagedSend;
	
	if (!(pManager = pLink->pLinkFileSendingMode_SendManager))
	{
		return;
	}

	if (!eaSize(&pManager->ppManagedFileSends))
	{
		return;
	}

	pManagedSend = pManager->ppManagedFileSends[0];

	if (pManagedSend->iUID == iUID)
	{
		if (bAlreadyExists)
		{
			//the file already exists... so we just proceed as if the file send succeeded
			if (pManagedSend->pSuccessPacket)
			{
				pktSend(&pManagedSend->pSuccessPacket);
			}

			if (pManagedSend->pSuccessCB)
			{
				pManagedSend->pSuccessCB(pManagedSend->pLocalFileName, "Success", pManagedSend->pSuccessCBUserData);
			}

			StructDestroy(parse_LinkFileSendingMode_ManagedFileSend, pManagedSend);
			eaRemove(&pManager->ppManagedFileSends, 0);
		}
		else
		{
			//the file doesn't already exist... need to ACTUALLY start the send now
			pManagedSend->pFile = fopen(pManagedSend->pLocalFileName, "rb");
			if (!pManagedSend->pFile)
			{
				linkFileSendingMode_FailManagedSend(pLink, pManagedSend, "Couldn't open file for reading after doing CRC handshake");
			}
			else
			{
				fseek(pManagedSend->pFile, 0, SEEK_END);
				pManagedSend->iFileSize = ftell(pManagedSend->pFile);
				fseek(pManagedSend->pFile, 0, SEEK_SET);
				pManagedSend->iHandle = linkFileSendingMode_BeginSendingFile(pLink, pManagedSend->iCmd, pManagedSend->pRemoteFileName, pManagedSend->iFileSize);
			}
		}
	}
}

static void linkFileSendingMode_HandleCancelSend(NetLink *pLink, Packet *pPak)
{
	LinkFileSendingMode_ReceiveManager *pManager;

	if (!(pLink && pLink->pLinkFileSendingMode_ReceiveManager))
	{
		return;
	}

	pManager = pLink->pLinkFileSendingMode_ReceiveManager;

	CancelReceiveByHandle(pManager, pktGetBits(pPak, 32));
}

//returns true if it "stole" the packet, so you can exit from the receieve cmd function
bool linkFileSendingMode_ReceiveHelper(NetLink *pLink, int iMainCmd, Packet *pPak)
{
	int iInnerCmd;

	if (iMainCmd != SHAREDCMD_FILESENDINGMODE)
	{
		return false;
	}

	iInnerCmd = pktGetBits(pPak, 32);

	switch (iInnerCmd)
	{
	xcase LINKFILESENDINGMODE_CMD_BEGINSEND:
		linkFileSendingMode_HandleBeginSend(pLink, pPak);
			

	xcase LINKFILESENDINGMODE_CMD_UPDATESEND:
		linkFileSendingMode_HandleUpdateSend(pLink, pPak);
		
		
	xcase LINKFILESENDINGMODE_CMD_CANCELSEND:
		linkFileSendingMode_HandleCancelSend(pLink, pPak);

	xcase LINKFILESENDINGMODE_CMD_CHECK_FOR_FILE_EXISTENCE:
		linkFileSendingMode_HandleCheckForFileExistence(pLink, pPak);

	xcase LINKFILESENDINGMODE_CMD_FILE_EXISTENCE_REPORT:
		linkFileSendingMode_HandleFileExistenceReport(pLink, pPak);

	xdefault:
		RECEIVE_ERROR(pLink, "Received internal LFSM command %d, this is unrecognized", 
			iInnerCmd);

	}

	return true;
}

#define LFSM_SENDFILE_BUFSIZE (64 * 1024)
#define LFSM_DEFAULT_BYTES_PER_TICK (64 * 1024)

bool linkFileSendingMode_SendFileBlocking(NetLink *pLink, int iCmd, char *pLocalFileName, char *pRemoteFileName)
{
	FILE *pFile;
	S64 iSize;
	int iHandle;
	char buf[LFSM_SENDFILE_BUFSIZE];
	S64 iBytesToRead;

	pFile = fopen(pLocalFileName, "rb");

	if (!pFile)
	{
		return false;
	}

	fseek(pFile, 0, SEEK_END);
	iSize = ftell(pFile);
	fseek(pFile, 0, SEEK_SET);

	iHandle = linkFileSendingMode_BeginSendingFile(pLink, iCmd, pRemoteFileName, iSize);

	while (iSize)
	{
		
		iBytesToRead = LFSM_SENDFILE_BUFSIZE;
		if (iBytesToRead > iSize)
		{
			iBytesToRead = iSize;
		}

		if ((S64)fread(buf, 1, iBytesToRead, pFile) != iBytesToRead)
		{
			linkFileSendingMode_CancelSend(pLink, iHandle);
			fclose(pFile);
			return false;
		}

		linkFileSendingMode_SendBytes(pLink, iHandle, buf, iBytesToRead);
		iSize -= iBytesToRead;
	}

	fclose(pFile);
	return true;
}


bool linkFileSendingMode_linkHasActiveManagedFileSend(NetLink *pLink)
{
	if (!pLink)
	{
		return false;
	}

	if (!pLink->pLinkFileSendingMode_SendManager)
	{
		return false;
	}

	return !!eaSize(&pLink->pLinkFileSendingMode_SendManager->ppManagedFileSends);
}

bool linkFileSendingMode_SendManagedFile(NetLink *pLink, int iCmd, ManagedSendFlags eFlags, char *pLocalFileName, char *pRemoteFileName, U32 iBytesPerTick, 
	ManagedFileSendCB pErrorCB, void *pErrorCBUserData,
	ManagedFileSendCB pSuccessCB, void *pSuccessCBUserData, 
	Packet *pSuccessPacket)
{
	FILE *pFile = NULL;
	LinkFileSendingMode_ManagedFileSend *pManagedSend;
	LinkFileSendingMode_SendManager *pManager;
	static U32 siNextUID;
	U32 iCRC = 0;

	if (!(pLink && pLink->connected && !pLink->disconnected && pLink->pLinkFileSendingMode_SendManager))
	{
		if (pErrorCB)
		{
			pErrorCB(pLocalFileName, "Link nonexistant or disconnected or not initted for file sending mode", pErrorCBUserData);
		}
		pktFree(&pSuccessPacket);
		return false;
	}

	pManager = pLink->pLinkFileSendingMode_SendManager;

	if (eFlags & SENDMANAGED_CHECK_FOR_EXISTENCE)
	{
		iCRC = cryptAdlerFile(pLocalFileName);
		if (!iCRC)
		{
			if (pErrorCB)
			{
				pErrorCB(pLocalFileName, "Couldn't open file", pErrorCBUserData);
			}
			pktFree(&pSuccessPacket);
			return false;
		}
	
	}
	else
	{
		pFile = fopen(pLocalFileName, "rb");
		if (!pFile)
		{
			if (pErrorCB)
			{
				pErrorCB(pLocalFileName, "Couldn't open file", pErrorCBUserData);
			}
			pktFree(&pSuccessPacket);
			return false;
		}
	}

	pManagedSend = StructCreate(parse_LinkFileSendingMode_ManagedFileSend);
	pManagedSend->pLocalFileName = strdup(pLocalFileName);
	pManagedSend->pRemoteFileName = strdup(pRemoteFileName);
	pManagedSend->iBytesPerTick = iBytesPerTick ? iBytesPerTick : LFSM_DEFAULT_BYTES_PER_TICK;
	pManagedSend->pSuccessPacket = pSuccessPacket;
	pManagedSend->pErrorCB = pErrorCB;
	pManagedSend->pSuccessCB = pSuccessCB;
	pManagedSend->pErrorCBUserData = pErrorCBUserData;
	pManagedSend->pSuccessCBUserData = pSuccessCBUserData;
	pManagedSend->pSuccessPacket = pSuccessPacket;
	pManagedSend->pLinkName = strdup(linkDebugName(pLink));
	pManagedSend->eFlags = eFlags;
	pManagedSend->iUID = siNextUID++;
	pManagedSend->iCmd = iCmd;


	if (eFlags & SENDMANAGED_CHECK_FOR_EXISTENCE)
	{
		Packet *pPak = pktCreate(pLink, SHAREDCMD_FILESENDINGMODE);
		pktSendBits(pPak, 32, LINKFILESENDINGMODE_CMD_CHECK_FOR_FILE_EXISTENCE);
		pktSendString(pPak, pRemoteFileName);
		pktSendBits(pPak, 32, pManagedSend->iUID);
		pktSendBits(pPak, 32, iCRC);
		pktSend(&pPak);
	}
	else
	{
		fseek(pFile, 0, SEEK_END);
		pManagedSend->iFileSize = ftell(pFile);
		fseek(pFile, 0, SEEK_SET);
		pManagedSend->iHandle = linkFileSendingMode_BeginSendingFile(pLink, iCmd, pRemoteFileName, pManagedSend->iFileSize);
		pManagedSend->pFile = pFile;
	}

	eaPush(&pManager->ppManagedFileSends, pManagedSend);

	eaPush(&sppAllManagedFileSends, pManagedSend);

	return true;
}

void linkFileSendingMode_FailManagedSend(NetLink *pLink, LinkFileSendingMode_ManagedFileSend *pManagedSend, FORMAT_STR const char* format, ...)
{
	char *pFullErrorString = NULL;
	estrGetVarArgs(&pFullErrorString, format);

	if (pManagedSend->pFile)
	{
		linkFileSendingMode_CancelSend(pLink, pManagedSend->iHandle);
	}

	if (pManagedSend->pErrorCB)
	{
		pManagedSend->pErrorCB(pManagedSend->pLocalFileName, pFullErrorString, pManagedSend->pErrorCBUserData);
	}

	eaFindAndRemove(&pLink->pLinkFileSendingMode_SendManager->ppManagedFileSends, pManagedSend);
	StructDestroy(parse_LinkFileSendingMode_ManagedFileSend, pManagedSend);
}

void linkFileSendingMode_Tick(NetLink *pLink)
{
	LinkFileSendingMode_ManagedFileSend *pManagedSend;
	LinkFileSendingMode_SendManager *pManager;
	S64 iBytesToSend;
	char *pBuf;

	if (!(pLink && pLink->connected && !pLink->disconnected && pLink->pLinkFileSendingMode_SendManager))
	{
		return;
	}

	pManager = pLink->pLinkFileSendingMode_SendManager;

	if (!eaSize(&pManager->ppManagedFileSends))
	{
		return;
	}

	pManagedSend = pManager->ppManagedFileSends[0];

	//if this flag is set, then we've sent LINKFILESENDINGMODE_CMD_CHECK_FOR_FILE_EXISTENCE
	//already, waiting to get LINKFILESENDINGMODE_CMD_FILE_EXISTENCE_REPORT back
	if (pManagedSend->eFlags & SENDMANAGED_CHECK_FOR_EXISTENCE)
	{
		return;
	}

	iBytesToSend = MIN(pManagedSend->iBytesPerTick, pManagedSend->iFileSize - pManagedSend->iBytesSent);

	pBuf = ScratchAlloc(iBytesToSend);

	if ((S64)fread(pBuf, 1, iBytesToSend, pManagedSend->pFile) != iBytesToSend)
	{
		linkFileSendingMode_FailManagedSend(pLink, pManagedSend, "Unable to read %d bytes from file", (int)iBytesToSend);
		ScratchFree(pBuf);
		return;
	}

	if (!linkFileSendingMode_SendBytes(pLink, pManagedSend->iHandle, pBuf, iBytesToSend))
	{
		linkFileSendingMode_FailManagedSend(pLink, pManagedSend, "Unable to send %d bytes", (int)iBytesToSend);
		ScratchFree(pBuf);
		return;
	}

	ScratchFree(pBuf);
	pManagedSend->iBytesSent += iBytesToSend;
	if (pManagedSend->iBytesSent == pManagedSend->iFileSize)
	{
		if (pManagedSend->pSuccessPacket)
		{
			pktSend(&pManagedSend->pSuccessPacket);
		}

		if (pManagedSend->pSuccessCB)
		{
			pManagedSend->pSuccessCB(pManagedSend->pLocalFileName, "Success", pManagedSend->pSuccessCBUserData);
		}

		fclose(pManagedSend->pFile);
		pManagedSend->pFile = NULL;
		StructDestroy(parse_LinkFileSendingMode_ManagedFileSend, pManagedSend);
		eaRemove(&pManager->ppManagedFileSends, 0);
	}
}

void linkFileSendingMode_Receive_Disconnect(NetLink *pLink)
{
	if (pLink->pLinkFileSendingMode_ReceiveManager)
	{
		stashTableClearStruct(pLink->pLinkFileSendingMode_ReceiveManager->sFilesBeingReceived, NULL, parse_LFSM_FileBeingReceived);	
	}
}
void linkFileSendingMode_Send_Disconnect(NetLink *pLink)
{
	LinkFileSendingMode_SendManager *pManager = pLink->pLinkFileSendingMode_SendManager;
	char *pDisconnectReason = NULL;
	linkGetDisconnectReason(pLink, &pDisconnectReason);

	while (eaSize(&pManager->ppManagedFileSends))
	{
		linkFileSendingMode_FailManagedSend(pLink, pManager->ppManagedFileSends[0], "Link disconnected mid-send due to %s", pDisconnectReason);
	}

	estrDestroy(&pDisconnectReason);
}


typedef struct MultipleManagedFileSend
{
	char *pLinkName;
	char **ppRemainingFileNames;
	ManagedFileSendCB pErrorCB;
	void *pErrorCBUserData; 
	ManagedFileSendCB pSuccessCB;
	void *pSuccessCBUserData; 
	Packet *pSuccessPacket; 
	NetLink *pLink; 
	int iCmd;
	ManagedSendFlags eFlags;
	U32 iBytesPerTick;
} MultipleManagedFileSend;

static MultipleManagedFileSend **sppAllMultipleSends = NULL;

void DestroyMultipleSend(MultipleManagedFileSend *pMultipleSend)
{
	eaDestroyEx(&pMultipleSend->ppRemainingFileNames, NULL);
	if (pMultipleSend->pSuccessPacket)
	{
		pktFree(&pMultipleSend->pSuccessPacket);
	}

	eaFindAndRemove(&sppAllMultipleSends, pMultipleSend);
	free(pMultipleSend);

}

void MultipleFileErrorCB(char *pFileName, char *pErrorString, MultipleManagedFileSend *pMultipleSend)
{
	if (pMultipleSend->pErrorCB)
	{
		pMultipleSend->pErrorCB(pFileName, pErrorString, pMultipleSend->pErrorCBUserData);
	}

	DestroyMultipleSend(pMultipleSend);
}

void MultipleFileSuccessCB(char *pFileName, char *pErrorString, MultipleManagedFileSend *pMultipleSend)
{
	int iNumRemainingFiles = eaSize(&pMultipleSend->ppRemainingFileNames);
	char *pLocalFile;
	char *pRemoteFile;

	if (iNumRemainingFiles == 2)
	{
		linkFileSendingMode_SendManagedFile(pMultipleSend->pLink, pMultipleSend->iCmd, pMultipleSend->eFlags,
			pMultipleSend->ppRemainingFileNames[0], pMultipleSend->ppRemainingFileNames[1],
			pMultipleSend->iBytesPerTick, pMultipleSend->pErrorCB, pMultipleSend->pErrorCBUserData, 
			pMultipleSend->pSuccessCB, pMultipleSend->pSuccessCBUserData, pMultipleSend->pSuccessPacket);

		//linkFileSendingMode_SendManagedFile takes ownership of the packet
		pMultipleSend->pSuccessPacket = NULL;

		DestroyMultipleSend(pMultipleSend);
		return;
	}

	pLocalFile = eaRemove(&pMultipleSend->ppRemainingFileNames, 0);
	pRemoteFile = eaRemove(&pMultipleSend->ppRemainingFileNames, 0);

	linkFileSendingMode_SendManagedFile(pMultipleSend->pLink, pMultipleSend->iCmd, pMultipleSend->eFlags, pLocalFile, pRemoteFile,
		pMultipleSend->iBytesPerTick, MultipleFileErrorCB, pMultipleSend, MultipleFileSuccessCB, pMultipleSend, NULL);

	free(pLocalFile);
	free(pRemoteFile);
}



bool linkFileSendingMode_SendMultipleManagedFiles(NetLink *pLink, int iCmd, ManagedSendFlags eFlags,

	//must have an even size... [0] is a local filename, [1] the accompanying remote, [2] local, [3] remote, etc.
	char **ppLocalAndRemoteFileNames, 
	
	U32 iBytesPerTick, 
	ManagedFileSendCB pErrorCB, void *pErrorCBUserData,
	ManagedFileSendCB pSuccessCB, void *pSuccessCBUserData, 
	Packet *pSuccessPacket)
{
	MultipleManagedFileSend *pMultipleSend;
	int iNumFileNames = eaSize(&ppLocalAndRemoteFileNames);
	bool bRetVal;

	if (iNumFileNames % 2 || !iNumFileNames)
	{
		if (pErrorCB)
		{
			pErrorCB(ppLocalAndRemoteFileNames[0], "Must pass in a positive even number of filenames", pErrorCBUserData);
		}
		return false;
	}

	if (iNumFileNames == 2)
	{
		return linkFileSendingMode_SendManagedFile(pLink, iCmd, eFlags, ppLocalAndRemoteFileNames[0], ppLocalAndRemoteFileNames[1],
			iBytesPerTick, pErrorCB, pErrorCBUserData, pSuccessCB, pSuccessCBUserData, pSuccessPacket);
	}

	pMultipleSend = calloc(sizeof(MultipleManagedFileSend), 1);
	pMultipleSend->pErrorCB = pErrorCB;
	pMultipleSend->pErrorCBUserData = pErrorCBUserData;
	pMultipleSend->pSuccessCB = pSuccessCB;
	pMultipleSend->pSuccessCBUserData = pSuccessCBUserData;
	pMultipleSend->pLink = pLink;
	pMultipleSend->iCmd = iCmd;
	pMultipleSend->eFlags = eFlags;
	pMultipleSend->iBytesPerTick = iBytesPerTick;
	pMultipleSend->pSuccessPacket = pSuccessPacket;
	pMultipleSend->pLinkName = linkDebugName(pLink);
	eaPush(&sppAllMultipleSends, pMultipleSend);

	bRetVal = linkFileSendingMode_SendManagedFile(pLink, iCmd, eFlags, ppLocalAndRemoteFileNames[0], ppLocalAndRemoteFileNames[1],
		iBytesPerTick, MultipleFileErrorCB, pMultipleSend, MultipleFileSuccessCB, pMultipleSend, NULL);

	if (!bRetVal)
	{
		//do nothing, our error CB has already destroyed the manager
	}
	else
	{
		int i;
		for (i = 2; i < iNumFileNames; i++)
		{
			eaPush(&pMultipleSend->ppRemainingFileNames, strdup(ppLocalAndRemoteFileNames[i]));
		}
	}

	return bRetVal;
}

//LinkFileSendingMode_ManagedFileSend **sppAllManagedFileSends
//static MultipleManagedFileSend **sppAllMultipleSends = NULL;
char *netGetFileSendingSummaryString(void)
{
	static char *spRetVal = NULL;
	estrClear(&spRetVal);

	FOR_EACH_IN_EARRAY(sppAllManagedFileSends, LinkFileSendingMode_ManagedFileSend, pSend)
	{
		static char *pSentString = NULL;
		static char *pSizeString = NULL;

		estrMakePrettyBytesString(&pSentString, pSend->iBytesSent);
		estrMakePrettyBytesString(&pSizeString, pSend->iFileSize);
		estrConcatf(&spRetVal, "On %s, sending %s. %s/%s\n", pSend->pLinkName, pSend->pLocalFileName,
			pSentString, pSizeString);
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(sppAllMultipleSends, MultipleManagedFileSend, pMultipleSend)
	{
		int i;

		for (i = 0; i < eaSize(&pMultipleSend->ppRemainingFileNames); i+=2)
		{
			estrConcatf(&spRetVal, "On %s, still waiting to send %s\n",
				pMultipleSend->pLinkName, pMultipleSend->ppRemainingFileNames[i]);
		}
	}
	FOR_EACH_END;


	if (estrLength(&spRetVal) == 0)
	{
		return "No files being sent";
	}

	return spRetVal;
}

#include "netFileSendingMode_c_ast.c"

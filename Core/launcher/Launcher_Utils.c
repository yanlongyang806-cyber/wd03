#include "Launcher_Utils.h"
#include "Launcher_Utils_h_ast.h"
#include "../../Controller/pub/ControllerPub.h"
#include "StashTable.h"
#include "crypt.h"
#include "error.h"
#include "net.h"
#include "GlobalComm.h"
#include "ControllerLink.h"
#include "utils/zutils.h"
#include "file.h"
#include "Launcher.h"


void HandleControllerFileRequestFulfilledCB(bool bSucceeded, void *pZippedData, int iZippedSize, void *pUnzipBuffer, int iUnzipBufferSize, int iBytesUnzipped,
	void *pUserData);

StashTable sCRCsByFileName = NULL;

bool FileExistsAndMatchesCRC(char *pFileName, U32 iCRC)
{
	char tempFileName[CRYPTIC_MAX_PATH];
	char tempFileName2[CRYPTIC_MAX_PATH];
	char *pTemp;
	U32 iFoundCRC;

	if (!sCRCsByFileName)
	{
		sCRCsByFileName = stashTableCreateWithStringKeys(16, StashDeepCopyKeys);
	}

	strcpy(tempFileName, pFileName);
	pTemp = strchr(tempFileName, ' ');
	if (pTemp)
	{
		*pTemp = 0;
	}

	if (stashFindInt(sCRCsByFileName, tempFileName, &iFoundCRC))
	{
		if (iFoundCRC == iCRC)
		{
			return true;
		}

		return false;
	}

	if (fileIsAbsolutePath(tempFileName))
	{
		strcpy(tempFileName2, tempFileName);
	}
	else
	{
		sprintf(tempFileName2, "./%s", tempFileName);
	}

	iFoundCRC = cryptAdlerFile(tempFileName2);

	if (iFoundCRC)
	{
		stashAddInt(sCRCsByFileName, tempFileName, iFoundCRC, false);
	}

	return iFoundCRC == iCRC;
}
	
StashTable sControllerFileRequestsByFileName = NULL;

CachedControllerFileRequest *GetControllerFileRequest(char *pFileName, U32 iCRC)
{
	char tempFileName[CRYPTIC_MAX_PATH];
	char *pTemp;
	CachedControllerFileRequest *pRequest;
	Packet *pOutPack;

	if (!sControllerFileRequestsByFileName)
	{
		sControllerFileRequestsByFileName = stashTableCreateWithStringKeys(16, StashDefault);
	}

	strcpy(tempFileName, pFileName);
	pTemp = strchr(tempFileName, ' ');
	if (pTemp)
	{
		*pTemp = 0;
	}

	if (stashFindPointer(sControllerFileRequestsByFileName, tempFileName, &pRequest))
	{
		if (pRequest->iCRC != iCRC)
		{
			AssertOrAlert("CRC_MISMATCH", "Launcher has gotten two launch requests for %s from the controller with two different CRCs. This is illegal and will cause the current launch request to fail",
				tempFileName);
			return NULL;
		}

		return pRequest;
	}

	pRequest = StructCreate(parse_CachedControllerFileRequest);
	pRequest->pFileName = strdup(tempFileName);
	pRequest->iCRC = iCRC;
	stashAddPointer(sControllerFileRequestsByFileName, pRequest->pFileName, pRequest, false);

	pOutPack = pktCreate(GetControllerLink(), TO_CONTROLLER_LAUNCHER_REQUESTING_EXE_FROM_NAME_AND_CRC);
	pktSendString(pOutPack, pRequest->pFileName);
	pktSendBits(pOutPack, 32, pRequest->iCRC);
	pktSend(&pOutPack);

	return pRequest;
}

AUTO_FIXUPFUNC;
TextParserResult fixupCachedControllerFileRequest(CachedControllerFileRequest* pRequest, enumTextParserFixupType eFixupType, void *pExtraData)
{
	switch (eFixupType)
	{
		case FIXUPTYPE_DESTRUCTOR:
		{
			SAFE_FREE(pRequest->pZippedBuffer);
			SAFE_FREE(pRequest->pUnzippedBuffer);
			break;
		}
	}
	return 1;
}


void HandleControllerFileRequestFulfilled(NetLink *pLink, Packet *pPack)
{
	U32 iSendID = pktGetBits(pPack, 32);
	char *pFileName = pktGetStringTemp(pPack);
	U32 iCRC = pktGetBits(pPack, 32);
	U32 iOriginalSize = pktGetBits(pPack, 32);
	U32 iZippedSize = pktGetBits(pPack, 32);
	U32 iAmountAlreadySent = pktGetBits(pPack, 32);
	U32 iAmountBeingSentThisPacket = pktGetBits(pPack, 32);


	CachedControllerFileRequest *pRequest = NULL;

	Packet *pHandshakePak = pktCreate(pLink, TO_CONTROLLER_FILE_SERVING_HANDSHAKE);
	pktSendBits(pHandshakePak, 32, iSendID);
	pktSend(&pHandshakePak);

	if (!stashFindPointer(sControllerFileRequestsByFileName, pFileName, &pRequest))
	{
		pRequest = StructCreate(parse_CachedControllerFileRequest);
		pRequest->pFileName = strdup(pFileName);
		pRequest->iCRC = iCRC;
		pRequest->iZippedBufferSize = iZippedSize;
		pRequest->pZippedBuffer = malloc(pRequest->iZippedBufferSize);
		stashAddPointer(sControllerFileRequestsByFileName, pRequest->pFileName, pRequest, true);
	}
	
	if (pRequest->pUnzippedBuffer)
	{
		AssertOrAlert("FILE_SEND_OUT_OF_SYNC", "The controller is sending us more of %s which we've already fully received and are unzipping... something is wrong",
			pFileName);
		return;
	}

	printf("Controller just sent us %d bytes of %s... we have already received %d bytes of it\n",
		iAmountBeingSentThisPacket, pFileName, pRequest->iBytesAlreadyReceived);

	if (pRequest && pRequest->iCRC != iCRC)
	{
		AssertOrAlert("CRC_MISMATCH", "Launcher got an executable named %s from the controller with the wrong CRC. Cached launches will fail",
			pFileName);

		stashRemovePointer(sControllerFileRequestsByFileName, pFileName, NULL);
		StructDestroySafe(parse_CachedControllerFileRequest, &pRequest);
		return;
	}
		
	if (pRequest->iBytesAlreadyReceived != iAmountAlreadySent)
	{
		AssertOrAlert("FILE_SEND_OUT_OF_SYNC", "The controller thinks it send us %d bytes of %s, we think it sent us %d bytes",
			iAmountAlreadySent, pFileName, pRequest->iBytesAlreadyReceived);

		stashRemovePointer(sControllerFileRequestsByFileName, pFileName, NULL);
		StructDestroySafe(parse_CachedControllerFileRequest, &pRequest);
		return;
	}

	pRequest->iZippedBufferSize = iZippedSize;

	if (!pRequest->pZippedBuffer)
	{
		pRequest->pZippedBuffer = malloc(iZippedSize);
	}

	pktGetBytes(pPack, iAmountBeingSentThisPacket, pRequest->pZippedBuffer + iAmountAlreadySent);
	pRequest->iBytesAlreadyReceived += iAmountBeingSentThisPacket;

	if (pRequest->iBytesAlreadyReceived == iZippedSize)
	{
		pRequest->pUnzippedBuffer = malloc(iOriginalSize);
		ThreadedUnzip(pRequest->pZippedBuffer, pRequest->iZippedBufferSize, pRequest->pUnzippedBuffer, iOriginalSize, HandleControllerFileRequestFulfilledCB, pRequest->pFileName);
	}
}


void HandleControllerFileRequestFulfilledCB(bool bSucceeded, void *pZippedData, int iZippedSize, void *pUnzipBuffer, int iUnzipBufferSize, int iBytesUnzipped,
	char *pFileName)
{
	CachedControllerFileRequest *pRequest;
	FILE *pOutFile;

	if (!stashRemovePointer(sControllerFileRequestsByFileName, pFileName, &pRequest))
	{
		AssertOrAlert("FILE_SEND_OUT_OF_SYNC", "The launcher finished unzipping a file it no longer knows about, something is wrong");
		return;
	}


	if (!bSucceeded)
	{
		AssertOrAlert("UNZIP_FAIL", "Launcher got an executable named %s from the controller, but could not unzip it, cached launches will fail",
			pFileName);

		StructDestroySafe(parse_CachedControllerFileRequest, &pRequest);
		return;
	}

	mkdirtree_const(pFileName);

	pOutFile = fopen(pFileName, "wb");
	if (!pOutFile)
	{
		AssertOrAlert("EXE_WRITE_FAIL", "Launcher got an executable named %s from the controller, but could not write it, cached launches will fail",
			pFileName);

		StructDestroySafe(parse_CachedControllerFileRequest, &pRequest);
		return;
	}

	fwrite(pUnzipBuffer, iUnzipBufferSize, 1, pOutFile);
	fclose(pOutFile);


	FOR_EACH_IN_EARRAY_FORWARDS(pRequest->ppPendingLaunchRequests, ServerLaunchRequest, pLaunchRequest)
	{
		StartProcessFromRequest(pLaunchRequest);
	}
	FOR_EACH_END;


	if (!sCRCsByFileName)
	{
		sCRCsByFileName = stashTableCreateWithStringKeys(16, StashDeepCopyKeys);
	}

	stashAddInt(sCRCsByFileName, pFileName, pRequest->iCRC, true);

	StructDestroy(parse_CachedControllerFileRequest, pRequest);
}















#include "Launcher_Utils_h_ast.c"
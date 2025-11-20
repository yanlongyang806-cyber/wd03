#include "TransactionSystem.h"
#include "estring.h"
#include "ThreadSafeMemoryPool.h"

TSMP_DEFINE(TransDataBlock);

AUTO_RUN;
void TransDataBlockInitSystem(void)
{
	TSMP_CREATE(TransDataBlock, 16);
}

//all of these are safe for NULL pBlock
void PutTransDataBlockIntoPacket(Packet *pPack, TransDataBlock *pBlock)
{
	if (!pBlock || TransDataBlockIsEmpty(pBlock))
	{
		pktSendBits(pPack, 1, 0);
		return;
	}

	pktSendBits(pPack, 1, 1);
	pktSendString(pPack, pBlock->pString1);
	pktSendString(pPack, pBlock->pString2);
	pktSendBitsAuto(pPack, pBlock->iDataSize);
	if (pBlock->iDataSize)
	{
		pktSendBytes(pPack, pBlock->iDataSize, pBlock->pData);
	}
}


bool TransDataBlockIsEmpty(TransDataBlock *pBlock)
{
	if(!pBlock)
		return true;

	if (pBlock->iDataSize)
	{
		assert(pBlock->pData);
		return false;
	}
	else
	{
		assert(!pBlock->pData);
	}

	return (pBlock->pString1 == NULL || pBlock->pString1[0] == 0) && (pBlock->pString2 == NULL || pBlock->pString2[0] == 0);
}

//memsets to zero, does NOT free anything already there
void TransDataBlockInit(TransDataBlock *pBlock)
{
	memset(pBlock, 0, sizeof(TransDataBlock));
}

//estrDestroy and the frees the pData, also resets everything to NULL, does NOT 
//free the pBlock itself
void TransDataBlockClear(TransDataBlock *pBlock)
{
	if (!pBlock->bTempStrings)
	{
		estrDestroy(&pBlock->pString1);
		estrDestroy(&pBlock->pString2);
	}

	if (pBlock->iDataSize)
	{
		assert(pBlock->pData);
		free(pBlock->pData);
	}
	else
	{
		assert(!pBlock->pData);
	}

	memset(pBlock, 0, sizeof(TransDataBlock));
}




//same as TransDataBlockClear, then frees the pBlock
void TransDataBlockDestroy(TransDataBlock *pBlock)
{
	if (!pBlock->bTempStrings)
	{
		estrDestroy(&pBlock->pString1);
		estrDestroy(&pBlock->pString2);
	}

	if (pBlock->iDataSize)
	{
		assert(pBlock->pData);
		free(pBlock->pData);
	}
	else
	{
		assert(!pBlock->pData);
	}

	TSMP_FREE(TransDataBlock, pBlock);
}

//returns either NULL or a malloced non-empty data block
TransDataBlock *GetTransDataBlockFromPacket(Packet *pPack)
{
	TransDataBlock *pRetVal;

	if (!pktGetBits(pPack, 1))
	{
		return NULL;
	}

	pRetVal = TSMP_ALLOC(TransDataBlock);
	memset(pRetVal, 0, sizeof(TransDataBlock));

	estrCopyFromPacketNonEmpty(&pRetVal->pString1, pPack);
	estrCopyFromPacketNonEmpty(&pRetVal->pString2, pPack);

	pRetVal->iDataSize = pktGetBitsAuto(pPack);
	if (pRetVal->iDataSize)
	{
		pRetVal->pData = malloc(pRetVal->iDataSize);
		pktGetBytes(pPack, pRetVal->iDataSize, pRetVal->pData);
	}

	return pRetVal;
}


//returns either NULL or a malloced non-empty data block
TransDataBlock *GetTransDataBlockFromPacket_Temp(Packet *pPack)
{
	TransDataBlock *pRetVal;

	if (!pktGetBits(pPack, 1))
	{
		return NULL;
	}

	pRetVal = TSMP_ALLOC(TransDataBlock);
	memset(pRetVal, 0, sizeof(TransDataBlock));

	pRetVal->pString1 = pktGetStringTemp(pPack);
	if (!pRetVal->pString1[0])
	{
		pRetVal->pString1 = NULL;
	}

	pRetVal->pString2 = pktGetStringTemp(pPack);
	if (!pRetVal->pString2[0])
	{
		pRetVal->pString2 = NULL;
	}
	pRetVal->bTempStrings = true;

	pRetVal->iDataSize = pktGetBitsAuto(pPack);
	if (pRetVal->iDataSize)
	{
		pRetVal->pData = malloc(pRetVal->iDataSize);
		pktGetBytes(pPack, pRetVal->iDataSize, pRetVal->pData);
	}

	return pRetVal;
}

void SetTransDataBlockFromPacket(Packet *pPack, TransDataBlock *pBlock)
{


	TransDataBlockClear(pBlock);

	if (!pktGetBits(pPack, 1))
	{
		return;
	}

	estrCopyFromPacketNonEmpty(&pBlock->pString1, pPack);
	estrCopyFromPacketNonEmpty(&pBlock->pString2, pPack);

	pBlock->iDataSize = pktGetBitsAuto(pPack);
	if (pBlock->iDataSize)
	{
		pBlock->pData = malloc(pBlock->iDataSize);
		pktGetBytes(pPack, pBlock->iDataSize, pBlock->pData);
	}
}


//copies the estring and duplicates and re-mallocs the data
void TransDataBlockCopy(TransDataBlock *pDest, TransDataBlock *pSrc)
{
	TransDataBlockClear(pDest);

	estrCopy2(&pDest->pString1, pSrc->pString1);
	estrCopy2(&pDest->pString2, pSrc->pString2);

	if (pSrc->iDataSize)
	{
		pDest->iDataSize = pSrc->iDataSize;
		pDest->pData = malloc(pDest->iDataSize);
		memcpy(pDest->pData, pSrc->pData, pDest->iDataSize);
	}
}


void TransactionReturnVal_VerboseDump(TransactionReturnVal *pRetVal, char **ppOutEString)
{
	int i;
	if (!pRetVal)
	{
		estrConcatf(ppOutEString, "NULL RetVal");
		return;
	}

	estrConcatf(ppOutEString, "Outcome: %s\nTrans name: %s\nID: %u\nFlags:%u\nNumBaseTransactions: %d\n",
		StaticDefineInt_FastIntToString(enumTransactionOutcomeEnum, pRetVal->eOutcome),
		pRetVal->pTransactionName, pRetVal->iID, pRetVal->eFlags, pRetVal->iNumBaseTransactions);

	for (i = 0; i < pRetVal->iNumBaseTransactions; i++)
	{
		estrConcatf(ppOutEString, "BaseTrans %d: (%s) %s\n", i, StaticDefineInt_FastIntToString(enumTransactionOutcomeEnum, pRetVal->pBaseReturnVals[i].eOutcome), pRetVal->pBaseReturnVals[i].returnString);
	}
}
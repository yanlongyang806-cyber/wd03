#include "ByteBlock.h"
#include "ByteBlock_h_ast.h"
#include "timing.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););


int sortByteBlocksByOffset(const ByteBlock **pBlock1, const ByteBlock **pBlock2)
{
	if ((*pBlock1)->iStartByteOffset > (*pBlock2)->iStartByteOffset)
	{
		return 1;
	}
	else if ((*pBlock1)->iStartByteOffset < (*pBlock2)->iStartByteOffset)
	{
		return -1;
	}
	else
	{
		return 0;
	}
}

//only need to sort by byte, not by bit
int sortSingleBits(const SingleBit **pBlock1, const SingleBit **pBlock2)
{
	if ((*pBlock1)->iByteOffset > (*pBlock2)->iByteOffset)
	{
		return 1;
	}
	else if ((*pBlock1)->iByteOffset < (*pBlock2)->iByteOffset)
	{
		return -1;
	}
	else
	{
		return 0;
	}
}

void GroupByteBlocks(ByteBlockList *pList)
{
	int i;

	eaQSort(pList->ppBlocks, sortByteBlocksByOffset);

	
	i = 0; 

	while (i < eaSize(&pList->ppBlocks) - 1)
	{
		if (pList->ppBlocks[i]->iStartByteOffset + pList->ppBlocks[i]->iSize >= pList->ppBlocks[i+1]->iStartByteOffset)
		{
			int iPotentialNewSize = pList->ppBlocks[i+1]->iStartByteOffset - pList->ppBlocks[i]->iStartByteOffset + pList->ppBlocks[i+1]->iSize;
			if (iPotentialNewSize > pList->ppBlocks[i]->iSize)
			{
				pList->ppBlocks[i]->iSize = iPotentialNewSize;
			}

			StructDestroy(parse_ByteBlock, pList->ppBlocks[i+1]);
			eaRemove(&pList->ppBlocks, i+1);
		}
		else
		{
			i++;
		}
	}
}

//only works on grouped lists
void InvertByteBlockList(ByteBlockList *pOutList, ByteBlockList *pInList, int iByteSize)
{
	int iInSize = eaSize(&pInList->ppBlocks);
	int i;

	if (iInSize == 0)
	{
		ByteBlock *pBlock = StructCreate(parse_ByteBlock);
		pBlock->iSize = iByteSize;
		pBlock->iStartByteOffset = 0;
		eaPush(&pOutList->ppBlocks, pBlock);
		return;
	}


	if (pInList->ppBlocks[0]->iStartByteOffset > 0)
	{
		ByteBlock *pBlock = StructCreate(parse_ByteBlock);
		pBlock->iStartByteOffset = 0;
		pBlock->iSize = pInList->ppBlocks[0]->iStartByteOffset;
		eaPush(&pOutList->ppBlocks, pBlock);
	}

	for (i=0; i < iInSize - 1; i++)
	{
		ByteBlock *pBlock = StructCreate(parse_ByteBlock);
		pBlock->iStartByteOffset = pInList->ppBlocks[i]->iStartByteOffset + pInList->ppBlocks[i]->iSize;
		pBlock->iSize = pInList->ppBlocks[i+1]->iStartByteOffset - pBlock->iStartByteOffset;
		eaPush(&pOutList->ppBlocks, pBlock);
	}

	if (pInList->ppBlocks[iInSize-1]->iStartByteOffset + pInList->ppBlocks[iInSize-1]->iSize < iByteSize)
	{
		ByteBlock *pBlock = StructCreate(parse_ByteBlock);
		pBlock->iStartByteOffset = pInList->ppBlocks[iInSize-1]->iStartByteOffset + pInList->ppBlocks[iInSize-1]->iSize;
		pBlock->iSize = iByteSize - pBlock->iStartByteOffset;
		eaPush(&pOutList->ppBlocks, pBlock);
	}
}

void AddByte(ByteBlockList *pList, int iByte)
{
	ByteBlock *pNewBlock = StructCreate(parse_ByteBlock);
	pNewBlock->iStartByteOffset = iByte;
	pNewBlock->iSize = 1;
	eaPush(&pList->ppBlocks, pNewBlock);
}


void CompileByteCopyGroup(ByteCopyGroup *pOutGroup, RawByteCopyGroup *pInGroup)
{
	int i;
	ByteBlockList outBlockList = {0};
	MaskedByte **ppOutMaskedByteList = NULL;

	if (pInGroup->bDefaultToOn)
	{
		//first, for every bit that is off, turn that byte off
		for (i=0; i < eaSize(&pInGroup->ppOffBits); i++)
		{
			AddByte(&pInGroup->offBlocks, pInGroup->ppOffBits[i]->iByteOffset);
		}
	
		//now, the list of bytes to copy is the inverse of the grouped list of bytes NOT to copy
		GroupByteBlocks(&pInGroup->offBlocks);
		InvertByteBlockList(&outBlockList, &pInGroup->offBlocks, pInGroup->iTotalSize);

		//now, sort our off bits so we can group them together into bytes.
		eaQSort(pInGroup->ppOffBits, sortSingleBits);

		//now, walk through the list of bits, making masks
		i = 0;

		while (i < eaSize(&pInGroup->ppOffBits))
		{
			MaskedByte *pMask = StructCreate(parse_MaskedByte);
			pMask->iByteOffset = pInGroup->ppOffBits[i]->iByteOffset;
			pMask->iMask = 0xff;

			while (i < eaSize(&pInGroup->ppOffBits) && pInGroup->ppOffBits[i]->iByteOffset == pMask->iByteOffset)
			{
				pMask->iMask &= ~(1 << (pInGroup->ppOffBits[i]->iBitOffset));
				i++;
			}

			if (pMask->iMask)
			{
				eaPush(&ppOutMaskedByteList, pMask);
			}
			else
			{
				StructDestroy(parse_MaskedByte, pMask);
			}
		}
	}
	else
	{
		//in this case, we ignore all the off bits and off byte groups...
		//first we sort then on bits, then add them ask masks, promoting any full byte to a byte group
		//then we group the on bytes
		eaQSort(pInGroup->ppOnBits, sortSingleBits);

		i = 0;

		while (i < eaSize(&pInGroup->ppOnBits))
		{
			MaskedByte *pMask = StructCreate(parse_MaskedByte);
			pMask->iByteOffset = pInGroup->ppOnBits[i]->iByteOffset;
			pMask->iMask = 0;

			while (i < eaSize(&pInGroup->ppOnBits) && pInGroup->ppOnBits[i]->iByteOffset == pMask->iByteOffset)
			{
				pMask->iMask |= (1 << (pInGroup->ppOnBits[i]->iBitOffset));
				i++;
			}

			if (pMask->iMask == 0xff)
			{
				ByteBlock *pNewBlock = StructCreate(parse_ByteBlock);
				pNewBlock->iStartByteOffset = pMask->iByteOffset;
				pNewBlock->iSize = 1;
				eaPush(&pInGroup->onBlocks.ppBlocks, pNewBlock);
				StructDestroy(parse_MaskedByte, pMask);
			}
			else
			{
				eaPush(&ppOutMaskedByteList, pMask);
			}
		}

		outBlockList.ppBlocks = pInGroup->onBlocks.ppBlocks;
		pInGroup->onBlocks.ppBlocks = NULL;

		GroupByteBlocks(&outBlockList);
	}

	pOutGroup->iNumByteBlocks = eaSize(&outBlockList.ppBlocks);
	if (pOutGroup->iNumByteBlocks)
	{
		pOutGroup->pByteBlocks = calloc(sizeof(ByteBlock) * pOutGroup->iNumByteBlocks, 1);

		for (i=0; i < pOutGroup->iNumByteBlocks; i++)
		{
			memcpy(pOutGroup->pByteBlocks + i, outBlockList.ppBlocks[i], sizeof(ByteBlock));
		}
	}

	pOutGroup->iNumMaskedBytes = eaSize(&ppOutMaskedByteList);
	if (pOutGroup->iNumMaskedBytes)
	{
		pOutGroup->pMaskedBytes = calloc(sizeof(MaskedByte) * pOutGroup->iNumMaskedBytes, 1);
		for (i=0; i < pOutGroup->iNumMaskedBytes; i++)
		{
			memcpy(pOutGroup->pMaskedBytes + i, ppOutMaskedByteList[i], sizeof(MaskedByte));
		}
	}

	eaDestroyStruct(&ppOutMaskedByteList, parse_MaskedByte);
	StructDeInit(parse_ByteBlockList, &outBlockList);
}

void CopyMemoryWithByteCopyGroup(void *pDest, void *pSrc, ByteCopyGroup *pGroup)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	for (i=0; i < pGroup->iNumByteBlocks; i++)
	{
		memcpy_fast(((char*)pDest) + pGroup->pByteBlocks[i].iStartByteOffset, ((char*)pSrc) + pGroup->pByteBlocks[i].iStartByteOffset, pGroup->pByteBlocks[i].iSize);
	}

	for (i=0; i < pGroup->iNumMaskedBytes; i++)
	{
		((char*)pDest)[pGroup->pMaskedBytes[i].iByteOffset] &= ~pGroup->pMaskedBytes[i].iMask;
		((char*)pDest)[pGroup->pMaskedBytes[i].iByteOffset] |= (((char*)pSrc)[pGroup->pMaskedBytes[i].iByteOffset]) & pGroup->pMaskedBytes[i].iMask;
	}

	PERFINFO_AUTO_STOP();
}


/*
AUTO_RUN;
void ByteCopyGroupTest(void)
{
	RawByteCopyGroup rawGroup = {8, 1};
	ByteBlock testBlock1 = {0, 1};
	ByteBlock testBlock2 = {1, 2};
	SingleBit testBit1 = {5, 0};
	SingleBit testBit2 = {5, 1};
	SingleBit testBit3 = {5, 2};
	SingleBit testBit4 = {5, 3};
	SingleBit testBit5 = {5, 4};
	SingleBit testBit6 = {5, 5};
	SingleBit testBit7 = {5, 6};
	SingleBit testBit8 = {5, 7};
	ByteCopyGroup outGroup = {0};


	eaPush(&rawGroup.offBlocks.ppBlocks, StructClone(parse_ByteBlock, &testBlock1));
	eaPush(&rawGroup.offBlocks.ppBlocks, StructClone(parse_ByteBlock, &testBlock2));

	eaPush(&rawGroup.ppOffBits, StructClone(parse_SingleBit, &testBit1));
	eaPush(&rawGroup.ppOffBits, StructClone(parse_SingleBit, &testBit2));
	eaPush(&rawGroup.ppOffBits, StructClone(parse_SingleBit, &testBit3));
	eaPush(&rawGroup.ppOffBits, StructClone(parse_SingleBit, &testBit4));
	eaPush(&rawGroup.ppOffBits, StructClone(parse_SingleBit, &testBit5));
	eaPush(&rawGroup.ppOffBits, StructClone(parse_SingleBit, &testBit6));
	eaPush(&rawGroup.ppOffBits, StructClone(parse_SingleBit, &testBit7));
	eaPush(&rawGroup.ppOffBits, StructClone(parse_SingleBit, &testBit8));


	CompileByteCopyGroup(&outGroup, &rawGroup);

}*/



/*

AUTO_RUN;
void byteBlockTest(void)
{

	int iNumBlocks;
	int iTestNum;
	int i, j;

	for (iNumBlocks = 2; iNumBlocks < 50; iNumBlocks++)
	{
		for (iTestNum = 0; iTestNum < 100; iTestNum++)
		{
			ByteBlockList startingList = {0};
			ByteBlockList groupedList = {0};
			ByteBlockList invertedList = {0};
			U8 startingArray[1024] = {0};
			U8 groupedArray[1024] = {0};
			U8 invertedArray[1024] = {0};

			for (i=0; i < iNumBlocks; i++)
			{
				ByteBlock *pBlock = StructCreate(parse_ByteBlock);
				pBlock->iStartByteOffset = randInt(1022);
				pBlock->iSize = 1 + randInt(1024 - pBlock->iStartByteOffset - 1);

				eaPush(&startingList.ppBlocks, pBlock);
			}

			StructCopyAll(parse_ByteBlockList, &startingList, &groupedList);
	
			GroupByteBlocks(&groupedList);

			InvertByteBlockList(&invertedList, &groupedList, 1024);

			


			for (i=0; i < eaSize(&startingList.ppBlocks); i++)
			{
				for (j = startingList.ppBlocks[i]->iStartByteOffset; j < startingList.ppBlocks[i]->iStartByteOffset + startingList.ppBlocks[i]->iSize; j++)
				{
					startingArray[j] = 1;
				}
			}


			for (i=0; i < eaSize(&invertedList.ppBlocks); i++)
			{
				if (i > 0)
				{
					assert(invertedList.ppBlocks[i]->iStartByteOffset > 0 && invertedList.ppBlocks[i]->iStartByteOffset < 1024 && invertedArray[invertedList.ppBlocks[i]->iStartByteOffset-1] == 0);
				}		
				
				for (j = invertedList.ppBlocks[i]->iStartByteOffset; j < invertedList.ppBlocks[i]->iStartByteOffset + invertedList.ppBlocks[i]->iSize; j++)
				{
					assert(j >= 0 && j < 1024);
					assert(invertedArray[j] == 0);
					invertedArray[j] = 1;
				}
			}

			for (i=0; i < eaSize(&groupedList.ppBlocks); i++)
			{
				if (i > 0)
				{
					assert(groupedList.ppBlocks[i]->iStartByteOffset > 0 && groupedList.ppBlocks[i]->iStartByteOffset < 1024 && groupedArray[groupedList.ppBlocks[i]->iStartByteOffset-1] == 0);
				}		
				
				for (j = groupedList.ppBlocks[i]->iStartByteOffset; j < groupedList.ppBlocks[i]->iStartByteOffset + groupedList.ppBlocks[i]->iSize; j++)
				{
					assert(j >= 0 && j < 1024);
					assert(groupedArray[j] == 0);
					groupedArray[j] = 1;
				}
			}

			for (i=0; i < 1024; i++)
			{
				assert(startingArray[i] == groupedArray[i]);
				assert(startingArray[i] + invertedArray[i] == 1);
			}


			StructDeInit(parse_ByteBlockList, &startingList);
			StructDeInit(parse_ByteBlockList, &groupedList);
		}
	}
}

*/

#include "ByteBlock_h_ast.c"
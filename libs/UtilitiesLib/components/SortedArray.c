#include "SortedArray.h"

SortedArray * sortedArrayCreateInternal(int iSize MEM_DBG_PARMS)
{
	SortedArray * pArray;
	pArray = scalloc(1,sizeof(SortedArray));
	pArray->iSize = iSize;
	pArray->iNumVals = 0;
	pArray->paVals = scalloc(sizeof(SortedArrayKeyValPair),iSize);

	return pArray;
}

void sortedArrayDestroy(SortedArray * pArray)
{
	free(pArray->paVals);
	free(pArray);
}

void sortedArrayClear(SortedArray * pArray)
{
	pArray->iNumVals = 0;
}
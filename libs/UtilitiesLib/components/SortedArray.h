#ifndef SORTEDARRAY_H
#define SORTEDARRAY_H

typedef struct SortedArrayKeyValPair
{
	void * pKey;
	void * pVal;
} SortedArrayKeyValPair;

typedef struct SortedArray
{
	int iSize;
	int iNumVals;
	SortedArrayKeyValPair * paVals;
} SortedArray;

SortedArray * sortedArrayCreateInternal(int iSize MEM_DBG_PARMS);

#define sortedArrayCreate(iSize) sortedArrayCreateInternal(iSize MEM_DBG_PARMS_INIT)

void sortedArrayDestroy(SortedArray * pArray);
void sortedArrayClear(SortedArray * pArray);

__forceinline void sortedArrayAddValueAtKnownPosition(SortedArray * pArray,void * pKey,void *pVal,int iInsertPos);
__forceinline void sortedArrayAddValue(SortedArray * pArray,void * pKey,void *pVal);
__forceinline bool sortedArrayFindValue(SortedArray * pArray,void * pKey,void ** ppVal,int * piInsertPos);
__forceinline void * sortedArrayGetKey(SortedArray * pArray,int iIndex);
__forceinline void * sortedArrayGetVal(SortedArray * pArray,int iIndex);

__forceinline bool sortedArrayIsFull(SortedArray * pArray)
{
	return pArray->iNumVals == pArray->iSize;
}

__forceinline void sortedArrayAddValueAtKnownPosition(SortedArray * pArray,void * pKey,void *pVal,int iInsertPos)
{
	if (pArray->iNumVals == pArray->iSize)
	{
		devassert(0 && "Out of array space, tell a programmer");
		return;
	}

	memmove(&pArray->paVals[iInsertPos+1],&pArray->paVals[iInsertPos],sizeof(SortedArrayKeyValPair)*(pArray->iNumVals-iInsertPos));

	pArray->paVals[iInsertPos].pKey = pKey;
	pArray->paVals[iInsertPos].pVal = pVal;

	pArray->iNumVals++;
}

__forceinline bool sortedArrayFindValue(SortedArray * pArray,void * pKey,void ** ppVal,int * piInsertPos)
{
	// binary search
	int lo = 0;
	int hi = pArray->iNumVals-1;
	int iInsertPos=0;

	while (lo <= hi)
	{
		int mid = (lo + hi)/2;
		iInsertPos = lo;
		if (pArray->paVals[mid].pKey == pKey)
		{
			*ppVal = pArray->paVals[mid].pVal;
			return true;
		}
		else if (pKey < pArray->paVals[mid].pKey)
		{
			hi = mid-1;
		}
		else
		{
			lo = mid+1;
		}
	}

	if (piInsertPos)
	{
		*piInsertPos = iInsertPos;
	}

	return false;
}

__forceinline void sortedArrayAddValue(SortedArray * pArray,void * pKey,void *pVal)
{
	void * pFoundVal;
	int iInsertPos;
	bool bFound = sortedArrayFindValue(pArray,pKey,&pFoundVal,&iInsertPos);

	assert(!bFound);
	sortedArrayAddValueAtKnownPosition(pArray,pKey,pVal,iInsertPos);
}

__forceinline void * sortedArrayGetKey(SortedArray * pArray,int iIndex)
{
	return pArray->paVals[iIndex].pKey;
}

__forceinline void * sortedArrayGetVal(SortedArray * pArray,int iIndex)
{
	return pArray->paVals[iIndex].pVal;
}

#endif